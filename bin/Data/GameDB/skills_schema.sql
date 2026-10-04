-- Skills system schema — Phase 1: Hidden XP tracking
-- All data-driven, no client visibility

-- Skill definitions (static game rules)
CREATE TABLE IF NOT EXISTS skills (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    category    TEXT NOT NULL,
    max_level   INTEGER DEFAULT 10
);

-- Level thresholds (static, exponential curve)
CREATE TABLE IF NOT EXISTS skill_levels (
    level       INTEGER PRIMARY KEY,
    xp_required INTEGER NOT NULL
);

-- XP trigger rules: action → skill + amount (static)
CREATE TABLE IF NOT EXISTS xp_triggers (
    action      TEXT NOT NULL,
    skill_id    INTEGER NOT NULL REFERENCES skills(id),
    xp_amount   INTEGER DEFAULT 1,
    PRIMARY KEY (action, skill_id)
);

-- Player runtime skill state (mutable, per-player)
CREATE TABLE IF NOT EXISTS player_skills (
    player_id   INTEGER NOT NULL,
    skill_id    INTEGER NOT NULL REFERENCES skills(id),
    xp          INTEGER DEFAULT 0,
    level       INTEGER DEFAULT 0,
    PRIMARY KEY (player_id, skill_id)
);

-- Skill definitions
INSERT OR IGNORE INTO skills VALUES
(1,  'Melee',       'combat',   10),
(2,  'Ranged',      'combat',   10),
(3,  'Defense',     'combat',   10),
(4,  'Stealth',     'combat',   10),
(10, 'Knapping',    'crafting', 10),
(11, 'Woodwork',    'crafting', 10),
(12, 'Leatherwork', 'crafting', 10),
(13, 'Weaving',     'crafting', 10),
(14, 'Pottery',     'crafting', 10),
(15, 'Smelting',    'crafting', 10),
(16, 'Smithing',    'crafting', 10),
(20, 'Tracking',    'survival', 10),
(21, 'Trapping',    'survival', 10),
(22, 'Fishing',     'survival', 10),
(23, 'Foraging',    'survival', 10),
(24, 'Cooking',     'survival', 10),
(25, 'Herbalism',   'survival', 10),
(26, 'Farming',     'survival', 10),
(27, 'Swimming',    'survival', 10),
(28, 'FireMaking',  'survival', 10),
(30, 'Animal Lore', 'knowledge', 10),
(31, 'Terrain Lore','knowledge', 10),
(32, 'Weather Lore','knowledge', 10),
(33, 'Trade',       'knowledge', 10);

-- Level XP thresholds (exponential)
INSERT OR IGNORE INTO skill_levels VALUES
(0, 0), (1, 10), (2, 30), (3, 60), (4, 100), (5, 200),
(6, 350), (7, 550), (8, 800), (9, 1100), (10, 1500);

-- XP triggers — Phase 1 subset (combat + gathering + crafting)
INSERT OR IGNORE INTO xp_triggers VALUES
('melee_hit',    1,  3),
('melee_miss',   1,  1),
('melee_kill',   1,  5),
('ranged_hit',   2,  3),
('ranged_miss',  2,  1),
('ranged_kill',  2,  5),
('take_damage',  3,  2),
('dodge',        3,  3),
('ambush_attempt', 4, 2),
('ambush_success', 4, 5),
('prospect',     10, 3),
('craft_stone',  10, 2),
('craft_wood',   11, 2),
('craft_leather',12, 2),
('craft_fiber',  13, 2),
('craft_clay',   14, 2),
('craft_smelt',  15, 3),
('craft_metal',  16, 3),
('track_find',   20, 3),
('trap_place',   21, 1),
('trap_catch',   21, 5),
('fish_catch',   22, 2),
('forage',       23, 1),
('cook',         24, 2),
('use_herb',     25, 2),
('gather_herb',  25, 1),
('farm_plant',   26, 1),
('farm_harvest', 26, 3),
('swim',         27, 1),
('fire_ignite',  28, 5),
('fire_tend',    28, 1),
('fire_torch',   28, 2),
('observe_animal',  30, 1),
('bury_dead',       30, 3),
('explore_terrain', 31, 1),
('survive_storm',   32, 2),
('complete_trade',  33, 3);
