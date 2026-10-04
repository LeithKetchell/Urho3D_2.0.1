// Copyright (c) 2008-2024 the Urho3D project
// License: MIT

#pragma once

#ifdef URHO3D_VULKAN

#include "../Container/HashMap.h"
#include "../Core/Object.h"
#include "../Core/Profiler.h"
#include "../IO/Log.h"
#include <chrono>

namespace Urho3D
{

/// Fixed-size ring buffer for O(1) push/average without heap allocation or element shifting.
template <typename T, unsigned CAPACITY>
struct RingBuffer
{
    T data_[CAPACITY]{};
    unsigned head_ = 0;
    unsigned count_ = 0;
    T runningSum_{};

    void Push(T value)
    {
        if (count_ == CAPACITY)
            runningSum_ -= data_[head_];
        else
            ++count_;
        data_[head_] = value;
        runningSum_ += value;
        head_ = (head_ + 1) % CAPACITY;
    }

    T Average() const { return count_ > 0 ? runningSum_ / (T)count_ : T{}; }
    bool Empty() const { return count_ == 0; }
    unsigned Size() const { return count_; }
    void Clear() { head_ = 0; count_ = 0; runningSum_ = T{}; }
};

/// High-resolution timer for Vulkan graphics profiling.
/// Measures per-phase timing in command buffer recording, state management, and submission.
class VulkanProfiler : public RefCounted
{
public:
    /// Constructor
    VulkanProfiler() = default;

    /// Start timing a phase
    void StartPhase(const String& phaseName)
    {
        phaseStart_ = std::chrono::high_resolution_clock::now();
        currentPhase_ = phaseName;
    }

    /// End timing current phase and record measurement
    void EndPhase()
    {
        if (currentPhase_.Empty())
            return;

        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - phaseStart_);

        phaseTimings_[currentPhase_].Push(duration.count());
        currentPhase_.Clear();
    }

    /// Record frame time and update FPS
    void RecordFrame(float deltaTime)
    {
        frameTime_ = deltaTime;
        frameCount_++;
        totalTime_ += deltaTime;

        frameTimes_.Push(deltaTime * 1000.0f);

        float avgFrameTime = frameTimes_.Average();
        if (avgFrameTime > 0.0f)
            fps_ = 1000.0f / avgFrameTime;
    }

    /// Get current FPS
    float GetFPS() const { return fps_; }

    /// Get current frame time in milliseconds
    float GetFrameTime() const { return frameTime_ * 1000.0f; }

    /// Get average frame time from rolling buffer in milliseconds
    float GetAverageFrameTime() const { return frameTimes_.Average(); }

    /// Get average time for a phase in milliseconds
    float GetAveragePhaseTime(const String& phaseName) const
    {
        auto it = phaseTimings_.Find(phaseName);
        if (it == phaseTimings_.End() || it->second_.Empty())
            return 0.0f;

        return (float)(it->second_.Average() / 1000.0);
    }

    /// Get all phase timing statistics for debug output
    void PrintStats() const
    {
        for (const auto& pair : phaseTimings_)
        {
            if (pair.second_.Empty())
                continue;

            double avg = pair.second_.Average();

            URHO3D_LOGINFO(String("Vulkan Phase: ") + pair.first_ +
                           " | Avg: " + String(avg / 1000.0f, 2) + "ms");
        }
    }

    /// Clear all recorded timings
    void Clear()
    {
        phaseTimings_.Clear();
        currentPhase_.Clear();
        frameTimes_.Clear();
        fps_ = 0.0f;
        frameTime_ = 0.0f;
        frameCount_ = 0;
        totalTime_ = 0.0f;
        counters_.Clear();
    }

    /// Phase 36A: Increment a counter (for tracking descriptor set operations)
    void IncrementCounter(const String& counterName)
    {
        counters_[counterName]++;
    }

    /// Phase 36A: Get counter value
    unsigned GetCounter(const String& counterName) const
    {
        auto it = counters_.Find(counterName);
        return (it != counters_.End()) ? it->second_ : 0;
    }

    /// Phase 36A: Reset a specific counter
    void ResetCounter(const String& counterName)
    {
        counters_[counterName] = 0;
    }

private:
    /// Start time of current phase
    std::chrono::high_resolution_clock::time_point phaseStart_;

    /// Current phase being measured
    String currentPhase_;

    /// Recorded timings per phase (in microseconds, 60-frame ring buffer)
    HashMap<String, RingBuffer<double, 60>> phaseTimings_;

    /// FPS tracking
    float fps_ = 0.0f;
    float frameTime_ = 0.0f;
    unsigned frameCount_ = 0;
    float totalTime_ = 0.0f;
    RingBuffer<float, 60> frameTimes_;

    /// Phase 36A: Operation counters (e.g., descriptor set bindings, pipeline layout creations)
    HashMap<String, unsigned> counters_;
};

} // namespace Urho3D

#endif // URHO3D_VULKAN
