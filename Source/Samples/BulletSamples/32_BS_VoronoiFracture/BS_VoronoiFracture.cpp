// Simplified port of bullet3/examples/VoronoiFracture/VoronoiFractureDemo.cpp
// Original: generates Voronoi cells from convex hull, connects with breakable constraints.
// Urho3D port: approximates with randomly-sized box pieces in a cube formation,
// connected by point constraints. Drop heavy objects to shatter.

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

#include <Bullet/BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/UI.h>

#include "BS_VoronoiFracture.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_VoronoiFracture)

void BS_VoronoiFracture::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "VoronoiFracture — pre-shattered breakable objects\n"
        "F=drop heavy ball to shatter | LMB=throw\n"
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

void BS_VoronoiFracture::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();
    auto* physicsWorld = scene_->CreateComponent<PhysicsWorld>();
    physicsWorld->SetGravity(Vector3(0.0f, -10.0f, 0.0f));
    physicsWorld->SetNumIterations(20);

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

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    groundNode->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundNode->GetComponent<StaticModel>()->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    groundNode->CreateComponent<RigidBody>()->SetFriction(0.6f);
    groundNode->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

    // Create several shattered objects
    CreateShatteredObject(Vector3(-4.0f, 4.0f, 0.0f), 2.0f, 27);
    CreateShatteredObject(Vector3(0.0f, 4.0f, 0.0f), 2.0f, 27);
    CreateShatteredObject(Vector3(4.0f, 4.0f, 0.0f), 2.0f, 27);
    CreateShatteredObject(Vector3(-2.0f, 8.0f, 0.0f), 1.5f, 8);
    CreateShatteredObject(Vector3(2.0f, 8.0f, 0.0f), 1.5f, 8);

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 6.0f, -12.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));
}

void BS_VoronoiFracture::CreateShatteredObject(const Vector3& position, float size, int numPieces)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* baseMat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    // Determine grid dimensions that give approximately numPieces
    int side = (int)ceilf(cbrtf((float)numPieces));
    float pieceSize = size / side;

    Vector<Node*> pieces;

    for (int x = 0; x < side; ++x)
    {
        for (int y = 0; y < side; ++y)
        {
            for (int z = 0; z < side; ++z)
            {
                // Random size variation
                float scale = pieceSize * (0.7f + Random() * 0.6f);
                Vector3 offset(
                    (x - side / 2.0f + 0.5f) * pieceSize,
                    (y - side / 2.0f + 0.5f) * pieceSize,
                    (z - side / 2.0f + 0.5f) * pieceSize
                );

                Node* piece = scene_->CreateChild("VoronoiPiece");
                piece->SetPosition(position + offset);
                piece->SetScale(scale);

                piece->CreateComponent<StaticModel>()->SetModel(boxMdl);
                // Random color per piece
                SharedPtr<Material> mat = baseMat->Clone();
                mat->SetShaderParameter("MatDiffColor", Variant(Color(
                    0.4f + Random() * 0.6f,
                    0.4f + Random() * 0.6f,
                    0.4f + Random() * 0.6f
                )));
                piece->GetComponent<StaticModel>()->SetMaterial(mat);

                auto* body = piece->CreateComponent<RigidBody>();
                // Mass proportional to volume
                body->SetMass(scale * scale * scale);
                body->SetFriction(0.6f);
                piece->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

                pieces.Push(piece);
            }
        }
    }

    // Connect adjacent pieces with breakable constraints
    for (unsigned i = 0; i < pieces.Size(); ++i)
    {
        for (unsigned j = i + 1; j < pieces.Size(); ++j)
        {
            float dist = (pieces[i]->GetPosition() - pieces[j]->GetPosition()).Length();
            if (dist < pieceSize * 1.6f)  // Adjacent
            {
                auto* constraint = pieces[i]->CreateComponent<Constraint>();
                constraint->SetConstraintType(CONSTRAINT_POINT);
                constraint->SetOtherBody(pieces[j]->GetComponent<RigidBody>());
                constraint->SetWorldPosition(
                    (pieces[i]->GetPosition() + pieces[j]->GetPosition()) * 0.5f);
                constraint->SetCFM(0.015f);
                constraint->SetERP(0.8f);
                // Make constraints breakable
                auto* btConstraint = constraint->GetConstraint();
                if (btConstraint)
                    btConstraint->setBreakingImpulseThreshold(1.0f);
            }
        }
    }
}

void BS_VoronoiFracture::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_VoronoiFracture::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_VoronoiFracture, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_VoronoiFracture, HandlePostRenderUpdate));
}

void BS_VoronoiFracture::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    auto* input = GetSubsystem<Input>();
    if (input->GetKeyPress(KEY_F))
    {
        // Raycast along camera forward to find drop target
        Vector3 dropPos(0.0f, 15.0f, 0.0f);
        auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
        Vector3 camPos = cameraNode_->GetWorldPosition();
        Vector3 camFwd = cameraNode_->GetWorldDirection();
        PhysicsRaycastResult result;
        physicsWorld->RaycastSingle(result, Ray(camPos, camFwd), 300.0f);
        if (result.body_)
            dropPos = Vector3(result.position_.x_, result.position_.y_ + 15.0f, result.position_.z_);

        auto* cache = GetSubsystem<ResourceCache>();
        Node* ball = scene_->CreateChild("HeavyBall");
        ball->SetPosition(dropPos);
        ball->SetScale(2.0f);
        ball->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
        ball->GetComponent<StaticModel>()->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
        auto* body = ball->CreateComponent<RigidBody>();
        body->SetMass(100.0f);
        ball->CreateComponent<CollisionShape>()->SetSphere(1.0f);
    }

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_VoronoiFracture::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
