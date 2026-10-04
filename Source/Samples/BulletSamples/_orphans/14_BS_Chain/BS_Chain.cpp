// Port of bulletphysics/bullet3/examples/ExtendedTutorials/Chain.cpp
// 10 boxes stacked vertically, connected by point-to-point constraints.
// Top box is static (anchor), bottom 9 are dynamic. 2 constraints per pair.
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
#include <Urho3D/Physics/Constraint.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_Chain.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Chain)

// Original values from Chain.cpp
static const int NUM_BOXES = 10;
// Original half-extents: (1, 1, 0.25) — Urho3D node scale = 2× half-extents
static const Vector3 BOX_SCALE(2.0f, 2.0f, 0.5f);

void BS_Chain::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Bullet Chain — 10 vertical boxes, point-to-point constraints\n"
        "Top box anchored, bottom 9 swing freely\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Chain::CreateScene()
{
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

    // Ground — original: btBoxShape(50,50,50) at y=-50
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
        groundNode->SetScale(Vector3(100.0f, 100.0f, 100.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
    }

    // 10 chain boxes — stacked vertically at (0, 5+i*2, 0)
    // Boxes 0-8: mass=1 (dynamic), box 9: mass=0 (static anchor at top)
    Node* boxes[NUM_BOXES];
    for (int i = 0; i < NUM_BOXES; i++)
    {
        Node* boxNode = scene_->CreateChild("ChainBox");
        // Slight X offset on lower boxes so chain swings rather than balancing perfectly
        float xOffset = (i < NUM_BOXES - 1) ? (NUM_BOXES - 1 - i) * 0.05f : 0.0f;
        boxNode->SetPosition(Vector3(xOffset, 5.0f + i * 2.0f, 0.0f));
        boxNode->SetScale(BOX_SCALE);

        auto* model = boxNode->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(cache->GetResource<Material>(i == NUM_BOXES - 1
            ? "Materials/StoneTiled.xml" : "Materials/StoneEnvMapSmall.xml"));
        model->SetCastShadows(true);

        auto* body = boxNode->CreateComponent<RigidBody>();
        body->SetBoxShape(Vector3::ONE);
        if (i < NUM_BOXES - 1)
            body->SetMass(1.0f);
        // else mass=0 (static anchor)

        boxes[i] = boxNode;
    }

    // Point-to-point constraints — 2 per adjacent pair (left and right)
    // Original pivots: body i top = (-0.5, 1, 0) and (0.5, 1, 0)
    //                  body i+1 bottom = (-0.5, -1, 0) and (0.5, -1, 0)
    // In Urho3D, Constraint positions are in node-local scaled space (divided by node scale)
    for (int i = 0; i < NUM_BOXES - 1; i++)
    {
        // Left constraint
        auto* leftConstraint = boxes[i]->CreateComponent<Constraint>();
        leftConstraint->SetConstraintType(CONSTRAINT_POINT);
        leftConstraint->SetOtherBody(boxes[i + 1]->GetComponent<RigidBody>());
        // Pivot in body i: (-0.5, 1, 0) in physics space → divide by half-extents for normalized
        leftConstraint->SetPosition(Vector3(-0.5f / BOX_SCALE.x_, 1.0f / BOX_SCALE.y_, 0.0f));
        leftConstraint->SetOtherPosition(Vector3(-0.5f / BOX_SCALE.x_, -1.0f / BOX_SCALE.y_, 0.0f));

        // Right constraint
        auto* rightConstraint = boxes[i]->CreateComponent<Constraint>();
        rightConstraint->SetConstraintType(CONSTRAINT_POINT);
        rightConstraint->SetOtherBody(boxes[i + 1]->GetComponent<RigidBody>());
        rightConstraint->SetPosition(Vector3(0.5f / BOX_SCALE.x_, 1.0f / BOX_SCALE.y_, 0.0f));
        rightConstraint->SetOtherPosition(Vector3(0.5f / BOX_SCALE.x_, -1.0f / BOX_SCALE.y_, 0.0f));
    }

    // Camera — positioned to see the full vertical chain
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 15.0f, -25.0f));
    cameraNode_->LookAt(Vector3(0.0f, 15.0f, 0.0f));
}

void BS_Chain::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Chain::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Chain, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Chain, HandlePostRenderUpdate));
}

void BS_Chain::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_Chain::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
