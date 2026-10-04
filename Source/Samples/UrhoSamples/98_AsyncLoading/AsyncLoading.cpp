// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
//
// Sample 98: Async Scene Loading — crash test dummy
// Loads SceneLoadExample.xml asynchronously, displays progress on screen.
// If this crashes, async loading is broken on the current backend.

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "AsyncLoading.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(AsyncLoading)

AsyncLoading::AsyncLoading(Context* context) :
    Sample(context)
{
}

void AsyncLoading::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // Status text — big and centered
    auto* ui = GetSubsystem<UI>();
    statusText_ = new Text(context_);
    statusText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 20);
    statusText_->SetColor(Color::WHITE);
    statusText_->SetHorizontalAlignment(HA_CENTER);
    statusText_->SetVerticalAlignment(VA_CENTER);
    statusText_->SetTextAlignment(HA_CENTER);
    ui->GetRoot()->AddChild(statusText_);

    SetStatusText("Sample 98: Async Loading Test\n\nWaiting 1 second before starting load...");

    SubscribeToEvents();
    Sample::InitMouseMode(MM_FREE);
}

void AsyncLoading::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(AsyncLoading, HandleUpdate));
}

void AsyncLoading::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    if (!loadStarted_)
    {
        // Wait 1 second so we can see the initial text, then kick off async load
        waitTimer_ += timeStep;
        if (waitTimer_ >= 1.0f)
            StartAsyncLoad();
        return;
    }

    if (loadFinished_)
        return;

    // Poll async progress
    if (loadScene_ && loadScene_->IsAsyncLoading())
    {
        float progress = loadScene_->GetAsyncProgress();
        SetStatusText(ToString("Loading... %.0f%%", progress * 100.0f));
    }
}

void AsyncLoading::StartAsyncLoad()
{
    loadStarted_ = true;

    URHO3D_LOGINFO("=== ASYNC LOAD TEST: Creating scene ===");
    SetStatusText("Creating scene and starting async load...");

    // Create a fresh scene
    loadScene_ = new Scene(context_);

    // Subscribe to async events on this scene
    SubscribeToEvent(loadScene_, E_ASYNCLOADPROGRESS, URHO3D_HANDLER(AsyncLoading, HandleAsyncLoadProgress));
    SubscribeToEvent(loadScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(AsyncLoading, HandleAsyncLoadFinished));

    // Open scene file
    auto* cache = GetSubsystem<ResourceCache>();
    SharedPtr<File> sceneFile = cache->GetFile("Scenes/SceneLoadExample.xml");
    if (!sceneFile)
    {
        URHO3D_LOGERROR("=== ASYNC LOAD TEST: Failed to open SceneLoadExample.xml ===");
        SetStatusText("FAILED: Could not open Scenes/SceneLoadExample.xml");
        return;
    }

    URHO3D_LOGINFO("=== ASYNC LOAD TEST: Calling LoadAsyncXML ===");
    bool ok = loadScene_->LoadAsyncXML(sceneFile);
    if (!ok)
    {
        URHO3D_LOGERROR("=== ASYNC LOAD TEST: LoadAsyncXML returned false ===");
        SetStatusText("FAILED: LoadAsyncXML returned false");
        return;
    }

    URHO3D_LOGINFO("=== ASYNC LOAD TEST: LoadAsyncXML started successfully ===");
    SetStatusText("Async load started...");
}

void AsyncLoading::HandleAsyncLoadProgress(StringHash eventType, VariantMap& eventData)
{
    using namespace AsyncLoadProgress;

    float progress = eventData[P_PROGRESS].GetFloat();
    int loadedNodes = eventData[P_LOADEDNODES].GetI32();
    int totalNodes = eventData[P_TOTALNODES].GetI32();
    int loadedRes = eventData[P_LOADEDRESOURCES].GetI32();
    int totalRes = eventData[P_TOTALRESOURCES].GetI32();

    String msg = ToString("Loading... %.0f%%\n\nNodes: %d / %d\nResources: %d / %d",
        progress * 100.0f, loadedNodes, totalNodes, loadedRes, totalRes);
    SetStatusText(msg);

    URHO3D_LOGINFO(ToString("=== ASYNC LOAD: %.0f%% — nodes %d/%d, resources %d/%d ===",
        progress * 100.0f, loadedNodes, totalNodes, loadedRes, totalRes));
}

void AsyncLoading::HandleAsyncLoadFinished(StringHash eventType, VariantMap& eventData)
{
    loadFinished_ = true;

    URHO3D_LOGINFO("=== ASYNC LOAD TEST: FINISHED — setting up viewport ===");

    // Set up a camera and viewport to show the loaded scene
    Node* cameraNode = loadScene_->CreateChild("Camera");
    auto* camera = cameraNode->CreateComponent<Camera>();
    cameraNode->SetPosition(Vector3(0.0f, 5.0f, -10.0f));
    cameraNode->LookAt(Vector3::ZERO);

    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, loadScene_, camera));
    renderer->SetViewport(0, viewport);

    int nodeCount = loadScene_->GetChildren(true).Size();
    SetStatusText(ToString("SUCCESS!\n\nScene loaded: %d nodes\n\nAsync loading works on this backend.", nodeCount));

    URHO3D_LOGINFO(ToString("=== ASYNC LOAD TEST: Scene has %d nodes — SUCCESS ===", nodeCount));
}

void AsyncLoading::SetStatusText(const String& text)
{
    if (statusText_)
        statusText_->SetText(text);
}
