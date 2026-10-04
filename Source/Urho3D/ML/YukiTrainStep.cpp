// YukiTrainStep — assembled full-sequence training step (M8). Implementation.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Standalone end-to-end gate: compile with -DYUKI_M8_DRIVER_STANDALONE alongside
// YukiMath.cpp, YukiForwardCache.cpp, YukiBackward.cpp (monolith), YukiLoss.cpp,
// YukiGradCheck.cpp and YukiTrainAdapter.cpp to finite-difference the WHOLE network.

#include "../Precompiled.h"
#include "YukiTrainStep.h"
#include "YukiTrainAdapter.h"   // BuildModelPtrs / BuildModelGrads, TotalWeights
#include "YukiForwardCache.h"   // ForwardWithCache, YukiForwardCache
#include "YukiBackward.h"       // monolith M3/M4/M5/M6
#include "YukiLoss.h"           // M7 SequenceCrossEntropy, SGDStep
#include "YukiMath.h"

#include <vector>

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

// View-taking core: the caller supplies pre-built weight views (w + wl, with
// w.layers == wl) and gradient views (g + gl, with g.layers == gl). This decouples
// forward+backward from buffer layout — the buffers can be flat OR paged. The paged
// trainer builds these views from PagedFloatBuffer spans; the flat overload below
// builds them from a contiguous buffer (original behaviour, bit-for-bit).
float YukiSequenceForwardBackward(const YukiModelPtrs& w, const YukiLayerPtrs* wl,
                                  const YukiModelGrads& g, YukiLayerGrads* gl,
                                  const YukiDims& d, const unsigned* tokens, unsigned S)
{
    const unsigned D = d.embedDim, F = d.ffDim, V = d.vocabSize, H = d.nHeads, L = d.nLayers;

    // Forward pass with full activation cache (M2) — the exact thing we differentiate.
    YukiForwardCache cache;
    ForwardWithCache(w, d, tokens, S, cache);

    // Next-token targets; last position has no next token -> sentinel >= vocab skips it.
    std::vector<unsigned> targets(S);
    for (unsigned p = 0; p + 1 < S; ++p) targets[p] = tokens[p + 1];
    targets[S - 1] = V;

    // M7: mean loss + seed gradient dLogits[S×V].
    std::vector<float> dLogits(S * V, 0.0f), probsScratch(V, 0.0f);
    const float loss = SequenceCrossEntropy(cache.logits.Buffer(), targets.data(), S, V,
                                            dLogits.data(), probsScratch.data());

    // ── Head backward (M3) ────────────────────────────────────────────────────
    // logits = finalLNout · outputProj   (every position)
    std::vector<float> dFinalLNout(S * D, 0.0f);
    OutputProjBackward(cache.finalLNout.Buffer(), w.outputProj, dLogits.data(),
                       dFinalLNout.data(), g.outputProj, S, D, V);

    // finalLNout = LayerNorm_final(lastLayer.out) -> grad into the top layer's `out`.
    std::vector<float> dOut(S * D, 0.0f);
    FinalNormBackward(cache.layers[L - 1].out.Buffer(), w.finalNorm, dFinalLNout.data(),
                      dOut.data(), g.finalNorm, g.finalNormBias, S, D);

    // ── Per-layer backward, top → bottom ────────────────────────────────────────
    std::vector<float> dB(S * D), dA(S * D), dX(S * D);
    for (int li = (int)L - 1; li >= 0; --li)
    {
        const unsigned l = (unsigned)li;
        const YukiLayerCache& lc = cache.layers[l];
        const YukiLayerPtrs& lw = wl[l];
        YukiLayerGrads& lg = gl[l];

        // out = LayerNorm(b)  [M5] -> dB
        std::fill(dB.begin(), dB.end(), 0.0f);
        SeqLayerNormBackward(lc.b.Buffer(), lw.norm, dOut.data(),
                             dB.data(), lg.norm, lg.normBias, S, D);

        // b = a + FF(a)  [M4 monolith, per-token over S] -> dA, dWff1, dWff2
        std::fill(dA.begin(), dA.end(), 0.0f);
        for (unsigned i = 0; i < S; ++i)
            FFNBackward(lc.a.Buffer() + i * D, lw.ff1, lw.ff2,
                        lc.ff1pre.Buffer() + i * F, lc.ff1post.Buffer() + i * F,
                        dB.data() + i * D,
                        dA.data() + i * D, lg.ff1, lg.ff2, D, F);

        // a = x + Attn(x)  [M6 monolith: A=probs, headsOut=context] -> dX + proj grads
        std::fill(dX.begin(), dX.end(), 0.0f);
        AttentionBackward(lc.x.Buffer(), lw.q, lw.k, lw.v, lw.o,
                          lc.q.Buffer(), lc.k.Buffer(), lc.v.Buffer(),
                          lc.probs.Buffer(), lc.context.Buffer(),
                          dA.data(),
                          dX.data(), lg.q, lg.k, lg.v, lg.o, S, D, H);

        // Layer l's input x is layer (l-1)'s output -> dX is the next layer's dOut.
        dOut = dX;
    }

    // ── Embedding backward (M3) ─────────────────────────────────────────────────
    // After the loop dOut is the gradient w.r.t. the layer-0 input = embedding lookup.
    EmbeddingBackward(tokens, dOut.data(), g.embedding, S, D);

    return loss;
}

float YukiSequenceForwardBackward(const float* weights, const YukiDims& d,
                                  const unsigned* tokens, unsigned S, float* gradOut)
{
    // Flat overload: tile the contiguous weight/grad buffers into views, then run the
    // view-taking core. Behaviour identical to the pre-paging implementation.
    const unsigned L = d.nLayers;
    YukiModelPtrs w{};
    std::vector<YukiLayerPtrs> wl(L);
    BuildModelPtrs(weights, d, w, wl.data());

    YukiModelGrads g{};
    std::vector<YukiLayerGrads> gl(L);
    BuildModelGrads(gradOut, d, g, gl.data());

    return YukiSequenceForwardBackward(w, wl.data(), g, gl.data(), d, tokens, S);
}

float YukiSequenceLoss(const YukiModelPtrs& w, const YukiDims& d,
                       const unsigned* tokens, unsigned S)
{
    // View-taking core (forward-only). The caller has set w.layers.
    const unsigned V = d.vocabSize;

    YukiForwardCache cache;
    ForwardWithCache(w, d, tokens, S, cache);

    std::vector<unsigned> targets(S);
    for (unsigned p = 0; p + 1 < S; ++p) targets[p] = tokens[p + 1];
    targets[S - 1] = V;   // sentinel >= vocab skips the last position

    // dLogits is required by the signature but discarded here (cheap to compute).
    std::vector<float> dLogits(S * V, 0.0f), probsScratch(V, 0.0f);
    return SequenceCrossEntropy(cache.logits.Buffer(), targets.data(), S, V,
                                dLogits.data(), probsScratch.data());
}

float YukiSequenceLoss(const float* weights, const YukiDims& d,
                       const unsigned* tokens, unsigned S)
{
    // Flat overload: tile the contiguous buffer into views, then run the core.
    const unsigned L = d.nLayers;
    YukiModelPtrs w{};
    std::vector<YukiLayerPtrs> wl(L);
    BuildModelPtrs(weights, d, w, wl.data());
    return YukiSequenceLoss(w, d, tokens, S);
}

float YukiTrainStep(float* weights, float* gradScratch, const YukiDims& d,
                    const unsigned* tokens, unsigned S, float lr)
{
    const unsigned long long count = TotalWeights(d);
    for (unsigned long long i = 0; i < count; ++i) gradScratch[i] = 0.0f;
    const float loss = YukiSequenceForwardBackward(weights, d, tokens, S, gradScratch);
    SGDStep(weights, gradScratch, lr, (unsigned)count);
    return loss;
}

}

}

#ifdef YUKI_M8_DRIVER_STANDALONE
#include "YukiGradCheck.h"
#include <cstdio>
#include <random>

using namespace Urho3D::YukiMath;

static std::mt19937 g_rng(101);
static float Rn() { std::normal_distribution<float> dist(0.0f, 0.4f); return dist(g_rng); }

int main()
{
    // Small but fully general transformer: vocab≠dim≠ff, multiple layers/heads.
    YukiDims d{};
    d.embedDim = 8; d.nLayers = 2; d.nHeads = 2; d.ffDim = 16; d.vocabSize = 11; d.maxSeqLen = 8;
    const unsigned S = 5;

    const unsigned long long total = TotalWeights(d);
    std::vector<float> weights(total), grad(total, 0.0f), scratch(total, 0.0f);
    for (auto& v : weights) v = Rn();

    // A token stream with a repeat (exercises the embedding scatter accumulation).
    std::vector<unsigned> tokens = { 3, 0, 7, 3, 5 };

    // Analytic gradients for the whole net (one forward+backward).
    std::fill(grad.begin(), grad.end(), 0.0f);
    YukiSequenceForwardBackward(weights.data(), d, tokens.data(), S, grad.data());

    // Loss closure for finite differences — recomputes the full forward each call.
    auto loss = [&]() -> double
    {
        return (double)YukiSequenceForwardBackward(weights.data(), d, tokens.data(), S, scratch.data());
    };

    // Block offsets, mirroring the contract layout.
    const unsigned long long dd = (unsigned long long)d.embedDim * d.embedDim;
    const unsigned long long df = (unsigned long long)d.embedDim * d.ffDim;
    const unsigned long long fd = (unsigned long long)d.ffDim * d.embedDim;
    const unsigned long long embN = (unsigned long long)d.vocabSize * d.embedDim;
    const unsigned long long perLayer = 4 * dd + df + fd + 2ULL * d.embedDim;
    const unsigned long long finalOff = embN + perLayer * d.nLayers;
    const unsigned long long outOff = finalOff + 2ULL * d.embedDim;

    std::printf("Yuki M8 — assembled driver, END-TO-END finite-difference gate over the whole net:\n");
    bool ok = true;
    auto chk = [&](const char* n, unsigned long long off, unsigned long long c)
    {
        GradCheckResult r = CheckGradient(loss, weights.data() + off, grad.data() + off, (unsigned)c);
        std::printf("  [%s] %-18s max rel err = %.3e\n", r.passed ? "PASS" : "FAIL", n, (double)r.maxRelErr);
        if (!r.passed) ok = false;
    };

    chk("embedding", 0, embN);
    for (unsigned l = 0; l < d.nLayers; ++l)
    {
        const unsigned long long lb = embN + perLayer * l;
        char name[32];
        std::snprintf(name, sizeof(name), "layer %u (all W)", l);
        chk(name, lb, perLayer);
    }
    chk("finalNorm+bias", finalOff, 2ULL * d.embedDim);
    chk("outputProj", outOff, (unsigned long long)d.embedDim * d.vocabSize);

    // And the whole buffer at once — catches any block-boundary mistake.
    chk("WHOLE network", 0, total);

    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
#endif
