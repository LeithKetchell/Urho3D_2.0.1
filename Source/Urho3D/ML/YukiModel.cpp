// YukiModel — Cartridge format loader.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiModel.h"
#include "../IO/File.h"
#include "../IO/Log.h"
#include "../Core/Context.h"

#include <cstring>
#include <cmath>
#include <cassert>

#include "../ML/PagedFloatBuffer.h"
#include "../ML/YukiTrainAdapter.h"   // YukiModelPtrs/Grads view structs for the paged builders

#include "../DebugNew.h"

namespace Urho3D
{

// ─── Topology ────────────────────────────────────────────────────────────────

unsigned long long YukiTopology::TotalWeights() const
{
    // Embedding: vocabSize × embedDim
    unsigned long long total = (unsigned long long)vocabSize * embedDim;

    // Per layer:
    //   Q,K,V,Out projections: 4 × (embedDim × embedDim)
    //   FF1: embedDim × ffDim
    //   FF2: ffDim × embedDim
    //   LayerNorm: embedDim (weights) + embedDim (bias)
    unsigned long long perLayer =
        4ULL * embedDim * embedDim +    // Q, K, V, Out
        (unsigned long long)embedDim * ffDim +  // FF1
        (unsigned long long)ffDim * embedDim +  // FF2
        2ULL * embedDim;                // norm weights + bias

    total += perLayer * nLayers;

    // Final layer norm: embedDim + embedDim
    total += 2ULL * embedDim;

    // Output projection: embedDim × vocabSize
    total += (unsigned long long)embedDim * vocabSize;

    return total;
}

// ─── YukiModel ───────────────────────────────────────────────────────────────

YukiModel::YukiModel(Context* context) :
    Object(context)
{
    memset(&header_, 0, sizeof(header_));
}

YukiModel::~YukiModel()
{
    Unload();
}

bool YukiModel::Load(const String& path)
{
    Unload();

    File file(context_, path, FILE_READ);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("YukiModel: Cannot open " + path);
        return false;
    }

    const long long fileSize = file.GetSize();   // i64 — cartridges may exceed 4 GB
    if (fileSize < (long long)sizeof(YukiCartridgeHeader))
    {
        URHO3D_LOGERROR("YukiModel: File too small");
        return false;
    }

    // Header only (fixed, tiny) — the weight region is streamed straight into pages below,
    // never slurped into one contiguous buffer (that's the old 2 GB ceiling).
    if (file.Read(&header_, sizeof(YukiCartridgeHeader)) != sizeof(YukiCartridgeHeader))
    {
        URHO3D_LOGERROR("YukiModel: Header read short");
        return false;
    }
    if (header_.magic != YUKI_MAGIC)
    {
        URHO3D_LOGERROR("YukiModel: Invalid magic (not a YUKI cartridge)");
        return false;
    }
    if (header_.version > YUKI_CART_VERSION)
    {
        URHO3D_LOGWARNING("YukiModel: Cartridge version " + String(header_.version) +
                          " is newer than supported " + String(YUKI_CART_VERSION));
    }
    if ((long long)header_.fileSize != fileSize)
    {
        URHO3D_LOGWARNING("YukiModel: File size mismatch (header says " +
                          String(header_.fileSize) + ", actual " + String((unsigned long long)fileSize) + ")");
    }

    const YukiTopology& t = header_.topology;

    // Stream the weight region into the paged store, page by page (each page <= a few hundred
    // MB, well under a single 32-bit Read). No 2 GB contiguous allocation.
    if (!weights_.Configure(t.embedDim, t.nLayers, t.ffDim, t.vocabSize))
    {
        URHO3D_LOGERROR("YukiModel: a single weight tensor exceeds the 2^31 page ceiling — cannot load");
        return false;
    }
    file.Seek((long long)header_.weightsOffset);
    for (unsigned p = 0; p < weights_.PageCount(); ++p)
    {
        const unsigned bytes = weights_.PageElems(p) * (unsigned)sizeof(float);
        if (file.Read(weights_.PageData(p), bytes) != bytes)
        {
            URHO3D_LOGERROR("YukiModel: weight data read short");
            Unload();
            return false;
        }
    }

    // Vocabulary region → its own small flat blob.
    if (header_.vocabOffset > 0 && (long long)header_.vocabOffset < fileSize)
    {
        const long long vocabBytes = fileSize - (long long)header_.vocabOffset;
        vocabBlob_.Resize((unsigned)vocabBytes);   // vocab is small (vocabSize × 32) — fits i32
        file.Seek((long long)header_.vocabOffset);
        file.Read(vocabBlob_.Buffer(), (unsigned)vocabBytes);
    }
    file.Close();

    // Resolve weight pointers
    if (!ResolveWeights())
    {
        URHO3D_LOGERROR("YukiModel: Failed to resolve weights");
        Unload();
        return false;
    }

    path_ = path;
    loaded_ = true;

    URHO3D_LOGINFOF("YukiModel: Loaded '%s' — %u layers, %u dim, %u heads, %u vocab, %llu weights (%.1f MB)",
        path.CString(), t.nLayers, t.embedDim, t.nHeads, t.vocabSize,
        t.TotalWeights(), (float)t.TotalWeightBytes() / (1024.0f * 1024.0f));

    return true;
}

void YukiModel::Unload()
{
    weights_.Clear();
    vocabBlob_.Clear();
    layerWeights_.Clear();
    embeddingWeights_ = nullptr;
    outputWeights_ = nullptr;
    finalNormWeights_ = nullptr;
    finalNormBias_ = nullptr;
    vocabData_ = nullptr;
    loaded_ = false;
}

bool YukiModel::ResolveWeights()
{
    const YukiTopology& t = header_.topology;
    if (weights_.Size() < t.TotalWeights())
    {
        URHO3D_LOGERROR("YukiModel: Not enough weight data (need " + String(t.TotalWeights()) +
                        ", have " + String(weights_.Size()) + ")");
        return false;
    }

    // Resolve each tensor as a contiguous span into the paged store. Pages are tensor-aligned,
    // so every span is a single contiguous float* — inference reads these unchanged.
    unsigned long long o = 0;
    const unsigned long long dd = (unsigned long long)t.embedDim * t.embedDim;

    embeddingWeights_ = weights_.Span(o, (unsigned long long)t.vocabSize * t.embedDim);
    o += (unsigned long long)t.vocabSize * t.embedDim;

    layerWeights_.Resize(t.nLayers);
    for (unsigned i = 0; i < t.nLayers; ++i)
    {
        YukiLayerWeights& lw = layerWeights_[i];
        lw.qWeights = weights_.Span(o, dd); o += dd;
        lw.kWeights = weights_.Span(o, dd); o += dd;
        lw.vWeights = weights_.Span(o, dd); o += dd;
        lw.outWeights = weights_.Span(o, dd); o += dd;
        lw.ff1Weights = weights_.Span(o, (unsigned long long)t.embedDim * t.ffDim); o += (unsigned long long)t.embedDim * t.ffDim;
        lw.ff2Weights = weights_.Span(o, (unsigned long long)t.ffDim * t.embedDim); o += (unsigned long long)t.ffDim * t.embedDim;
        lw.normWeights = weights_.Span(o, t.embedDim); o += t.embedDim;
        lw.normBias = weights_.Span(o, t.embedDim); o += t.embedDim;
    }

    finalNormWeights_ = weights_.Span(o, t.embedDim); o += t.embedDim;
    finalNormBias_ = weights_.Span(o, t.embedDim); o += t.embedDim;
    outputWeights_ = weights_.Span(o, (unsigned long long)t.embedDim * t.vocabSize);

    // Vocabulary — now in its own blob (offset 0 within the blob is the first entry).
    if (!vocabBlob_.Empty())
    {
        vocabData_ = reinterpret_cast<const char*>(vocabBlob_.Buffer());
        vocabEntrySize_ = 32;  // Fixed-width token entries
        BuildTokenIndex();     // hash the vocab once so GetTokenIndex is O(1), not an O(vocab) scan
    }

    return true;
}

void YukiModel::BuildTokenIndex()
{
    // One-time hash of the fixed-width vocab blob. GetTokenIndex was a linear strncmp over every
    // vocab entry (O(vocabSize)); called once per token by BuildTokenSet/SelectTrainingBatch/inference,
    // that is O(corpusTokens × vocabSize) — fine for a small corpus, but once /ingest grew the memory
    // table the main-thread BuildTokenSet at startup ballooned into billions of strncmp and the UI froze
    // before its first frame. Hashing collapses each lookup to O(1). First occurrence wins, exactly as
    // the old forward scan returned the lowest matching index.
    tokenIndex_.Clear();
    if (!vocabData_ || !vocabEntrySize_)
        return;
    const unsigned V = header_.topology.vocabSize;
    for (unsigned i = 0; i < V; ++i)
    {
        const char* entry = vocabData_ + (unsigned long long)i * vocabEntrySize_;
        // Entries are fixed-width and may fill all 32 bytes without a terminator — bound by strlen AND
        // the entry width, matching GetToken's decode so keys round-trip identically.
        String key(entry, Min((unsigned)strlen(entry), vocabEntrySize_));
        if (!tokenIndex_.Contains(key))   // keep the first index for a duplicate token (old scan semantics)
            tokenIndex_[key] = i;
    }
}

String YukiModel::GetToken(unsigned index) const
{
    if (!vocabData_ || index >= header_.topology.vocabSize)
        return String::EMPTY;

    const char* entry = vocabData_ + (unsigned long long)index * vocabEntrySize_;
    return String(entry, Min((unsigned)strlen(entry), vocabEntrySize_));
}

unsigned YukiModel::GetTokenIndex(const String& token) const
{
    if (!vocabData_)
        return header_.topology.vocabSize;

    // O(1) path: the vocab was hashed at load. A token shorter than the entry width round-trips
    // exactly to a key (the old strncmp stopped at the token's '\0', i.e. an exact C-string match).
    if (!tokenIndex_.Empty() && token.Length() < vocabEntrySize_)
    {
        HashMap<String, unsigned>::ConstIterator it = tokenIndex_.Find(token);
        return it != tokenIndex_.End() ? it->second_ : header_.topology.vocabSize;
    }

    // Rare fallback: a token as long as / longer than the fixed entry width. The old scan compared
    // only the first vocabEntrySize_ bytes, so a truncated match is possible and must be preserved —
    // this also covers a model whose vocab hash wasn't built.
    for (unsigned i = 0; i < header_.topology.vocabSize; ++i)
    {
        const char* entry = vocabData_ + (unsigned long long)i * vocabEntrySize_;
        if (strncmp(entry, token.CString(), vocabEntrySize_) == 0)
            return i;
    }

    return header_.topology.vocabSize;
}

namespace
{
// Maximal-munch operator table, longest match first. Covers common C-family /
// scripting-language operators; anything not listed here falls through to the
// single-symbol case, so new operators degrade gracefully rather than erroring.
const char* const kOps3[] = { "<<=", ">>=", "...", "->*" };
const char* const kOps2[] = {
    "::", "->", "++", "--", "==", "!=", "<=", ">=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>",
    "//", "/*", "*/", ".*"
};
const String kNumSuffixChars("fFlLuU");

inline bool MatchOpAt(const String& s, unsigned i, const char* op, unsigned len)
{
    if (i + len > s.Length())
        return false;
    for (unsigned k = 0; k < len; ++k)
        if (s[i + k] != op[k])
            return false;
    return true;
}

/// Whether `cp` is a non-ASCII codepoint that CONTINUES an identifier/word
/// run (letter, combining mark, or digit) — i.e. Unicode General Category
/// Lu/Ll/Lt/Lm/Lo (letters), Mn/Mc/Me (combining marks — attach to the
/// preceding base letter, e.g. accented Latin, Devanagari vowel signs), or
/// Nd (decimal digit — non-ASCII digit systems behave like ASCII 0-9 does
/// inside an identifier). Anything NOT in this table — punctuation, symbols,
/// separators, control/format characters, or an unassigned codepoint — ends
/// the run and becomes its own atomic token, same treatment ASCII
/// punctuation already gets.
///
/// MECHANICALLY GENERATED, not hand-curated: computed directly from the
/// authoritative Unicode Character Database (UnicodeData.txt, unicode.org)
/// by extracting every codepoint >= U+0080 whose General Category is one of
/// the above, then merging adjacent codepoints/ranges of that category into
/// contiguous spans. 787 ranges cover all ~29,600 individual codepoint
/// entries in those categories. This replaces an earlier hand-typed
/// "punctuation" blacklist that was ESTIMATED from memory and contained
/// real errors (ª U+00AA, º U+00BA, µ U+00B5 were misclassified as
/// punctuation; they are letters) — this table is the fix: a WHITELIST
/// derived from ground truth rather than a blacklist guessed from memory,
/// and it fails safe in the opposite direction (an unrecognised/unassigned
/// codepoint becomes its own token rather than silently joining a word run).
/// Binary search: sorted ascending, non-overlapping, checked at every
/// non-ASCII codepoint in the hot tokenizer path.
///
/// Regenerate if this ever needs updating: download UnicodeData.txt from
/// unicode.org/Public/UCD/latest/ucd/, parse field 0 (codepoint) and field 2
/// (category), handle "<..., First>"/"<..., Last>" paired rows as ranges,
/// filter to codepoint >= 0x80 and category in {Lu,Ll,Lt,Lm,Lo,Mn,Mc,Me,Nd},
/// sort, and merge adjacent/overlapping ranges.
bool IsNonAsciiWordChar(c32 cp)
{
    struct Range { c32 lo, hi; };
    static const Range kWordRanges[] = {
    {0x00AA,0x00AA}, {0x00B5,0x00B5}, {0x00BA,0x00BA}, {0x00C0,0x00D6},
    {0x00D8,0x00F6}, {0x00F8,0x02C1}, {0x02C6,0x02D1}, {0x02E0,0x02E4},
    {0x02EC,0x02EC}, {0x02EE,0x02EE}, {0x0300,0x0374}, {0x0376,0x0377},
    {0x037A,0x037D}, {0x037F,0x037F}, {0x0386,0x0386}, {0x0388,0x038A},
    {0x038C,0x038C}, {0x038E,0x03A1}, {0x03A3,0x03F5}, {0x03F7,0x0481},
    {0x0483,0x052F}, {0x0531,0x0556}, {0x0559,0x0559}, {0x0560,0x0588},
    {0x0591,0x05BD}, {0x05BF,0x05BF}, {0x05C1,0x05C2}, {0x05C4,0x05C5},
    {0x05C7,0x05C7}, {0x05D0,0x05EA}, {0x05EF,0x05F2}, {0x0610,0x061A},
    {0x0620,0x0669}, {0x066E,0x06D3}, {0x06D5,0x06DC}, {0x06DF,0x06E8},
    {0x06EA,0x06FC}, {0x06FF,0x06FF}, {0x0710,0x074A}, {0x074D,0x07B1},
    {0x07C0,0x07F5}, {0x07FA,0x07FA}, {0x07FD,0x07FD}, {0x0800,0x082D},
    {0x0840,0x085B}, {0x0860,0x086A}, {0x0870,0x0887}, {0x0889,0x088F},
    {0x0897,0x08E1}, {0x08E3,0x0963}, {0x0966,0x096F}, {0x0971,0x0983},
    {0x0985,0x098C}, {0x098F,0x0990}, {0x0993,0x09A8}, {0x09AA,0x09B0},
    {0x09B2,0x09B2}, {0x09B6,0x09B9}, {0x09BC,0x09C4}, {0x09C7,0x09C8},
    {0x09CB,0x09CE}, {0x09D7,0x09D7}, {0x09DC,0x09DD}, {0x09DF,0x09E3},
    {0x09E6,0x09F1}, {0x09FC,0x09FC}, {0x09FE,0x09FE}, {0x0A01,0x0A03},
    {0x0A05,0x0A0A}, {0x0A0F,0x0A10}, {0x0A13,0x0A28}, {0x0A2A,0x0A30},
    {0x0A32,0x0A33}, {0x0A35,0x0A36}, {0x0A38,0x0A39}, {0x0A3C,0x0A3C},
    {0x0A3E,0x0A42}, {0x0A47,0x0A48}, {0x0A4B,0x0A4D}, {0x0A51,0x0A51},
    {0x0A59,0x0A5C}, {0x0A5E,0x0A5E}, {0x0A66,0x0A75}, {0x0A81,0x0A83},
    {0x0A85,0x0A8D}, {0x0A8F,0x0A91}, {0x0A93,0x0AA8}, {0x0AAA,0x0AB0},
    {0x0AB2,0x0AB3}, {0x0AB5,0x0AB9}, {0x0ABC,0x0AC5}, {0x0AC7,0x0AC9},
    {0x0ACB,0x0ACD}, {0x0AD0,0x0AD0}, {0x0AE0,0x0AE3}, {0x0AE6,0x0AEF},
    {0x0AF9,0x0AFF}, {0x0B01,0x0B03}, {0x0B05,0x0B0C}, {0x0B0F,0x0B10},
    {0x0B13,0x0B28}, {0x0B2A,0x0B30}, {0x0B32,0x0B33}, {0x0B35,0x0B39},
    {0x0B3C,0x0B44}, {0x0B47,0x0B48}, {0x0B4B,0x0B4D}, {0x0B55,0x0B57},
    {0x0B5C,0x0B5D}, {0x0B5F,0x0B63}, {0x0B66,0x0B6F}, {0x0B71,0x0B71},
    {0x0B82,0x0B83}, {0x0B85,0x0B8A}, {0x0B8E,0x0B90}, {0x0B92,0x0B95},
    {0x0B99,0x0B9A}, {0x0B9C,0x0B9C}, {0x0B9E,0x0B9F}, {0x0BA3,0x0BA4},
    {0x0BA8,0x0BAA}, {0x0BAE,0x0BB9}, {0x0BBE,0x0BC2}, {0x0BC6,0x0BC8},
    {0x0BCA,0x0BCD}, {0x0BD0,0x0BD0}, {0x0BD7,0x0BD7}, {0x0BE6,0x0BEF},
    {0x0C00,0x0C0C}, {0x0C0E,0x0C10}, {0x0C12,0x0C28}, {0x0C2A,0x0C39},
    {0x0C3C,0x0C44}, {0x0C46,0x0C48}, {0x0C4A,0x0C4D}, {0x0C55,0x0C56},
    {0x0C58,0x0C5A}, {0x0C5C,0x0C5D}, {0x0C60,0x0C63}, {0x0C66,0x0C6F},
    {0x0C80,0x0C83}, {0x0C85,0x0C8C}, {0x0C8E,0x0C90}, {0x0C92,0x0CA8},
    {0x0CAA,0x0CB3}, {0x0CB5,0x0CB9}, {0x0CBC,0x0CC4}, {0x0CC6,0x0CC8},
    {0x0CCA,0x0CCD}, {0x0CD5,0x0CD6}, {0x0CDC,0x0CDE}, {0x0CE0,0x0CE3},
    {0x0CE6,0x0CEF}, {0x0CF1,0x0CF3}, {0x0D00,0x0D0C}, {0x0D0E,0x0D10},
    {0x0D12,0x0D44}, {0x0D46,0x0D48}, {0x0D4A,0x0D4E}, {0x0D54,0x0D57},
    {0x0D5F,0x0D63}, {0x0D66,0x0D6F}, {0x0D7A,0x0D7F}, {0x0D81,0x0D83},
    {0x0D85,0x0D96}, {0x0D9A,0x0DB1}, {0x0DB3,0x0DBB}, {0x0DBD,0x0DBD},
    {0x0DC0,0x0DC6}, {0x0DCA,0x0DCA}, {0x0DCF,0x0DD4}, {0x0DD6,0x0DD6},
    {0x0DD8,0x0DDF}, {0x0DE6,0x0DEF}, {0x0DF2,0x0DF3}, {0x0E01,0x0E3A},
    {0x0E40,0x0E4E}, {0x0E50,0x0E59}, {0x0E81,0x0E82}, {0x0E84,0x0E84},
    {0x0E86,0x0E8A}, {0x0E8C,0x0EA3}, {0x0EA5,0x0EA5}, {0x0EA7,0x0EBD},
    {0x0EC0,0x0EC4}, {0x0EC6,0x0EC6}, {0x0EC8,0x0ECE}, {0x0ED0,0x0ED9},
    {0x0EDC,0x0EDF}, {0x0F00,0x0F00}, {0x0F18,0x0F19}, {0x0F20,0x0F29},
    {0x0F35,0x0F35}, {0x0F37,0x0F37}, {0x0F39,0x0F39}, {0x0F3E,0x0F47},
    {0x0F49,0x0F6C}, {0x0F71,0x0F84}, {0x0F86,0x0F97}, {0x0F99,0x0FBC},
    {0x0FC6,0x0FC6}, {0x1000,0x1049}, {0x1050,0x109D}, {0x10A0,0x10C5},
    {0x10C7,0x10C7}, {0x10CD,0x10CD}, {0x10D0,0x10FA}, {0x10FC,0x1248},
    {0x124A,0x124D}, {0x1250,0x1256}, {0x1258,0x1258}, {0x125A,0x125D},
    {0x1260,0x1288}, {0x128A,0x128D}, {0x1290,0x12B0}, {0x12B2,0x12B5},
    {0x12B8,0x12BE}, {0x12C0,0x12C0}, {0x12C2,0x12C5}, {0x12C8,0x12D6},
    {0x12D8,0x1310}, {0x1312,0x1315}, {0x1318,0x135A}, {0x135D,0x135F},
    {0x1380,0x138F}, {0x13A0,0x13F5}, {0x13F8,0x13FD}, {0x1401,0x166C},
    {0x166F,0x167F}, {0x1681,0x169A}, {0x16A0,0x16EA}, {0x16F1,0x16F8},
    {0x1700,0x1715}, {0x171F,0x1734}, {0x1740,0x1753}, {0x1760,0x176C},
    {0x176E,0x1770}, {0x1772,0x1773}, {0x1780,0x17D3}, {0x17D7,0x17D7},
    {0x17DC,0x17DD}, {0x17E0,0x17E9}, {0x180B,0x180D}, {0x180F,0x1819},
    {0x1820,0x1878}, {0x1880,0x18AA}, {0x18B0,0x18F5}, {0x1900,0x191E},
    {0x1920,0x192B}, {0x1930,0x193B}, {0x1946,0x196D}, {0x1970,0x1974},
    {0x1980,0x19AB}, {0x19B0,0x19C9}, {0x19D0,0x19D9}, {0x1A00,0x1A1B},
    {0x1A20,0x1A5E}, {0x1A60,0x1A7C}, {0x1A7F,0x1A89}, {0x1A90,0x1A99},
    {0x1AA7,0x1AA7}, {0x1AB0,0x1ADD}, {0x1AE0,0x1AEB}, {0x1B00,0x1B4C},
    {0x1B50,0x1B59}, {0x1B6B,0x1B73}, {0x1B80,0x1BF3}, {0x1C00,0x1C37},
    {0x1C40,0x1C49}, {0x1C4D,0x1C7D}, {0x1C80,0x1C8A}, {0x1C90,0x1CBA},
    {0x1CBD,0x1CBF}, {0x1CD0,0x1CD2}, {0x1CD4,0x1CFA}, {0x1D00,0x1F15},
    {0x1F18,0x1F1D}, {0x1F20,0x1F45}, {0x1F48,0x1F4D}, {0x1F50,0x1F57},
    {0x1F59,0x1F59}, {0x1F5B,0x1F5B}, {0x1F5D,0x1F5D}, {0x1F5F,0x1F7D},
    {0x1F80,0x1FB4}, {0x1FB6,0x1FBC}, {0x1FBE,0x1FBE}, {0x1FC2,0x1FC4},
    {0x1FC6,0x1FCC}, {0x1FD0,0x1FD3}, {0x1FD6,0x1FDB}, {0x1FE0,0x1FEC},
    {0x1FF2,0x1FF4}, {0x1FF6,0x1FFC}, {0x2071,0x2071}, {0x207F,0x207F},
    {0x2090,0x209C}, {0x20D0,0x20F0}, {0x2102,0x2102}, {0x2107,0x2107},
    {0x210A,0x2113}, {0x2115,0x2115}, {0x2119,0x211D}, {0x2124,0x2124},
    {0x2126,0x2126}, {0x2128,0x2128}, {0x212A,0x212D}, {0x212F,0x2139},
    {0x213C,0x213F}, {0x2145,0x2149}, {0x214E,0x214E}, {0x2183,0x2184},
    {0x2C00,0x2CE4}, {0x2CEB,0x2CF3}, {0x2D00,0x2D25}, {0x2D27,0x2D27},
    {0x2D2D,0x2D2D}, {0x2D30,0x2D67}, {0x2D6F,0x2D6F}, {0x2D7F,0x2D96},
    {0x2DA0,0x2DA6}, {0x2DA8,0x2DAE}, {0x2DB0,0x2DB6}, {0x2DB8,0x2DBE},
    {0x2DC0,0x2DC6}, {0x2DC8,0x2DCE}, {0x2DD0,0x2DD6}, {0x2DD8,0x2DDE},
    {0x2DE0,0x2DFF}, {0x2E2F,0x2E2F}, {0x3005,0x3006}, {0x302A,0x302F},
    {0x3031,0x3035}, {0x303B,0x303C}, {0x3041,0x3096}, {0x3099,0x309A},
    {0x309D,0x309F}, {0x30A1,0x30FA}, {0x30FC,0x30FF}, {0x3105,0x312F},
    {0x3131,0x318E}, {0x31A0,0x31BF}, {0x31F0,0x31FF}, {0x3400,0x4DBF},
    {0x4E00,0xA48C}, {0xA4D0,0xA4FD}, {0xA500,0xA60C}, {0xA610,0xA62B},
    {0xA640,0xA672}, {0xA674,0xA67D}, {0xA67F,0xA6E5}, {0xA6F0,0xA6F1},
    {0xA717,0xA71F}, {0xA722,0xA788}, {0xA78B,0xA7DC}, {0xA7F1,0xA827},
    {0xA82C,0xA82C}, {0xA840,0xA873}, {0xA880,0xA8C5}, {0xA8D0,0xA8D9},
    {0xA8E0,0xA8F7}, {0xA8FB,0xA8FB}, {0xA8FD,0xA92D}, {0xA930,0xA953},
    {0xA960,0xA97C}, {0xA980,0xA9C0}, {0xA9CF,0xA9D9}, {0xA9E0,0xA9FE},
    {0xAA00,0xAA36}, {0xAA40,0xAA4D}, {0xAA50,0xAA59}, {0xAA60,0xAA76},
    {0xAA7A,0xAAC2}, {0xAADB,0xAADD}, {0xAAE0,0xAAEF}, {0xAAF2,0xAAF6},
    {0xAB01,0xAB06}, {0xAB09,0xAB0E}, {0xAB11,0xAB16}, {0xAB20,0xAB26},
    {0xAB28,0xAB2E}, {0xAB30,0xAB5A}, {0xAB5C,0xAB69}, {0xAB70,0xABEA},
    {0xABEC,0xABED}, {0xABF0,0xABF9}, {0xAC00,0xD7A3}, {0xD7B0,0xD7C6},
    {0xD7CB,0xD7FB}, {0xF900,0xFA6D}, {0xFA70,0xFAD9}, {0xFB00,0xFB06},
    {0xFB13,0xFB17}, {0xFB1D,0xFB28}, {0xFB2A,0xFB36}, {0xFB38,0xFB3C},
    {0xFB3E,0xFB3E}, {0xFB40,0xFB41}, {0xFB43,0xFB44}, {0xFB46,0xFBB1},
    {0xFBD3,0xFD3D}, {0xFD50,0xFD8F}, {0xFD92,0xFDC7}, {0xFDF0,0xFDFB},
    {0xFE00,0xFE0F}, {0xFE20,0xFE2F}, {0xFE70,0xFE74}, {0xFE76,0xFEFC},
    {0xFF10,0xFF19}, {0xFF21,0xFF3A}, {0xFF41,0xFF5A}, {0xFF66,0xFFBE},
    {0xFFC2,0xFFC7}, {0xFFCA,0xFFCF}, {0xFFD2,0xFFD7}, {0xFFDA,0xFFDC},
    {0x10000,0x1000B}, {0x1000D,0x10026}, {0x10028,0x1003A}, {0x1003C,0x1003D},
    {0x1003F,0x1004D}, {0x10050,0x1005D}, {0x10080,0x100FA}, {0x101FD,0x101FD},
    {0x10280,0x1029C}, {0x102A0,0x102D0}, {0x102E0,0x102E0}, {0x10300,0x1031F},
    {0x1032D,0x10340}, {0x10342,0x10349}, {0x10350,0x1037A}, {0x10380,0x1039D},
    {0x103A0,0x103C3}, {0x103C8,0x103CF}, {0x10400,0x1049D}, {0x104A0,0x104A9},
    {0x104B0,0x104D3}, {0x104D8,0x104FB}, {0x10500,0x10527}, {0x10530,0x10563},
    {0x10570,0x1057A}, {0x1057C,0x1058A}, {0x1058C,0x10592}, {0x10594,0x10595},
    {0x10597,0x105A1}, {0x105A3,0x105B1}, {0x105B3,0x105B9}, {0x105BB,0x105BC},
    {0x105C0,0x105F3}, {0x10600,0x10736}, {0x10740,0x10755}, {0x10760,0x10767},
    {0x10780,0x10785}, {0x10787,0x107B0}, {0x107B2,0x107BA}, {0x10800,0x10805},
    {0x10808,0x10808}, {0x1080A,0x10835}, {0x10837,0x10838}, {0x1083C,0x1083C},
    {0x1083F,0x10855}, {0x10860,0x10876}, {0x10880,0x1089E}, {0x108E0,0x108F2},
    {0x108F4,0x108F5}, {0x10900,0x10915}, {0x10920,0x10939}, {0x10940,0x10959},
    {0x10980,0x109B7}, {0x109BE,0x109BF}, {0x10A00,0x10A03}, {0x10A05,0x10A06},
    {0x10A0C,0x10A13}, {0x10A15,0x10A17}, {0x10A19,0x10A35}, {0x10A38,0x10A3A},
    {0x10A3F,0x10A3F}, {0x10A60,0x10A7C}, {0x10A80,0x10A9C}, {0x10AC0,0x10AC7},
    {0x10AC9,0x10AE6}, {0x10B00,0x10B35}, {0x10B40,0x10B55}, {0x10B60,0x10B72},
    {0x10B80,0x10B91}, {0x10C00,0x10C48}, {0x10C80,0x10CB2}, {0x10CC0,0x10CF2},
    {0x10D00,0x10D27}, {0x10D30,0x10D39}, {0x10D40,0x10D65}, {0x10D69,0x10D6D},
    {0x10D6F,0x10D85}, {0x10E80,0x10EA9}, {0x10EAB,0x10EAC}, {0x10EB0,0x10EB1},
    {0x10EC2,0x10EC7}, {0x10EFA,0x10F1C}, {0x10F27,0x10F27}, {0x10F30,0x10F50},
    {0x10F70,0x10F85}, {0x10FB0,0x10FC4}, {0x10FE0,0x10FF6}, {0x11000,0x11046},
    {0x11066,0x11075}, {0x1107F,0x110BA}, {0x110C2,0x110C2}, {0x110D0,0x110E8},
    {0x110F0,0x110F9}, {0x11100,0x11134}, {0x11136,0x1113F}, {0x11144,0x11147},
    {0x11150,0x11173}, {0x11176,0x11176}, {0x11180,0x111C4}, {0x111C9,0x111CC},
    {0x111CE,0x111DA}, {0x111DC,0x111DC}, {0x11200,0x11211}, {0x11213,0x11237},
    {0x1123E,0x11241}, {0x11280,0x11286}, {0x11288,0x11288}, {0x1128A,0x1128D},
    {0x1128F,0x1129D}, {0x1129F,0x112A8}, {0x112B0,0x112EA}, {0x112F0,0x112F9},
    {0x11300,0x11303}, {0x11305,0x1130C}, {0x1130F,0x11310}, {0x11313,0x11328},
    {0x1132A,0x11330}, {0x11332,0x11333}, {0x11335,0x11339}, {0x1133B,0x11344},
    {0x11347,0x11348}, {0x1134B,0x1134D}, {0x11350,0x11350}, {0x11357,0x11357},
    {0x1135D,0x11363}, {0x11366,0x1136C}, {0x11370,0x11374}, {0x11380,0x11389},
    {0x1138B,0x1138B}, {0x1138E,0x1138E}, {0x11390,0x113B5}, {0x113B7,0x113C0},
    {0x113C2,0x113C2}, {0x113C5,0x113C5}, {0x113C7,0x113CA}, {0x113CC,0x113D3},
    {0x113E1,0x113E2}, {0x11400,0x1144A}, {0x11450,0x11459}, {0x1145E,0x11461},
    {0x11480,0x114C5}, {0x114C7,0x114C7}, {0x114D0,0x114D9}, {0x11580,0x115B5},
    {0x115B8,0x115C0}, {0x115D8,0x115DD}, {0x11600,0x11640}, {0x11644,0x11644},
    {0x11650,0x11659}, {0x11680,0x116B8}, {0x116C0,0x116C9}, {0x116D0,0x116E3},
    {0x11700,0x1171A}, {0x1171D,0x1172B}, {0x11730,0x11739}, {0x11740,0x11746},
    {0x11800,0x1183A}, {0x118A0,0x118E9}, {0x118FF,0x11906}, {0x11909,0x11909},
    {0x1190C,0x11913}, {0x11915,0x11916}, {0x11918,0x11935}, {0x11937,0x11938},
    {0x1193B,0x11943}, {0x11950,0x11959}, {0x119A0,0x119A7}, {0x119AA,0x119D7},
    {0x119DA,0x119E1}, {0x119E3,0x119E4}, {0x11A00,0x11A3E}, {0x11A47,0x11A47},
    {0x11A50,0x11A99}, {0x11A9D,0x11A9D}, {0x11AB0,0x11AF8}, {0x11B60,0x11B67},
    {0x11BC0,0x11BE0}, {0x11BF0,0x11BF9}, {0x11C00,0x11C08}, {0x11C0A,0x11C36},
    {0x11C38,0x11C40}, {0x11C50,0x11C59}, {0x11C72,0x11C8F}, {0x11C92,0x11CA7},
    {0x11CA9,0x11CB6}, {0x11D00,0x11D06}, {0x11D08,0x11D09}, {0x11D0B,0x11D36},
    {0x11D3A,0x11D3A}, {0x11D3C,0x11D3D}, {0x11D3F,0x11D47}, {0x11D50,0x11D59},
    {0x11D60,0x11D65}, {0x11D67,0x11D68}, {0x11D6A,0x11D8E}, {0x11D90,0x11D91},
    {0x11D93,0x11D98}, {0x11DA0,0x11DA9}, {0x11DB0,0x11DDB}, {0x11DE0,0x11DE9},
    {0x11EE0,0x11EF6}, {0x11F00,0x11F10}, {0x11F12,0x11F3A}, {0x11F3E,0x11F42},
    {0x11F50,0x11F5A}, {0x11FB0,0x11FB0}, {0x12000,0x12399}, {0x12480,0x12543},
    {0x12F90,0x12FF0}, {0x13000,0x1342F}, {0x13440,0x13455}, {0x13460,0x143FA},
    {0x14400,0x14646}, {0x16100,0x16139}, {0x16800,0x16A38}, {0x16A40,0x16A5E},
    {0x16A60,0x16A69}, {0x16A70,0x16ABE}, {0x16AC0,0x16AC9}, {0x16AD0,0x16AED},
    {0x16AF0,0x16AF4}, {0x16B00,0x16B36}, {0x16B40,0x16B43}, {0x16B50,0x16B59},
    {0x16B63,0x16B77}, {0x16B7D,0x16B8F}, {0x16D40,0x16D6C}, {0x16D70,0x16D79},
    {0x16E40,0x16E7F}, {0x16EA0,0x16EB8}, {0x16EBB,0x16ED3}, {0x16F00,0x16F4A},
    {0x16F4F,0x16F87}, {0x16F8F,0x16F9F}, {0x16FE0,0x16FE1}, {0x16FE3,0x16FE4},
    {0x16FF0,0x16FF3}, {0x17000,0x18CD5}, {0x18CFF,0x18D1E}, {0x18D80,0x18DF2},
    {0x1AFF0,0x1AFF3}, {0x1AFF5,0x1AFFB}, {0x1AFFD,0x1AFFE}, {0x1B000,0x1B122},
    {0x1B132,0x1B132}, {0x1B150,0x1B152}, {0x1B155,0x1B155}, {0x1B164,0x1B167},
    {0x1B170,0x1B2FB}, {0x1BC00,0x1BC6A}, {0x1BC70,0x1BC7C}, {0x1BC80,0x1BC88},
    {0x1BC90,0x1BC99}, {0x1BC9D,0x1BC9E}, {0x1CCF0,0x1CCF9}, {0x1CF00,0x1CF2D},
    {0x1CF30,0x1CF46}, {0x1D165,0x1D169}, {0x1D16D,0x1D172}, {0x1D17B,0x1D182},
    {0x1D185,0x1D18B}, {0x1D1AA,0x1D1AD}, {0x1D242,0x1D244}, {0x1D400,0x1D454},
    {0x1D456,0x1D49C}, {0x1D49E,0x1D49F}, {0x1D4A2,0x1D4A2}, {0x1D4A5,0x1D4A6},
    {0x1D4A9,0x1D4AC}, {0x1D4AE,0x1D4B9}, {0x1D4BB,0x1D4BB}, {0x1D4BD,0x1D4C3},
    {0x1D4C5,0x1D505}, {0x1D507,0x1D50A}, {0x1D50D,0x1D514}, {0x1D516,0x1D51C},
    {0x1D51E,0x1D539}, {0x1D53B,0x1D53E}, {0x1D540,0x1D544}, {0x1D546,0x1D546},
    {0x1D54A,0x1D550}, {0x1D552,0x1D6A5}, {0x1D6A8,0x1D6C0}, {0x1D6C2,0x1D6DA},
    {0x1D6DC,0x1D6FA}, {0x1D6FC,0x1D714}, {0x1D716,0x1D734}, {0x1D736,0x1D74E},
    {0x1D750,0x1D76E}, {0x1D770,0x1D788}, {0x1D78A,0x1D7A8}, {0x1D7AA,0x1D7C2},
    {0x1D7C4,0x1D7CB}, {0x1D7CE,0x1D7FF}, {0x1DA00,0x1DA36}, {0x1DA3B,0x1DA6C},
    {0x1DA75,0x1DA75}, {0x1DA84,0x1DA84}, {0x1DA9B,0x1DA9F}, {0x1DAA1,0x1DAAF},
    {0x1DF00,0x1DF1E}, {0x1DF25,0x1DF2A}, {0x1E000,0x1E006}, {0x1E008,0x1E018},
    {0x1E01B,0x1E021}, {0x1E023,0x1E024}, {0x1E026,0x1E02A}, {0x1E030,0x1E06D},
    {0x1E08F,0x1E08F}, {0x1E100,0x1E12C}, {0x1E130,0x1E13D}, {0x1E140,0x1E149},
    {0x1E14E,0x1E14E}, {0x1E290,0x1E2AE}, {0x1E2C0,0x1E2F9}, {0x1E4D0,0x1E4F9},
    {0x1E5D0,0x1E5FA}, {0x1E6C0,0x1E6DE}, {0x1E6E0,0x1E6F5}, {0x1E6FE,0x1E6FF},
    {0x1E7E0,0x1E7E6}, {0x1E7E8,0x1E7EB}, {0x1E7ED,0x1E7EE}, {0x1E7F0,0x1E7FE},
    {0x1E800,0x1E8C4}, {0x1E8D0,0x1E8D6}, {0x1E900,0x1E94B}, {0x1E950,0x1E959},
    {0x1EE00,0x1EE03}, {0x1EE05,0x1EE1F}, {0x1EE21,0x1EE22}, {0x1EE24,0x1EE24},
    {0x1EE27,0x1EE27}, {0x1EE29,0x1EE32}, {0x1EE34,0x1EE37}, {0x1EE39,0x1EE39},
    {0x1EE3B,0x1EE3B}, {0x1EE42,0x1EE42}, {0x1EE47,0x1EE47}, {0x1EE49,0x1EE49},
    {0x1EE4B,0x1EE4B}, {0x1EE4D,0x1EE4F}, {0x1EE51,0x1EE52}, {0x1EE54,0x1EE54},
    {0x1EE57,0x1EE57}, {0x1EE59,0x1EE59}, {0x1EE5B,0x1EE5B}, {0x1EE5D,0x1EE5D},
    {0x1EE5F,0x1EE5F}, {0x1EE61,0x1EE62}, {0x1EE64,0x1EE64}, {0x1EE67,0x1EE6A},
    {0x1EE6C,0x1EE72}, {0x1EE74,0x1EE77}, {0x1EE79,0x1EE7C}, {0x1EE7E,0x1EE7E},
    {0x1EE80,0x1EE89}, {0x1EE8B,0x1EE9B}, {0x1EEA1,0x1EEA3}, {0x1EEA5,0x1EEA9},
    {0x1EEAB,0x1EEBB}, {0x1FBF0,0x1FBF9}, {0x20000,0x2A6DF}, {0x2A700,0x2B81D},
    {0x2B820,0x2CEAD}, {0x2CEB0,0x2EBE0}, {0x2EBF0,0x2EE5D}, {0x2F800,0x2FA1D},
    {0x30000,0x3134A}, {0x31350,0x33479}, {0xE0100,0xE01EF},
    };

    // Binary search over the sorted, non-overlapping range table.
    unsigned lo = 0, hi = sizeof(kWordRanges) / sizeof(kWordRanges[0]);
    while (lo < hi)
    {
        const unsigned mid = lo + (hi - lo) / 2;
        if (cp < kWordRanges[mid].lo)
            hi = mid;
        else if (cp > kWordRanges[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}
}

Vector<String> YukiModel::Tokenize(const String& text)
{
    // The ONE tokenizer — code-primary, conversational-secondary. See header doc
    // for the full rule summary. Case is preserved throughout (no ToLower): code
    // identifiers are case-sensitive, and folding case is a strictly lossy
    // operation there, so unlike the old scheme this is not a "deferred decision"
    // — for code-primary it's simply wrong to lowercase.
    Vector<String> tokens;
    const unsigned n = text.Length();
    bool atLineStart = true;
    unsigned i = 0;

    while (i < n)
    {
        const char c = text[i];

        if (c == '\n')
        {
            tokens.Push(String("\n"));
            atLineStart = true;
            ++i;
            continue;
        }
        if (c == '\r')
        {
            ++i;
            continue;
        }

        if (atLineStart && (c == ' ' || c == '\t'))
        {
            // Indent: one token per tab, one token per 2 spaces (a lone trailing
            // space rounds down to its own 1-space token). Bounds vocab growth
            // vs. one distinct token per indent width, while still preserving
            // structure (Python et al. depend on exact indent).
            while (i < n && (text[i] == ' ' || text[i] == '\t'))
            {
                if (text[i] == '\t')
                {
                    tokens.Push(String("\t"));
                    ++i;
                    continue;
                }
                unsigned spaces = 0;
                while (i < n && text[i] == ' ' && spaces < 2)
                {
                    ++spaces;
                    ++i;
                }
                tokens.Push(spaces == 2 ? String("  ") : String(" "));
            }
            continue;
        }
        atLineStart = false;

        if (c == ' ' || c == '\t')
        {
            // Mid-line whitespace carries no meaning in either domain — drop it,
            // same as before.
            while (i < n && (text[i] == ' ' || text[i] == '\t'))
                ++i;
            continue;
        }

        // Multi-char operators, longest match first.
        bool matched = false;
        for (const char* op : kOps3)
        {
            if (MatchOpAt(text, i, op, 3))
            {
                tokens.Push(String(op));
                i += 3;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        for (const char* op : kOps2)
        {
            if (MatchOpAt(text, i, op, 2))
            {
                tokens.Push(String(op));
                i += 2;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;

        // Numeric literal: optional 0x/0b prefix, digits with '_' separators,
        // at most one '.', optional exponent, optional trailing type suffix
        // (f/F/l/L/u/U). Kept as one token so "3.14f" and "0x1F" survive intact
        // instead of being shredded into digit/punctuation fragments.
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isDotDigit = (c == '.' && i + 1 < n && text[i + 1] >= '0' && text[i + 1] <= '9');
        if (isDigit || isDotDigit)
        {
            unsigned start = i;
            if (c == '0' && i + 1 < n &&
                (text[i + 1] == 'x' || text[i + 1] == 'X' || text[i + 1] == 'b' || text[i + 1] == 'B'))
                i += 2;
            bool seenDot = false, seenExp = false;
            while (i < n)
            {
                const char d = text[i];
                if ((d >= '0' && d <= '9') || d == '_' ||
                    (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))
                {
                    ++i;
                    continue;
                }
                if (d == '.' && !seenDot && !seenExp)
                {
                    seenDot = true;
                    ++i;
                    continue;
                }
                if ((d == 'e' || d == 'E') && !seenExp)
                {
                    seenExp = true;
                    ++i;
                    if (i < n && (text[i] == '+' || text[i] == '-'))
                        ++i;
                    continue;
                }
                break;
            }
            while (i < n && kNumSuffixChars.Contains(text[i]))
                ++i;
            tokens.Push(text.Substring(start, i - start));
            continue;
        }

        // Identifier/word run: ASCII [A-Za-z_][A-Za-z0-9_]* chars AND/OR any
        // non-ASCII codepoint CLASSIFIED AS LETTER-LIKE (see
        // IsNonAsciiWordChar — a verified whitelist, not a blanket "any
        // non-ASCII" rule), mixed freely in one run. This is what makes it
        // merge-eligible for BPE (see class doc): with no whitespace between
        // Japanese words, or any other unsegmented script, BPE is the ONLY
        // mechanism that can discover word/phrase-length units from the
        // codepoint stream — same job it already does for camelCase
        // fragments, just over a different alphabet. Every non-ASCII
        // codepoint is decoded via NextUTF8Char to classify it (never split
        // mid-sequence either way); the run is still extracted as a plain
        // byte-range Substring at the end.
        bool startsWordRun = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!startsWordRun && (unsigned char)c >= 0x80)
        {
            i32 peek = (i32)i;
            const c32 cp = text.NextUTF8Char(peek);
            // SAFETY: never trust NextUTF8Char to advance on malformed/truncated
            // UTF-8 (a lone continuation byte, a corrupted multi-byte sequence —
            // real possibility on ingested files or network text, not guaranteed
            // clean input). If it returns without moving `peek` past `i`, the
            // classification branches below would push a ZERO-LENGTH token and
            // leave `i` unchanged, hanging the outer loop forever. Force forward
            // progress by treating an unadvanced decode as one raw invalid byte,
            // emitted as its own token — degraded output on genuinely corrupt
            // input, never a hang.
            if ((unsigned)peek <= i)
            {
                tokens.Push(String(c, 1));
                ++i;
                continue;
            }
            if (IsNonAsciiWordChar(cp))
            {
                startsWordRun = true;   // letter/mark/digit — continues into the run below
            }
            else
            {
                // Own atomic token — the exact original byte slice (not a
                // re-encode of `cp`), so this round-trips byte-for-byte
                // regardless of any normalisation NextUTF8Char applies
                // internally. Same treatment ASCII punctuation gets below.
                tokens.Push(text.Substring(i, (unsigned)peek - (i32)i));
                i = (unsigned)peek;
                continue;
            }
        }

        if (startsWordRun)
        {
            unsigned start = i;
            while (i < n)
            {
                const char d = text[i];
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') ||
                    (d >= '0' && d <= '9') || d == '_')
                {
                    ++i;
                    continue;
                }
                if ((unsigned char)d >= 0x80)
                {
                    // Non-ASCII lead byte — decode the WHOLE codepoint (1-4
                    // bytes) via NextUTF8Char and classify it. Anything NOT
                    // in the letter/mark/digit whitelist ends the run right
                    // here (handled as its own token on the next top-level
                    // loop iteration); a whitelisted codepoint continues it.
                    i32 peek = (i32)i;
                    const c32 cp = text.NextUTF8Char(peek);
                    // SAFETY: same non-advancing-decode guard as the entry
                    // point above — an unadvanced decode here would set
                    // i = peek = itself and loop this `while (i < n)`
                    // forever, never returning to the outer dispatch where
                    // the entry-point guard could catch it. End the run
                    // instead; the outer loop's own guard handles the byte.
                    if ((unsigned)peek <= i)
                        break;
                    if (!IsNonAsciiWordChar(cp))
                        break;
                    i = (unsigned)peek;
                    continue;
                }
                break;
            }
            tokens.Push(text.Substring(start, i - start));
            continue;
        }

        // Fallback: lone ASCII symbol (quotes, braces, etc.) as its own token.
        // Non-ASCII punctuation/symbols are handled above, before this point.
        tokens.Push(String(c, 1));
        ++i;
    }

    return tokens;
}

// ─── Cartridge Creation ──────────────────────────────────────────────────────

bool CreateEmptyCartridge(Context* context, const String& path,
                           const YukiTopology& topology,
                           const Vector<String>& vocabulary)
{
    if (vocabulary.Size() != topology.vocabSize)
    {
        URHO3D_LOGERROR("CreateEmptyCartridge: Vocabulary size mismatch");
        return false;
    }

    unsigned long long totalWeights = topology.TotalWeights();
    unsigned long long weightsBytes = totalWeights * sizeof(float);
    unsigned vocabEntrySize = 32;
    unsigned long long vocabBytes = (unsigned long long)topology.vocabSize * vocabEntrySize;

    // Build header
    YukiCartridgeHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = YUKI_MAGIC;
    header.version = YUKI_CART_VERSION;
    header.topology = topology;
    header.weightsOffset = sizeof(YukiCartridgeHeader);
    header.vocabOffset = header.weightsOffset + weightsBytes;
    header.fileSize = header.vocabOffset + vocabBytes;

    File file(context, path, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("CreateEmptyCartridge: Cannot create " + path);
        return false;
    }

    // Write header
    file.Write(&header, sizeof(header));

    // Write random weights — Xavier initialization
    // Scale: sqrt(2 / (fan_in + fan_out)), approximated as sqrt(1 / embedDim)
    float scale = sqrtf(1.0f / (float)topology.embedDim);
    unsigned long long remaining = totalWeights;
    const unsigned BATCH = 4096;
    float batch[BATCH];

    // Simple LCG random — good enough for weight init
    unsigned rng = 12345;
    while (remaining > 0)
    {
        unsigned count = (unsigned)Min((unsigned long long)BATCH, remaining);
        for (unsigned i = 0; i < count; ++i)
        {
            // LCG → uniform → Box-Muller approximation
            rng = rng * 1103515245 + 12345;
            float u = (float)(rng & 0x7FFFFFFF) / (float)0x7FFFFFFF;
            batch[i] = (u * 2.0f - 1.0f) * scale;
        }
        file.Write(batch, count * sizeof(float));
        remaining -= count;
    }

    // Write vocabulary — fixed-width entries
    char entry[32];
    for (unsigned i = 0; i < topology.vocabSize; ++i)
    {
        memset(entry, 0, vocabEntrySize);
        if (i < vocabulary.Size())
        {
            unsigned len = Min((unsigned)vocabulary[i].Length(), vocabEntrySize - 1);
            memcpy(entry, vocabulary[i].CString(), len);
        }
        file.Write(entry, vocabEntrySize);
    }

    file.Close();

    URHO3D_LOGINFOF("CreateEmptyCartridge: Wrote '%s' — %llu weights (%.1f MB), %u vocab",
        path.CString(), totalWeights,
        (float)weightsBytes / (1024.0f * 1024.0f),
        topology.vocabSize);

    return true;
}

// ─── Save ────────────────────────────────────────────────────────────────────

bool YukiModel::Save()
{
    return SaveAs(path_);
}

bool YukiModel::SaveAs(const String& path)
{
    return SaveWeightsAs(weights_, path);   // serialize the LIVE weights
}

bool YukiModel::SaveWeightsAs(PagedFloatBuffer& w, const String& path)
{
    if (!loaded_ || !w.IsConfigured())
    {
        URHO3D_LOGERROR("YukiModel: Nothing to save");
        return false;
    }

    File file(context_, path, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("YukiModel: Cannot write " + path);
        return false;
    }

    // Reconstruct the flat on-disk layout: header | weights (pages, sequential) | vocab. The
    // header's offsets already describe exactly this contiguous order (set at creation), and the
    // topology is fixed, so they stay valid across training writes. Weights come from `w`: the LIVE
    // buffer for SaveAs, or the elite snapshot for a non-destructive trailing flush of the best.
    file.Write(&header_, sizeof(header_));
    for (unsigned p = 0; p < w.PageCount(); ++p)
        file.Write(w.PageData(p), w.PageElems(p) * (unsigned)sizeof(float));
    if (!vocabBlob_.Empty())
        file.Write(vocabBlob_.Buffer(), vocabBlob_.Size());
    file.Close();

    URHO3D_LOGINFO("YukiModel: Saved " + path + " (" +
        String((unsigned long long)(w.Size() * sizeof(float) / 1024)) + " KB weights)");
    return true;
}

unsigned YukiModel::CountUnknownTokens(const String& text) const
{
    unsigned count = 0;
    Vector<String> words = Tokenize(text);
    for (const String& word : words)
    {
        if (GetTokenIndex(word.Trimmed()) >= header_.topology.vocabSize)
            count++;
    }
    return count;
}

// ─── Expand ──────────────────────────────────────────────────────────────────

bool ExpandCartridge(Context* context, const String& srcPath,
                      const String& dstPath,
                      const YukiTopology& newTopology,
                      const Vector<String>& newVocabulary)
{
    // Load old cartridge
    YukiModel oldModel(context);
    if (!oldModel.Load(srcPath))
    {
        URHO3D_LOGERROR("ExpandCartridge: Cannot load source " + srcPath);
        return false;
    }

    const YukiTopology& oldT = oldModel.GetTopology();

    // Create new empty cartridge
    if (!CreateEmptyCartridge(context, dstPath, newTopology, newVocabulary))
    {
        URHO3D_LOGERROR("ExpandCartridge: Cannot create destination");
        return false;
    }

    // Load the new cartridge (has random weights)
    YukiModel newModel(context);
    if (!newModel.Load(dstPath))
        return false;

    // The dimension-expansion copy below is intricate flat-pointer arithmetic. Bridge it with a
    // flat scratch: pull the paged random-init out, mutate it flat (code unchanged), write it
    // back. Expansion is a rare offline op; a >2^31-weight target would need a paged migration
    // (a follow-up) — guard it loudly rather than truncate.
    const unsigned long long newTotal = newModel.GetWeights().Size();
    if (newTotal > 0x7fffffffULL)
    {
        URHO3D_LOGERROR("ExpandCartridge: new topology has " + String(newTotal) +
            " weights (>2^31) — paged expansion not yet supported");
        return false;
    }
    Vector<float> flat((unsigned)newTotal);
    newModel.GetWeights().CopyToFlat(flat.Buffer());   // random-init starting point, flattened
    float* newWeights = flat.Buffer();
    unsigned newDim = newTopology.embedDim;
    unsigned oldDim = oldT.embedDim;
    unsigned copyDim = Min(newDim, oldDim);

    // Copy embedding weights — row by row (each row = one token's embedding)
    // Old tokens that exist in new vocab get their learned embeddings
    for (unsigned i = 0; i < oldT.vocabSize; ++i)
    {
        String token = oldModel.GetToken(i);
        unsigned newIdx = 0;
        // Find this token in new vocabulary
        for (unsigned j = 0; j < newVocabulary.Size(); ++j)
        {
            if (newVocabulary[j] == token)
            {
                newIdx = j;
                // Copy the embedding vector (up to copyDim floats)
                const float* oldEmb = oldModel.GetEmbeddingWeights() + i * oldDim;
                float* newEmb = newWeights + newIdx * newDim;
                memcpy(newEmb, oldEmb, copyDim * sizeof(float));
                break;
            }
        }
    }

    // Copy per-layer weights — for layers that exist in both
    unsigned copyLayers = Min(oldT.nLayers, newTopology.nLayers);
    // Skip past embedding in new weights pointer
    float* layerStart = newWeights + (unsigned long long)newTopology.vocabSize * newDim;

    unsigned long long oldLayerStride =
        4ULL * oldDim * oldDim + (unsigned long long)oldDim * oldT.ffDim +
        (unsigned long long)oldT.ffDim * oldDim + 2ULL * oldDim;

    unsigned long long newLayerStride =
        4ULL * newDim * newDim + (unsigned long long)newDim * newTopology.ffDim +
        (unsigned long long)newTopology.ffDim * newDim + 2ULL * newDim;

    for (unsigned layer = 0; layer < copyLayers; ++layer)
    {
        const YukiLayerWeights& oldLW = oldModel.GetLayerWeights(layer);
        float* dst = layerStart + layer * newLayerStride;

        // Q,K,V,Out: copy [copyDim × copyDim] submatrix from [oldDim × oldDim]
        const float* srcPtrs[4] = { oldLW.qWeights, oldLW.kWeights, oldLW.vWeights, oldLW.outWeights };
        for (int m = 0; m < 4; ++m)
        {
            for (unsigned row = 0; row < copyDim; ++row)
            {
                memcpy(dst + row * newDim, srcPtrs[m] + row * oldDim, copyDim * sizeof(float));
            }
            dst += (unsigned long long)newDim * newDim;
        }

        // FF1: [copyDim rows × min(oldFfDim, newFfDim) cols]
        unsigned copyFf = Min(oldT.ffDim, newTopology.ffDim);
        for (unsigned row = 0; row < copyDim; ++row)
            memcpy(dst + row * newTopology.ffDim, oldLW.ff1Weights + row * oldT.ffDim, copyFf * sizeof(float));
        dst += (unsigned long long)newDim * newTopology.ffDim;

        // FF2: [min(oldFfDim, newFfDim) rows × copyDim cols]
        for (unsigned row = 0; row < copyFf; ++row)
            memcpy(dst + row * newDim, oldLW.ff2Weights + row * oldDim, copyDim * sizeof(float));
        dst += (unsigned long long)newTopology.ffDim * newDim;

        // Norm weights + bias
        memcpy(dst, oldLW.normWeights, copyDim * sizeof(float));
        dst += newDim;
        memcpy(dst, oldLW.normBias, copyDim * sizeof(float));
    }

    // Write the migrated flat scratch back into the paged store, then save.
    newModel.GetWeights().CopyFromFlat(flat.Buffer());
    newModel.SaveAs(dstPath);

    URHO3D_LOGINFOF("ExpandCartridge: %ux%u (%u layers) → %ux%u (%u layers), %u → %u vocab",
        oldDim, oldDim, oldT.nLayers,
        newDim, newDim, newTopology.nLayers,
        oldT.vocabSize, newTopology.vocabSize);

    return true;
}

// ─── PagedFloatBuffer ────────────────────────────────────────────────────────
// Engine-side impl co-located here rather than in its own PagedFloatBuffer.cpp:
// adding a brand-new .cpp to the Urho3D source GLOB needs a cmake reconfigure, which
// this broker/sandbox build environment blocks (configure_file -> "Operation not
// permitted"). Hosting the methods in an already-compiled TU sidesteps that. The pure
// packing logic lives in YukiPaging.h; the standalone verification gate stays in
// PagedFloatBuffer.cpp. If cmake is ever unblocked here, this block moves back into
// PagedFloatBuffer.cpp unchanged.

using YukiMath::YukiPage;

bool PagedFloatBuffer::Configure(unsigned embedDim, unsigned nLayers, unsigned ffDim,
                                 unsigned vocabSize, unsigned strideElems)
{
    Clear();

    const unsigned tc = YukiMath::YukiTensorCount(nLayers);
    Vector<unsigned long long> lens(tc);
    if (YukiMath::YukiEnumerateTensors(embedDim, nLayers, ffDim, vocabSize, lens.Buffer(), tc) != tc)
        return false;

    Vector<YukiPage> plan(tc);   // page count never exceeds tensor count
    unsigned long long total = 0;
    const unsigned np = YukiMath::YukiPlanPages(lens.Buffer(), tc, strideElems,
                                                plan.Buffer(), tc, &total);
    if (np == 0)
        return false;   // a single tensor exceeds the i32 page ceiling — unsupported

    dir_.Resize(np);
    pages_.Resize(np);
    for (unsigned p = 0; p < np; ++p)
    {
        dir_[p] = plan[p];
        pages_[p].Resize((unsigned)plan[p].elems);   // elems is i32-safe by construction
    }
    size_ = total;
    Zero();
    return true;
}

void PagedFloatBuffer::Clear()
{
    pages_.Clear();
    dir_.Clear();
    size_ = 0;
}

float* PagedFloatBuffer::Span(unsigned long long offset, unsigned long long len)
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), offset);
    assert(p < dir_.Size());
    // Tensor-aligned pages guarantee the whole span sits in one page.
    assert(offset + len <= dir_[p].base + dir_[p].elems);
    return pages_[p].Buffer() + (unsigned)(offset - dir_[p].base);
}

const float* PagedFloatBuffer::Span(unsigned long long offset, unsigned long long len) const
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), offset);
    assert(p < dir_.Size());
    assert(offset + len <= dir_[p].base + dir_[p].elems);
    return pages_[p].Buffer() + (unsigned)(offset - dir_[p].base);
}

void PagedFloatBuffer::Zero()
{
    for (unsigned p = 0; p < pages_.Size(); ++p)
        memset(pages_[p].Buffer(), 0, (size_t)dir_[p].elems * sizeof(float));
}

void PagedFloatBuffer::CopyFrom(const PagedFloatBuffer& src)
{
    assert(SameLayout(src));
    for (unsigned p = 0; p < pages_.Size(); ++p)
        memcpy(pages_[p].Buffer(), src.pages_[p].Buffer(), (size_t)dir_[p].elems * sizeof(float));
}

void PagedFloatBuffer::CopyFromFlat(const float* src)
{
    // src[base .. base+elems) maps element-for-element onto each page (same logical order).
    for (unsigned p = 0; p < pages_.Size(); ++p)
        memcpy(pages_[p].Buffer(), src + dir_[p].base, (size_t)dir_[p].elems * sizeof(float));
}

void PagedFloatBuffer::CopyToFlat(float* dst) const
{
    for (unsigned p = 0; p < pages_.Size(); ++p)
        memcpy(dst + dir_[p].base, pages_[p].Buffer(), (size_t)dir_[p].elems * sizeof(float));
}

bool PagedFloatBuffer::ContentEquals(const PagedFloatBuffer& other) const
{
    if (!SameLayout(other))
        return false;
    for (unsigned p = 0; p < pages_.Size(); ++p)
        if (memcmp(pages_[p].Buffer(), other.pages_[p].Buffer(), (size_t)dir_[p].elems * sizeof(float)) != 0)
            return false;
    return true;
}

float& PagedFloatBuffer::At(unsigned long long idx)
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), idx);
    assert(p < dir_.Size());
    return pages_[p].Buffer()[(unsigned)(idx - dir_[p].base)];
}

float PagedFloatBuffer::At(unsigned long long idx) const
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), idx);
    assert(p < dir_.Size());
    return pages_[p].Buffer()[(unsigned)(idx - dir_[p].base)];
}

bool PagedFloatBuffer::SameLayout(const PagedFloatBuffer& other) const
{
    if (dir_.Size() != other.dir_.Size() || size_ != other.size_)
        return false;
    for (unsigned p = 0; p < dir_.Size(); ++p)
        if (dir_[p].base != other.dir_[p].base || dir_[p].elems != other.dir_[p].elems)
            return false;
    return true;
}

// Paged view builders — mirror YukiMath::BuildModelPtrs / BuildModelGrads exactly
// (same layout order, contract §2), but each per-tensor pointer comes from Span(),
// which is a contiguous float* because pages are tensor-aligned. So the identical
// forward/backward code runs whether the buffer is flat or paged.
void PagedFloatBuffer::BuildModelPtrs(const YukiMath::YukiDims& d, YukiMath::YukiModelPtrs& m,
                                      YukiMath::YukiLayerPtrs* layerOut) const
{
    const unsigned long long dd = (unsigned long long)d.embedDim * d.embedDim;
    const unsigned long long df = (unsigned long long)d.embedDim * d.ffDim;
    const unsigned long long fd = (unsigned long long)d.ffDim * d.embedDim;
    const unsigned long long ed = d.embedDim;
    const unsigned long long emb = (unsigned long long)d.vocabSize * d.embedDim;
    const unsigned long long op = (unsigned long long)d.embedDim * d.vocabSize;

    unsigned long long o = 0;
    m.embedding = Span(o, emb); o += emb;
    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        YukiMath::YukiLayerPtrs& lw = layerOut[i];
        lw.q = Span(o, dd); o += dd;
        lw.k = Span(o, dd); o += dd;
        lw.v = Span(o, dd); o += dd;
        lw.o = Span(o, dd); o += dd;
        lw.ff1 = Span(o, df); o += df;
        lw.ff2 = Span(o, fd); o += fd;
        lw.norm = Span(o, ed); o += ed;
        lw.normBias = Span(o, ed); o += ed;
    }
    m.finalNorm = Span(o, ed); o += ed;
    m.finalNormBias = Span(o, ed); o += ed;
    m.outputProj = Span(o, op); o += op;
    m.layers = layerOut;
}

void PagedFloatBuffer::BuildModelGrads(const YukiMath::YukiDims& d, YukiMath::YukiModelGrads& g,
                                       YukiMath::YukiLayerGrads* layerOut)
{
    const unsigned long long dd = (unsigned long long)d.embedDim * d.embedDim;
    const unsigned long long df = (unsigned long long)d.embedDim * d.ffDim;
    const unsigned long long fd = (unsigned long long)d.ffDim * d.embedDim;
    const unsigned long long ed = d.embedDim;
    const unsigned long long emb = (unsigned long long)d.vocabSize * d.embedDim;
    const unsigned long long op = (unsigned long long)d.embedDim * d.vocabSize;

    unsigned long long o = 0;
    g.embedding = Span(o, emb); o += emb;
    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        YukiMath::YukiLayerGrads& lg = layerOut[i];
        lg.q = Span(o, dd); o += dd;
        lg.k = Span(o, dd); o += dd;
        lg.v = Span(o, dd); o += dd;
        lg.o = Span(o, dd); o += dd;
        lg.ff1 = Span(o, df); o += df;
        lg.ff2 = Span(o, fd); o += fd;
        lg.norm = Span(o, ed); o += ed;
        lg.normBias = Span(o, ed); o += ed;
    }
    g.finalNorm = Span(o, ed); o += ed;
    g.finalNormBias = Span(o, ed); o += ed;
    g.outputProj = Span(o, op); o += op;
    g.layers = layerOut;
}

}
