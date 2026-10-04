#pragma once

#include "Sample.h"

/// Bullet SDK Pendulum — chain of point-to-point constrained spheres.
class BS_Pendulum : public Sample
{
    URHO3D_OBJECT(BS_Pendulum, Sample);

public:
    explicit BS_Pendulum(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
