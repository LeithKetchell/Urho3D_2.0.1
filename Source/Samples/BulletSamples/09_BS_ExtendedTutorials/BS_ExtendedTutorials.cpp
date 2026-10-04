// Port of bullet3/examples/ExtendedTutorials/NewtonsCradle.cpp
// Original: adjustable number of pendula suspended from point constraints.
// Demonstrates conservation of momentum and restitution.
//
// Also includes a Bridge demo on the right side — planks connected by hinges.

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

#include "BS_ExtendedTutorials.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_ExtendedTutorials)

void BS_ExtendedTutorials::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "ExtendedTutorials — Newton's Cradle + Bridge\n"
        "Left: Newton's Cradle (5 balls)\n"
        "Right: Rope bridge (20 planks)\n"
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

void BS_ExtendedTutorials::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();
    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));
    physicsWorld->SetNumIterations(50);
    physicsWorld->SetSplitImpulse(true);

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

    // Newton's Cradle on the left
    CreateNewtonsCradle(Vector3(-5.0f, 0.0f, 0.0f), 5, 0.5f, 5.0f);

    // Bridge on the right — 20 planks connected by hinge constraints
    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");
    int numPlanks = 20;
    float plankLength = 1.0f;
    float plankWidth = 2.0f;
    float plankHeight = 0.15f;
    float gap = 0.05f;

    Node* prevNode = nullptr;
    for (int i = 0; i < numPlanks; ++i)
    {
        Node* plank = scene_->CreateChild("BridgePlank");
        float x = 5.0f + i * (plankLength + gap);
        plank->SetPosition(Vector3(x, 5.0f, 0.0f));
        plank->SetScale(Vector3(plankLength, plankHeight, plankWidth));

        auto* model = plank->CreateComponent<StaticModel>();
        model->SetModel(boxMdl);
        model->SetMaterial(mat);

        auto* body = plank->CreateComponent<RigidBody>();
        // First and last plank are anchored (mass 0)
        body->SetMass((i == 0 || i == numPlanks - 1) ? 0.0f : 0.5f);
        body->SetLinearDamping(0.2f);
    body->SetAngularDamping(0.2f);

        auto* shape = plank->CreateComponent<CollisionShape>();
        shape->SetBox(Vector3::ONE);

        if (prevNode)
        {
            auto* constraint = prevNode->CreateComponent<Constraint>();
            constraint->SetConstraintType(CONSTRAINT_HINGE);
            constraint->SetOtherBody(body);
            // Position at the right edge of previous plank / left edge of current
            float jointX = x - (plankLength + gap) * 0.5f;
            constraint->SetWorldPosition(Vector3(jointX, 5.0f, 0.0f));
            constraint->SetAxis(Vector3::FORWARD);
            constraint->SetOtherAxis(Vector3::FORWARD);
            constraint->SetLowLimit(Vector2(-30.0f, 0.0f));
            constraint->SetHighLimit(Vector2(30.0f, 0.0f));
        }

        prevNode = plank;
    }

    // Drop a ball on the bridge
    Node* ball = scene_->CreateChild("BridgeBall");
    ball->SetPosition(Vector3(15.0f, 10.0f, 0.0f));
    ball->SetScale(1.0f);
    ball->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
    ball->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* ballBody = ball->CreateComponent<RigidBody>();
    ballBody->SetMass(5.0f);
    ball->CreateComponent<CollisionShape>()->SetSphere(1.0f);

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(5.0f, 8.0f, -15.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

void BS_ExtendedTutorials::CreateNewtonsCradle(const Vector3& position, int numBalls, float ballRadius, float stringLength)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* sphereMdl = cache->GetResource<Model>("Models/Sphere.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    float spacing = ballRadius * 2.01f;  // Slight gap to prevent resting contact
    float anchorY = position.y_ + stringLength + ballRadius;

    // String spread — distance from center to each string attachment in Z
    // A real cradle has two strings per ball, one on each side
    float stringSpread = ballRadius * 1.5f;

    // Two support bars at the top (one on each side in Z, like a real cradle frame)
    for (int side = 0; side < 2; ++side)
    {
        float z = (side == 0) ? -stringSpread : stringSpread;
        Node* barNode = scene_->CreateChild("CradleBar");
        barNode->SetPosition(Vector3(position.x_, anchorY + 0.1f, z));
        barNode->SetScale(Vector3(numBalls * spacing + 0.5f, 0.2f, 0.15f));
        barNode->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        SharedPtr<Material> barMat = cache->GetResource<Material>("Materials/StoneSmall.xml")->Clone();
        barMat->SetShaderParameter("MatDiffColor", Variant(Color(0.3f, 0.3f, 0.3f)));
        barNode->GetComponent<StaticModel>()->SetMaterial(barMat);
    }

    for (int i = 0; i < numBalls; ++i)
    {
        float x = position.x_ + (i - numBalls / 2.0f + 0.5f) * spacing;

        // Ball
        Node* ballNode = scene_->CreateChild("CradleBall");
        ballNode->SetPosition(Vector3(x, position.y_ + ballRadius, 0.0f));
        ballNode->SetScale(ballRadius * 2.0f);
        ballNode->CreateComponent<StaticModel>()->SetModel(sphereMdl);
        ballNode->GetComponent<StaticModel>()->SetMaterial(mat);

        auto* body = ballNode->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetRestitution(1.0f);
        body->SetFriction(0.0f);
        body->SetLinearDamping(0.0f);
        body->SetAngularDamping(0.0f);

        auto* shape = ballNode->CreateComponent<CollisionShape>();
        shape->SetSphere(1.0f);

        // Two strings per ball — one on each side in Z, like a real cradle
        // This naturally constrains the ball to swing in the X-Y plane
        auto* constraintLeft = ballNode->CreateComponent<Constraint>();
        constraintLeft->SetConstraintType(CONSTRAINT_POINT);
        constraintLeft->SetWorldPosition(Vector3(x, anchorY, -stringSpread));

        auto* constraintRight = ballNode->CreateComponent<Constraint>();
        constraintRight->SetConstraintType(CONSTRAINT_POINT);
        constraintRight->SetWorldPosition(Vector3(x, anchorY, stringSpread));

        // Displace the first ball to start the motion
        if (i == 0)
        {
            ballNode->SetPosition(Vector3(x - stringLength * 0.7f, position.y_ + ballRadius + stringLength * 0.3f, 0.0f));
        }
    }
}

void BS_ExtendedTutorials::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_ExtendedTutorials::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_ExtendedTutorials, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_ExtendedTutorials, HandlePostRenderUpdate));
}

void BS_ExtendedTutorials::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_ExtendedTutorials::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);

    // Draw pendulum strings
    auto* debug = scene_->GetComponent<DebugRenderer>();
    Vector<Node*> allChildren;
    scene_->GetChildren(allChildren, false);
    Vector<Node*> balls;
    for (unsigned i = 0; i < allChildren.Size(); ++i)
    {
        if (allChildren[i]->GetName() == "CradleBall")
            balls.Push(allChildren[i]);
    }
    for (unsigned i = 0; i < balls.Size(); ++i)
    {
        Vector3 ballPos = balls[i]->GetWorldPosition();
        // Draw all constraints (two strings per ball)
        Vector<Component*> constraints;
        balls[i]->GetComponents(constraints, Constraint::GetTypeStatic());
        for (unsigned j = 0; j < constraints.Size(); ++j)
        {
            auto* constraint = static_cast<Constraint*>(constraints[j]);
            Vector3 anchor = constraint->GetWorldPosition();
            debug->AddLine(anchor, ballPos, Color::WHITE);
        }
    }
}
