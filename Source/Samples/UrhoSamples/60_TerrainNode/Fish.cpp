// Fish — aquatic creature. Swims underwater, avoids shallows, reacts to camera.

#include "Fish.h"
#include "FishSpatialHash.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/IO/Log.h>

Fish::Fish(Context* context) :
    WaterAnimal(context)
{
    // Randomize lifespan ±20% so fish don't all die at the same time
    maxAge_ = maxAge_ * Random(0.8f, 1.2f);
}

void Fish::RegisterObject(Context* context)
{
    context->RegisterFactory<Fish>();
}

void Fish::Start()
{
    WaterAnimal::Start();

    cachedModel_ = node_->GetComponent<StaticModel>();

    // cameraNode_ is set via SetCameraNode() after CreateComponent<Fish>() returns —
    // not available yet during Start()
}

void Fish::SetMaterials(Material* base, Material* orbit, Material* stare)
{
    baseMat_ = SharedPtr<Material>(base);
    orbitMat_ = SharedPtr<Material>(orbit);
    stareMat_ = SharedPtr<Material>(stare);
}

void Fish::Update(float timeStep)
{
    // --- Age tick ---
    age_ += timeStep;
    if (!mature_ && age_ >= maturityAge_)
        mature_ = true;

    // --- Natural death (old age) ---
    if (age_ >= maxAge_)
    {
        node_->Remove();
        return;
    }

    // --- Hunt scan: big fish look for school fish prey ---
    // Rate-limited: only scan when not already hunting, ~every 0.5s via hunt timer reuse
    if (!hunting_ && spatialHash_)
    {
        huntTimer_ -= timeStep;
        if (huntTimer_ <= 0.0f)
        {
            huntTimer_ = 0.5f;
            Vector3 pos = node_->GetWorldPosition();
            float range = GetVisionRange();

            nearbyBuf_.Clear();
            spatialHash_->Query(pos, range, nearbyBuf_);

            Fish* bestPrey = nullptr;
            float bestDistSq = M_INFINITY;
            for (unsigned i = 0; i < nearbyBuf_.Size(); ++i)
            {
                Fish* other = nearbyBuf_[i];
                if (other == this) continue;
                if (!other->IsSchoolFish()) continue;
                if (!CanSee(other)) continue;

                float distSq = (other->GetNode()->GetWorldPosition() - pos).LengthSquared();
                if (distSq < bestDistSq)
                {
                    bestDistSq = distSq;
                    bestPrey = other;
                }
            }

            if (bestPrey)
            {
                huntTarget_ = bestPrey->GetNode();
                hunting_ = true;
                huntTimer_ = 5.0f;  // memory window
            }
        }
    }

    // --- Active hunt: chase prey ---
    if (hunting_)
    {
        Node* prey = huntTarget_.Get();
        if (!prey)
        {
            // Prey gone — give up
            hunting_ = false;
            huntTarget_.Reset();
        }
        else
        {
            // Can we still see it? Refresh memory.
            auto* preyCreature = prey->GetComponent<Creature>();
            if (preyCreature && CanSee(preyCreature))
                huntTimer_ = 5.0f;

            huntTimer_ -= timeStep;
            if (huntTimer_ <= 0.0f)
            {
                // Lost the prey — give up
                hunting_ = false;
                huntTarget_.Reset();
            }
        }
    }

    Swim(timeStep);
    ClampToWaterColumn();
}

void Fish::Swim(float timeStep)
{
    Vector3 pos = node_->GetWorldPosition();
    Quaternion rot = node_->GetWorldRotation();
    Vector3 forward = rot * Vector3::BACK;

    // --- Neighbour avoidance via spatial hash ---
    Vector3 desiredDir = forward;
    float nearestDist = M_INFINITY;
    Vector3 nearestDir;

    if (spatialHash_)
    {
        nearbyBuf_.Clear();
        spatialHash_->Query(pos, comfortDist_, nearbyBuf_);
        for (unsigned i = 0; i < nearbyBuf_.Size(); ++i)
        {
            if (nearbyBuf_[i] == this) continue;
            Vector3 diff = nearbyBuf_[i]->GetNode()->GetWorldPosition() - pos;
            float dist = diff.Length();
            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearestDir = diff;
            }
        }
    }

    if (nearestDist < comfortDist_ && nearestDist > 0.001f)
    {
        float urgency = 1.0f - (nearestDist / comfortDist_);
        Vector3 awayDir = -nearestDir.Normalized();
        awayDir.y_ = 0.0f;
        if (awayDir.LengthSquared() > 0.001f)
            awayDir.Normalize();
        else
            awayDir = forward;
        desiredDir = forward.Lerp(awayDir, urgency);
    }

    // --- Hunt steering: override direction when chasing prey ---
    if (hunting_ && huntTarget_)
    {
        Vector3 toPrey = huntTarget_->GetWorldPosition() - pos;
        if (toPrey.LengthSquared() > 0.01f)
        {
            toPrey.Normalize();
            // Strong bias toward prey — overrides neighbor avoidance
            desiredDir = desiredDir.Lerp(toPrey, 0.8f);
        }
    }

    // --- Camera interaction ---
    Node* camNode = cameraNode_;
    int fishZone = 0;

    if (camNode)
    {
        Vector3 camPos = camNode->GetWorldPosition();
        Vector3 toCam3D = camPos - pos;
        Vector3 toCamFlat = toCam3D;
        toCamFlat.y_ = 0.0f;
        float camDist = toCamFlat.Length();

        if (camDist < 1.0f && camDist > 0.01f)
        {
            Vector3 radial = toCamFlat.LengthSquared() > 0.001f ? toCamFlat.Normalized() : forward;
            Vector3 tangentCW(radial.z_, 0.0f, -radial.x_);
            Vector3 tangentCCW(-radial.z_, 0.0f, radial.x_);
            desiredDir = (forward.DotProduct(tangentCW) >= forward.DotProduct(tangentCCW)) ? tangentCW : tangentCCW;
            fishZone = 1;
        }
        else if (camDist < stareDist_ && camDist > 0.5f)
        {
            Vector3 faceCam = toCam3D;
            if (faceCam.LengthSquared() > 0.001f)
                faceCam.Normalize();
            desiredDir = faceCam;
            fishZone = 2;
        }
        else if (camDist < orbitFar_ && camDist > 1.0f)
        {
            Vector3 radial = toCamFlat / camDist;
            Vector3 tangentCW(radial.z_, 0.0f, -radial.x_);
            Vector3 tangentCCW(-radial.z_, 0.0f, radial.x_);
            Vector3 tangent = (forward.DotProduct(tangentCW) >= forward.DotProduct(tangentCCW)) ? tangentCW : tangentCCW;
            float orbitStrength = Clamp(1.0f - (camDist - orbitNear_) / (orbitFar_ - orbitNear_), 0.0f, 0.8f);
            desiredDir = desiredDir.Lerp(tangent, orbitStrength);
            fishZone = 1;
        }
    }

    // --- Random wander impulse ---
    if (Random(1.0f) < 0.02f)
    {
        float wanderAngle = Random(-90.0f, 90.0f);
        Quaternion wanderRot(wanderAngle, Vector3::UP);
        desiredDir = wanderRot * desiredDir;
        desiredDir.y_ += Random(-0.15f, 0.15f);
    }

    // --- Boundary avoidance ---
    float distFromCenter = Vector2(pos.x_, pos.z_).Length();
    if (distFromCenter > boundary_)
    {
        Vector3 toCenter = -pos;
        toCenter.y_ = 0.0f;
        toCenter.Normalize();
        float boundaryUrgency = Clamp((distFromCenter - boundary_) / 10.0f, 0.0f, 1.0f);
        desiredDir = desiredDir.Lerp(toCenter, boundaryUrgency);
    }

    // --- Shallow water avoidance (via WaterAnimal helper) ---
    Vector3 shallowAvoid = GetShallowAvoidance(pos, forward, 3.0f);
    if (shallowAvoid.LengthSquared() > 0.001f)
        desiredDir = desiredDir.Lerp(desiredDir + shallowAvoid, shallowAvoid.Length());

    // --- Finalize direction ---
    desiredDir.y_ *= 0.8f;
    if (desiredDir.LengthSquared() > 0.001f)
        desiredDir.Normalize();
    else
        desiredDir = forward;

    Quaternion targetRot;
    targetRot.FromLookRotation(-desiredDir);
    rot = rot.Slerp(targetRot, turnSpeed_ * timeStep);
    node_->SetRotation(rot);

    // Move forward — slow down near camera, speed up when hunting
    float speedFactor = 1.0f;
    if (hunting_ && huntTarget_)
        speedFactor = 1.8f;  // burst speed while chasing
    else if (camNode)
    {
        Vector3 toCamFlat = camNode->GetWorldPosition() - pos;
        toCamFlat.y_ = 0.0f;
        float camDist = toCamFlat.Length();
        if (camDist < orbitFar_)
            speedFactor = Lerp(0.15f, 1.0f, Clamp(camDist / orbitFar_, 0.0f, 1.0f));
    }

    forward = rot * Vector3::BACK;
    pos += forward * swimSpeed_ * speedFactor * timeStep;
    node_->SetPosition(pos);

    // --- Material swap based on zone ---
    StaticModel* sm = cachedModel_;
    if (sm)
    {
        Material* want = (fishZone == 2) ? stareMat_.Get() :
                         (fishZone == 1) ? orbitMat_.Get() :
                                           baseMat_.Get();
        if (want && sm->GetMaterial() != want)
            sm->SetMaterial(want);
    }
}

void Fish::InitFromDB(int /*creatureId*/, const String& modelPath, float speed,
                      float wanderRad, float visionRange, float visionAngle,
                      bool isPredator)
{
    if (!modelPath.Empty())
        dbModelPath_ = modelPath;
    dbSwimSpeed_ = speed;
    swimSpeed_ = speed;
    dbWanderRadius_ = wanderRad;
    boundary_ = wanderRad;
    // Use base class DB behavior members
    InitBehaviorStats(speed, wanderRad, visionRange, visionAngle, isPredator, false, 0.0f);
}
