// BranchPointDetector — identify anatomical landmarks from skeleton topology.

#include "../Precompiled.h"

#include "BranchPointDetector.h"
#include "Skeleton.h"
#include "../IO/Log.h"

namespace Urho3D
{

Vector<BranchPoint> BranchPointDetector::Detect(const Skeleton& skeleton)
{
    Vector<BranchPoint> result;
    const Vector<Bone>& bones = skeleton.GetBones();
    unsigned numBones = bones.Size();

    if (numBones == 0)
        return result;

    // Count children and compute depth for each bone
    Vector<int> childCount(numBones, 0);
    Vector<int> depth(numBones, 0);

    for (unsigned i = 0; i < numBones; ++i)
    {
        int parent = bones[i].parentIndex_;
        if (parent >= 0 && parent < (int)numBones && parent != (int)i)
            childCount[parent]++;
    }

    // Compute depth from root via parent chain
    for (unsigned i = 0; i < numBones; ++i)
    {
        int d = 0;
        int current = (int)i;
        while (current >= 0 && current < (int)numBones)
        {
            int parent = bones[current].parentIndex_;
            if (parent < 0 || parent >= (int)numBones || parent == current)
                break;
            current = parent;
            ++d;
            if (d > (int)numBones) break;  // cycle guard
        }
        depth[i] = d;
    }

    // Find all branch points (3+ children) — these are the major joints
    Vector<unsigned> branchBones;
    for (unsigned i = 0; i < numBones; ++i)
    {
        if (childCount[i] >= 3)
            branchBones.Push(i);
    }

    // Sort branch points by depth (shallowest first)
    for (unsigned i = 0; i < branchBones.Size(); ++i)
    {
        for (unsigned j = i + 1; j < branchBones.Size(); ++j)
        {
            if (depth[branchBones[j]] < depth[branchBones[i]])
                Swap(branchBones[i], branchBones[j]);
        }
    }

    // Classify: first major branch = hip, second = shoulder
    int hipIdx = -1;
    int shoulderIdx = -1;

    if (branchBones.Size() >= 1)
        hipIdx = branchBones[0];
    if (branchBones.Size() >= 2)
        shoulderIdx = branchBones[1];

    if (hipIdx >= 0)
    {
        BranchPoint bp;
        bp.boneIndex = hipIdx;
        bp.type = BRANCH_HIP;
        bp.childCount = childCount[hipIdx];
        bp.depth = depth[hipIdx];
        result.Push(bp);
        URHO3D_LOGINFOF("[BranchPoint] HIP: Bone[%d] '%s' depth=%d children=%d",
            hipIdx, bones[hipIdx].name_.CString(), depth[hipIdx], childCount[hipIdx]);
    }

    if (shoulderIdx >= 0)
    {
        BranchPoint bp;
        bp.boneIndex = shoulderIdx;
        bp.type = BRANCH_SHOULDER;
        bp.childCount = childCount[shoulderIdx];
        bp.depth = depth[shoulderIdx];
        result.Push(bp);
        URHO3D_LOGINFOF("[BranchPoint] SHOULDER: Bone[%d] '%s' depth=%d children=%d",
            shoulderIdx, bones[shoulderIdx].name_.CString(), depth[shoulderIdx], childCount[shoulderIdx]);

        // Head: walk up from shoulder along the chain with fewest children.
        // The head is the first bone above the shoulder branch that has 0-1 children
        // and is not part of an arm chain (arms are the branches, head is the continuation).
        // Strategy: from shoulder, follow the child that leads to the shallowest subtree
        // with 0-1 children — that's the neck/head chain.
        int headCandidate = -1;
        for (unsigned i = 0; i < numBones; ++i)
        {
            if (bones[i].parentIndex_ == shoulderIdx && childCount[i] <= 1)
            {
                // Follow this chain to find the terminal
                int current = (int)i;
                while (current >= 0 && current < (int)numBones && childCount[current] == 1)
                {
                    // Find the single child
                    for (unsigned c = 0; c < numBones; ++c)
                    {
                        if (bones[c].parentIndex_ == current)
                        {
                            current = (int)c;
                            break;
                        }
                    }
                }
                // current is now a terminal or a bone with 0 or 2+ children
                // Walk back one — the last bone with 0-1 children is likely the head
                headCandidate = current;
                break;
            }
        }

        // If no single-child path from shoulder, look for the child with the
        // fewest descendants — that's the head direction, not an arm
        if (headCandidate < 0)
        {
            int bestChild = -1;
            int fewestDesc = 99999;
            for (unsigned i = 0; i < numBones; ++i)
            {
                if (bones[i].parentIndex_ != shoulderIdx)
                    continue;
                // Count descendants
                int desc = 0;
                for (unsigned j = 0; j < numBones; ++j)
                {
                    int cur = (int)j;
                    while (cur >= 0 && cur < (int)numBones)
                    {
                        if (cur == (int)i) { desc++; break; }
                        int p = bones[cur].parentIndex_;
                        if (p < 0 || p >= (int)numBones || p == cur) break;
                        cur = p;
                    }
                }
                if (desc < fewestDesc)
                {
                    fewestDesc = desc;
                    bestChild = (int)i;
                }
            }
            if (bestChild >= 0)
            {
                // Walk to terminal
                int current = bestChild;
                while (current >= 0 && current < (int)numBones && childCount[current] == 1)
                {
                    for (unsigned c = 0; c < numBones; ++c)
                    {
                        if (bones[c].parentIndex_ == current)
                        {
                            current = (int)c;
                            break;
                        }
                    }
                }
                headCandidate = current;
            }
        }

        if (headCandidate >= 0)
        {
            BranchPoint bp;
            bp.boneIndex = headCandidate;
            bp.type = BRANCH_HEAD;
            bp.childCount = childCount[headCandidate];
            bp.depth = depth[headCandidate];
            result.Push(bp);
            URHO3D_LOGINFOF("[BranchPoint] HEAD: Bone[%d] '%s' depth=%d children=%d",
                headCandidate, bones[headCandidate].name_.CString(),
                depth[headCandidate], childCount[headCandidate]);
        }
    }

    return result;
}

} // namespace Urho3D
