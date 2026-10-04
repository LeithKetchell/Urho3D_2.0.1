// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#pragma once

#include "../../Core/Object.h"
#include "../../Container/HashMap.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class Material;
class VulkanGraphicsImpl;
class Texture2D;

/// Material descriptor set cache key
struct MaterialDescriptorKey
{
    /// Material pointer
    Material* material_;
    /// Texture state hash (for quick dirty detection)
    uint32_t textureHash_;

    /// Hash for HashMap
    uint32_t ToHash() const
    {
        return (uint32_t)(size_t)material_ ^ (textureHash_ << 16);
    }

    /// Equality comparison
    bool operator==(const MaterialDescriptorKey& rhs) const
    {
        return material_ == rhs.material_ && textureHash_ == rhs.textureHash_;
    }
};

/// Material parameters as GPU constant buffer structure
struct VulkanMaterialConstants
{
    /// Diffuse color (vec4)
    float diffuseColor_[4];
    /// Specular color (vec4)
    float specularColor_[4];
    /// Ambient color (vec4)
    float ambientColor_[4];
    /// Metallic factor
    float metallic_;
    /// Roughness factor
    float roughness_;
    /// Shininess/specular exponent
    float shininess_;
    /// Padding for alignment
    float padding_;
};

/// Manages Material → VkDescriptorSet mapping and updates.
/// Caches descriptor sets by material and handles texture/sampler bindings.
///
/// \brief GPU descriptor management for materials
/// \details Maps Urho3D Material properties to Vulkan descriptor sets.
/// Supports automatic caching and dirty detection for material parameter changes.
/// Handles texture binding, sampler binding, and constant buffer updates.
///
/// Architecture:
/// - HashMap<MaterialDescriptorKey, VkDescriptorSet> for caching
/// - Lazy creation on first access
/// - Dirty flag per material for optimization
/// - Support for material parameter constants
///
/// \code
/// VulkanMaterialDescriptorManager descriptors(context, graphics);
/// descriptors.Initialize();
/// VkDescriptorSet matDescriptor = descriptors.GetDescriptor(material);
/// // ... bind descriptor set during drawing ...
/// descriptors.UpdateIfDirty(material);  // Update if material changed
/// \endcode
class VulkanMaterialDescriptorManager : public Object
{
    URHO3D_OBJECT(VulkanMaterialDescriptorManager, Object);

public:
    /// Construct descriptor manager.
    VulkanMaterialDescriptorManager(Context* context, VulkanGraphicsImpl* graphics);

    /// Destruct descriptor manager.
    ~VulkanMaterialDescriptorManager();

    /// Initialize descriptor manager with layout and pool.
    /// Creates VkDescriptorSetLayout for materials and allocates descriptor pool.
    /// \return True if initialization successful
    bool Initialize();

    /// Get or create descriptor set for material.
    /// Caches result for future access unless material becomes dirty.
    /// \param material Material to get descriptor for
    /// \return VkDescriptorSet for the material, or VK_NULL_HANDLE if failed
    VkDescriptorSet GetDescriptor(Material* material);

    /// Update descriptor set if material properties changed.
    /// Automatically detects dirty materials and updates texture/parameter bindings.
    /// \param material Material to update
    /// \return True if descriptor was updated
    bool UpdateIfDirty(Material* material);

    /// Force update of descriptor set regardless of dirty state.
    /// \param material Material to update
    /// \return True if update successful
    bool ForceUpdate(Material* material);

    /// Clear all cached descriptor sets and reset manager state.
    /// Called at end of frame to prepare for next frame.
    void Reset();

    /// Get number of cached descriptor sets.
    uint32_t GetCachedDescriptorCount() const { return static_cast<uint32_t>(descriptorCache_.Size()); }

    /// Check if descriptor manager is initialized and ready.
    bool IsInitialized() const { return graphics_ != nullptr && descriptorSetLayout_ != VK_NULL_HANDLE; }

private:
    /// Extract material parameters to GPU structure
    void ExtractMaterialParameters(Material* material, VulkanMaterialConstants& outParams);

    /// Calculate texture state hash for dirty detection
    uint32_t CalculateTextureHash(Material* material);

    /// Create descriptor set layout for materials
    bool CreateDescriptorSetLayout();

    /// Create or retrieve descriptor set for material
    VkDescriptorSet CreateDescriptorSet(Material* material);

    /// Update texture bindings in descriptor set
    bool UpdateTextureBindings(Material* material, VkDescriptorSet descriptorSet);

    /// Update material parameter constant buffer
    bool UpdateParameterBuffer(Material* material, VkDescriptorSet descriptorSet);

    /// Phase 26: Invalidate descriptor set variant cache for shader program
    /// Clears cached descriptor sets when shader is recompiled or changed
    void InvalidateVariantCacheForShader(uint64_t shaderHash);

    /// Phase 26: Get descriptor set variant cache statistics
    /// Returns count of cached variants currently in use
    uint32_t GetVariantCacheSize() const { return static_cast<uint32_t>(descriptorSetVariantCache_.Size()); }

    /// Parent graphics implementation
    VulkanGraphicsImpl* graphics_;

    /// Descriptor set layout for all materials
    VkDescriptorSetLayout descriptorSetLayout_;

    /// Descriptor pool for allocating material descriptor sets (Phase 16.1)
    VkDescriptorPool descriptorPool_;

    /// Cache of created descriptor sets
    HashMap<MaterialDescriptorKey, VkDescriptorSet> descriptorCache_;

    /// Track dirty materials for optimization
    HashMap<Material*, bool> materialDirtyFlags_;

    /// Phase 11 (Quick Win #8): Cache previous texture hashes for dirty detection
    /// Enables automatic detection of material state changes without external tracking
    HashMap<Material*, uint32_t> materialStateHashes_;

    /// Phase 11: Counter for dirty materials detected this frame
    /// Used for profiling and optimization metrics
    uint32_t dirtyMaterialsDetectedThisFrame_{0};

    /// Phase 11: Total descriptor set updates due to dirty materials
    /// Tracks optimization effectiveness of dirty flag system
    uint32_t dirtyMaterialUpdateCount_{0};

    /// Phase 24: Cache for generated descriptor set layouts from shader reflection
    /// Stores per-shader layouts to avoid recomputation when multiple materials use same shader
    HashMap<uint64_t, VkDescriptorSetLayout> layoutCache_;

    /// Phase 25: Cache for dynamically allocated descriptor sets per shader variant
    /// Enables reuse of descriptor sets when multiple materials use same shader
    /// Key format: (shader_hash << 32) | material_hash for shader+material combination
    HashMap<uint64_t, VkDescriptorSet> descriptorSetVariantCache_;

    /// Phase 25: Counter for dynamic descriptor set allocation tracking
    /// Tracks total descriptor sets allocated from pools for profiling
    uint32_t dynamicDescriptorSetCount_{0};

    /// Phase 26: Cache invalidation counter for debugging
    /// Tracks how many times variant cache was invalidated (shader recompilation)
    uint32_t cacheInvalidationCount_{0};

    /// Phase 26: Total variant cache hits for profiling
    /// Counts successful descriptor set reuse from variant cache
    uint32_t variantCacheHitCount_{0};

    /// Phase 26: Total variant cache misses for profiling
    /// Counts descriptor set allocations due to variant cache miss
    uint32_t variantCacheMissCount_{0};
};

}
