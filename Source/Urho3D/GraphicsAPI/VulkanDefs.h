//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//

#pragma once

#ifdef URHO3D_VULKAN

#include <vulkan/vulkan.h>

namespace Urho3D
{

// Vulkan-specific constants and definitions

/// Default frame pipelining depth (number of in-flight frames) - Triple buffering for better GPU utilization
static const uint32_t VULKAN_FRAME_COUNT = 3;

/// Default descriptor pool sizes
static const uint32_t VULKAN_DESCRIPTOR_POOL_SIZE = 1000;
static const uint32_t VULKAN_DESCRIPTOR_SET_LAYOUT_POOL_SIZE = 100;

/// Default command buffer pool sizes
static const uint32_t VULKAN_COMMAND_BUFFER_PER_FRAME = 1;

/// Swapchain configuration (Quick Win #7 - Triple buffering support)
static const uint32_t VULKAN_DEFAULT_SWAPCHAIN_IMAGES = 3;
static const VkFormat VULKAN_PREFERRED_SURFACE_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;
static const VkFormat VULKAN_FALLBACK_SURFACE_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;

/// Depth buffer configuration
static const VkFormat VULKAN_PREFERRED_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
static const VkFormat VULKAN_FALLBACK_DEPTH_FORMAT = VK_FORMAT_D24_UNORM_S8_UINT;

/// MSAA defaults
static const uint32_t VULKAN_PREFERRED_MSAA_SAMPLES = 4;  ///< Default 4x MSAA if available
static const uint32_t VULKAN_MAX_MSAA_SAMPLES = 16;       ///< Maximum supported MSAA level

/// Pipeline cache settings
static const uint64_t VULKAN_PIPELINE_CACHE_SIZE = 10 * 1024 * 1024;  // 10 MB

/// Vulkan extension names
static const char* VULKAN_REQUIRED_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
static const uint32_t VULKAN_REQUIRED_EXTENSION_COUNT = 1;

/// Optional Vulkan extensions (used if available)
static const char* VULKAN_OPTIONAL_EXTENSIONS[] = {
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
};
static const uint32_t VULKAN_OPTIONAL_EXTENSION_COUNT = 1;

/// Timeline semaphore constants (Phase 33)
static const uint64_t VULKAN_TIMELINE_INITIAL_VALUE = 0;  ///< Initial timeline counter value
static const uint32_t VULKAN_TIMELINE_TIMEOUT_NS = 1000000000;  ///< 1 second timeout for timeline waits

} // namespace Urho3D

#endif  // URHO3D_VULKAN
