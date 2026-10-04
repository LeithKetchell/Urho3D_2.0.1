// YukiInference — Run the cartridge.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../ML/YukiInference.h"
#include "../ML/YukiDispatch.h"
#include "../IO/Log.h"
#include "../Core/Context.h"

#include <cmath>
#include <cstring>

#include "../DebugNew.h"

namespace Urho3D
{

YukiInference::YukiInference(Context* context) :
    Object(context)
{
}

YukiInference::~YukiInference() = default;

void YukiInference::SetModel(YukiModel* model)
{
    model_ = model;

    if (model && model->IsLoaded())
    {
        const YukiTopology& t = model->GetTopology();
        unsigned maxBuf = t.embedDim * t.maxSeqLen;
        unsigned ffBuf = t.ffDim * t.maxSeqLen;

        hiddenState_.Resize(maxBuf);
        scratch1_.Resize(Max(maxBuf, ffBuf));
        scratch2_.Resize(Max(maxBuf, ffBuf));
        logits_.Resize(t.vocabSize);
    }
}

// ─── Token Generation ────────────────────────────────────────────────────────

unsigned YukiInference::Predict(const Vector<unsigned>& tokenIndices)
{
    if (!model_ || !model_->IsLoaded() || tokenIndices.Empty())
        return 0;

    const YukiTopology& t = model_->GetTopology();
    unsigned seqLen = Min((unsigned)tokenIndices.Size(), t.maxSeqLen);
    unsigned dim = t.embedDim;

    // Embed tokens into hidden state
    const float* emb = model_->GetEmbeddingWeights();
    memset(hiddenState_.Buffer(), 0, seqLen * dim * sizeof(float));

    for (unsigned i = 0; i < seqLen; ++i)
    {
        unsigned idx = tokenIndices[i];
        if (idx < t.vocabSize)
            memcpy(hiddenState_.Buffer() + i * dim, emb + idx * dim, dim * sizeof(float));
    }

    // Forward through all layers
    for (unsigned layer = 0; layer < t.nLayers; ++layer)
        ForwardLayer(layer, hiddenState_.Buffer(), seqLen);

    // Final layer norm
    float* lastPos = hiddenState_.Buffer() + (seqLen - 1) * dim;
    LayerNorm(model_->GetFinalNormWeights(), model_->GetFinalNormBias(), lastPos, dim);

    // Output projection: [1 × dim] × [dim × vocab] → [1 × vocab]
    MatMul(lastPos, model_->GetOutputWeights(), logits_.Buffer(), 1, dim, t.vocabSize);

    // Federation P3 (additive): output-layer trigger + single-expert dispatch. AFTER logits, BEFORE the
    // pick — null dispatch is a no-op (hot path unchanged); on an organic fire it composes a secondary
    // (expert) step into logits_. Greedy stays identity when cold: argmax is over the same buffer.
    if (dispatch_)
        dispatch_->MaybeRoute(tokenIndices, logits_.Buffer(), t.vocabSize);

    // Argmax
    unsigned best = 0;
    float bestVal = logits_[0];
    for (unsigned i = 1; i < t.vocabSize; ++i)
    {
        if (logits_[i] > bestVal)
        {
            bestVal = logits_[i];
            best = i;
        }
    }

    return best;
}

Vector<unsigned> YukiInference::Generate(const Vector<unsigned>& inputTokens,
                                          unsigned maxTokens,
                                          unsigned endToken,
                                          float temperature)
{
    if (!model_ || !model_->IsLoaded())
        return {};

    const YukiTopology& t = model_->GetTopology();
    Vector<unsigned> tokens = inputTokens;

    for (unsigned step = 0; step < maxTokens; ++step)
    {
        // Keep within context window
        unsigned contextStart = 0;
        if (tokens.Size() > t.maxSeqLen)
            contextStart = tokens.Size() - t.maxSeqLen;

        Vector<unsigned> context;
        for (unsigned i = contextStart; i < tokens.Size(); ++i)
            context.Push(tokens[i]);

        // Forward pass
        unsigned seqLen = context.Size();
        unsigned dim = t.embedDim;

        const float* emb = model_->GetEmbeddingWeights();
        memset(hiddenState_.Buffer(), 0, seqLen * dim * sizeof(float));

        for (unsigned i = 0; i < seqLen; ++i)
        {
            unsigned idx = context[i];
            if (idx < t.vocabSize)
                memcpy(hiddenState_.Buffer() + i * dim, emb + idx * dim, dim * sizeof(float));
        }

        for (unsigned layer = 0; layer < t.nLayers; ++layer)
            ForwardLayer(layer, hiddenState_.Buffer(), seqLen);

        float* lastPos = hiddenState_.Buffer() + (seqLen - 1) * dim;
        LayerNorm(model_->GetFinalNormWeights(), model_->GetFinalNormBias(), lastPos, dim);
        MatMul(lastPos, model_->GetOutputWeights(), logits_.Buffer(), 1, dim, t.vocabSize);

        // Federation P3 (additive): output-layer trigger + single-expert dispatch. AFTER logits, BEFORE
        // sampling — null dispatch is a no-op; on an organic fire it composes the expert's secondary step
        // into logits_, which SampleLogits then draws from. Uses the same context window as the core step.
        if (dispatch_)
            dispatch_->MaybeRoute(context, logits_.Buffer(), t.vocabSize);

        // Sample with temperature
        unsigned nextToken = SampleLogits(logits_.Buffer(), t.vocabSize, temperature);
        tokens.Push(nextToken);

        if (nextToken == endToken)
            break;
    }

    return tokens;
}

String YukiInference::GenerateText(const String& input, unsigned maxTokens)
{
    Vector<unsigned> inputTokens = Tokenize(input);
    if (inputTokens.Empty())
        return String::EMPTY;

    Vector<unsigned> output = Generate(inputTokens, maxTokens, 0, 0.8f);

    // Return only the generated part
    Vector<unsigned> generated;
    for (unsigned i = inputTokens.Size(); i < output.Size(); ++i)
        generated.Push(output[i]);

    return Detokenize(generated);
}

// ─── Layer Operations ────────────────────────────────────────────────────────

void YukiInference::ForwardLayer(unsigned layerIdx, float* hidden, unsigned seqLen)
{
    const YukiLayerWeights& lw = model_->GetLayerWeights(layerIdx);

    // Self-attention + residual
    Attention(lw, hidden, seqLen);

    // Feedforward + residual
    FeedForward(lw, hidden, seqLen);

    // Layer norm
    unsigned dim = model_->GetTopology().embedDim;
    for (unsigned i = 0; i < seqLen; ++i)
        LayerNorm(lw.normWeights, lw.normBias, hidden + i * dim, dim);
}

void YukiInference::Attention(const YukiLayerWeights& lw, float* hidden, unsigned seqLen)
{
    unsigned dim = model_->GetTopology().embedDim;
    unsigned nHeads = model_->GetTopology().nHeads;
    unsigned headDim = dim / nHeads;

    // Q = hidden × Wq, K = hidden × Wk, V = hidden × Wv
    float* Q = scratch1_.Buffer();
    float* K = scratch2_.Buffer();

    MatMul(hidden, lw.qWeights, Q, seqLen, dim, dim);
    MatMul(hidden, lw.kWeights, K, seqLen, dim, dim);

    // V into a temp region of scratch2 after K
    float* V = scratch2_.Buffer() + seqLen * dim;
    MatMul(hidden, lw.vWeights, V, seqLen, dim, dim);

    // Scaled dot-product attention per head
    float scale = 1.0f / sqrtf((float)headDim);

    for (unsigned h = 0; h < nHeads; ++h)
    {
        for (unsigned i = 0; i < seqLen; ++i)
        {
            // Compute attention scores for position i, head h
            float scores[512];  // Max seq len
            for (unsigned j = 0; j <= i; ++j)  // Causal mask
            {
                float dot = 0.0f;
                for (unsigned d = 0; d < headDim; ++d)
                    dot += Q[i * dim + h * headDim + d] * K[j * dim + h * headDim + d];
                scores[j] = dot * scale;
            }

            // Softmax over scores[0..i]
            Softmax(scores, i + 1);

            // Weighted sum of V
            for (unsigned d = 0; d < headDim; ++d)
            {
                float sum = 0.0f;
                for (unsigned j = 0; j <= i; ++j)
                    sum += scores[j] * V[j * dim + h * headDim + d];
                // Write to Q as temp output
                Q[i * dim + h * headDim + d] = sum;
            }
        }
    }

    // Output projection + residual
    float* attnOut = scratch2_.Buffer();
    MatMul(Q, lw.outWeights, attnOut, seqLen, dim, dim);

    for (unsigned i = 0; i < seqLen * dim; ++i)
        hidden[i] += attnOut[i];
}

void YukiInference::FeedForward(const YukiLayerWeights& lw, float* hidden, unsigned seqLen)
{
    unsigned dim = model_->GetTopology().embedDim;
    unsigned ffDim = model_->GetTopology().ffDim;

    float* ff1Out = scratch1_.Buffer();
    float* ff2Out = scratch2_.Buffer();

    // FF1: [seqLen × dim] × [dim × ffDim] → [seqLen × ffDim]
    MatMul(hidden, lw.ff1Weights, ff1Out, seqLen, dim, ffDim);
    GELU(ff1Out, seqLen * ffDim);

    // FF2: [seqLen × ffDim] × [ffDim × dim] → [seqLen × dim]
    MatMul(ff1Out, lw.ff2Weights, ff2Out, seqLen, ffDim, dim);

    // Residual
    for (unsigned i = 0; i < seqLen * dim; ++i)
        hidden[i] += ff2Out[i];
}

// ─── Math Primitives ─────────────────────────────────────────────────────────

void YukiInference::MatMul(const float* A, const float* B, float* C,
                            unsigned m, unsigned k, unsigned n)
{
    // Naive matmul — correct first, fast later (Vulkan compute)
    for (unsigned i = 0; i < m; ++i)
    {
        for (unsigned j = 0; j < n; ++j)
        {
            float sum = 0.0f;
            for (unsigned p = 0; p < k; ++p)
                sum += A[i * k + p] * B[p * n + j];
            C[i * n + j] = sum;
        }
    }
}

void YukiInference::LayerNorm(const float* weights, const float* bias,
                               float* data, unsigned dim)
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

    float invStd = 1.0f / sqrtf(var + 1e-5f);

    for (unsigned i = 0; i < dim; ++i)
        data[i] = weights[i] * (data[i] - mean) * invStd + bias[i];
}

void YukiInference::Softmax(float* data, unsigned len)
{
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

void YukiInference::GELU(float* data, unsigned len)
{
    // Approximate GELU: x * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
    const float sqrt2pi = 0.7978845608f;
    for (unsigned i = 0; i < len; ++i)
    {
        float x = data[i];
        float cube = x * x * x;
        data[i] = 0.5f * x * (1.0f + tanhf(sqrt2pi * (x + 0.044715f * cube)));
    }
}

unsigned YukiInference::SampleLogits(const float* logits, unsigned vocabSize, float temperature)
{
    if (temperature <= 0.0f)
    {
        // Greedy
        unsigned best = 0;
        for (unsigned i = 1; i < vocabSize; ++i)
            if (logits[i] > logits[best]) best = i;
        return best;
    }

    // Apply temperature
    Vector<float> scaled(vocabSize);
    for (unsigned i = 0; i < vocabSize; ++i)
        scaled[i] = logits[i] / temperature;

    Softmax(scaled.Buffer(), vocabSize);

    // Random sample from distribution
    // Simple LCG
    static unsigned rng = 42;
    rng = rng * 1103515245 + 12345;
    float r = (float)(rng & 0x7FFFFFFF) / (float)0x7FFFFFFF;

    float cumulative = 0.0f;
    for (unsigned i = 0; i < vocabSize; ++i)
    {
        cumulative += scaled[i];
        if (r <= cumulative)
            return i;
    }

    return vocabSize - 1;
}

// ─── Tokenizer ───────────────────────────────────────────────────────────────

Vector<unsigned> YukiInference::Tokenize(const String& text)
{
    Vector<unsigned> tokens;
    if (!model_ || !model_->IsLoaded())
        return tokens;

    // Canonical shared tokenizer (code-primary: case-preserving, multi-char
    // operators, numeric literals, indent/newline) — same tokens as
    // training/vocab-build.
    Vector<String> words = YukiModel::Tokenize(text);
    const unsigned vocabSize = model_->GetTopology().vocabSize;
    for (const String& word : words)
    {
        const unsigned idx = model_->GetTokenIndex(word);
        if (idx < vocabSize)
        {
            tokens.Push(idx);
            continue;
        }
        // OOV fallback: decompose to individual characters rather than
        // dropping the whole token. Matters far more here than it did for the
        // old prose-first tokenizer — an unseen identifier is the normal case
        // for code, not the exception, so silently vanishing it would gut the
        // model's view of what it's operating on. Only characters truly absent
        // from vocab (never seen at vocab-build time) still get dropped.
        for (unsigned k = 0; k < word.Length(); ++k)
        {
            const unsigned cidx = model_->GetTokenIndex(String(word[k], 1));
            if (cidx < vocabSize)
                tokens.Push(cidx);
        }
    }

    return tokens;
}

String YukiInference::Detokenize(const Vector<unsigned>& tokens)
{
    if (!model_ || !model_->IsLoaded())
        return String::EMPTY;

    String result;
    for (unsigned i = 0; i < tokens.Size(); ++i)
    {
        if (i > 0)
            result += " ";
        result += model_->GetToken(tokens[i]);
    }

    return result;
}

}
