// PagedFloatBuffer — a logically-flat, 64-bit-addressable float buffer.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Backs the Yuki weight / gradient / Adam-moment buffers when a model exceeds the
// 2^31-element ceiling of a single Urho3D Vector<float>. Physically a
// Vector<Vector<float>> whose page boundaries are TENSOR-ALIGNED (see YukiPaging.h):
// every weight tensor lives wholly inside one page, so Span() hands back a
// contiguous float* per tensor and the training kernels are untouched.
//
// Two identically-Configure(...)d buffers have identical page directories, so the
// gradient reduce and the Adam step walk them page-for-page in lockstep.

#pragma once

#include "../Container/Vector.h"
#include "../ML/YukiPaging.h"

namespace Urho3D
{

// View structs (full defs in YukiForwardCache.h / YukiTrainAdapter.h). Forward-declared
// here so PagedFloatBuffer.h doesn't drag the ML view headers into every includer; the
// paged view builders are defined alongside the impl.
namespace YukiMath
{
    struct YukiDims;
    struct YukiModelPtrs;
    struct YukiLayerPtrs;
    struct YukiModelGrads;
    struct YukiLayerGrads;
}

/// 64-bit-addressable float store, paged with tensor-aligned boundaries.
class URHO3D_API PagedFloatBuffer
{
public:
    PagedFloatBuffer() = default;

    /// Allocate + zero pages for a topology. Page boundaries are tensor-aligned so no
    /// weight tensor is ever split. Returns false (and Clear()s) if a single tensor
    /// exceeds the i32 page ceiling — that needs intra-tensor paging, out of scope.
    bool Configure(unsigned embedDim, unsigned nLayers, unsigned ffDim, unsigned vocabSize,
                   unsigned strideElems = YukiMath::YUKI_PAGE_STRIDE);

    /// Release all pages.
    void Clear();

    bool IsConfigured() const { return size_ != 0; }
    /// Total float count across all pages (64-bit — this is the point of the class).
    unsigned long long Size() const { return size_; }

    unsigned PageCount() const { return pages_.Size(); }
    float* PageData(unsigned p) { return pages_[p].Buffer(); }
    const float* PageData(unsigned p) const { return pages_[p].Buffer(); }
    unsigned PageElems(unsigned p) const { return dir_[p].elems; }
    unsigned long long PageBase(unsigned p) const { return dir_[p].base; }

    /// Contiguous float* for a tensor known to live wholly within one page (asserted).
    /// This is the accessor the adapter uses to build per-tensor views.
    float* Span(unsigned long long offset, unsigned long long len);
    const float* Span(unsigned long long offset, unsigned long long len) const;

    /// Zero every page.
    void Zero();

    /// Copy contents from an identically-configured buffer (page directories must match).
    void CopyFrom(const PagedFloatBuffer& src);

    /// Fill the pages from a flat, contiguous buffer of exactly Size() floats laid out
    /// in the same logical order (the model store is flat until it is paged too).
    void CopyFromFlat(const float* src);
    /// Write the pages out to a flat, contiguous buffer of exactly Size() floats.
    void CopyToFlat(float* dst) const;
    /// True if `other` has the same layout AND identical contents (checkpoint dedup).
    bool ContentEquals(const PagedFloatBuffer& other) const;

    /// Scalar element access — for cold/debug paths only, never hot loops.
    float& At(unsigned long long idx);
    float At(unsigned long long idx) const;

    /// True if `other` has the same page directory (so page-parallel ops are valid).
    bool SameLayout(const PagedFloatBuffer& other) const;

    /// Build per-tensor weight views (const) into the paged spans — the paged analogue
    /// of YukiMath::BuildModelPtrs. Sets w.layers = layerOut. Each tensor is wholly
    /// within one page (tensor-aligned), so every view is one contiguous float*.
    void BuildModelPtrs(const YukiMath::YukiDims& dims, YukiMath::YukiModelPtrs& w,
                        YukiMath::YukiLayerPtrs* layerOut) const;
    /// Build per-tensor gradient views (mutable) — paged analogue of BuildModelGrads.
    void BuildModelGrads(const YukiMath::YukiDims& dims, YukiMath::YukiModelGrads& g,
                         YukiMath::YukiLayerGrads* layerOut);

    /// Single-page fast pointer, or nullptr if multi-page (contiguous-only paths).
    float* ContiguousData() { return pages_.Size() == 1 ? pages_[0].Buffer() : nullptr; }
    const float* ContiguousData() const { return pages_.Size() == 1 ? pages_[0].Buffer() : nullptr; }

private:
    Vector<Vector<float> > pages_;        ///< The float storage, one Vector per page.
    Vector<YukiMath::YukiPage> dir_;      ///< Page directory (base + elems), parallel to pages_.
    unsigned long long size_{};           ///< Total element count.
};

}
