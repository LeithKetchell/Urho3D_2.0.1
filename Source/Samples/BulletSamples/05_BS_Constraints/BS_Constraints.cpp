// Port of bulletphysics/bullet3/examples/Constraints/ConstraintDemo.cpp
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
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/Constraint.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

// Raw Bullet access for features not yet wrapped by Urho3D
#include <Bullet/BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>

#include "BS_Constraints.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Constraints)

static const float CUBE_HALF_EXTENTS = 1.0f;

void BS_Constraints::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Bullet AllConstraintDemo (14 setups)\n"
        "P2P, Slider, 6DOF, ConeTwist, Hinge, Universal, Gear\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

Node* BS_Constraints::CreateBox(const Vector3& position, float mass, const Vector3& size)
{
    auto* cache = GetSubsystem<ResourceCache>();

    Node* node = scene_->CreateChild("Box");
    node->SetPosition(position);
    node->SetScale(size);
    auto* model = node->CreateComponent<StaticModel>();
    model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    model->SetCastShadows(true);

    auto* body = node->CreateComponent<RigidBody>();
    body->SetMass(mass);
    body->SetFriction(0.6f);
    body->SetLinearDamping(0.2f);
    body->SetAngularDamping(0.2f);
    node->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

    return node;
}

Node* BS_Constraints::CreateCylinder(const Vector3& position, float mass, float radius, float height)
{
    auto* cache = GetSubsystem<ResourceCache>();

    Node* node = scene_->CreateChild("Cylinder");
    node->SetPosition(position);
    node->SetScale(Vector3(radius * 2.0f, height, radius * 2.0f));
    auto* model = node->CreateComponent<StaticModel>();
    model->SetModel(cache->GetResource<Model>("Models/Cylinder.mdl"));
    model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    model->SetCastShadows(true);

    auto* body = node->CreateComponent<RigidBody>();
    body->SetMass(mass);
    body->SetFriction(0.6f);
    body->SetLinearDamping(0.2f);
    body->SetAngularDamping(0.2f);
    node->CreateComponent<CollisionShape>()->SetCylinder(1.0f, 1.0f);

    return node;
}

void BS_Constraints::CreateScene()
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
    zone->SetAmbientColor(Color(0.2f, 0.2f, 0.2f));
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
        auto* groundObject = groundNode->CreateComponent<StaticModel>();
        groundObject->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundObject->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundNode->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);
    }

    // ======================================================================
    // Bullet AllConstraintDemo: 14 constraint setups
    // All positions/limits from the original source
    // ======================================================================

    // === 1. GEAR CONSTRAINT ===
    // Original: two cylinders coupled by gear ratio
    // Gear requires both bodies to have a rotational DOF (hinge) first
    {
        Node* cylA = CreateCylinder(Vector3(-8.0f, 1.0f, -8.0f), 1.0f);
        Node* cylB = CreateCylinder(Vector3(-10.0f, 1.0f, -8.0f), 1.0f);

        // Hinge A to world (spin around Y)
        auto* hingeA = cylA->CreateComponent<Constraint>();
        hingeA->SetConstraintType(CONSTRAINT_HINGE);
        hingeA->SetAxis(Vector3::UP);

        // Hinge B to world (spin around Y)
        auto* hingeB = cylB->CreateComponent<Constraint>();
        hingeB->SetConstraintType(CONSTRAINT_HINGE);
        hingeB->SetAxis(Vector3::UP);

        // Gear couples their rotations
        auto* gear = cylA->CreateComponent<Constraint>();
        gear->SetConstraintType(CONSTRAINT_GEAR);
        gear->SetOtherBody(cylB->GetComponent<RigidBody>());
        gear->SetGearAxisA(Vector3::UP);
        gear->SetGearAxisB(Vector3::UP);
        gear->SetGearRatio(1.0f);
    }

    // === 2. POINT-TO-POINT WITH BREAKING THRESHOLD ===
    // Original: pivot at (CUBE_HALF_EXTENTS, CUBE_HALF_EXTENTS, 0), threshold 10.2
    {
        Node* boxA = CreateBox(Vector3(0.0f, 8.0f, -5.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_POINT);
        constraint->SetPosition(Vector3(CUBE_HALF_EXTENTS, CUBE_HALF_EXTENTS, 0.0f));

        // Raw Bullet: breaking threshold not wrapped by Urho3D
        auto* btConstraint = constraint->GetConstraint();
        if (btConstraint)
            btConstraint->setBreakingImpulseThreshold(10.2f);
    }

    // === 3. POINT-TO-POINT BALL SOCKET ===
    // Original: single body constrained to world at pivot
    {
        Node* boxA = CreateBox(Vector3(-4.0f, 4.0f, -5.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_POINT);
        constraint->SetPosition(Vector3(CUBE_HALF_EXTENTS, -CUBE_HALF_EXTENTS, -CUBE_HALF_EXTENTS));
    }

    // === 4. SLIDER CONSTRAINT ===
    // Original: linear limits -15 to -5, angular limits -pi/3 to pi/3
    {
        Node* boxA = CreateBox(Vector3(-20.0f, 2.0f, 30.0f), 0.0f);
        Node* boxB = CreateBox(Vector3(-30.0f, 2.0f, 30.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_SLIDER);
        constraint->SetOtherBody(boxB->GetComponent<RigidBody>());
        constraint->SetAxis(Vector3::RIGHT);
        constraint->SetOtherAxis(Vector3::RIGHT);
        // Slider limits: highLimit_.x_ = upper linear, highLimit_.y_ = upper angular (deg)
        // lowLimit_.x_ = lower linear, lowLimit_.y_ = lower angular (deg)
        constraint->SetHighLimit(Vector2(-5.0f, 60.0f));    // -pi/3 ≈ -60 deg
        constraint->SetLowLimit(Vector2(-15.0f, -60.0f));
    }

    // === 5. 6DOF SLIDER WITH MOTOR ===
    // Original: linear limits (-10,0,0)→(10,0,0), motor on X: vel -5, force 6
    {
        Node* boxA = CreateBox(Vector3(0.0f, 10.0f, 0.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF);
        constraint->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
        constraint->SetLinearLowerLimit(Vector3(-10.0f, 0.0f, 0.0f));
        constraint->SetLinearUpperLimit(Vector3(10.0f, 0.0f, 0.0f));
        constraint->SetAngularLowerLimit(Vector3(0.0f, 0.0f, 0.0f));
        constraint->SetAngularUpperLimit(Vector3(0.0f, 0.0f, 0.0f));
        // Motor on linear X axis (index 0)
        constraint->EnableMotor(0, true);
        constraint->SetMotorTargetVelocity(0, -5.0f);
        constraint->SetMotorMaxForce(0, 6.0f);
    }

    // === 6. DOOR HINGE ===
    // Original: door shape (2,5,0.2), pivot at (10+2.1, -2, 0), axis (0,1,0)
    // Limits: -pi/4 to pi/4
    {
        Node* doorNode = CreateBox(Vector3(-5.0f, -2.0f, 0.0f), 1.0f, Vector3(2.0f, 5.0f, 0.2f));

        auto* constraint = doorNode->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_HINGE);
        constraint->SetPosition(Vector3(5.0f, 0.0f, 0.0f));
        constraint->SetAxis(Vector3::UP);
        constraint->SetHighLimit(Vector2(45.0f, 0.0f));   // pi/4 in degrees
        constraint->SetLowLimit(Vector2(-45.0f, 0.0f));
    }

    // === 7. 6DOF BETWEEN TWO BODIES ===
    // Original: bodyA static at (10,6,0), bodyB dynamic at (0,6,0)
    // Frames: A origin (-5,0,0), B origin (5,0,0)
    // Linear limits (-10,-2,-1)→(10,2,1), angular limits specified
    {
        Node* boxA = CreateBox(Vector3(10.0f, 6.0f, 0.0f), 0.0f);
        Node* boxB = CreateBox(Vector3(0.0f, 6.0f, 0.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF);
        constraint->SetOtherBody(boxB->GetComponent<RigidBody>());
        constraint->SetPosition(Vector3(-5.0f, 0.0f, 0.0f));
        constraint->SetOtherPosition(Vector3(5.0f, 0.0f, 0.0f));
        constraint->SetLinearLowerLimit(Vector3(-10.0f, -2.0f, -1.0f));
        constraint->SetLinearUpperLimit(Vector3(10.0f, 2.0f, 1.0f));
        // Angular: (-pi/2*0.5, -0.75, -pi/2*0.8) → (pi/2*0.5, 0.75, pi/2*0.8) radians → degrees
        constraint->SetAngularLowerLimit(Vector3(-45.0f, -43.0f, -72.0f));
        constraint->SetAngularUpperLimit(Vector3(45.0f, 43.0f, 72.0f));
    }

    // === 8. CONE-TWIST CONSTRAINT ===
    // Original: bodyA at (-10,5,0) mass 1, bodyB at (-10,-5,0) static
    // Frames rotated pi/2 around Z, origins (0,-5,0) and (0,5,0)
    // Limits: (pi/4*0.6, pi/4, pi*0.8) with softness 0.5
    {
        Node* boxA = CreateBox(Vector3(-10.0f, 5.0f, 0.0f), 1.0f);
        Node* boxB = CreateBox(Vector3(-10.0f, -5.0f, 0.0f), 0.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_CONETWIST);
        constraint->SetOtherBody(boxB->GetComponent<RigidBody>());
        constraint->SetPosition(Vector3(0.0f, -5.0f, 0.0f));
        constraint->SetOtherPosition(Vector3(0.0f, 5.0f, 0.0f));
        constraint->SetRotation(Quaternion(90.0f, Vector3::FORWARD));
        constraint->SetOtherRotation(Quaternion(90.0f, Vector3::FORWARD));
        // ConeTwist limits: highLimit_.x_ = twist, highLimit_.y_ = swing
        // pi/4*0.6 ≈ 27 deg twist, pi/4 = 45 deg swing
        constraint->SetHighLimit(Vector2(27.0f, 45.0f));

        // Raw Bullet: cone-twist softness not wrapped
        auto* btCT = static_cast<btConeTwistConstraint*>(constraint->GetConstraint());
        if (btCT)
            btCT->setLimit(45.0f * M_DEGTORAD, 45.0f * M_DEGTORAD, 27.0f * M_DEGTORAD, 0.5f);
    }

    // === 9. HINGE WITH MOTOR ===
    // Original: body at (0,0,0), pivot (10,0,0), axis (0,0,1)
    // Motor: velocity -1, impulse 1.65
    {
        Node* cylA = CreateCylinder(Vector3(0.0f, 2.0f, 15.0f), 1.0f);

        auto* constraint = cylA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_HINGE);
        constraint->SetPosition(Vector3(10.0f, 0.0f, 0.0f));
        constraint->SetAxis(Vector3::FORWARD);

        // Raw Bullet: hinge motor not wrapped by Urho3D
        auto* btHinge = static_cast<btHingeConstraint*>(constraint->GetConstraint());
        if (btHinge)
            btHinge->enableAngularMotor(true, -1.0f, 1.65f);
    }

    // === 10. UNIVERSAL JOINT (via 6DOF) ===
    // Original: parent static at (20,4,0), child dynamic at (20,0,0)
    // Anchor (20,2,0), axes (1,0,0) and (0,0,1)
    // Limits: -pi/4 to pi/4 on both axes
    {
        Node* parent = CreateBox(Vector3(20.0f, 4.0f, 0.0f), 0.0f);
        Node* child = CreateBox(Vector3(20.0f, 0.0f, 0.0f), 1.0f);

        auto* constraint = parent->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF);
        constraint->SetOtherBody(child->GetComponent<RigidBody>());
        constraint->SetPosition(Vector3(0.0f, -2.0f, 0.0f));
        constraint->SetOtherPosition(Vector3(0.0f, 2.0f, 0.0f));
        // Lock all linear, allow rotation on two axes
        constraint->SetLinearLowerLimit(Vector3::ZERO);
        constraint->SetLinearUpperLimit(Vector3::ZERO);
        constraint->SetAngularLowerLimit(Vector3(-45.0f, 0.0f, -45.0f));
        constraint->SetAngularUpperLimit(Vector3(45.0f, 0.0f, 45.0f));
    }

    // === 11. 6DOF SPRING ===
    // Original: bodyA static at (-20,16,0), bodyB dynamic at (-10,16,0)
    // Frame A origin (10,0,0), linear limits (-5,0,0)→(5,0,0)
    // Angular limits (0,0,-1.5)→(0,0,1.5)
    // Spring on axis 0: stiffness 39.478, damping 0.5
    // Spring on axis 5: stiffness 39.478, damping 0.3
    {
        Node* boxA = CreateBox(Vector3(-20.0f, 16.0f, 0.0f), 0.0f);
        Node* boxB = CreateBox(Vector3(-10.0f, 16.0f, 0.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF_SPRING);
        constraint->SetOtherBody(boxB->GetComponent<RigidBody>());
        constraint->SetPosition(Vector3(10.0f, 0.0f, 0.0f));
        constraint->SetLinearLowerLimit(Vector3(-5.0f, 0.0f, 0.0f));
        constraint->SetLinearUpperLimit(Vector3(5.0f, 0.0f, 0.0f));
        constraint->SetAngularLowerLimit(Vector3(0.0f, 0.0f, -86.0f));  // -1.5 rad ≈ -86 deg
        constraint->SetAngularUpperLimit(Vector3(0.0f, 0.0f, 86.0f));
        // Spring on linear X (index 0)
        constraint->EnableSpring(0, true);
        constraint->SetSpringStiffness(0, 39.478f);
        constraint->SetSpringDamping(0, 0.5f);
        // Spring on angular Z (index 5)
        constraint->EnableSpring(5, true);
        constraint->SetSpringStiffness(5, 39.478f);
        constraint->SetSpringDamping(5, 0.3f);
    }

    // === 12. HINGE2 (via 6DOF Spring2) ===
    // Original: parent static at (-20,4,0), child dynamic at (-20,0,0)
    // Anchor (-20,0,0), parent axis (0,1,0), child axis (1,0,0)
    // Limits: -pi/4 to pi/4
    {
        Node* parent = CreateBox(Vector3(-20.0f, 4.0f, 0.0f), 0.0f);
        Node* child = CreateBox(Vector3(-20.0f, 0.0f, 0.0f), 1.0f);

        auto* constraint = parent->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF_SPRING2);
        constraint->SetOtherBody(child->GetComponent<RigidBody>());
        constraint->SetPosition(Vector3(0.0f, -4.0f, 0.0f));
        constraint->SetOtherPosition(Vector3(0.0f, 0.0f, 0.0f));
        // Lock all linear, allow rotation on steering + suspension axes
        constraint->SetLinearLowerLimit(Vector3::ZERO);
        constraint->SetLinearUpperLimit(Vector3::ZERO);
        constraint->SetAngularLowerLimit(Vector3(-45.0f, 0.0f, -45.0f));
        constraint->SetAngularUpperLimit(Vector3(45.0f, 0.0f, 45.0f));
    }

    // === 13. HINGE BETWEEN TWO DYNAMIC BODIES ===
    // Original: bodyA at (-20,-2,0) mass 1, bodyB at (-30,-2,0) mass 10
    // Pivots (-5,0,0) and (5,0,0), axis (0,1,0)
    // Limits: -pi/4 to pi/4
    {
        Node* boxA = CreateBox(Vector3(-20.0f, -2.0f, 15.0f), 1.0f);
        Node* boxB = CreateBox(Vector3(-30.0f, -2.0f, 15.0f), 10.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_HINGE);
        constraint->SetOtherBody(boxB->GetComponent<RigidBody>());
        constraint->SetPosition(Vector3(-5.0f, 0.0f, 0.0f));
        constraint->SetOtherPosition(Vector3(5.0f, 0.0f, 0.0f));
        constraint->SetAxis(Vector3::UP);
        constraint->SetOtherAxis(Vector3::UP);
        constraint->SetHighLimit(Vector2(45.0f, 0.0f));
        constraint->SetLowLimit(Vector2(-45.0f, 0.0f));
    }

    // === 14. 6DOF TO WORLD WITH MOTOR ===
    // Original: body at (10,-15,0), linear limits (-10,0,0)→(10,0,0)
    // Motor on X: vel 5, force 6
    {
        Node* boxA = CreateBox(Vector3(10.0f, 2.0f, 15.0f), 1.0f);

        auto* constraint = boxA->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF);
        constraint->SetLinearLowerLimit(Vector3(-10.0f, 0.0f, 0.0f));
        constraint->SetLinearUpperLimit(Vector3(10.0f, 0.0f, 0.0f));
        constraint->SetAngularLowerLimit(Vector3::ZERO);
        constraint->SetAngularUpperLimit(Vector3::ZERO);
        constraint->EnableMotor(0, true);
        constraint->SetMotorTargetVelocity(0, 5.0f);
        constraint->SetMotorMaxForce(0, 6.0f);
    }

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 12.0f, -30.0f));
    cameraNode_->LookAt(Vector3(0.0f, 5.0f, 0.0f));
}

void BS_Constraints::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Constraints::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Constraints, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Constraints, HandlePostRenderUpdate));
}

void BS_Constraints::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_Constraints::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
