// Copyright (c) 2008-2024 the Urho3D project
// License: MIT

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Container/RefCounted.h"
#include "../../Math/Matrix3x4.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

/// Push constant data for per-batch rendering. Fits within typical 256-byte limit.
/// Total size: 104 bytes (well under limit for maximum compatibility)
struct VulkanBatchPushConstants
{
    /// Model transform matrix (3x4 = 48 bytes)
    Matrix3x4 model;
    /// Normal/inverse model matrix for lighting calculations (3x4 = 48 bytes)
    Matrix3x4 normalMatrix;
    /// Material index for bindless access or material parameter lookup (4 bytes)
    uint32_t materialIndex;
    /// Batch-specific flags (skinning, billboarding, etc.) (4 bytes)
    uint32_t batchFlags;

    /// Total size: 104 bytes
    static constexpr size_t SIZE = sizeof(Matrix3x4) * 2 + sizeof(uint32_t) * 2;
    static constexpr size_t ALIGNMENT = 16; // Vulkan alignment requirement

    VulkanBatchPushConstants() = default;
    VulkanBatchPushConstants(const Matrix3x4& m, uint32_t flags = 0)
        : model(m), normalMatrix(m), materialIndex(0), batchFlags(flags)
    {
    }
};

/// Manages push constant data for a frame of rendering.
/// Tracks all push constant updates and coordinates with command buffer recording.
class VulkanPushConstantManager : public RefCounted
{
public:
    /// Constructor
    VulkanPushConstantManager();

    /// Reset push constant state for new frame
    void Reset();

    /// Queue a batch's push constants for the next draw call
    void QueueBatchConstants(const Matrix3x4& worldTransform, uint32_t materialIndex, uint32_t flags);

    /// Get the current batch's push constants (for recording to command buffer)
    const VulkanBatchPushConstants& GetCurrentConstants() const { return currentConstants_; }

    /// Update current constants with new batch data
    void UpdateConstants(const Matrix3x4& worldTransform, uint32_t materialIndex, uint32_t flags);

    /// Calculate normal matrix from model matrix (for lighting calculations)
    static Matrix3x4 CalculateNormalMatrix(const Matrix3x4& modelMatrix);

    /// Get push constant range definition for pipeline layout
    static VkPushConstantRange GetPushConstantRange(VkShaderStageFlagBits stageFlags = VK_SHADER_STAGE_VERTEX_BIT)
    {
        VkPushConstantRange range{};
        range.stageFlags = stageFlags;
        range.offset = 0;
        range.size = VulkanBatchPushConstants::SIZE;
        return range;
    }

    /// Validate push constants fit within device limits
    static bool ValidateSize(VkPhysicalDeviceProperties& deviceProps)
    {
        return VulkanBatchPushConstants::SIZE <= deviceProps.limits.maxPushConstantsSize;
    }

private:
    /// Current batch push constant data
    VulkanBatchPushConstants currentConstants_;
};

} // namespace Urho3D

#endif // URHO3D_VULKAN
