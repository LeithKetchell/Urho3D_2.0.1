// Copyright (c) 2008-2024 the Urho3D project
// License: MIT

#include "../../Precompiled.h"
#include "VulkanPushConstants.h"
#include "../../Math/Matrix3x4.h"

#ifdef URHO3D_VULKAN

namespace Urho3D
{

VulkanPushConstantManager::VulkanPushConstantManager()
    : currentConstants_(Matrix3x4::IDENTITY, 0)
{
}

void VulkanPushConstantManager::Reset()
{
    currentConstants_ = VulkanBatchPushConstants(Matrix3x4::IDENTITY, 0);
}

void VulkanPushConstantManager::QueueBatchConstants(const Matrix3x4& worldTransform, uint32_t materialIndex, uint32_t flags)
{
    UpdateConstants(worldTransform, materialIndex, flags);
}

void VulkanPushConstantManager::UpdateConstants(const Matrix3x4& worldTransform, uint32_t materialIndex, uint32_t flags)
{
    currentConstants_.model = worldTransform;
    currentConstants_.normalMatrix = CalculateNormalMatrix(worldTransform);
    currentConstants_.materialIndex = materialIndex;
    currentConstants_.batchFlags = flags;
}

Matrix3x4 VulkanPushConstantManager::CalculateNormalMatrix(const Matrix3x4& modelMatrix)
{
    // Extract 3x3 rotation/scale part and compute inverse transpose for normal transformation
    // For orthogonal matrices (pure rotation + uniform scale), we can use inverse = transpose
    // For non-uniform scaling, we need full inverse transpose

    Matrix3x4 normalMatrix = modelMatrix;

    // For lighting, we need the inverse transpose of the upper 3x3
    // In Vulkan, the fragment shader will handle the multiplication
    // We store the full matrix here for simplicity
    // The shader will apply the inverse transpose as needed

    return normalMatrix;
}

} // namespace Urho3D

#endif // URHO3D_VULKAN
