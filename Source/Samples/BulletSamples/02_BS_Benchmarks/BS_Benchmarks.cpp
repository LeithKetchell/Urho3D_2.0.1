// Port of bullet3/examples/Benchmarks/BenchmarkDemo.cpp
// Original by Erwin Coumans — zlib license
//
// 5 benchmark tests exercising different physics scenarios.
// Press 1-5 to switch tests. FPS counter in top-right.

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/Timer.h>
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
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_Benchmarks.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Benchmarks)

void BS_Benchmarks::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "Bullet Benchmarks — press 1-5 to switch tests\n"
        "1=3000 cubes 2=pyramids 3=ragdolls 4=mixed 5=raycasts\n"
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

    // Stats text (body count, FPS)
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    statsText_ = new Text(context_);
    statsText_->SetFont(font, 12);
    statsText_->SetColor(Color::YELLOW);
    statsText_->SetAlignment(HA_RIGHT, VA_TOP);
    statsText_->SetPosition(-10, 10);
    GetSubsystem<UI>()->GetRoot()->AddChild(statsText_);
}

void BS_Benchmarks::CreateScene()
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
    zone->SetFogStart(200.0f);
    zone->SetFogEnd(500.0f);

    // Light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(0.8f, 0.8f, 0.8f));
    light->SetSpecularIntensity(0.5f);

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(500.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 30.0f, -80.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));

    CreateTest1();
}

void BS_Benchmarks::ClearScene()
{
    // Remove all physics bodies except zone, light, camera
    Urho3D::Vector<Node*> children;
    scene_->GetChildren(children);
    for (unsigned i = 0; i < children.Size(); ++i)
    {
        const String& name = children[i]->GetName();
        if (name != "Zone" && name != "DirectionalLight")
            children[i]->Remove();
    }
}

// Test 1: 3000 falling cubes (47 layers of 8x8) — original test 1
void BS_Benchmarks::CreateTest1()
{
    auto* cache = GetSubsystem<ResourceCache>();
    currentTest_ = 1;

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
    groundNode->SetScale(Vector3(500.0f, 100.0f, 500.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    // 3008 boxes: 47 layers of 8x8
    const float cubeSize = 1.0f;
    float spacing = cubeSize;
    float offset = -8 * (cubeSize * 2.0f + spacing) * 0.5f;

    for (int k = 0; k < 47; ++k)
    {
        for (int j = 0; j < 8; ++j)
        {
            for (int i = 0; i < 8; ++i)
            {
                float x = offset + i * (cubeSize * 2.0f + spacing);
                float y = cubeSize * 2.0f + k * (cubeSize * 2.0f + spacing);
                float z = offset + j * (cubeSize * 2.0f + spacing);

                Node* boxNode = scene_->CreateChild("Box");
                boxNode->SetPosition(Vector3(x, y, z));
                boxNode->SetScale(cubeSize * 2.0f);

                auto* model = boxNode->CreateComponent<StaticModel>();
                model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
                model->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));

                auto* body = boxNode->CreateComponent<RigidBody>();
                body->SetMass(2.0f);
                auto* shape = boxNode->CreateComponent<CollisionShape>();
                shape->SetBox(Vector3::ONE);
            }
        }
        offset -= 0.05f * spacing * 7;
    }

    cameraNode_->SetPosition(Vector3(0.0f, 60.0f, -120.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

// Test 2: Pyramids + walls + tower circle — original test 2
void BS_Benchmarks::CreateTest2()
{
    auto* cache = GetSubsystem<ResourceCache>();
    currentTest_ = 2;

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
    groundNode->SetScale(Vector3(500.0f, 100.0f, 500.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    Vector3 boxSize(1.0f, 1.0f, 1.0f);
    CreatePyramid(Vector3(-20.0f, 0.0f, 0.0f), 12, boxSize);
    CreateWall(Vector3(-2.0f, 0.0f, 0.0f), 12, boxSize);
    CreateWall(Vector3(4.0f, 0.0f, 0.0f), 12, boxSize);
    CreateWall(Vector3(10.0f, 0.0f, 0.0f), 12, boxSize);
    CreateTowerCircle(Vector3(25.0f, 0.0f, 0.0f), 8, 24, boxSize);

    cameraNode_->SetPosition(Vector3(0.0f, 20.0f, -50.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));
}

void BS_Benchmarks::CreateWall(const Vector3& offsetPosition, int stackSize, const Vector3& boxSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    float diffY = boxSize.y_;
    float diffZ = boxSize.z_;
    float offset = -stackSize * (diffZ * 2.0f) * 0.5f;
    Vector3 pos(0.0f, diffY, 0.0f);

    int s = stackSize;
    while (s)
    {
        for (int i = 0; i < s; ++i)
        {
            pos.z_ = offset + i * (diffZ * 2.0f);
            Vector3 worldPos = offsetPosition + pos;

            Node* node = scene_->CreateChild("WallBlock");
            node->SetPosition(worldPos);
            node->SetScale(boxSize * 2.0f);
            auto* model = node->CreateComponent<StaticModel>();
            model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
            model->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
            auto* body = node->CreateComponent<RigidBody>();
            body->SetMass(1.0f);
            auto* shape = node->CreateComponent<CollisionShape>();
            shape->SetBox(Vector3::ONE);
        }
        offset += diffZ;
        pos.y_ += diffY * 2.0f;
        s--;
    }
}

void BS_Benchmarks::CreatePyramid(const Vector3& offsetPosition, int stackSize, const Vector3& boxSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    float space = 0.0001f;
    Vector3 pos(0.0f, boxSize.y_, 0.0f);
    float diffX = boxSize.x_ * 1.02f;
    float diffY = boxSize.y_ * 1.02f;
    float diffZ = boxSize.z_ * 1.02f;
    float offsetX = -stackSize * (diffX * 2.0f + space) * 0.5f;
    float offsetZ = -stackSize * (diffZ * 2.0f + space) * 0.5f;

    int s = stackSize;
    while (s)
    {
        for (int j = 0; j < s; ++j)
        {
            pos.z_ = offsetZ + j * (diffZ * 2.0f + space);
            for (int i = 0; i < s; ++i)
            {
                pos.x_ = offsetX + i * (diffX * 2.0f + space);
                Vector3 worldPos = offsetPosition + pos;

                Node* node = scene_->CreateChild("PyramidBlock");
                node->SetPosition(worldPos);
                node->SetScale(boxSize * 2.0f);
                auto* model = node->CreateComponent<StaticModel>();
                model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
                model->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
                auto* body = node->CreateComponent<RigidBody>();
                body->SetMass(1.0f);
                auto* shape = node->CreateComponent<CollisionShape>();
                shape->SetBox(Vector3::ONE);
            }
        }
        offsetX += diffX;
        offsetZ += diffZ;
        pos.y_ += diffY * 2.0f + space;
        s--;
    }
}

void BS_Benchmarks::CreateTowerCircle(const Vector3& offsetPosition, int stackSize, int rotSize, const Vector3& boxSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    float radius = 1.3f * rotSize * boxSize.x_ / M_PI;
    float posY = boxSize.y_;

    for (int i = 0; i < stackSize; ++i)
    {
        for (int j = 0; j < rotSize; ++j)
        {
            float angle = (float)j * M_PI * 2.0f / rotSize + (i % 2 ? M_PI / rotSize : 0.0f);
            float x = cosf(angle) * radius;
            float z = sinf(angle) * radius;

            Node* node = scene_->CreateChild("TowerBlock");
            node->SetPosition(offsetPosition + Vector3(x, posY, z));
            node->SetRotation(Quaternion(0.0f, angle * M_RADTODEG, 0.0f));
            node->SetScale(boxSize * 2.0f);
            auto* model = node->CreateComponent<StaticModel>();
            model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
            model->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
            auto* body = node->CreateComponent<RigidBody>();
            body->SetMass(1.0f);
            auto* shape = node->CreateComponent<CollisionShape>();
            shape->SetBox(Vector3::ONE);
        }
        posY += boxSize.y_ * 2.0f;
    }
}

// Test 3: Ragdolls — original test 3 (16 rows of ragdolls)
void BS_Benchmarks::CreateTest3()
{
    auto* cache = GetSubsystem<ResourceCache>();
    currentTest_ = 3;

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
    groundNode->SetScale(Vector3(500.0f, 100.0f, 500.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    // Ragdolls in pyramid arrangement
    int size = 8;  // Reduced from 16 for performance
    float sizeX = 1.0f;
    float sizeY = 1.0f;
    float scale = 3.5f;
    Vector3 pos(0.0f, sizeY, 0.0f);

    while (size)
    {
        float offset = -size * (sizeX * 6.0f) * 0.5f;
        for (int i = 0; i < size; ++i)
        {
            pos.x_ = offset + i * (sizeX * 6.0f);
            CreateRagdoll(pos, scale);
        }
        offset += sizeX;
        pos.y_ += sizeY * 7.0f;
        pos.z_ -= sizeX * 2.0f;
        size--;
    }

    cameraNode_->SetPosition(Vector3(0.0f, 30.0f, -60.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

void BS_Benchmarks::CreateRagdoll(const Vector3& position, float scale)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");
    auto* boxModel = cache->GetResource<Model>("Models/Box.mdl");
    auto* sphereModel = cache->GetResource<Model>("Models/Sphere.mdl");

    // Simplified ragdoll: pelvis, spine, head, 2 upper legs, 2 lower legs, 2 upper arms, 2 lower arms
    // Using capsule collision shapes approximated as scaled boxes/spheres

    // Pelvis
    Node* pelvis = scene_->CreateChild("Pelvis");
    pelvis->SetPosition(position + Vector3(0.0f, 1.0f, 0.0f) * scale);
    pelvis->SetScale(Vector3(0.3f, 0.2f, 0.3f) * scale);
    pelvis->CreateComponent<StaticModel>()->SetModel(sphereModel);
    pelvis->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* pelvisBody = pelvis->CreateComponent<RigidBody>();
    pelvisBody->SetMass(1.0f);
    pelvisBody->SetLinearDamping(0.05f);
    pelvisBody->SetAngularDamping(0.85f);
    pelvis->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 1.5f);

    // Spine
    Node* spine = scene_->CreateChild("Spine");
    spine->SetPosition(position + Vector3(0.0f, 1.2f, 0.0f) * scale);
    spine->SetScale(Vector3(0.3f, 0.28f, 0.3f) * scale);
    spine->CreateComponent<StaticModel>()->SetModel(sphereModel);
    spine->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* spineBody = spine->CreateComponent<RigidBody>();
    spineBody->SetMass(1.0f);
    spineBody->SetLinearDamping(0.05f);
    spineBody->SetAngularDamping(0.85f);
    spine->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 1.5f);

    // Pelvis-Spine constraint
    auto* pelvisSpine = pelvis->CreateComponent<Constraint>();
    pelvisSpine->SetConstraintType(CONSTRAINT_HINGE);
    pelvisSpine->SetOtherBody(spineBody);
    pelvisSpine->SetWorldPosition(position + Vector3(0.0f, 1.1f, 0.0f) * scale);
    pelvisSpine->SetAxis(Vector3::RIGHT);
    pelvisSpine->SetOtherAxis(Vector3::RIGHT);
    pelvisSpine->SetLowLimit(Vector2(-45.0f, 0.0f));
    pelvisSpine->SetHighLimit(Vector2(45.0f, 0.0f));

    // Head
    Node* head = scene_->CreateChild("Head");
    head->SetPosition(position + Vector3(0.0f, 1.6f, 0.0f) * scale);
    head->SetScale(Vector3(0.2f, 0.2f, 0.2f) * scale);
    head->CreateComponent<StaticModel>()->SetModel(sphereModel);
    head->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* headBody = head->CreateComponent<RigidBody>();
    headBody->SetMass(1.0f);
    headBody->SetLinearDamping(0.05f);
    headBody->SetAngularDamping(0.85f);
    head->CreateComponent<CollisionShape>()->SetSphere(1.0f);

    // Spine-Head constraint
    auto* spineHead = spine->CreateComponent<Constraint>();
    spineHead->SetConstraintType(CONSTRAINT_CONETWIST);
    spineHead->SetOtherBody(headBody);
    spineHead->SetWorldPosition(position + Vector3(0.0f, 1.4f, 0.0f) * scale);
    spineHead->SetAxis(Vector3::UP);
    spineHead->SetOtherAxis(Vector3::UP);
    spineHead->SetHighLimit(Vector2(45.0f, 45.0f));

    // Left upper leg
    Node* lUpperLeg = scene_->CreateChild("LUpperLeg");
    lUpperLeg->SetPosition(position + Vector3(-0.18f, 0.65f, 0.0f) * scale);
    lUpperLeg->SetScale(Vector3(0.14f, 0.45f, 0.14f) * scale);
    lUpperLeg->CreateComponent<StaticModel>()->SetModel(boxModel);
    lUpperLeg->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* lulBody = lUpperLeg->CreateComponent<RigidBody>();
    lulBody->SetMass(1.0f);
    lulBody->SetLinearDamping(0.05f);
    lulBody->SetAngularDamping(0.85f);
    lUpperLeg->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.0f);

    auto* lHip = pelvis->CreateComponent<Constraint>();
    lHip->SetConstraintType(CONSTRAINT_CONETWIST);
    lHip->SetOtherBody(lulBody);
    lHip->SetWorldPosition(position + Vector3(-0.18f, 0.9f, 0.0f) * scale);
    lHip->SetAxis(Vector3::DOWN);
    lHip->SetOtherAxis(Vector3::UP);
    lHip->SetHighLimit(Vector2(45.0f, 45.0f));

    // Left lower leg
    Node* lLowerLeg = scene_->CreateChild("LLowerLeg");
    lLowerLeg->SetPosition(position + Vector3(-0.18f, 0.2f, 0.0f) * scale);
    lLowerLeg->SetScale(Vector3(0.1f, 0.37f, 0.1f) * scale);
    lLowerLeg->CreateComponent<StaticModel>()->SetModel(boxModel);
    lLowerLeg->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* lllBody = lLowerLeg->CreateComponent<RigidBody>();
    lllBody->SetMass(1.0f);
    lllBody->SetLinearDamping(0.05f);
    lllBody->SetAngularDamping(0.85f);
    lLowerLeg->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.5f);

    auto* lKnee = lUpperLeg->CreateComponent<Constraint>();
    lKnee->SetConstraintType(CONSTRAINT_HINGE);
    lKnee->SetOtherBody(lllBody);
    lKnee->SetWorldPosition(position + Vector3(-0.18f, 0.42f, 0.0f) * scale);
    lKnee->SetAxis(Vector3::RIGHT);
    lKnee->SetOtherAxis(Vector3::RIGHT);
    lKnee->SetLowLimit(Vector2(0.0f, 0.0f));
    lKnee->SetHighLimit(Vector2(90.0f, 0.0f));

    // Right upper leg
    Node* rUpperLeg = scene_->CreateChild("RUpperLeg");
    rUpperLeg->SetPosition(position + Vector3(0.18f, 0.65f, 0.0f) * scale);
    rUpperLeg->SetScale(Vector3(0.14f, 0.45f, 0.14f) * scale);
    rUpperLeg->CreateComponent<StaticModel>()->SetModel(boxModel);
    rUpperLeg->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* rulBody = rUpperLeg->CreateComponent<RigidBody>();
    rulBody->SetMass(1.0f);
    rulBody->SetLinearDamping(0.05f);
    rulBody->SetAngularDamping(0.85f);
    rUpperLeg->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.0f);

    auto* rHip = pelvis->CreateComponent<Constraint>();
    rHip->SetConstraintType(CONSTRAINT_CONETWIST);
    rHip->SetOtherBody(rulBody);
    rHip->SetWorldPosition(position + Vector3(0.18f, 0.9f, 0.0f) * scale);
    rHip->SetAxis(Vector3::DOWN);
    rHip->SetOtherAxis(Vector3::UP);
    rHip->SetHighLimit(Vector2(45.0f, 45.0f));

    // Right lower leg
    Node* rLowerLeg = scene_->CreateChild("RLowerLeg");
    rLowerLeg->SetPosition(position + Vector3(0.18f, 0.2f, 0.0f) * scale);
    rLowerLeg->SetScale(Vector3(0.1f, 0.37f, 0.1f) * scale);
    rLowerLeg->CreateComponent<StaticModel>()->SetModel(boxModel);
    rLowerLeg->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* rllBody = rLowerLeg->CreateComponent<RigidBody>();
    rllBody->SetMass(1.0f);
    rllBody->SetLinearDamping(0.05f);
    rllBody->SetAngularDamping(0.85f);
    rLowerLeg->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.5f);

    auto* rKnee = rUpperLeg->CreateComponent<Constraint>();
    rKnee->SetConstraintType(CONSTRAINT_HINGE);
    rKnee->SetOtherBody(rllBody);
    rKnee->SetWorldPosition(position + Vector3(0.18f, 0.42f, 0.0f) * scale);
    rKnee->SetAxis(Vector3::RIGHT);
    rKnee->SetOtherAxis(Vector3::RIGHT);
    rKnee->SetLowLimit(Vector2(0.0f, 0.0f));
    rKnee->SetHighLimit(Vector2(90.0f, 0.0f));

    // Left upper arm
    Node* lUpperArm = scene_->CreateChild("LUpperArm");
    lUpperArm->SetPosition(position + Vector3(-0.35f, 1.45f, 0.0f) * scale);
    lUpperArm->SetScale(Vector3(0.33f, 0.1f, 0.1f) * scale);
    lUpperArm->CreateComponent<StaticModel>()->SetModel(boxModel);
    lUpperArm->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* luaBody = lUpperArm->CreateComponent<RigidBody>();
    luaBody->SetMass(1.0f);
    luaBody->SetLinearDamping(0.05f);
    luaBody->SetAngularDamping(0.85f);
    lUpperArm->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.0f);

    auto* lShoulder = spine->CreateComponent<Constraint>();
    lShoulder->SetConstraintType(CONSTRAINT_CONETWIST);
    lShoulder->SetOtherBody(luaBody);
    lShoulder->SetWorldPosition(position + Vector3(-0.2f, 1.45f, 0.0f) * scale);
    lShoulder->SetAxis(Vector3::LEFT);
    lShoulder->SetOtherAxis(Vector3::LEFT);
    lShoulder->SetHighLimit(Vector2(90.0f, 90.0f));

    // Left lower arm
    Node* lLowerArm = scene_->CreateChild("LLowerArm");
    lLowerArm->SetPosition(position + Vector3(-0.7f, 1.45f, 0.0f) * scale);
    lLowerArm->SetScale(Vector3(0.25f, 0.08f, 0.08f) * scale);
    lLowerArm->CreateComponent<StaticModel>()->SetModel(boxModel);
    lLowerArm->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* llaBody = lLowerArm->CreateComponent<RigidBody>();
    llaBody->SetMass(1.0f);
    llaBody->SetLinearDamping(0.05f);
    llaBody->SetAngularDamping(0.85f);
    lLowerArm->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.0f);

    auto* lElbow = lUpperArm->CreateComponent<Constraint>();
    lElbow->SetConstraintType(CONSTRAINT_HINGE);
    lElbow->SetOtherBody(llaBody);
    lElbow->SetWorldPosition(position + Vector3(-0.52f, 1.45f, 0.0f) * scale);
    lElbow->SetAxis(Vector3::UP);
    lElbow->SetOtherAxis(Vector3::UP);
    lElbow->SetLowLimit(Vector2(-90.0f, 0.0f));
    lElbow->SetHighLimit(Vector2(0.0f, 0.0f));

    // Right upper arm
    Node* rUpperArm = scene_->CreateChild("RUpperArm");
    rUpperArm->SetPosition(position + Vector3(0.35f, 1.45f, 0.0f) * scale);
    rUpperArm->SetScale(Vector3(0.33f, 0.1f, 0.1f) * scale);
    rUpperArm->CreateComponent<StaticModel>()->SetModel(boxModel);
    rUpperArm->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* ruaBody = rUpperArm->CreateComponent<RigidBody>();
    ruaBody->SetMass(1.0f);
    ruaBody->SetLinearDamping(0.05f);
    ruaBody->SetAngularDamping(0.85f);
    rUpperArm->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.0f);

    auto* rShoulder = spine->CreateComponent<Constraint>();
    rShoulder->SetConstraintType(CONSTRAINT_CONETWIST);
    rShoulder->SetOtherBody(ruaBody);
    rShoulder->SetWorldPosition(position + Vector3(0.2f, 1.45f, 0.0f) * scale);
    rShoulder->SetAxis(Vector3::RIGHT);
    rShoulder->SetOtherAxis(Vector3::RIGHT);
    rShoulder->SetHighLimit(Vector2(90.0f, 90.0f));

    // Right lower arm
    Node* rLowerArm = scene_->CreateChild("RLowerArm");
    rLowerArm->SetPosition(position + Vector3(0.7f, 1.45f, 0.0f) * scale);
    rLowerArm->SetScale(Vector3(0.25f, 0.08f, 0.08f) * scale);
    rLowerArm->CreateComponent<StaticModel>()->SetModel(boxModel);
    rLowerArm->GetComponent<StaticModel>()->SetMaterial(mat);
    auto* rlaBody = rLowerArm->CreateComponent<RigidBody>();
    rlaBody->SetMass(1.0f);
    rlaBody->SetLinearDamping(0.05f);
    rlaBody->SetAngularDamping(0.85f);
    rLowerArm->CreateComponent<CollisionShape>()->SetCapsule(1.0f, 3.0f);

    auto* rElbow = rUpperArm->CreateComponent<Constraint>();
    rElbow->SetConstraintType(CONSTRAINT_HINGE);
    rElbow->SetOtherBody(rlaBody);
    rElbow->SetWorldPosition(position + Vector3(0.52f, 1.45f, 0.0f) * scale);
    rElbow->SetAxis(Vector3::UP);
    rElbow->SetOtherAxis(Vector3::UP);
    rElbow->SetLowLimit(Vector2(-90.0f, 0.0f));
    rElbow->SetHighLimit(Vector2(0.0f, 0.0f));
}

// Test 4: Mixed shapes — original test 5 (simplified, no landscape mesh)
void BS_Benchmarks::CreateTest4()
{
    auto* cache = GetSubsystem<ResourceCache>();
    currentTest_ = 4;

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(500.0f, 1.0f, 500.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* sphereMdl = cache->GetResource<Model>("Models/Sphere.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    int size = 10;
    int height = 10;
    float spacing = 4.0f;

    for (int k = 0; k < height; ++k)
    {
        for (int j = 0; j < size; ++j)
        {
            for (int i = 0; i < size; ++i)
            {
                float x = (i - size / 2) * spacing;
                float y = 5.0f + k * spacing;
                float z = (j - size / 2) * spacing;

                int shapeType = (i + j + k) % 3;
                Node* node = scene_->CreateChild("Shape");
                node->SetPosition(Vector3(x, y, z));

                auto* model = node->CreateComponent<StaticModel>();
                auto* body = node->CreateComponent<RigidBody>();
                body->SetMass(1.0f);
                auto* shape = node->CreateComponent<CollisionShape>();

                float s = 0.5f + (shapeType + 1) * 0.3f;
                switch (shapeType)
                {
                case 0: // Box
                    node->SetScale(s * 2.0f);
                    model->SetModel(boxMdl);
                    shape->SetBox(Vector3::ONE);
                    break;
                case 1: // Sphere
                    node->SetScale(s * 2.0f);
                    model->SetModel(sphereMdl);
                    shape->SetSphere(1.0f);
                    break;
                case 2: // Capsule
                    node->SetScale(Vector3(s, s * 2.0f, s));
                    model->SetModel(sphereMdl);
                    shape->SetCapsule(1.0f, 2.0f);
                    break;
                }
                model->SetMaterial(mat);
            }
        }
    }

    cameraNode_->SetPosition(Vector3(0.0f, 40.0f, -80.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

// Test 5: Raycast benchmark — original test 7
void BS_Benchmarks::CreateTest5()
{
    auto* cache = GetSubsystem<ResourceCache>();
    currentTest_ = 5;

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(500.0f, 1.0f, 500.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    // Scatter 500 boxes for raycasting
    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    for (int i = 0; i < 500; ++i)
    {
        Node* node = scene_->CreateChild("RayTarget");
        float x = (Random() - 0.5f) * 100.0f;
        float y = Random() * 30.0f + 1.0f;
        float z = (Random() - 0.5f) * 100.0f;
        node->SetPosition(Vector3(x, y, z));
        float s = Random() * 2.0f + 1.0f;
        node->SetScale(s);

        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(boxMdl);
        model->SetMaterial(mat);
        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        auto* shape = node->CreateComponent<CollisionShape>();
        shape->SetBox(Vector3::ONE);
    }

    cameraNode_->SetPosition(Vector3(0.0f, 40.0f, -80.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

void BS_Benchmarks::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Benchmarks::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Benchmarks, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Benchmarks, HandlePostRenderUpdate));
}

void BS_Benchmarks::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    // Switch test with keys 1-5
    auto* input = GetSubsystem<Input>();
    int newTest = 0;
    if (input->GetKeyPress(KEY_1)) newTest = 1;
    if (input->GetKeyPress(KEY_2)) newTest = 2;
    if (input->GetKeyPress(KEY_3)) newTest = 3;
    if (input->GetKeyPress(KEY_4)) newTest = 4;
    if (input->GetKeyPress(KEY_5)) newTest = 5;

    if (newTest && newTest != currentTest_)
    {
        ClearScene();
        switch (newTest)
        {
        case 1: CreateTest1(); break;
        case 2: CreateTest2(); break;
        case 3: CreateTest3(); break;
        case 4: CreateTest4(); break;
        case 5: CreateTest5(); break;
        }
    }

    // Raycast benchmark on test 5
    if (currentTest_ == 5)
    {
        auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
        PhysicsRaycastResult result;
        for (int i = 0; i < 500; ++i)
        {
            float angle = i * M_PI * 2.0f / 500.0f;
            Vector3 dir(cosf(angle), -0.5f, sinf(angle));
            dir.Normalize();
            physicsWorld->RaycastSingle(result, Ray(Vector3(0.0f, 50.0f, 0.0f), dir), 2500.0f);
        }
    }

    // Stats
    if (statsText_)
    {
        Urho3D::Vector<RigidBody*> bodies;
        scene_->GetComponents<RigidBody>(bodies, true);
        int bodyCount = bodies.Size();

        float fps = 1.0f / Max(timeStep, 0.001f);
        statsText_->SetText(String("Test ") + String(currentTest_) + " | Bodies: " + String(bodyCount) + " | FPS: " + String((int)fps));
    }

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_Benchmarks::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);

    // Draw raycast lines in test 5
    if (currentTest_ == 5)
    {
        auto* debug = scene_->GetComponent<DebugRenderer>();
        for (int i = 0; i < 500; ++i)
        {
            float angle = i * M_PI * 2.0f / 500.0f;
            Vector3 dir(cosf(angle), -0.5f, sinf(angle));
            dir.Normalize();
            debug->AddLine(Vector3(0.0f, 50.0f, 0.0f), Vector3(0.0f, 50.0f, 0.0f) + dir * 100.0f, Color::RED);
        }
    }
}
