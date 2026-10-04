-- World Database Schema (game_world.db)
-- Dynamic runtime state — THIS is the save file
-- Separate from game_rules.db (static balance data)
-- Uses WAL mode for concurrent reads and crash safety

-- Player runtime state (changes every tick)
CREATE TABLE IF NOT EXISTS player_stats (
    player_id   INTEGER PRIMARY KEY,
    hp          INTEGER DEFAULT 20,
    max_hp      INTEGER DEFAULT 20,
    hunger      REAL DEFAULT 100.0,
    thirst      REAL DEFAULT 100.0,
    stamina     REAL DEFAULT 100.0,
    warmth      REAL DEFAULT 15.0,
    pos_x       REAL DEFAULT 0,
    pos_y       REAL DEFAULT 0,
    pos_z       REAL DEFAULT 0,
    shelter_id  INTEGER DEFAULT 0,
    alive       INTEGER DEFAULT 1
);

-- Player inventory (items carried and equipped)
CREATE TABLE IF NOT EXISTS player_inventory (
    player_id   INTEGER NOT NULL,
    item_id     INTEGER NOT NULL,
    quantity    INTEGER DEFAULT 1,
    durability  INTEGER DEFAULT -1,
    slot        TEXT DEFAULT 'bag',
    PRIMARY KEY (player_id, item_id, slot)
);

-- Placed buildings (player-built structures in the world)
CREATE TABLE IF NOT EXISTS placed_buildings (
    id          INTEGER PRIMARY KEY,
    building_id INTEGER NOT NULL,
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
    snapped_to  INTEGER DEFAULT 0,
    parent_id   INTEGER DEFAULT 0       -- furniture placed inside this building (0 = standalone)
);

-- Terrain edits (heightmap modifications — append-only journal)
CREATE TABLE IF NOT EXISTS terrain_edits (
    id          INTEGER PRIMARY KEY,
    patch_x     INTEGER NOT NULL,
    patch_z     INTEGER NOT NULL,
    local_x     INTEGER NOT NULL,
    local_z     INTEGER NOT NULL,
    old_height  REAL NOT NULL,
    new_height  REAL NOT NULL,
    editor_id   INTEGER NOT NULL,
    edit_day    INTEGER NOT NULL
);

-- Fire pits (server-authoritative campfire state)
CREATE TABLE IF NOT EXISTS fire_pits (
    pit_id      INTEGER PRIMARY KEY,
    pos_x       REAL NOT NULL,
    pos_y       REAL NOT NULL,
    pos_z       REAL NOT NULL,
    fuel        REAL DEFAULT 0,
    max_fuel    REAL DEFAULT 600,
    burn_rate   REAL DEFAULT 1.0,
    wetness     REAL DEFAULT 0,
    state       INTEGER DEFAULT 0,
    region_id   INTEGER DEFAULT 0
);

-- No hardcoded fire pits — NPCs create and tend them dynamically.
-- The fire_pits table persists across restarts; pits that burn out are removed.

-- Server state (key-value store for game time, season, etc.)
CREATE TABLE IF NOT EXISTS server_state (
    key         TEXT PRIMARY KEY,
    value       TEXT
);

-- Placed crops (farming system runtime state)
CREATE TABLE IF NOT EXISTS placed_crops (
    crop_id         INTEGER PRIMARY KEY,
    owner_id        INTEGER NOT NULL,
    seed_item_id    INTEGER NOT NULL,
    pos_x           REAL NOT NULL,
    pos_y           REAL NOT NULL,
    pos_z           REAL NOT NULL,
    planted_day     REAL NOT NULL,
    growth_stage    INTEGER DEFAULT 0,
    region_id       INTEGER DEFAULT 0
);

-- Trees (server-authoritative forest state)
CREATE TABLE IF NOT EXISTS trees (
    tree_id     INTEGER PRIMARY KEY,
    species     INTEGER NOT NULL,       -- 0=oak, 1=pine, 2=eucalyptus, 3=acacia, 4=willow, 5=sheoak
    pos_x       REAL NOT NULL,
    pos_z       REAL NOT NULL,
    scale       REAL DEFAULT 1.0,
    region_id   INTEGER DEFAULT 0,
    planted_day INTEGER DEFAULT 0,      -- game day planted (0 = initial generation)
    hp          INTEGER DEFAULT 100,    -- health — 0 = harvested/dead
    growth_stage INTEGER DEFAULT 3,     -- 0=stump, 1=sapling, 2=young, 3=mature
    yields_resin INTEGER DEFAULT 0,     -- 1 = produces harvestable resin (acacia)
    last_tapped  REAL DEFAULT NULL      -- wallclock timestamp of last resin tap (24h cooldown)
);
-- Migrations handled in WorldDB::EnsureSchema (per-statement, tolerates duplicates)

-- NPC social bonds (familiarity, family, mating)
CREATE TABLE IF NOT EXISTS npc_bonds (
    npc_a       INTEGER NOT NULL,       -- lower spawnId
    npc_b       INTEGER NOT NULL,       -- higher spawnId
    bond_type   TEXT DEFAULT 'acquaintance', -- acquaintance, mate, parent, child
    familiarity REAL DEFAULT 0.0,       -- 0-100, increments from shared activity
    PRIMARY KEY (npc_a, npc_b)
);

-- Animal production tracking (Phase 16 — milk, wool, meat, hide yields)
CREATE TABLE IF NOT EXISTS animal_production (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    animal_id   INTEGER NOT NULL,       -- spawnId of the producing animal
    species_id  INTEGER NOT NULL,       -- creatureId (7=Cow, 11=Alpaca, etc.)
    product     TEXT NOT NULL,           -- 'milk', 'wool', 'meat', 'hide'
    quantity    INTEGER NOT NULL,        -- units produced
    tamer_id    INTEGER NOT NULL,       -- spawnId of the NPC who owns/tamed the animal
    campfire_id INTEGER NOT NULL,       -- settlement that benefits
    game_day    REAL NOT NULL            -- game day when production occurred
);
CREATE INDEX IF NOT EXISTS idx_production_campfire ON animal_production(campfire_id);
CREATE INDEX IF NOT EXISTS idx_production_species ON animal_production(species_id, product);

-- Irrigation channels (line segments from water to farmland)
CREATE TABLE IF NOT EXISTS irrigation_channels (
    channel_id  INTEGER PRIMARY KEY,
    owner_id    INTEGER NOT NULL,
    water_x     REAL NOT NULL,
    water_z     REAL NOT NULL,
    farm_x      REAL NOT NULL,
    farm_z      REAL NOT NULL
);

-- Crop rotation history (tracks what was last grown at each location)
CREATE TABLE IF NOT EXISTS crop_history (
    tile_x      INTEGER NOT NULL,       -- grid-snapped X (meters, rounded to 2m tiles)
    tile_z      INTEGER NOT NULL,       -- grid-snapped Z
    last_seed_id INTEGER NOT NULL,      -- seed item ID of last crop harvested here
    last_harvest_day REAL NOT NULL,     -- game day of last harvest
    consecutive INTEGER DEFAULT 1,      -- times same crop grown in a row at this tile
    PRIMARY KEY (tile_x, tile_z)
);

-- Settlement history (firsts — named achievements)
CREATE TABLE IF NOT EXISTS settlement_history (
    campfire_id INTEGER NOT NULL,
    category    TEXT NOT NULL,           -- 'first_tool', 'first_building', 'first_bronze', etc.
    npc_name    TEXT NOT NULL,           -- name of the NPC who achieved it
    npc_spawn_id INTEGER NOT NULL,
    game_day    INTEGER NOT NULL,
    PRIMARY KEY (campfire_id, category)  -- one first per category per settlement
);

-- Settlement innovations (Phase 34 — philosopher mechanic)
CREATE TABLE IF NOT EXISTS settlement_innovations (
    campfire_id     INTEGER NOT NULL,
    innovation_id   INTEGER NOT NULL,       -- matches InnovationType enum
    discoverer_name TEXT NOT NULL,
    game_day        INTEGER NOT NULL,
    PRIMARY KEY (campfire_id, innovation_id)
);

-- Alloy metadata — hidden trace element tag on Mystery Alloy items (Phase 38)
CREATE TABLE IF NOT EXISTS alloy_metadata (
    owner_id        INTEGER NOT NULL,
    trace_element   INTEGER NOT NULL,       -- deposit type (7=Mn, 9=W, 10=Ni, 11=C, 13=Cr)
    quantity        INTEGER DEFAULT 1,
    PRIMARY KEY (owner_id, trace_element)
);

-- Alloy observations — counts of trace-alloy tool uses per settlement (Phase 38)
CREATE TABLE IF NOT EXISTS alloy_observations (
    campfire_id     INTEGER NOT NULL,
    trace_element   INTEGER NOT NULL,
    use_count       INTEGER DEFAULT 0,
    discovered      INTEGER DEFAULT 0,      -- 1 = observation triggered, logged in settlement_history
    PRIMARY KEY (campfire_id, trace_element)
);

-- Alloy knowledge — settlement understands trace element effects (Phase 39)
CREATE TABLE IF NOT EXISTS alloy_knowledge (
    campfire_id     INTEGER NOT NULL,
    element_id      INTEGER NOT NULL,       -- deposit type
    discovery_count INTEGER DEFAULT 0,      -- increments per accidental discovery
    effect_known    INTEGER DEFAULT 0,      -- 1 = settlement understands this element
    PRIMARY KEY (campfire_id, element_id)
);

-- Botanical Discovery: species-property mapping (what each tree species can yield)
CREATE TABLE IF NOT EXISTS species_properties (
    species         INTEGER NOT NULL,       -- tree species ID (0-5: oak, pine, eucalyptus, acacia, willow, sheoak)
    property        TEXT NOT NULL,          -- 'resin', 'medicine', 'dye', 'poison', 'fiber'
    yield_item_id   INTEGER NOT NULL,       -- item produced when harvested
    skill_id        INTEGER NOT NULL,       -- required skill (e.g. SKILL_HERBALISM=25)
    skill_level     INTEGER DEFAULT 2,      -- minimum skill level to attempt
    cooldown        REAL DEFAULT 86400.0,   -- real-time seconds between harvests per tree
    yield_min       INTEGER DEFAULT 1,      -- min items per successful harvest
    yield_max       INTEGER DEFAULT 2,      -- max items per successful harvest
    PRIMARY KEY (species, property)
);

-- Seed data: Acacia yields resin (migrated from yields_resin column)
INSERT OR IGNORE INTO species_properties (species, property, yield_item_id, skill_id, skill_level, cooldown, yield_min, yield_max)
VALUES (3, 'resin', 870, 25, 2, 86400.0, 1, 2);
-- Willow yields growth hormone (Herbalism 3+, 12h cooldown)
INSERT OR IGNORE INTO species_properties (species, property, yield_item_id, skill_id, skill_level, cooldown, yield_min, yield_max)
VALUES (4, 'growth', 875, 25, 3, 43200.0, 1, 3);
-- Eucalyptus yields medicine (Herbalism 4+, 24h cooldown)
INSERT OR IGNORE INTO species_properties (species, property, yield_item_id, skill_id, skill_level, cooldown, yield_min, yield_max)
VALUES (2, 'medicine', 877, 25, 4, 86400.0, 1, 2);
-- She-Oak yields bark (Herbalism 2+, 24h cooldown) — waterproof vessel material
INSERT OR IGNORE INTO species_properties (species, property, yield_item_id, skill_id, skill_level, cooldown, yield_min, yield_max)
VALUES (5, 'bark', 879, 25, 2, 86400.0, 1, 2);
-- Future:
-- Oak bark for tannin/dye:   (0, 'dye', ???, 25, 3, 43200.0, 1, 1)

-- Per-tree harvest cooldowns (replaces last_tapped column)
CREATE TABLE IF NOT EXISTS tree_harvests (
    tree_id     INTEGER NOT NULL,
    property    TEXT NOT NULL,
    last_time   REAL NOT NULL,             -- wallclock timestamp of last harvest
    PRIMARY KEY (tree_id, property)
);

-- Settlement botanical knowledge: tracks which species-properties a settlement has discovered
CREATE TABLE IF NOT EXISTS settlement_botanical (
    campfire_id INTEGER NOT NULL,
    species     INTEGER NOT NULL,
    property    TEXT NOT NULL,
    discoverer  TEXT NOT NULL,              -- NPC name who made the discovery
    game_day    INTEGER NOT NULL,
    PRIMARY KEY (campfire_id, species, property)
);

-- Well state (Water Phase 4)
CREATE TABLE IF NOT EXISTS well_state (
    building_id     INTEGER PRIMARY KEY,    -- references placed_buildings.id
    water_reserve   INTEGER DEFAULT 0,      -- current available draws
    max_yield       INTEGER DEFAULT 10,     -- daily capacity (terrain-dependent)
    last_refill     REAL DEFAULT 0.0        -- wallclock timestamp of last overnight refill
);

-- Rain Collection: Water Barrel state
CREATE TABLE IF NOT EXISTS barrel_state (
    building_id     INTEGER PRIMARY KEY,    -- references placed_buildings.id
    water_reserve   REAL DEFAULT 0.0,       -- litres currently stored
    capacity        REAL DEFAULT 20.0,      -- max litres
    under_roof      INTEGER DEFAULT 0       -- 1 = blocked by roof, 0 = open sky
);

-- Building locks: ties a placed building to a lock item and owner
CREATE TABLE IF NOT EXISTS building_locks (
    building_id     INTEGER PRIMARY KEY,        -- references placed_buildings.id
    lock_item_id    INTEGER NOT NULL,           -- 932 (bronze) or 933 (iron)
    owner_id        INTEGER NOT NULL,           -- player_id who installed the lock
    key_item_id     INTEGER NOT NULL            -- 930 (bronze) or 931 (iron)
);

-- Indices for common queries
CREATE INDEX IF NOT EXISTS idx_world_inventory_player ON player_inventory(player_id);
CREATE INDEX IF NOT EXISTS idx_world_buildings_owner ON placed_buildings(owner_id);
CREATE INDEX IF NOT EXISTS idx_world_terrain_patch ON terrain_edits(patch_x, patch_z);
CREATE INDEX IF NOT EXISTS idx_world_crops_owner ON placed_crops(owner_id);
CREATE INDEX IF NOT EXISTS idx_world_trees_region ON trees(region_id);
CREATE INDEX IF NOT EXISTS idx_world_bonds_a ON npc_bonds(npc_a);
CREATE INDEX IF NOT EXISTS idx_world_bonds_b ON npc_bonds(npc_b);
CREATE INDEX IF NOT EXISTS idx_species_props ON species_properties(species);
CREATE INDEX IF NOT EXISTS idx_tree_harvests ON tree_harvests(tree_id);

-- Settlement patches — one settlement per 2x2 terrain patch block (128x128 world units).
-- 16x16 = 256 settlement slots per terrain. Exclusive claim — no overlaps.
CREATE TABLE IF NOT EXISTS settlement_patches (
    terrain_id      INTEGER NOT NULL DEFAULT 0,     -- 0 = origin terrain
    spatch_x        INTEGER NOT NULL,               -- 0-15 (settlement patch coords)
    spatch_z        INTEGER NOT NULL,               -- 0-15
    settlement_id   INTEGER NOT NULL,               -- campfire/settlement index
    claimed_at      INTEGER DEFAULT (strftime('%s','now')),
    PRIMARY KEY (terrain_id, spatch_x, spatch_z)
);
CREATE INDEX IF NOT EXISTS idx_spatch_settlement ON settlement_patches(settlement_id);

-- Water bodies — disconnected water regions identified by flood-fill at terrain gen time.
-- Computed once, cached permanently. Feeds fish distribution and lazy zone system.
CREATE TABLE IF NOT EXISTS water_bodies (
    body_id         INTEGER PRIMARY KEY,
    terrain_id      INTEGER NOT NULL DEFAULT 0,
    area_pixels     INTEGER NOT NULL,           -- number of heightmap pixels in this body
    area_world      REAL NOT NULL,              -- area in world units squared
    center_x        REAL NOT NULL,              -- centroid world X
    center_z        REAL NOT NULL,              -- centroid world Z
    min_depth       REAL NOT NULL,              -- shallowest point (world Y relative to water level)
    max_depth       REAL NOT NULL               -- deepest point
);

-- Fish spawn points — viable locations within each water body for distributing fish.
-- Spaced ~10m apart, depth > 1m, away from edges.
CREATE TABLE IF NOT EXISTS fish_spawn_points (
    id              INTEGER PRIMARY KEY,
    body_id         INTEGER NOT NULL,
    pos_x           REAL NOT NULL,
    pos_z           REAL NOT NULL,
    depth           REAL NOT NULL,              -- depth below water level (positive = deeper)
    FOREIGN KEY (body_id) REFERENCES water_bodies(body_id)
);
CREATE INDEX IF NOT EXISTS idx_fish_spawn_body ON fish_spawn_points(body_id);

-- Death log — every creature death with who, where, why
CREATE TABLE IF NOT EXISTS death_log (
    id          INTEGER PRIMARY KEY,
    npc_name    TEXT DEFAULT '',          -- syllable name (humans) or species (animals)
    species     TEXT NOT NULL,            -- 'CaveMan', 'Wolf', 'Deer', etc.
    spawn_id    INTEGER NOT NULL,
    creature_id INTEGER NOT NULL,
    pos_x       REAL NOT NULL,
    pos_z       REAL NOT NULL,
    cause       INTEGER NOT NULL,         -- 0=combat 1=drown 2=starve 3=age 4=scavenge 5=fall 6=fire 7=dehydrate 8=freeze
    killer_name TEXT DEFAULT '',          -- player username or empty for environmental
    game_day    INTEGER DEFAULT 0,
    timestamp   REAL DEFAULT 0            -- wallclock
);
CREATE INDEX IF NOT EXISTS idx_death_log_species ON death_log(species);
CREATE INDEX IF NOT EXISTS idx_death_log_cause ON death_log(cause);

-- Map backups — terrain maps stored as BLOBs for reset/restore
-- Server writes these when maps are modified; Reset World restores before deleting DB
CREATE TABLE IF NOT EXISTS map_backups (
    filename    TEXT PRIMARY KEY,       -- e.g. 'HeightMap.png', 'WaterHeightMap.png'
    data        BLOB NOT NULL,
    updated_at  REAL DEFAULT (strftime('%s','now'))
);

-- Tapestry inscriptions — dynamic descriptions from settlement_history
CREATE TABLE IF NOT EXISTS tapestry_inscriptions (
    owner_id        INTEGER NOT NULL,       -- player_id who owns the tapestry
    inscription     TEXT NOT NULL,           -- first settlement event text
    PRIMARY KEY (owner_id)
);
