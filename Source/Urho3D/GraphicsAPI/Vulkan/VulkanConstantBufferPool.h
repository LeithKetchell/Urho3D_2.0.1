//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan constant buffer pooling for efficient memory management (Quick Win #6)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Container/HashMap.h"
#include "../../Math/MathDefs.h"
#include <vulkan/vulkan.h>
#include "../../Container/RefCounted.h"
#include <vk_mem_alloc.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// \brief Single constant buffer within a pool
/// \details Represents one fixed-size buffer allocation from VMA. Contains metadata for
/// tracking usage, fragmentation, and memory mapping. Buffers are reused across frames
/// and allocated in bulk to reduce VkCreateBuffer() overhead.
///
/// **Memory Layout:**
/// - buffer: VkBuffer handle for GPU binding
/// - allocation: VMA allocation metadata
/// - mappedData: CPU-writable pointer (persistent mapping)
/// - frameRegions: Per-frame regions for triple-buffering (prevents overwrites)
/// - isUsed: Whether this buffer has any allocations this frame

/// \brief Frame region within a pooled buffer (for triple-buffering)
/// \details Each frame gets its own region to prevent overwrites while GPU is rendering
struct FrameRegion
{
    uint32_t offset{0};         ///< Start offset of this frame's region
    uint32_t size{0};           ///< Size of this frame's region
    uint32_t currentOffset{0};  ///< Current allocation position within this region
};

struct PooledConstantBuffer
{
    VkBuffer buffer{};                      ///< VkBuffer handle
    VmaAllocation allocation{};             ///< VMA allocation tracking
    void* mappedData{nullptr};              ///< CPU-mapped pointer for writing
    uint32_t size{0};                       ///< Total buffer size in bytes
    FrameRegion frameRegions[8];            ///< Per-image regions (max 8 swapchain images)
    uint32_t regionCount{0};                ///< Actual number of regions in use
    bool isUsed{false};                     ///< Whether buffer used this frame
};

/// \brief Manages pooled constant buffers with dynamic allocation
/// \details Implements frame-coherent constant buffer pooling: multiple 4MB buffers
/// that are allocated from once per frame, then reset for next frame. Avoids individual
/// buffer creation for each constant buffer update (major bottleneck in naive implementations).
/// Implements Quick Win #6.
///
/// **Pool Strategy:**
/// - Maintains up to 8 pre-allocated 4MB buffers (MAX_POOL_BUFFERS)
/// - Each frame: linearly allocate into buffers, then reset offsets
/// - Avoids fragmentation through frame-boundary resets
/// - Single persistent CPU mapping per buffer (no flush calls)
///
/// **Allocation Algorithm:**
/// 1. AllocateBuffer(data, size) finds first buffer with enough space
/// 2. Linearly writes data to mappedData + currentOffset
/// 3. Returns (buffer, offset) for descriptor binding
/// 4. ResetFrameAllocations() called at frame start to reset all offsets
/// 5. If all buffers exhausted, creates new buffer (up to MAX_POOL_BUFFERS)
///
/// **Performance Benefits:**
/// - Single vkCreateBuffer() per pool initialization (not per-frame)
/// - Linear sub-allocations avoid fragmentation
/// - Frame-boundary resets = zero waste between frames
/// - Persistent mapping = no vkMapMemory() calls per frame
/// - Typical: 100+ constant buffer bindings per frame from 1-2 pool buffers
///
/// **Typical Usage:**
/// 1. Initialize(impl, poolSizeInBytes) during Graphics setup
/// 2. ResetFrameAllocations() at start of each frame
/// 3. AllocateBuffer(data, size, buffer, offset) for each constant buffer
/// 4. Bind returned (buffer, offset) in descriptor set
/// 5. Next frame: offsets automatically reset, reuse same buffers
class VulkanConstantBufferPool : public RefCounted
{
public:
    /// \brief Constructor
    VulkanConstantBufferPool();

    /// \brief Destructor
    /// \details Automatically releases all pooled buffers via Release()
    ~VulkanConstantBufferPool();

    /// \brief Initialize pool with graphics implementation
    /// \param impl VulkanGraphicsImpl for access to device and VMA allocator
    /// \param poolSizeInBytes Size of each pool buffer (default: 4MB)
    /// \returns True if initialization successful, false on error
    /// \details Creates first pool buffer and initializes statistics.
    /// Additional buffers created dynamically as needed.
    bool Initialize(VulkanGraphicsImpl* impl, uint32_t poolSizeInBytes = 4 * 1024 * 1024);

    /// \brief Set number of regions per buffer (must match swapchain image count)
    /// \param count Number of regions (typically 2-4 for double/triple/quad buffering)
    void SetRegionCount(uint32_t count) { regionCount_ = count; }

    /// \brief Release all pooled buffers
    /// \details Calls vkDestroyBuffer() and vmaFreeMemory() on all buffers.
    /// Safe to call multiple times. Called automatically on destruction.
    void Release();

    /// \brief Begin a new frame with the given frame index (for triple-buffering)
    /// \param frameIndex Frame index (0-2) for triple-buffering
    /// \details Resets ONLY this frame's region offsets, leaving other frames' data intact.
    /// This prevents Frame N+1 from overwriting Frame N's constant buffer while GPU is still rendering.
    /// Must be called at start of each frame before new allocations.
    void BeginFrame(uint32_t frameIndex);

    /// \brief Reset per-frame allocations (DEPRECATED - use BeginFrame instead)
    /// \details Resets currentOffset to 0 for all buffers and clears isUsed flags.
    /// @deprecated Use BeginFrame() for proper triple-buffering synchronization
    void ResetFrameAllocations();

    /// \brief Allocate space in the pool and return buffer + offset
    /// \param data Constant buffer data to copy
    /// \param dataSize Size of data in bytes
    /// \param outBuffer Output parameter: VkBuffer handle
    /// \param outOffset Output parameter: Offset within buffer
    /// \returns True if allocation successful, false if data too large or pool exhausted
    /// \details Finds first pool buffer with currentOffset + dataSize <= totalSize.
    /// Copies data to mappedData, increments currentOffset, returns (buffer, offset).
    /// If no buffer has space, creates new buffer (if under MAX_POOL_BUFFERS limit).
    /// On failure, outBuffer and outOffset unchanged.
    bool AllocateBuffer(
        const void* data,
        uint32_t dataSize,
        VkBuffer& outBuffer,
        VkDeviceSize& outOffset
    );

    /// \brief Get current pool statistics
    /// \details Structure containing pool utilization metrics for profiling
    struct PoolStats
    {
        uint32_t totalPoolSize{0};          ///< Sum of all pool buffer sizes
        uint32_t usedSize{0};               ///< Bytes actually allocated this frame
        uint32_t wastedSize{0};             ///< Bytes unused (fragmentation)
        uint32_t allocatedBuffers{0};       ///< Number of buffers currently in pool
        uint32_t peakFrameSize{0};          ///< Highest usedSize in any frame
        uint32_t allocationCount{0};        ///< Number of AllocateBuffer() calls this frame
        float averageFragmentation{0.0f};   ///< Wasted / totalPoolSize percentage
    };

    /// \brief Get pool statistics
    /// \returns PoolStats structure with current utilization metrics
    /// \details Used for performance profiling and pool sizing optimization
    PoolStats GetStatistics() const;

    /// \brief Reset statistics
    /// \details Clears allocationCount and peakFrameSize for new profiling session
    void ResetStatistics()
    {
        stats_.allocationCount = 0;
        stats_.peakFrameSize = 0;
    }

    /// \brief Enable/disable dynamic pool sizing (Phase B Quick Win #11)
    /// \param enable Whether to auto-adjust pool size based on usage patterns
    /// \details When enabled, pool grows buffer sizes if utilization > 85% for 3+ frames
    /// and shrinks if utilization < 40% for 5+ frames. Non-invasive auto-optimization.
    void EnableDynamicSizing(bool enable) { dynamicSizingEnabled_ = enable; }

    /// \brief Check if dynamic sizing is enabled
    bool IsDynamicSizingEnabled() const { return dynamicSizingEnabled_; }

    /// \brief Set utilization threshold for growing pool (default: 85%)
    /// \param threshold Percentage 0-100 where pool grows if exceeded
    void SetGrowthThreshold(uint32_t threshold) { growthThreshold_ = Urho3D::Min(threshold, 100u); }

    /// \brief Set utilization threshold for shrinking pool (default: 40%)
    /// \param threshold Percentage 0-100 where pool shrinks if below
    void SetShrinkThreshold(uint32_t threshold) { shrinkThreshold_ = Urho3D::Min(threshold, 100u); }

    /// \brief Set number of consecutive frames to trigger growth (default: 3)
    /// \param frames Number of high-utilization frames before growing
    void SetGrowthFrameWindow(uint32_t frames) { growthFrameWindow_ = Urho3D::Max(frames, 1u); }

    /// \brief Set number of consecutive frames to trigger shrinking (default: 5)
    /// \param frames Number of low-utilization frames before shrinking
    void SetShrinkFrameWindow(uint32_t frames) { shrinkFrameWindow_ = Urho3D::Max(frames, 1u); }

    /// \brief Get recommended pool size based on usage history
    /// \return Suggested poolSizePerBuffer in bytes for optimal performance
    /// \details Analyzes recent frame sizes to recommend buffer size increase.
    /// Returns current size if growth not needed yet.
    uint32_t GetRecommendedPoolSize() const;

private:
    /// \brief Create new buffer in pool
    /// \param size Size of new buffer in bytes
    /// \returns True if buffer created successfully, false on allocation error
    /// \details Allocates VkBuffer via VMA and maps it for CPU access.
    /// Called during Initialize() and dynamically if pool exhausted.
    bool CreatePoolBuffer(uint32_t size);

    /// \brief Find pool buffer with enough space for allocation
    /// \param requiredSize Size needed for allocation
    /// \returns Pointer to PooledConstantBuffer with space, nullptr if none available
    /// \details Iterates pools looking for: currentOffset + requiredSize <= size.
    /// Returns first matching buffer (greedy first-fit allocation).
    PooledConstantBuffer* FindAvailableBuffer(uint32_t requiredSize);

    /// Graphics implementation (for access to device and allocator)
    VulkanGraphicsImpl* impl_{nullptr};

    /// Pool buffers (multiple buffers to avoid exhaustion)
    Vector<PooledConstantBuffer> poolBuffers_;

    /// Pool size in bytes
    uint32_t poolSizePerBuffer_{4 * 1024 * 1024};  // 4MB per buffer

    /// Maximum buffers in pool
    static constexpr uint32_t MAX_POOL_BUFFERS = 8;

    /// Current frame size (resets each frame)
    uint32_t currentFrameSize_{0};

    /// Current frame/image index (0 to regionCount_-1)
    uint32_t currentFrameIndex_{0};

    /// Number of regions per buffer (matches swapchain image count)
    uint32_t regionCount_{3};

    /// Statistics
    mutable PoolStats stats_{};

    /// \brief Phase B Quick Win #11: Dynamic pool sizing configuration
    /// \details Tracks historical usage and auto-adjusts buffer sizes for optimal performance

    /// Whether dynamic sizing is enabled
    bool dynamicSizingEnabled_{false};

    /// Utilization threshold for growing pool (percentage, default: 85)
    uint32_t growthThreshold_{85};

    /// Utilization threshold for shrinking pool (percentage, default: 40)
    uint32_t shrinkThreshold_{40};

    /// Number of consecutive high-utilization frames needed to trigger growth (default: 3)
    uint32_t growthFrameWindow_{3};

    /// Number of consecutive low-utilization frames needed to trigger shrinking (default: 5)
    uint32_t shrinkFrameWindow_{5};

    /// History of last 10 frame sizes for trend analysis
    Vector<uint32_t> frameHistoryLog_;

    /// Number of consecutive frames above growth threshold
    uint32_t consecutiveHighUtilizationFrames_{0};

    /// Number of consecutive frames below shrink threshold
    uint32_t consecutiveLowUtilizationFrames_{0};

    /// Last frame where pool was adjusted (to prevent thrashing)
    uint32_t lastAdjustmentFrameNumber_{0};

    /// Total number of frames processed (for adjustment timing)
    uint32_t totalFramesProcessed_{0};

    /// Minimum frames between adjustments to prevent rapid resizing (default: 30)
    static constexpr uint32_t MIN_FRAMES_BETWEEN_ADJUSTMENTS = 30;

    /// Maximum multiplier for buffer size growth per adjustment (default: 1.5x)
    static constexpr float MAX_GROWTH_MULTIPLIER = 1.5f;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
