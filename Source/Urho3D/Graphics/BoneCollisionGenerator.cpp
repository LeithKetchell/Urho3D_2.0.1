// BoneCollisionGenerator — compute per-bone OBBs and best-fit primitives from vertex weights.
// OBB via PCA (Bullet's Jacobi eigendecomposition on covariance matrix).
// Greedy volume search: box → cylinder → capsule → sphere. Stop when volume stops shrinking.

#include "../Precompiled.h"

#include "BoneCollisionGenerator.h"
#include "AnimatedModel.h"
#include "Model.h"
#include "Skeleton.h"
#include "../GraphicsAPI/VertexBuffer.h"
#include "../GraphicsAPI/GraphicsDefs.h"
#include "../IO/Log.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/RigidBody.h"
#include "../Physics/CollisionShape.h"
#include "../Physics/Constraint.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"

#include <Bullet/LinearMath/btMatrix3x3.h>

namespace Urho3D
{

Vector<BoneBounds> BoneCollisionGenerator::Generate(Model* model)
{
    Vector<BoneBounds> result;

    if (!model)
        return result;

    Skeleton& skeleton = model->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();
    unsigned numBones = bones.Size();

    if (numBones == 0)
        return result;

    // Initialize per-bone bounds
    result.Resize(numBones);
    for (unsigned i = 0; i < numBones; ++i)
    {
        result[i].boneIndex = (int)i;
        result[i].vertexCount = 0;
    }

    // Step 1: collect per-bone vertex positions in bone-local space + build AABBs
    Vector<bool> seeded(numBones, false);

    const Vector<SharedPtr<VertexBuffer>>& vertexBuffers = model->GetVertexBuffers();

    for (unsigned vbIdx = 0; vbIdx < vertexBuffers.Size(); ++vbIdx)
    {
        VertexBuffer* vb = vertexBuffers[vbIdx];
        if (!vb)
            continue;

        const byte* data = vb->GetShadowData();
        if (!data)
        {
            URHO3D_LOGWARNING("BoneCollisionGenerator: No shadow data in vertex buffer " + String(vbIdx));
            continue;
        }

        unsigned vertexCount = vb->GetVertexCount();
        unsigned vertexSize = vb->GetVertexSize();

        const VertexElement* posElem = vb->GetElement(SEM_POSITION);
        const VertexElement* weightElem = vb->GetElement(SEM_BLENDWEIGHTS);
        const VertexElement* indexElem = vb->GetElement(SEM_BLENDINDICES);

        if (!posElem || !weightElem || !indexElem)
            continue;

        unsigned posOffset = posElem->offset_;
        unsigned weightOffset = weightElem->offset_;
        unsigned indexOffset = indexElem->offset_;

        for (unsigned v = 0; v < vertexCount; ++v)
        {
            const byte* vertex = data + v * vertexSize;
            const Vector3& pos = *reinterpret_cast<const Vector3*>(vertex + posOffset);
            const float* weights = reinterpret_cast<const float*>(vertex + weightOffset);
            const unsigned char* indices = reinterpret_cast<const unsigned char*>(vertex + indexOffset);

            float maxWeight = 0.0f;
            unsigned bestBone = 0;
            for (unsigned w = 0; w < 4; ++w)
            {
                if (weights[w] > maxWeight)
                {
                    maxWeight = weights[w];
                    bestBone = indices[w];
                }
            }

            if (bestBone >= numBones)
                continue;

            Vector3 localPos = bones[bestBone].offsetMatrix_ * pos;

            if (!seeded[bestBone])
            {
                result[bestBone].box.Define(localPos);
                seeded[bestBone] = true;
            }
            else
            {
                result[bestBone].box.Merge(localPos);
            }
            result[bestBone].vertexCount++;
            result[bestBone].vertices.Push(localPos);
        }
    }

    // Step 2: compute OBB via PCA, then greedy volume search for best-fit primitive.
    static const char* shapeNames[] = {"box", "cylinder", "capsule", "sphere", "hull"};
    unsigned activeBones = 0;

    for (unsigned i = 0; i < numBones; ++i)
    {
        if (result[i].vertexCount < 3)
            continue;
        activeBones++;

        const Vector<Vector3>& verts = result[i].vertices;

        // ── OBB via PCA ──
        // Centroid
        Vector3 centroid = Vector3::ZERO;
        for (unsigned v = 0; v < verts.Size(); ++v)
            centroid += verts[v];
        centroid /= (float)verts.Size();

        // Covariance matrix (3x3 symmetric)
        btMatrix3x3 cov(0, 0, 0, 0, 0, 0, 0, 0, 0);
        for (unsigned v = 0; v < verts.Size(); ++v)
        {
            Vector3 rel = verts[v] - centroid;
            cov[0][0] += rel.x_ * rel.x_;
            cov[0][1] += rel.x_ * rel.y_;
            cov[0][2] += rel.x_ * rel.z_;
            cov[1][1] += rel.y_ * rel.y_;
            cov[1][2] += rel.y_ * rel.z_;
            cov[2][2] += rel.z_ * rel.z_;
        }
        float invN = 1.0f / (float)verts.Size();
        cov[0][0] *= invN; cov[0][1] *= invN; cov[0][2] *= invN;
        cov[1][1] *= invN; cov[1][2] *= invN; cov[2][2] *= invN;
        cov[1][0] = cov[0][1];
        cov[2][0] = cov[0][2];
        cov[2][1] = cov[1][2];

        // Jacobi eigendecomposition — Bullet gives us the rotation matrix
        btMatrix3x3 rot;
        cov.diagonalize(rot, 0.00001f, 50);

        // Principal axes from rotation matrix columns
        Vector3 axes[3];
        axes[0] = Vector3(rot[0][0], rot[1][0], rot[2][0]).Normalized();
        axes[1] = Vector3(rot[0][1], rot[1][1], rot[2][1]).Normalized();
        axes[2] = Vector3(rot[0][2], rot[1][2], rot[2][2]).Normalized();

        // Project vertices onto principal axes to find OBB extents
        Vector3 obbMin(M_INFINITY, M_INFINITY, M_INFINITY);
        Vector3 obbMax(-M_INFINITY, -M_INFINITY, -M_INFINITY);
        for (unsigned v = 0; v < verts.Size(); ++v)
        {
            Vector3 rel = verts[v] - centroid;
            float px = rel.DotProduct(axes[0]);
            float py = rel.DotProduct(axes[1]);
            float pz = rel.DotProduct(axes[2]);
            obbMin.x_ = Min(obbMin.x_, px); obbMax.x_ = Max(obbMax.x_, px);
            obbMin.y_ = Min(obbMin.y_, py); obbMax.y_ = Max(obbMax.y_, py);
            obbMin.z_ = Min(obbMin.z_, pz); obbMax.z_ = Max(obbMax.z_, pz);
        }

        Vector3 halfExtents((obbMax.x_ - obbMin.x_) * 0.5f,
                            (obbMax.y_ - obbMin.y_) * 0.5f,
                            (obbMax.z_ - obbMin.z_) * 0.5f);

        // Adjust center to midpoint of projected extents
        Vector3 obbCenterOffset = axes[0] * ((obbMax.x_ + obbMin.x_) * 0.5f)
                                + axes[1] * ((obbMax.y_ + obbMin.y_) * 0.5f)
                                + axes[2] * ((obbMax.z_ + obbMin.z_) * 0.5f);
        Vector3 obbCenter = centroid + obbCenterOffset;

        // Sort axes so halfExtents.x >= y >= z (primary axis = longest)
        float he[3] = {halfExtents.x_, halfExtents.y_, halfExtents.z_};
        int axisOrder[3] = {0, 1, 2};
        for (int a = 0; a < 2; ++a)
            for (int b = a + 1; b < 3; ++b)
                if (he[b] > he[a])
                {
                    Swap(he[a], he[b]);
                    Swap(axisOrder[a], axisOrder[b]);
                }

        Vector3 sortedAxes[3] = {axes[axisOrder[0]], axes[axisOrder[1]], axes[axisOrder[2]]};
        Vector3 sortedHE(he[0], he[1], he[2]);

        // Build OBB rotation quaternion from sorted axes
        Matrix3 obbMat(sortedAxes[0].x_, sortedAxes[1].x_, sortedAxes[2].x_,
                       sortedAxes[0].y_, sortedAxes[1].y_, sortedAxes[2].y_,
                       sortedAxes[0].z_, sortedAxes[1].z_, sortedAxes[2].z_);
        Quaternion obbRot(obbMat);

        result[i].obbCenter = obbCenter;
        result[i].obbHalfExtents = sortedHE;
        result[i].obbRotation = obbRot;
        result[i].primaryAxis = 0;  // always X after sorting

        float volOBB = 8.0f * sortedHE.x_ * sortedHE.y_ * sortedHE.z_;
        result[i].obbVolume = volOBB;

        // ── Greedy volume search: box → cylinder → capsule → sphere ──
        // Stop when volume stops shrinking.

        // Stage 2: greedy volume search — try tighter primitives, stop when
        // volume stops shrinking. Each candidate uses the OBB primary axis.
        float bestVol = volOBB;
        result[i].shapeType = BONE_SHAPE_BOX;
        result[i].halfLength = sortedHE.x_;
        result[i].radius = 0.0f;
        result[i].volume = volOBB;

        // Cylinder along OBB primary axis, radius = max of two shorter half-extents
        float cylR = Max(sortedHE.y_, sortedHE.z_);
        float cylH = sortedHE.x_ * 2.0f;
        float volCyl = M_PI * cylR * cylR * cylH;
        if (volCyl < bestVol)
        {
            bestVol = volCyl;
            result[i].shapeType = BONE_SHAPE_CYLINDER;
            result[i].halfLength = sortedHE.x_;
            result[i].radius = cylR;
            result[i].volume = volCyl;

            // Capsule: tighter than cylinder for round-ended bones.
            // Radius from max perpendicular distance to OBB primary axis.
            float capsR = 0.0f;
            float capsAxMin = 0.0f, capsAxMax = 0.0f;
            for (unsigned v = 0; v < verts.Size(); ++v)
            {
                Vector3 rel = verts[v] - obbCenter;
                float axProj = rel.DotProduct(sortedAxes[0]);
                Vector3 perpVec = rel - sortedAxes[0] * axProj;
                float perpDist = perpVec.Length();
                if (perpDist > capsR) capsR = perpDist;
                if (v == 0 || axProj < capsAxMin) capsAxMin = axProj;
                if (v == 0 || axProj > capsAxMax) capsAxMax = axProj;
            }
            float capsSpan = capsAxMax - capsAxMin;
            float capsCylLen = Max(capsSpan - capsR * 2.0f, 0.0f);
            float volCaps = M_PI * capsR * capsR * capsCylLen
                          + (4.0f / 3.0f) * M_PI * capsR * capsR * capsR;
            if (volCaps < bestVol)
            {
                bestVol = volCaps;
                result[i].shapeType = BONE_SHAPE_CAPSULE;
                result[i].halfLength = capsSpan * 0.5f;
                result[i].radius = capsR;
                result[i].volume = volCaps;

                // Sphere: tightest for roughly cubic bones.
                float maxDistSq = 0.0f;
                for (unsigned v = 0; v < verts.Size(); ++v)
                {
                    float dSq = (verts[v] - obbCenter).LengthSquared();
                    if (dSq > maxDistSq) maxDistSq = dSq;
                }
                float sphR = sqrtf(maxDistSq);
                float volSph = (4.0f / 3.0f) * M_PI * sphR * sphR * sphR;
                if (volSph < bestVol)
                {
                    bestVol = volSph;
                    result[i].shapeType = BONE_SHAPE_SPHERE;
                    result[i].halfLength = 0.0f;
                    result[i].radius = sphR;
                    result[i].volume = volSph;
                }
            }
        }

        float saved = (volOBB > 0.001f) ? (1.0f - bestVol / volOBB) * 100.0f : 0.0f;

        URHO3D_LOGINFOF("  Bone[%u] '%s': %u verts, OBB=(%.3f,%.3f,%.3f) → %s  vol-saved=%.0f%%",
            i, bones[i].name_.CString(), result[i].vertexCount,
            sortedHE.x_ * 2.0f, sortedHE.y_ * 2.0f, sortedHE.z_ * 2.0f,
            shapeNames[result[i].shapeType], saved);
    }

    URHO3D_LOGINFOF("BoneCollisionGenerator: %u/%u bones have vertices", activeBones, numBones);

    return result;
}

unsigned BoneCollisionGenerator::CreateBodies(Node* modelNode, const Vector<BoneBounds>& bounds, bool kinematic)
{
    if (!modelNode)
        return 0;

    auto* animModel = modelNode->GetComponent<AnimatedModel>(true);
    if (!animModel)
    {
        URHO3D_LOGWARNING("BoneCollisionGenerator::CreateBodies: No AnimatedModel found");
        return 0;
    }

    Skeleton& skeleton = animModel->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();
    unsigned created = 0;

    for (unsigned i = 0; i < bounds.Size() && i < bones.Size(); ++i)
    {
        const BoneBounds& bb = bounds[i];
        if (bb.vertexCount == 0)
            continue;

        Node* boneNode = bones[i].node_;
        if (!boneNode)
            continue;

        // Skip if body already exists on this bone
        if (boneNode->GetComponent<RigidBody>())
            continue;

        // Derive mass from the best-fit volume (already computed in Generate)
        float volume = bb.volume > 0.0f ? bb.volume : bb.obbVolume;
        // Density ~1000 kg/m³ (water/tissue), clamped to reasonable range
        // Mass must be > 0 even for kinematic — mass 0 is static in Bullet
        float mass = Clamp(volume * 1000.0f, 0.1f, 20.0f);

        auto* body = boneNode->CreateComponent<RigidBody>();
        body->SetMass(mass);
        body->SetKinematic(kinematic);
        body->SetFriction(0.5f);
        body->SetRestitution(0.1f);

        auto* shape = boneNode->CreateComponent<CollisionShape>();

        // Shape is in bone-local space: position offset from bone pivot (joint)
        // to shape center, rotation orients the shape along the bone's vertex cloud.
        // Bone node transform handles world positioning — animation drives it.
        // Same pattern as the hardcoded ragdoll in Sample 13.
        Vector3 position = bb.obbCenter;
        Quaternion rotation = bb.obbRotation;

        switch (bb.shapeType)
        {
        case BONE_SHAPE_SPHERE:
            shape->SetSphere(bb.radius * 2.0f, position);
            break;

        case BONE_SHAPE_CYLINDER:
            shape->SetCylinder(bb.radius * 2.0f, bb.halfLength * 2.0f, position, rotation);
            break;

        case BONE_SHAPE_CAPSULE:
            shape->SetCapsule(bb.radius * 2.0f, bb.halfLength * 2.0f, position, rotation);
            break;

        case BONE_SHAPE_BOX:
            shape->SetBox(bb.obbHalfExtents * 2.0f, position, rotation);
            break;

        case BONE_SHAPE_HULL:
            if (!bb.vertices.Empty())
                shape->SetConvexHull(bb.vertices);
            else
                shape->SetBox(bb.obbHalfExtents * 2.0f, position, rotation);
            break;
        }

        ++created;
    }

    // Register for pre-physics animation sync so bone transforms are current
    // when Bullet reads kinematic body positions.
    if (created > 0 && kinematic)
    {
        Scene* scene = modelNode->GetScene();
        auto* physWorld = scene ? scene->GetComponent<PhysicsWorld>() : nullptr;
        if (physWorld)
            physWorld->AddPrePhysicsAnimModel(animModel);
    }

    URHO3D_LOGINFOF("BoneCollisionGenerator::CreateBodies: %u bodies created (%s)",
        created, kinematic ? "kinematic" : "dynamic");

    return created;
}

unsigned BoneCollisionGenerator::CreateConstraints(Node* modelNode, const Vector<BoneBounds>& bounds,
    float stiffness, float damping)
{
    if (!modelNode)
        return 0;

    auto* animModel = modelNode->GetComponent<AnimatedModel>(true);
    if (!animModel)
    {
        URHO3D_LOGWARNING("BoneCollisionGenerator::CreateConstraints: No AnimatedModel found");
        return 0;
    }

    Skeleton& skeleton = animModel->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();
    unsigned created = 0;

    for (unsigned i = 0; i < bounds.Size() && i < bones.Size(); ++i)
    {
        if (bounds[i].vertexCount == 0)
            continue;

        Node* childNode = bones[i].node_;
        if (!childNode)
            continue;

        auto* childBody = childNode->GetComponent<RigidBody>();
        if (!childBody)
            continue;

        // Find parent bone with a RigidBody
        int parentIdx = bones[i].parentIndex_;
        if (parentIdx < 0 || parentIdx >= (int)bones.Size())
            continue;

        Node* parentNode = bones[parentIdx].node_;
        if (!parentNode)
            continue;

        auto* parentBody = parentNode->GetComponent<RigidBody>();
        if (!parentBody)
            continue;

        // Skip if constraint already exists
        if (childNode->GetComponent<Constraint>())
            continue;

        auto* constraint = childNode->CreateComponent<Constraint>();
        constraint->SetConstraintType(CONSTRAINT_6DOF_SPRING2);
        constraint->SetOtherBody(parentBody);
        constraint->SetDisableCollision(true);

        // Anchor: on the child body, the joint is at the bone's origin (0,0,0).
        // On the parent body, the joint is at the child's bind-pose position
        // relative to the parent (i.e., the child bone node's local position).
        constraint->SetPosition(Vector3::ZERO);
        constraint->SetOtherPosition(childNode->GetPosition());

        // Lock linear axes — bones don't slide apart
        constraint->SetLinearLowerLimit(Vector3::ZERO);
        constraint->SetLinearUpperLimit(Vector3::ZERO);

        // Angular limits — reasonable anatomical range (degrees)
        // Loose defaults; per-bone tuning is a future step
        constraint->SetAngularLowerLimit(Vector3(-30.0f, -30.0f, -30.0f));
        constraint->SetAngularUpperLimit(Vector3(30.0f, 30.0f, 30.0f));

        // Enable springs on all 3 angular axes — motors target bind pose
        for (int axis = 3; axis <= 5; ++axis)
        {
            constraint->EnableSpring(axis, true);
            constraint->SetSpringStiffness(axis, stiffness);
            constraint->SetSpringDamping(axis, damping);
        }

        ++created;
    }

    URHO3D_LOGINFOF("BoneCollisionGenerator::CreateConstraints: %u constraints created (stiffness=%.0f, damping=%.1f)",
        created, stiffness, damping);

    return created;
}

} // namespace Urho3D
