// Port of bullet3/examples/RollingFrictionDemo/RollingFrictionDemo.cpp
// Original by Erwin Coumans — zlib license

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

/// 125 mixed shapes on an inclined plane with rolling and spinning friction.
/// Throw boxes with left mouse button.
class BS_RollingFriction : public Sample
{
    URHO3D_OBJECT(BS_RollingFriction, Sample);

public:
    explicit BS_RollingFriction(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
};
