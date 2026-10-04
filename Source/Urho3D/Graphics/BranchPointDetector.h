// BranchPointDetector — identify anatomical landmarks from skeleton topology.
// No bone names, no heuristics — pure branching pattern analysis.

#pragma once

#include "../Container/Vector.h"

namespace Urho3D
{

class Skeleton;

/// Anatomical landmark type identified by branching pattern.
enum BranchPointType
{
    BRANCH_NONE = 0,
    BRANCH_HIP,         // lowest major branch: 3+ children (2 legs + spine)
    BRANCH_SHOULDER,    // upper major branch: 3+ children (2 arms + neck/head)
    BRANCH_HEAD         // terminal above shoulder branch, 0-1 children
};

/// A detected branch point in the skeleton.
struct BranchPoint
{
    /// Bone index in the skeleton.
    int boneIndex{-1};
    /// Detected landmark type.
    BranchPointType type{BRANCH_NONE};
    /// Number of direct children.
    int childCount{0};
    /// Depth from root (0 = root).
    int depth{0};
};

/// Detect anatomical landmarks from skeleton branching patterns alone.
class URHO3D_API BranchPointDetector
{
public:
    /// Analyze a skeleton and return detected branch points.
    /// Only returns bones identified as landmarks (hip, shoulder, head).
    static Vector<BranchPoint> Detect(const Skeleton& skeleton);
};

} // namespace Urho3D
