#pragma once

#include "Sample.h"

/// Bullet SDK AllConstraintDemo — all 14 constraint setups.
class BS_Constraints : public Sample
{
    URHO3D_OBJECT(BS_Constraints, Sample);

public:
    explicit BS_Constraints(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    /// Helper: create a box node with rigid body.
    Node* CreateBox(const Vector3& position, float mass, const Vector3& size = Vector3::ONE);
    /// Helper: create a cylinder node with rigid body.
    Node* CreateCylinder(const Vector3& position, float mass, float radius = 0.5f, float height = 1.0f);
};
