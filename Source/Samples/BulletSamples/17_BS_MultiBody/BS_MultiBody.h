// Port of bullet3/examples/MultiBody — NOT PORTABLE — Requires btMultiBody (featherstone articulated bodies)
#pragma once

#include "Sample.h"

class BS_MultiBody : public Sample
{
    URHO3D_OBJECT(BS_MultiBody, Sample);

public:
    explicit BS_MultiBody(Context* context);

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
};
