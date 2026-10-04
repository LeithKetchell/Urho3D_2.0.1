#pragma once

#include "Sample.h"
#include <Urho3D/Container/Vector.h>

/// Radial gravity — objects fall toward a planet center, not down.
class BS_PlanetGravity : public Sample
{
    URHO3D_OBJECT(BS_PlanetGravity, Sample);

public:
    explicit BS_PlanetGravity(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    Node* planetNode_{};
    Vector3 planetCenter_;
    float planetRadius_{5.0f};
    float gravityStrength_{15.0f};
    Vector<Node*> orbiters_;
};
