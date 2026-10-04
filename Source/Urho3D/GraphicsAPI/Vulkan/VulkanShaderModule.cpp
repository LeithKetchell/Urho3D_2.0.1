//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan shader module management (Phase 8)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanShaderModule.h"
#include "VulkanShaderCompiler.h"
#include "../ShaderVariation.h"
#include "../Shader.h"
#include "../../IO/Log.h"

namespace Urho3D

// Disable verbose debug logging for performance
#define VULKAN_DEBUG_LOGGING 0

{

VkShaderModule VulkanShaderModule::CreateShaderModule(
    VkDevice device,
    const Vector<uint32_t>& spirvBytecode)
{
    if (!device || spirvBytecode.Empty())
    {
        URHO3D_LOGERROR("Invalid device or empty SPIR-V bytecode");
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvBytecode.Size() * sizeof(uint32_t);
    createInfo.pCode = !spirvBytecode.Empty() ? &spirvBytecode[0] : nullptr;

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create shader module");
        return nullptr;
    }

    // Disabled for performance - this happens per draw call
    // URHO3D_LOGDEBUG("Shader module created, size: " + String(createInfo.codeSize) + " bytes");
    return shaderModule;
}

void VulkanShaderModule::DestroyShaderModule(VkDevice device, VkShaderModule module)
{
    if (device && module)
    {
        vkDestroyShaderModule(device, module, nullptr);
    }
}

bool VulkanShaderModule::GetOrCompileSPIRV(
    ShaderVariation* variation,
    Vector<uint32_t>& spirvBytecode,
    String& errorOutput)
{
    if (!variation)
    {
        errorOutput = "Invalid shader variation";
        return false;
    }

    // Check if already cached (stored as unsigned char in byteCode_)
    const Vector<unsigned char>& cachedBytecode = variation->GetByteCode();
    if (!cachedBytecode.Empty())
    {
        // Convert cached unsigned char bytecode back to uint32_t
        spirvBytecode.Clear();
        spirvBytecode.Reserve(cachedBytecode.Size() / 4);

        const uint32_t* spirvWords = reinterpret_cast<const uint32_t*>(!cachedBytecode.Empty() ? &cachedBytecode[0] : nullptr);
        for (unsigned i = 0; i < cachedBytecode.Size() / 4; ++i)
        {
            spirvBytecode.Push(spirvWords[i]);
        }

        // Disabled for performance - this happens per draw call
        // URHO3D_LOGDEBUG("Using cached SPIR-V bytecode for: " + variation->GetFullName());
        return true;
    }

    // Get shader source and defines from variation
    Shader* owner = variation->GetOwner();
    if (!owner)
    {
        errorOutput = "Shader variation has no owner";
        return false;
    }

    ShaderType shaderType = variation->GetShaderType();
    String shaderSource = owner->GetSourceCode(shaderType);

    // Disabled for performance - uncomment for debugging shader compilation
    //         variation->GetFullName().CString(), (int)shaderType, shaderSource.Length());
    // const char* src = shaderSource.CString();
    // for (int i = 0; i < 100 && i < (int)shaderSource.Length(); ++i) {
    // }

    if (shaderSource.Empty())
    {
        errorOutput = "Shader has no source code";
        return false;
    }

    String defines = variation->GetDefines();

    // Compile GLSL to SPIR-V using VulkanShaderCompiler
    Vector<uint32_t> compiledSpirv;
    String compilerOutput;

    HashMap<StringHash, ShaderVariation::CustomUniformInfo> customUniformMap;
    if (!VulkanShaderCompiler::CompileGLSLToSPIRV(
        shaderSource,
        defines,
        shaderType,
        compiledSpirv,
        compilerOutput,
        &customUniformMap))
    {
        errorOutput = "SPIR-V compilation failed: " + compilerOutput;
        URHO3D_LOGERROR(errorOutput);
        return false;
    }

    if (compiledSpirv.Empty())
    {
        errorOutput = "SPIR-V compilation produced empty bytecode";
        URHO3D_LOGERROR(errorOutput);
        return false;
    }

    // Cache the bytecode in ShaderVariation (convert uint32_t to unsigned char)
    Vector<unsigned char> cacheData;
    cacheData.Reserve(compiledSpirv.Size() * 4);

    const unsigned char* spirvBytes = reinterpret_cast<const unsigned char*>(!compiledSpirv.Empty() ? &compiledSpirv[0] : nullptr);
    for (unsigned i = 0; i < compiledSpirv.Size() * 4; ++i)
    {
        cacheData.Push(spirvBytes[i]);
    }

    // Store in variation's bytecode field (Phase 9 enhancement: could add dedicated spirvBytecode_ field)
    // For now, we'll use the existing byteCode_ field which is not used in Vulkan backend
    variation->SetByteCode(cacheData);

    // Store custom uniform map (bare uniforms wrapped into CustomVS/CustomPS blocks)
    if (!customUniformMap.Empty())
        variation->SetCustomUniformMap(customUniformMap);

    spirvBytecode = compiledSpirv;

    URHO3D_LOGDEBUG("Compiled SPIR-V for: " + variation->GetFullName() +
                    " (" + String(compiledSpirv.Size() * 4) + " bytes)");

    return true;
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
