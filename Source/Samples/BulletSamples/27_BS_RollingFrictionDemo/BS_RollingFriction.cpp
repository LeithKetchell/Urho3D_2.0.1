// Port of bullet3/examples/RollingFrictionDemo/RollingFrictionDemo.cpp
// Original by Erwin Coumans — zlib license
//
// Original setup (Z-up, converted to Y-up here):
//   Ground 1: btBoxShape(10,5,25) at (0,0,-28), tilt 0.03*pi around Y, friction 0.5
//   Ground 2: btBoxShape(100,100,50) at (0,0,-54), friction 0.1
//   Dynamic: 125 mixed shapes (sphere, capsule, cone, cylinder in 3 orientations each)
//            mass 1.0, friction 1.0, rollingFriction 0.1, spinningFriction 0.1
//            5x5x5 grid, spacing 2.0
//   Gravity: (0,0,-10) → (0,-10,0) in Y-up
//   Camera: dist 35, yaw 0, pitch -14, target (0,0,0)
//
// Collision dimensions match original exactly. Visual models use node scale
// to approximate collision size; rotated variants (X/Z) have visual-physics
// orientation mismatch — use debug rendering (Space) to see true shapes.

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
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include <Bullet/BulletCollision/CollisionShapes/btCollisionShape.h>
#include <Bullet/BulletDynamics/Dynamics/btRigidBody.h>

#include "BS_RollingFriction.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_RollingFriction)

void BS_RollingFriction::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "Bullet RollingFrictionDemo — spheres on tilted ramps\n"
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

void BS_RollingFriction::CreateScene()
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
    light->SetColor(Color(1.0f, 1.0f, 1.0f));
    light->SetSpecularIntensity(1.0f);

    // Ground 1 — inclined ramp
    // Original: btBoxShape(10,5,25) at (0,0,-28), tilt 0.03*pi around Y-axis (Z-up)
    // Y-up: position (0,-28,0), tilt around Z-axis (FORWARD)
    Node* rampNode = scene_->CreateChild("Ramp");
    rampNode->SetPosition(Vector3(0.0f, -28.0f, 0.0f));
    rampNode->SetRotation(Quaternion(0.03f * 180.0f, Vector3::FORWARD)); // 0.03*pi rad ≈ 5.4 degrees
    rampNode->SetScale(Vector3(20.0f, 10.0f, 50.0f));
    auto* rampModel = rampNode->CreateComponent<StaticModel>();
    rampModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    rampModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* rampBody = rampNode->CreateComponent<RigidBody>();
    rampBody->SetMass(0.0f);
    rampBody->SetFriction(0.5f);
    auto* rampShape = rampNode->CreateComponent<CollisionShape>();
    rampShape->SetBox(Vector3::ONE);

    // Ground 2 — flat base below ramp
    // Original: btBoxShape(100,100,50) at (0,0,-54)
    // Y-up: position (0,-54,0), half-extents (100,50,100) → size (200,100,200)
    Node* baseNode = scene_->CreateChild("Base");
    baseNode->SetPosition(Vector3(0.0f, -54.0f, 0.0f));
    baseNode->SetScale(Vector3(200.0f, 100.0f, 200.0f));
    auto* baseModel = baseNode->CreateComponent<StaticModel>();
    baseModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    baseModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* baseBody = baseNode->CreateComponent<RigidBody>();
    baseBody->SetMass(0.0f);
    baseBody->SetFriction(0.1f);
    auto* baseShape = baseNode->CreateComponent<CollisionShape>();
    baseShape->SetBox(Vector3::ONE);

    // 125 dynamic shapes — 10 shape types cycling, 5x5x5 grid
    // Original positions (Z-up): (2i+start_x, 2j+start_z, 20+2k+start_y)
    //   start_x = -5 - 5/2 = -7 (integer div), start_y = -5, start_z = -3 - 5/2 = -5
    //   Z-up: (2i-7, 2j-5, 15+2k) → Y-up (swap Y↔Z): (2i-7, 15+2k, 2j-5)
    // Original pre-increments shapeIndex before first array access
    const Quaternion rotX(90.0f, Vector3::FORWARD);  // Y→X orientation
    const Quaternion rotZ(90.0f, Vector3::RIGHT);    // Y→Z orientation

    // Shape dimensions use model unit sizes, node scale brings them to world size.
    // Stock models: Sphere/Cylinder/Cone all have diameter=2.0, height=2.0
    // Capsule.mdl: diameter=1.0, total height=2.0
    //
    // Original Bullet dimensions → Urho3D SetXxx params + node scale:
    //   btSphereShape(0.5)              → SetSphere(2.0), scale 0.5
    //   btCapsuleShape(0.25, 0.5)       → SetCapsule(1.0, 2.0), scale 0.5
    //   btConeShape(0.25, 0.5)          → SetCone(2.0, 2.0), scale 0.25
    //   btCylinderShape(0.25,0.5,0.25)  → SetCylinder(2.0, 2.0), scale (0.25, 0.5, 0.25)

    int shapeIndex = 0;
    for (int k = 0; k < 5; ++k)
    {
        for (int i = 0; i < 5; ++i)
        {
            for (int j = 0; j < 5; ++j)
            {
                Node* node = scene_->CreateChild("Shape");
                node->SetPosition(Vector3(-7.0f + 2.0f * i, 15.0f + 2.0f * k, -5.0f + 2.0f * j));

                auto* body = node->CreateComponent<RigidBody>();
                body->SetMass(1.0f);
                body->SetFriction(1.0f);
                body->SetRollingFriction(0.1f);
                body->SetSpinningFriction(0.1f);

                auto* shape = node->CreateComponent<CollisionShape>();
                auto* model = node->CreateComponent<StaticModel>();
                model->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
                model->SetCastShadows(true);

                // Pre-increment (original does shapeIndex++ before colShapes[shapeIndex % 10])
                ++shapeIndex;

                switch (shapeIndex % 10)
                {
                case 0: // Sphere r=0.5
                    shape->SetSphere(2.0f);
                    model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
                    node->SetScale(0.5f);
                    break;
                case 1: // Capsule Y: r=0.25, cyl_h=0.5, total=1.0
                    shape->SetCapsule(1.0f, 2.0f);
                    model->SetModel(cache->GetResource<Model>("Models/Capsule.mdl"));
                    node->SetScale(0.5f);
                    break;
                case 2: // Capsule X
                    shape->SetCapsule(1.0f, 2.0f, Vector3::ZERO, rotX);
                    model->SetModel(cache->GetResource<Model>("Models/Capsule.mdl"));
                    node->SetScale(0.5f);
                    break;
                case 3: // Capsule Z
                    shape->SetCapsule(1.0f, 2.0f, Vector3::ZERO, rotZ);
                    model->SetModel(cache->GetResource<Model>("Models/Capsule.mdl"));
                    node->SetScale(0.5f);
                    break;
                case 4: // Cone Y: r=0.25, h=0.5
                    shape->SetCone(2.0f, 2.0f);
                    model->SetModel(cache->GetResource<Model>("Models/Cone.mdl"));
                    node->SetScale(0.25f);
                    break;
                case 5: // Cone X
                    shape->SetCone(2.0f, 2.0f, Vector3::ZERO, rotX);
                    model->SetModel(cache->GetResource<Model>("Models/Cone.mdl"));
                    node->SetScale(0.25f);
                    break;
                case 6: // Cone Z
                    shape->SetCone(2.0f, 2.0f, Vector3::ZERO, rotZ);
                    model->SetModel(cache->GetResource<Model>("Models/Cone.mdl"));
                    node->SetScale(0.25f);
                    break;
                case 7: // Cylinder Y: halfExtents(0.25, 0.5, 0.25)
                    shape->SetCylinder(2.0f, 2.0f);
                    model->SetModel(cache->GetResource<Model>("Models/Cylinder.mdl"));
                    node->SetScale(Vector3(0.25f, 0.5f, 0.25f));
                    break;
                case 8: // Cylinder X: halfExtents(0.5, 0.25, 0.25)
                    shape->SetCylinder(2.0f, 2.0f, Vector3::ZERO, rotX);
                    model->SetModel(cache->GetResource<Model>("Models/Cylinder.mdl"));
                    node->SetScale(Vector3(0.25f, 0.5f, 0.25f));
                    break;
                case 9: // Cylinder Z: halfExtents(0.25, 0.25, 0.5)
                    shape->SetCylinder(2.0f, 2.0f, Vector3::ZERO, rotZ);
                    model->SetModel(cache->GetResource<Model>("Models/Cylinder.mdl"));
                    node->SetScale(Vector3(0.25f, 0.5f, 0.25f));
                    break;
                }

                // Anisotropic rolling friction (matches original)
                btCollisionShape* btShape = shape->GetCollisionShape();
                if (btShape)
                {
                    body->GetBody()->setAnisotropicFriction(
                        btShape->getAnisotropicRollingFrictionDirection(),
                        btCollisionObject::CF_ANISOTROPIC_ROLLING_FRICTION);
                }
            }
        }
    }

    // Camera — original: dist 35, yaw 0, pitch -14, target (0,0,0)
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 10.0f, -35.0f));
    cameraNode_->SetRotation(Quaternion(25.0f, 0.0f, 0.0f));
}

void BS_RollingFriction::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_RollingFriction::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_RollingFriction, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_RollingFriction, HandlePostRenderUpdate));
}

void BS_RollingFriction::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_RollingFriction::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
