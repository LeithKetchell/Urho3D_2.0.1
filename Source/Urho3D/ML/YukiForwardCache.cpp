// YukiForwardCache — Caching forward pass for training (M2).
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiForwardCache.h"

#include <cmath>
#include <cstring>

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

void YukiForwardCache::Allocate(const YukiDims& d, unsigned S)
{
    dims = d;
    seqLen = S;

    const unsigned D = d.embedDim, F = d.ffDim, H = d.nHeads, V = d.vocabSize;

    layers.Resize(d.nLayers);
    for (unsigned L = 0; L < d.nLayers; ++L)
    {
        YukiLayerCache& c = layers[L];
        c.x.Resize(S * D);
        c.q.Resize(S * D);
        c.k.Resize(S * D);
        c.v.Resize(S * D);
        c.probs.Resize(H * S * S);
        c.context.Resize(S * D);
        c.a.Resize(S * D);
        c.ff1pre.Resize(S * F);
        c.ff1post.Resize(S * F);
        c.b.Resize(S * D);
        c.out.Resize(S * D);
    }

    finalLNout.Resize(S * D);
    logits.Resize(S * V);
}

void ForwardWithCache(const YukiModelPtrs& m, const YukiDims& d,
                      const unsigned* tokens, unsigned S, YukiForwardCache& c)
{
    c.Allocate(d, S);

    if (d.nLayers == 0 || S == 0)
        return;

    const unsigned D = d.embedDim, F = d.ffDim, H = d.nHeads, V = d.vocabSize;
    const unsigned hd = D / H;
    const float scale = 1.0f / sqrtf((float)hd);

    // Embedding lookup → layer 0 input.
    {
        float* x0 = c.layers[0].x.Buffer();
        for (unsigned i = 0; i < S; ++i)
        {
            const float* e = m.embedding + (size_t)tokens[i] * D;
            for (unsigned k = 0; k < D; ++k)
                x0[i * D + k] = e[k];
        }
    }

    Vector<float> scores;
    scores.Resize(S);

    for (unsigned L = 0; L < d.nLayers; ++L)
    {
        YukiLayerCache& lc = c.layers[L];
        const YukiLayerPtrs& w = m.layers[L];
        const float* x = lc.x.Buffer();
        float* Q = lc.q.Buffer();
        float* K = lc.k.Buffer();
        float* Vv = lc.v.Buffer();

        // Q, K, V projections.
        MatMul(x, w.q, Q, S, D, D);
        MatMul(x, w.k, K, S, D, D);
        MatMul(x, w.v, Vv, S, D, D);

        // Causal scaled-dot-product attention, per head.
        float* ctx = lc.context.Buffer();
        float* probs = lc.probs.Buffer();
        memset(ctx, 0, (size_t)S * D * sizeof(float));
        memset(probs, 0, (size_t)H * S * S * sizeof(float));

        for (unsigned h = 0; h < H; ++h)
        {
            for (unsigned i = 0; i < S; ++i)
            {
                for (unsigned j = 0; j <= i; ++j)
                {
                    float dot = 0.0f;
                    for (unsigned t = 0; t < hd; ++t)
                        dot += Q[i * D + h * hd + t] * K[j * D + h * hd + t];
                    scores[j] = dot * scale;
                }

                Softmax(scores.Buffer(), i + 1);

                for (unsigned j = 0; j <= i; ++j)
                    probs[((size_t)h * S + i) * S + j] = scores[j];

                for (unsigned t = 0; t < hd; ++t)
                {
                    float sum = 0.0f;
                    for (unsigned j = 0; j <= i; ++j)
                        sum += scores[j] * Vv[j * D + h * hd + t];
                    ctx[i * D + h * hd + t] = sum;
                }
            }
        }

        // Output projection + attention residual:  a = x + ctx·Wo
        float* a = lc.a.Buffer();
        MatMul(ctx, w.o, a, S, D, D);
        for (unsigned idx = 0; idx < S * D; ++idx)
            a[idx] += x[idx];

        // Feedforward:  b = a + FF(a),  FF(a) = GELU(a·Wff1)·Wff2
        float* ff1 = lc.ff1pre.Buffer();
        MatMul(a, w.ff1, ff1, S, D, F);

        float* ff1a = lc.ff1post.Buffer();
        memcpy(ff1a, ff1, (size_t)S * F * sizeof(float));
        GELU(ff1a, S * F);

        float* b = lc.b.Buffer();
        MatMul(ff1a, w.ff2, b, S, F, D);
        for (unsigned idx = 0; idx < S * D; ++idx)
            b[idx] += a[idx];

        // LayerNorm per position → layer output.
        float* out = lc.out.Buffer();
        memcpy(out, b, (size_t)S * D * sizeof(float));
        for (unsigned i = 0; i < S; ++i)
            LayerNorm(w.norm, w.normBias, out + i * D, D);

        // Feed next layer.
        if (L + 1 < d.nLayers)
            memcpy(c.layers[L + 1].x.Buffer(), out, (size_t)S * D * sizeof(float));
    }

    // Final LayerNorm per position + output projection to per-position logits.
    const float* lastOut = c.layers[d.nLayers - 1].out.Buffer();
    float* fln = c.finalLNout.Buffer();
    memcpy(fln, lastOut, (size_t)S * D * sizeof(float));
    for (unsigned i = 0; i < S; ++i)
        LayerNorm(m.finalNorm, m.finalNormBias, fln + i * D, D);

    MatMul(fln, m.outputProj, c.logits.Buffer(), S, D, V);
}

}

}
