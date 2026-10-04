// YukiModel — Cartridge format loader.
// Copyright (c) 2026 Urho3D project. License: MIT.
// Written by Leith Ketchell
//
// The cartridge is a binary file containing a neural network.
// Memory-map it, pointer-cast to weight arrays, ready to run.
// No JSON, no GGUF, no parsing. Plug it in, it runs.
//
// CARTRIDGE FORMAT:
//   [Header]       — magic, version, topology, sizes
//   [Weights]      — raw float32 arrays, sequential by layer
//   [Vocabulary]   — token entries, fixed-width
//
// TOPOLOGY (transformer):
//   Embedding → N × (Attention + FeedForward + LayerNorm) → Output
//   Size determined by: embed_dim, n_layers, n_heads, vocab_size, ff_dim
//
// The topology is fixed at creation. Weights refine through training.
// Different cartridge, different capacity. Same code runs all of them.

#pragma once

#include "../Core/Object.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/HashMap.h"     // O(1) token -> index lookup (built once at load)
#include "../ML/PagedFloatBuffer.h"   // paged (64-bit-addressable) weight store

namespace Urho3D
{

/// Cartridge file magic: "YUKI"
static const unsigned YUKI_MAGIC = 0x494B5559;  // 'Y','U','K','I' little-endian
/// Current cartridge format version (distinct from the app's YUKI_VERSION string).
static const unsigned YUKI_CART_VERSION = 1;

/// Topology descriptor — fixed at cartridge creation.
struct YukiTopology
{
    unsigned embedDim;      ///< Width of token embeddings and hidden state.
    unsigned nLayers;       ///< Number of transformer layers.
    unsigned nHeads;        ///< Attention heads per layer.
    unsigned ffDim;         ///< Feedforward hidden dimension (typically 4× embedDim).
    unsigned vocabSize;     ///< Token vocabulary size.
    unsigned maxSeqLen;     ///< Maximum sequence length (context window).

    /// Total weight count for this topology.
    unsigned long long TotalWeights() const;
    /// Total bytes for weights (float32).
    unsigned long long TotalWeightBytes() const { return TotalWeights() * sizeof(float); }
};

/// Cartridge file header — first bytes of the file.
struct YukiCartridgeHeader
{
    unsigned magic;         ///< Must be YUKI_MAGIC.
    unsigned version;       ///< Format version.
    YukiTopology topology;  ///< Network shape.
    unsigned long long weightsOffset;   ///< Byte offset to weight data.
    unsigned long long vocabOffset;     ///< Byte offset to vocabulary.
    unsigned long long fileSize;        ///< Total file size (for validation).
};

/// Per-layer weight pointers — resolved after loading.
struct YukiLayerWeights
{
    const float* qWeights;      ///< Query projection [embedDim × embedDim]
    const float* kWeights;      ///< Key projection
    const float* vWeights;      ///< Value projection
    const float* outWeights;    ///< Output projection
    const float* ff1Weights;    ///< Feedforward layer 1 [embedDim × ffDim]
    const float* ff2Weights;    ///< Feedforward layer 2 [ffDim × embedDim]
    const float* normWeights;   ///< Layer norm weights [embedDim]
    const float* normBias;      ///< Layer norm bias [embedDim]
};

/// Loaded cartridge — memory-mapped weights, ready for inference.
class URHO3D_API YukiModel : public Object
{
    URHO3D_OBJECT(YukiModel, Object);

public:
    explicit YukiModel(Context* context);
    ~YukiModel() override;

    /// Load a cartridge file. Returns true on success.
    bool Load(const String& path);
    /// Unload and free resources.
    void Unload();
    /// Return whether a cartridge is loaded.
    bool IsLoaded() const { return loaded_; }

    /// Get the topology.
    const YukiTopology& GetTopology() const { return header_.topology; }
    /// Get embedding weights [vocabSize × embedDim].
    const float* GetEmbeddingWeights() const { return embeddingWeights_; }
    /// Get output projection weights [embedDim × vocabSize].
    const float* GetOutputWeights() const { return outputWeights_; }
    /// Get final layer norm weights.
    const float* GetFinalNormWeights() const { return finalNormWeights_; }
    const float* GetFinalNormBias() const { return finalNormBias_; }
    /// Get per-layer weights.
    const YukiLayerWeights& GetLayerWeights(unsigned layer) const { return layerWeights_[layer]; }

    /// Get vocabulary token string by index.
    String GetToken(unsigned index) const;
    /// Get token index by string. Returns vocabSize if not found.
    unsigned GetTokenIndex(const String& token) const;

    /// Canonical tokenizer — THE single place text becomes tokens, shared by vocab
    /// building, training, and inference. Code-primary, conversational-secondary:
    ///   - Identifiers/numbers are case-preserved (code is case-sensitive).
    ///   - Multi-char operators (==, ->, ::, &&, <<=, ...) match as one token
    ///     (maximal munch), not one token per character.
    ///   - Numeric literals (0x/0b prefix, '_' separators, decimal point,
    ///     exponent, trailing type suffix) are kept whole.
    ///   - '\n' is always its own token; leading indent on a line is encoded as
    ///     one token per tab and one token per 2 spaces; other whitespace is
    ///     dropped (doesn't change meaning in either domain).
    ///   - Everything else not covered above is a lone symbol token.
    /// Static: pure text -> token strings, vocab-independent; compose with
    /// GetTokenIndex for ids.
    /// BREAKING CHANGE from the old lowercased/per-char-punctuation scheme —
    /// token identity has changed, so any existing vocab/cartridge needs a
    /// rebuild rather than a hot-swap.
    static Vector<String> Tokenize(const String& text);

    /// Get cartridge file path.
    const String& GetPath() const { return path_; }

    /// Mutable paged weight access — for training and GPU upload. 64-bit addressable, so a
    /// model can exceed the 2^31-element ceiling of a single flat Vector. Per-tensor pointers
    /// (GetEmbeddingWeights etc.) are contiguous spans into this store (pages are tensor-aligned).
    PagedFloatBuffer& GetWeights() { return weights_; }
    const PagedFloatBuffer& GetWeights() const { return weights_; }
    /// Save current weights back to the cartridge file.
    bool Save();
    /// Save to a new path.
    bool SaveAs(const String& path);
    /// Serialize a PROVIDED weight buffer to `path` — non-destructive, never touches weights_. Used by the
    /// trainer's trailing best-flush to write the elite snapshot without rolling the live model back.
    bool SaveWeightsAs(PagedFloatBuffer& weights, const String& path);

    /// Check if vocabulary contains a token.
    bool HasToken(const String& token) const { return GetTokenIndex(token) < header_.topology.vocabSize; }
    /// Count unknown tokens in text.
    unsigned CountUnknownTokens(const String& text) const;

private:
    /// Resolve weight pointers from raw data.
    bool ResolveWeights();

    /// Build the token -> index hash from vocabData_ (called once after the vocab blob is set).
    /// Turns GetTokenIndex from an O(vocabSize) strncmp scan into an O(1) lookup — the corpus-wide
    /// tokenization at startup/train scaled with vocabSize×tokens and froze the UI once the memory
    /// corpus grew (BuildTokenSet on the main thread). First occurrence wins, matching the old scan.
    void BuildTokenIndex();

    String path_;
    YukiCartridgeHeader header_;
    PagedFloatBuffer weights_;          ///< Paged weight store (was the flat weight region of the file).
    bool loaded_{};

    /// Resolved per-tensor spans into weights_ (each tensor is contiguous — pages are tensor-aligned).
    const float* embeddingWeights_{};
    const float* outputWeights_{};
    const float* finalNormWeights_{};
    const float* finalNormBias_{};
    Vector<YukiLayerWeights> layerWeights_;

    /// Vocabulary — small fixed-width entries, kept flat in its own blob.
    Vector<unsigned char> vocabBlob_;   ///< Owns the vocabulary bytes (vocabData_ points into it).
    const char* vocabData_{};
    unsigned vocabEntrySize_{};
    HashMap<String, unsigned> tokenIndex_;   ///< token string -> vocab index; O(1) GetTokenIndex.
};

/// Create an empty cartridge file with random weights.
/// Used by YukiForge to initialize before training.
URHO3D_API bool CreateEmptyCartridge(Context* context, const String& path,
                                      const YukiTopology& topology,
                                      const Vector<String>& vocabulary);

/// Expand a cartridge to a larger topology, preserving existing weights.
/// New weights are random-initialized. Existing weights copied where dimensions match.
/// The brain grows but doesn't forget.
URHO3D_API bool ExpandCartridge(Context* context, const String& srcPath,
                                 const String& dstPath,
                                 const YukiTopology& newTopology,
                                 const Vector<String>& newVocabulary);

}
