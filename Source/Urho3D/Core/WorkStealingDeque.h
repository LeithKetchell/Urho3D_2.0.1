// Copyright (c) 2025 the Urho3D project
// License: MIT
//
// Phase 1.2: Lock-Free Work-Stealing Deque
// Based on Chase-Lev work-stealing deque algorithm
// Reference: Dynamic Circular Work-Stealing Deque (Leiserson & Plgo)

#pragma once

#include "../Container/Vector.h"
#include "../Container/Ptr.h"
#include <atomic>
#include <cstring>

namespace Urho3D
{

/// Forward declaration
struct WorkItem;

/// Lock-free work-stealing deque for efficient task distribution
/// Only the owner thread can push/pop from the tail
/// Other threads can steal from the head in a lock-free manner
class URHO3D_API WorkStealingDeque : public RefCounted
{
public:
    /// Construct with initial capacity
    explicit WorkStealingDeque(uint32_t initialCapacity = 256);
    /// Destruct
    ~WorkStealingDeque();

    /// Push work item to tail (owner thread only)
    /// Thread-safety: Only call from owner thread
    void Push(WorkItem* item);

    /// Pop work item from tail (owner thread only)
    /// Thread-safety: Only call from owner thread
    /// Returns nullptr if deque is empty
    WorkItem* Pop();

    /// Steal work item from head (any thread)
    /// Thread-safety: Safe to call from any thread
    /// Returns nullptr if steal fails or deque is empty
    WorkItem* Steal();

    /// Get current size (approximate, lock-free read)
    uint32_t GetSize() const;

    /// Check if deque is empty (approximate)
    bool IsEmpty() const;

    /// Get number of successful steals (statistics)
    uint64_t GetSteals() const { return steals_; }

    /// Get number of failed steal attempts (statistics)
    uint64_t GetFailedSteals() const { return failedSteals_; }

    /// Reset statistics
    void ResetStats()
    {
        steals_ = 0;
        failedSteals_ = 0;
    }

private:
    /// Grow the deque buffer to larger capacity
    void Grow();

    /// Number of entries in buffer
    std::atomic<uint64_t> size_{0};

    /// Head position (for stealing)
    std::atomic<uint64_t> head_{0};

    /// Tail position (for owner thread)
    uint64_t tail_{0};

    /// Task buffer array
    Vector<WorkItem*> buffer_;

    /// Current capacity (must be power of 2)
    uint32_t capacity_;

    /// Statistics
    uint64_t steals_{0};
    uint64_t failedSteals_{0};

    /// Maximum capacity before resizing
    static constexpr uint32_t MAX_CAPACITY = 1024 * 1024;  // 1M items max
};

} // namespace Urho3D
