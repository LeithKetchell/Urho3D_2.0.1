// Port of bullet3/examples/Vehicles/Hinge2Vehicle.cpp
// 4-wheeled vehicle using 6DOF Spring2 constraints (= btHinge2Constraint).
// Steering via constraint motor on Y rotation, drive via X rotation motor,
// suspension via spring on Y translation.
// Arrow keys: Up/Down = forward/reverse, Left/Right = steer.

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

#include "BS_Vehicles.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Vehicles)

// Vehicle constants from original Bullet3 Hinge2Vehicle
static const float CHASSIS_MASS = 800.0f;
static const float WHEEL_MASS = 15.0f;
static const float WHEEL_RADIUS = 0.4f;
static const float WHEEL_WIDTH = 0.3f;
static const float CHASSIS_WIDTH = 1.6f;
static const float CHASSIS_HEIGHT = 0.5f;
static const float CHASSIS_LENGTH = 3.0f;
static const float WHEEL_BASE = 2.4f;     // front-to-rear
static const float TRACK_WIDTH = 1.4f;    // left-to-right
static const float SUSPENSION_REST = 0.6f;
static const float SUSPENSION_STIFFNESS = 40.0f;  // ~pi^2 * 4
static const float SUSPENSION_DAMPING = 0.5f;
static const float SUSPENSION_TRAVEL = 0.3f;
static const float MAX_STEER = 0.3f;       // radians (~17 degrees)
static const float MAX_ENGINE_FORCE = 1000.0f;

void BS_Vehicles::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "Hinge2 Vehicle — constraint-based suspension + steering\n"
        "Arrow keys: Up=forward Down=reverse Left/Right=steer\n"
        "WASD+mouse | C=cycle camera | Space=debug draw"
    );
    SetupViewport();
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);

    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);

    // Start in chase camera mode following the chassis
    SetCameraTarget(chassisNode_);
    SetChaseDistance(12.0f);
    SetChaseHeight(4.0f);
    SetCameraMode(CAM_CHASE);
}

void BS_Vehicles::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();
    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));
    physicsWorld->SetNumIterations(20);

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.3f, 0.3f, 0.3f));
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(0.8f, 0.8f, 0.8f));
    light->SetCastShadows(true);

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    groundNode->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundNode->GetComponent<StaticModel>()->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    groundNode->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);
    groundBody->SetFriction(1.0f);

    // Ramp
    Node* ramp = scene_->CreateChild("Ramp");
    ramp->SetPosition(Vector3(20.0f, 0.5f, 0.0f));
    ramp->SetRotation(Quaternion(0.0f, 0.0f, -10.0f));
    ramp->SetScale(Vector3(15.0f, 0.3f, 6.0f));
    ramp->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    ramp->GetComponent<StaticModel>()->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
    auto* rampBody = ramp->CreateComponent<RigidBody>();
    ramp->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);
    rampBody->SetFriction(1.0f);

    CreateVehicle(Vector3(0.0f, 3.0f, 0.0f));

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 5.0f, -15.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));
}

void BS_Vehicles::CreateVehicle(const Vector3& position)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* cylinderMdl = cache->GetResource<Model>("Models/Cylinder.mdl");

    SharedPtr<Material> bodyMat = cache->GetResource<Material>("Materials/StoneSmall.xml")->Clone();
    bodyMat->SetShaderParameter("MatDiffColor", Variant(Color(0.2f, 0.4f, 0.8f)));
    SharedPtr<Material> wheelMat = cache->GetResource<Material>("Materials/StoneSmall.xml")->Clone();
    wheelMat->SetShaderParameter("MatDiffColor", Variant(Color(0.2f, 0.2f, 0.2f)));

    // === Chassis ===
    // Node at scale 1 — visual is a scaled child, physics shape sized explicitly
    chassisNode_ = scene_->CreateChild("Chassis");
    chassisNode_->SetPosition(position);

    Node* chassisVisual = chassisNode_->CreateChild("ChassisVisual");
    chassisVisual->SetScale(Vector3(CHASSIS_WIDTH, CHASSIS_HEIGHT, CHASSIS_LENGTH));
    chassisVisual->CreateComponent<StaticModel>()->SetModel(boxMdl);
    chassisVisual->GetComponent<StaticModel>()->SetMaterial(bodyMat);
    chassisVisual->GetComponent<StaticModel>()->SetCastShadows(true);

    auto* chassisBody = chassisNode_->CreateComponent<RigidBody>();
    chassisBody->SetMass(CHASSIS_MASS);
    chassisNode_->CreateComponent<CollisionShape>()->SetBox(Vector3(CHASSIS_WIDTH, CHASSIS_HEIGHT, CHASSIS_LENGTH));
    chassisBody->SetFriction(0.5f);
    chassisBody->SetLinearDamping(0.1f);
    chassisBody->SetAngularDamping(0.1f);

    // === Wheels ===
    // Wheel positions relative to chassis center
    float wheelX[] = {-TRACK_WIDTH / 2.0f, TRACK_WIDTH / 2.0f, -TRACK_WIDTH / 2.0f, TRACK_WIDTH / 2.0f};
    float wheelZ[] = {WHEEL_BASE / 2.0f, WHEEL_BASE / 2.0f, -WHEEL_BASE / 2.0f, -WHEEL_BASE / 2.0f};

    for (int i = 0; i < 4; ++i)
    {
        wheels_[i] = scene_->CreateChild("Wheel");
        Vector3 wheelPos = position + Vector3(wheelX[i], -SUSPENSION_REST, wheelZ[i]);
        wheels_[i]->SetPosition(wheelPos);

        // Visual: cylinder rotated so axis points along X (left-right)
        Node* wheelVisual = wheels_[i]->CreateChild("WheelVisual");
        wheelVisual->SetRotation(Quaternion(0.0f, 0.0f, 90.0f));
        wheelVisual->SetScale(Vector3(WHEEL_RADIUS * 2.0f, WHEEL_WIDTH, WHEEL_RADIUS * 2.0f));
        wheelVisual->CreateComponent<StaticModel>()->SetModel(cylinderMdl);
        wheelVisual->GetComponent<StaticModel>()->SetMaterial(wheelMat);
        wheelVisual->GetComponent<StaticModel>()->SetCastShadows(true);

        auto* wheelBody = wheels_[i]->CreateComponent<RigidBody>();
        wheelBody->SetMass(WHEEL_MASS);
        // Sphere collision — btCylinderShapeX not exposed, sphere rolls correctly
        wheels_[i]->CreateComponent<CollisionShape>()->SetSphere(WHEEL_RADIUS * 2.0f);
        wheelBody->SetFriction(1.5f);
        wheelBody->SetRollingFriction(0.1f);

        // === 6DOF Spring2 constraint (= Hinge2) ===
        // btHinge2Constraint axes: axis1=suspension(Y), axis2=spin(X)
        // 6DOF Spring2 DOFs:
        //   Linear 0(X)=locked, 1(Y)=suspension spring, 2(Z)=locked
        //   Angular 3(X)=wheel spin (free), 4(Y)=steering (front) or locked (rear), 5(Z)=locked
        auto* constraint = chassisNode_->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF_SPRING2);
        constraint->SetOtherBody(wheelBody);
        constraint->SetWorldPosition(wheelPos);
        constraint->SetAxis(Vector3::UP);
        constraint->SetOtherAxis(Vector3::UP);

        // SetAxis(UP) maps constraint frame X → UP, so:
        //   Index 0 (linear X)  = UP       → suspension travel
        //   Index 1 (linear Y)  = LEFT     → locked
        //   Index 2 (linear Z)  = FORWARD  → locked
        //   Index 3 (angular X) = around UP      → steering
        //   Index 4 (angular Y) = around LEFT    → wheel spin (axle)
        //   Index 5 (angular Z) = around FORWARD → locked

        // Linear limits: only X allowed (suspension travel along UP)
        constraint->SetLinearLowerLimit(Vector3(-SUSPENSION_TRAVEL, 0.0f, 0.0f));
        constraint->SetLinearUpperLimit(Vector3(SUSPENSION_TRAVEL, 0.0f, 0.0f));

        // Angular limits
        if (i < 2)
        {
            // Front wheels: X=steering range, Y=free spin, Z=locked
            constraint->SetAngularLowerLimit(Vector3(-MAX_STEER * M_RADTODEG, -180.0f, 0.0f));
            constraint->SetAngularUpperLimit(Vector3(MAX_STEER * M_RADTODEG, 180.0f, 0.0f));
        }
        else
        {
            // Rear wheels: X=locked, Y=free spin, Z=locked
            constraint->SetAngularLowerLimit(Vector3(0.0f, -180.0f, 0.0f));
            constraint->SetAngularUpperLimit(Vector3(0.0f, 180.0f, 0.0f));
        }

        // Suspension spring on linear X (index 0) — along UP axis
        constraint->EnableSpring(0, true);
        constraint->SetSpringStiffness(0, SUSPENSION_STIFFNESS);
        constraint->SetSpringDamping(0, SUSPENSION_DAMPING);

        // Drive motor on angular Y (index 4) — wheel spin around axle
        constraint->EnableMotor(4, true);
        constraint->SetMotorTargetVelocity(4, 0.0f);
        constraint->SetMotorMaxForce(4, MAX_ENGINE_FORCE);

        // Steering motor on angular X (index 3) — front wheels only
        if (i < 2)
        {
            constraint->EnableMotor(3, true);
            constraint->SetMotorTargetVelocity(3, 0.0f);
            constraint->SetMotorMaxForce(3, 100.0f);
        }

        wheelConstraints_[i] = constraint;
    }
}

void BS_Vehicles::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Vehicles::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Vehicles, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Vehicles, HandlePostRenderUpdate));
}

void BS_Vehicles::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    auto* input = GetSubsystem<Input>();

    // Engine force — drive all 4 wheels
    float engineVelocity = 0.0f;
    if (input->GetKeyDown(KEY_UP))
        engineVelocity = 30.0f;
    else if (input->GetKeyDown(KEY_DOWN))
        engineVelocity = -30.0f;

    // Drive rear wheels only (indices 2, 3) — motor on index 4 (angular Y = wheel spin)
    for (int i = 2; i < 4; ++i)
    {
        if (wheelConstraints_[i])
        {
            wheelConstraints_[i]->SetMotorTargetVelocity(4, engineVelocity);
            wheelConstraints_[i]->SetMotorMaxForce(4, MAX_ENGINE_FORCE);
        }
    }

    // Steering — gradual input, auto-center
    if (input->GetKeyDown(KEY_LEFT))
        steeringAngle_ = Clamp(steeringAngle_ + 1.0f * timeStep, -MAX_STEER, MAX_STEER);
    else if (input->GetKeyDown(KEY_RIGHT))
        steeringAngle_ = Clamp(steeringAngle_ - 1.0f * timeStep, -MAX_STEER, MAX_STEER);
    else
        steeringAngle_ *= 0.9f;  // auto-center

    // Apply steering to front wheel constraints (indices 0, 1) — motor on index 3 (angular X = around UP)
    for (int i = 0; i < 2; ++i)
    {
        if (wheelConstraints_[i])
        {
            float steerVelocity = (steeringAngle_ * M_RADTODEG) * 5.0f;
            wheelConstraints_[i]->SetMotorTargetVelocity(3, steerVelocity);
        }
    }

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_Vehicles::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
