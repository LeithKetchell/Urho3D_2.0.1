// SkeletonRemapper — map bones between two different skeletons by shape + hierarchy.
// No bone names used. Matching is structural: shape type, aspect ratio, hierarchy
// depth, relative position. Bones that share similar fitted shapes at the same
// structural position in the hierarchy are the same bone, regardless of naming.
//
// Designed to work with BoneCollisionGenerator output. The shared shape pool
// makes this trivial: if two bones on different rigs use the same capsule
// dimensions, they map to each other.

#pragma once

#include "BoneCollisionGenerator.h"
#include "../Container/Vector.h"
#include "../Container/HashMap.h"

namespace Urho3D
{

class Model;

/// Single bone-to-bone mapping entry.
struct BoneMapping
{
    /// Source skeleton bone index.
    int sourceBone{-1};
    /// Target skeleton bone index.
    int targetBone{-1};
    /// Match confidence (0.0 = no match, 1.0 = perfect).
    float confidence{0.0f};
};

/// Build bone-to-bone mappings between two skeletons using shape + hierarchy matching.
/// No bone names are consulted — matching is purely structural.
class URHO3D_API SkeletonRemapper
{
public:
    /// Generate a mapping from source skeleton to target skeleton.
    /// Both models must have been through BoneCollisionGenerator::Generate() first —
    /// pass the resulting BoneBounds vectors directly.
    static Vector<BoneMapping> BuildMapping(
        Model* sourceModel, const Vector<BoneBounds>& sourceBounds,
        Model* targetModel, const Vector<BoneBounds>& targetBounds);

    /// Convenience: generate BoneBounds internally and build the mapping.
    static Vector<BoneMapping> BuildMapping(Model* sourceModel, Model* targetModel);

    /// Log the mapping to the Urho3D log for diagnostics.
    static void LogMapping(Model* sourceModel, Model* targetModel,
                           const Vector<BoneMapping>& mapping);

    // Implementation helpers are in the .cpp — no private members needed in the header.
};

} // namespace Urho3D
