-- Economic Doctrine Schema
-- Core law: Extraction > Regeneration (humans are net negative)
-- This drives expansion, trade, and conflict.

-- ============================================================
-- RESOURCE TYPES — what can be extracted from the land
-- ============================================================

CREATE TABLE IF NOT EXISTS resource_types (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,
    category        TEXT NOT NULL,          -- 'flora', 'mineral', 'soil', 'water'
    item_id         INTEGER DEFAULT 0,      -- which item this yields (FK to items)
    regen_per_day   REAL NOT NULL,          -- units regenerated per game-day
    extract_per_use REAL DEFAULT 1.0,       -- units removed per extraction
    extract_time    REAL DEFAULT 3.0,       -- seconds to extract once
    tool_req        INTEGER DEFAULT 0,      -- required tool item_id (0 = bare hands)
    scarcity_50     REAL DEFAULT 0.3,       -- below this fraction, yield halves
    scarcity_0      REAL DEFAULT 0.05,      -- below this fraction, yield drops to zero
    seasonal        TEXT DEFAULT 'any',     -- 'any', 'spring,summer', etc.
    tier            INTEGER DEFAULT 0
);

-- ============================================================
-- REGION RESOURCES — per-region resource pools (world state)
-- Goes in game_world.db, but schema defined here for reference
-- ============================================================

CREATE TABLE IF NOT EXISTS region_resources (
    region_id       INTEGER NOT NULL,
    resource_id     INTEGER NOT NULL REFERENCES resource_types(id),
    current_amount  REAL NOT NULL,          -- units currently available
    max_amount      REAL NOT NULL,          -- pristine maximum (carrying capacity)
    total_extracted REAL DEFAULT 0,         -- lifetime extraction (analytics)
    last_regen_day  INTEGER DEFAULT 0,      -- last game-day regeneration was applied
    PRIMARY KEY (region_id, resource_id)
);

-- ============================================================
-- EXTRACTION LOG — who took what, when, where
-- Append-only. Server uses this for scarcity analytics.
-- ============================================================

CREATE TABLE IF NOT EXISTS extraction_log (
    id              INTEGER PRIMARY KEY,
    player_id       INTEGER NOT NULL,
    region_id       INTEGER NOT NULL,
    resource_id     INTEGER NOT NULL,
    amount          REAL NOT NULL,
    game_day        INTEGER NOT NULL,
    scarcity_mod    REAL DEFAULT 1.0       -- what modifier was applied at extraction time
);

-- ============================================================
-- SOIL QUALITY — farming depletes soil, requires fallowing
-- ============================================================

CREATE TABLE IF NOT EXISTS soil_quality (
    region_id       INTEGER NOT NULL,
    plot_x          REAL NOT NULL,
    plot_z          REAL NOT NULL,
    fertility       REAL DEFAULT 1.0,      -- 1.0 = pristine, 0.0 = barren
    regen_rate      REAL DEFAULT 0.02,     -- fertility recovered per day when fallow
    deplete_rate    REAL DEFAULT 0.08,     -- fertility lost per harvest (4x regen)
    last_harvest    INTEGER DEFAULT 0,
    fallow          INTEGER DEFAULT 1,     -- 1 = not planted, regrowing
    PRIMARY KEY (region_id, plot_x, plot_z)
);

-- ============================================================
-- BREEDING RULES — how fast animals reproduce
-- Key constraint: breed_rate < human kill rate at steady state
-- ============================================================

CREATE TABLE IF NOT EXISTS breeding_rules (
    creature_id     INTEGER PRIMARY KEY REFERENCES creatures(id),
    breed_interval  INTEGER NOT NULL,      -- game-days between births (day-tick path)
    litter_size     INTEGER DEFAULT 1,     -- offspring per birth (day-tick path)
    maturity_days   INTEGER DEFAULT 3,     -- days before offspring count as adults
    min_pop_breed   INTEGER DEFAULT 2,     -- need at least 2 to breed
    max_pop_ratio   REAL DEFAULT 0.8,      -- stop breeding above 80% carrying capacity
    starvation_threshold REAL DEFAULT 0.2, -- below 20% food resources, no breeding
    birth_rate      REAL DEFAULT 1.3       -- Death System Phase 1: per-kill replacement multiplier.
                                           -- Accumulated on every harvested kill; spawns when >=1.0.
                                           -- 1.6 = fast breeder, 1.1 = barely replaces. Independent
                                           -- of breed_interval (kill-driven, not day-tick).
);

-- ============================================================
-- TRADE VALUES — relative scarcity-based item values
-- Base values in "labor-hours" — how long it takes to obtain
-- Scarcity multiplier applied at runtime
-- ============================================================

CREATE TABLE IF NOT EXISTS trade_values (
    item_id         INTEGER PRIMARY KEY REFERENCES items(id),
    base_value      REAL NOT NULL,         -- labor-hours to obtain at full abundance
    scarcity_mult   REAL DEFAULT 1.0,      -- current multiplier (updated by server)
    demand_mult     REAL DEFAULT 1.0,      -- how much people want it (seasonal)
    last_update     INTEGER DEFAULT 0      -- game-day of last recalculation
);

-- ============================================================
-- ECONOMIC CONSTANTS — tuning knobs
-- ============================================================

CREATE TABLE IF NOT EXISTS economic_constants (
    key             TEXT PRIMARY KEY,
    value           REAL NOT NULL,
    description     TEXT
);

-- Indices
CREATE INDEX IF NOT EXISTS idx_region_resources_region ON region_resources(region_id);
CREATE INDEX IF NOT EXISTS idx_extraction_log_region ON extraction_log(region_id, game_day);
CREATE INDEX IF NOT EXISTS idx_extraction_log_player ON extraction_log(player_id, game_day);
CREATE INDEX IF NOT EXISTS idx_soil_region ON soil_quality(region_id);
CREATE INDEX IF NOT EXISTS idx_trade_values_scarcity ON trade_values(scarcity_mult);

-- ============================================================
-- SEED DATA: Resource Types
-- ============================================================

-- Flora (renewable but slow)
INSERT OR IGNORE INTO resource_types (id, name, category, item_id, regen_per_day, extract_per_use, extract_time, tool_req, scarcity_50, scarcity_0, tier) VALUES
(1,  'Trees',          'flora',   11, 0.5,  1.0, 5.0, 100, 0.3, 0.05, 0),  -- 0.5 logs/day regen, 1 log per chop
(2,  'Berry Bushes',   'flora',    6, 1.0,  3.0, 2.0,   0, 0.4, 0.1,  0),  -- 1 berry/day regen, 3 per pick
(3,  'Tall Grass',     'flora',    3, 2.0,  2.0, 1.5,   0, 0.3, 0.05, 0),  -- fiber regrows faster
(4,  'Reeds',          'flora',    3, 1.5,  2.0, 2.0,   0, 0.3, 0.05, 0),  -- waterside fiber
(5,  'Flint Nodules',  'mineral',  5, 0.1,  1.0, 3.0,   0, 0.2, 0.02, 0),  -- barely regenerates (erosion)
(6,  'Loose Stone',    'mineral',  1, 0.3,  2.0, 2.0,   0, 0.3, 0.05, 0),  -- surface stone
(7,  'Clay Deposits',  'mineral',  4, 0.2,  3.0, 4.0, 102, 0.25,0.05, 0),  -- riverbank clay
(8,  'Shallow Fish',   'flora',   10, 1.0,  1.0, 3.0,   0, 0.4, 0.1,  0),  -- hand-catchable fish
(9,  'Deep Fish',      'flora',   10, 1.5,  2.0, 4.0, 105, 0.3, 0.05, 1);  -- rod fishing, better yield

-- Minerals (non-renewable or very slow)
INSERT OR IGNORE INTO resource_types (id, name, category, item_id, regen_per_day, extract_per_use, extract_time, tool_req, scarcity_50, scarcity_0, tier) VALUES
(10, 'Copper Vein',    'mineral', 800, 0.05, 1.0, 6.0, 106, 0.2, 0.02, 3),  -- almost non-renewable
(11, 'Tin Vein',       'mineral', 801, 0.03, 1.0, 6.0, 106, 0.2, 0.02, 3),  -- rarer than copper
(12, 'Iron Deposit',   'mineral', 802, 0.02, 1.0, 8.0, 106, 0.15,0.01, 5),  -- very slow regen
(13, 'Gold Flakes',    'mineral', 803, 0.01, 1.0, 10.0,106, 0.1, 0.01, 3);  -- almost zero regen

-- Soil (abstract — represents arable land quality)
INSERT OR IGNORE INTO resource_types (id, name, category, item_id, regen_per_day, extract_per_use, extract_time, scarcity_50, scarcity_0, tier) VALUES
(20, 'Topsoil',        'soil',     0, 0.02, 0.08, 0.0, 0.3, 0.05, 0);  -- 4x faster depletion than regen

-- ============================================================
-- SEED DATA: Breeding Rules
-- Key: breed_interval is tuned so that at max hunting pressure
-- (1 kill/day), population DECLINES. One human depletes one region.
-- ============================================================

INSERT OR IGNORE INTO breeding_rules (creature_id, breed_interval, litter_size, maturity_days, min_pop_breed, max_pop_ratio, birth_rate) VALUES
( 1, 4,  2, 2, 2, 0.8, 1.6),  -- Rabbit: fast breeder, 2 kits every 4 days. Highest birth_rate.
( 2, 8,  1, 5, 2, 0.8, 1.3),  -- Deer: 1 fawn per 8 days. Moderate replacement.
( 3, 10, 2, 4, 2, 0.7, 1.2),  -- Fox: predator, slower replacement
( 4, 10, 1, 6, 2, 0.8, 1.2),  -- Stag: same as deer but rarer
( 5, 12, 2, 5, 2, 0.6, 1.1),  -- Wolf: apex, barely replaces
( 6, 14, 1, 7, 2, 0.8, 1.2),  -- Bull: slow, one calf
( 7, 10, 1, 6, 2, 0.8, 1.4),  -- Cow: livestock, higher fertility
( 8, 3,  3, 1, 2, 0.9, 1.8),  -- Fish: fast spawner — highest replacement
( 9, 12, 1, 6, 2, 0.8, 1.2),  -- Donkey
(10, 12, 1, 7, 2, 0.7, 1.2),  -- Horse
(11, 8,  1, 5, 2, 0.8, 1.3),  -- Alpaca
(12, 14, 2, 6, 2, 0.6, 1.1),  -- Husky: predator companion
(13, 14, 2, 6, 2, 0.6, 1.1),  -- ShibaInu: predator companion
(20, 30, 1, 14, 2, 0.5, 1.0),  -- CaveMan: slow breeder, 1 child per 30 days, 14 day maturity, low pop cap
(21, 30, 1, 14, 2, 0.5, 1.0);  -- CaveWoman: matches CaveMan

-- ============================================================
-- SEED DATA: Region Resources (Home Valley starting amounts)
-- ============================================================

INSERT OR IGNORE INTO region_resources (region_id, resource_id, current_amount, max_amount) VALUES
(1,  1,  100.0, 100.0),  -- Trees: 100 logs worth
(1,  2,   40.0,  40.0),  -- Berry bushes
(1,  3,   60.0,  60.0),  -- Tall grass
(1,  4,   30.0,  30.0),  -- Reeds
(1,  5,   15.0,  15.0),  -- Flint
(1,  6,   40.0,  40.0),  -- Loose stone
(1,  7,   25.0,  25.0),  -- Clay
(1,  8,   20.0,  20.0),  -- Shallow fish (separate from population fish)
(1,  9,   30.0,  30.0),  -- Deep fish
(1, 10,   12.0,  12.0),  -- Copper
(1, 11,    8.0,   8.0),  -- Tin
(1, 12,    5.0,   5.0),  -- Iron
(1, 13,    3.0,   3.0),  -- Gold
(1, 20,   50.0,  50.0);  -- Topsoil fertility units

-- ============================================================
-- SEED DATA: Trade Values (in labor-hours)
-- ============================================================

INSERT OR IGNORE INTO trade_values (item_id, base_value) VALUES
-- Raw materials (cheap, abundant early)
(1,  0.1),   -- Rough Stone
(2,  0.1),   -- Stick
(3,  0.05),  -- Plant Fiber
(4,  0.3),   -- Clay
(5,  0.2),   -- Flint
(6,  0.1),   -- Berries
(7,  0.5),   -- Raw Meat
(8,  0.7),   -- Cooked Meat
(11, 0.5),   -- Log
(12, 0.4),   -- Plank
-- Animal products (moderate — requires killing)
(20, 0.3),   -- Sinew (the gateway material)
(21, 0.8),   -- Hide
(22, 1.2),   -- Leather (processed)
(23, 0.2),   -- Bone
(24, 0.6),   -- Fur
(25, 0.3),   -- Gut
(26, 0.4),   -- Fat
(27, 0.5),   -- Antler
-- Processed materials
(40, 0.3),   -- Sharp Stone
(41, 0.2),   -- Cordage
(42, 0.5),   -- Rope
(43, 0.8),   -- Charcoal
(44, 0.6),   -- Fired Clay
-- Tools (expensive — multi-step crafting)
(100, 2.0),  -- Hand Axe
(101, 1.5),  -- Scraper
(102, 1.5),  -- Digging Stick
(103, 1.0),  -- Bone Needle
(104, 1.0),  -- Fire Kit
(105, 2.0),  -- Fishing Rod
(106, 2.5),  -- Stone Pick
-- Weapons
(200, 2.5),  -- Spear
(201, 3.0),  -- Bow
(206, 2.0),  -- Knife
-- Metals (very expensive — deep tech tree + scarce)
(800, 3.0),  -- Copper Ore
(801, 4.0),  -- Tin Ore
(802, 6.0),  -- Iron Ore
(803, 8.0),  -- Gold Nugget
(804, 5.0),  -- Copper Ingot
(805, 6.0),  -- Tin Ingot
(806, 10.0), -- Bronze Ingot
(807, 15.0); -- Iron Ingot

-- ============================================================
-- SEED DATA: Economic Constants
-- ============================================================

INSERT OR IGNORE INTO economic_constants (key, value, description) VALUES
('extraction_to_regen_ratio',   3.0,  'A human extracts ~3x faster than nature regenerates. The entropy law.'),
('scarcity_value_cap',          5.0,  'Max scarcity multiplier for trade values.'),
('soil_fallow_bonus',           1.5,  'Fallow soil regenerates 1.5x normal rate.'),
('migration_threshold',         0.3,  'Animals migrate out below 30% carrying capacity.'),
('migration_attraction',        0.7,  'Animals migrate in above 70% carrying capacity in source region.'),
('forest_regen_base',           0.5,  'Trees per day per region at full soil quality.'),
('overhunt_penalty',            0.5,  'Breeding rate halved when population below 40%.'),
('trade_value_update_interval', 7.0,  'Recalculate trade values every 7 game-days.'),
('depletion_warning',           0.25, 'UI warning when resource below 25%.'),
('exhaustion_expansion_trigger',0.1,  'Below 10% triggers expansion pressure event.');
