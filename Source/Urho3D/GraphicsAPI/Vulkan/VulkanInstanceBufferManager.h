//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//
// Vulkan Instance Buffer Management (Phase 12)

#pragma once

#include "../../Container/Vector.h"
#include "../../Core/Object.h"
#include "../../Math/Matrix3x4.h"
#include "VulkanInstanceData.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Urho3D
{

class VertexBuffer;
class VulkanGraphicsImpl;

/// \brief Manages GPU vertex buffer for instance data streaming
/// \details Allocates and updates a dynamic vertex buffer used as an additional
/// vertex stream (stream 2+) for per-instance attributes. Supports up to 1M instances.
/// Uses VMA for efficient memory management and supports dynamic updates.
class VulkanInstanceBufferManager : public Object
{
    URHO3D_OBJECT(VulkanInstanceBufferManager, Object);

public:
    /// Constructor with context and graphics implementation
    explicit VulkanInstanceBufferManager(Context* context, VulkanGraphicsImpl* graphics);

    /// Destructor - releases GPU resources
    ~VulkanInstanceBufferManager();

    /// Release GPU resources
    void Release();

    /// \brief Initialize instance buffer with specified capacity
    /// \param maxInstances Maximum number of instances to support
    /// \returns True if initialization successful
    bool Initialize(uint32_t maxInstances = 65536);

    /// \brief Get or create instance buffer for current frame
    /// \returns Vertex buffer for binding as additional stream
    VertexBuffer* GetInstanceBuffer();

    /// \brief Allocate space for instances and return data pointer
    /// \param instanceCount Number of instances
    /// \param outStride Output parameter: bytes per instance
    /// \param outStartIndex Output parameter: first instance index
    /// \returns Pointer to locked buffer data, or null on failure
    void* AllocateInstances(uint32_t instanceCount, uint32_t& outStride, uint32_t& outStartIndex);

    /// \brief Update instance data for a batch group
    /// \param startIndex Starting index in instance buffer
    /// \param instances Array of VulkanInstanceData
    /// \param count Number of instances
    /// \returns True if update successful
    bool UpdateInstances(uint32_t startIndex, const VulkanInstanceData* instances, uint32_t count);

    /// \brief Reset for new frame - clears allocation pointer
    void Reset();

    /// \brief Get current number of allocated instances
    /// \returns Number of instances currently in buffer
    uint32_t GetAllocatedInstanceCount() const { return allocatedInstances_; }

    /// \brief Get maximum instance capacity
    /// \returns Maximum instances the buffer can hold
    uint32_t GetMaxInstances() const { return maxInstances_; }

    /// \brief Get instance stride in bytes
    /// \returns Always VulkanInstanceData::GetStride() = 112 bytes
    uint32_t GetInstanceStride() const { return VulkanInstanceData::GetStride(); }

    /// \brief Get Vulkan buffer handle
    /// \returns VkBuffer for direct binding
    VkBuffer GetVulkanBuffer() const;

private:
    /// Graphics implementation pointer
    VulkanGraphicsImpl* graphics_;

    /// Instance vertex buffer
    SharedPtr<VertexBuffer> instanceBuffer_;

    /// VMA allocation
    VmaAllocation allocation_;

    /// VMA-allocated buffer handle
    VkBuffer vulkanBuffer_;

    /// Maximum instances
    uint32_t maxInstances_;

    /// Currently allocated instances
    uint32_t allocatedInstances_;

    /// Current frame data pointer (if locked)
    void* lockedData_;
};

} // namespace Urho3D
