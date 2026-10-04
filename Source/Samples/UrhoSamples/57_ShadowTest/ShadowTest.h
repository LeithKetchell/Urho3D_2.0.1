#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

namespace Urho3D
{
class Node;
class Scene;
}

class ShadowTest : public Sample
{
    URHO3D_OBJECT(ShadowTest, Sample);

public:
    explicit ShadowTest(Context* context);
    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
    Node* lightNode_{};
    float lightAngle_{0.0f};
};
