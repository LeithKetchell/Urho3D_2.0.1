-- Building System Schema
-- Loaded by GameDB on startup

CREATE TABLE IF NOT EXISTS building_types (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,
    category        TEXT NOT NULL,
    tier            INTEGER DEFAULT 1,
    footprint_x     REAL DEFAULT 2.0,
    footprint_z     REAL DEFAULT 2.0,
    height          REAL DEFAULT 2.5,
    max_hp          INTEGER DEFAULT 100,
    decay_rate      REAL DEFAULT 1.0,
    warmth          REAL DEFAULT 0,
    storage_slots   INTEGER DEFAULT 0,
    sleep_capacity  INTEGER DEFAULT 0,
    respawn         INTEGER DEFAULT 0,
    snap_type       TEXT DEFAULT 'free',
    model           TEXT,
    ghost_model     TEXT,
    description     TEXT
);

CREATE TABLE IF NOT EXISTS building_recipes (
    building_id INTEGER NOT NULL REFERENCES building_types(id),
    item_id     INTEGER NOT NULL,
    quantity    INTEGER DEFAULT 1
);

CREATE TABLE IF NOT EXISTS snap_rules (
    from_type   TEXT NOT NULL,
    to_type     TEXT NOT NULL,
    offset_x    REAL DEFAULT 0,
    offset_z    REAL DEFAULT 0,
    align       TEXT DEFAULT 'end'
);

CREATE TABLE IF NOT EXISTS placed_buildings (
    id          INTEGER PRIMARY KEY,
    building_id INTEGER NOT NULL REFERENCES building_types(id),
    owner_id    INTEGER NOT NULL,
    pos_x       REAL NOT NULL,
    pos_y       REAL NOT NULL,
    pos_z       REAL NOT NULL,
    rotation    REAL DEFAULT 0,
    hp          INTEGER NOT NULL,
    built_day   INTEGER NOT NULL,
    last_repair INTEGER DEFAULT 0,
    gate_open   INTEGER DEFAULT 0,
    storage     TEXT DEFAULT '[]',
    snapped_to  INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS repair_costs (
    building_id INTEGER NOT NULL REFERENCES building_types(id),
    item_id     INTEGER NOT NULL,
    quantity    INTEGER DEFAULT 1,
    hp_restored INTEGER DEFAULT 25
);

CREATE TABLE IF NOT EXISTS weather_damage (
    weather     TEXT NOT NULL,
    tier        INTEGER NOT NULL,
    extra_decay REAL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS wall_strength (
    building_id INTEGER NOT NULL REFERENCES building_types(id),
    creature_id INTEGER NOT NULL,
    blocks      INTEGER DEFAULT 1
);
