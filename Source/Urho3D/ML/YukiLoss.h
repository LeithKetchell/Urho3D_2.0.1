// YukiLoss — Full-sequence training loss + SGD step (M7).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// The top of the backward chain and the bottom of the optimizer.
//   - SequenceCrossEntropy: averages per-position cross-entropy over the
//     sequence and produces dLogits = d(mean loss)/d(logits), the seed gradient
//     the backward passes (M3+) propagate down from.
//   - SGDStep: the plain weight update w -= lr·g.
//
// Built on the M0 primitives (SoftmaxCrossEntropy, CrossEntropyGradient); pure,
// grad-checks standalone.

#pragma once

#include "../Urho3D.h"

namespace Urho3D
{

namespace YukiMath
{

/// Full-sequence cross-entropy for next-token prediction.
///
/// For each position p in [0, S): if targets[p] < vocab, accumulate the
/// cross-entropy of softmax(logits[p]) against targets[p] and write that
/// position's gradient into dLogits[p]. Positions with targets[p] >= vocab are
/// skipped (e.g. the final position that has no next token) and their dLogits
/// rows are left zero.
///
/// Returns the MEAN loss over valid positions, and dLogits is the gradient of
/// that mean (scaled by 1/validCount). `probsScratch` is caller-provided scratch
/// of length `vocab`. dLogits has length S*vocab and is fully overwritten.
URHO3D_API float SequenceCrossEntropy(const float* logits, const unsigned* targets,
                                      unsigned S, unsigned vocab,
                                      float* dLogits, float* probsScratch);

/// Plain SGD update: weights[i] -= lr * grads[i] for i in [0, count).
URHO3D_API void SGDStep(float* weights, const float* grads, float lr, unsigned count);

/// Adam update — per-parameter adaptive step (Kingma & Ba 2014). Plain SGD was
/// measured too weak to converge a vocab-4096 transformer in a sane number of
/// passes; Adam adapts the step per weight and carries momentum through plateaus.
///
/// `m` and `v` are the first/second moment buffers (length `count`, caller-owned,
/// zero-initialised before the first step). `t` is the 1-based timestep used for
/// bias correction. Updates `weights` in place; `m`/`v` are advanced.
URHO3D_API void AdamStep(float* weights, const float* grads, float* m, float* v,
                         unsigned long long t, float lr, float beta1, float beta2,
                         float eps, unsigned count);

}

}
