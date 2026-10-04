// YukiMath — Differentiable math primitives for training.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiMath.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "../DebugNew.h"

namespace Urho3D
{

namespace YukiMath
{

// GELU tanh-approximation constants — must match the forward pass exactly so the
// backward derivative is the true adjoint of what the network computed.
static const float GELU_C = 0.7978845608f;   // sqrt(2/pi)
static const float GELU_A = 0.044715f;

// ─── Forward primitives ──────────────────────────────────────────────────────

void MatMul(const float* A, const float* B, float* C,
            unsigned m, unsigned k, unsigned n)
{
    // ikj order: the inner loop runs contiguously over j in BOTH B (B[p*n+j]) and C (C[i*n+j]),
    // so it streams cache lines and auto-vectorizes (a SAXPY). The naive ijp order walked column
    // j of B with stride n — for the S×D×V output projection (n=vocab≈27822) that stride is ~111 KB,
    // a cache miss every iteration (the 16s forward Yuki-CPU bottleneck).
    // BIT-IDENTICAL to the old code: for each (i,j) the accumulation over the contraction index p
    // still happens in increasing-p order into a single accumulator (C[i*n+j], zeroed first).
    for (unsigned i = 0; i < m; ++i)
    {
        float* Crow = C + i * n;
        for (unsigned j = 0; j < n; ++j)
            Crow[j] = 0.0f;
        for (unsigned p = 0; p < k; ++p)
        {
            const float a = A[i * k + p];
            const float* Brow = B + p * n;
            for (unsigned j = 0; j < n; ++j)
                Crow[j] += a * Brow[j];
        }
    }
}

void Softmax(float* data, unsigned len)
{
    if (len == 0)
        return;

    float maxVal = data[0];
    for (unsigned i = 1; i < len; ++i)
        if (data[i] > maxVal) maxVal = data[i];

    float sum = 0.0f;
    for (unsigned i = 0; i < len; ++i)
    {
        data[i] = expf(data[i] - maxVal);
        sum += data[i];
    }

    float invSum = 1.0f / sum;
    for (unsigned i = 0; i < len; ++i)
        data[i] *= invSum;
}

void GELU(float* data, unsigned len)
{
    for (unsigned i = 0; i < len; ++i)
    {
        float x = data[i];
        float cube = x * x * x;
        data[i] = 0.5f * x * (1.0f + tanhf(GELU_C * (x + GELU_A * cube)));
    }
}

void LayerNorm(const float* weights, const float* bias,
               float* data, unsigned dim, float eps)
{
    float mean = 0.0f;
    for (unsigned i = 0; i < dim; ++i)
        mean += data[i];
    mean /= (float)dim;

    float var = 0.0f;
    for (unsigned i = 0; i < dim; ++i)
    {
        float d = data[i] - mean;
        var += d * d;
    }
    var /= (float)dim;

    float invStd = 1.0f / sqrtf(var + eps);

    for (unsigned i = 0; i < dim; ++i)
        data[i] = weights[i] * (data[i] - mean) * invStd + bias[i];
}

// ─── Backward primitives ─────────────────────────────────────────────────────

void MatMulBackward(const float* A, const float* B, const float* dC,
                    float* dA, float* dB,
                    unsigned m, unsigned k, unsigned n)
{
    // dA = dC · Bᵀ  →  dA[i,p] += Σⱼ dC[i,j] · B[p,j]
    if (dA)
    {
        for (unsigned i = 0; i < m; ++i)
        {
            for (unsigned p = 0; p < k; ++p)
            {
                float sum = 0.0f;
                for (unsigned j = 0; j < n; ++j)
                    sum += dC[i * n + j] * B[p * n + j];
                dA[i * k + p] += sum;
            }
        }
    }

    // dB = Aᵀ · dC  →  dB[p,j] += Σᵢ A[i,p] · dC[i,j]
    // The naive p,j,i order strided over i in BOTH A (A[i*k+p]) and dC (dC[i*n+j]) — a cache miss
    // per iteration, the ~13s OutputProjBackward bottleneck at n=vocab. Reorder so the inner loop
    // streams contiguously over j: accumulate each output row in i-order into a scratch row `acc`
    // (contiguous dC[i*n+j] and acc[j]), then fold it into dB once. BIT-IDENTICAL — for each (p,j)
    // acc[j] sums the i terms in the SAME increasing-i order the old `sum` did, starting from 0,
    // and the single `dB[p*n+j] += acc[j]` matches the old single `dB[p*n+j] += sum`.
    if (dB)
    {
        std::vector<float> acc(n);
        for (unsigned p = 0; p < k; ++p)
        {
            std::memset(acc.data(), 0, n * sizeof(float));
            for (unsigned i = 0; i < m; ++i)
            {
                const float a = A[i * k + p];
                const float* dCrow = dC + i * n;
                for (unsigned j = 0; j < n; ++j)
                    acc[j] += a * dCrow[j];
            }
            float* dBrow = dB + p * n;
            for (unsigned j = 0; j < n; ++j)
                dBrow[j] += acc[j];
        }
    }
}

void SoftmaxBackward(const float* y, const float* dy, float* dx, unsigned len)
{
    // dx_i = y_i · (dy_i − Σⱼ dyⱼ·yⱼ)
    float dot = 0.0f;
    for (unsigned i = 0; i < len; ++i)
        dot += dy[i] * y[i];

    for (unsigned i = 0; i < len; ++i)
        dx[i] += y[i] * (dy[i] - dot);
}

void GELUBackward(const float* x, const float* dy, float* dx, unsigned len)
{
    // g(x)  = 0.5·x·(1 + tanh(u)),  u = C·(x + A·x³)
    // g'(x) = 0.5·(1 + t) + 0.5·x·(1 − t²)·u',   t = tanh(u),  u' = C·(1 + 3A·x²)
    for (unsigned i = 0; i < len; ++i)
    {
        float xi = x[i];
        float u = GELU_C * (xi + GELU_A * xi * xi * xi);
        float t = tanhf(u);
        float du = GELU_C * (1.0f + 3.0f * GELU_A * xi * xi);
        float grad = 0.5f * (1.0f + t) + 0.5f * xi * (1.0f - t * t) * du;
        dx[i] += dy[i] * grad;
    }
}

void LayerNormBackward(const float* x, const float* w, const float* dy,
                       float* dx, float* dw, float* db,
                       unsigned dim, float eps)
{
    // Recompute forward statistics.
    float mean = 0.0f;
    for (unsigned i = 0; i < dim; ++i)
        mean += x[i];
    mean /= (float)dim;

    float var = 0.0f;
    for (unsigned i = 0; i < dim; ++i)
    {
        float d = x[i] - mean;
        var += d * d;
    }
    var /= (float)dim;

    float invStd = 1.0f / sqrtf(var + eps);

    // Parameter gradients: db += dy, dw += dy·xhat.
    if (db)
    {
        for (unsigned i = 0; i < dim; ++i)
            db[i] += dy[i];
    }
    if (dw)
    {
        for (unsigned i = 0; i < dim; ++i)
            dw[i] += dy[i] * (x[i] - mean) * invStd;
    }

    // Input gradient:
    //   g_i = dy_i · w_i
    //   dx_i = invStd/N · (N·g_i − Σⱼgⱼ − xhat_i · Σⱼ(gⱼ·xhatⱼ))
    if (dx)
    {
        float sumG = 0.0f;
        float sumGxhat = 0.0f;
        for (unsigned i = 0; i < dim; ++i)
        {
            float g = dy[i] * w[i];
            float xhat = (x[i] - mean) * invStd;
            sumG += g;
            sumGxhat += g * xhat;
        }

        float invN = 1.0f / (float)dim;
        for (unsigned i = 0; i < dim; ++i)
        {
            float g = dy[i] * w[i];
            float xhat = (x[i] - mean) * invStd;
            dx[i] += invStd * invN * ((float)dim * g - sumG - xhat * sumGxhat);
        }
    }
}

// ─── Loss ────────────────────────────────────────────────────────────────────

float SoftmaxCrossEntropy(const float* logits, unsigned vocab,
                          unsigned target, float* probs)
{
    float maxLogit = logits[0];
    for (unsigned v = 1; v < vocab; ++v)
        if (logits[v] > maxLogit) maxLogit = logits[v];

    float sum = 0.0f;
    for (unsigned v = 0; v < vocab; ++v)
    {
        probs[v] = expf(logits[v] - maxLogit);
        sum += probs[v];
    }

    float invSum = 1.0f / sum;
    for (unsigned v = 0; v < vocab; ++v)
        probs[v] *= invSum;

    float p = probs[target];
    if (p < 1e-10f) p = 1e-10f;
    return -logf(p);
}

void CrossEntropyGradient(const float* probs, unsigned vocab,
                          unsigned target, float* dLogits)
{
    for (unsigned v = 0; v < vocab; ++v)
        dLogits[v] += probs[v] - ((v == target) ? 1.0f : 0.0f);
}

// ─── Elementwise helpers ─────────────────────────────────────────────────────

void AddInPlace(float* dst, const float* src, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        dst[i] += src[i];
}

void AddScaled(float* dst, const float* src, float alpha, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        dst[i] += alpha * src[i];
}

}

}
