// KarenTest — headless telemetry emitter.
// Generates fake zones, plots, and messages for testing Karen viewer.

#pragma once

#include <Urho3D/Engine/Application.h>

using namespace Urho3D;

class KarenTest : public Application
{
    URHO3D_OBJECT(KarenTest, Application);

public:
    explicit KarenTest(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    void HandleUpdate(StringHash eventType, VariantMap& eventData);

    float plotValue_{};
    float plotDirection_{1.0f};
    unsigned frameCount_{};
};
