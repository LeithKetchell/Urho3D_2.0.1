// Port of bullet3/examples/BasicDemo/BasicExample.cpp
// Original by Erwin Coumans — zlib license
//
// Original setup:
//   Ground: btBoxShape(50,50,50) at (0,-50,0), mass 0
//   Dynamic: 125 btBoxShape(0.1,0.1,0.1), mass 1.0, 5x5x5 grid
//            x: 0..0.8, y: 2..2.8, z: 0..0.8, spacing 0.2
//   Gravity: default (0,-10,0)
//   Camera: distance 4, yaw 52, pitch -35

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_BasicDemo.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_BasicDemo)

void BS_BasicDemo::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "Bullet BasicDemo — 125 boxes (5x5x5 grid)\n"
        "WASD+mouse | Space=debug draw"
    );
    SetupViewport();
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);

    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);
}

void BS_BasicDemo::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();

    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));

    // Zone for ambient lighting
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Directional light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(1.0f, 1.0f, 1.0f));
    light->SetSpecularIntensity(1.0f);

    // Ground — original: btBoxShape(50,50,50) at (0,-50,0), mass 0
    // In Urho3D we use a visible box scaled to match
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
    groundNode->SetScale(Vector3(100.0f, 100.0f, 100.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    groundBody->SetMass(0.0f);
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE); // Scaled by node

    // 125 dynamic boxes — original: btBoxShape(0.1,0.1,0.1), mass 1.0
    // 5x5x5 grid, x: 0..0.8, y: 2..2.8, z: 0..0.8, spacing 0.2
    // Urho3D box half-extents via node scale: 0.2 (diameter = 0.2 matches original full extent)
    for (int k = 0; k < 5; ++k)
    {
        for (int i = 0; i < 5; ++i)
        {
            for (int j = 0; j < 5; ++j)
            {
                Node* boxNode = scene_->CreateChild("Box");
                boxNode->SetPosition(Vector3(0.2f * i, 2.0f + 0.2f * k, 0.2f * j));
                boxNode->SetScale(0.2f);

                auto* model = boxNode->CreateComponent<StaticModel>();
                model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
                model->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
                model->SetCastShadows(true);

                auto* body = boxNode->CreateComponent<RigidBody>();
                body->SetMass(1.0f);

                auto* shape = boxNode->CreateComponent<CollisionShape>();
                shape->SetBox(Vector3::ONE); // Scaled by node to 0.2
            }
        }
    }

    // Camera — original: distance 4, yaw 52, pitch -35
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    // Position camera to match original viewpoint approximately
    yaw_ = 52.0f;
    pitch_ = -35.0f;
    cameraNode_->SetPosition(Vector3(-2.0f, 3.0f, -3.0f));
    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
}

void BS_BasicDemo::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_BasicDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_BasicDemo, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_BasicDemo, HandlePostRenderUpdate));
}

void BS_BasicDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_BasicDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
