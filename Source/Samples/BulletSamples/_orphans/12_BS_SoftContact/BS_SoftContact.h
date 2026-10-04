#pragma once

#include "Sample.h"

/// Bullet SDK RigidBodySoftContact — compliant ground with stiffness/damping.
class BS_SoftContact : public Sample
{
    URHO3D_OBJECT(BS_SoftContact, Sample);

public:
    explicit BS_SoftContact(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
};
