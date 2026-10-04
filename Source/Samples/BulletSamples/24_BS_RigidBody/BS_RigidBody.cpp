// Port of bullet3/examples/RigidBody/KinematicRigidBodyExample.cpp
// + bullet3/examples/RigidBody/RigidBodySoftContact.cpp
//
// Left side: kinematic body sweeping back and forth, pushing dynamic boxes.
// Right side: soft-contact boxes (high damping, low restitution) stacked.

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
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_RigidBody.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_RigidBody)

void BS_RigidBody::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "RigidBody — kinematic pusher + soft contact stacking\n"
        "Left: kinematic body sweeps through boxes\n"
        "Right: soft-contact stacked boxes\n"
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

void BS_RigidBody::CreateScene()
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
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(0.8f, 0.8f, 0.8f));

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    // --- Left side: Kinematic pusher + dynamic boxes ---
    // Kinematic body
    kinematicNode_ = scene_->CreateChild("Kinematic");
    kinematicNode_->SetPosition(Vector3(-10.0f, 1.0f, 0.0f));
    kinematicNode_->SetScale(Vector3(2.0f, 2.0f, 8.0f));
    kinematicNode_->CreateComponent<StaticModel>()->SetModel(boxMdl);
    SharedPtr<Material> kinMat = mat->Clone();
    kinMat->SetShaderParameter("MatDiffColor", Variant(Color(0.8f, 0.2f, 0.2f)));
    kinematicNode_->GetComponent<StaticModel>()->SetMaterial(kinMat.Get());
    auto* kinBody = kinematicNode_->CreateComponent<RigidBody>();
    kinBody->SetKinematic(true);
    kinBody->SetMass(0.0f);
    kinematicNode_->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

    // Dynamic boxes in the path
    for (int i = 0; i < 20; ++i)
    {
        Node* box = scene_->CreateChild("DynBox");
        box->SetPosition(Vector3(-5.0f + (i % 5) * 1.2f, 0.5f + (i / 5) * 1.2f, (i % 3 - 1) * 1.5f));
        box->SetScale(1.0f);
        box->CreateComponent<StaticModel>()->SetModel(boxMdl);
        box->GetComponent<StaticModel>()->SetMaterial(mat);
        auto* body = box->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        box->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);
    }

    // --- Right side: Soft-contact stacked boxes ---
    SharedPtr<Material> softMat = mat->Clone();
    softMat->SetShaderParameter("MatDiffColor", Variant(Color(0.2f, 0.6f, 0.8f)));

    for (int i = 0; i < 10; ++i)
    {
        Node* box = scene_->CreateChild("SoftBox");
        box->SetPosition(Vector3(8.0f, 0.5f + i * 1.05f, 0.0f));
        box->SetScale(1.0f);
        box->CreateComponent<StaticModel>()->SetModel(boxMdl);
        box->GetComponent<StaticModel>()->SetMaterial(softMat);
        auto* body = box->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetLinearDamping(0.2f);
    body->SetAngularDamping(0.5f);
        body->SetRestitution(0.0f);
        body->SetFriction(1.0f);
        body->SetContactProcessingThreshold(0.0f);
        box->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);
    }

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 10.0f, -20.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

void BS_RigidBody::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_RigidBody::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_RigidBody, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_RigidBody, HandlePostRenderUpdate));
}

void BS_RigidBody::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    // Animate kinematic body — sweep back and forth
    kinematicTime_ += timeStep;
    if (kinematicNode_)
    {
        float x = sinf(kinematicTime_ * 0.5f) * 8.0f - 2.0f;
        kinematicNode_->SetPosition(Vector3(x, 1.0f, 0.0f));
    }

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_RigidBody::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
