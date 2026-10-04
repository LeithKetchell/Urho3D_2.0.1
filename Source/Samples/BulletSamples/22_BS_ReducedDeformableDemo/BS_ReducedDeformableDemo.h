// Port of bullet3/examples/ReducedDeformableDemo — NOT PORTABLE — Requires btReducedDeformableBody
#pragma once

#include "Sample.h"

class BS_ReducedDeformableDemo : public Sample
{
    URHO3D_OBJECT(BS_ReducedDeformableDemo, Sample);

public:
    explicit BS_ReducedDeformableDemo(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
