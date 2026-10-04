// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Core/Object.h"
#include "../../Container/HashMap.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// Manages Vulkan pipeline caching with disk persistence.
/// Two-tier: in-memory HashMap for O(1) per-frame lookups,
/// plus a native VkPipelineCache backed by a single file on disk.
/// The driver handles all internal cache logic — we just load/save the blob.
class VulkanPipelineCache : public Object
{
    URHO3D_OBJECT(VulkanPipelineCache, Object);

public:
    explicit VulkanPipelineCache(Context* context);
    ~VulkanPipelineCache();

    /// Initialize: create VkPipelineCache, loading prior data from disk if available.
    bool Initialize(VulkanGraphicsImpl* graphics);

    /// Save cache data to disk and destroy all pipelines and the VkPipelineCache.
    void Release();

    /// Look up pipeline by state hash only (no creation). Returns VK_NULL_HANDLE on miss.
    VkPipeline FindCachedPipeline(uint64_t stateHash) const;

    /// Look up pipeline by state hash, or create via vkCreateGraphicsPipelines
    /// using the native VkPipelineCache for driver-level acceleration.
    VkPipeline GetOrCreatePipeline(uint64_t stateHash, const VkGraphicsPipelineCreateInfo& createInfo);

    /// Destroy all cached pipelines (keeps VkPipelineCache alive).
    void ClearCaches();

    /// Log hit rates and pipeline counts.
    void ReportStatistics() const;

    uint32_t GetMemoryCacheSize() const { return static_cast<uint32_t>(pipelineMap_.Size()); }
    uint32_t GetMemoryCacheHits() const { return memoryCacheHits_; }
    uint32_t GetMemoryCacheMisses() const { return memoryCacheMisses_; }
    void IncrementHits() { memoryCacheHits_++; }

    void ResetStatistics()
    {
        memoryCacheHits_ = 0;
        memoryCacheMisses_ = 0;
        totalPipelinesCreated_ = 0;
    }

private:
    /// Determine the single cache file path.
    String GetCacheFilePath() const;

    /// Load VkPipelineCache blob from disk. Returns empty data if file missing.
    Vector<uint8_t> LoadCacheDataFromDisk();

    /// Save VkPipelineCache blob to disk.
    void SaveCacheDataToDisk();

    /// Set up the cache directory.
    bool InitializeCacheDirectory();

    VulkanGraphicsImpl* graphics_{nullptr};

    /// Native Vulkan pipeline cache — the driver stores compiled ISA here.
    VkPipelineCache vkCache_{VK_NULL_HANDLE};

    /// In-memory map: state hash -> VkPipeline (avoids redundant vkCreateGraphicsPipelines calls).
    HashMap<uint64_t, VkPipeline> pipelineMap_;

    uint32_t memoryCacheHits_{0};
    uint32_t memoryCacheMisses_{0};
    uint32_t totalPipelinesCreated_{0};
    uint32_t peakCacheSize_{0};

    String cacheDirectory_;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
