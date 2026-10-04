//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanStagingBufferManager.h"
#include "VulkanGraphicsImpl.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

namespace Urho3D
{

VulkanStagingBufferManager::VulkanStagingBufferManager(Context* context, VulkanGraphicsImpl* graphics) :
    Object(context),
    graphics_(graphics)
{
}

VulkanStagingBufferManager::~VulkanStagingBufferManager()
{
    Release();
}

void VulkanStagingBufferManager::Release()
{
    if (stagingBuffer_ != VK_NULL_HANDLE && graphics_ && graphics_->GetAllocator())
    {
        vmaDestroyBuffer(graphics_->GetAllocator(), stagingBuffer_, stagingAllocation_);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingAllocation_ = nullptr;
    }
    stagingMappedData_ = nullptr;
}

bool VulkanStagingBufferManager::Initialize(VkDeviceSize totalStagingSize)
{
    if (!graphics_)
    {
        URHO3D_LOGERROR("VulkanStagingBufferManager::Initialize - graphics_ is null");
        return false;
    }

    totalSize_ = totalStagingSize;
    usedSize_ = 0;

    // Create staging buffer info
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize_;
    // Usage: staging buffer for uploads
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Allocation info: CPU-visible for writing
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (!graphics_->GetAllocator())
    {
        URHO3D_LOGERROR("VulkanStagingBufferManager::Initialize - allocator not available");
        return false;
    }

    VmaAllocationInfo allocationInfo;
    VkResult result = vmaCreateBuffer(
        graphics_->GetAllocator(),
        &bufferInfo,
        &allocInfo,
        &stagingBuffer_,
        &stagingAllocation_,
        &allocationInfo
    );

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanStagingBufferManager::Initialize - vmaCreateBuffer failed: " + String((int)result));
        return false;
    }

    stagingMappedData_ = allocationInfo.pMappedData;

    URHO3D_LOGINFO("VulkanStagingBufferManager initialized - staging size: " + String(totalSize_ / (1024 * 1024)) + "MB");
    return true;
}

uint32_t VulkanStagingBufferManager::RequestUpload(
    const void* sourceData,
    VkDeviceSize size,
    VkBuffer destinationBuffer,
    VkDeviceSize destinationOffset)
{
    if (!sourceData || size == 0 || destinationBuffer == VK_NULL_HANDLE)
    {
        URHO3D_LOGWARNING("VulkanStagingBufferManager::RequestUpload - invalid parameters");
        return NINDEX;
    }

    if (usedSize_ + size > totalSize_)
    {
        URHO3D_LOGWARNING("VulkanStagingBufferManager::RequestUpload - insufficient staging space");
        return NINDEX;
    }

    // Copy data to staging buffer
    void* stagingPtr = static_cast<uint8_t*>(stagingMappedData_) + usedSize_;
    memcpy(stagingPtr, sourceData, size);

    // Create transfer request
    VulkanStagingRequest request(
        stagingBuffer_,
        destinationBuffer,
        size,
        usedSize_,
        destinationOffset,
        VK_NULL_HANDLE,  // Fence will be set when submitted
        frameIndex_
    );

    pendingRequests_.Push(request);
    usedSize_ += size;

    return pendingRequests_.Size() - 1;
}

uint32_t VulkanStagingBufferManager::SubmitPendingTransfers(VkCommandBuffer commandBuffer)
{
    if (pendingRequests_.Empty())
        return 0;

    uint32_t submitted = 0;

    for (const auto& request : pendingRequests_)
    {
        // Record copy command
        VkBufferCopy region{};
        region.srcOffset = request.stagingOffset;
        region.dstOffset = request.destinationOffset;
        region.size = request.size;

        vkCmdCopyBuffer(commandBuffer, request.stagingBuffer, request.destinationBuffer, 1, &region);

        // Move to in-flight list (fence will be set after command buffer submission)
        inFlightRequests_.Push(request);
        submitted++;
    }

    pendingRequests_.Clear();
    return submitted;
}

uint32_t VulkanStagingBufferManager::ProcessCompletedTransfers()
{
    if (inFlightRequests_.Empty())
        return 0;

    uint32_t completed = 0;
    Vector<VulkanStagingRequest> stillInFlight;

    for (const auto& request : inFlightRequests_)
    {
        if (request.transferFence != VK_NULL_HANDLE)
        {
            VkResult fenceStatus = vkGetFenceStatus(graphics_->GetDevice(), request.transferFence);

            if (fenceStatus == VK_SUCCESS)
            {
                // Transfer completed, destroy fence and recycle staging space
                vkDestroyFence(graphics_->GetDevice(), request.transferFence, nullptr);
                completed++;
            }
            else if (fenceStatus == VK_NOT_READY)
            {
                // Still in progress
                stillInFlight.Push(request);
            }
            // VK_ERROR_DEVICE_LOST would indicate a serious error
        }
    }

    inFlightRequests_ = stillInFlight;

    // Simple heuristic: if all transfers completed, recycle staging space
    if (completed > 0 && inFlightRequests_.Empty())
    {
        usedSize_ = 0;
        URHO3D_LOGDEBUG("VulkanStagingBufferManager - staging space recycled");
    }

    return completed;
}

void VulkanStagingBufferManager::BeginFrame(uint32_t frameIndex)
{
    frameIndex_ = frameIndex;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
