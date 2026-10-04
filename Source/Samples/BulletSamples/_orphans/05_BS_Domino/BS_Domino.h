#pragma once

#include "Sample.h"

/// Domino effect — a row of thin boxes that topple in sequence.
class BS_Domino : public Sample
{
    URHO3D_OBJECT(BS_Domino, Sample);

public:
    explicit BS_Domino(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    bool triggered_{false};
};
