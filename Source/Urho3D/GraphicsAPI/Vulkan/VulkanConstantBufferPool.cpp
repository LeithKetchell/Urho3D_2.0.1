//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan constant buffer pooling implementation (Quick Win #6)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanConstantBufferPool.h"
#include "VulkanGraphicsImpl.h"
#include "../../IO/Log.h"
#include <cstring>

#include "../../DebugNew.h"

namespace Urho3D
{

VulkanConstantBufferPool::VulkanConstantBufferPool()
{
}

VulkanConstantBufferPool::~VulkanConstantBufferPool()
{
    Release();
}

bool VulkanConstantBufferPool::Initialize(VulkanGraphicsImpl* impl, uint32_t poolSizeInBytes)
{
    if (!impl)
    {
        URHO3D_LOGERROR("Invalid graphics implementation passed to VulkanConstantBufferPool");
        return false;
    }

    impl_ = impl;
    poolSizePerBuffer_ = poolSizeInBytes;

    // Create initial pool buffer
    if (!CreatePoolBuffer(poolSizePerBuffer_))
    {
        URHO3D_LOGERROR("Failed to create initial constant buffer pool");
        return false;
    }

    URHO3D_LOGDEBUG("VulkanConstantBufferPool initialized with size: " + String(poolSizeInBytes / 1024 / 1024) + " MB");
    return true;
}

void VulkanConstantBufferPool::Release()
{
    // Destroy all pool buffers
    for (auto& buffer : poolBuffers_)
    {
        if (buffer.buffer && impl_)
        {
            vmaDestroyBuffer(impl_->GetAllocator(), buffer.buffer, buffer.allocation);
        }
    }
    poolBuffers_.Clear();
    impl_ = nullptr;

    URHO3D_LOGDEBUG("VulkanConstantBufferPool released (total allocations: " +
                   String(stats_.allocationCount) + ", peak frame size: " +
                   String(stats_.peakFrameSize / 1024) + " KB)");
}

bool VulkanConstantBufferPool::CreatePoolBuffer(uint32_t size)
{
    if (!impl_)
        return false;

    if (poolBuffers_.Size() >= MAX_POOL_BUFFERS)
    {
        URHO3D_LOGWARNING("Constant buffer pool reached maximum buffer count: " + String(MAX_POOL_BUFFERS));
        return false;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // Attempt to use memory pool for optimized allocation
    VulkanMemoryPoolManager* poolMgr = impl_->GetMemoryPoolManager();
    if (poolMgr)
    {
        VmaPool pool = poolMgr->GetPool(VulkanMemoryPoolType::ConstantBuffers);
        if (pool)
        {
            allocInfo.pool = pool;
        }
    }

    PooledConstantBuffer poolBuffer{};
    poolBuffer.size = size;

    if (vmaCreateBuffer(impl_->GetAllocator(), &bufferInfo, &allocInfo,
                       &poolBuffer.buffer, &poolBuffer.allocation, nullptr) != VK_SUCCESS)
    {
        // Fallback: attempt without pool if pool allocation failed
        if (poolMgr)
        {
            allocInfo.pool = nullptr;
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            if (vmaCreateBuffer(impl_->GetAllocator(), &bufferInfo, &allocInfo,
                               &poolBuffer.buffer, &poolBuffer.allocation, nullptr) != VK_SUCCESS)
            {
                URHO3D_LOGERROR("Failed to create constant buffer pool buffer");
                return false;
            }
        }
        else
        {
            URHO3D_LOGERROR("Failed to create constant buffer pool buffer");
            return false;
        }
    }

    // Map the buffer for persistent writing
    VmaAllocationInfo allocInfoResult;
    vmaGetAllocationInfo(impl_->GetAllocator(), poolBuffer.allocation, &allocInfoResult);
    poolBuffer.mappedData = allocInfoResult.pMappedData;

    if (!poolBuffer.mappedData)
    {
        URHO3D_LOGERROR("Failed to map constant buffer pool buffer");
        vmaDestroyBuffer(impl_->GetAllocator(), poolBuffer.buffer, poolBuffer.allocation);
        return false;
    }

    // Divide buffer into equal regions (one per swapchain image)
    uint32_t regionSize = size / regionCount_;
    poolBuffer.regionCount = regionCount_;
    for (uint32_t i = 0; i < regionCount_; ++i)
    {
        poolBuffer.frameRegions[i].offset = i * regionSize;
        poolBuffer.frameRegions[i].size = regionSize;
        poolBuffer.frameRegions[i].currentOffset = i * regionSize;
    }

    poolBuffer.isUsed = true;

    poolBuffers_.Push(poolBuffer);

    URHO3D_LOGDEBUG("Created constant buffer pool buffer (size: " +
                   String(size / 1024 / 1024) + " MB, total buffers: " +
                   String(poolBuffers_.Size()) + ")");

    stats_.totalPoolSize += size;
    return true;
}

PooledConstantBuffer* VulkanConstantBufferPool::FindAvailableBuffer(uint32_t requiredSize)
{
    if (!impl_ || requiredSize == 0)
        return nullptr;

    // Look for existing buffer with enough space in current frame's region
    for (auto& buffer : poolBuffers_)
    {
        FrameRegion& region = buffer.frameRegions[currentFrameIndex_];
        uint32_t allocatedInRegion = region.currentOffset - region.offset;
        uint32_t remainingSpace = region.size - allocatedInRegion;

        if (remainingSpace >= requiredSize)
            return &buffer;
    }

    // No buffer has space, create new one if possible
    if (poolBuffers_.Size() < MAX_POOL_BUFFERS)
    {
        uint32_t newBufferSize = poolSizePerBuffer_;
        // If single allocation exceeds pool size, create larger buffer
        if (requiredSize > newBufferSize)
            newBufferSize = ((requiredSize + 65535) / 65536) * 65536;  // Align to 64KB

        if (CreatePoolBuffer(newBufferSize))
        {
            return &poolBuffers_.Back();
        }
    }

    return nullptr;
}

bool VulkanConstantBufferPool::AllocateBuffer(
    const void* data,
    uint32_t dataSize,
    VkBuffer& outBuffer,
    VkDeviceSize& outOffset)
{
    if (!impl_ || !data || dataSize == 0)
        return false;

    // Align allocation to 256-byte boundary (Vulkan requirement for dynamic offsets)
    uint32_t alignedSize = ((dataSize + 255) / 256) * 256;

    PooledConstantBuffer* buffer = FindAvailableBuffer(alignedSize);
    if (!buffer)
    {
        URHO3D_LOGERROR("Constant buffer pool exhausted: requested " + String(dataSize) +
                       " bytes, aligned to " + String(alignedSize));
        return false;
    }

    // Get current frame's region
    FrameRegion& region = buffer->frameRegions[currentFrameIndex_];

    // Disabled for performance - uncomment for debugging constant buffer allocations
    // static int allocCount = 0;
    // if (allocCount < 20)
    // {
    //             allocCount, currentFrameIndex_, region.offset, region.size, dataSize, region.currentOffset);
    //     allocCount++;
    // }

    // Write data to this frame's region
    std::memcpy((char*)buffer->mappedData + region.currentOffset, data, dataSize);

    // Return buffer and offset within this frame's region
    outBuffer = buffer->buffer;
    outOffset = region.currentOffset;

    // Update tracking within this frame's region
    region.currentOffset += alignedSize;
    currentFrameSize_ += alignedSize;
    stats_.allocationCount++;

    if (currentFrameSize_ > stats_.peakFrameSize)
        stats_.peakFrameSize = currentFrameSize_;

    return true;
}

void VulkanConstantBufferPool::BeginFrame(uint32_t frameIndex)
{
    if (frameIndex >= regionCount_)
    {
        URHO3D_LOGERROR("Invalid frame/image index: " + String(frameIndex) + " (regionCount=" + String(regionCount_) + ")");
        return;
    }

    // Debug: BeginFrame called for frameIndex

    // Reset ONLY this image's region offsets in all buffers
    for (auto& buffer : poolBuffers_)
    {
        if (frameIndex < buffer.regionCount)
        {
            FrameRegion& region = buffer.frameRegions[frameIndex];
            region.currentOffset = region.offset;
        }
    }

    currentFrameSize_ = 0;
    currentFrameIndex_ = frameIndex;
}

void VulkanConstantBufferPool::ResetFrameAllocations()
{
    // DEPRECATED: Use BeginFrame() instead for proper triple-buffering
    // This method resets ALL frames, which causes synchronization bugs

    // Reset all buffer offsets for next frame
    for (auto& buffer : poolBuffers_)
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            buffer.frameRegions[i].currentOffset = buffer.frameRegions[i].offset;
        }
    }

    currentFrameSize_ = 0;

    // Phase B Quick Win #11: Evaluate pool utilization and apply dynamic sizing
    // TODO: Implement dynamic sizing algorithm for optimal memory utilization
    // if (dynamicSizingEnabled_)
    //     UpdateDynamicSizing();
}

VulkanConstantBufferPool::PoolStats VulkanConstantBufferPool::GetStatistics() const
{
    PoolStats stats = stats_;

    // Calculate current usage
    stats.allocatedBuffers = poolBuffers_.Size();
    stats.totalPoolSize = 0;
    stats.usedSize = 0;

    for (const auto& buffer : poolBuffers_)
    {
        stats.totalPoolSize += buffer.size;

        // Sum used space across all frame regions
        for (uint32_t i = 0; i < 3; ++i)
        {
            const FrameRegion& region = buffer.frameRegions[i];
            stats.usedSize += (region.currentOffset - region.offset);
        }
    }

    stats.wastedSize = stats.totalPoolSize - stats.usedSize;

    if (stats.totalPoolSize > 0)
    {
        stats.averageFragmentation = (float)stats.wastedSize / (float)stats.totalPoolSize;
    }

    return stats;
}

uint32_t VulkanConstantBufferPool::GetRecommendedPoolSize() const
{
    /// \brief Phase B Quick Win #11: Recommend optimal buffer size based on history
    /// \details Analyzes last 10 frames to recommend 1.25x peak usage size

    if (frameHistoryLog_.Empty())
        return poolSizePerBuffer_;

    // Find peak usage in history
    uint32_t peakUsage = 0;
    for (uint32_t frameSize : frameHistoryLog_)
    {
        if (frameSize > peakUsage)
            peakUsage = frameSize;
    }

    if (peakUsage == 0)
        return poolSizePerBuffer_;

    // Recommend 1.25x peak usage (25% headroom)
    uint32_t recommendedSize = static_cast<uint32_t>(peakUsage * 1.25f);

    // Align to 1MB boundaries for cleaner sizing
    const uint32_t ALIGNMENT = 1024 * 1024;
    recommendedSize = ((recommendedSize + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;

    return recommendedSize;
}

// TODO: Phase B Quick Win #11 - Dynamic sizing not yet exposed in public API
// This method was part of the implementation but is not currently called
// and not declared in the public header. Can be integrated later when needed.
/*
void VulkanConstantBufferPool::UpdateDynamicSizing()
{
    /// \brief Phase B Quick Win #11: Evaluate utilization and auto-adjust pool sizing
    /// \details Called at end of each frame to monitor trends and apply sizing changes

    if (!dynamicSizingEnabled_ || poolBuffers_.Empty())
        return;

    totalFramesProcessed_++;

    // Calculate current frame utilization
    uint32_t totalPoolSize = 0;
    uint32_t usedSize = 0;

    for (const auto& buffer : poolBuffers_)
    {
        totalPoolSize += buffer.size;
        usedSize += buffer.currentOffset;
    }

    // Track frame history (keep last 10 frames)
    if (frameHistoryLog_.Size() >= 10)
        frameHistoryLog_.Erase(frameHistoryLog_.Begin());

    frameHistoryLog_.Push(usedSize);

    if (totalPoolSize == 0)
        return;

    uint32_t utilizationPercent = (usedSize * 100) / totalPoolSize;

    // Check if we should grow the pool
    if (utilizationPercent > growthThreshold_)
    {
        consecutiveHighUtilizationFrames_++;
        consecutiveLowUtilizationFrames_ = 0;

        // Trigger growth if threshold met and enough time has passed
        uint32_t framesSinceAdjustment = totalFramesProcessed_ - lastAdjustmentFrameNumber_;
        if (consecutiveHighUtilizationFrames_ >= growthFrameWindow_ &&
            framesSinceAdjustment >= MIN_FRAMES_BETWEEN_ADJUSTMENTS)
        {
            uint32_t newSize = static_cast<uint32_t>(poolSizePerBuffer_ * MAX_GROWTH_MULTIPLIER);
            poolSizePerBuffer_ = newSize;
            lastAdjustmentFrameNumber_ = totalFramesProcessed_;
            consecutiveHighUtilizationFrames_ = 0;

            URHO3D_LOGINFO("VulkanConstantBufferPool: Auto-grown to " +
                          String(poolSizePerBuffer_ / 1024 / 1024) + "MB (utilization: " +
                          String(utilizationPercent) + "%)");
        }
    }
    // Check if we could shrink the pool
    else if (utilizationPercent < shrinkThreshold_ && poolBuffers_.Size() > 1)
    {
        consecutiveLowUtilizationFrames_++;
        consecutiveHighUtilizationFrames_ = 0;

        // Note: Actual shrinking is deferred until a new frame would fit in smaller buffers
        // This prevents constant resizing due to transient underutilization
    }
    else
    {
        // Reset counters if utilization is in normal range
        consecutiveHighUtilizationFrames_ = 0;
        consecutiveLowUtilizationFrames_ = 0;
    }
}
*/

} // namespace Urho3D

#endif  // URHO3D_VULKAN
