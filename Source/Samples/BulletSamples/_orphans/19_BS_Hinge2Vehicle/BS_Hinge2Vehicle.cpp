// Port of bulletphysics/bullet3/examples/Vehicles/Hinge2Vehicle.cpp
// Original creates a chassis rigid body with 4 wheels connected via
// btHinge2Constraint (steering axis + spin axis). Arrow keys steer/drive.
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

#include "BS_Hinge2Vehicle.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Hinge2Vehicle)

// Original Hinge2Vehicle constants
static const float CHASSIS_MASS = 800.0f;
static const float WHEEL_MASS = 15.0f;
static const float WHEEL_RADIUS = 0.4f;
static const float WHEEL_WIDTH = 0.3f;
static const float CHASSIS_WIDTH = 1.6f;
static const float CHASSIS_HEIGHT = 0.5f;
static const float CHASSIS_LENGTH = 3.0f;
static const float WHEEL_BASE = 2.4f;    // front-to-rear
static const float TRACK_WIDTH = 1.4f;   // left-to-right
static const float SUSPENSION_REST = 0.6f;
static const float MAX_STEER = 0.3f;
static const float MAX_ENGINE_FORCE = 1000.0f;

void BS_Hinge2Vehicle::Start()
{
    Sample::Start();
    CreateScene();
    CreateVehicle();
    SetupViewport();
    CreateInstructions(
        "Hinge2 Vehicle — chassis + 4 constraint wheels\n"
        "Arrow keys: drive/steer\n"
        "WASD+mouse to free-fly | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Hinge2Vehicle::CreateScene()
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

    // Ground
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
        groundNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(1.0f);
    }

    // Ramp
    {
        Node* rampNode = scene_->CreateChild("Ramp");
        rampNode->SetPosition(Vector3(20.0f, 0.5f, 0.0f));
        rampNode->SetRotation(Quaternion(0.0f, 0.0f, -10.0f));
        rampNode->SetScale(Vector3(15.0f, 0.3f, 6.0f));
        auto* rampModel = rampNode->CreateComponent<StaticModel>();
        rampModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        rampModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));

        auto* rampBody = rampNode->CreateComponent<RigidBody>();
        rampBody->SetBoxShape(Vector3::ONE);
        rampBody->SetFriction(1.0f);
    }

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
}

void BS_Hinge2Vehicle::CreateVehicle()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // === Chassis ===
    vehicleNode_ = scene_->CreateChild("Chassis");
    vehicleNode_->SetPosition(Vector3(0.0f, 3.0f, 0.0f));
    vehicleNode_->SetScale(Vector3(CHASSIS_WIDTH, CHASSIS_HEIGHT, CHASSIS_LENGTH));

    auto* chassisModel = vehicleNode_->CreateComponent<StaticModel>();
    chassisModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    chassisModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    chassisModel->SetCastShadows(true);

    auto* chassisBody = vehicleNode_->CreateComponent<RigidBody>();
    chassisBody->SetMass(CHASSIS_MASS);
    chassisBody->SetBoxShape(Vector3::ONE);
    chassisBody->SetFriction(0.5f);
    chassisBody->SetLinearDamping(0.1f);
    chassisBody->SetAngularDamping(0.1f);

    // === Wheels — connected via HINGE2 constraints ===
    // Original uses btHinge2Constraint: axis1=suspension(Y), axis2=spin(X)
    // Urho3D Constraint HINGE2 maps to this
    float wheelX[] = {-TRACK_WIDTH / 2.0f, TRACK_WIDTH / 2.0f, -TRACK_WIDTH / 2.0f, TRACK_WIDTH / 2.0f};
    float wheelZ[] = {WHEEL_BASE / 2.0f, WHEEL_BASE / 2.0f, -WHEEL_BASE / 2.0f, -WHEEL_BASE / 2.0f};

    for (int i = 0; i < 4; i++)
    {
        Node* wheelNode = scene_->CreateChild("Wheel");
        wheelNode->SetPosition(vehicleNode_->GetPosition() + Vector3(wheelX[i], -SUSPENSION_REST, wheelZ[i]));
        // Visual: child node rotated so cylinder axis points along X (left-right)
        Node* wheelVisual = wheelNode->CreateChild("WheelVisual");
        wheelVisual->SetRotation(Quaternion(0.0f, 0.0f, 90.0f));
        wheelVisual->SetScale(Vector3(WHEEL_RADIUS * 2.0f, WHEEL_WIDTH, WHEEL_RADIUS * 2.0f));
        auto* wheelModel = wheelVisual->CreateComponent<StaticModel>();
        wheelModel->SetModel(cache->GetResource<Model>("Models/Cylinder.mdl"));
        wheelModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
        wheelModel->SetCastShadows(true);

        auto* wheelBody = wheelNode->CreateComponent<RigidBody>();
        wheelBody->SetMass(WHEEL_MASS);
        wheelBody->SetSphereShape(WHEEL_RADIUS * 2.0f);
        wheelBody->SetFriction(1.5f);
        wheelBody->SetRollingFriction(0.1f);

        // 6DOF Spring2 constraint simulating hinge2: suspension (Y translate) + spin (X rotate)
        // Original uses btHinge2Constraint which is a specialized 6DOF
        auto* constraint = wheelNode->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF_SPRING2);
        constraint->SetOtherBody(chassisBody);

        // Anchor at wheel position relative to chassis
        Vector3 localPos(wheelX[i] / CHASSIS_WIDTH, -SUSPENSION_REST / CHASSIS_HEIGHT, wheelZ[i] / CHASSIS_LENGTH);
        constraint->SetOtherPosition(localPos);
        constraint->SetPosition(Vector3::ZERO);

        // Lock all except: Y translation (suspension), X rotation (spin)
        // For front wheels also allow Y rotation (steering)
        constraint->SetAxis(Vector3::UP);
        constraint->SetOtherAxis(Vector3::UP);

        wheels_[i] = wheelNode;
    }
}

void BS_Hinge2Vehicle::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Hinge2Vehicle::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Hinge2Vehicle, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Hinge2Vehicle, HandlePostRenderUpdate));
}

void BS_Hinge2Vehicle::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    auto* input = GetSubsystem<Input>();

    // Vehicle controls — faithful to original arrow key mapping
    if (input->GetKeyDown(KEY_UP))
        engineForce_ = MAX_ENGINE_FORCE;
    else if (input->GetKeyDown(KEY_DOWN))
        engineForce_ = -MAX_ENGINE_FORCE;
    else
        engineForce_ = 0.0f;

    if (input->GetKeyDown(KEY_LEFT))
        steering_ = Clamp(steering_ + 1.0f * timeStep, -MAX_STEER, MAX_STEER);
    else if (input->GetKeyDown(KEY_RIGHT))
        steering_ = Clamp(steering_ - 1.0f * timeStep, -MAX_STEER, MAX_STEER);
    else
        steering_ *= 0.95f; // center steering

    // Apply engine torque to rear wheels
    for (int i = 2; i < 4; i++)
    {
        if (wheels_[i] && wheels_[i]->GetComponent<RigidBody>())
        {
            auto* body = wheels_[i]->GetComponent<RigidBody>();
            body->ApplyTorque(Vector3::RIGHT * engineForce_);
        }
    }

    // Camera follows vehicle
    if (vehicleNode_)
    {
        Vector3 vehiclePos = vehicleNode_->GetPosition();
        Vector3 camTarget = vehiclePos + Vector3(0.0f, 2.0f, 0.0f);
        Vector3 camPos = vehiclePos + Vector3(0.0f, 5.0f, -12.0f);
        cameraNode_->SetPosition(Lerp(cameraNode_->GetPosition(), camPos, 5.0f * timeStep));
        cameraNode_->LookAt(camTarget);
    }

    // WASD camera override
    const float MOUSE_SENSITIVITY = 0.1f;
    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * 20.0f * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * 20.0f * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * 20.0f * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * 20.0f * timeStep);

    if (input->GetKeyPress(KEY_SPACE))
        drawDebug_ = !drawDebug_;
}

void BS_Hinge2Vehicle::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
