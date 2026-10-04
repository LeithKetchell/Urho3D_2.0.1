#pragma once
#include "Sample.h"

/// Port of Bullet SDK TwoJoint — two-link chain with sinusoidal torque.
class BS_TwoJoint : public Sample
{
    URHO3D_OBJECT(BS_TwoJoint, Sample);
public:
    explicit BS_TwoJoint(Context* context) : Sample(context) {}
    void Start() override;
private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    WeakPtr<Node> link1_;
    WeakPtr<Node> link2_;
    float simTime_{0.0f};
};
