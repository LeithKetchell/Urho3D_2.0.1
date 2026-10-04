// Port of bullet3/examples/MultiBodyBaseline — NOT PORTABLE — Requires btMultiBody baseline comparison
#pragma once

#include "Sample.h"

class BS_MultiBodyBaseline : public Sample
{
    URHO3D_OBJECT(BS_MultiBodyBaseline, Sample);

public:
    explicit BS_MultiBodyBaseline(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
