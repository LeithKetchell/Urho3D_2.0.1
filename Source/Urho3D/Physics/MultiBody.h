// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Scene/Component.h"
#include "../Math/Vector3.h"
#include "../Math/Quaternion.h"

#include <memory>

class btMultiBody;
class btMultiBodyLinkCollider;
class btCollisionShape;

namespace Urho3D
{

class PhysicsWorld;
class Node;

/// Featherstone articulated multi-body component. Wraps btMultiBody for stable, O(n) multi-link dynamics.
class URHO3D_API MultiBody : public Component
{
    URHO3D_OBJECT(MultiBody, Component);

public:
    /// Construct.
    explicit MultiBody(Context* context);
    /// Destruct.
    ~MultiBody() override;
    /// Register object factory.
    static void RegisterObject(Context* context);

    // === Setup (call before Build()) ===

    /// Set number of links (NOT including the base).
    void SetNumLinks(int numLinks);
    /// Set base mass. Zero = fixed base.
    void SetBaseMass(float mass);
    /// Set base inertia (diagonal, in base frame).
    void SetBaseInertia(const Vector3& inertia);
    /// Set whether the base is fixed in space.
    void SetFixedBase(bool fixed);
    /// Set whether the multi-body can sleep.
    void SetCanSleep(bool canSleep);

    // === Link setup (call after SetNumLinks, before Build()) ===

    /// Setup a revolute (hinge) joint link.
    void SetupRevolute(int linkIndex, float mass, const Vector3& inertia,
                       int parentIndex, const Quaternion& rotParentToThis,
                       const Vector3& jointAxis,
                       const Vector3& parentComToThisPivotOffset,
                       const Vector3& thisPivotToThisComOffset,
                       bool disableParentCollision = true);
    /// Setup a spherical (ball) joint link.
    void SetupSpherical(int linkIndex, float mass, const Vector3& inertia,
                        int parentIndex, const Quaternion& rotParentToThis,
                        const Vector3& parentComToThisPivotOffset,
                        const Vector3& thisPivotToThisComOffset,
                        bool disableParentCollision = true);
    /// Setup a prismatic (slider) joint link.
    void SetupPrismatic(int linkIndex, float mass, const Vector3& inertia,
                        int parentIndex, const Quaternion& rotParentToThis,
                        const Vector3& jointAxis,
                        const Vector3& parentComToThisPivotOffset,
                        const Vector3& thisPivotToThisComOffset,
                        bool disableParentCollision = true);
    /// Setup a fixed joint link.
    void SetupFixed(int linkIndex, float mass, const Vector3& inertia,
                    int parentIndex, const Quaternion& rotParentToThis,
                    const Vector3& parentComToThisPivotOffset,
                    const Vector3& thisPivotToThisComOffset);
    /// Setup a planar joint link.
    void SetupPlanar(int linkIndex, float mass, const Vector3& inertia,
                     int parentIndex, const Quaternion& rotParentToThis,
                     const Vector3& rotationAxis,
                     const Vector3& parentComToThisComOffset,
                     bool disableParentCollision = true);

    // === Collision shapes ===

    /// Set a box collision shape for a link (-1 = base).
    void SetLinkBoxShape(int linkIndex, const Vector3& halfExtents);
    /// Set a sphere collision shape for a link (-1 = base).
    void SetLinkSphereShape(int linkIndex, float radius);
    /// Set a capsule collision shape for a link (-1 = base).
    void SetLinkCapsuleShape(int linkIndex, float radius, float height);

    // === Visual node mapping ===

    /// Associate a scene node with a link for visual transform sync (-1 = base).
    void SetLinkNode(int linkIndex, Node* node);

    // === Finalize ===

    /// Build the btMultiBody, create colliders, add to physics world.
    void Build();

    // === Runtime control ===

    /// Set joint position for a link.
    void SetJointPos(int linkIndex, float position);
    /// Set joint velocity for a link.
    void SetJointVel(int linkIndex, float velocity);
    /// Add joint torque for a link.
    void AddJointTorque(int linkIndex, float torque);
    /// Set base velocity.
    void SetBaseVel(const Vector3& velocity);
    /// Set base angular velocity.
    void SetBaseOmega(const Vector3& omega);
    /// Apply force to base.
    void ApplyBaseForce(const Vector3& force);
    /// Apply torque to base.
    void ApplyBaseTorque(const Vector3& torque);
    /// Apply force to a link.
    void ApplyLinkForce(int linkIndex, const Vector3& force);
    /// Apply torque to a link.
    void ApplyLinkTorque(int linkIndex, const Vector3& torque);

    // === Query ===

    /// Return joint position.
    float GetJointPos(int linkIndex) const;
    /// Return joint velocity.
    float GetJointVel(int linkIndex) const;
    /// Return number of links.
    int GetNumLinks() const { return numLinks_; }
    /// Return whether built and in world.
    bool IsBuilt() const { return inWorld_; }

    /// Return raw btMultiBody pointer.
    btMultiBody* GetMultiBody() const { return multiBody_.get(); }

    /// Update visual node transforms from physics state. Called by PhysicsWorld after step.
    void UpdateTransforms();

    /// Remove multi-body from world. Called by PhysicsWorld destructor.
    void ReleaseMultiBody();

protected:
    void OnNodeSet(Node* node) override;
    void OnSceneSet(Scene* scene) override;

private:
    void AddToWorld();
    void RemoveFromWorld();

    /// The Bullet multi-body.
    std::unique_ptr<btMultiBody> multiBody_;
    /// Colliders for base + links.
    Vector<std::unique_ptr<btMultiBodyLinkCollider>> colliders_;
    /// Collision shapes for base + links.
    Vector<std::unique_ptr<btCollisionShape>> collisionShapes_;
    /// Visual nodes for each link (-1 = base stored at index 0, link i at index i+1).
    Vector<WeakPtr<Node>> linkNodes_;

    /// Physics world.
    WeakPtr<PhysicsWorld> physicsWorld_;

    /// Setup state.
    int numLinks_{0};
    float baseMass_{1.0f};
    Vector3 baseInertia_{Vector3::ONE};
    bool fixedBase_{false};
    bool canSleep_{true};
    bool inWorld_{false};

    /// Collision layer/mask for all colliders.
    unsigned collisionLayer_{1};
    unsigned collisionMask_{0xFFFF};
};

}
