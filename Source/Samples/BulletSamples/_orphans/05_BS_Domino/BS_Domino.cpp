// Domino effect — demonstrates rigid body collision propagation.
// A curved row of 50 thin boxes toppled by a sphere impulse.

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
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "BS_Domino.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Domino)

static const int NUM_DOMINOES = 50;
static const float DOMINO_SPACING = 0.8f;
static const float DOMINO_HEIGHT = 2.0f;
static const float DOMINO_WIDTH = 1.0f;
static const float DOMINO_DEPTH = 0.15f;

void BS_Domino::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Domino Effect — 50 dominoes in a spiral\n"
        "Press ENTER to push the trigger ball\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Domino::CreateScene()
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
        groundNode->SetScale(Vector3(100.0f, 1.0f, 100.0f));
        auto* groundModel = groundNode->CreateComponent<StaticModel>();
        groundModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        groundModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

        auto* groundBody = groundNode->CreateComponent<RigidBody>();
        groundBody->SetBoxShape(Vector3::ONE);
        groundBody->SetFriction(0.8f);
    }

    // === Curved row of dominoes ===
    // Spiral path: radius increases, angle steps
    float radius = 5.0f;
    float angleStep = 0.15f;  // radians between dominoes
    float radiusGrowth = 0.08f;

    for (int i = 0; i < NUM_DOMINOES; i++)
    {
        float angle = (float)i * angleStep;
        float r = radius + (float)i * radiusGrowth;
        float x = r * cosf(angle);
        float z = r * sinf(angle);

        // Face the next domino (tangent direction)
        float nextAngle = (float)(i + 1) * angleStep;
        float nextR = radius + (float)(i + 1) * radiusGrowth;
        float nextX = nextR * cosf(nextAngle);
        float nextZ = nextR * sinf(nextAngle);
        float facing = atan2f(nextX - x, nextZ - z) * (180.0f / M_PI);

        Node* node = scene_->CreateChild("Domino");
        node->SetPosition(Vector3(x, DOMINO_HEIGHT / 2.0f, z));
        node->SetRotation(Quaternion(facing, Vector3::UP));
        node->SetScale(Vector3(DOMINO_WIDTH, DOMINO_HEIGHT, DOMINO_DEPTH));

        auto* model = node->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        model->SetCastShadows(true);

        auto* body = node->CreateComponent<RigidBody>();
        body->SetMass(0.5f);
        body->SetBoxShape(Vector3::ONE);
        body->SetFriction(0.6f);
        body->SetRollingFriction(0.01f);
    }

    // === Trigger ball (pushed by pressing Enter) ===
    // First domino is at (radius, h/2, 0) facing ~+Z along the spiral tangent.
    // Place ball behind it (-Z) so it pushes the domino forward into the chain.
    {
        Node* ballNode = scene_->CreateChild("TriggerBall");
        ballNode->SetPosition(Vector3(radius, DOMINO_HEIGHT * 0.4f, -1.5f));

        auto* model = ballNode->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
        model->SetMaterial(cache->GetResource<Material>("Materials/StoneEnvMapSmall.xml"));
        model->SetCastShadows(true);

        auto* body = ballNode->CreateComponent<RigidBody>();
        body->SetMass(3.0f);
        body->SetSphereShape(1.0f);
        body->SetFriction(0.5f);
    }

    // Camera — overhead view of the spiral
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 20.0f, -15.0f));
    cameraNode_->LookAt(Vector3(0.0f, 0.0f, 5.0f));
}

void BS_Domino::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Domino::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Domino, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Domino, HandlePostRenderUpdate));
}

void BS_Domino::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    // Push the trigger ball toward the first domino
    auto* input = GetSubsystem<Input>();
    if (input->GetKeyPress(KEY_RETURN) && !triggered_)
    {
        triggered_ = true;
        Node* ball = scene_->GetChild("TriggerBall");
        if (ball)
        {
            auto* body = ball->GetComponent<RigidBody>();
            if (body)
            {
                // Push in +Z to topple the first domino forward along the spiral
                body->ApplyImpulse(Vector3(0.0f, 0.0f, 10.0f));
            }
        }
    }
}

void BS_Domino::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
