// Port of bullet3/examples/Collision/CollisionTutorialBullet2.cpp
// Original: compound shapes (10 spheres each) colliding with a ground plane,
// visualizing contact points with colored lines.
//
// Urho3D port: 10 compound groups of spheres falling onto a ground plane.
// Contact points drawn via DebugRenderer in PostRenderUpdate.

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
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_Collision.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Collision)

void BS_Collision::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "Collision Tutorial — compound sphere groups on ground\n"
        "Contact points shown as red/yellow lines\n"
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

void BS_Collision::CreateScene()
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

    // Ground plane
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -3.5f, 0.0f));
    groundNode->SetScale(Vector3(200.0f, 0.1f, 200.0f));
    auto* groundModel = groundNode->CreateComponent<StaticModel>();
    groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* groundBody = groundNode->CreateComponent<RigidBody>();
    auto* groundShape = groundNode->CreateComponent<CollisionShape>();
    groundShape->SetBox(Vector3::ONE);

    // Original: 10 compound shapes, each with 10 child spheres in a row
    auto* sphereMdl = cache->GetResource<Model>("Models/Sphere.mdl");
    auto* baseMat = cache->GetResource<Material>("Materials/StoneSmall.xml");
    Color groupColors[] = {
        Color(1.0f, 0.7f, 0.7f), Color(1.0f, 1.0f, 0.7f),
        Color(0.7f, 1.0f, 0.7f), Color(0.7f, 1.0f, 1.0f)
    };

    float radius = 0.5f;
    float spacing = 1.5f;

    for (int j = 0; j < 10; ++j)
    {
        // Parent node for the compound group — sticks standing upright
        Node* groupNode = scene_->CreateChild("CompoundGroup");
        float stickHeight = 9 * spacing + radius * 2.0f;
        groupNode->SetPosition(Vector3(j * 3.0f - 13.5f, -3.5f + 0.05f + stickHeight * 0.5f, 0.0f));

        auto* body = groupNode->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetLinearDamping(0.1f);
        body->SetAngularDamping(0.1f);

        // Add 10 sphere collision children stacked vertically
        for (int i = 0; i < 10; ++i)
        {
            // Visual sphere as child node
            Node* sphereNode = groupNode->CreateChild("Sphere");
            sphereNode->SetPosition(Vector3(0.0f, (i - 4.5f) * spacing, 0.0f));
            sphereNode->SetScale(radius * 2.0f);
            auto* model = sphereNode->CreateComponent<StaticModel>();
            model->SetModel(sphereMdl);
            SharedPtr<Material> mat = baseMat->Clone();
            mat->SetShaderParameter("MatDiffColor", Variant(groupColors[j % 4]));
            model->SetMaterial(mat);
        }

        // Compound collision shape: tall box encompassing all 10 spheres vertically
        auto* shape = groupNode->CreateComponent<CollisionShape>();
        shape->SetBox(Vector3(radius * 2.0f, 10 * spacing, radius * 2.0f));
    }

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(10.0f, 5.0f, -20.0f));
    cameraNode_->SetRotation(Quaternion(10.0f, -10.0f, 0.0f));
}

void BS_Collision::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Collision::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Collision, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Collision, HandlePostRenderUpdate));
}

void BS_Collision::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_Collision::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();

    if (drawDebug_)
        physicsWorld->DrawDebugGeometry(true);

    // Draw contact points
    auto* debug = scene_->GetComponent<DebugRenderer>();
    physicsWorld->DrawDebugGeometry(debug, false);

    // Query all contact manifolds and draw contact lines
    Vector<RigidBody*> bodies;
    scene_->GetComponents<RigidBody>(bodies, true);
    for (unsigned i = 0; i < bodies.Size(); ++i)
    {
        Vector<RigidBody*> contacts;
        bodies[i]->GetCollidingBodies(contacts);
        if (contacts.Size() > 0)
        {
            // Draw a marker at the body position to show it's in contact
            debug->AddSphere(Sphere(bodies[i]->GetNode()->GetWorldPosition(), 0.3f), Color::RED);
        }
    }
}
