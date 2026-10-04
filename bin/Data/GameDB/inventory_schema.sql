-- Inventory System Schema
-- Urho3D Survival Game — inventory rules, equipment slots, containers

CREATE TABLE IF NOT EXISTS inventory_rules (
    id                  INTEGER PRIMARY KEY DEFAULT 1,
    base_slots          INTEGER DEFAULT 10,
    max_weight          REAL DEFAULT 30.0,
    heavy_weight        REAL DEFAULT 45.0,
    max_weight_absolute REAL DEFAULT 60.0,
    encumbered_speed    REAL DEFAULT 0.7,
    heavy_speed         REAL DEFAULT 0.4,
    heavy_stamina_mult  REAL DEFAULT 2.0
);

INSERT OR IGNORE INTO inventory_rules DEFAULT VALUES;

CREATE TABLE IF NOT EXISTS equipment_slots (
    slot        TEXT PRIMARY KEY,
    label       TEXT NOT NULL,
    accepts     TEXT NOT NULL
);

INSERT OR IGNORE INTO equipment_slots VALUES
('hand',    'Main Hand',   'weapon,tool'),
('offhand', 'Off Hand',    'weapon,armor'),
('body',    'Body',        'armor,clothing'),
('head',    'Head',        'armor,clothing'),
('feet',    'Feet',        'clothing'),
('back',    'Back',        'clothing,container'),
('bag',     'Bag',         'any');

CREATE TABLE IF NOT EXISTS container_bonus (
    item_id     INTEGER PRIMARY KEY REFERENCES items(id),
    extra_slots INTEGER DEFAULT 0,
    extra_weight REAL DEFAULT 0,
    equip_slot  TEXT DEFAULT 'back'
);

INSERT OR IGNORE INTO container_bonus VALUES
(500, 5,  5.0,  'back'),
(503, 3,  2.0,  'hand'),
(419, 4,  3.0,  'back'),
(506, 2,  1.0,  'offhand'),
(507, 8,  8.0,  'back');
