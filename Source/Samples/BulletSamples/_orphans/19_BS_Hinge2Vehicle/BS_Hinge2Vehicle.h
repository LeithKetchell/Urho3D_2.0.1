#pragma once
#include "Sample.h"

/// Port of Bullet SDK Vehicles/Hinge2Vehicle — chassis + 4 wheels via hinge2 constraints.
class BS_Hinge2Vehicle : public Sample
{
    URHO3D_OBJECT(BS_Hinge2Vehicle, Sample);
public:
    explicit BS_Hinge2Vehicle(Context* context) : Sample(context) {}
    void Start() override;
private:
    void CreateScene();
    void CreateVehicle();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    WeakPtr<Node> vehicleNode_;
    WeakPtr<Node> wheels_[4];
    bool drawDebug_{false};
    float steering_{0.0f};
    float engineForce_{0.0f};
};
