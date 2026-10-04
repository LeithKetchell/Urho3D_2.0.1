//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan graphics pipeline state management (Phase 8)

#pragma once

#ifdef URHO3D_VULKAN

#include "../../Container/Vector.h"
#include "../../Container/HashMap.h"
#include "../../Math/StringHash.h"
#include "../GraphicsDefs.h"
#include <vulkan/vulkan.h>
#include <functional>

namespace Urho3D
{

/// Pipeline state for caching and reuse
struct VulkanPipelineState
{
    // Primitive topology
    PrimitiveType primitiveType = TRIANGLE_LIST;

    // Rasterization state
    CullMode cullMode = CULL_CCW;
    FillMode fillMode = FILL_SOLID;
    bool polygonOffsetEnable = false;

    // Depth/Stencil state
    CompareMode depthTest = CMP_LESSEQUAL;
    bool depthWrite = true;
    bool stencilTest = false;
    CompareMode stencilTestMode = CMP_ALWAYS;
    StencilOp stencilPass = OP_KEEP;
    StencilOp stencilFail = OP_KEEP;
    StencilOp stencilZFail = OP_KEEP;
    unsigned stencilRef = 0;
    unsigned stencilCompareMask = 0xFF;
    unsigned stencilWriteMask = 0xFF;

    // Blend state
    BlendMode blendMode = BLEND_REPLACE;
    bool alphaToCoverage = false;

    // Color write
    bool colorWrite = true;

    // Hash for caching
    unsigned Hash() const;
    bool operator==(const VulkanPipelineState& other) const;
};


} // namespace Urho3D

#endif  // URHO3D_VULKAN
