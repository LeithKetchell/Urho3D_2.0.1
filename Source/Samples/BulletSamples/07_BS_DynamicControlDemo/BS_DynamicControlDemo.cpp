// Port of bullet3/examples/DynamicControlDemo/MotorDemo.cpp
// Original by Erwin Coumans — zlib license
//
// 6-legged spider with motorized hinge joints. Each leg has a hip hinge
// and knee hinge. Motors drive sinusoidal target angles for walking motion.
// Two spiders: one free, one with fixed body (hanging in air).

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
#include <Urho3D/IO/Log.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/Constraint.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_DynamicControlDemo.h"

#include <Urho3D/DebugNew.h>

#include <Bullet/BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <Bullet/BulletDynamics/Dynamics/btRigidBody.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_DynamicControlDemo)

void BS_DynamicControlDemo::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "DynamicControlDemo — motorized 6-legged spiders\n"
        "Left: free spider | Right: fixed body\n"
        "+/- cycle speed | [/] muscle strength\n"
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

void BS_DynamicControlDemo::CreateScene()
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
    light->SetSpecularIntensity(0.5f);

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -10.0f, 0.0f));
    groundNode->SetScale(Vector3(400.0f, 20.0f, 400.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    // Spawn two spiders
    CreateSpider(Vector3(1.0f, 0.5f, 0.0f), false);  // free
    CreateSpider(Vector3(-2.0f, 0.5f, 0.0f), true);   // fixed body

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 3.0f, -11.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));
}

void BS_DynamicControlDemo::CreateSpider(const Vector3& position, bool fixed)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* sphereMdl = cache->GetResource<Model>("Models/Sphere.mdl");
    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    SpiderData spider;

    float bodySize = 0.25f;
    float legLength = 0.45f;
    float foreLegLength = 0.75f;
    float height = 0.5f;

    // Body (central capsule)
    spider.bodyNode = scene_->CreateChild("SpiderBody");
    spider.bodyNode->SetPosition(position + Vector3(0.0f, height, 0.0f));
    spider.bodyNode->SetScale(Vector3(bodySize * 2.0f, 0.1f, bodySize * 2.0f));
    spider.bodyNode->CreateComponent<StaticModel>()->SetModel(sphereMdl);
    spider.bodyNode->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* bodyRB = spider.bodyNode->CreateComponent<RigidBody>();
    bodyRB->SetMass(fixed ? 0.0f : 1.0f);
    bodyRB->SetLinearDamping(0.05f);
    bodyRB->SetAngularDamping(0.85f);
    spider.bodyNode->CreateComponent<CollisionShape>()->SetCapsule(
        bodySize * 2.0f, 0.1f + bodySize * 2.0f);

    for (int i = 0; i < NUM_LEGS; ++i)
    {
        float angle = 2.0f * M_PI * i / NUM_LEGS;
        float fSin = sinf(angle);
        float fCos = cosf(angle);

        // Upper leg (thigh)
        Vector3 thighPos(
            fCos * (bodySize + 0.5f * legLength),
            height,
            fSin * (bodySize + 0.5f * legLength)
        );
        Node* thighNode = scene_->CreateChild("Thigh");
        thighNode->SetPosition(position + thighPos);
        // Rotate thigh to point outward from body
        thighNode->SetRotation(Quaternion(0.0f, -angle * M_RADTODEG, 90.0f));
        thighNode->SetScale(Vector3(0.2f, legLength, 0.2f));
        thighNode->CreateComponent<StaticModel>()->SetModel(boxMdl);
        thighNode->GetComponent<StaticModel>()->SetMaterial(mat);
        auto* thighBody = thighNode->CreateComponent<RigidBody>();
        thighBody->SetMass(1.0f);
        thighBody->SetLinearDamping(0.05f);
    thighBody->SetAngularDamping(0.85f);
        thighNode->CreateComponent<CollisionShape>()->SetCapsule(1.0f, legLength / 0.2f);
        spider.legNodes[i * 2] = thighNode;

        // Hip joint (body -> thigh) — local frame rotated by -legAngle around Y
        // Matches original: localA.setEulerZYX(0, -fAngle, 0)
        auto* hipConstraint = spider.bodyNode->CreateComponent<Constraint>();
        hipConstraint->SetConstraintType(CONSTRAINT_HINGE);
        hipConstraint->SetOtherBody(thighBody);
        Vector3 hipPos = position + Vector3(fCos * bodySize, height, fSin * bodySize);
        hipConstraint->SetWorldPosition(hipPos);
        Quaternion hingeFrame(0.0f, -angle * M_RADTODEG, 0.0f);
        hipConstraint->SetRotation(hingeFrame);
        hipConstraint->SetOtherRotation(hingeFrame);
        hipConstraint->SetLowLimit(Vector2(-33.75f, 0.0f));
        hipConstraint->SetHighLimit(Vector2(22.5f, 0.0f));
        spider.joints[i * 2] = hipConstraint;

        // Lower leg (shin)
        Vector3 shinPos(
            fCos * (bodySize + legLength),
            height - 0.5f * foreLegLength,
            fSin * (bodySize + legLength)
        );
        Node* shinNode = scene_->CreateChild("Shin");
        shinNode->SetPosition(position + shinPos);
        shinNode->SetScale(Vector3(0.16f, foreLegLength, 0.16f));
        shinNode->CreateComponent<StaticModel>()->SetModel(boxMdl);
        shinNode->GetComponent<StaticModel>()->SetMaterial(mat);
        auto* shinBody = shinNode->CreateComponent<RigidBody>();
        shinBody->SetMass(1.0f);
        shinBody->SetLinearDamping(0.05f);
    shinBody->SetAngularDamping(0.85f);
        shinNode->CreateComponent<CollisionShape>()->SetCapsule(1.0f, foreLegLength / 0.16f);
        spider.legNodes[i * 2 + 1] = shinNode;

        // Knee joint (thigh -> shin)
        auto* kneeConstraint = thighNode->CreateComponent<Constraint>();
        kneeConstraint->SetConstraintType(CONSTRAINT_HINGE);
        kneeConstraint->SetOtherBody(shinBody);
        Vector3 kneePos = position + Vector3(
            fCos * (bodySize + legLength), height, fSin * (bodySize + legLength));
        kneeConstraint->SetWorldPosition(kneePos);
        Quaternion kneeFrame(0.0f, -angle * M_RADTODEG, 0.0f);
        kneeConstraint->SetRotation(kneeFrame);
        kneeConstraint->SetOtherRotation(kneeFrame);
        kneeConstraint->SetLowLimit(Vector2(-22.5f, 0.0f));
        kneeConstraint->SetHighLimit(Vector2(11.5f, 0.0f));
        spider.joints[i * 2 + 1] = kneeConstraint;
    }

    spiders_.Push(spider);
}

void BS_DynamicControlDemo::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_DynamicControlDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_DynamicControlDemo, HandleUpdate));
    SubscribeToEvent(E_PHYSICSPRESTEP, URHO3D_HANDLER(BS_DynamicControlDemo, HandlePhysicsPreStep));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_DynamicControlDemo, HandlePostRenderUpdate));
}

void BS_DynamicControlDemo::HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData)
{
    using namespace PhysicsPreStep;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    time_ += timeStep;

    // Drive motors with sinusoidal targets — matches original MotorDemo::setMotorTargets
    for (unsigned s = 0; s < spiders_.Size(); ++s)
    {
        SpiderData& spider = spiders_[s];
        for (int i = 0; i < NUM_LEGS * 2; ++i)
        {
            if (!spider.joints[i])
                continue;

            // Access the raw Bullet hinge constraint to use enableMotor
            btTypedConstraint* btConstraint = spider.joints[i]->GetConstraint();
            if (!btConstraint)
                continue;

            btHingeConstraint* hinge = dynamic_cast<btHingeConstraint*>(btConstraint);
            if (!hinge)
                continue;

            float curAngle = hinge->getHingeAngle();
            float targetPercent = fmodf(time_, cyclePeriod_) / cyclePeriod_;
            float targetAngle = 0.5f * (1.0f + sinf(2.0f * M_PI * targetPercent));
            float targetLimitAngle = hinge->getLowerLimit() +
                targetAngle * (hinge->getUpperLimit() - hinge->getLowerLimit());
            float angleError = targetLimitAngle - curAngle;
            float desiredVel = 1000000.0f * angleError / timeStep;
            hinge->enableAngularMotor(true, desiredVel, muscleStrength_);
        }
    }
}

void BS_DynamicControlDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    // +/- to adjust cycle speed, [/] for muscle strength
    auto* input = GetSubsystem<Input>();
    if (input->GetKeyDown(KEY_EQUALS) || input->GetKeyDown(KEY_KP_PLUS))
        cyclePeriod_ = Max(0.1f, cyclePeriod_ / 1.01f);
    if (input->GetKeyDown(KEY_MINUS) || input->GetKeyDown(KEY_KP_MINUS))
        cyclePeriod_ *= 1.01f;
    if (input->GetKeyDown(KEY_RIGHTBRACKET))
        muscleStrength_ *= 1.01f;
    if (input->GetKeyDown(KEY_LEFTBRACKET))
        muscleStrength_ = Max(0.01f, muscleStrength_ / 1.01f);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_DynamicControlDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
