// Port of bulletphysics/bullet3/examples/Raycast/RaytestDemo.cpp
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
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_Raycast.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Raycast)

// Original shape positions: (i-3)*5 for i=0..5 → -15, -10, -5, 0, 5, 10
// Original shapes: trimesh(static), convexhull, sphere, capsule, cylinder, box
// We use Urho3D shapes: box(static), sphere, sphere, capsule, cylinder, box
// Shape 0 is static (mass=0), shapes 1-5 are dynamic (mass=1)

void BS_Raycast::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Bullet RaytestDemo — 6 shapes with raycasting\n"
        "Red = all-hits ray | Blue = closest-hit ray\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Raycast::CreateScene()
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

    // Ground — original: btBoxShape(50,50,50) at y=-50
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -50.0f, 0.0f));
        groundNode->SetScale(Vector3(100.0f, 100.0f, 100.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(1.0f);
    }

    // === 6 shapes along X axis ===
    // Original positions: (i-3)*5 for i=0..5
    // Original shapes: trimesh, convexhull, sphere(r=1), capsule(0.2,1), cylinder(0.2,1,0.2), box(1,1,1)
    // Shape 0 static (mass=0), shapes 1-5 dynamic (mass=1)
    // We approximate: shape 0=box(static), 1=sphere, 2=sphere(r=1), 3=capsule, 4=cylinder, 5=box
    struct ShapeInfo {
        const char* name;
        float mass;
    };
    ShapeInfo shapes[6] = {
        {"Trimesh(box)", 0.0f},
        {"ConvexHull",   0.0f},
        {"Sphere",       0.0f},
        {"Capsule",      0.0f},
        {"Cylinder",     0.0f},
        {"Box",          0.0f}
    };

    for (int i = 0; i < 6; i++)
    {
        float x = (float)(i - 3) * 5.0f;  // -15, -10, -5, 0, 5, 10
        Node* node = scene_->CreateChild(shapes[i].name);
        node->SetPosition(Vector3(x, 2.0f, 0.0f));

        auto* model = node->CreateComponent<StaticModel>();
        model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        model->SetCastShadows(true);

        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(shapes[i].mass);
        body->SetFriction(1.0f);
        body->SetRollingFriction(0.03f);
        body->SetSpinningFriction(0.03f);

        // Visual and collision shapes matched
        switch (i)
        {
        case 0:  // Static box (trimesh substitute)
            node->SetScale(2.0f);
            model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
            body->SetBoxShape(Vector3::ONE);
            break;
        case 1:  // Convex hull (approximate with sphere)
            model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
            body->SetSphereShape(1.0f);
            break;
        case 2:  // Sphere r=1
            node->SetScale(2.0f);
            model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
            body->SetSphereShape(1.0f);
            break;
        case 3:  // Capsule
            node->SetScale(Vector3(1.0f, 2.0f, 1.0f));
            model->SetModel(cache->GetResource<Model>("Models/Capsule.mdl"));
            body->SetCapsuleShape(1.0f, 2.0f);
            break;
        case 4:  // Cylinder
            node->SetScale(Vector3(1.0f, 2.0f, 1.0f));
            model->SetModel(cache->GetResource<Model>("Models/Cylinder.mdl"));
            body->SetCylinderShape(1.0f, 1.0f);
            break;
        case 5:  // Box (1,1,1)
            model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
            body->SetBoxShape(Vector3::ONE);
            break;
        }
    }

    // Camera — original: dist=18, pitch=-30, yaw=129, target(-4.6,-4.7,-5.75)
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(-5.0f, 5.0f, -20.0f));
    cameraNode_->LookAt(Vector3(-2.0f, 1.0f, 0.0f));
}

void BS_Raycast::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Raycast::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Raycast, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Raycast, HandlePostRenderUpdate));
}

void BS_Raycast::CastRays()
{
    auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!physicsWorld || !debug)
        return;

    // Animate: oscillate the all-hits ray vertically (original: up += 0.01*dir)
    static float up = 0.0f;
    static float dir = 1.0f;
    up += 0.01f * dir;
    if (Abs(up) > 2.0f)
        dir *= -1.0f;

    // Rotate shape 1 (second collision object) — original rotates around Y
    // Find shape node by name
    Node* convexNode = scene_->GetChild("ConvexHull");
    if (convexNode)
    {
        auto* body = convexNode->GetComponent<RigidBody>();
        if (body)
        {
            Quaternion rot = convexNode->GetRotation();
            convexNode->SetRotation(rot * Quaternion(0.5f, Vector3::UP));
            // Keep it awake
            body->Activate();
        }
    }

    // === All-hits ray (red) ===
    // Sweeps through shape centers at y=2
    {
        Vector3 from(-30.0f, 2.0f + up, 0.0f);
        Vector3 to(30.0f, 2.0f, 0.0f);
        Vector3 direction = (to - from).Normalized();
        float maxDist = (to - from).Length();

        debug->AddLine(from, to, Color(0.0f, 0.0f, 0.0f));  // black ray line

        Ray ray(from, direction);
        Vector<PhysicsRaycastResult> results;
        physicsWorld->Raycast(results, ray, maxDist);

        for (unsigned i = 0; i < results.Size(); i++)
        {
            Vector3 hitPos = results[i].position_;
            Vector3 hitNormal = results[i].normal_;
            // Red sphere at hit + red normal line
            debug->AddSphere(Sphere(hitPos, 0.3f), Color::RED);
            debug->AddLine(hitPos, hitPos + hitNormal * 2.0f, Color::RED);
        }
    }

    // === Closest-hit ray (blue) ===
    {
        Vector3 from(-30.0f, 2.2f, 0.0f);
        Vector3 to(30.0f, 2.2f, 0.0f);
        Vector3 direction = (to - from).Normalized();
        float maxDist = (to - from).Length();

        debug->AddLine(from, to, Color(0.0f, 0.0f, 1.0f));  // blue ray line

        Ray ray(from, direction);
        PhysicsRaycastResult result;
        physicsWorld->RaycastSingle(result, ray, maxDist);

        if (result.body_)
        {
            Vector3 hitPos = result.position_;
            Vector3 hitNormal = result.normal_;
            debug->AddSphere(Sphere(hitPos, 0.5f), Color::BLUE);
            debug->AddLine(hitPos, hitPos + hitNormal * 3.0f, Color::BLUE);
            // Draw a cross at hit for visibility
            debug->AddLine(hitPos - Vector3::UP, hitPos + Vector3::UP, Color::BLUE);
            debug->AddLine(hitPos - Vector3::FORWARD, hitPos + Vector3::FORWARD, Color::BLUE);
        }
    }
}

void BS_Raycast::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    auto* input = GetSubsystem<Input>();
    const float MOVE_SPEED = 20.0f;
    const float MOUSE_SENSITIVITY = 0.1f;

    IntVector2 mouseMove = input->GetMouseMove();
    yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
    pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
    pitch_ = Clamp(pitch_, -90.0f, 90.0f);
    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);

    if (input->GetKeyPress(KEY_SPACE))
        drawDebug_ = !drawDebug_;
}

void BS_Raycast::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    // Always cast rays (they draw debug lines)
    CastRays();

    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
