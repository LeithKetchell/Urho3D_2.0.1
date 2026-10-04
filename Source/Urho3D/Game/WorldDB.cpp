// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Game/WorldDB.h"
#include "../IO/Log.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../IO/VectorBuffer.h"

#ifdef URHO3D_DATABASE_SQLITE

#include <SQLite/sqlite3.h>

#include "../DebugNew.h"

namespace Urho3D
{

WorldDB::WorldDB(Context* context) :
    Object(context)
{
}

WorldDB::~WorldDB()
{
    Close();
}

bool WorldDB::Open(const String& dbPath)
{
    if (db_)
        Close();

    int rc = sqlite3_open(dbPath.CString(), &db_);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: Failed to open " + dbPath + ": " + String(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    dbPath_ = dbPath;

    // Enable WAL mode for concurrent reads and crash safety
    char* errMsg = nullptr;
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
    if (errMsg)
    {
        URHO3D_LOGWARNING("WorldDB: WAL mode warning: " + String(errMsg));
        sqlite3_free(errMsg);
    }

    // NORMAL synchronous — survives app crash, power failure risk is minimal
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    URHO3D_LOGINFO("WorldDB: Opened " + dbPath + " (WAL mode)");
    return true;
}

void WorldDB::Close()
{
    if (db_)
    {
        // Final checkpoint before closing
        Checkpoint();
        sqlite3_close(db_);
        db_ = nullptr;
        URHO3D_LOGINFO("WorldDB: Closed");
    }
}

bool WorldDB::EnsureSchema(const String& schemaPath)
{
    if (!db_)
    {
        URHO3D_LOGERROR("WorldDB: Database not open");
        return false;
    }

    auto* fileSystem = GetSubsystem<FileSystem>();
    if (!fileSystem || !fileSystem->FileExists(schemaPath))
    {
        URHO3D_LOGERROR("WorldDB: Schema file not found: " + schemaPath);
        return false;
    }

    File file(context_, schemaPath, FILE_READ);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("WorldDB: Could not open schema file: " + schemaPath);
        return false;
    }

    unsigned size = file.GetSize();
    Vector<char> buffer(size + 1);
    file.Read(&buffer[0], size);
    buffer[size] = '\0';

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, &buffer[0], nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        String error = errMsg ? String(errMsg) : "unknown error";
        sqlite3_free(errMsg);
        URHO3D_LOGERROR("WorldDB: Failed to apply schema: " + error);
        return false;
    }

    URHO3D_LOGINFO("WorldDB: Schema applied from " + schemaPath);

    // Migrations for existing DBs — each tolerates "duplicate column" errors
    static const char* migrations[] = {
        "ALTER TABLE trees ADD COLUMN yields_resin INTEGER DEFAULT 0",
        "ALTER TABLE trees ADD COLUMN last_tapped REAL DEFAULT NULL",
        "ALTER TABLE trees ADD COLUMN growth_stage INTEGER DEFAULT 3",
        nullptr
    };
    for (int i = 0; migrations[i]; ++i)
    {
        char* migErr = nullptr;
        sqlite3_exec(db_, migrations[i], nullptr, nullptr, &migErr);
        if (migErr) sqlite3_free(migErr);  // Ignore — column may already exist
    }

    return true;
}

bool WorldDB::LoadPlayer(int playerId, PlayerState& out)
{
    if (!db_)
        return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT player_id, hp, max_hp, hunger, thirst, stamina, warmth, "
                      "pos_x, pos_y, pos_z, shelter_id, alive "
                      "FROM player_stats WHERE player_id = ?;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: LoadPlayer prepare failed: " + String(sqlite3_errmsg(db_)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, playerId);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.playerId = sqlite3_column_int(stmt, 0);
        out.hp = sqlite3_column_int(stmt, 1);
        out.maxHp = sqlite3_column_int(stmt, 2);
        out.hunger = static_cast<float>(sqlite3_column_double(stmt, 3));
        out.thirst = static_cast<float>(sqlite3_column_double(stmt, 4));
        out.stamina = static_cast<float>(sqlite3_column_double(stmt, 5));
        out.warmth = static_cast<float>(sqlite3_column_double(stmt, 6));
        out.position.x_ = static_cast<float>(sqlite3_column_double(stmt, 7));
        out.position.y_ = static_cast<float>(sqlite3_column_double(stmt, 8));
        out.position.z_ = static_cast<float>(sqlite3_column_double(stmt, 9));
        out.shelterId = sqlite3_column_int(stmt, 10);
        out.alive = sqlite3_column_int(stmt, 11) != 0;
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}

bool WorldDB::SavePlayer(const PlayerState& state)
{
    if (!db_)
        return false;

    // INSERT OR REPLACE — creates if missing, updates if exists
    const char* sql =
        "INSERT OR REPLACE INTO player_stats "
        "(player_id, hp, max_hp, hunger, thirst, stamina, warmth, "
        "pos_x, pos_y, pos_z, shelter_id, alive) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: SavePlayer prepare failed: " + String(sqlite3_errmsg(db_)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, state.playerId);
    sqlite3_bind_int(stmt, 2, state.hp);
    sqlite3_bind_int(stmt, 3, state.maxHp);
    sqlite3_bind_double(stmt, 4, state.hunger);
    sqlite3_bind_double(stmt, 5, state.thirst);
    sqlite3_bind_double(stmt, 6, state.stamina);
    sqlite3_bind_double(stmt, 7, state.warmth);
    sqlite3_bind_double(stmt, 8, state.position.x_);
    sqlite3_bind_double(stmt, 9, state.position.y_);
    sqlite3_bind_double(stmt, 10, state.position.z_);
    sqlite3_bind_int(stmt, 11, state.shelterId);
    sqlite3_bind_int(stmt, 12, state.alive ? 1 : 0);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok)
        URHO3D_LOGERROR("WorldDB: SavePlayer failed: " + String(sqlite3_errmsg(db_)));

    sqlite3_finalize(stmt);
    return ok;
}

bool WorldDB::SavePlayerPosition(int playerId, const Vector3& pos)
{
    if (!db_)
        return false;

    const char* sql = "UPDATE player_stats SET pos_x = ?, pos_y = ?, pos_z = ? WHERE player_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: SavePlayerPosition prepare failed: " + String(sqlite3_errmsg(db_)));
        return false;
    }

    sqlite3_bind_double(stmt, 1, pos.x_);
    sqlite3_bind_double(stmt, 2, pos.y_);
    sqlite3_bind_double(stmt, 3, pos.z_);
    sqlite3_bind_int(stmt, 4, playerId);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool WorldDB::PlayerExists(int playerId)
{
    if (!db_)
        return false;

    const char* sql = "SELECT 1 FROM player_stats WHERE player_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, playerId);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

bool WorldDB::CreatePlayer(int playerId)
{
    if (!db_)
        return false;

    const char* sql =
        "INSERT OR IGNORE INTO player_stats (player_id) VALUES (?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: CreatePlayer prepare failed: " + String(sqlite3_errmsg(db_)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, playerId);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool WorldDB::SaveGameTime(float gameDay, float seasonAngle, float timeOfDay)
{
    if (!db_)
        return false;

    // Use a transaction for atomicity
    sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);

    bool ok = true;
    ok = ok && SaveServerState("game_day", String(gameDay));
    ok = ok && SaveServerState("season_angle", String(seasonAngle));
    ok = ok && SaveServerState("time_of_day", String(timeOfDay));

    if (ok)
        sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    else
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        URHO3D_LOGERROR("WorldDB: SaveGameTime failed, rolled back");
    }

    return ok;
}

bool WorldDB::LoadGameTime(GameTimeState& out)
{
    if (!db_)
        return false;

    String dayStr = LoadServerState("game_day");
    String seasonStr = LoadServerState("season_angle");
    String todStr = LoadServerState("time_of_day");

    if (dayStr.Empty())
        return false;

    out.gameDay = ToFloat(dayStr);
    out.seasonAngle = ToFloat(seasonStr);
    out.timeOfDay = ToFloat(todStr);
    return true;
}

bool WorldDB::SaveServerState(const String& key, const String& value)
{
    if (!db_)
        return false;

    const char* sql = "INSERT OR REPLACE INTO server_state (key, value) VALUES (?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: SaveServerState prepare failed: " + String(sqlite3_errmsg(db_)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, key.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.CString(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

String WorldDB::LoadServerState(const String& key)
{
    if (!db_)
        return String::EMPTY;

    const char* sql = "SELECT value FROM server_state WHERE key = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return String::EMPTY;

    sqlite3_bind_text(stmt, 1, key.CString(), -1, SQLITE_TRANSIENT);

    String result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (val)
            result = val;
    }

    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// Placed Crops
// ---------------------------------------------------------------------------

int WorldDB::InsertPlacedCrop(int ownerId, int seedItemId, float px, float py, float pz,
                              float plantedDay, int regionId)
{
    if (!db_) return -1;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT INTO placed_crops (owner_id, seed_item_id, pos_x, pos_y, pos_z, "
        "planted_day, growth_stage, region_id) VALUES (?, ?, ?, ?, ?, ?, 0, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, ownerId);
    sqlite3_bind_int(stmt, 2, seedItemId);
    sqlite3_bind_double(stmt, 3, px);
    sqlite3_bind_double(stmt, 4, py);
    sqlite3_bind_double(stmt, 5, pz);
    sqlite3_bind_double(stmt, 6, plantedDay);
    sqlite3_bind_int(stmt, 7, regionId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    return (int)sqlite3_last_insert_rowid(db_);
}

bool WorldDB::RemovePlacedCrop(int cropId)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "DELETE FROM placed_crops WHERE crop_id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, cropId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool WorldDB::UpdateCropGrowthStage(int cropId, int newStage)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE placed_crops SET growth_stage = ? WHERE crop_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, newStage);
    sqlite3_bind_int(stmt, 2, cropId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

Vector<WorldDB::PlacedCropInfo> WorldDB::GetAllPlacedCrops()
{
    Vector<PlacedCropInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT crop_id, owner_id, seed_item_id, pos_x, pos_y, pos_z, "
        "planted_day, growth_stage, region_id FROM placed_crops",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        PlacedCropInfo c;
        c.cropId      = sqlite3_column_int(stmt, 0);
        c.ownerId     = sqlite3_column_int(stmt, 1);
        c.seedItemId  = sqlite3_column_int(stmt, 2);
        c.posX        = (float)sqlite3_column_double(stmt, 3);
        c.posY        = (float)sqlite3_column_double(stmt, 4);
        c.posZ        = (float)sqlite3_column_double(stmt, 5);
        c.plantedDay  = (float)sqlite3_column_double(stmt, 6);
        c.growthStage = sqlite3_column_int(stmt, 7);
        c.regionId    = sqlite3_column_int(stmt, 8);
        result.Push(c);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool WorldDB::GetPlacedCrop(int cropId, PlacedCropInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT crop_id, owner_id, seed_item_id, pos_x, pos_y, pos_z, "
        "planted_day, growth_stage, region_id FROM placed_crops WHERE crop_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, cropId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.cropId      = sqlite3_column_int(stmt, 0);
        out.ownerId     = sqlite3_column_int(stmt, 1);
        out.seedItemId  = sqlite3_column_int(stmt, 2);
        out.posX        = (float)sqlite3_column_double(stmt, 3);
        out.posY        = (float)sqlite3_column_double(stmt, 4);
        out.posZ        = (float)sqlite3_column_double(stmt, 5);
        out.plantedDay  = (float)sqlite3_column_double(stmt, 6);
        out.growthStage = sqlite3_column_int(stmt, 7);
        out.regionId    = sqlite3_column_int(stmt, 8);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// ---------------------------------------------------------------------------
// Crop Rotation History
// ---------------------------------------------------------------------------

bool WorldDB::GetCropHistory(int tileX, int tileZ, CropHistoryEntry& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT tile_x, tile_z, last_seed_id, last_harvest_day, consecutive "
        "FROM crop_history WHERE tile_x = ? AND tile_z = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, tileX);
    sqlite3_bind_int(stmt, 2, tileZ);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.tileX          = sqlite3_column_int(stmt, 0);
        out.tileZ          = sqlite3_column_int(stmt, 1);
        out.lastSeedId     = sqlite3_column_int(stmt, 2);
        out.lastHarvestDay = (float)sqlite3_column_double(stmt, 3);
        out.consecutive    = sqlite3_column_int(stmt, 4);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

void WorldDB::RecordCropHarvest(int tileX, int tileZ, int seedItemId, float gameDay)
{
    if (!db_) return;

    CropHistoryEntry existing;
    bool hadPrevious = GetCropHistory(tileX, tileZ, existing);

    int consecutive = 1;
    if (hadPrevious && existing.lastSeedId == seedItemId)
        consecutive = existing.consecutive + 1;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT OR REPLACE INTO crop_history (tile_x, tile_z, last_seed_id, last_harvest_day, consecutive) "
        "VALUES (?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return;

    sqlite3_bind_int(stmt, 1, tileX);
    sqlite3_bind_int(stmt, 2, tileZ);
    sqlite3_bind_int(stmt, 3, seedItemId);
    sqlite3_bind_double(stmt, 4, gameDay);
    sqlite3_bind_int(stmt, 5, consecutive);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---------------------------------------------------------------------------
// Irrigation Channels
// ---------------------------------------------------------------------------

int WorldDB::InsertIrrigationChannel(int ownerId, float waterX, float waterZ, float farmX, float farmZ)
{
    if (!db_) return -1;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT INTO irrigation_channels (owner_id, water_x, water_z, farm_x, farm_z) "
        "VALUES (?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, ownerId);
    sqlite3_bind_double(stmt, 2, waterX);
    sqlite3_bind_double(stmt, 3, waterZ);
    sqlite3_bind_double(stmt, 4, farmX);
    sqlite3_bind_double(stmt, 5, farmZ);
    sqlite3_step(stmt);
    int id = (int)sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return id;
}

Vector<WorldDB::IrrigationChannel> WorldDB::GetAllIrrigationChannels()
{
    Vector<IrrigationChannel> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT channel_id, owner_id, water_x, water_z, farm_x, farm_z FROM irrigation_channels",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        IrrigationChannel ch;
        ch.channelId = sqlite3_column_int(stmt, 0);
        ch.ownerId   = sqlite3_column_int(stmt, 1);
        ch.waterX    = (float)sqlite3_column_double(stmt, 2);
        ch.waterZ    = (float)sqlite3_column_double(stmt, 3);
        ch.farmX     = (float)sqlite3_column_double(stmt, 4);
        ch.farmZ     = (float)sqlite3_column_double(stmt, 5);
        result.Push(ch);
    }
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// Well State
// ---------------------------------------------------------------------------

bool WorldDB::GetWellState(int buildingId, WellState& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT building_id, water_reserve, max_yield, last_refill FROM well_state WHERE building_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, buildingId);
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.buildingId   = sqlite3_column_int(stmt, 0);
        out.waterReserve = sqlite3_column_int(stmt, 1);
        out.maxYield     = sqlite3_column_int(stmt, 2);
        out.lastRefill   = (float)sqlite3_column_double(stmt, 3);
        sqlite3_finalize(stmt);
        return true;
    }
    sqlite3_finalize(stmt);
    return false;
}

bool WorldDB::SaveWellState(const WellState& state)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT OR REPLACE INTO well_state (building_id, water_reserve, max_yield, last_refill) "
        "VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, state.buildingId);
    sqlite3_bind_int(stmt, 2, state.waterReserve);
    sqlite3_bind_int(stmt, 3, state.maxYield);
    sqlite3_bind_double(stmt, 4, state.lastRefill);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int WorldDB::DepleteWell(int buildingId, int amount)
{
    if (!db_) return 0;

    WellState ws;
    if (!GetWellState(buildingId, ws))
        return 0;

    int consumed = Min(ws.waterReserve, amount);
    ws.waterReserve -= consumed;
    SaveWellState(ws);
    return consumed;
}

void WorldDB::RefillWells(float rainfallIntensity, float gameDay)
{
    if (!db_ || rainfallIntensity <= 0.0f) return;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE well_state SET water_reserve = MIN(water_reserve + CAST(? * max_yield AS INTEGER), max_yield * 10), "
        "last_refill = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return;

    sqlite3_bind_double(stmt, 1, rainfallIntensity * 10.0);
    sqlite3_bind_double(stmt, 2, gameDay);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---------------------------------------------------------------------------
// Fire Pits
// ---------------------------------------------------------------------------

bool WorldDB::SaveFirePits(const Vector<FirePitDBInfo>& pits)
{
    if (!db_) return false;

    // Clear existing rows and re-insert (full snapshot)
    Execute("DELETE FROM fire_pits");

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT INTO fire_pits (pit_id, pos_x, pos_y, pos_z, fuel, max_fuel, "
        "burn_rate, wetness, state, region_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    for (unsigned i = 0; i < pits.Size(); ++i)
    {
        const FirePitDBInfo& p = pits[i];
        sqlite3_bind_int(stmt, 1, static_cast<int>(p.pitId));
        sqlite3_bind_double(stmt, 2, p.position.x_);
        sqlite3_bind_double(stmt, 3, p.position.y_);
        sqlite3_bind_double(stmt, 4, p.position.z_);
        sqlite3_bind_double(stmt, 5, p.fuel);
        sqlite3_bind_double(stmt, 6, p.maxFuel);
        sqlite3_bind_double(stmt, 7, p.burnRate);
        sqlite3_bind_double(stmt, 8, p.wetness);
        sqlite3_bind_int(stmt, 9, p.state);
        sqlite3_bind_int(stmt, 10, p.regionId);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    URHO3D_LOGINFOF("WorldDB: Saved %u fire pits", pits.Size());
    return true;
}

Vector<FirePitDBInfo> WorldDB::LoadFirePits()
{
    Vector<FirePitDBInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT pit_id, pos_x, pos_y, pos_z, fuel, max_fuel, "
        "burn_rate, wetness, state, region_id FROM fire_pits",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        FirePitDBInfo p;
        p.pitId    = static_cast<unsigned>(sqlite3_column_int(stmt, 0));
        p.position = Vector3(
            static_cast<float>(sqlite3_column_double(stmt, 1)),
            static_cast<float>(sqlite3_column_double(stmt, 2)),
            static_cast<float>(sqlite3_column_double(stmt, 3)));
        p.fuel     = static_cast<float>(sqlite3_column_double(stmt, 4));
        p.maxFuel  = static_cast<float>(sqlite3_column_double(stmt, 5));
        p.burnRate = static_cast<float>(sqlite3_column_double(stmt, 6));
        p.wetness  = static_cast<float>(sqlite3_column_double(stmt, 7));
        p.state    = sqlite3_column_int(stmt, 8);
        p.regionId = sqlite3_column_int(stmt, 9);
        result.Push(p);
    }

    sqlite3_finalize(stmt);
    URHO3D_LOGINFOF("WorldDB: Loaded %u fire pits", result.Size());
    return result;
}

bool WorldDB::Checkpoint()
{
    if (!db_)
        return false;

    int rc = sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGWARNING("WorldDB: Checkpoint returned " + String(rc));
        return false;
    }

    return true;
}

bool WorldDB::Backup(const String& backupPath)
{
    if (!db_)
    {
        URHO3D_LOGERROR("WorldDB: Cannot backup — database not open");
        return false;
    }

    // Flush WAL first
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_FULL, nullptr, nullptr);

    // Use SQLite backup API — safe even during concurrent writes
    sqlite3* destDb = nullptr;
    int rc = sqlite3_open(backupPath.CString(), &destDb);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: Backup failed to open destination: " + backupPath);
        if (destDb)
            sqlite3_close(destDb);
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(destDb, "main", db_, "main");
    if (!backup)
    {
        URHO3D_LOGERROR("WorldDB: Backup init failed: " + String(sqlite3_errmsg(destDb)));
        sqlite3_close(destDb);
        return false;
    }

    // Copy all pages in one step (-1 = all)
    rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    sqlite3_close(destDb);

    if (rc != SQLITE_DONE)
    {
        URHO3D_LOGERROR("WorldDB: Backup failed with code " + String(rc));
        return false;
    }

    URHO3D_LOGINFO("WorldDB: Backed up to " + backupPath);
    return true;
}

bool WorldDB::Execute(const String& sql)
{
    if (!db_)
        return false;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.CString(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        String error = errMsg ? String(errMsg) : "unknown error";
        sqlite3_free(errMsg);
        URHO3D_LOGERROR("WorldDB: Execute failed: " + error);
        return false;
    }
    return true;
}

// =============================================================================
// Map Backups — pristine map BLOBs for reset/restore
// =============================================================================

bool WorldDB::HasMapBackup(const String& filename)
{
    if (!db_)
        return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT 1 FROM map_backups WHERE filename = ?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, filename.CString(), -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool WorldDB::BackupMap(const String& filename, Image* image)
{
    if (!db_ || !image)
        return false;

    // Don't overwrite an existing backup — preserve the original
    if (HasMapBackup(filename))
    {
        URHO3D_LOGINFO("WorldDB: Map backup already exists for " + filename + " — skipping");
        return true;
    }

    // Serialize image to PNG in memory
    VectorBuffer pngBuf;
    if (!image->Save(pngBuf))
    {
        URHO3D_LOGERROR("WorldDB: Failed to serialize " + filename + " to PNG");
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO map_backups (filename, data) VALUES (?, ?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        URHO3D_LOGERROR("WorldDB: Failed to prepare map backup insert: " + String(sqlite3_errmsg(db_)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, filename.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, pngBuf.GetData(), pngBuf.GetSize(), SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        URHO3D_LOGERROR("WorldDB: Failed to store map backup for " + filename);
        return false;
    }

    URHO3D_LOGINFO("WorldDB: Backed up pristine " + filename + " (" + String(pngBuf.GetSize()) + " bytes)");
    return true;
}

// =============================================================================
// Inventory — CRUD on player_inventory (game_world.db)
// =============================================================================

Vector<InventorySlot> WorldDB::GetPlayerInventory(int playerId)
{
    Vector<InventorySlot> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, quantity, durability, slot FROM player_inventory WHERE player_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, playerId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        InventorySlot s;
        s.itemId = sqlite3_column_int(stmt, 0);
        s.quantity = sqlite3_column_int(stmt, 1);
        s.durability = sqlite3_column_int(stmt, 2);
        const char* slot = (const char*)sqlite3_column_text(stmt, 3);
        s.slotType = slot ? slot : "bag";
        result.Push(s);
    }
    sqlite3_finalize(stmt);
    return result;
}

int WorldDB::GetItemCount(int playerId, int itemId)
{
    if (!db_) return 0;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT COALESCE(SUM(quantity), 0) FROM player_inventory "
        "WHERE player_id = ? AND item_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, itemId);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool WorldDB::AddItemToInventory(int playerId, int itemId, int qty,
                                  int stackMax, float itemWeight, int itemDurability,
                                  float maxWeight, int availableSlots)
{
    if (!db_ || qty <= 0) return false;

    // Try to stack into existing bag slot first
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT quantity FROM player_inventory "
        "WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, itemId);

    bool existingSlot = false;
    int existingQty = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        existingQty = sqlite3_column_int(stmt, 0);
        existingSlot = true;
    }
    sqlite3_finalize(stmt);

    if (existingSlot && existingQty + qty <= stackMax)
    {
        // Update existing stack
        rc = sqlite3_prepare_v2(db_,
            "UPDATE player_inventory SET quantity = quantity + ? "
            "WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, qty);
        sqlite3_bind_int(stmt, 2, playerId);
        sqlite3_bind_int(stmt, 3, itemId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
    }

    if (!existingSlot)
    {
        // Check available slots (caller computed this from rules + container bonus)
        if (availableSlots <= 0)
            return false;

        // Insert new slot
        rc = sqlite3_prepare_v2(db_,
            "INSERT INTO player_inventory (player_id, item_id, quantity, durability, slot) "
            "VALUES (?, ?, ?, ?, 'bag')",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, itemId);
        sqlite3_bind_int(stmt, 3, qty);
        sqlite3_bind_int(stmt, 4, itemDurability > 0 ? itemDurability : -1);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
    }

    // Existing slot but would exceed stack_max
    return false;
}

bool WorldDB::RemoveItemFromInventory(int playerId, int itemId, int qty)
{
    if (!db_ || qty <= 0) return false;

    int have = GetItemCount(playerId, itemId);
    if (have < qty)
        return false;

    // Decrement from bag slot first
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT quantity FROM player_inventory "
        "WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, itemId);

    int bagQty = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        bagQty = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (bagQty <= qty)
    {
        rc = sqlite3_prepare_v2(db_,
            "DELETE FROM player_inventory WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, itemId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    else
    {
        rc = sqlite3_prepare_v2(db_,
            "UPDATE player_inventory SET quantity = quantity - ? "
            "WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, qty);
        sqlite3_bind_int(stmt, 2, playerId);
        sqlite3_bind_int(stmt, 3, itemId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return true;
}

Vector<InventorySlot> WorldDB::GetPlayerBagItems(int playerId)
{
    Vector<InventorySlot> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, quantity, durability, slot FROM player_inventory "
        "WHERE player_id = ? AND slot = 'bag'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, playerId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        InventorySlot s;
        s.itemId = sqlite3_column_int(stmt, 0);
        s.quantity = sqlite3_column_int(stmt, 1);
        s.durability = sqlite3_column_int(stmt, 2);
        s.slotType = "bag";
        result.Push(s);
    }
    sqlite3_finalize(stmt);
    return result;
}

int WorldDB::GetOccupiedBagSlots(int playerId)
{
    if (!db_) return 0;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM player_inventory WHERE player_id = ? AND slot = 'bag'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, playerId);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool WorldDB::EquipItem(int playerId, int itemId, const String& slot)
{
    if (!db_ || slot.Empty()) return false;

    // Check item exists in bag
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT quantity FROM player_inventory "
        "WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, itemId);

    int bagQty = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        bagQty = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (bagQty < 1)
        return false;

    // Check target slot is empty
    if (GetEquippedItem(playerId, slot) != 0)
        return false;

    // Remove one from bag
    if (bagQty <= 1)
    {
        rc = sqlite3_prepare_v2(db_,
            "DELETE FROM player_inventory WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, itemId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    else
    {
        rc = sqlite3_prepare_v2(db_,
            "UPDATE player_inventory SET quantity = quantity - 1 "
            "WHERE player_id = ? AND item_id = ? AND slot = 'bag'",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, itemId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Insert into equipment slot
    rc = sqlite3_prepare_v2(db_,
        "INSERT INTO player_inventory (player_id, item_id, quantity, durability, slot) "
        "VALUES (?, ?, 1, -1, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_int(stmt, 2, itemId);
    sqlite3_bind_text(stmt, 3, slot.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return true;
}

bool WorldDB::UnequipItem(int playerId, const String& slot, int& outItemId,
                           int stackMax, float itemWeight, int itemDurability,
                           float maxWeight, int availableSlots)
{
    if (!db_ || slot.Empty()) return false;

    outItemId = GetEquippedItem(playerId, slot);
    if (outItemId == 0)
        return false;

    // Remove from equipment slot
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "DELETE FROM player_inventory WHERE player_id = ? AND slot = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_text(stmt, 2, slot.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Add back to bag
    if (!AddItemToInventory(playerId, outItemId, 1, stackMax, itemWeight, itemDurability,
                            maxWeight, availableSlots))
    {
        // Bag full — re-insert into equipment slot (rollback)
        rc = sqlite3_prepare_v2(db_,
            "INSERT INTO player_inventory (player_id, item_id, quantity, durability, slot) "
            "VALUES (?, ?, 1, -1, ?)",
            -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, playerId);
            sqlite3_bind_int(stmt, 2, outItemId);
            sqlite3_bind_text(stmt, 3, slot.CString(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        outItemId = 0;
        return false;
    }

    return true;
}

int WorldDB::GetEquippedItem(int playerId, const String& slot)
{
    if (!db_) return 0;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id FROM player_inventory WHERE player_id = ? AND slot = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_text(stmt, 2, slot.CString(), -1, SQLITE_TRANSIENT);

    int itemId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        itemId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return itemId;
}

int WorldDB::DeductDurability(int playerId, const String& slot, int amount)
{
    if (!db_ || amount <= 0) return -1;

    // Read current durability
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT durability FROM player_inventory WHERE player_id = ? AND slot = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, playerId);
    sqlite3_bind_text(stmt, 2, slot.CString(), -1, SQLITE_TRANSIENT);

    int current = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        current = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    // -1 means no durability tracking (infinite)
    if (current < 0)
        return -1;

    int remaining = current - amount;
    if (remaining <= 0)
    {
        // Item broke — remove it
        rc = sqlite3_prepare_v2(db_,
            "DELETE FROM player_inventory WHERE player_id = ? AND slot = ?",
            -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, playerId);
            sqlite3_bind_text(stmt, 2, slot.CString(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return 0;
    }

    // Update remaining durability
    rc = sqlite3_prepare_v2(db_,
        "UPDATE player_inventory SET durability = ? WHERE player_id = ? AND slot = ?",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, remaining);
        sqlite3_bind_int(stmt, 2, playerId);
        sqlite3_bind_text(stmt, 3, slot.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return remaining;
}

// =============================================================================
// Placed Buildings — CRUD on placed_buildings (game_world.db)
// =============================================================================

static PlacedBuildingDBInfo ReadWorldPlacedBuildingRow(sqlite3_stmt* stmt)
{
    PlacedBuildingDBInfo info;
    info.id = sqlite3_column_int(stmt, 0);
    info.buildingId = sqlite3_column_int(stmt, 1);
    info.ownerId = sqlite3_column_int(stmt, 2);
    info.posX = (float)sqlite3_column_double(stmt, 3);
    info.posY = (float)sqlite3_column_double(stmt, 4);
    info.posZ = (float)sqlite3_column_double(stmt, 5);
    info.rotation = (float)sqlite3_column_double(stmt, 6);
    info.hp = sqlite3_column_int(stmt, 7);
    info.builtDay = sqlite3_column_int(stmt, 8);
    info.lastRepair = sqlite3_column_int(stmt, 9);
    info.gateOpen = sqlite3_column_int(stmt, 10) != 0;
    info.snappedTo = sqlite3_column_int(stmt, 11);
    return info;
}

int WorldDB::InsertPlacedBuilding(int buildingId, int ownerId, float px, float py, float pz,
                                   float rotation, int hp, int builtDay, int snappedTo,
                                   int parentId)
{
    if (!db_) return -1;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT INTO placed_buildings (building_id, owner_id, pos_x, pos_y, pos_z, "
        "rotation, hp, built_day, snapped_to, parent_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, buildingId);
    sqlite3_bind_int(stmt, 2, ownerId);
    sqlite3_bind_double(stmt, 3, px);
    sqlite3_bind_double(stmt, 4, py);
    sqlite3_bind_double(stmt, 5, pz);
    sqlite3_bind_double(stmt, 6, rotation);
    sqlite3_bind_int(stmt, 7, hp);
    sqlite3_bind_int(stmt, 8, builtDay);
    sqlite3_bind_int(stmt, 9, snappedTo);
    sqlite3_bind_int(stmt, 10, parentId);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;

    return (int)sqlite3_last_insert_rowid(db_);
}

bool WorldDB::RemovePlacedBuilding(int placedId)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "DELETE FROM placed_buildings WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, placedId);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && changes > 0;
}

Vector<int> WorldDB::GetChildBuildingIds(int parentId)
{
    Vector<int> ids;
    if (!db_ || parentId <= 0) return ids;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id FROM placed_buildings WHERE parent_id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return ids;

    sqlite3_bind_int(stmt, 1, parentId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        ids.Push(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    return ids;
}

int WorldDB::RemoveChildBuildings(int parentId)
{
    if (!db_ || parentId <= 0) return 0;

    // First collect IDs of children to remove (for broadcast)
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "DELETE FROM placed_buildings WHERE parent_id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, parentId);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? changes : 0;
}

Vector<PlacedBuildingDBInfo> WorldDB::GetAllPlacedBuildings()
{
    Vector<PlacedBuildingDBInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, building_id, owner_id, pos_x, pos_y, pos_z, rotation, "
        "hp, built_day, last_repair, gate_open, snapped_to FROM placed_buildings",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadWorldPlacedBuildingRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

Vector<PlacedBuildingDBInfo> WorldDB::GetPlacedBuildingsByOwner(int ownerId)
{
    Vector<PlacedBuildingDBInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, building_id, owner_id, pos_x, pos_y, pos_z, rotation, "
        "hp, built_day, last_repair, gate_open, snapped_to "
        "FROM placed_buildings WHERE owner_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, ownerId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadWorldPlacedBuildingRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

bool WorldDB::UpdateBuildingHp(int placedId, int newHp)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE placed_buildings SET hp = ? WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, newHp);
    sqlite3_bind_int(stmt, 2, placedId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool WorldDB::SetGateOpen(int placedId, bool open)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE placed_buildings SET gate_open = ? WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, open ? 1 : 0);
    sqlite3_bind_int(stmt, 2, placedId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool WorldDB::SetLastRepair(int placedId, int gameDay)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE placed_buildings SET last_repair = ? WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, gameDay);
    sqlite3_bind_int(stmt, 2, placedId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool WorldDB::GetPlacedBuilding(int placedId, PlacedBuildingDBInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, building_id, owner_id, pos_x, pos_y, pos_z, rotation, "
        "hp, built_day, last_repair, gate_open, snapped_to "
        "FROM placed_buildings WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, placedId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out = ReadWorldPlacedBuildingRow(stmt);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

int WorldDB::GetPlacedBuildingOwner(int placedId)
{
    if (!db_) return -1;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT owner_id FROM placed_buildings WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, placedId);
    int owner = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        owner = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return owner;
}

bool WorldDB::UpdateBuildingHP(int placedId, int newHp)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE placed_buildings SET hp = ? WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, newHp);
    sqlite3_bind_int(stmt, 2, placedId);
    bool result = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return result;
}

bool WorldDB::UpdateBuildingStorage(int placedId, const String& storageJson)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE placed_buildings SET storage = ? WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, storageJson.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, placedId);
    bool result = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return result;
}

String WorldDB::GetBuildingStorageJson(int placedId)
{
    if (!db_) return String::EMPTY;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT storage FROM placed_buildings WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return String::EMPTY;
    sqlite3_bind_int(stmt, 1, placedId);
    String result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* text = (const char*)sqlite3_column_text(stmt, 0);
        if (text)
            result = text;
    }
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// NPC Social Bonds
// ---------------------------------------------------------------------------

float WorldDB::GetBondFamiliarity(unsigned npcA, unsigned npcB)
{
    if (!db_) return 0.0f;
    unsigned lo = Min(npcA, npcB);
    unsigned hi = Max(npcA, npcB);

    sqlite3_stmt* stmt = nullptr;
    float fam = 0.0f;
    if (sqlite3_prepare_v2(db_, "SELECT familiarity FROM npc_bonds WHERE npc_a = ? AND npc_b = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)lo);
        sqlite3_bind_int(stmt, 2, (int)hi);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            fam = (float)sqlite3_column_double(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return fam;
}

void WorldDB::IncrementFamiliarity(unsigned npcA, unsigned npcB, float amount)
{
    if (!db_) return;
    unsigned lo = Min(npcA, npcB);
    unsigned hi = Max(npcA, npcB);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO npc_bonds (npc_a, npc_b, familiarity) VALUES (?, ?, ?)"
        " ON CONFLICT(npc_a, npc_b) DO UPDATE SET familiarity = MIN(100.0, familiarity + ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)lo);
        sqlite3_bind_int(stmt, 2, (int)hi);
        sqlite3_bind_double(stmt, 3, amount);
        sqlite3_bind_double(stmt, 4, amount);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void WorldDB::SetBondType(unsigned npcA, unsigned npcB, const String& bondType)
{
    if (!db_) return;
    unsigned lo = Min(npcA, npcB);
    unsigned hi = Max(npcA, npcB);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO npc_bonds (npc_a, npc_b, bond_type) VALUES (?, ?, ?)"
        " ON CONFLICT(npc_a, npc_b) DO UPDATE SET bond_type = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)lo);
        sqlite3_bind_int(stmt, 2, (int)hi);
        sqlite3_bind_text(stmt, 3, bondType.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, bondType.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

String WorldDB::GetBondType(unsigned npcA, unsigned npcB)
{
    if (!db_) return String::EMPTY;
    unsigned lo = Min(npcA, npcB);
    unsigned hi = Max(npcA, npcB);

    sqlite3_stmt* stmt = nullptr;
    String result;
    if (sqlite3_prepare_v2(db_, "SELECT bond_type FROM npc_bonds WHERE npc_a = ? AND npc_b = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)lo);
        sqlite3_bind_int(stmt, 2, (int)hi);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            result = (const char*)sqlite3_column_text(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Animal Production Tracking (Phase 16)
// ---------------------------------------------------------------------------

void WorldDB::LogAnimalProduction(unsigned animalId, int speciesId, const String& product,
                                   int quantity, unsigned tamerId, unsigned campfireId, float gameDay)
{
    if (!db_) return;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO animal_production (animal_id, species_id, product, quantity, "
                      "tamer_id, campfire_id, game_day) VALUES (?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)animalId);
        sqlite3_bind_int(stmt, 2, speciesId);
        sqlite3_bind_text(stmt, 3, product.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, quantity);
        sqlite3_bind_int(stmt, 5, (int)tamerId);
        sqlite3_bind_int(stmt, 6, (int)campfireId);
        sqlite3_bind_double(stmt, 7, (double)gameDay);
        if (sqlite3_step(stmt) != SQLITE_DONE)
            URHO3D_LOGERRORF("LogAnimalProduction: step failed for %s", product.CString());
    }
    sqlite3_finalize(stmt);
}

int WorldDB::GetSettlementProduction(unsigned campfireId, const String& product)
{
    if (!db_) return 0;
    int total = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COALESCE(SUM(quantity),0) FROM animal_production "
                                "WHERE campfire_id = ? AND product = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)campfireId);
        sqlite3_bind_text(stmt, 2, product.CString(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return total;
}

// ---------------------------------------------------------------------------
// Water Bodies
// ---------------------------------------------------------------------------

int WorldDB::InsertWaterBody(int terrainId, int areaPixels, float areaWorld,
                              float cx, float cz, float minDepth, float maxDepth)
{
    if (!db_) return -1;
    sqlite3_stmt* stmt = nullptr;
    int bodyId = -1;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO water_bodies (terrain_id, area_pixels, area_world, center_x, center_z, min_depth, max_depth) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        sqlite3_bind_int(stmt, 2, areaPixels);
        sqlite3_bind_double(stmt, 3, areaWorld);
        sqlite3_bind_double(stmt, 4, cx);
        sqlite3_bind_double(stmt, 5, cz);
        sqlite3_bind_double(stmt, 6, minDepth);
        sqlite3_bind_double(stmt, 7, maxDepth);
        if (sqlite3_step(stmt) == SQLITE_DONE)
            bodyId = (int)sqlite3_last_insert_rowid(db_);
        sqlite3_finalize(stmt);
    }
    return bodyId;
}

bool WorldDB::InsertFishSpawnPoint(int bodyId, float px, float pz, float depth)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO fish_spawn_points (body_id, pos_x, pos_z, depth) VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, bodyId);
        sqlite3_bind_double(stmt, 2, px);
        sqlite3_bind_double(stmt, 3, pz);
        sqlite3_bind_double(stmt, 4, depth);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    return ok;
}

Vector<WorldDB::WaterBodyInfo> WorldDB::GetWaterBodies(int terrainId)
{
    Vector<WaterBodyInfo> result;
    if (!db_) return result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT body_id, terrain_id, area_pixels, area_world, center_x, center_z, min_depth, max_depth "
        "FROM water_bodies WHERE terrain_id = ? ORDER BY area_pixels DESC",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            WaterBodyInfo b;
            b.bodyId = sqlite3_column_int(stmt, 0);
            b.terrainId = sqlite3_column_int(stmt, 1);
            b.areaPixels = sqlite3_column_int(stmt, 2);
            b.areaWorld = (float)sqlite3_column_double(stmt, 3);
            b.centerX = (float)sqlite3_column_double(stmt, 4);
            b.centerZ = (float)sqlite3_column_double(stmt, 5);
            b.minDepth = (float)sqlite3_column_double(stmt, 6);
            b.maxDepth = (float)sqlite3_column_double(stmt, 7);
            result.Push(b);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

Vector<WorldDB::FishSpawnPoint> WorldDB::GetFishSpawnPoints(int bodyId)
{
    Vector<FishSpawnPoint> result;
    if (!db_) return result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, body_id, pos_x, pos_z, depth FROM fish_spawn_points WHERE body_id = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, bodyId);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            FishSpawnPoint p;
            p.id = sqlite3_column_int(stmt, 0);
            p.bodyId = sqlite3_column_int(stmt, 1);
            p.posX = (float)sqlite3_column_double(stmt, 2);
            p.posZ = (float)sqlite3_column_double(stmt, 3);
            p.depth = (float)sqlite3_column_double(stmt, 4);
            result.Push(p);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int WorldDB::GetTotalFishSpawnCount(int terrainId)
{
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM fish_spawn_points f JOIN water_bodies w ON f.body_id = w.body_id "
        "WHERE w.terrain_id = ?", -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

bool WorldDB::HasWaterBodies(int terrainId)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    bool has = false;
    if (sqlite3_prepare_v2(db_,
        "SELECT 1 FROM water_bodies WHERE terrain_id = ? LIMIT 1",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            has = true;
        sqlite3_finalize(stmt);
    }
    return has;
}

// ---------------------------------------------------------------------------
// Settlement Patches
// ---------------------------------------------------------------------------

bool WorldDB::IsSettlementPatchFree(int terrainId, int sx, int sz)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    bool free = true;
    if (sqlite3_prepare_v2(db_,
        "SELECT 1 FROM settlement_patches WHERE terrain_id = ? AND spatch_x = ? AND spatch_z = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        sqlite3_bind_int(stmt, 2, sx);
        sqlite3_bind_int(stmt, 3, sz);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            free = false;
        sqlite3_finalize(stmt);
    }
    return free;
}

bool WorldDB::ClaimSettlementPatch(int terrainId, int sx, int sz, int settlementId)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db_,
        "INSERT OR IGNORE INTO settlement_patches (terrain_id, spatch_x, spatch_z, settlement_id) "
        "VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        sqlite3_bind_int(stmt, 2, sx);
        sqlite3_bind_int(stmt, 3, sz);
        sqlite3_bind_int(stmt, 4, settlementId);
        ok = (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0);
        sqlite3_finalize(stmt);
    }
    if (ok)
        URHO3D_LOGINFOF("[WorldDB] Settlement %d claimed patch (%d, %d) on terrain %d",
            settlementId, sx, sz, terrainId);
    return ok;
}

bool WorldDB::GetSettlementPatch(int settlementId, int& outTerrainId, int& outSx, int& outSz)
{
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db_,
        "SELECT terrain_id, spatch_x, spatch_z FROM settlement_patches WHERE settlement_id = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, settlementId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            outTerrainId = sqlite3_column_int(stmt, 0);
            outSx = sqlite3_column_int(stmt, 1);
            outSz = sqlite3_column_int(stmt, 2);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

int WorldDB::GetFreeSettlementPatchCount(int terrainId)
{
    if (!db_) return 0;
    // Total possible = 16*16 = 256, minus claimed
    sqlite3_stmt* stmt = nullptr;
    int claimed = 0;
    if (sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM settlement_patches WHERE terrain_id = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            claimed = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return 256 - claimed;
}

int WorldDB::GetSettlementAtPatch(int terrainId, int sx, int sz)
{
    if (!db_) return -1;
    sqlite3_stmt* stmt = nullptr;
    int result = -1;
    if (sqlite3_prepare_v2(db_,
        "SELECT settlement_id FROM settlement_patches WHERE terrain_id = ? AND spatch_x = ? AND spatch_z = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, terrainId);
        sqlite3_bind_int(stmt, 2, sx);
        sqlite3_bind_int(stmt, 3, sz);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            result = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return result;
}

}

#endif // URHO3D_DATABASE_SQLITE
