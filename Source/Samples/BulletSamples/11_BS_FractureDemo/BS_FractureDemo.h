// Simplified port of bullet3/examples/FractureDemo/FractureDemo.cpp
// Original uses btFractureBody/btFractureDynamicsWorld (not exposed by Urho3D).
// This port simulates fracture by breaking constraint-connected box groups on impact.
// Color transfers from impacting objects to fragments on collision.
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_FractureDemo : public Sample
{
    URHO3D_OBJECT(BS_FractureDemo, Sample);

public:
    explicit BS_FractureDemo(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void CreateWall(const Vector3& position, int width, int height);
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePhysicsCollision(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
    bool started_{false};
};
