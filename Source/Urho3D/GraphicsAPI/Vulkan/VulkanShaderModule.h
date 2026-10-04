//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan shader module management (Phase 8)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class ShaderVariation;

/// Manages shader module creation from SPIR-V bytecode
class VulkanShaderModule
{
public:
    /// Create shader module from SPIR-V bytecode
    static VkShaderModule CreateShaderModule(
        VkDevice device,
        const Vector<uint32_t>& spirvBytecode);

    /// Destroy shader module
    static void DestroyShaderModule(VkDevice device, VkShaderModule module);

    /// Get or compile shader variation to SPIR-V
    static bool GetOrCompileSPIRV(
        ShaderVariation* variation,
        Vector<uint32_t>& spirvBytecode,
        String& errorOutput);
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
