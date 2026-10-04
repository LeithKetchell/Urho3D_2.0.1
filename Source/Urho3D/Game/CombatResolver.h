// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "../Math/Vector3.h"

namespace Urho3D
{

/// Result of one combat round.
struct AttackResult
{
    bool hit{false};
    bool crit{false};        ///< Natural 20
    bool fumble{false};      ///< Natural 1
    int  roll{0};            ///< Raw d20 result
    int  damage{0};          ///< Damage dealt (0 on miss)
    int  targetHpRemaining{0};
    bool targetDied{false};
};

/// Server-side dice roller and hit resolver.
/// All randomness for combat goes through here — no client RNG.
class URHO3D_API CombatResolver : public Object
{
    URHO3D_OBJECT(CombatResolver, Object);

public:
    explicit CombatResolver(Context* context);

    static void RegisterObject(Context* context);

    /// Roll d20. Returns 1–20. Uses external RNG if set, else rand().
    int RollD20();
    /// Roll dN (d4, d6, d8…). Returns 1–N. Uses external RNG if set, else rand().
    int RollDN(int n);

    /// External RNG callback: takes (sides) and returns 1..sides.
    /// When set, replaces internal rand() for all dice rolls.
    typedef int (*DiceRollFunc)(int sides);
    void SetExternalRNG(DiceRollFunc func) { externalRNG_ = func; }

    /// Resolve one melee or ranged attack.
    /// @param attackMod   attacker's total attack modifier (weapon + situational)
    /// @param baseDamage  weapon base damage
    /// @param damageVar   damage variance die (4 = d4, 6 = d6…)
    /// @param targetDef   defender's defense value (10 = naked baseline)
    /// @param targetHp    defender's current HP
    /// @param strMod      attacker's STR modifier — added to damage on hit
    /// @param targetDexMod defender's DEX modifier — added to defense (dodge chance)
    AttackResult ResolveAttack(int attackMod, int baseDamage, int damageVar,
                               int targetDef, int targetHp,
                               int strMod = 0, int targetDexMod = 0);

    /// True if attacker is within weapon range of target.
    bool InRange(const Vector3& from, const Vector3& to, float rangeMetres) const;

private:
    DiceRollFunc externalRNG_{nullptr};
};

} // namespace Urho3D
