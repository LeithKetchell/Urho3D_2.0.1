// Port of bullet3/examples/BulletRobotics/BoxStack.cpp
// Original: 8 cubes with exponentially increasing masses stacked vertically.
// Mass sequence: 1, 4, 16, 64, 256, 1024, 4096, 16384
// Also includes FixJointBoxes concept: pairs of boxes connected by fixed constraints.

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
#include <Urho3D/Physics/Constraint.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_BulletRobotics.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_BulletRobotics)

void BS_BulletRobotics::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "BulletRobotics BoxStack — 8 cubes, mass x4 each level\n"
        "Plus fixed-joint box pairs on the right\n"
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

void BS_BulletRobotics::CreateScene()
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
    light->SetSpecularIntensity(0.5f);

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

    // --- BoxStack: 8 cubes with exponentially increasing mass ---
    // Original: mass starts at 1, multiplied by 4 each level
    // Positioned at increasing Z (we use Y for Urho3D up-axis)
    float cubeSize = 1.0f;
    float mass = 1.0f;
    Color colors[] = {
        Color(0.8f, 0.2f, 0.2f), Color(0.2f, 0.8f, 0.2f),
        Color(0.2f, 0.2f, 0.8f), Color(0.8f, 0.8f, 0.2f),
        Color(0.8f, 0.2f, 0.8f), Color(0.2f, 0.8f, 0.8f),
        Color(0.6f, 0.4f, 0.2f), Color(0.8f, 0.8f, 0.8f)
    };

    for (int i = 0; i < 8; ++i)
    {
        Node* node = scene_->CreateChild("StackCube");
        node->SetPosition(Vector3(-3.0f, cubeSize * 0.5f + i * (cubeSize + 0.05f), 0.0f));
        node->SetScale(cubeSize);

        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(boxMdl);
        // Create colored material via clone
        auto* baseMat = cache->GetResource<Material>("Materials/StoneSmall.xml");
        SharedPtr<Material> mat = baseMat->Clone();
        mat->SetShaderParameter("MatDiffColor", Variant(colors[i]));
        model->SetMaterial(mat);
        model->SetCastShadows(true);

        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(mass);
        auto* shape = node->CreateComponent<CollisionShape>();
        shape->SetBox(Vector3::ONE);

        mass *= 2.0f;
    }

    // --- FixJointBoxes: pairs of boxes connected by fixed constraints ---
    for (int i = 0; i < 4; ++i)
    {
        // Box A
        Node* boxA = scene_->CreateChild("FixJointA");
        boxA->SetPosition(Vector3(5.0f, 1.0f + i * 3.0f, 0.0f));
        boxA->SetScale(cubeSize);
        auto* modelA = boxA->CreateComponent<StaticModel>();
        modelA->SetModel(boxMdl);
        modelA->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
        auto* bodyA = boxA->CreateComponent<RigidBody>();
        bodyA->SetMass(1.0f);
        boxA->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

        // Box B
        Node* boxB = scene_->CreateChild("FixJointB");
        boxB->SetPosition(Vector3(6.5f, 1.0f + i * 3.0f, 0.0f));
        boxB->SetScale(cubeSize);
        auto* modelB = boxB->CreateComponent<StaticModel>();
        modelB->SetModel(boxMdl);
        modelB->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
        auto* bodyB = boxB->CreateComponent<RigidBody>();
        bodyB->SetMass(1.0f);
        boxB->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

        // Fixed constraint between A and B (6DOF with all limits locked to zero)
        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF);
        constraint->SetOtherBody(bodyB);
        constraint->SetWorldPosition((boxA->GetPosition() + boxB->GetPosition()) * 0.5f);
        constraint->SetLinearLowerLimit(Vector3::ZERO);
        constraint->SetLinearUpperLimit(Vector3::ZERO);
        constraint->SetAngularLowerLimit(Vector3::ZERO);
        constraint->SetAngularUpperLimit(Vector3::ZERO);
    }

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(2.0f, 8.0f, -15.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));
}

void BS_BulletRobotics::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_BulletRobotics::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_BulletRobotics, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_BulletRobotics, HandlePostRenderUpdate));
}

void BS_BulletRobotics::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_BulletRobotics::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
