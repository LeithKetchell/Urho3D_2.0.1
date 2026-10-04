// Port of bullet3/examples/RenderingExamples — NOT PORTABLE — Bullet3 custom renderer demos, not physics
#pragma once

#include "Sample.h"

class BS_RenderingExamples : public Sample
{
    URHO3D_OBJECT(BS_RenderingExamples, Sample);

public:
    explicit BS_RenderingExamples(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
