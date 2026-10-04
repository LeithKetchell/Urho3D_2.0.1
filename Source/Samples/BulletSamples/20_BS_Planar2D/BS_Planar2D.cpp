// Port of bulletphysics/bullet3/examples/Planar2D/Planar2D.cpp
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

#include "BS_Planar2D.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Planar2D)

static const int ARRAY_SIZE_X = 5;
static const int ARRAY_SIZE_Y = 5;

void BS_Planar2D::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Planar 2D Physics — boxes constrained to XY plane\n"
        "LinearFactor(1,1,0) AngularFactor(0,0,1)\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Planar2D::CreateScene()
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
    zone->SetFogColor(Color(0.15f, 0.15f, 0.25f));
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

    // Ground — wide flat plane in XY (the Z=0 plane is our 2D world)
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -5.0f, 0.0f));
        groundNode->SetScale(Vector3(40.0f, 1.0f, 2.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(0.8f);
    }

    // Triangular stacking of 2D-constrained boxes (original: 5x5)
    // Using 3 different visual materials to distinguish shapes
    String materials[] = {
        "Materials/StoneEnvMapSmall.xml",
        "Materials/StoneTiled.xml",
        "Materials/StoneEnvMapSmall.xml"
    };

    Vector3 startPos(-5.0f, 8.0f, 0.0f);
    Vector3 deltaX(1.0f, 2.0f, 0.0f);
    Vector3 deltaY(2.0f, 0.0f, 0.0f);

    for (int i = 0; i < ARRAY_SIZE_X; i++)
    {
        Vector3 pos = startPos + deltaX * (float)i;
        for (int j = i; j < ARRAY_SIZE_Y; j++)
        {
            Vector3 objPos = pos + Vector3(10.0f, 0.0f, 0.0f);

            Node* node = scene_->CreateChild("Block2D");
            node->SetPosition(objPos);
            node->SetScale(Vector3(1.0f, 1.0f, 0.3f));

            auto* model = node->CreateComponent<StaticModel>();
            model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
            model->SetMaterial(cache->GetResource<Material>(materials[j % 3]));
            model->SetCastShadows(true);

            auto* body = node->CreateComponent<RigidBody>();
            body->SetMass(1.0f);
            body->SetBoxShape(Vector3::ONE);
            body->SetFriction(0.5f);

            // KEY: Constrain to XY plane (original Planar2D physics)
            body->SetLinearFactor(Vector3(1.0f, 1.0f, 0.0f));
            body->SetAngularFactor(Vector3(0.0f, 0.0f, 1.0f));

            pos += deltaY;
        }
    }

    // Camera — side view looking at XY plane
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(5.0f, 5.0f, -15.0f));
    cameraNode_->LookAt(Vector3(5.0f, 5.0f, 0.0f));
}

void BS_Planar2D::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Planar2D::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Planar2D, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Planar2D, HandlePostRenderUpdate));
}

void BS_Planar2D::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_Planar2D::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
