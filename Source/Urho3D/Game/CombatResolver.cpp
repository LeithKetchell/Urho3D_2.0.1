// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "CombatResolver.h"
#include "../Core/Context.h"
#include "../Math/MathDefs.h"

#include <cstdlib>
#include <ctime>

namespace Urho3D
{

CombatResolver::CombatResolver(Context* context)
    : Object(context)
{
    // Seed with time on first creation (server-side only)
    srand((unsigned)time(nullptr));
}

void CombatResolver::RegisterObject(Context* context)
{
    context->RegisterFactory<CombatResolver>();
}

int CombatResolver::RollD20()
{
    if (externalRNG_)
        return externalRNG_(20);
    return (rand() % 20) + 1;
}

int CombatResolver::RollDN(int n)
{
    if (n <= 1) return 1;
    if (externalRNG_)
        return externalRNG_(n);
    return (rand() % n) + 1;
}

AttackResult CombatResolver::ResolveAttack(int attackMod, int baseDamage, int damageVar,
                                            int targetDef, int targetHp,
                                            int strMod, int targetDexMod)
{
    AttackResult r;
    r.roll = RollD20();

    // Natural 1 always misses, natural 20 always crits
    r.fumble = (r.roll == 1);
    r.crit   = (r.roll == 20);

    // DEX modifier adds to defense (dodge chance) — DnD AC formula
    int effectiveDef = targetDef + Max(0, targetDexMod);

    int total = r.roll + attackMod;
    r.hit = !r.fumble && (r.crit || total >= effectiveDef);

    if (r.hit)
    {
        // STR modifier adds flat bonus damage on hit (min 0 — weak creatures don't heal targets)
        r.damage = baseDamage + Max(0, strMod) + (damageVar > 0 ? RollDN(damageVar) : 0);
        if (r.crit)
            r.damage *= 2;
    }

    r.targetHpRemaining = Max(0, targetHp - r.damage);
    r.targetDied        = (r.targetHpRemaining <= 0);
    return r;
}

bool CombatResolver::InRange(const Vector3& from, const Vector3& to, float rangeMetres) const
{
    return (to - from).Length() <= rangeMetres;
}

} // namespace Urho3D
