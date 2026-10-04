// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Math/Vector3.h"
#include "../Game/GameDB.h"  // InventorySlot, PlacedBuildingDBInfo
#include "../Resource/Image.h"

#ifdef URHO3D_DATABASE_SQLITE

struct sqlite3;

namespace Urho3D
{

/// Player runtime state loaded/saved by WorldDB.
struct PlayerState
{
    int playerId;
    int hp{20};
    int maxHp{20};
    float hunger{100.0f};
    float thirst{100.0f};
    float stamina{100.0f};
    float warmth{15.0f};
    Vector3 position;
    int shelterId{0};
    bool alive{true};
};

/// Server time state.
struct GameTimeState
{
    float gameDay{0.0f};
    float seasonAngle{0.0f};
    float timeOfDay{0.0f};
};

/// Fire pit state for WorldDB persistence.
struct FirePitDBInfo
{
    unsigned pitId{0};
    Vector3 position;
    float fuel{0.0f};
    float maxFuel{600.0f};
    float burnRate{1.0f};
    float wetness{0.0f};
    int state{0};       // 0=UNLIT, 1=LIT, 2=EMBERS, 3=COLD
    int regionId{0};
};

/// Dynamic world database — read-write runtime state.
/// Separate from GameDB (static rules). THIS is the save file.
/// Uses WAL mode for concurrent reads and crash safety.
class URHO3D_API WorldDB : public Object
{
    URHO3D_OBJECT(WorldDB, Object);

public:
    explicit WorldDB(Context* context);
    ~WorldDB() override;

    /// Open or create the world database. Enables WAL mode.
    bool Open(const String& dbPath);
    /// Close the database.
    void Close();
    /// Return true if database is open.
    bool IsOpen() const { return db_ != nullptr; }
    /// Get raw sqlite3 handle for direct queries (e.g. admin UI table browser).
    sqlite3* GetHandle() const { return db_; }

    /// Apply schema if tables don't exist yet.
    bool EnsureSchema(const String& schemaPath);

    // --- Player State ---

    /// Load player state from database. Returns false if player not found.
    bool LoadPlayer(int playerId, PlayerState& out);
    /// Save full player state. Inserts if not exists, updates if exists.
    bool SavePlayer(const PlayerState& state);
    /// Save only player position (lightweight, called on disconnect).
    bool SavePlayerPosition(int playerId, const Vector3& pos);
    /// Check if a player exists in the world database.
    bool PlayerExists(int playerId);
    /// Create a new player with default values. Returns false if already exists.
    bool CreatePlayer(int playerId);

    // --- Server State ---

    /// Save current game time (game day, season angle, time of day).
    bool SaveGameTime(float gameDay, float seasonAngle, float timeOfDay);
    /// Load game time. Returns false if no saved state.
    bool LoadGameTime(GameTimeState& out);
    /// Save an arbitrary key-value pair to server_state.
    bool SaveServerState(const String& key, const String& value);
    /// Load an arbitrary key-value from server_state. Returns empty string if not found.
    String LoadServerState(const String& key);

    // --- Inventory ---

    /// Get all inventory slots for a player.
    Vector<InventorySlot> GetPlayerInventory(int playerId);
    /// Count how many of an item the player has (across all slots).
    int GetItemCount(int playerId, int itemId);
    /// Add item to player inventory with stacking. Caller provides item rules from GameDB.
    /// stackMax: max stack size, itemWeight: per-unit weight, itemDurability: default durability,
    /// maxWeight: absolute weight limit, baseSlots: max bag slots (incl container bonus).
    bool AddItemToInventory(int playerId, int itemId, int qty,
                            int stackMax, float itemWeight, int itemDurability,
                            float maxWeight, int availableSlots);
    /// Remove item from player inventory. Returns false if insufficient.
    bool RemoveItemFromInventory(int playerId, int itemId, int qty);
    /// Get the sum of (quantity) for all bag items. Caller multiplies by weights from GameDB.
    Vector<InventorySlot> GetPlayerBagItems(int playerId);
    /// Get occupied bag slot count.
    int GetOccupiedBagSlots(int playerId);
    /// Equip item from bag to equipment slot. Returns false if item not in bag or slot occupied.
    bool EquipItem(int playerId, int itemId, const String& slot);
    /// Unequip item from equipment slot back to bag. Returns false if slot empty or bag full.
    /// Caller must provide bag capacity info for the add-back.
    bool UnequipItem(int playerId, const String& slot, int& outItemId,
                     int stackMax, float itemWeight, int itemDurability,
                     float maxWeight, int availableSlots);
    /// Get the item currently in an equipment slot (0 = empty).
    int GetEquippedItem(int playerId, const String& slot);
    /// Deduct durability from an equipped item. Returns remaining durability.
    /// If durability reaches 0, the item is removed (broken). Returns -1 if item has no durability tracking.
    int DeductDurability(int playerId, const String& slot, int amount = 1);

    // --- Placed Buildings ---

    /// Insert a new placed building. Returns the new row ID, or -1 on failure.
    int InsertPlacedBuilding(int buildingId, int ownerId, float px, float py, float pz,
                             float rotation, int hp, int builtDay, int snappedTo,
                             int parentId = 0);
    /// Remove a placed building by ID. Returns true if found.
    bool RemovePlacedBuilding(int placedId);
    /// Get IDs of all placed buildings with the given parent_id.
    Vector<int> GetChildBuildingIds(int parentId);
    /// Remove all placed buildings whose parent_id matches. Returns count removed.
    int RemoveChildBuildings(int parentId);
    /// Get all placed buildings.
    Vector<PlacedBuildingDBInfo> GetAllPlacedBuildings();
    /// Get placed buildings owned by a player.
    Vector<PlacedBuildingDBInfo> GetPlacedBuildingsByOwner(int ownerId);
    /// Update building HP. Returns true on success.
    bool UpdateBuildingHp(int placedId, int newHp);
    /// Update gate open state.
    bool SetGateOpen(int placedId, bool open);
    /// Update last repair day.
    bool SetLastRepair(int placedId, int gameDay);
    /// Get a single placed building by ID.
    bool GetPlacedBuilding(int placedId, PlacedBuildingDBInfo& out);
    /// Get the owner ID of a placed building (-1 if not found).
    int GetPlacedBuildingOwner(int placedId);
    /// Update HP of a placed building.
    bool UpdateBuildingHP(int placedId, int newHp);
    /// Update storage JSON string of a placed building.
    bool UpdateBuildingStorage(int placedId, const String& storageJson);
    /// Get storage JSON string of a placed building (empty string if none).
    String GetBuildingStorageJson(int placedId);

    // --- Placed Crops ---

    /// Placed crop runtime state.
    struct PlacedCropInfo
    {
        int cropId;
        int ownerId;
        int seedItemId;
        float posX, posY, posZ;
        float plantedDay;
        int growthStage;
        int regionId;
    };

    /// Insert a new placed crop. Returns the new crop_id, or -1 on failure.
    int InsertPlacedCrop(int ownerId, int seedItemId, float px, float py, float pz,
                         float plantedDay, int regionId);
    /// Remove a placed crop by ID. Returns true if found.
    bool RemovePlacedCrop(int cropId);
    /// Update crop growth stage.
    bool UpdateCropGrowthStage(int cropId, int newStage);
    /// Get all placed crops.
    Vector<PlacedCropInfo> GetAllPlacedCrops();
    /// Get a single placed crop by ID.
    bool GetPlacedCrop(int cropId, PlacedCropInfo& out);

    // --- Crop Rotation History ---

    struct CropHistoryEntry
    {
        int tileX, tileZ;
        int lastSeedId;
        float lastHarvestDay;
        int consecutive;
    };

    /// Get crop history for a tile (snapped to 2m grid). Returns false if no history.
    bool GetCropHistory(int tileX, int tileZ, CropHistoryEntry& out);
    /// Record a harvest at a tile. Updates consecutive count if same crop, resets if different.
    void RecordCropHarvest(int tileX, int tileZ, int seedItemId, float gameDay);

    // --- Irrigation Channels ---

    struct IrrigationChannel
    {
        int channelId;
        int ownerId;
        float waterX, waterZ;
        float farmX, farmZ;
    };

    /// Insert a new irrigation channel. Returns channel_id or -1.
    int InsertIrrigationChannel(int ownerId, float waterX, float waterZ, float farmX, float farmZ);
    /// Get all irrigation channels.
    Vector<IrrigationChannel> GetAllIrrigationChannels();

    // --- Well State ---

    struct WellState
    {
        int buildingId;
        int waterReserve{100};
        int maxYield{10};
        float lastRefill{0.0f};
    };

    /// Load well state. Returns false if not tracked yet.
    bool GetWellState(int buildingId, WellState& out);
    /// Create or update well state.
    bool SaveWellState(const WellState& state);
    /// Deplete water from a well. Returns actual amount consumed.
    int DepleteWell(int buildingId, int amount);
    /// Refill wells based on rainfall. Called from weather tick.
    void RefillWells(float rainfallIntensity, float gameDay);

    // --- Fire Pits ---

    /// Save all fire pits (replaces existing rows).
    bool SaveFirePits(const Vector<FirePitDBInfo>& pits);
    /// Load all fire pits.
    Vector<FirePitDBInfo> LoadFirePits();

    // --- WAL Checkpoint ---

    /// Flush WAL to main database file.
    bool Checkpoint();

    // --- Backup ---

    /// Copy world.db to backup path using SQLite backup API. Safe during WAL.
    bool Backup(const String& backupPath);

    // --- NPC Social Bonds ---

    /// Get familiarity between two NPCs (0 if no bond exists). Always normalizes order.
    float GetBondFamiliarity(unsigned npcA, unsigned npcB);
    /// Increment familiarity between two NPCs. Creates bond if it doesn't exist.
    void IncrementFamiliarity(unsigned npcA, unsigned npcB, float amount);
    /// Set bond type (acquaintance, mate, parent, child).
    void SetBondType(unsigned npcA, unsigned npcB, const String& bondType);
    /// Get bond type between two NPCs. Returns empty string if no bond.
    String GetBondType(unsigned npcA, unsigned npcB);

    // --- Animal Production Tracking (Phase 16) ---

    /// Log an animal production event (milk, wool, meat, hide).
    void LogAnimalProduction(unsigned animalId, int speciesId, const String& product,
                             int quantity, unsigned tamerId, unsigned campfireId, float gameDay);
    /// Get total production for a settlement by product type.
    int GetSettlementProduction(unsigned campfireId, const String& product);

    // --- Water Bodies ---

    struct WaterBodyInfo
    {
        int bodyId;
        int terrainId;
        int areaPixels;
        float areaWorld;
        float centerX, centerZ;
        float minDepth, maxDepth;
    };

    struct FishSpawnPoint
    {
        int id;
        int bodyId;
        float posX, posZ;
        float depth;
    };

    /// Insert a water body. Returns body_id.
    int InsertWaterBody(int terrainId, int areaPixels, float areaWorld,
                        float cx, float cz, float minDepth, float maxDepth);
    /// Insert a fish spawn point.
    bool InsertFishSpawnPoint(int bodyId, float px, float pz, float depth);
    /// Get all water bodies for a terrain.
    Vector<WaterBodyInfo> GetWaterBodies(int terrainId);
    /// Get all fish spawn points for a body.
    Vector<FishSpawnPoint> GetFishSpawnPoints(int bodyId);
    /// Get total fish spawn point count for a terrain.
    int GetTotalFishSpawnCount(int terrainId);
    /// Check if water bodies have been computed for this terrain.
    bool HasWaterBodies(int terrainId);

    // --- Settlement Patches ---

    /// Check if a settlement patch is unclaimed.
    bool IsSettlementPatchFree(int terrainId, int sx, int sz);
    /// Claim a settlement patch for a settlement. Returns false if already claimed.
    bool ClaimSettlementPatch(int terrainId, int sx, int sz, int settlementId);
    /// Get the settlement patch coords for a settlement. Returns false if no claim.
    bool GetSettlementPatch(int settlementId, int& outTerrainId, int& outSx, int& outSz);
    /// Count unclaimed settlement patches on a terrain.
    int GetFreeSettlementPatchCount(int terrainId);
    /// Get the settlement ID that owns a patch (-1 if unclaimed).
    int GetSettlementAtPatch(int terrainId, int sx, int sz);

    // --- Map Backups ---

    /// Store a pristine map image as a BLOB. Skips if a backup already exists for this filename.
    bool BackupMap(const String& filename, Image* image);
    /// Check if a backup exists for this filename.
    bool HasMapBackup(const String& filename);

    /// Execute a single SQL statement.
    bool Execute(const String& sql);

private:
    sqlite3* db_{nullptr};
    String dbPath_;
};

}

#endif // URHO3D_DATABASE_SQLITE
