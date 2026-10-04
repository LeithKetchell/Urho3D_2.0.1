// LandAnimal — terrain-roaming creature.

#include "LandAnimal.h"
#include "LandAnimalSpatialHash.h"
#include "BuildingSystem.h"
#include "EcosystemManager.h"
#include "ScentRegistry.h"   // Death System Phase 3 — scavenger scent queries

#include <Urho3D/Core/Context.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/ParticleEffect.h>
#include <Urho3D/Graphics/ParticleEmitter.h>
#include <Urho3D/Graphics/Skeleton.h>
#include <Urho3D/Audio/SoundSource3D.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Resource/ResourceCache.h>

#include <climits>

unsigned LandAnimal::nextStaggerID_ = 0;

// ============================================================================
// Corpse tuning — see PLAN_DEATH_SYSTEM.md Phase 2
// ============================================================================
// CORPSE_DURATION: how long the body is visible after the death anim ends.
// CORPSE_FADE_DURATION: extra time to sink the body into the ground before removal.
// CORPSE_SINK_DEPTH: metres to lower the node Y over the fade window.
// During the fade phase stateTimer_ goes negative; when it reaches -CORPSE_FADE_DURATION
// the node is removed from the scene.
#define CORPSE_DURATION (Creature::GetDeathRules().corpseDuration)
static const float CORPSE_FADE_DURATION  =   2.0f;
static const float CORPSE_SINK_DEPTH     =   1.5f;

// ============================================================================
// Scavenger tuning — see PLAN_DEATH_SYSTEM.md Phase 3
// ============================================================================
// SCENT_DETECTION_RADIUS: max distance a scavenger will sense a kill.
// SCAVENGER_EAT_DURATION: seconds at the kill site before returning to wander.
// SCAVENGER_ARRIVAL_DIST: how close the scavenger needs to get before "on site".
// SCAVENGE_PROBE_INTERVAL: idle/wander frames between scent scans (rate-limited
//                          so we don't grep the marker list every FixedUpdate).
static const float SCENT_DETECTION_RADIUS  = 50.0f;
static const float SCAVENGER_EAT_DURATION  =  6.0f;
static const float SCAVENGER_ARRIVAL_DIST  =  2.0f;
static const float SCAVENGE_PROBE_INTERVAL =  1.0f;

// ============================================================================
// Vision scan tuning — see PLAN_ANIMAL_VISION.md Phase 2
// ============================================================================
// VISION_SCAN_INTERVAL: seconds between vision scans per animal. Staggered
//                       across the herd by adding a per-animal offset.
// BULK_SIMILARITY_THRESHOLD: how close two animals' GetDesiredSize() must be
//                            to count as "similar bulk" (stag/bull charge logic).
static const float VISION_SCAN_INTERVAL       = 0.5f;
static const float BULK_SIMILARITY_THRESHOLD  = 0.5f;

LandAnimal::LandAnimal(Context* context) :
    Creature(context)
{
    staggerID_ = nextStaggerID_++;
}

void LandAnimal::Start()
{
    Creature::Start();

    // Every land animal gets a 3D sound source for footsteps, vocalizations, combat
    if (!node_->GetComponent<SoundSource3D>())
    {
        auto* source = node_->CreateComponent<SoundSource3D>();
        source->SetSoundType("Effect");
        source->SetNearDistance(1.0f);
        source->SetFarDistance(50.0f);
    }

    // Phase 0: Kinematic physics capsule for collision detection.
    // Does not change movement — creature still uses MoveToward() + SnapToTerrain().
    // Capsule size derived from model bounding box via GetDesiredSize().
    if (!node_->GetComponent<RigidBody>())
    {
        float size = GetDesiredSize();
        float capsuleHeight = size * 0.9f;
        float capsuleDiameter = size * 0.4f;

        auto* body = node_->CreateComponent<RigidBody>();
        body->SetMass(1.0f);
        body->SetKinematic(true);
        body->SetAngularFactor(Vector3::ZERO);
        body->SetCollisionLayer(4);   // creature layer
        body->SetCollisionMask(3);    // collides with terrain (1) and static (2)

        auto* shape = node_->CreateComponent<CollisionShape>();
        shape->SetCapsule(capsuleDiameter, capsuleHeight, Vector3(0.0f, capsuleHeight * 0.5f, 0.0f));
    }
}

// ============================================================================
// Creature Audio Framework (Phase 5)
// ============================================================================

static void PlayCreatureSound(Node* node, const String& path, SharedPtr<Sound>& cached)
{
    if (path.Empty() || !node)
        return;

    // Lazy-load on first play
    if (!cached)
    {
        auto* cache = node->GetSubsystem<ResourceCache>();
        if (cache)
            cached = cache->GetResource<Sound>(path);
        if (!cached)
            return; // File missing — silent, no error spam
    }

    auto* source = node->GetComponent<SoundSource3D>();
    if (source)
        source->Play(cached);
}

void LandAnimal::PlayFootstep()
{
    PlayCreatureSound(node_, footstepSoundPath_, footstepSound_);
}

void LandAnimal::PlayVocalization()
{
    PlayCreatureSound(node_, vocalizationSoundPath_, vocalizationSound_);
}

void LandAnimal::PlayHit()
{
    PlayCreatureSound(node_, hitSoundPath_, hitSound_);
}

void LandAnimal::PlayDeath()
{
    PlayCreatureSound(node_, deathSoundPath_, deathSound_);
}

// ============================================================================
// Head bone discovery — structural, rig-name agnostic
// ============================================================================
//
// Rule (Leith, 2026-04-07): the head bone is the topmost bone reachable from
// the root by walking children that ascend in bind-pose world Y. When more
// than one ascending child exists at a junction (e.g. spine-vs-arm at the
// shoulder, or antlers-vs-skull at the deer head), pick the child whose
// remaining sub-chain is shortest — the head's continuation is short
// (typically a leaf), limbs and antlers are long.
//
// This rule is structural: it ignores bone names entirely, so it survives
// every exporter renaming the head bone to mixamorig:Head, head_jnt, Head_M,
// or whatever else. It's also the seed shape for the future cross-skeleton
// rigging system — same idea, larger scope.
//
// The selected bone's NAME is logged once per discovery as a sanity check —
// not as a fallback, just so we can spot when an animator did the obvious
// thing and named it "Head".

namespace
{
    /// Maximum chain length when descending from a bone, following the
    /// child with the highest world Y at each step. Used as the
    /// "remaining sub-chain length" metric for the shortest-path tiebreaker.
    int LongestAscendingChain(int startIdx, const Vector<Bone>& bones,
                              const Vector<Vector3>& worldPos)
    {
        int len = 0;
        int current = startIdx;
        // Cap at total bone count to guard against pathological loops.
        for (int hop = 0; hop < (int)bones.Size(); ++hop)
        {
            int bestChild = -1;
            float bestY = worldPos[current].y_;
            for (unsigned i = 0; i < bones.Size(); ++i)
            {
                if ((int)i == current) continue;
                if (bones[i].parentIndex_ != current) continue;
                if (worldPos[i].y_ > bestY)
                {
                    bestY = worldPos[i].y_;
                    bestChild = (int)i;
                }
            }
            if (bestChild < 0) break;
            current = bestChild;
            ++len;
        }
        return len;
    }
}

int LandAnimal::FindHeadBoneIndex(const Skeleton& skel, const String& diagSpecies)
{
    const Vector<Bone>& bones = skel.GetBones();
    if (bones.Size() < 2)
        return -1;

    // --- Pass 0: try by name first. A bone named "Head" or "Neck" is
    // authoritative — most rigs follow this convention. Case-insensitive.
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (bones[i].name_.Contains("Head", false))
        {
            if (!diagSpecies.Empty())
                URHO3D_LOGINFOF("LandAnimal[%s]: head bone -> '%s' (idx %d) found by name (%u bones in skeleton)",
                    diagSpecies.CString(), bones[i].name_.CString(), (int)i, bones.Size());
            return (int)i;
        }
    }
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (bones[i].name_.Contains("Neck", false))
        {
            if (!diagSpecies.Empty())
                URHO3D_LOGINFOF("LandAnimal[%s]: neck bone -> '%s' (idx %d) used as head proxy, found by name (%u bones in skeleton)",
                    diagSpecies.CString(), bones[i].name_.CString(), (int)i, bones.Size());
            return (int)i;
        }
    }

    // --- Pass 1: compute bind-pose world transform for every bone.
    // Bones in .mdl files are stored in hierarchy order (parent before
    // child), so a single forward pass suffices.
    Vector<Vector3> worldPos;
    Vector<Quaternion> worldRot;
    worldPos.Resize(bones.Size());
    worldRot.Resize(bones.Size());
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        const Bone& b = bones[i];
        const bool isRoot = (b.parentIndex_ == (int)i || b.parentIndex_ < 0
                             || b.parentIndex_ >= (int)bones.Size());
        if (isRoot)
        {
            worldPos[i] = b.initialPosition_;
            worldRot[i] = b.initialRotation_;
        }
        else
        {
            const int p = b.parentIndex_;
            worldPos[i] = worldPos[p] + worldRot[p] * b.initialPosition_;
            worldRot[i] = worldRot[p] * b.initialRotation_;
        }
    }

    // --- Pass 2: locate the root (parent index == self).
    int rootIdx = -1;
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (bones[i].parentIndex_ == (int)i)
        {
            rootIdx = (int)i;
            break;
        }
    }
    if (rootIdx < 0)
        rootIdx = 0;

    // --- Pass 3: walk from root, ascending Y. At each junction with
    // multiple Y-ascending children, take the one whose remaining
    // ascending sub-chain is SHORTEST (shortest path = head; longer
    // paths = limbs / antlers).
    int current = rootIdx;
    for (int hop = 0; hop < (int)bones.Size(); ++hop)
    {
        // Gather Y-ascending children of `current`.
        int bestChild = -1;
        int bestSubLen = INT_MAX;
        float bestY = worldPos[current].y_;
        for (unsigned i = 0; i < bones.Size(); ++i)
        {
            if ((int)i == current) continue;
            if (bones[i].parentIndex_ != current) continue;
            if (worldPos[i].y_ <= worldPos[current].y_) continue;

            const int subLen = LongestAscendingChain((int)i, bones, worldPos);
            // Tiebreaker rule: shortest sub-chain wins. On true ties (rare),
            // prefer the higher-Y child (closer to the visual top of the
            // skull) — keeps deterministic behaviour without name lookup.
            if (subLen < bestSubLen
                || (subLen == bestSubLen && worldPos[i].y_ > bestY))
            {
                bestSubLen = subLen;
                bestY = worldPos[i].y_;
                bestChild = (int)i;
            }
        }
        if (bestChild < 0)
            break;
        current = bestChild;
    }

    if (!diagSpecies.Empty() && current >= 0 && current < (int)bones.Size())
    {
        const Bone& head = bones[current];
        URHO3D_LOGINFOF(
            "LandAnimal[%s]: head bone -> '%s' (idx %d) bind worldY=%.3f "
            "(%u bones in skeleton)",
            diagSpecies.CString(), head.name_.CString(), current,
            worldPos[current].y_, bones.Size());
    }

    return current;
}

Vector3 LandAnimal::GetHeadWorldPos() const
{
    if (!node_)
        return Vector3::ZERO;
    auto* mdl = node_->GetComponent<AnimatedModel>(true);
    if (!mdl || headBoneIndex_ < 0)
        return node_->GetWorldPosition();
    const Skeleton& skel = mdl->GetSkeleton();
    const Vector<Bone>& bones = skel.GetBones();
    if (headBoneIndex_ >= (int)bones.Size())
        return node_->GetWorldPosition();
    const Bone& bone = bones[headBoneIndex_];
    if (!bone.node_)
        return node_->GetWorldPosition();
    return bone.node_->GetWorldPosition();
}

// ============================================================================
// Prefix-based animation path construction
// ============================================================================

String LandAnimal::GetIdleAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;

    // 30% chance for alternate idle (head-low grazing pose)
    // Herbivores use "Idle_Headlow", predators use "Idle_2_HeadLow"
    if (Random(1.0f) < 0.3f)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        String headlow = GetModelDir() + prefix + "Idle_Headlow.ani";
        if (cache && cache->Exists(headlow))
            return headlow;
        String headlow2 = GetModelDir() + prefix + "Idle_2_HeadLow.ani";
        if (cache && cache->Exists(headlow2))
            return headlow2;
    }
    // 20% chance for Idle_2 (head movement variety)
    if (Random(1.0f) < 0.3f)
        return GetModelDir() + prefix + "Idle_2.ani";

    return GetModelDir() + prefix + "Idle.ani";
}

String LandAnimal::GetRunAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;
    return GetModelDir() + prefix + "Gallop.ani";
}

String LandAnimal::GetWalkAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;
    return GetModelDir() + prefix + "Walk.ani";
}

String LandAnimal::GetEatAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;
    return GetModelDir() + prefix + "Eating.ani";
}

String LandAnimal::GetDieAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;
    return GetModelDir() + prefix + "Death.ani";
}

String LandAnimal::GetAttackAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;

    // Predators (Wolf, Fox, Husky, ShibaInu) have "Attack.ani".
    // Herbivores have "Attack_Headbutt.ani" and "Attack_Kick.ani" instead.
    // Try plain name first, fall back to headbutt/kick variants.
    String plain = GetModelDir() + prefix + "Attack.ani";
    auto* cache = GetSubsystem<ResourceCache>();
    if (cache && cache->Exists(plain))
        return plain;

    // Randomize between headbutt and kick
    if (Random(1.0f) < 0.5f)
        return GetModelDir() + prefix + "Attack_Headbutt.ani";
    return GetModelDir() + prefix + "Attack_Kick.ani";
}

String LandAnimal::GetHitReactAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;
    // Randomize left/right flinch — fall back to Left if Right missing
    if (Random(1.0f) < 0.5f)
        return GetModelDir() + prefix + "Idle_HitReact_Left.ani";
    auto* cache = GetSubsystem<ResourceCache>();
    String right = GetModelDir() + prefix + "Idle_HitReact_Right.ani";
    if (cache && cache->Exists(right))
        return right;
    return GetModelDir() + prefix + "Idle_HitReact_Left.ani";
}

String LandAnimal::GetLookAnim() const
{
    String prefix = GetAnimPrefix();
    if (prefix.Empty())
        return String::EMPTY;
    return GetModelDir() + prefix + "Idle_Headlow.ani";
}

void LandAnimal::FixedUpdate(float timeStep)
{

    stateTimer_ -= timeStep;

    // Read server-replicated AI state from node Vars (set by AuthServer, replicated by Urho3D).
    if (node_ && spawnId_ != 0)
    {
        const Variant& aiVar = node_->GetVar("AIState");
        if (!aiVar.IsEmpty())
        {
            auto newState = static_cast<CreatureState>(aiVar.GetI32());
            float moveSpeed = node_->GetVar("MoveSpeed").GetFloat();
            // Use server's target position for client-side lerp, not the node's
            // own world position (which is set by replication — chasing own tail).
            const Variant& targetVar = node_->GetVar("TargetPos");
            Vector3 targetPos = targetVar.IsEmpty() ? node_->GetWorldPosition() : targetVar.GetVector3();
            ApplyServerState(newState, targetPos, moveSpeed);

            // Juvenile scaling — offspring render at 40%-100% of adult size.
            // Creature::Start() sets the adult-normalized scale. Growth modulates it.
            const Variant& growthVar = node_->GetVar("GrowthProgress");
            if (!growthVar.IsEmpty())
            {
                float gp = growthVar.GetFloat();
                if (gp < 1.0f)
                {
                    auto* mdl = node_->GetComponent<AnimatedModel>(true);
                    if (mdl)
                    {
                        BoundingBox bb = mdl->GetBoundingBox();
                        float longest = Max(bb.Size().x_, Max(bb.Size().y_, bb.Size().z_));
                        if (longest > 0.001f)
                            node_->SetScale((GetDesiredSize() / longest) * (0.4f + 0.6f * gp));
                    }
                }
            }
        }
        else if (spawnId_ == 50)
        {
            static bool warned = false;
            if (!warned) { URHO3D_LOGWARNING("[Trace50] AIState Var is EMPTY"); warned = true; }
        }
    }

    // Retry animation apply if AnimationController wasn't ready on first attempt
    if (pendingAnimApply_ && node_)
    {
        auto* animCtrl = node_->GetComponent<AnimationController>(true);
        if (animCtrl)
        {
            URHO3D_LOGINFOF("[AnimRetry] %s found AnimationController — applying state %d",
                node_->GetName().CString(), (int)state_);
            OnStateEnter(state_);
        }
    }

    // Server-driven mode — lerp toward server position, face movement direction.
    // Nodes are LOCAL (not replicated), so client must move them toward serverPos_.
    if (serverDriven_ && node_)
    {
        Vector3 current = node_->GetWorldPosition();
        Vector3 delta = serverPos_ - current;
        delta.y_ = 0.0f;
        float dist = delta.Length();

        if (dist > 0.1f)
        {
            float step = Max(serverMoveSpeed_, 3.0f) * timeStep;
            if (step > dist)
                step = dist;
            Vector3 newPos = current + delta * (step / dist);

            if (terrain_)
            {
                float terrainY = terrain_->GetHeight(newPos);
                // waterLevel_ (5.5) is the head-submersion threshold.
                // Visual water surface is ~0.5 below that. Place model origin
                // (feet) about 0.8m below the visual surface so upper body
                // shows above water — matching possessed swim positioning.
                if (terrainY < waterLevel_ - 0.5f)
                {
                    float swimY = waterLevel_ - 1.3f;
                    // Don't swim below terrain floor
                    newPos.y_ = Max(swimY, terrainY + 0.2f);
                }
                else
                {
                    newPos.y_ = terrainY;
                }
            }

            node_->SetWorldPosition(newPos);

            delta.Normalize();
            node_->SetWorldDirection(delta);
        }

        PostMovementUpdate(timeStep);
        return;
    }

    // Vitals decay (humans only — animals return false)
    if (HasVitals())
    {
        // Sample sun altitude from the scene's directional light
        float sunAlt = 0.5f;
        Scene* scene = GetScene();
        if (scene)
        {
            Node* dl = scene->GetChild("DirectionalLight", true);
            if (dl)
                sunAlt = -dl->GetDirection().y_;  // -dir.y > 0 = sun above
        }
        UpdateVitals(timeStep, sunAlt);
    }

    // Vision scan — rate-limited, staggered per-animal.
    // Runs before the state machine so a freshly-spotted predator pre-empts
    // the current state (idle grazer bolts immediately, etc.).
    visionScanTimer_ -= timeStep;
    if (visionScanTimer_ <= 0.0f)
    {
        // Stagger: spread scans across frames so 50 animals don't all scan
        // on the same tick. Offset by staggerID modulo interval.
        visionScanTimer_ = VISION_SCAN_INTERVAL + (float)(staggerID_ % 5) * 0.1f;
        if (VisionScan())
            return;  // state changed (e.g. FLEE) — skip the rest of this tick
    }

    switch (state_)
    {
    case CREATURE_IDLE:
        if (stateTimer_ <= 0.0f)
        {
            // Vital-driven behavior: pick the most depleted need
            CreatureState needPick = HasVitals() ? PickNeedState() : CREATURE_IDLE;
            if (needPick != CREATURE_IDLE)
            {
                SetState(needPick);
            }
            // Scavenger probe BEFORE the random ambient picker — a wolf with
            // a fresh kill nearby investigates instead of wandering aimlessly.
            // TryEnterScavenge is a no-op for non-scavenger species.
            else if (TryEnterScavenge())
            {
                // State already set to CREATURE_SCAVENGE; nothing else to do.
            }
            // Phase 4: herbivores forage when local food is poor
            else if (ecosystem_ && !IsPredator() && (GetFoodGrassWeight() + GetFoodShrubWeight()) > 0.0f)
            {
                Vector3 pos = node_->GetWorldPosition();
                float localFood = ecosystem_->SampleFoodValue(pos.x_, pos.z_,
                    GetFoodGrassWeight(), GetFoodShrubWeight());

                if (localFood < 0.15f)
                {
                    // Poor food here — forage toward best food in range
                    if (PickForageTarget())
                    {
                        SetState(CREATURE_FORAGE);
                    }
                    else
                    {
                        PickWanderTarget();
                        SetState(CREATURE_WANDER);
                    }
                }
                else
                {
                    // Adequate food — normal ambient behavior, eat more often in rich areas
                    float eatChance = Clamp(localFood * 0.5f, 0.15f, 0.40f);
                    float roll = Random(1.0f);
                    if (!GetEatAnim().Empty() && roll < eatChance)
                        SetState(CREATURE_EAT);
                    else if (!GetLookAnim().Empty() && roll < eatChance + 0.15f)
                        SetState(CREATURE_LOOK);
                    else if (!GetSitAnim().Empty() && roll < eatChance + 0.25f)
                        SetState(CREATURE_SIT);
                    else
                    {
                        PickWanderTarget();
                        SetState(CREATURE_WANDER);
                    }
                }
            }
            else
            {
                // Predators and animals without ecosystem — random ambient behavior
                float roll = Random(1.0f);
                if (!GetEatAnim().Empty() && roll < 0.20f)
                    SetState(CREATURE_EAT);
                else if (!GetLookAnim().Empty() && roll < 0.35f)
                    SetState(CREATURE_LOOK);
                else if (!GetSitAnim().Empty() && roll < 0.45f)
                    SetState(CREATURE_SIT);
                else
                {
                    PickWanderTarget();
                    SetState(CREATURE_WANDER);
                }
            }
        }
        break;

    case CREATURE_EAT:
        // Phase 4: grazing reduces grass density
        if (ecosystem_ && (GetFoodGrassWeight() + GetFoodShrubWeight()) > 0.0f)
        {
            Vector3 pos = node_->GetWorldPosition();
            ecosystem_->ReduceGrassDensity(pos.x_, pos.z_, 0.5f * timeStep * 60.0f);
        }
        // fall through
    case CREATURE_LOOK:
    case CREATURE_SIT:
    case CREATURE_GREET:
    case CREATURE_CROUCH:
        if (stateTimer_ <= 0.0f)
        {
            if (HasVitals())
                OnActionComplete(state_);
            SetState(CREATURE_IDLE);
        }
        break;

    case CREATURE_SLEEP:
        if (stateTimer_ <= 0.0f)
        {
            if (HasVitals())
                OnActionComplete(state_);
            SetState(CREATURE_IDLE);
        }
        break;

    case CREATURE_WANDER:
    {
        // Wall collision check before moving
        Vector3 pos = node_->GetWorldPosition();
        Vector3 dir = wanderTarget_ - pos;
        dir.y_ = 0.0f;
        float dist = dir.Length();

        if (dist > 0.1f)
        {
            dir.Normalize();
            Vector3 testPos = pos + dir * GetWanderSpeed() * timeStep;
            if (IsBlockedByWall(pos, testPos))
            {
                SetState(CREATURE_IDLE);
                break;
            }

            // Don't walk into water
            if (terrain_)
            {
                float groundY = terrain_->GetHeight(testPos);
                if (groundY < waterLevel_)
                {
                    SetState(CREATURE_IDLE);
                    break;
                }
            }
        }

        MoveToward(wanderTarget_, GetWanderSpeed(), timeStep);

        // Check if reached target
        pos = node_->GetWorldPosition();
        Vector3 diff = wanderTarget_ - pos;
        diff.y_ = 0.0f;
        if (diff.Length() < 1.0f || stateTimer_ <= 0.0f)
        {
            // Scavengers passing near a fresh kill investigate before
            // resetting to idle. Non-scavengers fall straight to IDLE.
            if (!TryEnterScavenge())
                SetState(CREATURE_IDLE);
        }
        break;
    }

    case CREATURE_FORAGE:
    {
        // Walk slowly toward best food source. On arrival, eat.
        Vector3 pos = node_->GetWorldPosition();
        Vector3 dir = wanderTarget_ - pos;
        dir.y_ = 0.0f;
        float dist = dir.Length();

        if (dist > 1.5f)
        {
            dir.Normalize();
            float forageSpeed = GetWanderSpeed() * 0.6f;
            Vector3 testPos = pos + dir * forageSpeed * timeStep;
            if (IsBlockedByWall(pos, testPos))
            {
                SetState(CREATURE_IDLE);
                break;
            }
            if (terrain_)
            {
                float groundY = terrain_->GetHeight(testPos);
                if (groundY < waterLevel_)
                {
                    SetState(CREATURE_IDLE);
                    break;
                }
            }
            MoveToward(wanderTarget_, forageSpeed, timeStep);
        }

        // Arrived or timer expired — eat at this spot
        if (dist <= 1.5f || stateTimer_ <= 0.0f)
            SetState(CREATURE_EAT);
        break;
    }

    case CREATURE_ALERT:
    {
        // Face the threat during the stare
        Vector3 toThreat = alertThreatPos_ - node_->GetWorldPosition();
        toThreat.y_ = 0.0f;
        if (toThreat.LengthSquared() > 0.01f)
            node_->SetWorldDirection(toThreat.Normalized());

        // When timer expires, bolt.
        if (stateTimer_ <= 0.0f)
        {
            Vector3 myPos = node_->GetWorldPosition();
            Vector3 awayDir = myPos - alertThreatPos_;
            awayDir.y_ = 0.0f;
            if (awayDir.LengthSquared() < 0.01f)
                awayDir = Vector3(Random(-1.0f, 1.0f), 0.0f, Random(-1.0f, 1.0f));
            awayDir.Normalize();
            wanderTarget_ = myPos + awayDir * GetFleeDistance();
            SetState(CREATURE_FLEE);
        }
        break;
    }

    case CREATURE_VICTORY:
        // Stand still and celebrate. Timer expires → idle.
        if (stateTimer_ <= 0.0f)
            SetState(CREATURE_IDLE);
        break;

    case CREATURE_FLEE:
        if (stateTimer_ <= 0.0f)
            SetState(CREATURE_IDLE);
        else
            MoveToward(wanderTarget_, GetFleeSpeed(), timeStep);
        break;

    case CREATURE_TRAPPED:
        // Frozen — no movement, no AI transitions, no timer countdown.
        // DEX affects trap-escape check speed: high DEX creatures wriggle free faster.
        // Server ultimately decides release, but client ticks a struggle timer for
        // visual feedback and potential offline-mode self-release.
        {
            float escapeRate = 1.0f + stats_.DexMod() * 0.15f;  // +15% per DEX mod
            combatTimer_ += timeStep * Clamp(escapeRate, 0.5f, 2.5f);
        }
        break;

    case CREATURE_DIE:
        if (!deathSoundPlayed_)
        {
            PlayDeath();
            deathSoundPlayed_ = true;
        }
        if (stateTimer_ <= 0.0f)
        {
            // Death animation finished. Become a harvestable corpse instead of
            // teleport-respawning. Capture Y now so the fade sink has an anchor
            // independent of any terrain snap that may run on this frame.
            corpseStartY_ = node_->GetWorldPosition().y_;
            SetState(CREATURE_CORPSE);

            // Phase 4 polish: subtle dust haze attached to the corpse so it
            // sinks with the body during the fade. Particle node is a child
            // of the creature node — it dies automatically when the corpse
            // node is removed at the end of the sink fade. Failure to load
            // the effect is non-fatal: the corpse just lacks the visual.
            auto* cache = GetSubsystem<ResourceCache>();
            if (cache)
            {
                auto* fx = cache->GetResource<ParticleEffect>("Particle/CorpseHaze.xml");
                if (fx)
                {
                    Node* hazeNode = node_->CreateChild("CorpseHaze");
                    auto* emitter = hazeNode->CreateComponent<ParticleEmitter>();
                    emitter->SetEffect(fx);
                    emitter->SetEmitting(true);
                }
            }
        }
        break;

    case CREATURE_CORPSE:
        // Phase 1 (visible): stateTimer_ counts down from CORPSE_DURATION → 0.
        // Phase 2 (sink fade): stateTimer_ goes negative, body sinks at a rate
        // proportional to -stateTimer_ / CORPSE_FADE_DURATION until it has dropped
        // CORPSE_SINK_DEPTH metres, then the node is removed from the scene.
        if (stateTimer_ < 0.0f)
        {
            float fadeProgress = Min(-stateTimer_ / CORPSE_FADE_DURATION, 1.0f);
            Vector3 pos = node_->GetWorldPosition();
            pos.y_ = corpseStartY_ - fadeProgress * CORPSE_SINK_DEPTH;
            node_->SetWorldPosition(pos);

            if (fadeProgress >= 1.0f)
            {
                node_->Remove();
                return;
            }
        }
        break;

    case CREATURE_HUNT:
    {
        // Predator chasing prey. Update target position each frame from the
        // tracked node. If prey is gone or out of range, memory timer ticks
        // down; when it expires the predator gives up.
        Node* prey = huntTarget_.Get();
        if (!prey)
        {
            // Prey removed (killed, despawned) — give up immediately
            huntTarget_.Reset();
            SetState(CREATURE_IDLE);
            break;
        }

        wanderTarget_ = prey->GetWorldPosition();
        Vector3 pos = node_->GetWorldPosition();
        Vector3 toPrey = wanderTarget_ - pos;
        toPrey.y_ = 0.0f;
        float dist = toPrey.Length();

        // Can we still see the prey? If yes, refresh the memory timer.
        auto* preyCreature = prey->GetComponent<Creature>();
        if (preyCreature && CanSee(preyCreature))
            stateTimer_ = 5.0f;  // refresh memory

        // Prey dead? Celebrate the kill.
        if (preyCreature)
        {
            CreatureState preyState = preyCreature->GetState();
            if (preyState == CREATURE_DIE || preyState == CREATURE_CORPSE)
            {
                huntTarget_.Reset();
                if (!GetVictoryAnim().Empty())
                    SetState(CREATURE_VICTORY);
                else
                    SetState(CREATURE_IDLE);
                break;
            }
        }

        // Memory expired — lost the prey
        if (stateTimer_ <= 0.0f)
        {
            huntTarget_.Reset();
            SetState(CREATURE_IDLE);
            break;
        }

        // Close enough to attack? Transition to FIGHT.
        if (dist < 2.0f)
        {
            SetState(CREATURE_FIGHT);
            break;
        }

        // Wall/water guards
        Vector3 dir = toPrey;
        dir.Normalize();
        Vector3 testPos = pos + dir * GetFleeSpeed() * timeStep;
        if (IsBlockedByWall(pos, testPos))
        {
            huntTarget_.Reset();
            SetState(CREATURE_IDLE);
            break;
        }
        if (terrain_)
        {
            float groundY = terrain_->GetHeight(testPos);
            if (groundY < waterLevel_)
            {
                huntTarget_.Reset();
                SetState(CREATURE_IDLE);
                break;
            }
        }

        // Chase at run speed (same as flee speed — full gallop)
        MoveToward(wanderTarget_, GetFleeSpeed(), timeStep);
        break;
    }

    case CREATURE_FIGHT:
    {
        // Melee attack cycle. Face the prey, play attack anim, deal damage on
        // timer expiry, then re-evaluate: chase again if prey fled, idle if dead.
        Node* prey = huntTarget_.Get();
        if (!prey)
        {
            huntTarget_.Reset();
            SetState(CREATURE_IDLE);
            break;
        }

        // Face prey
        Vector3 toPrey = prey->GetWorldPosition() - node_->GetWorldPosition();
        toPrey.y_ = 0.0f;
        if (toPrey.LengthSquared() > 0.01f)
            node_->SetWorldDirection(toPrey.Normalized());

        if (stateTimer_ <= 0.0f)
        {
            // Attack cycle done — check if prey is still alive and in range
            auto* preyCreature = prey->GetComponent<Creature>();
            float dist = toPrey.Length();

            if (!preyCreature || preyCreature->GetState() == CREATURE_DIE ||
                preyCreature->GetState() == CREATURE_CORPSE)
            {
                huntTarget_.Reset();
                // Brief victory anim if available, then idle
                String victoryAnim = GetVictoryAnim();
                if (!victoryAnim.Empty())
                {
                    auto* animCtrl = node_->GetComponent<AnimationController>(true);
                    if (animCtrl)
                        animCtrl->PlayExclusive(victoryAnim, 0, false, 0.2f);
                }
                SetState(CREATURE_IDLE);
            }
            else if (dist > 3.0f)
            {
                // Prey fled — resume chase
                SetState(CREATURE_HUNT);
            }
            else
            {
                // Still in range — attack again
                SetState(CREATURE_FIGHT);
            }
        }
        break;
    }

    case CREATURE_SWIM:
    {
        // Server-driven NPCs follow the server position through water — the
        // server handles swim logic, client just interpolates (handled above).
        // Local-AI animals panic-swim toward the nearest shore.
        if (serverDriven_)
        {
            // Server handles movement — check if we've reached dry land
            if (terrain_)
            {
                float groundY = terrain_->GetHeight(node_->GetWorldPosition());
                if (groundY >= waterLevel_)
                    SetState(CREATURE_IDLE);
            }
            break;
        }

        // Local AI: paddle toward the nearest shore. Land animals don't
        // belong in water; this state keeps them alive while they escape.
        if (terrain_)
        {
            Vector3 pos = node_->GetWorldPosition();

            // Search outward in 8 directions for dry land
            if (stateTimer_ <= 0.0f || wanderTarget_.y_ < waterLevel_)
            {
                float bestDist = M_INFINITY;
                Vector3 bestTarget = pos;
                for (int a = 0; a < 8; ++a)
                {
                    float angle = a * 45.0f * M_DEGTORAD;
                    Vector3 probe = pos + Vector3(cosf(angle) * 15.0f, 0.0f, sinf(angle) * 15.0f);
                    float h = terrain_->GetHeight(probe);
                    if (h >= waterLevel_)
                    {
                        float d = (probe - pos).LengthSquared();
                        if (d < bestDist)
                        {
                            bestDist = d;
                            bestTarget = probe;
                            bestTarget.y_ = h;
                        }
                    }
                }
                wanderTarget_ = bestTarget;
                stateTimer_ = 5.0f;  // re-scan interval
            }

            MoveToward(wanderTarget_, GetWanderSpeed() * 0.5f, timeStep);

            // Keep Y at swim level (SnapToTerrain is skipped for CREATURE_SWIM)
            Vector3 swimPos = node_->GetWorldPosition();
            float groundY = terrain_->GetHeight(swimPos);
            if (groundY < waterLevel_ - 0.5f)
            {
                swimPos.y_ = Max(waterLevel_ - 1.3f, groundY + 0.2f);
                node_->SetWorldPosition(swimPos);
            }

            // Check if we reached dry land
            if (groundY >= waterLevel_)
                SetState(CREATURE_IDLE);
        }
        break;
    }

    case CREATURE_SCAVENGE:
    {
        // Two phases gated by scavengeArrived_:
        //   travel: walk toward wanderTarget_ (the scent position), holding
        //           the eat timer at full so it doesn't drain en route
        //   eat:    timer counts down naturally; on expiry return to IDLE
        Vector3 pos = node_->GetWorldPosition();
        Vector3 toTarget = wanderTarget_ - pos;
        toTarget.y_ = 0.0f;
        float dist = toTarget.Length();

        if (!scavengeArrived_)
        {
            // Travel: re-pin the eat timer each frame so it stays at the full
            // SCAVENGER_EAT_DURATION value until we actually arrive. The base
            // FixedUpdate decrement at the top still runs; this overrides it.
            stateTimer_ = SCAVENGER_EAT_DURATION;

            if (dist <= SCAVENGER_ARRIVAL_DIST)
            {
                // Arrival: switch to eat anim and start the timer countdown.
                scavengeArrived_ = true;
                auto* animCtrl = node_->GetComponent<AnimationController>(true);
                if (animCtrl && !GetEatAnim().Empty())
                    animCtrl->PlayExclusive(GetEatAnim(), 0, true, 0.3f);
            }
            else
            {
                // Wall + water guards from CREATURE_WANDER apply here too —
                // a scavenger shouldn't path through walls or into rivers.
                Vector3 dir = toTarget;
                dir.Normalize();
                Vector3 testPos = pos + dir * GetWanderSpeed() * timeStep;
                if (IsBlockedByWall(pos, testPos))
                {
                    scavengeArrived_ = false;
                    SetState(CREATURE_IDLE);
                    break;
                }
                if (terrain_)
                {
                    float groundY = terrain_->GetHeight(testPos);
                    if (groundY < waterLevel_)
                    {
                        scavengeArrived_ = false;
                        SetState(CREATURE_IDLE);
                        break;
                    }
                }
                MoveToward(wanderTarget_, GetWanderSpeed(), timeStep);
            }
        }
        else
        {
            // Eat phase: just wait out the timer. When it expires, the kill
            // is "consumed" (visually) and we return to wander.
            if (stateTimer_ <= 0.0f)
            {
                scavengeArrived_ = false;
                SetState(CREATURE_IDLE);
            }
        }
        break;
    }

    case CREATURE_FISH:
    {
        // Fishing at water's edge. Timer was set in SetState with DEX scaling.
        // When timer expires, catch is complete — return to idle.
        // DEX already reduced the base timer in SetState; no additional logic needed here.
        if (stateTimer_ <= 0.0f)
            SetState(CREATURE_IDLE);
        break;
    }
    }

    PostMovementUpdate(timeStep);
}

void LandAnimal::PostMovementUpdate(float timeStep)
{
    static unsigned frameCounter = 0;
    ++frameCounter;

    // Footstep sound — distance accumulator while walking/running
    if (node_ && (state_ == CREATURE_WANDER || state_ == CREATURE_FLEE ||
        state_ == CREATURE_HUNT || state_ == CREATURE_SCAVENGE ||
        state_ == CREATURE_FORAGE))
    {
        Vector3 pos = node_->GetWorldPosition();
        if (!footstepPosInitialized_)
        {
            lastFootstepPos_ = pos;
            footstepPosInitialized_ = true;
        }
        else
        {
            Vector3 diff = pos - lastFootstepPos_;
            diff.y_ = 0.0f;
            if (diff.LengthSquared() > FOOTSTEP_DISTANCE * FOOTSTEP_DISTANCE)
            {
                PlayFootstep();
                lastFootstepPos_ = pos;
            }
        }
    }

    // Idle vocalization — random call every 10-30 seconds while idle
    if (state_ == CREATURE_IDLE || state_ == CREATURE_EAT || state_ == CREATURE_SIT)
    {
        vocalizationTimer_ -= timeStep;
        if (vocalizationTimer_ <= 0.0f)
        {
            PlayVocalization();
            vocalizationTimer_ = Random(10.0f, 30.0f);
        }
    }

    // Corpses don't get terrain-snapped (would fight the sink fade) and don't
    // run drowning checks (already dead). Skip both for any non-living state.
    // Server-driven creatures get their Y from the server — SnapToTerrain would
    // fight the replicated position and cause jitter.
    // Swimming creatures float at water level — terrain snap would pull them to the lake floor.
    if (state_ != CREATURE_CORPSE && state_ != CREATURE_SWIM && !serverDriven_)
        SnapToTerrain();

    // Drowning: only when the water covers the head bone (anatomical head,
    // where the air goes in). Body / feet can be wet — that's swimming, not
    // drowning. Head bone is found structurally, see FindHeadBoneIndex.
    float drownTime = GetDrownTime();
    if (drownTime >= 0.0f && state_ != CREATURE_DIE && state_ != CREATURE_CORPSE)
    {
        Vector3 pos = node_->GetWorldPosition();

        // Lazy one-shot discovery — model is attached on a child node and
        // isn't guaranteed ready in Start().
        if (!headBoneSearched_)
        {
            auto* mdl = node_->GetComponent<AnimatedModel>(true);
            if (mdl)
                headBoneIndex_ = FindHeadBoneIndex(mdl->GetSkeleton(), GetTypeName());
            headBoneSearched_ = true;
        }

        float headY = pos.y_;
        if (headBoneIndex_ >= 0)
        {
            headY = GetHeadWorldPos().y_;
        }
        else
        {
            // Stopgap when no head bone could be discovered (skeleton too
            // small / not yet attached). Bbox top is anatomically wrong but
            // it's better than the foot Y.
            auto* mdl = node_->GetComponent<AnimatedModel>(true);
            if (mdl)
                headY = mdl->GetWorldBoundingBox().max_.y_;
        }

        if (headY < waterLevel_)
        {
            drownTimer_ += timeStep;
            if (state_ != CREATURE_FLEE && terrain_ && (frameCounter % 3 == staggerID_ % 3))
            {
                float bestY = -M_INFINITY;
                Vector3 bestTarget = pos;
                for (int i = 0; i < 4; ++i)
                {
                    float angle = (float)i * 90.0f;
                    float x = pos.x_ + 10.0f * Cos(angle);
                    float z = pos.z_ + 10.0f * Sin(angle);
                    float y = terrain_->GetHeight(Vector3(x, 0.0f, z));
                    if (y > bestY)
                    {
                        bestY = y;
                        bestTarget = Vector3(x, y, z);
                    }
                }
                wanderTarget_ = bestTarget;
                SetState(CREATURE_FLEE);
            }
            // Phase 5d: server-authoritative creatures die via MSG_CREATURE_DEATH,
            // not client-side timer. Keep the flee behavior as prediction.
            if (drownTimer_ >= drownTime && spawnId_ == 0)
                SetState(CREATURE_DIE);
        }
        else
        {
            drownTimer_ = 0.0f;
        }
    }
}

bool LandAnimal::VisionScan()
{
    if (!spatialHash_ || !node_ || GetVisionRange() <= 0.0f)
        return false;

    // Don't scan while dead, corpse, trapped, already fleeing, or already alert
    if (state_ == CREATURE_DIE || state_ == CREATURE_CORPSE ||
        state_ == CREATURE_TRAPPED || state_ == CREATURE_FLEE ||
        state_ == CREATURE_ALERT)
        return false;

    Vector3 myPos = node_->GetWorldPosition();
    float range = GetVisionRange();

    Vector<LandAnimal*> neighbors;
    spatialHash_->Query(myPos, range, neighbors);

    // --- Herbivore: flee from visible predators ---
    if (!IsPredator())
    {
        for (unsigned i = 0; i < neighbors.Size(); ++i)
        {
            LandAnimal* other = neighbors[i];
            if (other == this) continue;
            if (!other->IsPredator()) continue;
            // Skip dead/corpse predators — not a threat
            CreatureState otherState = other->GetState();
            if (otherState == CREATURE_DIE || otherState == CREATURE_CORPSE)
                continue;
            if (!CanSee(other))
                continue;

            // Threat spotted — freeze and stare before fleeing
            alertThreatPos_ = other->GetNode()->GetWorldPosition();

            // Face the threat
            Vector3 toThreat = alertThreatPos_ - myPos;
            toThreat.y_ = 0.0f;
            if (toThreat.LengthSquared() > 0.01f)
                node_->SetWorldDirection(toThreat.Normalized());

            SetState(CREATURE_ALERT);
            return true;
        }
    }

    // --- Predator: hunt visible prey ---
    // Scavenge check already ran before VisionScan (easy meal wins).
    // Only hunt if not already scavenging and not already hunting.
    if (IsPredator() && state_ != CREATURE_SCAVENGE && state_ != CREATURE_HUNT)
    {
        LandAnimal* bestPrey = nullptr;
        float bestDistSq = M_INFINITY;

        for (unsigned i = 0; i < neighbors.Size(); ++i)
        {
            LandAnimal* other = neighbors[i];
            if (other == this) continue;
            if (other->IsPredator()) continue;  // don't hunt other predators
            CreatureState otherState = other->GetState();
            if (otherState == CREATURE_DIE || otherState == CREATURE_CORPSE)
                continue;
            // Only hunt smaller animals — predators ignore similar-bulk animals
            if (other->GetDesiredSize() >= GetDesiredSize())
                continue;
            if (!CanSee(other))
                continue;

            float distSq = (other->GetNode()->GetWorldPosition() - myPos).LengthSquared();
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestPrey = other;
            }
        }

        if (bestPrey)
        {
            huntTarget_ = bestPrey->GetNode();
            wanderTarget_ = bestPrey->GetNode()->GetWorldPosition();
            SetState(CREATURE_HUNT);
            return true;
        }
    }

    // --- Stag/Bull: charge similar-bulk intruders ---
    float mySize = GetDesiredSize();
    if (mySize >= 1.8f && !IsPredator())
    {
        for (unsigned i = 0; i < neighbors.Size(); ++i)
        {
            LandAnimal* other = neighbors[i];
            if (other == this) continue;
            CreatureState otherState = other->GetState();
            if (otherState == CREATURE_DIE || otherState == CREATURE_CORPSE)
                continue;
            float otherSize = other->GetDesiredSize();
            if (Abs(mySize - otherSize) > BULK_SIMILARITY_THRESHOLD)
                continue;
            if (!CanSee(other))
                continue;

            // Similar-bulk animal spotted — charge!
            huntTarget_ = other->GetNode();
            wanderTarget_ = other->GetNode()->GetWorldPosition();
            SetState(CREATURE_FIGHT);
            return true;
        }
    }

    return false;
}

bool LandAnimal::PickForageTarget()
{
    if (!ecosystem_ || !node_)
        return false;

    float gw = GetFoodGrassWeight();
    float sw = GetFoodShrubWeight();
    if (gw + sw <= 0.0f)
        return false;

    float radius = GetWanderRadius();
    Vector3 pos = node_->GetWorldPosition();
    float bestFood = -1.0f;
    Vector3 bestPos = pos;

    // Sample 8 candidate positions, pick the richest food source
    for (int i = 0; i < 8; ++i)
    {
        float x = pos.x_ + Random(-radius, radius);
        float z = pos.z_ + Random(-radius, radius);
        float y = terrain_ ? terrain_->GetHeight(Vector3(x, 0.0f, z)) : 0.0f;
        if (y <= waterLevel_)
            continue;

        float food = ecosystem_->SampleFoodValue(x, z, gw, sw);
        if (food > bestFood)
        {
            bestFood = food;
            bestPos = Vector3(x, y, z);
        }
    }

    if (bestFood <= 0.0f)
        return false;

    wanderTarget_ = bestPos;
    stateTimer_ = 10.0f;  // max forage travel time before giving up
    return true;
}

void LandAnimal::PickWanderTarget()
{
    float radius = GetWanderRadius();

    // 25% chance: head toward the water's edge to drink
    bool seekWater = (Random(1.0f) < 0.25f);

    if (seekWater && terrain_)
    {
        Vector3 pos = node_->GetWorldPosition();
        float bestDist = M_INFINITY;
        Vector3 bestTarget = pos;
        bool found = false;

        for (int i = 0; i < 12; ++i)
        {
            float angle = Random(0.0f, 360.0f);
            float dist = Random(5.0f, radius);
            float x = pos.x_ + dist * Cos(angle);
            float z = pos.z_ + dist * Sin(angle);
            float y = terrain_->GetHeight(Vector3(x, 0.0f, z));

            if (y > waterLevel_ && y < waterLevel_ + 1.0f)
            {
                float d = Vector2(x - pos.x_, z - pos.z_).Length();
                if (d < bestDist)
                {
                    bestDist = d;
                    bestTarget = Vector3(x, y, z);
                    found = true;
                }
            }
        }

        if (found)
        {
            wanderTarget_ = bestTarget;
            return;
        }
    }

    // Phase 4: food-biased wander for herbivores
    float gw = GetFoodGrassWeight();
    float sw = GetFoodShrubWeight();
    if (ecosystem_ && (gw + sw) > 0.0f)
    {
        Vector3 bestPos = node_->GetWorldPosition();
        float bestFood = -1.0f;

        for (int i = 0; i < 6; ++i)
        {
            float x = homePos_.x_ + Random(-radius, radius);
            float z = homePos_.z_ + Random(-radius, radius);
            float y = terrain_ ? terrain_->GetHeight(Vector3(x, 0.0f, z)) : 0.0f;
            if (y <= waterLevel_)
                continue;

            float food = ecosystem_->SampleFoodValue(x, z, gw, sw);
            if (food > bestFood)
            {
                bestFood = food;
                bestPos = Vector3(x, y, z);
            }
        }

        if (bestFood >= 0.0f)
        {
            wanderTarget_ = bestPos;
            return;
        }
    }

    // Normal wander: pick dry land (fallback for predators or no ecosystem)
    for (int tries = 0; tries < 8; ++tries)
    {
        float shrink = (tries > 3) ? 0.5f : 1.0f;
        float x = homePos_.x_ + Random(-radius * shrink, radius * shrink);
        float z = homePos_.z_ + Random(-radius * shrink, radius * shrink);
        float y = terrain_ ? terrain_->GetHeight(Vector3(x, 0.0f, z)) : 0.0f;

        if (y > waterLevel_)
        {
            wanderTarget_ = Vector3(x, y, z);
            return;
        }
    }

    wanderTarget_ = node_->GetWorldPosition();
}

void LandAnimal::SnapToTerrain()
{
    if (!terrain_)
        return;

    // Phase 2: skip terrain snap when body is dynamic (physics-driven possessed mode).
    // The physics engine handles ground contact via collision response.
    auto* body = node_->GetComponent<RigidBody>();
    if (body && !body->IsKinematic())
        return;

    Vector3 pos = node_->GetWorldPosition();

    float dx = pos.x_ - cachedHeightPos_.x_;
    float dz = pos.z_ - cachedHeightPos_.z_;
    if (dx * dx + dz * dz > 0.25f)
    {
        cachedHeight_ = terrain_->GetHeight(pos);
        cachedHeightPos_ = pos;
    }

    if (Abs(pos.y_ - cachedHeight_) > 0.01f)
    {
        pos.y_ = cachedHeight_;
        node_->SetWorldPosition(pos);

        // Phase 0: keep kinematic body in sync after terrain snap
        if (body)
            body->SetPosition(pos);
    }
}

bool LandAnimal::TryEnterScavenge()
{
    // Cheap early-out: most species are not scavengers.
    if (!IsScavenger() || !node_)
        return false;

    Vector3 pos = node_->GetWorldPosition();
    const ScentMarker* nearest = ScentRegistry::FindNearest(pos, SCENT_DETECTION_RADIUS);
    if (!nearest)
        return false;

    wanderTarget_ = nearest->position;
    scavengeArrived_ = false;   // start fresh in travel phase
    SetState(CREATURE_SCAVENGE);
    return true;
}

void LandAnimal::Respawn()
{
    if (!terrain_)
        return;

    float radius = GetWanderRadius();
    Vector3 spawnPos = homePos_;

    for (int tries = 0; tries < 20; ++tries)
    {
        float x = homePos_.x_ + Random(-radius, radius);
        float z = homePos_.z_ + Random(-radius, radius);
        float y = terrain_->GetHeight(Vector3(x, 0.0f, z));
        if (y > waterLevel_)
        {
            spawnPos = Vector3(x, y, z);
            break;
        }
    }

    node_->SetWorldPosition(spawnPos);
    drownTimer_ = 0.0f;
    state_ = CREATURE_IDLE;
    stateTimer_ = Random(GetMinIdleDuration(), GetMaxIdleDuration());
    OnStateEnter(CREATURE_IDLE);
}

bool LandAnimal::IsBlockedByWall(const Vector3& from, const Vector3& to) const
{
    if (!buildingSystem_)
        return false;

    int creatureId = GetCreatureId();
    if (creatureId <= 0)
        return false;

    const auto& buildings = buildingSystem_->GetPlacedBuildings();

    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        const PlacedBuilding& pb = buildings[i];

        if (pb.snapType != "wall" && pb.snapType != "gate" && pb.snapType != "corner")
            continue;

        if (pb.snapType == "gate" && pb.gateOpen)
            continue;

        float dx = to.x_ - pb.position.x_;
        float dz = to.z_ - pb.position.z_;
        float radius = pb.footprintX * 0.6f;
        float r2 = radius * radius;
        float dist2 = dx * dx + dz * dz;
        if (dist2 > r2)
            continue;

        float dxFrom = from.x_ - pb.position.x_;
        float dzFrom = from.z_ - pb.position.z_;
        if (dxFrom * dxFrom + dzFrom * dzFrom <= r2)
            continue;

        // DB-driven wall_strength lookup
        if (gameDB_ && gameDB_->DoesWallBlock(pb.buildingTypeId, creatureId))
            return true;
    }

    return false;
}
