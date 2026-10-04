// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "HelloWorld.h"

#include <Urho3D/DebugNew.h>

// Expands to this example's entry-point
URHO3D_DEFINE_APPLICATION_MAIN(HelloWorld)

HelloWorld::HelloWorld(Context* context) :
    Sample(context)
{
}

void HelloWorld::Start()
{
    // Execute base class startup
    Sample::Start();

    // Load UI style and set as default for all UI elements
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // Create "Hello World" Text
    CreateText();

    // Finally subscribe to the update event. Note that by subscribing events at this point we have already missed some events
    // like the ScreenMode event sent by the Graphics subsystem when opening the application window. To catch those as well we
    // could subscribe in the constructor instead.
    SubscribeToEvents();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_FREE);

    // TEMPORARY: Disable ProfilerUI to avoid Mesa RADV driver bug with many pipeline barriers
    // TODO: Re-enable when driver bug is fixed
    // auto* graphics = GetSubsystem<Graphics>();
    // auto* ui = GetSubsystem<UI>();
    // profilerUI_ = new ProfilerUI(context_);
    // profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    // profilerUI_->SetVisible(true);
}

void HelloWorld::CreateText()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();
    UIElement* root = ui->GetRoot();

    // Construct new Text object
    SharedPtr<Text> helloText(new Text(context_));
    helloText->SetText("Hello World from Urho3D!");
    helloText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 30);
    helloText->SetColor(Color(0.0f, 1.0f, 0.0f));

    // Align Text center-screen
    helloText->SetHorizontalAlignment(HA_CENTER);
    helloText->SetVerticalAlignment(VA_CENTER);

    // Add Text instance to the UI root element
    root->AddChild(helloText);

    // Add Vulkan indicator in top-left corner
    SharedPtr<Text> vulkanIndicator(new Text(context_));
    vulkanIndicator->SetText("Using: Vulkan");
    vulkanIndicator->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 14);
    vulkanIndicator->SetColor(Color::YELLOW);
    vulkanIndicator->SetPosition(10, 10);
    root->AddChild(vulkanIndicator);
}

void HelloWorld::SubscribeToEvents()
{
    // Subscribe HandleUpdate() function for processing update events
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(HelloWorld, HandleUpdate));
}

void HelloWorld::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;

    // Take the frame time step
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // TEMPORARY: ProfilerUI disabled to avoid Mesa RADV driver bug
    // // Update profiler display
    // if (profilerUI_)
    // {
    //     GetSubsystem<Graphics>()->GetVulkanProfiler()->RecordFrame(timeStep);
    //     profilerUI_->Update();
    // }
}
