// YukiPaging — pure page-planning for 64-bit-addressable weight buffers.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Urho3D's Vector indexes with i32, so a single flat Vector<float> cannot hold more
// than 2^31-1 elements. A Yuki model large enough to exceed that ceiling stores its
// weight/gradient/Adam buffers as a set of PAGES instead — a Vector<Vector<float>>,
// 64-bit-addressable across pages while each page stays a normal i32-indexed Vector.
//
// The one invariant that keeps the hot-path math untouched: page boundaries are
// TENSOR-ALIGNED. No single weight tensor (a Q/K/V/Out/FF/norm/embedding block) is
// ever split across two pages, so every per-tensor view the training kernels consume
// is still one contiguous float* — the forward/backward/loss code never changes.
//
// This header is PURE (no engine dependency, only scalars) so the packing math is
// verifiable standalone with g++ (-DYUKI_PAGING_STANDALONE gate in
// PagedFloatBuffer.cpp), exactly like the M8 adapter tiling gate.

#pragma once

namespace Urho3D
{

namespace YukiMath
{

/// One page in a paged float buffer: a contiguous run of `elems` floats whose first
/// element sits at flat element offset `base`. `elems` is i32-safe by construction.
struct YukiPage
{
    unsigned long long base;   ///< Flat element offset of this page's first element.
    unsigned           elems;  ///< Float count in this page (<= 2^31-1).
};

/// Default page stride: 2^26 floats = 64M elements = 256 MB. This is the TARGET size
/// for packing small tensors together; a single tensor larger than the stride gets
/// its own (larger) page — the stride is a packing hint, not a hard cap. Chosen so a
/// page maps 1:1 onto a GPU storage buffer under typical maxStorageBufferRange.
static const unsigned YUKI_PAGE_STRIDE = 1u << 26;

/// Hard per-page (and per-tensor) ceiling: Urho3D's Vector indexes with i32, so no
/// single page can exceed INT_MAX elements. A lone tensor above this needs
/// intra-tensor paging + a page-aware matmul — out of scope; callers refuse loudly.
static const unsigned long long YUKI_PAGE_MAX = 0x7fffffffULL;

/// Number of distinct weight tensors in a topology: embedding + 8 per layer
/// (q,k,v,o,ff1,ff2,norm,normBias) + finalNorm + finalNormBias + outputProj.
inline unsigned YukiTensorCount(unsigned nLayers)
{
    return 1u + 8u * nLayers + 3u;
}

/// Enumerate the ordered per-tensor element lengths for a topology, in the EXACT
/// order YukiTrainAdapter::BuildModelPtrs tiles them. Writes up to `maxOut` lengths;
/// returns the count actually written (== YukiTensorCount(nLayers)), or 0 if the
/// caller's buffer is too small. Pure 64-bit arithmetic — no intermediate overflow.
inline unsigned YukiEnumerateTensors(unsigned embedDim, unsigned nLayers, unsigned ffDim,
                                     unsigned vocabSize,
                                     unsigned long long* outLens, unsigned maxOut)
{
    const unsigned need = YukiTensorCount(nLayers);
    if (!outLens || maxOut < need)
        return 0;

    const unsigned long long dd = (unsigned long long)embedDim * embedDim;
    const unsigned long long df = (unsigned long long)embedDim * ffDim;
    const unsigned long long fd = (unsigned long long)ffDim * embedDim;
    const unsigned long long ed = embedDim;

    unsigned n = 0;
    outLens[n++] = (unsigned long long)vocabSize * embedDim;   // embedding
    for (unsigned i = 0; i < nLayers; ++i)
    {
        outLens[n++] = dd;   // q
        outLens[n++] = dd;   // k
        outLens[n++] = dd;   // v
        outLens[n++] = dd;   // o
        outLens[n++] = df;   // ff1
        outLens[n++] = fd;   // ff2
        outLens[n++] = ed;   // norm
        outLens[n++] = ed;   // normBias
    }
    outLens[n++] = ed;                                         // finalNorm
    outLens[n++] = ed;                                         // finalNormBias
    outLens[n++] = (unsigned long long)embedDim * vocabSize;   // outputProj
    return n;
}

/// Greedily pack the ordered tensor lengths into pages of target `strideElems`,
/// NEVER splitting a tensor across pages — a tensor larger than the stride occupies
/// its own page. Order is preserved (the flat layout is a fixed contract, so tensors
/// cannot be reordered to pack tighter). Writes page descriptors to `outPages`.
///
/// Returns the page count, or 0 on error: a single tensor exceeds YUKI_PAGE_MAX
/// (unsupported), or `maxPages` is too small (page count never exceeds tensorCount,
/// so sizing outPages to tensorCount is always safe). Writes the total element count
/// (== sum of tensor lengths) to *outTotal.
inline unsigned YukiPlanPages(const unsigned long long* tensorLens, unsigned tensorCount,
                              unsigned strideElems,
                              YukiPage* outPages, unsigned maxPages,
                              unsigned long long* outTotal)
{
    unsigned long long total = 0;
    unsigned np = 0;
    unsigned long long curBase = 0;
    unsigned curElems = 0;
    bool open = false;

    for (unsigned t = 0; t < tensorCount; ++t)
    {
        const unsigned long long L = tensorLens[t];
        if (L > YUKI_PAGE_MAX)
            return 0;   // a single tensor cannot fit in one i32-indexed page

        // Close the current page if adding this tensor would push it past the stride
        // target. A tensor is only ever added whole, so it never straddles a boundary.
        if (open && (unsigned long long)curElems + L > strideElems)
        {
            if (np >= maxPages) return 0;
            outPages[np].base = curBase;
            outPages[np].elems = curElems;
            ++np;
            curBase += curElems;
            curElems = 0;
            open = false;
        }

        open = true;
        curElems += (unsigned)L;   // safe: packed <= stride, or lone tensor <= YUKI_PAGE_MAX
        total += L;
    }

    if (open)
    {
        if (np >= maxPages) return 0;
        outPages[np].base = curBase;
        outPages[np].elems = curElems;
        ++np;
    }

    if (outTotal)
        *outTotal = total;
    return np;
}

/// Locate the page holding flat element `idx`. Linear scan — page counts are small
/// (hundreds even for multi-billion-weight models). Returns pageCount if not found.
inline unsigned YukiPageOf(const YukiPage* pages, unsigned pageCount, unsigned long long idx)
{
    for (unsigned p = 0; p < pageCount; ++p)
    {
        if (idx >= pages[p].base && idx < pages[p].base + pages[p].elems)
            return p;
    }
    return pageCount;
}

}

}
