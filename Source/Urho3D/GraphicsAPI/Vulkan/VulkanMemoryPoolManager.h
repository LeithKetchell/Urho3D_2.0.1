//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan VMA pool strategy for optimized memory allocation (Quick Win #8)

#pragma once

#ifdef URHO3D_VULKAN

#include <vulkan/vulkan.h>
#include "../../Container/RefCounted.h"
#include <vk_mem_alloc.h>

namespace Urho3D
{

class VulkanGraphicsImpl;

/// \brief Memory pool types for different allocation patterns
/// \details Enumerates specialized allocation strategies for different buffer types.
/// Each pool has dedicated memory allocation strategy optimized for its usage pattern.
enum class VulkanMemoryPoolType
{
    /// Constant buffers - small (< 64KB), frequently updated, host-writable (CPU-to-GPU)
    /// VMA Strategy: HOST_VISIBLE, Frequent updates
    ConstantBuffers = 0,

    /// Dynamic vertex/index buffers - medium (100KB-10MB), frequently updated, host-writable
    /// VMA Strategy: HOST_VISIBLE, Mapped for streaming data
    DynamicGeometry = 1,

    /// Static vertex/index buffers - large (100MB+), rarely updated, GPU-preferred
    /// VMA Strategy: GPU_ONLY, One-time initialization, high bandwidth
    StaticGeometry = 2,

    /// Staging buffers - temporary, CPU-to-GPU transfers, small lifetime
    /// VMA Strategy: HOST_VISIBLE, Temporary, freed after transfer
    Staging = 3,

    /// Texture/image allocations - large, GPU-only, texture compression optimized
    /// VMA Strategy: GPU_ONLY, Optimized for texture locality
    Textures = 4,

    /// Count of pool types (used for array sizing)
    Count = 5
};

/// \brief VMA memory pool with dedicated strategy
/// \details Represents one specialized VMA pool with its own memory strategy.
/// Pools reduce external fragmentation by grouping similar allocation patterns.
struct VulkanMemoryPool
{
    VmaPool pool{};                         ///< VMA pool handle
    VmaMemoryUsage memoryUsage;             ///< VMA allocation strategy (GPU_ONLY, HOST_VISIBLE, etc.)
    VkMemoryPropertyFlags propertyFlags;    ///< Vulkan memory properties (DEVICE_LOCAL, HOST_VISIBLE, etc.)
    uint64_t totalSize{0};                  ///< Total pool size in bytes
    uint64_t allocatedSize{0};              ///< Currently allocated bytes
    uint32_t allocationCount{0};            ///< Number of active allocations
    const char* name{};                     ///< Pool name for logging ("ConstantBuffers", etc.)
};

/// \brief Manages dedicated VMA pools for different buffer allocation patterns
/// \details Creates separate VMA allocation pools for each buffer type (constant, dynamic geometry,
/// static geometry, staging, textures). Each pool uses optimized allocation strategy for its
/// usage pattern. Reduces external fragmentation compared to single monolithic pool.
/// Implements Quick Win #8.
///
/// **Pool Strategies:**
///
/// **ConstantBuffers (0):**
/// - Small allocations (< 64KB)
/// - Frequently updated (per-frame)
/// - Strategy: HOST_VISIBLE memory, frequent updates OK
/// - Use: Per-object transformation matrices, lighting parameters
///
/// **DynamicGeometry (1):**
/// - Medium allocations (100KB - 10MB)
/// - Frequently updated (per-frame or per-scene)
/// - Strategy: HOST_VISIBLE, streaming updates
/// - Use: Skinned meshes, particle buffers, animated geometry
///
/// **StaticGeometry (2):**
/// - Large allocations (100MB+)
/// - Rarely updated (one-time or level-change)
/// - Strategy: GPU_ONLY, high bandwidth
/// - Use: Terrain, static meshes, LOD geometry
///
/// **Staging (3):**
/// - Temporary allocations
/// - Lifetime: single frame or transfer operation
/// - Strategy: HOST_VISIBLE, CPU-GPU transfer
/// - Use: Image uploads, buffer initialization
///
/// **Textures (4):**
/// - Large GPU-only allocations
/// - Optimized for texture locality
/// - Strategy: GPU_ONLY, texture-optimized
/// - Use: Color maps, normal maps, PBR textures
///
/// **Performance Benefits:**
/// - Reduced external fragmentation vs. monolithic pool
/// - Optimized memory type selection for each pattern
/// - Statistics per pool type for profiling
/// - Graceful degradation if specific pool unavailable
///
/// **Typical Usage:**
/// 1. Initialize(impl) during Graphics setup
/// 2. GetPool(ConstantBuffers) to get appropriate pool for allocation
/// 3. Use returned VmaPool in vmaCreateBuffer()
/// 4. GetAllStatistics() for memory profiling
class VulkanMemoryPoolManager : public RefCounted
{
public:
    /// \brief Constructor
    VulkanMemoryPoolManager();

    /// \brief Destructor
    /// \details Automatically releases all pools via Release()
    ~VulkanMemoryPoolManager();

    /// \brief Initialize memory pools with optimized strategies
    /// \param impl VulkanGraphicsImpl for device and allocator access
    /// \returns True if all pools created successfully, false on error
    /// \details Creates all 5 pool types with specialized configurations
    bool Initialize(VulkanGraphicsImpl* impl);

    /// \brief Release all memory pools
    /// \details Destroys all VMA pools and frees their memory.
    /// Safe to call multiple times. Called automatically on destruction.
    void Release();

    /// \brief Get pool for specific memory type
    /// \param poolType Type of allocation (ConstantBuffers, StaticGeometry, etc.)
    /// \returns VmaPool handle for use in vmaCreateBuffer()
    VmaPool GetPool(VulkanMemoryPoolType poolType) const;

    /// \brief Get memory usage hint for pool type
    /// \param poolType Type of allocation
    /// \returns VmaMemoryUsage strategy (GPU_ONLY, HOST_VISIBLE, etc.)
    VmaMemoryUsage GetMemoryUsage(VulkanMemoryPoolType poolType) const;

    /// \brief Get property flags for pool type
    /// \param poolType Type of allocation
    /// \returns VkMemoryPropertyFlags (DEVICE_LOCAL, HOST_VISIBLE, HOST_COHERENT, etc.)
    VkMemoryPropertyFlags GetPropertyFlags(VulkanMemoryPoolType poolType) const;

    /// \brief Get statistics for specific pool
    /// \details Structure containing pool utilization and allocation metrics
    struct PoolStatistics
    {
        uint64_t totalSize{0};              ///< Total pool size in bytes
        uint64_t allocatedSize{0};          ///< Currently allocated bytes
        uint32_t allocationCount{0};        ///< Number of active allocations
        float utilizationPercent{0.0f};     ///< Allocated / Total percentage
        const char* poolName{};             ///< Pool name for logging
    };

    /// \brief Get statistics for specific pool type
    /// \param poolType Type of allocation
    /// \returns PoolStatistics with utilization metrics
    PoolStatistics GetPoolStatistics(VulkanMemoryPoolType poolType) const;

    /// \brief Get all pool statistics
    /// \details Structure containing metrics for all pool types combined
    struct AllPoolsStatistics
    {
        PoolStatistics pools[(int)VulkanMemoryPoolType::Count];  ///< Statistics per pool type
        uint64_t totalAllocated{0};         ///< Sum of all allocated bytes
        uint64_t totalCapacity{0};          ///< Sum of all pool sizes
        float overallUtilization{0.0f};     ///< totalAllocated / totalCapacity percentage
    };

    /// \brief Get all pool statistics
    /// \returns AllPoolsStatistics with metrics for all pool types
    /// \details Used for memory profiling and optimization
    AllPoolsStatistics GetAllStatistics() const;

    /// \brief Reset statistics (for profiling)
    /// \details Clears per-pool and global statistics counters
    void ResetStatistics();

private:
    /// \brief Create a single memory pool with optimized configuration
    /// \param poolType Type of pool (ConstantBuffers, StaticGeometry, etc.)
    /// \param poolName Human-readable pool name for logging
    /// \param poolSize Size of pool to create
    /// \param memoryUsage VMA allocation strategy hint
    /// \param propertyFlags Vulkan memory property flags
    /// \param bufferUsageFlags Vulkan buffer usage hints
    /// \returns True if pool created successfully, false on error
    /// \details Creates single VmaPool with specified configuration using vmaCreatePool().
    /// Called from Initialize() for each pool type.
    bool CreatePool(
        VulkanMemoryPoolType poolType,
        const char* poolName,
        uint64_t poolSize,
        VmaMemoryUsage memoryUsage,
        VkMemoryPropertyFlags propertyFlags,
        VkBufferUsageFlags bufferUsageFlags
    );

    /// Graphics implementation (for device and allocator access)
    VulkanGraphicsImpl* impl_{nullptr};

    /// Memory pools for different allocation strategies
    /// Indexed by VulkanMemoryPoolType enum
    VulkanMemoryPool pools_[(int)VulkanMemoryPoolType::Count];

    /// Flag to track if pools are initialized
    bool isInitialized_{false};
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
