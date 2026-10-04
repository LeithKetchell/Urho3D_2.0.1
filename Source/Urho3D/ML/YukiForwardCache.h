// YukiForwardCache — Caching forward pass for training (M2).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Inference throws its intermediates away. Training can't: every backward pass
// (M3–M8) reads the activations the forward produced. This module runs the
// forward with the M0 primitives and KEEPS everything — so the backward passes
// are exact adjoints of what the forward actually computed.
//
// THIS HEADER IS THE CONTRACT. The backward milestones read YukiForwardCache;
// its field layout must not change silently. Layer semantics mirror
// YukiInference exactly (post-norm):
//     a   = x + Attn(x)
//     b   = a + FF(a)
//     out = LayerNorm(b)
// then per position: logits = LayerNorm_final(out) × Wout.
//
// Pure — no Object/Context/YukiModel dependency — so it grad-checks standalone
// (g++ + stub headers, no engine build). The engine adapter (M8) fills the
// pointer views below from a YukiModel.

#pragma once

#include "../Urho3D.h"
#include "../Container/Vector.h"
#include "../ML/YukiMath.h"

namespace Urho3D
{

namespace YukiMath
{

/// Topology, mirrored from YukiTopology without dragging in YukiModel.h.
struct YukiDims
{
    unsigned embedDim;
    unsigned nLayers;
    unsigned nHeads;
    unsigned ffDim;
    unsigned vocabSize;
    unsigned maxSeqLen;
};

/// Per-layer weight pointers (mirror YukiLayerWeights). Row-major.
struct YukiLayerPtrs
{
    const float* q;         ///< [embedDim × embedDim]
    const float* k;         ///< [embedDim × embedDim]
    const float* v;         ///< [embedDim × embedDim]
    const float* o;         ///< [embedDim × embedDim]
    const float* ff1;       ///< [embedDim × ffDim]
    const float* ff2;       ///< [ffDim × embedDim]
    const float* norm;      ///< [embedDim]
    const float* normBias;  ///< [embedDim]
};

/// Whole-model weight pointers.
struct YukiModelPtrs
{
    const float* embedding;      ///< [vocabSize × embedDim]
    const float* outputProj;     ///< [embedDim × vocabSize]
    const float* finalNorm;      ///< [embedDim]
    const float* finalNormBias;  ///< [embedDim]
    const YukiLayerPtrs* layers; ///< [nLayers]
};

/// Cached activations for one layer. S = seqLen, D = embedDim, F = ffDim, H = heads.
struct YukiLayerCache
{
    Vector<float> x;        ///< [S × D]   layer input (embeddings for layer 0, else prev out)
    Vector<float> q;        ///< [S × D]   query projection
    Vector<float> k;        ///< [S × D]   key projection
    Vector<float> v;        ///< [S × D]   value projection
    Vector<float> probs;    ///< [H × S × S] causal softmax weights (j>i left zero)
    Vector<float> context;  ///< [S × D]   attention output before Wo
    Vector<float> a;        ///< [S × D]   x + attnOut (post-attention residual)
    Vector<float> ff1pre;   ///< [S × F]   pre-GELU
    Vector<float> ff1post;  ///< [S × F]   post-GELU
    Vector<float> b;        ///< [S × D]   a + FF(a) (post-FF residual, pre-LayerNorm)
    Vector<float> out;      ///< [S × D]   LayerNorm(b) — layer output / next layer input
};

/// The full forward cache. The contract every backward pass reads from.
struct YukiForwardCache
{
    Vector<YukiLayerCache> layers;
    Vector<float> finalLNout;  ///< [S × D]     LayerNorm_final(last layer out), per position
    Vector<float> logits;      ///< [S × vocab] per-position next-token logits
    unsigned seqLen{};
    YukiDims dims{};

    /// Resize every buffer for the given dims + sequence length.
    void Allocate(const YukiDims& d, unsigned seqLen);
};

/// Run the forward pass and fill `cache` with every activation backprop needs,
/// producing per-position logits in cache.logits. Uses the M0 primitives so the
/// later backward passes are exact adjoints. `tokens` has `seqLen` entries, each
/// a valid index < vocabSize.
URHO3D_API void ForwardWithCache(const YukiModelPtrs& model, const YukiDims& dims,
                                 const unsigned* tokens, unsigned seqLen,
                                 YukiForwardCache& cache);

}

}
