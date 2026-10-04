#pragma once

#include "Sample.h"

class BS_HelloPhysics : public Sample
{
    URHO3D_OBJECT(BS_HelloPhysics, Sample);

public:
    explicit BS_HelloPhysics(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
