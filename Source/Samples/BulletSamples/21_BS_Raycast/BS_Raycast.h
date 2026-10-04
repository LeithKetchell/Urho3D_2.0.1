#pragma once

#include "Sample.h"

/// Bullet SDK RaytestDemo — raycasting against 6 shapes.
class BS_Raycast : public Sample
{
    URHO3D_OBJECT(BS_Raycast, Sample);

public:
    explicit BS_Raycast(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
    void CastRays();

    bool drawDebug_{true};
    float animPhase_{0.0f};
};
