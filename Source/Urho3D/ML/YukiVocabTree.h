// YukiVocabTree — Persistent BPE merge tree for the identifier/word vocab class.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Sits BELOW YukiModel::Tokenize, not beside it. Tokenize() already splits text
// into pre-tokens with hard boundaries a merge can never cross: identifiers/words,
// whole numeric literals, multi-char operators, indent units, newline, lone
// symbols. Only the identifier/word class is BPE-eligible — everything else the
// pre-tokenizer emits is already atomic and stays a single vocab entry (a leaf),
// exactly as it was before this class existed.
//
// STRUCTURE: every vocab entry is either a LEAF (a single character, or one of
// the non-mergeable atomic pre-tokens above — literal text stored directly) or a
// MERGE (two earlier ids, leftId/rightId, both < this entry's own id by
// construction — the tree is built bottom-up, so no cycle is possible and no
// cycle guard is needed at load, unlike YukiDispatch's user-editable route graph).
// A merge's literal text is never stored on disk; it is reconstructed once at
// load by walking the tree bottom-up and cached in RAM, so GetLiteral/GetId cost
// exactly what the old flat-vocab GetToken/GetTokenIndex cost: O(1) after a
// one-time build (BuildTokenIndex's existing convention, extended).
//
// WHY A TREE AND NOT JUST A RANKED MERGE LIST: the merge list alone is enough to
// encode text, but the parent-pointer TREE is the thing three other features
// share — cartridge storage (a merge entry costs ~8 bytes as two ids instead of
// up to 32 bytes as a literal string), warm-start embedding init at Expand() time
// (a new merge's embedding can start as a function of its two parents' ALREADY
// TRAINED embeddings instead of random noise), and multi-level OOV backoff (an
// unseen identifier resolves to the largest known CHUNKS the tree already has,
// not a blind drop to individual characters). See Claude/YUKI_BPE_TREE_DESIGN.md.

#pragma once

#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/HashMap.h"

namespace Urho3D
{

/// Sentinel: `leftId`/`rightId` unused (this entry is a leaf, not a merge).
/// Never a valid vocab id (ids are always < vocab size, which fits in far fewer
/// bits) — used as a return-value/argument sentinel, not stored on disk.
static const unsigned YUKI_VOCAB_LEAF = 0xFFFFFFFFu;

/// One MERGE vocab entry: this id's literal text is GetLiteral(leftId) +
/// GetLiteral(rightId). Both ids are guaranteed < this entry's own id.
struct YukiVocabMerge
{
    unsigned leftId;
    unsigned rightId;
};

/// One learned BPE merge RULE, in learn order (rank = learn order; lower ranks
/// merge first at encode time — the classic BPE "apply lowest-rank applicable
/// pair" loop). Stored by literal text (not id) so a rule set can be learned
/// once and replayed against a tree built independently from the same corpus —
/// see BuildVocabTreeFromRules.
struct YukiMergeRule
{
    String left;
    String right;
    unsigned rank;
};

/// O(1)-lookup runtime index over a rule set. A rule set is naturally stored/
/// transmitted as an ordered Vector<YukiMergeRule> (see YukiMergeRule doc); this
/// index is built ONCE from that list (cartridge load, or once right after
/// LearnBpeMerges) and reused across every YukiVocabTree::Encode call — Encode
/// itself never re-scans the rule list, so per-pretoken cost stays independent
/// of total vocab size.
class URHO3D_API YukiBpeRuleIndex
{
public:
    void Build(const Vector<YukiMergeRule>& rules);
    void Clear() { rank_.Clear(); }
    bool IsEmpty() const { return rank_.Empty(); }

    /// Rank of the rule merging `left` + `right` (in that order), or -1 if no
    /// such rule was learned.
    int RankOf(const String& left, const String& right) const;

private:
    /// Key = left + '\x01' + right ('\x01' never appears in tokenizer output,
    /// so it's a safe unambiguous separator without a struct-keyed HashMap).
    HashMap<String, int> rank_;
};

/// Persistent BPE vocabulary: leaves [0, LeafCount()) + merges
/// [LeafCount(), Size()). See file header for the leaf/merge split rationale.
class URHO3D_API YukiVocabTree
{
public:
    YukiVocabTree() = default;

    /// Build from an explicit leaf literal table + merge id-pairs. Rebuilds the
    /// literal cache and the literal->id hash. Returns false (and clears) on a
    /// malformed merge — leftId/rightId not both less than that entry's own id
    /// (the acyclicity invariant every caller of this class relies on).
    bool Configure(const Vector<String>& leafLiterals, const Vector<YukiVocabMerge>& merges);

    void Clear();
    bool IsConfigured() const { return leafCount_ != 0 || !merges_.Empty(); }

    unsigned LeafCount() const { return leafCount_; }
    unsigned MergeCount() const { return merges_.Size(); }
    unsigned Size() const { return leafCount_ + merges_.Size(); }

    bool IsLeaf(unsigned id) const { return id < leafCount_; }
    /// This id's two parents. False (and untouched outputs) if `id` is a leaf
    /// or out of range.
    bool GetParents(unsigned id, unsigned& leftId, unsigned& rightId) const;

    /// Reconstructed literal text for any id — O(1), served from the cache
    /// built once in Configure(). A static empty String if `id` is out of range.
    const String& GetLiteral(unsigned id) const;
    /// Literal text -> id, exact match. Returns Size() (one past the last valid
    /// id) if not found — the same "not found" convention YukiModel::
    /// GetTokenIndex already uses with vocabSize, so callers migrate unchanged.
    unsigned GetId(const String& literal) const;

    /// Encode ONE pre-token (as produced by YukiModel::Tokenize) into vocab
    /// ids. If `preferWhole` and the WHOLE pretoken is already a known leaf or
    /// merge literal — the common case for most identifiers/words, and ALWAYS
    /// the case for the non-mergeable atomic categories (operators, numeric
    /// literals, indent, newline), since those are looked up whole and never
    /// entered into the merge loop — that single id is returned directly.
    /// Otherwise: split to per-character leaf ids and repeatedly apply
    /// whichever adjacent pair has the lowest rank in `rules` until none
    /// applies (standard BPE encode). Any surviving symbol that still isn't a
    /// known leaf/merge literal (a rules/tree mismatch, or a character truly
    /// absent from the leaf set) falls back through EncodeUnknownLiteral.
    void Encode(const String& pretoken, const YukiBpeRuleIndex& rules,
                Vector<unsigned>& idsOut, bool preferWhole = true) const;

    /// Multi-level-floor OOV resolution for a literal that isn't a known leaf
    /// or merge (reached only when Encode's merge loop still leaves an unknown
    /// symbol — see Encode doc). Falls back to per-character leaf ids,
    /// dropping any character truly absent from the leaf set. This is the
    /// worst-case floor; Encode's merge loop already finds the largest known
    /// CHUNKS before ever reaching here, which is the actual multi-level
    /// backoff — this function is just where it bottoms out.
    void EncodeUnknownLiteral(const String& text, Vector<unsigned>& idsOut) const;

private:
    void RebuildCache();

    unsigned leafCount_{0};
    Vector<String> leafLiterals_;      ///< ids [0, leafCount_)
    Vector<YukiVocabMerge> merges_;    ///< ids [leafCount_, leafCount_ + merges_.Size())
    Vector<String> literalCache_;      ///< id -> reconstructed literal, ALL ids, built once
    HashMap<String, unsigned> idOf_;   ///< literal -> id, exact match, first occurrence wins
};

/// Offline vocab-builder: learn BPE merges from a corpus of IDENTIFIER/WORD
/// pre-tokens ONLY — the caller has already run YukiModel::Tokenize and kept
/// just that class (operators/numerics/indent/newline/symbols are never merge
/// candidates; see file header). Standard greedy BPE, reference-complexity
/// implementation: each pretoken starts as its sequence of single-character
/// symbols; repeat {count every adjacent pair across the WHOLE corpus, merge
/// the single most frequent pair into one new symbol everywhere it occurs}
/// until `targetMergeCount` merges are learned or no pair repeats (a
/// single-occurrence pair buys no compression, so learning stops early rather
/// than wasting a vocab slot on it).
///
/// COMPLEXITY: O(targetMergeCount x corpus token count) — a full corpus rescan
/// per learned merge, matching a reference/textbook BPE trainer, not the
/// incremental-priority-queue version production tokenizer trainers use once a
/// corpus is very large. Fine for the code+prose corpus scale this project
/// currently runs at; flagged here so it's a deliberate choice to revisit, not
/// a silent ceiling, if the ingest corpus grows enough for this to matter.
///
/// Returns learned merges in learn order (= rank order, lowest first) via
/// `rulesOut`, ready for YukiBpeRuleIndex::Build / BuildVocabTreeFromRules.
URHO3D_API void LearnBpeMerges(const Vector<String>& wordPretokens, unsigned targetMergeCount,
                               Vector<YukiMergeRule>& rulesOut);

/// Assemble a YukiVocabTree from learned rules (see LearnBpeMerges) plus every
/// distinct NON-mergeable atomic pre-token observed in the corpus (`extraLeaves`
/// — every operator/numeric-literal/indent/newline/symbol token
/// YukiModel::Tokenize can emit outside the identifier/word class; caller
/// de-duplicates before calling). Every distinct single character in
/// `wordPretokens` is collected automatically as the per-character leaf floor
/// and need not be included in `extraLeaves`. Leaf id order is the SORTED
/// literal order (not corpus-scan or HashMap-iteration order), so the same
/// corpus always assembles the same tree byte-for-byte regardless of scan
/// order or hash seed — required for a reproducible cartridge.
/// Returns false if a rule references a symbol that never became a leaf or an
/// earlier merge (a rules/corpus mismatch — e.g. rules learned from a
/// different corpus than the one `extraLeaves`/`wordPretokens` describe).
URHO3D_API bool BuildVocabTreeFromRules(const Vector<String>& wordPretokens,
                                        const Vector<String>& extraLeaves,
                                        const Vector<YukiMergeRule>& rules,
                                        YukiVocabTree& treeOut);

}
