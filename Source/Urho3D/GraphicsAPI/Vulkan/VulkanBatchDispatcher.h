// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#pragma once

#include "../../Core/Object.h"
#include "../../Container/Vector.h"
#include "VulkanInstanceData.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class Batch;
class BatchGroup;
class Camera;
class View;
class VulkanGraphicsImpl;
class VulkanMaterialDescriptorManager;
class VulkanPushConstantManager;

/// Information about a recorded batch draw with push constants
struct VulkanBatchDrawInfo
{
    /// Geometry type
    uint32_t geometryType_;
    /// Instance count
    uint32_t instanceCount_;
    /// Index buffer offset
    uint32_t firstIndex_;
    /// Vertex buffer offset
    int32_t vertexOffset_;
    /// First instance index (or instance buffer start index for batch groups)
    uint32_t firstInstance_;
    /// World transform matrix for this batch
    Matrix3x4 worldTransform_;
    /// Material index for descriptor binding
    uint32_t materialIndex_;
    /// Instance buffer handle (Phase 15.1) - non-zero if this batch uses instancing
    VkBuffer instanceBuffer_{VK_NULL_HANDLE};
    /// Instance buffer offset in bytes (Phase 15.1)
    VkDeviceSize instanceBufferOffset_{0};
};

/// Converts Urho3D Batch/BatchGroup rendering calls to Vulkan indirect draws.
/// Records batch data to GPU buffers and submits as single indirect draw command.
///
/// \brief GPU-driven batch dispatcher for efficient Vulkan rendering
/// \details This class intercepts Batch::Draw() calls and instead of immediate rendering,
/// records them to an indirect draw command buffer. All batches with compatible state
/// are then submitted in a single vkCmdDrawIndexedIndirect call, dramatically reducing
/// CPU-to-GPU overhead and improving cache locality.
///
/// Typical usage:
/// \code
/// VulkanBatchDispatcher dispatcher;
/// dispatcher.Initialize(graphics);
/// for (auto& batch : batches) {
///     batch.RecordToIndirectBuffer(view, camera, &dispatcher);
/// }
/// dispatcher.SubmitIndirectDraws(cmdBuffer);
/// \endcode
///
/// Thread Safety: Not thread-safe. Must be used from single render thread per frame.
/// Memory: Uses VulkanIndirectDrawManager from VulkanGraphicsImpl for GPU buffers.
class VulkanBatchDispatcher : public Object
{
    URHO3D_OBJECT(VulkanBatchDispatcher, Object);

public:
    /// Construct dispatcher.
    VulkanBatchDispatcher(Context* context);

    /// Destruct dispatcher.
    ~VulkanBatchDispatcher();

    /// Initialize with graphics implementation and allocate GPU buffers.
    /// \param graphics Vulkan graphics implementation instance
    /// \return True if initialization successful
    bool Initialize(VulkanGraphicsImpl* graphics);

    /// Record a single batch to the indirect command buffer.
    /// Called from Batch::Draw() during rendering pipeline.
    /// \param batch Batch to record
    /// \param view Current rendering view
    /// \param camera Active camera
    /// \return True if batch was recorded successfully
    bool RecordBatch(const Batch* batch, View* view, Camera* camera);

    /// Record a batch group (multiple instances) to indirect command buffer.
    /// Called from BatchGroup::Draw() during rendering pipeline.
    /// \param group Batch group to record
    /// \param view Current rendering view
    /// \param camera Active camera
    /// \return True if batch group was recorded successfully
    bool RecordBatchGroup(const BatchGroup* group, View* view, Camera* camera);

    /// Submit all recorded indirect draw commands.
    /// Called once per frame after all batches have been recorded.
    /// \param cmdBuffer Command buffer to record draw calls into
    void SubmitIndirectDraws(VkCommandBuffer cmdBuffer);

    /// Clear all recorded data for next frame.
    /// Must be called at end of frame before next frame's recording begins.
    void Reset();

    /// Get number of recorded batches waiting for submission.
    uint32_t GetPendingBatchCount() const { return static_cast<uint32_t>(batchInfo_.Size()); }

    /// Get number of recorded indirect draw commands.
    uint32_t GetRecordedCommandCount() const { return static_cast<uint32_t>(indirectCommands_.Size()); }

    /// Check if dispatcher is initialized and ready for batch recording.
    bool IsInitialized() const { return graphics_ != nullptr; }

private:
    /// Parent graphics implementation
    VulkanGraphicsImpl* graphics_;

    /// Material descriptor manager (created in Initialize)
    SharedPtr<VulkanMaterialDescriptorManager> materialDescriptors_;

    /// Push constant manager for batch transforms (Phase 13)
    SharedPtr<VulkanPushConstantManager> pushConstantMgr_;

    /// Recorded indirect draw commands waiting for submission
    Vector<VkDrawIndexedIndirectCommand> indirectCommands_;

    /// Per-batch metadata for tracking (includes transforms)
    Vector<VulkanBatchDrawInfo> batchInfo_;

    /// Current pipeline state for batching optimization
    VkPipeline currentPipeline_;
    VkPipelineLayout currentPipelineLayout_;
};

}
