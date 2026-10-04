-- AI Tuning System: runtime-tunable NPC AI parameters
-- Loaded on server start, editable via admin protocol (Phase 2+)

CREATE TABLE IF NOT EXISTS ai_tuning (
    key     TEXT PRIMARY KEY,
    value   REAL NOT NULL,
    label   TEXT,
    category TEXT DEFAULT 'general',
    min_val REAL DEFAULT 0.0,
    max_val REAL DEFAULT 100.0
);

-- Vital decay rates (per second)
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('hunger_decay_rate',   0.15, 'Hunger Decay Rate',   'vitals', 0.01, 5.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('thirst_decay_rate',   0.08, 'Thirst Decay Rate',   'vitals', 0.01, 5.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('stamina_drain_active', 0.10, 'Stamina Drain (Active)', 'vitals', 0.01, 5.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('stamina_regen_rest',  0.30, 'Stamina Regen (Rest)', 'vitals', 0.01, 5.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('warmth_night_drain',  0.40, 'Warmth Night Drain',  'vitals', 0.01, 5.0);

-- Task thresholds (0-100 vital values)
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('hunger_eat_inv_threshold',      60.0, 'Eat (Inventory) Threshold',   'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('hunger_eat_abstract_threshold', 25.0, 'Eat (Abstract) Threshold',    'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('hunger_hunt_threshold',         50.0, 'Hunt Threshold',              'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('hunger_trap_threshold',         70.0, 'Trap Place Threshold',        'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('warmth_critical_threshold',     25.0, 'Warmth Critical Threshold',   'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('stamina_critical_threshold',    20.0, 'Stamina Critical Threshold',  'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('hunger_gather_threshold',       60.0, 'Gather Threshold',            'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('warmth_sit_threshold',          70.0, 'Sit by Fire Threshold',       'thresholds', 0.0, 100.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('wander_chance',                 0.35, 'Wander Chance',               'thresholds', 0.0, 1.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('darkness_deep_night',           0.80, 'Deep Night Darkness',         'thresholds', 0.0, 1.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('darkness_dusk',                 0.60, 'Dusk Darkness',               'thresholds', 0.0, 1.0);

-- Timing
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('task_eval_interval',            2.0,  'Task Eval Interval (s)',      'timing', 0.1, 30.0);

-- Fire
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('campfire_tend_threshold',       60.0, 'Campfire Tend Threshold (BU)', 'fire', 0.0, 600.0);
INSERT OR IGNORE INTO ai_tuning (key, value, label, category, min_val, max_val)
VALUES ('campfire_tend_range',           4.0,  'Campfire Tend Range (m)',      'fire', 1.0, 50.0);
