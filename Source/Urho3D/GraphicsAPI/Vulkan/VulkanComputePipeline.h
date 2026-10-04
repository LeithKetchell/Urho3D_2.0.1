//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/HashMap.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class ShaderVariation;

/// \brief Vulkan compute pipeline cache and management
/// \details Manages compute pipeline creation, caching, and lifecycle.
/// Compute pipelines are separate from graphics pipelines and used for
/// GPU computation tasks like particle systems, post-processing, physics.
class VulkanComputePipeline
{
public:
    /// \brief Constructor
    /// \param device Vulkan logical device
    explicit VulkanComputePipeline(VkDevice device);

    /// \brief Destructor - cleans up all cached pipelines
    ~VulkanComputePipeline();

    /// \brief Get or create compute pipeline
    /// \param layout Pipeline layout with descriptor set layouts
    /// \param shader Compute shader module
    /// \returns VkPipeline handle or VK_NULL_HANDLE on failure
    /// \details Checks cache first, creates new pipeline if not found.
    /// Pipeline is cached by shader hash for reuse.
    VkPipeline GetOrCreatePipeline(VkPipelineLayout layout, VkShaderModule shader);

    /// \brief Clear all cached pipelines
    void Clear();

private:
    /// Vulkan device
    VkDevice device_;

    /// Cached compute pipelines keyed by the FULL 64-bit VkShaderModule handle (no 32-bit truncation —
    /// two modules sharing low-32 bits would collide and return a pipeline built for the wrong module).
    HashMap<uint64_t, VkPipeline> pipelineCache_;

    /// Prevent copying
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
};

}

#endif // URHO3D_VULKAN
