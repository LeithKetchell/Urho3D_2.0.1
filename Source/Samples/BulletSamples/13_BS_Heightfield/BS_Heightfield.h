#pragma once
#include "Sample.h"

/// Bullet SDK HeightfieldExample — terrain collision with falling objects.
class BS_Heightfield : public Sample
{
    URHO3D_OBJECT(BS_Heightfield, Sample);
public:
    explicit BS_Heightfield(Context* context) : Sample(context) {}
    void Start() override;
private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
