// MilesApp.h
#pragma once

#include <Urho3D/Engine/Application.h>

namespace Urho3D {

class MilesApp : public Application {
    URHO3D_OBJECT(MilesApp, Application);

public:
    MilesApp(Context* context);
    ~MilesApp() override;

    // Application lifecycle
    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    // Handle console commands
    void HandleMistralCommand(StringHash eventType, VariantMap& eventData);
};

}
