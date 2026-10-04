#pragma once

#include "Sample.h"

/// Bullet SDK Planar2D — 2D physics with constrained linear/angular factors.
class BS_Planar2D : public Sample
{
    URHO3D_OBJECT(BS_Planar2D, Sample);

public:
    explicit BS_Planar2D(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    bool drawDebug_{false};
};
