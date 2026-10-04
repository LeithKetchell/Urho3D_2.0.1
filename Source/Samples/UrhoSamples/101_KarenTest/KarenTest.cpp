// KarenTest — headless telemetry emitter.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/KarenClient.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/IO/Log.h>

#include "KarenTest.h"

URHO3D_DEFINE_APPLICATION_MAIN(KarenTest)

KarenTest::KarenTest(Context* context) : Application(context) {}

void KarenTest::Setup()
{
    engineParameters_[EP_HEADLESS] = true;
    engineParameters_[EP_LOG_NAME] = "KarenTest.log";
    engineParameters_[EP_SOUND] = false;
}

void KarenTest::Start()
{
    // Register and start KarenClient
    context_->RegisterSubsystem(new KarenClient(context_));
    auto* karen = GetSubsystem<KarenClient>();
    karen->Start("KarenTest");

    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(KarenTest, HandleUpdate));

    URHO3D_LOGINFO("KarenTest: Emitting telemetry. Run Karen viewer to see it.");
}

void KarenTest::Stop()
{
    auto* karen = GetSubsystem<KarenClient>();
    if (karen)
        karen->Stop();
}

void KarenTest::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    auto* karen = GetSubsystem<KarenClient>();
    if (!karen || !karen->IsActive())
        return;

    // Frame marker
    karen->MarkFrame();

    // Simulate zones
    karen->BeginZone("GameLogic");
    {
        karen->BeginZone("Physics");
        // Fake work
        volatile int x = 0;
        for (int i = 0; i < 10000; ++i)
            x += i;
        karen->EndZone();

        karen->BeginZone("AI");
        for (int i = 0; i < 5000; ++i)
            x += i;
        karen->EndZone();
    }
    karen->EndZone();

    karen->BeginZone("Rendering");
    for (int i = 0; i < 20000; ++i)
        volatile int y = i * i;
    karen->EndZone();

    // Sine wave plot
    plotValue_ += plotDirection_ * timeStep * 50.0f;
    if (plotValue_ > 100.0f || plotValue_ < 0.0f)
        plotDirection_ = -plotDirection_;
    karen->Plot("CPU Load", plotValue_);
    karen->Plot("Memory", 45.0f + plotValue_ * 0.2f);

    // Periodic messages
    frameCount_++;
    if (frameCount_ % 60 == 0)
        karen->Message("Frame " + String(frameCount_) + " — " +
                       String(karen->GetViewerCount()) + " viewers");
}
