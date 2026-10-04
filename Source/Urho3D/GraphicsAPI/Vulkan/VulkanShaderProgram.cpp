//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan shader program implementation (Phase 9)

#include "../Precompiled.h"
#include "VulkanShaderProgram.h"
#include "../ShaderVariation.h"
#include "../ConstantBuffer.h"
#include "../../IO/Log.h"

#ifdef URHO3D_VULKAN

namespace Urho3D
{

VulkanShaderProgram::VulkanShaderProgram(ShaderVariation* vs, ShaderVariation* ps) :
    vertexShader_(vs),
    pixelShader_(ps)
{
    LinkParameters();
}

VulkanShaderProgram::~VulkanShaderProgram()
{
    // Constant buffers are cleaned up automatically via SharedPtr
    // Pipeline layout should be cleaned up by caller before destruction
}

const ShaderParameter* VulkanShaderProgram::GetParameter(StringHash param) const
{
    auto it = parameters_.Find(param);
    if (it != parameters_.End())
        return &it->second_;
    return nullptr;
}

void VulkanShaderProgram::LinkParameters()
{
    parameters_.Clear();

    // Combine parameters from vertex shader
    if (vertexShader_)
    {
        const HashMap<StringHash, ShaderParameter>& vsParams = vertexShader_->GetParameters();
        for (auto it = vsParams.Begin(); it != vsParams.End(); ++it)
        {
            parameters_[it->first_] = it->second_;
        }
    }

    // Combine parameters from pixel shader
    if (pixelShader_)
    {
        const HashMap<StringHash, ShaderParameter>& psParams = pixelShader_->GetParameters();
        for (auto it = psParams.Begin(); it != psParams.End(); ++it)
        {
            parameters_[it->first_] = it->second_;
        }
    }

    // Initialize constant buffers for each parameter group if not already created
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
    {
        if (!constantBuffers_[i])
        {
            // Constant buffers will be created on-demand when parameters are set
            // For now, leave as null - they'll be created in SetShaderParameter
        }
    }

    // Phase 23: Clear reflected resources (will be populated during shader compilation)
    reflectedResources_.Clear();
}

bool VulkanShaderProgram::CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout)
{
    if (!device)
        return false;

    // Create pipeline layout with push constants and descriptor sets
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // Add descriptor set layout if provided
    if (descriptorSetLayout)
    {
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
    }

    // Add push constants for fast-changing data (Phase 9)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 128;  // 104 bytes for VulkanBatchPushConstants + padding

    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create pipeline layout");
        return false;
    }

    return true;
}

bool VulkanShaderProgram::CreatePipelineLayout(VkDevice device, const Vector<VkDescriptorSetLayout>& descriptorSetLayouts)
{
    // Phase 36: Multi-set pipeline layout creation for deferred rendering
    // Supports up to 3 descriptor sets:
    //   Set 0: Material descriptors (textures, samplers from material)
    //   Set 1: G-Buffer texture descriptors (for lighting pass)
    //   Set 2: Constant buffer descriptors (light parameters)

    if (!device)
        return false;

    // Create pipeline layout with push constants and multiple descriptor sets
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // Add all descriptor set layouts
    if (!descriptorSetLayouts.Empty())
    {
        layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.Size());
        layoutInfo.pSetLayouts = &descriptorSetLayouts[0];
    }

    // Add push constants for fast-changing data (Phase 9)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 128;  // 104 bytes for VulkanBatchPushConstants + padding

    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create pipeline layout with " + String(descriptorSetLayouts.Size()) + " descriptor sets");
        return false;
    }

    URHO3D_LOGDEBUG("Created pipeline layout with " + String(descriptorSetLayouts.Size()) + " descriptor set(s)");
    return true;
}

void VulkanShaderProgram::DestroyPipelineLayout(VkDevice device)
{
    if (device && pipelineLayout_)
    {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = nullptr;
    }
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
