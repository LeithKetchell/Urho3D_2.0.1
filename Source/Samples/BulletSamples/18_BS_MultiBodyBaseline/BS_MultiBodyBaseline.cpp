// Port of bullet3/examples/MultiBodyBaseline — NOT PORTABLE — Requires btMultiBody baseline comparison
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_MultiBodyBaseline.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_MultiBodyBaseline)

BS_MultiBodyBaseline::BS_MultiBodyBaseline(Context* context) :
    Sample(context)
{
}

void BS_MultiBodyBaseline::Start()
{
    Sample::Start();

    CreateScene();
    SetupViewport();
    SubscribeToEvents();

    Sample::InitMouseMode(MM_RELATIVE);

    CreateInstructions("MultiBodyBaseline — NOT PORTABLE — Requires btMultiBody baseline comparison");
}

void BS_MultiBodyBaseline::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<PhysicsWorld>();

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(0.7f, 0.7f, 0.7f));

    // Ground
    Node* floorNode = scene_->CreateChild("Floor");
    floorNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    floorNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    auto* floorModel = floorNode->CreateComponent<StaticModel>();
    floorModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    floorModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* floorBody = floorNode->CreateComponent<RigidBody>();
    auto* floorShape = floorNode->CreateComponent<CollisionShape>();
    floorShape->SetBox(Vector3::ONE);

    // Camera
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();
    cameraNode_->SetPosition(Vector3(0.0f, 5.0f, -20.0f));
}

void BS_MultiBodyBaseline::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_MultiBodyBaseline::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_MultiBodyBaseline, HandleUpdate));
}

void BS_MultiBodyBaseline::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);
}
