// Port of bulletphysics/bullet3/examples/SoftDemo/SoftDemo.cpp
// Original has 32 Init_* functions cycling through soft body demos.
// This port implements the subset our SoftBody API supports:
// Cloth, Pressure, Volume, Ropes, RopeAttach, ClothAttach, Aero, Friction, Impact.
// N/P keys cycle through demos.
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
#include <Urho3D/Physics/SoftBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_SoftDemo.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_SoftDemo)

static const char* DEMO_NAMES[] = {
    "Cloth", "Pressure", "Volume", "Ropes", "RopeAttach",
    "ClothAttach", "Aero", "Friction", "Impact"
};

void BS_SoftDemo::Start()
{
    Sample::Start();
    SetupViewport();
    CreateInstructions(
        "Bullet SoftDemo — 9 soft body sub-demos\n"
        "N=next demo | P=previous demo\n"
        "Cloth, Pressure, Ropes, Aero, Impact...\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
    InitDemo(0);
}

void BS_SoftDemo::ClearDemo()
{
    // Remove all scene children except camera
    if (scene_)
        scene_->RemoveAllChildren();
    scene_ = nullptr;
}

void BS_SoftDemo::InitDemo(int index)
{
    ClearDemo();

    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();

    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));

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
        groundNode->SetPosition(Vector3(0.0f, -5.0f, 0.0f));
        groundNode->SetScale(Vector3(100.0f, 1.0f, 100.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
    }

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 5.0f, -20.0f));
    cameraNode_->LookAt(Vector3(0.0f, 2.0f, 0.0f));

    // Update viewport with new scene
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, camera));
    renderer->SetViewport(0, viewport);

    currentDemo_ = index;

    switch (index)
    {
    case 0: Init_Cloth(); break;
    case 1: Init_Pressure(); break;
    case 2: Init_Volume(); break;
    case 3: Init_Ropes(); break;
    case 4: Init_RopeAttach(); break;
    case 5: Init_ClothAttach(); break;
    case 6: Init_Aero(); break;
    case 7: Init_Friction(); break;
    case 8: Init_Impact(); break;
    }

    // HUD
    auto* ui = GetSubsystem<UI>();
    if (!demoText_)
    {
        demoText_ = ui->GetRoot()->CreateChild<Text>();
        demoText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 14);
        demoText_->SetHorizontalAlignment(HA_CENTER);
        demoText_->SetVerticalAlignment(VA_TOP);
        demoText_->SetPosition(0, 5);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "SoftDemo %d/%d: %s\nN=next P=prev | WASD+mouse | Space=debug",
             index + 1, NUM_DEMOS, DEMO_NAMES[index]);
    demoText_->SetText(buf);
}

// === Init_Cloth: 100kg cloth patch suspended at 4 corners ===
// Original: btSoftBodyHelpers::CreatePatch, fix 4 corners, mass 100
void BS_SoftDemo::Init_Cloth()
{
    Node* clothNode = scene_->CreateChild("Cloth");
    clothNode->SetPosition(Vector3(0.0f, 8.0f, 0.0f));
    auto* softBody = clothNode->CreateComponent<SoftBody>();
    // Patch corners in local space: 8x8 meter cloth
    softBody->CreatePatch(
        Vector3(-4.0f, 0.0f, -4.0f),  // corner00
        Vector3(4.0f, 0.0f, -4.0f),   // corner10
        Vector3(-4.0f, 0.0f, 4.0f),   // corner01
        Vector3(4.0f, 0.0f, 4.0f),    // corner11
        31, 31,                         // resolution (original uses 31x31)
        1 + 2 + 4 + 8,                // fix all 4 corners
        true                            // diagonals
    );
    softBody->SetTotalMass(100.0f);
    softBody->SetLinearStiffness(0.5f);
    softBody->SetDamping(0.01f);
    softBody->SetMaterial(GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
}

// === Init_Pressure: ellipsoid with internal pressure ===
// Original: btSoftBodyHelpers::CreateEllipsoid, pressure kPR=2500
void BS_SoftDemo::Init_Pressure()
{
    Node* node = scene_->CreateChild("PressureBody");
    node->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    auto* softBody = node->CreateComponent<SoftBody>();
    softBody->CreateEllipsoid(Vector3::ZERO, Vector3(2.0f, 2.0f, 2.0f), 32);
    softBody->SetTotalMass(10.0f);
    softBody->SetPressure(2500.0f);
    softBody->SetLinearStiffness(0.9f);
    softBody->SetDamping(0.01f);
    softBody->SetMaterial(GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/StoneSmall.xml"));
}

// === Init_Volume: ellipsoid with volume conservation ===
// Original: CreateEllipsoid, kVC=20
void BS_SoftDemo::Init_Volume()
{
    Node* node = scene_->CreateChild("VolumeBody");
    node->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    auto* softBody = node->CreateComponent<SoftBody>();
    softBody->CreateEllipsoid(Vector3::ZERO, Vector3(2.0f, 2.0f, 2.0f), 32);
    softBody->SetTotalMass(10.0f);
    softBody->SetVolumeStiffness(20.0f);
    softBody->SetLinearStiffness(0.5f);
    softBody->SetMaterial(GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/StoneSmall.xml"));
}

// === Init_Ropes: 15 ropes with varying stiffness ===
// Original: 15 ropes, LST from 0.1 to 0.9
void BS_SoftDemo::Init_Ropes()
{
    auto* cache = GetSubsystem<ResourceCache>();
    for (int i = 0; i < 15; i++)
    {
        float x = -7.0f + (float)i;
        Node* ropeNode = scene_->CreateChild("Rope");
        ropeNode->SetPosition(Vector3(x, 10.0f, 0.0f));
        auto* softBody = ropeNode->CreateComponent<SoftBody>();
        softBody->CreateRope(
            Vector3(0.0f, 0.0f, 0.0f),
            Vector3(0.0f, -8.0f, 0.0f),
            16, 1  // fix top end only
        );
        softBody->SetTotalMass(0.5f);
        float stiffness = 0.1f + 0.8f * (float)i / 14.0f;
        softBody->SetLinearStiffness(stiffness);
        softBody->SetDamping(0.01f);
        softBody->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    }
}

// === Init_RopeAttach: two ropes anchored to a rigid body ===
// Original: two ropes attached to a rigid box via AppendAnchor
void BS_SoftDemo::Init_RopeAttach()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Rigid box that ropes attach to
    Node* boxNode = scene_->CreateChild("Box");
    boxNode->SetPosition(Vector3(0.0f, 4.0f, 0.0f));
    auto* boxModel = boxNode->CreateComponent<StaticModel>();
    boxModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    boxModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    boxModel->SetCastShadows(true);
    auto* boxBody = boxNode->CreateComponent<RigidBody>();
    boxBody->SetMass(5.0f);
    boxBody->SetBoxShape(Vector3::ONE);
    boxBody->SetFriction(0.5f);

    // Left rope
    Node* rope1Node = scene_->CreateChild("Rope1");
    rope1Node->SetPosition(Vector3(-2.0f, 10.0f, 0.0f));
    auto* rope1 = rope1Node->CreateComponent<SoftBody>();
    rope1->CreateRope(Vector3::ZERO, Vector3(0.0f, -6.0f, 0.0f), 16, 1);
    rope1->SetTotalMass(0.5f);
    rope1->SetLinearStiffness(0.8f);
    // Anchor bottom of rope to box
    rope1->AppendAnchor(rope1->GetNumNodes() - 1, boxBody);
    rope1->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));

    // Right rope
    Node* rope2Node = scene_->CreateChild("Rope2");
    rope2Node->SetPosition(Vector3(2.0f, 10.0f, 0.0f));
    auto* rope2 = rope2Node->CreateComponent<SoftBody>();
    rope2->CreateRope(Vector3::ZERO, Vector3(0.0f, -6.0f, 0.0f), 16, 1);
    rope2->SetTotalMass(0.5f);
    rope2->SetLinearStiffness(0.8f);
    rope2->AppendAnchor(rope2->GetNumNodes() - 1, boxBody);
    rope2->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
}

// === Init_ClothAttach: cloth attached to moving rigid body ===
// Original: cloth patch with one corner anchored to a rigid body
void BS_SoftDemo::Init_ClothAttach()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Moving box
    Node* boxNode = scene_->CreateChild("Box");
    boxNode->SetPosition(Vector3(0.0f, 8.0f, 0.0f));
    auto* boxModel = boxNode->CreateComponent<StaticModel>();
    boxModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    boxModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    boxModel->SetCastShadows(true);
    auto* boxBody = boxNode->CreateComponent<RigidBody>();
    boxBody->SetMass(2.0f);
    boxBody->SetBoxShape(Vector3::ONE);
    boxBody->SetLinearVelocity(Vector3(2.0f, 0.0f, 0.0f));

    // Cloth attached to box
    Node* clothNode = scene_->CreateChild("Cloth");
    clothNode->SetPosition(Vector3(0.0f, 8.0f, 0.0f));
    auto* softBody = clothNode->CreateComponent<SoftBody>();
    softBody->CreatePatch(
        Vector3(-3.0f, 0.0f, -3.0f),
        Vector3(3.0f, 0.0f, -3.0f),
        Vector3(-3.0f, 0.0f, 3.0f),
        Vector3(3.0f, 0.0f, 3.0f),
        16, 16, 0, true
    );
    softBody->SetTotalMass(5.0f);
    softBody->SetLinearStiffness(0.4f);
    softBody->SetDamping(0.01f);
    // Anchor corner 0 to the box
    softBody->AppendAnchor(0, boxBody);
    softBody->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
}

// === Init_Aero: aerodynamic cloth with wind ===
// Original: small cloth patches with aeroModel and wind velocity
void BS_SoftDemo::Init_Aero()
{
    auto* cache = GetSubsystem<ResourceCache>();

    for (int i = 0; i < 8; i++)
    {
        float x = -6.0f + (float)(i % 4) * 4.0f;
        float z = -3.0f + (float)(i / 4) * 6.0f;

        Node* node = scene_->CreateChild("AeroCloth");
        node->SetPosition(Vector3(x, 8.0f, z));
        auto* softBody = node->CreateComponent<SoftBody>();
        softBody->CreatePatch(
            Vector3(-1.0f, 0.0f, -1.0f),
            Vector3(1.0f, 0.0f, -1.0f),
            Vector3(-1.0f, 0.0f, 1.0f),
            Vector3(1.0f, 0.0f, 1.0f),
            8, 8,
            1,  // fix corner 0 only
            true
        );
        softBody->SetTotalMass(0.5f);
        softBody->SetLinearStiffness(0.5f);
        softBody->SetWindVelocity(Vector3(4.0f + (float)i * 0.5f, 0.0f, 0.0f));
        softBody->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    }
}

// === Init_Friction: soft boxes with varying friction ===
// Original: 20 soft boxes on a slope with friction from 0.1 to 1.0
void BS_SoftDemo::Init_Friction()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Tilted platform
    {
        Node* rampNode = scene_->CreateChild("Ramp");
        rampNode->SetPosition(Vector3(0.0f, 2.0f, 0.0f));
        rampNode->SetRotation(Quaternion(0.0f, 0.0f, -15.0f));
        rampNode->SetScale(Vector3(20.0f, 0.5f, 10.0f));
        auto* rampModel = rampNode->CreateComponent<StaticModel>();
        rampModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        rampModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
        auto* rampBody = rampNode->CreateComponent<RigidBody>();
        rampBody->SetBoxShape(Vector3::ONE);
    }

    for (int i = 0; i < 10; i++)
    {
        float z = -4.0f + (float)i;
        Node* node = scene_->CreateChild("FrictionBody");
        node->SetPosition(Vector3(-6.0f, 6.0f, z));
        auto* softBody = node->CreateComponent<SoftBody>();
        softBody->CreateEllipsoid(Vector3::ZERO, Vector3(0.5f, 0.5f, 0.5f), 16);
        softBody->SetTotalMass(1.0f);
        float friction = 0.1f + 0.9f * (float)i / 9.0f;
        softBody->SetFriction(friction);
        softBody->SetLinearStiffness(0.5f);
        softBody->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
    }
}

// === Init_Impact: rope impacted by falling box ===
// Original: rope + falling rigid box
void BS_SoftDemo::Init_Impact()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Rope stretched horizontally
    Node* ropeNode = scene_->CreateChild("Rope");
    ropeNode->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    auto* rope = ropeNode->CreateComponent<SoftBody>();
    rope->CreateRope(Vector3(-5.0f, 0.0f, 0.0f), Vector3(5.0f, 0.0f, 0.0f), 32, 1 + 2);
    rope->SetTotalMass(1.0f);
    rope->SetLinearStiffness(0.8f);
    rope->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));

    // Heavy box falls onto the rope
    Node* boxNode = scene_->CreateChild("FallingBox");
    boxNode->SetPosition(Vector3(0.0f, 12.0f, 0.0f));
    auto* boxModel = boxNode->CreateComponent<StaticModel>();
    boxModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    boxModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    boxModel->SetCastShadows(true);
    auto* boxBody = boxNode->CreateComponent<RigidBody>();
    boxBody->SetMass(10.0f);
    boxBody->SetBoxShape(Vector3::ONE);
    boxBody->SetFriction(0.5f);
}

void BS_SoftDemo::SetupViewport()
{
    // Viewport set in InitDemo after scene creation
}

void BS_SoftDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_SoftDemo, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_SoftDemo, HandlePostRenderUpdate));
}

void BS_SoftDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    // N = next demo, P = previous demo
    auto* input = GetSubsystem<Input>();
    if (input->GetKeyPress(KEY_N))
        InitDemo((currentDemo_ + 1) % NUM_DEMOS);
    if (input->GetKeyPress(KEY_P))
        InitDemo((currentDemo_ + NUM_DEMOS - 1) % NUM_DEMOS);
}

void BS_SoftDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_ && scene_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
