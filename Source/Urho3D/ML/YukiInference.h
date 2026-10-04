// YukiInference — Run the cartridge. CPU and Vulkan compute.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Feed tokens in, get tokens out. The model is the cartridge.
// CPU fallback always present. Vulkan compute when available.

#pragma once

#include "../Core/Object.h"
#include "../ML/YukiModel.h"

namespace Urho3D
{

class YukiDispatch;

/// Inference engine — runs a loaded YukiModel.
class URHO3D_API YukiInference : public Object
{
    URHO3D_OBJECT(YukiInference, Object);

public:
    explicit YukiInference(Context* context);
    ~YukiInference() override;

    /// Attach a loaded model.
    void SetModel(YukiModel* model);

    /// Federation P3 (additive): attach an OPTIONAL output-layer trigger/dispatch. Null (default) = the
    /// federation is off and the hot path is byte-identical. Borrowed pointer — NOT owned here; the owner
    /// (Yuki) keeps it alive. The core's own inference context leaves this null; only the CORE context is
    /// given a dispatch, never an expert's context (that keeps recursion structurally impossible).
    void SetDispatch(YukiDispatch* dispatch) { dispatch_ = dispatch; }

    /// Read-only view of the logits from the most recent Predict()/Generate() step. Additive accessor —
    /// the output head is untouched. The dispatch reads a pinned expert's logits through this after
    /// running the expert's own forward pass. Sized to vocabSize once a model is attached.
    const Vector<float>& GetLogits() const { return logits_; }

    /// Generate the next token given input token indices.
    /// Returns the predicted token index.
    unsigned Predict(const Vector<unsigned>& tokenIndices);

    /// Generate a sequence of tokens from input tokens.
    /// Stops at maxTokens or when endToken is produced.
    Vector<unsigned> Generate(const Vector<unsigned>& inputTokens,
                              unsigned maxTokens = 128,
                              unsigned endToken = 0,
                              float temperature = 0.8f);

    /// Convenience: tokenize string, generate, detokenize.
    String GenerateText(const String& input, unsigned maxTokens = 128);

private:
    /// CPU forward pass through the full network.
    void Forward(const float* input, unsigned seqLen, float* output);

    /// Per-layer forward pass.
    void ForwardLayer(unsigned layerIdx, float* hidden, unsigned seqLen);

    /// Attention mechanism.
    void Attention(const YukiLayerWeights& lw, float* hidden, unsigned seqLen);

    /// Feedforward network.
    void FeedForward(const YukiLayerWeights& lw, float* hidden, unsigned seqLen);

    /// Layer normalization.
    void LayerNorm(const float* weights, const float* bias, float* data, unsigned dim);

    /// Matrix multiply: C[m×n] = A[m×k] × B[k×n]
    void MatMul(const float* A, const float* B, float* C,
                unsigned m, unsigned k, unsigned n);

    /// Softmax in-place.
    void Softmax(float* data, unsigned len);

    /// GELU activation in-place.
    void GELU(float* data, unsigned len);

    /// Sample from logits with temperature.
    unsigned SampleLogits(const float* logits, unsigned vocabSize, float temperature);

    /// Tokenize via the canonical YukiModel::Tokenize, mapped to vocab ids.
    /// OOV words fall back to per-character ids rather than being dropped.
    Vector<unsigned> Tokenize(const String& text);
    /// Detokenize — join tokens with spaces.
    String Detokenize(const Vector<unsigned>& tokens);

    WeakPtr<YukiModel> model_;

    /// Federation P3 (additive, optional): output-layer trigger/dispatch. Null = federation off (hot path
    /// unchanged). Borrowed — not owned here.
    YukiDispatch* dispatch_{};

    /// Scratch buffers — allocated once, reused.
    Vector<float> hiddenState_;
    Vector<float> scratch1_;
    Vector<float> scratch2_;
    Vector<float> logits_;
};

}
