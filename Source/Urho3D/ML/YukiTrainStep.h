// YukiTrainStep — assembled full-sequence training step (M8).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// The capstone: wires the verified pieces into one forward+backward+SGD over a
// sequence, per the M7/M8 contract (monolith canonical).
//   M2 ForwardWithCache  ->  M7 SequenceCrossEntropy (loss + dLogits seed)
//   ->  M3 OutputProjBackward, FinalNormBackward
//   ->  per layer top→bottom:  M5 SeqLayerNormBackward
//                              M4 FFNBackward (per-token, looped over S)
//                              M6 AttentionBackward  (A=probs, headsOut=context)
//   ->  M3 EmbeddingBackward  ->  M7 SGDStep
//
// Pure (flat float buffers + YukiMath/cache/loss/backward), so the WHOLE assembled
// network grad-checks standalone against finite differences — not just the pieces.

#pragma once

#include "../Urho3D.h"
#include "YukiForwardCache.h"   // YukiDims, YukiModelPtrs, YukiLayerPtrs
#include "YukiTrainAdapter.h"   // YukiModelGrads, YukiLayerGrads (view-taking overloads)

namespace Urho3D
{

namespace YukiMath
{

/// Accumulate full-sequence gradients into `gradOut` (length TotalWeights(dims),
/// ZEROED by the caller — every backward op does +=). No weight update. Returns the
/// mean next-token cross-entropy loss. `tokens` has `seqLen` ids (seqLen >= 2); the
/// target for position p is tokens[p+1], the last position is skipped (no next
/// token). `weights` is the flat cartridge buffer laid out per the contract.
URHO3D_API float YukiSequenceForwardBackward(const float* weights, const YukiDims& dims,
                                             const unsigned* tokens, unsigned seqLen,
                                             float* gradOut);

/// View-taking core of the sequence forward+backward. The caller supplies pre-built
/// weight views (w + wl, with w.layers == wl) and gradient views (g + gl, with
/// g.layers == gl), so the underlying buffers may be flat OR paged. Grads accumulate
/// (+=) into g/gl — the caller zeroes them first. Returns the mean loss.
URHO3D_API float YukiSequenceForwardBackward(const YukiModelPtrs& w, const YukiLayerPtrs* wl,
                                             const YukiModelGrads& g, YukiLayerGrads* gl,
                                             const YukiDims& dims,
                                             const unsigned* tokens, unsigned seqLen);

/// Forward-only mean next-token cross-entropy loss for one sequence — no backward
/// and no weight update, so it costs roughly half a forward+backward pass. Same
/// loss value YukiSequenceForwardBackward returns; used for the fixed-probe elite
/// metric (a stable, comparable quality number independent of the training subset).
URHO3D_API float YukiSequenceLoss(const float* weights, const YukiDims& dims,
                                  const unsigned* tokens, unsigned seqLen);

/// View-taking, forward-only loss. The caller has set w.layers. Buffer-layout
/// agnostic (flat or paged) — used to score paged elite/probe weights.
URHO3D_API float YukiSequenceLoss(const YukiModelPtrs& w, const YukiDims& dims,
                                  const unsigned* tokens, unsigned seqLen);

/// One SGD training step: zero `gradScratch`, accumulate the sequence gradients,
/// then apply weights -= lr·grad over the whole flat buffer. `weights` and
/// `gradScratch` are both length TotalWeights(dims). Returns the mean loss.
URHO3D_API float YukiTrainStep(float* weights, float* gradScratch, const YukiDims& dims,
                               const unsigned* tokens, unsigned seqLen, float lr);

}

}
