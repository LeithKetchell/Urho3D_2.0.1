// Port of bullet3/examples/RoboticsLearning — NOT PORTABLE — Requires pybullet + ML infrastructure
#pragma once

#include "Sample.h"

class BS_RoboticsLearning : public Sample
{
    URHO3D_OBJECT(BS_RoboticsLearning, Sample);

public:
    explicit BS_RoboticsLearning(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
