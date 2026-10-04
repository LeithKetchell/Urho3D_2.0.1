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
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "ShadowTest.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(ShadowTest)

ShadowTest::ShadowTest(Context* context) :
    Sample(context)
{
}

void ShadowTest::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    SetupViewport();
    SubscribeToEvents();

    Sample::InitMouseMode(MM_RELATIVE);

    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler(), graphics);
    profilerUI_->SetVisible(true);
}

void ShadowTest::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();

    // Large white floor
    Node* floorNode = scene_->CreateChild("Floor");
    floorNode->SetScale(Vector3(50.0f, 1.0f, 50.0f));
    auto* floorModel = floorNode->CreateComponent<StaticModel>();
    floorModel->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    floorModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

    // Single cube, raised above floor
    Node* cubeNode = scene_->CreateChild("Cube");
    cubeNode->SetPosition(Vector3(0.0f, 1.0f, 0.0f));
    cubeNode->SetScale(2.0f);
    auto* cubeModel = cubeNode->CreateComponent<StaticModel>();
    cubeModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    cubeModel->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));
    cubeModel->SetCastShadows(true);

    // Spot light above and to the side, pointing down at 45 degrees, rotates around Y
    lightNode_ = scene_->CreateChild("Light");
    lightNode_->SetPosition(Vector3(0.0f, 8.0f, -8.0f));
    lightNode_->SetDirection(Vector3(0.0f, -0.707f, 0.707f));
    auto* light = lightNode_->CreateComponent<Light>();
    light->SetLightType(LIGHT_SPOT);
    light->SetColor(Color(1.0f, 1.0f, 0.8f));
    light->SetBrightness(2.0f);
    light->SetRange(30.0f);
    light->SetFov(60.0f);
    light->SetCastShadows(true);
    light->SetShadowBias(BiasParameters(0.00025f, 0.5f));

    // Camera looking at the cube from an elevated angle
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();
    cameraNode_->SetPosition(Vector3(0.0f, 8.0f, -10.0f));
    cameraNode_->SetRotation(Quaternion(35.0f, 0.0f, 0.0f));
}

void ShadowTest::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void ShadowTest::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(ShadowTest, HandleUpdate));
}

void ShadowTest::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Rotate light around Y axis at 30 degrees/sec
    lightAngle_ += 30.0f * timeStep;
    if (lightAngle_ > 360.0f)
        lightAngle_ -= 360.0f;

    // Orbit spot light around the cube at radius 8, height 8
    float rad = lightAngle_ * M_DEGTORAD;
    lightNode_->SetPosition(Vector3(sinf(rad) * 8.0f, 8.0f, cosf(rad) * 8.0f));
    // Always point at the cube (origin)
    lightNode_->SetDirection((Vector3::ZERO - lightNode_->GetPosition()).Normalized());

    // Camera movement
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    auto* input = GetSubsystem<Input>();
    const float MOVE_SPEED = 10.0f;
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

    // Update profiler
    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}
