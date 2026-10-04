// YukiTrainAdapter — engine adapter for the Yuki training loop (M8). Implementation.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Standalone offset-tiling gate: compile with -DYUKI_M8_STANDALONE to verify the
// weight slicing matches YukiModel::ResolveWeights byte-for-byte and exactly tiles
// TotalWeights — no engine build.

#include "../Precompiled.h"
#include "YukiTrainAdapter.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

unsigned long long TotalWeights(const YukiDims& d)
{
    // Mirror of YukiModel::TotalWeights (YukiModel.cpp).
    unsigned long long total = (unsigned long long)d.vocabSize * d.embedDim;   // embedding

    const unsigned long long perLayer =
        4ULL * d.embedDim * d.embedDim +                 // q, k, v, o
        (unsigned long long)d.embedDim * d.ffDim +       // ff1
        (unsigned long long)d.ffDim * d.embedDim +       // ff2
        2ULL * d.embedDim;                               // norm weights + bias
    total += perLayer * d.nLayers;

    total += 2ULL * d.embedDim;                              // final norm w + b
    total += (unsigned long long)d.embedDim * d.vocabSize;   // output projection
    return total;
}

void BuildModelPtrs(const float* weights, const YukiDims& d,
                    YukiModelPtrs& m, YukiLayerPtrs* layerOut)
{
    const float* p = weights;

    m.embedding = p; p += (unsigned long long)d.vocabSize * d.embedDim;

    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        YukiLayerPtrs& lw = layerOut[i];
        lw.q = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lw.k = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lw.v = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lw.o = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lw.ff1 = p;      p += (unsigned long long)d.embedDim * d.ffDim;
        lw.ff2 = p;      p += (unsigned long long)d.ffDim * d.embedDim;
        lw.norm = p;     p += d.embedDim;
        lw.normBias = p; p += d.embedDim;
    }

    m.finalNorm = p;     p += d.embedDim;
    m.finalNormBias = p; p += d.embedDim;
    m.outputProj = p;    p += (unsigned long long)d.embedDim * d.vocabSize;

    m.layers = layerOut;
    // Postcondition (asserted by the M8 gate): p == weights + TotalWeights(d).
}

void BuildModelGrads(float* grads, const YukiDims& d,
                     YukiModelGrads& g, YukiLayerGrads* layerOut)
{
    // Identical walk to BuildModelPtrs — the grad buffer is byte-for-byte parallel
    // to the weight buffer (M7/M8 contract §2), so the slicing offsets match exactly.
    float* p = grads;

    g.embedding = p; p += (unsigned long long)d.vocabSize * d.embedDim;

    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        YukiLayerGrads& lg = layerOut[i];
        lg.q = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lg.k = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lg.v = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lg.o = p;        p += (unsigned long long)d.embedDim * d.embedDim;
        lg.ff1 = p;      p += (unsigned long long)d.embedDim * d.ffDim;
        lg.ff2 = p;      p += (unsigned long long)d.ffDim * d.embedDim;
        lg.norm = p;     p += d.embedDim;
        lg.normBias = p; p += d.embedDim;
    }

    g.finalNorm = p;     p += d.embedDim;
    g.finalNormBias = p; p += d.embedDim;
    g.outputProj = p;    p += (unsigned long long)d.embedDim * d.vocabSize;

    g.layers = layerOut;
    // Postcondition (asserted by the M8 gate): p == grads + TotalWeights(d).
}

}

}

#ifdef YUKI_M8_STANDALONE
#include <cstdio>
#include <vector>

using namespace Urho3D::YukiMath;

// Independent reference offsets — a different code path from BuildModelPtrs, so a
// stride bug in one is caught by disagreement with the other. Returns the element
// offset of each named view; ref-walks the same ResolveWeights ordering by hand.
struct RefOffsets
{
    unsigned long long embedding;
    std::vector<unsigned long long> q, k, v, o, ff1, ff2, norm, normBias;
    unsigned long long finalNorm, finalNormBias, outputProj, end;
};

static RefOffsets ReferenceLayout(const YukiDims& d)
{
    RefOffsets r;
    unsigned long long off = 0;
    const unsigned long long dd = (unsigned long long)d.embedDim * d.embedDim;
    const unsigned long long df = (unsigned long long)d.embedDim * d.ffDim;
    const unsigned long long fd = (unsigned long long)d.ffDim * d.embedDim;

    r.embedding = off; off += (unsigned long long)d.vocabSize * d.embedDim;
    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        r.q.push_back(off);        off += dd;
        r.k.push_back(off);        off += dd;
        r.v.push_back(off);        off += dd;
        r.o.push_back(off);        off += dd;
        r.ff1.push_back(off);      off += df;
        r.ff2.push_back(off);      off += fd;
        r.norm.push_back(off);     off += d.embedDim;
        r.normBias.push_back(off); off += d.embedDim;
    }
    r.finalNorm = off;     off += d.embedDim;
    r.finalNormBias = off; off += d.embedDim;
    r.outputProj = off;    off += (unsigned long long)d.embedDim * d.vocabSize;
    r.end = off;
    return r;
}

static int g_fail = 0;
static void expect(const char* name, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_fail;
}

int main()
{
    // A non-trivial topology: vocab != dim != ffDim, multiple layers/heads.
    YukiDims d{};
    d.embedDim = 8; d.nLayers = 3; d.nHeads = 2; d.ffDim = 32; d.vocabSize = 37; d.maxSeqLen = 16;

    const unsigned long long total = TotalWeights(d);
    std::vector<float> weights(total, 0.0f);

    YukiModelPtrs m{};
    std::vector<YukiLayerPtrs> layers(d.nLayers);
    BuildModelPtrs(weights.data(), d, m, layers.data());

    const RefOffsets ref = ReferenceLayout(d);
    const float* base = weights.data();
    auto at = [&](const float* p) -> unsigned long long { return (unsigned long long)(p - base); };

    std::printf("Yuki M8 — weight-buffer adapter, offset-tiling gate (vs independent ResolveWeights walk):\n");

    // TotalWeights agrees with the hand-walked end.
    expect("TotalWeights == reference end", total == ref.end);

    // Each view sits at the reference offset.
    expect("embedding offset",     at(m.embedding) == ref.embedding);
    expect("finalNorm offset",     at(m.finalNorm) == ref.finalNorm);
    expect("finalNormBias offset", at(m.finalNormBias) == ref.finalNormBias);
    expect("outputProj offset",    at(m.outputProj) == ref.outputProj);

    bool layersOk = (m.layers == layers.data());
    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        const YukiLayerPtrs& lw = m.layers[i];
        layersOk = layersOk
            && at(lw.q) == ref.q[i] && at(lw.k) == ref.k[i] && at(lw.v) == ref.v[i]
            && at(lw.o) == ref.o[i] && at(lw.ff1) == ref.ff1[i] && at(lw.ff2) == ref.ff2[i]
            && at(lw.norm) == ref.norm[i] && at(lw.normBias) == ref.normBias[i];
    }
    expect("all per-layer view offsets", layersOk);

    // Exact tiling: the last view ends precisely at the buffer end (no gap/overlap).
    const float* lastEnd = m.outputProj + (unsigned long long)d.embedDim * d.vocabSize;
    expect("views tile to buffer end", at(lastEnd) == total);

    // Gradient views must be byte-for-byte parallel to the weight views (contract §2):
    // same offsets, so a backward op writing dW lands on the same slot as W.
    std::vector<float> grads(total, 0.0f);
    float* gbase = grads.data();
    YukiModelGrads gv{};
    std::vector<YukiLayerGrads> glayers(d.nLayers);
    BuildModelGrads(gbase, d, gv, glayers.data());
    auto atg = [&](const float* p) -> unsigned long long { return (unsigned long long)(p - gbase); };

    bool gradsParallel =
        atg(gv.embedding) == ref.embedding &&
        atg(gv.finalNorm) == ref.finalNorm &&
        atg(gv.finalNormBias) == ref.finalNormBias &&
        atg(gv.outputProj) == ref.outputProj &&
        (gv.layers == glayers.data());
    for (unsigned i = 0; i < d.nLayers; ++i)
    {
        const YukiLayerGrads& lg = gv.layers[i];
        gradsParallel = gradsParallel
            && atg(lg.q) == ref.q[i] && atg(lg.k) == ref.k[i] && atg(lg.v) == ref.v[i]
            && atg(lg.o) == ref.o[i] && atg(lg.ff1) == ref.ff1[i] && atg(lg.ff2) == ref.ff2[i]
            && atg(lg.norm) == ref.norm[i] && atg(lg.normBias) == ref.normBias[i];
    }
    expect("grad views parallel to weight views", gradsParallel);
    const float* gLastEnd = gv.outputProj + (unsigned long long)d.embedDim * d.vocabSize;
    expect("grad views tile to buffer end", atg(gLastEnd) == total);

    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
#endif
