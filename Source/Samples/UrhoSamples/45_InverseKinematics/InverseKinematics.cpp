// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
//
// Sample 45: Animation-Driven Ragdoll
// Demonstrates per-bone physics bodies driven by animation via spring constraints.
// Foot planting is emergent from foot bodies colliding with the terrain surface.
//
// Phase 1 stub: bone shapes are hardcoded for the Mixamo skeleton.
// Leith's OBB generator will replace hardcoded sizes with mesh-derived volumes.

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Skeleton.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/Constraint.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Graphics/BoneCollisionGenerator.h>
#include <Urho3D/Graphics/SkeletonRemapper.h>

#include "InverseKinematics.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

URHO3D_DEFINE_APPLICATION_MAIN(InverseKinematics)

// Stubbed bone dimensions for Mixamo skeleton.
// Format: boneName, shapeType (0=capsule, 1=sphere), diameter, height
// Old hardcoded bone shapes removed — now generated from vertex weights
// via BoneCollisionGenerator::Generate()

InverseKinematics::InverseKinematics(Context* context) :
    Sample(context)
{
}

void InverseKinematics::Start()
{
    Sample::Start();

    CreateScene();
    CreateInstructions();
    SetupViewport();
    SubscribeToEvents();

    Sample::InitMouseMode(MM_RELATIVE);
    GetSubsystem<Input>()->SetMouseVisible(true);

    // Load UI style for ProfilerUI
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);
}

void InverseKinematics::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();
    scene_->CreateComponent<PhysicsWorld>();

    // Floor with collision
    floorNode_ = scene_->CreateChild("Floor");
    floorNode_->SetScale(Vector3(50.0f, 1.0f, 50.0f));
    auto* planeObject = floorNode_->CreateComponent<StaticModel>();
    planeObject->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    planeObject->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    floorNode_->CreateComponent<RigidBody>();
    auto* floorShape = floorNode_->CreateComponent<CollisionShape>();
    floorShape->SetBox(Vector3(1, 0, 1));

    // Directional light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetCastShadows(true);
    light->SetShadowBias(BiasParameters(0.00005f, 0.5f));
    light->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));

    // Generate bone collision shapes and create bodies — CavemanMan
    {
        static const char* shapeNames[] = {"box", "sphere", "capsule"};
        URHO3D_LOGINFO("=== BoneCollision: CavemanWoman ===");
        auto* caveModel = cache->GetResource<Model>("Models/Characters/CavemanWoman.mdl");
        if (caveModel)
        {
            characterNode_ = scene_->CreateChild("CavemanWoman");
            characterNode_->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
            auto* caveMdl = characterNode_->CreateComponent<AnimatedModel>();
            caveMdl->SetModel(caveModel, true, true);
            caveMdl->ApplyMaterialList("Models/Characters/CavemanWoman.txt");
            caveMdl->SetCastShadows(true);

            // Normalize to ~1.7m (human height) using Creature::Start() pattern
            BoundingBox bb = caveMdl->GetBoundingBox();
            float longest = Max(bb.Size().x_, Max(bb.Size().y_, bb.Size().z_));
            if (longest > 0.001f)
                characterNode_->SetScale(1.7f / longest);

            // Feet sit at y=0 — offset only if min_.y_ is negative (origin above feet)
            float scale = characterNode_->GetScale().x_;
            if (bb.min_.y_ < -0.001f)
                characterNode_->SetPosition(Vector3(0.0f, -bb.min_.y_ * scale, 0.0f));

            animCtrl_ = characterNode_->CreateComponent<AnimationController>();
            animCtrl_->PlayExclusive("Models/Characters/Caveman_walk.ani", 0, true, 0.0f);

            Node* caveNode = characterNode_;

            // Generate bone bounds — no physics, just data for debug draw
            boneBoundsData_ = BoneCollisionGenerator::Generate(caveModel);
            URHO3D_LOGINFOF("  [CavemanWoman] %u bones with vertices", boneBoundsData_.Size());
        }
        else
        {
            URHO3D_LOGWARNING("CavemanMan.mdl not found — skipping comparison");
        }
    }

    // Cross-skeleton shape mapping test: Jack (Bip01 rig) → CavemanMan (Mixamo rig)
    {
        auto* jackModel = cache->GetResource<Model>("Models/Jack.mdl");
        auto* caveModel = cache->GetResource<Model>("Models/Characters/CavemanMan.mdl");
        if (jackModel && caveModel)
        {
            URHO3D_LOGINFO("=== SkeletonRemapper: Jack → CavemanMan ===");
            Vector<BoneMapping> mapping = SkeletonRemapper::BuildMapping(jackModel, caveModel);
            SkeletonRemapper::LogMapping(jackModel, caveModel, mapping);
        }
    }

    // Camera
    cameraRotateNode_ = scene_->CreateChild("CameraRotate");
    cameraNode_ = cameraRotateNode_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();
    cameraNode_->SetPosition(Vector3(0, 0, -4));
    cameraRotateNode_->SetPosition(Vector3(0, 0.8f, 0));
    pitch_ = 20;
    yaw_ = 50;
}

void InverseKinematics::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    auto* instructionText = ui->GetRoot()->CreateChild<Text>();
    instructionText->SetText(
        "Left-Click and drag to look around\n"
        "Right-Click and drag to change incline\n"
        "Press Space to reset floor\n"
        "Press D to toggle bone boxes");
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetVerticalAlignment(VA_CENTER);
    instructionText->SetPosition(0, ui->GetRoot()->GetHeight() / 4);
}

void InverseKinematics::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void InverseKinematics::UpdateCameraAndFloor(float /*timeStep*/)
{
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    auto* input = GetSubsystem<Input>();
    const float MOUSE_SENSITIVITY = 0.1f;

    if (input->GetMouseButtonDown(MOUSEB_LEFT))
    {
        IntVector2 mouseMove = input->GetMouseMove();
        yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
        pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
        pitch_ = Clamp(pitch_, -90.0f, 90.0f);
    }

    if (input->GetMouseButtonDown(MOUSEB_RIGHT))
    {
        IntVector2 mouseMoveInt = input->GetMouseMove();
        floorPitch_ += MOUSE_SENSITIVITY * mouseMoveInt.y_;
        floorPitch_ = Clamp(floorPitch_, -90.0f, 90.0f);
        floorRoll_ += MOUSE_SENSITIVITY * mouseMoveInt.x_;
    }

    if (input->GetKeyPress(KEY_SPACE))
    {
        if (!ragdollActive_)
            ActivateRagdoll();
    }

    if (input->GetKeyPress(KEY_D))
        drawDebug_ = !drawDebug_;

    cameraRotateNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
    floorNode_->SetRotation(Quaternion(floorPitch_, 0, floorRoll_));
}

void InverseKinematics::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(InverseKinematics, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(InverseKinematics, HandlePostRenderUpdate));
}

void InverseKinematics::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    UpdateCameraAndFloor(timeStep);

    // Update profiler
    if (profilerUI_)
    {
        GetSubsystem<Graphics>()->GetVulkanProfiler()->RecordFrame(timeStep);
        profilerUI_->Update();
    }
}

void InverseKinematics::HandlePostRenderUpdate(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (!drawDebug_ || !characterNode_)
        return;

    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!debug)
        return;

    auto* animModel = characterNode_->GetComponent<AnimatedModel>();
    if (!animModel)
        return;

    const Skeleton& skel = animModel->GetSkeleton();
    const Vector<Bone>& bones = skel.GetBones();

    for (unsigned i = 0; i < boneBoundsData_.Size() && i < bones.Size(); ++i)
    {
        if (boneBoundsData_[i].vertexCount == 0)
            continue;

        Node* boneNode = bones[i].node_;
        if (!boneNode)
            continue;

        // Bone node world transform positions the bone-local AABB in world space
        Matrix3x4 worldTransform = boneNode->GetWorldTransform();
        debug->AddBoundingBox(boneBoundsData_[i].box, worldTransform,
            ragdollActive_ ? Color::GREEN : Color::RED, false);
    }
}

void InverseKinematics::ActivateRagdoll()
{
    if (!characterNode_ || ragdollActive_)
        return;

    auto* animModel = characterNode_->GetComponent<AnimatedModel>();
    if (!animModel)
        return;

    Skeleton& skel = animModel->GetSkeleton();
    Vector<Bone>& bones = skel.GetModifiableBones();

    // Step 1: add RigidBody + CollisionShape to each bone with vertices
    for (unsigned i = 0; i < boneBoundsData_.Size() && i < bones.Size(); ++i)
    {
        const BoneBounds& bb = boneBoundsData_[i];
        if (bb.vertexCount == 0)
            continue;

        Node* boneNode = bones[i].node_;
        if (!boneNode)
            continue;

        auto* body = boneNode->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetLinearDamping(0.05f);
        body->SetAngularDamping(0.85f);
        body->SetLinearRestThreshold(1.5f);
        body->SetAngularRestThreshold(2.5f);

        auto* shape = boneNode->CreateComponent<CollisionShape>();
        shape->SetBox(bb.box.Size(), bb.box.Center());
    }

    // Step 2: add constraints between parent-child bone pairs
    for (unsigned i = 0; i < boneBoundsData_.Size() && i < bones.Size(); ++i)
    {
        if (boneBoundsData_[i].vertexCount == 0)
            continue;

        Node* childNode = bones[i].node_;
        if (!childNode || !childNode->GetComponent<RigidBody>())
            continue;

        int parentIdx = bones[i].parentIndex_;
        if (parentIdx < 0 || parentIdx >= (int)bones.Size() || parentIdx == (int)i)
            continue;

        Node* parentNode = bones[parentIdx].node_;
        if (!parentNode || !parentNode->GetComponent<RigidBody>())
            continue;

        auto* constraint = childNode->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_CONETWIST);
        constraint->SetDisableCollision(true);
        constraint->SetOtherBody(parentNode->GetComponent<RigidBody>());
        constraint->SetWorldPosition(childNode->GetWorldPosition());
        constraint->SetAxis(Vector3::DOWN);
        constraint->SetOtherAxis(Vector3::DOWN);
        constraint->SetHighLimit(Vector2(45.0f, 45.0f));
        constraint->SetLowLimit(Vector2::ZERO);
    }

    // Step 3: disable animation on all bones — physics owns them now
    for (unsigned i = 0; i < bones.Size(); ++i)
        bones[i].animated_ = false;

    ragdollActive_ = true;
    URHO3D_LOGINFO("Ragdoll activated — press Space was pressed");
}
