// Copyright (c) 2025 the Urho3D project
// License: MIT
//
// Phase 1.2: Lock-Free Work-Stealing Deque Implementation

#include "../Precompiled.h"
#include "WorkStealingDeque.h"
#include "WorkQueue.h"
#include <cstdio>

namespace Urho3D
{

WorkStealingDeque::WorkStealingDeque(uint32_t initialCapacity)
    : capacity_(initialCapacity)
{
    // Ensure capacity is power of 2
    if ((capacity_ & (capacity_ - 1)) != 0)
    {
        capacity_ = 1;
        while (capacity_ < initialCapacity)
            capacity_ <<= 1;
    }

    buffer_.Resize(capacity_);
    std::memset(buffer_.Buffer(), 0, capacity_ * sizeof(WorkItem*));
}

WorkStealingDeque::~WorkStealingDeque() = default;

void WorkStealingDeque::Push(WorkItem* item)
{
    // Owner thread only - no synchronization needed for tail write
    uint64_t t = tail_;
    buffer_[t & (capacity_ - 1)] = item;

    // Ensure write is visible before updating tail
    std::atomic_thread_fence(std::memory_order_release);
    tail_ = t + 1;

    // Update size for lock-free reads
    size_.store(tail_ - head_.load(std::memory_order_acquire), std::memory_order_release);

    // Phase 4 Instrumentation: REMOVED - was causing performance issues
    // static thread_local bool logged_push = false;
    // if (!logged_push && t < 10)  // Log first few pushes per thread
    // {
    //     fprintf(stderr, "[Phase4] WorkStealingDeque::Push called (verification that queue is used)\n");
    //     fflush(stderr);
    //     logged_push = true;
    // }
}

WorkItem* WorkStealingDeque::Pop()
{
    // Owner thread only
    if (tail_ == 0)
        return nullptr;

    uint64_t t = tail_ - 1;
    tail_ = t;

    // Synchronize with potential stealers
    std::atomic_thread_fence(std::memory_order_seq_cst);

    uint64_t h = head_.load(std::memory_order_acquire);

    if (t < h)
    {
        // Deque is empty, restore tail
        tail_ = h;
        return nullptr;
    }

    WorkItem* item = buffer_[t & (capacity_ - 1)];

    if (t == h)
    {
        // This was the last item, need to advance head
        head_.compare_exchange_strong(h, h + 1, std::memory_order_seq_cst, std::memory_order_seq_cst);
        tail_ = h + 1;
    }

    // Update size
    size_.store(tail_ - head_.load(std::memory_order_acquire), std::memory_order_release);

    // BUGFIX: Never return null items from buffer
    if (!item)
    {
        fprintf(stderr, "[ERROR] WorkStealingDeque::Pop() returned null item from buffer at position %llu\n",
                (unsigned long long)(t & (capacity_ - 1)));
        fflush(stderr);
    }

    return item;
}

WorkItem* WorkStealingDeque::Steal()
{
    // Any thread can call this - fully synchronized
    uint64_t h = head_.load(std::memory_order_acquire);

    // Ensure head read is visible
    std::atomic_thread_fence(std::memory_order_seq_cst);

    uint64_t t = tail_;  // Load tail with acquire semantics (owner's write)

    if (h >= t)
    {
        // Deque is empty
        failedSteals_++;
        return nullptr;
    }

    // Try to steal the item at head
    WorkItem* item = buffer_[h & (capacity_ - 1)];

    // BUGFIX: Don't steal null items
    if (!item)
    {
        // Buffer position is null - this shouldn't happen but can occur during initialization
        failedSteals_++;
        return nullptr;
    }

    // Try to increment head atomically
    if (head_.compare_exchange_strong(h, h + 1, std::memory_order_seq_cst, std::memory_order_seq_cst))
    {
        // Steal succeeded
        steals_++;

        // Phase 4 Instrumentation: REMOVED - was causing performance issues
        // static thread_local bool logged_steal = false;
        // if (!logged_steal)
        // {
        //     fprintf(stderr, "[Phase4] WorkStealingDeque::Steal SUCCEEDED (work-stealing is active)\n");
        //     fflush(stderr);
        //     logged_steal = true;
        // }

        // Update size
        size_.store(t - (h + 1), std::memory_order_release);

        return item;
    }

    // CAS failed - another stealer got it first
    failedSteals_++;
    return nullptr;
}

void WorkStealingDeque::Grow()
{
    // Owner thread only - should only be called when tail catches up to capacity
    uint32_t newCapacity = capacity_ << 1;

    if (newCapacity > MAX_CAPACITY)
    {
        fprintf(stderr, "[ERROR] WorkStealingDeque: Maximum capacity exceeded\n");
        fflush(stderr);
        return;
    }

    Vector<WorkItem*> newBuffer(newCapacity);

    // Copy existing items to new buffer
    uint64_t h = head_.load(std::memory_order_acquire);
    uint64_t t = tail_;

    for (uint64_t i = h; i < t; ++i)
    {
        newBuffer[i & (newCapacity - 1)] = buffer_[i & (capacity_ - 1)];
    }

    buffer_ = std::move(newBuffer);
    capacity_ = newCapacity;
}

uint32_t WorkStealingDeque::GetSize() const
{
    // Lock-free size read (approximate due to concurrent access)
    uint64_t h = head_.load(std::memory_order_acquire);
    uint64_t t = tail_;  // Note: This is not synchronized, approximate
    return static_cast<uint32_t>(t - h);
}

bool WorkStealingDeque::IsEmpty() const
{
    uint64_t h = head_.load(std::memory_order_acquire);
    uint64_t t = tail_;  // Approximate
    return h >= t;
}

} // namespace Urho3D
