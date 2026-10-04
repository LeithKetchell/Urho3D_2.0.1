// SchoolFish — tiny schooling fish with flocking behavior.

#include "SchoolFish.h"
#include "FishSpatialHash.h"
#include "SchoolState.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/IO/Log.h>

SchoolFish::SchoolFish(Context* context) :
    Fish(context)
{
    // School fish defaults — smaller, faster, wider vision (prey)
    dbSwimSpeed_ = 0.8f;
    swimSpeed_ = 0.8f;
    dbWanderRadius_ = 40.0f;
    boundary_ = 40.0f;
    maturityAge_ = 45.0f;  // School fish mature faster than big fish
    InitBehaviorStats(0.8f, 40.0f, 15.0f, 0.3f, false, false, 0.0f);

    // Stagger breed timers so not all fish try to breed on the same frame
    breedTimer_ = Random(BREED_INTERVAL_BASE * 0.5f, BREED_INTERVAL_BASE * 1.5f);
}

void SchoolFish::RegisterObject(Context* context)
{
    context->RegisterFactory<SchoolFish>();
}

void SchoolFish::Start()
{
    Fish::Start();
    adultScale_ = node_->GetScale().x_;  // capture adult size before growth kicks in
}

void SchoolFish::Stop()
{
    // Decrement live population count when this fish is removed from the scene.
    // totalCaught is incremented only by explicit fishing/harvest actions (not here)
    // to distinguish natural death/cleanup from player-caused depletion.
    if (popState_ && popState_->currentCount > 0)
        popState_->currentCount--;

    Fish::Stop();
}

void SchoolFish::ComputeSchoolState(Vector3& centroid, Vector3& avgHeading, unsigned& count)
{
    centroid = Vector3::ZERO;
    avgHeading = Vector3::ZERO;
    count = 0;

    // Use school state cache — first fish to call triggers computation, rest get cached result
    if (schoolCache_)
    {
        SchoolState& state = schoolCache_->GetState(schoolID_);
        if (state.frameComputed == schoolCache_->GetCurrentFrame())
        {
            // Already computed this frame — use cached
            centroid = state.centroid;
            avgHeading = state.averageHeading;
            count = (unsigned)state.memberCount;
            return;
        }

        // First fish in this school this frame — compute via spatial hash
        if (spatialHash_)
        {
            schoolBuf_.Clear();
            spatialHash_->QuerySchool(node_->GetWorldPosition(), GetWanderRadius(), schoolID_, schoolBuf_);

            for (unsigned i = 0; i < schoolBuf_.Size(); ++i)
            {
                Node* n = schoolBuf_[i]->GetNode();
                centroid += n->GetWorldPosition();
                avgHeading += n->GetWorldRotation() * Vector3::BACK;
                ++count;
            }

            if (count > 0)
            {
                centroid /= (float)count;
                if (avgHeading.LengthSquared() > 0.001f)
                    avgHeading.Normalize();
            }

            state.centroid = centroid;
            state.averageHeading = avgHeading;
            state.memberCount = (int)count;
            state.frameComputed = schoolCache_->GetCurrentFrame();
            return;
        }
    }

    // Fallback: no hash/cache available (shouldn't happen in normal operation)
}

void SchoolFish::Update(float timeStep)
{
    // Age tick (inherited, but we also run breeding)
    age_ += timeStep;
    if (!mature_ && age_ >= maturityAge_)
        mature_ = true;

    // Natural death (old age)
    if (age_ >= maxAge_)
    {
        node_->Remove();
        return;
    }

    // Visual growth — scale represents age continuously
    {
        float growthFrac = Clamp(age_ / graduationAge_, 0.0f, 1.0f);
        float scale = adultScale_ * Lerp(0.6f, 2.0f, growthFrac);  // grow to 2x school scale at graduation
        node_->SetScale(scale);
    }

    // Graduation — leave the school and become a solitary Fish
    if (age_ >= graduationAge_)
    {
        Graduate();
        return;  // this component is now removed
    }

    // Breeding attempt (only mature fish try)
    if (mature_)
        TryBreed(timeStep);

    Vector3 pos = node_->GetWorldPosition();
    Quaternion rot = node_->GetWorldRotation();
    Vector3 forward = rot * Vector3::BACK;

    // --- Predator scan: check for visible big fish ---
    if (!scattering_ && spatialHash_)
    {
        separationBuf_.Clear();
        spatialHash_->Query(pos, GetVisionRange(), separationBuf_);
        for (unsigned i = 0; i < separationBuf_.Size(); ++i)
        {
            Fish* other = separationBuf_[i];
            if (other == static_cast<Fish*>(this)) continue;
            if (other->IsSchoolFish()) continue;  // only flee from big fish
            if (!CanSee(other)) continue;

            // Predator spotted — scatter!
            Vector3 awayDir = pos - other->GetNode()->GetWorldPosition();
            awayDir.y_ *= 0.3f;  // mostly horizontal scatter
            if (awayDir.LengthSquared() < 0.01f)
                awayDir = Vector3(Random(-1.0f, 1.0f), 0.0f, Random(-1.0f, 1.0f));
            awayDir.Normalize();
            // Add individual randomness so the school doesn't flee in a sheet
            awayDir += Vector3(Random(-0.4f, 0.4f), Random(-0.1f, 0.1f), Random(-0.4f, 0.4f));
            awayDir.Normalize();

            scattering_ = true;
            scatterTimer_ = SCATTER_DURATION;
            scatterDir_ = awayDir;
            break;  // one predator is enough
        }
    }

    // --- Scatter mode: flee independently, ignore school ---
    if (scattering_)
    {
        scatterTimer_ -= timeStep;
        if (scatterTimer_ <= 0.0f)
        {
            scattering_ = false;
            // Fall through to normal schooling — cohesion will reform
        }
        else
        {
            // Flee in scatter direction at burst speed
            Vector3 desiredDir = scatterDir_;

            // Boundary avoidance still applies
            float boundary = GetWanderRadius();
            float distFromCenter = Vector2(pos.x_, pos.z_).Length();
            if (distFromCenter > boundary)
            {
                Vector3 toCenter = -pos;
                toCenter.y_ = 0.0f;
                toCenter.Normalize();
                float boundaryUrgency = Clamp((distFromCenter - boundary) / 10.0f, 0.0f, 1.0f);
                desiredDir = desiredDir.Lerp(toCenter, boundaryUrgency);
            }

            // Shallow avoidance
            if (terrain_)
            {
                Vector3 probe = pos + desiredDir * 2.0f;
                float probeH = terrain_->GetHeight(probe);
                float waterLevel = 5.0f;
                float probeDepth = waterLevel - probeH;
                if (probeDepth < 1.0f)
                {
                    Vector3 toDeep = pos - probe;
                    toDeep.y_ = 0.0f;
                    if (toDeep.LengthSquared() > 0.001f)
                        toDeep.Normalize();
                    desiredDir = desiredDir.Lerp(toDeep, 0.8f);
                }
            }

            desiredDir.y_ *= 0.5f;
            if (desiredDir.LengthSquared() > 0.001f)
                desiredDir.Normalize();

            Quaternion targetRot;
            targetRot.FromLookRotation(-desiredDir);
            rot = rot.Slerp(targetRot, schoolTurnSpeed_ * 1.5f * timeStep);
            node_->SetRotation(rot);

            forward = rot * Vector3::BACK;
            pos += forward * SCATTER_SPEED * timeStep;

            // Water column clamp
            float waterLevel = 5.0f;
            float terrainH = terrain_ ? terrain_->GetHeight(pos) : 0.0f;
            float floorY = terrainH + 0.2f;
            float ceilY = waterLevel - 0.2f;
            if (floorY > ceilY) floorY = ceilY;
            pos.y_ = Clamp(pos.y_, floorY, ceilY);

            node_->SetPosition(pos);
            return;  // skip normal schooling this frame
        }
    }

    // --- Normal schooling behavior ---

    // --- Schooling forces ---
    Vector3 centroid;
    Vector3 avgHeading;
    unsigned schoolSize;
    ComputeSchoolState(centroid, avgHeading, schoolSize);

    Vector3 desiredDir = forward;

    if (schoolSize > 0)
    {
        // Cohesion — steer toward school centroid
        Vector3 toCentroid = centroid - pos;
        float distToCentroid = toCentroid.Length();
        if (distToCentroid > 0.5f)
        {
            toCentroid.Normalize();
            desiredDir += toCentroid * cohesionWeight_;
        }

        // Alignment — match average school heading
        desiredDir += avgHeading * alignmentWeight_;

        // Separation — push away from too-close neighbours via spatial hash
        if (spatialHash_)
        {
            separationBuf_.Clear();
            spatialHash_->Query(pos, separationDist_, separationBuf_);
            for (unsigned i = 0; i < separationBuf_.Size(); ++i)
            {
                if (separationBuf_[i] == static_cast<Fish*>(this)) continue;
                Vector3 diff = pos - separationBuf_[i]->GetNode()->GetWorldPosition();
                float dist = diff.Length();
                if (dist < separationDist_ && dist > 0.001f)
                {
                    diff.Normalize();
                    float urgency = 1.0f - (dist / separationDist_);
                    desiredDir += diff * urgency * separationWeight_;
                }
            }
        }
    }

    // --- Random wander (smaller than regular fish) ---
    if (Random(1.0f) < 0.01f)
    {
        float wanderAngle = Random(-45.0f, 45.0f);
        Quaternion wanderRot(wanderAngle, Vector3::UP);
        desiredDir = wanderRot * desiredDir;
        desiredDir.y_ += Random(-0.05f, 0.05f);
    }

    // --- Boundary avoidance ---
    float boundary = GetWanderRadius();
    float distFromCenter = Vector2(pos.x_, pos.z_).Length();
    if (distFromCenter > boundary)
    {
        Vector3 toCenter = -pos;
        toCenter.y_ = 0.0f;
        toCenter.Normalize();
        float boundaryUrgency = Clamp((distFromCenter - boundary) / 10.0f, 0.0f, 1.0f);
        desiredDir = desiredDir.Lerp(toCenter, boundaryUrgency);
    }

    // --- Shallow water avoidance ---
    if (terrain_)
    {
        Vector3 probe = pos + forward * 2.0f;
        float probeH = terrain_->GetHeight(probe);
        float waterLevel = 5.0f;
        float probeDepth = waterLevel - probeH;
        if (probeDepth < 1.0f)
        {
            Vector3 toDeep = pos - probe;
            toDeep.y_ = 0.0f;
            if (toDeep.LengthSquared() > 0.001f)
                toDeep.Normalize();
            float shallowUrgency = Clamp(1.0f - (probeDepth / 1.0f), 0.0f, 1.0f);
            desiredDir = desiredDir.Lerp(toDeep, shallowUrgency);
        }
    }

    // --- Finalize direction ---
    desiredDir.y_ *= 0.5f;  // dampen vertical movement — schools stay at level
    if (desiredDir.LengthSquared() > 0.001f)
        desiredDir.Normalize();
    else
        desiredDir = forward;

    // Slerp toward desired heading
    Quaternion targetRot;
    targetRot.FromLookRotation(-desiredDir);
    rot = rot.Slerp(targetRot, schoolTurnSpeed_ * timeStep);
    node_->SetRotation(rot);

    // Move forward
    forward = rot * Vector3::BACK;
    float speed = GetWanderSpeed();
    pos += forward * speed * timeStep;

    // Clamp to water column
    float waterLevel = 5.0f;
    float terrainH = terrain_ ? terrain_->GetHeight(pos) : 0.0f;
    float floorY = terrainH + 0.2f;
    float ceilY = waterLevel - 0.2f;
    if (floorY > ceilY)
        floorY = ceilY;
    pos.y_ = Clamp(pos.y_, floorY, ceilY);

    node_->SetPosition(pos);
}

void SchoolFish::Graduate()
{
    // Swap from SchoolFish to solitary Fish on the same node.
    // Keep position, rotation, model — just change the AI component.
    Node* n = node_;

    // Capture state before removal
    Node* camNode = GetCameraNode();
    FishSpatialHash* hash = spatialHash_;
    float currentAge = age_;
    float remainingLife = maxAge_ - age_;

    // Remove this SchoolFish component (triggers Stop → pop decrement)
    n->RemoveComponent(this);

    // Attach solitary Fish component
    auto* fish = n->CreateComponent<Fish>();
    fish->SetCameraNode(camNode);
    fish->SetSpatialHash(hash);

    // Transfer age — the fish continues aging from where it was
    // Set a lifespan relative to remaining life
    fish->SetMaxAge(currentAge + remainingLife);

    // Solitary fish are predators with wider wander radius
    fish->InitFromDB(8, "Models/UrhoFish.mdl", 0.5f, 75.0f, 20.0f, 0.5f, true);

    // Materials — use the base material already on the model
    auto* sm = n->GetComponent<StaticModel>();
    if (sm)
    {
        Material* mat = sm->GetMaterial();
        fish->SetMaterials(mat, mat, mat);
    }

    // Fire FishBorn event so TerrainNode tracks the graduated fish
    using namespace FishBorn;
    VariantMap& eventData = fish->GetEventDataMap();
    eventData[P_NODE] = n;
    eventData[P_SCHOOLID] = (unsigned)0xFFFFFFFF;  // sentinel: not a school fish
    fish->SendEvent(E_FISHBORN, eventData);

    URHO3D_LOGDEBUG("SchoolFish graduated to solitary Fish");
}

void SchoolFish::TryBreed(float timeStep)
{
    if (!popState_)
        return;

    // Check breeding multiplier — 0 means population collapse, no breeding
    float breedMult = popState_->GetBreedingMultiplier();
    if (breedMult <= 0.0f)
        return;

    // At or above cap — no breeding
    if (popState_->currentCount >= popState_->populationCap)
        return;

    // Tick breed timer (faster when stressed)
    breedTimer_ -= timeStep * breedMult;
    if (breedTimer_ > 0.0f)
        return;

    // Reset timer for next attempt
    breedTimer_ = BREED_INTERVAL_BASE + Random(-5.0f, 5.0f);

    // Need at least 2 mature fish in the school to breed
    // Use the school state cache — memberCount includes all fish (mature or not)
    // but requiring 2+ total with ourselves mature is a sufficient approximation
    if (schoolCache_)
    {
        SchoolState& state = schoolCache_->GetState(schoolID_);
        if (state.memberCount < 2)
            return;
    }

    // Spawn a juvenile near our position
    Node* juvenile = node_->GetScene()->CreateTemporaryChild("SchoolFish", LOCAL);
    if (!juvenile)
        return;

    Vector3 pos = node_->GetWorldPosition();
    Vector3 offset(Random(-1.0f, 1.0f), Random(-0.2f, 0.2f), Random(-1.0f, 1.0f));
    juvenile->SetPosition(pos + offset);
    juvenile->SetRotation(Quaternion(0.0f, Random(0.0f, 360.0f), 0.0f));
    juvenile->SetScale(node_->GetScale());

    // Copy model from parent
    auto* parentModel = node_->GetComponent<StaticModel>();
    if (parentModel)
    {
        auto* sm = juvenile->CreateComponent<StaticModel>();
        sm->SetModel(parentModel->GetModel(), true);
        sm->SetMaterial(parentModel->GetMaterial());
        sm->SetCastShadows(false);
    }

    // Attach SchoolFish component with same school membership
    auto* baby = juvenile->CreateComponent<SchoolFish>();
    baby->SetSchoolID(schoolID_);
    baby->SetCameraNode(GetCameraNode());
    baby->SetSpatialHash(spatialHash_);
    baby->SetSchoolCache(schoolCache_);
    baby->SetPopulationState(popState_);
    if (sunLightNode_)
        baby->SetSunLightNode(sunLightNode_);

    // Update population count
    popState_->currentCount++;

    // Notify TerrainNode to track the new node in fishNodes_ / spatial hash
    using namespace FishBorn;
    VariantMap& eventData = GetEventDataMap();
    eventData[P_NODE] = juvenile;
    eventData[P_SCHOOLID] = schoolID_;
    SendEvent(E_FISHBORN, eventData);

    URHO3D_LOGDEBUGF("SchoolFish: juvenile spawned in school %u (pop %d/%d)",
                     schoolID_, popState_->currentCount, popState_->populationCap);
}
