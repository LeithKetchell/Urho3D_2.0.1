-- Profession & Schedule system (Phase: NPC Professions)
-- Loaded into game_rules.db — static balance data.
-- Uses CREATE TABLE IF NOT EXISTS / INSERT OR IGNORE for idempotent re-application.

PRAGMA journal_mode = WAL;

-- ── Professions ──
-- Each profession has a primary skill that governs task quality and a
-- minimum epoch tier required to unlock it (0 = Stone Age, always available).
CREATE TABLE IF NOT EXISTS professions (
    id              INTEGER PRIMARY KEY,
    name            TEXT    UNIQUE NOT NULL,
    primary_skill   INTEGER NOT NULL REFERENCES skills(id),
    tier_unlock     INTEGER NOT NULL DEFAULT 0
);

-- ── Profession Schedule ──
-- 24-hour daily routine per profession.  task_type maps to server STASK enums;
-- location is a hint for pathfinding (camp, wilderness, station, water, forest).
CREATE TABLE IF NOT EXISTS profession_schedule (
    profession_id   INTEGER NOT NULL REFERENCES professions(id),
    hour            INTEGER NOT NULL CHECK(hour >= 0 AND hour <= 23),
    task_type       TEXT    NOT NULL,
    location        TEXT    NOT NULL DEFAULT 'camp',
    PRIMARY KEY (profession_id, hour)
);

-- ══════════════════════════════════════════════════════════════════════
-- Seed: Professions
-- ══════════════════════════════════════════════════════════════════════

INSERT OR IGNORE INTO professions VALUES
(1, 'Gatherer',  23, 0),   -- Foraging
(2, 'Hunter',    20, 0),   -- Tracking
(3, 'Crafter',   10, 0),   -- Knapping
(4, 'Builder',   11, 0),   -- Woodwork
(5, 'Healer',    25, 0),   -- Herbalism
(6, 'Farmer',    26, 2),   -- Farming
(7, 'Fisherman', 22, 0);   -- Fishing

-- ══════════════════════════════════════════════════════════════════════
-- Seed: Daily Schedules
-- ══════════════════════════════════════════════════════════════════════
-- task_type values match AuthServer STASK string mapping:
--   idle, wander, gather, hunt, craft, build, cook, fish,
--   gather_herbs, heal, rest, sleep, track, butcher, forage, repair
--
-- Hours are game-hours (0-23).  Grouped into natural periods:
--   Night  00-04   Sleep
--   Dawn   05-06   Transition / light tasks
--   Morning 07-11  Primary work
--   Midday  12-13  Break / secondary
--   Afternoon 14-17  Primary work continued
--   Evening 18-20  Wind-down / social
--   Night   21-23  Sleep

-- ── Gatherer ──
INSERT OR IGNORE INTO profession_schedule VALUES
(1,  0, 'sleep',   'camp'),
(1,  1, 'sleep',   'camp'),
(1,  2, 'sleep',   'camp'),
(1,  3, 'sleep',   'camp'),
(1,  4, 'sleep',   'camp'),
(1,  5, 'forage',  'wilderness'),
(1,  6, 'forage',  'wilderness'),
(1,  7, 'gather',  'wilderness'),
(1,  8, 'gather',  'wilderness'),
(1,  9, 'gather',  'wilderness'),
(1, 10, 'gather',  'forest'),
(1, 11, 'gather',  'forest'),
(1, 12, 'rest',    'camp'),
(1, 13, 'cook',    'camp'),
(1, 14, 'gather',  'wilderness'),
(1, 15, 'gather',  'wilderness'),
(1, 16, 'gather',  'wilderness'),
(1, 17, 'forage',  'forest'),
(1, 18, 'rest',    'camp'),
(1, 19, 'idle',    'camp'),
(1, 20, 'idle',    'camp'),
(1, 21, 'sleep',   'camp'),
(1, 22, 'sleep',   'camp'),
(1, 23, 'sleep',   'camp');

-- ── Hunter ──
INSERT OR IGNORE INTO profession_schedule VALUES
(2,  0, 'sleep',   'camp'),
(2,  1, 'sleep',   'camp'),
(2,  2, 'sleep',   'camp'),
(2,  3, 'sleep',   'camp'),
(2,  4, 'sleep',   'camp'),
(2,  5, 'track',   'wilderness'),
(2,  6, 'track',   'wilderness'),
(2,  7, 'hunt',    'wilderness'),
(2,  8, 'hunt',    'wilderness'),
(2,  9, 'hunt',    'wilderness'),
(2, 10, 'hunt',    'forest'),
(2, 11, 'hunt',    'forest'),
(2, 12, 'butcher', 'camp'),
(2, 13, 'cook',    'camp'),
(2, 14, 'hunt',    'wilderness'),
(2, 15, 'hunt',    'wilderness'),
(2, 16, 'track',   'forest'),
(2, 17, 'gather',  'wilderness'),
(2, 18, 'rest',    'camp'),
(2, 19, 'rest',    'camp'),
(2, 20, 'idle',    'camp'),
(2, 21, 'sleep',   'camp'),
(2, 22, 'sleep',   'camp'),
(2, 23, 'sleep',   'camp');

-- ── Crafter ──
INSERT OR IGNORE INTO profession_schedule VALUES
(3,  0, 'sleep',   'camp'),
(3,  1, 'sleep',   'camp'),
(3,  2, 'sleep',   'camp'),
(3,  3, 'sleep',   'camp'),
(3,  4, 'sleep',   'camp'),
(3,  5, 'gather',  'wilderness'),
(3,  6, 'gather',  'wilderness'),
(3,  7, 'craft',   'station'),
(3,  8, 'craft',   'station'),
(3,  9, 'craft',   'station'),
(3, 10, 'craft',   'station'),
(3, 11, 'craft',   'station'),
(3, 12, 'rest',    'camp'),
(3, 13, 'cook',    'camp'),
(3, 14, 'craft',   'station'),
(3, 15, 'craft',   'station'),
(3, 16, 'craft',   'station'),
(3, 17, 'gather',  'wilderness'),
(3, 18, 'rest',    'camp'),
(3, 19, 'idle',    'camp'),
(3, 20, 'idle',    'camp'),
(3, 21, 'sleep',   'camp'),
(3, 22, 'sleep',   'camp'),
(3, 23, 'sleep',   'camp');

-- ── Builder ──
INSERT OR IGNORE INTO profession_schedule VALUES
(4,  0, 'sleep',   'camp'),
(4,  1, 'sleep',   'camp'),
(4,  2, 'sleep',   'camp'),
(4,  3, 'sleep',   'camp'),
(4,  4, 'sleep',   'camp'),
(4,  5, 'gather',  'forest'),
(4,  6, 'gather',  'forest'),
(4,  7, 'build',   'camp'),
(4,  8, 'build',   'camp'),
(4,  9, 'build',   'camp'),
(4, 10, 'build',   'camp'),
(4, 11, 'repair',  'camp'),
(4, 12, 'rest',    'camp'),
(4, 13, 'cook',    'camp'),
(4, 14, 'build',   'camp'),
(4, 15, 'build',   'camp'),
(4, 16, 'build',   'camp'),
(4, 17, 'craft',   'station'),
(4, 18, 'rest',    'camp'),
(4, 19, 'idle',    'camp'),
(4, 20, 'idle',    'camp'),
(4, 21, 'sleep',   'camp'),
(4, 22, 'sleep',   'camp'),
(4, 23, 'sleep',   'camp');

-- ── Healer ──
INSERT OR IGNORE INTO profession_schedule VALUES
(5,  0, 'sleep',      'camp'),
(5,  1, 'sleep',      'camp'),
(5,  2, 'sleep',      'camp'),
(5,  3, 'sleep',      'camp'),
(5,  4, 'sleep',      'camp'),
(5,  5, 'gather_herbs','forest'),
(5,  6, 'gather_herbs','forest'),
(5,  7, 'heal',       'camp'),
(5,  8, 'heal',       'camp'),
(5,  9, 'gather_herbs','wilderness'),
(5, 10, 'gather_herbs','wilderness'),
(5, 11, 'craft',      'station'),
(5, 12, 'rest',       'camp'),
(5, 13, 'cook',       'camp'),
(5, 14, 'heal',       'camp'),
(5, 15, 'heal',       'camp'),
(5, 16, 'gather_herbs','forest'),
(5, 17, 'craft',      'station'),
(5, 18, 'rest',       'camp'),
(5, 19, 'idle',       'camp'),
(5, 20, 'idle',       'camp'),
(5, 21, 'sleep',      'camp'),
(5, 22, 'sleep',      'camp'),
(5, 23, 'sleep',      'camp');

-- ── Farmer ──
INSERT OR IGNORE INTO profession_schedule VALUES
(6,  0, 'sleep',   'camp'),
(6,  1, 'sleep',   'camp'),
(6,  2, 'sleep',   'camp'),
(6,  3, 'sleep',   'camp'),
(6,  4, 'sleep',   'camp'),
(6,  5, 'gather',  'wilderness'),
(6,  6, 'farm',    'camp'),
(6,  7, 'farm',    'camp'),
(6,  8, 'farm',    'camp'),
(6,  9, 'farm',    'camp'),
(6, 10, 'farm',    'camp'),
(6, 11, 'farm',    'camp'),
(6, 12, 'rest',    'camp'),
(6, 13, 'cook',    'camp'),
(6, 14, 'farm',    'camp'),
(6, 15, 'farm',    'camp'),
(6, 16, 'farm',    'camp'),
(6, 17, 'gather',  'wilderness'),
(6, 18, 'rest',    'camp'),
(6, 19, 'idle',    'camp'),
(6, 20, 'idle',    'camp'),
(6, 21, 'sleep',   'camp'),
(6, 22, 'sleep',   'camp'),
(6, 23, 'sleep',   'camp');

-- ── Fisherman ──
INSERT OR IGNORE INTO profession_schedule VALUES
(7,  0, 'sleep',   'camp'),
(7,  1, 'sleep',   'camp'),
(7,  2, 'sleep',   'camp'),
(7,  3, 'sleep',   'camp'),
(7,  4, 'sleep',   'camp'),
(7,  5, 'fish',    'water'),
(7,  6, 'fish',    'water'),
(7,  7, 'fish',    'water'),
(7,  8, 'fish',    'water'),
(7,  9, 'fish',    'water'),
(7, 10, 'fish',    'water'),
(7, 11, 'fish',    'water'),
(7, 12, 'rest',    'camp'),
(7, 13, 'cook',    'camp'),
(7, 14, 'fish',    'water'),
(7, 15, 'fish',    'water'),
(7, 16, 'fish',    'water'),
(7, 17, 'craft',   'station'),
(7, 18, 'rest',    'camp'),
(7, 19, 'idle',    'camp'),
(7, 20, 'idle',    'camp'),
(7, 21, 'sleep',   'camp'),
(7, 22, 'sleep',   'camp'),
(7, 23, 'sleep',   'camp');
