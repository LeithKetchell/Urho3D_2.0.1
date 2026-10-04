// YukiMath — Differentiable math primitives for training.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// The bedrock of backprop. Pure functions, no Object, no Context — just
// floats in, floats out. Forward ops mirror YukiInference exactly so the
// gradient checker can finite-difference them; backward ops are their
// analytic adjoints.
//
// CONTRACT:
//   - Forward ops write their result (in-place or to an output buffer).
//   - Backward ops ACCUMULATE (+=) into gradient buffers. Callers zero the
//     buffers first; accumulation is what makes residual branches and
//     per-position parameter gradients sum correctly.
//   - Any gradient output pointer may be null to skip that gradient.
//
// All matrices are row-major. Sizes are in elements, not bytes.

#pragma once

#include "../Urho3D.h"

namespace Urho3D
{

namespace YukiMath
{

// ─── Forward primitives (mirror YukiInference) ───────────────────────────────

/// Matrix multiply: C[m×n] = A[m×k] × B[k×n]. Writes C (overwrites).
URHO3D_API void MatMul(const float* A, const float* B, float* C,
                       unsigned m, unsigned k, unsigned n);

/// Softmax over `len` elements, in-place. Numerically stable (max-subtracted).
URHO3D_API void Softmax(float* data, unsigned len);

/// Approximate GELU activation, in-place (tanh formulation).
URHO3D_API void GELU(float* data, unsigned len);

/// Layer normalization, in-place: y = w·(x-mean)/std + b over `dim` elements.
URHO3D_API void LayerNorm(const float* weights, const float* bias,
                          float* data, unsigned dim, float eps = 1e-5f);

// ─── Backward primitives (analytic adjoints, accumulate into gradients) ───────

/// Backward of MatMul C = A×B. Given upstream dC[m×n] and the forward inputs,
/// accumulate dA[m×k] += dC·Bᵀ and dB[k×n] += Aᵀ·dC. Pass null to skip either.
URHO3D_API void MatMulBackward(const float* A, const float* B, const float* dC,
                               float* dA, float* dB,
                               unsigned m, unsigned k, unsigned n);

/// Backward of Softmax. Given the softmax output y[len] and upstream dy[len],
/// accumulate dx[len] += y ⊙ (dy − Σⱼ dyⱼ·yⱼ).
URHO3D_API void SoftmaxBackward(const float* y, const float* dy,
                                float* dx, unsigned len);

/// Backward of GELU. Needs the PRE-activation input x[len] (not the output)
/// and upstream dy[len]; accumulates dx[len] += dy · g'(x).
URHO3D_API void GELUBackward(const float* x, const float* dy,
                             float* dx, unsigned len);

/// Backward of LayerNorm. Given the forward input x[dim], the scale weights
/// w[dim], and upstream dy[dim], accumulate dx[dim], dw[dim] and db[dim].
/// Any of dx/dw/db may be null to skip. `eps` must match the forward pass.
URHO3D_API void LayerNormBackward(const float* x, const float* w, const float* dy,
                                  float* dx, float* dw, float* db,
                                  unsigned dim, float eps = 1e-5f);

// ─── Loss ────────────────────────────────────────────────────────────────────

/// Softmax + cross-entropy in one pass. Fills `probs[vocab]` with softmax(logits)
/// and returns the loss −log(probs[target]). `target` must be < vocab.
URHO3D_API float SoftmaxCrossEntropy(const float* logits, unsigned vocab,
                                     unsigned target, float* probs);

/// Gradient of softmax-cross-entropy w.r.t. logits, given the softmax probs:
/// accumulate dLogits[v] += probs[v] − (v == target ? 1 : 0).
URHO3D_API void CrossEntropyGradient(const float* probs, unsigned vocab,
                                     unsigned target, float* dLogits);

// ─── Elementwise helpers ─────────────────────────────────────────────────────

/// dst[i] += src[i] for i in [0, n). Residual add and gradient accumulation.
URHO3D_API void AddInPlace(float* dst, const float* src, unsigned n);

/// dst[i] += alpha · src[i] for i in [0, n). Scaled accumulate (axpy).
URHO3D_API void AddScaled(float* dst, const float* src, float alpha, unsigned n);

}

}
