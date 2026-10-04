// Radial gravity demo — per-body gravity pointing toward a planet center.
// Uses Bullet's btRigidBody::setGravity() via Urho3D's SetGravityOverride().

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
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_PlanetGravity.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_PlanetGravity)

void BS_PlanetGravity::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Planet Gravity — radial gravity toward center\n"
        "40 boxes fall toward a sphere\n"
        "Uses SetGravityOverride() per body each frame\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_PlanetGravity::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();

    // Zero world gravity — we apply per-body gravity in PreStep
    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3::ZERO);

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.2f, 0.2f, 0.25f));
    zone->SetFogColor(Color(0.05f, 0.05f, 0.1f));
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

    // Planet — large static sphere at the origin
    planetCenter_ = Vector3::ZERO;
    planetNode_ = scene_->CreateChild("Planet");
    planetNode_->SetPosition(planetCenter_);
    // Visual: use a scaled box as sphere approximation (we have Box.mdl)
    // The collision shape IS a sphere
    planetNode_->SetScale(Vector3(planetRadius_ * 2.0f, planetRadius_ * 2.0f, planetRadius_ * 2.0f));
    auto* planetModel = planetNode_->CreateComponent<StaticModel>();
    planetModel->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
    planetModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

    auto* planetBody = planetNode_->CreateComponent<RigidBody>();
    planetBody->SetSphereShape(1.0f);

    // Scatter boxes in orbit — spawn at varying distances and heights
    int numObjects = 40;
    for (int i = 0; i < numObjects; i++)
    {
        float angle = (float)i * (2.0f * M_PI / (float)numObjects);
        // Two rings at different heights
        float height = (i % 2 == 0) ? 0.0f : 3.0f;
        float dist = planetRadius_ + 5.0f + (float)(i % 5) * 2.0f;

        float x = dist * cosf(angle);
        float z = dist * sinf(angle);

        Node* node = scene_->CreateChild("Orbiter");
        float scale = 0.3f + (float)(i % 3) * 0.15f;
        node->SetPosition(Vector3(x, height, z));
        node->SetScale(Vector3(scale, scale, scale));

        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        model->SetCastShadows(true);

        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetBoxShape(Vector3::ONE);
        body->SetFriction(0.8f);
        body->SetLinearDamping(0.05f);

        orbiters_.Push(node);
    }

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 20.0f, -35.0f));
    cameraNode_->LookAt(Vector3(0.0f, 0.0f, 0.0f));
}

void BS_PlanetGravity::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_PlanetGravity::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_PlanetGravity, HandleUpdate));
    SubscribeToEvent(E_PHYSICSPRESTEP, URHO3D_HANDLER(BS_PlanetGravity, HandlePhysicsPreStep));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_PlanetGravity, HandlePostRenderUpdate));
}

void BS_PlanetGravity::HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData)
{
    // Update gravity for every dynamic body to point toward planet center
    for (unsigned i = 0; i < orbiters_.Size(); i++)
    {
        auto* body = orbiters_[i]->GetComponent<RigidBody>();
        if (!body || body->GetMass() <= 0.0f)
            continue;

        Vector3 pos = orbiters_[i]->GetWorldPosition();
        Vector3 dir = planetCenter_ - pos;
        float dist = dir.Length();
        if (dist < 0.01f)
            continue;

        // Gravity = G * M / r^2, simplified to constant strength
        Vector3 gravity = dir.Normalized() * gravityStrength_;
        body->SetGravityOverride(gravity);
    }
}

void BS_PlanetGravity::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_PlanetGravity::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
