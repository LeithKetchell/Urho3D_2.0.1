-- Population Dynamics Schema
-- Phase 1: tracking only — regions, per-species counts, daily deltas.
-- Seeded for the default Home Valley terrain.

CREATE TABLE IF NOT EXISTS regions (
    id          INTEGER PRIMARY KEY,
    name        TEXT,
    center_x    REAL NOT NULL,
    center_z    REAL NOT NULL,
    radius      REAL DEFAULT 256.0,
    biome       TEXT DEFAULT 'temperate',
    forest_pct  REAL DEFAULT 0.5,
    water_pct   REAL DEFAULT 0.15,
    grass_pct   REAL DEFAULT 0.4,
    owner_id    INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS population (
    region_id    INTEGER NOT NULL REFERENCES regions(id),
    creature_id  INTEGER NOT NULL REFERENCES creatures(id),
    count        INTEGER DEFAULT 0,
    max_count    INTEGER DEFAULT 0,
    born_today   INTEGER DEFAULT 0,
    died_today   INTEGER DEFAULT 0,
    killed_today INTEGER DEFAULT 0,
    migrated_in  INTEGER DEFAULT 0,
    migrated_out INTEGER DEFAULT 0,
    PRIMARY KEY (region_id, creature_id)
);

-- Home Valley — centered on terrain origin
INSERT OR IGNORE INTO regions (id, name, center_x, center_z, radius, biome, forest_pct, water_pct, grass_pct)
VALUES (1, 'Home Valley', 0.0, 0.0, 256.0, 'temperate', 0.5, 0.15, 0.4);

-- Seed populations (creature IDs match creatures table)
INSERT OR IGNORE INTO population (region_id, creature_id, count, max_count) VALUES
(1,  1,  8, 20),  -- Rabbit: prolific
(1,  2,  8, 15),  -- Deer: common
(1,  3,  3,  6),  -- Fox: sparse predator
(1,  4,  3,  6),  -- Stag: uncommon
(1,  5,  2,  4),  -- Wolf: rare predator
(1,  6,  2,  3),  -- Bull: uncommon
(1,  7,  3,  6),  -- Cow: common
(1,  8, 15, 30),  -- Fish: abundant
(1,  9,  2,  4),  -- Donkey: uncommon
(1, 10,  2,  5),  -- Horse: uncommon
(1, 11,  3,  6),  -- Alpaca: moderate
(1, 12,  2,  3),  -- Husky: rare
(1, 13,  2,  3),  -- ShibaInu: rare
(1, 20,  3,  8),  -- CaveMan: small tribe
(1, 21,  3,  8);  -- CaveWoman: small tribe
