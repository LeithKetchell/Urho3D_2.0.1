// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>
#include <Urho3D/Graphics/BoneCollisionGenerator.h>

namespace Urho3D
{
class AnimationController;
class Node;
class Scene;
class AnimatedModel;
}

/// Bone collision box demo.
/// Generates per-bone bounding boxes from vertex weights and draws them.
class InverseKinematics : public Sample
{
    URHO3D_OBJECT(InverseKinematics, Sample);

public:
    explicit InverseKinematics(Context* context);

    void Start() override;

protected:
    SharedPtr<Urho3D::AnimationController> animCtrl_;
    SharedPtr<Urho3D::Node> characterNode_;
    SharedPtr<Urho3D::Node> floorNode_;
    float floorPitch_{};
    float floorRoll_{};
    bool drawDebug_{true};

private:
    void CreateScene();
    void CreateInstructions();
    void SetupViewport();
    void UpdateCameraAndFloor(float timeStep);
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<Node> cameraRotateNode_;
    SharedPtr<ProfilerUI> profilerUI_;

    /// Per-bone bounding boxes from BoneCollisionGenerator.
    Vector<BoneBounds> boneBoundsData_;
    /// Ragdoll activated flag.
    bool ragdollActive_{false};
    /// Activate ragdoll — stop animation, add physics bodies + constraints.
    void ActivateRagdoll();
};
