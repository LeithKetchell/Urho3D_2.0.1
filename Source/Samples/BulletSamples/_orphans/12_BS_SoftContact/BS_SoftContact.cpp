// Port of bulletphysics/bullet3/examples/RigidBody/RigidBodySoftContact.cpp
// Copyright (c) Google Inc / Erwin Coumans, zlib license

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

#include "BS_SoftContact.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_SoftContact)

// Original solver settings from Bullet demo
// m_erp2 = 0, m_globalCfm = 0, numIterations = 3, SOLVER_SIMD, no splitImpulse
// Ground: contactStiffnessAndDamping(300, 10)
// Single compound sphere (mass=1) with slight rotation, dropped from y=3

void BS_SoftContact::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Bullet RigidBodySoftContact\n"
        "Ground has soft contact: stiffness=300, damping=10\n"
        "Watch the compliant bounce vs rigid contact\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_SoftContact::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();

    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));
    // Original: 3 solver iterations, no split impulse
    physicsWorld->SetNumIterations(3);
    physicsWorld->SetSplitImpulse(false);

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

    // === Soft contact ground ===
    // Original: btBoxShape(50,50,50) at y=-50, stiffness=300, damping=10
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
        groundNode->SetScale(Vector3(100.0f, 100.0f, 100.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(0.5f);
        // Soft contact: stiffness=300, damping=10 (from original)
        groundBody->SetContactStiffnessAndDamping(300.0f, 10.0f);
    }

    // === Dynamic sphere with slight rotation ===
    // Original: compound(sphere r=0.5), mass=1, rotation(1,1,1, PI/10), at y=3
    // We drop multiple to make it more interesting (original only has 1x1x1 array)
    {
        // Drop a grid of spheres to show the soft bounce
        for (int k = 0; k < 3; k++)
        {
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    float x = 2.0f * (float)i + 0.1f;
                    float y = 3.0f + 2.0f * (float)k;
                    float z = 2.0f * (float)j;

                    Node* node = scene_->CreateChild("Sphere");
                    node->SetPosition(Vector3(x, y, z));
                    // Original: slight rotation Quaternion(Vector3(1,1,1), PI/10)
                    node->SetRotation(Quaternion(18.0f, Vector3(1.0f, 1.0f, 1.0f).Normalized()));

                    auto* model = node->CreateComponent<StaticModel>();
                    model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
                    model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
                    model->SetCastShadows(true);

                    auto* body = node->CreateComponent<RigidBody>();
                    body->SetMass(1.0f);
                    // Original: compound with sphere child, r=0.5
                    body->SetSphereShape(1.0f);
                    body->SetFriction(0.5f);
                }
            }
        }
    }

    // Camera — original: dist=3, pitch=-35, yaw=52, target(0, 0.46, 0)
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(3.0f, 5.0f, -8.0f));
    cameraNode_->LookAt(Vector3(3.0f, 0.0f, 3.0f));
}

void BS_SoftContact::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_SoftContact::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_SoftContact, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_SoftContact, HandlePostRenderUpdate));
}

void BS_SoftContact::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_SoftContact::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
