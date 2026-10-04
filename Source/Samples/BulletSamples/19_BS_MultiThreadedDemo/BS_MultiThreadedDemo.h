// Port of bullet3/examples/MultiThreadedDemo — NOT PORTABLE — Requires Bullet3 threading infrastructure
#pragma once

#include "Sample.h"

class BS_MultiThreadedDemo : public Sample
{
    URHO3D_OBJECT(BS_MultiThreadedDemo, Sample);

public:
    explicit BS_MultiThreadedDemo(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
