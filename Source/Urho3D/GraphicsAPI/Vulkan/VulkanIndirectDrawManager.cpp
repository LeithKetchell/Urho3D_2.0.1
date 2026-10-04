//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanIndirectDrawManager.h"
#include "VulkanGraphicsImpl.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

namespace Urho3D
{

VulkanIndirectDrawManager::VulkanIndirectDrawManager(Context* context, VulkanGraphicsImpl* graphics) :
    Object(context),
    graphics_(graphics),
    commandBuffer_(VK_NULL_HANDLE),
    allocation_(nullptr),
    mappedData_(nullptr),
    maxCommands_(0),
    commandCount_(0)
{
}

VulkanIndirectDrawManager::~VulkanIndirectDrawManager()
{
    Release();
}

void VulkanIndirectDrawManager::Release()
{
    if (commandBuffer_ != VK_NULL_HANDLE && graphics_ && graphics_->GetAllocator())
    {
        vmaDestroyBuffer(graphics_->GetAllocator(), commandBuffer_, allocation_);
        commandBuffer_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
    mappedData_ = nullptr;
}

bool VulkanIndirectDrawManager::Initialize(uint32_t maxCommands)
{
    if (!graphics_)
    {
        URHO3D_LOGERROR("VulkanIndirectDrawManager::Initialize - graphics_ is null");
        return false;
    }

    maxCommands_ = maxCommands;
    commandCount_ = 0;

    // Calculate buffer size: each command is VkDrawIndexedIndirectCommand (20 bytes)
    VkDeviceSize bufferSize = maxCommands * sizeof(VkDrawIndexedIndirectCommand);

    // Create buffer info
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    // Usage: indirect draw + transfer dst for updates
    bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Allocation info: CPU-visible for command writing
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (!graphics_->GetAllocator())
    {
        URHO3D_LOGERROR("VulkanIndirectDrawManager::Initialize - allocator not available");
        return false;
    }

    VmaAllocationInfo allocationInfo;
    VkResult result = vmaCreateBuffer(
        graphics_->GetAllocator(),
        &bufferInfo,
        &allocInfo,
        &commandBuffer_,
        &allocation_,
        &allocationInfo
    );

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanIndirectDrawManager::Initialize - vmaCreateBuffer failed: " + String((int)result));
        return false;
    }

    mappedData_ = allocationInfo.pMappedData;
    stagingCommands_.Clear();

    URHO3D_LOGINFO("VulkanIndirectDrawManager initialized - max commands: " + String(maxCommands));
    return true;
}

uint32_t VulkanIndirectDrawManager::AddCommand(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    if (commandCount_ >= maxCommands_)
    {
        URHO3D_LOGWARNING("VulkanIndirectDrawManager::AddCommand - command buffer full");
        return NINDEX;
    }

    VulkanIndirectDrawCommand cmd(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    stagingCommands_.Push(cmd);

    uint32_t index = commandCount_;
    commandCount_++;

    return index;
}

void VulkanIndirectDrawManager::Reset()
{
    // Copy staged commands to GPU buffer
    if (mappedData_ && !stagingCommands_.Empty())
    {
        memcpy(mappedData_, stagingCommands_.Buffer(), stagingCommands_.Size() * sizeof(VulkanIndirectDrawCommand));
    }

    stagingCommands_.Clear();
    commandCount_ = 0;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
