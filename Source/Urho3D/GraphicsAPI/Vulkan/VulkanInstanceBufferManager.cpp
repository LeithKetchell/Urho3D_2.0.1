//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//

#include "VulkanInstanceBufferManager.h"

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanGraphicsImpl.h"
#include "../../Graphics/Graphics.h"
#include "../VertexBuffer.h"
#include "../../IO/Log.h"

namespace Urho3D
{

VulkanInstanceBufferManager::VulkanInstanceBufferManager(Context* context, VulkanGraphicsImpl* graphics) :
    Object(context),
    graphics_(graphics),
    allocation_(nullptr),
    vulkanBuffer_(VK_NULL_HANDLE),
    maxInstances_(0),
    allocatedInstances_(0),
    lockedData_(nullptr)
{
}

VulkanInstanceBufferManager::~VulkanInstanceBufferManager()
{
    Release();
}

void VulkanInstanceBufferManager::Release()
{
    if (vulkanBuffer_ != VK_NULL_HANDLE && graphics_ && graphics_->GetAllocator())
    {
        vmaDestroyBuffer(graphics_->GetAllocator(), vulkanBuffer_, allocation_);
        vulkanBuffer_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
    instanceBuffer_ = nullptr;
    lockedData_ = nullptr;
}

bool VulkanInstanceBufferManager::Initialize(uint32_t maxInstances)
{
    if (!graphics_)
    {
        URHO3D_LOGERROR("VulkanInstanceBufferManager::Initialize - graphics_ is null");
        return false;
    }

    maxInstances_ = maxInstances;
    allocatedInstances_ = 0;

    // Calculate buffer size: each instance is VulkanInstanceData (112 bytes)
    VkDeviceSize bufferSize = maxInstances * sizeof(VulkanInstanceData);

    // Create buffer info
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    // Usage: vertex buffer for instance data stream
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Allocation info: CPU-visible for instance writing
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (!graphics_->GetAllocator())
    {
        URHO3D_LOGERROR("VulkanInstanceBufferManager::Initialize - allocator not available");
        return false;
    }

    VmaAllocationInfo allocationInfo;
    VkResult result = vmaCreateBuffer(
        graphics_->GetAllocator(),
        &bufferInfo,
        &allocInfo,
        &vulkanBuffer_,
        &allocation_,
        &allocationInfo
    );

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanInstanceBufferManager::Initialize - vmaCreateBuffer failed: " + String((int)result));
        return false;
    }

    lockedData_ = allocationInfo.pMappedData;

    URHO3D_LOGINFO("VulkanInstanceBufferManager initialized - max instances: " + String(maxInstances));
    return true;
}

VertexBuffer* VulkanInstanceBufferManager::GetInstanceBuffer()
{
    return instanceBuffer_;
}

void* VulkanInstanceBufferManager::AllocateInstances(uint32_t instanceCount, uint32_t& outStride, uint32_t& outStartIndex)
{
    if (!vulkanBuffer_ || !lockedData_ || allocatedInstances_ + instanceCount > maxInstances_)
    {
        URHO3D_LOGWARNING("VulkanInstanceBufferManager::AllocateInstances - insufficient space");
        return nullptr;
    }

    outStride = VulkanInstanceData::GetStride();
    outStartIndex = allocatedInstances_;

    allocatedInstances_ += instanceCount;

    // Return pointer to the allocated region in mapped memory
    return static_cast<uint8_t*>(lockedData_) + outStartIndex * outStride;
}

bool VulkanInstanceBufferManager::UpdateInstances(uint32_t startIndex, const VulkanInstanceData* instances, uint32_t count)
{
    if (!vulkanBuffer_ || !lockedData_ || startIndex + count > allocatedInstances_)
    {
        URHO3D_LOGWARNING("VulkanInstanceBufferManager::UpdateInstances - out of bounds");
        return false;
    }

    uint32_t stride = VulkanInstanceData::GetStride();
    void* data = static_cast<uint8_t*>(lockedData_) + startIndex * stride;
    memcpy(data, instances, count * stride);

    return true;
}

void VulkanInstanceBufferManager::Reset()
{
    allocatedInstances_ = 0;
}

VkBuffer VulkanInstanceBufferManager::GetVulkanBuffer() const
{
    return vulkanBuffer_;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
