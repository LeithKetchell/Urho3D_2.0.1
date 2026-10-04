// Port of bullet3/examples/DeformableDemo — NOT PORTABLE — Requires btDeformableMultiBodyDynamicsWorld
#pragma once

#include "Sample.h"

class BS_DeformableDemo : public Sample
{
    URHO3D_OBJECT(BS_DeformableDemo, Sample);

public:
    explicit BS_DeformableDemo(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
