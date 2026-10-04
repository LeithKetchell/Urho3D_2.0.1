-- Game Database Schema
-- Urho3D Survival Game — SQL-driven game rules
-- Apply: sqlite3 game_rules.db < schema.sql

PRAGMA journal_mode=WAL;

-- Items: everything the player can hold, wear, place, eat, or throw
CREATE TABLE IF NOT EXISTS items (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    category    TEXT NOT NULL,
    stack_max   INTEGER DEFAULT 1,
    weight      REAL DEFAULT 1.0,
    durability  INTEGER DEFAULT 0,
    decay_time  REAL DEFAULT 0,
    model       TEXT,
    icon        TEXT,
    description TEXT,
    tier        INTEGER DEFAULT 0
);

-- Recipes: every crafting recipe
CREATE TABLE IF NOT EXISTS recipes (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    output_id   INTEGER NOT NULL REFERENCES items(id),
    output_qty  INTEGER DEFAULT 1,
    craft_time  REAL DEFAULT 2.0,
    tool_req    INTEGER DEFAULT 0,
    station_req INTEGER DEFAULT 0,
    tier        INTEGER DEFAULT 0,
    description TEXT
);

CREATE TABLE IF NOT EXISTS recipe_inputs (
    recipe_id   INTEGER NOT NULL REFERENCES recipes(id),
    item_id     INTEGER NOT NULL REFERENCES items(id),
    quantity    INTEGER DEFAULT 1,
    consumed    INTEGER DEFAULT 1,
    UNIQUE(recipe_id, item_id)
);

-- Combat stats for weapons and armor
CREATE TABLE IF NOT EXISTS combat_stats (
    item_id     INTEGER PRIMARY KEY REFERENCES items(id),
    attack_mod  INTEGER DEFAULT 0,
    defense_mod INTEGER DEFAULT 0,
    damage      INTEGER DEFAULT 0,
    damage_var  INTEGER DEFAULT 4,
    range       REAL DEFAULT 2.0,
    speed_mod   INTEGER DEFAULT 0,
    slot        TEXT DEFAULT 'hand',
    two_handed  INTEGER DEFAULT 0
);

-- Creatures: every animal and NPC
CREATE TABLE IF NOT EXISTS creatures (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,
    hp              INTEGER NOT NULL,
    attack          INTEGER DEFAULT 0,
    defense         INTEGER DEFAULT 10,
    damage          INTEGER DEFAULT 0,
    damage_var      INTEGER DEFAULT 4,
    speed           INTEGER DEFAULT 5,
    detection_range REAL DEFAULT 15.0,
    aggression      TEXT DEFAULT 'flee',
    pack_size       INTEGER DEFAULT 1,
    habitat         TEXT DEFAULT 'any',
    model           TEXT,
    idle_anim       TEXT,
    run_anim        TEXT,
    attack_anim     TEXT,
    die_anim        TEXT,
    desired_size    REAL DEFAULT 1.0,
    wander_radius   REAL DEFAULT 20.0,
    sound_near      REAL DEFAULT 1.0,    -- SoundSource3D near distance (metres)
    sound_far       REAL DEFAULT 50.0,  -- SoundSource3D far distance (metres)
    flee_speed      REAL DEFAULT 6.0,   -- run speed when fleeing
    flee_distance   REAL DEFAULT 30.0,  -- how far to flee
    min_idle        REAL DEFAULT 3.0,   -- minimum idle duration (seconds)
    max_idle        REAL DEFAULT 8.0,   -- maximum idle duration (seconds)
    vision_range    REAL DEFAULT 0.0,   -- sight distance (0 = blind)
    vision_angle    REAL DEFAULT 0.5,   -- cosine of vision half-angle (0.5 = 120 deg)
    is_predator     INTEGER DEFAULT 0,  -- hunts live prey
    is_scavenger    INTEGER DEFAULT 0,  -- investigates corpses
    food_grass_wt   REAL DEFAULT 0.6    -- vegetation food preference (0 = carnivore)
);

-- Loot tables: what creatures drop
CREATE TABLE IF NOT EXISTS loot_table (
    creature_id INTEGER NOT NULL REFERENCES creatures(id),
    item_id     INTEGER NOT NULL REFERENCES items(id),
    quantity    INTEGER DEFAULT 1,
    chance      REAL DEFAULT 1.0,
    tool_req    INTEGER DEFAULT 0
);

-- Gather sources: world objects the player can interact with
CREATE TABLE IF NOT EXISTS gather_sources (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    item_id     INTEGER NOT NULL REFERENCES items(id),
    quantity    INTEGER DEFAULT 1,
    tool_req    INTEGER DEFAULT 0,
    terrain     TEXT DEFAULT 'any',
    min_height  REAL DEFAULT -999,
    max_height  REAL DEFAULT 999,
    model       TEXT,
    respawn     REAL DEFAULT 0,
    seasonal    TEXT DEFAULT 'any'
);

-- Trap rules: what each trap catches
CREATE TABLE IF NOT EXISTS trap_rules (
    trap_id       INTEGER NOT NULL REFERENCES items(id),
    creature_id   INTEGER NOT NULL REFERENCES creatures(id),
    hold_strength INTEGER DEFAULT 10,
    bait_id       INTEGER DEFAULT 0,
    attract_range REAL DEFAULT 30.0,
    attract_time  REAL DEFAULT 120.0,
    live_capture  INTEGER DEFAULT 0     -- 1 = cage trap (captures alive for taming)
);

-- Food properties
CREATE TABLE IF NOT EXISTS food_properties (
    item_id       INTEGER PRIMARY KEY REFERENCES items(id),
    hunger        INTEGER DEFAULT 0,
    health        INTEGER DEFAULT 0,
    warmth        REAL DEFAULT 0,
    spoil_time    REAL DEFAULT 0,
    cooked        INTEGER DEFAULT 0,
    poison_chance REAL DEFAULT 0
);

-- Climate rules: temperature by season and time of day
CREATE TABLE IF NOT EXISTS climate_rules (
    season      TEXT NOT NULL,
    time_of_day TEXT NOT NULL,
    base_temp   REAL NOT NULL,
    wind_chill  REAL DEFAULT 0,
    rain_chill  REAL DEFAULT 0
);

-- Situational combat modifiers (Phase 3). Values used by server ComputeSituationalMods.
-- Tunable via SQL UPDATE without recompilation.
CREATE TABLE IF NOT EXISTS combat_modifiers (
    name        TEXT PRIMARY KEY,
    attack_mod  INTEGER DEFAULT 0,
    defense_mod INTEGER DEFAULT 0,
    speed_mod   INTEGER DEFAULT 0,
    description TEXT
);

-- Clothing warmth values
CREATE TABLE IF NOT EXISTS clothing_warmth (
    item_id     INTEGER PRIMARY KEY REFERENCES items(id),
    warmth      REAL DEFAULT 0,
    rain_resist REAL DEFAULT 0
);

-- Building placement rules
CREATE TABLE IF NOT EXISTS building_rules (
    item_id     INTEGER PRIMARY KEY REFERENCES items(id),
    min_flat    REAL DEFAULT 0.85,
    near_water  INTEGER DEFAULT 0,
    near_fire   INTEGER DEFAULT 0,
    footprint_x REAL DEFAULT 2.0,
    footprint_z REAL DEFAULT 2.0,
    capacity    INTEGER DEFAULT 0,
    warmth      REAL DEFAULT 0,
    respawn     INTEGER DEFAULT 0
);

-- Player runtime tables
CREATE TABLE IF NOT EXISTS player_inventory (
    player_id   INTEGER NOT NULL,
    item_id     INTEGER NOT NULL REFERENCES items(id),
    quantity    INTEGER DEFAULT 1,
    durability  INTEGER DEFAULT -1,
    slot        TEXT DEFAULT 'bag',
    PRIMARY KEY (player_id, item_id, slot)
);

CREATE TABLE IF NOT EXISTS player_buildings (
    id          INTEGER PRIMARY KEY,
    player_id   INTEGER NOT NULL,
    building_id INTEGER NOT NULL REFERENCES items(id),
    pos_x       REAL NOT NULL,
    pos_y       REAL NOT NULL,
    pos_z       REAL NOT NULL,
    rotation    REAL DEFAULT 0,
    health      INTEGER DEFAULT 100,
    storage     TEXT DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS player_stats (
    player_id   INTEGER PRIMARY KEY,
    hp          INTEGER DEFAULT 20,
    max_hp      INTEGER DEFAULT 20,
    hunger      INTEGER DEFAULT 100,
    warmth      REAL DEFAULT 15.0,
    pos_x       REAL DEFAULT 0,
    pos_y       REAL DEFAULT 0,
    pos_z       REAL DEFAULT 0,
    shelter_id  INTEGER DEFAULT 0,
    alive       INTEGER DEFAULT 1
);

-- Crop types: farming rules per seed item
CREATE TABLE IF NOT EXISTS crop_types (
    seed_item_id    INTEGER PRIMARY KEY REFERENCES items(id),
    harvest_item_id INTEGER NOT NULL REFERENCES items(id),
    harvest_qty     INTEGER DEFAULT 3,
    seed_return     INTEGER DEFAULT 1,
    plant_season    TEXT NOT NULL DEFAULT 'spring',
    harvest_season  TEXT NOT NULL DEFAULT 'autumn',
    grow_days       INTEGER DEFAULT 90,
    min_flat        REAL DEFAULT 0.9,
    near_water_range REAL DEFAULT 30.0,
    tool_req        INTEGER DEFAULT 102,
    model           TEXT,
    description     TEXT
);

-- Indices for common queries
CREATE INDEX IF NOT EXISTS idx_recipe_inputs_recipe ON recipe_inputs(recipe_id);
CREATE INDEX IF NOT EXISTS idx_loot_creature ON loot_table(creature_id);
CREATE INDEX IF NOT EXISTS idx_gather_terrain ON gather_sources(terrain);
CREATE INDEX IF NOT EXISTS idx_trap_rules_trap ON trap_rules(trap_id);
CREATE INDEX IF NOT EXISTS idx_inventory_player ON player_inventory(player_id);
CREATE INDEX IF NOT EXISTS idx_buildings_player ON player_buildings(player_id);
