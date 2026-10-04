//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan descriptor pool management (Phase 7)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Container/HashMap.h"
#include "../../Math/StringHash.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class Graphics;

/// Descriptor set layout hash for caching
struct DescriptorSetLayoutKey
{
    Vector<VkDescriptorSetLayoutBinding> bindings;

    unsigned Hash() const;
    bool operator==(const DescriptorSetLayoutKey& other) const;
};

/// Manages descriptor pool and descriptor set allocations
class VulkanDescriptorPool
{
public:
    /// Constructor
    VulkanDescriptorPool();
    /// Destructor
    ~VulkanDescriptorPool();

    /// Initialize the descriptor pool
    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice);

    /// Release resources
    void Release();

    /// Get or create descriptor set layout
    VkDescriptorSetLayout GetOrCreateDescriptorSetLayout(
        const Vector<VkDescriptorSetLayoutBinding>& bindings);

    /// Allocate a descriptor set
    VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);

    /// Get or reuse descriptor set (with caching for common patterns)
    VkDescriptorSet GetOrCreateDescriptorSet(VkDescriptorSetLayout layout, uint64_t resourceBindingHash);

    /// Reset per-frame descriptor set cache
    void ResetFrameCache();

    /// Get descriptor pool
    VkDescriptorPool GetPool() const { return descriptorPool_; }

    /// Get device
    VkDevice GetDevice() const { return device_; }

private:
    VkDevice device_{};
    VkDescriptorPool descriptorPool_{};

    // Cached descriptor set layouts by binding configuration
    HashMap<StringHash, VkDescriptorSetLayout> layoutCache_;

    // Per-frame descriptor set cache by (layout hash + resource binding hash)
    // Key: layout hash XOR resource binding hash, Value: descriptor set
    HashMap<uint64_t, VkDescriptorSet> frameDescriptorSetCache_;
    // Descriptor set caching can be implemented later with proper hashing

    // Track which descriptor sets are in use this frame (for potential reuse)
    Vector<VkDescriptorSet> activeDescriptorSets_;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
