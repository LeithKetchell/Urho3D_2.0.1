// Port of bulletphysics/bullet3/examples/Heightfield/HeightfieldExample.cpp
// Original creates btHeightfieldTerrainShape with procedural height data and
// drops objects onto it. This port uses Urho3D's Terrain component with
// CollisionShape::SetTerrain() for the same effect.
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
#include <Urho3D/Graphics/Terrain.h>
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

#include "BS_Heightfield.h"

#include <Urho3D/DebugNew.h>

URHO3D_DEFINE_APPLICATION_MAIN(BS_Heightfield)

void BS_Heightfield::Start()
{
    Sample::Start();
    CreateScene();
    SetupViewport();
    CreateInstructions(
        "Bullet HeightfieldExample — terrain collision\n"
        "30 boxes and spheres dropped onto heightfield terrain\n"
        "WASD+mouse | Space=debug draw"
    );
    SubscribeToEvents();
    Sample::InitMouseMode(MM_RELATIVE);
}

void BS_Heightfield::CreateScene()
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
    zone->SetFogColor(Color(0.5f, 0.5f, 0.7f));
    zone->SetFogStart(200.0f);
    zone->SetFogEnd(600.0f);

    // Light
    Node* lightNode = scene_->CreateChild("Light");
    lightNode->SetDirection(Vector3(0.3f, -0.5f, 0.425f));
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetCastShadows(true);
    light->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    light->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));

    // === Heightfield terrain (faithful to Bullet's HeightfieldExample) ===
    // Original uses btHeightfieldTerrainShape with procedural radial height data.
    // Urho3D equivalent: Terrain component + CollisionShape::SetTerrain()
    Node* terrainNode = scene_->CreateChild("Terrain");
    terrainNode->SetPosition(Vector3::ZERO);
    auto* terrain = terrainNode->CreateComponent<Terrain>();
    terrain->SetPatchSize(64);
    terrain->SetSpacing(Vector3(2.0f, 0.5f, 2.0f));
    terrain->SetSmoothing(true);
    terrain->SetHeightMap(cache->GetResource<Image>("Textures/HeightMap.png"));
    terrain->SetMaterial(cache->GetResource<Material>("Materials/Terrain.xml"));
    terrain->SetOccluder(true);

    auto* terrainBody = terrainNode->CreateComponent<RigidBody>();
    terrainBody->SetCollisionLayer(2);
    auto* terrainShape = terrainNode->CreateComponent<CollisionShape>();
    terrainShape->SetTerrain();

    // === Drop objects onto the terrain (like the original drops cubes/spheres) ===
    // Original drops objects at grid positions above the terrain
    String models[] = {"Models/Box.mdl", "Models/Sphere.mdl"};
    String materials[] = {"Materials/StoneEnvMapSmall.xml", "Materials/StoneSmall.xml"};

    for (int i = 0; i < 30; i++)
    {
        float x = -40.0f + (float)(i % 6) * 16.0f;
        float z = -40.0f + (float)(i / 6) * 16.0f;
        float y = terrain->GetHeight(Vector3(x, 0.0f, z)) + 15.0f + (float)(i % 3) * 5.0f;

        Node* objNode = scene_->CreateChild("FallingObject");
        objNode->SetPosition(Vector3(x, y, z));

        auto* model = objNode->CreateComponent<StaticModel>();
        int type = i % 2;
        model->SetModel(cache->GetResource<Model>(models[type]));
        model->SetMaterial(cache->GetResource<Material>(materials[type]));
        model->SetCastShadows(true);

        auto* body = objNode->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetFriction(0.5f);
        body->SetCollisionLayer(1);
        body->SetCollisionMask(3);

        auto* shape = objNode->CreateComponent<CollisionShape>();
        if (type == 0)
            shape->SetBox(Vector3::ONE);
        else
            shape->SetSphere(0.5f);
    }

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(600.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 30.0f, -60.0f));
    cameraNode_->LookAt(Vector3(0.0f, 10.0f, 0.0f));
}

void BS_Heightfield::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void BS_Heightfield::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(BS_Heightfield, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(BS_Heightfield, HandlePostRenderUpdate));
}

void BS_Heightfield::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);
}

void BS_Heightfield::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug_)
        scene_->GetComponent<PhysicsWorld>()->DrawDebugGeometry(true);
}
