#pragma once

#include "Sample.h"

/// Bullet SDK stacking benchmark — towers of boxes tested for solver stability.
class BS_Stacking : public Sample
{
    URHO3D_OBJECT(BS_Stacking, Sample);

public:
    explicit BS_Stacking(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
