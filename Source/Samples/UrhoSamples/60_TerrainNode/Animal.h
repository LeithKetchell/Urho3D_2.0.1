// Animal — base class for terrain-roaming wildlife.
// Subclasses override asset paths and behavior tuning.

#pragma once

#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Core/Object.h>

using namespace Urho3D;

class BuildingSystem;
namespace Urho3D { class GameDB; }

/// Fired when any animal enters the ANIMAL_DIE state (player kill, drowning, etc.).
URHO3D_EVENT(E_ANIMALDIED, AnimalDied)
{
    URHO3D_PARAM(P_CREATUREID, CreatureId);   // int — matches creatures table id
    URHO3D_PARAM(P_POSITION,   Position);     // Vector3 — world position of the animal
}

/// Animal behavior states.
enum AnimalState
{
    ANIMAL_IDLE,
    ANIMAL_WANDER,
    ANIMAL_FLEE,
    ANIMAL_FIGHT,   ///< Aggressive animals charge and attack
    ANIMAL_DIE
};

/// Base class for all animals. Provides state machine, terrain following, wander AI.
class Animal : public LogicComponent
{
    URHO3D_OBJECT(Animal, LogicComponent);

public:
    explicit Animal(Context* context);

    static void RegisterObject(Context* context);

    void Start() override;
    void FixedUpdate(float timeStep) override;

    /// Get current behavior state.
    AnimalState GetState() const { return state_; }

    /// Set home position (center of wander area).
    void SetHomePosition(const Vector3& pos) { homePos_ = pos; }

    /// Set building system reference for wall collision checks.
    void SetBuildingSystem(BuildingSystem* bs) { buildingSystem_ = bs; }

    /// Set GameDB reference for DB-driven wall blocking lookups.
    void SetGameDB(GameDB* db) { gameDB_ = db; }

    /// Get the creature ID for wall_strength lookups. Subclasses override.
    virtual int GetCreatureId() const { return 0; }

    // --- Combat ---
    /// Apply damage to this animal. Returns true if it died.
    bool TakeDamage(int amount);
    /// Initialise combat stats from a creature record (called by TerrainNode after spawn).
    void InitCombatStats(int hp, int attack, int defense, int damage, int damageVar,
                         int speed, float fleeFraction, const String& aggression);

    int GetHp()      const { return hp_; }
    int GetMaxHp()   const { return maxHp_; }
    int GetAttack()  const { return attack_; }
    int GetDefense() const { return defense_; }
    int GetDamage()  const { return damage_; }
    int GetDamageVar() const { return damageVar_; }
    float GetWeaponRange() const { return 2.0f; }

    /// Node ID of the entity currently fighting this animal (0 = none).
    unsigned combatTarget_{0};

protected:
    // --- Subclass overrides: asset paths ---
    virtual String GetModelPath() const = 0;
    virtual String GetIdleAnim() const = 0;
    virtual String GetRunAnim() const = 0;
    virtual String GetDieAnim() const = 0;

    // --- Subclass overrides: behavior tuning ---
    /// Real-world size in metres for the longest bounding box axis.
    virtual float GetDesiredSize() const { return 1.0f; }
    virtual float GetWanderRadius() const { return 20.0f; }
    virtual float GetWanderSpeed() const { return 2.0f; }
    virtual float GetFleeSpeed() const { return 6.0f; }
    virtual float GetFleeDistance() const { return 30.0f; }
    virtual float GetMinIdleDuration() const { return 3.0f; }
    virtual float GetMaxIdleDuration() const { return 8.0f; }
    /// Seconds the animal can survive submerged before dying. Override to -1 for aquatic.
    virtual float GetDrownTime() const { return 5.0f; }

    /// Called when entering a new state. Subclasses can play extra animations.
    virtual void OnStateEnter(AnimalState newState);

    /// Transition to a new state.
    void SetState(AnimalState newState);

    /// Cached terrain pointer.
    WeakPtr<Terrain> terrain_;

    /// Current state.
    AnimalState state_{ANIMAL_IDLE};

private:
    void PickWanderTarget();
    void MoveToward(const Vector3& target, float speed, float timeStep);
    void SnapToTerrain();
    void Respawn();
    bool IsBlockedByWall(const Vector3& from, const Vector3& to) const;

    Vector3 homePos_;
    Vector3 wanderTarget_;
    float stateTimer_{0.0f};
    float drownTimer_{0.0f};
    float waterLevel_{5.5f};

    /// Terrain height cache — avoid redundant GetHeight calls.
    float cachedHeight_{0.0f};
    Vector3 cachedHeightPos_{Vector3::ZERO};

    /// Stagger ID for drowning scan distribution across frames.
    unsigned staggerID_{0};
    static unsigned nextStaggerID_;

    /// Building system for wall collision (raw ptr — same scene lifetime).
    BuildingSystem* buildingSystem_{nullptr};
    /// GameDB for wall_strength lookups (raw ptr — same scene lifetime).
    GameDB* gameDB_{nullptr};

    // --- Combat stats (loaded from GameDB creatures table) ---
    int hp_{10};
    int maxHp_{10};
    int attack_{0};
    int defense_{10};
    int damage_{1};
    int damageVar_{4};
    int speed_{5};
    float fleeFraction_{0.5f};    ///< Flee when HP drops below this fraction
    String aggression_{"passive"}; ///< "passive", "defensive", "aggressive"
    float combatTimer_{0.0f};      ///< Cooldown between attacks
};
