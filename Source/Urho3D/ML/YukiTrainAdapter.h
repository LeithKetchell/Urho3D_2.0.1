// YukiTrainAdapter — engine adapter for the Yuki training loop (M8).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// The pure forward-cache (M2) and backward (M3–M7) work on flat float buffers and
// the YukiModelPtrs / YukiLayerPtrs views — no Object, no YukiModel. The cartridge
// (YukiModel) stores its weights as one contiguous float buffer in a fixed order.
// This adapter is the bridge the M2 header calls out:
//
//     "The engine adapter (M8) fills the pointer views below from a YukiModel."
//
// It slices a flat weight buffer into the views ForwardWithCache reads. The layout
// MUST match YukiModel::ResolveWeights exactly — that ordering is the contract the
// gradient buffer (sized to TotalWeights) also mirrors element-for-element.
//
// Pure (depends only on YukiForwardCache's structs), so the offset tiling is
// verifiable standalone with g++ + stub headers — no engine build.

#pragma once

#include "../Urho3D.h"
#include "../ML/YukiForwardCache.h"   // YukiDims, YukiModelPtrs, YukiLayerPtrs

namespace Urho3D
{

namespace YukiMath
{

/// Total weight count for a topology. Mirrors YukiModel::TotalWeights exactly so
/// the adapter, the cartridge, and the gradient buffer all agree on the size.
URHO3D_API unsigned long long TotalWeights(const YukiDims& dims);

/// Slice a flat weight buffer into the pointer views ForwardWithCache reads. Layout
/// (matching YukiModel::ResolveWeights):
///   embedding[vocab×dim]
///   per layer: q,k,v,o[dim×dim], ff1[dim×ffDim], ff2[ffDim×dim], norm[dim], normBias[dim]
///   finalNorm[dim], finalNormBias[dim]
///   outputProj[dim×vocab]
/// `layerOut` must point to nLayers caller-owned YukiLayerPtrs; model.layers is set
/// to it. The views exactly tile [weights, weights + TotalWeights(dims)).
URHO3D_API void BuildModelPtrs(const float* weights, const YukiDims& dims,
                               YukiModelPtrs& model, YukiLayerPtrs* layerOut);

/// Mutable per-layer gradient views — the write targets the backward ops accumulate
/// into. Mirrors YukiLayerPtrs field-for-field, but float* (the backward ops do +=).
struct YukiLayerGrads
{
    float* q;        ///< dWq        [embedDim × embedDim]
    float* k;        ///< dWk        [embedDim × embedDim]
    float* v;        ///< dWv        [embedDim × embedDim]
    float* o;        ///< dWout      [embedDim × embedDim]
    float* ff1;      ///< dWff1      [embedDim × ffDim]
    float* ff2;      ///< dWff2      [ffDim × embedDim]
    float* norm;     ///< dNormW     [embedDim]
    float* normBias; ///< dNormB     [embedDim]
};

/// Mutable whole-model gradient views (mirror YukiModelPtrs, but float*).
struct YukiModelGrads
{
    float* embedding;      ///< [vocabSize × embedDim]
    float* outputProj;     ///< [embedDim × vocabSize]
    float* finalNorm;      ///< [embedDim]
    float* finalNormBias;  ///< [embedDim]
    YukiLayerGrads* layers;///< [nLayers]
};

/// Slice a flat GRADIENT buffer into mutable views, byte-for-byte parallel to
/// BuildModelPtrs over the weight buffer (M7/M8 contract §2). The caller zeroes the
/// flat buffer once; the backward ops accumulate into these views; then one
/// SGDStep walks the whole buffer. `layerOut` must point to nLayers caller-owned
/// YukiLayerGrads. The views exactly tile [grads, grads + TotalWeights(dims)).
URHO3D_API void BuildModelGrads(float* grads, const YukiDims& dims,
                                YukiModelGrads& out, YukiLayerGrads* layerOut);

}

}
