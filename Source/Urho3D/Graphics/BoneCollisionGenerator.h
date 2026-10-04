// BoneCollisionGenerator — compute per-bone collision shapes from vertex weights.
// Step 1: OBB per bone via PCA on bone-local vertices.
// Step 2: greedy volume search — box → cylinder → capsule → hull.
//         Each tighter than the last. Stop when volume stops shrinking.

#pragma once

#include "../Math/BoundingBox.h"
#include "../Math/Matrix3.h"
#include "../Math/Quaternion.h"
#include "../Math/Vector3.h"
#include "../Container/Vector.h"

namespace Urho3D
{

class Model;
class Node;

/// Best-fit primitive shape type (ordered by expected tightness).
enum BoneShapeType
{
    BONE_SHAPE_BOX = 0,     // OBB — starting shape, always computed
    BONE_SHAPE_CYLINDER,    // tighter than box for limbs
    BONE_SHAPE_CAPSULE,     // tighter than cylinder for round-ended limbs
    BONE_SHAPE_SPHERE,      // tightest for roughly cubic bones (head)
    BONE_SHAPE_HULL         // convex hull — tightest possible
};

/// Per-bone collision shape result.
struct BoneBounds
{
    /// Bone index in the skeleton.
    int boneIndex{-1};

    /// AABB in bone-local space (kept for backward compatibility).
    BoundingBox box;

    /// OBB: center in bone-local space.
    Vector3 obbCenter;
    /// OBB: half-extents along principal axes (sorted: x >= y >= z).
    Vector3 obbHalfExtents;
    /// OBB: orientation — rotation from bone-local to OBB-local space.
    Quaternion obbRotation;

    /// Number of vertices assigned to this bone.
    unsigned vertexCount{0};

    /// Best-fit primitive shape type (result of greedy volume search).
    BoneShapeType shapeType{BONE_SHAPE_BOX};
    /// Primary axis index in OBB space (0=X, always longest after sorting).
    int primaryAxis{0};
    /// Best-fit capsule/cylinder: half-length along primary axis.
    float halfLength{0.0f};
    /// Best-fit capsule/sphere/cylinder: radius.
    float radius{0.0f};

    /// Volume of the best-fit shape. Used by greedy search.
    float volume{0.0f};
    /// Volume of the OBB (for comparison / savings logging).
    float obbVolume{0.0f};

    /// Vertex positions in bone-local space (for hull creation).
    Vector<Vector3> vertices;
};

/// Generate per-bone collision shapes from a skinned model's vertex weights.
class URHO3D_API BoneCollisionGenerator
{
public:
    /// Compute per-bone bounding boxes and best-fit primitives.
    static Vector<BoneBounds> Generate(Model* model);

    /// Create RigidBody + CollisionShape on each bone node from pre-computed bounds.
    /// Bodies are kinematic by default (animation-driven). Pass kinematic=false for
    /// full ragdoll (dynamic bodies driven by physics).
    /// Returns the number of bodies created.
    static unsigned CreateBodies(Node* modelNode, const Vector<BoneBounds>& bounds, bool kinematic = true);

    /// Create 6DOF_SPRING2 constraints between parent-child bone pairs.
    /// Spring motors target the bind pose — physics pushes back, animation drives.
    /// Must be called after CreateBodies. Returns the number of constraints created.
    static unsigned CreateConstraints(Node* modelNode, const Vector<BoneBounds>& bounds,
        float stiffness = 100.0f, float damping = 5.0f);
};

} // namespace Urho3D
