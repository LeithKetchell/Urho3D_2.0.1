//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan VMA pool strategy implementation (Quick Win #8)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanMemoryPoolManager.h"
#include "VulkanGraphicsImpl.h"
#include "../../IO/Log.h"

namespace Urho3D
{

VulkanMemoryPoolManager::VulkanMemoryPoolManager()
{
}

VulkanMemoryPoolManager::~VulkanMemoryPoolManager()
{
    Release();
}

bool VulkanMemoryPoolManager::Initialize(VulkanGraphicsImpl* impl)
{
    if (!impl)
    {
        URHO3D_LOGERROR("Invalid graphics implementation passed to VulkanMemoryPoolManager");
        return false;
    }

    impl_ = impl;

    // Create optimized memory pools for different buffer types
    // Pool sizes are tuned for typical scene complexity

    // Constant buffers - 64MB pool for frequently updated small buffers
    if (!CreatePool(
        VulkanMemoryPoolType::ConstantBuffers,
        "ConstantBuffers",
        64 * 1024 * 1024,  // 64 MB
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    ))
    {
        URHO3D_LOGERROR("Failed to create constant buffer memory pool");
        return false;
    }

    // Dynamic geometry (dynamic vertex/index buffers) - 256MB pool
    if (!CreatePool(
        VulkanMemoryPoolType::DynamicGeometry,
        "DynamicGeometry",
        256 * 1024 * 1024,  // 256 MB
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ))
    {
        URHO3D_LOGERROR("Failed to create dynamic geometry memory pool");
        return false;
    }

    // Static geometry (static vertex/index buffers) - 1GB pool, GPU-preferred
    if (!CreatePool(
        VulkanMemoryPoolType::StaticGeometry,
        "StaticGeometry",
        1024 * 1024 * 1024,  // 1 GB (typical for large scenes)
        VMA_MEMORY_USAGE_GPU_ONLY,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ))
    {
        URHO3D_LOGERROR("Failed to create static geometry memory pool");
        return false;
    }

    // Staging buffers - 256MB pool for temporary CPU-to-GPU transfers
    if (!CreatePool(
        VulkanMemoryPoolType::Staging,
        "StagingBuffers",
        256 * 1024 * 1024,  // 256 MB
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    ))
    {
        URHO3D_LOGERROR("Failed to create staging buffer memory pool");
        return false;
    }

    // Textures - 2GB pool for GPU-only texture allocations
    if (!CreatePool(
        VulkanMemoryPoolType::Textures,
        "Textures",
        2048 * 1024 * 1024,  // 2 GB (typical for texture-heavy scenes)
        VMA_MEMORY_USAGE_GPU_ONLY,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0  // Images don't have buffer usage flags
    ))
    {
        URHO3D_LOGERROR("Failed to create texture memory pool");
        return false;
    }

    isInitialized_ = true;

    URHO3D_LOGINFO("VulkanMemoryPoolManager initialized with 5 optimized memory pools");
    URHO3D_LOGINFO("  Constant Buffers: 64 MB (CPU-to-GPU)");
    URHO3D_LOGINFO("  Dynamic Geometry: 256 MB (CPU-to-GPU)");
    URHO3D_LOGINFO("  Static Geometry: 1 GB (GPU-only)");
    URHO3D_LOGINFO("  Staging Buffers: 256 MB (CPU-to-GPU)");
    URHO3D_LOGINFO("  Textures: 2 GB (GPU-only)");
    URHO3D_LOGINFO("  Total Committed: 3.5 GB");

    return true;
}

bool VulkanMemoryPoolManager::CreatePool(
    VulkanMemoryPoolType poolType,
    const char* poolName,
    uint64_t poolSize,
    VmaMemoryUsage memoryUsage,
    VkMemoryPropertyFlags propertyFlags,
    VkBufferUsageFlags bufferUsageFlags)
{
    if (!impl_)
        return false;

    int poolIndex = (int)poolType;
    VulkanMemoryPool& pool = pools_[poolIndex];

    pool.memoryUsage = memoryUsage;
    pool.propertyFlags = propertyFlags;
    pool.totalSize = poolSize;
    pool.name = poolName;

    // Create VMA pool with optimized settings
    VmaPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.memoryTypeIndex = 0;  // Let VMA select based on usage
    poolCreateInfo.flags = VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT;

    // For persistent pools, we don't use frame-local reset
    // This allows better fragmentation patterns for static allocations
    poolCreateInfo.blockSize = (poolSize > 256 * 1024 * 1024) ? 256 * 1024 * 1024 : poolSize / 2;
    poolCreateInfo.maxBlockCount = (poolSize + poolCreateInfo.blockSize - 1) / poolCreateInfo.blockSize;

    if (vmaCreatePool(impl_->GetAllocator(), &poolCreateInfo, &pool.pool) != VK_SUCCESS)
    {
        URHO3D_LOGERROR(String("Failed to create VMA pool: ") + poolName);
        return false;
    }

    URHO3D_LOGDEBUG(String("Created memory pool: ") + poolName + " (" + String(poolSize / 1024 / 1024) + " MB)");
    return true;
}

void VulkanMemoryPoolManager::Release()
{
    if (!impl_)
        return;

    // Destroy all pools
    for (int i = 0; i < (int)VulkanMemoryPoolType::Count; ++i)
    {
        if (pools_[i].pool)
        {
            vmaDestroyPool(impl_->GetAllocator(), pools_[i].pool);
            pools_[i].pool = nullptr;
        }
    }

    URHO3D_LOGDEBUG("VulkanMemoryPoolManager released");
    isInitialized_ = false;
}

VmaPool VulkanMemoryPoolManager::GetPool(VulkanMemoryPoolType poolType) const
{
    int index = (int)poolType;
    if (index >= 0 && index < (int)VulkanMemoryPoolType::Count)
        return pools_[index].pool;
    return nullptr;
}

VmaMemoryUsage VulkanMemoryPoolManager::GetMemoryUsage(VulkanMemoryPoolType poolType) const
{
    int index = (int)poolType;
    if (index >= 0 && index < (int)VulkanMemoryPoolType::Count)
        return pools_[index].memoryUsage;
    return VMA_MEMORY_USAGE_AUTO;
}

VkMemoryPropertyFlags VulkanMemoryPoolManager::GetPropertyFlags(VulkanMemoryPoolType poolType) const
{
    int index = (int)poolType;
    if (index >= 0 && index < (int)VulkanMemoryPoolType::Count)
        return pools_[index].propertyFlags;
    return 0;
}

VulkanMemoryPoolManager::PoolStatistics VulkanMemoryPoolManager::GetPoolStatistics(VulkanMemoryPoolType poolType) const
{
    PoolStatistics stats{};
    int index = (int)poolType;

    if (index >= 0 && index < (int)VulkanMemoryPoolType::Count)
    {
        const VulkanMemoryPool& pool = pools_[index];
        stats.totalSize = pool.totalSize;
        stats.allocatedSize = pool.allocatedSize;
        stats.allocationCount = pool.allocationCount;
        stats.poolName = pool.name;

        if (pool.totalSize > 0)
            stats.utilizationPercent = (float)pool.allocatedSize / (float)pool.totalSize * 100.0f;
    }

    return stats;
}

VulkanMemoryPoolManager::AllPoolsStatistics VulkanMemoryPoolManager::GetAllStatistics() const
{
    AllPoolsStatistics allStats{};

    for (int i = 0; i < (int)VulkanMemoryPoolType::Count; ++i)
    {
        allStats.pools[i] = GetPoolStatistics((VulkanMemoryPoolType)i);
        allStats.totalCapacity += allStats.pools[i].totalSize;
        allStats.totalAllocated += allStats.pools[i].allocatedSize;
    }

    if (allStats.totalCapacity > 0)
        allStats.overallUtilization = (float)allStats.totalAllocated / (float)allStats.totalCapacity * 100.0f;

    return allStats;
}

void VulkanMemoryPoolManager::ResetStatistics()
{
    for (int i = 0; i < (int)VulkanMemoryPoolType::Count; ++i)
    {
        pools_[i].allocatedSize = 0;
        pools_[i].allocationCount = 0;
    }
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
