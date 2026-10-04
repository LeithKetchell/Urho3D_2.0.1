// YukiGradCheck — Finite-difference gradient checker (implementation).
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Implements the contract in YukiGradCheck.h: CheckGradient is the reusable
// central-difference primitive that every Yuki backward pass (M3-M8) must clear,
// and RunYukiGradCheckSelfTest exercises the M0 math library so the foundation
// itself is proven, not assumed.
//
// Standalone verification (no engine): compile with -DYUKI_GC_STANDALONE so the
// PASS/FAIL lines go to stdout instead of the engine Log.

#include "../Precompiled.h"
#include "YukiGradCheck.h"
#include "YukiMath.h"
#ifndef YUKI_GC_STANDALONE
#include "../IO/Log.h"
#endif

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

GradCheckResult CheckGradient(const std::function<double()>& loss,
                              float* params, const float* analyticGrad,
                              unsigned count, float eps, float tol, float atol)
{
    GradCheckResult r;
    r.maxRelErr = 0.0f;
    r.worstIndex = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        const float orig = params[i];
        params[i] = orig + eps;
        const double lossPlus = loss();
        params[i] = orig - eps;
        const double lossMinus = loss();
        params[i] = orig;                                   // restore exactly

        const double numGrad = (lossPlus - lossMinus) / (2.0 * (double)eps);
        const double ana = (double)analyticGrad[i];
        const double absErr = fabs(numGrad - ana);
        // Near-zero gradient: relative error is ill-conditioned (finite-diff noise
        // on a true zero reads as rel-err 1.0). Judge by absolute error instead.
        if (absErr < (double)atol)
            continue;
        const double denom = fmax(1e-5, fabs(numGrad) + fabs(ana));
        const float rel = (float)(absErr / denom);
        if (rel > r.maxRelErr)
        {
            r.maxRelErr = rel;
            r.worstIndex = i;
        }
    }
    r.passed = r.maxRelErr < tol;
    return r;
}

// ─── Self-test: drive each M0 primitive through CheckGradient ─────────────────

namespace
{
    std::mt19937 g_rng(1234);                               // fixed seed -> deterministic
    float Randn() { std::normal_distribution<float> d(0.0f, 1.0f); return d(g_rng); }

    void LogCheck(const char* name, const GradCheckResult& r)
    {
#ifdef YUKI_GC_STANDALONE
        std::printf("  [%s] %-24s max rel err = %.3e\n",
                    r.passed ? "PASS" : "FAIL", name, (double)r.maxRelErr);
#else
        URHO3D_LOGINFOF("YukiGradCheck [%s] %-24s max rel err = %e",
                        r.passed ? "PASS" : "FAIL", name, r.maxRelErr);
#endif
    }
}

bool RunYukiGradCheckSelfTest()
{
    bool allPass = true;
    auto record = [&](const char* name, const GradCheckResult& r)
    {
        LogCheck(name, r);
        if (!r.passed)
            allPass = false;
    };

    // ── MatMul: C[m×n] = A[m×k]·B[k×n]; loss = Σ(W ⊙ C), so dC = W ──────────────
    {
        const unsigned m = 3, k = 4, n = 2;
        std::vector<float> A(m * k), B(k * n), W(m * n), C(m * n);
        for (auto& v : A) v = Randn();
        for (auto& v : B) v = Randn();
        for (auto& v : W) v = Randn();
        auto loss = [&]() -> double
        {
            MatMul(A.data(), B.data(), C.data(), m, k, n);
            double s = 0.0;
            for (unsigned i = 0; i < m * n; ++i) s += (double)W[i] * (double)C[i];
            return s;
        };
        // analytic: dA = W·Bᵀ, dB = Aᵀ·W  (dC == W)
        std::vector<float> dA(m * k, 0.0f), dB(k * n, 0.0f);
        MatMulBackward(A.data(), B.data(), W.data(), dA.data(), dB.data(), m, k, n);
        record("MatMul dA", CheckGradient(loss, A.data(), dA.data(), m * k));
        record("MatMul dB", CheckGradient(loss, B.data(), dB.data(), k * n));
    }

    // ── GELU: y = gelu(x); loss = Σ(W ⊙ y) ─────────────────────────────────────
    {
        const unsigned len = 6;
        std::vector<float> x(len), W(len), y(len);
        for (auto& v : x) v = Randn();
        for (auto& v : W) v = Randn();
        auto loss = [&]() -> double
        {
            for (unsigned i = 0; i < len; ++i) y[i] = x[i];
            GELU(y.data(), len);
            double s = 0.0;
            for (unsigned i = 0; i < len; ++i) s += (double)W[i] * (double)y[i];
            return s;
        };
        std::vector<float> dx(len, 0.0f);
        GELUBackward(x.data(), W.data(), dx.data(), len);   // dy == W
        record("GELU dx", CheckGradient(loss, x.data(), dx.data(), len));
    }

    // ── LayerNorm: y = norm(x; w, b); loss = Σ(g ⊙ y) ──────────────────────────
    {
        const unsigned dim = 5;
        std::vector<float> x(dim), w(dim), b(dim), g(dim), y(dim);
        for (auto& v : x) v = Randn();
        for (auto& v : w) v = Randn() * 0.5f + 1.0f;
        for (auto& v : b) v = Randn() * 0.1f;
        for (auto& v : g) v = Randn();
        auto loss = [&]() -> double
        {
            for (unsigned i = 0; i < dim; ++i) y[i] = x[i];
            LayerNorm(w.data(), b.data(), y.data(), dim);
            double s = 0.0;
            for (unsigned i = 0; i < dim; ++i) s += (double)g[i] * (double)y[i];
            return s;
        };
        std::vector<float> dx(dim, 0.0f), dw(dim, 0.0f), db(dim, 0.0f);
        LayerNormBackward(x.data(), w.data(), g.data(), dx.data(), dw.data(), db.data(), dim);
        record("LayerNorm dx", CheckGradient(loss, x.data(), dx.data(), dim));
        record("LayerNorm dw", CheckGradient(loss, w.data(), dw.data(), dim));
        record("LayerNorm db", CheckGradient(loss, b.data(), db.data(), dim));
    }

    // ── Softmax + cross-entropy: loss = CE(softmax(logits), target) ─────────────
    {
        const unsigned vocab = 7, target = 3;
        std::vector<float> logits(vocab), probs(vocab);
        for (auto& v : logits) v = Randn();
        auto loss = [&]() -> double
        {
            return (double)SoftmaxCrossEntropy(logits.data(), vocab, target, probs.data());
        };
        loss();                                             // populate probs for the analytic grad
        std::vector<float> dLogits(vocab, 0.0f);
        CrossEntropyGradient(probs.data(), vocab, target, dLogits.data());
        record("SoftmaxCE dlogits", CheckGradient(loss, logits.data(), dLogits.data(), vocab));
    }

    return allPass;
}

}

}

#ifdef YUKI_GC_STANDALONE
int main()
{
    std::printf("YukiGradCheck M1 — self-test via reusable CheckGradient:\n");
    const bool ok = Urho3D::YukiMath::RunYukiGradCheckSelfTest();
    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES PRESENT");
    return ok ? 0 : 1;
}
#endif
