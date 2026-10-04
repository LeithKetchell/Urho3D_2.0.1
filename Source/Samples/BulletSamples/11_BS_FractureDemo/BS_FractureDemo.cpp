// Simplified port of bullet3/examples/FractureDemo/FractureDemo.cpp
// Original uses btFractureBody + btFractureDynamicsWorld for automatic fracture.
// Urho3D does not expose these. This port approximates fracture using breakable
// point constraints between blocks in a wall. Color transfers from impacting
// objects to fragments on collision — you can see the impact propagate.
//
// Press F to launch a wrecking ball at the wall.

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
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_FractureDemo.h"

#include <Bullet/BulletDynamics/ConstraintSolver/btTypedConstraint.h>

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_FractureDemo)

void BS_FractureDemo::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "FractureDemo — wrecking ball vs breakable wall\n"
        "F=launch wrecking ball\n"
        "Color transfers from ball to fragments on impact\n"
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

void BS_FractureDemo::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();
    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));
    physicsWorld->SetUpdateEnabled(false);  // Paused until user presses G

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.4f, 0.4f, 0.5f));
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(0.9f, 0.9f, 0.85f));
    light->SetSpecularIntensity(0.5f);
    light->SetCastShadows(true);

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(100.0f, 1.0f, 100.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    // Build a wall to demolish
    CreateWall(Vector3(0.0f, 0.0f, 0.0f), 12, 10);

    // Camera — positioned to see the wall and the ball trajectory
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(-15.0f, 8.0f, -20.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 30.0f, 0.0f));
}

void BS_FractureDemo::CreateWall(const Vector3& position, int width, int height)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* baseMat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    float blockSize = 1.0f;
    float breakingThreshold = 8.0f;

    // Store nodes for constraint creation
    Vector<Vector<Node*>> grid(height);

    for (int row = 0; row < height; ++row)
    {
        grid[row].Resize(width);
        // Offset alternate rows by half a block for a brick pattern
        float offsetX = (row % 2 == 0) ? 0.0f : blockSize * 0.5f;

        for (int col = 0; col < width; ++col)
        {
            Node* node = scene_->CreateChild("WallBlock");
            float x = position.x_ + (col - width / 2.0f + 0.5f) * blockSize + offsetX;
            float y = position.y_ + row * blockSize + blockSize * 0.5f;
            node->SetPosition(Vector3(x, y, position.z_));
            node->SetScale(blockSize * 0.95f);

            node->CreateComponent<StaticModel>()->SetModel(boxMdl);
            // Give each block a warm color — variations of terracotta/brick
            SharedPtr<Material> mat = baseMat->Clone();
            float r = 0.6f + 0.3f * ((row * 7 + col * 13) % 17) / 17.0f;
            float g = 0.3f + 0.2f * ((row * 11 + col * 3) % 13) / 13.0f;
            float b = 0.15f + 0.1f * ((row * 5 + col * 9) % 11) / 11.0f;
            mat->SetShaderParameter("MatDiffColor", Variant(Color(r, g, b)));
            node->GetComponent<StaticModel>()->SetMaterial(mat);
            node->GetComponent<StaticModel>()->SetCastShadows(true);

            auto* body = node->CreateComponent<RigidBody>();
            body->SetMass(1.0f);
            body->SetFriction(0.8f);
            node->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

            grid[row][col] = node;
        }
    }

    // Connect adjacent blocks with breakable point constraints
    for (int row = 0; row < height; ++row)
    {
        for (int col = 0; col < width; ++col)
        {
            // Horizontal neighbor (same row, next column)
            if (col + 1 < width)
            {
                auto* constraint = grid[row][col]->CreateComponent<Constraint>();
                constraint->SetConstraintType(CONSTRAINT_POINT);
                constraint->SetOtherBody(grid[row][col + 1]->GetComponent<RigidBody>());
                constraint->SetWorldPosition(
                    (grid[row][col]->GetPosition() + grid[row][col + 1]->GetPosition()) * 0.5f);
                if (constraint->GetConstraint())
                    constraint->GetConstraint()->setBreakingImpulseThreshold(breakingThreshold);
            }

            // Vertical neighbor (row above, same column)
            if (row + 1 < height)
            {
                auto* constraint = grid[row][col]->CreateComponent<Constraint>();
                constraint->SetConstraintType(CONSTRAINT_POINT);
                constraint->SetOtherBody(grid[row + 1][col]->GetComponent<RigidBody>());
                constraint->SetWorldPosition(
                    (grid[row][col]->GetPosition() + grid[row + 1][col]->GetPosition()) * 0.5f);
                if (constraint->GetConstraint())
                    constraint->GetConstraint()->setBreakingImpulseThreshold(breakingThreshold);
            }

            // Diagonal neighbor for brick pattern (row above, offset column)
            if (row + 1 < height && row % 2 == 0 && col > 0)
            {
                auto* constraint = grid[row][col]->CreateComponent<Constraint>();
                constraint->SetConstraintType(CONSTRAINT_POINT);
                constraint->SetOtherBody(grid[row + 1][col - 1]->GetComponent<RigidBody>());
                constraint->SetWorldPosition(
                    (grid[row][col]->GetPosition() + grid[row + 1][col - 1]->GetPosition()) * 0.5f);
                if (constraint->GetConstraint())
                    constraint->GetConstraint()->setBreakingImpulseThreshold(breakingThreshold);
            }
        }
    }
}

void BS_FractureDemo::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_FractureDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_FractureDemo, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_FractureDemo, HandlePostRenderUpdate));
    SubscribeToEvent(E_PHYSICSCOLLISION, URHO3D_HANDLER(BS_FractureDemo, HandlePhysicsCollision));
}

void BS_FractureDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    // Check for broken constraints and remove them
    {
        Vector<Node*> blocks;
        Vector<Node*> allChildren;
        scene_->GetChildren(allChildren, false);
        for (unsigned i = 0; i < allChildren.Size(); ++i)
        {
            if (allChildren[i]->GetName() == "WallBlock")
                blocks.Push(allChildren[i]);
        }
        for (unsigned i = 0; i < blocks.Size(); ++i)
        {
            Vector<Component*> constraints;
            blocks[i]->GetComponents(constraints, Constraint::GetTypeStatic());
            for (unsigned j = 0; j < constraints.Size(); ++j)
            {
                auto* c = static_cast<Constraint*>(constraints[j]);
                btTypedConstraint* btc = c->GetConstraint();
                if (btc && btc->getAppliedImpulse() >= btc->getBreakingImpulseThreshold())
                {
                    blocks[i]->RemoveComponent(c);
                }
            }
        }
    }

    auto* input = GetSubsystem<Input>();

    // F key = launch a wrecking ball toward the wall (enables physics on first press)
    if (input->GetKeyPress(KEY_F))
    {
        if (!started_)
        {
            started_ = true;
            scene_->GetComponent<PhysicsWorld>()->SetUpdateEnabled(true);
        }
        auto* cache = GetSubsystem<ResourceCache>();
        Node* ball = scene_->CreateChild("WreckingBall");
        ball->SetPosition(Vector3(-20.0f, 5.0f, 0.0f));
        ball->SetScale(3.0f);
        ball->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
        // Bright red wrecking ball — its color will spread to fragments
        SharedPtr<Material> ballMat = cache->GetResource<Material>("Materials/StoneSmall.xml")->Clone();
        ballMat->SetShaderParameter("MatDiffColor", Variant(Color(1.0f, 0.1f, 0.1f)));
        ball->GetComponent<StaticModel>()->SetMaterial(ballMat);
        ball->GetComponent<StaticModel>()->SetCastShadows(true);

        auto* body = ball->CreateComponent<RigidBody>();
        body->SetMass(100.0f);
        body->SetRestitution(0.3f);
        body->SetFriction(0.5f);
        body->SetLinearVelocity(Vector3(25.0f, 5.0f, 0.0f));
        ball->CreateComponent<CollisionShape>()->SetSphere(1.0f);
    }

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_FractureDemo::HandlePhysicsCollision(StringHash eventType, VariantMap& eventData)
{
    using namespace PhysicsCollision;

    auto* nodeA = static_cast<Node*>(eventData[P_NODEA].GetPtr());
    auto* nodeB = static_cast<Node*>(eventData[P_NODEB].GetPtr());

    if (!nodeA || !nodeB)
        return;

    // Get colors from both objects' materials
    auto* modelA = nodeA->GetComponent<StaticModel>();
    auto* modelB = nodeB->GetComponent<StaticModel>();
    if (!modelA || !modelB)
        return;

    auto* matA = modelA->GetMaterial();
    auto* matB = modelB->GetMaterial();
    if (!matA || !matB)
        return;

    // Only transfer color when a non-wall object hits a wall block
    bool aIsWall = (nodeA->GetName() == "WallBlock");
    bool bIsWall = (nodeB->GetName() == "WallBlock");

    // Need exactly one wall block involved (wall-wall contacts are too frequent)
    if (aIsWall == bIsWall)
        return;

    // Identify the wall node and the impactor
    Node* wallNode = aIsWall ? nodeA : nodeB;
    Node* otherNode = aIsWall ? nodeB : nodeA;

    // Only transfer from wrecking balls or already-colored fragments
    const String& otherName = otherNode->GetName();
    if (otherName != "WreckingBall")
        return;

    auto* wallModel = wallNode->GetComponent<StaticModel>();
    auto* otherModel = otherNode->GetComponent<StaticModel>();
    if (!wallModel || !otherModel)
        return;

    auto* wallMat = wallModel->GetMaterial();
    auto* otherMat = otherModel->GetMaterial();
    if (!wallMat || !otherMat)
        return;

    const Variant& wallColorVar = wallMat->GetShaderParameter("MatDiffColor");
    const Variant& otherColorVar = otherMat->GetShaderParameter("MatDiffColor");
    if (wallColorVar.IsEmpty() || otherColorVar.IsEmpty())
        return;

    // Blend the wall block toward the impactor's color
    Color wallColor = wallColorVar.GetColor();
    Color otherColor = otherColorVar.GetColor();
    Color blended = wallColor.Lerp(otherColor, 0.6f);
    wallMat->SetShaderParameter("MatDiffColor", Variant(blended));
}

void BS_FractureDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
