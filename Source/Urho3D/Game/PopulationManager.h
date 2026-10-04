// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#ifdef URHO3D_DATABASE_SQLITE

#include "../Core/Object.h"
#include "../Container/HashMap.h"
#include "../Container/Vector.h"

struct sqlite3;

namespace Urho3D
{

class GameDB;

/// Per-region, per-species population entry (in-memory cache of the population table).
struct PopulationEntry
{
    int   regionId{0};
    int   creatureId{0};
    int   count{0};
    int   maxCount{0};
    /// Death System Phase 1 — kill-driven replacement accumulator.
    /// Each kill adds birthRate to birthAccumulator. While >= 1.0 and below
    /// maxCount, decrement and emit a replacement spawn request.
    float birthAccumulator{0.0f};
    /// Per-species birth rate, loaded from breeding_rules.birth_rate.
    /// Defaults to 1.3 if the row is missing.
    float birthRate{1.3f};
};

/// Result of a RecordKill call: how many replacement spawns the caller
/// should now broadcast to clients. Empty == no replacement triggered.
struct ReplacementSpawn
{
    int regionId{0};
    int creatureId{0};
};

/// Server-side population tracker.
/// Reads from the population + regions tables in game_rules.db.
/// Phase 1: tracking only — age deaths, player kills, no breeding or migration yet.
class URHO3D_API PopulationManager : public Object
{
    URHO3D_OBJECT(PopulationManager, Object);

public:
    explicit PopulationManager(Context* context);

    /// Initialize from an open GameDB. Loads regions and population into memory.
    void Initialize(GameDB* gameDB);

    /// Run one game-day population tick: apply age deaths, reset daily counters, persist.
    void DailyTick();

    /// Decrement population when any animal dies (player kill, drowning, etc.).
    /// Persists the change to SQLite immediately. Also accumulates the
    /// per-species birth rate; the returned vector lists any replacement
    /// spawns the caller should broadcast to connected clients (empty if
    /// the accumulator did not cross 1.0). Death System Phase 1.
    Vector<ReplacementSpawn> RecordKill(int regionId, int creatureId);

    /// Pick a random spawn position inside a region. Returns (x, 0, z) —
    /// the caller (client side) snaps Y to terrain height. Centre + random
    /// offset within the region's radius.
    Vector3 PickSpawnPositionInRegion(int regionId) const;

    /// Get current live count for a species in a region.
    int GetPopulation(int regionId, int creatureId) const;

    /// Get carrying capacity for a species in a region.
    int GetMaxPopulation(int regionId, int creatureId) const;

    /// Find which region ID contains a world position. Returns -1 if none.
    int FindRegion(float x, float z) const;

    /// Return true if successfully initialised.
    bool IsReady() const { return db_ != nullptr; }

private:
    struct RegionInfo { int id; float cx, cz, radius; };

    Vector<RegionInfo> regions_;
    HashMap<unsigned, PopulationEntry> populations_;
    sqlite3* db_{nullptr};

    static unsigned MakeKey(int regionId, int creatureId)
    {
        return (unsigned(regionId) << 16) | unsigned(creatureId & 0xFFFF);
    }
};

} // namespace Urho3D

#endif // URHO3D_DATABASE_SQLITE
