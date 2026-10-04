//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// SPIR-V reflection utilities for descriptor set creation (Phase 7)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace Urho3D
{

/// \brief Reflection data for a single SPIR-V resource descriptor
/// \details Represents a single uniform buffer, sampler, image, or other descriptor
/// extracted from SPIR-V bytecode via reflection. Contains all information needed
/// to create corresponding VkDescriptorSetLayoutBinding.
///
/// **Fields:**
/// - set: Descriptor set number (from OpDecorate/DecorationDescriptorSet)
/// - binding: Binding index within descriptor set (from OpDecorate/DecorationBinding)
/// - descriptorType: Vulkan descriptor type (UNIFORM_BUFFER, SAMPLER, etc.)
/// - descriptorCount: Array size (typically 1 for simple bindings)
/// - stageFlags: Shader stages using this resource (VS, FS, GS, etc.)
///
/// **Typical Usage:**
/// 1. ReflectShaderResources() populates Vector<SPIRVResource> from shader bytecode
/// 2. For each resource, create VkDescriptorSetLayoutBinding with matching fields
/// 3. Create VkDescriptorSetLayout from collected bindings
/// 4. Use in descriptor pool and pipeline layout creation
struct SPIRVResource
{
    uint32_t set = 0;                                               ///< Descriptor set number (default: 0)
    uint32_t binding = 0;                                          ///< Binding index (default: 0)
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;  ///< Resource type (default: uniform buffer)
    uint32_t descriptorCount = 1;                                  ///< Array size (default: 1)
    VkShaderStageFlags stageFlags = 0;                            ///< Shader stages (VK_SHADER_STAGE_*_BIT)
};

/// \brief SPIR-V reflection helper for analyzing shader bytecode
/// \details Provides automatic shader reflection to extract descriptor bindings from
/// compiled SPIR-V bytecode. Enables automatic descriptor set layout generation without
/// manual shader annotation. Uses two-pass SPIR-V parsing: first collecting OpDecorate
/// instructions for binding/set information, then correlating with OpVariable declarations.
///
/// **How SPIR-V Reflection Works:**
/// 1. Parse SPIR-V header to validate magic number and version
/// 2. First pass: Collect all OpDecorate instructions to build binding/set mappings
/// 3. Second pass: Parse OpVariable instructions and match with collected decorations
/// 4. Return Vector<SPIRVResource> with all discovered descriptors
///
/// **Example SPIR-V Layout:**
/// ```
/// OpDecorate %var Binding 0           ; binding = 0
/// OpDecorate %var DescriptorSet 0     ; set = 0
/// OpVariable %type %var ...           ; Create resource with set=0, binding=0
/// ```
///
/// **Supported Resource Types:**
/// - VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER (uniform blocks)
/// - VK_DESCRIPTOR_TYPE_STORAGE_BUFFER (storage blocks)
/// - VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER (samplers)
/// - VK_DESCRIPTOR_TYPE_STORAGE_IMAGE (storage images)
/// - VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER (textures + samplers)
/// - And all other Vulkan descriptor types
///
/// **Integration with Shader Compiler:**
/// - Typically called on output of VulkanShaderCompiler::CompileGLSLToSPIRV()
/// - Enables automatic descriptor set generation from GLSL layout qualifiers
/// - Reduces manual synchronization between shader and C++ descriptor setup
///
/// **Thread Safety:**
/// - All methods are static and stateless (no shared state)
/// - Safe for concurrent reflection of multiple shaders
class VulkanSPIRVReflect
{
public:
    /// \brief Reflect SPIR-V bytecode to extract descriptor bindings
    /// \param spirvBytecode Compiled SPIR-V bytecode (output from shader compiler)
    /// \param shaderStage Shader stage this bytecode belongs to (VS, FS, etc.)
    /// \param resources Output parameter: Vector of extracted SPIRVResource descriptors
    /// \returns True if reflection successful, false if bytecode invalid
    /// \details Analyzes SPIR-V bytecode to automatically extract all descriptor bindings
    /// (uniform buffers, samplers, images, etc.) and populate resources vector. Uses
    /// two-pass parsing for robustness. On success, resources contains all discovered
    /// descriptors with correct set/binding/type information. Returns true even if no
    /// resources found (adds default uniform buffer as fallback).
    static bool ReflectShaderResources(
        const Vector<uint32_t>& spirvBytecode,
        VkShaderStageFlagBits shaderStage,
        Vector<SPIRVResource>& resources);

private:
    /// \brief Validate SPIR-V module header
    /// \param spirv SPIR-V bytecode to validate
    /// \returns True if valid header found, false if magic number or size invalid
    /// \details Checks magic number (0x07230203) and minimum size (5 words).
    /// Warns if version is older than 1.0.0 but continues anyway.
    /// Must be called before any other reflection operations.
    static bool ValidateSPIRVHeader(const Vector<uint32_t>& spirv);

    /// \brief Extract OpDecorate instructions for resource metadata
    /// \param spirv SPIR-V bytecode to analyze
    /// \param resources Output parameter: populated with reflected resources
    /// \details Two-pass algorithm:
    /// 1. First pass: Scan all OpDecorate instructions to build HashMap of binding/set assignments
    /// 2. Second pass: Scan OpVariable instructions to match with decorations and create resources
    /// Handles correlation between variable IDs and their decoration metadata.
    /// Infers descriptor types from storage class (0 = image, others = buffer).
    static void ExtractResourceMetadata(
        const Vector<uint32_t>& spirv,
        VkShaderStageFlagBits shaderStage,
        Vector<SPIRVResource>& resources);
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
