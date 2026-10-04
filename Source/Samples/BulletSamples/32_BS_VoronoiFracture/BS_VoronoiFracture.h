// Simplified port of bullet3/examples/VoronoiFracture/VoronoiFractureDemo.cpp
// Pre-shattered object: random convex pieces connected by breakable constraints
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_VoronoiFracture : public Sample
{
    URHO3D_OBJECT(BS_VoronoiFracture, Sample);

public:
    explicit BS_VoronoiFracture(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void CreateShatteredObject(const Vector3& position, float size, int numPieces);
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
};
