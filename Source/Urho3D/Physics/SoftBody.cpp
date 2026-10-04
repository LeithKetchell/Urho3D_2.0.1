// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Graphics/Camera.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Material.h"
#include "../Graphics/Model.h"
#include "../Graphics/Octree.h"
#include "../GraphicsAPI/IndexBuffer.h"
#include "../GraphicsAPI/VertexBuffer.h"
#include "../IO/Log.h"
#include "../Physics/PhysicsEvents.h"
#include "../Physics/PhysicsUtils.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/RigidBody.h"
#include "../Physics/SoftBody.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Scene.h"

#include <Bullet/BulletSoftBody/btSoftBody.h>
#include <Bullet/BulletSoftBody/btSoftBodyHelpers.h>
#include <Bullet/BulletSoftBody/btSoftMultiBodyDynamicsWorld.h>

namespace Urho3D
{

extern const char* PHYSICS_CATEGORY;

static const char* softBodyTypeNames[] =
{
    "Rope",
    "Patch",
    "Ellipsoid",
    "TriMesh",
    "ConvexHull",
    nullptr
};

SoftBody::SoftBody(Context* context) :
    Drawable(context, DrawableTypes::Geometry)
{
}

SoftBody::~SoftBody()
{
    ReleaseSoftBody();

    if (physicsWorld_)
    {
        physicsWorld_->RemoveSoftBody(this);
        physicsWorld_.Reset();
    }
}

void SoftBody::RegisterObject(Context* context)
{
    context->RegisterFactory<SoftBody>(PHYSICS_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, true, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE("Soft Body Type", softBodyType_, softBodyTypeNames, SOFTBODY_PATCH, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Linear Stiffness", linearStiffness_, 0.5f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Angular Stiffness", angularStiffness_, 0.5f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Volume Stiffness", volumeStiffness_, 0.5f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Damping", damping_, 0.01f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Pressure", pressure_, 0.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Friction", friction_, 0.5f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Total Mass", totalMass_, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Position Iterations", positionIterations_, 4, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Collision Margin", collisionMargin_, 0.05f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Collision Layer", collisionLayer_, 1u, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Collision Mask", collisionMask_, 0xFFFFFFFFu, AM_DEFAULT);

    // Rope parameters
    URHO3D_ATTRIBUTE("Rope From", ropeFrom_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Rope To", ropeTo_, Vector3::FORWARD, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Rope Resolution", ropeResolution_, 8, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Fixed Ends Mask", fixedEndsMask_, 0, AM_DEFAULT);

    // Patch parameters
    URHO3D_ATTRIBUTE("Patch Corner00", patchCorner00_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Patch Corner10", patchCorner10_, Vector3::RIGHT, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Patch Corner01", patchCorner01_, Vector3::FORWARD, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Patch Corner11", patchCorner11_, Vector3(1.0f, 0.0f, 1.0f), AM_DEFAULT);
    URHO3D_ATTRIBUTE("Patch ResX", patchResX_, 8, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Patch ResY", patchResY_, 8, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Fixed Corners Mask", fixedCornersMask_, 0, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Patch Diagonals", patchDiagonals_, true, AM_DEFAULT);

    // Ellipsoid parameters
    URHO3D_ATTRIBUTE("Ellipsoid Center", ellipsoidCenter_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Ellipsoid Radius", ellipsoidRadius_, Vector3::ONE, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Ellipsoid Resolution", ellipsoidResolution_, 16, AM_DEFAULT);

    // Model reference (for TriMesh/ConvexHull)
    URHO3D_ACCESSOR_ATTRIBUTE("Model", GetModelAttr, SetModelAttr, ResourceRef(Model::GetTypeStatic()), AM_DEFAULT);
    URHO3D_ATTRIBUTE("Model LOD Level", modelLodLevel_, 0, AM_DEFAULT);
}

Geometry* SoftBody::GetLodGeometry(i32 batchIndex, i32 level)
{
    return geometry_;
}

void SoftBody::UpdateBatches(const FrameInfo& frame)
{
    // btSoftBody vertices are in world space, so use identity transform instead of node transform
    const BoundingBox& worldBoundingBox = GetWorldBoundingBox();
    distance_ = frame.camera_->GetDistance(worldBoundingBox.Center());

    for (unsigned i = 0; i < batches_.Size(); ++i)
    {
        batches_[i].distance_ = distance_;
        batches_[i].worldTransform_ = &Matrix3x4::IDENTITY;
    }

    float scale = worldBoundingBox.Size().DotProduct(DOT_SCALE);
    float newLodDistance = frame.camera_->GetLodDistance(distance_, scale, lodBias_);

    if (newLodDistance != lodDistance_)
        lodDistance_ = newLodDistance;
}

void SoftBody::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    if (!softBody_ || !debug)
        return;

    // Draw links (edges) as debug lines
    for (int i = 0; i < softBody_->m_links.size(); ++i)
    {
        const btSoftBody::Link& link = softBody_->m_links[i];
        Vector3 from = ToVector3(link.m_n[0]->m_x);
        Vector3 to = ToVector3(link.m_n[1]->m_x);
        debug->AddLine(from, to, Color::GREEN, depthTest);
    }
}

// === Creation Methods ===

void SoftBody::CreateRope(const Vector3& from, const Vector3& to, int resolution, int fixedEndsMask)
{
    softBodyType_ = SOFTBODY_ROPE;
    ropeFrom_ = from;
    ropeTo_ = to;
    ropeResolution_ = resolution;
    fixedEndsMask_ = fixedEndsMask;

    if (physicsWorld_)
        AddBodyToWorld();
}

void SoftBody::CreatePatch(const Vector3& corner00, const Vector3& corner10,
                           const Vector3& corner01, const Vector3& corner11,
                           int resX, int resY, int fixedCornersMask, bool generateDiagonals)
{
    softBodyType_ = SOFTBODY_PATCH;
    patchCorner00_ = corner00;
    patchCorner10_ = corner10;
    patchCorner01_ = corner01;
    patchCorner11_ = corner11;
    patchResX_ = resX;
    patchResY_ = resY;
    fixedCornersMask_ = fixedCornersMask;
    patchDiagonals_ = generateDiagonals;

    if (physicsWorld_)
        AddBodyToWorld();
}

void SoftBody::CreateEllipsoid(const Vector3& center, const Vector3& radius, int resolution)
{
    softBodyType_ = SOFTBODY_ELLIPSOID;
    ellipsoidCenter_ = center;
    ellipsoidRadius_ = radius;
    ellipsoidResolution_ = resolution;

    if (physicsWorld_)
        AddBodyToWorld();
}

void SoftBody::CreateFromModel(Model* model, int lodLevel)
{
    if (!model)
    {
        URHO3D_LOGERROR("Null model for soft body creation");
        return;
    }

    softBodyType_ = SOFTBODY_TRIMESH;
    model_ = model;
    modelLodLevel_ = lodLevel;

    if (physicsWorld_)
        AddBodyToWorld();
}

void SoftBody::CreateConvexHull(Model* model, int lodLevel)
{
    if (!model)
    {
        URHO3D_LOGERROR("Null model for soft body creation");
        return;
    }

    softBodyType_ = SOFTBODY_CONVEXHULL;
    model_ = model;
    modelLodLevel_ = lodLevel;

    if (physicsWorld_)
        AddBodyToWorld();
}

// === Configuration Properties ===

void SoftBody::SetLinearStiffness(float stiffness)
{
    linearStiffness_ = Clamp(stiffness, 0.0f, 1.0f);
    if (softBody_ && softBody_->m_materials.size() > 0)
        softBody_->m_materials[0]->m_kLST = linearStiffness_;
}

void SoftBody::SetAngularStiffness(float stiffness)
{
    angularStiffness_ = Clamp(stiffness, 0.0f, 1.0f);
    if (softBody_ && softBody_->m_materials.size() > 0)
        softBody_->m_materials[0]->m_kAST = angularStiffness_;
}

void SoftBody::SetVolumeStiffness(float stiffness)
{
    volumeStiffness_ = Clamp(stiffness, 0.0f, 1.0f);
    if (softBody_ && softBody_->m_materials.size() > 0)
        softBody_->m_materials[0]->m_kVST = volumeStiffness_;
}

void SoftBody::SetDamping(float damping)
{
    damping_ = Max(damping, 0.0f);
    if (softBody_)
        softBody_->m_cfg.kDP = damping_;
}

void SoftBody::SetPressure(float pressure)
{
    pressure_ = pressure;
    if (softBody_)
        softBody_->m_cfg.kPR = pressure_;
}

void SoftBody::SetFriction(float friction)
{
    friction_ = Max(friction, 0.0f);
    if (softBody_)
        softBody_->m_cfg.kDF = friction_;
}

void SoftBody::SetPositionIterations(int iterations)
{
    positionIterations_ = Max(iterations, 1);
    if (softBody_)
        softBody_->m_cfg.piterations = positionIterations_;
}

void SoftBody::SetTotalMass(float mass, bool fromFaces)
{
    totalMass_ = Max(mass, 0.0f);
    if (softBody_)
        softBody_->setTotalMass(totalMass_, fromFaces);
}

void SoftBody::SetCollisionMargin(float margin)
{
    collisionMargin_ = Max(margin, 0.0f);
    if (softBody_)
        softBody_->getCollisionShape()->setMargin(collisionMargin_);
}

void SoftBody::SetCollisionLayer(unsigned layer)
{
    if (layer != collisionLayer_)
    {
        collisionLayer_ = layer;
        if (inWorld_)
            AddBodyToWorld();
    }
}

void SoftBody::SetCollisionMask(unsigned mask)
{
    if (mask != collisionMask_)
    {
        collisionMask_ = mask;
        if (inWorld_)
            AddBodyToWorld();
    }
}

void SoftBody::SetSoftSoftCollision(bool enable)
{
    if (enable != softSoftCollision_)
    {
        softSoftCollision_ = enable;
        if (softBody_)
        {
            if (enable)
                softBody_->m_cfg.collisions |= btSoftBody::fCollision::VF_SS;
            else
                softBody_->m_cfg.collisions &= ~btSoftBody::fCollision::VF_SS;
        }
    }
}

// === Anchor/Attachment ===

void SoftBody::AppendAnchor(int nodeIndex, RigidBody* body, bool disableCollision, float influence)
{
    if (!softBody_ || !body || !body->GetBody())
    {
        URHO3D_LOGERROR("Cannot append anchor: soft body or rigid body not ready");
        return;
    }

    if (nodeIndex < 0 || nodeIndex >= softBody_->m_nodes.size())
    {
        URHO3D_LOGERROR("Soft body node index out of bounds");
        return;
    }

    softBody_->appendAnchor(nodeIndex, body->GetBody(), disableCollision, influence);
}

// === Accessors ===

int SoftBody::GetNumNodes() const
{
    return softBody_ ? softBody_->m_nodes.size() : 0;
}

Vector3 SoftBody::GetNodePosition(int index) const
{
    if (!softBody_ || index < 0 || index >= softBody_->m_nodes.size())
        return Vector3::ZERO;
    return ToVector3(softBody_->m_nodes[index].m_x);
}

Vector3 SoftBody::GetNodeVelocity(int index) const
{
    if (!softBody_ || index < 0 || index >= softBody_->m_nodes.size())
        return Vector3::ZERO;
    return ToVector3(softBody_->m_nodes[index].m_v);
}

// === Force/Impulse ===

void SoftBody::AddForce(const Vector3& force)
{
    if (softBody_)
        softBody_->addForce(ToBtVector3(force));
}

void SoftBody::AddNodeForce(const Vector3& force, int nodeIndex)
{
    if (softBody_ && nodeIndex >= 0 && nodeIndex < softBody_->m_nodes.size())
        softBody_->addForce(ToBtVector3(force), nodeIndex);
}

void SoftBody::SetVelocity(const Vector3& velocity)
{
    if (softBody_)
        softBody_->setVelocity(ToBtVector3(velocity));
}

void SoftBody::SetWindVelocity(const Vector3& wind)
{
    if (softBody_)
        softBody_->setWindVelocity(ToBtVector3(wind));
}

Vector3 SoftBody::GetWindVelocity() const
{
    if (softBody_)
        return ToVector3(softBody_->getWindVelocity());
    return Vector3::ZERO;
}

// === Lifecycle ===

void SoftBody::ReleaseSoftBody()
{
    RemoveBodyFromWorld();
}

void SoftBody::OnSetEnabled()
{
    bool enabled = IsEnabledEffective();

    if (enabled && !inWorld_)
        AddBodyToWorld();
    else if (!enabled && inWorld_)
        RemoveBodyFromWorld();
}

void SoftBody::SetMaterial(Material* material)
{
    if (!material)
        return;

    // Clone material and force two-sided rendering for soft bodies
    SharedPtr<Material> cloned = material->Clone();
    cloned->SetCullMode(CULL_NONE);
    cloned->SetShadowCullMode(CULL_NONE);

    if (batches_.Size() > 0)
        batches_[0].material_ = cloned;
    MarkNetworkUpdate();
}

Material* SoftBody::GetMaterial() const
{
    return batches_.Size() > 0 ? batches_[0].material_ : nullptr;
}

void SoftBody::SetModelAttr(const ResourceRef& value)
{
    auto* cache = GetSubsystem<ResourceCache>();
    model_ = cache->GetResource<Model>(value.name_);
}

ResourceRef SoftBody::GetModelAttr() const
{
    return GetResourceRef(model_, Model::GetTypeStatic());
}

void SoftBody::OnSceneSet(Scene* scene)
{
    // Call base class to handle octree registration
    Drawable::OnSceneSet(scene);

    if (scene)
    {
        if (scene == node_)
            URHO3D_LOGWARNING(GetTypeName() + " should not be created to the root scene node");

        physicsWorld_ = scene->GetOrCreateComponent<PhysicsWorld>();
        physicsWorld_->AddSoftBody(this);

        AddBodyToWorld();
    }
    else
    {
        // Unsubscribe from physics events before releasing
        if (physicsWorld_)
            UnsubscribeFromEvent(physicsWorld_, E_PHYSICSPOSTSTEP);

        ReleaseSoftBody();

        if (physicsWorld_)
        {
            physicsWorld_->RemoveSoftBody(this);
            physicsWorld_.Reset();
        }
    }
}

void SoftBody::OnWorldBoundingBoxUpdate()
{
    if (softBody_)
    {
        btVector3 aabbMin, aabbMax;
        softBody_->getAabb(aabbMin, aabbMax);
        worldBoundingBox_ = BoundingBox(ToVector3(aabbMin), ToVector3(aabbMax));
    }
    else
    {
        worldBoundingBox_.Define(-0.5f, 0.5f);
    }
}

void SoftBody::AddBodyToWorld()
{
    if (!physicsWorld_)
        return;

    // Remove existing body first
    if (softBody_)
        RemoveBodyFromWorld();

    btSoftBodyWorldInfo& worldInfo = physicsWorld_->GetSoftBodyWorldInfo();

    switch (softBodyType_)
    {
    case SOFTBODY_ROPE:
        softBody_ = btSoftBodyHelpers::CreateRope(
            worldInfo,
            ToBtVector3(ropeFrom_),
            ToBtVector3(ropeTo_),
            ropeResolution_,
            fixedEndsMask_);
        break;

    case SOFTBODY_PATCH:
        softBody_ = btSoftBodyHelpers::CreatePatch(
            worldInfo,
            ToBtVector3(patchCorner00_),
            ToBtVector3(patchCorner10_),
            ToBtVector3(patchCorner01_),
            ToBtVector3(patchCorner11_),
            patchResX_, patchResY_,
            fixedCornersMask_,
            patchDiagonals_);
        break;

    case SOFTBODY_ELLIPSOID:
        softBody_ = btSoftBodyHelpers::CreateEllipsoid(
            worldInfo,
            ToBtVector3(ellipsoidCenter_),
            ToBtVector3(ellipsoidRadius_),
            ellipsoidResolution_);
        break;

    case SOFTBODY_TRIMESH:
    {
        if (!model_)
        {
            URHO3D_LOGERROR("No model set for TriMesh soft body");
            return;
        }

        // Extract vertex and index data from model
        Urho3D::Geometry* geom = model_->GetGeometry(0, modelLodLevel_);
        if (!geom)
        {
            URHO3D_LOGERROR("Model has no geometry for soft body");
            return;
        }

        SharedArrayPtr<byte> vertexData;
        SharedArrayPtr<byte> indexData;
        i32 vertexSize;
        i32 indexSize;
        const Vector<VertexElement>* elements;

        geom->GetRawDataShared(vertexData, vertexSize, indexData, indexSize, elements);
        if (!vertexData || !indexData || !elements)
        {
            URHO3D_LOGERROR("Model has no CPU-side geometry data for soft body");
            return;
        }

        unsigned vertexCount = geom->GetVertexCount();
        unsigned indexStart = geom->GetIndexStart();
        unsigned indexCount = geom->GetIndexCount();
        unsigned triangleCount = indexCount / 3;

        // Build vertex array (positions only)
        Vector<btScalar> vertices(vertexCount * 3);
        unsigned posOffset = VertexBuffer::GetElementOffset(*elements, TYPE_VECTOR3, SEM_POSITION);
        for (unsigned i = 0; i < vertexCount; ++i)
        {
            const auto* pos = reinterpret_cast<const float*>(&vertexData[i * vertexSize + posOffset]);
            vertices[i * 3] = pos[0];
            vertices[i * 3 + 1] = pos[1];
            vertices[i * 3 + 2] = pos[2];
        }

        // Build triangle index array
        Vector<int> triangles(triangleCount * 3);
        for (unsigned i = 0; i < indexCount; ++i)
        {
            if (indexSize == 2)
                triangles[i] = reinterpret_cast<const unsigned short*>(indexData.Get())[indexStart + i];
            else
                triangles[i] = reinterpret_cast<const unsigned*>(indexData.Get())[indexStart + i];
        }

        softBody_ = btSoftBodyHelpers::CreateFromTriMesh(
            worldInfo,
            vertices.Buffer(),
            triangles.Buffer(),
            triangleCount,
            true);
        break;
    }

    case SOFTBODY_CONVEXHULL:
    {
        if (!model_)
        {
            URHO3D_LOGERROR("No model set for ConvexHull soft body");
            return;
        }

        Urho3D::Geometry* geom = model_->GetGeometry(0, modelLodLevel_);
        if (!geom)
        {
            URHO3D_LOGERROR("Model has no geometry for soft body");
            return;
        }

        SharedArrayPtr<byte> vertexData;
        SharedArrayPtr<byte> indexData;
        i32 vertexSize;
        i32 indexSize;
        const Vector<VertexElement>* elements;

        geom->GetRawDataShared(vertexData, vertexSize, indexData, indexSize, elements);
        if (!vertexData || !elements)
        {
            URHO3D_LOGERROR("Model has no CPU-side geometry data for soft body");
            return;
        }

        unsigned vertexCount = geom->GetVertexCount();
        unsigned posOffset = VertexBuffer::GetElementOffset(*elements, TYPE_VECTOR3, SEM_POSITION);

        // Build btVector3 vertex array
        Vector<btVector3> btVertices(vertexCount);
        for (unsigned i = 0; i < vertexCount; ++i)
        {
            const auto* pos = reinterpret_cast<const float*>(&vertexData[i * vertexSize + posOffset]);
            btVertices[i] = btVector3(pos[0], pos[1], pos[2]);
        }

        softBody_ = btSoftBodyHelpers::CreateFromConvexHull(
            worldInfo,
            btVertices.Buffer(),
            vertexCount,
            true);
        break;
    }
    }

    if (!softBody_)
    {
        URHO3D_LOGERROR("Failed to create soft body");
        return;
    }

    // Apply configuration
    ApplyConfig();

    // Add to world
    btSoftMultiBodyDynamicsWorld* softWorld = physicsWorld_->GetSoftMultiBodyWorld();
    softWorld->addSoftBody(softBody_, collisionLayer_, collisionMask_);
    inWorld_ = true;

    // Create visual mesh for rendering
    CreateVisualMesh();

    // Subscribe to physics post-step for visual mesh updates
    SubscribeToEvent(physicsWorld_, E_PHYSICSPOSTSTEP, URHO3D_HANDLER(SoftBody, HandlePhysicsPostStep));

    // Set up batches for rendering
    if (batches_.Empty())
    {
        batches_.Resize(1);
        batches_[0].geometry_ = geometry_;
        batches_[0].geometryType_ = GEOM_STATIC;
    }
}

void SoftBody::RemoveBodyFromWorld()
{
    if (softBody_ && inWorld_ && physicsWorld_)
    {
        btSoftMultiBodyDynamicsWorld* softWorld = physicsWorld_->GetSoftMultiBodyWorld();
        if (softWorld)
            softWorld->removeSoftBody(softBody_);
    }
    inWorld_ = false;

    delete softBody_;
    softBody_ = nullptr;
}

void SoftBody::CreateVisualMesh()
{
    if (!softBody_)
        return;

    int numNodes = softBody_->m_nodes.size();
    int numFaces = softBody_->m_faces.size();

    if (!vertexBuffer_)
    {
        vertexBuffer_ = new VertexBuffer(context_);
        vertexBuffer_->SetShadowed(true);
    }
    if (!indexBuffer_)
    {
        indexBuffer_ = new IndexBuffer(context_);
        indexBuffer_->SetShadowed(true);
    }
    if (!geometry_)
        geometry_ = new Geometry(context_);

    // All soft body types use POSITION + NORMAL + TEXCOORD vertex format
    Vector<VertexElement> elements;
    elements.Push(VertexElement(TYPE_VECTOR3, SEM_POSITION));
    elements.Push(VertexElement(TYPE_VECTOR3, SEM_NORMAL));
    elements.Push(VertexElement(TYPE_VECTOR2, SEM_TEXCOORD));

    if (softBodyType_ == SOFTBODY_ROPE)
    {
        // Ribbon geometry: 4 vertices per segment (quad), 2 triangles per segment
        int numSegments = numNodes - 1;
        if (numSegments < 1)
            return;

        int numVerts = numSegments * 4;
        int numIndices = numSegments * 6;
        vertexBuffer_->SetSize(numVerts, elements, true);

        indexBuffer_->SetSize(numIndices, false);
        auto* idx = (unsigned short*)indexBuffer_->Lock(0, numIndices);
        if (idx)
        {
            for (int s = 0; s < numSegments; ++s)
            {
                unsigned short base = s * 4;
                // Two triangles per quad
                *idx++ = base;
                *idx++ = base + 1;
                *idx++ = base + 2;
                *idx++ = base + 2;
                *idx++ = base + 1;
                *idx++ = base + 3;
            }
            indexBuffer_->Unlock();
        }

        geometry_->SetVertexBuffer(0, vertexBuffer_);
        geometry_->SetIndexBuffer(indexBuffer_);
        geometry_->SetDrawRange(TRIANGLE_LIST, 0, numIndices);
    }
    else
    {
        // Triangle rendering for patches, ellipsoids, meshes
        vertexBuffer_->SetSize(numNodes, elements, true);

        if (numFaces > 0)
        {
            // Build index buffer from faces
            bool largeIndices = numNodes > 65535;
            indexBuffer_->SetSize(numFaces * 3, largeIndices);

            if (largeIndices)
            {
                auto* dest = (unsigned*)indexBuffer_->Lock(0, numFaces * 3);
                if (dest)
                {
                    for (int f = 0; f < numFaces; ++f)
                    {
                        const btSoftBody::Face& face = softBody_->m_faces[f];
                        for (int v = 0; v < 3; ++v)
                            *dest++ = (unsigned)(face.m_n[v] - &softBody_->m_nodes[0]);
                    }
                    indexBuffer_->Unlock();
                }
            }
            else
            {
                auto* dest = (unsigned short*)indexBuffer_->Lock(0, numFaces * 3);
                if (dest)
                {
                    for (int f = 0; f < numFaces; ++f)
                    {
                        const btSoftBody::Face& face = softBody_->m_faces[f];
                        for (int v = 0; v < 3; ++v)
                            *dest++ = (unsigned short)(face.m_n[v] - &softBody_->m_nodes[0]);
                    }
                    indexBuffer_->Unlock();
                }
            }

            geometry_->SetVertexBuffer(0, vertexBuffer_);
            geometry_->SetIndexBuffer(indexBuffer_);
            geometry_->SetDrawRange(TRIANGLE_LIST, 0, numFaces * 3);
        }
        else
        {
            // No faces — fallback
            geometry_->SetVertexBuffer(0, vertexBuffer_);
            geometry_->SetDrawRange(POINT_LIST, 0, 0, 0, numNodes);
        }
    }

    // Update batch geometry reference
    if (batches_.Size() > 0)
        batches_[0].geometry_ = geometry_;

    // Initial vertex data fill
    UpdateVisualMesh();
}

void SoftBody::UpdateVisualMesh()
{
    if (!softBody_ || !vertexBuffer_)
        return;

    int numNodes = softBody_->m_nodes.size();

    if (softBodyType_ == SOFTBODY_ROPE)
    {
        // Ribbon geometry: 4 vertices per segment (2 on each side of the rope)
        int numSegments = numNodes - 1;
        if (numSegments < 1)
            return;

        const float ROPE_WIDTH = 0.05f;

        auto* dest = (float*)vertexBuffer_->Lock(0, numSegments * 4, true);
        if (dest)
        {
            for (int s = 0; s < numSegments; ++s)
            {
                const btVector3& p0 = softBody_->m_nodes[s].m_x;
                const btVector3& p1 = softBody_->m_nodes[s + 1].m_x;

                // Direction along rope segment — guard against zero-length (NaN)
                btVector3 diff = p1 - p0;
                float len2 = diff.length2();
                btVector3 dir;
                if (len2 < 1e-8f)
                    dir = btVector3(1, 0, 0);  // degenerate segment, use arbitrary direction
                else
                    dir = diff / btSqrt(len2);

                // Use world up to compute a perpendicular (ribbon faces up)
                btVector3 up(0, 1, 0);
                btVector3 side = dir.cross(up);
                if (side.length2() < 0.001f)
                {
                    up = btVector3(1, 0, 0);
                    side = dir.cross(up);
                }
                float sideLen2 = side.length2();
                if (sideLen2 < 1e-8f)
                    side = btVector3(0, 0, ROPE_WIDTH);
                else
                    side = side / btSqrt(sideLen2) * ROPE_WIDTH;

                btVector3 normalVec = side.cross(dir);
                float nLen2 = normalVec.length2();
                btVector3 normal = (nLen2 < 1e-8f) ? btVector3(0, 1, 0) : normalVec / btSqrt(nLen2);
                float u0 = (float)s / numSegments;
                float u1 = (float)(s + 1) / numSegments;

                // Vertex 0: p0 - side
                *dest++ = p0.x() - side.x(); *dest++ = p0.y() - side.y(); *dest++ = p0.z() - side.z();
                *dest++ = normal.x(); *dest++ = normal.y(); *dest++ = normal.z();
                *dest++ = u0; *dest++ = 0.0f;

                // Vertex 1: p0 + side
                *dest++ = p0.x() + side.x(); *dest++ = p0.y() + side.y(); *dest++ = p0.z() + side.z();
                *dest++ = normal.x(); *dest++ = normal.y(); *dest++ = normal.z();
                *dest++ = u0; *dest++ = 1.0f;

                // Vertex 2: p1 - side
                *dest++ = p1.x() - side.x(); *dest++ = p1.y() - side.y(); *dest++ = p1.z() - side.z();
                *dest++ = normal.x(); *dest++ = normal.y(); *dest++ = normal.z();
                *dest++ = u1; *dest++ = 0.0f;

                // Vertex 3: p1 + side
                *dest++ = p1.x() + side.x(); *dest++ = p1.y() + side.y(); *dest++ = p1.z() + side.z();
                *dest++ = normal.x(); *dest++ = normal.y(); *dest++ = normal.z();
                *dest++ = u1; *dest++ = 1.0f;
            }
            vertexBuffer_->Unlock();
        }
    }
    else
    {
        // Vertex format: POSITION + NORMAL + TEXCOORD
        auto* dest = (float*)vertexBuffer_->Lock(0, numNodes, true);
        if (dest)
        {
            for (int i = 0; i < numNodes; ++i)
            {
                const btSoftBody::Node& node = softBody_->m_nodes[i];
                // Position — guard NaN
                float px = std::isfinite(node.m_x.x()) ? node.m_x.x() : 0.0f;
                float py = std::isfinite(node.m_x.y()) ? node.m_x.y() : 0.0f;
                float pz = std::isfinite(node.m_x.z()) ? node.m_x.z() : 0.0f;
                *dest++ = px;
                *dest++ = py;
                *dest++ = pz;
                // Normal — guard NaN
                float nx = std::isfinite(node.m_n.x()) ? node.m_n.x() : 0.0f;
                float ny = std::isfinite(node.m_n.y()) ? node.m_n.y() : 1.0f;
                float nz = std::isfinite(node.m_n.z()) ? node.m_n.z() : 0.0f;
                *dest++ = nx;
                *dest++ = ny;
                *dest++ = nz;
                // Texcoord — generate from grid position for patches, spherical for ellipsoids
                if (softBodyType_ == SOFTBODY_PATCH)
                {
                    int ix = i % patchResX_;
                    int iy = i / patchResX_;
                    *dest++ = (float)ix / (patchResX_ - 1);
                    *dest++ = (float)iy / (patchResY_ - 1);
                }
                else
                {
                    *dest++ = atan2f(nx, nz) / (2.0f * M_PI) + 0.5f;
                    *dest++ = ny * 0.5f + 0.5f;
                }
            }
            vertexBuffer_->Unlock();
        }
    }

}

void SoftBody::UpdateGeometry(const FrameInfo& frame)
{
    if (geometryDirty_)
    {
        UpdateVisualMesh();
        geometryDirty_ = false;
    }
}

UpdateGeometryType SoftBody::GetUpdateGeometryType()
{
    return geometryDirty_ ? UPDATE_MAIN_THREAD : UPDATE_NONE;
}

void SoftBody::HandlePhysicsPostStep(StringHash eventType, VariantMap& eventData)
{
    // Check for NaN/infinity in soft body nodes
    if (softBody_)
    {
        for (int i = 0; i < softBody_->m_nodes.size(); ++i)
        {
            const btVector3& p = softBody_->m_nodes[i].m_x;
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()))
            {
                URHO3D_LOGERROR("SoftBody node " + String(i) + " has NaN/Inf position! Type=" +
                    String((int)softBodyType_));
                return;  // Don't update — simulation is broken
            }
        }
    }

    // Mark dirty — actual vertex buffer update deferred to UpdateGeometry (render phase, after frame sync)
    geometryDirty_ = true;
    worldBoundingBoxDirty_ = true;
    if (octant_)
        octant_->GetRoot()->QueueUpdate(this);
}

void SoftBody::ApplyConfig()
{
    if (!softBody_)
        return;

    // Material properties
    if (softBody_->m_materials.size() > 0)
    {
        btSoftBody::Material* mat = softBody_->m_materials[0];
        mat->m_kLST = linearStiffness_;
        mat->m_kAST = angularStiffness_;
        mat->m_kVST = volumeStiffness_;
    }

    // Config properties
    softBody_->m_cfg.kDP = damping_;
    softBody_->m_cfg.kPR = pressure_;
    softBody_->m_cfg.kDF = friction_;
    softBody_->m_cfg.piterations = positionIterations_;

    // Collision margin
    softBody_->getCollisionShape()->setMargin(collisionMargin_);

    // Mass
    softBody_->setTotalMass(totalMass_);

    // Generate bending constraints for better cloth behavior
    if (softBodyType_ == SOFTBODY_PATCH)
        softBody_->generateBendingConstraints(2, softBody_->m_materials[0]);

    // Enable collision flags
    softBody_->m_cfg.collisions |= btSoftBody::fCollision::SDF_RS; // soft-rigid (always)
    if (softSoftCollision_)
        softBody_->m_cfg.collisions |= btSoftBody::fCollision::VF_SS;  // soft-soft (opt-in)
}

} // namespace Urho3D
