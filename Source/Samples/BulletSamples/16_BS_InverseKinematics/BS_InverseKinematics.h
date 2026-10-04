// Port of bullet3/examples/InverseKinematics — NOT PORTABLE — Requires custom IK solver on btMultiBody
#pragma once

#include "Sample.h"

class BS_InverseKinematics : public Sample
{
    URHO3D_OBJECT(BS_InverseKinematics, Sample);

public:
    explicit BS_InverseKinematics(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
