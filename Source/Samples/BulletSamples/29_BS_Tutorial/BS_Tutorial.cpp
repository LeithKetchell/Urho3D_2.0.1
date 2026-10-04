// Port of bullet3/examples/Tutorial/Dof6ConstraintTutorial
// Demonstrates the 4 Urho3D constraint types side by side:
//   1. POINT — ball joint (3 DOF rotation)
//   2. HINGE — single axis rotation
//   3. SLIDER — single axis translation
//   4. CONETWIST — limited cone rotation

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

#include "BS_Tutorial.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Tutorial)

void BS_Tutorial::Start()
{
    Sample::Start();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions(
        "Constraint Tutorial — 4 constraint types side by side\n"
        "Left to right: Point, Hinge, Slider, ConeTwist\n"
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

void BS_Tutorial::CreateScene()
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

    // Ground
    Node* groundNode = scene_->CreateChild("Ground");
    groundNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    groundNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    groundNode->CreateComponent<StaticModel>()->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    groundNode->GetComponent<StaticModel>()->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    groundNode->CreateComponent<RigidBody>();
    groundNode->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

    auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
    auto* mat = cache->GetResource<Material>("Materials/StoneSmall.xml");

    float spacing = 6.0f;
    float anchorHeight = 6.0f;
    ConstraintType types[] = {CONSTRAINT_POINT, CONSTRAINT_HINGE, CONSTRAINT_SLIDER, CONSTRAINT_CONETWIST};
    const char* names[] = {"Point", "Hinge", "Slider", "ConeTwist"};
    Color colors[] = {Color::RED, Color::GREEN, Color::BLUE, Color::YELLOW};

    for (int i = 0; i < 4; ++i)
    {
        float x = (i - 1.5f) * spacing;

        // Anchor body (static)
        Node* anchor = scene_->CreateChild("Anchor");
        anchor->SetPosition(Vector3(x, anchorHeight, 0.0f));
        anchor->SetScale(0.5f);
        anchor->CreateComponent<StaticModel>()->SetModel(boxMdl);
        SharedPtr<Material> anchorMat = mat->Clone();
        anchorMat->SetShaderParameter("MatDiffColor", Variant(Color(0.3f, 0.3f, 0.3f)));
        anchor->GetComponent<StaticModel>()->SetMaterial(anchorMat);
        auto* anchorBody = anchor->CreateComponent<RigidBody>();
        anchor->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

        // Dynamic body
        Node* dynNode = scene_->CreateChild(names[i]);
        dynNode->SetPosition(Vector3(x, anchorHeight - 2.5f, 0.0f));
        dynNode->SetScale(1.0f);
        dynNode->CreateComponent<StaticModel>()->SetModel(boxMdl);
        SharedPtr<Material> dynMat = mat->Clone();
        dynMat->SetShaderParameter("MatDiffColor", Variant(colors[i]));
        dynNode->GetComponent<StaticModel>()->SetMaterial(dynMat);
        auto* dynBody = dynNode->CreateComponent<RigidBody>();
        dynBody->SetMass(1.0f);
        dynNode->CreateComponent<CollisionShape>()->SetBox(Vector3::ONE);

        // Constraint
        auto* constraint = anchor->CreateComponent<Constraint>();
        constraint->SetConstraintType(types[i]);
        constraint->SetOtherBody(dynBody);
        constraint->SetWorldPosition(Vector3(x, anchorHeight - 1.0f, 0.0f));

        switch (types[i])
        {
        case CONSTRAINT_POINT:
            // Ball joint — no axis needed
            break;
        case CONSTRAINT_HINGE:
            constraint->SetAxis(Vector3::FORWARD);
            constraint->SetOtherAxis(Vector3::FORWARD);
            constraint->SetLowLimit(Vector2(-90.0f, 0.0f));
            constraint->SetHighLimit(Vector2(90.0f, 0.0f));
            break;
        case CONSTRAINT_SLIDER:
            constraint->SetAxis(Vector3::UP);
            constraint->SetOtherAxis(Vector3::UP);
            constraint->SetLowLimit(Vector2(-3.0f, 0.0f));
            constraint->SetHighLimit(Vector2(0.0f, 0.0f));
            break;
        case CONSTRAINT_CONETWIST:
            constraint->SetAxis(Vector3::DOWN);
            constraint->SetOtherAxis(Vector3::UP);
            constraint->SetHighLimit(Vector2(30.0f, 30.0f));
            break;
        }

        // Give a small push to start motion
        dynBody->ApplyImpulse(Vector3(1.0f, 0.0f, 0.5f));
    }

    // Camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<Camera>()->SetFarClip(300.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 6.0f, -15.0f));
    cameraNode_->SetRotation(Quaternion(15.0f, 0.0f, 0.0f));
}

void BS_Tutorial::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Tutorial::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Tutorial, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Tutorial, HandlePostRenderUpdate));
}

void BS_Tutorial::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();
    MoveCamera(timeStep);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void BS_Tutorial::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
