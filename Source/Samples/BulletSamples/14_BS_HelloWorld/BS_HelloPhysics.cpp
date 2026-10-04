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
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_HelloPhysics.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_HelloPhysics)

void BS_HelloPhysics::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Bullet HelloWorld — single box falls and bounces\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_HelloPhysics::CreateScene()
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
    Node* lightNode = scene_->CreateChild("Light");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetCastShadows(true);
    light->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    light->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));

    // Floor
    Node* floorNode = scene_->CreateChild("Floor");
    floorNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    floorNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    auto* floorObject = floorNode->CreateComponent<StaticModel>();
    floorObject->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    floorObject->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* floorBody = floorNode->CreateComponent<RigidBody>();
    floorBody->SetBoxShape(Vector3::ONE);
    floorBody->SetRestitution(0.5f);

    // A single falling box
    Node* boxNode = scene_->CreateChild("Box");
    boxNode->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    auto* boxObject = boxNode->CreateComponent<StaticModel>();
    boxObject->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    boxObject->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    boxObject->SetCastShadows(true);
    auto* boxBody = boxNode->CreateComponent<RigidBody>();
    boxBody->SetMass(1.0f);
    boxBody->SetFriction(0.75f);
    boxBody->SetBoxShape(Vector3::ONE);
    boxBody->SetRestitution(0.5f);

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 3.0f, -10.0f));
}

void BS_HelloPhysics::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_HelloPhysics::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_HelloPhysics, HandleUpdate));
}

void BS_HelloPhysics::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}
