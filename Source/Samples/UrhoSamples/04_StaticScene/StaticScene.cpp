// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/View.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/IO/Log.h>

#include "StaticScene.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(StaticScene)

StaticScene::StaticScene(Context* context) :
    Sample(context)
{
}

void StaticScene::Start()
{
    // Execute base class startup
    Sample::Start();

    // Load UI style for ProfilerUI (must be before creating UI elements)
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateInstructions();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Hook up to the frame update events
    SubscribeToEvents();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_RELATIVE);

    // Initialize profiler UI
    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler(), graphics);
    profilerUI_->SetVisible(true);
}

void StaticScene::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();

    // Floor plane
    Node* planeNode = scene_->CreateChild("Plane");
    planeNode->SetScale(Vector3(20.0f, 1.0f, 20.0f));
    auto* planeObject = planeNode->CreateComponent<StaticModel>();
    planeObject->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    planeObject->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

    // Directional light with shadows
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetCastShadows(true);
    light->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    light->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));

    // One mushroom
    Node* mushroomNode = scene_->CreateChild("Mushroom");
    mushroomNode->SetPosition(Vector3(0.0f, 0.0f, 3.0f));
    mushroomNode->SetScale(2.0f);
    auto* mushroomObject = mushroomNode->CreateComponent<StaticModel>();
    mushroomObject->SetModel(cache->GetResource<Model>("Models/Mushroom.mdl"));
    mushroomObject->SetMaterial(cache->GetResource<Material>("Materials/Mushroom.xml"));
    mushroomObject->SetCastShadows(true);

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-100.0f, 100.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Camera looking at the mushroom from above-ish
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();
    cameraNode_->SetPosition(Vector3(0.0f, 5.0f, -5.0f));
    cameraNode_->SetRotation(Quaternion(30.0f, 0.0f, 0.0f));
}

// CreateInstructions is provided by the Sample base class.
// Override here if you need custom instruction text:
// void StaticScene::CreateInstructions()
// {
//     Sample::CreateInstructions("Your custom text here");
// }

void StaticScene::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();

    // Set up a viewport to the Renderer subsystem so that the 3D scene can be seen. We need to define the scene and the camera
    // at minimum. Additionally we could configure the viewport screen size and the rendering path (eg. forward / deferred) to
    // use, but now we just use full screen and default render path configured in the engine command line options
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

// MoveCamera is provided by the Sample base class.
// Override here if you need custom camera controls:
// void StaticScene::MoveCamera(float timeStep)
// {
//     Sample::MoveCamera(timeStep);
// }

void StaticScene::SubscribeToEvents()
{
    // Subscribe HandleUpdate() function for processing update events
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(StaticScene, HandleUpdate));
}

void StaticScene::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;

    // Take the frame time step, which is stored as a float
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // AUTO-TEST: Disabled - camera stays fixed for debugging
    // autoTestTimer_ += timeStep;
    // if (autoTestTimer_ < 3.0f)
    // {
    //     const float AUTO_MOVE_SPEED = 15.0f;
    //     cameraNode_->Translate(Vector3::FORWARD * AUTO_MOVE_SPEED * timeStep);
    // }

    // Move the camera, scale movement with time step
    MoveCamera(timeStep);

    // Update profiler
    auto* graphics = GetSubsystem<Graphics>();
    auto* renderer = GetSubsystem<Renderer>();
    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);

        // Add custom stats to profiler UI
        unsigned numBatches = graphics->GetNumBatches();
        unsigned numInstanced = graphics->GetNumInstancedDrawCalls();
        unsigned totalInstances = graphics->GetTotalInstanceCount();
        unsigned numVBBinds = graphics->GetNumVertexBufferBinds();
        unsigned numInstBinds = graphics->GetNumInstanceBufferBinds();
        unsigned numPipelineChanges = graphics->GetNumPipelineChanges();
        Vector3 camPos = cameraNode_->GetPosition();

        // Get instances written to buffer from the view
        unsigned instancesInBuffer = 0;
        if (renderer->GetNumViewports() > 0)
        {
            Viewport* viewport = renderer->GetViewport(0);
            if (viewport && viewport->GetView())
                instancesInBuffer = viewport->GetView()->GetInstancesWrittenToBuffer();
        }

        String stats;
        stats += "In buffer: " + String(instancesInBuffer) + " | Submit: " + String(totalInstances) + "\n";
        stats += "InstDraws: " + String(numInstanced) + " | Batches: " + String(numBatches) + "\n";
        stats += "VBBinds: " + String(numVBBinds) + " | InstBinds: " + String(numInstBinds) + "\n";
        stats += "Pipelines: " + String(numPipelineChanges) + "\n";
        char camBuf[64];
        sprintf(camBuf, "Cam: %.1f, %.1f, %.1f", camPos.x_, camPos.y_, camPos.z_);
        stats += String(camBuf);

        profilerUI_->SetCustomStats(stats);
    }

}

