// Port of bullet3/examples/RobotSimulator — NOT PORTABLE — Requires pybullet SharedMemory infrastructure
#pragma once

#include "Sample.h"

class BS_RobotSimulator : public Sample
{
    URHO3D_OBJECT(BS_RobotSimulator, Sample);

public:
    explicit BS_RobotSimulator(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
