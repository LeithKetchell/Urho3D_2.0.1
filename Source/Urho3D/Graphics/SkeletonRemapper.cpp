// SkeletonRemapper — topological bone matching between skeletons.
// Uses BranchPointDetector to anchor hip/shoulder/head landmarks,
// then classifies limbs by subtree size and matches chains positionally.
// No bone names consulted — purely structural.

#include "../Precompiled.h"

#include "SkeletonRemapper.h"
#include "BranchPointDetector.h"
#include "Model.h"
#include "Skeleton.h"
#include "../IO/Log.h"
#include "../Math/MathDefs.h"

namespace Urho3D
{

// ============================================================================
// Helpers
// ============================================================================

/// Get direct children of a bone.
static void GetChildren(const Vector<Bone>& bones, int parentIdx, Vector<int>& children)
{
    children.Clear();
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if ((int)i == parentIdx) continue;
        if (bones[i].parentIndex_ == parentIdx)
            children.Push((int)i);
    }
}

/// Count all descendants of a bone.
static int DescendantCount(const Vector<Bone>& bones, int boneIndex)
{
    int count = 0;
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if ((int)i == boneIndex) continue;
        int current = (int)i;
        for (int hop = 0; hop < (int)bones.Size(); ++hop)
        {
            int parent = bones[current].parentIndex_;
            if (parent == boneIndex) { ++count; break; }
            if (parent == current || parent < 0 || parent >= (int)bones.Size()) break;
            current = parent;
        }
    }
    return count;
}

/// Compute bind-pose world position for a bone.
static Vector3 BoneWorldPos(const Vector<Bone>& bones, int boneIndex)
{
    Vector3 pos = bones[boneIndex].initialPosition_;
    int current = boneIndex;
    for (int hop = 0; hop < (int)bones.Size(); ++hop)
    {
        int parent = bones[current].parentIndex_;
        if (parent == current || parent < 0 || parent >= (int)bones.Size()) break;
        pos = bones[parent].initialPosition_ + bones[parent].initialRotation_ * pos;
        current = parent;
    }
    return pos;
}

/// Walk a bone chain following the largest-subtree child at forks.
/// Stops at stopAt bone index, or at a leaf.
static void WalkChain(const Vector<Bone>& bones, int startBone, Vector<int>& chain, int stopAt = -1)
{
    chain.Clear();
    int current = startBone;
    for (int hop = 0; hop < (int)bones.Size(); ++hop)
    {
        chain.Push(current);
        if (current == stopAt) break;
        Vector<int> children;
        GetChildren(bones, current, children);
        if (children.Size() == 1)
            current = children[0];
        else if (children.Size() > 1)
        {
            int bestChild = children[0];
            int bestDesc = DescendantCount(bones, children[0]);
            for (unsigned c = 1; c < children.Size(); ++c)
            {
                int d = DescendantCount(bones, children[c]);
                if (d > bestDesc) { bestDesc = d; bestChild = children[c]; }
            }
            current = bestChild;
        }
        else
            break;
    }
}

/// Match two bone chains by position (1st→1st, 2nd→2nd, etc.)
static void MatchChains(const Vector<int>& srcChain, const Vector<int>& tgtChain,
    Vector<BoneMapping>& result, Vector<bool>& tgtClaimed)
{
    unsigned count = Min(srcChain.Size(), tgtChain.Size());
    for (unsigned i = 0; i < count; ++i)
    {
        int s = srcChain[i];
        int t = tgtChain[i];
        if (!tgtClaimed[t] && result[s].targetBone < 0)
        {
            result[s].targetBone = t;
            result[s].confidence = 0.9f - (float)i * 0.02f;
            tgtClaimed[t] = true;
        }
    }
}

/// Sort children by descendant count descending.
static Vector<int> SortByDescendants(const Vector<Bone>& bones, const Vector<int>& children)
{
    Vector<int> sorted = children;
    for (unsigned i = 0; i < sorted.Size(); ++i)
        for (unsigned j = i + 1; j < sorted.Size(); ++j)
            if (DescendantCount(bones, sorted[j]) > DescendantCount(bones, sorted[i]))
                Swap(sorted[i], sorted[j]);
    return sorted;
}

// ============================================================================
// Limb classification by subtree size + bind-pose X sign
// ============================================================================

/// From hip: biggest subtree = spine, two smallest = legs (left/right by X).
static void ClassifyHipChildren(const Vector<Bone>& bones, const Vector<Vector3>& worldPos,
    const Vector<int>& children,
    int& spineChild, int& leftLeg, int& rightLeg)
{
    spineChild = leftLeg = rightLeg = -1;
    if (children.Empty()) return;

    Vector<int> sorted = SortByDescendants(bones, children);
    spineChild = sorted[0];

    // Remaining = leg candidates, left/right by bind-pose X
    Vector<int> legs;
    for (unsigned i = 1; i < sorted.Size(); ++i)
    {
        // Skip helper bones with zero descendants (Bip01_Footsteps etc.)
        if (DescendantCount(bones, sorted[i]) > 0)
            legs.Push(sorted[i]);
    }

    if (legs.Size() >= 2)
    {
        if (worldPos[legs[0]].x_ < worldPos[legs[1]].x_)
            { leftLeg = legs[0]; rightLeg = legs[1]; }
        else
            { leftLeg = legs[1]; rightLeg = legs[0]; }
    }
    else if (legs.Size() == 1)
        leftLeg = legs[0];
}

/// From shoulder: two biggest subtrees = arms (left/right by X), smallest = head/neck.
static void ClassifyShoulderChildren(const Vector<Bone>& bones, const Vector<Vector3>& worldPos,
    const Vector<int>& children,
    int& headChild, int& leftArm, int& rightArm)
{
    headChild = leftArm = rightArm = -1;
    if (children.Empty()) return;

    Vector<int> sorted = SortByDescendants(bones, children);

    if (sorted.Size() >= 3)
    {
        // Two biggest = arms, smallest = head
        Vector<int> arms;
        arms.Push(sorted[0]);
        arms.Push(sorted[1]);
        headChild = sorted[2];

        if (worldPos[arms[0]].x_ < worldPos[arms[1]].x_)
            { leftArm = arms[0]; rightArm = arms[1]; }
        else
            { leftArm = arms[1]; rightArm = arms[0]; }
    }
    else if (sorted.Size() == 2)
    {
        // Two children from shoulder — both arms, no separate head child
        if (worldPos[sorted[0]].x_ < worldPos[sorted[1]].x_)
            { leftArm = sorted[0]; rightArm = sorted[1]; }
        else
            { leftArm = sorted[1]; rightArm = sorted[0]; }
    }
    else if (sorted.Size() == 1)
        headChild = sorted[0];
}

// ============================================================================
// BuildMapping — topological approach
// ============================================================================

Vector<BoneMapping> SkeletonRemapper::BuildMapping(
    Model* sourceModel, const Vector<BoneBounds>& sourceBounds,
    Model* targetModel, const Vector<BoneBounds>& targetBounds)
{
    Vector<BoneMapping> result;

    if (!sourceModel || !targetModel)
        return result;

    const Skeleton& srcSkel = sourceModel->GetSkeleton();
    const Skeleton& tgtSkel = targetModel->GetSkeleton();
    const Vector<Bone>& srcBones = srcSkel.GetBones();
    const Vector<Bone>& tgtBones = tgtSkel.GetBones();

    result.Resize(srcBones.Size());
    for (unsigned i = 0; i < srcBones.Size(); ++i)
    {
        result[i].sourceBone = (int)i;
        result[i].targetBone = -1;
        result[i].confidence = 0.0f;
    }

    Vector<bool> tgtClaimed(tgtBones.Size(), false);

    // Precompute bind-pose world positions
    Vector<Vector3> srcWorldPos(srcBones.Size());
    Vector<Vector3> tgtWorldPos(tgtBones.Size());
    for (unsigned i = 0; i < srcBones.Size(); ++i)
        srcWorldPos[i] = BoneWorldPos(srcBones, (int)i);
    for (unsigned i = 0; i < tgtBones.Size(); ++i)
        tgtWorldPos[i] = BoneWorldPos(tgtBones, (int)i);

    // ── Step 1: Detect branch point landmarks ──
    Vector<BranchPoint> srcBP = BranchPointDetector::Detect(srcSkel);
    Vector<BranchPoint> tgtBP = BranchPointDetector::Detect(tgtSkel);

    int srcHip = -1, srcShoulder = -1, srcHead = -1;
    int tgtHip = -1, tgtShoulder = -1, tgtHead = -1;

    for (unsigned i = 0; i < srcBP.Size(); ++i)
    {
        if (srcBP[i].type == BRANCH_HIP) srcHip = srcBP[i].boneIndex;
        if (srcBP[i].type == BRANCH_SHOULDER) srcShoulder = srcBP[i].boneIndex;
        if (srcBP[i].type == BRANCH_HEAD) srcHead = srcBP[i].boneIndex;
    }
    for (unsigned i = 0; i < tgtBP.Size(); ++i)
    {
        if (tgtBP[i].type == BRANCH_HIP) tgtHip = tgtBP[i].boneIndex;
        if (tgtBP[i].type == BRANCH_SHOULDER) tgtShoulder = tgtBP[i].boneIndex;
        if (tgtBP[i].type == BRANCH_HEAD) tgtHead = tgtBP[i].boneIndex;
    }

    if (srcHip < 0 || tgtHip < 0)
    {
        URHO3D_LOGWARNING("SkeletonRemapper: Hip not found on one or both skeletons");
        return result;
    }

    // ── Step 2: Match landmark bones ──
    result[srcHip].targetBone = tgtHip;
    result[srcHip].confidence = 0.95f;
    tgtClaimed[tgtHip] = true;

    if (srcShoulder >= 0 && tgtShoulder >= 0)
    {
        result[srcShoulder].targetBone = tgtShoulder;
        result[srcShoulder].confidence = 0.95f;
        tgtClaimed[tgtShoulder] = true;
    }

    if (srcHead >= 0 && tgtHead >= 0)
    {
        result[srcHead].targetBone = tgtHead;
        result[srcHead].confidence = 0.90f;
        tgtClaimed[tgtHead] = true;
    }

    // ── Step 3: Classify and match hip children ──
    Vector<int> srcHipChildren, tgtHipChildren;
    GetChildren(srcBones, srcHip, srcHipChildren);
    GetChildren(tgtBones, tgtHip, tgtHipChildren);

    int srcSpineChild, srcLeftLeg, srcRightLeg;
    int tgtSpineChild, tgtLeftLeg, tgtRightLeg;
    ClassifyHipChildren(srcBones, srcWorldPos, srcHipChildren, srcSpineChild, srcLeftLeg, srcRightLeg);
    ClassifyHipChildren(tgtBones, tgtWorldPos, tgtHipChildren, tgtSpineChild, tgtLeftLeg, tgtRightLeg);

    // Spine chain: hip child → shoulder
    if (srcSpineChild >= 0 && tgtSpineChild >= 0 && srcShoulder >= 0 && tgtShoulder >= 0)
    {
        Vector<int> srcSpine, tgtSpine;
        WalkChain(srcBones, srcSpineChild, srcSpine, srcShoulder);
        WalkChain(tgtBones, tgtSpineChild, tgtSpine, tgtShoulder);
        MatchChains(srcSpine, tgtSpine, result, tgtClaimed);
        URHO3D_LOGINFOF("SkeletonRemapper: matched spine (%u src, %u tgt)", srcSpine.Size(), tgtSpine.Size());
    }

    // Left leg
    if (srcLeftLeg >= 0 && tgtLeftLeg >= 0)
    {
        Vector<int> srcChain, tgtChain;
        WalkChain(srcBones, srcLeftLeg, srcChain);
        WalkChain(tgtBones, tgtLeftLeg, tgtChain);
        MatchChains(srcChain, tgtChain, result, tgtClaimed);
        URHO3D_LOGINFOF("SkeletonRemapper: matched left_leg (%u src, %u tgt)", srcChain.Size(), tgtChain.Size());
    }

    // Right leg
    if (srcRightLeg >= 0 && tgtRightLeg >= 0)
    {
        Vector<int> srcChain, tgtChain;
        WalkChain(srcBones, srcRightLeg, srcChain);
        WalkChain(tgtBones, tgtRightLeg, tgtChain);
        MatchChains(srcChain, tgtChain, result, tgtClaimed);
        URHO3D_LOGINFOF("SkeletonRemapper: matched right_leg (%u src, %u tgt)", srcChain.Size(), tgtChain.Size());
    }

    // ── Step 4: Classify and match shoulder children ──
    if (srcShoulder >= 0 && tgtShoulder >= 0)
    {
        Vector<int> srcShoulderChildren, tgtShoulderChildren;
        GetChildren(srcBones, srcShoulder, srcShoulderChildren);
        GetChildren(tgtBones, tgtShoulder, tgtShoulderChildren);

        int srcHeadChild, srcLeftArm, srcRightArm;
        int tgtHeadChild, tgtLeftArm, tgtRightArm;
        ClassifyShoulderChildren(srcBones, srcWorldPos, srcShoulderChildren, srcHeadChild, srcLeftArm, srcRightArm);
        ClassifyShoulderChildren(tgtBones, tgtWorldPos, tgtShoulderChildren, tgtHeadChild, tgtLeftArm, tgtRightArm);

        // Left arm
        if (srcLeftArm >= 0 && tgtLeftArm >= 0)
        {
            Vector<int> srcChain, tgtChain;
            WalkChain(srcBones, srcLeftArm, srcChain);
            WalkChain(tgtBones, tgtLeftArm, tgtChain);
            MatchChains(srcChain, tgtChain, result, tgtClaimed);
            URHO3D_LOGINFOF("SkeletonRemapper: matched left_arm (%u src, %u tgt)", srcChain.Size(), tgtChain.Size());
        }

        // Right arm
        if (srcRightArm >= 0 && tgtRightArm >= 0)
        {
            Vector<int> srcChain, tgtChain;
            WalkChain(srcBones, srcRightArm, srcChain);
            WalkChain(tgtBones, tgtRightArm, tgtChain);
            MatchChains(srcChain, tgtChain, result, tgtClaimed);
            URHO3D_LOGINFOF("SkeletonRemapper: matched right_arm (%u src, %u tgt)", srcChain.Size(), tgtChain.Size());
        }

        // Head chain
        if (srcHeadChild >= 0 && tgtHeadChild >= 0)
        {
            Vector<int> srcChain, tgtChain;
            WalkChain(srcBones, srcHeadChild, srcChain);
            WalkChain(tgtBones, tgtHeadChild, tgtChain);
            MatchChains(srcChain, tgtChain, result, tgtClaimed);
            URHO3D_LOGINFOF("SkeletonRemapper: matched head (%u src, %u tgt)", srcChain.Size(), tgtChain.Size());
        }
    }

    // Count matches
    unsigned matched = 0;
    for (unsigned i = 0; i < result.Size(); ++i)
        if (result[i].targetBone >= 0) ++matched;

    URHO3D_LOGINFOF("SkeletonRemapper: %u/%u source bones matched (%u target bones)",
        matched, srcBones.Size(), tgtBones.Size());

    return result;
}

Vector<BoneMapping> SkeletonRemapper::BuildMapping(Model* sourceModel, Model* targetModel)
{
    if (!sourceModel || !targetModel)
        return Vector<BoneMapping>();

    Vector<BoneBounds> srcBounds = BoneCollisionGenerator::Generate(sourceModel);
    Vector<BoneBounds> tgtBounds = BoneCollisionGenerator::Generate(targetModel);

    return BuildMapping(sourceModel, srcBounds, targetModel, tgtBounds);
}

void SkeletonRemapper::LogMapping(Model* sourceModel, Model* targetModel,
                                   const Vector<BoneMapping>& mapping)
{
    if (!sourceModel || !targetModel)
        return;

    const Vector<Bone>& srcBones = sourceModel->GetSkeleton().GetBones();
    const Vector<Bone>& tgtBones = targetModel->GetSkeleton().GetBones();

    URHO3D_LOGINFO("=== Skeleton Remapping ===");
    for (unsigned i = 0; i < mapping.Size(); ++i)
    {
        const BoneMapping& m = mapping[i];
        if (m.targetBone >= 0 && m.targetBone < (int)tgtBones.Size())
        {
            URHO3D_LOGINFOF("  [%d] '%s' -> [%d] '%s' (%.0f%%)",
                m.sourceBone, srcBones[m.sourceBone].name_.CString(),
                m.targetBone, tgtBones[m.targetBone].name_.CString(),
                m.confidence * 100.0f);
        }
        else
        {
            URHO3D_LOGINFOF("  [%d] '%s' -> UNMATCHED",
                m.sourceBone, srcBones[m.sourceBone].name_.CString());
        }
    }
    URHO3D_LOGINFO("=========================");
}

} // namespace Urho3D
