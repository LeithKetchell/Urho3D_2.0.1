// YukiBackward — Transformer backward components (M4+).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Each component is the analytic reverse of a piece of YukiInference's forward,
// built only from M0 primitives (YukiMath) and gated by M1's CheckGradient.
// M4: feed-forward block backward.

#pragma once

#include "../Urho3D.h"

namespace Urho3D
{

namespace YukiMath
{

/// FFN backward (M4). Reverse of the forward block:
///   ff1  = h · Wff1            (h[dim] · Wff1[dim×ffDim] -> ff1[ffDim])
///   ff1g = GELU(ff1)
///   ff2  = ff1g · Wff2         (ff1g[ffDim] · Wff2[ffDim×dim] -> ff2[dim])
///   hOut = h + ff2             (residual)
/// Given upstream dHout[dim] and the cached forward intermediates ff1 (pre-GELU)
/// and ff1g (post-GELU), accumulate dh[dim], dWff1[dim×ffDim], dWff2[ffDim×dim].
/// Caller zeroes the gradient buffers. The residual routes dHout into dh directly,
/// then the matmul branch accumulates on top.
URHO3D_API void FFNBackward(const float* h, const float* Wff1, const float* Wff2,
                            const float* ff1, const float* ff1g, const float* dHout,
                            float* dh, float* dWff1, float* dWff2,
                            unsigned dim, unsigned ffDim);

/// Attention backward (M6). Reverse of causal multi-head self-attention:
///   Q,K,V = H·Wq, H·Wk, H·Wv ; per head: scores = (Qh·Khᵀ)/√headDim, causal
///   mask (j<=i), softmax -> A ; headOut = A·Vh ; out = concat(heads)·Wout ;
///   Hout = H + out.
/// Given upstream dHout[seqLen×dim] and the cached projections Q,K,V[seqLen×dim],
/// per-head attention weights A[nHeads×seqLen×seqLen] (masked entries zero) and
/// headsOut[seqLen×dim], accumulate dH, dWq, dWk, dWv, dWout. Caller zeroes them.
URHO3D_API void AttentionBackward(const float* H, const float* Wq, const float* Wk,
                                  const float* Wv, const float* Wout,
                                  const float* Q, const float* K, const float* V,
                                  const float* A, const float* headsOut,
                                  const float* dHout,
                                  float* dH, float* dWq, float* dWk, float* dWv, float* dWout,
                                  unsigned seqLen, unsigned dim, unsigned nHeads);

// ─── M3: head backward (output projection, final LayerNorm, embedding) ────────
// The non-transformer ends of the net: the output projection + final LayerNorm at
// the top, and the embedding lookup at the bottom. Each is the analytic reverse of
// the matching YukiInference / YukiForwardCache forward step, built only from M0.

/// Output-projection backward (M3). The top of the net, scored at EVERY position
/// (inference needed only the last; training scores all S):
///   logits[S×vocab] = finalLNout[S×D] · outputProj[D×vocab]
/// Given upstream dLogits[S×vocab] and the cached finalLNout, accumulate
/// dFinalLNout[S×D] and dOutputProj[D×vocab]. Caller zeroes the gradient buffers.
URHO3D_API void OutputProjBackward(const float* finalLNout, const float* outputProj,
                                   const float* dLogits,
                                   float* dFinalLNout, float* dOutputProj,
                                   unsigned seqLen, unsigned dim, unsigned vocab);

/// Final-LayerNorm backward (M3). Per position s:
///   finalLNout[s] = LayerNorm(finalNorm, finalNormBias, lastOut[s])
/// Given upstream dFinalLNout[S×D] and the cached last-layer output lastOut[S×D],
/// accumulate per-position dLastOut[S×D] and the shared-across-positions
/// dFinalNorm[D] and dFinalNormBias[D]. Caller zeroes the gradient buffers; the
/// per-position norm-weight grads sum into dFinalNorm/dFinalNormBias.
URHO3D_API void FinalNormBackward(const float* lastOut, const float* finalNorm,
                                  const float* dFinalLNout,
                                  float* dLastOut, float* dFinalNorm, float* dFinalNormBias,
                                  unsigned seqLen, unsigned dim, float eps = 1e-5f);

/// Sequence LayerNorm backward (M5). The per-layer post-norm `out = LayerNorm(b)`
/// applied at every position with weights shared across the sequence:
///   out[s] = LayerNorm(normW, normB, b[s])   for s in [0, S)
/// Given upstream dOut[S×D] and the cached pre-norm input b[S×D], accumulate the
/// per-position dB[S×D] and the shared-across-positions dNormW[D], dNormB[D].
/// Caller zeroes the gradient buffers; the per-position weight grads sum into
/// dNormW/dNormB. (Mechanically the same operation as M3's FinalNormBackward —
/// kept separate so each milestone's verified code stays frozen.)
URHO3D_API void SeqLayerNormBackward(const float* b, const float* normW,
                                     const float* dOut,
                                     float* dB, float* dNormW, float* dNormB,
                                     unsigned seqLen, unsigned dim, float eps = 1e-5f);

/// Embedding backward (M3). The bottom of backprop: layer-0 input is the token
/// embedding lookup, x[s] = embedding[tokens[s]]. The adjoint of that gather is a
/// scatter-add: dEmbedding[tokens[s]·D + d] += dx[s·D + d]. Repeated tokens
/// accumulate — the shared row receives every position that read it. Caller zeroes
/// dEmbedding.
URHO3D_API void EmbeddingBackward(const unsigned* tokens, const float* dx,
                                  float* dEmbedding,
                                  unsigned seqLen, unsigned dim);

}

}
