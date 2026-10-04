#pragma once

#include "Sample.h"
#include <Urho3D/Container/Vector.h>

/// Bullet SDK MotorDemo — 6-legged walking spider with sinusoidal motor control.
class BS_MotorDemo : public Sample
{
    URHO3D_OBJECT(BS_MotorDemo, Sample);

public:
    explicit BS_MotorDemo(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    void SpawnTestRig(const Vector3& offset, bool fixed);

    struct TestRig {
        Node* body{};
        Node* legs[12]{};       // 6 thighs + 6 shins
        Node* constraints[12]{}; // nodes holding hinge constraints
    };

    Vector<TestRig> rigs_;
    float time_{};
    float cyclePeriod_{2000.0f};   // ms
    float muscleStrength_{0.5f};
};
