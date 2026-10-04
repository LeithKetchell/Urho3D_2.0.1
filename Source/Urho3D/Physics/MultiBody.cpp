// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../Physics/MultiBody.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/PhysicsUtils.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"

#include <Bullet/BulletDynamics/Featherstone/btMultiBody.h>
#include <Bullet/BulletDynamics/Featherstone/btMultiBodyLinkCollider.h>
#include <Bullet/BulletSoftBody/btSoftMultiBodyDynamicsWorld.h>
#include <Bullet/BulletCollision/CollisionShapes/btBoxShape.h>
#include <Bullet/BulletCollision/CollisionShapes/btSphereShape.h>
#include <Bullet/BulletCollision/CollisionShapes/btCapsuleShape.h>

namespace Urho3D
{

extern const char* PHYSICS_CATEGORY;

MultiBody::MultiBody(Context* context) :
    Component(context)
{
}

MultiBody::~MultiBody()
{
    ReleaseMultiBody();
}

void MultiBody::RegisterObject(Context* context)
{
    context->RegisterFactory<MultiBody>(PHYSICS_CATEGORY);

    URHO3D_ATTRIBUTE("Num Links", numLinks_, 0, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Base Mass", baseMass_, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Base Inertia", baseInertia_, Vector3::ONE, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Fixed Base", fixedBase_, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Can Sleep", canSleep_, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Collision Layer", collisionLayer_, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Collision Mask", collisionMask_, 0xFFFF, AM_DEFAULT);
}

void MultiBody::SetNumLinks(int numLinks)
{
    if (inWorld_)
    {
        URHO3D_LOGWARNING("Cannot change link count after Build()");
        return;
    }
    numLinks_ = numLinks;
    // Resize node tracking: index 0 = base, 1..numLinks = links
    linkNodes_.Resize(numLinks + 1);
}

void MultiBody::SetBaseMass(float mass)
{
    baseMass_ = mass;
}

void MultiBody::SetBaseInertia(const Vector3& inertia)
{
    baseInertia_ = inertia;
}

void MultiBody::SetFixedBase(bool fixed)
{
    fixedBase_ = fixed;
}

void MultiBody::SetCanSleep(bool canSleep)
{
    canSleep_ = canSleep;
}

void MultiBody::SetupRevolute(int linkIndex, float mass, const Vector3& inertia,
                               int parentIndex, const Quaternion& rotParentToThis,
                               const Vector3& jointAxis,
                               const Vector3& parentComToThisPivotOffset,
                               const Vector3& thisPivotToThisComOffset,
                               bool disableParentCollision)
{
    if (!multiBody_)
    {
        // Create the btMultiBody now if not yet created
        multiBody_ = std::make_unique<btMultiBody>(numLinks_, baseMass_,
            ToBtVector3(baseInertia_), fixedBase_, canSleep_);
    }

    multiBody_->setupRevolute(linkIndex, mass, ToBtVector3(inertia), parentIndex,
        ToBtQuaternion(rotParentToThis), ToBtVector3(jointAxis),
        ToBtVector3(parentComToThisPivotOffset), ToBtVector3(thisPivotToThisComOffset),
        disableParentCollision);
}

void MultiBody::SetupSpherical(int linkIndex, float mass, const Vector3& inertia,
                                int parentIndex, const Quaternion& rotParentToThis,
                                const Vector3& parentComToThisPivotOffset,
                                const Vector3& thisPivotToThisComOffset,
                                bool disableParentCollision)
{
    if (!multiBody_)
    {
        multiBody_ = std::make_unique<btMultiBody>(numLinks_, baseMass_,
            ToBtVector3(baseInertia_), fixedBase_, canSleep_);
    }

    multiBody_->setupSpherical(linkIndex, mass, ToBtVector3(inertia), parentIndex,
        ToBtQuaternion(rotParentToThis),
        ToBtVector3(parentComToThisPivotOffset), ToBtVector3(thisPivotToThisComOffset),
        disableParentCollision);
}

void MultiBody::SetupPrismatic(int linkIndex, float mass, const Vector3& inertia,
                                int parentIndex, const Quaternion& rotParentToThis,
                                const Vector3& jointAxis,
                                const Vector3& parentComToThisPivotOffset,
                                const Vector3& thisPivotToThisComOffset,
                                bool disableParentCollision)
{
    if (!multiBody_)
    {
        multiBody_ = std::make_unique<btMultiBody>(numLinks_, baseMass_,
            ToBtVector3(baseInertia_), fixedBase_, canSleep_);
    }

    multiBody_->setupPrismatic(linkIndex, mass, ToBtVector3(inertia), parentIndex,
        ToBtQuaternion(rotParentToThis), ToBtVector3(jointAxis),
        ToBtVector3(parentComToThisPivotOffset), ToBtVector3(thisPivotToThisComOffset),
        disableParentCollision);
}

void MultiBody::SetupFixed(int linkIndex, float mass, const Vector3& inertia,
                            int parentIndex, const Quaternion& rotParentToThis,
                            const Vector3& parentComToThisPivotOffset,
                            const Vector3& thisPivotToThisComOffset)
{
    if (!multiBody_)
    {
        multiBody_ = std::make_unique<btMultiBody>(numLinks_, baseMass_,
            ToBtVector3(baseInertia_), fixedBase_, canSleep_);
    }

    multiBody_->setupFixed(linkIndex, mass, ToBtVector3(inertia), parentIndex,
        ToBtQuaternion(rotParentToThis),
        ToBtVector3(parentComToThisPivotOffset), ToBtVector3(thisPivotToThisComOffset));
}

void MultiBody::SetupPlanar(int linkIndex, float mass, const Vector3& inertia,
                              int parentIndex, const Quaternion& rotParentToThis,
                              const Vector3& rotationAxis,
                              const Vector3& parentComToThisComOffset,
                              bool disableParentCollision)
{
    if (!multiBody_)
    {
        multiBody_ = std::make_unique<btMultiBody>(numLinks_, baseMass_,
            ToBtVector3(baseInertia_), fixedBase_, canSleep_);
    }

    multiBody_->setupPlanar(linkIndex, mass, ToBtVector3(inertia), parentIndex,
        ToBtQuaternion(rotParentToThis), ToBtVector3(rotationAxis),
        ToBtVector3(parentComToThisComOffset), disableParentCollision);
}

void MultiBody::SetLinkBoxShape(int linkIndex, const Vector3& halfExtents)
{
    int idx = linkIndex + 1; // -1 (base) → 0, 0 → 1, etc.
    if (idx < 0 || idx > numLinks_)
    {
        URHO3D_LOGWARNING("Invalid link index for SetLinkBoxShape");
        return;
    }
    // Ensure collision shapes vector is large enough
    if (collisionShapes_.Size() < (unsigned)(numLinks_ + 1))
        collisionShapes_.Resize(numLinks_ + 1);

    collisionShapes_[idx] = std::make_unique<btBoxShape>(ToBtVector3(halfExtents));
}

void MultiBody::SetLinkSphereShape(int linkIndex, float radius)
{
    int idx = linkIndex + 1;
    if (idx < 0 || idx > numLinks_)
    {
        URHO3D_LOGWARNING("Invalid link index for SetLinkSphereShape");
        return;
    }
    if (collisionShapes_.Size() < (unsigned)(numLinks_ + 1))
        collisionShapes_.Resize(numLinks_ + 1);

    collisionShapes_[idx] = std::make_unique<btSphereShape>(radius);
}

void MultiBody::SetLinkCapsuleShape(int linkIndex, float radius, float height)
{
    int idx = linkIndex + 1;
    if (idx < 0 || idx > numLinks_)
    {
        URHO3D_LOGWARNING("Invalid link index for SetLinkCapsuleShape");
        return;
    }
    if (collisionShapes_.Size() < (unsigned)(numLinks_ + 1))
        collisionShapes_.Resize(numLinks_ + 1);

    collisionShapes_[idx] = std::make_unique<btCapsuleShape>(radius, height);
}

void MultiBody::SetLinkNode(int linkIndex, Node* node)
{
    int idx = linkIndex + 1;
    if (idx < 0 || idx >= (int)linkNodes_.Size())
    {
        URHO3D_LOGWARNING("Invalid link index for SetLinkNode");
        return;
    }
    linkNodes_[idx] = node;
}

void MultiBody::Build()
{
    if (inWorld_)
    {
        URHO3D_LOGWARNING("MultiBody already built");
        return;
    }
    if (!multiBody_)
    {
        URHO3D_LOGWARNING("No links set up — call Setup*() before Build()");
        return;
    }
    if (!physicsWorld_)
    {
        URHO3D_LOGWARNING("MultiBody not in a scene with PhysicsWorld");
        return;
    }

    // Finalize the multi-body
    multiBody_->finalizeMultiDof();

    // Set base position from node
    if (node_)
    {
        multiBody_->setBaseWorldTransform(btTransform(
            ToBtQuaternion(node_->GetWorldRotation()),
            ToBtVector3(node_->GetWorldPosition())));
    }

    // Create colliders for base + each link
    auto* world = physicsWorld_->GetSoftMultiBodyWorld();
    colliders_.Resize(numLinks_ + 1);

    // Base collider
    {
        auto collider = std::make_unique<btMultiBodyLinkCollider>(multiBody_.get(), -1);
        collider->setWorldTransform(multiBody_->getBaseWorldTransform());
        if (collisionShapes_.Size() > 0 && collisionShapes_[0])
            collider->setCollisionShape(collisionShapes_[0].get());
        world->addCollisionObject(collider.get(), (short)collisionLayer_, (short)collisionMask_);
        multiBody_->setBaseCollider(collider.get());
        colliders_[0] = std::move(collider);
    }

    // Link colliders
    for (int i = 0; i < numLinks_; ++i)
    {
        auto collider = std::make_unique<btMultiBodyLinkCollider>(multiBody_.get(), i);
        collider->setWorldTransform(multiBody_->getLink(i).m_cachedWorldTransform);
        int shapeIdx = i + 1;
        if (shapeIdx < (int)collisionShapes_.Size() && collisionShapes_[shapeIdx])
            collider->setCollisionShape(collisionShapes_[shapeIdx].get());
        world->addCollisionObject(collider.get(), (short)collisionLayer_, (short)collisionMask_);
        multiBody_->getLink(i).m_collider = collider.get();
        colliders_[i + 1] = std::move(collider);
    }

    // Add multi-body to world
    world->addMultiBody(multiBody_.get());
    inWorld_ = true;

    physicsWorld_->AddMultiBody(this);
}

void MultiBody::SetJointPos(int linkIndex, float position)
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        multiBody_->setJointPos(linkIndex, position);
}

void MultiBody::SetJointVel(int linkIndex, float velocity)
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        multiBody_->setJointVel(linkIndex, velocity);
}

void MultiBody::AddJointTorque(int linkIndex, float torque)
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        multiBody_->addJointTorque(linkIndex, torque);
}

void MultiBody::SetBaseVel(const Vector3& velocity)
{
    if (multiBody_)
        multiBody_->setBaseVel(ToBtVector3(velocity));
}

void MultiBody::SetBaseOmega(const Vector3& omega)
{
    if (multiBody_)
        multiBody_->setBaseOmega(ToBtVector3(omega));
}

void MultiBody::ApplyBaseForce(const Vector3& force)
{
    if (multiBody_)
        multiBody_->addBaseForce(ToBtVector3(force));
}

void MultiBody::ApplyBaseTorque(const Vector3& torque)
{
    if (multiBody_)
        multiBody_->addBaseTorque(ToBtVector3(torque));
}

void MultiBody::ApplyLinkForce(int linkIndex, const Vector3& force)
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        multiBody_->addLinkForce(linkIndex, ToBtVector3(force));
}

void MultiBody::ApplyLinkTorque(int linkIndex, const Vector3& torque)
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        multiBody_->addLinkTorque(linkIndex, ToBtVector3(torque));
}

float MultiBody::GetJointPos(int linkIndex) const
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        return (float)multiBody_->getJointPos(linkIndex);
    return 0.0f;
}

float MultiBody::GetJointVel(int linkIndex) const
{
    if (multiBody_ && linkIndex >= 0 && linkIndex < numLinks_)
        return (float)multiBody_->getJointVel(linkIndex);
    return 0.0f;
}

void MultiBody::UpdateTransforms()
{
    if (!multiBody_ || !inWorld_)
        return;

    // Update base node
    if (linkNodes_.Size() > 0 && linkNodes_[0])
    {
        const btTransform& baseTr = multiBody_->getBaseWorldTransform();
        linkNodes_[0]->SetWorldPosition(ToVector3(baseTr.getOrigin()));
        linkNodes_[0]->SetWorldRotation(ToQuaternion(baseTr.getRotation()));
    }

    // Update link nodes
    for (int i = 0; i < numLinks_; ++i)
    {
        int idx = i + 1;
        if (idx < (int)linkNodes_.Size() && linkNodes_[idx])
        {
            const btTransform& linkTr = multiBody_->getLink(i).m_cachedWorldTransform;
            linkNodes_[idx]->SetWorldPosition(ToVector3(linkTr.getOrigin()));
            linkNodes_[idx]->SetWorldRotation(ToQuaternion(linkTr.getRotation()));
        }
    }
}

void MultiBody::ReleaseMultiBody()
{
    RemoveFromWorld();
    multiBody_.reset();
}

void MultiBody::OnNodeSet(Node* node)
{
    // Nothing needed until Build()
}

void MultiBody::OnSceneSet(Scene* scene)
{
    if (scene)
    {
        if (scene == node_)
            URHO3D_LOGWARNING("MultiBody should not be created to the root scene node");

        physicsWorld_ = scene->GetOrCreateComponent<PhysicsWorld>();
    }
    else
    {
        RemoveFromWorld();
        physicsWorld_.Reset();
    }
}

void MultiBody::AddToWorld()
{
    // Called by Build()
}

void MultiBody::RemoveFromWorld()
{
    if (!inWorld_ || !physicsWorld_)
        return;

    auto* world = physicsWorld_->GetSoftMultiBodyWorld();

    // Remove colliders
    for (auto& collider : colliders_)
    {
        if (collider)
            world->removeCollisionObject(collider.get());
    }
    colliders_.Clear();

    // Remove multi-body
    if (multiBody_)
        world->removeMultiBody(multiBody_.get());

    physicsWorld_->RemoveMultiBody(this);
    inWorld_ = false;
}

}
