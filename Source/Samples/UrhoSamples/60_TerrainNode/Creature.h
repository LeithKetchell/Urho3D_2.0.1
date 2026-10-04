// Creature — base class for ALL living things (land and water).
// Provides state machine, model auto-scaling, smooth movement, respawn.
// Domain-specific behavior lives in LandAnimal / WaterAnimal.

#pragma once

#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Core/Object.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/Game/GameDB.h>

using namespace Urho3D;

/// Fired when any creature enters the DIE state.
URHO3D_EVENT(E_CREATUREDIED, CreatureDied)
{
    URHO3D_PARAM(P_CREATUREID, CreatureId);   // int — matches creatures table id
    URHO3D_PARAM(P_POSITION,   Position);     // Vector3 — world position
}

/// Fired on a creature's node when it is hit (or missed). Subscribe on the node to show damage numbers.
URHO3D_EVENT(E_COMBAT_RESULT, CombatResult)
{
    URHO3D_PARAM(P_DAMAGE, Damage);  // int — 0 on miss
    URHO3D_PARAM(P_CRIT,   Crit);   // bool
    URHO3D_PARAM(P_MISS,   Miss);   // bool
}

/// Creature behavior states.
enum CreatureState
{
    CREATURE_IDLE,
    CREATURE_WANDER,
    CREATURE_EAT,       ///< Grazing / eating / gathering
    CREATURE_FLEE,
    CREATURE_FIGHT,     ///< Aggressive creatures charge and attack
    CREATURE_DIE,
    CREATURE_SIT,       ///< Sitting (near campfire, resting)
    CREATURE_SLEEP,     ///< Sleeping (night cycle)
    CREATURE_LOOK,      ///< Alert — looking around
    CREATURE_GREET,     ///< Greeting nearby player
    CREATURE_SWIM,      ///< In water
    CREATURE_CROUCH,    ///< Crouched movement (sneak/hunt)
    CREATURE_TRAPPED,   ///< Caught in a snare/trap — frozen until harvested
    CREATURE_CORPSE,    ///< Death animation finished — body is a harvestable static prop until cleanup
    CREATURE_SCAVENGE,  ///< Predator drawn to a nearby kill site — moves to scent then briefly eats
    CREATURE_HUNT,      ///< Predator chasing live prey — pursues at run speed until catch or lose sight
    CREATURE_ALERT,     ///< Prey spotted a threat — frozen stare, then flee. Naturalistic delay.
    CREATURE_VICTORY,   ///< Brief celebration after a successful kill (2-3s, then idle)
    CREATURE_FORAGE,    ///< Herbivore moving toward best food source in perception radius
    CREATURE_SHEAR,     ///< Shearing a tamed alpaca for wool
    CREATURE_FISH,      ///< Fishing at water's edge — sit/wait for catch
    CREATURE_FOLLOW     ///< Following owner — pack animal trailing its NPC
};

/// Base class for all creatures. Thin by design — only what every creature shares.
class Creature : public LogicComponent
{
    URHO3D_OBJECT(Creature, LogicComponent);

public:
    explicit Creature(Context* context);

    void Start() override;
    void Update(float timeStep) override;

    /// Get current behavior state.
    CreatureState GetState() const { return state_; }

    /// Set home position (center of wander area).
    void SetHomePosition(const Vector3& pos) { homePos_ = pos; }
    const Vector3& GetHomePosition() const { return homePos_; }

    /// Get the creature ID for DB lookups. Subclasses override.
    virtual int GetCreatureId() const { return 0; }

    // --- Combat ---
    /// Initialize combat stats from GameDB values.
    void InitCombatStats(int hp, int attack, int defense, int damage, int damageVar,
                         int speed, float fleeFraction, const String& aggression);
    /// Set DB-driven behavior overrides (flee, vision, predator, food preference).
    void InitBehaviorStats(float fleeSpeed, float fleeDistance, float visionRange,
                           float visionAngle, bool isPredator, bool isScavenger, float foodGrassWt);
    /// Apply damage. Returns true if creature died.
    bool TakeDamage(int amount);
    /// Authoritative HP overwrite from the server (Combat Phase 2).
    /// Skips the TakeDamage flee/fight transitions; the server is the source
    /// of truth and any state transitions follow from MSG_CREATURE_DEATH.
    void SetHp(int newHp);
    int GetHp() const { return hp_; }
    int GetMaxHp() const { return maxHp_; }
    int GetDefense() const { return defense_; }

    // --- DnD Ability Scores (rolled at birth, manifest as behavior) ---
    struct AbilityScores
    {
        int strength{10};
        int dexterity{10};
        int constitution{10};
        int intelligence{10};
        int wisdom{10};
        int charisma{10};

        /// Standard DnD modifier: (score - 10) / 2
        static int Mod(int score) { return (score - 10) / 2; }
        int StrMod() const { return Mod(strength); }
        int DexMod() const { return Mod(dexterity); }
        int ConMod() const { return Mod(constitution); }
        int IntMod() const { return Mod(intelligence); }
        int WisMod() const { return Mod(wisdom); }
        int ChaMod() const { return Mod(charisma); }
    };
    const AbilityScores& GetStats() const { return stats_; }

    /// Roll ability scores using the given method. Call at creature creation.
    void RollStats(int method = 1);  // 0=3d6, 1=4d6-drop-lowest
    /// Roll ability scores with parental bias (lineage inheritance).
    void RollStatsWithLineage(const AbilityScores& parent1, const AbilityScores& parent2);

    /// Server-driven state injection (Resource Chain Phase 2 trap-catch).
    /// Externally-set states like CREATURE_TRAPPED need a public setter so the
    /// network message handler can apply them without being inside the AI loop.
    /// AI-internal transitions still call this from within Update().
    void SetState(CreatureState newState);

    /// Server-assigned spawn tracking ID for AI state correlation.
    void SetSpawnId(unsigned id) { spawnId_ = id; }
    unsigned GetSpawnId() const { return spawnId_; }

    /// Phase 25: Selective breeding traits — affects visual scale and behavior.
    void SetTraits(float size, float speed, float yield);
    float GetTraitSize() const { return traitSize_; }
    float GetTraitSpeed() const { return traitSpeed_; }
    float GetTraitYield() const { return traitYield_; }

    /// Apply server-authoritative AI state (NPC AI Phase 2).
    /// Marks creature as server-driven, stores position for lerp, sets state.
    void ApplyServerState(CreatureState newState, const Vector3& pos, float moveSpeed);
    /// True when server is driving this creature's AI.
    bool IsServerDriven() const { return serverDriven_; }

    // --- Vision ---
    /// Maximum sight distance in world units. DB value if set, else subclass override.
    /// WIS modifier scales vision range for prey creatures: +10% per +1 WIS mod.
    /// Wise prey spot predators sooner. Predator vision is unchanged (hunting instinct, not wisdom).
    virtual float GetVisionRange() const
    {
        float base = dbVisionRange_ >= 0.0f ? dbVisionRange_ : 0.0f;
        if (!IsPredator() && base > 0.0f)
            base *= (1.0f + stats_.WisMod() * 0.1f);
        return Max(0.0f, base);
    }
    /// Cosine of the half-angle of the vision cone. DB value if set, else subclass override.
    virtual float GetVisionCosAngle() const { return dbVisionAngle_ >= 0.0f ? dbVisionAngle_ : 0.5f; }
    /// True if this creature is a predator. DB value if set, else subclass override.
    virtual bool IsPredator() const { return dbIsPredator_ >= 0 ? (dbIsPredator_ != 0) : false; }
    /// Dot-product vision cone test. Returns true if 'other' is within this
    /// creature's vision range AND inside the forward-facing cone.
    bool CanSee(const Creature* other) const;

    // --- Vitals access (for UI billboards and inspect HUD) ---
    float GetHunger() const { return hunger_; }
    float GetThirst() const { return thirst_; }
    float GetStamina() const { return stamina_; }
    float GetWarmth() const { return warmth_; }
    void SetServerVitals(float hp, float hunger, float thirst, float warmth, float stamina)
    {
        hp_ = (int)hp; hunger_ = hunger; thirst_ = thirst; warmth_ = warmth; stamina_ = stamina;
    }
    virtual bool HasVitals() const { return false; }

    /// Load survival rules from GameDB into static caches. Call once after InitGameDB().
    static void LoadSurvivalRules(GameDB* db);

    /// Public read access to cached DB survival thresholds for HUD status icons.
    static const HungerRules&  GetHungerRules()  { return hungerRules_; }
    static const ThirstRules&  GetThirstRules()  { return thirstRules_; }
    static const WarmthRules&  GetWarmthRules()  { return warmthRules_; }
    static const StaminaRules& GetStaminaRules() { return staminaRules_; }
    static const DeathRules&   GetDeathRules()   { return deathRules_; }

    /// Bark vessel contents (0=empty, 1=fire, 2=water). Broadcast via trailing u8 on AI state.
    void SetVesselContents(unsigned char v) { vesselContents_ = v; }
    unsigned char GetVesselContents() const { return vesselContents_; }

    /// Server task (STASK enum). Used by client for prop/tool selection.
    void SetServerTask(int task);
    int GetServerTask() const { return serverTask_; }

protected:
    /// DnD ability scores — rolled at creation, affect all behavior.
    AbilityScores stats_;

    // --- Subclass overrides: asset paths ---
    virtual String GetModelPath() const = 0;
    virtual String GetIdleAnim() const { return String::EMPTY; }
    virtual String GetRunAnim() const { return String::EMPTY; }
    virtual String GetWalkAnim() const { return String::EMPTY; }
    virtual String GetEatAnim() const { return String::EMPTY; }
    virtual String GetDieAnim() const { return String::EMPTY; }
    virtual String GetSitAnim() const { return String::EMPTY; }
    virtual String GetSleepAnim() const { return String::EMPTY; }
    virtual String GetLookAnim() const { return String::EMPTY; }
    virtual String GetGreetAnim() const { return String::EMPTY; }
    virtual String GetSwimAnim() const { return String::EMPTY; }
    virtual String GetJumpAnim() const { return String::EMPTY; }
    virtual String GetCrouchWalkAnim() const { return String::EMPTY; }
    virtual String GetAttackAnim() const { return String::EMPTY; }
    virtual String GetVictoryAnim() const { return String::EMPTY; }
    virtual String GetScreamAnim() const { return String::EMPTY; }
    /// Hit reaction animation (overlay on layer 1 when taking damage).
    virtual String GetHitReactAnim() const { return String::EMPTY; }
    /// One-shot interaction animation overlaid on the first moment of CREATURE_SIT
    /// when the creature was previously walking (WANDER→SIT). Used to read the
    /// invisible server-side STASK_TEND_FIRE fuel-add as a visible "reaching to
    /// stoke the fire" gesture on the client. Non-empty only for species that
    /// tend fires (humans). Empty = no overlay, sit loop plays straight.
    virtual String GetTendAnim() const { return String::EMPTY; }
    /// Treading water — idle animation while in water (not swimming forward).
    virtual String GetTreadWaterAnim() const { return String::EMPTY; }
    /// Low crawl — stealth/sneak movement.
    virtual String GetCrawlAnim() const { return String::EMPTY; }
    /// Standing idle — upright idle variant (e.g. alert/aware).
    virtual String GetStandingAnim() const { return String::EMPTY; }
    /// Disappointed reaction animation.
    virtual String GetDisappointedAnim() const { return String::EMPTY; }
    /// Dance/celebration animation.
    virtual String GetDanceAnim() const { return String::EMPTY; }
    /// Shearing animation (kneeling gather gesture at animal).
    virtual String GetShearAnim() const { return String::EMPTY; }
    /// Fishing animation (sitting at water's edge, waiting for a bite).
    virtual String GetFishAnim() const { return String::EMPTY; }

    // --- Subclass overrides: behavior tuning ---
    virtual float GetDesiredSize() const { return 1.0f; }
    virtual float GetWanderRadius() const { return 20.0f; }
    virtual float GetWanderSpeed() const { return 2.0f; }
    virtual float GetFleeSpeed() const { return dbFleeSpeed_ >= 0.0f ? dbFleeSpeed_ : 6.0f; }
    virtual float GetFleeDistance() const { return dbFleeDistance_ >= 0.0f ? dbFleeDistance_ : 30.0f; }
    virtual float GetMinIdleDuration() const { return 3.0f; }
    virtual float GetMaxIdleDuration() const { return 8.0f; }

    /// Called when entering a new state. Override for custom animations.
    virtual void OnStateEnter(CreatureState newState);

    /// Move toward a target position with smooth turning. Returns the new facing direction.
    void MoveToward(const Vector3& target, float speed, float timeStep);

    /// Pick a wander target — default: random point within wander radius on dry land.
    /// Subclasses override for domain-specific wandering.
    virtual void PickWanderTarget();

    /// Respawn at a safe location near home. Subclasses override for domain-specific logic.
    virtual void Respawn();

    /// Cached terrain pointer (most creatures need this).
    WeakPtr<Terrain> terrain_;

    /// Current state.
    CreatureState state_{CREATURE_IDLE};
    /// True when OnStateEnter couldn't find AnimationController — retry each frame.
    bool pendingAnimApply_{true};

    /// Previous state — captured on every SetState transition. Used by
    /// OnStateEnter to decide on transition-specific animation overlays
    /// (e.g. WANDER→SIT plays a one-shot tend/pickup gesture for cavemen).
    CreatureState prevState_{CREATURE_IDLE};

    /// State timer — counts down to trigger transitions.
    float stateTimer_{0.0f};

    // --- Vitals (0-100, full = good, empty = critical need) ---
    // Subclasses opt-in by overriding UseVitals() to true.
    float hunger_{100.0f};      ///< Full = fed. Depletes always. Restored by EAT.
    float thirst_{100.0f};      ///< Full = hydrated. Depletes always. Restored by drinking.
    float stamina_{100.0f};     ///< Full = rested. Depletes with activity. Restored by SLEEP/SIT.
    float warmth_{100.0f};      ///< Full = warm. Depletes faster at night. Restored by campfire.

    // --- Cached DB rules (loaded once via LoadSurvivalRules) ---
    static HungerRules  hungerRules_;
    static ThirstRules  thirstRules_;
    static WarmthRules  warmthRules_;
    static StaminaRules staminaRules_;
    static DeathRules   deathRules_;
    static bool         rulesLoaded_;

    /// Tick vitals based on time of day. Pass sun altitude in [-1, 1].
    void UpdateVitals(float timeStep, float sunAltitude);
    /// Pick the most urgent need and return the matching state. Returns CREATURE_IDLE if none urgent.
    CreatureState PickNeedState();
    /// Restore vitals when an action completes.
    void OnActionComplete(CreatureState state);

    /// Server-assigned spawn tracking ID for AI state correlation (0 = untracked).
    unsigned spawnId_{0};
    /// Bark vessel contents: 0=empty, 1=fire, 2=water. Updated from AI state broadcast.
    unsigned char vesselContents_{0};

    /// Phase 25: Selective breeding traits (1.0 = average, 0.8-1.2 range).
    float traitSize_{1.0f};
    float traitSpeed_{1.0f};
    float traitYield_{1.0f};
    /// Server task (STASK enum). Drives prop selection on the client.
    int serverTask_{0};

    /// Server-authoritative AI state (NPC AI Phase 2).
    /// When true, this creature's brain is server-driven — local AI skips decisions,
    /// client lerps position toward serverPos_ and applies server-set state.
    bool serverDriven_{false};
    Vector3 serverPos_;          ///< Last server-broadcast position
    float serverMoveSpeed_{0.f}; ///< Last server-broadcast move speed

    /// Home position and current wander target.
    Vector3 homePos_;
    Vector3 wanderTarget_;

    // --- Combat stats (set via InitCombatStats) ---
    int   hp_{10};
    int   maxHp_{10};
    int   attack_{0};
    int   defense_{0};
    int   damage_{1};
    int   damageVar_{0};
    int   speed_{5};
    float fleeFraction_{0.3f};
    String aggression_{"passive"};
    float combatTimer_{0.0f};

    // --- DB-driven behavior stats (set via InitBehaviorStats) ---
    float dbFleeSpeed_{-1.0f};       ///< <0 = use subclass override
    float dbFleeDistance_{-1.0f};
    float dbVisionRange_{-1.0f};
    float dbVisionAngle_{-1.0f};
    float dbFoodGrassWt_{-1.0f};
    int   dbIsPredator_{-1};         ///< <0 = use subclass override
    int   dbIsScavenger_{-1};

    // --- Damage text (self-owned floating UI) ---
    void HandleCombatResult(StringHash eventType, VariantMap& eventData);
    WeakPtr<Text> damageText_;
    float damageTextTimer_{0.0f};
    float damageTextStartY_{0.0f};
};
