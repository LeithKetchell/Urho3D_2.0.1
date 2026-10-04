//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan descriptor pool management (Phase 7)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanDescriptorPool.h"
#include "../../IO/Log.h"
#include <cstring>

namespace Urho3D
{

unsigned DescriptorSetLayoutKey::Hash() const
{
    unsigned hash = 0;
    for (const auto& binding : bindings)
    {
        hash = hash * 31 + binding.binding;
        hash = hash * 31 + binding.descriptorType;
        hash = hash * 31 + binding.descriptorCount;
    }
    return hash;
}

bool DescriptorSetLayoutKey::operator==(const DescriptorSetLayoutKey& other) const
{
    if (bindings.Size() != other.bindings.Size())
        return false;

    for (size_t i = 0; i < bindings.Size(); ++i)
    {
        const auto& a = bindings[i];
        const auto& b = other.bindings[i];

        if (a.binding != b.binding ||
            a.descriptorType != b.descriptorType ||
            a.descriptorCount != b.descriptorCount ||
            a.stageFlags != b.stageFlags)
        {
            return false;
        }
    }
    return true;
}

VulkanDescriptorPool::VulkanDescriptorPool()
{
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    Release();
}

bool VulkanDescriptorPool::Initialize(VkDevice device, VkPhysicalDevice physicalDevice)
{
    if (!device)
    {
        URHO3D_LOGERROR("Invalid device passed to VulkanDescriptorPool");
        return false;
    }

    device_ = device;

    // Create descriptor pool that can hold reasonable number of descriptors
    // Pool sizes for common descriptor types
    VkDescriptorPoolSize poolSizes[] = {
        // Uniform buffers (for constants)
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 128},

        // Storage buffers (for structured data)
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 64},

        // Sampled images (textures)
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 512},

        // Combined image samplers
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512},

        // Storage images
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128},

        // Samplers
        {VK_DESCRIPTOR_TYPE_SAMPLER, 256},

        // Input attachments (for deferred rendering)
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow freeing individual sets
    poolInfo.maxSets = 1024;  // Max number of descriptor sets
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create descriptor pool");
        return false;
    }

    URHO3D_LOGDEBUG("Descriptor pool created successfully");
    return true;
}

void VulkanDescriptorPool::Release()
{
    // Clear frame cache
    frameDescriptorSetCache_.Clear();
    activeDescriptorSets_.Clear();

    // Destroy cached layouts
    for (auto& pair : layoutCache_)
    {
        if (pair.second_ && device_)
        {
            vkDestroyDescriptorSetLayout(device_, pair.second_, nullptr);
        }
    }
    layoutCache_.Clear();

    // Destroy pool
    if (descriptorPool_ && device_)
    {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = nullptr;
    }

    device_ = nullptr;
}

VkDescriptorSetLayout VulkanDescriptorPool::GetOrCreateDescriptorSetLayout(
    const Vector<VkDescriptorSetLayoutBinding>& bindings)
{
    if (!device_ || bindings.Empty())
        return nullptr;

    // Create key for caching
    DescriptorSetLayoutKey key;
    key.bindings = bindings;
    StringHash keyHash(key.Hash());

    // Check if we already have this layout
    auto it = layoutCache_.Find(keyHash);
    if (it != layoutCache_.End())
    {
        return it->second_;
    }

    // Create new layout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.Size());
    layoutInfo.pBindings = !bindings.Empty() ? &bindings[0] : nullptr;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to create descriptor set layout");
        return nullptr;
    }

    // Cache it
    layoutCache_[keyHash] = layout;

    URHO3D_LOGDEBUG("Created descriptor set layout with " + String(bindings.Size()) + " bindings");
    return layout;
}

VkDescriptorSet VulkanDescriptorPool::AllocateDescriptorSet(VkDescriptorSetLayout layout)
{
    if (!descriptorPool_ || !layout || !device_)
        return nullptr;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet) != VK_SUCCESS)
    {
        URHO3D_LOGERROR("Failed to allocate descriptor set");
        return nullptr;
    }

    return descriptorSet;
}

VkDescriptorSet VulkanDescriptorPool::GetOrCreateDescriptorSet(VkDescriptorSetLayout layout, uint64_t resourceBindingHash)
{
    if (!descriptorPool_ || !layout || !device_)
        return nullptr;

    // Create cache key from layout and resource binding hash
    // Use layout pointer as hash component (unique per layout)
    uint64_t layoutHash = reinterpret_cast<uint64_t>(layout);
    uint64_t cacheKey = layoutHash ^ resourceBindingHash;

    // Check if descriptor set is already cached for this frame
    auto it = frameDescriptorSetCache_.Find(cacheKey);
    if (it != frameDescriptorSetCache_.End())
    {
        return it->second_;
    }

    // Allocate new descriptor set
    VkDescriptorSet descriptorSet = AllocateDescriptorSet(layout);
    if (!descriptorSet)
        return nullptr;

    // Cache it for frame reuse
    frameDescriptorSetCache_[cacheKey] = descriptorSet;
    activeDescriptorSets_.Push(descriptorSet);

    URHO3D_LOGDEBUG("Cached descriptor set (total: " + String(frameDescriptorSetCache_.Size()) + ")");
    return descriptorSet;
}

void VulkanDescriptorPool::ResetFrameCache()
{
    // Clear per-frame descriptor set cache
    // Note: Sets are freed back to pool, reusable across frames if pool allows
    frameDescriptorSetCache_.Clear();
    activeDescriptorSets_.Clear();
    URHO3D_LOGDEBUG("Frame descriptor set cache reset");
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
