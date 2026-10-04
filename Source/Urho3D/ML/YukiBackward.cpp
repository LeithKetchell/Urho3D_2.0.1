// YukiBackward — Transformer backward components (M4+). Implementation.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Standalone grad-check: compile with -DYUKI_M4_STANDALONE alongside YukiMath.cpp
// and YukiGradCheck.cpp to run the FFN-backward finite-difference gate.

#include "../Precompiled.h"
#include "YukiBackward.h"
#include "YukiMath.h"

#include <cmath>
#include <vector>

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

void FFNBackward(const float* h, const float* Wff1, const float* Wff2,
                 const float* ff1, const float* ff1g, const float* dHout,
                 float* dh, float* dWff1, float* dWff2,
                 unsigned dim, unsigned ffDim)
{
    // residual: hOut = h + ff2  ->  dh gets dHout directly; dff2 == dHout.
    AddInPlace(dh, dHout, dim);

    // ff2 = ff1g · Wff2  ->  dff1g += dHout · Wff2ᵀ ; dWff2 += ff1gᵀ · dHout.
    std::vector<float> dff1g(ffDim, 0.0f);
    MatMulBackward(ff1g, Wff2, dHout, dff1g.data(), dWff2, 1, ffDim, dim);

    // ff1g = GELU(ff1)  ->  dff1 += dff1g · g'(ff1).
    std::vector<float> dff1(ffDim, 0.0f);
    GELUBackward(ff1, dff1g.data(), dff1.data(), ffDim);

    // ff1 = h · Wff1  ->  dh += dff1 · Wff1ᵀ (onto the residual) ; dWff1 += hᵀ · dff1.
    MatMulBackward(h, Wff1, dff1.data(), dh, dWff1, 1, dim, ffDim);
}

void AttentionBackward(const float* H, const float* Wq, const float* Wk,
                       const float* Wv, const float* Wout,
                       const float* Q, const float* K, const float* V,
                       const float* A, const float* headsOut,
                       const float* dHout,
                       float* dH, float* dWq, float* dWk, float* dWv, float* dWout,
                       unsigned seqLen, unsigned dim, unsigned nHeads)
{
    const unsigned headDim = dim / nHeads;
    const float scale = 1.0f / std::sqrt((float)headDim);

    // residual: Hout = H + out  ->  dH += dHout ; dout = dHout
    AddInPlace(dH, dHout, seqLen * dim);

    // out = headsOut · Wout  ->  dHeadsOut += dHout·Woutᵀ ; dWout += headsOutᵀ·dHout
    std::vector<float> dHeadsOut(seqLen * dim, 0.0f);
    MatMulBackward(headsOut, Wout, dHout, dHeadsOut.data(), dWout, seqLen, dim, dim);

    // projection-input gradients, filled per head then routed back through Wq/Wk/Wv
    std::vector<float> dQ(seqLen * dim, 0.0f), dK(seqLen * dim, 0.0f), dV(seqLen * dim, 0.0f);

    for (unsigned hd = 0; hd < nHeads; ++hd)
    {
        const unsigned c0 = hd * headDim;
        const float* Ah = A + hd * seqLen * seqLen;

        for (unsigned i = 0; i < seqLen; ++i)
        {
            const unsigned len = i + 1;                       // causal: j in [0, i]

            // headOut[i] = Σ_j A[i,j]·V[j]  ->  dA[i,j] += dHeadOut[i]·V[j] ; dV[j] += A[i,j]·dHeadOut[i]
            std::vector<float> dArow(len, 0.0f);
            for (unsigned j = 0; j < len; ++j)
            {
                float da = 0.0f;
                for (unsigned d = 0; d < headDim; ++d)
                {
                    const float dho = dHeadsOut[i * dim + c0 + d];
                    da += dho * V[j * dim + c0 + d];
                    dV[j * dim + c0 + d] += Ah[i * seqLen + j] * dho;
                }
                dArow[j] = da;
            }

            // softmax backward over the (length-len) row
            std::vector<float> dScores(len, 0.0f);
            SoftmaxBackward(&Ah[i * seqLen], dArow.data(), dScores.data(), len);

            // scores[i,j] = (Q[i]·K[j])·scale  ->  dQ[i] += g·K[j] ; dK[j] += g·Q[i]
            for (unsigned j = 0; j < len; ++j)
            {
                const float g = dScores[j] * scale;
                for (unsigned d = 0; d < headDim; ++d)
                {
                    dQ[i * dim + c0 + d] += g * K[j * dim + c0 + d];
                    dK[j * dim + c0 + d] += g * Q[i * dim + c0 + d];
                }
            }
        }
    }

    // Q=H·Wq, K=H·Wk, V=H·Wv  ->  dH += dProj·Wᵀ (all three) ; dW += Hᵀ·dProj
    MatMulBackward(H, Wq, dQ.data(), dH, dWq, seqLen, dim, dim);
    MatMulBackward(H, Wk, dK.data(), dH, dWk, seqLen, dim, dim);
    MatMulBackward(H, Wv, dV.data(), dH, dWv, seqLen, dim, dim);
}

// ─── M3: head backward ────────────────────────────────────────────────────────

void OutputProjBackward(const float* finalLNout, const float* outputProj,
                        const float* dLogits,
                        float* dFinalLNout, float* dOutputProj,
                        unsigned seqLen, unsigned dim, unsigned vocab)
{
    // logits = finalLNout · outputProj across all S positions in one matmul:
    //   dFinalLNout += dLogits · outputProjᵀ ; dOutputProj += finalLNoutᵀ · dLogits
    MatMulBackward(finalLNout, outputProj, dLogits, dFinalLNout, dOutputProj,
                   seqLen, dim, vocab);
}

void FinalNormBackward(const float* lastOut, const float* finalNorm,
                       const float* dFinalLNout,
                       float* dLastOut, float* dFinalNorm, float* dFinalNormBias,
                       unsigned seqLen, unsigned dim, float eps)
{
    // The norm weights are shared across positions, so dFinalNorm/dFinalNormBias
    // accumulate over every row; dLastOut is per-position.
    for (unsigned s = 0; s < seqLen; ++s)
    {
        const unsigned o = s * dim;
        LayerNormBackward(lastOut + o, finalNorm, dFinalLNout + o,
                          dLastOut ? dLastOut + o : nullptr,
                          dFinalNorm, dFinalNormBias, dim, eps);
    }
}

void EmbeddingBackward(const unsigned* tokens, const float* dx,
                       float* dEmbedding, unsigned seqLen, unsigned dim)
{
    // x[s] = embedding[tokens[s]] is a gather; its adjoint is a scatter-add. Every
    // position's input gradient lands on the row of the token it read, summing when
    // a token repeats (AddInPlace is +=, so the accumulation falls out for free).
    for (unsigned s = 0; s < seqLen; ++s)
        AddInPlace(dEmbedding + (size_t)tokens[s] * dim, dx + (size_t)s * dim, dim);
}

// ─── M5: per-layer LayerNorm backward over the sequence ───────────────────────

void SeqLayerNormBackward(const float* b, const float* normW, const float* dOut,
                          float* dB, float* dNormW, float* dNormB,
                          unsigned seqLen, unsigned dim, float eps)
{
    // out[s] = LayerNorm(normW, normB, b[s]). The norm weights are shared across
    // positions, so dNormW/dNormB accumulate over every row; dB is per-position.
    for (unsigned s = 0; s < seqLen; ++s)
    {
        const unsigned o = s * dim;
        LayerNormBackward(b + o, normW, dOut + o,
                          dB ? dB + o : nullptr,
                          dNormW, dNormB, dim, eps);
    }
}

}

}

#ifdef YUKI_M4_STANDALONE
#include "YukiGradCheck.h"
#include <cstdio>
#include <random>

using namespace Urho3D::YukiMath;

static std::mt19937 g_rng(7);
static float Rn() { std::normal_distribution<float> d(0.0f, 1.0f); return d(g_rng); }

static void FFNForward(const float* h, const float* Wff1, const float* Wff2,
                       float* ff1, float* ff1g, float* hOut,
                       unsigned dim, unsigned ffDim)
{
    MatMul(h, Wff1, ff1, 1, dim, ffDim);
    for (unsigned i = 0; i < ffDim; ++i) ff1g[i] = ff1[i];
    GELU(ff1g, ffDim);
    MatMul(ff1g, Wff2, hOut, 1, ffDim, dim);
    for (unsigned i = 0; i < dim; ++i) hOut[i] += h[i];   // residual
}

int main()
{
    const unsigned dim = 4, ffDim = 6;
    std::vector<float> h(dim), Wff1(dim * ffDim), Wff2(ffDim * dim), W(dim);
    for (auto& v : h) v = Rn();
    for (auto& v : Wff1) v = Rn();
    for (auto& v : Wff2) v = Rn();
    for (auto& v : W) v = Rn();

    std::vector<float> ff1(ffDim), ff1g(ffDim), hOut(dim);
    auto loss = [&]() -> double
    {
        FFNForward(h.data(), Wff1.data(), Wff2.data(), ff1.data(), ff1g.data(), hOut.data(), dim, ffDim);
        double s = 0.0;
        for (unsigned i = 0; i < dim; ++i) s += (double)W[i] * (double)hOut[i];
        return s;
    };

    loss();   // populate ff1/ff1g at the unperturbed params for the analytic backward
    std::vector<float> dh(dim, 0.0f), dWff1(dim * ffDim, 0.0f), dWff2(ffDim * dim, 0.0f);
    FFNBackward(h.data(), Wff1.data(), Wff2.data(), ff1.data(), ff1g.data(), W.data(),
                dh.data(), dWff1.data(), dWff2.data(), dim, ffDim);

    std::printf("Yuki M4 — FFN backward, finite-difference gate:\n");
    bool ok = true;
    auto chk = [&](const char* n, float* p, const float* g, unsigned c)
    {
        GradCheckResult r = CheckGradient(loss, p, g, c);
        std::printf("  [%s] %-8s max rel err = %.3e\n", r.passed ? "PASS" : "FAIL", n, (double)r.maxRelErr);
        if (!r.passed) ok = false;
    };
    chk("dh", h.data(), dh.data(), dim);
    chk("dWff1", Wff1.data(), dWff1.data(), dim * ffDim);
    chk("dWff2", Wff2.data(), dWff2.data(), ffDim * dim);
    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
#endif

#ifdef YUKI_M6_STANDALONE
#include "YukiGradCheck.h"
#include <cstdio>
#include <random>

using namespace Urho3D::YukiMath;

static std::mt19937 g_rng6(11);
static float R6() { std::normal_distribution<float> d(0.0f, 1.0f); return d(g_rng6); }

static void AttnForward(const float* H, const float* Wq, const float* Wk, const float* Wv, const float* Wout,
                        float* Q, float* K, float* V, float* A, float* headsOut, float* Hout,
                        unsigned seqLen, unsigned dim, unsigned nHeads)
{
    MatMul(H, Wq, Q, seqLen, dim, dim);
    MatMul(H, Wk, K, seqLen, dim, dim);
    MatMul(H, Wv, V, seqLen, dim, dim);
    const unsigned headDim = dim / nHeads;
    const float scale = 1.0f / std::sqrt((float)headDim);
    for (unsigned k = 0; k < seqLen * dim; ++k) headsOut[k] = 0.0f;
    for (unsigned k = 0; k < nHeads * seqLen * seqLen; ++k) A[k] = 0.0f;
    for (unsigned hd = 0; hd < nHeads; ++hd)
    {
        const unsigned c0 = hd * headDim;
        float* Ah = A + hd * seqLen * seqLen;
        for (unsigned i = 0; i < seqLen; ++i)
        {
            const unsigned len = i + 1;
            std::vector<float> row(len);
            for (unsigned j = 0; j < len; ++j)
            {
                float dot = 0.0f;
                for (unsigned d = 0; d < headDim; ++d) dot += Q[i * dim + c0 + d] * K[j * dim + c0 + d];
                row[j] = dot * scale;
            }
            Softmax(row.data(), len);
            for (unsigned j = 0; j < len; ++j) Ah[i * seqLen + j] = row[j];
            for (unsigned d = 0; d < headDim; ++d)
            {
                float acc = 0.0f;
                for (unsigned j = 0; j < len; ++j) acc += row[j] * V[j * dim + c0 + d];
                headsOut[i * dim + c0 + d] = acc;
            }
        }
    }
    std::vector<float> out(seqLen * dim);
    MatMul(headsOut, Wout, out.data(), seqLen, dim, dim);
    for (unsigned k = 0; k < seqLen * dim; ++k) Hout[k] = H[k] + out[k];
}

int main()
{
    const unsigned seqLen = 3, dim = 4, nHeads = 2;
    std::vector<float> H(seqLen * dim), Wq(dim * dim), Wk(dim * dim), Wv(dim * dim), Wout(dim * dim), W(seqLen * dim);
    for (auto& v : H) v = R6();
    for (auto& v : Wq) v = R6();
    for (auto& v : Wk) v = R6();
    for (auto& v : Wv) v = R6();
    for (auto& v : Wout) v = R6();
    for (auto& v : W) v = R6();

    std::vector<float> Q(seqLen * dim), K(seqLen * dim), V(seqLen * dim),
        A(nHeads * seqLen * seqLen), headsOut(seqLen * dim), Hout(seqLen * dim);
    auto loss = [&]() -> double
    {
        AttnForward(H.data(), Wq.data(), Wk.data(), Wv.data(), Wout.data(),
                    Q.data(), K.data(), V.data(), A.data(), headsOut.data(), Hout.data(), seqLen, dim, nHeads);
        double s = 0.0;
        for (unsigned k = 0; k < seqLen * dim; ++k) s += (double)W[k] * (double)Hout[k];
        return s;
    };

    loss();   // populate Q/K/V/A/headsOut at the unperturbed params
    std::vector<float> dH(seqLen * dim, 0.0f), dWq(dim * dim, 0.0f), dWk(dim * dim, 0.0f),
        dWv(dim * dim, 0.0f), dWout(dim * dim, 0.0f);
    AttentionBackward(H.data(), Wq.data(), Wk.data(), Wv.data(), Wout.data(),
                      Q.data(), K.data(), V.data(), A.data(), headsOut.data(),
                      W.data(), dH.data(), dWq.data(), dWk.data(), dWv.data(), dWout.data(),
                      seqLen, dim, nHeads);

    std::printf("Yuki M6 — attention backward, finite-difference gate:\n");
    bool ok = true;
    auto chk = [&](const char* n, float* p, const float* g, unsigned c)
    {
        GradCheckResult r = CheckGradient(loss, p, g, c);
        std::printf("  [%s] %-6s max rel err = %.3e\n", r.passed ? "PASS" : "FAIL", n, (double)r.maxRelErr);
        if (!r.passed) ok = false;
    };
    chk("dH", H.data(), dH.data(), seqLen * dim);
    chk("dWq", Wq.data(), dWq.data(), dim * dim);
    chk("dWk", Wk.data(), dWk.data(), dim * dim);
    chk("dWv", Wv.data(), dWv.data(), dim * dim);
    chk("dWout", Wout.data(), dWout.data(), dim * dim);
    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
#endif

#ifdef YUKI_M3_STANDALONE
#include "YukiGradCheck.h"
#include <cstdio>
#include <random>

using namespace Urho3D::YukiMath;

static std::mt19937 g_rng3(23);
static float R3() { std::normal_distribution<float> d(0.0f, 1.0f); return d(g_rng3); }

// Head forward: embedding gather -> final LayerNorm -> output projection. This is
// exactly the non-transformer top+bottom of the net (the layers are M4/M5/M6),
// composed so each M3 weight buffer can be finite-differenced in isolation. The
// embedding feeds straight into the final norm here — no layers in between — which
// is all M3's adjoints need to be exercised end to end.
static void HeadForward(const unsigned* tokens, const float* embedding,
                        const float* finalNorm, const float* finalNormBias,
                        const float* outputProj,
                        float* x, float* finalLNout, float* logits,
                        unsigned seqLen, unsigned dim, unsigned vocab)
{
    for (unsigned s = 0; s < seqLen; ++s)
    {
        const float* erow = embedding + (size_t)tokens[s] * dim;
        for (unsigned d = 0; d < dim; ++d)
        {
            x[s * dim + d] = erow[d];
            finalLNout[s * dim + d] = erow[d];   // LayerNorm runs in-place on finalLNout
        }
        LayerNorm(finalNorm, finalNormBias, finalLNout + s * dim, dim);
    }
    MatMul(finalLNout, outputProj, logits, seqLen, dim, vocab);
}

int main()
{
    const unsigned seqLen = 3, dim = 4, vocab = 5;
    // Token 2 repeats (positions 0 and 2) — exercises the embedding scatter-add
    // accumulation onto a shared row; rows 1/3/4 stay untouched (legit zero grad).
    const unsigned tokens[seqLen] = { 2, 0, 2 };

    std::vector<float> embedding(vocab * dim), finalNorm(dim), finalNormBias(dim),
        outputProj(dim * vocab), W(seqLen * vocab);
    for (auto& v : embedding) v = R3();
    for (auto& v : finalNorm) v = R3();
    for (auto& v : finalNormBias) v = R3();
    for (auto& v : outputProj) v = R3();
    for (auto& v : W) v = R3();

    std::vector<float> x(seqLen * dim), finalLNout(seqLen * dim), logits(seqLen * vocab);
    auto loss = [&]() -> double
    {
        HeadForward(tokens, embedding.data(), finalNorm.data(), finalNormBias.data(),
                    outputProj.data(), x.data(), finalLNout.data(), logits.data(),
                    seqLen, dim, vocab);
        double s = 0.0;   // arbitrary scalar readout; d(loss)/d(logits) = W
        for (unsigned k = 0; k < seqLen * vocab; ++k) s += (double)W[k] * (double)logits[k];
        return s;
    };

    loss();   // populate x/finalLNout at the unperturbed params for the analytic pass

    std::vector<float> dEmbedding(vocab * dim, 0.0f), dFinalNorm(dim, 0.0f),
        dFinalNormBias(dim, 0.0f), dOutputProj(dim * vocab, 0.0f),
        dFinalLNout(seqLen * dim, 0.0f), dx(seqLen * dim, 0.0f);

    OutputProjBackward(finalLNout.data(), outputProj.data(), W.data(),
                       dFinalLNout.data(), dOutputProj.data(), seqLen, dim, vocab);
    FinalNormBackward(x.data(), finalNorm.data(), dFinalLNout.data(),
                      dx.data(), dFinalNorm.data(), dFinalNormBias.data(), seqLen, dim);
    EmbeddingBackward(tokens, dx.data(), dEmbedding.data(), seqLen, dim);

    std::printf("Yuki M3 — head backward (output-proj, final-LN, embedding), finite-difference gate:\n");
    bool ok = true;
    auto chk = [&](const char* n, float* p, const float* g, unsigned c)
    {
        GradCheckResult r = CheckGradient(loss, p, g, c);
        std::printf("  [%s] %-14s max rel err = %.3e\n", r.passed ? "PASS" : "FAIL", n, (double)r.maxRelErr);
        if (!r.passed) ok = false;
    };
    chk("dOutputProj", outputProj.data(), dOutputProj.data(), dim * vocab);
    chk("dFinalNorm", finalNorm.data(), dFinalNorm.data(), dim);
    chk("dFinalNormBias", finalNormBias.data(), dFinalNormBias.data(), dim);
    chk("dEmbedding", embedding.data(), dEmbedding.data(), vocab * dim);
    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
#endif

#ifdef YUKI_M5_STANDALONE
#include "YukiGradCheck.h"
#include <cstdio>
#include <random>

using namespace Urho3D::YukiMath;

static std::mt19937 g_rng5(31);
static float R5() { std::normal_distribution<float> d(0.0f, 1.0f); return d(g_rng5); }

// Per-layer post-norm forward over the sequence: out[s] = LayerNorm(normW, normB, b[s]).
static void SeqLayerNormForward(const float* b, const float* normW, const float* normB,
                                float* out, unsigned seqLen, unsigned dim)
{
    for (unsigned s = 0; s < seqLen; ++s)
    {
        for (unsigned d = 0; d < dim; ++d) out[s * dim + d] = b[s * dim + d];   // LN in-place
        LayerNorm(normW, normB, out + s * dim, dim);
    }
}

int main()
{
    const unsigned seqLen = 3, dim = 4;
    std::vector<float> b(seqLen * dim), normW(dim), normB(dim), W(seqLen * dim);
    for (auto& v : b) v = R5();
    for (auto& v : normW) v = R5();
    for (auto& v : normB) v = R5();
    for (auto& v : W) v = R5();

    std::vector<float> out(seqLen * dim);
    auto loss = [&]() -> double
    {
        SeqLayerNormForward(b.data(), normW.data(), normB.data(), out.data(), seqLen, dim);
        double s = 0.0;   // arbitrary scalar readout; d(loss)/d(out) = W
        for (unsigned k = 0; k < seqLen * dim; ++k) s += (double)W[k] * (double)out[k];
        return s;
    };

    loss();   // populate out at the unperturbed params for the analytic pass

    std::vector<float> dB(seqLen * dim, 0.0f), dNormW(dim, 0.0f), dNormB(dim, 0.0f);
    SeqLayerNormBackward(b.data(), normW.data(), W.data(),
                         dB.data(), dNormW.data(), dNormB.data(), seqLen, dim);

    std::printf("Yuki M5 — per-layer LayerNorm backward over the sequence, finite-difference gate:\n");
    bool ok = true;
    auto chk = [&](const char* n, float* p, const float* g, unsigned c)
    {
        GradCheckResult r = CheckGradient(loss, p, g, c);
        std::printf("  [%s] %-8s max rel err = %.3e\n", r.passed ? "PASS" : "FAIL", n, (double)r.maxRelErr);
        if (!r.passed) ok = false;
    };
    chk("dB", b.data(), dB.data(), seqLen * dim);          // per-position input grad
    chk("dNormW", normW.data(), dNormW.data(), dim);       // shared, summed across positions
    chk("dNormB", normB.data(), dNormB.data(), dim);       // shared, summed across positions
    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
#endif
