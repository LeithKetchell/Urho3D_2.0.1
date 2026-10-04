-- Phenomena Rules — conditions that trigger observable world events
-- Apply: sqlite3 game_rules.db < phenomena_rules.sql

-- condition_type: what proximity/interaction triggers the phenomenon
-- result_type: category of outcome (visual hint for client)
-- visual_hint: client-side effect name (particle, glow, etc.)
-- insight_category: which knowledge branch this feeds (metallurgy, agriculture, etc.)
-- epoch_tier_required: minimum epoch/tech tier before phenomenon can occur (0 = always)
CREATE TABLE IF NOT EXISTS phenomena_rules (
    id                  INTEGER PRIMARY KEY,
    condition_type      TEXT NOT NULL,
    result_type         TEXT NOT NULL,
    visual_hint         TEXT NOT NULL DEFAULT 'glow',
    insight_category    TEXT NOT NULL DEFAULT 'observation',
    epoch_tier_required INTEGER DEFAULT 0,
    description         TEXT
);

-- Seed data: initial phenomenon conditions
INSERT OR IGNORE INTO phenomena_rules (id, condition_type, result_type, visual_hint, insight_category, epoch_tier_required, description)
VALUES
    (1, 'FIRE_NEAR_ORE',     'ore_color_change',   'ember_glow',      'metallurgy',   0, 'Fire pit near metal deposit causes ore to discolor — hints at smelting'),
    (2, 'SEED_ON_FERTILE',   'spontaneous_growth',  'sprout_particles', 'agriculture',  0, 'Dropped seed on fertile soil begins to sprout — hints at farming'),
    (3, 'CLAY_NEAR_HEAT',    'clay_hardening',      'steam_wisps',      'pottery',      1, 'Clay near fire hardens — hints at pottery and kiln use'),
    (4, 'LIGHTNING_STRIKE',  'fire_ignition',       'flash_burn',       'fire_mastery', 0, 'Lightning strike ignites dry grass or tree — hints at fire control');

CREATE INDEX IF NOT EXISTS idx_phenomena_condition ON phenomena_rules(condition_type);
CREATE INDEX IF NOT EXISTS idx_phenomena_epoch ON phenomena_rules(epoch_tier_required);
