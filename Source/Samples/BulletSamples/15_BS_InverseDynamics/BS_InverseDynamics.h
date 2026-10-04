// Port of bullet3/examples/InverseDynamics — NOT PORTABLE — Requires btMultiBody (featherstone)
#pragma once

#include "Sample.h"

class BS_InverseDynamics : public Sample
{
    URHO3D_OBJECT(BS_InverseDynamics, Sample);

public:
    explicit BS_InverseDynamics(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
