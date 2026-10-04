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

#include "MotionBlur.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(MotionBlur)

MotionBlur::MotionBlur(Context* context) :
    Sample(context)
{
}

void MotionBlur::Start()
{
    Sample::Start();

    // Load UI style for ProfilerUI
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions();
    SetupViewport();
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);

    // Initialize profiler UI
    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler(), graphics);
    profilerUI_->SetVisible(true);
}

void MotionBlur::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();

    // Zone for ambient and fog
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-100.0f, 100.0f));
    zone->SetAmbientColor(Color(0.1f, 0.1f, 0.15f));
    zone->SetFogColor(Color(0.05f, 0.05f, 0.1f));
    zone->SetFogStart(30.0f);
    zone->SetFogEnd(80.0f);

    // Floor
    Node* floorNode = scene_->CreateChild("Floor");
    floorNode->SetScale(Vector3(50.0f, 1.0f, 50.0f));
    auto* floorModel = floorNode->CreateComponent<StaticModel>();
    floorModel->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    floorModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

    // Scattered mushrooms (static)
    for (int i = 0; i < 20; ++i)
    {
        Node* mushNode = scene_->CreateChild("Mushroom");
        float x = (Random(40.0f) - 20.0f);
        float z = (Random(40.0f) - 20.0f);
        mushNode->SetPosition(Vector3(x, 0.0f, z));
        mushNode->SetRotation(Quaternion(0.0f, Random(360.0f), 0.0f));
        mushNode->SetScale(1.0f + Random(1.5f));
        auto* mushModel = mushNode->CreateComponent<StaticModel>();
        mushModel->SetModel(cache->GetResource<Model>("Models/Mushroom.mdl"));
        mushModel->SetMaterial(cache->GetResource<Material>("Materials/Mushroom.xml"));
        mushModel->SetCastShadows(true);
    }

    // Orbiting boxes (moving objects)
    for (int i = 0; i < 5; ++i)
    {
        Node* orbitNode = scene_->CreateChild("OrbitPivot");
        Node* boxNode = orbitNode->CreateChild("OrbitBox");
        float radius = 5.0f + i * 3.0f;
        float height = 1.5f + i * 0.8f;
        boxNode->SetPosition(Vector3(radius, height, 0.0f));
        boxNode->SetScale(0.8f + Random(0.6f));
        auto* boxModel = boxNode->CreateComponent<StaticModel>();
        boxModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        boxModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        boxModel->SetCastShadows(true);
        orbitingNodes_.Push(SharedPtr<Node>(orbitNode));
    }

    // === DIRECTIONAL LIGHT (rotating sun) ===
    dirLightNode_ = scene_->CreateChild("DirectionalLight");
    dirLightNode_->SetDirection(Vector3(0.5f, -1.0f, 0.7f));
    auto* dirLight = dirLightNode_->CreateComponent<Light>();
    dirLight->SetLightType(LIGHT_DIRECTIONAL);
    dirLight->SetColor(Color(0.8f, 0.7f, 0.6f));
    dirLight->SetBrightness(0.8f);
    dirLight->SetCastShadows(true);
    dirLight->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    dirLight->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));

    // === POINT LIGHTS (orbiting, colored) ===
    Color pointColors[] = {Color(1.0f, 0.3f, 0.1f), Color(0.1f, 0.5f, 1.0f), Color(0.2f, 1.0f, 0.3f)};
    for (int i = 0; i < 3; ++i)
    {
        Node* pointNode = scene_->CreateChild("PointLight");
        float angle = i * 120.0f * M_DEGTORAD;
        pointNode->SetPosition(Vector3(Cos(angle) * 8.0f, 3.0f, Sin(angle) * 8.0f));
        auto* pointLight = pointNode->CreateComponent<Light>();
        pointLight->SetLightType(LIGHT_POINT);
        pointLight->SetRange(15.0f);
        pointLight->SetColor(pointColors[i]);
        pointLight->SetBrightness(1.5f);
        pointLight->SetCastShadows(true);
        pointLightNodes_.Push(SharedPtr<Node>(pointNode));
    }

    // === SPOT LIGHT (scanning) ===
    spotLightNode_ = scene_->CreateChild("SpotLight");
    spotLightNode_->SetPosition(Vector3(0.0f, 10.0f, 0.0f));
    spotLightNode_->SetDirection(Vector3(0.0f, -1.0f, 0.5f));
    auto* spotLight = spotLightNode_->CreateComponent<Light>();
    spotLight->SetLightType(LIGHT_SPOT);
    spotLight->SetRange(20.0f);
    spotLight->SetFov(45.0f);
    spotLight->SetColor(Color(1.0f, 1.0f, 0.8f));
    spotLight->SetBrightness(2.0f);
    spotLight->SetCastShadows(true);
    spotLight->SetShadowBias(BiasParameters(0.00025f, 0.5f));

    // Camera
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();
    cameraNode_->SetPosition(Vector3(0.0f, 8.0f, -18.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

void MotionBlur::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    instructionText_ = ui->GetRoot()->CreateChild<Text>();
    instructionText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    instructionText_->SetHorizontalAlignment(HA_CENTER);
    instructionText_->SetVerticalAlignment(VA_CENTER);
    instructionText_->SetPosition(0, ui->GetRoot()->GetHeight() / 4);
    UpdateInstructions();
}

void MotionBlur::UpdateInstructions()
{
    String text;
    text += "WASD + Mouse to move camera\n";
    text += "[M] Motion Blur: " + String(motionBlurEnabled_ ? "ON" : "OFF");
    text += "  Strength: " + String(motionBlurStrength_, 2) + "\n";
    text += "[B] Bloom: " + String(bloomEnabled_ ? "ON" : "OFF") + "\n";
    text += "[F] FXAA: " + String(fxaaEnabled_ ? "ON" : "OFF") + "\n";
    text += "[+/-] Adjust motion blur strength";
    instructionText_->SetText(text);
}

void MotionBlur::SetupViewport()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* renderer = GetSubsystem<Renderer>();

    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);

    // Use ForwardHWDepth render path — creates a readable depth RTT for motion blur
    SharedPtr<RenderPath> renderPath(new RenderPath());
    renderPath->Load(cache->GetResource<XMLFile>("RenderPaths/ForwardHWDepth.xml"));
    viewport->SetRenderPath(renderPath);

    RenderPath* rp = viewport->GetRenderPath();

    // Motion blur first (operates on clean scene depth + color)
    rp->Append(cache->GetResource<XMLFile>("PostProcess/MotionBlur.xml"));
    rp->SetShaderParameter("MotionBlurStrength", motionBlurStrength_);
    rp->SetShaderParameter("MotionBlurSamples", 8.0f);
    rp->SetEnabled("MotionBlur", true);

    // Bloom after motion blur
    rp->Append(cache->GetResource<XMLFile>("PostProcess/Bloom.xml"));
    rp->SetShaderParameter("BloomThreshold", 0.3f);
    rp->SetShaderParameter("BloomMix", Vector2(0.9f, 0.4f));
    rp->SetEnabled("Bloom", true);

    // FXAA last (anti-aliasing on final output)
    rp->Append(cache->GetResource<XMLFile>("PostProcess/FXAA3.xml"));
    rp->SetEnabled("FXAA3", true);
}

void MotionBlur::AnimateScene(float timeStep)
{
    sceneTime_ += timeStep;

    // Rotate directional light (slow sun sweep)
    if (dirLightNode_)
    {
        float sunAngle = sceneTime_ * 10.0f;
        dirLightNode_->SetDirection(Vector3(Sin(sunAngle * M_DEGTORAD) * 0.6f, -1.0f, Cos(sunAngle * M_DEGTORAD) * 0.8f));
    }

    // Orbit point lights around the scene center
    for (unsigned i = 0; i < pointLightNodes_.Size(); ++i)
    {
        float baseAngle = i * 120.0f;
        float angle = (baseAngle + sceneTime_ * 30.0f * (1.0f + i * 0.3f)) * M_DEGTORAD;
        float radius = 8.0f + Sin(sceneTime_ * 0.5f + i) * 3.0f;
        float height = 3.0f + Sin(sceneTime_ * 0.7f + i * 2.0f) * 1.5f;
        pointLightNodes_[i]->SetPosition(Vector3(Cos(angle) * radius, height, Sin(angle) * radius));
    }

    // Sweep spot light
    if (spotLightNode_)
    {
        float sweepAngle = Sin(sceneTime_ * 0.8f) * 40.0f;
        spotLightNode_->SetDirection(Vector3(Sin(sweepAngle * M_DEGTORAD), -1.0f, Cos(sweepAngle * M_DEGTORAD) * 0.5f));
    }

    // Rotate orbiting objects
    for (unsigned i = 0; i < orbitingNodes_.Size(); ++i)
    {
        float speed = 20.0f + i * 10.0f;
        orbitingNodes_[i]->SetRotation(Quaternion(0.0f, sceneTime_ * speed, 0.0f));
        // Also spin the box itself
        Node* box = orbitingNodes_[i]->GetChild("OrbitBox");
        if (box)
            box->SetRotation(Quaternion(sceneTime_ * 60.0f, sceneTime_ * 40.0f, 0.0f));
    }
}

void MotionBlur::MoveCamera(float timeStep)
{
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    auto* input = GetSubsystem<Input>();
    const float MOVE_SPEED = 20.0f;
    const float MOUSE_SENSITIVITY = 0.1f;

    IntVector2 mouseMove = input->GetMouseMove();
    yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
    pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
    pitch_ = Clamp(pitch_, -90.0f, 90.0f);
    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);

    // Toggle effects
    if (input->GetKeyPress(KEY_M))
    {
        motionBlurEnabled_ = !motionBlurEnabled_;
        auto* renderer = GetSubsystem<Renderer>();
        Viewport* vp = renderer->GetViewport(0);
        if (vp && vp->GetRenderPath())
            vp->GetRenderPath()->SetEnabled("MotionBlur", motionBlurEnabled_);
        UpdateInstructions();
    }
    if (input->GetKeyPress(KEY_B))
    {
        bloomEnabled_ = !bloomEnabled_;
        auto* renderer = GetSubsystem<Renderer>();
        Viewport* vp = renderer->GetViewport(0);
        if (vp && vp->GetRenderPath())
            vp->GetRenderPath()->SetEnabled("Bloom", bloomEnabled_);
        UpdateInstructions();
    }
    if (input->GetKeyPress(KEY_F))
    {
        fxaaEnabled_ = !fxaaEnabled_;
        auto* renderer = GetSubsystem<Renderer>();
        Viewport* vp = renderer->GetViewport(0);
        if (vp && vp->GetRenderPath())
            vp->GetRenderPath()->SetEnabled("FXAA3", fxaaEnabled_);
        UpdateInstructions();
    }

    // Adjust motion blur strength
    if (input->GetKeyPress(KEY_KP_PLUS) || input->GetKeyPress(KEY_EQUALS))
    {
        motionBlurStrength_ = Min(motionBlurStrength_ + 0.25f, 4.0f);
        UpdateInstructions();
    }
    if (input->GetKeyPress(KEY_KP_MINUS) || input->GetKeyPress(KEY_MINUS))
    {
        motionBlurStrength_ = Max(motionBlurStrength_ - 0.25f, 0.0f);
        UpdateInstructions();
    }
}

void MotionBlur::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(MotionBlur, HandleUpdate));
}

void MotionBlur::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
    AnimateScene(timeStep);

    // Get current view-projection matrix from the camera
    // Use the same projection that View.cpp uses during rendering:
    // View.cpp toggles flipVertical ON before rendering (Vulkan Y-flip),
    // then calls GetGPUProjection() which returns flipMatrix * projection_ on Vulkan.
    // Since we're outside the render loop (flipVertical=OFF), manually apply the flip.
    auto* camera = cameraNode_->GetComponent<Camera>();
    auto* renderer = GetSubsystem<Renderer>();
    Matrix4 projection = camera->GetProjection();
    if (Graphics::GetGAPI() == GAPI_VULKAN)
    {
        projection.m10_ = -projection.m10_;
        projection.m11_ = -projection.m11_;
        projection.m12_ = -projection.m12_;
        projection.m13_ = -projection.m13_;
    }
    Matrix4 currentViewProj = projection * camera->GetView();

    // Update motion blur parameters on the render path
    Viewport* vp = renderer->GetViewport(0);
    if (vp && vp->GetRenderPath())
    {
        RenderPath* rp = vp->GetRenderPath();
        for (unsigned i = 0; i < rp->GetNumCommands(); ++i)
        {
            RenderPathCommand* cmd = rp->GetCommand(i);
            if (cmd && cmd->tag_ == "MotionBlur")
            {
                cmd->shaderParameters_["MotionBlurStrength"] = motionBlurStrength_;
                // Pass reprojection matrix: inverse(currentVP) * prevVP
                // This avoids world-space depth reconstruction entirely
                if (prevViewProjValid_)
                {
                    Matrix4 currentVPInv = currentViewProj.Inverse();
                    Matrix4 reproj = currentVPInv * prevViewProj_;
                    cmd->shaderParameters_["PrevViewProj"] = reproj;
                }
                break;
            }
        }
    }

    // Store current VP for next frame
    prevViewProj_ = currentViewProj;
    prevViewProjValid_ = true;

    // Update profiler
    auto* graphics = GetSubsystem<Graphics>();
    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);

        unsigned numBatches = graphics->GetNumBatches();
        unsigned numPipelineChanges = graphics->GetNumPipelineChanges();
        Vector3 camPos = cameraNode_->GetPosition();

        String stats;
        stats += "Batches: " + String(numBatches) + " | Pipelines: " + String(numPipelineChanges) + "\n";
        char camBuf[64];
        sprintf(camBuf, "Cam: %.1f, %.1f, %.1f", camPos.x_, camPos.y_, camPos.z_);
        stats += String(camBuf);
        profilerUI_->SetCustomStats(stats);
    }
}
