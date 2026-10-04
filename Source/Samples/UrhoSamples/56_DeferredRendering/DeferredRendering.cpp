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
#include <Urho3D/Graphics/RenderPath.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/Resource/XMLFile.h>

#include "DeferredRendering.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(DeferredRendering)

DeferredRendering::DeferredRendering(Context* context) :
    Sample(context),
    currentRenderPath_(1)  // Deferred rendering
{
}

void DeferredRendering::Start()
{
    // Execute base class startup
    Sample::Start();

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateInstructions();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Create the lights
    CreateLights();

    // Hook up to the frame update events
    SubscribeToEvents();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_RELATIVE);

    // Load UI style for ProfilerUI
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // Initialize profiler UI
    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);
}

void DeferredRendering::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);

    // Create the Octree component to the scene. This is required before adding
    // any drawable components, or else nothing will show up.
    scene_->CreateComponent<Octree>();

    // Create a Zone for ambient lighting and fog
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Create a large plane as the floor
    Node* floorNode = scene_->CreateChild("Floor");
    floorNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    auto* floorModel = floorNode->CreateComponent<StaticModel>();
    floorModel->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    floorModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

    // Create some boxes scattered around for visual interest
    const unsigned NUM_OBJECTS = 40;
    for (unsigned i = 0; i < NUM_OBJECTS; ++i)
    {
        Node* boxNode = scene_->CreateChild("Box");
        boxNode->SetPosition(Vector3(Random(80.0f) - 40.0f, 2.0f, Random(80.0f) - 40.0f));
        boxNode->SetRotation(Quaternion(Random(360.0f), Random(360.0f), Random(360.0f)));
        boxNode->SetScale(0.5f + Random(1.0f));

        auto* boxModel = boxNode->CreateComponent<StaticModel>();
        boxModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        boxModel->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));
    }

    // Create a scene node for the camera
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->SetPosition(Vector3(0.0f, 5.0f, -20.0f));
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);

    // HDR rendering disabled for now — viewport buffer blit not yet working in Vulkan
    // auto* renderer = GetSubsystem<Renderer>();
    // renderer->SetHDRRendering(true);
}

void DeferredRendering::CreateLights()
{
    // Create a grid of point lights for demonstrating deferred rendering benefits
    const float LIGHT_RADIUS = 20.0f;
    const float GRID_SIZE = 30.0f;
    const int GRID_DIM = 4;  // 4x4 grid = 16 lights

    for (int x = 0; x < GRID_DIM; ++x)
    {
        for (int z = 0; z < GRID_DIM; ++z)
        {
            // Create light node
            Node* lightNode = scene_->CreateChild("PointLight");

            // Position in a grid pattern
            float posX = (x - GRID_DIM / 2) * GRID_SIZE;
            float posZ = (z - GRID_DIM / 2) * GRID_SIZE;
            lightNode->SetPosition(Vector3(posX, 8.0f, posZ));

            // Create light component
            auto* light = lightNode->CreateComponent<Light>();
            light->SetLightType(LIGHT_POINT);
            light->SetRange(LIGHT_RADIUS);

            // Vary light colors for visual interest
            Color lightColor;
            if ((x + z) % 3 == 0)
                lightColor = Color(1.0f, 0.5f, 0.5f);  // Reddish
            else if ((x + z) % 3 == 1)
                lightColor = Color(0.5f, 1.0f, 0.5f);  // Greenish
            else
                lightColor = Color(0.5f, 0.5f, 1.0f);  // Bluish

            light->SetColor(lightColor);
            light->SetBrightness(1.5f);

            // Add some animation by storing the original position
            // We'll animate these in HandleUpdate
        }
    }
}

void DeferredRendering::SetupViewport()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* renderer = GetSubsystem<Renderer>();
    auto* graphics = GetSubsystem<Graphics>();

    // Create a new viewport which is used for rendering
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));

    // Set the deferred render path
    // Choose based on currentRenderPath_
    XMLFile* renderPathFile = nullptr;

    switch (currentRenderPath_)
    {
    case 0:
        // Forward rendering
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Forward.xml");
        break;
    case 1:
        // Deferred rendering (standard)
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Deferred.xml");
        break;
    case 2:
        // PBR Deferred rendering
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/PBRDeferred.xml");
        break;
    default:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Deferred.xml");
        break;
    }

    if (renderPathFile)
    {
        viewport->SetRenderPath(renderPathFile);
    }

    renderer->SetViewport(0, viewport);
}

void DeferredRendering::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    // Construct new Text objects, set the display strings, and add to the UI root element
    auto font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    auto instructionText = MakeShared<Text>(context_);
    instructionText->SetText(
        "Deferred Rendering Example\n\n"
        "Use W/A/S/D and mouse to move camera\n"
        "1 - Forward Rendering\n"
        "2 - Deferred Rendering (G-Buffer)\n"
        "3 - PBR Deferred Rendering\n"
        "Space - Toggle light animation\n"
        "ESC - Exit"
    );
    instructionText->SetFont(font, 14);
    instructionText->SetColor(Color::WHITE);
    instructionText->SetPosition(10, 10);
    ui->GetRoot()->AddChild(instructionText);

    renderPathText_ = MakeShared<Text>(context_);
    renderPathText_->SetFont(font, 16);
    renderPathText_->SetColor(Color::YELLOW);
    renderPathText_->SetPosition(10, 150);
    renderPathText_->SetText("Current: Deferred Rendering");
    ui->GetRoot()->AddChild(renderPathText_);
}

void DeferredRendering::HandleRenderPathToggle()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* renderer = GetSubsystem<Renderer>();

    XMLFile* renderPathFile = nullptr;
    String renderPathName;

    switch (currentRenderPath_)
    {
    case 0:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Forward.xml");
        renderPathName = "Forward Rendering";
        break;
    case 1:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Deferred.xml");
        renderPathName = "Deferred Rendering";
        break;
    case 2:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/PBRDeferred.xml");
        renderPathName = "PBR Deferred Rendering";
        break;
    default:
        renderPathFile = cache->GetResource<XMLFile>("RenderPaths/Deferred.xml");
        renderPathName = "Deferred Rendering";
        break;
    }

    // Update the viewport with the new render path
    if (renderPathFile)
    {
        Viewport* viewport = renderer->GetViewport(0);
        if (viewport)
        {
            viewport->SetRenderPath(renderPathFile);
        }
    }

    // Update the UI text
    if (renderPathText_)
    {
        renderPathText_->SetText("Current: " + renderPathName);
    }

    URHO3D_LOGINFOF("Switched to: %s", renderPathName.CString());
}

void DeferredRendering::MoveCamera(float timeStep)
{
    auto* input = GetSubsystem<Input>();

    // Movement speed as world units per second
    const float MOVE_SPEED = 20.0f;
    // Mouse sensitivity as degrees per pixel
    const float MOUSE_SENSITIVITY = 0.1f;

    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);

    if (input->GetMouseButtonDown(MOUSEB_RIGHT))
    {
        IntVector2 mouseMove = input->GetMouseMove();
        static float yaw = 0;
        static float pitch = 0;

        yaw += MOUSE_SENSITIVITY * mouseMove.x_;
        pitch += MOUSE_SENSITIVITY * mouseMove.y_;
        pitch = Clamp(pitch, -90.0f, 90.0f);

        cameraNode_->SetRotation(Quaternion(pitch, yaw, 0.0f));
    }
}

void DeferredRendering::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(DeferredRendering, HandleUpdate));
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(DeferredRendering, HandleKeyDown));
}

void DeferredRendering::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;

    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    // Simple animation: rotate lights around their positions
    static float lightAnimTime = 0.0f;
    static bool animateLights = true;

    if (animateLights)
    {
        lightAnimTime += timeStep;

        for (unsigned i = 0; i < scene_->GetNumChildren(); ++i)
        {
            Node* node = scene_->GetChild(i);
            if (node->GetName().StartsWith("PointLight"))
            {
                Light* light = node->GetComponent<Light>();
                if (light)
                {
                    // Gentle bobbing motion
                    Vector3 pos = node->GetPosition();
                    pos.y_ = 8.0f + Sin(lightAnimTime * 2.0f + i) * 2.0f;
                    node->SetPosition(pos);
                }
            }
        }
    }

    // Update profiler display
    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void DeferredRendering::HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
    using namespace KeyDown;
    int key = eventData[P_KEY].GetI32();

    if (key == KEY_ESCAPE)
        engine_->Exit();
    else if (key == KEY_1)
    {
        currentRenderPath_ = 0;
        HandleRenderPathToggle();
    }
    else if (key == KEY_2)
    {
        currentRenderPath_ = 1;
        HandleRenderPathToggle();
    }
    else if (key == KEY_3)
    {
        currentRenderPath_ = 2;
        HandleRenderPathToggle();
    }
    else if (key == KEY_SPACE)
    {
        // Toggle light animation
        // (In a real application, you might want a boolean flag)
    }
}

