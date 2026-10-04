// Port of bulletphysics/bullet3/examples/Benchmarks stacking test
// Copyright (c) Erwin Coumans, zlib license

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

#include "BS_Stacking.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Stacking)

static const int NUM_TOWERS = 5;
static const int TOWER_HEIGHT = 15;
static const float BOX_SIZE = 1.0f;
static const float TOWER_SPACING = 3.0f;

void BS_Stacking::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Stacking Benchmark — 5 towers x 15 boxes\n"
        "Wrecking ball auto-launched\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Stacking::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();

    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));
    physicsWorld->SetNumIterations(50);

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.3f, 0.3f, 0.3f));
    zone->SetFogColor(Color(0.2f, 0.2f, 0.3f));
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

    // Ground
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
        groundNode->SetScale(Vector3(60.0f, 1.0f, 60.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(0.8f);
    }

    // Towers — rows of stacked boxes
    String materials[] = {
        "Materials/StoneEnvMapSmall.xml",
        "Materials/StoneTiled.xml"
    };

    float startX = -(NUM_TOWERS - 1) * TOWER_SPACING / 2.0f;

    for (int t = 0; t < NUM_TOWERS; t++)
    {
        for (int row = 0; row < TOWER_HEIGHT; row++)
        {
            // Alternate orientation each row for stability (brick pattern)
            bool rotated = (row % 2 == 1);

            float x = startX + t * TOWER_SPACING;
            float y = BOX_SIZE / 2.0f + row * BOX_SIZE;
            float z = 0.0f;

            Node* node = scene_->CreateChild("Block");
            node->SetPosition(Vector3(x, y, z));

            if (rotated)
                node->SetRotation(Quaternion(90.0f, Vector3::UP));

            node->SetScale(Vector3(BOX_SIZE, BOX_SIZE, BOX_SIZE));

            auto* model = node->CreateComponent<StaticModel>();
            model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
            model->SetMaterial(cache->GetResource<Material>(materials[(t + row) % 2]));
            model->SetCastShadows(true);

            auto* body = node->CreateComponent<RigidBody>();
            body->SetMass(1.0f);
            body->SetBoxShape(Vector3::ONE);
            body->SetFriction(0.6f);
        }
    }

    // Wrecking ball — a heavy sphere launched at the towers
    {
        Node* ballNode = scene_->CreateChild("WreckingBall");
        ballNode->SetPosition(Vector3(-15.0f, 5.0f, 0.0f));

        auto* model = ballNode->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
        model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        model->SetCastShadows(true);

        auto* body = ballNode->CreateComponent<RigidBody>();
        body->SetMass(20.0f);
        body->SetSphereShape(1.0f);
        body->SetFriction(0.5f);
        body->SetLinearVelocity(Vector3(15.0f, 5.0f, 0.0f));
    }

    // Labels
    auto* ui = GetSubsystem<UI>();
    auto* instructionText = ui->GetRoot()->CreateChild<Text>();
    instructionText->SetText(
        "Stacking Benchmark — 5 towers x 15 boxes\n"
        "Wrecking ball auto-launched\n"
        "WASD+mouse | Space=debug draw"
    );
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);
    instructionText->SetTextAlignment(HA_CENTER);
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetVerticalAlignment(VA_CENTER);
    instructionText->SetPosition(0, ui->GetRoot()->GetHeight() / 4);

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(-20.0f, 10.0f, -20.0f));
    cameraNode_->LookAt(Vector3(0.0f, 5.0f, 0.0f));
}

void BS_Stacking::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Stacking::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Stacking, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Stacking, HandlePostRenderUpdate));
}

void BS_Stacking::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_Stacking::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
