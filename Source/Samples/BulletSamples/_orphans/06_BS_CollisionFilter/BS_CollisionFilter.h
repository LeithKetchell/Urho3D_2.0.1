#pragma once

#include "Sample.h"

/// Collision filtering — groups that selectively collide or pass through.
class BS_CollisionFilter : public Sample
{
    URHO3D_OBJECT(BS_CollisionFilter, Sample);

public:
    explicit BS_CollisionFilter(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
