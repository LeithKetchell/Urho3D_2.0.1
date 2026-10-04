// Port of bullet3/examples/BulletRobotics/BoxStack.cpp
// 8 cubes stacked with exponentially increasing mass (1, 4, 16, 64...)
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_BulletRobotics : public Sample
{
    URHO3D_OBJECT(BS_BulletRobotics, Sample);

public:
    explicit BS_BulletRobotics(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
};
