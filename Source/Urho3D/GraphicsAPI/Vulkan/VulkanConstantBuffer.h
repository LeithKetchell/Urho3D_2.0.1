//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan constant buffer (uniform buffer) management (Phase 9)

#pragma once

#ifdef URHO3D_VULKAN

#include <vulkan/vulkan.h>
#include "../../Container/HashMap.h"
#include "../../Container/Vector.h"
#include "VulkanGraphicsImpl.h"

namespace Urho3D
{

/// Vulkan uniform buffer for shader parameters
class VulkanConstantBuffer
{
public:
    VulkanConstantBuffer();
    ~VulkanConstantBuffer();

    /// Create a constant buffer with specified size
    bool Create(unsigned size);

    /// Release the buffer
    void Release();

    /// Update buffer data
    bool SetData(const void* data, unsigned size);

    /// Get the VkBuffer handle
    VkBuffer GetBuffer() const { return buffer_; }

    /// Get the buffer size in bytes
    unsigned GetSize() const { return size_; }

private:
    VkBuffer buffer_{};
    VmaAllocation allocation_{};
    void* mappedData_{};  // Persistent mapping pointer (Quick Win #10)
    unsigned size_{};
};

/// Manager for per-frame constant buffers
class VulkanConstantBufferManager
{
public:
    VulkanConstantBufferManager();
    ~VulkanConstantBufferManager();

    /// Initialize the manager
    bool Initialize();

    /// Release all buffers
    void Release();

    /// Get or create a constant buffer for the given slot
    VulkanConstantBuffer* GetOrCreateBuffer(unsigned slot, unsigned size);

    /// Update a constant buffer
    bool SetBufferData(unsigned slot, const void* data, unsigned size);

    /// Bind all constant buffers to descriptor set
    bool BindToDescriptorSet(VkDescriptorSet descriptorSet, VkPipelineLayout pipelineLayout);

private:
    HashMap<unsigned, VulkanConstantBuffer*> buffers_;
    VulkanGraphicsImpl* impl_{};
};

}

#endif
