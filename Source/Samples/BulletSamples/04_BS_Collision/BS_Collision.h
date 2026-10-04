// Port of bullet3/examples/Collision/CollisionTutorialBullet2.cpp
// Collision detection visualization — contact points between sphere compounds and ground plane
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_Collision : public Sample
{
    URHO3D_OBJECT(BS_Collision, Sample);

public:
    explicit BS_Collision(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
};
