// YukiVocabTree — implementation.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiVocabTree.h"
#include "../Container/HashSet.h"
#include "../Container/Sort.h"   // confirmed against YukiCartRegistry.cpp/YukiTrainer.cpp's own usage
#include "../IO/Log.h"

namespace Urho3D
{

// ─── UTF-8 codepoint splitting ──────────────────────────────────────────────────

namespace
{
// Split `text` into one String per Unicode CODEPOINT, not per byte — every
// per-character loop below (Encode's merge-start, EncodeUnknownLiteral,
// LearnBpeMerges, BuildVocabTreeFromRules) used to do `String(text[i], 1)` in
// a byte loop, which shreds a multi-byte UTF-8 character (e.g. any Japanese
// codepoint, 2-3 bytes) into meaningless individual continuation bytes. Uses
// Urho3D's canonical UTF-8 API (String::NextUTF8Char) rather than raw byte
// indexing — advances a byte offset past however many bytes the codepoint
// actually takes (1-4), so a character is never split mid-sequence. ASCII
// text is unaffected: every ASCII codepoint is exactly one byte, so this
// degrades to the old per-byte behaviour for pure-ASCII input.
Vector<String> SplitUTF8Codepoints(const String& text)
{
    Vector<String> out;
    i32 byteOffset = 0;
    const i32 n = (i32)text.Length();
    while (byteOffset < n)
    {
        const i32 start = byteOffset;
        text.NextUTF8Char(byteOffset);   // advances byteOffset past this one codepoint
        // SAFETY: never trust NextUTF8Char to advance on malformed/truncated
        // UTF-8 — a lone continuation byte or corrupted sequence is real
        // possibility for text reaching BPE from /ingest or the network, not
        // guaranteed clean. Without this, an unadvanced decode loops this
        // `while` forever (byteOffset stays at `start`, `n` never reached).
        // Force forward progress by one byte instead — degraded output on
        // genuinely corrupt input, never a hang. This one fix covers all
        // four callers below (Encode, EncodeUnknownLiteral, LearnBpeMerges,
        // BuildVocabTreeFromRules), since they all route through here.
        if (byteOffset <= start)
            byteOffset = start + 1;
        out.Push(text.Substring((unsigned)start, (unsigned)(byteOffset - start)));
    }
    return out;
}
}

// ─── YukiBpeRuleIndex ──────────────────────────────────────────────────────────

namespace
{
inline String RuleKey(const String& left, const String& right)
{
    return left + String('\x01') + right;
}
}

void YukiBpeRuleIndex::Build(const Vector<YukiMergeRule>& rules)
{
    rank_.Clear();
    for (const YukiMergeRule& r : rules)
    {
        // First occurrence wins on a duplicate (left,right) pair — shouldn't
        // happen from LearnBpeMerges (each pair is merged at most once), but a
        // hand-assembled or externally-supplied rule set could repeat one; the
        // lowest rank is the one that should apply, and rules are expected in
        // rank order, so first-seen IS lowest-rank.
        const String key = RuleKey(r.left, r.right);
        if (!rank_.Contains(key))
            rank_[key] = (int)r.rank;
    }
}

int YukiBpeRuleIndex::RankOf(const String& left, const String& right) const
{
    auto it = rank_.Find(RuleKey(left, right));
    return it == rank_.End() ? -1 : it->second_;
}

// ─── YukiVocabTree ──────────────────────────────────────────────────────────────

bool YukiVocabTree::Configure(const Vector<String>& leafLiterals, const Vector<YukiVocabMerge>& merges)
{
    // Acyclicity check up front — every downstream function (GetLiteral,
    // RebuildCache) assumes leftId/rightId < the merge's own id and will
    // read garbage or loop forever if that's violated. Catch it here, once,
    // rather than in every caller.
    const unsigned leafCount = leafLiterals.Size();
    for (unsigned i = 0; i < merges.Size(); ++i)
    {
        const unsigned ownId = leafCount + i;
        if (merges[i].leftId >= ownId || merges[i].rightId >= ownId)
        {
            URHO3D_LOGERRORF("YukiVocabTree::Configure: merge %u has a parent >= its own id "
                             "(left=%u right=%u own=%u) — refusing (would not be acyclic)",
                             i, merges[i].leftId, merges[i].rightId, ownId);
            Clear();
            return false;
        }
    }

    leafCount_ = leafCount;
    leafLiterals_ = leafLiterals;
    merges_ = merges;
    RebuildCache();
    return true;
}

void YukiVocabTree::Clear()
{
    leafCount_ = 0;
    leafLiterals_.Clear();
    merges_.Clear();
    literalCache_.Clear();
    idOf_.Clear();
}

void YukiVocabTree::RebuildCache()
{
    literalCache_.Resize(leafCount_ + merges_.Size());
    idOf_.Clear();

    for (unsigned i = 0; i < leafCount_; ++i)
    {
        literalCache_[i] = leafLiterals_[i];
        // First occurrence wins — matches YukiModel::BuildTokenIndex's existing
        // convention for a duplicate literal (shouldn't happen for a properly
        // de-duplicated leaf set, but this keeps behaviour defined either way).
        if (!idOf_.Contains(literalCache_[i]))
            idOf_[literalCache_[i]] = i;
    }
    for (unsigned i = 0; i < merges_.Size(); ++i)
    {
        const unsigned id = leafCount_ + i;
        // Both parents are < id (checked in Configure), and leaves/earlier
        // merges are already filled in above/in this same forward pass, so a
        // single forward walk suffices — no recursion needed despite this
        // being a tree.
        literalCache_[id] = literalCache_[merges_[i].leftId] + literalCache_[merges_[i].rightId];
        if (!idOf_.Contains(literalCache_[id]))
            idOf_[literalCache_[id]] = id;
    }
}

bool YukiVocabTree::GetParents(unsigned id, unsigned& leftId, unsigned& rightId) const
{
    if (id < leafCount_)
        return false;
    const unsigned idx = id - leafCount_;
    if (idx >= merges_.Size())
        return false;
    leftId = merges_[idx].leftId;
    rightId = merges_[idx].rightId;
    return true;
}

const String& YukiVocabTree::GetLiteral(unsigned id) const
{
    static const String kEmpty;
    return id < literalCache_.Size() ? literalCache_[id] : kEmpty;
}

unsigned YukiVocabTree::GetId(const String& literal) const
{
    auto it = idOf_.Find(literal);
    return it == idOf_.End() ? Size() : it->second_;
}

void YukiVocabTree::Encode(const String& pretoken, const YukiBpeRuleIndex& rules,
                           Vector<unsigned>& idsOut, bool preferWhole) const
{
    idsOut.Clear();
    if (pretoken.Empty())
        return;

    if (preferWhole)
    {
        const unsigned whole = GetId(pretoken);
        if (whole < Size())
        {
            idsOut.Push(whole);
            return;
        }
    }

    // Start as per-CODEPOINT symbols (not per-byte — see SplitUTF8Codepoints);
    // repeatedly apply the LOWEST-rank applicable adjacent pair (classic BPE
    // encode) until none applies. O(symbols^2) worst case per pretoken
    // (rescans adjacent pairs after every merge) — pretokens are
    // identifier/word-length, so this is cheap; RankOf itself is O(1) via
    // YukiBpeRuleIndex, so this does NOT rescan the rule list (that would be
    // the actually expensive version).
    Vector<String> symbols = SplitUTF8Codepoints(pretoken);

    for (;;)
    {
        int bestRank = -1;
        unsigned bestPos = 0;
        for (unsigned p = 0; p + 1 < symbols.Size(); ++p)
        {
            const int r = rules.RankOf(symbols[p], symbols[p + 1]);
            if (r >= 0 && (bestRank < 0 || r < bestRank))
            {
                bestRank = r;
                bestPos = p;
            }
        }
        if (bestRank < 0)
            break;
        symbols[bestPos] = symbols[bestPos] + symbols[bestPos + 1];
        symbols.Erase(bestPos + 1);
    }

    for (const String& sym : symbols)
    {
        const unsigned id = GetId(sym);
        if (id < Size())
            idsOut.Push(id);
        else
            EncodeUnknownLiteral(sym, idsOut);   // rules/tree mismatch or truly-novel run — see doc
    }
}

void YukiVocabTree::EncodeUnknownLiteral(const String& text, Vector<unsigned>& idsOut) const
{
    // Per-CODEPOINT floor, not per-byte (see SplitUTF8Codepoints) — a dropped
    // "character truly absent from the leaf set" now means one Unicode
    // character, not one UTF-8 continuation byte.
    for (const String& ch : SplitUTF8Codepoints(text))
    {
        const unsigned id = GetId(ch);
        if (id < Size())
            idsOut.Push(id);
        // else: character truly absent from the leaf set — dropped. This is
        // the same floor YukiInference::Tokenize's per-character OOV fallback
        // already had; Encode's merge loop above is what makes this the RARE
        // path instead of the common one.
    }
}

// ─── LearnBpeMerges ─────────────────────────────────────────────────────────────

void LearnBpeMerges(const Vector<String>& wordPretokens, unsigned targetMergeCount,
                    Vector<YukiMergeRule>& rulesOut)
{
    rulesOut.Clear();
    if (wordPretokens.Empty() || targetMergeCount == 0)
        return;

    Vector<Vector<String> > corpus;
    corpus.Reserve(wordPretokens.Size());
    for (const String& w : wordPretokens)
    {
        // Per-CODEPOINT start (see SplitUTF8Codepoints), not per-byte — a
        // Japanese pretoken now starts as a sequence of whole characters, so
        // merges learned over it build up real multi-character units, the
        // same way English merges build up subwords, instead of merging
        // meaningless UTF-8 continuation-byte fragments.
        Vector<String> syms = SplitUTF8Codepoints(w);
        if (!syms.Empty())
            corpus.Push(syms);
    }

    for (unsigned rank = 0; rank < targetMergeCount; ++rank)
    {
        // Count every adjacent pair across the whole corpus this round. Three
        // parallel maps keyed by the same RuleKey — count, plus the two
        // literal halves (avoids re-splitting the key string to recover them
        // for the winning pair below).
        HashMap<String, unsigned> pairCount;
        HashMap<String, String> pairLeft;
        HashMap<String, String> pairRight;
        for (const Vector<String>& word : corpus)
        {
            for (unsigned p = 0; p + 1 < word.Size(); ++p)
            {
                const String key = RuleKey(word[p], word[p + 1]);
                unsigned& c = pairCount[key];
                if (c == 0)
                {
                    pairLeft[key] = word[p];
                    pairRight[key] = word[p + 1];
                }
                ++c;
            }
        }
        if (pairCount.Empty())
            break;   // every word fully collapsed to single symbols — nothing left to pair

        // Most frequent pair wins. Ties broken by HashMap iteration order,
        // which is insertion-stable in Urho3D's HashMap for a fixed corpus —
        // VERIFY this against your HashMap implementation if you need
        // byte-for-byte reproducibility across engine versions; if it isn't
        // insertion-stable, tie-breaking here is only deterministic per-run,
        // not across Urho3D versions.
        String bestKey;
        unsigned bestCount = 0;
        for (auto it = pairCount.Begin(); it != pairCount.End(); ++it)
        {
            if (it->second_ > bestCount)
            {
                bestCount = it->second_;
                bestKey = it->first_;
            }
        }
        if (bestCount < 2)
            break;   // a single-occurrence pair buys no compression — stop, don't spend a vocab slot on it

        const String bestLeft = pairLeft[bestKey];
        const String bestRight = pairRight[bestKey];
        const String merged = bestLeft + bestRight;

        YukiMergeRule rule;
        rule.left = bestLeft;
        rule.right = bestRight;
        rule.rank = rank;
        rulesOut.Push(rule);

        // Apply the merge everywhere it occurs: left to right, non-overlapping
        // (after consuming a pair at p, resume scanning from p+1's new
        // content — do not re-check the just-merged symbol against itself).
        for (Vector<String>& word : corpus)
        {
            for (unsigned p = 0; p + 1 < word.Size(); ++p)
            {
                if (word[p] == bestLeft && word[p + 1] == bestRight)
                {
                    word[p] = merged;
                    word.Erase(p + 1);
                }
            }
        }
    }

    URHO3D_LOGINFOF("LearnBpeMerges: learned %u/%u merges from %u pretokens",
                    rulesOut.Size(), targetMergeCount, wordPretokens.Size());
}

// ─── BuildVocabTreeFromRules ────────────────────────────────────────────────────

bool BuildVocabTreeFromRules(const Vector<String>& wordPretokens, const Vector<String>& extraLeaves,
                             const Vector<YukiMergeRule>& rules, YukiVocabTree& treeOut)
{
    // Leaf set = every distinct single character seen in the word corpus (the
    // per-char floor Encode/EncodeUnknownLiteral always need) + every distinct
    // non-mergeable atomic pre-token the caller supplies.
    HashSet<String> leafSet;
    for (const String& w : wordPretokens)
        for (const String& ch : SplitUTF8Codepoints(w))
            leafSet.Insert(ch);
    for (const String& s : extraLeaves)
        leafSet.Insert(s);

    Vector<String> leaves;
    leaves.Reserve(leafSet.Size());
    for (const String& s : leafSet)
        leaves.Push(s);
    // Sorted id order — NOT corpus-scan or HashSet-iteration order — so the
    // same corpus always assembles the same tree byte-for-byte. Sort(begin,
    // end) confirmed against YukiCartRegistry.cpp's own identical call
    // (Sort(lines.Begin(), lines.End())), not assumed.
    Sort(leaves.Begin(), leaves.End());

    HashMap<String, unsigned> idOf;
    for (unsigned i = 0; i < leaves.Size(); ++i)
        idOf[leaves[i]] = i;

    Vector<YukiVocabMerge> merges;
    merges.Reserve(rules.Size());
    for (unsigned r = 0; r < rules.Size(); ++r)
    {
        auto itL = idOf.Find(rules[r].left);
        auto itR = idOf.Find(rules[r].right);
        if (itL == idOf.End() || itR == idOf.End())
        {
            URHO3D_LOGERRORF("BuildVocabTreeFromRules: rule %u ('%s'+'%s') references a symbol "
                             "that is neither a leaf nor an earlier merge — rules/corpus mismatch",
                             r, rules[r].left.CString(), rules[r].right.CString());
            return false;
        }

        YukiVocabMerge m;
        m.leftId = itL->second_;
        m.rightId = itR->second_;
        merges.Push(m);

        const unsigned newId = leaves.Size() + merges.Size() - 1;
        const String mergedLiteral = rules[r].left + rules[r].right;
        // First occurrence wins, same convention as everywhere else in this
        // file — a rule set shouldn't produce the same literal twice, but
        // this keeps the id map's behaviour defined if it ever does.
        if (!idOf.Contains(mergedLiteral))
            idOf[mergedLiteral] = newId;
    }

    return treeOut.Configure(leaves, merges);
}

}
