// Port of bullet3/examples/DynamicControlDemo/MotorDemo.cpp
// 6-legged spider with motorized hinge joints driven by sinusoidal targets
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_DynamicControlDemo : public Sample
{
    URHO3D_OBJECT(BS_DynamicControlDemo, Sample);

public:
    explicit BS_DynamicControlDemo(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void CreateSpider(const Vector3& position, bool fixed);
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    static const int NUM_LEGS = 6;
    static const int JOINTS_PER_SPIDER = NUM_LEGS * 2;

    struct SpiderData
    {
        Node* bodyNode{};
        Node* legNodes[NUM_LEGS * 2]{};     // upper + lower for each leg
        Constraint* joints[NUM_LEGS * 2]{}; // hip + knee for each leg
    };

    Vector<SpiderData> spiders_;
    float time_{0.0f};
    float cyclePeriod_{2.0f};  // seconds
    float muscleStrength_{0.5f};
    SharedPtr<ProfilerUI> profilerUI_;
};
