#pragma once

#include "Sample.h"

class btRaycastVehicle;
class btVehicleRaycaster;
class btHingeConstraint;
class btSliderConstraint;

/// Bullet SDK ForkLiftDemo — raycast vehicle with lift and fork mechanism.
class BS_ForkLift : public Sample
{
    URHO3D_OBJECT(BS_ForkLift, Sample);

public:
    explicit BS_ForkLift(Context* context) : Sample(context) {}
    ~BS_ForkLift() override;

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    void ResetVehicle();

    // Vehicle state (original global variables)
    float engineForce_{0.0f};
    float breakingForce_{10.0f};
    float vehicleSteering_{0.0f};

    // Raw Bullet vehicle (not wrapped by Urho3D)
    btRaycastVehicle* vehicle_{nullptr};
    btVehicleRaycaster* vehicleRayCaster_{nullptr};

    // Urho3D nodes for the mechanism
    WeakPtr<Node> chassisNode_;
    WeakPtr<Node> liftNode_;
    WeakPtr<Node> forkNode_;
    WeakPtr<Node> loadNode_;

    // Raw Bullet constraints for lift/fork (not wrapped by Urho3D)
    btHingeConstraint* liftHinge_{nullptr};
    btSliderConstraint* forkSlider_{nullptr};

    bool drawDebug_{true};
};
