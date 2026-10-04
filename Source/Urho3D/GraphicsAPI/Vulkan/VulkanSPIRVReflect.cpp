//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// SPIR-V reflection utilities for descriptor set creation (Phase 7)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanSPIRVReflect.h"
#include "../../IO/Log.h"
#include <cstring>

namespace Urho3D

// Debug logging disabled — only enable for descriptor troubleshooting
// #define VULKAN_DEBUG_LOGGING 1

{

// SPIR-V Constants
static const uint32_t SPIRV_MAGIC = 0x07230203;
static const uint32_t SPIRV_VERSION = 0x00010000;

// SPIR-V Opcodes
enum SPIRVOp
{
    OpNop = 0,
    OpTypeVoid = 19,
    OpTypeBool = 20,
    OpTypeInt = 21,
    OpTypeFloat = 22,
    OpTypeVector = 23,
    OpTypeMatrix = 24,
    OpTypeImage = 25,
    OpTypeSampler = 26,
    OpTypeSampledImage = 27,
    OpTypeArray = 28,
    OpTypeRuntimeArray = 29,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpConstant = 43,
    OpVariable = 59,
    OpLoad = 61,
    OpDecorate = 71,
    OpMemberDecorate = 72,
};

// SPIR-V Decorations
enum SPIRVDecoration
{
    DecorationDescriptorSet = 34,
    DecorationBinding = 33,
};

// Storage classes for OpVariable
enum SPIRVStorageClass
{
    StorageClassUniform = 0,
    StorageClassUniformConstant = 0,
};

bool VulkanSPIRVReflect::ReflectShaderResources(
    const Vector<uint32_t>& spirvBytecode,
    VkShaderStageFlagBits shaderStage,
    Vector<SPIRVResource>& resources)
{
    resources.Clear();

    if (spirvBytecode.Empty())
    {
        URHO3D_LOGERROR("Empty SPIR-V bytecode");
        return false;
    }

    // Validate SPIR-V header
    if (!ValidateSPIRVHeader(spirvBytecode))
    {
        URHO3D_LOGERROR("Invalid SPIR-V header");
        return false;
    }

    // Extract resource metadata from decorations and variable declarations
    ExtractResourceMetadata(spirvBytecode, shaderStage, resources);

    // Set shader stage flags for all resources found
    for (auto& res : resources)
    {
        res.stageFlags = shaderStage;
    }

    if (!resources.Empty())
    {
        URHO3D_LOGDEBUG("Reflected " + String((int)resources.Size()) + " resources from SPIR-V");
    }
    else
    {
        // Default fallback: Add standard uniform buffer binding
        SPIRVResource uniformBuffer;
        uniformBuffer.set = 0;
        uniformBuffer.binding = 0;
        uniformBuffer.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBuffer.descriptorCount = 1;
        uniformBuffer.stageFlags = shaderStage;
        resources.Push(uniformBuffer);

        URHO3D_LOGDEBUG("No resources found in SPIR-V, using default uniform buffer binding");
    }

    return true;
}

bool VulkanSPIRVReflect::ValidateSPIRVHeader(const Vector<uint32_t>& spirv)
{
    if (spirv.Size() < 5)
    {
        URHO3D_LOGERROR("SPIR-V bytecode too short for header");
        return false;
    }

    // Check magic number
    if (spirv[0] != SPIRV_MAGIC)
    {
        URHO3D_LOGERROR("Invalid SPIR-V magic number");
        return false;
    }

    // Check version (we support v1.0+)
    uint32_t version = spirv[1];
    if (version < SPIRV_VERSION)
    {
        URHO3D_LOGWARNING("SPIR-V version " + String(version >> 16) + "." + String((version >> 8) & 0xFF) + " may not be fully supported");
    }

    // Header format:
    // [0] Magic number
    // [1] Version
    // [2] Generator's magic number
    // [3] Bound (all IDs < bound)
    // [4] Schema (instruction schema version)

    return true;
}

void VulkanSPIRVReflect::ExtractResourceMetadata(
    const Vector<uint32_t>& spirv,
    VkShaderStageFlagBits shaderStage,
    Vector<SPIRVResource>& resources)
{
    resources.Clear();

    if (spirv.Size() < 5)
        return;

    // First pass: Collect all decorations (binding/set info)
    // Map from variable ID to its binding/set decorations
    HashMap<uint32_t, uint32_t> bindingMap;    // ID -> binding number
    HashMap<uint32_t, uint32_t> descriptorSetMap;  // ID -> descriptor set
    HashMap<uint32_t, VkDescriptorType> descriptorTypeMap;  // ID -> type

    size_t idx = 5;  // Skip header

    // First pass: extract decorations
    while (idx < spirv.Size())
    {
        uint32_t word = spirv[idx];
        uint16_t wordCount = word >> 16;
        uint16_t opcode = word & 0xFFFF;

        if (wordCount == 0)
            break;

        // OpDecorate: opcode, target_id, decoration, [literal arguments]
        if (opcode == OpDecorate && wordCount >= 3)
        {
            uint32_t targetId = spirv[idx + 1];
            uint32_t decoration = spirv[idx + 2];

            // Extract binding number (Binding = 33)
            if (decoration == DecorationBinding && wordCount >= 4)
            {
                uint32_t bindingValue = spirv[idx + 3];
                bindingMap[targetId] = bindingValue;
            }

            // Extract descriptor set (DescriptorSet = 34)
            if (decoration == DecorationDescriptorSet && wordCount >= 4)
            {
                uint32_t setNumber = spirv[idx + 3];
                descriptorSetMap[targetId] = setNumber;
            }
        }

        idx += wordCount;
    }

    // Second pass: extract variables with their storage class and type
    // Also track resource index → variable ID for OpLoad filtering in Pass 3
    HashMap<unsigned, uint32_t> resourceIndexToVariableId;  // index in resources → variable ID
    idx = 5;

    while (idx < spirv.Size())
    {
        uint32_t word = spirv[idx];
        uint16_t wordCount = word >> 16;
        uint16_t opcode = word & 0xFFFF;

        if (wordCount == 0)
            break;

        // OpVariable: opcode, result_type, result_id, storage_class, [initializer]
        if (opcode == OpVariable && wordCount >= 4)
        {
            uint32_t resultId = spirv[idx + 2];
            uint32_t storageClass = spirv[idx + 3];

            // Check if this variable has binding info (it's a resource)
            if (bindingMap.Contains(resultId))
            {
                SPIRVResource resource;
                resource.binding = bindingMap[resultId];
                resource.set = descriptorSetMap.Contains(resultId) ? descriptorSetMap[resultId] : 0;
                resource.descriptorCount = 1;

                // Infer descriptor type from storage class
                // StorageClass 0 = UniformConstant (typically samplers/sampled images)
                // For now, default to uniform buffer - real implementation would analyze OpTypePointer
                if (storageClass == 0)
                {
                    resource.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                }
                else
                {
                    resource.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                }

                // Track mapping: resource index → variable ID (for OpLoad filtering)
                unsigned resourceIndex = resources.Size();
                resourceIndexToVariableId[resourceIndex] = resultId;

                resources.Push(resource);
            }
        }

        idx += wordCount;
    }

    // Pass 3 DISABLED: The OpLoad detection was filtering ALL samplers including
    // actively used ones (sDiffMap in Bloom/CopyFramebuffer). Root cause: the OpLoad
    // opcode scan doesn't find loads, possibly due to SPIR-V optimizer transformations.
    // All declared samplers are now included in the pipeline layout. Unused ones get
    // a valid fallback texture binding in CreateReflectionBasedDescriptorSet_Vulkan.

    // DEBUG: Disabled for performance
    // static int reflectionDebugCount = 0;
    // if (reflectionDebugCount < 3)
    // {
    //
    //     unsigned uniformBufferCount = 0;
    //     unsigned samplerCount = 0;
    //
    //     for (const SPIRVResource& res : resources)
    //     {
    //         const char* typeStr = (res.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
    //                               ? "UNIFORM_BUFFER" : "SAMPLER";
    //                 res.set, res.binding, typeStr);
    //
    //         if (res.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
    //             uniformBufferCount++;
    //         else
    //             samplerCount++;
    //     }
    //
    //             uniformBufferCount, samplerCount);
    //     reflectionDebugCount++;
    // }
}


} // namespace Urho3D

#endif  // URHO3D_VULKAN
