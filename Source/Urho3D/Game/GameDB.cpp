// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../Game/GameDB.h"
#include "../IO/Log.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../Math/MathDefs.h"

#ifdef URHO3D_DATABASE_SQLITE

#include <SQLite/sqlite3.h>

#include "../DebugNew.h"

namespace Urho3D
{

GameDB::GameDB(Context* context) :
    Object(context)
{
}

GameDB::~GameDB()
{
    Close();
}

bool GameDB::Open(const String& dbPath)
{
    if (db_)
        Close();

    int rc = sqlite3_open(dbPath.CString(), &db_);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERROR("GameDB: Failed to open " + dbPath + ": " + String(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Enable WAL mode for concurrent reads
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    URHO3D_LOGINFO("GameDB: Opened " + dbPath);
    return true;
}

void GameDB::Close()
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
        URHO3D_LOGINFO("GameDB: Closed");
    }
}

bool GameDB::ExecuteFile(const String& sqlPath)
{
    if (!db_)
    {
        URHO3D_LOGERROR("GameDB: Database not open");
        return false;
    }

    // Read file contents
    auto* fileSystem = GetSubsystem<FileSystem>();
    if (!fileSystem || !fileSystem->FileExists(sqlPath))
    {
        URHO3D_LOGERROR("GameDB: SQL file not found: " + sqlPath);
        return false;
    }

    File file(context_, sqlPath, FILE_READ);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("GameDB: Could not open SQL file: " + sqlPath);
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
        URHO3D_LOGERROR("GameDB: Failed to execute " + sqlPath + ": " + error);
        return false;
    }

    URHO3D_LOGINFO("GameDB: Executed " + sqlPath);
    return true;
}

bool GameDB::Execute(const String& sql)
{
    if (!db_)
        return false;

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.CString(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        String error = errMsg ? String(errMsg) : "unknown error";
        sqlite3_free(errMsg);
        URHO3D_LOGERROR("GameDB: SQL error: " + error);
        return false;
    }
    return true;
}

// --- Item queries ---

bool GameDB::GetItem(int itemId, ItemInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, category, stack_max, weight, durability, decay_time, "
        "model, icon, description, tier FROM items WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, itemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.id = sqlite3_column_int(stmt, 0);
        out.name = (const char*)sqlite3_column_text(stmt, 1);
        out.category = (const char*)sqlite3_column_text(stmt, 2);
        out.stackMax = sqlite3_column_int(stmt, 3);
        out.weight = (float)sqlite3_column_double(stmt, 4);
        out.durability = sqlite3_column_int(stmt, 5);
        out.decayTime = (float)sqlite3_column_double(stmt, 6);
        const char* model = (const char*)sqlite3_column_text(stmt, 7);
        out.model = model ? model : "";
        const char* icon = (const char*)sqlite3_column_text(stmt, 8);
        out.icon = icon ? icon : "";
        const char* desc = (const char*)sqlite3_column_text(stmt, 9);
        out.description = desc ? desc : "";
        out.tier = sqlite3_column_int(stmt, 10);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<ItemInfo> GameDB::GetItemsByCategory(const String& category)
{
    Vector<ItemInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, category, stack_max, weight, durability, decay_time, "
        "model, icon, description, tier FROM items WHERE category = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, category.CString(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ItemInfo info;
        info.id = sqlite3_column_int(stmt, 0);
        info.name = (const char*)sqlite3_column_text(stmt, 1);
        info.category = (const char*)sqlite3_column_text(stmt, 2);
        info.stackMax = sqlite3_column_int(stmt, 3);
        info.weight = (float)sqlite3_column_double(stmt, 4);
        info.durability = sqlite3_column_int(stmt, 5);
        info.decayTime = (float)sqlite3_column_double(stmt, 6);
        const char* model = (const char*)sqlite3_column_text(stmt, 7);
        info.model = model ? model : "";
        const char* icon = (const char*)sqlite3_column_text(stmt, 8);
        info.icon = icon ? icon : "";
        const char* desc = (const char*)sqlite3_column_text(stmt, 9);
        info.description = desc ? desc : "";
        info.tier = sqlite3_column_int(stmt, 10);
        result.Push(info);
    }
    sqlite3_finalize(stmt);
    return result;
}

float GameDB::GetClothingWarmth(int itemId)
{
    if (!db_) return 0.0f;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT warmth FROM clothing_warmth WHERE item_id = ?",
                           -1, &stmt, nullptr) != SQLITE_OK)
        return 0.0f;

    sqlite3_bind_int(stmt, 1, itemId);
    float warmth = 0.0f;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        warmth = (float)sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return warmth;
}

// --- Creature queries ---

static void ReadCreatureRow(sqlite3_stmt* stmt, CreatureInfo& out)
{
    out.id = sqlite3_column_int(stmt, 0);
    out.name = (const char*)sqlite3_column_text(stmt, 1);
    out.hp = sqlite3_column_int(stmt, 2);
    out.attack = sqlite3_column_int(stmt, 3);
    out.defense = sqlite3_column_int(stmt, 4);
    out.damage = sqlite3_column_int(stmt, 5);
    out.damageVar = sqlite3_column_int(stmt, 6);
    out.speed = sqlite3_column_int(stmt, 7);
    out.detectionRange = (float)sqlite3_column_double(stmt, 8);
    out.aggression = (const char*)sqlite3_column_text(stmt, 9);
    out.packSize = sqlite3_column_int(stmt, 10);
    out.habitat = (const char*)sqlite3_column_text(stmt, 11);
    const char* model = (const char*)sqlite3_column_text(stmt, 12);
    out.model = model ? model : "";
    const char* idle = (const char*)sqlite3_column_text(stmt, 13);
    out.idleAnim = idle ? idle : "";
    const char* run = (const char*)sqlite3_column_text(stmt, 14);
    out.runAnim = run ? run : "";
    const char* atk = (const char*)sqlite3_column_text(stmt, 15);
    out.attackAnim = atk ? atk : "";
    const char* die = (const char*)sqlite3_column_text(stmt, 16);
    out.dieAnim = die ? die : "";
    out.desiredSize = (float)sqlite3_column_double(stmt, 17);
    out.wanderRadius = (float)sqlite3_column_double(stmt, 18);
    out.fleeSpeed = (float)sqlite3_column_double(stmt, 19);
    out.fleeDistance = (float)sqlite3_column_double(stmt, 20);
    out.visionRange = (float)sqlite3_column_double(stmt, 21);
    out.visionAngle = (float)sqlite3_column_double(stmt, 22);
    out.isPredator = sqlite3_column_int(stmt, 23);
    out.isScavenger = sqlite3_column_int(stmt, 24);
    out.foodGrassWt = (float)sqlite3_column_double(stmt, 25);
}

bool GameDB::GetCreature(int creatureId, CreatureInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, hp, attack, defense, damage, damage_var, speed, "
        "detection_range, aggression, pack_size, habitat, model, idle_anim, "
        "run_anim, attack_anim, die_anim, desired_size, wander_radius, "
        "flee_speed, flee_distance, vision_range, vision_angle, "
        "is_predator, is_scavenger, food_grass_wt "
        "FROM creatures WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, creatureId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ReadCreatureRow(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<CreatureInfo> GameDB::GetCreaturesByHabitat(const String& habitat)
{
    Vector<CreatureInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, hp, attack, defense, damage, damage_var, speed, "
        "detection_range, aggression, pack_size, habitat, model, idle_anim, "
        "run_anim, attack_anim, die_anim, desired_size, wander_radius, "
        "flee_speed, flee_distance, vision_range, vision_angle, "
        "is_predator, is_scavenger, food_grass_wt "
        "FROM creatures WHERE habitat = ? OR habitat = 'any'", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, habitat.CString(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        CreatureInfo info;
        ReadCreatureRow(stmt, info);
        result.Push(info);
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Loot queries ---

Vector<LootDrop> GameDB::GetLoot(int creatureId)
{
    Vector<LootDrop> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT lt.item_id, i.name, lt.quantity, lt.chance, lt.tool_req "
        "FROM loot_table lt JOIN items i ON i.id = lt.item_id "
        "WHERE lt.creature_id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, creatureId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        LootDrop drop;
        drop.itemId = sqlite3_column_int(stmt, 0);
        drop.itemName = (const char*)sqlite3_column_text(stmt, 1);
        drop.quantity = sqlite3_column_int(stmt, 2);
        drop.chance = (float)sqlite3_column_double(stmt, 3);
        drop.toolReq = sqlite3_column_int(stmt, 4);
        result.Push(drop);
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Trap queries ---

bool GameDB::GetTrapRule(int trapItemId, int creatureId, TrapRule& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT trap_id, creature_id, hold_strength, bait_id, attract_range, attract_time, "
        "COALESCE(live_capture, 0) "
        "FROM trap_rules WHERE trap_id = ? AND creature_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, trapItemId);
    sqlite3_bind_int(stmt, 2, creatureId);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.trapItemId   = sqlite3_column_int(stmt, 0);
        out.creatureId   = sqlite3_column_int(stmt, 1);
        out.holdStrength = sqlite3_column_int(stmt, 2);
        out.baitItemId   = sqlite3_column_int(stmt, 3);
        out.attractRange = (float)sqlite3_column_double(stmt, 4);
        out.attractTime  = (float)sqlite3_column_double(stmt, 5);
        out.liveCapture  = sqlite3_column_int(stmt, 6) != 0;
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// --- Recipe queries ---

static void ReadRecipeRow(sqlite3_stmt* stmt, RecipeInfo& out)
{
    out.id = sqlite3_column_int(stmt, 0);
    out.name = (const char*)sqlite3_column_text(stmt, 1);
    const char* desc = (const char*)sqlite3_column_text(stmt, 2);
    out.description = desc ? desc : "";
    out.outputId = sqlite3_column_int(stmt, 3);
    out.outputQty = sqlite3_column_int(stmt, 4);
    out.craftTime = (float)sqlite3_column_double(stmt, 5);
    out.toolReq = sqlite3_column_int(stmt, 6);
    out.stationReq = sqlite3_column_int(stmt, 7);
    out.tier = sqlite3_column_int(stmt, 8);
}

bool GameDB::GetRecipe(int recipeId, RecipeInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, description, output_id, output_qty, craft_time, tool_req, station_req, tier "
        "FROM recipes WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, recipeId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ReadRecipeRow(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);

    if (!found) return false;

    // Populate inputs
    rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, quantity, consumed FROM recipe_inputs WHERE recipe_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return true; // recipe found, inputs failed — still return true

    sqlite3_bind_int(stmt, 1, recipeId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        RecipeInfo::Input input;
        input.itemId = sqlite3_column_int(stmt, 0);
        input.quantity = sqlite3_column_int(stmt, 1);
        input.consumed = sqlite3_column_int(stmt, 2) != 0;
        out.inputs.Push(input);
    }
    sqlite3_finalize(stmt);
    return true;
}

Vector<RecipeInfo> GameDB::GetRecipesForTier(int maxTier)
{
    Vector<RecipeInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, description, output_id, output_qty, craft_time, tool_req, station_req, tier "
        "FROM recipes WHERE tier <= ? ORDER BY tier, id", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, maxTier);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        RecipeInfo info;
        ReadRecipeRow(stmt, info);
        result.Push(info);
    }
    sqlite3_finalize(stmt);

    // Populate inputs for each recipe
    for (unsigned i = 0; i < result.Size(); ++i)
    {
        rc = sqlite3_prepare_v2(db_,
            "SELECT item_id, quantity, consumed FROM recipe_inputs WHERE recipe_id = ?",
            -1, &stmt, nullptr);
        if (rc != SQLITE_OK) continue;

        sqlite3_bind_int(stmt, 1, result[i].id);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            RecipeInfo::Input input;
            input.itemId = sqlite3_column_int(stmt, 0);
            input.quantity = sqlite3_column_int(stmt, 1);
            input.consumed = sqlite3_column_int(stmt, 2) != 0;
            result[i].inputs.Push(input);
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

bool GameDB::CanCraft(int recipeId, const HashMap<int, int>& inventory)
{
    if (!db_) return false;

    // Get recipe inputs
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, quantity, consumed FROM recipe_inputs WHERE recipe_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, recipeId);
    bool canCraft = true;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int itemId = sqlite3_column_int(stmt, 0);
        int qty = sqlite3_column_int(stmt, 1);
        bool consumed = sqlite3_column_int(stmt, 2) != 0;

        if (consumed)
        {
            auto it = inventory.Find(itemId);
            if (it == inventory.End() || it->second_ < qty)
            {
                canCraft = false;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);

    if (!canCraft) return false;

    // Check tool requirement
    rc = sqlite3_prepare_v2(db_,
        "SELECT tool_req FROM recipes WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, recipeId);
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int toolReq = sqlite3_column_int(stmt, 0);
        if (toolReq != 0)
        {
            auto it = inventory.Find(toolReq);
            if (it == inventory.End() || it->second_ < 1)
                canCraft = false;
        }
    }
    sqlite3_finalize(stmt);

    return canCraft;
}

// --- Combat queries ---

bool GameDB::GetCombatStats(int itemId, CombatInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT attack_mod, defense_mod, damage, damage_var, range, speed_mod, slot, two_handed "
        "FROM combat_stats WHERE item_id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, itemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.attackMod = sqlite3_column_int(stmt, 0);
        out.defenseMod = sqlite3_column_int(stmt, 1);
        out.damage = sqlite3_column_int(stmt, 2);
        out.damageVar = sqlite3_column_int(stmt, 3);
        out.range = (float)sqlite3_column_double(stmt, 4);
        out.speedMod = sqlite3_column_int(stmt, 5);
        out.slot = (const char*)sqlite3_column_text(stmt, 6);
        out.twoHanded = sqlite3_column_int(stmt, 7) != 0;
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// --- Survival queries ---

bool GameDB::GetFoodProperties(int itemId, FoodInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT hunger, health, warmth, spoil_time, cooked, poison_chance "
        "FROM food_properties WHERE item_id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, itemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.hunger = sqlite3_column_int(stmt, 0);
        out.health = sqlite3_column_int(stmt, 1);
        out.warmth = (float)sqlite3_column_double(stmt, 2);
        out.spoilTime = (float)sqlite3_column_double(stmt, 3);
        out.cooked = sqlite3_column_int(stmt, 4) != 0;
        out.poisonChance = (float)sqlite3_column_double(stmt, 5);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetClimateRules(const String& season, const String& timeOfDay, ClimateInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT base_temp, wind_chill, rain_chill FROM climate_rules "
        "WHERE season = ? AND time_of_day = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, season.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, timeOfDay.CString(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.baseTemp = (float)sqlite3_column_double(stmt, 0);
        out.windChill = (float)sqlite3_column_double(stmt, 1);
        out.rainChill = (float)sqlite3_column_double(stmt, 2);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// --- Survival rule queries (singleton rows) ---

bool GameDB::GetHungerRules(HungerRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT max_hunger, drain_per_day, sprint_mult, work_mult, swim_mult, "
        "starve_hp_day, low_threshold, critical_threshold, eat_restore FROM hunger_rules LIMIT 1",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.maxHunger = sqlite3_column_int(stmt, 0);
        out.drainPerDay = (float)sqlite3_column_double(stmt, 1);
        out.sprintMult = (float)sqlite3_column_double(stmt, 2);
        out.workMult = (float)sqlite3_column_double(stmt, 3);
        out.swimMult = (float)sqlite3_column_double(stmt, 4);
        out.starveHpDay = (float)sqlite3_column_double(stmt, 5);
        out.lowThreshold = sqlite3_column_int(stmt, 6);
        out.criticalThreshold = sqlite3_column_int(stmt, 7);
        out.eatRestore = sqlite3_column_int(stmt, 8);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetThirstRules(ThirstRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT max_thirst, drain_per_day, heat_mult, sprint_mult, work_mult, "
        "dehydrate_hp_day, low_threshold, critical_threshold, eat_restore FROM thirst_rules LIMIT 1",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.maxThirst = sqlite3_column_int(stmt, 0);
        out.drainPerDay = (float)sqlite3_column_double(stmt, 1);
        out.heatMult = (float)sqlite3_column_double(stmt, 2);
        out.sprintMult = (float)sqlite3_column_double(stmt, 3);
        out.workMult = (float)sqlite3_column_double(stmt, 4);
        out.dehydrateHpDay = (float)sqlite3_column_double(stmt, 5);
        out.lowThreshold = sqlite3_column_int(stmt, 6);
        out.criticalThreshold = sqlite3_column_int(stmt, 7);
        out.eatRestore = sqlite3_column_int(stmt, 8);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetWarmthRules(WarmthRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT comfort_min, cold_threshold, severe_cold, heat_threshold, "
        "cold_hp_per_day, severe_hp_per_day, heat_hp_per_day, "
        "fire_warmth, fire_range, activity_warmth, sprint_heat, "
        "night_multiplier, low_threshold, sit_restore "
        "FROM warmth_rules LIMIT 1", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.comfortMin = (float)sqlite3_column_double(stmt, 0);
        out.coldThreshold = (float)sqlite3_column_double(stmt, 1);
        out.severeCold = (float)sqlite3_column_double(stmt, 2);
        out.heatThreshold = (float)sqlite3_column_double(stmt, 3);
        out.coldHpPerDay = (float)sqlite3_column_double(stmt, 4);
        out.severeHpPerDay = (float)sqlite3_column_double(stmt, 5);
        out.heatHpPerDay = (float)sqlite3_column_double(stmt, 6);
        out.fireWarmth = (float)sqlite3_column_double(stmt, 7);
        out.fireRange = (float)sqlite3_column_double(stmt, 8);
        out.activityWarmth = (float)sqlite3_column_double(stmt, 9);
        out.sprintHeat = (float)sqlite3_column_double(stmt, 10);
        out.nightMultiplier = (float)sqlite3_column_double(stmt, 11);
        out.lowThreshold = sqlite3_column_int(stmt, 12);
        out.sitRestore = (float)sqlite3_column_double(stmt, 13);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetStaminaRules(StaminaRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT max_stamina, regen_per_second, regen_sleeping, sprint_cost_sec, "
        "melee_cost, ranged_cost, chop_cost, mine_cost, build_cost, swim_cost_sec, "
        "low_threshold, sit_restore, sleep_restore FROM stamina_rules LIMIT 1", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.maxStamina = sqlite3_column_int(stmt, 0);
        out.regenPerSecond = (float)sqlite3_column_double(stmt, 1);
        out.regenSleeping = (float)sqlite3_column_double(stmt, 2);
        out.sprintCostSec = (float)sqlite3_column_double(stmt, 3);
        out.meleeCost = (float)sqlite3_column_double(stmt, 4);
        out.rangedCost = (float)sqlite3_column_double(stmt, 5);
        out.chopCost = (float)sqlite3_column_double(stmt, 6);
        out.mineCost = (float)sqlite3_column_double(stmt, 7);
        out.buildCost = (float)sqlite3_column_double(stmt, 8);
        out.swimCostSec = (float)sqlite3_column_double(stmt, 9);
        out.lowThreshold = sqlite3_column_int(stmt, 10);
        out.sitRestore = (float)sqlite3_column_double(stmt, 11);
        out.sleepRestore = (float)sqlite3_column_double(stmt, 12);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetDeathRules(DeathRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT respawn_delay, hp_on_respawn, hunger_on_respawn, thirst_on_respawn, "
        "stamina_on_respawn, drop_inventory, corpse_duration, skill_loss "
        "FROM death_rules LIMIT 1", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.respawnDelay = (float)sqlite3_column_double(stmt, 0);
        out.hpOnRespawn = sqlite3_column_int(stmt, 1);
        out.hungerOnRespawn = sqlite3_column_int(stmt, 2);
        out.thirstOnRespawn = sqlite3_column_int(stmt, 3);
        out.staminaOnRespawn = sqlite3_column_int(stmt, 4);
        out.dropInventory = sqlite3_column_int(stmt, 5) != 0;
        out.corpseDuration = (float)sqlite3_column_double(stmt, 6);
        out.skillLoss = (float)sqlite3_column_double(stmt, 7);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetFireRules(FireRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT light_radius, warmth_radius, warmth_value, fuel_per_hour, "
        "cook_requires_fuel, light_flicker FROM fire_rules LIMIT 1",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.lightRadius = (float)sqlite3_column_double(stmt, 0);
        out.warmthRadius = (float)sqlite3_column_double(stmt, 1);
        out.warmthValue = (float)sqlite3_column_double(stmt, 2);
        out.fuelPerHour = (float)sqlite3_column_double(stmt, 3);
        out.cookRequiresFuel = sqlite3_column_int(stmt, 4) != 0;
        out.lightFlicker = sqlite3_column_int(stmt, 5) != 0;
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetFuelInfo(int itemId, FuelInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, fuel_value, heat_bonus FROM fuel_types WHERE item_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, itemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.itemId = sqlite3_column_int(stmt, 0);
        out.fuelValue = (float)sqlite3_column_double(stmt, 1);
        out.heatBonus = (float)sqlite3_column_double(stmt, 2);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetWaterSource(const String& type, WaterSourceInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, type, thirst_restore, interact_time, disease_chance, requires "
        "FROM water_sources WHERE type = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, type.CString(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.id = sqlite3_column_int(stmt, 0);
        out.name = (const char*)sqlite3_column_text(stmt, 1);
        out.type = (const char*)sqlite3_column_text(stmt, 2);
        out.thirstRestore = sqlite3_column_int(stmt, 3);
        out.interactTime = (float)sqlite3_column_double(stmt, 4);
        out.diseaseChance = (float)sqlite3_column_double(stmt, 5);
        out.requires = sqlite3_column_int(stmt, 6);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<WaterSourceInfo> GameDB::GetAllWaterSources()
{
    Vector<WaterSourceInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, type, thirst_restore, interact_time, disease_chance, requires "
        "FROM water_sources", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        WaterSourceInfo info;
        info.id = sqlite3_column_int(stmt, 0);
        info.name = (const char*)sqlite3_column_text(stmt, 1);
        info.type = (const char*)sqlite3_column_text(stmt, 2);
        info.thirstRestore = sqlite3_column_int(stmt, 3);
        info.interactTime = (float)sqlite3_column_double(stmt, 4);
        info.diseaseChance = (float)sqlite3_column_double(stmt, 5);
        info.requires = sqlite3_column_int(stmt, 6);
        result.Push(info);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool GameDB::GetClothingWarmth(int itemId, float& warmth, float& rainResist)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT warmth, rain_resist FROM clothing_warmth WHERE item_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, itemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        warmth = (float)sqlite3_column_double(stmt, 0);
        rainResist = (float)sqlite3_column_double(stmt, 1);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// ============================================================================
// Inventory queries
// ============================================================================

bool GameDB::GetInventoryRules(InventoryRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT base_slots, max_weight, heavy_weight, max_weight_absolute, "
        "encumbered_speed, heavy_speed, heavy_stamina_mult FROM inventory_rules WHERE id = 1",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.baseSlots = sqlite3_column_int(stmt, 0);
        out.maxWeight = (float)sqlite3_column_double(stmt, 1);
        out.heavyWeight = (float)sqlite3_column_double(stmt, 2);
        out.maxWeightAbsolute = (float)sqlite3_column_double(stmt, 3);
        out.encumberedSpeed = (float)sqlite3_column_double(stmt, 4);
        out.heavySpeed = (float)sqlite3_column_double(stmt, 5);
        out.heavyStaminaMult = (float)sqlite3_column_double(stmt, 6);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<InventorySlot> GameDB::GetPlayerInventory(int playerId)
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

float GameDB::GetPlayerWeight(int playerId)
{
    if (!db_) return 0.0f;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT COALESCE(SUM(pi.quantity * i.weight), 0) "
        "FROM player_inventory pi JOIN items i ON i.id = pi.item_id "
        "WHERE pi.player_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0.0f;

    sqlite3_bind_int(stmt, 1, playerId);
    float weight = 0.0f;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        weight = (float)sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return weight;
}

int GameDB::GetItemCount(int playerId, int itemId)
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

bool GameDB::AddItemToInventory(int playerId, int itemId, int qty)
{
    if (!db_ || qty <= 0) return false;

    // Get item info for stack_max and weight check
    ItemInfo item;
    if (!GetItem(itemId, item))
        return false;

    // Weight check
    InventoryRules rules;
    if (!GetInventoryRules(rules))
        return false;

    float currentWeight = GetPlayerWeight(playerId);
    float addedWeight = item.weight * qty;
    if (currentWeight + addedWeight > rules.maxWeightAbsolute)
        return false;

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

    if (existingSlot && existingQty + qty <= item.stackMax)
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
        // Check available slots
        if (GetAvailableSlots(playerId) <= 0)
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
        sqlite3_bind_int(stmt, 4, item.durability > 0 ? item.durability : -1);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return true;
    }

    // Existing slot but would exceed stack_max — not enough room
    return false;
}

bool GameDB::RemoveItemFromInventory(int playerId, int itemId, int qty)
{
    if (!db_ || qty <= 0) return false;

    // Check current count
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
        // Remove the entire row
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
        // Decrement
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

int GameDB::GetAvailableSlots(int playerId)
{
    if (!db_) return 0;

    InventoryRules rules;
    if (!GetInventoryRules(rules))
        return 0;

    int maxSlots = rules.baseSlots;

    // Check for container bonus from equipped back slot
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT cb.extra_slots FROM player_inventory pi "
        "JOIN container_bonus cb ON cb.item_id = pi.item_id "
        "WHERE pi.player_id = ? AND (pi.slot = 'back' OR pi.slot = 'hand')",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, playerId);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            maxSlots += sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    // Count occupied bag slots
    rc = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM player_inventory WHERE player_id = ? AND slot = 'bag'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(stmt, 1, playerId);
    int used = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        used = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return maxSlots - used;
}

// =============================================================================
// Equipment queries
// =============================================================================

bool GameDB::EquipItem(int playerId, int itemId, const String& slot)
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

bool GameDB::UnequipItem(int playerId, const String& slot, int& outItemId)
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
    if (!AddItemToInventory(playerId, outItemId, 1))
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

int GameDB::GetEquippedItem(int playerId, const String& slot)
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

// =============================================================================
// Gather Source queries
// =============================================================================

static GatherSourceInfo ReadGatherSourceRow(sqlite3_stmt* stmt)
{
    GatherSourceInfo info;
    info.id = sqlite3_column_int(stmt, 0);
    info.name = (const char*)sqlite3_column_text(stmt, 1);
    info.itemId = sqlite3_column_int(stmt, 2);
    info.quantity = sqlite3_column_int(stmt, 3);
    info.toolReq = sqlite3_column_int(stmt, 4);
    const char* t = (const char*)sqlite3_column_text(stmt, 5);
    info.terrain = t ? t : "any";
    info.minHeight = (float)sqlite3_column_double(stmt, 6);
    info.maxHeight = (float)sqlite3_column_double(stmt, 7);
    const char* m = (const char*)sqlite3_column_text(stmt, 8);
    info.model = m ? m : "";
    info.respawn = (float)sqlite3_column_double(stmt, 9);
    const char* s = (const char*)sqlite3_column_text(stmt, 10);
    info.seasonal = s ? s : "any";
    return info;
}

Vector<GatherSourceInfo> GameDB::GetAllGatherSources()
{
    Vector<GatherSourceInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, item_id, quantity, tool_req, terrain, "
        "min_height, max_height, model, respawn, seasonal "
        "FROM gather_sources", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadGatherSourceRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

Vector<GatherSourceInfo> GameDB::GetGatherSourcesByTerrain(const String& terrain)
{
    Vector<GatherSourceInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, item_id, quantity, tool_req, terrain, "
        "min_height, max_height, model, respawn, seasonal "
        "FROM gather_sources WHERE terrain = ? OR terrain = 'any'",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, terrain.CString(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadGatherSourceRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

// =============================================================================
// Building queries
// =============================================================================

static BuildingTypeDBInfo ReadBuildingTypeRow(sqlite3_stmt* stmt)
{
    BuildingTypeDBInfo info;
    info.id = sqlite3_column_int(stmt, 0);
    info.name = (const char*)sqlite3_column_text(stmt, 1);
    const char* cat = (const char*)sqlite3_column_text(stmt, 2);
    info.category = cat ? cat : "";
    info.tier = sqlite3_column_int(stmt, 3);
    info.footprintX = (float)sqlite3_column_double(stmt, 4);
    info.footprintZ = (float)sqlite3_column_double(stmt, 5);
    info.height = (float)sqlite3_column_double(stmt, 6);
    info.maxHp = sqlite3_column_int(stmt, 7);
    info.decayRate = (float)sqlite3_column_double(stmt, 8);
    info.warmth = (float)sqlite3_column_double(stmt, 9);
    info.storageSlots = sqlite3_column_int(stmt, 10);
    info.sleepCapacity = sqlite3_column_int(stmt, 11);
    info.respawn = sqlite3_column_int(stmt, 12) != 0;
    const char* snap = (const char*)sqlite3_column_text(stmt, 13);
    info.snapType = snap ? snap : "free";
    const char* mdl = (const char*)sqlite3_column_text(stmt, 14);
    info.model = mdl ? mdl : "";
    const char* ghost = (const char*)sqlite3_column_text(stmt, 15);
    info.ghostModel = ghost ? ghost : "";
    const char* desc = (const char*)sqlite3_column_text(stmt, 16);
    info.description = desc ? desc : "";
    return info;
}

bool GameDB::GetBuildingType(int typeId, BuildingTypeDBInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, category, tier, footprint_x, footprint_z, height, "
        "max_hp, decay_rate, warmth, storage_slots, sleep_capacity, respawn, "
        "snap_type, model, ghost_model, description "
        "FROM building_types WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, typeId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out = ReadBuildingTypeRow(stmt);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<BuildingTypeDBInfo> GameDB::GetAllBuildingTypes()
{
    Vector<BuildingTypeDBInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, category, tier, footprint_x, footprint_z, height, "
        "max_hp, decay_rate, warmth, storage_slots, sleep_capacity, respawn, "
        "snap_type, model, ghost_model, description "
        "FROM building_types ORDER BY tier, id", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadBuildingTypeRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

Vector<BuildingTypeDBInfo> GameDB::GetBuildingTypesByCategory(const String& category)
{
    Vector<BuildingTypeDBInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, category, tier, footprint_x, footprint_z, height, "
        "max_hp, decay_rate, warmth, storage_slots, sleep_capacity, respawn, "
        "snap_type, model, ghost_model, description "
        "FROM building_types WHERE category = ? ORDER BY tier, id",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, category.CString(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadBuildingTypeRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

Vector<BuildingRecipeInput> GameDB::GetBuildingRecipe(int buildingTypeId)
{
    Vector<BuildingRecipeInput> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, quantity FROM building_recipes WHERE building_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, buildingTypeId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        BuildingRecipeInput input;
        input.itemId = sqlite3_column_int(stmt, 0);
        input.quantity = sqlite3_column_int(stmt, 1);
        result.Push(input);
    }
    sqlite3_finalize(stmt);
    return result;
}

// =============================================================================
// Placed building queries
// =============================================================================

static PlacedBuildingDBInfo ReadPlacedBuildingRow(sqlite3_stmt* stmt)
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

int GameDB::InsertPlacedBuilding(int buildingId, int ownerId, float px, float py, float pz,
                                  float rotation, int hp, int builtDay, int snappedTo)
{
    if (!db_) return -1;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "INSERT INTO placed_buildings (building_id, owner_id, pos_x, pos_y, pos_z, "
        "rotation, hp, built_day, snapped_to) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
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

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;

    return (int)sqlite3_last_insert_rowid(db_);
}

bool GameDB::RemovePlacedBuilding(int placedId)
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

Vector<PlacedBuildingDBInfo> GameDB::GetAllPlacedBuildings()
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
        result.Push(ReadPlacedBuildingRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

Vector<PlacedBuildingDBInfo> GameDB::GetPlacedBuildingsByOwner(int ownerId)
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
        result.Push(ReadPlacedBuildingRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

bool GameDB::UpdateBuildingHp(int placedId, int newHp)
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

bool GameDB::SetGateOpen(int placedId, bool open)
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

bool GameDB::SetLastRepair(int placedId, int gameDay)
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

bool GameDB::GetPlacedBuilding(int placedId, PlacedBuildingDBInfo& out)
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
        out = ReadPlacedBuildingRow(stmt);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

int GameDB::GetPlacedBuildingOwner(int placedId)
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

// =============================================================================
// Repair cost queries
// =============================================================================

Vector<RepairCostInfo> GameDB::GetRepairCosts(int buildingTypeId)
{
    Vector<RepairCostInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT item_id, quantity, hp_restored FROM repair_costs WHERE building_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, buildingTypeId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        RepairCostInfo cost;
        cost.itemId = sqlite3_column_int(stmt, 0);
        cost.quantity = sqlite3_column_int(stmt, 1);
        cost.hpRestored = sqlite3_column_int(stmt, 2);
        result.Push(cost);
    }
    sqlite3_finalize(stmt);
    return result;
}

// =============================================================================
// Weather damage queries
// =============================================================================

float GameDB::GetWeatherDamage(const String& weather, int tier)
{
    if (!db_) return 0.0f;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT extra_decay FROM weather_damage WHERE weather = ? AND tier = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0.0f;

    sqlite3_bind_text(stmt, 1, weather.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, tier);
    float damage = 0.0f;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        damage = (float)sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return damage;
}

// =============================================================================
// Wall strength queries
// =============================================================================

bool GameDB::DoesWallBlock(int buildingTypeId, int creatureId)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT blocks FROM wall_strength WHERE building_id = ? AND creature_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, buildingTypeId);
    sqlite3_bind_int(stmt, 2, creatureId);
    bool blocks = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        blocks = sqlite3_column_int(stmt, 0) != 0;
    sqlite3_finalize(stmt);
    return blocks;
}

// =============================================================================
// Snap rules queries
// =============================================================================

String GameDB::GetSnapAlign(const String& fromType, const String& toType)
{
    if (!db_) return String::EMPTY;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT align FROM snap_rules WHERE from_type = ? AND to_type = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return String::EMPTY;

    sqlite3_bind_text(stmt, 1, fromType.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, toType.CString(), -1, SQLITE_TRANSIENT);
    String align;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* a = (const char*)sqlite3_column_text(stmt, 0);
        align = a ? a : "";
    }
    sqlite3_finalize(stmt);
    return align;
}

// ---------------------------------------------------------------------------
// Crop queries
// ---------------------------------------------------------------------------

static CropTypeInfo ReadCropRow(sqlite3_stmt* stmt)
{
    CropTypeInfo c;
    c.seedItemId     = sqlite3_column_int(stmt, 0);
    c.harvestItemId  = sqlite3_column_int(stmt, 1);
    c.harvestQty     = sqlite3_column_int(stmt, 2);
    c.seedReturn     = sqlite3_column_int(stmt, 3);
    c.plantSeason    = (const char*)sqlite3_column_text(stmt, 4);
    c.harvestSeason  = (const char*)sqlite3_column_text(stmt, 5);
    c.growDays       = sqlite3_column_int(stmt, 6);
    c.minFlat        = (float)sqlite3_column_double(stmt, 7);
    c.nearWaterRange = (float)sqlite3_column_double(stmt, 8);
    c.toolReq        = sqlite3_column_int(stmt, 9);
    const char* m    = (const char*)sqlite3_column_text(stmt, 10);
    c.model          = m ? m : "";
    return c;
}

bool GameDB::GetCropType(int seedItemId, CropTypeInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT seed_item_id, harvest_item_id, harvest_qty, seed_return, "
        "plant_season, harvest_season, grow_days, min_flat, near_water_range, "
        "tool_req, model FROM crop_types WHERE seed_item_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, seedItemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out = ReadCropRow(stmt);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<CropTypeInfo> GameDB::GetAllCropTypes()
{
    Vector<CropTypeInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT seed_item_id, harvest_item_id, harvest_qty, seed_return, "
        "plant_season, harvest_season, grow_days, min_flat, near_water_range, "
        "tool_req, model FROM crop_types",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(ReadCropRow(stmt));
    sqlite3_finalize(stmt);
    return result;
}

bool GameDB::GetAlloyProperties(int alloyItemId, AlloyPropertiesInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT alloy_item_id, damage_mod, defense_mod, durability_pct, special "
        "FROM alloy_properties WHERE alloy_item_id = ?",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, alloyItemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.alloyItemId = sqlite3_column_int(stmt, 0);
        out.damageMod = sqlite3_column_int(stmt, 1);
        out.defenseMod = sqlite3_column_int(stmt, 2);
        out.durabilityPct = sqlite3_column_int(stmt, 3);
        const char* sp = (const char*)sqlite3_column_text(stmt, 4);
        out.special = sp ? sp : "";
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// ---------------------------------------------------------------------------
// Skill system
// ---------------------------------------------------------------------------

void GameDB::CacheSkillRules()
{
    if (!db_) return;

    // Cache level thresholds
    levelThresholds_.Clear();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT level, xp_required FROM skill_levels ORDER BY level", -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int level = sqlite3_column_int(stmt, 0);
            int xpReq = sqlite3_column_int(stmt, 1);
            // Ensure vector is big enough (levels may not be contiguous but typically are 0-10)
            while ((int)levelThresholds_.Size() <= level)
                levelThresholds_.Push(0);
            levelThresholds_[level] = xpReq;
        }
    }
    sqlite3_finalize(stmt);

    // Cache max levels per skill
    skillMaxLevels_.Clear();
    stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT id, max_level FROM skills", -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            skillMaxLevels_[sqlite3_column_int(stmt, 0)] = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);

    // Cache XP triggers
    xpTriggers_.Clear();
    stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT action, skill_id, xp_amount FROM xp_triggers", -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* action = (const char*)sqlite3_column_text(stmt, 0);
            XPTrigger trigger;
            trigger.skillId = sqlite3_column_int(stmt, 1);
            trigger.xpAmount = sqlite3_column_int(stmt, 2);
            xpTriggers_[action ? action : ""].Push(trigger);
        }
    }
    sqlite3_finalize(stmt);

    URHO3D_LOGINFOF("GameDB: cached %u skill rules, %u XP triggers, %u level thresholds",
        skillMaxLevels_.Size(), xpTriggers_.Size(), levelThresholds_.Size());
}

void GameDB::CacheTechniqueDiscovery()
{
    if (!db_) return;

    discoveryChains_.Clear();
    skillEpochTier_.Clear();
    skillNames_.Clear();

    // Cache skill names for logging
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT id, name FROM skills", -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            skillNames_[sqlite3_column_int(stmt, 0)] = (const char*)sqlite3_column_text(stmt, 1);
    }
    sqlite3_finalize(stmt);

    // Cache epoch tiers and level caps from technique_tiers
    stmt = nullptr;
    skillLevelCaps_.Clear();
    if (sqlite3_prepare_v2(db_, "SELECT skill_id, epoch_tier, level_cap FROM technique_tiers", -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int skillId = sqlite3_column_int(stmt, 0);
            skillEpochTier_[skillId] = sqlite3_column_int(stmt, 1);
            skillLevelCaps_[skillId] = sqlite3_column_int(stmt, 2);
        }
    }
    sqlite3_finalize(stmt);

    // Cache discovery chains
    stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT source_skill_id, target_skill_id, prereq_rating, discovery_dc FROM technique_discovery",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            DiscoveryChain chain;
            int sourceId = sqlite3_column_int(stmt, 0);
            chain.targetSkillId = sqlite3_column_int(stmt, 1);
            chain.prereqRating = sqlite3_column_int(stmt, 2);
            chain.dc = sqlite3_column_int(stmt, 3);
            discoveryChains_[sourceId].Push(chain);
        }
    }
    sqlite3_finalize(stmt);

    URHO3D_LOGINFOF("GameDB: cached %u technique discovery chains, %u epoch tiers",
        discoveryChains_.Size(), skillEpochTier_.Size());
}

String GameDB::CheckTechniqueDiscovery(int playerId, int sourceSkillId, int sourceLevel, int settlementEpoch)
{
    if (!db_) return String::EMPTY;

    auto it = discoveryChains_.Find(sourceSkillId);
    if (it == discoveryChains_.End())
        return String::EMPTY;

    for (const DiscoveryChain& chain : it->second_)
    {
        if (sourceLevel < chain.prereqRating)
            continue;

        // Epoch gate — target skill may require a higher epoch
        auto epochIt = skillEpochTier_.Find(chain.targetSkillId);
        if (epochIt != skillEpochTier_.End() && settlementEpoch < epochIt->second_)
            continue;

        // Already discovered?
        int existing = GetSkillLevel(playerId, chain.targetSkillId);
        if (existing > 0)
            continue;

        // Roll d20 + INT mod vs DC (smarter NPCs discover faster)
        int intMod = 0;
        auto intIt = pendingIntMods_.Find(playerId);
        if (intIt != pendingIntMods_.End())
            intMod = intIt->second_;
        int roll = (Rand() % 20) + 1 + intMod;
        if (roll < chain.dc)
            continue;

        // Discovery! Insert with level 1, xp 0
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO player_skills (player_id, skill_id, xp, level) VALUES (?, ?, 0, 1)",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, playerId);
            sqlite3_bind_int(stmt, 2, chain.targetSkillId);
            if (sqlite3_step(stmt) != SQLITE_DONE)
                URHO3D_LOGERRORF("CheckTechniqueDiscovery: step failed for skill %d", chain.targetSkillId);
        }
        sqlite3_finalize(stmt);

        auto nameIt = skillNames_.Find(chain.targetSkillId);
        String name = (nameIt != skillNames_.End()) ? nameIt->second_ : String(chain.targetSkillId);

        auto sourceNameIt = skillNames_.Find(sourceSkillId);
        String sourceName = (sourceNameIt != skillNames_.End()) ? sourceNameIt->second_ : String(sourceSkillId);

        URHO3D_LOGINFOF("[Technique] Player %d discovered %s (from %s %d, roll %d vs DC %d)",
            playerId, name.CString(), sourceName.CString(), sourceLevel, roll, chain.dc);

        pendingDiscoveries_.Push({playerId, chain.targetSkillId, name});
        return name;
    }

    return String::EMPTY;
}

bool GameDB::AwardXP(int playerId, const String& action)
{
    if (!db_) return false;

    auto it = xpTriggers_.Find(action);
    if (it == xpTriggers_.End())
        return false;

    bool awarded = false;
    for (const XPTrigger& trigger : it->second_)
    {
        int oldLevel = GetSkillLevel(playerId, trigger.skillId);
        int currentXP = GetSkillXP(playerId, trigger.skillId);
        int newXP = currentXP + trigger.xpAmount;
        int newLevel = ComputeLevel(newXP);

        // Cap at max level
        auto maxIt = skillMaxLevels_.Find(trigger.skillId);
        int maxLevel = (maxIt != skillMaxLevels_.End()) ? maxIt->second_ : 10;
        if (newLevel > maxLevel)
            newLevel = maxLevel;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR REPLACE INTO player_skills (player_id, skill_id, xp, level) "
                          "VALUES (?, ?, ?, ?)";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, playerId);
            sqlite3_bind_int(stmt, 2, trigger.skillId);
            sqlite3_bind_int(stmt, 3, newXP);
            sqlite3_bind_int(stmt, 4, newLevel);
            sqlite3_step(stmt);
            awarded = true;
        }
        sqlite3_finalize(stmt);

        // Check technique discovery on level-up
        if (newLevel > oldLevel && !discoveryChains_.Empty())
            CheckTechniqueDiscovery(playerId, trigger.skillId, newLevel);
    }
    return awarded;
}

bool GameDB::AwardXP(int playerId, const String& action, float xpMultiplier)
{
    if (!db_) return false;

    auto it = xpTriggers_.Find(action);
    if (it == xpTriggers_.End())
        return false;

    bool awarded = false;
    for (const XPTrigger& trigger : it->second_)
    {
        int scaledXP = static_cast<int>(trigger.xpAmount * xpMultiplier + 0.5f);
        if (scaledXP < 1) scaledXP = 1;
        int oldLevel = GetSkillLevel(playerId, trigger.skillId);
        int currentXP = GetSkillXP(playerId, trigger.skillId);
        int newXP = currentXP + scaledXP;
        int newLevel = ComputeLevel(newXP);

        auto maxIt = skillMaxLevels_.Find(trigger.skillId);
        int maxLevel = (maxIt != skillMaxLevels_.End()) ? maxIt->second_ : 10;
        if (newLevel > maxLevel)
            newLevel = maxLevel;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR REPLACE INTO player_skills (player_id, skill_id, xp, level) "
                          "VALUES (?, ?, ?, ?)";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, playerId);
            sqlite3_bind_int(stmt, 2, trigger.skillId);
            sqlite3_bind_int(stmt, 3, newXP);
            sqlite3_bind_int(stmt, 4, newLevel);
            sqlite3_step(stmt);
            awarded = true;
        }
        sqlite3_finalize(stmt);

        // Check technique discovery on level-up
        if (newLevel > oldLevel && !discoveryChains_.Empty())
            CheckTechniqueDiscovery(playerId, trigger.skillId, newLevel);
    }
    return awarded;
}

bool GameDB::AwardXP(int playerId, const String& action, float xpMultiplier, int settlementEpoch)
{
    if (!db_) return false;

    auto it = xpTriggers_.Find(action);
    if (it == xpTriggers_.End())
        return false;

    bool awarded = false;
    for (const XPTrigger& trigger : it->second_)
    {
        int scaledXP = static_cast<int>(trigger.xpAmount * xpMultiplier + 0.5f);
        if (scaledXP < 1) scaledXP = 1;
        int oldLevel = GetSkillLevel(playerId, trigger.skillId);
        int currentXP = GetSkillXP(playerId, trigger.skillId);
        int newXP = currentXP + scaledXP;
        int newLevel = ComputeLevel(newXP);

        // Cap at epoch-gated effective max level
        int maxLevel = GetEffectiveMaxLevel(trigger.skillId, settlementEpoch);
        if (maxLevel <= 0)
            continue;  // skill not available at this epoch
        if (newLevel > maxLevel)
            newLevel = maxLevel;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR REPLACE INTO player_skills (player_id, skill_id, xp, level) "
                          "VALUES (?, ?, ?, ?)";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, playerId);
            sqlite3_bind_int(stmt, 2, trigger.skillId);
            sqlite3_bind_int(stmt, 3, newXP);
            sqlite3_bind_int(stmt, 4, newLevel);
            sqlite3_step(stmt);
            awarded = true;
        }
        sqlite3_finalize(stmt);

        // Check technique discovery on level-up (epoch-aware)
        if (newLevel > oldLevel && !discoveryChains_.Empty())
            CheckTechniqueDiscovery(playerId, trigger.skillId, newLevel, settlementEpoch);
    }
    return awarded;
}

int GameDB::GetEffectiveMaxLevel(int skillId, int settlementEpoch) const
{
    auto tierIt = skillEpochTier_.Find(skillId);
    if (tierIt == skillEpochTier_.End())
    {
        // No epoch restriction — use absolute max from skills table
        auto maxIt = skillMaxLevels_.Find(skillId);
        return (maxIt != skillMaxLevels_.End()) ? maxIt->second_ : 10;
    }

    if (settlementEpoch < tierIt->second_)
        return 0;  // epoch not reached — skill unavailable

    // Use technique_tiers.level_cap
    auto capIt = skillLevelCaps_.Find(skillId);
    return (capIt != skillLevelCaps_.End()) ? capIt->second_ : 10;
}

int GameDB::GetSkillLevel(int playerId, int skillId)
{
    if (!db_) return 0;

    int level = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT level FROM player_skills WHERE player_id=? AND skill_id=?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, skillId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            level = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return level;
}

int GameDB::GetSkillXP(int playerId, int skillId)
{
    if (!db_) return 0;

    int xp = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT xp FROM player_skills WHERE player_id=? AND skill_id=?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, skillId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            xp = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return xp;
}

bool GameDB::AddXPDirect(int playerId, int skillId, int xpAmount)
{
    if (!db_ || xpAmount <= 0)
        return false;

    int currentXP = GetSkillXP(playerId, skillId);
    int newXP = currentXP + xpAmount;
    int newLevel = ComputeLevel(newXP);

    auto maxIt = skillMaxLevels_.Find(skillId);
    int maxLevel = (maxIt != skillMaxLevels_.End()) ? maxIt->second_ : 10;
    if (newLevel > maxLevel)
        newLevel = maxLevel;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO player_skills (player_id, skill_id, xp, level) "
                      "VALUES (?, ?, ?, ?)";
    bool ok = false;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, skillId);
        sqlite3_bind_int(stmt, 3, newXP);
        sqlite3_bind_int(stmt, 4, newLevel);
        if (sqlite3_step(stmt) == SQLITE_DONE)
            ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool GameDB::ActionRewardsSkill(const String& action, int skillId) const
{
    auto it = xpTriggers_.Find(action);
    if (it == xpTriggers_.End())
        return false;
    for (const auto& trigger : it->second_)
    {
        if (trigger.skillId == skillId)
            return true;
    }
    return false;
}

int GameDB::ComputeLevel(int xp) const
{
    int level = 0;
    for (unsigned i = 0; i < levelThresholds_.Size(); ++i)
    {
        if (xp >= levelThresholds_[i])
            level = (int)i;
        else
            break;
    }
    return level;
}

// --- Economic Doctrine queries ---

bool GameDB::GetResourceType(int resourceId, ResourceTypeInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, name, category, item_id, regen_per_day, extract_per_use, extract_time, "
        "tool_req, scarcity_50, scarcity_0, seasonal, tier "
        "FROM resource_types WHERE id = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, resourceId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.id = sqlite3_column_int(stmt, 0);
        out.name = (const char*)sqlite3_column_text(stmt, 1);
        out.category = (const char*)sqlite3_column_text(stmt, 2);
        out.itemId = sqlite3_column_int(stmt, 3);
        out.regenPerDay = (float)sqlite3_column_double(stmt, 4);
        out.extractPerUse = (float)sqlite3_column_double(stmt, 5);
        out.extractTime = (float)sqlite3_column_double(stmt, 6);
        out.toolReq = sqlite3_column_int(stmt, 7);
        out.scarcity50 = (float)sqlite3_column_double(stmt, 8);
        out.scarcity0 = (float)sqlite3_column_double(stmt, 9);
        out.seasonal = (const char*)sqlite3_column_text(stmt, 10);
        out.tier = sqlite3_column_int(stmt, 11);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<ResourceTypeInfo> GameDB::GetAllResourceTypes()
{
    Vector<ResourceTypeInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, name, category, item_id, regen_per_day, extract_per_use, extract_time, "
        "tool_req, scarcity_50, scarcity_0, seasonal, tier FROM resource_types",
        -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ResourceTypeInfo r;
        r.id = sqlite3_column_int(stmt, 0);
        r.name = (const char*)sqlite3_column_text(stmt, 1);
        r.category = (const char*)sqlite3_column_text(stmt, 2);
        r.itemId = sqlite3_column_int(stmt, 3);
        r.regenPerDay = (float)sqlite3_column_double(stmt, 4);
        r.extractPerUse = (float)sqlite3_column_double(stmt, 5);
        r.extractTime = (float)sqlite3_column_double(stmt, 6);
        r.toolReq = sqlite3_column_int(stmt, 7);
        r.scarcity50 = (float)sqlite3_column_double(stmt, 8);
        r.scarcity0 = (float)sqlite3_column_double(stmt, 9);
        r.seasonal = (const char*)sqlite3_column_text(stmt, 10);
        r.tier = sqlite3_column_int(stmt, 11);
        result.Push(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

Vector<ResourceTypeInfo> GameDB::GetResourceTypesByCategory(const String& category)
{
    Vector<ResourceTypeInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, name, category, item_id, regen_per_day, extract_per_use, extract_time, "
        "tool_req, scarcity_50, scarcity_0, seasonal, tier FROM resource_types WHERE category = ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    sqlite3_bind_text(stmt, 1, category.CString(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ResourceTypeInfo r;
        r.id = sqlite3_column_int(stmt, 0);
        r.name = (const char*)sqlite3_column_text(stmt, 1);
        r.category = (const char*)sqlite3_column_text(stmt, 2);
        r.itemId = sqlite3_column_int(stmt, 3);
        r.regenPerDay = (float)sqlite3_column_double(stmt, 4);
        r.extractPerUse = (float)sqlite3_column_double(stmt, 5);
        r.extractTime = (float)sqlite3_column_double(stmt, 6);
        r.toolReq = sqlite3_column_int(stmt, 7);
        r.scarcity50 = (float)sqlite3_column_double(stmt, 8);
        r.scarcity0 = (float)sqlite3_column_double(stmt, 9);
        r.seasonal = (const char*)sqlite3_column_text(stmt, 10);
        r.tier = sqlite3_column_int(stmt, 11);
        result.Push(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool GameDB::GetRegionResource(int regionId, int resourceId, RegionResourceInfo& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT region_id, resource_id, current_amount, max_amount, total_extracted, last_regen_day "
        "FROM region_resources WHERE region_id = ? AND resource_id = ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, regionId);
    sqlite3_bind_int(stmt, 2, resourceId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.regionId = sqlite3_column_int(stmt, 0);
        out.resourceId = sqlite3_column_int(stmt, 1);
        out.currentAmount = (float)sqlite3_column_double(stmt, 2);
        out.maxAmount = (float)sqlite3_column_double(stmt, 3);
        out.totalExtracted = (float)sqlite3_column_double(stmt, 4);
        out.lastRegenDay = sqlite3_column_int(stmt, 5);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

Vector<RegionResourceInfo> GameDB::GetRegionResources(int regionId)
{
    Vector<RegionResourceInfo> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT region_id, resource_id, current_amount, max_amount, total_extracted, last_regen_day "
        "FROM region_resources WHERE region_id = ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    sqlite3_bind_int(stmt, 1, regionId);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        RegionResourceInfo r;
        r.regionId = sqlite3_column_int(stmt, 0);
        r.resourceId = sqlite3_column_int(stmt, 1);
        r.currentAmount = (float)sqlite3_column_double(stmt, 2);
        r.maxAmount = (float)sqlite3_column_double(stmt, 3);
        r.totalExtracted = (float)sqlite3_column_double(stmt, 4);
        r.lastRegenDay = sqlite3_column_int(stmt, 5);
        result.Push(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

float GameDB::GetScarcityModifier(int regionId, int resourceId)
{
    RegionResourceInfo pool;
    if (!GetRegionResource(regionId, resourceId, pool))
        return 0.0f;

    ResourceTypeInfo type;
    if (!GetResourceType(resourceId, type))
        return 0.0f;

    if (pool.maxAmount <= 0.0f)
        return 0.0f;

    float fraction = pool.currentAmount / pool.maxAmount;

    // Below scarcity_0: resource is exhausted
    if (fraction <= type.scarcity0)
        return 0.0f;

    // Below scarcity_50: linear interpolation from 0 to 0.5
    if (fraction <= type.scarcity50)
    {
        float t = (fraction - type.scarcity0) / (type.scarcity50 - type.scarcity0);
        return t * 0.5f;
    }

    // Above scarcity_50: linear interpolation from 0.5 to 1.0
    float t = (fraction - type.scarcity50) / (1.0f - type.scarcity50);
    return 0.5f + t * 0.5f;
}

ExtractionResult GameDB::ExtractResource(int playerId, int regionId, int resourceId, int gameDay)
{
    ExtractionResult result;
    result.success = false;
    result.itemId = 0;
    result.quantity = 0;
    result.scarcityMod = 0.0f;
    result.remaining = 0.0f;

    if (!db_) return result;

    // Get the resource type for item_id and extract_per_use
    ResourceTypeInfo type;
    if (!GetResourceType(resourceId, type))
        return result;

    // Get current pool state
    RegionResourceInfo pool;
    if (!GetRegionResource(regionId, resourceId, pool))
        return result;

    // Compute scarcity modifier
    float scarcity = GetScarcityModifier(regionId, resourceId);
    if (scarcity <= 0.0f)
        return result;  // Exhausted — nothing to extract

    // Apply scarcity to yield: full yield at scarcity 1.0, reduced yield below
    float rawYield = type.extractPerUse;
    float actualYield = rawYield * scarcity;

    // Can't extract more than what's left
    if (actualYield > pool.currentAmount)
        actualYield = pool.currentAmount;

    // Round to integer quantity (floor, min 1 if anything remains)
    int quantity = (int)actualYield;
    if (quantity < 1 && pool.currentAmount > 0.0f)
        quantity = 1;

    // Deduct from pool
    float newAmount = pool.currentAmount - (float)quantity;
    if (newAmount < 0.0f) newAmount = 0.0f;

    // Update region_resources
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "UPDATE region_resources SET current_amount = ?, total_extracted = total_extracted + ? "
        "WHERE region_id = ? AND resource_id = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_double(stmt, 1, newAmount);
        sqlite3_bind_double(stmt, 2, (double)quantity);
        sqlite3_bind_int(stmt, 3, regionId);
        sqlite3_bind_int(stmt, 4, resourceId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Log extraction
    stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO extraction_log (player_id, region_id, resource_id, amount, game_day, scarcity_mod) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, playerId);
        sqlite3_bind_int(stmt, 2, regionId);
        sqlite3_bind_int(stmt, 3, resourceId);
        sqlite3_bind_double(stmt, 4, (double)quantity);
        sqlite3_bind_int(stmt, 5, gameDay);
        sqlite3_bind_double(stmt, 6, (double)scarcity);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    result.success = true;
    result.itemId = type.itemId;
    result.quantity = quantity;
    result.scarcityMod = scarcity;
    result.remaining = newAmount;
    return result;
}

void GameDB::RegenerateResources(int regionId, int gameDay)
{
    if (!db_) return;

    // Get all resources for this region and apply daily regeneration
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT rr.resource_id, rr.current_amount, rr.max_amount, rr.last_regen_day, "
        "rt.regen_per_day "
        "FROM region_resources rr "
        "JOIN resource_types rt ON rr.resource_id = rt.id "
        "WHERE rr.region_id = ? AND rr.last_regen_day < ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_int(stmt, 1, regionId);
    sqlite3_bind_int(stmt, 2, gameDay);

    // Collect updates (can't modify while iterating)
    struct RegenUpdate { int resourceId; float newAmount; };
    Vector<RegenUpdate> updates;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int resId = sqlite3_column_int(stmt, 0);
        float current = (float)sqlite3_column_double(stmt, 1);
        float max = (float)sqlite3_column_double(stmt, 2);
        int lastDay = sqlite3_column_int(stmt, 3);
        float regenRate = (float)sqlite3_column_double(stmt, 4);

        // Regenerate for each day missed
        int daysMissed = gameDay - lastDay;
        if (daysMissed <= 0) continue;

        float regen = regenRate * (float)daysMissed;
        float newAmount = current + regen;
        if (newAmount > max) newAmount = max;

        RegenUpdate u;
        u.resourceId = resId;
        u.newAmount = newAmount;
        updates.Push(u);
    }
    sqlite3_finalize(stmt);

    // Apply updates
    for (unsigned i = 0; i < updates.Size(); ++i)
    {
        stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
            "UPDATE region_resources SET current_amount = ?, last_regen_day = ? "
            "WHERE region_id = ? AND resource_id = ?",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_double(stmt, 1, (double)updates[i].newAmount);
            sqlite3_bind_int(stmt, 2, gameDay);
            sqlite3_bind_int(stmt, 3, regionId);
            sqlite3_bind_int(stmt, 4, updates[i].resourceId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

bool GameDB::GetBreedingRules(int creatureId, BreedingRules& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT creature_id, breed_interval, litter_size, maturity_days, min_pop_breed, "
        "max_pop_ratio, starvation_threshold, birth_rate "
        "FROM breeding_rules WHERE creature_id = ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, creatureId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.creatureId = sqlite3_column_int(stmt, 0);
        out.breedInterval = sqlite3_column_int(stmt, 1);
        out.litterSize = sqlite3_column_int(stmt, 2);
        out.maturityDays = sqlite3_column_int(stmt, 3);
        out.minPopBreed = sqlite3_column_int(stmt, 4);
        out.maxPopRatio = (float)sqlite3_column_double(stmt, 5);
        out.starvationThreshold = (float)sqlite3_column_double(stmt, 6);
        out.birthRate = sqlite3_column_type(stmt, 7) != SQLITE_NULL
            ? (float)sqlite3_column_double(stmt, 7) : 1.0f;
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool GameDB::GetTradeValue(int itemId, TradeValue& out)
{
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT item_id, base_value, scarcity_mult, demand_mult, last_update "
        "FROM trade_values WHERE item_id = ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(stmt, 1, itemId);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.itemId = sqlite3_column_int(stmt, 0);
        out.baseValue = (float)sqlite3_column_double(stmt, 1);
        out.scarcityMult = (float)sqlite3_column_double(stmt, 2);
        out.demandMult = (float)sqlite3_column_double(stmt, 3);
        out.lastUpdate = sqlite3_column_int(stmt, 4);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

void GameDB::UpdateTradeValues(int gameDay)
{
    if (!db_) return;

    float interval = GetEconomicConstant("trade_value_update_interval", 7.0f);
    float scarcityCap = GetEconomicConstant("scarcity_value_cap", 5.0f);

    // For each item that has a trade value and is linked to a resource type,
    // compute average scarcity across all regions and update the multiplier.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT tv.item_id, rt.id "
        "FROM trade_values tv "
        "JOIN resource_types rt ON rt.item_id = tv.item_id "
        "WHERE tv.last_update + ? <= ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_double(stmt, 1, (double)interval);
    sqlite3_bind_int(stmt, 2, gameDay);

    struct ValueUpdate { int itemId; float scarcityMult; };
    Vector<ValueUpdate> updates;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int itemId = sqlite3_column_int(stmt, 0);
        int resourceId = sqlite3_column_int(stmt, 1);

        // Average scarcity across all regions that have this resource
        sqlite3_stmt* avgStmt = nullptr;
        float avgScarcity = 1.0f;
        if (sqlite3_prepare_v2(db_,
            "SELECT AVG(current_amount / max_amount) FROM region_resources "
            "WHERE resource_id = ? AND max_amount > 0",
            -1, &avgStmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(avgStmt, 1, resourceId);
            if (sqlite3_step(avgStmt) == SQLITE_ROW)
                avgScarcity = (float)sqlite3_column_double(avgStmt, 0);
            sqlite3_finalize(avgStmt);
        }

        // Inverse relationship: scarcer = more valuable
        // At full abundance (1.0): multiplier = 1.0
        // At half abundance (0.5): multiplier = 2.0
        // At near-zero: multiplier capped at scarcityCap
        float mult = 1.0f;
        if (avgScarcity > 0.01f)
            mult = 1.0f / avgScarcity;
        if (mult > scarcityCap)
            mult = scarcityCap;

        ValueUpdate u;
        u.itemId = itemId;
        u.scarcityMult = mult;
        updates.Push(u);
    }
    sqlite3_finalize(stmt);

    // Apply updates
    for (unsigned i = 0; i < updates.Size(); ++i)
    {
        stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
            "UPDATE trade_values SET scarcity_mult = ?, last_update = ? WHERE item_id = ?",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_double(stmt, 1, (double)updates[i].scarcityMult);
            sqlite3_bind_int(stmt, 2, gameDay);
            sqlite3_bind_int(stmt, 3, updates[i].itemId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

float GameDB::GetEconomicConstant(const String& key, float defaultVal)
{
    if (!db_) return defaultVal;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT value FROM economic_constants WHERE key = ?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return defaultVal;

    sqlite3_bind_text(stmt, 1, key.CString(), -1, SQLITE_TRANSIENT);
    float result = defaultVal;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = (float)sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

}

#endif // URHO3D_DATABASE_SQLITE
