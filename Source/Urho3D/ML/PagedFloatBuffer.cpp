// PagedFloatBuffer — implementation.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Standalone packing gate: compile with -DYUKI_PAGING_STANDALONE (g++, no engine) to
// verify YukiPaging's tiling matches an independent reference byte-for-byte — the
// tensor-alignment invariant and the "tiles exactly TotalWeights" postcondition are
// checked without an engine build, mirroring the M8 adapter gate:
//
//     g++ -std=c++14 -DYUKI_PAGING_STANDALONE -I Source/Urho3D/ML \
//         Source/Urho3D/ML/PagedFloatBuffer.cpp -o /tmp/paging_gate && /tmp/paging_gate

#ifndef YUKI_PAGING_STANDALONE

#include "../Precompiled.h"
#include "PagedFloatBuffer.h"

#include <cstring>

#include "../DebugNew.h"

namespace Urho3D
{

using YukiMath::YukiPage;

bool PagedFloatBuffer::Configure(unsigned embedDim, unsigned nLayers, unsigned ffDim,
                                 unsigned vocabSize, unsigned strideElems)
{
    Clear();

    const unsigned tc = YukiMath::YukiTensorCount(nLayers);
    Vector<unsigned long long> lens(tc);
    if (YukiMath::YukiEnumerateTensors(embedDim, nLayers, ffDim, vocabSize, lens.Buffer(), tc) != tc)
        return false;

    Vector<YukiPage> plan(tc);   // page count never exceeds tensor count
    unsigned long long total = 0;
    const unsigned np = YukiMath::YukiPlanPages(lens.Buffer(), tc, strideElems,
                                                plan.Buffer(), tc, &total);
    if (np == 0)
        return false;   // a single tensor exceeds the i32 page ceiling — unsupported

    dir_.Resize(np);
    pages_.Resize(np);
    for (unsigned p = 0; p < np; ++p)
    {
        dir_[p] = plan[p];
        pages_[p].Resize((unsigned)plan[p].elems);   // elems is i32-safe by construction
    }
    size_ = total;
    Zero();
    return true;
}

void PagedFloatBuffer::Clear()
{
    pages_.Clear();
    dir_.Clear();
    size_ = 0;
}

float* PagedFloatBuffer::Span(unsigned long long offset, unsigned long long len)
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), offset);
    assert(p < dir_.Size());
    // Tensor-aligned pages guarantee the whole span sits in one page.
    assert(offset + len <= dir_[p].base + dir_[p].elems);
    return pages_[p].Buffer() + (unsigned)(offset - dir_[p].base);
}

const float* PagedFloatBuffer::Span(unsigned long long offset, unsigned long long len) const
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), offset);
    assert(p < dir_.Size());
    assert(offset + len <= dir_[p].base + dir_[p].elems);
    return pages_[p].Buffer() + (unsigned)(offset - dir_[p].base);
}

void PagedFloatBuffer::Zero()
{
    for (unsigned p = 0; p < pages_.Size(); ++p)
        memset(pages_[p].Buffer(), 0, (size_t)dir_[p].elems * sizeof(float));
}

void PagedFloatBuffer::CopyFrom(const PagedFloatBuffer& src)
{
    assert(SameLayout(src));
    for (unsigned p = 0; p < pages_.Size(); ++p)
        memcpy(pages_[p].Buffer(), src.pages_[p].Buffer(), (size_t)dir_[p].elems * sizeof(float));
}

float& PagedFloatBuffer::At(unsigned long long idx)
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), idx);
    assert(p < dir_.Size());
    return pages_[p].Buffer()[(unsigned)(idx - dir_[p].base)];
}

float PagedFloatBuffer::At(unsigned long long idx) const
{
    const unsigned p = YukiMath::YukiPageOf(dir_.Buffer(), dir_.Size(), idx);
    assert(p < dir_.Size());
    return pages_[p].Buffer()[(unsigned)(idx - dir_[p].base)];
}

bool PagedFloatBuffer::SameLayout(const PagedFloatBuffer& other) const
{
    if (dir_.Size() != other.dir_.Size() || size_ != other.size_)
        return false;
    for (unsigned p = 0; p < dir_.Size(); ++p)
        if (dir_[p].base != other.dir_[p].base || dir_[p].elems != other.dir_[p].elems)
            return false;
    return true;
}

}

#else  // YUKI_PAGING_STANDALONE — pure packing verification, no engine.

#include "YukiPaging.h"
#include <cstdio>
#include <vector>

using namespace Urho3D::YukiMath;

// Independent reference: recompute TotalWeights by a separate hand walk, then
// cross-check against the planner. A stride bug in one path is caught by the other.
static unsigned long long RefTotal(unsigned e, unsigned nL, unsigned f, unsigned v)
{
    unsigned long long t = (unsigned long long)v * e;
    const unsigned long long per = 4ULL * e * e + (unsigned long long)e * f
                                 + (unsigned long long)f * e + 2ULL * e;
    t += per * nL;
    t += 2ULL * e;
    t += (unsigned long long)e * v;
    return t;
}

static int gFail = 0;
static void Check(bool ok, const char* what)
{
    if (!ok) { printf("  FAIL: %s\n", what); gFail = 1; }
}

static void Run(unsigned e, unsigned nL, unsigned f, unsigned v, unsigned stride)
{
    printf("topology embed=%u layers=%u ff=%u vocab=%u stride=%u\n", e, nL, f, v, stride);

    const unsigned tc = YukiTensorCount(nL);
    std::vector<unsigned long long> lens(tc);
    const unsigned n = YukiEnumerateTensors(e, nL, f, v, lens.data(), tc);
    Check(n == tc, "enumerate count");

    unsigned long long sum = 0;
    for (unsigned i = 0; i < tc; ++i) sum += lens[i];
    Check(sum == RefTotal(e, nL, f, v), "tensor lens sum == TotalWeights");

    std::vector<YukiPage> pages(tc);
    unsigned long long total = 0;
    const unsigned np = YukiPlanPages(lens.data(), tc, stride, pages.data(), tc, &total);
    Check(np > 0, "plan succeeded");
    Check(total == sum, "planned total == tensor sum");

    unsigned long long expectBase = 0;
    for (unsigned p = 0; p < np; ++p)
    {
        Check(pages[p].base == expectBase, "page base contiguous");
        Check(pages[p].elems > 0, "page non-empty");
        Check((unsigned long long)pages[p].elems <= YUKI_PAGE_MAX, "page within i32 ceiling");
        expectBase += pages[p].elems;
    }
    Check(expectBase == total, "pages tile exactly total");

    // no tensor split: each tensor [off, off+len) lies within a single page
    unsigned long long off = 0;
    for (unsigned i = 0; i < tc; ++i)
    {
        const unsigned long long L = lens[i];
        const unsigned pa = YukiPageOf(pages.data(), np, off);
        Check(pa < np, "tensor start located");
        if (pa < np)
            Check(off + L <= pages[pa].base + pages[pa].elems, "tensor does not cross page");
        off += L;
    }
    printf("  %u tensors -> %u pages, %llu elems\n", tc, np, (unsigned long long)total);
}

int main()
{
    Run(64, 2, 256, 4096, YUKI_PAGE_STRIDE);       // tiny — one page
    Run(512, 6, 2048, 32000, YUKI_PAGE_STRIDE);    // modest
    Run(2048, 12, 8192, 50000, YUKI_PAGE_STRIDE);  // large — multi-page, big embedding
    Run(4096, 24, 16384, 100000, YUKI_PAGE_STRIDE);// huge — single tensors exceed stride, >2^31 total
    Run(512, 6, 2048, 32000, 1u << 20);            // small stride -> many pages, same tiling
    printf(gFail ? "PAGING GATE: FAIL\n" : "PAGING GATE: PASS\n");
    return gFail;
}

#endif
