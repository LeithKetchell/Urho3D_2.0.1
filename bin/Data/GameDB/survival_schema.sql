-- Survival Pressure Schema
-- Extends base game database with survival mechanics tables
-- Apply after schema.sql

-- Extend player_stats with thirst and stamina
-- (hunger already exists in base schema)

-- Note: SQLite doesn't support ALTER TABLE ADD COLUMN IF NOT EXISTS
-- So we use a safe pattern: try to add, ignore error if exists

-- Hunger configuration
CREATE TABLE IF NOT EXISTS hunger_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    max_hunger          INTEGER DEFAULT 100,
    drain_per_day       REAL DEFAULT 25.0,
    sprint_mult         REAL DEFAULT 1.5,
    work_mult           REAL DEFAULT 1.3,
    swim_mult           REAL DEFAULT 2.0,
    starve_hp_day       REAL DEFAULT 5.0,
    low_threshold       INTEGER DEFAULT 25,
    critical_threshold  INTEGER DEFAULT 10,
    eat_restore         INTEGER DEFAULT 35
);

-- Thirst configuration
CREATE TABLE IF NOT EXISTS thirst_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    max_thirst          INTEGER DEFAULT 100,
    drain_per_day       REAL DEFAULT 40.0,
    heat_mult           REAL DEFAULT 1.5,
    sprint_mult         REAL DEFAULT 1.5,
    work_mult           REAL DEFAULT 1.5,
    dehydrate_hp_day    REAL DEFAULT 8.0,
    low_threshold       INTEGER DEFAULT 30,
    critical_threshold  INTEGER DEFAULT 15,
    eat_restore         INTEGER DEFAULT 25
);

-- Water sources
CREATE TABLE IF NOT EXISTS water_sources (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL,
    type            TEXT NOT NULL,
    thirst_restore  INTEGER DEFAULT 30,
    interact_time   REAL DEFAULT 2.0,
    disease_chance  REAL DEFAULT 0,
    requires        INTEGER DEFAULT 0,
    notes           TEXT
);

-- Warmth configuration
CREATE TABLE IF NOT EXISTS warmth_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    comfort_min         REAL DEFAULT 5.0,
    cold_threshold      REAL DEFAULT 0.0,
    severe_cold         REAL DEFAULT -10.0,
    heat_threshold      REAL DEFAULT 40.0,
    cold_hp_per_day     REAL DEFAULT 3.0,
    severe_hp_per_day   REAL DEFAULT 8.0,
    heat_hp_per_day     REAL DEFAULT 4.0,
    fire_warmth         REAL DEFAULT 15.0,
    fire_range          REAL DEFAULT 5.0,
    activity_warmth     REAL DEFAULT 3.0,
    sprint_heat         REAL DEFAULT 5.0,
    night_multiplier    REAL DEFAULT 8.0,
    low_threshold       INTEGER DEFAULT 30,
    sit_restore         REAL DEFAULT 30.0
);

-- Stamina configuration
CREATE TABLE IF NOT EXISTS stamina_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    max_stamina         INTEGER DEFAULT 100,
    regen_per_second    REAL DEFAULT 2.0,
    regen_sleeping      REAL DEFAULT 10.0,
    sprint_cost_sec     REAL DEFAULT 5.0,
    melee_cost          REAL DEFAULT 8.0,
    ranged_cost         REAL DEFAULT 5.0,
    chop_cost           REAL DEFAULT 10.0,
    mine_cost           REAL DEFAULT 12.0,
    build_cost          REAL DEFAULT 6.0,
    swim_cost_sec       REAL DEFAULT 4.0,
    low_threshold       INTEGER DEFAULT 20,
    empty_penalty       TEXT DEFAULT 'no_sprint,slow_attack,slow_craft',
    sit_restore         REAL DEFAULT 20.0,
    sleep_restore       REAL DEFAULT 60.0
);

-- Sleep configuration
CREATE TABLE IF NOT EXISTS sleep_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    min_duration        REAL DEFAULT 10.0,
    stamina_mult        REAL DEFAULT 5.0,
    hp_per_hour         REAL DEFAULT 1.0,
    shelter_hp_bonus    REAL DEFAULT 1.0,
    hunger_req          INTEGER DEFAULT 15,
    thirst_req          INTEGER DEFAULT 15,
    interrupt_damage    INTEGER DEFAULT 5
);

-- Death and respawn configuration
CREATE TABLE IF NOT EXISTS death_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    respawn_delay       REAL DEFAULT 5.0,
    hp_on_respawn       INTEGER DEFAULT 10,
    hunger_on_respawn   INTEGER DEFAULT 50,
    thirst_on_respawn   INTEGER DEFAULT 50,
    stamina_on_respawn  INTEGER DEFAULT 50,
    drop_inventory      INTEGER DEFAULT 1,
    corpse_duration     REAL DEFAULT 600.0,
    skill_loss          REAL DEFAULT 0.0
);

-- Fire configuration
CREATE TABLE IF NOT EXISTS fire_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    light_radius        REAL DEFAULT 8.0,
    warmth_radius       REAL DEFAULT 5.0,
    warmth_value        REAL DEFAULT 15.0,
    fuel_per_hour       REAL DEFAULT 1.0,
    cook_requires_fuel  INTEGER DEFAULT 1,
    light_flicker       INTEGER DEFAULT 1
);

-- Fuel types
CREATE TABLE IF NOT EXISTS fuel_types (
    item_id     INTEGER NOT NULL REFERENCES items(id),
    fuel_value  REAL DEFAULT 1.0,
    heat_bonus  REAL DEFAULT 0
);
