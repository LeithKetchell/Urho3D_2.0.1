// Port of bullet3/examples/Vehicles/Hinge2Vehicle.cpp
// 4-wheeled vehicle using 6DOF Spring2 constraints (steering + drive + suspension)
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>
#include <Urho3D/Physics/Constraint.h>

class BS_Vehicles : public Sample
{
    URHO3D_OBJECT(BS_Vehicles, Sample);

public:
    explicit BS_Vehicles(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void CreateVehicle(const Vector3& position);
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    Node* chassisNode_{};
    Node* wheels_[4]{};
    Constraint* wheelConstraints_[4]{};
    float steeringAngle_{0.0f};
    SharedPtr<ProfilerUI> profilerUI_;
};
