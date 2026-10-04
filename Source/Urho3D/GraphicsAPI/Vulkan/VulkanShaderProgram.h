//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan shader program (Phase 9)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/HashMap.h"
#include "../../Container/Ptr.h"
#include "../../Container/Vector.h"
#include "../ShaderVariation.h"
#include "../GraphicsDefs.h"
#include "VulkanSPIRVReflect.h"
#include <vulkan/vulkan.h>

namespace Urho3D
{

class ShaderVariation;
class ConstantBuffer;

/// Vulkan shader program linking vertex and pixel shaders with parameter metadata
class VulkanShaderProgram : public RefCounted
{
public:
    /// Constructor
    VulkanShaderProgram(ShaderVariation* vs, ShaderVariation* ps);
    /// Destructor
    virtual ~VulkanShaderProgram();

    /// Get vertex shader variation
    ShaderVariation* GetVertexShader() const { return vertexShader_; }

    /// Get pixel shader variation
    ShaderVariation* GetPixelShader() const { return pixelShader_; }

    /// Check if program has a parameter
    bool HasParameter(StringHash param) const { return parameters_.Contains(param); }

    /// Get parameter metadata
    const ShaderParameter* GetParameter(StringHash param) const;

    /// Get pipeline layout
    VkPipelineLayout GetPipelineLayout() const { return pipelineLayout_; }

    /// Create pipeline layout with descriptor set layout (single set, legacy)
    bool CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);

    /// Create pipeline layout with multiple descriptor set layouts (Phase 36: Multi-set support)
    /// Supports up to 3 descriptor sets: Set 0 (materials), Set 1 (textures), Set 2 (constants)
    bool CreatePipelineLayout(VkDevice device, const Vector<VkDescriptorSetLayout>& descriptorSetLayouts);

    /// Destroy pipeline layout
    void DestroyPipelineLayout(VkDevice device);

    /// Phase 23: Get reflected shader resources from SPIR-V analysis
    /// Returns vector of descriptor bindings extracted from compiled shaders
    const Vector<SPIRVResource>& GetReflectedResources() const { return reflectedResources_; }

    /// Phase 23: Set reflected shader resources (called during shader compilation)
    void SetReflectedResources(const Vector<SPIRVResource>& resources) { reflectedResources_ = resources; }

    /// Shader parameters (combined from VS and PS)
    HashMap<StringHash, ShaderParameter> parameters_;

    /// Constant buffers for each parameter group
    SharedPtr<ConstantBuffer> constantBuffers_[MAX_SHADER_PARAMETER_GROUPS];

private:
    /// Combine parameters from both shader variations
    void LinkParameters();

    /// Vertex shader
    WeakPtr<ShaderVariation> vertexShader_;

    /// Pixel shader
    WeakPtr<ShaderVariation> pixelShader_;

    /// Pipeline layout for descriptor set binding
    VkPipelineLayout pipelineLayout_{nullptr};

    /// Phase 23: Cached SPIR-V reflection results (descriptor bindings)
    /// Extracted from compiled shader bytecode for automatic descriptor layout generation
    Vector<SPIRVResource> reflectedResources_;
};

} // namespace Urho3D

#endif  // URHO3D_VULKAN
