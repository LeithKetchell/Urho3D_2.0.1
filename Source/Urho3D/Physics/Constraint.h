// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

/// \file

#pragma once

#include "../Math/Vector3.h"
#include "../Scene/Component.h"

#include <memory>

class btTypedConstraint;

namespace Urho3D
{

/// Supported constraint types.
enum ConstraintType
{
    CONSTRAINT_POINT = 0,
    CONSTRAINT_HINGE,
    CONSTRAINT_SLIDER,
    CONSTRAINT_CONETWIST,
    CONSTRAINT_6DOF,
    CONSTRAINT_6DOF_SPRING,
    CONSTRAINT_GEAR,
    CONSTRAINT_6DOF_SPRING2
};

class PhysicsWorld;
class RigidBody;

/// Physics constraint component. Connects two rigid bodies together, or one rigid body to a static point.
class URHO3D_API Constraint : public Component
{
    URHO3D_OBJECT(Constraint, Component);

    friend class RigidBody;

public:
    /// Construct.
    explicit Constraint(Context* context);
    /// Destruct.
    ~Constraint() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Apply attribute changes that can not be applied immediately. Called after scene load or a network update.
    void ApplyAttributes() override;
    /// Handle enabled/disabled state change.
    void OnSetEnabled() override;
    /// Return the depended on nodes to order network updates.
    void GetDependencyNodes(Vector<Node*>& dest) override;
    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

    /// Set constraint type and recreate the constraint.
    /// @property
    void SetConstraintType(ConstraintType type);
    /// Set other body to connect to. Set to null to connect to the static world.
    /// @property
    void SetOtherBody(RigidBody* body);
    /// Set constraint position relative to own body.
    /// @property
    void SetPosition(const Vector3& position);
    /// Set constraint rotation relative to own body.
    /// @property
    void SetRotation(const Quaternion& rotation);
    /// Set constraint rotation relative to own body by specifying the axis.
    /// @property
    void SetAxis(const Vector3& axis);
    /// Set constraint position relative to the other body. If connected to the static world, is a world space position.
    /// @property
    void SetOtherPosition(const Vector3& position);
    /// Set constraint rotation relative to the other body. If connected to the static world, is a world space rotation.
    /// @property
    void SetOtherRotation(const Quaternion& rotation);
    /// Set constraint rotation relative to the other body by specifying the axis.
    /// @property
    void SetOtherAxis(const Vector3& axis);
    /// Set constraint world space position. Resets both own and other body relative position, ie. zeroes the constraint error.
    /// @property
    void SetWorldPosition(const Vector3& position);
    /// Set high limit. Interpretation is constraint type specific.
    /// @property
    void SetHighLimit(const Vector2& limit);
    /// Set low limit. Interpretation is constraint type specific.
    /// @property
    void SetLowLimit(const Vector2& limit);
    /// Set constraint error reduction parameter. Zero = leave to default.
    /// @property{set_erp}
    void SetERP(float erp);
    /// Set constraint force mixing parameter. Zero = leave to default.
    /// @property{set_cfm}
    void SetCFM(float cfm);
    /// Set whether to disable collisions between connected bodies.
    /// @property
    void SetDisableCollision(bool disable);

    /// Set 6DOF linear lower limit (world units per axis).
    void SetLinearLowerLimit(const Vector3& limit);
    /// Set 6DOF linear upper limit (world units per axis).
    void SetLinearUpperLimit(const Vector3& limit);
    /// Set 6DOF angular lower limit (degrees per axis).
    void SetAngularLowerLimit(const Vector3& limit);
    /// Set 6DOF angular upper limit (degrees per axis).
    void SetAngularUpperLimit(const Vector3& limit);

    /// Enable/disable motor on axis (0-2 linear, 3-5 angular). 6DOF types only.
    void EnableMotor(int index, bool enable);
    /// Set motor target velocity on axis. 6DOF types only.
    void SetMotorTargetVelocity(int index, float velocity);
    /// Set motor max force on axis. 6DOF types only.
    void SetMotorMaxForce(int index, float force);

    /// Enable/disable spring on axis (0-2 linear, 3-5 angular). 6DOF Spring types only.
    void EnableSpring(int index, bool enable);
    /// Set spring stiffness on axis. 6DOF Spring types only.
    void SetSpringStiffness(int index, float stiffness);
    /// Set spring damping on axis. 6DOF Spring types only.
    void SetSpringDamping(int index, float damping);
    /// Reset spring equilibrium point on next ApplyLimits() call. Use after repositioning
    /// constrained bodies so the springs recalculate their rest position. Without this,
    /// the equilibrium is only set once (on constraint creation) to prevent per-frame motor
    /// updates from continuously resetting it.
    void ResetEquilibriumPoint() { equilibriumSet_ = false; ApplyLimits(); }

    /// Set gear ratio. Gear constraint only.
    void SetGearRatio(float ratio);
    /// Set gear axis on own body. Gear constraint only.
    void SetGearAxisA(const Vector3& axis);
    /// Set gear axis on other body. Gear constraint only.
    void SetGearAxisB(const Vector3& axis);

    /// Return physics world.
    PhysicsWorld* GetPhysicsWorld() const { return physicsWorld_; }

    /// Return Bullet constraint.
    btTypedConstraint* GetConstraint() const { return constraint_.get(); }

    /// Return constraint type.
    /// @property
    ConstraintType GetConstraintType() const { return constraintType_; }

    /// Return rigid body in own scene node.
    /// @property
    RigidBody* GetOwnBody() const { return ownBody_; }

    /// Return the other rigid body. May be null if connected to the static world.
    /// @property
    RigidBody* GetOtherBody() const { return otherBody_; }

    /// Return constraint position relative to own body.
    /// @property
    const Vector3& GetPosition() const { return position_; }

    /// Return constraint rotation relative to own body.
    /// @property
    const Quaternion& GetRotation() const { return rotation_; }

    /// Return constraint position relative to other body.
    /// @property
    const Vector3& GetOtherPosition() const { return otherPosition_; }

    /// Return constraint rotation relative to other body.
    /// @property
    const Quaternion& GetOtherRotation() const { return otherRotation_; }

    /// Return constraint world position, calculated from own body.
    /// @property
    Vector3 GetWorldPosition() const;

    /// Return high limit.
    /// @property
    const Vector2& GetHighLimit() const { return highLimit_; }

    /// Return low limit.
    /// @property
    const Vector2& GetLowLimit() const { return lowLimit_; }

    /// Return constraint error reduction parameter.
    /// @property{get_erp}
    float GetERP() const { return erp_; }

    /// Return constraint force mixing parameter.
    /// @property{get_cfm}
    float GetCFM() const { return cfm_; }

    /// Return whether collisions between connected bodies are disabled.
    /// @property
    bool GetDisableCollision() const { return disableCollision_; }

    /// Return 6DOF linear lower limit.
    const Vector3& GetLinearLowerLimit() const { return linearLowerLimit_; }
    /// Return 6DOF linear upper limit.
    const Vector3& GetLinearUpperLimit() const { return linearUpperLimit_; }
    /// Return 6DOF angular lower limit (degrees).
    const Vector3& GetAngularLowerLimit() const { return angularLowerLimit_; }
    /// Return 6DOF angular upper limit (degrees).
    const Vector3& GetAngularUpperLimit() const { return angularUpperLimit_; }
    /// Return gear ratio.
    float GetGearRatio() const { return gearRatio_; }
    /// Return gear axis A.
    const Vector3& GetGearAxisA() const { return gearAxisA_; }
    /// Return gear axis B.
    const Vector3& GetGearAxisB() const { return gearAxisB_; }

    /// Release the constraint.
    void ReleaseConstraint();
    /// Apply constraint frames.
    void ApplyFrames();

protected:
    /// Handle node being assigned.
    void OnNodeSet(Node* node) override;
    /// Handle scene being assigned.
    void OnSceneSet(Scene* scene) override;
    /// Handle node transform being dirtied.
    void OnMarkedDirty(Node* node) override;

private:
    /// Create the constraint.
    void CreateConstraint();
    /// Apply high and low constraint limits.
    void ApplyLimits();
    /// Adjust other body position.
    void AdjustOtherBodyPosition();
    /// Mark constraint dirty.
    void MarkConstraintDirty() { recreateConstraint_ = true; }
    /// Mark frames dirty.
    void MarkFramesDirty() { framesDirty_ = true; }

    /// Physics world.
    WeakPtr<PhysicsWorld> physicsWorld_;
    /// Own rigid body.
    WeakPtr<RigidBody> ownBody_;
    /// Other rigid body.
    WeakPtr<RigidBody> otherBody_;

    /// Bullet constraint.
    std::unique_ptr<btTypedConstraint> constraint_;

    /// Constraint type.
    ConstraintType constraintType_;
    /// Constraint position.
    Vector3 position_;
    /// Constraint rotation.
    Quaternion rotation_;
    /// Constraint other body position.
    Vector3 otherPosition_;
    /// Constraint other body axis.
    Quaternion otherRotation_;
    /// Cached world scale for determining if the constraint position needs update.
    Vector3 cachedWorldScale_;
    /// High limit.
    Vector2 highLimit_;
    /// Low limit.
    Vector2 lowLimit_;
    /// Error reduction parameter.
    float erp_;
    /// Constraint force mixing parameter.
    float cfm_;
    /// Other body node ID for pending constraint recreation.
    unsigned otherBodyNodeID_;
    /// Disable collision between connected bodies flag.
    bool disableCollision_;

    /// 6DOF per-axis linear limits.
    Vector3 linearLowerLimit_;
    Vector3 linearUpperLimit_;
    /// 6DOF per-axis angular limits (degrees).
    Vector3 angularLowerLimit_;
    Vector3 angularUpperLimit_;
    /// 6DOF motor state (0-2 linear XYZ, 3-5 angular XYZ).
    bool motorEnabled_[6]{};
    float motorTargetVelocity_[6]{};
    float motorMaxForce_[6]{};
    /// 6DOF spring state (0-2 linear XYZ, 3-5 angular XYZ).
    bool springEnabled_[6]{};
    float springStiffness_[6]{};
    float springDamping_[6]{};
    /// Gear constraint parameters.
    float gearRatio_;
    Vector3 gearAxisA_;
    Vector3 gearAxisB_;

    /// Recreate constraint flag.
    bool recreateConstraint_;
    /// Coordinate frames dirty flag.
    bool framesDirty_;
    /// Constraint creation retry flag if attributes initially set without scene.
    bool retryCreation_;
    /// Spring equilibrium point has been set.
    bool equilibriumSet_{false};
};

}
