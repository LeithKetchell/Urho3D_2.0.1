#pragma once

#include "Sample.h"

/// Port of bullet3/examples/ExtendedTutorials/Chain.cpp — vertical chain with point-to-point constraints.
class BS_Chain : public Sample
{
    URHO3D_OBJECT(BS_Chain, Sample);

public:
    explicit BS_Chain(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
