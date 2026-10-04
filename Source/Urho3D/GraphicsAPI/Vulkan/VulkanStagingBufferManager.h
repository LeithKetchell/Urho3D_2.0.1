//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//
// Vulkan Staging Buffer Management (Phase 10 Enhancement)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Core/Object.h"
#include "VulkanStagingRequest.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// \brief Manages staging buffers for efficient GPU transfers
/// \details Implements asynchronous GPU buffer uploads using staging buffers.
/// Staging buffers reside in CPU-visible memory and are copied to device-local
/// buffers via vkCmdCopyBuffer. This avoids stalling the GPU when uploading
/// large amounts of data. Supports frame-pipelined transfers for smooth rendering.
///
/// **Key Benefits:**
/// - Non-blocking uploads: CPU and GPU work in parallel
/// - Efficient large transfers: Staging buffer pool reduces allocations
/// - Frame pipelining: Multiple frames can have pending transfers
/// - Automatic cleanup: Fences track completion and free staging space
///
/// **Typical Usage:**
/// 1. Initialize(totalStagingSize) during Graphics setup
/// 2. RequestUpload(srcData, size, destBuffer) to queue a transfer
/// 3. Engine automatically submits pending transfers
/// 4. Staging space is freed when GPU signals fence completion
class VulkanStagingBufferManager : public Object
{
    URHO3D_OBJECT(VulkanStagingBufferManager, Object);

public:
    /// \brief Constructor
    /// \param context Urho3D Context for object registration
    /// \param graphics VulkanGraphicsImpl pointer for Vulkan resource access
    explicit VulkanStagingBufferManager(Context* context, VulkanGraphicsImpl* graphics);

    /// \brief Destructor - releases GPU resources
    ~VulkanStagingBufferManager();

    /// Release GPU resources
    void Release();

    /// \brief Initialize staging buffer pool
    /// \param totalStagingSize Total size of staging buffer pool in bytes
    /// \returns True if initialization successful, false on error
    /// \details Allocates a CPU-visible staging buffer pool.
    /// Typical size: 64MB for moderate workloads, 256MB+ for large scenes.
    bool Initialize(VkDeviceSize totalStagingSize = 64 * 1024 * 1024);

    /// \brief Request asynchronous GPU upload
    /// \param sourceData Pointer to CPU memory to upload
    /// \param size Size of data in bytes
    /// \param destinationBuffer VkBuffer to copy to (must have TRANSFER_DST_BIT)
    /// \param destinationOffset Offset in destination buffer
    /// \returns Transfer request ID, or NINDEX on failure
    /// \details Copies sourceData to staging buffer and queues a transfer.
    /// Transfer occurs on next Frame() call. Returns immediately.
    uint32_t RequestUpload(
        const void* sourceData,
        VkDeviceSize size,
        VkBuffer destinationBuffer,
        VkDeviceSize destinationOffset = 0
    );

    /// \brief Submit pending transfers to GPU
    /// \param commandBuffer VkCommandBuffer to record transfer commands
    /// \returns Number of transfers submitted
    /// \details Records vkCmdCopyBuffer commands for all pending transfers.
    /// Called by graphics engine at end of frame.
    uint32_t SubmitPendingTransfers(VkCommandBuffer commandBuffer);

    /// \brief Clean up completed transfers
    /// \returns Number of transfers completed and cleaned up
    /// \details Checks transfer fences and frees staging space.
    /// Called by graphics engine each frame.
    uint32_t ProcessCompletedTransfers();

    /// \brief Get total staging buffer size
    /// \returns Size in bytes
    VkDeviceSize GetTotalSize() const { return totalSize_; }

    /// \brief Get available staging space
    /// \returns Bytes available for new uploads
    VkDeviceSize GetAvailableSize() const { return totalSize_ - usedSize_; }

    /// \brief Get number of pending transfers
    /// \returns Count of queued but not yet submitted transfers
    uint32_t GetPendingTransferCount() const { return pendingRequests_.Size(); }

    /// \brief Get number of in-flight transfers
    /// \returns Count of submitted but not yet completed transfers
    uint32_t GetInFlightTransferCount() const { return inFlightRequests_.Size(); }

    /// \brief Reset for new frame
    /// \details Call at beginning of each frame to track transfer completion.
    void BeginFrame(uint32_t frameIndex);

    /// \brief Get staging buffer handle
    /// \returns VkBuffer for the staging buffer pool
    VkBuffer GetStagingBuffer() const { return stagingBuffer_; }

private:
    /// Graphics implementation pointer
    VulkanGraphicsImpl* graphics_;

    /// Staging buffer (CPU-visible)
    VkBuffer stagingBuffer_{VK_NULL_HANDLE};

    /// VMA allocation for staging buffer
    VmaAllocation stagingAllocation_{nullptr};

    /// Mapped CPU pointer for staging buffer
    void* stagingMappedData_{nullptr};

    /// Total staging buffer size
    VkDeviceSize totalSize_{0};

    /// Currently used portion of staging buffer
    VkDeviceSize usedSize_{0};

    /// Current frame index for tracking
    uint32_t frameIndex_{0};

    /// Pending transfers waiting to be submitted
    Vector<VulkanStagingRequest> pendingRequests_;

    /// In-flight transfers waiting for GPU completion
    Vector<VulkanStagingRequest> inFlightRequests_;

    /// Completed transfers ready for cleanup (deferred)
    Vector<VulkanStagingRequest> completedRequests_;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
