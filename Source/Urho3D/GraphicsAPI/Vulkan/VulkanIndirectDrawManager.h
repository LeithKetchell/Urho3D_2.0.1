//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//
// Vulkan Indirect Draw Command Support (Phase 12)

#pragma once

#include "../../Container/Vector.h"
#include "../../Core/Object.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// \brief Vulkan indirect draw command structure
/// \details Matches VkDrawIndexedIndirectCommand for vkCmdDrawIndexedIndirect()
struct VulkanIndirectDrawCommand
{
    /// Number of indices to draw
    uint32_t indexCount;

    /// Number of instances to draw
    uint32_t instanceCount;

    /// Index buffer offset (in indices, not bytes)
    uint32_t firstIndex;

    /// Vertex buffer offset (added to vertex indices)
    int32_t vertexOffset;

    /// First instance index (for instance differentiation)
    uint32_t firstInstance;

    /// Constructor with parameters
    VulkanIndirectDrawCommand(uint32_t idxCount = 0, uint32_t instCount = 1, uint32_t fidx = 0, int32_t voff = 0, uint32_t finst = 0) :
        indexCount(idxCount),
        instanceCount(instCount),
        firstIndex(fidx),
        vertexOffset(voff),
        firstInstance(finst)
    {
    }
};

/// \brief Manages indirect draw command buffers for GPU-driven rendering
/// \details Allocates GPU-side command buffer for indirect draws, supporting up to 1M commands.
/// Commands can be recorded once and replayed multiple times, reducing CPU overhead.
class VulkanIndirectDrawManager : public Object
{
    URHO3D_OBJECT(VulkanIndirectDrawManager, Object);

public:
    /// Constructor with context and graphics implementation
    explicit VulkanIndirectDrawManager(Context* context, VulkanGraphicsImpl* graphics);

    /// Destructor - releases GPU resources
    ~VulkanIndirectDrawManager();

    /// Release GPU resources
    void Release();

    /// \brief Initialize indirect command buffer
    /// \param maxCommands Maximum number of indirect draw commands to support
    /// \returns True if initialization successful
    bool Initialize(uint32_t maxCommands = 65536);

    /// \brief Add indirect draw command
    /// \param indexCount Number of indices to draw
    /// \param instanceCount Number of instances
    /// \param firstIndex Starting index in index buffer
    /// \param vertexOffset Base vertex offset
    /// \param firstInstance First instance index
    /// \returns Command index for later reference
    uint32_t AddCommand(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);

    /// \brief Get indirect command buffer handle
    /// \returns VkBuffer for vkCmdDrawIndexedIndirect()
    VkBuffer GetCommandBuffer() const { return commandBuffer_; }

    /// \brief Get device offset for commands
    /// \returns GPU buffer offset in bytes
    VkDeviceSize GetCommandBufferOffset() const { return 0; }

    /// \brief Get number of recorded commands
    /// \returns Current command count
    uint32_t GetCommandCount() const { return commandCount_; }

    /// \brief Reset for next frame
    void Reset();

    /// \brief Get stride between commands (always sizeof(VkDrawIndexedIndirectCommand) = 20 bytes)
    /// \returns Command stride
    static constexpr VkDeviceSize GetCommandStride() { return sizeof(VkDrawIndexedIndirectCommand); }

private:
    /// Graphics implementation pointer
    VulkanGraphicsImpl* graphics_;

    /// GPU-side command buffer
    VkBuffer commandBuffer_;

    /// Buffer allocation
    VmaAllocation allocation_;

    /// Mapped CPU-side pointer for command writing
    void* mappedData_;

    /// Maximum commands
    uint32_t maxCommands_;

    /// Currently recorded commands
    uint32_t commandCount_;

    /// Staging vector for command collection before GPU upload
    Vector<VulkanIndirectDrawCommand> stagingCommands_;
};

} // namespace Urho3D
