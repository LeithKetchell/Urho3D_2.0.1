//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanSecondaryCommandBuffer.h"
#include "VulkanGraphicsImpl.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

namespace Urho3D
{

// ===========================
// VulkanSecondaryCommandBuffer
// ===========================

VulkanSecondaryCommandBuffer::VulkanSecondaryCommandBuffer(VulkanGraphicsImpl* impl)
    : impl_(impl)
{
}

VulkanSecondaryCommandBuffer::~VulkanSecondaryCommandBuffer()
{
    if (impl_ && commandPool_)
    {
        VkDevice device = impl_->GetDevice();
        if (commandBuffer_)
            vkFreeCommandBuffers(device, commandPool_, 1, &commandBuffer_);
        vkDestroyCommandPool(device, commandPool_, nullptr);
    }
}

bool VulkanSecondaryCommandBuffer::Initialize(uint32_t threadIndex, VkCommandBufferLevel level)
{
    if (!impl_)
        return false;

    threadIndex_ = threadIndex;
    level_ = level;

    VkDevice device = impl_->GetDevice();
    uint32_t queueFamilyIndex = 0;  // Assuming graphics queue family at index 0

    // Create command pool for this thread
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create secondary command buffer pool for thread " + String(threadIndex));
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = level_;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to allocate secondary command buffer for thread " + String(threadIndex));
        vkDestroyCommandPool(device, commandPool_, nullptr);
        commandPool_ = nullptr;
        return false;
    }

    return true;
}

bool VulkanSecondaryCommandBuffer::Begin(VkCommandBufferUsageFlags flags)
{
    if (!commandBuffer_ || isRecording_)
        return false;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;

    // For secondary buffers inheriting render pass state from primary
    VkCommandBufferInheritanceInfo inheritanceInfo{};
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.renderPass = impl_->GetRenderPass();
    inheritanceInfo.subpass = 0;  // Default subpass
    inheritanceInfo.framebuffer = impl_->GetCurrentFramebuffer();

    if (level_ == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
        beginInfo.pInheritanceInfo = &inheritanceInfo;

    if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to begin secondary command buffer recording for thread " + String(threadIndex_));
        return false;
    }

    isRecording_ = true;
    return true;
}

bool VulkanSecondaryCommandBuffer::End()
{
    if (!commandBuffer_ || !isRecording_)
        return false;

    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to end secondary command buffer recording for thread " + String(threadIndex_));
        return false;
    }

    isRecording_ = false;
    return true;
}

bool VulkanSecondaryCommandBuffer::Reset(VkCommandBufferResetFlags flags)
{
    if (!commandBuffer_ || isRecording_)
        return false;

    if (vkResetCommandBuffer(commandBuffer_, flags) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to reset secondary command buffer for thread " + String(threadIndex_));
        return false;
    }

    return true;
}

// =======================================
// VulkanSecondaryCommandBufferPool
// =======================================

VulkanSecondaryCommandBufferPool::VulkanSecondaryCommandBufferPool(VulkanGraphicsImpl* impl)
    : impl_(impl)
{
}

VulkanSecondaryCommandBufferPool::~VulkanSecondaryCommandBufferPool()
{
    buffers_.Clear();
}

bool VulkanSecondaryCommandBufferPool::Initialize(uint32_t numThreads)
{
    if (!impl_)
        return false;

    buffers_.Resize(numThreads);

    for (uint32_t i = 0; i < numThreads; ++i)
    {
        auto buffer = MakeShared<VulkanSecondaryCommandBuffer>(impl_);
        if (!buffer->Initialize(i, VK_COMMAND_BUFFER_LEVEL_SECONDARY))
        {
            URHO3D_LOGERROR("Failed to initialize secondary command buffer for thread " + String(i));
            buffers_.Clear();
            return false;
        }
        buffers_[i] = buffer;
    }

    URHO3D_LOGINFO("Initialized secondary command buffer pool with " + String(numThreads) + " threads");
    return true;
}

VulkanSecondaryCommandBuffer* VulkanSecondaryCommandBufferPool::GetThreadBuffer(uint32_t threadIndex)
{
    if (threadIndex >= buffers_.Size())
        return nullptr;

    return buffers_[threadIndex].Get();
}

void VulkanSecondaryCommandBufferPool::ResetAllBuffers()
{
    for (auto& buffer : buffers_)
    {
        if (buffer)
            buffer->Reset();
    }
}

Vector<VkCommandBuffer> VulkanSecondaryCommandBufferPool::GetRecordedCommandBuffers() const
{
    Vector<VkCommandBuffer> result;
    result.Reserve(buffers_.Size());

    for (const auto& buffer : buffers_)
    {
        if (buffer && !buffer->IsRecording())  // Only get finished buffers
            result.Push(buffer->GetHandle());
    }

    return result;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
