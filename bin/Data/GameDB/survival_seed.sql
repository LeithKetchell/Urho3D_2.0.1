-- Survival Pressure Seed Data
-- Apply after survival_schema.sql

-- Default rules (single row each)
INSERT OR IGNORE INTO hunger_rules DEFAULT VALUES;
INSERT OR IGNORE INTO thirst_rules DEFAULT VALUES;
INSERT OR IGNORE INTO warmth_rules DEFAULT VALUES;
INSERT OR IGNORE INTO stamina_rules DEFAULT VALUES;
INSERT OR IGNORE INTO sleep_rules DEFAULT VALUES;
INSERT OR IGNORE INTO death_rules DEFAULT VALUES;
INSERT OR IGNORE INTO fire_rules DEFAULT VALUES;

-- Water sources
INSERT OR IGNORE INTO water_sources VALUES
(1, 'River Water',    'river',   30, 2.0, 0.05, 0,   'Slight illness risk. Crouch at river edge.'),
(2, 'Lake Water',     'lake',    25, 2.0, 0.1,  0,   'Still water — higher illness risk.'),
(3, 'Well Water',     'well',    35, 2.0, 0.0,  0,   'Clean. Safe. Must be near well building.'),
(4, 'Rain Collect',   'rain',    20, 0.0, 0.0,  501, 'Passive. Clay pot left out in rain fills slowly.'),
(5, 'Boiled Water',   'clay_pot',40, 3.0, 0.0,  501, 'Boil water at fire. Safest. Best restoration.'),
(6, 'Snow Melt',      'clay_pot',25, 3.0, 0.0,  501, 'Melt snow at fire. Winter water source.');

-- Fuel types
INSERT OR IGNORE INTO fuel_types VALUES
(2,  0.5, 0),
(11, 2.0, 0),
(43, 4.0, 5.0);
