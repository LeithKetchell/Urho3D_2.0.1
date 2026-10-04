//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Vulkan graphics pipeline state management (Phase 8)

#include "../../Precompiled.h"

#ifdef URHO3D_VULKAN

#include "VulkanPipelineState.h"
#include "../../IO/Log.h"
#include <algorithm>

namespace Urho3D
{

unsigned VulkanPipelineState::Hash() const
{
    unsigned hash = 0;
    hash = hash * 31 + primitiveType;
    hash = hash * 31 + cullMode;
    hash = hash * 31 + fillMode;
    hash = hash * 31 + polygonOffsetEnable;
    hash = hash * 31 + depthTest;
    hash = hash * 31 + depthWrite;
    hash = hash * 31 + stencilTest;
    hash = hash * 31 + blendMode;
    hash = hash * 31 + alphaToCoverage;
    hash = hash * 31 + colorWrite;
    return hash;
}

bool VulkanPipelineState::operator==(const VulkanPipelineState& other) const
{
    return primitiveType == other.primitiveType &&
           cullMode == other.cullMode &&
           fillMode == other.fillMode &&
           polygonOffsetEnable == other.polygonOffsetEnable &&
           depthTest == other.depthTest &&
           depthWrite == other.depthWrite &&
           stencilTest == other.stencilTest &&
           blendMode == other.blendMode &&
           alphaToCoverage == other.alphaToCoverage &&
           colorWrite == other.colorWrite;
}

} // namespace Urho3D

#endif  // URHO3D_VULKAN
