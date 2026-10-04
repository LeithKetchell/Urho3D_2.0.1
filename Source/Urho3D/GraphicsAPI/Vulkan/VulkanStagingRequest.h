//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//
// Vulkan Staging Buffer Request Structure (Phase 10 Enhancement)

#pragma once

#ifdef URHO3D_VULKAN

#include <vulkan/vulkan.h>

namespace Urho3D
{

/// \brief Represents a pending GPU transfer request
/// \details Encapsulates all information needed to perform an asynchronous
/// GPU transfer from a staging buffer to a device-local buffer.
struct VulkanStagingRequest
{
    /// Source staging buffer
    VkBuffer stagingBuffer{VK_NULL_HANDLE};

    /// Destination device-local buffer
    VkBuffer destinationBuffer{VK_NULL_HANDLE};

    /// Size of data to transfer in bytes
    VkDeviceSize size{0};

    /// Offset in staging buffer
    VkDeviceSize stagingOffset{0};

    /// Offset in destination buffer
    VkDeviceSize destinationOffset{0};

    /// Fence to signal when transfer completes
    VkFence transferFence{VK_NULL_HANDLE};

    /// Frame index this request was submitted in
    uint32_t frameIndex{0};

    /// Optional callback data (for deferred cleanup)
    void* userData{nullptr};

    /// \brief Default constructor
    VulkanStagingRequest() = default;

    /// \brief Parameterized constructor
    VulkanStagingRequest(
        VkBuffer src,
        VkBuffer dst,
        VkDeviceSize transferSize,
        VkDeviceSize srcOffset,
        VkDeviceSize dstOffset,
        VkFence fence,
        uint32_t frame,
        void* data = nullptr
    ) : stagingBuffer(src),
        destinationBuffer(dst),
        size(transferSize),
        stagingOffset(srcOffset),
        destinationOffset(dstOffset),
        transferFence(fence),
        frameIndex(frame),
        userData(data)
    {
    }

    /// \brief Check if request is valid
    /// \returns True if all required fields are set
    bool IsValid() const
    {
        return stagingBuffer != VK_NULL_HANDLE &&
               destinationBuffer != VK_NULL_HANDLE &&
               size > 0 &&
               transferFence != VK_NULL_HANDLE;
    }
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
