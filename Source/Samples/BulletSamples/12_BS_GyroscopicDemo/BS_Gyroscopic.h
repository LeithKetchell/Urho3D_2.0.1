#pragma once

#include "Sample.h"

/// Bullet SDK GyroscopicDemo — 4 gyroscopic modes in zero gravity.
class BS_Gyroscopic : public Sample
{
    URHO3D_OBJECT(BS_Gyroscopic, Sample);

public:
    explicit BS_Gyroscopic(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
