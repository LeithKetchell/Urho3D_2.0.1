//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#include "../Precompiled.h"
#include "VulkanComputePipeline.h"
#include "../../IO/Log.h"

#ifdef URHO3D_VULKAN

namespace Urho3D
{

VulkanComputePipeline::VulkanComputePipeline(VkDevice device)
    : device_(device)
{
}

VulkanComputePipeline::~VulkanComputePipeline()
{
    Clear();
}

VkPipeline VulkanComputePipeline::GetOrCreatePipeline(VkPipelineLayout layout, VkShaderModule shader)
{
    if (!device_ || !layout || !shader)
    {
        URHO3D_LOGERROR("VulkanComputePipeline::GetOrCreatePipeline: Invalid parameters");
        return VK_NULL_HANDLE;
    }

    // Generate cache key from shader module handle
    unsigned cacheKey = static_cast<unsigned>(reinterpret_cast<uintptr_t>(shader));

    // Check cache first
    auto it = pipelineCache_.Find(cacheKey);
    if (it != pipelineCache_.End())
    {
        return it->second_;
    }

    // Create new compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = layout;

    // Compute shader stage
    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shader;
    shaderStage.pName = "main";  // GLSL entry point

    pipelineInfo.stage = shaderStage;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanComputePipeline::GetOrCreatePipeline: Failed to create compute pipeline, error code: " + String((int)result));
        return VK_NULL_HANDLE;
    }

    // Cache the pipeline
    pipelineCache_[cacheKey] = pipeline;

    URHO3D_LOGDEBUG("VulkanComputePipeline: Created and cached compute pipeline (total cached: " + String(pipelineCache_.Size()) + ")");

    return pipeline;
}

void VulkanComputePipeline::Clear()
{
    if (!device_)
        return;

    // Destroy all cached pipelines
    for (auto it = pipelineCache_.Begin(); it != pipelineCache_.End(); ++it)
    {
        if (it->second_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, it->second_, nullptr);
        }
    }

    pipelineCache_.Clear();

    URHO3D_LOGDEBUG("VulkanComputePipeline: Cleared all cached pipelines");
}

}

#endif // URHO3D_VULKAN
