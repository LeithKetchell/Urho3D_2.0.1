// Port of bullet3/examples/RigidBody (KinematicRigidBodyExample + RigidBodySoftContact)
// Kinematic body on scripted path pushing dynamic objects, plus soft contact demo
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_RigidBody : public Sample
{
    URHO3D_OBJECT(BS_RigidBody, Sample);

public:
    explicit BS_RigidBody(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    Node* kinematicNode_{};
    float kinematicTime_{0.0f};
    SharedPtr<ProfilerUI> profilerUI_;
};
