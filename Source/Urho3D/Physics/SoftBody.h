// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Graphics/Drawable.h"

class btSoftBody;

namespace Urho3D
{

class Geometry;
class IndexBuffer;
class Material;
class Model;
class PhysicsWorld;
class RigidBody;
class VertexBuffer;

/// Soft body type.
enum SoftBodyType
{
    SOFTBODY_ROPE = 0,
    SOFTBODY_PATCH,
    SOFTBODY_ELLIPSOID,
    SOFTBODY_TRIMESH,
    SOFTBODY_CONVEXHULL
};

/// Soft body physics component. Wraps Bullet's btSoftBody for cloth, rope, and deformable mesh simulation.
class URHO3D_API SoftBody : public Drawable
{
    URHO3D_OBJECT(SoftBody, Drawable);

public:
    /// Construct.
    explicit SoftBody(Context* context);
    /// Destruct.
    ~SoftBody() override;
    /// Register object factory. Drawable must be registered first.
    static void RegisterObject(Context* context);

    /// Return the geometry for a specific LOD level.
    Geometry* GetLodGeometry(i32 batchIndex, i32 level) override;
    /// Process octree update. Overridden to use identity transform since soft body vertices are in world space.
    void UpdateBatches(const FrameInfo& frame) override;
    /// Prepare geometry for rendering. Called during render phase after frame sync.
    void UpdateGeometry(const FrameInfo& frame) override;
    /// Return whether a geometry update is necessary.
    UpdateGeometryType GetUpdateGeometryType() override;
    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

    // === Creation Methods ===

    /// Create a rope between two points.
    void CreateRope(const Vector3& from, const Vector3& to, int resolution, int fixedEndsMask);
    /// Create a rectangular cloth patch.
    void CreatePatch(const Vector3& corner00, const Vector3& corner10,
                     const Vector3& corner01, const Vector3& corner11,
                     int resX, int resY, int fixedCornersMask, bool generateDiagonals);
    /// Create an ellipsoid volume.
    void CreateEllipsoid(const Vector3& center, const Vector3& radius, int resolution);
    /// Create from a triangle mesh Model resource.
    void CreateFromModel(Model* model, int lodLevel = 0);
    /// Create from a convex hull of a Model resource.
    void CreateConvexHull(Model* model, int lodLevel = 0);

    // === Configuration Properties ===

    /// @property
    void SetLinearStiffness(float stiffness);
    /// @property
    float GetLinearStiffness() const { return linearStiffness_; }
    /// @property
    void SetAngularStiffness(float stiffness);
    /// @property
    float GetAngularStiffness() const { return angularStiffness_; }
    /// @property
    void SetVolumeStiffness(float stiffness);
    /// @property
    float GetVolumeStiffness() const { return volumeStiffness_; }
    /// @property
    void SetDamping(float damping);
    /// @property
    float GetDamping() const { return damping_; }
    /// @property
    void SetPressure(float pressure);
    /// @property
    float GetPressure() const { return pressure_; }
    /// @property
    void SetFriction(float friction);
    /// @property
    float GetFriction() const { return friction_; }
    /// @property
    void SetPositionIterations(int iterations);
    /// @property
    int GetPositionIterations() const { return positionIterations_; }
    /// @property
    void SetTotalMass(float mass, bool fromFaces = false);
    /// @property
    float GetTotalMass() const { return totalMass_; }
    /// @property
    void SetCollisionMargin(float margin);
    /// @property
    float GetCollisionMargin() const { return collisionMargin_; }
    /// @property
    void SetCollisionLayer(unsigned layer);
    /// @property
    unsigned GetCollisionLayer() const { return collisionLayer_; }
    /// @property
    void SetCollisionMask(unsigned mask);
    /// @property
    unsigned GetCollisionMask() const { return collisionMask_; }
    /// Enable/disable soft-soft collision (VF_SS). Default false.
    /// @property
    void SetSoftSoftCollision(bool enable);
    /// @property
    bool GetSoftSoftCollision() const { return softSoftCollision_; }

    // === Anchor/Attachment ===

    /// Attach a soft body node to a rigid body.
    void AppendAnchor(int nodeIndex, RigidBody* body, bool disableCollision = false, float influence = 1.0f);

    // === Accessors ===

    /// Return number of soft body nodes (vertices).
    /// @property
    int GetNumNodes() const;
    /// Return position of a specific node.
    Vector3 GetNodePosition(int index) const;
    /// Return velocity of a specific node.
    Vector3 GetNodeVelocity(int index) const;
    /// Return the Bullet soft body.
    btSoftBody* GetSoftBody() const { return softBody_; }
    /// Return soft body type.
    /// @property
    SoftBodyType GetSoftBodyType() const { return softBodyType_; }

    // === Force/Impulse ===

    /// Apply force to all nodes.
    void AddForce(const Vector3& force);
    /// Apply force to a specific node.
    void AddNodeForce(const Vector3& force, int nodeIndex);
    /// Set velocity of all nodes.
    void SetVelocity(const Vector3& velocity);
    /// Set wind velocity.
    /// @property
    void SetWindVelocity(const Vector3& wind);
    /// @property
    Vector3 GetWindVelocity() const;

    // === Lifecycle ===

    /// Release the soft body from the physics world.
    void ReleaseSoftBody();
    /// Handle enabled/disabled state change.
    void OnSetEnabled() override;

    /// Set material on the soft body drawable.
    /// @property
    void SetMaterial(Material* material);
    /// Return material.
    /// @property
    Material* GetMaterial() const;

    // === Serialization Attributes ===
    void SetModelAttr(const ResourceRef& value);
    ResourceRef GetModelAttr() const;

protected:
    /// Handle scene being assigned.
    void OnSceneSet(Scene* scene) override;
    /// Recalculate the world-space bounding box.
    void OnWorldBoundingBoxUpdate() override;

private:
    /// Create the Bullet soft body and add to world.
    void AddBodyToWorld();
    /// Remove from world.
    void RemoveBodyFromWorld();
    /// Create visual rendering mesh from soft body data.
    void CreateVisualMesh();
    /// Update visual mesh vertex data from soft body node positions.
    void UpdateVisualMesh();
    /// Handle physics post-step to update visual mesh.
    void HandlePhysicsPostStep(StringHash eventType, VariantMap& eventData);
    /// Apply configuration to soft body.
    void ApplyConfig();

    /// Bullet soft body.
    btSoftBody* softBody_{};
    /// Physics world.
    WeakPtr<PhysicsWorld> physicsWorld_;
    /// Model resource (for TriMesh/ConvexHull types).
    SharedPtr<Model> model_;

    // Visual rendering
    SharedPtr<VertexBuffer> vertexBuffer_;
    SharedPtr<IndexBuffer> indexBuffer_;
    SharedPtr<Geometry> geometry_;

    // Soft body configuration
    SoftBodyType softBodyType_{SOFTBODY_PATCH};
    float linearStiffness_{0.5f};
    float angularStiffness_{0.5f};
    float volumeStiffness_{0.5f};
    float damping_{0.01f};
    float pressure_{0.0f};
    float friction_{0.5f};
    float totalMass_{1.0f};
    float collisionMargin_{0.05f};
    int positionIterations_{4};
    unsigned collisionLayer_{1};
    unsigned collisionMask_{0xFFFFFFFF};
    bool softSoftCollision_{false};

    // Creation parameters (for serialization)
    Vector3 ropeFrom_;
    Vector3 ropeTo_{Vector3::FORWARD};
    int ropeResolution_{8};
    int fixedEndsMask_{0};
    Vector3 patchCorner00_;
    Vector3 patchCorner10_{Vector3::RIGHT};
    Vector3 patchCorner01_{Vector3::FORWARD};
    Vector3 patchCorner11_{Vector3(1.0f, 0.0f, 1.0f)};
    int patchResX_{8};
    int patchResY_{8};
    int fixedCornersMask_{0};
    bool patchDiagonals_{true};
    Vector3 ellipsoidCenter_;
    Vector3 ellipsoidRadius_{Vector3::ONE};
    int ellipsoidResolution_{16};
    int modelLodLevel_{0};

    // State
    bool inWorld_{};
    bool geometryDirty_{};
};

}
