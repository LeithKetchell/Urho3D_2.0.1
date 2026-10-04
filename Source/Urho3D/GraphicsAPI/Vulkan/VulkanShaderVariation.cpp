//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan shader variation compilation (Phase 6)

#include "../Precompiled.h"
#include "../Shader.h"
#include "../ShaderVariation.h"
#include "../../Graphics/Graphics.h"
#include "VulkanGraphicsImpl.h"
#include "VulkanShaderModule.h"
#include "../../IO/Log.h"

#ifdef URHO3D_VULKAN

namespace Urho3D
{

bool ShaderVariation::Create_Vulkan()
{
    Release();

    Shader* owner = GetOwner();
    if (!owner)
    {
        compilerOutput_ = "Owner shader is null";
        return false;
    }

    // Get SPIR-V bytecode (compiled or cached)
    Vector<uint32_t> spirvBytecode;
    if (!VulkanShaderModule::GetOrCompileSPIRV(this, spirvBytecode, compilerOutput_))
    {
        URHO3D_LOGERROR("Failed to get SPIR-V for shader: " + GetFullName());
        return false;
    }

    // For now, we don't store the shader module here - it will be created on-demand in PrepareDraw
    // This allows for lazy loading and proper GPU resource management

    URHO3D_LOGDEBUG("Created Vulkan shader variation: " + GetFullName());
    return true;
}

void ShaderVariation::Release_Vulkan()
{
    // Shader module destruction is handled by graphics pipeline cleanup
    // The cached VkShaderModule in object_.ptr_ will be destroyed when the pipeline is destroyed
    // Do NOT destroy it here as it may still be in use by active pipelines

    byteCode_.Clear();
    URHO3D_LOGDEBUG("Released Vulkan shader variation");
}

void ShaderVariation::OnDeviceLost_Vulkan()
{
    // Vulkan device loss is handled at the graphics level
    // No per-shader action needed
}

void ShaderVariation::SetDefines_Vulkan(const String& defines)
{
    // Update defines and invalidate cache
    defines_ = defines;
    byteCode_.Clear();  // Clear cached bytecode so it will be recompiled

    URHO3D_LOGDEBUG("Updated defines for shader: " + GetName() + " to: " + defines);
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
