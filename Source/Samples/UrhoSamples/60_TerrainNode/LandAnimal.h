// LandAnimal — terrain-roaming creature.
// Adds: terrain snap, water avoidance, drowning, shore seeking, wall collision.

#pragma once

#include "Creature.h"
#include <Urho3D/Audio/Sound.h>
#include <Urho3D/Core/Context.h>

namespace Urho3D { class Skeleton; }

class BuildingSystem;
class LandAnimalSpatialHash;

class LandAnimal : public Creature
{
    URHO3D_OBJECT(LandAnimal, Creature);

public:
    explicit LandAnimal(Context* context);

    void Start() override;
    void FixedUpdate(float timeStep) override;

    /// Set building system reference for wall collision checks.
    void SetBuildingSystem(BuildingSystem* bs) { buildingSystem_ = bs; }

    /// Set GameDB reference for DB-driven wall blocking lookups.
    void SetGameDB(GameDB* db) { gameDB_ = db; }

    /// Set spatial hash for vision neighbor queries.
    void SetSpatialHash(LandAnimalSpatialHash* hash) { spatialHash_ = hash; }

    /// Set ecosystem manager for food queries (Phase 4).
    void SetEcosystem(class EcosystemManager* eco) { ecosystem_ = eco; }

    /// Food preference weights (Phase 4). DB value if set, else subclass override.
    /// Predators return 0,0 (don't eat vegetation).
    virtual float GetFoodGrassWeight() const { return dbFoodGrassWt_ >= 0.0f ? dbFoodGrassWt_ : 0.6f; }
    virtual float GetFoodShrubWeight() const { return 0.3f; }

    /// True if this species investigates corpses. DB value if set, else subclass override.
    virtual bool IsScavenger() const { return dbIsScavenger_ >= 0 ? (dbIsScavenger_ != 0) : false; }

    /// Find the head bone in a skeleton purely by structure: walk from
    /// the root, at each step pick the child whose bind-pose world Y is
    /// highest, stop when no child is higher. Returns the bone index
    /// (the topmost bone reachable along an ascending-Y chain), or -1
    /// if the skeleton is empty / unusable. Rig-name agnostic — survives
    /// any exporter renaming.
    /// diagSpecies: optional label for one-shot info log on discovery.
    static int FindHeadBoneIndex(const Urho3D::Skeleton& skel,
                                 const Urho3D::String& diagSpecies = Urho3D::String::EMPTY);

    /// World position of the head bone (anatomical head, not bbox top).
    /// Falls back to node origin if no head bone has been discovered.
    Urho3D::Vector3 GetHeadWorldPos() const;

    // --- Creature audio framework (Phase 5) ---
    // Subclasses set path strings in their constructors. Empty = silent (no error).
    // Play helpers lazy-load from ResourceCache and play on the node's SoundSource3D.
protected:
    String footstepSoundPath_;
    String vocalizationSoundPath_;
    String hitSoundPath_;
    String deathSoundPath_;

    void PlayFootstep();
    void PlayVocalization();
    void PlayHit();
    void PlayDeath();

    /// Seconds the animal can survive submerged before dying. Override to increase/decrease.
    virtual float GetDrownTime() const { return 5.0f; }

    // --- Prefix-based animation system ---
    // Subclasses only need to override GetAnimPrefix() and GetModelDir().
    // All anim paths are constructed automatically: <ModelDir><AnimPrefix><Action>.ani

    /// Animation name prefix, e.g. "Deer_AnimalArmature". Empty = legacy per-species paths.
    virtual String GetAnimPrefix() const { return String::EMPTY; }
    /// Directory containing model + anims, e.g. "Models/Animals/". Must end with '/'.
    virtual String GetModelDir() const { return "Models/Animals/"; }

    // Default implementations build paths from prefix. Override for non-standard naming.
    String GetIdleAnim() const override;
    String GetRunAnim() const override;
    String GetWalkAnim() const override;
    String GetEatAnim() const override;
    String GetDieAnim() const override;
    String GetAttackAnim() const override;
    String GetHitReactAnim() const override;
    String GetLookAnim() const override;

    /// Override to pick land-aware wander targets (shore seeking, dry land).
    void PickWanderTarget() override;

    /// Phase 4: pick the best food position within wander radius.
    /// Sets wanderTarget_ and stateTimer_. Returns false if no food found.
    bool PickForageTarget();

    /// Override to respawn on dry land.
    void Respawn() override;

    /// Post-movement housekeeping: terrain snap + drowning check.
    /// Call at the end of FixedUpdate after state machine logic.
    void PostMovementUpdate(float timeStep);

    float waterLevel_{5.5f};

    /// World Y captured the moment the creature transitions DIE → CORPSE.
    float corpseStartY_{0.0f};

    /// Check if movement from→to is blocked by a building wall.
    bool IsBlockedByWall(const Vector3& from, const Vector3& to) const;

private:
    void SnapToTerrain();

    /// Scavenger gate: if this creature is a scavenger and a fresh scent is
    /// within SCENT_DETECTION_RADIUS, set wanderTarget_ to the scent and call
    /// SetState(CREATURE_SCAVENGE). Returns true if the state was changed,
    /// false if no scent matched (caller proceeds with normal behavior).
    /// Safe to call when terrainNode_ is null — returns false.
    bool TryEnterScavenge();

    /// Cached head bone index. -1 = not yet searched. Discovered lazily on
    /// first FixedUpdate so the AnimatedModel has been attached.
    int headBoneIndex_{-1};
    /// Set true once we've attempted discovery, regardless of outcome.
    /// Prevents repeated O(N²) searches when discovery fails (no skeleton).
    bool headBoneSearched_{false};

    float drownTimer_{0.0f};

    /// SCAVENGE state has two phases: travel-to-kill, then on-site eat.
    /// scavengeArrived_ flips true the first frame the creature reaches the
    /// kill site, switching the per-frame logic from walk to eat. Cleared
    /// when the creature returns to wander.
    bool scavengeArrived_{false};

    /// Terrain height cache.
    float cachedHeight_{0.0f};
    Vector3 cachedHeightPos_{Vector3::ZERO};

    /// Stagger ID for drowning scan distribution.
    unsigned staggerID_{0};
    static unsigned nextStaggerID_;

    // Creature audio — cached Sound resources (lazy-loaded on first play)
    SharedPtr<Sound> footstepSound_;
    SharedPtr<Sound> vocalizationSound_;
    SharedPtr<Sound> hitSound_;
    SharedPtr<Sound> deathSound_;

    // Footstep distance accumulator — plays footstep every N meters of horizontal travel
    Vector3 lastFootstepPos_;
    bool footstepPosInitialized_{false};
    static constexpr float FOOTSTEP_DISTANCE = 2.5f;

    // Vocalization idle timer — random interval between idle calls
    float vocalizationTimer_{0.0f};

    // Death sound one-shot gate
    bool deathSoundPlayed_{false};

    /// Building system for wall collision.
    BuildingSystem* buildingSystem_{nullptr};
    /// GameDB for wall_strength lookups.
    GameDB* gameDB_{nullptr};

    /// Spatial hash for vision neighbor queries.
    LandAnimalSpatialHash* spatialHash_{nullptr};

    /// Ecosystem manager for food queries (Phase 4).
    class EcosystemManager* ecosystem_{nullptr};

    /// Current hunt target (predators only). WeakPtr auto-nulls if prey is
    /// removed from the scene (killed, despawned).
    WeakPtr<Node> huntTarget_;

protected:
    /// Position of the threat that triggered CREATURE_ALERT.
    /// Used to face the threat during the stare, then flee away from it.
    Vector3 alertThreatPos_;

    /// Vision scan accumulator — rate-limited to avoid per-frame queries.
    float visionScanTimer_{0.0f};

    /// Scan nearby animals for threats/prey using vision cones.
    /// Returns true if a state change occurred (e.g. entered FLEE or HUNT).
    bool VisionScan();
};
