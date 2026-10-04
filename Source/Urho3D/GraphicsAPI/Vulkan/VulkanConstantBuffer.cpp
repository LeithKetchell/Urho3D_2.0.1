//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan constant buffer (uniform buffer) stub implementation (Phase 9 - Simplified)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanConstantBuffer.h"
#include "../../IO/Log.h"

namespace Urho3D
{

// ============================================
// VulkanConstantBuffer - Stub Implementation
// ============================================

VulkanConstantBuffer::VulkanConstantBuffer()
{
}

VulkanConstantBuffer::~VulkanConstantBuffer()
{
    Release();
}

bool VulkanConstantBuffer::Create(unsigned size)
{
    Release();

    if (!size)
        return false;

    size_ = size;
    buffer_ = nullptr;  // Stub - would allocate with vmaCreateBuffer in full implementation
    mappedData_ = nullptr;

    URHO3D_LOGDEBUG("VulkanConstantBuffer::Create stub called (size=" + String(size) + ")");
    return true;
}

void VulkanConstantBuffer::Release()
{
    if (!buffer_)
        return;

    // Stub - would call vmaDestroyBuffer in full implementation
    buffer_ = nullptr;
    allocation_ = nullptr;
    mappedData_ = nullptr;
    size_ = 0;
}

bool VulkanConstantBuffer::SetData(const void* data, unsigned size)
{
    if (!data || size > size_)
        return false;

    // Stub - would copy to persistently mapped memory in full implementation
    return true;
}

// ============================================
// VulkanConstantBufferManager - Stub
// ============================================

VulkanConstantBufferManager::VulkanConstantBufferManager()
{
}

VulkanConstantBufferManager::~VulkanConstantBufferManager()
{
    Release();
}

bool VulkanConstantBufferManager::Initialize()
{
    // Stub - would get graphics implementation in full implementation
    impl_ = nullptr;
    URHO3D_LOGDEBUG("VulkanConstantBufferManager::Initialize stub called");
    return true;
}

void VulkanConstantBufferManager::Release()
{
    for (auto it = buffers_.Begin(); it != buffers_.End(); ++it)
    {
        delete it->second_;
    }
    buffers_.Clear();
}

VulkanConstantBuffer* VulkanConstantBufferManager::GetOrCreateBuffer(unsigned slot, unsigned size)
{
    if (!impl_)
    {
        // Fallback: create buffer without impl
        VulkanConstantBuffer* buffer = buffers_[slot];
        if (!buffer)
        {
            buffer = new VulkanConstantBuffer();
            if (buffer->Create(size))
            {
                buffers_[slot] = buffer;
            }
            else
            {
                delete buffer;
                return nullptr;
            }
        }
        return buffer;
    }

    VulkanConstantBuffer* buffer = buffers_[slot];
    if (!buffer)
    {
        buffer = new VulkanConstantBuffer();
        if (buffer->Create(size))
        {
            buffers_[slot] = buffer;
        }
        else
        {
            delete buffer;
            return nullptr;
        }
    }

    return buffer;
}

bool VulkanConstantBufferManager::SetBufferData(unsigned slot, const void* data, unsigned size)
{
    VulkanConstantBuffer* buffer = GetOrCreateBuffer(slot, size);
    if (!buffer)
        return false;

    return buffer->SetData(data, size);
}

bool VulkanConstantBufferManager::BindToDescriptorSet(VkDescriptorSet descriptorSet,
                                                       VkPipelineLayout pipelineLayout)
{
    // Stub - would bind buffers to descriptor sets in full implementation
    URHO3D_LOGDEBUG("VulkanConstantBufferManager::BindToDescriptorSet stub called");
    return true;
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
