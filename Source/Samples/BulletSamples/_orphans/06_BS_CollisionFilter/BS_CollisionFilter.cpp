// Collision filtering — demonstrates layer/mask collision groups.
// Red, green, and blue objects each collide with the ground but
// only selectively with each other.

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

#include "BS_CollisionFilter.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_CollisionFilter)

// Collision layers (bit masks)
static const unsigned LAYER_GROUND = 1;
static const unsigned LAYER_RED    = 2;
static const unsigned LAYER_GREEN  = 4;
static const unsigned LAYER_BLUE   = 8;

void BS_CollisionFilter::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Collision Filtering — layer/mask groups\n"
        "Red & Blue pass through each other\n"
        "Green collides with everything\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_CollisionFilter::CreateScene()
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

    // Create colored materials by cloning a base lit material
    auto* baseMat = cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml");
    auto redMat = baseMat->Clone("Red");
    redMat->SetShaderParameter("MatDiffColor", Color(1.0f, 0.2f, 0.2f));
    auto greenMat = baseMat->Clone("Green");
    greenMat->SetShaderParameter("MatDiffColor", Color(0.2f, 1.0f, 0.2f));
    auto blueMat = baseMat->Clone("Blue");
    blueMat->SetShaderParameter("MatDiffColor", Color(0.3f, 0.3f, 1.0f));

    // Ground — collides with ALL groups
    {
        Node* groundNode = scene_->CreateChild("Ground");
        groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
        groundNode->SetScale(Vector3(40.0f, 1.0f, 40.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetCollisionLayer(LAYER_GROUND);
        groundBody->SetCollisionMask(LAYER_RED | LAYER_GREEN | LAYER_BLUE);
    }

    // Demo layout:
    // LEFT SIDE (x=-4): Red box drops onto green platform — stops (red collides with green)
    // CENTER (x=0):     Red and blue boxes drop from same height — pass through each other
    // RIGHT SIDE (x=4): Blue box drops onto green platform — stops (blue collides with green)

    // Green platforms (static) at left and right — collide with red and blue
    for (float x : {-4.0f, 4.0f})
    {
        Node* node = scene_->CreateChild("GreenPlatform");
        node->SetPosition(Vector3(x, 3.0f, 0.0f));
        node->SetScale(Vector3(3.0f, 0.3f, 3.0f));
        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(greenMat);
        model->SetCastShadows(true);
        auto* body = node->CreateComponent<RigidBody>();
        body->SetBoxShape(Vector3::ONE);
        body->SetCollisionLayer(LAYER_GREEN);
        body->SetCollisionMask(LAYER_GROUND | LAYER_RED | LAYER_BLUE);
    }

    // Left side: red box above green platform — should land on it
    {
        Node* node = scene_->CreateChild("RedOnGreen");
        node->SetPosition(Vector3(-4.0f, 8.0f, 0.0f));
        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(redMat);
        model->SetCastShadows(true);
        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetBoxShape(Vector3::ONE);
        body->SetCollisionLayer(LAYER_RED);
        body->SetCollisionMask(LAYER_GROUND | LAYER_GREEN);
    }

    // Right side: blue box above green platform — should land on it
    {
        Node* node = scene_->CreateChild("BlueOnGreen");
        node->SetPosition(Vector3(4.0f, 8.0f, 0.0f));
        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(blueMat);
        model->SetCastShadows(true);
        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetBoxShape(Vector3::ONE);
        body->SetCollisionLayer(LAYER_BLUE);
        body->SetCollisionMask(LAYER_GROUND | LAYER_GREEN);
    }

    // Center: red and blue boxes at same X, staggered Y — should pass through each other
    {
        Node* node = scene_->CreateChild("RedCenter");
        node->SetPosition(Vector3(0.0f, 10.0f, 0.0f));
        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
        model->SetMaterial(redMat);
        model->SetCastShadows(true);
        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetSphereShape(1.0f);
        body->SetCollisionLayer(LAYER_RED);
        body->SetCollisionMask(LAYER_GROUND | LAYER_GREEN);
    }
    {
        Node* node = scene_->CreateChild("BlueCenter");
        node->SetPosition(Vector3(0.0f, 6.0f, 0.0f));
        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
        model->SetMaterial(blueMat);
        model->SetCastShadows(true);
        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetSphereShape(1.0f);
        body->SetCollisionLayer(LAYER_BLUE);
        body->SetCollisionMask(LAYER_GROUND | LAYER_GREEN);
    }

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 8.0f, -15.0f));
    cameraNode_->LookAt(Vector3(0.0f, 3.0f, 0.0f));
}

void BS_CollisionFilter::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_CollisionFilter::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_CollisionFilter, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_CollisionFilter, HandlePostRenderUpdate));
}

void BS_CollisionFilter::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_CollisionFilter::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
