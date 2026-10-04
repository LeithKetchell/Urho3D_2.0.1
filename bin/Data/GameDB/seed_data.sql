-- Game Database Seed Data
-- Urho3D Survival Game
-- Apply after schema.sql: sqlite3 game_rules.db < seed_data.sql

-- ============================================================
-- ITEMS
-- ============================================================

-- Raw materials
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(1,   'Rough Stone',     'raw',      20, 0.5, 'A loose stone. Can be knapped into sharp stone.'),
(2,   'Stick',           'raw',      20, 0.3, 'A fallen branch. Foundation of many tools.'),
(3,   'Plant Fiber',     'raw',      30, 0.1, 'Pulled from tall grass. Weak binding material.'),
(4,   'Clay',            'raw',      15, 1.0, 'Dug from riverbanks. Used for pottery and kilns.'),
(5,   'Flint',           'raw',      10, 0.4, 'Sharp-edged stone. Better than rough stone for cutting.'),
(6,   'Berries',         'food',     20, 0.1, 'Seasonal. Restores a little health.'),
(7,   'Raw Meat',        'food',     5,  1.0, 'Spoils fast. Cook it or lose it.'),
(8,   'Cooked Meat',     'food',     5,  0.8, 'Nutritious. Lasts longer than raw.'),
(9,   'Dried Meat',      'food',     10, 0.5, 'Preserved. Lasts through winter.'),
(10,  'Small Fish',      'food',     10, 0.3, 'Caught bare-handed in shallows.'),
(11,  'Log',             'raw',      5,  3.0, 'Chopped from a tree. Heavy.'),
(12,  'Plank',           'material', 10, 1.5, 'Split from a log. Building material.'),
(13,  'Milk',            'food',     5,  0.5, 'From a tamed cow. Nutritious.');

-- Fire fuel (Fire System Phase 1a — IDs coordinated with ResourceMap.h Phase 1b)
-- Softwood + hardwood are BOTH required for friction ignition per PLAN_FIRE_SYSTEM.md.
-- Category 'fuel' matches Charcoal (id 43). Rock for fire-pit construction reuses
-- item 1 'Rough Stone' — no new rock item is needed.
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(15,  'Softwood',        'fuel',     15, 0.8, 'Lightweight wood. Kindles easily, burns fast. Paired with hardwood for friction fires.'),
(16,  'Hardwood',        'fuel',     10, 1.2, 'Dense wood. Slow to light, long burn. Paired with softwood for friction fires.');

-- Animal products
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(20,  'Sinew',           'raw',      20, 0.1, 'The gateway material. Binds everything.'),
(21,  'Hide',            'raw',      5,  2.0, 'Raw animal skin. Needs scraping.'),
(22,  'Leather',         'material', 5,  1.5, 'Processed hide. Durable, flexible.'),
(23,  'Bone',            'raw',      10, 0.3, 'Needles, hooks, small tools.'),
(24,  'Fur',             'raw',      5,  1.0, 'Warmth. Trade value.'),
(25,  'Gut',             'raw',      10, 0.2, 'Bowstrings, snare cord, rope.'),
(26,  'Fat',             'raw',      5,  0.5, 'Fuel, waterproofing, cooking.'),
(27,  'Antler',          'raw',      3,  0.8, 'Digging tool, weapon tip.'),
(28,  'Tusk',            'raw',      2,  1.0, 'Strong point. Digging, piercing.'),
(29,  'Claw',            'raw',      5,  0.2, 'Decoration, small blades.'),
(30,  'Feather',         'raw',      20, 0.05,'Arrow fletching.');

-- Processed materials
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(40,  'Sharp Stone',     'material', 10, 0.4, 'Knapped from rough stone. Cutting edge.'),
(41,  'Cordage',         'material', 10, 0.2, 'Twisted fiber. Stronger than raw.'),
(42,  'Rope',            'material', 5,  0.5, 'Braided cordage or gut. Strong.'),
(43,  'Charcoal',        'fuel',     15, 0.3, 'Burns hotter than wood. Needed for metal smelting.'),
(44,  'Fired Clay',      'material', 10, 1.0, 'Hardened in fire. Bricks, pots.');

-- Tools
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(100, 'Hand Axe',        'tool',     1,  1.5, 'Chop trees, split wood.'),
(101, 'Scraper',         'tool',     1,  0.8, 'Process hide into leather.'),
(102, 'Digging Stick',   'tool',     1,  1.0, 'Dig clay, plant seeds, shallow mining.'),
(103, 'Bone Needle',     'tool',     1,  0.1, 'Sew hide into clothing.'),
(104, 'Fire Kit',        'tool',     1,  0.5, 'Start fires. Friction method.'),
(105, 'Fishing Rod',     'tool',     1,  1.0, 'Reliable fishing.'),
(106, 'Stone Pick',      'tool',     1,  2.0, 'Open-cut mining.'),
(107, 'Hammer Stone',    'tool',     1,  1.5, 'Shaping, knapping, forging.'),
(108, 'Torch',           'tool',     3,  0.6, 'Unlit. Light from an existing fire to carry flame.'),
(109, 'Burning Torch',   'tool',     1,  0.6, 'Lit torch. Burns for a few minutes. Can ignite a cold fire pit.');

-- Weapons
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(200, 'Spear',           'weapon',   1,  2.0, 'Melee. Reach advantage.'),
(201, 'Bow',             'weapon',   1,  1.0, 'Ranged. Needs arrows.'),
(202, 'Arrow',           'ammo',     20, 0.1, 'Ammunition for bow.'),
(203, 'Sling',           'weapon',   1,  0.3, 'Ranged. Uses stones as ammo.'),
(204, 'Club',            'weapon',   1,  2.0, 'Heavy. Blunt damage.'),
(205, 'Javelin',         'weapon',   3,  1.5, 'Thrown. One use per throw, recoverable.'),
(206, 'Knife',           'weapon',   1,  0.5, 'Fast, weak. Better for skinning than fighting.');

-- Armor & clothing
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(300, 'Hide Wrap',       'clothing', 1,  1.5, 'Basic coverage. Slight warmth.'),
(301, 'Leather Tunic',   'armor',    1,  3.0, 'First real armor.'),
(302, 'Fur Cloak',       'clothing', 1,  2.0, 'Warmth in winter. No armor value.'),
(303, 'Hide Boots',      'clothing', 1,  1.0, 'Foot protection. Faster on rough terrain.'),
(304, 'Leather Cap',     'armor',    1,  0.8, 'Head protection.'),
(305, 'Wood Shield',     'armor',    1,  3.0, 'Block hits. Requires one hand.');

-- Traps
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(400, 'Simple Snare',    'trap',     3,  0.5, 'Catches small animals. Single use.'),
(401, 'Meat Trap',       'trap',     1,  2.0, 'Baited with meat. Attracts carnivores. Gamble.'),
(402, 'Berry Trap',      'trap',     1,  2.0, 'Baited with berries. Attracts herbivores. Safe.'),
(403, 'Fish Trap',       'trap',     1,  1.5, 'Placed in water. Passive fish collection.'),
(404, 'Pit Trap',        'trap',     1,  0.0, 'Dug into ground. Large animals.'),
(405, 'Deadfall Trap',   'trap',     1,  3.0, 'Heavy stone, trigger stick. Kills outright on success.');

-- Cage traps (live capture — tames animal on collection)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, model, icon, description, tier) VALUES
(406, 'Small Cage Trap', 'trap', 1, 2.5, 40, 0, 'Models/CharacterItems/Cage_Small.mdl', NULL, 'Wicker cage. Captures small animals alive for taming.', 2),
(407, 'Large Cage Trap', 'trap', 1, 5.0, 60, 0, 'Models/CharacterItems/Cage_Small.mdl', NULL, 'Reinforced cage. Captures medium animals alive.',        3),
(408, 'Iron Cage Trap',  'trap', 1, 8.0, 120,0, 'Models/CharacterItems/Cage_Small.mdl', NULL, 'Iron-barred cage. Captures large and dangerous animals.',5);

-- Containers
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(500, 'Leather Bag',     'container',1,  1.0, 'Carry +5 inventory slots.'),
(501, 'Clay Pot',        'container',1,  2.0, 'Store water, grain, or stew. Cooking vessel.'),
(502, 'Clay Jar',        'container',5,  1.5, 'Smaller storage. Preserves food longer.'),
(503, 'Basket',          'container',1,  0.5, 'Woven fiber. Carry foraged goods.'),
(505, 'Wooden Tub',      'container',1,  4.0, 'Carved wooden tub. Holds water, stores goods, or washes hides.');
(504, 'Barrel',          'container',1,  3.0, 'Wooden barrel. Bulk storage for liquids and grain.');
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, tier, description) VALUES
(506, 'Hide Pouch',      'container',1,  0.3, 1, 'Small belt pouch. +2 inventory slots.'),
(507, 'Leather Backpack','container',1,  1.5, 2, 'Sturdy leather pack. +8 inventory slots, +8 kg capacity.');

-- Buildings/structures
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(600, 'Lean-To',         'building', 1,  0.0, 'Basic shelter. Blocks wind. Sleep here to heal.'),
(601, 'Hut',             'building', 1,  0.0, 'Real shelter. Warmth, storage, respawn point.'),
(602, 'Stone Ring',      'building', 1,  0.0, 'Fireplace. Required for cooking and fire kit use.'),
(603, 'Drying Rack',     'building', 1,  0.0, 'Hang meat to dry. Preserves food.'),
(604, 'Kiln',            'building', 1,  0.0, 'Fire clay, smelt copper. Needs charcoal.'),
(605, 'Storage Pit',     'building', 1,  0.0, 'Underground storage. Keeps food cool.'),
(606, 'Tanning Frame',   'building', 1,  0.0, 'Stretch and scrape hides. Bulk leather processing.'),
(607, 'Fish Weir',       'building', 1,  0.0, 'Stone dam in stream. Passive fish + gold collection.'),
(608, 'Charcoal Kiln',   'building', 1,  0.0, 'Slow-burn wood into charcoal.');

-- Seeds
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(700, 'Wheat Seed',      'seed',     20, 0.05,'Plant in spring, harvest in autumn.'),
(701, 'Flax Seed',       'seed',     20, 0.05,'Plant in spring. Yields fiber.'),
(702, 'Berry Bush Seed', 'seed',     10, 0.05,'Plant near water. Grows slowly.');

-- Crop harvest yields
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(710, 'Wheat Grain',     'food',     20, 0.2, 'Harvested wheat. Edible raw, better cooked.'),
(711, 'Flax Fiber',      'raw',      20, 0.15,'Harvested flax. Strong fiber for cordage.'),
(712, 'Fresh Berries',   'food',     15, 0.1, 'Cultivated berries. Reliable food source.'),
(720, 'Medicinal Herbs', 'material', 10, 0.2, 'Forest herbs. Heals wounds, fights infection.'),
(730, 'Wild Mushroom',  'food',     15, 0.15,'Foraged from forest floor. Edible, slight heal.');

INSERT OR IGNORE INTO food_properties (item_id, hunger, health, warmth, spoil_time, cooked, poison_chance) VALUES
(710, 8, 1, 0, 3600, 0, 0),
(712, 6, 1, 0, 600, 0, 0),
(730, 4, 2, 0, 1800, 0, 0.05);

-- Crop type rules (seed -> harvest mapping)
INSERT OR IGNORE INTO crop_types (seed_item_id, harvest_item_id, harvest_qty, seed_return, plant_season, harvest_season, grow_days, min_flat, near_water_range, tool_req, model) VALUES
(700, 710, 4, 2, 'spring', 'autumn', 90, 0.9, 30.0, 102, 'Models/Crops/WheatCrop.mdl'),
(701, 711, 3, 1, 'spring', 'autumn', 90, 0.9, 30.0, 102, 'Models/Nature/Wheat.mdl'),
(702, 712, 5, 2, 'any',    'any',    60, 0.85, 15.0, 102, 'Models/Props/BerryBush.mdl');

-- Metals (later tiers)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, tier, description) VALUES
(800, 'Copper Ore',      'raw',      15, 1.0, 3, 'Smelted into copper. First metal.'),
(801, 'Tin Ore',         'raw',      15, 1.0, 3, 'Useless alone. Vital for bronze.'),
(802, 'Iron Ore',        'raw',      15, 1.5, 5, 'Needs high-temp forge to smelt.'),
(803, 'Gold Nugget',     'raw',      10, 2.0, 3, 'Trade currency. Too soft for tools.'),
(804, 'Copper Ingot',    'material', 10, 1.5, 3, 'Smelted copper. Soft metal.'),
(805, 'Tin Ingot',       'material', 10, 1.5, 3, 'Smelted tin. Combine with copper.'),
(806, 'Bronze Ingot',    'material', 10, 2.0, 4, 'The alloy that changes everything.'),
(807, 'Iron Ingot',      'material', 10, 2.5, 5, 'Superior metal.'),
(810, 'Copper Axe',      'tool',     1,  1.5, 3, 'Better than stone, still soft.'),
(811, 'Copper Spear',    'weapon',   1,  2.0, 3, 'Holds an edge longer than stone.'),
(812, 'Bronze Axe',      'tool',     1,  2.0, 4, 'Hard alloy. Real tool.'),
(813, 'Bronze Spear',    'weapon',   1,  2.0, 4, 'The weapon of armies.'),
(814, 'Bronze Sword',    'weapon',   1,  2.5, 4, 'Close combat. Fast, deadly.'),
(815, 'Bronze Armor',    'armor',    1,  8.0, 4, 'Heavy. Very hard to penetrate.'),
(816, 'Bronze Shield',   'armor',    1,  5.0, 4, 'Blocks most attacks.'),
(817, 'Bronze Knife',    'weapon',   1,  1.0, 4, 'Fast, sharp. Skinning and close work.'),
(820, 'Iron Axe',        'tool',     1,  2.0, 5, 'Cuts anything.'),
(821, 'Iron Sword',      'weapon',   1,  3.0, 5, 'The pinnacle.'),
(822, 'Iron Armor',      'armor',    1,  10.0,5, 'Fortress on legs.'),
-- Phase 32: Steel Age
(825, 'Steel Ingot',     'material', 10, 3.0, 6, 'Hardened iron-carbon alloy. The apex of metallurgy.'),
(830, 'Steel Axe',       'tool',     1,  2.5, 6, 'Unbreakable edge. Fells trees in half the time.'),
(831, 'Steel Sword',     'weapon',   1,  3.5, 6, 'Holds its edge forever. The ultimate blade.'),
(832, 'Steel Armor',     'armor',    1,  12.0,6, 'Plate steel. Near-impenetrable.'),
(833, 'Steel Spear',     'weapon',   1,  2.5, 6, 'Long reach. Formation weapon.'),
(834, 'Iron Shield',     'armor',    1,  6.0, 5, 'Iron-bound wood. Blocks one strike per round.'),
(835, 'Steel Shield',    'armor',    1,  7.0, 6, 'Full steel. The wall between life and death.'),
-- Phase 37: Trace element ores
(840, 'Manganese Ore',   'raw',      10, 2.0, 5, 'Dark heavy ore. Toughens steel when smelted.'),
(841, 'Chromium Ore',    'raw',      10, 2.5, 6, 'Silvery ore. Resists corrosion.'),
(842, 'Tungsten Ore',    'raw',      5,  4.0, 7, 'Extremely dense. Holds an edge like nothing else.'),
(843, 'Nickel Ore',      'raw',      10, 2.0, 5, 'Pale metallic ore. Resists cold and weather.'),
(844, 'Rich Carbon',     'raw',      15, 1.0, 4, 'Black mineral carbon. Hardens iron dramatically.'),
-- Phase 38: Mystery alloy
(850, 'Mystery Alloy Ingot', 'material', 5, 3.0, 5, 'Strange metal. Different from normal iron. Properties unknown.'),
-- Phase 40: Named alloy ingots
(851, 'Manganese Steel Ingot', 'material', 5, 3.0, 7, 'Tough steel. Absorbs impact without cracking.'),
(852, 'Chromium Steel Ingot',  'material', 5, 3.0, 7, 'Stainless steel. Resists corrosion and weather.'),
(853, 'Tungsten Steel Ingot',  'material', 5, 4.0, 8, 'Ultra-hard steel. Holds an edge indefinitely.'),
(854, 'Nickel Steel Ingot',    'material', 5, 3.0, 7, 'Weather-proof steel. Performs in any conditions.'),
(855, 'Carbon Steel Ingot',    'material', 5, 3.0, 6, 'Hard but brittle. The sharpest edge known.'),
-- Phase 41: Hybrid alloy
(860, 'Hybrid Alloy Ingot',   'material', 5, 4.0, 8, 'Master-blended steel. Two trace elements fused. The apex.'),
-- Fire Carrying Phase 1: Tree Resin
(870, 'Tree Resin',           'material', 20, 0.3, 1, 'Sticky aromatic sap from Acacia. Burns slowly. Essential for resin torches.'),
-- Fire Carrying Phase 2: Resin Torch
(871, 'Resin Torch',          'weapon',   1, 0.8, 2, 'Slow-burning torch coated in tree resin. Survives light rain. Burns for hours.'),
-- Fire Carrying Phase 3: Fire Bundle
(872, 'Fire Bundle',          'material', 1, 0.5, 3, 'Bark-wrapped resin bundle. Carries fire for a full day. Unwrap to light a torch.'),
-- Fire Carrying Phase 4: Fire Pot
(873, 'Bark Vessel',          'material', 1, 0.8, 3, 'Hardwood bark holding wet embers. Water slows the burn to a days-long smolder.'),
-- Botanical Discovery: Willow Bark
(875, 'Willow Extract',        'material', 10, 0.3, 2, 'Rooting hormone from willow bark. Apply to cuttings and seeds to accelerate growth.'),
-- Botanical Discovery: Eucalyptus
(877, 'Eucalyptus Leaves',   'material', 15, 0.1, 1, 'Pungent aromatic leaves. Clears lungs, wards illness, repels insects.'),
(879, 'She-Oak Bark',        'material', 10, 0.4, 2, 'Thick waterproof bark from She-Oak. Shaped into vessels that hold water or fire.'),
(878, 'Eucalyptus Poultice', 'consumable', 5, 0.2, 2, 'Crushed leaf compress. Heals wounds and prevents infection. Restores 20 HP.');

-- Textiles and Weaving
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, description) VALUES
(410, 'Reed Basket',     'container', 3,  0.8, 1, 'Woven basket. Storage and fish trapping.'),
(411, 'Woven Mat',       'material',  5,  1.0, 1, 'Floor covering. Slight warmth bonus.'),
(412, 'Flax Thread',     'material', 20,  0.1, 1, 'Spun from flax fiber. Weaving input.'),
(413, 'Rough Cloth',     'material', 10,  0.5, 1, 'Hand-woven cloth. Coarse but functional.'),
(414, 'Fine Cloth',      'material', 10,  0.4, 1, 'Loom-woven. Smooth, strong.'),
(415, 'Cloth Wrap',      'clothing',  1,  0.8, 2, 'Simple cloth garment. Cool and light.'),
(416, 'Cloth Tunic',     'clothing',  1,  1.0, 2, 'Proper garment. Decent warmth.'),
(417, 'Cloth Bandage',   'medical',  10,  0.1, 1, 'Stops bleeding. Heals minor wounds.'),
(418, 'Fishing Net',     'tool',      1,  1.5, 2, 'Cast net. Area catch in shallow water.'),
(419, 'Cargo Net',       'tool',      1,  2.0, 2, 'Carry more. +4 inventory slots when equipped.');

-- ============================================================
-- RECIPES
-- ============================================================

-- Tier 0: Bare hands
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(1,  'Knap Sharp Stone',    40, 1, 0, 'Knock two stones together.'),
(2,  'Twist Cordage',       41, 1, 0, 'Twist fiber into cord.'),
(3,  'Simple Snare',        400,1, 0, 'A basic trap for small game.'),
(4,  'Basket',              503,1, 0, 'Woven from fiber. Carry more.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(1, 1, 2),
(2, 3, 3),
(3, 2, 2), (3, 3, 3),
(4, 3, 8);

-- Tier 0.5: Bait traps
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(10, 'Meat Trap',    401, 1, 0, 'Baited with meat. Attracts carnivores.'),
(11, 'Berry Trap',   402, 1, 0, 'Baited with berries. Attracts herbivores.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(10, 2, 3), (10, 3, 1), (10, 7, 1),
(11, 2, 3), (11, 3, 1), (11, 6, 3);

-- Tier 1: Sinew-bound tools
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(20, 'Hand Axe',     100, 1, 0, 'Sharp stone lashed to stick. Chops trees.'),
(21, 'Spear',        200, 1, 0, 'Reach. The first real weapon.'),
(22, 'Scraper',      101, 1, 0, 'For processing hides.'),
(23, 'Club',         204, 1, 0, 'Heavy stick. Blunt force.'),
(24, 'Knife',        206, 1, 0, 'Small, fast. Skinning tool.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(20, 40, 1), (20, 2, 1), (20, 20, 2),
(21, 2, 1),  (21, 40, 1),(21, 20, 2),
(22, 40, 1), (22, 2, 1), (22, 20, 1),
(23, 2, 1),  (23, 1, 1), (23, 20, 1),
(24, 5, 1),  (24, 2, 1), (24, 20, 1);

-- Tier 1: Processing
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(30, 'Scrape Hide',     22, 1, 101, 0,   'Leather from raw hide. Needs scraper.'),
(31, 'Fire Kit',        104,1, 0,   0,   'Friction fire. Two sticks and hope.'),
(32, 'Stone Ring',      602,1, 0,   0,   'Fireplace. Needed for cooking.'),
(33, 'Lean-To',         600,1, 100, 0,   'Basic shelter. Needs axe for poles.'),
(34, 'Bone Needle',     103,1, 0,   0,   'Carved from bone with sharp stone.'),
(35, 'Digging Stick',   102,1, 0,   0,   'Horn or antler tip on a stick.'),
(36, 'Torch',           108,1, 0,   0,   'Stick wrapped in fiber. Light from fire to carry.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(30, 21, 1),
(31, 2, 2), (31, 3, 1),
(32, 1, 8),
(33, 2, 6), (33, 22, 2), (33, 41, 3),
(34, 23, 1), (34, 40, 1),
(35, 2, 1), (35, 27, 1), (35, 20, 1),
(36, 2, 1), (36, 3, 2);

-- Tier 1: Food & preservation
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(40, 'Cook Meat',        8,  1, 0, 602, 'Roast over fire. Heals more, lasts longer.'),
(41, 'Drying Rack',      603,1, 100, 0, 'Hang meat to dry. Needs axe.'),
(42, 'Dry Meat',         9,  2, 0, 603, 'Preserved. Survives winter.'),
(43, 'Fish Trap',        403,1, 0,   0, 'Woven trap placed in water. Passive fish collection.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(40, 7, 1), (40, 43, 1),
(41, 2, 4), (41, 41, 2), (41, 22, 1),
(42, 7, 3),
(43, 410, 1), (43, 2, 4);   -- Reed Basket x1 + Stick x4 (was: 6 Stick + 4 Plant Fiber)

-- Tier 1: Weaving basics (craft_fiber → SKILL_WEAVING)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(150, 'Reed Basket',   410, 1, 0,   0,  'Woven from plant fiber. First weaving product.'),
(151, 'Woven Mat',     411, 1, 0,   0,  'Plaited fiber mat. Floor and bedding.'),
(152, 'Flax Thread',   412, 3, 0,   0,  'Spin flax into thread.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(150, 3, 8),          -- Plant Fiber x8
(151, 3, 12),         -- Plant Fiber x12
(152, 711, 2);        -- Flax Fiber x2

-- Tier 2: Hand-woven cloth (needs Bone Needle)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(153, 'Rough Cloth',   413, 1, 103, 0,  'Hand-weave thread into cloth. Needs Bone Needle.'),
(155, 'Cloth Wrap',    415, 1, 103, 0,  'Simple cloth garment.'),
(157, 'Cloth Bandage', 417, 3, 103, 0,  'Tear cloth into strips.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(153, 412, 4),         -- Flax Thread x4
(155, 413, 2), (155, 41, 1),  -- Rough Cloth x2 + Cordage x1
(157, 413, 1);         -- Rough Cloth x1

-- Tier 3: Loom-woven textiles (station_req = 91 = Loom)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(154, 'Fine Cloth',    414, 2, 103, 91, 'Loom-woven cloth. Superior quality.'),
(156, 'Cloth Tunic',   416, 1, 103, 91, 'Loom-made tunic. Good warmth.'),
(158, 'Fishing Net',   418, 1, 0,   91, 'Woven net for area fishing. Loom required.'),
(159, 'Cargo Net',     419, 1, 0,   91, 'Woven carry net. Loom required.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(154, 412, 6),         -- Flax Thread x6
(156, 414, 3), (156, 412, 2),  -- Fine Cloth x3 + Flax Thread x2
(158, 412, 10), (158, 41, 4),  -- Flax Thread x10 + Cordage x4
(159, 412, 8), (159, 41, 6);   -- Flax Thread x8 + Cordage x6

-- Tier assignments for weaving recipes
UPDATE recipes SET tier = 1 WHERE id IN (150, 151, 152);
UPDATE recipes SET tier = 2 WHERE id IN (153, 155, 157);
UPDATE recipes SET tier = 3 WHERE id IN (154, 156, 158, 159);
UPDATE recipes SET tier = 4 WHERE id IN (160, 161, 162, 163);

-- Craft action for weaving recipes → craft_fiber → SKILL_WEAVING
-- (CraftForOwner maps craft_fiber to SKILL_WEAVING at line 7036)

-- Tier 2: Advanced tools
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(50, 'Bow',           201, 1, 206, 'Stick + gut string. Ranged hunting.'),
(51, 'Arrow x5',      202, 5, 206, 'Fletched shafts.'),
(52, 'Fishing Rod',   105, 1, 0,   'Stick + gut + bone hook.'),
(53, 'Sling',         203, 1, 0,   'Hide + cordage. Flings stones.'),
(54, 'Javelin x3',    205, 3, 206, 'Thrown spear. Recoverable.'),
(55, 'Rope',          42,  1, 0,   'Braided cordage or gut. Strong.'),
(56, 'Stone Pick',    106, 1, 0,   'For mining surface ore.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(50, 2, 1), (50, 25, 1), (50, 20, 1),
(51, 2, 5), (51, 5, 3), (51, 30, 5),
(52, 2, 1), (52, 25, 1), (52, 23, 1),
(53, 21, 1),(53, 41, 2),
(54, 2, 3), (54, 40, 3), (54, 20, 3),
(55, 41, 5),
(56, 1, 2), (56, 2, 1), (56, 20, 2);

-- Tier 2: Clothing & armor
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(60, 'Hide Wrap',      300, 1, 103, 'Basic body coverage. Needs needle.'),
(61, 'Leather Tunic',  301, 1, 103, 'First real armor. Needs needle.'),
(62, 'Fur Cloak',      302, 1, 103, 'Warmth in winter. Needs needle.'),
(63, 'Hide Boots',     303, 1, 103, 'Foot protection.'),
(64, 'Leather Cap',    304, 1, 103, 'Head protection.'),
(65, 'Wood Shield',    305, 1, 100, 'Needs axe to shape planks.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(60, 21, 2), (60, 20, 2),
(61, 22, 3), (61, 20, 3),
(62, 24, 3), (62, 20, 2),
(63, 22, 1), (63, 20, 1),
(64, 22, 1), (64, 20, 1),
(65, 12, 3), (65, 20, 2), (65, 22, 1);

-- Tier 2: Buildings
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(70, 'Hut',            601, 1, 100, 'Real shelter. Warmth, storage, respawn.'),
(71, 'Storage Pit',    605, 1, 102, 'Underground storage. Keeps food cool.'),
(72, 'Tanning Frame',  606, 1, 100, 'Bulk hide processing.'),
(73, 'Split Log',      12,  2, 100, 'Axe splits log into planks.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(70, 11, 8), (70, 42, 2), (70, 22, 4), (70, 41, 4),
(71, 1, 10), (71, 11, 2),
(72, 2, 4), (72, 22, 1), (72, 42, 1),
(73, 11, 1);

-- Tier 3: Fire-based processing
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(80, 'Charcoal Kiln',   608, 1, 100, 0,   'Slow-burn wood to charcoal.'),
(81, 'Burn Charcoal',   43,  3, 0,   608, 'Logs become charcoal.'),
(82, 'Kiln',            604, 1, 100, 0,   'Clay + stone. Fires pottery, smelts copper.'),
(83, 'Fire Clay',       44,  2, 0,   604, 'Harden clay in kiln.'),
(84, 'Clay Pot',        501, 1, 0,   604, 'Cooking vessel, water storage.'),
(85, 'Clay Jar',        502, 2, 0,   604, 'Small sealed storage.'),
(86, 'Fish Weir',       607, 1, 100, 0,   'Stone dam in stream. Passive yield.'),
(87, 'Wooden Tub',      505, 1, 100, 0,   'Carve a tub from planks. Water storage, hide washing.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(80, 1, 12), (80, 4, 6),
(81, 11, 3),
(82, 1, 15), (82, 4, 10),
(83, 4, 3),
(84, 4, 5),
(85, 4, 3),
(86, 1, 20), (86, 11, 4),
(87, 12, 4), (87, 41, 2);

-- Tier 3: Metal (copper age)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(90, 'Smelt Copper',    804, 1, 0, 604, 'Ore to ingot. Kiln heat.'),
(91, 'Smelt Tin',       805, 1, 0, 604, 'Ore to ingot.'),
(92, 'Alloy Bronze',    806, 1, 0, 604, 'Copper + tin = the alloy that changes everything.'),
(93, 'Copper Axe',      810, 1, 107, 604, 'Hammered copper. Better than stone.'),
(94, 'Copper Spear',    811, 1, 107, 604, 'Holds edge longer than stone.'),
(95, 'Bronze Axe',      812, 1, 107, 604, 'Hard alloy tool.'),
(96, 'Bronze Spear',    813, 1, 107, 604, 'The weapon of armies.'),
(97, 'Bronze Sword',    814, 1, 107, 604, 'Close combat. Fast.'),
(98, 'Bronze Armor',    815, 1, 107, 604, 'Heavy plate. Very protective.'),
(99, 'Bronze Shield',   816, 1, 107, 604, 'Blocks most attacks.'),
(100,'Bronze Knife',    817, 1, 107, 604, 'Fast, sharp. Skinning tool.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(90, 800, 3), (90, 43, 1),
(91, 801, 3), (91, 43, 1),
(92, 804, 2), (92, 805, 1), (92, 43, 2),
(93, 804, 2), (93, 2, 1), (93, 20, 2),
(94, 804, 1), (94, 2, 1), (94, 20, 2),
(95, 806, 2), (95, 2, 1), (95, 20, 2),
(96, 806, 1), (96, 2, 1), (96, 20, 2),
(97, 806, 2), (97, 22, 1), (97, 20, 1),
(98, 806, 5), (98, 22, 3), (98, 20, 4),
(99, 806, 2), (99, 11, 1), (99, 22, 1),
(100, 806, 1), (100, 22, 1), (100, 20, 1);

-- Tier 5: Iron Age (DC 20 — master smith only)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(110, 'Smelt Iron',     807, 1, 0,   604, 'High-temp smelt. Kiln only.'),
(111, 'Iron Axe',       820, 1, 107, 604, 'Forge an iron axe. Cuts anything.'),
(112, 'Iron Sword',     821, 1, 107, 604, 'Forge an iron sword. The pinnacle.'),
(113, 'Iron Armor',     822, 1, 107, 604, 'Forge iron armor. Fortress on legs.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(110, 802, 5), (110, 43, 3),          -- Iron Ore x5 + Charcoal x3
(111, 807, 2), (111, 2, 1), (111, 20, 2),  -- Iron Ingot x2 + Stick + Sinew x2
(112, 807, 2), (112, 22, 1), (112, 20, 1), -- Iron Ingot x2 + Leather + Sinew
(113, 807, 5), (113, 22, 3), (113, 20, 4); -- Iron Ingot x5 + Leather x3 + Sinew x4

-- Tier 6: Steel Age (DC 23 — master smith at Forge)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(120, 'Forge Steel',     825, 1, 107, 81, 'Iron-carbon alloy. Forge only.'),
(121, 'Steel Axe',       830, 1, 107, 81, 'Unbreakable edge.'),
(122, 'Steel Sword',     831, 1, 107, 81, 'The ultimate blade.'),
(123, 'Steel Armor',     832, 1, 107, 81, 'Near-impenetrable plate.'),
(124, 'Steel Spear',     833, 1, 107, 81, 'Formation weapon. Long reach.'),
(125, 'Iron Shield',     834, 1, 107, 81, 'Iron-bound wood shield.'),
(126, 'Steel Shield',    835, 1, 107, 81, 'Full steel shield.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(120, 807, 2), (120, 43, 2),                -- Iron Ingot x2 + Charcoal x2
(121, 825, 2), (121, 2, 1), (121, 20, 2),   -- Steel Ingot x2 + Stick + Sinew x2
(122, 825, 2), (122, 22, 1), (122, 20, 1),  -- Steel Ingot x2 + Leather + Sinew
(123, 825, 5), (123, 22, 3), (123, 20, 4),  -- Steel Ingot x5 + Leather x3 + Sinew x4
(124, 825, 1), (124, 2, 2), (124, 20, 2),   -- Steel Ingot + Stick x2 + Sinew x2
(125, 807, 2), (125, 12, 2), (125, 22, 1),  -- Iron Ingot x2 + Plank x2 + Leather
(126, 825, 3), (126, 22, 1);                -- Steel Ingot x3 + Leather

-- Tier 7: Named Alloys (Phase 40 — settlement-gated via alloy_knowledge)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(130, 'Manganese Steel', 851, 1, 107, 81, 'Tough alloy. Impact resistant.'),
(131, 'Chromium Steel',  852, 1, 107, 81, 'Stainless alloy. Corrosion proof.'),
(132, 'Tungsten Steel',  853, 1, 107, 81, 'Ultra-hard alloy. Extreme edge retention.'),
(133, 'Nickel Steel',    854, 1, 107, 81, 'Weather-proof alloy. Cold resistant.'),
(134, 'Carbon Steel',    855, 1, 107, 81, 'Hard brittle alloy. Maximum edge.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(130, 807, 2), (130, 840, 1), (130, 43, 2),  -- Iron x2 + Manganese Ore + Charcoal x2
(131, 807, 2), (131, 841, 1), (131, 43, 2),  -- Iron x2 + Chromium Ore + Charcoal x2
(132, 807, 2), (132, 842, 1), (132, 43, 3),  -- Iron x2 + Tungsten Ore + Charcoal x3
(133, 807, 2), (133, 843, 1), (133, 43, 2),  -- Iron x2 + Nickel Ore + Charcoal x2
(134, 807, 2), (134, 844, 2), (134, 43, 1);  -- Iron x2 + Rich Carbon x2 + Charcoal x1

-- ============================================================
-- RECIPE TIERS (D&D-style DC: higher tier = harder to craft)
-- DC = 5 + tier * 3.  Skill check: d20 + craftLevel >= DC.
-- Tier 0: DC 5  (trivial — knapping, twisting fiber)
-- Tier 1: DC 8  (easy — lashing, simple traps, basic tools)
-- Tier 2: DC 11 (moderate — bows, clothing, shelters)
-- Tier 3: DC 14 (hard — kilns, smelting, pottery)
-- Tier 4: DC 17 (expert — bronze weapons, armor)
-- Tier 5: DC 20 (master — iron smelting, iron weapons, iron armor)
-- Tier 6: DC 23 (grandmaster — steel alloy, steel weapons/armor at Forge)
-- ============================================================
UPDATE recipes SET tier = 0 WHERE id IN (1, 2, 3, 4);
UPDATE recipes SET tier = 1 WHERE id IN (10, 11, 20, 21, 22, 23, 24, 30, 31, 32, 34, 35, 36);
UPDATE recipes SET tier = 2 WHERE id IN (33, 40, 41, 42, 43, 50, 51, 52, 53, 54, 55, 56, 60, 61, 62, 63, 64, 65, 70, 71, 72, 73);
UPDATE recipes SET tier = 3 WHERE id IN (80, 81, 82, 83, 84, 85, 86);
UPDATE recipes SET tier = 4 WHERE id IN (90, 91, 92, 93, 94, 95, 96, 97, 98, 99);
UPDATE recipes SET tier = 5 WHERE id IN (110, 111, 112, 113);
UPDATE recipes SET tier = 6 WHERE id IN (120, 121, 122, 123, 124, 125, 126);
UPDATE recipes SET tier = 7 WHERE id IN (130, 131, 133, 134);  -- Named alloys (Smelting 9+)
UPDATE recipes SET tier = 8 WHERE id IN (132);                  -- Tungsten (Smelting 10)

-- ============================================================
-- CRAFT TIME AUDIT — tier-based craft durations (seconds)
-- Tier 0: 3s (knapping a stone tool), Tier 1: 5s (basic craft),
-- Tier 2: 8s (clothing/tools), Tier 3: 12s (bronze-age),
-- Tier 4: 15s (smelting/forging), Tier 5+: 20s (iron/steel)
-- Only updates recipes still at the 2.0 default.
-- ============================================================
UPDATE recipes SET craft_time = 3.0  WHERE tier = 0 AND craft_time = 2.0;
UPDATE recipes SET craft_time = 5.0  WHERE tier = 1 AND craft_time = 2.0;
UPDATE recipes SET craft_time = 8.0  WHERE tier = 2 AND craft_time = 2.0;
UPDATE recipes SET craft_time = 12.0 WHERE tier = 3 AND craft_time = 2.0;
UPDATE recipes SET craft_time = 15.0 WHERE tier = 4 AND craft_time = 2.0;
UPDATE recipes SET craft_time = 20.0 WHERE tier >= 5 AND craft_time = 2.0;

-- ============================================================
-- FIRE CARRYING RECIPES (Phases 1-4)
-- ============================================================
-- Tap Tree: player/NPC action, not a craft recipe — handled by HandleTapTree server-side
-- Resin Torch: Torch + Resin, no station (Herbalism 2+)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(140, 'Resin Torch',     871, 1, 0, 'Coat a torch in tree resin. Burns for hours, survives light rain.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(140, 108, 1), (140, 870, 1);  -- Torch(108) + Tree Resin(870)

-- Fire Bundle: Bark + Resin x2 + Dry Grass, FireMaking 4+
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(141, 'Fire Bundle',     872, 1, 0, 'Wrap resin and tinder in bark. Carries fire for a full day.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(141, 3, 4), (141, 870, 2);  -- Fiber/Grass(3) x4 + Tree Resin(870) x2

-- Fire Pot: Clay Pot + Resin x3, FireMaking 5+ and Pottery
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, description) VALUES
(142, 'Bark Vessel',     873, 1, 0, 0,   'Shape hardwood bark into a watertight vessel. Carries fire or water.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(142, 879, 2), (142, 41, 1);  -- She-Oak Bark(879) x2 + Cordage(41) — shaped bark vessel

-- Unwrap Fire Bundle: consumes bundle, creates lit torch (no skill check — tier 0)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(143, 'Unwrap Fire Bundle', 109, 1, 0, 'Tear open the bundle. The embers light a fresh torch.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(143, 872, 1);  -- Fire Bundle(872) → Burning Torch(109)

UPDATE recipes SET tier = 1 WHERE id IN (140);    -- Resin Torch: DC 8 (easy, Herbalism 2+)
UPDATE recipes SET tier = 3 WHERE id IN (141);    -- Fire Bundle: DC 14 (hard, FireMaking 4+)
UPDATE recipes SET tier = 4 WHERE id IN (142);    -- Fire Pot: DC 17 (expert, FireMaking 5+ + Pottery)
UPDATE recipes SET tier = 0 WHERE id IN (143);    -- Unwrap: DC 5 (trivial, anyone can do it)

-- Botanical Discovery: Willow Extract is not crafted into products —
-- it is applied directly to saplings/crops as a growth accelerator.
-- No recipe needed. Server applies the multiplier when NPC uses extract on a planting.

-- Botanical Discovery: Eucalyptus Poultice recipe
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, description) VALUES
(147, 'Eucalyptus Poultice', 878, 1, 0, 'Crush leaves into a healing compress. Prevents infection.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(147, 877, 2), (147, 41, 1);  -- Eucalyptus Leaves x2 + Cordage(41) → Poultice

UPDATE recipes SET tier = 2 WHERE id IN (147);    -- Poultice: DC 11 (Herbalism 4+)

-- ============================================================
-- ALLOY PROPERTIES (Phase 40 — damage/defense/durability mods per alloy material)
-- ============================================================
-- Applied when gear is crafted from named alloy ingots.
-- alloy_item_id = the ingot; mods are additive to base stats.

CREATE TABLE IF NOT EXISTS alloy_properties (
    alloy_item_id   INTEGER PRIMARY KEY,
    damage_mod      INTEGER DEFAULT 0,
    defense_mod     INTEGER DEFAULT 0,
    durability_pct  INTEGER DEFAULT 0,      -- percentage bonus (+50 = 50% more durable)
    special         TEXT DEFAULT ''          -- keyword for special effects
);

INSERT OR IGNORE INTO alloy_properties VALUES
(851,  0,  2,  20, 'impact_absorb'),    -- Manganese: tough, shield bonus
(852,  0,  0,   0, 'corrosion_proof'),   -- Chromium: no weather decay
(853,  1,  0,  50, 'edge_retention'),    -- Tungsten: hardest edge
(854,  0,  1,  10, 'cold_immune'),       -- Nickel: no cold penalty
(855,  3, -1, -10, 'brittle_hard');      -- Carbon: hardest but fragile

-- ============================================================
-- COMBAT STATS
-- ============================================================

INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(200,  3, 4, 2.5, 'hand'),
(201,  2, 3, 30.0,'hand'),
(203,  1, 2, 20.0,'hand'),
(204,  1, 5, 1.5, 'hand'),
(205,  2, 4, 15.0,'hand'),
(206,  1, 2, 1.0, 'hand'),
(811,  4, 5, 2.5, 'hand'),
(813,  5, 6, 2.5, 'hand'),
(814,  6, 7, 1.5, 'hand'),
(817,  4, 4, 1.0, 'hand'),
(820,  5, 6, 2.5, 'hand'),
(821,  8, 10, 1.5, 'hand'),
-- Phase 32: Steel weapons (+2 dmg over iron, spear has 3.0 range)
(830,  6, 8,  2.5, 'hand'),   -- Steel Axe
(831, 10, 12, 1.5, 'hand'),   -- Steel Sword
(833,  7, 8,  3.0, 'hand'),   -- Steel Spear (long range, formation)
-- Fire Carrying: Resin Torch (weak weapon, but you're swinging fire)
(871,  1, 3,  1.5, 'hand');   -- Resin Torch (low damage, fire damage potential)

INSERT OR IGNORE INTO combat_stats (item_id, defense_mod, slot) VALUES
(300,  1, 'body'),
(301,  2, 'body'),
(302,  0, 'body'),
(303,  1, 'feet'),
(304,  1, 'head'),
(305,  3, 'offhand'),
(815,  5, 'body'),
(816,  5, 'offhand'),
(822,  8, 'body'),
-- Phase 32: Steel armor + shields (+3 def over iron)
(832, 11, 'body'),    -- Steel Armor
(834,  5, 'offhand'), -- Iron Shield
(835,  8, 'offhand'), -- Steel Shield
-- Cloth & Wool garments (Iron Age Textiles Phase 1)
(415,  1, 'body'),    -- Cloth Wrap
(416,  1, 'body'),    -- Cloth Tunic
(883,  2, 'body'),    -- Wool Tunic (better warmth than cloth)
(884,  3, 'body'),    -- Wool Cloak (best non-fur warmth)
(892,  4, 'body');    -- Padded Gambeson (cloth + hide composite)

-- ============================================================
-- COMBAT MODIFIERS (Phase 3)
-- Server applies these situationally during HandleAttack.
-- ============================================================

INSERT OR IGNORE INTO combat_modifiers VALUES ('high_ground',  1,  0,  0, 'Attacker uphill by >2m');
INSERT OR IGNORE INTO combat_modifiers VALUES ('in_water',    -2, -2, -3, 'Waist-deep water');
INSERT OR IGNORE INTO combat_modifiers VALUES ('night_blind', -3,  0,  0, 'No torch at night');
INSERT OR IGNORE INTO combat_modifiers VALUES ('ambush',       4,  0,  0, 'Target unaware');
INSERT OR IGNORE INTO combat_modifiers VALUES ('wounded',     -2,  0, -1, 'Below 25% HP');
INSERT OR IGNORE INTO combat_modifiers VALUES ('cornered',     2,  0,  0, 'No escape route — desperate');
INSERT OR IGNORE INTO combat_modifiers VALUES ('home_ground',  0,  2,  0, 'Defending own territory');

-- ============================================================
-- CREATURES
-- IDs must match GetCreatureId() in species headers and spawn table in TerrainNode.cpp
-- ============================================================

INSERT OR IGNORE INTO creatures VALUES
-- id  name        hp  atk def dmg var spd  detect  aggro       pack habitat
--     model, idle, run, attack, die, size, wander, sound_near, sound_far,
--     flee_speed, flee_dist, min_idle, max_idle, vision_range, vision_angle, predator, scavenger, food_grass_wt
(1,  'Rabbit',    2,  0, 12,  0, 0,  9,  10.0, 'flee',        1, 'grassland',
     'Models/Animals/Rabbit.mdl', 'Idle', 'Run', NULL, 'Die', 0.35, 12.0, 0.5, 15.0,
     7.0, 20.0, 2.0, 6.0, 15.0, 0.4, 0, 0, 0.8),
(2,  'Deer',      8,  1, 11,  2, 4,  8,  20.0, 'flee',        3, 'forest',
     'Models/Animals/Deer.mdl', 'Idle', 'Gallop', 'Attack_Headbutt', 'Death', 1.5, 30.0, 1.0, 40.0,
     8.0, 30.0, 4.0, 10.0, 30.0, 0.4, 0, 0, 0.4),
(3,  'Fox',       5,  2, 13,  2, 4,  7,  15.0, 'flee',        1, 'forest',
     'Models/Animals/Fox.mdl', 'Idle', 'Gallop', 'Attack', 'Death', 0.8, 25.0, 0.8, 30.0,
     7.0, 25.0, 3.0, 7.0, 25.0, 0.5, 1, 1, 0.0),
(4,  'Stag',     12,  3, 12,  4, 4,  7,  25.0, 'defensive',   2, 'forest',
     'Models/Animals/Stag.mdl', 'Idle', 'Gallop', 'Attack_Headbutt', 'Death', 1.8, 35.0, 1.5, 60.0,
     9.0, 35.0, 5.0, 12.0, 35.0, 0.4, 0, 0, 0.4),
(5,  'Wolf',     12,  5, 13,  5, 4,  8,  25.0, 'aggressive',  4, 'forest',
     'Models/Animals/Wolf.mdl', 'Idle', 'Gallop', 'Attack', 'Death', 1.0, 35.0, 2.0, 80.0,
     9.0, 35.0, 3.0, 8.0, 35.0, 0.5, 1, 1, 0.0),
(6,  'Bull',     20,  4, 14,  8, 6,  5,  12.0, 'territorial', 1, 'grassland',
     'Models/Animals/Bull.mdl', 'Idle', 'Gallop', 'Attack_Headbutt', 'Death', 2.0, 20.0, 2.0, 70.0,
     7.0, 15.0, 5.0, 15.0, 25.0, 0.5, 0, 0, 0.7),
(7,  'Cow',      12,  1, 10,  2, 2,  4,  10.0, 'flee',        3, 'grassland',
     'Models/Animals/Cow.mdl', 'Idle', 'Gallop', 'Attack_Kick', 'Death', 1.8, 15.0, 1.5, 60.0,
     5.0, 20.0, 6.0, 15.0, 20.0, 0.4, 0, 0, 0.7),
(8,  'Fish',      1,  0, 14,  0, 0,  8,   5.0, 'flee',        1, 'water',
     'Models/UrhoFish.mdl', 'Swim', 'Swim', NULL, NULL, 0.2, 10.0, 0.3, 5.0,
     4.0, 10.0, 2.0, 5.0, 0.0, 0.5, 0, 0, 0.0),
(9,  'Donkey',   10,  1, 11,  3, 4,  5,  12.0, 'flee',        2, 'grassland',
     'Models/Animals/Donkey.mdl', 'Idle', 'Gallop', 'Attack_Kick', 'Death', 1.6, 20.0, 2.0, 80.0,
     6.0, 25.0, 5.0, 12.0, 25.0, 0.4, 0, 0, 0.7),
(10, 'Horse',    14,  2, 12,  4, 4,  9,  20.0, 'flee',        3, 'grassland',
     'Models/Animals/Horse.mdl', 'Idle', 'Gallop', 'Attack_Kick', 'Death', 2.2, 40.0, 2.0, 80.0,
     12.0, 45.0, 4.0, 10.0, 35.0, 0.3, 0, 0, 0.8),
(11, 'Alpaca',    8,  0, 10,  1, 2,  5,  12.0, 'flee',        4, 'mountain',
     'Models/Animals/Alpaca.mdl', 'Idle', 'Gallop', 'Attack_Kick', 'Death', 1.2, 15.0, 1.0, 40.0,
     6.0, 20.0, 4.0, 10.0, 20.0, 0.4, 0, 0, 0.7),
(12, 'Husky',     6,  3, 12,  3, 4,  8,  20.0, 'defensive',   2, 'any',
     'Models/Animals/Husky.mdl', 'Idle', 'Gallop', 'Attack', 'Death', 0.7, 20.0, 1.5, 60.0,
     8.0, 15.0, 2.0, 5.0, 25.0, 0.5, 1, 1, 0.0),
(13, 'ShibaInu',  4,  2, 11,  2, 2,  7,  15.0, 'flee',        1, 'any',
     'Models/Animals/ShibaInu.mdl', 'Idle', 'Gallop', 'Attack', 'Death', 0.5, 15.0, 1.0, 40.0,
     7.0, 15.0, 2.0, 6.0, 20.0, 0.5, 0, 0, 0.6),
(20, 'CaveMan',  15,  3, 12,  4, 4,  5,  20.0, 'defensive',   3, 'inland',
     'Models/Characters/CavemanMan.mdl', 'Caveman_idle', 'Caveman_running', 'Caveman_attack', 'Caveman_die', 1.8, 20.0, 1.5, 50.0,
     6.0, 20.0, 3.0, 8.0, 30.0, 0.5, 0, 0, 0.6),
(21, 'CaveWoman', 12,  2, 11,  3, 4,  5,  20.0, 'defensive',   3, 'inland',
     'Models/Characters/CavemanWoman.mdl', 'Caveman_idle', 'Caveman_running', 'Caveman_attack', 'Caveman_die', 1.65, 20.0, 1.5, 50.0,
     6.0, 20.0, 3.0, 8.0, 30.0, 0.5, 0, 0, 0.6);

-- ============================================================
-- LOOT TABLES
-- ============================================================

-- Rabbit (small: sinew, meat, fur, bone)
INSERT OR IGNORE INTO loot_table VALUES
(1, 20, 1, 1.0, 0),
(1, 7,  1, 1.0, 0),
(1, 24, 1, 0.8, 0),
(1, 23, 1, 0.5, 0);

-- Deer (medium herbivore: sinew, meat, hide, bone, antler, gut)
INSERT OR IGNORE INTO loot_table VALUES
(2, 20, 3, 1.0, 0),
(2, 7,  3, 1.0, 0),
(2, 21, 2, 1.0, 0),
(2, 23, 2, 0.8, 0),
(2, 27, 1, 0.7, 0),
(2, 25, 1, 0.5, 206);

-- Fox (small predator: sinew, meat, fur, bone)
INSERT OR IGNORE INTO loot_table VALUES
(3, 20, 1, 1.0, 0),
(3, 7,  1, 1.0, 0),
(3, 24, 1, 0.9, 0),
(3, 23, 1, 0.5, 0);

-- Stag (large herbivore: sinew, meat, hide, bone, antler, gut)
INSERT OR IGNORE INTO loot_table VALUES
(4, 20, 4, 1.0, 0),
(4, 7,  4, 1.0, 0),
(4, 21, 3, 1.0, 0),
(4, 23, 2, 0.8, 0),
(4, 27, 2, 0.9, 0),
(4, 25, 2, 0.6, 206);

-- Wolf (predator: sinew, meat, fur, bone, gut)
INSERT OR IGNORE INTO loot_table VALUES
(5, 20, 2, 1.0, 0),
(5, 7,  2, 1.0, 0),
(5, 24, 1, 1.0, 0),
(5, 23, 2, 0.7, 0),
(5, 25, 1, 0.6, 206);

-- Bull (large bovine: sinew, meat, hide, bone, fat, gut)
INSERT OR IGNORE INTO loot_table VALUES
(6, 20, 5, 1.0, 0),
(6, 7,  6, 1.0, 0),
(6, 21, 3, 1.0, 0),
(6, 23, 3, 0.8, 0),
(6, 26, 3, 0.9, 206),
(6, 25, 2, 0.6, 206);

-- Cow (large bovine: sinew, meat, hide, bone, fat)
INSERT OR IGNORE INTO loot_table VALUES
(7, 20, 4, 1.0, 0),
(7, 7,  5, 1.0, 0),
(7, 21, 3, 1.0, 0),
(7, 23, 2, 0.8, 0),
(7, 26, 2, 0.7, 206);

-- Fish (small: meat, bone)
INSERT OR IGNORE INTO loot_table VALUES
(8, 7,  1, 1.0, 0),
(8, 23, 1, 0.3, 0);

-- Donkey (medium: sinew, meat, hide, bone)
INSERT OR IGNORE INTO loot_table VALUES
(9, 20, 3, 1.0, 0),
(9, 7,  3, 1.0, 0),
(9, 21, 2, 1.0, 0),
(9, 23, 2, 0.7, 0);

-- Horse (large: sinew, meat, hide, bone, gut)
INSERT OR IGNORE INTO loot_table VALUES
(10, 20, 4, 1.0, 0),
(10, 7,  5, 1.0, 0),
(10, 21, 3, 1.0, 0),
(10, 23, 2, 0.8, 0),
(10, 25, 2, 0.6, 206);

-- Alpaca (medium: sinew, meat, fur/wool, bone)
INSERT OR IGNORE INTO loot_table VALUES
(11, 20, 2, 1.0, 0),
(11, 7,  2, 1.0, 0),
(11, 24, 2, 1.0, 0),
(11, 23, 1, 0.7, 0);

-- Husky (small predator: sinew, meat, fur, bone)
INSERT OR IGNORE INTO loot_table VALUES
(12, 20, 1, 1.0, 0),
(12, 7,  1, 1.0, 0),
(12, 24, 1, 0.9, 0),
(12, 23, 1, 0.5, 0);

-- ShibaInu (small: sinew, meat, fur, bone)
INSERT OR IGNORE INTO loot_table VALUES
(13, 20, 1, 1.0, 0),
(13, 7,  1, 1.0, 0),
(13, 24, 1, 0.8, 0),
(13, 23, 1, 0.4, 0);

-- ============================================================
-- GATHER SOURCES
-- ============================================================

INSERT OR IGNORE INTO gather_sources VALUES
(1, 'Loose Stone',    1,  1, 0,   'any',       -999, 999, 'Models/Props/Stone.mdl',     0,    'any'),
(2, 'Fallen Stick',   2,  1, 0,   'forest',    -999, 999, 'Models/Nature/log.mdl',     600,  'any'),
(3, 'Tall Grass',     3,  2, 0,   'grassland', -999, 10,  'Models/Props/TallGrass.mdl', 300,  'spring,summer'),
(4, 'River Clay',     4,  1, 0,   'riverbank', -999, 8,   NULL,                         1800, 'any'),
(5, 'Berry Bush',     6,  3, 0,   'forest',    -999, 15,  'Models/Nature/BushBerries_1.mdl', 3600, 'summer,autumn'),
(6, 'Flint Outcrop',  5,  2, 0,   'mountain',  15,   999, 'Models/Nature/Rock_1.mdl', 0,    'any'),
(7, 'Reed Bed',       3,  3, 0,   'water',     -999, 7,   'Models/Nature/Grass_Common_Tall.mdl',     600,  'spring,summer,autumn');

-- Trees (terrain=forest, yield=Log, tool_req=Hand Axe)
INSERT OR IGNORE INTO gather_sources VALUES
(10, 'Birch Tree',     11, 2, 100, 'forest',    -999, 999, 'Models/Nature/BirchTree_1.mdl',   0, 'any'),
(11, 'Common Tree',    11, 2, 100, 'forest',    -999, 999, 'Models/Nature/CommonTree_1.mdl',  0, 'any'),
(12, 'Pine Tree',      11, 2, 100, 'forest',    -999, 999, 'Models/Nature/PineTree_1.mdl',    0, 'any'),
(13, 'Willow Tree',    11, 2, 100, 'forest',    -999, 999, 'Models/Nature/Willow_1.mdl',      0, 'any'),
(14, 'Palm Tree',      11, 2, 100, 'forest',    -999, 999, 'Models/Nature/PalmTree_1.mdl',    0, 'any'),
(15, 'Twisted Tree',   11, 2, 100, 'forest',    -999, 999, 'Models/Nature/TwistedTree_1.mdl', 0, 'any'),
(16, 'Oak Tree',       11, 2, 100, 'forest',    -999, 999, 'Models/Nature/tree_oak.mdl',      0, 'any');

-- Rocks (terrain=any, yield=Rough Stone, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(20, 'Boulder',        1,  2, 0,   'any',       -999, 999, 'Models/Nature/Rock_1.mdl',        0, 'any'),
(21, 'Medium Rock',    1,  1, 0,   'any',       -999, 999, 'Models/Nature/Rock_Medium_1.mdl', 0, 'any'),
(22, 'River Pebbles',  1,  1, 0,   'riverbank', -999, 8,   'Models/Nature/Pebble_Round_1.mdl',0, 'any');

-- Bushes (terrain=grassland, yield=Plant Fiber, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(30, 'Wild Bush',      3,  2, 0,   'grassland', -999, 15,  'Models/Nature/Bush_1.mdl',        1800, 'any'),
(31, 'Common Bush',    3,  2, 0,   'grassland', -999, 15,  'Models/Nature/Bush_Common.mdl',   1800, 'any'),
(32, 'Flowering Bush', 3,  1, 0,   'grassland', -999, 15,  'Models/Nature/Bush_Common_Flowers.mdl', 1800, 'spring,summer');

-- Mushrooms (terrain=forest, yield=Wild Mushroom, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(40, 'Common Mushroom',    730, 1, 0, 'forest',   -999, 15,  'Models/Nature/Mushroom_Common.mdl',     3600, 'autumn'),
(41, 'Red Mushroom',       730, 1, 0, 'forest',   -999, 15,  'Models/Nature/mushroom_red.mdl',        3600, 'summer,autumn'),
(42, 'Tan Mushroom',       730, 1, 0, 'forest',   -999, 15,  'Models/Nature/mushroom_tan.mdl',        3600, 'summer,autumn'),
(43, 'Chicken of Woods',   730, 2, 0, 'forest',   -999, 15,  'Models/Nature/Mushroom_Laetiporus.mdl', 3600, 'autumn');

-- Ferns (terrain=forest, yield=Plant Fiber, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(50, 'Forest Fern',    3,  1, 0,   'forest',    -999, 15,  'Models/Nature/Fern_1.mdl',        600, 'spring,summer,autumn');

-- Water plants (terrain=water, yield=Plant Fiber, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(60, 'Cattail',        3,  2, 0,   'water',     -999, 7,   'Models/Pond/CatTail_1.mdl',       1200, 'spring,summer,autumn'),
(61, 'Water Hyacinth', 3,  1, 0,   'water',     -999, 7,   'Models/Pond/Water_Hyacinth_1.mdl',1200, 'summer'),
(62, 'Water Lily',     3,  1, 0,   'water',     -999, 7,   'Models/Pond/Water_Lily_Leaf_1.mdl',1200, 'summer'),
(63, 'Water Mint',     3,  1, 0,   'water',     -999, 7,   'Models/Pond/Water_Mint_1.mdl',    1200, 'spring,summer'),
(64, 'Swamp Calla',    3,  1, 0,   'water',     -999, 7,   'Models/Pond/Swamp_Calla_1.mdl',   1200, 'spring,summer');

-- KayKit Trees (stylized, terrain=forest, yield=Log, tool_req=Hand Axe)
INSERT OR IGNORE INTO gather_sources VALUES
(70, 'KayKit Tree 1',      11, 2, 100, 'forest',    -999, 999, 'Models/KayKit/Tree_1_A_Color1.mdl',      0, 'any'),
(71, 'KayKit Tree 2',      11, 2, 100, 'forest',    -999, 999, 'Models/KayKit/Tree_2_A_Color1.mdl',      0, 'any'),
(72, 'KayKit Tree 3',      11, 2, 100, 'forest',    -999, 999, 'Models/KayKit/Tree_3_A_Color1.mdl',      0, 'any'),
(73, 'KayKit Tree 4',      11, 2, 100, 'forest',    -999, 999, 'Models/KayKit/Tree_4_A_Color1.mdl',      0, 'any'),
(74, 'KayKit Dead Tree 1', 11, 1, 100, 'forest',    -999, 999, 'Models/KayKit/Tree_Bare_1_A_Color1.mdl', 0, 'any'),
(75, 'KayKit Dead Tree 2', 11, 1, 100, 'forest',    -999, 999, 'Models/KayKit/Tree_Bare_2_A_Color1.mdl', 0, 'any');

-- KayKit Bushes (stylized, terrain=grassland, yield=Plant Fiber, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(76, 'KayKit Bush 1',      3,  2, 0,   'grassland', -999, 15,  'Models/KayKit/Bush_1_A_Color1.mdl',      1800, 'any'),
(77, 'KayKit Bush 2',      3,  2, 0,   'grassland', -999, 15,  'Models/KayKit/Bush_2_A_Color1.mdl',      1800, 'any'),
(78, 'KayKit Bush 3',      3,  2, 0,   'grassland', -999, 15,  'Models/KayKit/Bush_3_A_Color1.mdl',      1800, 'any'),
(79, 'KayKit Bush 4',      3,  2, 0,   'grassland', -999, 15,  'Models/KayKit/Bush_4_A_Color1.mdl',      1800, 'any');

-- KayKit Rocks (stylized, terrain=any, yield=Rough Stone, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(80, 'KayKit Rock 1',      1,  2, 0,   'any',       -999, 999, 'Models/KayKit/Rock_1_A_Color1.mdl',      0, 'any'),
(81, 'KayKit Rock 2',      1,  1, 0,   'any',       -999, 999, 'Models/KayKit/Rock_2_A_Color1.mdl',      0, 'any'),
(82, 'KayKit Rock 3',      1,  2, 0,   'any',       -999, 999, 'Models/KayKit/Rock_3_A_Color1.mdl',      0, 'any');

-- KayKit Grass (stylized, terrain=grassland, yield=Plant Fiber, no tool)
INSERT OR IGNORE INTO gather_sources VALUES
(83, 'KayKit Grass 1',     3,  2, 0,   'grassland', -999, 10,  'Models/KayKit/Grass_1_A_Color1.mdl',     300, 'spring,summer'),
(84, 'KayKit Grass 2',     3,  2, 0,   'grassland', -999, 10,  'Models/KayKit/Grass_2_A_Color1.mdl',     300, 'spring,summer');

-- Desert biome gather sources (terrain='desert' = ECO_BARREN areas)
-- Cactus yields Plant Fiber (item 3), no tool needed, slow respawn
INSERT OR IGNORE INTO gather_sources VALUES
(90, 'Barrel Cactus',      3,  2, 0,   'desert', -999, 999, 'Models/Nature/Cactus_1.mdl',        3600, 'any'),
(91, 'Tall Cactus',        3,  2, 0,   'desert', -999, 999, 'Models/Nature/Cactus_2.mdl',        3600, 'any'),
(92, 'Column Cactus',      3,  1, 0,   'desert', -999, 999, 'Models/Nature/Cactus_3.mdl',        3600, 'any'),
(93, 'Flowering Cactus',   3,  3, 0,   'desert', -999, 999, 'Models/Nature/CactusFlower_1.mdl',  3600, 'any'),
(94, 'Pink Cactus',        3,  3, 0,   'desert', -999, 999, 'Models/Nature/CactusFlowers_2.mdl', 3600, 'any');

-- Desert palm trees yield Log (item 11), tool required (Hand Axe = 100)
INSERT OR IGNORE INTO gather_sources VALUES
(95, 'Desert Palm 1',      11, 2, 100, 'desert', -999, 999, 'Models/Nature/PalmTree_1.mdl',      0, 'any'),
(96, 'Desert Palm 2',      11, 2, 100, 'desert', -999, 999, 'Models/Nature/PalmTree_2.mdl',      0, 'any'),
(97, 'Desert Palm 3',      11, 2, 100, 'desert', -999, 999, 'Models/Nature/PalmTree_3.mdl',      0, 'any'),
(98, 'Desert Palm 4',      11, 2, 100, 'desert', -999, 999, 'Models/Nature/PalmTree_4.mdl',      0, 'any');

-- Desert sandstone yields Rough Stone (item 1), no tool needed
INSERT OR IGNORE INTO gather_sources VALUES
(99, 'Desert Sandstone',   1,  2, 0,   'desert', -999, 999, 'Models/Nature/Rock_1.mdl',          0, 'any');

-- Desert dead trees yield Softwood (item 15), no tool needed (dry brittle wood)
INSERT OR IGNORE INTO gather_sources VALUES
(100, 'Dead Birch',         15, 2, 0,   'desert', -999, 999, 'Models/Nature/BirchTree_Dead_1.mdl',  0, 'any'),
(101, 'Dead Oak',           15, 2, 0,   'desert', -999, 999, 'Models/Nature/CommonTree_Dead_1.mdl', 0, 'any'),
(102, 'Dead Willow',        15, 2, 0,   'desert', -999, 999, 'Models/Nature/Willow_Dead_1.mdl',     0, 'any');

-- ============================================================
-- TRAP RULES
-- ============================================================

-- trap_id, creature_id, difficulty, bait_item, radius, cooldown
INSERT OR IGNORE INTO trap_rules VALUES
(400, 1,  2,  0, 20.0, 60.0),   -- Simple Snare: Rabbit
(400, 3,  8,  0, 15.0, 90.0),   -- Simple Snare: Fox
(400, 13, 6,  0, 15.0, 90.0),   -- Simple Snare: ShibaInu
(401, 5, 10,  7, 40.0, 180.0),  -- Meat Trap: Wolf
(401, 12, 8,  7, 30.0, 120.0),  -- Meat Trap: Husky
(402, 2,  6,  6, 40.0, 120.0),  -- Berry Trap: Deer
(402, 7,  6,  6, 30.0, 120.0),  -- Berry Trap: Cow
(402, 11, 4,  6, 25.0, 90.0),   -- Berry Trap: Alpaca
(403, 8,  2,  0, 10.0, 60.0),   -- Fish Trap: Fish
(404, 4,  5,  6, 25.0, 120.0),  -- Deadfall: Stag
(404, 2,  4,  6, 25.0, 90.0),   -- Deadfall: Deer
(404, 6, 10,  7, 20.0, 300.0),  -- Deadfall: Bull
(405, 5,  6,  7, 20.0, 180.0),  -- Pit Trap: Wolf
(405, 10, 8,  6, 25.0, 150.0);  -- Pit Trap: Horse

-- Cage trap rules (live_capture = 1 — tames on collection)
-- (trap_id, creature_id, hold_strength, bait_id, attract_range, attract_time, live_capture)
INSERT OR IGNORE INTO trap_rules VALUES
-- Small Cage (406): rabbits, foxes, shiba inu — small animals, berry/no bait
(406, 1,  4,  6, 20.0, 90.0,  1),   -- Rabbit (berries)
(406, 3, 10,  7, 15.0, 120.0, 1),   -- Fox (meat)
(406, 13, 8,  7, 15.0, 120.0, 1),   -- ShibaInu (meat)
(406, 12,10,  7, 20.0, 120.0, 1),   -- Husky (meat)
-- Large Cage (407): deer, alpaca, cow — medium animals, berry bait
(407, 2,  8,  6, 30.0, 150.0, 1),   -- Deer (berries)
(407, 7,  8,  6, 25.0, 120.0, 1),   -- Cow (berries)
(407, 11, 6,  6, 25.0, 120.0, 1),   -- Alpaca (berries)
(407, 9, 10,  6, 25.0, 150.0, 1),   -- Donkey (berries)
-- Iron Cage (408): horse, stag, wolf, bull — large/dangerous, meat or berry
(408, 10,12,  6, 30.0, 180.0, 1),   -- Horse (berries)
(408, 4, 10,  6, 30.0, 150.0, 1),   -- Stag (berries)
(408, 5, 14,  7, 25.0, 180.0, 1),   -- Wolf (meat — dangerous!)
(408, 6, 16,  7, 20.0, 300.0, 1);   -- Bull (meat — very dangerous!)

-- ============================================================
-- FOOD PROPERTIES
-- ============================================================

INSERT OR IGNORE INTO food_properties VALUES
(6,  5,  1,  0,    300,   0, 0),
(7,  15, 3,  0,    180,   0, 0.1),
(8,  20, 5,  0.1,  1800,  1, 0),
(9,  12, 3,  0,    86400, 1, 0),
(10, 8,  2,  0,    120,   0, 0),
-- Medical consumables (no hunger, health-only)
(417, 0, 10, 0,    0,     0, 0),   -- Cloth Bandage: +10 HP
(878, 0, 20, 0,    0,     0, 0);   -- Eucalyptus Poultice: +20 HP

-- ============================================================
-- CLIMATE RULES
-- ============================================================

INSERT OR IGNORE INTO climate_rules VALUES
('spring', 'day',   15.0, 2.0, 3.0),
('spring', 'night', 8.0,  3.0, 4.0),
('summer', 'day',   25.0, 1.0, 2.0),
('summer', 'night', 15.0, 2.0, 3.0),
('autumn', 'day',   10.0, 3.0, 4.0),
('autumn', 'night', 3.0,  4.0, 5.0),
('winter', 'day',   2.0,  5.0, 6.0),
('winter', 'night', -5.0, 7.0, 8.0);

-- ============================================================
-- CLOTHING WARMTH
-- ============================================================

INSERT OR IGNORE INTO clothing_warmth VALUES
(300, 3.0, 0.1),
(301, 4.0, 0.2),
(302, 8.0, 0.3),
(303, 2.0, 0.1),
(304, 2.0, 0.1);

-- ============================================================
-- BUILDING RULES
-- ============================================================

INSERT OR IGNORE INTO building_rules VALUES
(600, 0.8,  0, 0, 3.0, 2.0, 0,  10.0, 0),
(601, 0.85, 0, 0, 4.0, 4.0, 10, 20.0, 1),
(602, 0.7,  0, 0, 1.5, 1.5, 0,  15.0, 0),
(603, 0.8,  0, 0, 2.0, 1.0, 3,  0,    0),
(604, 0.85, 0, 0, 2.0, 2.0, 0,  0,    0),
(605, 0.85, 0, 0, 2.0, 2.0, 15, 0,    0),
(606, 0.8,  1, 0, 2.0, 3.0, 4,  0,    0),
(607, 0.5,  1, 0, 4.0, 2.0, 5,  0,    0),
(608, 0.8,  0, 0, 2.0, 2.0, 0,  0,    0);

-- Tool/weapon/armor durability values (uses before breaking)
UPDATE items SET durability = 50  WHERE id = 100;  -- Hand Axe
UPDATE items SET durability = 30  WHERE id = 101;  -- Scraper
UPDATE items SET durability = 40  WHERE id = 102;  -- Digging Stick
UPDATE items SET durability = 20  WHERE id = 103;  -- Bone Needle
UPDATE items SET durability = 25  WHERE id = 104;  -- Fire Kit
UPDATE items SET durability = 40  WHERE id = 105;  -- Fishing Rod
UPDATE items SET durability = 60, tier = 1  WHERE id = 106;  -- Stone Pick
UPDATE items SET durability = 50  WHERE id = 107;  -- Hammer Stone
UPDATE items SET durability = 40  WHERE id = 200;  -- Spear
UPDATE items SET durability = 60  WHERE id = 201;  -- Bow
UPDATE items SET durability = 30  WHERE id = 203;  -- Sling
UPDATE items SET durability = 50  WHERE id = 204;  -- Club
UPDATE items SET durability = 15  WHERE id = 205;  -- Javelin
UPDATE items SET durability = 30  WHERE id = 206;  -- Knife
UPDATE items SET durability = 30  WHERE id = 300;  -- Hide Wrap
UPDATE items SET durability = 40  WHERE id = 301;  -- Leather Tunic
UPDATE items SET durability = 40  WHERE id = 302;  -- Fur Cloak
UPDATE items SET durability = 30  WHERE id = 303;  -- Hide Boots
UPDATE items SET durability = 30  WHERE id = 304;  -- Leather Cap
UPDATE items SET durability = 60  WHERE id = 305;  -- Wood Shield

-- Metal tools/weapons/armor — tougher than stone/leather equivalents
UPDATE items SET durability = 70  WHERE id = 810;  -- Copper Axe
UPDATE items SET durability = 60  WHERE id = 811;  -- Copper Spear
UPDATE items SET durability = 100 WHERE id = 812;  -- Bronze Axe
UPDATE items SET durability = 90  WHERE id = 813;  -- Bronze Spear
UPDATE items SET durability = 80  WHERE id = 814;  -- Bronze Sword
UPDATE items SET durability = 120 WHERE id = 815;  -- Bronze Armor
UPDATE items SET durability = 100 WHERE id = 816;  -- Bronze Shield
UPDATE items SET durability = 60  WHERE id = 817;  -- Bronze Knife
UPDATE items SET durability = 150 WHERE id = 820;  -- Iron Axe
UPDATE items SET durability = 120 WHERE id = 821;  -- Iron Sword
UPDATE items SET durability = 180 WHERE id = 822;  -- Iron Armor
-- Phase 32: Steel durability (+30% over iron)
UPDATE items SET durability = 195 WHERE id = 830;  -- Steel Axe
UPDATE items SET durability = 156 WHERE id = 831;  -- Steel Sword
UPDATE items SET durability = 234 WHERE id = 832;  -- Steel Armor
UPDATE items SET durability = 156 WHERE id = 833;  -- Steel Spear
UPDATE items SET durability = 150 WHERE id = 834;  -- Iron Shield
UPDATE items SET durability = 195 WHERE id = 835;  -- Steel Shield

-- Food decay times (game days until spoiled — 0 means no decay)
UPDATE items SET decay_time = 7.0  WHERE id = 6;    -- Berries (1 week)
UPDATE items SET decay_time = 5.0  WHERE id = 7;    -- Raw Meat
UPDATE items SET decay_time = 10.0 WHERE id = 8;    -- Cooked Meat
UPDATE items SET decay_time = 30.0 WHERE id = 9;    -- Dried Meat
UPDATE items SET decay_time = 3.0  WHERE id = 10;   -- Small Fish (raw)
UPDATE items SET decay_time = 5.0  WHERE id = 13;   -- Milk
UPDATE items SET decay_time = 50.0 WHERE id = 710;  -- Wheat Grain (long shelf life)

-- ============================================================
-- ITEM MODELS — Wire existing 3D models to item definitions
-- ============================================================

-- Stone Age tools — promoted to Models/Tools/ (correct pickup visuals)
UPDATE items SET model = 'Models/Tools/axe.mdl'              WHERE id = 100;  -- Hand Axe
UPDATE items SET model = 'Models/Tools/knife.mdl'            WHERE id = 101;  -- Scraper (knife model)
UPDATE items SET model = 'Models/Tools/chisel.mdl'           WHERE id = 103;  -- Bone Needle (chisel model)
UPDATE items SET model = 'Models/Tools/pickaxe.mdl'          WHERE id = 106;  -- Stone Pick
UPDATE items SET model = 'Models/Tools/hammer.mdl'           WHERE id = 107;  -- Hammer Stone
UPDATE items SET model = 'Models/Tools/torch.mdl'            WHERE id = 108;  -- Torch (unlit)
UPDATE items SET model = 'Models/Tools/torch.mdl'            WHERE id = 109;  -- Burning Torch

-- Stone Age weapons
UPDATE items SET model = 'Models/Weapons/Spear.mdl'          WHERE id = 200;  -- Spear
UPDATE items SET model = 'Models/Weapons/Bow_Wooden.mdl'     WHERE id = 201;  -- Bow
UPDATE items SET model = 'Models/Weapons/Arrow.mdl'          WHERE id = 202;  -- Arrow
UPDATE items SET model = 'Models/Weapons/Dagger.mdl'         WHERE id = 206;  -- Knife

-- Copper Age
UPDATE items SET model = 'Models/Weapons/Axe_Small.mdl'      WHERE id = 810;  -- Copper Axe (small, primitive)
UPDATE items SET model = 'Models/Weapons/Spear.mdl'          WHERE id = 811;  -- Copper Spear

-- Bronze Age
UPDATE items SET model = 'Models/Megakit/Axe_Bronze.mdl'     WHERE id = 812;  -- Bronze Axe
UPDATE items SET model = 'Models/Weapons/Spear.mdl'          WHERE id = 813;  -- Bronze Spear
UPDATE items SET model = 'Models/Megakit/Sword_Bronze.mdl'   WHERE id = 814;  -- Bronze Sword
UPDATE items SET model = 'Models/Weapons/Shield_Round.mdl'   WHERE id = 816;  -- Bronze Shield
UPDATE items SET model = 'Models/Megakit/Table_Knife.mdl'    WHERE id = 817;  -- Bronze Knife

-- Iron Age
UPDATE items SET model = 'Models/Weapons/Axe.mdl'            WHERE id = 820;  -- Iron Axe
UPDATE items SET model = 'Models/Weapons/Sword.mdl'          WHERE id = 821;  -- Iron Sword
UPDATE items SET model = 'Models/Weapons/Shield_Round_2.mdl' WHERE id = 834;  -- Iron Shield (upgraded round)

-- Steel Age
UPDATE items SET model = 'Models/Weapons/Axe_Double.mdl'     WHERE id = 830;  -- Steel Axe (double-headed)
UPDATE items SET model = 'Models/Weapons/Sword_2.mdl'        WHERE id = 831;  -- Steel Sword (upgraded model)
UPDATE items SET model = 'Models/Weapons/Spear.mdl'          WHERE id = 833;  -- Steel Spear
UPDATE items SET model = 'Models/Weapons/Shield_Heater.mdl'  WHERE id = 835;  -- Steel Shield (heater shape)

-- Resources: raw ores
UPDATE items SET model = 'Models/Resources/Copper_Nugget_Large.mdl' WHERE id = 800;  -- Copper Ore
UPDATE items SET model = 'Models/Resources/Copper_Nugget_Medium.mdl' WHERE id = 801; -- Tin Ore
UPDATE items SET model = 'Models/Resources/Iron_Nugget_Large.mdl'   WHERE id = 802;  -- Iron Ore
UPDATE items SET model = 'Models/Resources/Gold_Nugget_Large.mdl'   WHERE id = 803;  -- Gold Nugget
UPDATE items SET model = 'Models/Resources/Iron_Nugget_Large.mdl'    WHERE id = 840;  -- Manganese Ore (dark heavy chunks)
UPDATE items SET model = 'Models/Resources/Silver_Nugget_Large.mdl'  WHERE id = 841;  -- Chromium Ore (silvery)
UPDATE items SET model = 'Models/Resources/Gold_Nugget_Medium.mdl'   WHERE id = 842;  -- Tungsten Ore (dense golden-grey)
UPDATE items SET model = 'Models/Resources/Silver_Nugget_Medium.mdl' WHERE id = 843;  -- Nickel Ore (pale metallic)
UPDATE items SET model = 'Models/Resources/Iron_Nuggets.mdl'         WHERE id = 844;  -- Rich Carbon (dark aggregate)

-- Resources: metal bars / ingots
UPDATE items SET model = 'Models/Resources/Copper_Bar.mdl'   WHERE id = 804;  -- Copper Ingot
UPDATE items SET model = 'Models/Resources/Copper_Bar.mdl'   WHERE id = 805;  -- Tin Ingot (reuse copper bar)
UPDATE items SET model = 'Models/Resources/Iron_Bar.mdl'     WHERE id = 806;  -- Bronze Ingot
UPDATE items SET model = 'Models/Resources/Iron_Bar.mdl'     WHERE id = 807;  -- Iron Ingot
UPDATE items SET model = 'Models/Resources/Iron_Bar.mdl'     WHERE id = 825;  -- Steel Ingot
UPDATE items SET model = 'Models/Resources/Iron_Bars.mdl'     WHERE id = 850;  -- Mystery Alloy Ingot (dark multi-bar)
UPDATE items SET model = 'Models/Resources/Iron_Bars.mdl'     WHERE id = 851;  -- Manganese Steel Ingot (dark multi-bar)
UPDATE items SET model = 'Models/Resources/Silver_Bars.mdl'   WHERE id = 852;  -- Chromium Steel Ingot (shiny multi-bar)
UPDATE items SET model = 'Models/Resources/Iron_Bars.mdl'     WHERE id = 853;  -- Tungsten Steel Ingot (dark multi-bar)
UPDATE items SET model = 'Models/Resources/Silver_Bars.mdl'   WHERE id = 854;  -- Nickel Steel Ingot (shiny multi-bar)
UPDATE items SET model = 'Models/Resources/Gold_Bars.mdl'     WHERE id = 855;  -- Carbon Steel Ingot (golden multi-bar)
UPDATE items SET model = 'Models/Resources/Gold_Bars.mdl'     WHERE id = 860;  -- Hybrid Alloy Ingot (golden multi-bar)

-- Resources: stone, wood
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Large.mdl' WHERE id = 1;  -- Rough Stone
UPDATE items SET model = 'Models/Resources/Wood_Log_A.mdl'   WHERE id = 11;   -- Log
UPDATE items SET model = 'Models/Resources/Wood_Plank_A.mdl' WHERE id = 12;   -- Plank

-- Character items — promoted from Staging/CharacterItems/
UPDATE items SET model = 'Models/CharacterItems/Backpack.mdl', tier = 1 WHERE id = 500;  -- Leather Bag (backpack visual)
UPDATE items SET model = 'Models/CharacterItems/Barrel.mdl'   WHERE id = 504;  -- Barrel (wooden storage)
UPDATE items SET model = 'Models/Buildings/StoneRing.mdl'     WHERE id = 602;  -- Stone Ring (fire pit visual)

-- ============================================================
-- Iron Age Textiles — Wool Production (Phase 1)
-- ============================================================
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, description, tier) VALUES
    (880, 'Wool',        'material', 20, 0.3, 0, 0.0, 'Raw wool sheared from alpaca.', 3),
    (881, 'Wool Thread', 'material', 20, 0.2, 0, 0.0, 'Spun wool thread. Ready for weaving.', 3),
    (882, 'Wool Cloth',  'material', 10, 0.5, 0, 0.0, 'Woven wool fabric. Warm and durable.', 3),
    (883, 'Wool Tunic',  'clothing',  1, 1.0, 3, 0.0, 'Warm wool tunic. Superior to cloth.', 3),
    (884, 'Wool Cloak',  'clothing',  1, 1.5, 3, 0.0, 'Heavy wool cloak. Best non-fur warmth.', 3);

-- Recipes: Wool Thread (spin 3 wool → 2 thread, no station)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (160, 'Wool Thread', 881, 2, 3.0, 0, 0, 3, 'Spin raw wool into thread.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (160, 880, 3, 1);

-- Recipes: Wool Cloth (4 wool thread at Loom → 1 cloth)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (161, 'Wool Cloth', 882, 1, 4.0, 0, 91, 3, 'Weave wool thread into cloth at loom.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (161, 881, 4, 1);

-- Recipes: Wool Tunic (2 wool cloth + 1 thread at Loom)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (162, 'Wool Tunic', 883, 1, 5.0, 0, 91, 3, 'Sew wool tunic at loom. Warm garment.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (162, 882, 2, 1),
    (162, 881, 1, 1);

-- Recipes: Wool Cloak (3 wool cloth + 1 hide + 1 thread at Loom)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (163, 'Wool Cloak', 884, 1, 6.0, 0, 91, 3, 'Heavy wool cloak lined with hide. Best warmth.');
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (163, 882, 3, 1),
    (163, 21, 1, 1),
    (163, 881, 1, 1);

-- Clothing warmth values for wool garments
INSERT OR IGNORE INTO clothing_warmth (item_id, warmth) VALUES
    (883, 12),   -- Wool Tunic: better than Cloth Tunic
    (884, 18);   -- Wool Cloak: best non-fur warmth (Fur Cloak = 20)

-- ============================================================
-- Iron Age Textiles Phase 2 — Natural Dyes
-- ============================================================
-- Dye materials (crafted from existing resources)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, description, tier) VALUES
    (885, 'Berry Dye',       'material', 10, 0.2, 0, 0.0, 'Deep red-purple pigment from crushed berries.', 3),
    (886, 'Ochre Dye',       'material', 10, 0.3, 0, 0.0, 'Warm earth-tone pigment from clay deposits.', 3),
    (887, 'Charcoal Dye',    'material', 10, 0.2, 0, 0.0, 'Dark black pigment from ground charcoal.', 3);

-- Dyed cloth variants (wool cloth + dye)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, description, tier) VALUES
    (888, 'Red Dyed Cloth',  'material', 10, 0.5, 0, 0.0, 'Wool cloth dyed berry red. For garments and banners.', 3),
    (889, 'Ochre Dyed Cloth','material', 10, 0.5, 0, 0.0, 'Wool cloth dyed warm ochre. Earth tones.', 3),
    (890, 'Black Dyed Cloth','material', 10, 0.5, 0, 0.0, 'Wool cloth dyed charcoal black. Status garment.', 3);

-- Recipes: Dye production (no station needed — mortar and pestle implied)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (164, 'Berry Dye',       885, 2, 2.0, 0, 0, 3, 'Crush berries into pigment paste.'),
    (165, 'Ochre Dye',       886, 2, 2.0, 0, 0, 3, 'Grind clay into ochre pigment.'),
    (166, 'Charcoal Dye',    887, 3, 2.0, 0, 0, 3, 'Grind charcoal into black pigment.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (164, 6,  5, 1),    -- Berries x5 → Berry Dye x2
    (165, 4,  3, 1),    -- Clay x3 → Ochre Dye x2
    (166, 43, 2, 1);    -- Charcoal x2 → Charcoal Dye x3

-- Recipes: Dyeing cloth (wool cloth + dye at loom)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (167, 'Red Dyed Cloth',   888, 1, 3.0, 0, 91, 3, 'Dye wool cloth berry red at loom.'),
    (168, 'Ochre Dyed Cloth', 889, 1, 3.0, 0, 91, 3, 'Dye wool cloth warm ochre at loom.'),
    (169, 'Black Dyed Cloth', 890, 1, 3.0, 0, 91, 3, 'Dye wool cloth charcoal black at loom.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (167, 882, 1, 1), (167, 885, 1, 1),  -- Wool Cloth x1 + Berry Dye x1
    (168, 882, 1, 1), (168, 886, 1, 1),  -- Wool Cloth x1 + Ochre Dye x1
    (169, 882, 1, 1), (169, 887, 1, 1);  -- Wool Cloth x1 + Charcoal Dye x1

-- Tier assignments for dye recipes
UPDATE recipes SET tier = 3 WHERE id IN (164, 165, 166, 167, 168, 169);

-- ============================================================
-- Iron Age Textiles Phase 3 — Advanced Garments (Weaving 5+)
-- ============================================================
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, description, tier) VALUES
    (891, 'Canvas',          'material',  5, 1.0, 0, 0.0, 'Heavy woven fabric. Tents, sails, bedrolls.', 4),
    (892, 'Padded Gambeson', 'armor',     1, 2.5, 4, 0.0, 'Layered wool padding. Light armor, good warmth.', 4),
    (893, 'Bedroll',         'furniture', 1, 2.0, 3, 0.0, 'Portable sleeping mat. Warmth bonus near campfire.', 4),
    (894, 'Banner',          'decoration',1, 0.8, 2, 0.0, 'Dyed cloth on a pole. Settlement morale +2.', 4);

-- Recipes: Advanced garments (Loom required, Weaving 5+ implied by tier 4)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (170, 'Canvas',          891, 1, 5.0, 0, 91, 4, 'Heavy-weave cloth at loom. Durable fabric.'),
    (171, 'Padded Gambeson', 892, 1, 8.0, 103, 91, 4, 'Layer wool cloth and thread into padded armor.'),
    (172, 'Bedroll',         893, 1, 6.0, 103, 91, 4, 'Sew canvas and wool into a sleeping roll.'),
    (173, 'Banner',          894, 1, 4.0, 0, 91, 4, 'Mount dyed cloth on a stick. Settlement pride.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (170, 882, 3, 1), (170, 881, 2, 1),                -- Wool Cloth x3 + Wool Thread x2 → Canvas
    (171, 882, 4, 1), (171, 881, 3, 1), (171, 24, 2, 1), -- Wool Cloth x4 + Thread x3 + Fur x2 → Gambeson
    (172, 891, 2, 1), (172, 882, 1, 1),                -- Canvas x2 + Wool Cloth x1 → Bedroll
    (173, 888, 1, 1), (173, 2, 2, 1);                  -- Red Dyed Cloth x1 + Stick x2 → Banner

-- Clothing warmth for advanced garments
INSERT OR IGNORE INTO clothing_warmth (item_id, warmth) VALUES
    (892, 14);   -- Padded Gambeson: warmth + light armor

-- Bedroll warmth bonus handled in AuthServer (campfire proximity check)
-- Banner morale bonus handled in AuthServer (settlement buff)

UPDATE recipes SET tier = 4 WHERE id IN (170, 171, 172, 173);

-- ============================================================
-- Iron Age Textiles Phase 4 — Sail & Tapestry (Weaving 5+/7+)
-- ============================================================
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, description, tier) VALUES
    (895, 'Sail',     'material',   1, 3.0, 5, 0.0, 'Canvas sail. Attach to canoe for faster travel.', 5),
    (896, 'Tapestry', 'decoration', 1, 2.0, 8, 0.0, 'Woven tapestry depicting settlement history. Morale +5.', 7);

-- Recipes: Sail (Canvas x2 + Cordage x2 at Loom, Weaving 5+)
--          Tapestry (Dyed Cloth x3 at Loom, Weaving 7+)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, craft_time, tool_req, station_req, tier, description) VALUES
    (174, 'Sail',     895, 1, 8.0, 0, 91, 5, 'Stitch canvas panels into a boat sail. Loom required.'),
    (175, 'Tapestry', 896, 1, 12.0, 103, 91, 7, 'Weave dyed cloth into a decorative tapestry. Master work.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity, consumed) VALUES
    (174, 891, 2, 1), (174, 41, 2, 1),                 -- Canvas x2 + Cordage x2 → Sail
    (175, 888, 1, 1), (175, 889, 1, 1), (175, 890, 1, 1); -- Red + Ochre + Black Dyed Cloth → Tapestry

UPDATE recipes SET tier = 5 WHERE id = 174;
UPDATE recipes SET tier = 7 WHERE id = 175;

-- Sail speed bonus handled in AuthServer (FISHING_BOAT_BONUS doubled when sail present)
-- Tapestry morale bonus handled in AuthServer (settlement buff +5, description from settlement_history)

-- Props / Resources (Log/Plank/Stone models set in weapons/resources section above)
UPDATE items SET model = 'Models/Props/Bag.mdl'              WHERE id = 24;   -- Fur
UPDATE items SET model = 'Models/Props/Rope_1.mdl'           WHERE id = 41;   -- Cordage
-- (Hammer Stone model set to Models/Tools/hammer.mdl above — workbench removed)

-- ============================================================
-- ITEM MODEL WIRING — Phase 2: Map existing on-disk assets
-- Assets sourced from: Megakit, Survival, Props, CharacterItems,
-- Buildings, Pond, Nature, Resources folders
-- ============================================================

-- Raw materials
UPDATE items SET model = 'Models/Nature/log.mdl'                     WHERE id = 2;    -- Stick
UPDATE items SET model = 'Models/Nature/Grass_Common_Tall.mdl'       WHERE id = 3;    -- Plant Fiber
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 4;    -- Clay
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 5;    -- Flint
UPDATE items SET model = 'Models/Props/Rope_1.mdl'                   WHERE id = 20;   -- Sinew
UPDATE items SET model = 'Models/CharacterItems/Pouch_Large.mdl'     WHERE id = 21;   -- Hide
UPDATE items SET model = 'Models/CharacterItems/Pouch_Large.mdl'     WHERE id = 22;   -- Leather
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 23;   -- Bone (placeholder)
UPDATE items SET model = 'Models/Props/Rope_2.mdl'                   WHERE id = 25;   -- Gut
UPDATE items SET model = 'Models/Megakit/SmallBottle.mdl'            WHERE id = 26;   -- Fat
UPDATE items SET model = 'Models/Nature/log.mdl'                     WHERE id = 27;   -- Antler (placeholder)
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 28;   -- Tusk (placeholder)
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 29;   -- Claw (placeholder)
UPDATE items SET model = 'Models/Nature/Plant_1.mdl'                 WHERE id = 30;   -- Feather (placeholder)
UPDATE items SET model = 'Models/Nature/Grass_Common_Tall.mdl'       WHERE id = 711;  -- Flax Fiber

-- Processed materials
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 40;   -- Sharp Stone
UPDATE items SET model = 'Models/Survival/rope_bundle_A.mdl'         WHERE id = 42;   -- Rope
UPDATE items SET model = 'Models/Resources/Stone_Brick.mdl'          WHERE id = 44;   -- Fired Clay

-- Fuel
UPDATE items SET model = 'Models/Resources/Wood_Log_B.mdl'           WHERE id = 15;   -- Softwood
UPDATE items SET model = 'Models/Resources/Wood_Log_A.mdl'           WHERE id = 16;   -- Hardwood
UPDATE items SET model = 'Models/Resources/Stone_Chunks_Small.mdl'   WHERE id = 43;   -- Charcoal

-- Food
UPDATE items SET model = 'Models/Nature/BushBerries_1.mdl'           WHERE id = 6;    -- Berries
UPDATE items SET model = 'Models/Megakit/Carrot.mdl'                 WHERE id = 7;    -- Raw Meat (placeholder)
UPDATE items SET model = 'Models/Megakit/Carrot.mdl'                 WHERE id = 8;    -- Cooked Meat (placeholder)
UPDATE items SET model = 'Models/Megakit/Carrot.mdl'                 WHERE id = 9;    -- Dried Meat (placeholder)
UPDATE items SET model = 'Models/UrhoFish.mdl'                       WHERE id = 10;   -- Small Fish
UPDATE items SET model = 'Models/Megakit/Bottle_1.mdl'               WHERE id = 13;   -- Milk
UPDATE items SET model = 'Models/Nature/Wheat.mdl'                   WHERE id = 710;  -- Wheat Grain
UPDATE items SET model = 'Models/Nature/BushBerries_2.mdl'           WHERE id = 712;  -- Fresh Berries

-- Seeds
UPDATE items SET model = 'Models/Nature/Wheat.mdl'                   WHERE id = 700;  -- Wheat Seed
UPDATE items SET model = 'Models/Nature/Plant_1.mdl'                 WHERE id = 701;  -- Flax Seed
UPDATE items SET model = 'Models/Nature/BushBerries_1.mdl'           WHERE id = 702;  -- Berry Bush Seed

-- Tools
UPDATE items SET model = 'Models/Survival/trowel.mdl'               WHERE id = 102;  -- Digging Stick
UPDATE items SET model = 'Models/Survival/handdrill.mdl'             WHERE id = 104;  -- Fire Kit
UPDATE items SET model = 'Models/CharacterItems/FishingRod.mdl'      WHERE id = 105;  -- Fishing Rod
UPDATE items SET model = 'Models/CharacterItems/Chain_Coil.mdl'      WHERE id = 418;  -- Fishing Net
UPDATE items SET model = 'Models/CharacterItems/Chain_Coil.mdl'      WHERE id = 419;  -- Cargo Net

-- Weapons
UPDATE items SET model = 'Models/Survival/rope_bundle_B.mdl'         WHERE id = 203;  -- Sling
UPDATE items SET model = 'Models/Survival/mallet.mdl'                WHERE id = 204;  -- Club
UPDATE items SET model = 'Models/Weapons/Spear.mdl'                  WHERE id = 205;  -- Javelin
UPDATE items SET model = 'Models/Tools/torch.mdl'                    WHERE id = 871;  -- Resin Torch

-- Traps
UPDATE items SET model = 'Models/CharacterItems/BearTrap_Open.mdl'   WHERE id = 400;  -- Simple Snare
UPDATE items SET model = 'Models/CharacterItems/BearTrap_Open.mdl'   WHERE id = 401;  -- Meat Trap
UPDATE items SET model = 'Models/CharacterItems/BearTrap_Open.mdl'   WHERE id = 402;  -- Berry Trap
UPDATE items SET model = 'Models/CharacterItems/Cage_Small.mdl'      WHERE id = 403;  -- Fish Trap
UPDATE items SET model = 'Models/CharacterItems/BearTrap_Closed.mdl' WHERE id = 404;  -- Pit Trap
UPDATE items SET model = 'Models/CharacterItems/BearTrap_Closed.mdl' WHERE id = 405;  -- Deadfall Trap

-- Containers
UPDATE items SET model = 'Models/Megakit/FarmCrate_Empty.mdl'        WHERE id = 410;  -- Reed Basket
UPDATE items SET model = 'Models/Pond/Clay_Pot.mdl'                  WHERE id = 501;  -- Clay Pot
UPDATE items SET model = 'Models/Megakit/Bottle_1.mdl'               WHERE id = 502;  -- Clay Jar
UPDATE items SET model = 'Models/Megakit/FarmCrate_Empty.mdl'        WHERE id = 503;  -- Basket
UPDATE items SET model = 'Models/Pond/Wooden_Tub.mdl'               WHERE id = 505;  -- Wooden Tub

-- Buildings
UPDATE items SET model = 'Models/Buildings/LeanTo.mdl'               WHERE id = 600;  -- Lean-To
UPDATE items SET model = 'Models/Buildings/Hut.mdl'                  WHERE id = 601;  -- Hut
UPDATE items SET model = 'Models/Megakit/WeaponStand.mdl'            WHERE id = 603;  -- Drying Rack
UPDATE items SET model = 'Models/Buildings/Kiln.mdl'                 WHERE id = 604;  -- Kiln
UPDATE items SET model = 'Models/Buildings/StorageHut.mdl'           WHERE id = 605;  -- Storage Pit
UPDATE items SET model = 'Models/Buildings/TanningFrame.mdl'         WHERE id = 606;  -- Tanning Frame
UPDATE items SET model = 'Models/Buildings/FishWeir.mdl'             WHERE id = 607;  -- Fish Weir
UPDATE items SET model = 'Models/Buildings/CharcoalKiln.mdl'         WHERE id = 608;  -- Charcoal Kiln

-- Clothing & Armor (pickup/inventory visuals — equip appearance is separate)
UPDATE items SET model = 'Models/Props/Textiles_A.mdl'              WHERE id = 300;  -- Hide Wrap
UPDATE items SET model = 'Models/Props/Textiles_B.mdl'              WHERE id = 301;  -- Leather Tunic
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 302;  -- Fur Cloak
UPDATE items SET model = 'Models/CharacterItems/Pouch_Large.mdl'    WHERE id = 303;  -- Hide Boots
UPDATE items SET model = 'Models/CharacterItems/Pouch_Large.mdl'    WHERE id = 304;  -- Leather Cap
UPDATE items SET model = 'Models/Megakit/Shield_Wooden.mdl'         WHERE id = 305;  -- Wood Shield
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 815;  -- Bronze Armor
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 822;  -- Iron Armor
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 832;  -- Steel Armor

-- Textiles & Weaving
UPDATE items SET model = 'Models/Props/Textiles_A.mdl'              WHERE id = 411;  -- Woven Mat
UPDATE items SET model = 'Models/Props/Rope_1.mdl'                  WHERE id = 412;  -- Flax Thread
UPDATE items SET model = 'Models/Props/Textiles_B.mdl'              WHERE id = 413;  -- Rough Cloth
UPDATE items SET model = 'Models/Props/Textiles_C.mdl'              WHERE id = 414;  -- Fine Cloth
UPDATE items SET model = 'Models/Props/Textiles_A.mdl'              WHERE id = 415;  -- Cloth Wrap
UPDATE items SET model = 'Models/Props/Textiles_B.mdl'              WHERE id = 416;  -- Cloth Tunic
UPDATE items SET model = 'Models/CharacterItems/Bandages.mdl'       WHERE id = 417;  -- Cloth Bandage

-- Wool products
UPDATE items SET model = 'Models/Resources/Textiles_Stack_Small.mdl' WHERE id = 880; -- Wool
UPDATE items SET model = 'Models/Props/Rope_1.mdl'                  WHERE id = 881;  -- Wool Thread
UPDATE items SET model = 'Models/Props/Textiles_A.mdl'              WHERE id = 882;  -- Wool Cloth
UPDATE items SET model = 'Models/Props/Textiles_B.mdl'              WHERE id = 883;  -- Wool Tunic
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 884;  -- Wool Cloak

-- Dyes
UPDATE items SET model = 'Models/Megakit/Potion_1.mdl'              WHERE id = 885;  -- Berry Dye
UPDATE items SET model = 'Models/Megakit/Potion_2.mdl'              WHERE id = 886;  -- Ochre Dye
UPDATE items SET model = 'Models/Megakit/Potion_4.mdl'              WHERE id = 887;  -- Charcoal Dye

-- Dyed cloth
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large_Colored.mdl' WHERE id = 888; -- Red Dyed Cloth
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large_Colored.mdl' WHERE id = 889; -- Ochre Dyed Cloth
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large_Colored.mdl' WHERE id = 890; -- Black Dyed Cloth

-- Advanced garments & decorations
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 891;  -- Canvas
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 892;  -- Padded Gambeson
UPDATE items SET model = 'Models/Megakit/Bed_Twin1.mdl'             WHERE id = 893;  -- Bedroll
UPDATE items SET model = 'Models/Megakit/Banner_1_Cloth.mdl'        WHERE id = 894;  -- Banner
UPDATE items SET model = 'Models/Props/Textiles_Stack_Large.mdl'    WHERE id = 895;  -- Sail
UPDATE items SET model = 'Models/Megakit/Banner_2_Cloth.mdl'        WHERE id = 896;  -- Tapestry

-- Botanicals & medicine
UPDATE items SET model = 'Models/Nature/Plant_1.mdl'                WHERE id = 720;  -- Medicinal Herbs
UPDATE items SET model = 'Models/Pickups/Mushroom_Common.mdl'      WHERE id = 730;  -- Wild Mushroom
UPDATE items SET model = 'Models/Megakit/Potion_1.mdl'              WHERE id = 870;  -- Tree Resin
UPDATE items SET model = 'Models/Survival/rope_bundle_A.mdl'        WHERE id = 872;  -- Fire Bundle
UPDATE items SET model = 'Models/Props/Bucket_Wooden_1.mdl'         WHERE id = 873;  -- Bark Vessel
UPDATE items SET model = 'Models/Megakit/Potion_2.mdl'              WHERE id = 875;  -- Willow Extract
UPDATE items SET model = 'Models/Nature/Plant_1.mdl'                WHERE id = 877;  -- Eucalyptus Leaves
UPDATE items SET model = 'Models/CharacterItems/Bandages.mdl'       WHERE id = 878;  -- Eucalyptus Poultice
UPDATE items SET model = 'Models/Resources/Wood_Plank_B.mdl'        WHERE id = 879;  -- She-Oak Bark

-- ============================================================
-- FURNITURE, CONTAINERS & PROPS — Megakit models
-- Promoted from brainfarts/Props_converted 2026-05-09
-- ============================================================

-- Furniture
INSERT OR IGNORE INTO items VALUES (900, 'Bench',            'furniture', 1, 3.0,  50, 0, 'Models/Megakit/Bench.mdl',             NULL, 'Simple wooden bench. Seats two.',           1);
INSERT OR IGNORE INTO items VALUES (901, 'Stool',            'furniture', 1, 1.5,  30, 0, 'Models/Megakit/Stool.mdl',              NULL, 'Three-legged stool.',                       1);
INSERT OR IGNORE INTO items VALUES (902, 'Table',            'furniture', 1, 5.0,  60, 0, 'Models/Megakit/Table_Large.mdl',         NULL, 'Large wooden table.',                        1);
INSERT OR IGNORE INTO items VALUES (903, 'Wall Shelf',       'furniture', 1, 2.0,  30, 0, 'Models/Megakit/Shelf_Arch.mdl',          NULL, 'Arched wall shelf for display.',             1);
INSERT OR IGNORE INTO items VALUES (904, 'Simple Shelf',     'furniture', 1, 1.5,  25, 0, 'Models/Megakit/Shelf_Simple.mdl',        NULL, 'Plain shelf for storage.',                   1);
INSERT OR IGNORE INTO items VALUES (905, 'Tool Bench',       'furniture', 1, 8.0,  80, 0, 'Models/Megakit/Workbench_Drawers.mdl',   NULL, 'Workbench with drawers for crafting.',       2);

-- Containers
INSERT OR IGNORE INTO items VALUES (910, 'Wooden Crate',     'container', 1, 3.0,  40, 0, 'Models/Megakit/Crate_Wooden.mdl',        NULL, 'Rough wooden crate. Stores goods.',          1);
INSERT OR IGNORE INTO items VALUES (911, 'Metal Crate',      'container', 1, 6.0,  80, 0, 'Models/Megakit/Crate_Metal.mdl',         NULL, 'Reinforced metal crate.',                    2);
INSERT OR IGNORE INTO items VALUES (912, 'Wooden Chest',     'container', 1, 4.0,  60, 0, 'Models/Megakit/Chest_Wood.mdl',          NULL, 'Lockable wooden chest.',                     1);
INSERT OR IGNORE INTO items VALUES (913, 'Cabinet',          'container', 1, 6.0,  50, 0, 'Models/Megakit/Cabinet.mdl',             NULL, 'Tall wooden cabinet with doors.',            2);
INSERT OR IGNORE INTO items VALUES (914, 'Wooden Bucket',    'container', 5, 1.0,  20, 0, 'Models/Megakit/Bucket_Wooden_1.mdl',     NULL, 'Carries water or small goods.',              0);
INSERT OR IGNORE INTO items VALUES (915, 'Cauldron',         'container', 1, 8.0, 100, 0, 'Models/Megakit/Cauldron.mdl',            NULL, 'Iron cauldron for cooking or brewing.',      2);

-- Keys and locks
INSERT OR IGNORE INTO items VALUES (930, 'Bronze Key',       'tool',      5, 0.1,   0, 0, 'Models/Tools/nail.mdl',                  NULL, 'Small bronze key. Locks and unlocks one chest or barrel.', 4);
INSERT OR IGNORE INTO items VALUES (931, 'Iron Key',         'tool',      5, 0.1,   0, 0, 'Models/Tools/nail.mdl',                  NULL, 'Iron key. Stronger lock. Harder to break.',                5);
INSERT OR IGNORE INTO items VALUES (932, 'Bronze Lock',      'material',  3, 0.5,   0, 0, 'Models/Tools/nail.mdl',                  NULL, 'Simple bronze lock mechanism.',                             4);
INSERT OR IGNORE INTO items VALUES (933, 'Iron Lock',        'material',  3, 0.8,   0, 0, 'Models/Tools/nail.mdl',                  NULL, 'Sturdy iron lock. Resists forced entry.',                   5);

-- Props
INSERT OR IGNORE INTO items VALUES (920, 'Bronze Pickaxe',   'tool',      1, 2.0,  80, 0, 'Models/Megakit/Pickaxe_Bronze.mdl',      NULL, 'Bronze-tipped pickaxe. +60% mining yield.',  4);
INSERT OR IGNORE INTO items VALUES (923, 'Copper Pickaxe',   'tool',      1, 1.8,  60, 0, 'Models/Tools/pickaxe.mdl',               NULL, 'Copper pickaxe. +30% mining yield.',         3);
INSERT OR IGNORE INTO items VALUES (924, 'Iron Pickaxe',     'tool',      1, 2.5, 120, 0, 'Models/Tools/pickaxe.mdl',               NULL, 'Iron pickaxe. +100% mining yield.',           5);
INSERT OR IGNORE INTO items VALUES (921, 'Market Stall',     'furniture', 1, 12.0, 60, 0, 'Models/Megakit/Stall_Empty.mdl',          NULL, 'Open market stall for trading.',             2);
INSERT OR IGNORE INTO items VALUES (922, 'Market Cart',      'furniture', 1, 15.0, 80, 0, 'Models/Megakit/Stall_Cart_Empty.mdl',     NULL, 'Wheeled cart for mobile trade.',             2);

-- ============================================================
-- EPOCH PROGRESSION — Weapons & Tools from Asset Audit
-- ============================================================

-- Bronze Age tools (tier 3-4)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, model, description) VALUES
(110, 'Bronze Saw',       'tool',   1, 1.5, 80,  4, 'Models/Tools/saw.mdl',       'Toothed bronze blade. Cuts planks twice as fast.'),
(111, 'Bronze Tongs',     'tool',   1, 1.0, 100, 4, 'Models/Tools/tongs.mdl',     'Grip hot metal. Required for forge work.'),
(112, 'Bronze Handplane', 'tool',   1, 1.0, 60,  4, 'Models/Tools/handplane.mdl', 'Shaves wood smooth. Fine woodwork.'),
(113, 'Grindstone',       'tool',   1, 5.0, 200, 4, 'Models/Tools/grindstone.mdl','Sharpen blades. +1 attack to weapons sharpened here.');

-- Iron Age tools (tier 5)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, model, description) VALUES
(114, 'Iron Shovel',      'tool',   1, 2.0, 120, 5, 'Models/Tools/shovel.mdl',    'Digs faster and deeper than a stick. Mining and farming.'),
(115, 'Iron File',        'tool',   1, 0.8, 100, 5, 'Models/Tools/file.mdl',      'Smooth rough edges. Finishing tool for metalwork.'),
(116, 'Iron Shears',      'tool',   1, 0.6, 80,  5, 'Models/Tools/scissors.mdl',  'Cut cloth and shear wool faster.'),
(117, 'Iron Lantern',     'tool',   1, 1.5, 200, 5, 'Models/Tools/lantern.mdl',   'Permanent enclosed flame. Wind and rain proof.'),
(118, 'Iron Bucket',      'tool',   1, 2.0, 150, 5, 'Models/Tools/bucket_metal.mdl', 'Carry water or materials. Sturdy.'),
(119, 'Iron Nails',       'material', 20, 0.1, 0, 5, 'Models/Tools/nail.mdl',     'Fasteners. Stronger joints for buildings and furniture.');

-- Bronze Age weapons (tier 4)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, model, description) VALUES
(207, 'Composite Bow',    'weapon', 1, 1.5, 80,  4, 'Models/Weapons/Bow_Wooden2.mdl', 'Laminated wood and sinew. Greater range and power.'),
(208, 'Bronze Mace',      'weapon', 1, 2.5, 100, 4, 'Models/Weapons/Hammer_Small.mdl','Flanged bronze head. Crushes bone through armor.');

-- Iron Age weapons (tier 5)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, model, description) VALUES
(209, 'Iron Knife',       'weapon', 1, 0.8, 80,  5, 'Models/Weapons/Dagger_2.mdl',   'Hardened iron blade. Holds an edge longer.'),
(210, 'Iron War Hammer',  'weapon', 1, 4.0, 120, 5, 'Models/Weapons/Hammer_Double.mdl','Double-headed iron. Shatters shields.'),
(211, 'Iron Scythe',      'weapon', 1, 3.0, 100, 5, 'Models/Weapons/Scythe.mdl',     'Long curved blade. Harvest crops or cut down foes.');

-- Steel Age weapons (tier 6)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, model, description) VALUES
(212, 'Steel Greatsword', 'weapon', 1, 5.0, 150, 6, 'Models/Weapons/Sword_Big.mdl',  'Two-handed. Massive reach and damage. Slow.'),
(213, 'Steel War Hammer', 'weapon', 1, 5.0, 160, 6, 'Models/Weapons/Hammer_Double.mdl','Steel-headed destroyer. Nothing survives a hit.');

-- Legendary weapons (tier 7) — rare crafts or quest rewards
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, model, description) VALUES
(214, 'Golden Bow',       'weapon', 1, 1.5, 200, 7, 'Models/Weapons/Bow_Golden.mdl',  'Gold-inlaid masterwork. The finest ranged weapon.'),
(215, 'Golden Sword',     'weapon', 1, 3.0, 200, 7, 'Models/Weapons/Sword_Golden.mdl','Gilded blade. Symbol of a chieftain.'),
(216, 'Golden Shield',    'armor',  1, 6.0, 200, 7, 'Models/Weapons/Shield_Celtic_Golden.mdl', 'Ornate golden shield. Defense and prestige.'),
(217, 'Iron Heater Shield','armor', 1, 5.5, 120, 5, 'Models/Weapons/Shield_Heater_2.mdl', 'Full-body iron shield. Superior coverage.');

-- ============================================================
-- COMBAT STATS — New weapons
-- ============================================================

-- Bronze Age
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(207,  4, 5, 35.0, 'hand'),   -- Composite Bow (better than basic bow)
(208,  5, 8, 1.5,  'hand');   -- Bronze Mace (blunt, good vs armor)

-- Iron Age
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(209,  6, 5, 1.0, 'hand'),    -- Iron Knife (fast)
(210,  7, 11, 1.5, 'hand'),   -- Iron War Hammer (heavy hitter)
(211,  5, 7, 2.5, 'hand');    -- Iron Scythe (reach weapon)

-- Steel Age
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(212, 10, 14, 2.0, 'hand'),   -- Steel Greatsword (two-handed beast)
(213,  9, 13, 1.5, 'hand');   -- Steel War Hammer

-- Legendary
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(214,  8, 8, 40.0, 'hand'),   -- Golden Bow
(215, 12, 15, 1.5, 'hand');   -- Golden Sword

-- Shields (defense)
INSERT OR IGNORE INTO combat_stats (item_id, defense_mod, slot) VALUES
(216, 10, 'offhand'),          -- Golden Shield
(217,  6, 'offhand');          -- Iron Heater Shield

-- Two-handed flags
UPDATE combat_stats SET two_handed = 1 WHERE item_id IN (212, 213);

-- ============================================================
-- EQUIP SLOTS — CharacterItems wiring (tools, traps, nets)
-- Tools carried in hand; traps/nets in hand when held
-- ============================================================

INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(100,  1, 3, 1.5, 'hand'),    -- Hand Axe (tool, doubles as weak weapon)
(102,  0, 1, 2.0, 'hand'),    -- Digging Stick (tool)
(105,  0, 1, 2.5, 'hand'),    -- Fishing Rod (CharacterItems/FishingRod.mdl)
(106,  1, 2, 1.5, 'hand'),    -- Stone Pick (tool)
(107,  1, 3, 1.0, 'hand'),    -- Hammer Stone (tool)
(108,  0, 1, 1.5, 'hand'),    -- Torch (unlit)
(109,  0, 2, 1.5, 'hand'),    -- Burning Torch
(400,  0, 1, 1.5, 'hand'),    -- Simple Snare (CharacterItems/BearTrap_Open.mdl)
(401,  0, 1, 1.5, 'hand'),    -- Meat Trap (CharacterItems/BearTrap_Open.mdl)
(402,  0, 1, 1.5, 'hand'),    -- Berry Trap (CharacterItems/BearTrap_Open.mdl)
(403,  0, 1, 1.5, 'hand'),    -- Fish Trap (CharacterItems/Cage_Small.mdl)
(404,  0, 1, 1.5, 'hand'),    -- Pit Trap (CharacterItems/BearTrap_Closed.mdl)
(405,  0, 1, 1.5, 'hand'),    -- Deadfall Trap (CharacterItems/BearTrap_Closed.mdl)
(418,  0, 1, 2.0, 'hand'),    -- Fishing Net (CharacterItems/Chain_Coil.mdl)
(419,  0, 1, 2.0, 'hand');    -- Cargo Net (CharacterItems/Chain_Coil.mdl)

-- Phase 31: Pickaxes (mining tools)
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(920,  2, 3, 1.5, 'hand'),    -- Bronze Pickaxe (tier 4)
(923,  1, 2, 1.5, 'hand'),    -- Copper Pickaxe (tier 3)
(924,  3, 4, 1.5, 'hand');    -- Iron Pickaxe (tier 5)

-- Bandage is a consumable, not equippable — no combat_stats needed (id 417)
-- Containers equipped via container_bonus table (back/offhand slot)
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(500,  0, 0, 0, 'back'),      -- Leather Bag (back)
(506,  0, 0, 0, 'offhand'),   -- Hide Pouch (offhand belt)
(507,  0, 0, 0, 'back');      -- Leather Backpack (back)

-- Model paths for containers
UPDATE items SET model = 'Models/CharacterItems/Pouch_Large.mdl'  WHERE id = 506;  -- Hide Pouch
UPDATE items SET model = 'Models/CharacterItems/Backpack.mdl'     WHERE id = 507;  -- Leather Backpack

-- ============================================================
-- MODEL PATHS — Wire crafting tool models to item definitions
-- ============================================================

-- Stone Age tools (tier 0) — basic models
UPDATE items SET model = 'Models/Tools/axe.mdl'               WHERE id = 100 AND model IS NULL;
UPDATE items SET model = 'Models/Tools/chisel.mdl'             WHERE id = 101 AND model IS NULL;
UPDATE items SET model = 'Models/Survival/trowel.mdl'          WHERE id = 102 AND model IS NULL;
UPDATE items SET model = 'Models/Tools/nail.mdl'               WHERE id = 103 AND model IS NULL;
UPDATE items SET model = 'Models/Survival/handdrill.mdl'       WHERE id = 104 AND model IS NULL;
UPDATE items SET model = 'Models/CharacterItems/FishingRod.mdl' WHERE id = 105 AND model IS NULL;
UPDATE items SET model = 'Models/Tools/pickaxe.mdl'            WHERE id = 106 AND model IS NULL;
UPDATE items SET model = 'Models/Tools/hammer.mdl'             WHERE id = 107 AND model IS NULL;
UPDATE items SET model = 'Models/Tools/torch.mdl'              WHERE id = 108 AND model IS NULL;
UPDATE items SET model = 'Models/Tools/torch.mdl'              WHERE id = 109 AND model IS NULL;

-- Metal Age axe upgrades (copper/bronze/iron/steel)
UPDATE items SET model = 'Models/Weapons/Axe_Small.mdl'        WHERE id = 810 AND model IS NULL;
UPDATE items SET model = 'Models/Megakit/Axe_Bronze.mdl'       WHERE id = 812 AND model IS NULL;
UPDATE items SET model = 'Models/Weapons/Axe.mdl'              WHERE id = 820 AND model IS NULL;
UPDATE items SET model = 'Models/Weapons/Axe_Double.mdl'       WHERE id = 830 AND model IS NULL;

-- Missing equip slots for tools without combat_stats
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(101,  0, 1, 1.0, 'hand'),    -- Scraper
(103,  0, 0, 0.5, 'hand'),    -- Bone Needle
(104,  0, 1, 1.0, 'hand');    -- Fire Kit

-- Copper/Bronze Axe combat_stats (equip slot, missing from earlier wiring)
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(810,  3, 4, 2.0, 'hand'),    -- Copper Axe
(812,  4, 5, 2.0, 'hand');    -- Bronze Axe

-- ============================================================
-- RECIPES — Crafting paths for new items
-- ============================================================

-- Bronze Age tools (station_req 604 = Kiln)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(176, 'Bronze Saw',       110, 1, 107, 604, 4, 'Toothed blade for cutting planks.'),
(177, 'Bronze Tongs',     111, 1, 107, 604, 4, 'Grip hot metal safely.'),
(178, 'Bronze Handplane', 112, 1, 107, 604, 4, 'Smooth wood surfaces.'),
(179, 'Grindstone',       113, 1, 107, 0,   4, 'Sharpening wheel from stone and bronze.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(176, 806, 2), (176, 12, 1),   -- 2 bronze ingot + 1 plank
(177, 806, 2),                  -- 2 bronze ingot
(178, 806, 1), (178, 12, 1),   -- 1 bronze ingot + 1 plank
(179, 1, 10), (179, 806, 1);   -- 10 rough stone + 1 bronze ingot

-- Iron Age tools (station_req 81 = Forge)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(180, 'Iron Shovel',      114, 1, 107, 81, 5, 'Dig deep. Mine fast.'),
(181, 'Iron File',        115, 1, 107, 81, 5, 'Finishing tool for metalwork.'),
(182, 'Iron Shears',      116, 1, 107, 81, 5, 'Cut cloth and shear wool.'),
(183, 'Iron Lantern',     117, 1, 107, 81, 5, 'Enclosed flame. Windproof.'),
(184, 'Iron Bucket',      118, 1, 107, 81, 5, 'Carry water or materials.'),
(185, 'Iron Nails',       119, 10,107, 81, 5, 'Batch of nails from one ingot.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(180, 807, 2), (180, 12, 1),    -- 2 iron ingot + 1 plank
(181, 807, 1),                   -- 1 iron ingot
(182, 807, 1),                   -- 1 iron ingot
(183, 807, 2), (183, 43, 1),    -- 2 iron ingot + 1 charcoal (for glass)
(184, 807, 2),                   -- 2 iron ingot
(185, 807, 1);                   -- 1 iron ingot → 10 nails

-- Bronze Age weapons (station_req 604 = Kiln)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(186, 'Composite Bow',    207, 1, 107, 0,   4, 'Laminated bow. Sinew-backed for power.'),
(187, 'Bronze Mace',      208, 1, 107, 604, 4, 'Flanged head on a haft.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(186, 12, 2), (186, 20, 3), (186, 25, 2),  -- 2 plank + 3 sinew + 2 gut
(187, 806, 3), (187, 2, 1);                 -- 3 bronze ingot + 1 stick

-- Iron Age weapons (station_req 81 = Forge)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(188, 'Iron Knife',       209, 1, 107, 81, 5, 'Hardened iron blade.'),
(189, 'Iron War Hammer',  210, 1, 107, 81, 5, 'Double-headed iron hammer.'),
(190, 'Iron Scythe',      211, 1, 107, 81, 5, 'Long curved harvesting blade.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(188, 807, 2), (188, 22, 1),                -- 2 iron ingot + 1 leather (grip)
(189, 807, 4), (189, 11, 1),                -- 4 iron ingot + 1 log (haft)
(190, 807, 3), (190, 2, 1);                 -- 3 iron ingot + 1 stick (handle)

-- Steel Age weapons (station_req 81 = Forge)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(191, 'Steel Greatsword', 212, 1, 107, 81, 6, 'Massive two-handed blade.'),
(192, 'Steel War Hammer', 213, 1, 107, 81, 6, 'Steel-headed war hammer.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(191, 825, 5), (191, 22, 2),                -- 5 steel ingot + 2 leather (grip)
(192, 825, 4), (192, 11, 1);                -- 4 steel ingot + 1 log (haft)

-- Legendary weapons (station_req 81 = Forge, high tier)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(193, 'Golden Bow',       214, 1, 107, 81, 7, 'Gold-inlaid masterwork bow.'),
(194, 'Golden Sword',     215, 1, 107, 81, 7, 'Gilded ceremonial blade.'),
(195, 'Golden Shield',    216, 1, 107, 81, 7, 'Ornate golden shield.'),
(196, 'Iron Heater Shield', 217, 1, 107, 81, 5, 'Full-body iron shield.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(193, 825, 2), (193, 803, 3), (193, 12, 2), (193, 20, 3),  -- 2 steel + 3 gold + 2 plank + 3 sinew
(194, 825, 3), (194, 803, 5),                                -- 3 steel + 5 gold
(195, 825, 3), (195, 803, 5), (195, 22, 2),                  -- 3 steel + 5 gold + 2 leather
(196, 807, 4), (196, 22, 1);                                  -- 4 iron ingot + 1 leather

-- Phase 31: Pickaxe recipes (mining tool upgrades)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(197, 'Copper Pickaxe',  923, 1, 107, 604, 3, 'Copper-tipped pick. +30% mining yield.'),
(198, 'Bronze Pickaxe',  920, 1, 107, 604, 4, 'Bronze pick. +60% mining yield.'),
(199, 'Iron Pickaxe',    924, 1, 107, 81,  5, 'Iron pick. Double mining yield.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(197, 804, 2), (197, 2, 1), (197, 20, 2),   -- 2 copper ingot + 1 stick + 2 sinew
(198, 806, 2), (198, 2, 1), (198, 20, 2),   -- 2 bronze ingot + 1 stick + 2 sinew
(199, 807, 3), (199, 2, 1), (199, 22, 1);   -- 3 iron ingot + 1 stick + 1 leather

-- Backpack and pouch recipes
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, tier, description) VALUES
(200, 'Hide Pouch',        506, 1, 103, 1, 'Small belt pouch from hide scraps.'),
(201, 'Leather Backpack',  507, 1, 103, 2, 'Sturdy leather pack. Best carrying capacity.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(200, 21, 2), (200, 41, 1),               -- 2 hide + 1 cordage
(201, 22, 4), (201, 41, 3), (201, 2, 2);  -- 4 leather + 3 cordage + 2 sticks

-- Key and lock recipes
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(202, 'Bronze Key',   930, 1, 107, 604, 4, 'Small bronze key for locking chests.'),
(203, 'Iron Key',     931, 1, 107, 81,  5, 'Iron key. Stronger, harder to forge.'),
(204, 'Bronze Lock',  932, 1, 107, 604, 4, 'Bronze lock mechanism.'),
(205, 'Iron Lock',    933, 1, 107, 81,  5, 'Iron lock. Resists forced entry.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(202, 806, 1),                   -- 1 bronze ingot
(203, 807, 1),                   -- 1 iron ingot
(204, 806, 2),                   -- 2 bronze ingots
(205, 807, 2);                   -- 2 iron ingots

-- ============================================================
-- ROPE AND CHAIN CRAFTING — additional fiber/metal binding recipes
-- ============================================================

-- New items: chain materials + advanced cordage
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, decay_time, model, icon, description, tier) VALUES
(930, 'Chain Link',     'material', 10, 0.5,  0, 0, 'Models/CharacterItems/Chain_Coil.mdl', NULL, 'Iron chain links. Forge into chain.',              5),
(931, 'Iron Chain',     'material',  5, 2.0,  0, 0, 'Models/CharacterItems/Chain_Coil.mdl', NULL, 'Strong iron chain. Tethers, bridges, mechanisms.', 5),
(932, 'Bronze Chain',   'material',  5, 1.8,  0, 0, 'Models/CharacterItems/Chain_Coil.mdl', NULL, 'Bronze chain. Decorative and functional.',          4),
(933, 'Gut Cord',       'material', 10, 0.2,  0, 0, 'Models/Props/Rope_2.mdl',              NULL, 'Twisted gut. Stronger than plant cordage.',         1),
(934, 'Sinew Cord',     'material', 10, 0.15, 0, 0, 'Models/Props/Rope_1.mdl',              NULL, 'Braided sinew. Tough, elastic binding.',            1),
(935, 'Flax Rope',      'material',  5, 0.4,  0, 0, 'Models/Survival/rope_bundle_B.mdl',    NULL, 'Strong flax rope. Better than plant-fiber rope.',   2),
(936, 'Tether',         'tool',      1, 1.5, 40, 0, 'Models/Survival/rope_bundle_A.mdl',    NULL, 'Rope or chain tether. Tie animals to a post.',      2),
(937, 'Chain Tether',   'tool',      1, 3.0, 80, 0, 'Models/CharacterItems/Chain_Coil.mdl', NULL, 'Iron chain tether. Strong, predator-proof.',        5);

-- Recipes: chain crafting (forge-based) — IDs 220-228
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(220, 'Chain Links',       930, 4, 111, 81, 5, 'Hammer iron rod into chain links at forge.'),
(221, 'Iron Chain',        931, 1, 111, 81, 5, 'Link iron chain from forged links.'),
(222, 'Bronze Chain',      932, 1, 111, 604, 4, 'Link bronze chain from bronze ingots.'),
(223, 'Gut Cord',          933, 2, 0,   0,  1, 'Twist gut into strong cord.'),
(224, 'Sinew Cord',        934, 2, 0,   0,  1, 'Braid sinew into elastic cord.'),
(225, 'Flax Cordage',       41, 2, 0,   0,  2, 'Twist flax fiber into cordage. Better yield than plant fiber.'),
(226, 'Flax Rope',         935, 1, 0,   0,  2, 'Braid flax cordage into strong rope.'),
(227, 'Rope Tether',       936, 1, 0,   0,  2, 'Tie a rope into a tether for animals.'),
(228, 'Chain Tether',      937, 1, 111, 0,  5, 'Attach chain to a stake ring for animal tethering.');

-- Recipe inputs: chain crafting
INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(220, 807, 1),               -- Chain Links: 1 iron ingot → 4 links
(221, 930, 6),               -- Iron Chain: 6 chain links → 1 chain
(222, 806, 2),               -- Bronze Chain: 2 bronze ingots → 1 chain

-- Recipe inputs: gut/sinew cord
(223, 25, 2),                -- Gut Cord: 2 gut → 2 cord
(224, 20, 3),                -- Sinew Cord: 3 sinew → 2 cord

-- Recipe inputs: flax processing
(225, 711, 3),               -- Flax Cordage: 3 flax fiber → 2 cordage (vs 3 plant fiber → 1)
(226, 41, 3),                -- Flax Rope: 3 cordage → 1 flax rope (same as normal but flax-quality)

-- Recipe inputs: tethers
(227, 42, 2), (227, 2, 1),  -- Rope Tether: 2 rope + 1 stick
(228, 931, 1), (228, 2, 1); -- Chain Tether: 1 iron chain + 1 stick

-- Gut/Sinew Cord can substitute for Cordage in existing recipes
-- (Cordage, Gut Cord, and Sinew Cord are all tier-1 binding materials)

-- Equip slots for tethers (hand-held tools)
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(936,  0, 1, 2.0, 'hand'),   -- Rope Tether
(937,  1, 2, 2.0, 'hand');   -- Chain Tether (heavier, slight weapon)

-- ============================================================
-- CAGE TRAP RECIPES
-- ============================================================

INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(229, 'Small Cage Trap',  406, 1, 100, 0, 2, 'Wicker cage for capturing small animals alive.'),
(230, 'Large Cage Trap',  407, 1, 100, 0, 3, 'Reinforced wooden cage for medium animals.'),
(231, 'Iron Cage Trap',   408, 1, 111, 81, 5, 'Iron-barred cage for dangerous animals.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(229, 2, 6), (229, 41, 3),                -- Small Cage: 6 sticks + 3 cordage
(230, 2, 10), (230, 42, 2), (230, 22, 2), -- Large Cage: 10 sticks + 2 rope + 2 leather
(231, 807, 4), (231, 931, 1);              -- Iron Cage: 4 iron ingots + 1 iron chain

-- Cage traps are hand-carried
INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(406,  0, 1, 1.5, 'hand'),   -- Small Cage Trap
(407,  0, 1, 1.5, 'hand'),   -- Large Cage Trap
(408,  0, 2, 1.5, 'hand');   -- Iron Cage Trap

-- ============================================================
-- FOX AND WOLF HUNTING — distinct pelts, higher trade value
-- ============================================================

-- Fox Pelt and Wolf Pelt — distinct from generic Fur (item 24)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(938, 'Fox Pelt',   'raw', 5, 0.8, 'Red-orange fox fur. Prized for warmth and trade.'),
(939, 'Wolf Pelt',  'raw', 3, 1.5, 'Thick grey wolf hide. High value. Symbol of prowess.');

-- Replace generic fur drops with species-specific pelts
UPDATE loot_table SET item_id = 938 WHERE creature_id = 3 AND item_id = 24;  -- Fox: Fur → Fox Pelt
UPDATE loot_table SET item_id = 939 WHERE creature_id = 5 AND item_id = 24;  -- Wolf: Fur → Wolf Pelt

-- Fox/Wolf pelts can substitute for generic Fur in leather/clothing recipes
-- (Fox Pelt and Wolf Pelt have trade value 2x and 3x generic Fur respectively)

-- ============================================================
-- FARM BUILDING ITEMS — eggs, flour, produced by farm buildings
-- ============================================================

INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(940, 'Egg', 'food', 10, 0.2, 'Fresh egg. Eat raw or cook for better nutrition.');

-- Egg food properties (raw egg: low nutrition, cooking bonus)
INSERT OR IGNORE INTO food_properties (item_id, hunger_restore, thirst_restore, hp_restore, cook_bonus, spoil_time) VALUES
(940, 8, 0, 0, 15, 3.0);

-- MODEL PATHS section removed — it overwrote correct Phase 2 assignments
-- (Digging Stick→shovel, Fire Kit→tongs, Fishing Rod→saw were wrong.
--  Correct: trowel.mdl, handdrill.mdl, FishingRod.mdl — set at lines ~1243)

-- ============================================================
-- WINDMILL GRAIN PROCESSING — grind wheat grain into flour
-- ============================================================

-- Flour item (food/material — used in bread, stew recipes)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(713, 'Flour', 'food', 20, 0.3, 'Ground wheat. Essential for bread and pastry.');

-- Food properties: flour is not very nutritious raw but has long shelf life
INSERT OR IGNORE INTO food_properties (item_id, hunger, health, warmth, spoil_time, cooked, poison_chance) VALUES
(713, 2, 0, 0, 7200, 0, 0);

-- Recipe: Grind Flour — 4 wheat grain → 3 flour (requires Windmill station)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(200, 'Grind Flour', 713, 3, 0, 89, 3, 'Grind wheat into flour at the windmill.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(200, 710, 4);  -- 4 Wheat Grain → 3 Flour

-- Recipe: Bake Bread — 2 flour + 1 water (clay pot) → 2 bread (requires Stone Ring fire)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, description) VALUES
(714, 'Bread', 'food', 10, 0.4, 'Baked flatbread. Filling and long-lasting.');

INSERT OR IGNORE INTO food_properties (item_id, hunger, health, warmth, spoil_time, cooked, poison_chance) VALUES
(714, 25, 2, 5, 3600, 1, 0);

INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(201, 'Bake Bread', 714, 2, 0, 602, 3, 'Bake flour into bread over a fire.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(201, 713, 2), (201, 501, 1);  -- 2 Flour + 1 Clay Pot (water vessel)

-- ============================================================
-- ADVANCED METALLURGY — alloy weapon/armor recipes + hybrid alloy
-- ============================================================

-- Hybrid Alloy: combine any 2 different alloy steel ingots at Forge (Master Smith only, tier 8)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(210, 'Hybrid Alloy (Manganese+Chromium)', 860, 1, 107, 81, 8, 'Fuse two alloy steels. The apex of metallurgy.'),
(211, 'Hybrid Alloy (Tungsten+Carbon)',    860, 1, 107, 81, 8, 'Fuse two alloy steels. Extreme hardness.'),
(212, 'Hybrid Alloy (Nickel+Manganese)',   860, 1, 107, 81, 8, 'Fuse two alloy steels. Tough and weather-proof.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(210, 851, 1), (210, 852, 1),  -- Manganese Steel + Chromium Steel
(211, 853, 1), (211, 855, 1),  -- Tungsten Steel + Carbon Steel
(212, 854, 1), (212, 851, 1);  -- Nickel Steel + Manganese Steel

-- Alloy weapons — substitute alloy ingot for iron/steel in weapon recipes
-- These are tier 7 variants of existing steel weapons with alloy bonuses

-- Alloy Sword variants (output items with alloy-enhanced combat stats)
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, durability, tier, description) VALUES
(940, 'Manganese Sword',  'weapon', 1, 3.5, 180, 7, 'Impact-resistant steel blade. Absorbs shock.'),
(941, 'Chromium Sword',   'weapon', 1, 3.5, 156, 7, 'Stainless steel blade. Never rusts.'),
(942, 'Tungsten Sword',   'weapon', 1, 4.0, 234, 7, 'Ultra-hard blade. Edge lasts forever.'),
(943, 'Carbon Sword',     'weapon', 1, 3.0, 140, 7, 'Razor-sharp but brittle. Maximum cut.'),
(944, 'Hybrid Sword',     'weapon', 1, 4.0, 250, 8, 'Master-forged. The finest blade known.');

INSERT OR IGNORE INTO combat_stats (item_id, attack_mod, damage, range, slot) VALUES
(940, 10, 12, 1.5, 'hand'),  -- Manganese: same as steel but +2 def from alloy
(941, 10, 12, 1.5, 'hand'),  -- Chromium: corrosion-proof
(942, 11, 13, 1.5, 'hand'),  -- Tungsten: +1 dmg from alloy
(943, 13, 15, 1.5, 'hand'),  -- Carbon: +3 dmg but brittle
(944, 12, 14, 1.5, 'hand');  -- Hybrid: balanced apex

-- Alloy sword recipes at Forge (tier 7-8, Smithing skill required)
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(220, 'Manganese Sword', 940, 1, 107, 81, 7, 'Forge sword from tough manganese steel.'),
(221, 'Chromium Sword',  941, 1, 107, 81, 7, 'Forge stainless chromium steel blade.'),
(222, 'Tungsten Sword',  942, 1, 107, 81, 7, 'Forge ultra-hard tungsten steel blade.'),
(223, 'Carbon Sword',    943, 1, 107, 81, 7, 'Forge razor-sharp carbon steel blade.'),
(224, 'Hybrid Sword',    944, 1, 107, 81, 8, 'Forge the ultimate blade from hybrid alloy.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(220, 851, 3), (220, 2, 1), (220, 22, 1),  -- 3 Manganese Steel + stick + leather
(221, 852, 3), (221, 2, 1), (221, 22, 1),  -- 3 Chromium Steel + stick + leather
(222, 853, 3), (222, 2, 1), (222, 22, 1),  -- 3 Tungsten Steel + stick + leather
(223, 855, 3), (223, 2, 1), (223, 22, 1),  -- 3 Carbon Steel + stick + leather
(224, 860, 3), (224, 2, 1), (224, 22, 1);  -- 3 Hybrid Alloy + stick + leather

-- Model assignments for alloy weapons (reuse steel sword model)
UPDATE items SET model = 'Models/Weapons/Sword_Big.mdl' WHERE id IN (940, 941, 942, 943, 944);

-- ============================================================
-- ALCHEMY CRAFTING SYSTEM — potions, salves, tonics from herbs
-- ============================================================

-- Alchemy consumable items
INSERT OR IGNORE INTO items (id, name, category, stack_max, weight, tier, description) VALUES
(950, 'Healing Salve',    'consumable', 5, 0.3, 2, 'Herbal salve. Restores 30 HP over time.'),
(951, 'Fever Cure',       'consumable', 5, 0.3, 2, 'Willow bark tonic. Cures fever and infection.'),
(952, 'Stamina Tonic',    'consumable', 5, 0.3, 3, 'Mushroom and honey brew. Restores 50 stamina.'),
(953, 'Antidote',         'consumable', 5, 0.2, 2, 'Charcoal and herb mix. Neutralizes poison.'),
(954, 'Warming Draught',  'consumable', 5, 0.3, 3, 'Spicy root brew. +20 warmth for 5 minutes.'),
(955, 'Strength Potion',  'consumable', 3, 0.4, 4, 'Concentrated extract. +2 attack for 3 minutes.');

-- Food/consumable properties (hunger, health, warmth, spoil_time, cooked, poison_chance)
INSERT OR IGNORE INTO food_properties (item_id, hunger, health, warmth, spoil_time, cooked, poison_chance) VALUES
(950, 0, 30, 0,   1800, 0, 0),   -- Healing Salve: +30 HP, 30min shelf
(951, 0, 10, 0,   1200, 0, 0),   -- Fever Cure: +10 HP, cures status
(952, 0,  0, 0,   900,  0, 0),   -- Stamina Tonic: stamina only
(953, 0,  5, 0,   1800, 0, 0),   -- Antidote: +5 HP, cures poison
(954, 0,  0, 20,  600,  0, 0),   -- Warming Draught: +20 warmth
(955, 0,  0, 0,   600,  0, 0);   -- Strength Potion: combat buff only

-- Alchemy recipes — all require Herbalist Hut (85) as station
INSERT OR IGNORE INTO recipes (id, name, output_id, output_qty, tool_req, station_req, tier, description) VALUES
(230, 'Healing Salve',    950, 2, 0, 85, 2, 'Grind herbs with fat into healing paste.'),
(231, 'Fever Cure',       951, 2, 0, 85, 2, 'Steep willow bark. Reduces fever and infection.'),
(232, 'Stamina Tonic',    952, 1, 0, 85, 3, 'Brew mushrooms into a restorative tonic.'),
(233, 'Antidote',         953, 2, 0, 85, 2, 'Mix charcoal with herbs to absorb toxins.'),
(234, 'Warming Draught',  954, 1, 0, 85, 3, 'Steep hot roots. Warms from the inside.'),
(235, 'Strength Potion',  955, 1, 0, 85, 4, 'Concentrated herbal extract. Temporary power.');

INSERT OR IGNORE INTO recipe_inputs (recipe_id, item_id, quantity) VALUES
(230, 720, 3), (230, 24, 1),    -- 3 Medicinal Herbs + 1 Fur (rendered fat substitute)
(231, 875, 2), (231, 501, 1),   -- 2 Willow Extract + 1 Clay Pot (water vessel)
(232, 730, 3), (232, 712, 2),   -- 3 Wild Mushroom + 2 Fresh Berries (natural sugars)
(233, 720, 2), (233, 34, 1),    -- 2 Medicinal Herbs + 1 Charcoal
(234, 720, 2), (234, 870, 1),   -- 2 Medicinal Herbs + 1 Tree Resin (aromatic heat)
(235, 720, 5), (235, 875, 2);   -- 5 Medicinal Herbs + 2 Willow Extract (concentrated)

-- Model assignments for alchemy items
UPDATE items SET model = 'Models/Megakit/Potion_1.mdl' WHERE id = 950;  -- Healing Salve
UPDATE items SET model = 'Models/Megakit/Potion_2.mdl' WHERE id = 951;  -- Fever Cure
UPDATE items SET model = 'Models/Megakit/Potion_4.mdl' WHERE id = 952;  -- Stamina Tonic
UPDATE items SET model = 'Models/Megakit/Potion_1.mdl' WHERE id = 953;  -- Antidote
UPDATE items SET model = 'Models/Megakit/Potion_2.mdl' WHERE id = 954;  -- Warming Draught
UPDATE items SET model = 'Models/Megakit/Potion_4.mdl' WHERE id = 955;  -- Strength Potion
