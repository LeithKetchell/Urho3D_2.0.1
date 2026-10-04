// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanSamplerCache.h"
#include "VulkanGraphicsImpl.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

namespace Urho3D
{

VulkanSamplerCache::VulkanSamplerCache(Context* context, VulkanGraphicsImpl* graphics) :
    Object(context),
    graphics_(graphics),
    cacheHits_(0),
    cacheMisses_(0)
{
}

VulkanSamplerCache::~VulkanSamplerCache()
{
    Reset();
    graphics_ = nullptr;
}

bool VulkanSamplerCache::Initialize()
{
    if (!graphics_)
        return false;

    URHO3D_LOGINFO("VulkanSamplerCache: Initialization complete");
    return true;
}

VkSampler VulkanSamplerCache::GetSampler(uint8_t filterMode, uint8_t addressModeU, uint8_t addressModeV, uint8_t addressModeW, uint8_t maxAnisotropy)
{
    if (!graphics_)
        return VK_NULL_HANDLE;

    VulkanSamplerKey key;
    key.filterMode_ = filterMode;
    key.addressModeU_ = addressModeU;
    key.addressModeV_ = addressModeV;
    key.addressModeW_ = addressModeW;
    key.maxAnisotropy_ = (maxAnisotropy > 0) ? maxAnisotropy : 1;

    // Check if already cached
    auto it = samplerCache_.Find(key);
    if (it != samplerCache_.End())
    {
        cacheHits_++;
        return it->second_;
    }

    // Create new sampler
    VkSampler sampler = CreateSampler(key);
    if (sampler == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    // Cache for future access
    cacheMisses_++;
    samplerCache_[key] = sampler;

    URHO3D_LOGDEBUG("VulkanSamplerCache: Created sampler - filter: " + String(static_cast<unsigned>(filterMode)) +
        ", address UVW: " + String(static_cast<unsigned>(addressModeU)) + "/" +
        String(static_cast<unsigned>(addressModeV)) + "/" + String(static_cast<unsigned>(addressModeW)) +
        ", anisotropy: " + String(static_cast<unsigned>(maxAnisotropy)));

    return sampler;
}

void VulkanSamplerCache::Reset()
{
    // Phase 17.1: Destroy all cached samplers
    VkDevice device = graphics_ ? graphics_->GetDevice() : nullptr;

    if (device)
    {
        for (auto it = samplerCache_.Begin(); it != samplerCache_.End(); ++it)
        {
            if (it->second_ != VK_NULL_HANDLE)
                vkDestroySampler(device, it->second_, nullptr);
        }
    }

    samplerCache_.Clear();
    cacheHits_ = 0;
    cacheMisses_ = 0;
}

VkSampler VulkanSamplerCache::CreateSampler(const VulkanSamplerKey& key)
{
    if (!graphics_)
        return VK_NULL_HANDLE;

    VkDevice device = graphics_->GetDevice();
    if (!device)
        return VK_NULL_HANDLE;

    // Phase 17.1: Create VkSampler with specified filter and address modes
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    // Map Urho3D filter modes to Vulkan VkFilter
    // 0=NEAREST, 1=LINEAR, 2=TRILINEAR, 3=ANISOTROPIC
    if (key.filterMode_ == 0)
    {
        // NEAREST
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    else if (key.filterMode_ == 1)
    {
        // LINEAR (no mipmap)
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    else if (key.filterMode_ == 2)
    {
        // TRILINEAR (linear with linear mipmap)
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    else
    {
        // ANISOTROPIC (linear with anisotropic filtering)
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = static_cast<float>(key.maxAnisotropy_);
    }

    // Map Urho3D address modes to Vulkan VkSamplerAddressMode (per-axis)
    auto mapAddressMode = [](uint8_t mode) -> VkSamplerAddressMode {
        if (mode == 1) return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        if (mode == 2) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;  // 0 = CLAMP
    };

    samplerInfo.addressModeU = mapAddressMode(key.addressModeU_);
    samplerInfo.addressModeV = mapAddressMode(key.addressModeV_);
    samplerInfo.addressModeW = mapAddressMode(key.addressModeW_);

    // Sampler parameters
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler;
    VkResult result = vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanSamplerCache: Failed to create sampler");
        return VK_NULL_HANDLE;
    }

    return sampler;
}

}

#endif  // URHO3D_VULKAN
