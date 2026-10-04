// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#pragma once

#include "../../Core/Object.h"
#include "../../Container/HashMap.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// Sampler cache key - identifies unique VkSampler by filter and address modes (per-axis)
struct VulkanSamplerKey
{
    /// Filter mode (NEAREST=0, LINEAR=1, TRILINEAR=2, ANISOTROPIC=3)
    uint8_t filterMode_;
    /// Address mode U (CLAMP=0, REPEAT=1, MIRROR=2)
    uint8_t addressModeU_;
    /// Address mode V (CLAMP=0, REPEAT=1, MIRROR=2)
    uint8_t addressModeV_;
    /// Address mode W (CLAMP=0, REPEAT=1, MIRROR=2)
    uint8_t addressModeW_;
    /// Max anisotropy level (1-16, default 1)
    uint8_t maxAnisotropy_;

    /// Hash for HashMap
    uint32_t ToHash() const
    {
        return (uint32_t)filterMode_ | ((uint32_t)addressModeU_ << 4) | ((uint32_t)addressModeV_ << 8) |
               ((uint32_t)addressModeW_ << 12) | ((uint32_t)maxAnisotropy_ << 16);
    }

    /// Equality comparison
    bool operator==(const VulkanSamplerKey& rhs) const
    {
        return filterMode_ == rhs.filterMode_ && addressModeU_ == rhs.addressModeU_ &&
               addressModeV_ == rhs.addressModeV_ && addressModeW_ == rhs.addressModeW_ &&
               maxAnisotropy_ == rhs.maxAnisotropy_;
    }
};

/// Manages VkSampler object caching by filter and address modes
/// Reuses samplers across materials with identical texture sampling parameters.
///
/// \brief Vulkan sampler cache for texture sampling configuration
/// \details Caches VkSampler objects by filter mode (NEAREST, LINEAR, TRILINEAR, ANISOTROPIC)
/// and address mode (CLAMP, REPEAT, MIRROR) combinations. Supports anisotropic filtering.
/// Reduces GPU memory usage by sharing samplers across materials.
///
/// Architecture:
/// - HashMap<VulkanSamplerKey, VkSampler> for caching
/// - Lazy creation on first access
/// - One sampler per unique filter+address combination
/// - Support for anisotropic filtering levels
///
/// \code
/// VulkanSamplerCache samplerCache(context, graphics);
/// samplerCache.Initialize();
/// VkSampler linearClampSampler = samplerCache.GetSampler(FILTER_LINEAR, CLAMP, 1);
/// // ... use sampler in descriptor sets ...
/// \endcode
class VulkanSamplerCache : public Object
{
    URHO3D_OBJECT(VulkanSamplerCache, Object);

public:
    /// Construct sampler cache.
    VulkanSamplerCache(Context* context, VulkanGraphicsImpl* graphics);

    /// Destruct sampler cache.
    ~VulkanSamplerCache();

    /// Initialize sampler cache.
    /// \return True if initialization successful
    bool Initialize();

    /// Get or create sampler with specified filter and per-axis address modes.
    /// Caches result for future access with same parameters.
    /// \param filterMode Filter mode (0=NEAREST, 1=LINEAR, 2=TRILINEAR, 3=ANISOTROPIC)
    /// \param addressModeU Address mode for U axis (0=CLAMP, 1=REPEAT, 2=MIRROR)
    /// \param addressModeV Address mode for V axis (defaults to same as U)
    /// \param addressModeW Address mode for W axis (defaults to same as U)
    /// \param maxAnisotropy Max anisotropy level (1-16, default 1)
    /// \return VkSampler for the configuration, or VK_NULL_HANDLE if failed
    VkSampler GetSampler(uint8_t filterMode, uint8_t addressModeU, uint8_t addressModeV, uint8_t addressModeW, uint8_t maxAnisotropy = 1);
    /// Convenience overload — single address mode applied to all axes.
    VkSampler GetSampler(uint8_t filterMode, uint8_t addressMode, uint8_t maxAnisotropy = 1)
    {
        return GetSampler(filterMode, addressMode, addressMode, addressMode, maxAnisotropy);
    }

    /// Clear all cached samplers.
    /// Called at shutdown or when graphics context is destroyed.
    void Reset();

    /// Get number of cached samplers.
    uint32_t GetCachedSamplerCount() const { return static_cast<uint32_t>(samplerCache_.Size()); }

    /// Get number of cache hits (sampler reused from cache).
    uint32_t GetCacheHits() const { return cacheHits_; }

    /// Get number of cache misses (new sampler created).
    uint32_t GetCacheMisses() const { return cacheMisses_; }

    /// Check if sampler cache is initialized and ready.
    bool IsInitialized() const { return graphics_ != nullptr; }

private:
    /// Create VkSampler for given filter and address mode combination
    VkSampler CreateSampler(const VulkanSamplerKey& key);

    /// Parent graphics implementation
    VulkanGraphicsImpl* graphics_;

    /// Cache of created samplers
    HashMap<VulkanSamplerKey, VkSampler> samplerCache_;

    /// Cache hit counter (for profiling)
    uint32_t cacheHits_;

    /// Cache miss counter (for profiling)
    uint32_t cacheMisses_;
};

}
