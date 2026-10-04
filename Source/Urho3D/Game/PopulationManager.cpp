// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "PopulationManager.h"

#ifdef URHO3D_DATABASE_SQLITE

#include "GameDB.h"
#include "../IO/Log.h"
#include "../Math/MathDefs.h"
#include "../Math/Random.h"

#include <SQLite/sqlite3.h>

#include "../DebugNew.h"

namespace Urho3D
{

PopulationManager::PopulationManager(Context* context) :
    Object(context)
{
}

void PopulationManager::Initialize(GameDB* gameDB)
{
    if (!gameDB || !gameDB->IsOpen())
    {
        URHO3D_LOGERROR("PopulationManager: GameDB not open");
        return;
    }
    db_ = gameDB->GetHandle();

    // Load regions
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, center_x, center_z, radius FROM regions;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERROR("PopulationManager: Failed to query regions — schema not loaded?");
        db_ = nullptr;
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        RegionInfo r;
        r.id     = sqlite3_column_int(stmt, 0);
        r.cx     = (float)sqlite3_column_double(stmt, 1);
        r.cz     = (float)sqlite3_column_double(stmt, 2);
        r.radius = (float)sqlite3_column_double(stmt, 3);
        regions_.Push(r);
    }
    sqlite3_finalize(stmt);

    // Load population counts
    rc = sqlite3_prepare_v2(db_,
        "SELECT region_id, creature_id, count, max_count FROM population;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERROR("PopulationManager: Failed to query population table");
        db_ = nullptr;
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        PopulationEntry pop;
        pop.regionId   = sqlite3_column_int(stmt, 0);
        pop.creatureId = sqlite3_column_int(stmt, 1);
        pop.count      = sqlite3_column_int(stmt, 2);
        pop.maxCount   = sqlite3_column_int(stmt, 3);
        populations_[MakeKey(pop.regionId, pop.creatureId)] = pop;
    }
    sqlite3_finalize(stmt);

    // ------------------------------------------------------------------
    // Death System Phase 1 — birth_rate migration + load
    //
    // Idempotent migration for databases created before the column existed.
    // ALTER TABLE will fail with "duplicate column" if it already exists; we
    // ignore the failure. The UPDATE statements always run and set the
    // per-species values from the plan, so old DBs converge to correct
    // state on next startup.
    // ------------------------------------------------------------------
    sqlite3_exec(db_,
        "ALTER TABLE breeding_rules ADD COLUMN birth_rate REAL DEFAULT 1.3;",
        nullptr, nullptr, nullptr);

    sqlite3_exec(db_,
        "UPDATE breeding_rules SET birth_rate = 1.6 WHERE creature_id = 1;"  // Rabbit
        "UPDATE breeding_rules SET birth_rate = 1.3 WHERE creature_id = 2;"  // Deer
        "UPDATE breeding_rules SET birth_rate = 1.2 WHERE creature_id = 3;"  // Fox
        "UPDATE breeding_rules SET birth_rate = 1.2 WHERE creature_id = 4;"  // Stag
        "UPDATE breeding_rules SET birth_rate = 1.1 WHERE creature_id = 5;"  // Wolf
        "UPDATE breeding_rules SET birth_rate = 1.2 WHERE creature_id = 6;"  // Bull
        "UPDATE breeding_rules SET birth_rate = 1.4 WHERE creature_id = 7;"  // Cow
        "UPDATE breeding_rules SET birth_rate = 1.8 WHERE creature_id = 8;"  // Fish
        "UPDATE breeding_rules SET birth_rate = 1.2 WHERE creature_id = 9;"  // Donkey
        "UPDATE breeding_rules SET birth_rate = 1.2 WHERE creature_id = 10;" // Horse
        "UPDATE breeding_rules SET birth_rate = 1.3 WHERE creature_id = 11;" // Alpaca
        "UPDATE breeding_rules SET birth_rate = 1.1 WHERE creature_id = 12;" // Husky
        "UPDATE breeding_rules SET birth_rate = 1.1 WHERE creature_id = 13;" // ShibaInu
        , nullptr, nullptr, nullptr);

    // Load birth_rate per species into the in-memory PopulationEntry cache.
    // Same value applies to every region for that species.
    if (sqlite3_prepare_v2(db_,
        "SELECT creature_id, birth_rate FROM breeding_rules;",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int   speciesId = sqlite3_column_int(stmt, 0);
            float rate      = (float)sqlite3_column_double(stmt, 1);
            for (auto it = populations_.Begin(); it != populations_.End(); ++it)
            {
                if (it->second_.creatureId == speciesId)
                    it->second_.birthRate = rate;
            }
        }
        sqlite3_finalize(stmt);
    }

    URHO3D_LOGINFOF("PopulationManager: %u regions, %u species entries loaded (birth_rate seeded)",
        regions_.Size(), populations_.Size());
}

void PopulationManager::DailyTick()
{
    if (!db_)
        return;

    // Age deaths: ~1/60 of count per day (average 60-day lifespan).
    // All SET expressions in SQLite evaluate from the original row before applying any change,
    // so count/60 in died_today and count - count/60 both use the same original count.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "UPDATE population SET "
        "  died_today   = MAX(0, count / 60), "
        "  count        = MAX(0, count - MAX(0, count / 60)), "
        "  born_today   = 0, "
        "  killed_today = 0, "
        "  migrated_in  = 0, "
        "  migrated_out = 0;",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Refresh in-memory cache
    if (sqlite3_prepare_v2(db_,
        "SELECT region_id, creature_id, count FROM population;",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            unsigned key = MakeKey(sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1));
            auto it = populations_.Find(key);
            if (it != populations_.End())
                it->second_.count = sqlite3_column_int(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }

    URHO3D_LOGINFO("PopulationManager: Daily tick — age deaths applied");
}

Vector<ReplacementSpawn> PopulationManager::RecordKill(int regionId, int creatureId)
{
    Vector<ReplacementSpawn> replacements;

    if (!db_)
        return replacements;

    unsigned key = MakeKey(regionId, creatureId);
    auto it = populations_.Find(key);
    if (it == populations_.End() || it->second_.count <= 0)
        return replacements;

    PopulationEntry& entry = it->second_;
    entry.count--;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "UPDATE population SET "
        "  count        = MAX(0, count - 1), "
        "  killed_today = killed_today + 1 "
        "WHERE region_id = ? AND creature_id = ?;",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, regionId);
        sqlite3_bind_int(stmt, 2, creatureId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Death System Phase 1 — kill-driven replacement accumulator.
    // Add the per-species birth rate to the accumulator. Each integer step
    // emits a replacement, capped at the carrying capacity. The day-tick
    // breeding loop in AuthServer continues to run independently as the
    // baseline growth path; this is the kill-driven feast-of-the-food-chain
    // path the design plan called for.
    entry.birthAccumulator += entry.birthRate;
    while (entry.birthAccumulator >= 1.0f)
    {
        entry.birthAccumulator -= 1.0f;
        if (entry.count >= entry.maxCount)
            break;  // capacity reached — drop the rest of the surplus this kill
        ReplacementSpawn rs;
        rs.regionId   = regionId;
        rs.creatureId = creatureId;
        replacements.Push(rs);
        entry.count++;  // optimistic in-memory bump; persisted below
    }

    // Persist count change if any replacements were spawned (the kill alone
    // already persisted the -1 above; this catches the +N from replacements).
    if (!replacements.Empty())
    {
        if (sqlite3_prepare_v2(db_,
            "UPDATE population SET count = ?, born_today = born_today + ? "
            "WHERE region_id = ? AND creature_id = ?;",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, entry.count);
            sqlite3_bind_int(stmt, 2, (int)replacements.Size());
            sqlite3_bind_int(stmt, 3, regionId);
            sqlite3_bind_int(stmt, 4, creatureId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return replacements;
}

Vector3 PopulationManager::PickSpawnPositionInRegion(int regionId) const
{
    for (const RegionInfo& r : regions_)
    {
        if (r.id != regionId)
            continue;
        // Random offset within radius using polar coordinates so the
        // distribution is uniform-ish across the region disc.
        float theta = Random(360.0f);
        float dist  = Random(0.0f, r.radius);
        float x = r.cx + dist * Cos(theta);
        float z = r.cz + dist * Sin(theta);
        return Vector3(x, 0.0f, z);  // client snaps Y to terrain
    }
    return Vector3::ZERO;
}

int PopulationManager::GetPopulation(int regionId, int creatureId) const
{
    auto it = populations_.Find(MakeKey(regionId, creatureId));
    return (it != populations_.End()) ? it->second_.count : 0;
}

int PopulationManager::GetMaxPopulation(int regionId, int creatureId) const
{
    auto it = populations_.Find(MakeKey(regionId, creatureId));
    return (it != populations_.End()) ? it->second_.maxCount : 0;
}

int PopulationManager::FindRegion(float x, float z) const
{
    float bestDistSq = M_INFINITY;
    int bestId = -1;
    for (const RegionInfo& r : regions_)
    {
        float dx = x - r.cx;
        float dz = z - r.cz;
        float distSq = dx * dx + dz * dz;
        if (distSq < r.radius * r.radius && distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = r.id;
        }
    }
    return bestId;
}

} // namespace Urho3D

#endif // URHO3D_DATABASE_SQLITE
