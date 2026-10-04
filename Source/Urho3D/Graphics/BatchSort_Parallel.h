// Copyright (c) 2008-2025 the Urho3D project
// License: MIT
//
// Phase 1.5: Parallel Merge-Sort for Rendering Batches
// Cache-aligned batch structures with multi-threaded sorting

#pragma once

#include "../Container/Vector.h"

namespace Urho3D
{

/// Rendering batch structure with cache alignment (64-byte line)
/// Minimizes false sharing and maximizes SIMD efficiency
struct alignas(64) SortedBatch
{
    /// Material for this batch
    void* material;                 // 8 bytes
    /// Geometry for this batch
    void* geometry;                 // 8 bytes
    /// Render order priority (lower = rendered first)
    uint32_t renderOrder;           // 4 bytes
    /// Batch type (e.g., opaque, transparent)
    uint32_t type;                  // 4 bytes
    /// Instance count
    uint32_t instanceCount;         // 4 bytes
    /// Vertex range start
    uint32_t vertexStart;           // 4 bytes
    /// Vertex range end
    uint32_t vertexEnd;             // 4 bytes
    /// Reserved for alignment
    uint32_t _reserved;             // 4 bytes
    // Total: 48 bytes + 16 bytes padding = 64 bytes (one cache line)
};

/// Cache line aligned batch sorter with parallel merge-sort
class URHO3D_API BatchSort_Parallel
{
public:
    /// Sort batches using parallel merge-sort
    /// numThreads: number of worker threads to use (0 = auto-detect)
    /// Returns: sorted array
    static Vector<SortedBatch> SortBatches(
        const Vector<SortedBatch>& input,
        uint32_t numThreads = 0
    );

    /// Comparator for batch sorting
    /// Returns true if a should come before b
    static bool BatchComparator(const SortedBatch& a, const SortedBatch& b)
    {
        // Primary: render order (lower = first)
        if (a.renderOrder != b.renderOrder)
            return a.renderOrder < b.renderOrder;
        // Secondary: material (group by material)
        if (a.material != b.material)
            return a.material < b.material;
        // Tertiary: geometry
        return a.geometry < b.geometry;
    }

    /// Sequential merge-sort (baseline)
    static void MergeSortSequential(Vector<SortedBatch>& batches);

    /// Parallel divide-and-conquer merge-sort
    /// Splits work across worker threads
    static void MergeSortParallel(
        Vector<SortedBatch>& batches,
        uint32_t numThreads
    );

private:
    /// Merge two sorted ranges
    static void Merge(
        Vector<SortedBatch>& data,
        uint32_t left,
        uint32_t mid,
        uint32_t right,
        Vector<SortedBatch>& temp
    );

    /// Recursive merge-sort kernel
    static void MergeSortKernel(
        Vector<SortedBatch>& data,
        uint32_t left,
        uint32_t right,
        Vector<SortedBatch>& temp,
        uint32_t depth,
        uint32_t maxDepth
    );

    /// Minimum partition size for parallelization
    static constexpr uint32_t MIN_PARTITION_SIZE = 256;
};

} // namespace Urho3D
