-- Technique Discovery Chain — Phase 1: Tiers, NPC ratings, discovery rules.
-- Extends skills_schema.sql with epoch-gated progression.

-- Epoch definitions (settlement tech level)
CREATE TABLE IF NOT EXISTS epochs (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    tier        INTEGER NOT NULL   -- 0=Stone, 1=Bronze, 2=Iron, 3=Steel, 4=Renaissance, 5=Modern
);

INSERT OR IGNORE INTO epochs VALUES
(1, 'Stone Age',     0),
(2, 'Bronze Age',    1),
(3, 'Iron Age',      2),
(4, 'Steel Age',     3),
(5, 'Renaissance',   4),
(6, 'Modern Age',    5);

-- Technique tier assignments: which skills are available at which epoch.
-- level_cap overrides skills.max_level when the epoch is lower than required.
CREATE TABLE IF NOT EXISTS technique_tiers (
    skill_id    INTEGER NOT NULL REFERENCES skills(id),
    epoch_tier  INTEGER NOT NULL DEFAULT 0,  -- minimum epoch tier required
    level_cap   INTEGER NOT NULL DEFAULT 10, -- max reachable level at this tier
    PRIMARY KEY (skill_id)
);

-- Stone Age (tier 0): primitive techniques, capped at 5
INSERT OR IGNORE INTO technique_tiers VALUES
(23, 0, 5),   -- Foraging: primitive, caps at 5
(20, 0, 5),   -- Tracking: primitive
(28, 0, 5),   -- FireMaking: primitive
(10, 0, 5),   -- Knapping: primitive stone tools
(21, 0, 4),   -- Trapping: primitive
(1,  0, 5),   -- Melee: always available
(3,  0, 5),   -- Defense: always available
(27, 0, 5),   -- Swimming: always available
(30, 0, 3),   -- Animal Lore: limited observation
(31, 0, 3);   -- Terrain Lore: limited

-- Intermediate (tier 1 — unlocked by Bronze Age)
INSERT OR IGNORE INTO technique_tiers VALUES
(22, 1, 8),   -- Fishing: intermediate, caps at 8
(26, 1, 7),   -- Farming: intermediate
(14, 1, 7),   -- Pottery: intermediate
(13, 1, 7),   -- Weaving: intermediate
(11, 1, 7),   -- Woodwork: intermediate
(12, 1, 7),   -- Leatherwork: intermediate
(24, 1, 7),   -- Cooking: intermediate
(25, 1, 6),   -- Herbalism: intermediate
(2,  1, 7),   -- Ranged: intermediate (bow)
(4,  1, 6),   -- Stealth: intermediate
(33, 1, 5);   -- Trade: intermediate

-- Advanced (tier 2 — unlocked by Iron Age)
INSERT OR IGNORE INTO technique_tiers VALUES
(15, 2, 10),  -- Smelting: advanced, full cap
(16, 2, 10),  -- Smithing: advanced, full cap
(32, 2, 8);   -- Weather Lore: advanced

-- Discovery rules: practicing skill A at high rating can unlock skill B.
-- DC = difficulty class for INT check. prereq_rating = minimum rating in skill A.
CREATE TABLE IF NOT EXISTS technique_discovery (
    source_skill_id  INTEGER NOT NULL REFERENCES skills(id),
    target_skill_id  INTEGER NOT NULL REFERENCES skills(id),
    prereq_rating    INTEGER NOT NULL DEFAULT 3,  -- minimum rating in source
    discovery_dc     INTEGER NOT NULL DEFAULT 12, -- INT check DC
    PRIMARY KEY (source_skill_id, target_skill_id)
);

-- Discovery chains: one technique leads to another
INSERT OR IGNORE INTO technique_discovery VALUES
(23, 25, 3, 12),  -- Foraging 3 → can discover Herbalism (DC 12)
(23, 26, 4, 14),  -- Foraging 4 → can discover Farming (DC 14)
(20, 21, 3, 11),  -- Tracking 3 → can discover Trapping (DC 11)
(21, 22, 3, 13),  -- Trapping 3 → can discover Fishing (DC 13)
(28, 24, 3, 10),  -- FireMaking 3 → can discover Cooking (DC 10)
(10, 11, 3, 12),  -- Knapping 3 → can discover Woodwork (DC 12)
(10, 14, 4, 13),  -- Knapping 4 → can discover Pottery (DC 13)
(12, 13, 3, 12),  -- Leatherwork 3 → can discover Weaving (DC 12)
(14, 15, 5, 16),  -- Pottery 5 → can discover Smelting (DC 16, epoch-gated)
(15, 16, 4, 14),  -- Smelting 4 → can discover Smithing (DC 14)
(30, 20, 2, 10),  -- Animal Lore 2 → can discover Tracking (DC 10)
(1,  4,  4, 13),  -- Melee 4 → can discover Stealth (DC 13)
(11, 2,  4, 14);  -- Woodwork 4 → can discover Ranged (DC 14, bows)

-- NPC skill state (mutable, per-NPC by spawn ID)
CREATE TABLE IF NOT EXISTS npc_skills (
    npc_spawn_id  INTEGER NOT NULL,
    skill_id      INTEGER NOT NULL REFERENCES skills(id),
    rating        INTEGER DEFAULT 0,    -- current level (0 = unknown)
    xp            INTEGER DEFAULT 0,    -- progress toward next level
    discovered    INTEGER DEFAULT 0,    -- 1 = NPC knows this technique
    PRIMARY KEY (npc_spawn_id, skill_id)
);
