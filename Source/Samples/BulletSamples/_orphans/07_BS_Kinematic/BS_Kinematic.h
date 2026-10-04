#pragma once

#include "Sample.h"

/// Bullet SDK KinematicRigidBodyExample — rotating platform with falling boxes.
class BS_Kinematic : public Sample
{
    URHO3D_OBJECT(BS_Kinematic, Sample);

public:
    explicit BS_Kinematic(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    WeakPtr<Node> groundNode_;
};
