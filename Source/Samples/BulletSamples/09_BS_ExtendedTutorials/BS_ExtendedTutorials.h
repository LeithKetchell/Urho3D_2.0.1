// Port of bullet3/examples/ExtendedTutorials/NewtonsCradle.cpp
// Newton's Cradle — 5 pendulum balls demonstrating conservation of momentum
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_ExtendedTutorials : public Sample
{
    URHO3D_OBJECT(BS_ExtendedTutorials, Sample);

public:
    explicit BS_ExtendedTutorials(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    void CreateNewtonsCradle(const Vector3& position, int numBalls, float ballRadius, float stringLength);

    SharedPtr<ProfilerUI> profilerUI_;
};
