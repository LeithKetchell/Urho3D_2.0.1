// Port of bullet3/examples/Tutorial/Dof6ConstraintTutorial
// Demonstrates all 6 degrees-of-freedom constraint types
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_Tutorial : public Sample
{
    URHO3D_OBJECT(BS_Tutorial, Sample);

public:
    explicit BS_Tutorial(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    SharedPtr<ProfilerUI> profilerUI_;
};
