// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#ifdef URHO3D_PHYSICS

#include "../AngelScript/Manual_Physics.h"

#include "../Graphics/Model.h"
#include "../Physics/CollisionShape.h"
#include "../Physics/MultiBody.h"
#include "../Physics/SoftBody.h"
#include "../Scene/Scene.h"

namespace Urho3D
{

// SoftBody factory function
static SoftBody* SoftBody_Factory()
{
    Context* context = GetScriptContext();
    return new SoftBody(context);
}

// MultiBody factory function
static MultiBody* MultiBody_Factory()
{
    Context* context = GetScriptContext();
    return new MultiBody(context);
}

// This function is called before ASRegisterGenerated()
void ASRegisterManualFirst_Physics(asIScriptEngine* engine)
{
    // Register SoftBody type early so it's available for cross-references
    engine->RegisterObjectType("SoftBody", 0, asOBJ_REF);

    // Register MultiBody type early
    engine->RegisterObjectType("MultiBody", 0, asOBJ_REF);

    // Register SoftBodyType enum
    engine->RegisterEnum("SoftBodyType");
    engine->RegisterEnumValue("SoftBodyType", "SOFTBODY_ROPE", SOFTBODY_ROPE);
    engine->RegisterEnumValue("SoftBodyType", "SOFTBODY_PATCH", SOFTBODY_PATCH);
    engine->RegisterEnumValue("SoftBodyType", "SOFTBODY_ELLIPSOID", SOFTBODY_ELLIPSOID);
    engine->RegisterEnumValue("SoftBodyType", "SOFTBODY_TRIMESH", SOFTBODY_TRIMESH);
    engine->RegisterEnumValue("SoftBodyType", "SOFTBODY_CONVEXHULL", SOFTBODY_CONVEXHULL);
}

// ========================================================================================

// template <class T> T* Node::GetComponent(bool recursive = false) const | File: ../Scene/Node.h
static PhysicsWorld* GetPhysicsWorld()
{
    Scene* scene = GetScriptContextScene();
    return scene ? scene->GetComponent<PhysicsWorld>() : nullptr;
}

// Register SoftBody class members and behaviors
static void Register_SoftBody(asIScriptEngine* engine)
{
    // Factory
    engine->RegisterObjectBehaviour("SoftBody", asBEHAVE_FACTORY, "SoftBody@+ f()", AS_FUNCTION(SoftBody_Factory), AS_CALL_CDECL);

    // Inheritance chain
    RegisterSubclass<Drawable, SoftBody>(engine, "Drawable", "SoftBody");
    RegisterSubclass<Component, SoftBody>(engine, "Component", "SoftBody");
    RegisterSubclass<Animatable, SoftBody>(engine, "Animatable", "SoftBody");
    RegisterSubclass<Serializable, SoftBody>(engine, "Serializable", "SoftBody");
    RegisterSubclass<Object, SoftBody>(engine, "Object", "SoftBody");
    RegisterSubclass<RefCounted, SoftBody>(engine, "RefCounted", "SoftBody");

    // Creation methods
    engine->RegisterObjectMethod("SoftBody", "void CreateRope(const Vector3&in, const Vector3&in, int, int)",
        AS_METHODPR(SoftBody, CreateRope, (const Vector3&, const Vector3&, int, int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void CreatePatch(const Vector3&in, const Vector3&in, const Vector3&in, const Vector3&in, int, int, int, bool)",
        AS_METHODPR(SoftBody, CreatePatch, (const Vector3&, const Vector3&, const Vector3&, const Vector3&, int, int, int, bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void CreateEllipsoid(const Vector3&in, const Vector3&in, int)",
        AS_METHODPR(SoftBody, CreateEllipsoid, (const Vector3&, const Vector3&, int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void CreateFromModel(Model@+, int = 0)",
        AS_METHODPR(SoftBody, CreateFromModel, (Model*, int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void CreateConvexHull(Model@+, int = 0)",
        AS_METHODPR(SoftBody, CreateConvexHull, (Model*, int), void), AS_CALL_THISCALL);

    // Configuration properties
    engine->RegisterObjectMethod("SoftBody", "void set_linearStiffness(float)", AS_METHODPR(SoftBody, SetLinearStiffness, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_linearStiffness() const", AS_METHODPR(SoftBody, GetLinearStiffness, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_angularStiffness(float)", AS_METHODPR(SoftBody, SetAngularStiffness, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_angularStiffness() const", AS_METHODPR(SoftBody, GetAngularStiffness, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_volumeStiffness(float)", AS_METHODPR(SoftBody, SetVolumeStiffness, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_volumeStiffness() const", AS_METHODPR(SoftBody, GetVolumeStiffness, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_damping(float)", AS_METHODPR(SoftBody, SetDamping, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_damping() const", AS_METHODPR(SoftBody, GetDamping, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_pressure(float)", AS_METHODPR(SoftBody, SetPressure, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_pressure() const", AS_METHODPR(SoftBody, GetPressure, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_friction(float)", AS_METHODPR(SoftBody, SetFriction, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_friction() const", AS_METHODPR(SoftBody, GetFriction, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_positionIterations(int)", AS_METHODPR(SoftBody, SetPositionIterations, (int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "int get_positionIterations() const", AS_METHODPR(SoftBody, GetPositionIterations, () const, int), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void SetTotalMass(float, bool = false)", AS_METHODPR(SoftBody, SetTotalMass, (float, bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_totalMass(float)", AS_METHODPR(SoftBody, SetTotalMass, (float, bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_totalMass() const", AS_METHODPR(SoftBody, GetTotalMass, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_collisionMargin(float)", AS_METHODPR(SoftBody, SetCollisionMargin, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "float get_collisionMargin() const", AS_METHODPR(SoftBody, GetCollisionMargin, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_collisionLayer(uint)", AS_METHODPR(SoftBody, SetCollisionLayer, (unsigned), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "uint get_collisionLayer() const", AS_METHODPR(SoftBody, GetCollisionLayer, () const, unsigned), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_collisionMask(uint)", AS_METHODPR(SoftBody, SetCollisionMask, (unsigned), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "uint get_collisionMask() const", AS_METHODPR(SoftBody, GetCollisionMask, () const, unsigned), AS_CALL_THISCALL);

    // Anchor
    engine->RegisterObjectMethod("SoftBody", "void AppendAnchor(int, RigidBody@+, bool = false, float = 1.0f)",
        AS_METHODPR(SoftBody, AppendAnchor, (int, RigidBody*, bool, float), void), AS_CALL_THISCALL);

    // Node accessors
    engine->RegisterObjectMethod("SoftBody", "int get_numNodes() const", AS_METHODPR(SoftBody, GetNumNodes, () const, int), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "Vector3 GetNodePosition(int) const", AS_METHODPR(SoftBody, GetNodePosition, (int) const, Vector3), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "Vector3 GetNodeVelocity(int) const", AS_METHODPR(SoftBody, GetNodeVelocity, (int) const, Vector3), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "SoftBodyType get_softBodyType() const", AS_METHODPR(SoftBody, GetSoftBodyType, () const, SoftBodyType), AS_CALL_THISCALL);

    // Force/Impulse
    engine->RegisterObjectMethod("SoftBody", "void AddForce(const Vector3&in)", AS_METHODPR(SoftBody, AddForce, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void AddNodeForce(const Vector3&in, int)", AS_METHODPR(SoftBody, AddNodeForce, (const Vector3&, int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void SetVelocity(const Vector3&in)", AS_METHODPR(SoftBody, SetVelocity, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "void set_windVelocity(const Vector3&in)", AS_METHODPR(SoftBody, SetWindVelocity, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "Vector3 get_windVelocity() const", AS_METHODPR(SoftBody, GetWindVelocity, () const, Vector3), AS_CALL_THISCALL);

    // Material
    engine->RegisterObjectMethod("SoftBody", "void set_material(Material@+)", AS_METHODPR(SoftBody, SetMaterial, (Material*), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("SoftBody", "Material@+ get_material() const", AS_METHODPR(SoftBody, GetMaterial, () const, Material*), AS_CALL_THISCALL);
}

// Register MultiBody class members and behaviors
static void Register_MultiBody(asIScriptEngine* engine)
{
    // Factory
    engine->RegisterObjectBehaviour("MultiBody", asBEHAVE_FACTORY, "MultiBody@+ f()", AS_FUNCTION(MultiBody_Factory), AS_CALL_CDECL);

    // Inheritance chain
    RegisterSubclass<Component, MultiBody>(engine, "Component", "MultiBody");
    RegisterSubclass<Animatable, MultiBody>(engine, "Animatable", "MultiBody");
    RegisterSubclass<Serializable, MultiBody>(engine, "Serializable", "MultiBody");
    RegisterSubclass<Object, MultiBody>(engine, "Object", "MultiBody");
    RegisterSubclass<RefCounted, MultiBody>(engine, "RefCounted", "MultiBody");

    // Setup
    engine->RegisterObjectMethod("MultiBody", "void SetNumLinks(int)", AS_METHODPR(MultiBody, SetNumLinks, (int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetBaseMass(float)", AS_METHODPR(MultiBody, SetBaseMass, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetBaseInertia(const Vector3&in)", AS_METHODPR(MultiBody, SetBaseInertia, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetFixedBase(bool)", AS_METHODPR(MultiBody, SetFixedBase, (bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetCanSleep(bool)", AS_METHODPR(MultiBody, SetCanSleep, (bool), void), AS_CALL_THISCALL);

    // Link setup
    engine->RegisterObjectMethod("MultiBody", "void SetupRevolute(int, float, const Vector3&in, int, const Quaternion&in, const Vector3&in, const Vector3&in, const Vector3&in, bool = true)",
        AS_METHODPR(MultiBody, SetupRevolute, (int, float, const Vector3&, int, const Quaternion&, const Vector3&, const Vector3&, const Vector3&, bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetupSpherical(int, float, const Vector3&in, int, const Quaternion&in, const Vector3&in, const Vector3&in, bool = true)",
        AS_METHODPR(MultiBody, SetupSpherical, (int, float, const Vector3&, int, const Quaternion&, const Vector3&, const Vector3&, bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetupPrismatic(int, float, const Vector3&in, int, const Quaternion&in, const Vector3&in, const Vector3&in, const Vector3&in, bool = true)",
        AS_METHODPR(MultiBody, SetupPrismatic, (int, float, const Vector3&, int, const Quaternion&, const Vector3&, const Vector3&, const Vector3&, bool), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetupFixed(int, float, const Vector3&in, int, const Quaternion&in, const Vector3&in, const Vector3&in)",
        AS_METHODPR(MultiBody, SetupFixed, (int, float, const Vector3&, int, const Quaternion&, const Vector3&, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetupPlanar(int, float, const Vector3&in, int, const Quaternion&in, const Vector3&in, const Vector3&in, bool = true)",
        AS_METHODPR(MultiBody, SetupPlanar, (int, float, const Vector3&, int, const Quaternion&, const Vector3&, const Vector3&, bool), void), AS_CALL_THISCALL);

    // Collision shapes
    engine->RegisterObjectMethod("MultiBody", "void SetLinkBoxShape(int, const Vector3&in)", AS_METHODPR(MultiBody, SetLinkBoxShape, (int, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetLinkSphereShape(int, float)", AS_METHODPR(MultiBody, SetLinkSphereShape, (int, float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetLinkCapsuleShape(int, float, float)", AS_METHODPR(MultiBody, SetLinkCapsuleShape, (int, float, float), void), AS_CALL_THISCALL);

    // Visual node mapping
    engine->RegisterObjectMethod("MultiBody", "void SetLinkNode(int, Node@+)", AS_METHODPR(MultiBody, SetLinkNode, (int, Node*), void), AS_CALL_THISCALL);

    // Build
    engine->RegisterObjectMethod("MultiBody", "void Build()", AS_METHODPR(MultiBody, Build, (), void), AS_CALL_THISCALL);

    // Runtime control
    engine->RegisterObjectMethod("MultiBody", "void SetJointPos(int, float)", AS_METHODPR(MultiBody, SetJointPos, (int, float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetJointVel(int, float)", AS_METHODPR(MultiBody, SetJointVel, (int, float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void AddJointTorque(int, float)", AS_METHODPR(MultiBody, AddJointTorque, (int, float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetBaseVel(const Vector3&in)", AS_METHODPR(MultiBody, SetBaseVel, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void SetBaseOmega(const Vector3&in)", AS_METHODPR(MultiBody, SetBaseOmega, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void ApplyBaseForce(const Vector3&in)", AS_METHODPR(MultiBody, ApplyBaseForce, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void ApplyBaseTorque(const Vector3&in)", AS_METHODPR(MultiBody, ApplyBaseTorque, (const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void ApplyLinkForce(int, const Vector3&in)", AS_METHODPR(MultiBody, ApplyLinkForce, (int, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "void ApplyLinkTorque(int, const Vector3&in)", AS_METHODPR(MultiBody, ApplyLinkTorque, (int, const Vector3&), void), AS_CALL_THISCALL);

    // Query
    engine->RegisterObjectMethod("MultiBody", "float GetJointPos(int) const", AS_METHODPR(MultiBody, GetJointPos, (int) const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "float GetJointVel(int) const", AS_METHODPR(MultiBody, GetJointVel, (int) const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "int get_numLinks() const", AS_METHODPR(MultiBody, GetNumLinks, () const, int), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("MultiBody", "bool get_isBuilt() const", AS_METHODPR(MultiBody, IsBuilt, () const, bool), AS_CALL_THISCALL);
}

// This function is called after ASRegisterGenerated()
void ASRegisterManualLast_Physics(asIScriptEngine* engine)
{
    // template <class T> T* Node::GetComponent(bool recursive = false) const | File: ../Scene/Node.h
    engine->RegisterGlobalFunction("PhysicsWorld@+ get_physicsWorld()", AS_FUNCTION(GetPhysicsWorld), AS_CALL_CDECL);

    // Register SoftBody class (after Drawable and other base classes are fully registered)
    Register_SoftBody(engine);

    // Register MultiBody class
    Register_MultiBody(engine);

    // === RigidBody shape API ===
    engine->RegisterObjectMethod("RigidBody", "void SetBoxShape(const Vector3&in, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(RigidBody, SetBoxShape, (const Vector3&, const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetSphereShape(float, const Vector3&in = Vector3(0,0,0))",
        AS_METHODPR(RigidBody, SetSphereShape, (float, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetCapsuleShape(float, float, const Vector3&in = Vector3(0,0,0))",
        AS_METHODPR(RigidBody, SetCapsuleShape, (float, float, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetCylinderShape(float, float, const Vector3&in = Vector3(0,0,0))",
        AS_METHODPR(RigidBody, SetCylinderShape, (float, float, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetConeShape(float, float, const Vector3&in = Vector3(0,0,0))",
        AS_METHODPR(RigidBody, SetConeShape, (float, float, const Vector3&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetStaticPlaneShape()",
        AS_METHODPR(RigidBody, SetStaticPlaneShape, (), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetTriMeshShape(Model@+, int = 0)",
        AS_METHODPR(RigidBody, SetTriMeshShape, (Model*, int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void SetConvexHullShape(Model@+, int = 0)",
        AS_METHODPR(RigidBody, SetConvexHullShape, (Model*, int), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void AddChildShape(ShapeType, const Vector3&in, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion(), float = 0.04)",
        AS_METHODPR(RigidBody, AddChildShape, (ShapeType, const Vector3&, const Vector3&, const Quaternion&, float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void ClearChildShapes()",
        AS_METHODPR(RigidBody, ClearChildShapes, (), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void set_shapeMargin(float)",
        AS_METHODPR(RigidBody, SetShapeMargin, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "float get_shapeMargin() const",
        AS_METHODPR(RigidBody, GetShapeMargin, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "int get_shapeType() const",
        AS_METHODPR(RigidBody, GetShapeType, () const, int), AS_CALL_THISCALL);

    // Spinning friction
    engine->RegisterObjectMethod("RigidBody", "void SetSpinningFriction(float)",
        AS_METHODPR(RigidBody, SetSpinningFriction, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "void set_spinningFriction(float)",
        AS_METHODPR(RigidBody, SetSpinningFriction, (float), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "float GetSpinningFriction() const",
        AS_METHODPR(RigidBody, GetSpinningFriction, () const, float), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("RigidBody", "float get_spinningFriction() const",
        AS_METHODPR(RigidBody, GetSpinningFriction, () const, float), AS_CALL_THISCALL);

    // Soft contact stiffness and damping
    engine->RegisterObjectMethod("RigidBody", "void SetContactStiffnessAndDamping(float, float)",
        AS_METHODPR(RigidBody, SetContactStiffnessAndDamping, (float, float), void), AS_CALL_THISCALL);

    // === CollisionShape API (default args can't auto-bind) ===
    engine->RegisterObjectMethod("CollisionShape", "void SetBox(const Vector3&in, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(CollisionShape, SetBox, (const Vector3&, const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("CollisionShape", "void SetSphere(float, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(CollisionShape, SetSphere, (float, const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("CollisionShape", "void SetCapsule(float, float, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(CollisionShape, SetCapsule, (float, float, const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("CollisionShape", "void SetCylinder(float, float, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(CollisionShape, SetCylinder, (float, float, const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("CollisionShape", "void SetCone(float, float, const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(CollisionShape, SetCone, (float, float, const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
    engine->RegisterObjectMethod("CollisionShape", "void SetStaticPlane(const Vector3&in = Vector3(0,0,0), const Quaternion&in = Quaternion())",
        AS_METHODPR(CollisionShape, SetStaticPlane, (const Vector3&, const Quaternion&), void), AS_CALL_THISCALL);
}

}

#endif // def URHO3D_PHYSICS
