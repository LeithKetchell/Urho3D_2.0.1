// AnimationRetargeter — retarget animations between different skeletons.
// Uses SkeletonRemapper's BoneMapping to remap bone tracks, and adjusts
// rotations for bind pose differences between source and target skeletons.
// The shapes are the universal adapter — any animation on any model.

#pragma once

#include "SkeletonRemapper.h"
#include "../Container/Ptr.h"

namespace Urho3D
{

class Animation;
class Model;

/// Retarget an animation from one skeleton to another.
class URHO3D_API AnimationRetargeter
{
public:
    /// Retarget a source animation to a target skeleton using a pre-built bone mapping.
    /// Returns a new Animation resource with tracks remapped to the target bone names
    /// and rotations adjusted for bind pose differences.
    /// The source animation is not modified.
    static SharedPtr<Animation> Retarget(
        Animation* sourceAnim,
        Model* sourceModel,
        Model* targetModel,
        const Vector<BoneMapping>& mapping);

    /// Convenience: build the mapping internally and retarget.
    static SharedPtr<Animation> Retarget(
        Animation* sourceAnim,
        Model* sourceModel,
        Model* targetModel);
};

} // namespace Urho3D
