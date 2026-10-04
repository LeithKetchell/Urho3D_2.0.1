// AnimationRetargeter — retarget animations between skeletons via shape mapping.

#include "../Precompiled.h"

#include "AnimationRetargeter.h"
#include "Animation.h"
#include "Model.h"
#include "Skeleton.h"
#include "../IO/Log.h"

namespace Urho3D
{

SharedPtr<Animation> AnimationRetargeter::Retarget(
    Animation* sourceAnim,
    Model* sourceModel,
    Model* targetModel,
    const Vector<BoneMapping>& mapping)
{
    if (!sourceAnim || !sourceModel || !targetModel)
        return SharedPtr<Animation>();

    const Skeleton& srcSkel = sourceModel->GetSkeleton();
    const Skeleton& tgtSkel = targetModel->GetSkeleton();
    const Vector<Bone>& srcBones = srcSkel.GetBones();
    const Vector<Bone>& tgtBones = tgtSkel.GetBones();

    // Build source bone name hash → mapping index lookup
    HashMap<StringHash, int> srcNameToMapping;
    for (unsigned i = 0; i < mapping.Size(); ++i)
    {
        int srcIdx = mapping[i].sourceBone;
        if (srcIdx >= 0 && srcIdx < (int)srcBones.Size())
            srcNameToMapping[srcBones[srcIdx].nameHash_] = (int)i;
    }

    SharedPtr<Animation> result(new Animation(sourceAnim->GetContext()));
    result->SetAnimationName(sourceAnim->GetAnimationName() + "_retarget");
    result->SetLength(sourceAnim->GetLength());

    const HashMap<StringHash, AnimationTrack>& srcTracks = sourceAnim->GetTracks();
    unsigned remapped = 0;
    unsigned skipped = 0;

    for (auto it = srcTracks.Begin(); it != srcTracks.End(); ++it)
    {
        const AnimationTrack& srcTrack = it->second_;

        // Find this track's source bone in the mapping
        auto mapIt = srcNameToMapping.Find(srcTrack.nameHash_);
        if (mapIt == srcNameToMapping.End())
        {
            ++skipped;
            continue;
        }

        const BoneMapping& bm = mapping[mapIt->second_];
        if (bm.targetBone < 0 || bm.targetBone >= (int)tgtBones.Size())
        {
            ++skipped;
            continue;
        }

        int srcBoneIdx = bm.sourceBone;
        int tgtBoneIdx = bm.targetBone;

        // Compute bind pose rotation difference.
        // Source animation keyframes are in source bone-local space relative to
        // the source bind pose. To retarget:
        //   targetLocalRot = tgtBindInverse * srcBind * srcKeyRot
        // This removes the source bind pose and applies the target bind pose.
        Quaternion srcBindRot = srcBones[srcBoneIdx].initialRotation_;
        Quaternion tgtBindRot = tgtBones[tgtBoneIdx].initialRotation_;
        Quaternion tgtBindInv = tgtBindRot.Inverse();
        Quaternion bindCorrection = tgtBindInv * srcBindRot;

        // Position scaling: ratio of bind pose bone lengths.
        // Use initialPosition magnitude as a proxy for bone length.
        float srcLen = srcBones[srcBoneIdx].initialPosition_.Length();
        float tgtLen = tgtBones[tgtBoneIdx].initialPosition_.Length();
        float posScale = (srcLen > 0.001f) ? tgtLen / srcLen : 1.0f;

        // Create target track with the target bone name
        AnimationTrack* tgtTrack = result->CreateTrack(tgtBones[tgtBoneIdx].name_);
        tgtTrack->channelMask_ = srcTrack.channelMask_;

        // Remap each keyframe
        for (unsigned k = 0; k < srcTrack.keyFrames_.Size(); ++k)
        {
            AnimationKeyFrame kf = srcTrack.keyFrames_[k];

            // Adjust rotation for bind pose difference
            if (!!(srcTrack.channelMask_ & AnimationChannels::Rotation))
                kf.rotation_ = bindCorrection * kf.rotation_;

            // Scale position by bone length ratio
            if (!!(srcTrack.channelMask_ & AnimationChannels::Position))
                kf.position_ = kf.position_ * posScale;

            // Scale passes through unchanged — same proportions assumed

            tgtTrack->AddKeyFrame(kf);
        }

        ++remapped;
    }

    // Copy trigger points and text keys unchanged
    for (unsigned i = 0; i < sourceAnim->GetNumTriggers(); ++i)
    {
        const AnimationTriggerPoint* tp = const_cast<Animation*>(sourceAnim)->GetTrigger(i);
        if (tp)
            result->AddTrigger(*tp);
    }
    for (unsigned i = 0; i < sourceAnim->GetNumTextKeys(); ++i)
    {
        const AnimationTextKey* tk = const_cast<Animation*>(sourceAnim)->GetTextKey(i);
        if (tk)
            result->AddTextKey(tk->time_, false, tk->name_, tk->data_);
    }

    URHO3D_LOGINFOF("AnimationRetargeter: %u tracks remapped, %u skipped (no mapping), "
        "%u triggers + %u text keys preserved",
        remapped, skipped, sourceAnim->GetNumTriggers(), sourceAnim->GetNumTextKeys());

    return result;
}

SharedPtr<Animation> AnimationRetargeter::Retarget(
    Animation* sourceAnim,
    Model* sourceModel,
    Model* targetModel)
{
    if (!sourceAnim || !sourceModel || !targetModel)
        return SharedPtr<Animation>();

    Vector<BoneMapping> mapping = SkeletonRemapper::BuildMapping(sourceModel, targetModel);
    return Retarget(sourceAnim, sourceModel, targetModel, mapping);
}

} // namespace Urho3D
