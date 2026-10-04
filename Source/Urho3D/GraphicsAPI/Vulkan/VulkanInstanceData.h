//
// Copyright (c) 2008-2025 the Urho3D project.
// License: MIT
//
// Vulkan GPU Instancing Data Structures (Phase 12)

#pragma once

#include "../../Math/Matrix3x4.h"
#include "../../Math/Vector4.h"

namespace Urho3D
{

/// \brief Extended GPU instance data for Vulkan vertex buffer streaming
/// \details Combines transform, normal matrix, material index, and custom per-instance data
/// into a single structure for efficient vertex buffer instancing with divisor=1 binding.
/// Total: 112 bytes per instance (optimal for GPU alignment)
struct VulkanInstanceData
{
    /// \brief Constructor - initialize with transform
    /// \param worldTransform World transformation matrix (3x4 = 48 bytes)
    explicit VulkanInstanceData(const Matrix3x4& worldTransform = Matrix3x4::IDENTITY) :
        worldTransform(worldTransform),
        materialIndex(0),
        customFlags(0)
    {
        // Normal matrix is typically computed in shader or set separately
        normalMatrix = Matrix3x4::ZERO;
    }

    /// World transformation matrix (3x4)
    /// Maps local coordinates to world space
    /// Size: 48 bytes (12 floats)
    Matrix3x4 worldTransform;

    /// Normal transformation matrix (3x4)
    /// For normal mapping - upper 3x3 of inverse-transpose of world matrix
    /// Can be computed in shader or pre-calculated
    /// Size: 48 bytes (12 floats)
    Matrix3x4 normalMatrix;

    /// Material index for bindless texture/material access
    /// Used to index into material constant buffer or texture arrays
    /// Range: 0 to 65535 (16-bit addressing in instanced descriptor set)
    /// Size: 4 bytes
    uint32_t materialIndex;

    /// Custom per-instance flags
    /// Bits 0-7: Visibility/layer flags
    /// Bits 8-15: Animation frame index
    /// Bits 16-23: Custom effect flags (fog, reflection, etc.)
    /// Bits 24-31: Reserved for future use
    /// Size: 4 bytes
    uint32_t customFlags;

    /// Padding for GPU alignment (8-byte boundary for optimal cache coherency)
    /// Reserved for future expansion (e.g., per-instance color, scale)
    /// Size: 8 bytes
    uint64_t reserved;

    /// \brief Calculate stride for instancing buffer
    /// \returns Size in bytes: always 112 for this structure
    static constexpr uint32_t GetStride() { return sizeof(VulkanInstanceData); }

    /// \brief Set material index
    /// \param index Material index (0-65535)
    void SetMaterialIndex(uint32_t index) { materialIndex = index & 0xFFFF; }

    /// \brief Get material index
    /// \returns Material index
    uint32_t GetMaterialIndex() const { return materialIndex & 0xFFFF; }

    /// \brief Set custom flags
    /// \param flags Custom flag bits
    void SetCustomFlags(uint32_t flags) { customFlags = flags; }

    /// \brief Get custom flags
    /// \returns Custom flag bits
    uint32_t GetCustomFlags() const { return customFlags; }
};

// Verify structure size for GPU alignment
static_assert(sizeof(VulkanInstanceData) == 112, "VulkanInstanceData must be 112 bytes for optimal GPU alignment");
static_assert(offsetof(VulkanInstanceData, worldTransform) == 0, "worldTransform offset must be 0");
static_assert(offsetof(VulkanInstanceData, normalMatrix) == 48, "normalMatrix offset must be 48");
static_assert(offsetof(VulkanInstanceData, materialIndex) == 96, "materialIndex offset must be 96");
static_assert(offsetof(VulkanInstanceData, customFlags) == 100, "customFlags offset must be 100");
static_assert(offsetof(VulkanInstanceData, reserved) == 104, "reserved offset must be 104");

} // namespace Urho3D
