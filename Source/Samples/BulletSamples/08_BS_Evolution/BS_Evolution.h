// Port of bullet3/examples/Evolution — NOT PORTABLE — Requires neural networks + evolutionary algorithms
#pragma once

#include "Sample.h"

class BS_Evolution : public Sample
{
    URHO3D_OBJECT(BS_Evolution, Sample);

public:
    explicit BS_Evolution(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
