#pragma once
#include "Sample.h"

/// Port of Bullet SDK SoftDemo — multiple soft body sub-demos.
/// N/P keys cycle through demos, faithful to original's Init_* functions.
class BS_SoftDemo : public Sample
{
    URHO3D_OBJECT(BS_SoftDemo, Sample);
public:
    explicit BS_SoftDemo(Context* context) : Sample(context) {}
    void Start() override;
private:
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    void InitDemo(int index);
    void ClearDemo();
    void Init_Cloth();
    void Init_Pressure();
    void Init_Volume();
    void Init_Ropes();
    void Init_RopeAttach();
    void Init_ClothAttach();
    void Init_Aero();
    void Init_Friction();
    void Init_Impact();

    static const int NUM_DEMOS = 9;
    int currentDemo_{0};
    WeakPtr<Text> demoText_;
};
