// Port of bulletphysics/bullet3/examples/DynamicControlDemo/MotorDemo.cpp
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
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include <Bullet/BulletDynamics/ConstraintSolver/btHingeConstraint.h>

#include "BS_MotorDemo.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_MotorDemo)

static const int NUM_LEGS = 6;
static const float BODY_RADIUS = 0.25f;
static const float LEG_LENGTH = 0.45f;
static const float FORELEG_LENGTH = 0.75f;
static const float BODY_HEIGHT = 0.5f;

void BS_MotorDemo::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Motor Demo — 6-legged walking spider\n"
        "Two rigs: dynamic (right) and fixed (left)\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_MotorDemo::CreateScene()
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

    // Ground
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
        groundNode->SetScale(Vector3(50.0f, 1.0f, 50.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(0.8f);
    }

    // Spawn two test rigs — one dynamic, one fixed (as in original)
    SpawnTestRig(Vector3(1.0f, 0.5f, 0.0f), false);
    SpawnTestRig(Vector3(-2.0f, 0.5f, 0.0f), true);

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 3.0f, -8.0f));
    cameraNode_->LookAt(Vector3(0.0f, 1.0f, 0.0f));
}

void BS_MotorDemo::SpawnTestRig(const Vector3& offset, bool fixed)
{
    auto* cache = GetSubsystem<ResourceCache>();

    TestRig rig;

    // Body — capsule shape
    rig.body = scene_->CreateChild("SpiderBody");
    rig.body->SetPosition(Vector3(0.0f, BODY_HEIGHT, 0.0f) + offset);

    auto* bodyModel = rig.body->CreateComponent<StaticModel>();
    bodyModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    bodyModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
    bodyModel->SetCastShadows(true);
    rig.body->SetScale(Vector3(BODY_RADIUS * 2.0f, 0.1f, BODY_RADIUS * 2.0f));

    auto* bodyRB = rig.body->CreateComponent<RigidBody>();
    bodyRB->SetMass(fixed ? 0.0f : 1.0f);
    bodyRB->SetCapsuleShape(BODY_RADIUS * 2.0f, 0.1f);
    bodyRB->SetLinearDamping(0.05f);
    bodyRB->SetAngularDamping(0.85f);

    for (int i = 0; i < NUM_LEGS; i++)
    {
        float angle = 2.0f * M_PI * (float)i / (float)NUM_LEGS;
        float fSin = sinf(angle);
        float fCos = cosf(angle);

        // Thigh
        Vector3 thighPos(
            fCos * (BODY_RADIUS + 0.5f * LEG_LENGTH),
            BODY_HEIGHT,
            fSin * (BODY_RADIUS + 0.5f * LEG_LENGTH)
        );
        thighPos += offset;

        Node* thighNode = scene_->CreateChild("Thigh");
        thighNode->SetPosition(thighPos);
        // Rotate capsule to lie horizontal pointing outward
        Vector3 boneDir(fCos, 0.0f, fSin);
        Vector3 up(0.0f, 1.0f, 0.0f);
        Vector3 axis = boneDir.CrossProduct(up);
        if (axis.LengthSquared() > 0.001f)
        {
            axis.Normalize();
            thighNode->SetRotation(Quaternion(90.0f, axis));
        }
        thighNode->SetScale(Vector3(0.2f, LEG_LENGTH, 0.2f));

        auto* thighModel = thighNode->CreateComponent<StaticModel>();
        thighModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        thighModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        thighModel->SetCastShadows(true);

        auto* thighRB = thighNode->CreateComponent<RigidBody>();
        thighRB->SetMass(1.0f);
        thighRB->SetCapsuleShape(0.2f, LEG_LENGTH);
        thighRB->SetLinearDamping(0.05f);
        thighRB->SetAngularDamping(0.85f);

        rig.legs[2 * i] = thighNode;

        // Hip constraint (hinge)
        auto* hipConstraint = thighNode->CreateComponent<Constraint>();
        hipConstraint->SetConstraintType(CONSTRAINT_HINGE);
        hipConstraint->SetOtherBody(bodyRB);
        // Pivot at body edge
        hipConstraint->SetPosition(Vector3(0.0f, -LEG_LENGTH * 0.45f / thighNode->GetScale().y_, 0.0f));
        hipConstraint->SetOtherPosition(Vector3(
            fCos * BODY_RADIUS / rig.body->GetScale().x_,
            0.0f,
            fSin * BODY_RADIUS / rig.body->GetScale().z_
        ));
        hipConstraint->SetAxis(Vector3::FORWARD);
        hipConstraint->SetOtherAxis(Vector3::FORWARD);
        // Original limits: -0.75 * PI/4 to PI/8
        hipConstraint->SetLowLimit(Vector2(-0.75f * 45.0f, 0.0f));
        hipConstraint->SetHighLimit(Vector2(22.5f, 0.0f));

        // Shin
        Vector3 shinPos(
            fCos * (BODY_RADIUS + LEG_LENGTH),
            BODY_HEIGHT - 0.5f * FORELEG_LENGTH,
            fSin * (BODY_RADIUS + LEG_LENGTH)
        );
        shinPos += offset;

        Node* shinNode = scene_->CreateChild("Shin");
        shinNode->SetPosition(shinPos);
        shinNode->SetScale(Vector3(0.16f, FORELEG_LENGTH, 0.16f));

        auto* shinModel = shinNode->CreateComponent<StaticModel>();
        shinModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        shinModel->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        shinModel->SetCastShadows(true);

        auto* shinRB = shinNode->CreateComponent<RigidBody>();
        shinRB->SetMass(1.0f);
        shinRB->SetCapsuleShape(0.16f, FORELEG_LENGTH);
        shinRB->SetLinearDamping(0.05f);
        shinRB->SetAngularDamping(0.85f);

        rig.legs[2 * i + 1] = shinNode;

        // Knee constraint (hinge)
        auto* kneeConstraint = shinNode->CreateComponent<Constraint>();
        kneeConstraint->SetConstraintType(CONSTRAINT_HINGE);
        kneeConstraint->SetOtherBody(thighRB);
        kneeConstraint->SetPosition(Vector3(0.0f, FORELEG_LENGTH * 0.45f / shinNode->GetScale().y_, 0.0f));
        kneeConstraint->SetOtherPosition(Vector3(0.0f, LEG_LENGTH * 0.45f / thighNode->GetScale().y_, 0.0f));
        kneeConstraint->SetAxis(Vector3::FORWARD);
        kneeConstraint->SetOtherAxis(Vector3::FORWARD);
        // Original limits: -PI/8 to 0.2 rad (~11.5 deg)
        kneeConstraint->SetLowLimit(Vector2(-22.5f, 0.0f));
        kneeConstraint->SetHighLimit(Vector2(11.5f, 0.0f));
    }

    rigs_.Push(rig);
}

void BS_MotorDemo::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_MotorDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_MotorDemo, HandleUpdate));
    SubscribeToEvent(E_PHYSICSPRESTEP, URHO3D_HANDLER(BS_MotorDemo, HandlePhysicsPreStep));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_MotorDemo, HandlePostRenderUpdate));
}

void BS_MotorDemo::HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData)
{
    using namespace PhysicsPreStep;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    float ms = timeStep * 1000000.0f;
    float minFPS = 1000000.0f / 60.0f;
    if (ms > minFPS)
        ms = minFPS;

    time_ += ms;

    for (unsigned r = 0; r < rigs_.Size(); r++)
    {
        TestRig& rig = rigs_[r];

        for (int i = 0; i < 2 * NUM_LEGS; i++)
        {
            Node* legNode = rig.legs[i];
            if (!legNode) continue;

            auto* constraint = legNode->GetComponent<Constraint>();
            if (!constraint) continue;

            auto* btConstraint = constraint->GetConstraint();
            if (!btConstraint) continue;

            auto* hingeC = dynamic_cast<btHingeConstraint*>(btConstraint);
            if (!hingeC) continue;

            float fCurAngle = hingeC->getHingeAngle();
            float fTargetPercent = fmodf(time_ / 1000.0f, cyclePeriod_) / cyclePeriod_;
            float fTargetAngle = 0.5f * (1.0f + sinf(2.0f * M_PI * fTargetPercent));
            float fTargetLimitAngle = hingeC->getLowerLimit() +
                fTargetAngle * (hingeC->getUpperLimit() - hingeC->getLowerLimit());
            float fAngleError = fTargetLimitAngle - fCurAngle;
            float fDesiredAngularVel = 1000000.0f * fAngleError / ms;

            hingeC->enableAngularMotor(true, fDesiredAngularVel, muscleStrength_);
        }
    }
}

void BS_MotorDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    // Adjust cycle period
    auto* input = GetSubsystem<Input>();
    if (input->GetKeyDown(KEY_KP_PLUS) || input->GetKeyDown(KEY_EQUALS))
        cyclePeriod_ /= 1.0f + timeStep;
    if (input->GetKeyDown(KEY_KP_MINUS) || input->GetKeyDown(KEY_MINUS))
        cyclePeriod_ *= 1.0f + timeStep;
    cyclePeriod_ = Clamp(cyclePeriod_, 100.0f, 10000.0f);
}

void BS_MotorDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
