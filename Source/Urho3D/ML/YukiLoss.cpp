// YukiLoss — Full-sequence training loss + SGD step (M7).
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiLoss.h"
#include "../ML/YukiMath.h"

#include <cstring>
#include <cmath>

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

float SequenceCrossEntropy(const float* logits, const unsigned* targets,
                           unsigned S, unsigned vocab,
                           float* dLogits, float* probsScratch)
{
    memset(dLogits, 0, (size_t)S * vocab * sizeof(float));

    double total = 0.0;
    unsigned count = 0;

    for (unsigned p = 0; p < S; ++p)
    {
        unsigned tgt = targets[p];
        if (tgt >= vocab)
            continue;  // no target for this position (e.g. last token)

        const float* zp = logits + (size_t)p * vocab;
        total += SoftmaxCrossEntropy(zp, vocab, tgt, probsScratch);
        ++count;

        // Per-position gradient probs - onehot, into this row (zeroed above).
        CrossEntropyGradient(probsScratch, vocab, tgt, dLogits + (size_t)p * vocab);
    }

    if (count == 0)
        return 0.0f;

    // dLogits currently holds the SUM gradient; scale to the MEAN.
    const float inv = 1.0f / (float)count;
    for (unsigned i = 0; i < S * vocab; ++i)
        dLogits[i] *= inv;

    return (float)(total / (double)count);
}

void SGDStep(float* weights, const float* grads, float lr, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        weights[i] -= lr * grads[i];
}

void AdamStep(float* weights, const float* grads, float* m, float* v,
              unsigned long long t, float lr, float beta1, float beta2,
              float eps, unsigned count)
{
    // Bias-correction denominators for this timestep. powf underflows to 0 after
    // many steps, leaving the corrections at 1 — the correct large-t limit.
    const float bc1 = 1.0f - powf(beta1, (float)t);
    const float bc2 = 1.0f - powf(beta2, (float)t);

    for (unsigned i = 0; i < count; ++i)
    {
        const float g = grads[i];
        m[i] = beta1 * m[i] + (1.0f - beta1) * g;        // first moment (momentum)
        v[i] = beta2 * v[i] + (1.0f - beta2) * g * g;    // second moment (variance)
        const float mHat = m[i] / bc1;
        const float vHat = v[i] / bc2;
        weights[i] -= lr * mHat / (sqrtf(vHat) + eps);
    }
}

}

}
