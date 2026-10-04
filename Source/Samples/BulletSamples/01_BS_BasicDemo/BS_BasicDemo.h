// Port of bullet3/examples/BasicDemo/BasicExample.cpp
// Original by Erwin Coumans — zlib license

#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

/// 125 dynamic boxes falling in a 5x5x5 grid onto a static ground plane.
class BS_BasicDemo : public Sample
{
    URHO3D_OBJECT(BS_BasicDemo, Sample);

public:
    explicit BS_BasicDemo(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
};
