// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../Graphics/DebugRenderer.h"
#include "../IO/Log.h"
#include "../Physics/Constraint.h"
#include "../Physics/PhysicsUtils.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/RigidBody.h"
#include "../Scene/Scene.h"

#include <Bullet/BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btGeneric6DofConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btGearConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <Bullet/BulletDynamics/ConstraintSolver/btSliderConstraint.h>
#include <Bullet/BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>

using namespace std;

namespace Urho3D
{

static const char* typeNames[] =
{
    "Point",
    "Hinge",
    "Slider",
    "ConeTwist",
    "6Dof",
    "6DofSpring",
    "Gear",
    "6DofSpring2",
    nullptr
};

extern const char* PHYSICS_CATEGORY;

Constraint::Constraint(Context* context) :
    Component(context),
    constraintType_(CONSTRAINT_POINT),
    position_(Vector3::ZERO),
    rotation_(Quaternion::IDENTITY),
    otherPosition_(Vector3::ZERO),
    otherRotation_(Quaternion::IDENTITY),
    highLimit_(Vector2::ZERO),
    lowLimit_(Vector2::ZERO),
    erp_(0.0f),
    cfm_(0.0f),
    otherBodyNodeID_(0),
    disableCollision_(false),
    linearLowerLimit_(Vector3::ZERO),
    linearUpperLimit_(Vector3::ZERO),
    angularLowerLimit_(Vector3::ZERO),
    angularUpperLimit_(Vector3::ZERO),
    gearRatio_(1.0f),
    gearAxisA_(Vector3::UP),
    gearAxisB_(Vector3::UP),
    recreateConstraint_(true),
    framesDirty_(false),
    retryCreation_(false)
{
}

Constraint::~Constraint()
{
    ReleaseConstraint();

    if (physicsWorld_)
        physicsWorld_->RemoveConstraint(this);
}

void Constraint::RegisterObject(Context* context)
{
    context->RegisterFactory<Constraint>(PHYSICS_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, true, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Constraint Type", constraintType_, MarkConstraintDirty, typeNames, CONSTRAINT_POINT, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Position", position_, AdjustOtherBodyPosition, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Rotation", rotation_, MarkFramesDirty, Quaternion::IDENTITY, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Other Body Position", otherPosition_, MarkFramesDirty, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Other Body Rotation", otherRotation_, MarkFramesDirty, Quaternion::IDENTITY, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Other Body NodeID", otherBodyNodeID_, MarkConstraintDirty, 0, AM_DEFAULT | AM_NODEID);
    URHO3D_ACCESSOR_ATTRIBUTE("High Limit", GetHighLimit, SetHighLimit, Vector2::ZERO, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Low Limit", GetLowLimit, SetLowLimit, Vector2::ZERO, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("ERP Parameter", GetERP, SetERP, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("CFM Parameter", GetCFM, SetCFM, 0.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Disable Collision", disableCollision_, MarkConstraintDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Linear Lower Limit", linearLowerLimit_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Linear Upper Limit", linearUpperLimit_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Angular Lower Limit", angularLowerLimit_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Angular Upper Limit", angularUpperLimit_, Vector3::ZERO, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Gear Ratio", gearRatio_, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Gear Axis A", gearAxisA_, Vector3::UP, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Gear Axis B", gearAxisB_, Vector3::UP, AM_DEFAULT);
}

void Constraint::ApplyAttributes()
{
    if (recreateConstraint_)
    {
        if (otherBody_)
            otherBody_->RemoveConstraint(this);

        otherBody_.Reset();

        Scene* scene = GetScene();
        if (scene && otherBodyNodeID_)
        {
            Node* otherNode = scene->GetNode(otherBodyNodeID_);
            if (otherNode)
                otherBody_ = otherNode->GetComponent<RigidBody>();
        }

        CreateConstraint();
    }
    else if (framesDirty_)
    {
        ApplyFrames();
        framesDirty_ = false;
    }
}

void Constraint::OnSetEnabled()
{
    if (constraint_)
        constraint_->setEnabled(IsEnabledEffective());
}

void Constraint::GetDependencyNodes(Vector<Node*>& dest)
{
    if (otherBody_ && otherBody_->GetNode())
        dest.Push(otherBody_->GetNode());
}

void Constraint::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    if (debug && physicsWorld_ && constraint_)
    {
        physicsWorld_->SetDebugRenderer(debug);
        physicsWorld_->SetDebugDepthTest(depthTest);
        physicsWorld_->GetWorld()->debugDrawConstraint(constraint_.get());
        physicsWorld_->SetDebugRenderer(nullptr);
    }
}

void Constraint::SetConstraintType(ConstraintType type)
{
    if (type != constraintType_ || !constraint_)
    {
        constraintType_ = type;
        CreateConstraint();
        MarkNetworkUpdate();
    }
}

void Constraint::SetOtherBody(RigidBody* body)
{
    if (otherBody_ != body)
    {
        if (otherBody_)
            otherBody_->RemoveConstraint(this);

        otherBody_ = body;

        // Update the connected body attribute
        Node* otherNode = otherBody_ ? otherBody_->GetNode() : nullptr;
        otherBodyNodeID_ = otherNode ? otherNode->GetID() : 0;

        CreateConstraint();
        MarkNetworkUpdate();
    }
}

void Constraint::SetPosition(const Vector3& position)
{
    if (position != position_)
    {
        position_ = position;
        ApplyFrames();
        MarkNetworkUpdate();
    }
}

void Constraint::SetRotation(const Quaternion& rotation)
{
    if (rotation != rotation_)
    {
        rotation_ = rotation;
        ApplyFrames();
        MarkNetworkUpdate();
    }
}

void Constraint::SetAxis(const Vector3& axis)
{
    switch (constraintType_)
    {
    case CONSTRAINT_POINT:
    case CONSTRAINT_HINGE:
        rotation_ = Quaternion(Vector3::FORWARD, axis);
        break;

    case CONSTRAINT_SLIDER:
    case CONSTRAINT_CONETWIST:
    case CONSTRAINT_6DOF:
    case CONSTRAINT_6DOF_SPRING:
    case CONSTRAINT_6DOF_SPRING2:
        rotation_ = Quaternion(Vector3::RIGHT, axis);
        break;

    default:
        break;
    }

    ApplyFrames();
    MarkNetworkUpdate();
}

void Constraint::SetOtherPosition(const Vector3& position)
{
    if (position != otherPosition_)
    {
        otherPosition_ = position;
        ApplyFrames();
        MarkNetworkUpdate();
    }
}

void Constraint::SetOtherRotation(const Quaternion& rotation)
{
    if (rotation != otherRotation_)
    {
        otherRotation_ = rotation;
        ApplyFrames();
        MarkNetworkUpdate();
    }
}

void Constraint::SetOtherAxis(const Vector3& axis)
{
    switch (constraintType_)
    {
    case CONSTRAINT_POINT:
    case CONSTRAINT_HINGE:
        otherRotation_ = Quaternion(Vector3::FORWARD, axis);
        break;

    case CONSTRAINT_SLIDER:
    case CONSTRAINT_CONETWIST:
    case CONSTRAINT_6DOF:
    case CONSTRAINT_6DOF_SPRING:
    case CONSTRAINT_6DOF_SPRING2:
        otherRotation_ = Quaternion(Vector3::RIGHT, axis);
        break;

    default:
        break;
    }

    ApplyFrames();
    MarkNetworkUpdate();
}

void Constraint::SetWorldPosition(const Vector3& position)
{
    if (constraint_)
    {
        btTransform ownBodyInverse = constraint_->getRigidBodyA().getWorldTransform().inverse();
        btTransform otherBodyInverse = constraint_->getRigidBodyB().getWorldTransform().inverse();
        btVector3 worldPos = ToBtVector3(position);
        position_ = (ToVector3(ownBodyInverse * worldPos) + ownBody_->GetCenterOfMass()) / cachedWorldScale_;
        otherPosition_ = ToVector3(otherBodyInverse * worldPos);
        if (otherBody_)
        {
            otherPosition_ += otherBody_->GetCenterOfMass();
            otherPosition_ /= otherBody_->GetNode()->GetWorldScale();
        }
        ApplyFrames();
        MarkNetworkUpdate();
    }
    else
        URHO3D_LOGWARNING("Constraint not created, world position could not be stored");
}

void Constraint::SetHighLimit(const Vector2& limit)
{
    if (limit != highLimit_)
    {
        highLimit_ = limit;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetLowLimit(const Vector2& limit)
{
    if (limit != lowLimit_)
    {
        lowLimit_ = limit;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetERP(float erp)
{
    erp = Max(erp, 0.0f);

    if (erp != erp_)
    {
        erp_ = erp;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetCFM(float cfm)
{
    cfm = Max(cfm, 0.0f);

    if (cfm != cfm_)
    {
        cfm_ = cfm;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetDisableCollision(bool disable)
{
    if (disable != disableCollision_)
    {
        disableCollision_ = disable;
        CreateConstraint();
        MarkNetworkUpdate();
    }
}

Vector3 Constraint::GetWorldPosition() const
{
    if (constraint_)
    {
        btTransform ownBody = constraint_->getRigidBodyA().getWorldTransform();
        return ToVector3(ownBody * ToBtVector3(position_ * cachedWorldScale_ - ownBody_->GetCenterOfMass()));
    }
    else
        return Vector3::ZERO;
}

void Constraint::ReleaseConstraint()
{
    if (constraint_)
    {
        if (ownBody_)
            ownBody_->RemoveConstraint(this);
        if (otherBody_)
            otherBody_->RemoveConstraint(this);

        if (physicsWorld_)
            physicsWorld_->GetWorld()->removeConstraint(constraint_.get());

        constraint_.reset();
        equilibriumSet_ = false;
    }
}

void Constraint::ApplyFrames()
{
    if (!constraint_ || !node_ || (otherBody_ && !otherBody_->GetNode()))
        return;

    cachedWorldScale_ = node_->GetWorldScale();

    Vector3 ownBodyScaledPosition = position_ * cachedWorldScale_ - ownBody_->GetCenterOfMass();
    Vector3 otherBodyScaledPosition =
        otherBody_ ? otherPosition_ * otherBody_->GetNode()->GetWorldScale() - otherBody_->GetCenterOfMass() : otherPosition_;

    switch (constraint_->getConstraintType())
    {
    case POINT2POINT_CONSTRAINT_TYPE:
        {
            auto* pointConstraint = static_cast<btPoint2PointConstraint*>(constraint_.get());
            pointConstraint->setPivotA(ToBtVector3(ownBodyScaledPosition));
            pointConstraint->setPivotB(ToBtVector3(otherBodyScaledPosition));
        }
        break;

    case HINGE_CONSTRAINT_TYPE:
        {
            auto* hingeConstraint = static_cast<btHingeConstraint*>(constraint_.get());
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            hingeConstraint->setFrames(ownFrame, otherFrame);
        }
        break;

    case SLIDER_CONSTRAINT_TYPE:
        {
            auto* sliderConstraint = static_cast<btSliderConstraint*>(constraint_.get());
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            sliderConstraint->setFrames(ownFrame, otherFrame);
        }
        break;

    case CONETWIST_CONSTRAINT_TYPE:
        {
            auto* coneTwistConstraint = static_cast<btConeTwistConstraint*>(constraint_.get());
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            coneTwistConstraint->setFrames(ownFrame, otherFrame);
        }
        break;

    case D6_CONSTRAINT_TYPE:
        {
            auto* dofConstraint = static_cast<btGeneric6DofConstraint*>(constraint_.get());
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            dofConstraint->setFrames(ownFrame, otherFrame);
        }
        break;

    case D6_SPRING_CONSTRAINT_TYPE:
        {
            auto* springConstraint = static_cast<btGeneric6DofSpringConstraint*>(constraint_.get());
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            springConstraint->setFrames(ownFrame, otherFrame);
        }
        break;

    case D6_SPRING_2_CONSTRAINT_TYPE:
        {
            auto* spring2Constraint = static_cast<btGeneric6DofSpring2Constraint*>(constraint_.get());
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            spring2Constraint->setFrames(ownFrame, otherFrame);
        }
        break;

    case GEAR_CONSTRAINT_TYPE:
        {
            auto* gearConstraint = static_cast<btGearConstraint*>(constraint_.get());
            btVector3 axisA = ToBtVector3(gearAxisA_);
            btVector3 axisB = ToBtVector3(gearAxisB_);
            gearConstraint->setAxisA(axisA);
            gearConstraint->setAxisB(axisB);
        }
        break;

    default:
        break;
    }
}

void Constraint::OnNodeSet(Node* node)
{
    if (node)
    {
        node->AddListener(this);
        cachedWorldScale_ = node->GetWorldScale();
    }
}

void Constraint::OnSceneSet(Scene* scene)
{
    if (scene)
    {
        if (scene == node_)
            URHO3D_LOGWARNING(GetTypeName() + " should not be created to the root scene node");

        physicsWorld_ = scene->GetOrCreateComponent<PhysicsWorld>();
        physicsWorld_->AddConstraint(this);

        // Create constraint now if necessary (attributes modified before adding to scene)
        if (retryCreation_)
            CreateConstraint();
    }
    else
    {
        ReleaseConstraint();

        if (physicsWorld_)
            physicsWorld_->RemoveConstraint(this);

        // Recreate when moved to a scene again
        retryCreation_ = true;
    }
}

void Constraint::OnMarkedDirty(Node* node)
{
    /// \todo This does not catch the connected body node's scale changing
    if (HasWorldScaleChanged(cachedWorldScale_, node->GetWorldScale()))
        ApplyFrames();
}

void Constraint::CreateConstraint()
{
    URHO3D_PROFILE(CreateConstraint);

    cachedWorldScale_ = node_->GetWorldScale();

    ReleaseConstraint();

    ownBody_ = GetComponent<RigidBody>();
    btRigidBody* ownBody = ownBody_ ? ownBody_->GetBody() : nullptr;
    btRigidBody* otherBody = otherBody_ ? otherBody_->GetBody() : nullptr;

    // If no physics world available now mark for retry later
    if (!physicsWorld_ || !ownBody)
    {
        retryCreation_ = true;
        return;
    }

    if (!otherBody)
        otherBody = &btTypedConstraint::getFixedBody();

    Vector3 ownBodyScaledPosition = position_ * cachedWorldScale_ - ownBody_->GetCenterOfMass();
    Vector3 otherBodyScaledPosition = otherBody_ ? otherPosition_ * otherBody_->GetNode()->GetWorldScale() -
                                                   otherBody_->GetCenterOfMass() : otherPosition_;

    switch (constraintType_)
    {
    case CONSTRAINT_POINT:
        {
            constraint_ = make_unique<btPoint2PointConstraint>(*ownBody, *otherBody, ToBtVector3(ownBodyScaledPosition),
                ToBtVector3(otherBodyScaledPosition));
        }
        break;

    case CONSTRAINT_HINGE:
        {
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            constraint_ = make_unique<btHingeConstraint>(*ownBody, *otherBody, ownFrame, otherFrame);
        }
        break;

    case CONSTRAINT_SLIDER:
        {
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            constraint_ = make_unique<btSliderConstraint>(*ownBody, *otherBody, ownFrame, otherFrame, false);
        }
        break;

    case CONSTRAINT_CONETWIST:
        {
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            constraint_ = make_unique<btConeTwistConstraint>(*ownBody, *otherBody, ownFrame, otherFrame);
        }
        break;

    case CONSTRAINT_6DOF:
        {
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            constraint_ = make_unique<btGeneric6DofConstraint>(*ownBody, *otherBody, ownFrame, otherFrame, false);
        }
        break;

    case CONSTRAINT_6DOF_SPRING:
        {
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            constraint_ = make_unique<btGeneric6DofSpringConstraint>(*ownBody, *otherBody, ownFrame, otherFrame, false);
        }
        break;

    case CONSTRAINT_GEAR:
        {
            constraint_ = make_unique<btGearConstraint>(*ownBody, *otherBody,
                ToBtVector3(gearAxisA_), ToBtVector3(gearAxisB_), gearRatio_);
        }
        break;

    case CONSTRAINT_6DOF_SPRING2:
        {
            btTransform ownFrame(ToBtQuaternion(rotation_), ToBtVector3(ownBodyScaledPosition));
            btTransform otherFrame(ToBtQuaternion(otherRotation_), ToBtVector3(otherBodyScaledPosition));
            constraint_ = make_unique<btGeneric6DofSpring2Constraint>(*ownBody, *otherBody, ownFrame, otherFrame);
        }
        break;

    default:
        break;
    }

    if (constraint_)
    {
        constraint_->setUserConstraintPtr(this);
        constraint_->setEnabled(IsEnabledEffective());
        ownBody_->AddConstraint(this);
        if (otherBody_)
            otherBody_->AddConstraint(this);

        ApplyLimits();

        physicsWorld_->GetWorld()->addConstraint(constraint_.get(), disableCollision_);
    }

    recreateConstraint_ = false;
    framesDirty_ = false;
    retryCreation_ = false;
}

void Constraint::ApplyLimits()
{
    if (!constraint_)
        return;

    switch (constraint_->getConstraintType())
    {
    case HINGE_CONSTRAINT_TYPE:
        {
            auto* hingeConstraint = static_cast<btHingeConstraint*>(constraint_.get());
            hingeConstraint->setLimit(lowLimit_.x_ * M_DEGTORAD, highLimit_.x_ * M_DEGTORAD);
        }
        break;

    case SLIDER_CONSTRAINT_TYPE:
        {
            auto* sliderConstraint = static_cast<btSliderConstraint*>(constraint_.get());
            sliderConstraint->setUpperLinLimit(highLimit_.x_);
            sliderConstraint->setUpperAngLimit(highLimit_.y_ * M_DEGTORAD);
            sliderConstraint->setLowerLinLimit(lowLimit_.x_);
            sliderConstraint->setLowerAngLimit(lowLimit_.y_ * M_DEGTORAD);
        }
        break;

    case CONETWIST_CONSTRAINT_TYPE:
        {
            auto* coneTwistConstraint = static_cast<btConeTwistConstraint*>(constraint_.get());
            coneTwistConstraint->setLimit(highLimit_.y_ * M_DEGTORAD, highLimit_.y_ * M_DEGTORAD, highLimit_.x_ * M_DEGTORAD);
        }
        break;

    case D6_CONSTRAINT_TYPE:
        {
            auto* dofConstraint = static_cast<btGeneric6DofConstraint*>(constraint_.get());
            dofConstraint->setLinearLowerLimit(ToBtVector3(linearLowerLimit_));
            dofConstraint->setLinearUpperLimit(ToBtVector3(linearUpperLimit_));
            dofConstraint->setAngularLowerLimit(ToBtVector3(angularLowerLimit_ * M_DEGTORAD));
            dofConstraint->setAngularUpperLimit(ToBtVector3(angularUpperLimit_ * M_DEGTORAD));

            // Apply motors
            btTranslationalLimitMotor* transMotor = dofConstraint->getTranslationalLimitMotor();
            for (int i = 0; i < 3; i++)
            {
                transMotor->m_enableMotor[i] = motorEnabled_[i];
                transMotor->m_targetVelocity[i] = motorTargetVelocity_[i];
                transMotor->m_maxMotorForce[i] = motorMaxForce_[i];
            }
            for (int i = 0; i < 3; i++)
            {
                btRotationalLimitMotor* rotMotor = dofConstraint->getRotationalLimitMotor(i);
                rotMotor->m_enableMotor = motorEnabled_[i + 3];
                rotMotor->m_targetVelocity = motorTargetVelocity_[i + 3];
                rotMotor->m_maxMotorForce = motorMaxForce_[i + 3];
            }
        }
        break;

    case D6_SPRING_CONSTRAINT_TYPE:
        {
            auto* springConstraint = static_cast<btGeneric6DofSpringConstraint*>(constraint_.get());
            springConstraint->setLinearLowerLimit(ToBtVector3(linearLowerLimit_));
            springConstraint->setLinearUpperLimit(ToBtVector3(linearUpperLimit_));
            springConstraint->setAngularLowerLimit(ToBtVector3(angularLowerLimit_ * M_DEGTORAD));
            springConstraint->setAngularUpperLimit(ToBtVector3(angularUpperLimit_ * M_DEGTORAD));

            // Apply motors (inherited from 6DOF)
            btTranslationalLimitMotor* transMotor = springConstraint->getTranslationalLimitMotor();
            for (int i = 0; i < 3; i++)
            {
                transMotor->m_enableMotor[i] = motorEnabled_[i];
                transMotor->m_targetVelocity[i] = motorTargetVelocity_[i];
                transMotor->m_maxMotorForce[i] = motorMaxForce_[i];
            }
            for (int i = 0; i < 3; i++)
            {
                btRotationalLimitMotor* rotMotor = springConstraint->getRotationalLimitMotor(i);
                rotMotor->m_enableMotor = motorEnabled_[i + 3];
                rotMotor->m_targetVelocity = motorTargetVelocity_[i + 3];
                rotMotor->m_maxMotorForce = motorMaxForce_[i + 3];
            }

            // Apply springs
            for (int i = 0; i < 6; i++)
            {
                springConstraint->enableSpring(i, springEnabled_[i]);
                springConstraint->setStiffness(i, springStiffness_[i]);
                springConstraint->setDamping(i, springDamping_[i]);
            }
            if (!equilibriumSet_)
            {
                springConstraint->setEquilibriumPoint();
                equilibriumSet_ = true;
            }
        }
        break;

    case D6_SPRING_2_CONSTRAINT_TYPE:
        {
            auto* spring2Constraint = static_cast<btGeneric6DofSpring2Constraint*>(constraint_.get());
            spring2Constraint->setLinearLowerLimit(ToBtVector3(linearLowerLimit_));
            spring2Constraint->setLinearUpperLimit(ToBtVector3(linearUpperLimit_));
            spring2Constraint->setAngularLowerLimit(ToBtVector3(angularLowerLimit_ * M_DEGTORAD));
            spring2Constraint->setAngularUpperLimit(ToBtVector3(angularUpperLimit_ * M_DEGTORAD));

            // Apply motors and springs
            for (int i = 0; i < 6; i++)
            {
                spring2Constraint->enableMotor(i, motorEnabled_[i]);
                spring2Constraint->setTargetVelocity(i, motorTargetVelocity_[i]);
                spring2Constraint->setMaxMotorForce(i, motorMaxForce_[i]);
                spring2Constraint->enableSpring(i, springEnabled_[i]);
                spring2Constraint->setStiffness(i, springStiffness_[i]);
                spring2Constraint->setDamping(i, springDamping_[i]);
            }
            if (!equilibriumSet_)
            {
                spring2Constraint->setEquilibriumPoint();
                equilibriumSet_ = true;
            }
        }
        break;

    case GEAR_CONSTRAINT_TYPE:
        {
            auto* gearConstraint = static_cast<btGearConstraint*>(constraint_.get());
            gearConstraint->setRatio(gearRatio_);
        }
        break;

    default:
        break;
    }

    if (erp_ != 0.0f)
        constraint_->setParam(BT_CONSTRAINT_STOP_ERP, erp_);
    if (cfm_ != 0.0f)
        constraint_->setParam(BT_CONSTRAINT_STOP_CFM, cfm_);
}

void Constraint::SetLinearLowerLimit(const Vector3& limit)
{
    if (limit != linearLowerLimit_)
    {
        linearLowerLimit_ = limit;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetLinearUpperLimit(const Vector3& limit)
{
    if (limit != linearUpperLimit_)
    {
        linearUpperLimit_ = limit;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetAngularLowerLimit(const Vector3& limit)
{
    if (limit != angularLowerLimit_)
    {
        angularLowerLimit_ = limit;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetAngularUpperLimit(const Vector3& limit)
{
    if (limit != angularUpperLimit_)
    {
        angularUpperLimit_ = limit;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::EnableMotor(int index, bool enable)
{
    if (index >= 0 && index < 6)
    {
        motorEnabled_[index] = enable;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetMotorTargetVelocity(int index, float velocity)
{
    if (index >= 0 && index < 6)
    {
        motorTargetVelocity_[index] = velocity;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetMotorMaxForce(int index, float force)
{
    if (index >= 0 && index < 6)
    {
        motorMaxForce_[index] = force;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::EnableSpring(int index, bool enable)
{
    if (index >= 0 && index < 6)
    {
        springEnabled_[index] = enable;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetSpringStiffness(int index, float stiffness)
{
    if (index >= 0 && index < 6)
    {
        springStiffness_[index] = stiffness;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetSpringDamping(int index, float damping)
{
    if (index >= 0 && index < 6)
    {
        springDamping_[index] = damping;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetGearRatio(float ratio)
{
    if (ratio != gearRatio_)
    {
        gearRatio_ = ratio;
        ApplyLimits();
        MarkNetworkUpdate();
    }
}

void Constraint::SetGearAxisA(const Vector3& axis)
{
    if (axis != gearAxisA_)
    {
        gearAxisA_ = axis;
        if (constraint_)
            ApplyFrames();
        MarkNetworkUpdate();
    }
}

void Constraint::SetGearAxisB(const Vector3& axis)
{
    if (axis != gearAxisB_)
    {
        gearAxisB_ = axis;
        if (constraint_)
            ApplyFrames();
        MarkNetworkUpdate();
    }
}

void Constraint::AdjustOtherBodyPosition()
{
    // Convenience for editing static constraints: if not connected to another body, adjust world position to match local
    // (when deserializing, the proper other body position will be read after own position, so this calculation is safely
    // overridden and does not accumulate constraint error
    if (constraint_ && !otherBody_)
    {
        btTransform ownBody = constraint_->getRigidBodyA().getWorldTransform();
        btVector3 worldPos = ownBody * ToBtVector3(position_ * cachedWorldScale_ - ownBody_->GetCenterOfMass());
        otherPosition_ = ToVector3(worldPos);
    }

    MarkFramesDirty();
}

}
