// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/HashMap.h"

#ifdef URHO3D_DATABASE_SQLITE

struct sqlite3;

namespace Urho3D
{

/// Result row from a creature query.
struct CreatureInfo
{
    int id;
    String name;
    int hp, attack, defense, damage, damageVar, speed;
    float detectionRange;
    String aggression, habitat;
    String model, idleAnim, runAnim, attackAnim, dieAnim;
    float desiredSize, wanderRadius;
    int packSize;
    float fleeSpeed{0.0f}, fleeDistance{0.0f};
    float visionRange{0.0f}, visionAngle{0.5f};
    int isPredator{0}, isScavenger{0};
    float foodGrassWt{0.0f};
};

/// Result row from a loot query.
struct LootDrop
{
    int itemId;
    String itemName;
    int quantity;
    float chance;
    int toolReq;
};

/// Result row from a trap_rules query — per (trap, creature) pair.
/// Schema: (trap_id, creature_id, hold_strength, bait_id, attract_range, attract_time).
struct TrapRule
{
    int   trapItemId;
    int   creatureId;
    int   holdStrength;  ///< d20 roll target. Caller rolls and compares.
    int   baitItemId;    ///< 0 if no bait required.
    float attractRange;  ///< Radius (metres) within which bait/scent attracts creatures.
    float attractTime;   ///< Seconds bait remains active before going stale.
    bool  liveCapture{false}; ///< true = cage trap (captures alive for taming, not harvest).
};

/// Result row from a recipe query.
struct RecipeInfo
{
    int id;
    String name;
    String description;
    int outputId, outputQty;
    float craftTime;
    int toolReq, stationReq, tier;
    /// Single recipe input.
    struct Input
    {
        int itemId;
        int quantity;
        bool consumed;
    };
    Vector<Input> inputs;
};

/// Result row from a combat stats query.
struct CombatInfo
{
    int attackMod, defenseMod, damage, damageVar;
    float range;
    int speedMod;
    String slot;
    bool twoHanded;
};

/// Result row from a food query.
struct FoodInfo
{
    int hunger, health;
    float warmth, spoilTime, poisonChance;
    bool cooked;
};

/// Result row from a climate query.
struct ClimateInfo
{
    float baseTemp, windChill, rainChill;
};

/// Hunger rules from database.
struct HungerRules
{
    int maxHunger;
    float drainPerDay, sprintMult, workMult, swimMult;
    float starveHpDay;
    int lowThreshold, criticalThreshold;
    int eatRestore{35};
};

/// Thirst rules from database.
struct ThirstRules
{
    int maxThirst;
    float drainPerDay, heatMult, sprintMult, workMult;
    float dehydrateHpDay;
    int lowThreshold, criticalThreshold;
    int eatRestore{25};
};

/// Warmth rules from database.
struct WarmthRules
{
    float comfortMin, coldThreshold, severeCold, heatThreshold;
    float coldHpPerDay, severeHpPerDay, heatHpPerDay;
    float fireWarmth, fireRange, activityWarmth, sprintHeat;
    float nightMultiplier{8.0f};
    int lowThreshold{30};
    float sitRestore{30.0f};
};

/// Stamina rules from database.
struct StaminaRules
{
    int maxStamina;
    float regenPerSecond, regenSleeping;
    float sprintCostSec, meleeCost, rangedCost;
    float chopCost, mineCost, buildCost, swimCostSec;
    int lowThreshold;
    float sitRestore{20.0f};
    float sleepRestore{60.0f};
};

/// Water source info from database.
struct WaterSourceInfo
{
    int id;
    String name, type;
    int thirstRestore;
    float interactTime, diseaseChance;
    int requires;
};

/// Death/respawn rules from database.
struct DeathRules
{
    float respawnDelay;
    int hpOnRespawn, hungerOnRespawn, thirstOnRespawn, staminaOnRespawn;
    bool dropInventory;
    float corpseDuration, skillLoss;
};

/// Fire configuration from database.
struct FireRules
{
    float lightRadius{8.0f};
    float warmthRadius{5.0f};
    float warmthValue{15.0f};
    float fuelPerHour{1.0f};
    bool cookRequiresFuel{true};
    bool lightFlicker{true};
};

/// Fuel type info from database.
struct FuelInfo
{
    int itemId;
    float fuelValue{1.0f};
    float heatBonus{0.0f};
};

/// Result row from a crop type query.
struct CropTypeInfo
{
    int seedItemId;
    int harvestItemId;
    int harvestQty;
    int seedReturn;
    String plantSeason;
    String harvestSeason;
    int growDays;
    float minFlat;
    float nearWaterRange;
    int toolReq;
    String model;
};

/// Result row from a gather source query.
struct GatherSourceInfo
{
    int id;
    String name;
    int itemId;
    int quantity;
    int toolReq;
    String terrain;      ///< "water", "riverbank", "grassland", "forest", "mountain", "any"
    float minHeight, maxHeight;
    String model;
    float respawn;
    String seasonal;     ///< "any", "spring", "summer", "autumn", "winter", or comma-separated
};

/// Result row from a building type query.
struct BuildingTypeDBInfo
{
    int id;
    String name, category;
    int tier;
    float footprintX, footprintZ, height;
    int maxHp;
    float decayRate, warmth;
    int storageSlots, sleepCapacity;
    bool respawn;
    String snapType;
    String model, ghostModel;
    String description;
};

/// Result row from a building recipe query.
struct BuildingRecipeInput
{
    int itemId;
    int quantity;
};

/// Result row from a placed building query.
struct PlacedBuildingDBInfo
{
    int id;
    int buildingId;
    int ownerId;
    float posX, posY, posZ;
    float rotation;
    int hp;
    int builtDay;
    int lastRepair;
    bool gateOpen;
    int snappedTo;
};

/// Repair cost entry for a building type.
struct RepairCostInfo
{
    int itemId;
    int quantity;
    int hpRestored;
};

/// Weather damage entry.
struct WeatherDamageInfo
{
    String weather;
    int tier;
    float extraDecay;
};

/// Alloy special properties for advanced steel ingots.
struct AlloyPropertiesInfo
{
    int alloyItemId{0};
    int damageMod{0};
    int defenseMod{0};
    int durabilityPct{0};     // percentage bonus
    String special;           // keyword: impact_absorb, corrosion_proof, edge_retention, cold_immune, brittle_hard
};

/// Result row from an item query.
struct ItemInfo
{
    int id;
    String name, category;
    int stackMax;
    float weight;
    int durability;
    float decayTime;
    String model, icon, description;
    int tier;
};

/// Inventory rules from database.
struct InventoryRules
{
    int baseSlots;
    float maxWeight, heavyWeight, maxWeightAbsolute;
    float encumberedSpeed, heavySpeed, heavyStaminaMult;
};

/// A single inventory slot (bag or equipment).
struct InventorySlot
{
    int itemId;
    int quantity;
    int durability;  // -1 = use item default
    String slotType; // "bag", "hand", "body", etc.
};

/// Resource type from the economic doctrine.
struct ResourceTypeInfo
{
    int id;
    String name, category;
    int itemId;
    float regenPerDay, extractPerUse, extractTime;
    int toolReq;
    float scarcity50, scarcity0;
    String seasonal;
    int tier;
};

/// Per-region resource pool state.
struct RegionResourceInfo
{
    int regionId, resourceId;
    float currentAmount, maxAmount;
    float totalExtracted;
    int lastRegenDay;
};

/// Breeding rules for a creature species.
struct BreedingRules
{
    int creatureId;
    int breedInterval, litterSize, maturityDays, minPopBreed;
    float maxPopRatio, starvationThreshold;
    float birthRate;    ///< Kill-driven replacement multiplier (1.0 = exact replacement)
};

/// Trade value for an item (labor-hours + scarcity).
struct TradeValue
{
    int itemId;
    float baseValue, scarcityMult, demandMult;
    int lastUpdate;
};

/// Result of an extraction attempt.
struct ExtractionResult
{
    bool success;
    int itemId;
    int quantity;        ///< Actual yield after scarcity modifier
    float scarcityMod;   ///< The modifier applied (1.0 = full, 0.0 = exhausted)
    float remaining;     ///< Resource left after extraction
};

/// SQL-driven game rules database. Server-side subsystem.
/// The game lives in the database. The engine is a dumb executor.
class URHO3D_API GameDB : public Object
{
    URHO3D_OBJECT(GameDB, Object);

public:
    explicit GameDB(Context* context);
    ~GameDB() override;

    /// Open the database file. Returns true on success.
    bool Open(const String& dbPath);
    /// Close the database.
    void Close();
    /// Execute a SQL file (schema, seed data, or balance patch). Supports multiple statements.
    bool ExecuteFile(const String& sqlPath);
    /// Execute a single SQL statement. For quick queries/updates.
    bool Execute(const String& sql);

    /// Return true if database is open.
    bool IsOpen() const { return db_ != nullptr; }

    // --- Item queries ---

    /// Get item info by ID.
    bool GetItem(int itemId, ItemInfo& out);
    /// Get all items in a category.
    Vector<ItemInfo> GetItemsByCategory(const String& category);
    /// Get clothing warmth value for an item (0 if not in clothing_warmth table).
    float GetClothingWarmth(int itemId);

    // --- Creature queries ---

    /// Get creature info by ID.
    bool GetCreature(int creatureId, CreatureInfo& out);
    /// Get all creatures for a habitat.
    Vector<CreatureInfo> GetCreaturesByHabitat(const String& habitat);

    // --- Loot queries ---

    /// Get loot drops for a creature. Caller rolls chance.
    Vector<LootDrop> GetLoot(int creatureId);

    /// Get trap rule for a (trap_item, creature) pair. Returns false if this trap doesn't catch this species.
    bool GetTrapRule(int trapItemId, int creatureId, TrapRule& out);

    // --- Recipe queries ---

    /// Get recipe by ID, with inputs populated.
    bool GetRecipe(int recipeId, RecipeInfo& out);
    /// Get all recipes available at a given tech tier or below.
    Vector<RecipeInfo> GetRecipesForTier(int maxTier);
    /// Check if player has ingredients (takes inventory item_id -> quantity map).
    bool CanCraft(int recipeId, const HashMap<int, int>& inventory);

    // --- Combat queries ---

    /// Get combat stats for an item.
    bool GetCombatStats(int itemId, CombatInfo& out);

    // --- Survival queries ---

    /// Get food properties for an item.
    bool GetFoodProperties(int itemId, FoodInfo& out);
    /// Get climate rules for a season/time combination.
    bool GetClimateRules(const String& season, const String& timeOfDay, ClimateInfo& out);

    /// Get hunger rules (singleton row).
    bool GetHungerRules(HungerRules& out);
    /// Get thirst rules (singleton row).
    bool GetThirstRules(ThirstRules& out);
    /// Get warmth rules (singleton row).
    bool GetWarmthRules(WarmthRules& out);
    /// Get stamina rules (singleton row).
    bool GetStaminaRules(StaminaRules& out);
    /// Get death/respawn rules (singleton row).
    bool GetDeathRules(DeathRules& out);
    /// Get water source by type.
    bool GetWaterSource(const String& type, WaterSourceInfo& out);
    /// Get all water sources.
    Vector<WaterSourceInfo> GetAllWaterSources();
    /// Get clothing warmth for an item.
    bool GetClothingWarmth(int itemId, float& warmth, float& rainResist);
    /// Get fire rules (singleton row).
    bool GetFireRules(FireRules& out);
    /// Get fuel value for an item. Returns false if item is not a fuel.
    bool GetFuelInfo(int itemId, FuelInfo& out);

    /// Apply a SQL balance patch file.
    bool ApplyPatch(const String& patchPath) { return ExecuteFile(patchPath); }

    /// Expose the raw SQLite handle for subsystems that run their own queries (e.g. PopulationManager).
    sqlite3* GetHandle() const { return db_; }

    // --- Inventory queries ---

    /// Get inventory rules (singleton row).
    bool GetInventoryRules(InventoryRules& out);
    /// Get all inventory slots for a player.
    Vector<InventorySlot> GetPlayerInventory(int playerId);
    /// Get total carried weight for a player.
    float GetPlayerWeight(int playerId);
    /// Count how many of an item the player has (across all slots).
    int GetItemCount(int playerId, int itemId);
    /// Add item to player inventory. Handles stacking. Returns false if over weight/slots.
    bool AddItemToInventory(int playerId, int itemId, int qty);
    /// Remove item from player inventory. Returns false if insufficient.
    bool RemoveItemFromInventory(int playerId, int itemId, int qty);
    /// Get available bag slot count for a player.
    int GetAvailableSlots(int playerId);

    // --- Equipment queries ---

    /// Equip item from bag to equipment slot. Returns false if item not in bag or slot occupied.
    bool EquipItem(int playerId, int itemId, const String& slot);
    /// Unequip item from equipment slot back to bag. Returns false if slot empty or bag full.
    bool UnequipItem(int playerId, const String& slot, int& outItemId);
    /// Get the item currently in an equipment slot (0 = empty).
    int GetEquippedItem(int playerId, const String& slot);

    // --- Gather source queries ---

    /// Get all gather sources.
    Vector<GatherSourceInfo> GetAllGatherSources();
    /// Get gather sources matching a terrain biome string.
    Vector<GatherSourceInfo> GetGatherSourcesByTerrain(const String& terrain);

    // --- Building queries ---

    /// Get building type by ID.
    bool GetBuildingType(int typeId, BuildingTypeDBInfo& out);
    /// Get all building types.
    Vector<BuildingTypeDBInfo> GetAllBuildingTypes();
    /// Get building types by category.
    Vector<BuildingTypeDBInfo> GetBuildingTypesByCategory(const String& category);
    /// Get recipe inputs for a building type.
    Vector<BuildingRecipeInput> GetBuildingRecipe(int buildingTypeId);

    // --- Placed building queries ---

    /// Insert a new placed building. Returns the new row ID, or -1 on failure.
    int InsertPlacedBuilding(int buildingId, int ownerId, float px, float py, float pz,
                             float rotation, int hp, int builtDay, int snappedTo);
    /// Remove a placed building by ID. Returns true if found.
    bool RemovePlacedBuilding(int placedId);
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

    // --- Repair cost queries ---

    /// Get repair costs for a building type.
    Vector<RepairCostInfo> GetRepairCosts(int buildingTypeId);

    // --- Weather damage queries ---

    /// Get extra decay for a weather condition and building tier.
    float GetWeatherDamage(const String& weather, int tier);

    // --- Wall strength queries ---

    /// Check if a building blocks a creature (returns true if blocked).
    bool DoesWallBlock(int buildingTypeId, int creatureId);

    // --- Snap rules ---

    /// Check if snap_type 'fromType' can snap to 'toType'. Returns align string or empty.
    String GetSnapAlign(const String& fromType, const String& toType);

    // --- Crop queries ---

    /// Get crop type rules for a seed item.
    bool GetCropType(int seedItemId, CropTypeInfo& out);
    /// Get all crop types.
    Vector<CropTypeInfo> GetAllCropTypes();

    /// Get alloy properties for an alloy ingot item ID. Returns false if not an alloy.
    bool GetAlloyProperties(int alloyItemId, AlloyPropertiesInfo& out);

    // --- Skill queries ---

    /// Cache XP trigger rules and level thresholds from database. Call after ExecuteFile(skills_schema).
    void CacheSkillRules();
    /// Cache technique discovery chains from technique_discovery table. Call after ExecuteFile(technique_schema).
    void CacheTechniqueDiscovery();
    /// Award XP for an action (e.g., "melee_hit"). Returns true if any XP was awarded.
    bool AwardXP(int playerId, const String& action);
    /// Award XP with a multiplier (e.g., 1.1f for +10% from Master Trader).
    bool AwardXP(int playerId, const String& action, float xpMultiplier);
    /// Award XP with epoch-gated level cap. settlementEpoch constrains max level per technique_tiers.
    bool AwardXP(int playerId, const String& action, float xpMultiplier, int settlementEpoch);
    /// Get effective max level for a skill given the settlement's epoch tier.
    /// Returns 0 if the skill's epoch hasn't been reached (can't learn yet).
    int GetEffectiveMaxLevel(int skillId, int settlementEpoch) const;
    /// Check if a skill level-up triggers technique discovery. Called internally after XP award.
    /// Returns discovered skill name (empty if none). settlementEpoch = current epoch tier (0-3).
    String CheckTechniqueDiscovery(int playerId, int sourceSkillId, int sourceLevel, int settlementEpoch = 0);
    /// Get player's current level for a skill (0 if no record).
    int GetSkillLevel(int playerId, int skillId);
    /// Get player's raw XP for a skill (0 if no record).
    int GetSkillXP(int playerId, int skillId);
    /// Add a specific amount of XP to a skill directly (for knowledge transfer, etc.).
    bool AddXPDirect(int playerId, int skillId, int xpAmount);
    /// Check if an action's XP triggers reward a specific skill ID.
    bool ActionRewardsSkill(const String& action, int skillId) const;

    // --- Economic Doctrine queries ---

    /// Get a resource type by ID.
    bool GetResourceType(int resourceId, ResourceTypeInfo& out);
    /// Get all resource types.
    Vector<ResourceTypeInfo> GetAllResourceTypes();
    /// Get resource types by category ('flora', 'mineral', 'soil', 'water').
    Vector<ResourceTypeInfo> GetResourceTypesByCategory(const String& category);

    /// Get a region's resource pool.
    bool GetRegionResource(int regionId, int resourceId, RegionResourceInfo& out);
    /// Get all resource pools for a region.
    Vector<RegionResourceInfo> GetRegionResources(int regionId);

    /// Attempt to extract a resource from a region. Applies scarcity modifier, logs extraction.
    /// Returns actual yield and remaining amount. The entropy law lives here.
    ExtractionResult ExtractResource(int playerId, int regionId, int resourceId, int gameDay);

    /// Run daily regeneration for all resources in a region. Call once per game-day.
    void RegenerateResources(int regionId, int gameDay);

    /// Compute the scarcity modifier for a resource (0.0 to 1.0).
    float GetScarcityModifier(int regionId, int resourceId);

    /// Get breeding rules for a creature.
    bool GetBreedingRules(int creatureId, BreedingRules& out);

    /// Get trade value for an item.
    bool GetTradeValue(int itemId, TradeValue& out);
    /// Update trade value scarcity multiplier. Called periodically by server.
    void UpdateTradeValues(int gameDay);

    /// Get an economic constant by key. Returns defaultVal if not found.
    float GetEconomicConstant(const String& key, float defaultVal = 0.0f);

private:
    /// Compute level from XP using cached threshold table.
    int ComputeLevel(int xp) const;

    struct XPTrigger { int skillId; int xpAmount; };
    /// action name → list of XP awards
    HashMap<String, Vector<XPTrigger>> xpTriggers_;
    /// level thresholds (index = level, value = xp_required)
    Vector<int> levelThresholds_;
    /// max_level per skill id
    HashMap<int, int> skillMaxLevels_;

    struct DiscoveryChain { int targetSkillId; int prereqRating; int dc; };
    /// source_skill_id → list of possible discoveries
    HashMap<int, Vector<DiscoveryChain>> discoveryChains_;
    /// player_id → INT modifier for technique discovery rolls (set by AuthServer)
    HashMap<int, int> pendingIntMods_;
    /// skill_id → minimum epoch tier required (from technique_tiers)
    HashMap<int, int> skillEpochTier_;
    /// skill_id → level_cap at its minimum epoch tier (from technique_tiers)
    HashMap<int, int> skillLevelCaps_;
public:
    /// Set INT modifier for a player's technique discovery rolls (higher INT = discover faster).
    void SetDiscoveryIntMod(int playerId, int intMod) { pendingIntMods_[playerId] = intMod; }
    /// Read-only access to epoch tiers for settlement epoch derivation.
    const HashMap<int, int>& GetSkillEpochTiers() const { return skillEpochTier_; }
private:
    /// skill_id → name (for logging)
    HashMap<int, String> skillNames_;

public:
    /// Pending discovery events for AuthServer to drain and broadcast.
    struct DiscoveryEvent { int playerId; int skillId; String skillName; };
    Vector<DiscoveryEvent> pendingDiscoveries_;
private:

    sqlite3* db_{nullptr};
};

}

#endif // URHO3D_DATABASE_SQLITE
