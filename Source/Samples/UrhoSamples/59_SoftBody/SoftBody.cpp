// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

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
#include <Urho3D/Physics/SoftBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "SoftBody.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletSoftBody/btSoftBody.h>

URHO3D_DEFINE_APPLICATION_MAIN(SoftBodyDemo)

SoftBodyDemo::SoftBodyDemo(Context* context) :
    Sample(context)
{
}

void SoftBodyDemo::Start()
{
    // Execute base class startup
    Sample::Start();

    // Load UI style for ProfilerUI
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateInstructions();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Hook up to the frame update and render post-update events
    SubscribeToEvents();

    // Set the mouse mode — RMB controls camera, LMB picks objects
    Sample::InitMouseMode(MM_RELATIVE);

    // Initialize profiler UI
    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);
}

void SoftBodyDemo::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);

    // Create octree, physics world, debug renderer
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<PhysicsWorld>();
    scene_->CreateComponent<DebugRenderer>();

    // Create a Zone for ambient lighting
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.3f, 0.3f, 0.3f));
    zone->SetFogColor(Color(0.7f, 0.8f, 0.9f));
    zone->SetFogStart(200.0f);
    zone->SetFogEnd(400.0f);

    // Create a directional light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetBrightness(1.2f);

    // === Ground plane ===
    Node* floorNode = scene_->CreateChild("Floor");
    floorNode->SetPosition(Vector3(0.0f, -0.5f, 0.0f));
    floorNode->SetScale(Vector3(200.0f, 1.0f, 200.0f));
    auto* floorModel = floorNode->CreateComponent<StaticModel>();
    floorModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    floorModel->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));
    auto* floorBody = floorNode->CreateComponent<RigidBody>();
    floorBody->SetBoxShape(Vector3::ONE);

    // === Rigid sphere obstacle (centered under cloth for draping) ===
    Node* sphereNode = scene_->CreateChild("Sphere");
    sphereNode->SetPosition(Vector3(0.0f, 4.0f, 0.0f));
    sphereNode->SetScale(Vector3(2.0f, 2.0f, 2.0f));
    auto* sphereModel = sphereNode->CreateComponent<StaticModel>();
    sphereModel->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
    sphereModel->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
    auto* sphereBody = sphereNode->CreateComponent<RigidBody>();
    sphereBody->SetSphereShape(1.0f);

    // === Throwable rigid body boxes ===
    // These can be picked up with LMB and thrown at the cloth
    auto* boxMat = cache->GetResource<Material>("Materials/StoneSmall.xml");
    for (int i = 0; i < 6; ++i)
    {
        Node* boxNode = scene_->CreateChild("ThrowBox");
        boxNode->SetPosition(Vector3(-8.0f + i * 1.5f, 0.5f, -8.0f));
        boxNode->SetScale(0.8f);
        auto* model = boxNode->CreateComponent<StaticModel>();
        model->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        model->SetMaterial(boxMat);
        auto* body = boxNode->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetFriction(0.75f);
        body->SetBoxShape(Vector3::ONE);
    }

    // === Cloth (patch) ===
    // Create a 16x16 cloth patch above the sphere, fixed at two corners
    Node* clothNode = scene_->CreateChild("Cloth");
    auto* cloth = clothNode->CreateComponent<Urho3D::SoftBody>();
    cloth->CreatePatch(
        Vector3(-3.0f, 8.0f, -3.0f),   // corner00
        Vector3(3.0f, 8.0f, -3.0f),    // corner10
        Vector3(-3.0f, 8.0f, 3.0f),    // corner01
        Vector3(3.0f, 8.0f, 3.0f),     // corner11
        16, 16,
        1 + 2,  // fix corners 0 and 1 (top edge)
        true);  // generate diagonals
    cloth->SetTotalMass(1.0f);
    cloth->SetLinearStiffness(0.8f);
    cloth->SetDamping(0.05f);
    cloth->SetPositionIterations(4);
    cloth->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));

    // === Rope ===
    // Create a rope between two posts, fixed at both ends
    // Left post
    Node* postLeft = scene_->CreateChild("PostLeft");
    postLeft->SetPosition(Vector3(-5.0f, 2.0f, -5.0f));
    postLeft->SetScale(Vector3(0.3f, 4.0f, 0.3f));
    auto* postLeftModel = postLeft->CreateComponent<StaticModel>();
    postLeftModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    postLeftModel->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
    auto* postLeftBody = postLeft->CreateComponent<RigidBody>();
    postLeftBody->SetBoxShape(Vector3::ONE);

    // Right post
    Node* postRight = scene_->CreateChild("PostRight");
    postRight->SetPosition(Vector3(5.0f, 2.0f, -5.0f));
    postRight->SetScale(Vector3(0.3f, 4.0f, 0.3f));
    auto* postRightModel = postRight->CreateComponent<StaticModel>();
    postRightModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    postRightModel->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));
    auto* postRightBody = postRight->CreateComponent<RigidBody>();
    postRightBody->SetBoxShape(Vector3::ONE);

    // Rope between posts
    Node* ropeNode = scene_->CreateChild("Rope");
    auto* rope = ropeNode->CreateComponent<Urho3D::SoftBody>();
    rope->CreateRope(
        Vector3(-5.0f, 4.0f, -5.0f),  // from (top of left post)
        Vector3(5.0f, 4.0f, -5.0f),   // to (top of right post)
        16,     // resolution
        1 + 2); // fix both ends
    rope->SetTotalMass(0.5f);
    rope->SetLinearStiffness(0.9f);
    rope->SetDamping(0.02f);
    rope->SetMaterial(cache->GetResource<Material>("Materials/GreenTransparent.xml"));

    // === Bouncing balls (ellipsoids with pressure — soft-soft collision demo) ===
    // Multiple soft balls dropped from staggered heights onto the same spot
    // They collide with each other (VF_SS) and bounce off the ground (SDF_RS)
    struct BallDef { Vector3 pos; Vector3 radius; float mass; Color tint; };
    BallDef balls[] = {
        { Vector3(-6.0f, 6.0f, 5.0f),  Vector3(0.8f, 0.8f, 0.8f), 2.0f, Color(1.0f, 0.3f, 0.3f) },
        { Vector3(-5.5f, 10.0f, 5.2f), Vector3(0.7f, 0.7f, 0.7f), 1.5f, Color(0.3f, 1.0f, 0.3f) },
        { Vector3(-6.5f, 14.0f, 4.8f), Vector3(0.9f, 0.9f, 0.9f), 2.5f, Color(0.3f, 0.3f, 1.0f) },
        { Vector3(-5.0f, 18.0f, 5.5f), Vector3(0.6f, 0.6f, 0.6f), 1.0f, Color(1.0f, 1.0f, 0.3f) },
    };

    for (int i = 0; i < 4; ++i)
    {
        Node* ballNode = scene_->CreateChild("Ball" + String(i));
        auto* ball = ballNode->CreateComponent<Urho3D::SoftBody>();
        ball->CreateEllipsoid(balls[i].pos, balls[i].radius, 16);
        ball->SetTotalMass(balls[i].mass);
        ball->SetPressure(100.0f);
        ball->SetLinearStiffness(0.5f);
        ball->SetDamping(0.01f);
        ball->SetSoftSoftCollision(true);

        // Clone material with unique tint per ball
        auto* baseMat = cache->GetResource<Material>("Materials/Stone.xml");
        auto clonedMat = baseMat->Clone();
        clonedMat->SetShaderParameter("MatDiffColor", Variant(balls[i].tint));
        ball->SetMaterial(clonedMat);
    }

    // === Camera ===
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();
    cameraNode_->SetPosition(Vector3(0.0f, 8.0f, -18.0f));
    cameraNode_->SetRotation(Quaternion(20.0f, 0.0f, 0.0f));
}

void SoftBodyDemo::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    // Construct new Text object, set string to display and font to use
    auto* instructionText = ui->GetRoot()->CreateChild<Text>();
    instructionText->SetText(
        "WASD to move, RMB to toggle mouse capture\n"
        "LMB hold to grab, release to throw\n"
        "Space to toggle debug geometry"
    );
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    instructionText->SetTextAlignment(HA_CENTER);

    // Position the text relative to the screen center
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetVerticalAlignment(VA_CENTER);
    instructionText->SetPosition(0, ui->GetRoot()->GetHeight() / 4);
}

void SoftBodyDemo::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();

    // Set up a viewport to the Renderer subsystem so that the 3D scene can be seen
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void SoftBodyDemo::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(SoftBodyDemo, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(SoftBodyDemo, HandlePostRenderUpdate));
}

void SoftBodyDemo::MoveCamera(float timeStep)
{
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    auto* input = GetSubsystem<Input>();

    const float MOVE_SPEED = 20.0f;
    const float MOUSE_SENSITIVITY = 0.1f;

    // RMB toggles mouse capture: click once to free cursor, click again to recapture
    if (input->GetMouseButtonPress(MOUSEB_RIGHT))
    {
        if (input->GetMouseMode() == MM_RELATIVE)
        {
            input->SetMouseVisible(true);
            input->SetMouseMode(MM_FREE);
        }
        else
        {
            input->SetMouseVisible(false);
            input->SetMouseMode(MM_RELATIVE);
        }
    }

    // Camera rotates with mouse only when captured
    if (input->GetMouseMode() == MM_RELATIVE)
    {
        IntVector2 mouseMove = input->GetMouseMove();
        yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
        pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
        pitch_ = Clamp(pitch_, -90.0f, 90.0f);
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
    }

    // WASD movement always works
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

void SoftBodyDemo::HandlePicking(float timeStep)
{
    auto* input = GetSubsystem<Input>();
    auto* physics = scene_->GetComponent<PhysicsWorld>();
    auto* camera = cameraNode_->GetComponent<Camera>();

    // Get ray from cursor position
    auto* graphics = GetSubsystem<Graphics>();
    IntVector2 pos = input->GetMousePosition();
    Ray cameraRay = camera->GetScreenRay(
        (float)pos.x_ / graphics->GetWidth(),
        (float)pos.y_ / graphics->GetHeight());

    if (input->GetMouseButtonPress(MOUSEB_LEFT))
    {
        // Try rigid body raycast first
        PhysicsRaycastResult result;
        physics->RaycastSingle(result, cameraRay, 100.0f);

        if (result.body_ && result.body_->GetMass() > 0.0f)
        {
            // Grab dynamic rigid body
            pickedBody_ = result.body_;
            pickDistance_ = result.distance_;

            btRigidBody* btBody = result.body_->GetBody();
            btVector3 localPivot = btBody->getCenterOfMassTransform().inverse() *
                btVector3(result.position_.x_, result.position_.y_, result.position_.z_);

            pickConstraint_ = new btPoint2PointConstraint(*btBody, localPivot);
            pickConstraint_->m_setting.m_impulseClamp = 30.0f;
            pickConstraint_->m_setting.m_tau = 0.1f;

            physics->GetWorld()->addConstraint(pickConstraint_, true);
            btBody->setActivationState(DISABLE_DEACTIVATION);
        }
        else
        {
            // No rigid body hit — try soft bodies
            btVector3 rayFrom(cameraRay.origin_.x_, cameraRay.origin_.y_, cameraRay.origin_.z_);
            btVector3 rayDir(cameraRay.direction_.x_, cameraRay.direction_.y_, cameraRay.direction_.z_);
            btVector3 rayTo = rayFrom + rayDir * 100.0f;

            float bestFraction = 1.0f;
            Urho3D::SoftBody* bestSB = nullptr;
            int bestNode = -1;

            const auto& softBodies = physics->GetSoftBodies();
            for (auto* sb : softBodies)
            {
                btSoftBody* bsb = sb->GetSoftBody();
                if (!bsb)
                    continue;

                btSoftBody::sRayCast sResult;
                if (bsb->rayTest(rayFrom, rayTo, sResult))
                {
                    if (sResult.fraction < bestFraction)
                    {
                        bestFraction = sResult.fraction;
                        bestSB = sb;

                        // Find closest node to hit point
                        btVector3 hitPos = rayFrom + (rayTo - rayFrom) * sResult.fraction;
                        float bestDist2 = 1e18f;
                        bestNode = 0;
                        for (int i = 0; i < bsb->m_nodes.size(); ++i)
                        {
                            float d2 = (bsb->m_nodes[i].m_x - hitPos).length2();
                            if (d2 < bestDist2)
                            {
                                bestDist2 = d2;
                                bestNode = i;
                            }
                        }
                    }
                }
            }

            if (bestSB && bestNode >= 0)
            {
                pickedSoftBody_ = bestSB;
                pickedSoftNode_ = bestNode;
                pickDistance_ = bestFraction * 100.0f;

                // Pin the node by saving and zeroing its inverse mass
                btSoftBody* bsb = bestSB->GetSoftBody();
                savedInvMass_ = bsb->m_nodes[bestNode].m_im;
                bsb->m_nodes[bestNode].m_im = 0.0f; // make kinematic
            }
        }
    }

    // Dragging rigid body
    if (input->GetMouseButtonDown(MOUSEB_LEFT) && pickConstraint_)
    {
        Vector3 target = cameraRay.origin_ + cameraRay.direction_ * pickDistance_;
        pickConstraint_->setPivotB(btVector3(target.x_, target.y_, target.z_));
    }

    // Dragging soft body node
    if (input->GetMouseButtonDown(MOUSEB_LEFT) && pickedSoftBody_ && pickedSoftNode_ >= 0)
    {
        Vector3 target = cameraRay.origin_ + cameraRay.direction_ * pickDistance_;
        btSoftBody* bsb = pickedSoftBody_->GetSoftBody();
        if (bsb)
        {
            bsb->m_nodes[pickedSoftNode_].m_x = btVector3(target.x_, target.y_, target.z_);
        }
    }

    // Release
    if (!input->GetMouseButtonDown(MOUSEB_LEFT))
    {
        // Release rigid body constraint
        if (pickConstraint_)
        {
            if (pickedBody_)
            {
                btRigidBody* btBody = pickedBody_->GetBody();
                if (btBody)
                    btBody->forceActivationState(ACTIVE_TAG);
            }
            physics->GetWorld()->removeConstraint(pickConstraint_);
            delete pickConstraint_;
            pickConstraint_ = nullptr;
            pickedBody_.Reset();
        }

        // Release soft body node — restore inverse mass
        if (pickedSoftBody_ && pickedSoftNode_ >= 0)
        {
            btSoftBody* bsb = pickedSoftBody_->GetSoftBody();
            if (bsb)
                bsb->m_nodes[pickedSoftNode_].m_im = savedInvMass_;

            pickedSoftBody_ = nullptr;
            pickedSoftNode_ = -1;
        }
    }
}

void SoftBodyDemo::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
    HandlePicking(timeStep);

    // Update profiler display
    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
    }
}

void SoftBodyDemo::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
