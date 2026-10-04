// HumanNPC — task-driven human character base class.
// Inherits LandAnimal for terrain physics (snap, drowning, walls, vision)
// but replaces the random animal state machine with a priority task queue.
// CaveMan and CaveWoman inherit from this instead of LandAnimal directly.
// When possessed by a player, AI suspends and controls drive movement.

#pragma once

#include "LandAnimal.h"
#include "ResourceMap.h"
#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Input/Controls.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Core/Object.h>

// Shared animation paths for caveman characters (default human NPC skin).
namespace CaveAnims
{
    static const char* DIR = "Models/Characters/";

    // Single animations
    static const char* IDLE    = "Caveman_idle.ani";
    static const char* WALK    = "Caveman_walk.ani";
    static const char* RUN     = "Caveman_running.ani";
    static const char* DIE     = "Caveman_die.ani";
    static const char* GREET   = "Caveman_Standing_Greeting.ani";
    static const char* SWIM    = "Caveman_Swimming.ani";
    static const char* CROUCH  = "Caveman_Crouched_Walking.ani";
    static const char* ATTACK  = "Caveman_attack.ani";
    static const char* VICTORY = "Caveman_victory.ani";
    static const char* SCREAM  = "Caveman_scream.ani";
    static const char* JUMP    = "Caveman_jump.ani";
    static const char* TREAD_WATER = "Caveman_Treading_Water.ani";
    static const char* CRAWL  = "Caveman_Low_Crawl.ani";
    static const char* STANDING = "Caveman_standing.ani";
    static const char* DISAPPOINTED = "Caveman_isappointed.ani";
    static const char* SAMBA  = "Caveman_samba.ani";

    // Variants — randomly picked each time
    static const char* EAT_VARIANTS[] = {
        "Caveman_Gathering_Objects.ani",
        "Caveman_Pick_Fruit.ani",
        "Caveman_Picking_Up.ani",
        "Caveman_Watering.ani"
    };
    static const char* SIT_VARIANTS[] = {
        "Caveman_Sitting_Idle.ani",
        "Caveman_Sitting_Dazed.ani"
    };
    static const char* SLEEP_VARIANTS[] = {
        "Caveman_Sleeping_Idle.ani",
        "Caveman_Laying_Sleeping.ani"
    };
    static const char* LOOK_VARIANTS[] = {
        "Caveman_Look_Around.ani",
        "Caveman_Looking_Around.ani"
    };

    inline String Pick(const char* variants[], unsigned count)
    {
        return String(DIR) + variants[Urho3D::Random((int)count)];
    }
}

/// Control bit flags for possession input routing.
const unsigned CTRL_FORWARD = 1;
const unsigned CTRL_BACK = 2;
const unsigned CTRL_LEFT = 4;
const unsigned CTRL_RIGHT = 8;
const unsigned CTRL_JUMP = 16;
const unsigned CTRL_SPRINT = 32;

/// Per-NPC insight accumulator for observed phenomena.
/// When an NPC witnesses a phenomenon (lightning strike, animal behavior, fire spreading),
/// a WIS check gates whether they actually absorb the observation.
/// Reaching the insight threshold flags the NPC as ready for a discovery attempt.
struct InsightEntry
{
    int phenomenonType{0};  ///< Category of observed phenomenon
    int count{0};           ///< Successful observations accumulated
    float lastSeen{0.0f};   ///< Scene time of last observation
};

/// Pre-resolved animation paths for a PP character skin.
/// Populated once at spawn by ApplySkin(); animation getters return these
/// instead of the default CaveAnims paths when hasSkin_ is true.
struct SkinAnims
{
    String idle, walk, run, die, greet, swim, jump, crouchWalk;
    String eat, sit, sleep, look, attack, victory, scream;
    String tend, treadWater, crawl, standing, disappointed, dance, shear, fish;
};

/// Per-NPC technique knowledge entry.
struct NPCTechnique
{
    int skillId{0};
    int rating{0};       ///< Current level (0 = unknown, 1-10)
    int xp{0};           ///< Progress toward next level
    bool discovered{false}; ///< Whether NPC knows this technique at all
};

class HumanNPC : public LandAnimal
{
    URHO3D_OBJECT(HumanNPC, LandAnimal);

public:
    explicit HumanNPC(Context* context);

    void Start() override;
    void FixedUpdate(float timeStep) override;

    /// Set the campfire node this NPC is associated with.
    void SetCampfireNode(Node* node) { campfireNode_ = node; }

    /// Set the camera node for proximity awareness (god-cam or possessed view).
    void SetCameraNode(Node* node) { cameraNode_ = node; }

    /// Set the resource map for O(1) gather queries instead of scene-walking.
    void SetResourceMap(ResourceMap* map) { resourceMap_ = map; }

    // --- Observation / Insight system ---
    /// Process a witnessed phenomenon. WIS check (d20 + WisMod vs DC) gates insight gain.
    /// Returns true if insight was gained and threshold reached (ready for discovery).
    bool ObservePhenomenon(int phenomenonType, const Vector3& pos);
    /// Get current insight count for a phenomenon category.
    int GetInsightCount(int phenomenonType) const;
    /// Check if NPC has enough insight to attempt discovery for a phenomenon category.
    bool IsReadyForDiscovery(int phenomenonType) const;

    // --- Technique system ---
    /// Award XP for a technique. INT mod accelerates learning. Returns true if level-up.
    bool AwardTechniqueXP(int skillId, int baseXP);
    /// Get current rating for a technique. Returns 0 if unknown.
    int GetTechniqueRating(int skillId) const;
    /// Check if NPC knows a technique.
    bool KnowsTechnique(int skillId) const;
    /// Attempt to discover a new technique from practicing a source skill.
    /// Rolls INT + rating vs DC. Returns discovered skill ID, or 0 if failed/nothing to discover.
    int AttemptDiscovery(int sourceSkillId);
    /// Teach a technique to another NPC. CHA mod affects speed. Returns XP transferred.
    int TeachTechnique(int skillId, HumanNPC* student);
    /// Grant initial knowledge of a primitive technique (Stone Age defaults).
    void GrantPrimitiveTechniques();
    /// Set settlement epoch tier (gates technique availability).
    void SetEpochTier(int tier) { epochTier_ = tier; }

    /// Apply a PP character skin by name (e.g. "PP_Farmer").
    /// Swaps model, materials, and animation paths. Call before tint/equipment.
    void ApplySkin(const String& skinName);

    // --- Skin-aware animation getters (shared by CaveMan/CaveWoman) ---
    String GetModelDir() const override { return CaveAnims::DIR; }

    String GetIdleAnim() const override { return hasSkin_ ? skinAnims_.idle : String(CaveAnims::DIR) + CaveAnims::IDLE; }
    String GetWalkAnim() const override { return hasSkin_ ? skinAnims_.walk : String(CaveAnims::DIR) + CaveAnims::WALK; }
    String GetRunAnim() const override { return hasSkin_ ? skinAnims_.run : String(CaveAnims::DIR) + CaveAnims::RUN; }
    String GetDieAnim() const override { return hasSkin_ ? skinAnims_.die : String(CaveAnims::DIR) + CaveAnims::DIE; }
    String GetGreetAnim() const override { return hasSkin_ ? skinAnims_.greet : String(CaveAnims::DIR) + CaveAnims::GREET; }
    String GetSwimAnim() const override { return hasSkin_ ? skinAnims_.swim : String(CaveAnims::DIR) + CaveAnims::SWIM; }
    String GetJumpAnim() const override { return hasSkin_ ? skinAnims_.jump : String(CaveAnims::DIR) + CaveAnims::JUMP; }
    String GetCrouchWalkAnim() const override { return hasSkin_ ? skinAnims_.crouchWalk : String(CaveAnims::DIR) + CaveAnims::CROUCH; }
    String GetEatAnim() const override { return hasSkin_ ? skinAnims_.eat : CaveAnims::Pick(CaveAnims::EAT_VARIANTS, 4); }
    String GetSitAnim() const override { return hasSkin_ ? skinAnims_.sit : CaveAnims::Pick(CaveAnims::SIT_VARIANTS, 2); }
    String GetSleepAnim() const override { return hasSkin_ ? skinAnims_.sleep : CaveAnims::Pick(CaveAnims::SLEEP_VARIANTS, 2); }
    String GetLookAnim() const override { return hasSkin_ ? skinAnims_.look : CaveAnims::Pick(CaveAnims::LOOK_VARIANTS, 2); }
    String GetAttackAnim() const override { return hasSkin_ ? skinAnims_.attack : String(CaveAnims::DIR) + CaveAnims::ATTACK; }
    String GetVictoryAnim() const override { return hasSkin_ ? skinAnims_.victory : String(CaveAnims::DIR) + CaveAnims::VICTORY; }
    String GetScreamAnim() const override { return hasSkin_ ? skinAnims_.scream : String(CaveAnims::DIR) + CaveAnims::SCREAM; }
    String GetTendAnim() const override { return hasSkin_ ? skinAnims_.tend : String(CaveAnims::DIR) + "Caveman_Picking_Up.ani"; }
    String GetTreadWaterAnim() const override { return hasSkin_ ? skinAnims_.treadWater : String(CaveAnims::DIR) + CaveAnims::TREAD_WATER; }
    String GetCrawlAnim() const override { return hasSkin_ ? skinAnims_.crawl : String(CaveAnims::DIR) + CaveAnims::CRAWL; }
    String GetStandingAnim() const override { return hasSkin_ ? skinAnims_.standing : String(CaveAnims::DIR) + CaveAnims::STANDING; }
    String GetDisappointedAnim() const override { return hasSkin_ ? skinAnims_.disappointed : String(CaveAnims::DIR) + CaveAnims::DISAPPOINTED; }
    String GetDanceAnim() const override { return hasSkin_ ? skinAnims_.dance : String(CaveAnims::DIR) + CaveAnims::SAMBA; }
    String GetShearAnim() const override { return hasSkin_ ? skinAnims_.shear : String(CaveAnims::DIR) + "Caveman_Picking_Up.ani"; }
    String GetFishAnim() const override { return hasSkin_ ? skinAnims_.fish : String(CaveAnims::DIR) + "Caveman_Sitting_Idle.ani"; }

    /// Possession — when true, AI suspends and player controls drive movement.
    void SetPossessed(bool possessed);
    bool IsPossessed() const { return possessed_; }

    /// Movement controls — set by TerrainNode each frame when possessed.
    Controls controls_;

protected:
    /// Process player controls when possessed (physics-driven).
    void UpdatePossessedMovement(float timeStep);

    /// Process swim controls when possessed and in water.
    void UpdatePossessedSwim(float timeStep);

    /// Set position through SmoothedTransform if present, else direct.
    void SetSmoothedPosition(const Vector3& pos);

    /// Physics collision handler — detects ground contact via contact normals.
    void HandleNodeCollision(StringHash eventType, VariantMap& eventData);

    /// True when being controlled by a player.
    bool possessed_{false};

    /// True when in water during possessed movement.
    bool possessedInWater_{false};

    /// Physics ground detection — set true by HandleNodeCollision, reset each FixedUpdate.
    bool onGround_{false};
    /// Time since last confirmed ground contact. Allows brief "soft grounding" after losing contact.
    float inAirTimer_{0.0f};
    /// Must release jump between jumps to prevent held-key repeated jumping.
    bool okToJump_{true};

    /// Tracks current animation in possessed mode to avoid redundant PlayExclusive calls.
    String possessedAnim_;

    /// Distance within which the campfire provides warmth and is a valid sit target.
    float campfireRadius_{8.0f};

    /// Maximum distance to search for gatherable resources.
    float gatherSearchRadius_{25.0f};

    /// Campfire this NPC gravitates toward.
    WeakPtr<Node> campfireNode_;

    /// Resource node currently being targeted for gathering (scene-walk fallback).
    WeakPtr<Node> gatherTarget_;

    /// Resource map for O(1) spatial queries.
    WeakPtr<ResourceMap> resourceMap_;

    /// Map-based gather target position and type.
    Vector3 gatherMapPos_;
    ResourceType gatherMapType_{RES_NONE};

    /// Held item node attached to right hand bone.
    WeakPtr<Node> heldItemNode_;

    /// Camera node — set externally, used for proximity awareness (wave at god).
    WeakPtr<Node> cameraNode_;

    /// Distance at which NPC notices the camera or another NPC and may greet.
    float greetRadius_{12.0f};

    /// Cooldown after greeting to avoid spamming.
    float greetCooldown_{0.0f};
    static constexpr float GREET_COOLDOWN_TIME = 30.0f;

    // --- PP Character Skin system ---
    bool hasSkin_{false};       ///< True when a PP skin is active (animation getters check this)
    String skinName_;           ///< e.g. "PP_Farmer", empty = default Caveman
    SkinAnims skinAnims_;       ///< Pre-resolved animation paths for the active skin

    // --- Observation / Insight system ---
    HashMap<unsigned, InsightEntry> insights_;  ///< phenomenonType → observation accumulator
    static constexpr int INSIGHT_THRESHOLD = 3; ///< Observations needed before discovery attempt

    // --- Technique system ---
    HashMap<unsigned, NPCTechnique> techniques_;  ///< skillId → knowledge state
    int epochTier_{0};  ///< Settlement's current epoch tier (gates what can be learned)

public:
    /// XP thresholds per level (loaded from DB, cached static).
    static Vector<int> xpThresholds_;
    static bool xpThresholdsLoaded_;
};
