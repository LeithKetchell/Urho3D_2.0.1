// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanMaterialDescriptorManager.h"
#include "VulkanGraphicsImpl.h"
#include "VulkanSPIRVReflect.h"
#include "../../Graphics/Material.h"
#include "../../GraphicsAPI/Texture2D.h"
#include "../../GraphicsAPI/GraphicsDefs.h"
#include "../../Graphics/Graphics.h"
#include "../../Math/Color.h"
#include "../../Math/Vector3.h"
#include "../../IO/Log.h"

#include "../../DebugNew.h"

namespace Urho3D
{

VulkanMaterialDescriptorManager::VulkanMaterialDescriptorManager(Context* context, VulkanGraphicsImpl* graphics) :
    Object(context),
    graphics_(graphics),
    descriptorSetLayout_(VK_NULL_HANDLE),
    descriptorPool_(VK_NULL_HANDLE)
{
}

VulkanMaterialDescriptorManager::~VulkanMaterialDescriptorManager()
{
    Reset();

    if (graphics_)
    {
        VkDevice device = graphics_->GetDevice();
        if (device)
        {
            // Destroy descriptor pool (Phase 16.1)
            if (descriptorPool_ != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(device, descriptorPool_, nullptr);

            // Destroy descriptor set layout
            if (descriptorSetLayout_ != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);

            // Phase 24: Destroy cached reflection-based layouts
            for (auto it = layoutCache_.Begin(); it != layoutCache_.End(); ++it)
            {
                if (it->second_ != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(device, it->second_, nullptr);
            }
            layoutCache_.Clear();

            // Phase 25: Clear descriptor set variant cache
            // Note: Individual descriptor sets are freed via descriptor pool
            // Only need to clear the map, not destroy individual sets
            descriptorSetVariantCache_.Clear();
            dynamicDescriptorSetCount_ = 0;
        }
    }

    graphics_ = nullptr;
}

bool VulkanMaterialDescriptorManager::Initialize()
{
    if (!graphics_)
        return false;

    VkDevice device = graphics_->GetDevice();
    if (!device)
        return false;

    // Create descriptor set layout for materials
    if (!CreateDescriptorSetLayout())
    {
        URHO3D_LOGERROR("VulkanMaterialDescriptorManager: Failed to create descriptor set layout");
        return false;
    }

    // Phase 16.1: Create descriptor pool for material descriptor sets
    // Pool size: ~100 descriptor sets with space for UBO + 5 samplers per set
    VkDescriptorPoolSize poolSizes[2];

    // Uniform buffer objects (one per material)
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 100;

    // Combined image samplers (5 per material: diffuse, normal, specular, emissive, environment)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 500;  // 100 materials * 5 samplers

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 100;  // Maximum 100 descriptor sets
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow individual set freeing

    VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanMaterialDescriptorManager: Failed to create descriptor pool");
        return false;
    }

    URHO3D_LOGINFO("VulkanMaterialDescriptorManager: Created descriptor pool with capacity for 100 materials");
    URHO3D_LOGINFO("VulkanMaterialDescriptorManager: Initialization complete");
    return true;
}

VkDescriptorSet VulkanMaterialDescriptorManager::GetDescriptor(Material* material)
{
    if (!material || !graphics_)
        return VK_NULL_HANDLE;

    // Calculate key for caching
    MaterialDescriptorKey key;
    key.material_ = material;
    key.textureHash_ = CalculateTextureHash(material);

    // Check if already cached
    auto it = descriptorCache_.Find(key);
    if (it != descriptorCache_.End())
        return it->second_;

    // Create new descriptor set
    VkDescriptorSet descriptorSet = CreateDescriptorSet(material);
    if (descriptorSet == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    // Cache for future access
    descriptorCache_[key] = descriptorSet;
    materialDirtyFlags_[material] = false;

    /// \brief Phase 11: Store initial state hash for dirty detection
    /// Enables automatic change detection on subsequent frames
    materialStateHashes_[material] = CalculateTextureHash(material);

    return descriptorSet;
}

bool VulkanMaterialDescriptorManager::UpdateIfDirty(Material* material)
{
    if (!material)
        return false;

    /// \brief Phase 11 (Quick Win #8): Auto-detect material state changes
    /// Compare current texture hash with stored hash to detect changes
    uint32_t currentHash = CalculateTextureHash(material);
    auto stateIt = materialStateHashes_.Find(material);

    bool stateChanged = (stateIt == materialStateHashes_.End()) || (stateIt->second_ != currentHash);
    bool explicitlyDirty = false;

    // Check if material is explicitly marked dirty
    auto dirtyIt = materialDirtyFlags_.Find(material);
    if (dirtyIt != materialDirtyFlags_.End() && dirtyIt->second_)
        explicitlyDirty = true;

    // If state changed or explicitly marked dirty, update
    if (stateChanged || explicitlyDirty)
    {
        // Force update for this material
        bool updateSuccess = ForceUpdate(material);

        // Store current state hash for next frame comparison
        if (updateSuccess)
        {
            materialStateHashes_[material] = currentHash;
            dirtyMaterialUpdateCount_++;
            if (stateChanged) dirtyMaterialsDetectedThisFrame_++;
        }

        return updateSuccess;
    }

    return false;  // Not dirty, no update needed
}

bool VulkanMaterialDescriptorManager::ForceUpdate(Material* material)
{
    if (!material || !graphics_)
        return false;

    // Find cached descriptor set for this material
    MaterialDescriptorKey key;
    key.material_ = material;
    key.textureHash_ = CalculateTextureHash(material);

    auto it = descriptorCache_.Find(key);
    if (it == descriptorCache_.End())
        return false;  // Descriptor set not cached - can't update

    VkDescriptorSet descriptorSet = it->second_;

    // Phase 14.2: Update material parameter and texture binding
    // 1. Update texture bindings first
    if (!UpdateTextureBindings(material, descriptorSet))
    {
        URHO3D_LOGWARNING("VulkanMaterialDescriptorManager: Failed to update texture bindings during ForceUpdate");
    }

    // 2. Update parameter buffer
    if (!UpdateParameterBuffer(material, descriptorSet))
    {
        URHO3D_LOGWARNING("VulkanMaterialDescriptorManager: Failed to update parameter buffer during ForceUpdate");
    }

    materialDirtyFlags_[material] = false;
    return true;
}

void VulkanMaterialDescriptorManager::Reset()
{
    // Clear cache and dirty flags (don't destroy VkDescriptorSets - they're managed by pool)
    descriptorCache_.Clear();
    materialDirtyFlags_.Clear();

    /// \brief Phase 11: Frame boundary cleanup for dirty tracking
    /// Note: Keep materialStateHashes_ for next frame comparison
    /// Only reset profiling counters for new frame
    dirtyMaterialsDetectedThisFrame_ = 0;
}

bool VulkanMaterialDescriptorManager::CreateDescriptorSetLayout()
{
    if (!graphics_)
        return false;

    VkDevice device = graphics_->GetDevice();
    if (!device)
        return false;

    // Phase 15.2: Create descriptor set layout for materials
    // Layout bindings for material properties and textures
    // Phase 24: Alternative approach - layouts can be auto-generated from shader reflection:
    // For shader programs with reflection data, bindings can be created via SPIRVResourceToBinding()
    // helper function, enabling bytecode-driven descriptor layout rather than manual specification.
    VkDescriptorSetLayoutBinding bindings[6] = {};

    // Binding 0: Material constant buffer (uniform buffer object)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 100: Diffuse texture + sampler (match shader binding)
    bindings[1].binding = 100;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 102: Normal map texture + sampler (match shader binding for sNormalMap)
    bindings[2].binding = 102;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    // Binding 103: Specular/roughness texture + sampler (match shader binding for sSpecMap)
    bindings[3].binding = 103;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].pImmutableSamplers = nullptr;

    // Binding 104: Emissive texture + sampler
    bindings[4].binding = 104;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].pImmutableSamplers = nullptr;

    // Binding 105: Environment texture + sampler
    bindings[5].binding = 105;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 6;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanMaterialDescriptorManager: Failed to create descriptor set layout");
        return false;
    }

    URHO3D_LOGINFO("VulkanMaterialDescriptorManager: Created descriptor set layout with 6 bindings (1 UBO + 5 textures)");
    return true;
}

VkDescriptorSet VulkanMaterialDescriptorManager::CreateDescriptorSet(Material* material)
{
    if (!material || !graphics_)
        return VK_NULL_HANDLE;

    VkDevice device = graphics_->GetDevice();
    if (!device || descriptorSetLayout_ == VK_NULL_HANDLE || descriptorPool_ == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    // Phase 16.2: Allocate descriptor set from pool
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS)
    {
        URHO3D_LOGERROR("VulkanMaterialDescriptorManager: Failed to allocate descriptor set for material");
        return VK_NULL_HANDLE;
    }

    /// \brief Quick Win #7: Batch descriptor updates for efficiency
    /// Collect all descriptor writes (texture + parameter) and submit in one call
    /// instead of separate vkUpdateDescriptorSets calls. This reduces CPU/GPU sync overhead.
    Vector<VkWriteDescriptorSet> batchedWrites;
    Vector<VkDescriptorImageInfo> imageInfos;
    VkDescriptorBufferInfo bufferBindInfo{};

    // Phase 16.3: Collect material parameter buffer write
    VulkanMaterialConstants params;
    ExtractMaterialParameters(material, params);

    VulkanConstantBufferPool* constantBufferPool = graphics_->GetConstantBufferPool();
    if (constantBufferPool)
    {
        VkBuffer paramBuffer = VK_NULL_HANDLE;
        VkDeviceSize paramOffset = 0;
        if (constantBufferPool->AllocateBuffer(&params, sizeof(VulkanMaterialConstants), paramBuffer, paramOffset))
        {
            bufferBindInfo.buffer = paramBuffer;
            bufferBindInfo.offset = paramOffset;
            bufferBindInfo.range = sizeof(VulkanMaterialConstants);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = 0;  // Binding 0: material parameter UBO
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferBindInfo;
            batchedWrites.Push(write);
        }
    }

    // Phase 16.4: Collect texture/sampler bindings
    VulkanSamplerCache* samplerCache = graphics_->GetSamplerCache();
    if (samplerCache)
    {
        Texture2D* diffuseTexture = dynamic_cast<Texture2D*>(material->GetTexture(TU_DIFFUSE));
        Texture2D* normalTexture = dynamic_cast<Texture2D*>(material->GetTexture(TU_NORMAL));
        Texture2D* specularTexture = dynamic_cast<Texture2D*>(material->GetTexture(TU_SPECULAR));
        Texture2D* emissiveTexture = dynamic_cast<Texture2D*>(material->GetTexture(TU_EMISSIVE));
        Texture2D* environmentTexture = dynamic_cast<Texture2D*>(material->GetTexture(TU_ENVIRONMENT));

        // Fallback to default textures
        if (!diffuseTexture) diffuseTexture = graphics_->GetDefaultDiffuseTexture();
        if (!normalTexture) normalTexture = graphics_->GetDefaultNormalTexture();
        if (!specularTexture) specularTexture = graphics_->GetDefaultSpecularTexture();
        // No defaults for emissive/environment - use null if not provided

        Texture2D* textures[] = {diffuseTexture, normalTexture, specularTexture, emissiveTexture, environmentTexture};
        const uint32_t bindingOffsets[] = {100, 102, 103, 104, 105};  // Match descriptor set layout bindings

        for (uint32_t i = 0; i < 5; ++i)
        {
            if (!textures[i]) continue;

            VkSampler sampler = samplerCache->GetSampler(
                textures[i]->GetFilterMode(),
                textures[i]->GetAddressMode(COORD_U),
                textures[i]->GetAddressMode(COORD_V),
                textures[i]->GetAddressMode(COORD_W),
                textures[i]->GetAnisotropy()
            );

            if (!sampler) continue;

            // Create descriptor image info explicitly to avoid Push() ambiguity
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = sampler;
            imageInfo.imageView = textures[i]->GetVkImageView();
            // Depth textures used as render targets are in DEPTH_STENCIL_READ_ONLY layout after render pass
            unsigned fmt = textures[i]->GetFormat();
            bool isDepth = (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT ||
                            fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT);
            imageInfo.imageLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos.Push(imageInfo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = bindingOffsets[i];
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfos.Back();
            batchedWrites.Push(write);
        }
    }

    // Phase 16.5: Submit all batched descriptor updates in one call
    if (!batchedWrites.Empty())
    {
        vkUpdateDescriptorSets(device, batchedWrites.Size(), batchedWrites.Buffer(), 0, nullptr);
        URHO3D_LOGDEBUG("VulkanMaterialDescriptorManager: Batched " + String(batchedWrites.Size()) +
                       " descriptor writes in single update call");
    }

    URHO3D_LOGDEBUG("VulkanMaterialDescriptorManager: Created descriptor set for material");
    return descriptorSet;
}

void VulkanMaterialDescriptorManager::ExtractMaterialParameters(Material* material, VulkanMaterialConstants& outParams)
{
    if (!material)
        return;

    // Phase 15.1: Extract material parameters from Urho3D Material for GPU binding
    // Initialize with reasonable defaults
    outParams.diffuseColor_[0] = 1.0f;
    outParams.diffuseColor_[1] = 1.0f;
    outParams.diffuseColor_[2] = 1.0f;
    outParams.diffuseColor_[3] = 1.0f;

    outParams.specularColor_[0] = 1.0f;
    outParams.specularColor_[1] = 1.0f;
    outParams.specularColor_[2] = 1.0f;
    outParams.specularColor_[3] = 1.0f;

    outParams.ambientColor_[0] = 0.1f;
    outParams.ambientColor_[1] = 0.1f;
    outParams.ambientColor_[2] = 0.1f;
    outParams.ambientColor_[3] = 1.0f;

    outParams.metallic_ = 0.0f;
    outParams.roughness_ = 0.5f;
    outParams.shininess_ = 16.0f;

    // Extract shader parameters from material
    const HashMap<StringHash, MaterialShaderParameter>& shaderParams = material->GetShaderParameters();

    // Extract metallic if available
    if (shaderParams.Contains(StringHash("Metallic")))
    {
        const Variant& metallic = material->GetShaderParameter("Metallic");
        if (metallic.GetType() == VAR_FLOAT)
            outParams.metallic_ = metallic.GetFloat();
    }

    // Extract roughness if available
    if (shaderParams.Contains(StringHash("Roughness")))
    {
        const Variant& roughness = material->GetShaderParameter("Roughness");
        if (roughness.GetType() == VAR_FLOAT)
            outParams.roughness_ = roughness.GetFloat();
    }

    // Extract specular (shininess) if available
    if (shaderParams.Contains(StringHash("SpecularExponent")))
    {
        const Variant& shininess = material->GetShaderParameter("SpecularExponent");
        if (shininess.GetType() == VAR_FLOAT)
            outParams.shininess_ = shininess.GetFloat();
    }

    // Clamp values to valid ranges
    outParams.metallic_ = Clamp(outParams.metallic_, 0.0f, 1.0f);
    outParams.roughness_ = Clamp(outParams.roughness_, 0.0f, 1.0f);
    outParams.shininess_ = Max(outParams.shininess_, 1.0f);  // Shininess must be >= 1
}

uint32_t VulkanMaterialDescriptorManager::CalculateTextureHash(Material* material)
{
    if (!material)
        return 0;

    // Phase 15.1: Calculate texture state hash for dirty detection
    // Hash changes when textures are modified, invalidating cached descriptors
    uint32_t hash = (uint32_t)(size_t)material;

    // Phase 15.2.5: Include texture pointers in hash for robust dirty detection
    // This ensures descriptor sets are regenerated when textures change
    Texture* diffuse = material->GetTexture(TU_DIFFUSE);
    Texture* normal = material->GetTexture(TU_NORMAL);
    Texture* specular = material->GetTexture(TU_SPECULAR);
    Texture* emissive = material->GetTexture(TU_EMISSIVE);
    Texture* environment = material->GetTexture(TU_ENVIRONMENT);

    // Combine texture pointers into hash
    if (diffuse)
        hash ^= ((uint32_t)(size_t)diffuse) << 1;
    if (normal)
        hash ^= ((uint32_t)(size_t)normal) << 2;
    if (specular)
        hash ^= ((uint32_t)(size_t)specular) << 3;
    if (emissive)
        hash ^= ((uint32_t)(size_t)emissive) << 4;
    if (environment)
        hash ^= ((uint32_t)(size_t)environment) << 5;

    // Include material parameter hash for robustness
    hash ^= material->GetShaderParameterHash();

    return hash;
}

/// Phase 19.1: Helper function to retrieve VkImageView from texture
/// Safely extracts the VkImageView handle from a Texture2D for descriptor binding.
/// Returns VK_NULL_HANDLE if texture is null or not initialized.
static inline VkImageView GetTextureImageView(Texture2D* texture)
{
    if (!texture)
        return VK_NULL_HANDLE;

    return texture->GetVkImageView();
}

/// Phase 24.1: Helper function to generate VkDescriptorSetLayoutBinding from SPIRVResource
/// Converts automatic reflection data from shader bytecode into Vulkan descriptor bindings.
/// Simplifies descriptor layout generation by enabling bytecode-driven rather than manual specification.
static inline VkDescriptorSetLayoutBinding SPIRVResourceToBinding(const SPIRVResource& resource)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = resource.binding;
    binding.descriptorType = resource.descriptorType;
    binding.descriptorCount = resource.descriptorCount;
    binding.stageFlags = resource.stageFlags;
    binding.pImmutableSamplers = nullptr;

    return binding;
}

/// Phase 25.1: Helper function for dynamic descriptor set allocation
/// Allocates descriptor sets using specified layout from descriptor pool.
/// Enables per-material variant descriptor set generation for shader-specific layouts.
/// Returns VK_NULL_HANDLE on allocation failure (e.g., pool exhausted).
static inline VkDescriptorSet AllocateDynamicDescriptorSet(
    VkDevice device,
    VkDescriptorPool pool,
    VkDescriptorSetLayout layout)
{
    if (!device || pool == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);

    if (result != VK_SUCCESS)
    {
        URHO3D_LOGWARNING("VulkanMaterialDescriptorManager: Failed to allocate dynamic descriptor set (result=" +
                         String((int)result) + ")");
        return VK_NULL_HANDLE;
    }

    return descriptorSet;
}

bool VulkanMaterialDescriptorManager::UpdateTextureBindings(Material* material, VkDescriptorSet descriptorSet)
{
    if (!material || !descriptorSet || !graphics_)
        return false;

    VkDevice device = graphics_->GetDevice();
    if (!device)
        return false;

    // Bind all material textures to their descriptor set bindings.
    // Layout supports 5 texture slots matching the shader compiler's sampler bindings:
    //   binding 100 = TU_DIFFUSE      (sDiffMap / sWeightMap0)
    //   binding 102 = TU_NORMAL       (sNormalMap / sDetailMap1)
    //   binding 103 = TU_SPECULAR     (sSpecMap / sDetailMap2)
    //   binding 104 = TU_EMISSIVE     (sEmissiveMap / sDetailMap3)
    //   binding 105 = TU_ENVIRONMENT  (sEnvMap / sWaterMap4)

    VulkanSamplerCache* samplerCache = graphics_->GetSamplerCache();
    if (!samplerCache)
        return false;

    // All 5 texture units the descriptor set layout declares
    const TextureUnit units[] = {TU_DIFFUSE, TU_NORMAL, TU_SPECULAR, TU_EMISSIVE, TU_ENVIRONMENT};
    const uint32_t bindings[] = {100, 102, 103, 104, 105};
    const unsigned NUM_SLOTS = 5;

    VkWriteDescriptorSet textureWrites[NUM_SLOTS];
    VkDescriptorImageInfo imageInfos[NUM_SLOTS];
    uint32_t writeCount = 0;

    for (unsigned i = 0; i < NUM_SLOTS; ++i)
    {
        Texture2D* tex = dynamic_cast<Texture2D*>(material->GetTexture(units[i]));

        // Fallback to default textures for the three standard slots
        if (!tex)
        {
            if (units[i] == TU_DIFFUSE)
                tex = graphics_->GetDefaultDiffuseTexture();
            else if (units[i] == TU_NORMAL)
                tex = graphics_->GetDefaultNormalTexture();
            else if (units[i] == TU_SPECULAR)
                tex = graphics_->GetDefaultSpecularTexture();
            // TU_EMISSIVE and TU_ENVIRONMENT: no fallback — skip if absent
        }

        if (!tex)
            continue;

        VkSampler sampler = samplerCache->GetSampler(
            tex->GetFilterMode(),
            tex->GetAddressMode(COORD_U),
            tex->GetAddressMode(COORD_V),
            tex->GetAddressMode(COORD_W),
            tex->GetAnisotropy()
        );
        if (!sampler)
            continue;

        imageInfos[writeCount].sampler = sampler;
        imageInfos[writeCount].imageView = GetTextureImageView(tex);
        {
            unsigned fmt = tex->GetFormat();
            bool isDepthFmt = (fmt == VK_FORMAT_D16_UNORM || fmt == VK_FORMAT_D32_SFLOAT ||
                               fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT);
            imageInfos[writeCount].imageLayout = isDepthFmt ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        textureWrites[writeCount] = {};
        textureWrites[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureWrites[writeCount].dstSet = descriptorSet;
        textureWrites[writeCount].dstBinding = bindings[i];
        textureWrites[writeCount].descriptorCount = 1;
        textureWrites[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureWrites[writeCount].pImageInfo = &imageInfos[writeCount];
        writeCount++;
    }

    if (writeCount > 0)
    {
        vkUpdateDescriptorSets(device, writeCount, textureWrites, 0, nullptr);
        URHO3D_LOGDEBUG("VulkanMaterialDescriptorManager: Bound " + String(writeCount) + " textures to material descriptor");
    }

    return true;
}

bool VulkanMaterialDescriptorManager::UpdateParameterBuffer(Material* material, VkDescriptorSet descriptorSet)
{
    if (!material || !descriptorSet || !graphics_)
        return false;

    VkDevice device = graphics_->GetDevice();
    if (!device)
        return false;

    // Phase 16.3: Update material parameter constant buffer
    // Extract parameters and prepare for GPU binding
    VulkanMaterialConstants params;
    ExtractMaterialParameters(material, params);

    // Phase 16.3.1: Allocate space from constant buffer pool (Quick Win #6)
    // Using VulkanConstantBufferPool for efficient memory management
    // instead of creating individual buffers per material
    VulkanConstantBufferPool* constantBufferPool = graphics_->GetConstantBufferPool();
    if (!constantBufferPool)
    {
        URHO3D_LOGWARNING("VulkanMaterialDescriptorManager: Constant buffer pool not available");
        return false;
    }

    // Allocate space for material parameters from the pool
    VkBuffer paramBuffer;
    VkDeviceSize paramOffset;
    if (!constantBufferPool->AllocateBuffer(&params, sizeof(VulkanMaterialConstants), paramBuffer, paramOffset))
    {
        URHO3D_LOGWARNING("VulkanMaterialDescriptorManager: Failed to allocate parameter buffer from pool");
        return false;
    }

    // Phase 16.3.2: Update descriptor binding with buffer info
    VkDescriptorBufferInfo bufferBindInfo{};
    bufferBindInfo.buffer = paramBuffer;
    bufferBindInfo.offset = paramOffset;
    bufferBindInfo.range = sizeof(VulkanMaterialConstants);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;  // Binding 0: material parameter UBO
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferBindInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    URHO3D_LOGDEBUG("VulkanMaterialDescriptorManager: Updated parameter buffer for material (offset=" +
                   String((int)paramOffset) + ")");
    return true;
}

void VulkanMaterialDescriptorManager::InvalidateVariantCacheForShader(uint64_t shaderHash)
{
    // Phase 26: Invalidate all cached descriptor sets for a shader
    // Called when shader is recompiled or changed to clear stale descriptor sets
    uint32_t invalidatedCount = 0;

    // Iterate through variant cache and remove entries matching shader
    auto it = descriptorSetVariantCache_.Begin();
    while (it != descriptorSetVariantCache_.End())
    {
        // Key format: (shader_hash << 32) | material_hash
        uint64_t cachedShaderHash = it->first_ >> 32;

        if (cachedShaderHash == shaderHash)
        {
            // Match found: remove this descriptor set from cache
            it = descriptorSetVariantCache_.Erase(it);
            invalidatedCount++;
        }
        else
        {
            ++it;
        }
    }

    // Update metrics
    cacheInvalidationCount_++;

    // Log invalidation event
    if (invalidatedCount > 0)
    {
        URHO3D_LOGDEBUG("VulkanMaterialDescriptorManager: Invalidated " + String(invalidatedCount) +
                       " descriptor sets for shader (invalidation #" + String(cacheInvalidationCount_) + ")");
    }
}

}

#endif  // URHO3D_VULKAN
