-- Building System Seed Data

-- Tier 1: Stick and hide
INSERT OR IGNORE INTO building_types VALUES
(1,  'Windbreak',     'shelter', 1, 3.0, 1.0, 1.5, 30,  2.0, 5.0,  0, 1, 0, 'free',
     'Models/Buildings/Windbreak.mdl', 'Models/Buildings/Windbreak_Ghost.mdl',
     'Three sticks and a hide. Blocks wind. Barely shelter.'),
(2,  'Lean-To',       'shelter', 1, 3.0, 2.0, 2.0, 50,  1.5, 10.0, 2, 1, 0, 'free',
     'Models/Buildings/LeanTo.mdl', 'Models/Buildings/LeanTo_Ghost.mdl',
     'Angled roof on poles. Rain protection. Sleep for one.'),
(3,  'Hide Tent',     'shelter', 1, 4.0, 4.0, 2.5, 80,  1.0, 15.0, 4, 2, 1, 'free',
     'Models/Buildings/HideTent.mdl', 'Models/Buildings/HideTent_Ghost.mdl',
     'Proper shelter. Fits a family. Respawn point.');

-- Tier 1: Stick fence
INSERT OR IGNORE INTO building_types VALUES
(10, 'Stick Fence',   'wall',    1, 2.0, 0.3, 1.2, 20,  3.0, 0.0, 0, 0, 0, 'wall',
     'Models/Buildings/Fence.mdl', 'Models/Buildings/StickFence_Ghost.mdl',
     'Woven sticks. Marks a boundary. Won''t stop much.'),
(11, 'Stick Gate',    'gate',    1, 2.0, 0.3, 1.2, 20,  3.0, 0.0, 0, 0, 0, 'gate',
     'Models/Buildings/StickGate.mdl', 'Models/Buildings/StickGate_Ghost.mdl',
     'Opening in stick fence. Toggles open/closed.');

-- Tier 2: Wood
INSERT OR IGNORE INTO building_types VALUES
(20, 'Wood Wall',     'wall',    2, 2.0, 0.4, 2.5, 150, 0.5, 0.0, 0, 0, 0, 'wall',
     'Models/Buildings/WoodWall.mdl', 'Models/Buildings/WoodWall_Ghost.mdl',
     'Log palisade. Keeps wolves out.'),
(21, 'Wood Gate',     'gate',    2, 2.0, 0.4, 2.5, 120, 0.5, 0.0, 0, 0, 0, 'gate',
     'Models/Buildings/WoodGate.mdl', 'Models/Buildings/WoodGate_Ghost.mdl',
     'Heavy wood gate. Barred from inside.'),
(22, 'Wood Corner',   'wall',    2, 0.4, 0.4, 2.5, 200, 0.5, 0.0, 0, 0, 0, 'corner',
     'Models/Buildings/WoodCorner.mdl', 'Models/Buildings/WoodCorner_Ghost.mdl',
     'Corner post. Walls snap to this.'),
(23, 'Hut',           'shelter', 2, 5.0, 5.0, 3.0, 200, 0.3, 20.0, 6, 3, 1, 'interior',
     'Models/Buildings/Hut.mdl', 'Models/Buildings/Hut_Ghost.mdl',
     'Wattle and daub. Warm, dry. Home.'),
(24, 'Longhouse',     'shelter', 2, 10.0,5.0, 3.5, 350, 0.3, 25.0, 12,6, 1, 'interior',
     'Models/Buildings/Longhouse.mdl', 'Models/Buildings/Longhouse_Ghost.mdl',
     'Extended dwelling. Room for a clan.');

-- Tier 2: Wood utility
INSERT OR IGNORE INTO building_types VALUES
(30, 'Storage Hut',   'storage', 2, 3.0, 3.0, 2.5, 150, 0.3, 0.0, 20,0, 0, 'interior',
     'Models/Buildings/StorageHut.mdl', 'Models/Buildings/StorageHut_Ghost.mdl',
     'Keeps items dry. 20 storage slots.'),
(31, 'Workshop',      'workshop',2, 4.0, 4.0, 2.5, 150, 0.3, 5.0, 0, 0, 0, 'interior',
     'Models/Buildings/Workshop.mdl', 'Models/Buildings/Workshop_Ghost.mdl',
     'Crafting station. Recipes requiring workshop unlock here.'),
(32, 'Watchtower',    'defense', 2, 2.0, 2.0, 5.0, 200, 0.5, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/Watchtower.mdl', 'Models/Buildings/Watchtower_Ghost.mdl',
     'Elevated platform. Ranged advantage. Spot enemies further.');

-- Tier 3: Stone
INSERT OR IGNORE INTO building_types VALUES
(40, 'Stone Wall',    'wall',    3, 2.0, 0.6, 2.5, 500, 0.1, 0.0, 0, 0, 0, 'wall',
     'Models/Buildings/StoneWall.mdl', 'Models/Buildings/StoneWall_Ghost.mdl',
     'Stacked dry stone. Lasts generations.'),
(41, 'Stone Gate',    'gate',    3, 2.0, 0.6, 2.5, 400, 0.1, 0.0, 0, 0, 0, 'gate',
     'Models/Buildings/StoneGate.mdl', 'Models/Buildings/StoneGate_Ghost.mdl',
     'Heavy stone archway with wood door.'),
(42, 'Stone House',   'shelter', 3, 6.0, 6.0, 3.0, 600, 0.05,30.0, 10,4, 1, 'interior',
     'Models/Buildings/StoneHouse.mdl', 'Models/Buildings/StoneHouse_Ghost.mdl',
     'Permanent dwelling. Warm, strong, dry.');
-- (Phase 33 Iron Gate + Stone Watchtower defined by coder at ~line 119)

-- Utility buildings
INSERT OR IGNORE INTO building_types VALUES
(50, 'Stone Ring',    'utility', 1, 1.5, 1.5, 0.5, 999, 0.0, 15.0, 0, 0, 0, 'free',
     'Models/Buildings/Bonfire.mdl', 'Models/Buildings/Bonfire.mdl',
     'Fireplace. Warmth, cooking, light.'),
(51, 'Drying Rack',  'utility', 1, 2.0, 1.0, 2.0, 40,  1.0, 0.0,  3, 0, 0, 'free',
     'Models/Buildings/DryingRack.mdl', 'Models/Buildings/DryingRack_Ghost.mdl',
     'Hang meat to preserve it.'),
(52, 'Tanning Frame', 'utility',1, 2.0, 3.0, 2.0, 40,  1.0, 0.0,  4, 0, 0, 'free',
     'Models/Buildings/TanningFrame.mdl','Models/Buildings/TanningFrame_Ghost.mdl',
     'Stretch and scrape hides.'),
(53, 'Kiln',         'workshop',2, 2.0, 2.0, 2.0, 200, 0.2, 0.0,  0, 0, 0, 'free',
     'Models/Buildings/Kiln.mdl', 'Models/Buildings/Kiln_Ghost.mdl',
     'Fire clay, smelt copper.'),
(54, 'Charcoal Kiln','workshop',2, 2.0, 2.0, 2.5, 150, 0.3, 0.0,  0, 0, 0, 'free',
     'Models/Buildings/CharcoalKiln.mdl','Models/Buildings/CharcoalKiln_Ghost.mdl',
     'Slow-burn wood to charcoal.'),
(55, 'Fish Weir',    'utility', 1, 4.0, 2.0, 1.0, 60,  1.5, 0.0,  5, 0, 0, 'free',
     'Models/Buildings/FishWeir.mdl', 'Models/Buildings/FishWeir_Ghost.mdl',
     'Stone dam in stream. Passive fish and gold collection.'),
-- Fire system Phase 2b: Woodpile stores softwood + hardwood independently.
-- storageSlots field repurposed as per-wood-type capacity (20 burn-units each).
(56, 'Woodpile',     'utility', 1, 1.5, 1.0, 1.0, 80,  1.0, 0.0, 20, 0, 0, 'free',
     'Models/Buildings/Woodpile.mdl', 'Models/Buildings/Woodpile_Ghost.mdl',
     'Stack of firewood. Holds softwood and hardwood independently. Wetness from rain.');

-- Water Phase 4: Well
INSERT OR IGNORE INTO building_types VALUES
(57, 'Well',          'utility', 2, 1.5, 1.5, 1.5, 300, 0.1, 0.0,  0, 0, 0, 'free',
     'Models/Buildings/Well.mdl', 'Models/Buildings/Well_Ghost.mdl',
     'Dug well. Provides water. Yield depends on terrain — high near rivers, low on hills.');

-- Water collection
INSERT OR IGNORE INTO building_types VALUES
(58, 'Water Barrel',  'utility', 2, 1.0, 1.0, 1.5, 80,  0.5, 0.0,  0, 0, 0, 'free',
     'Models/Buildings/WaterBarrel.mdl', 'Models/Buildings/WaterBarrel_Ghost.mdl',
     'Bark and wood barrel. Collects rainwater passively. Place in the open — roofs block collection.');

-- Memorial
INSERT OR IGNORE INTO building_types VALUES
(60, 'Grave',         'memorial',0, 1.0, 1.0, 0.5, 10,  0.0, 0.0,  0, 0, 0, 'free',
     'Models/Nature/stump_round.mdl', NULL,
     'Stone cairn marking a burial. Remembers the dead.');

-- Transport
INSERT OR IGNORE INTO building_types VALUES
(70, 'Dugout Canoe',   'transport',3, 3.0, 1.5, 1.0, 100, 1.0, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/Canoe.mdl', NULL,
     'Hollowed log canoe. Allows water crossing and deep-water fishing.');

-- Mining infrastructure (Phase 31)
INSERT OR IGNORE INTO building_types VALUES
(80, 'Mine Shaft',     'industry', 3, 2.0, 2.0, 3.0, 300, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/MineShaft.mdl', 'Models/Buildings/MineShaft_Ghost.mdl',
     'Deep mining access. +50% yield within 15m. Requires Knapping 5+ and Woodwork 4+.'),
-- Phase 32: Forge — upgrade from Kiln, steel-only recipes
(81, 'Forge',          'industry', 4, 2.5, 2.5, 3.0, 400, 0.2, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/Forge.mdl', 'Models/Buildings/Forge_Ghost.mdl',
     'Advanced metalwork. Steel recipes. +2 to metal craft DC rolls.'),
-- Phase 35: Workshops — specialised buildings with craft bonuses
(82, 'Smithy',         'workshop',4, 4.0, 4.0, 3.0, 300, 0.2, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/Smithy.mdl', 'Models/Buildings/Smithy_Ghost.mdl',
     'Dedicated metalwork shop. +3 to metal craft DC.'),
(83, 'Tannery',        'workshop',3, 4.0, 3.0, 2.5, 200, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/Tannery.mdl', 'Models/Buildings/Tannery_Ghost.mdl',
     'Leather processing. +2 to leather craft DC.'),
(84, 'Granary',        'workshop',3, 5.0, 5.0, 4.0, 400, 0.1, 0.0, 0, 0, 100, 'free',
     'Models/Buildings/Granary.mdl', 'Models/Buildings/Granary_Ghost.mdl',
     'Bulk food storage. 5x slower decay. +100 food capacity.'),
(85, 'Herbalist Hut',  'workshop',2, 3.0, 3.0, 2.5, 150, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/HerbalistHut.mdl', 'Models/Buildings/HerbalistHut_Ghost.mdl',
     'Medicinal herb workshop. +50% herb yield. Heal +10 HP.');

-- Military (Phase 36)
INSERT OR IGNORE INTO building_types VALUES
(90, 'Garrison',       'military',4, 5.0, 5.0, 3.5, 350, 0.2, 10.0, 0, 4, 0, 'free',
     'Models/Buildings/Garrison.mdl', 'Models/Buildings/Garrison_Ghost.mdl',
     'Barracks for guards. Holds 4 NPCs. Guards sleep here, respond faster to threats.');

-- Iron-age defences (Phase 33)
INSERT OR IGNORE INTO building_types VALUES
(43, 'Iron Gate',        'gate',    4, 2.0, 0.6, 2.5, 300, 0.05, 0.0, 0, 0, 0, 'gate',
     'Models/Buildings/IronGate.mdl', 'Models/Buildings/IronGate_Ghost.mdl',
     'Iron-reinforced gate. Predators cannot break through.'),
(33, 'Stone Watchtower', 'defense', 3, 2.5, 2.5, 6.0, 400, 0.1,  0.0, 0, 0, 0, 'free',
     'Models/Buildings/StoneWatchtower.mdl', 'Models/Buildings/StoneWatchtower_Ghost.mdl',
     'Taller stone tower. 80m detection radius. Spot enemies earlier.');

-- Loom (Weaving station, Phase 3 textiles)
INSERT OR IGNORE INTO building_types VALUES
(91, 'Loom',            'workshop',2, 2.5, 1.5, 2.0, 100, 0.5, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/Loom.mdl', 'Models/Buildings/Loom_Ghost.mdl',
     'Wooden frame loom. Weave fine cloth, nets, and garments.');

-- Storage Barrel (placeable, interactable container)
INSERT OR IGNORE INTO building_types VALUES
(92, 'Storage Barrel',  'storage', 1, 1.0, 1.0, 1.5, 60,  0.3, 0.0,  10, 0, 0, 'free',
     'Models/CharacterItems/Barrel.mdl', NULL,
     'Wooden barrel. 10-slot storage. Place near workshop or home.');

-- Palisade defence (sharpened log wall, stronger wood variant)
INSERT OR IGNORE INTO building_types VALUES
(26, 'Palisade Wall',  'wall',    2, 2.0, 0.3, 3.0, 180, 0.4, 0.0, 0, 0, 0, 'wall',
     'Models/Buildings/PalisadeWall.mdl', 'Models/Buildings/PalisadeWall_Ghost.mdl',
     'Sharpened log stakes. Taller than plank walls. Deters climbers.');

-- Deep Well (tier 3 stone well, upgrade from basic Well)
INSERT OR IGNORE INTO building_types VALUES
(59, 'Deep Well',       'utility', 3, 2.0, 2.0, 2.0, 400, 0.05, 0.0, 0, 0, 0, 'free',
     'Models/Buildings/WaterWell.mdl', NULL,
     'Stone-lined deep well. Higher yield than wood well. Never runs dry in rain.');

-- Farming & livestock buildings
INSERT OR IGNORE INTO building_types VALUES
(86, 'Barn',            'shelter', 3, 8.0, 6.0, 4.0, 350, 0.2, 10.0, 20, 0, 0, 'free',
     'Models/Buildings/Barn.mdl', NULL,
     'Timber barn. Bulk storage for crops and materials.'),
(87, 'Silo',            'utility', 3, 2.0, 2.0, 5.0, 300, 0.1, 0.0, 40, 0, 0, 'free',
     'Models/Buildings/Silo.mdl', NULL,
     'Grain silo. Large capacity food storage with slow decay.'),
(88, 'Chicken Coop',    'utility', 2, 2.5, 2.5, 2.0, 100, 0.5, 0.0,  0, 0, 0, 'free',
     'Models/Buildings/ChickenCoop.mdl', NULL,
     'Enclosed coop. Passive egg production.'),
(89, 'Windmill',        'workshop',3, 4.0, 4.0, 6.0, 300, 0.2, 0.0,  0, 0, 0, 'free',
     'Models/Buildings/Windmill.mdl', NULL,
     'Stone windmill. Grinds grain to flour. +2 to cooking DC.');

-- Furniture & decoration (Megakit props)
INSERT OR IGNORE INTO building_types VALUES
(100, 'Chest',           'furniture',  2, 1.0, 0.5, 0.8, 80,  0.3, 0.0, 8, 0, 0, 'free',
      'Models/Megakit/Chest_Wood.mdl', NULL,
      'Wooden chest. Personal storage.'),
(101, 'Bed',             'furniture',  2, 2.0, 1.0, 0.8, 60,  0.3, 0.0, 0, 1, 0, 'free',
      'Models/Megakit/Bed_Twin1.mdl', NULL,
      'Simple bed. Sleep indoors for warmth bonus.'),
(102, 'Bed (Large)',     'furniture',  2, 2.0, 1.2, 0.8, 60,  0.3, 0.0, 0, 1, 0, 'free',
      'Models/Megakit/Bed_Twin2.mdl', NULL,
      'Wider bed. Same function, different style.'),
(103, 'Bench',           'furniture',  1, 1.5, 0.5, 0.8, 40,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Bench.mdl', NULL,
      'Wooden bench. Sit to rest stamina.'),
(104, 'Table',           'furniture',  2, 2.0, 1.0, 0.8, 60,  0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Table_Large.mdl', NULL,
      'Large table. Crafting surface.'),
(105, 'Barrel',          'furniture',  1, 0.8, 0.8, 1.2, 80,  0.3, 0.0, 6, 0, 0, 'free',
      'Models/Megakit/Barrel.mdl', NULL,
      'Storage barrel. Holds liquids or dry goods.'),
(106, 'Apple Barrel',    'furniture',  1, 0.8, 0.8, 1.2, 80,  0.3, 0.0, 6, 0, 0, 'free',
      'Models/Megakit/Barrel_Apples.mdl', NULL,
      'Barrel of apples. Food storage.'),
(107, 'Bookcase',        'furniture',  2, 1.5, 0.5, 2.0, 60,  0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/Bookcase_2.mdl', NULL,
      'Tall bookcase. Stores scrolls and knowledge.'),
(108, 'Cauldron',        'furniture',  2, 1.0, 1.0, 0.8, 200, 0.1, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Cauldron.mdl', NULL,
      'Iron cauldron. Cooking station for stews and potions.'),
(109, 'Market Stall',    'decoration', 2, 3.0, 2.0, 2.5, 80,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Stall_Empty.mdl', NULL,
      'Open market stall. Trade display.'),
(110, 'Market Cart',     'decoration', 2, 3.0, 2.0, 2.0, 80,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Stall_Cart_Empty.mdl', NULL,
      'Wheeled market cart. Mobile trade.'),
(111, 'Chair',           'furniture',  1, 0.5, 0.5, 0.8, 30,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Chair_1.mdl', NULL,
      'Wooden chair. Sit to rest.'),
(112, 'Cabinet',         'furniture',  2, 1.0, 0.5, 1.5, 60,  0.3, 0.0, 6, 0, 0, 'free',
      'Models/Megakit/Cabinet.mdl', NULL,
      'Storage cabinet. Keeps goods dry.'),
(113, 'Shelf',           'furniture',  2, 1.5, 0.4, 1.8, 40,  0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/Shelf_Simple.mdl', NULL,
      'Wall shelf. Display or storage.'),
(114, 'Arch Shelf',      'furniture',  2, 1.5, 0.4, 2.0, 50,  0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/Shelf_Arch.mdl', NULL,
      'Arched decorative shelf.'),
(115, 'Crate (Wood)',    'furniture',  1, 1.0, 1.0, 1.0, 40,  0.5, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/Crate_Wooden.mdl', NULL,
      'Wooden crate. Basic storage.'),
(116, 'Crate (Metal)',   'furniture',  2, 1.0, 1.0, 1.0, 120, 0.1, 0.0, 6, 0, 0, 'free',
      'Models/Megakit/Crate_Metal.mdl', NULL,
      'Reinforced metal crate. Secure storage.'),
(117, 'Workbench',       'furniture',  2, 2.0, 1.0, 1.0, 80,  0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Workbench.mdl', NULL,
      'Sturdy workbench. General crafting surface.'),
(118, 'Workbench (Drawers)','furniture',2, 2.0, 1.0, 1.0, 80, 0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/Workbench_Drawers.mdl', NULL,
      'Workbench with drawers. Crafting surface plus tool storage.'),
(119, 'Stool',           'furniture',  1, 0.4, 0.4, 0.6, 20,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Stool.mdl', NULL,
      'Simple stool. Sit to rest stamina.'),
(120, 'Blacksmith Equipment','furniture', 4, 2.5, 2.0, 1.5, 200, 0.1, 0.0, 0, 0, 0, 'free',
      'Models/Buildings/BlacksmithEquipment.mdl', NULL,
      'Anvil, bellows, grindstone, trough. Place near a Forge or Smithy for craft bonus.'),
(121, 'Cooking Pot',     'furniture',  1, 0.5, 0.5, 0.4, 60,  0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Pot_1.mdl', NULL,
      'Clay cooking pot. Place near a fire for stew recipes.'),
(122, 'Mug',             'decoration', 1, 0.2, 0.2, 0.3, 15,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Mug.mdl', NULL,
      'Wooden mug. Drink from it.'),
(123, 'Plate',           'decoration', 1, 0.3, 0.3, 0.1, 10,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Table_Plate.mdl', NULL,
      'Wooden plate. Serve food.'),
(124, 'Apple Crate',     'furniture',  1, 1.0, 1.0, 0.8, 40,  0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/FarmCrate_Apple.mdl', NULL,
      'Crate of apples. Food storage.'),
(125, 'Carrot Crate',    'furniture',  1, 1.0, 1.0, 0.8, 40,  0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/FarmCrate_Carrot.mdl', NULL,
      'Crate of carrots. Food storage.'),
(126, 'Anvil',           'furniture',  3, 1.0, 0.5, 0.8, 300, 0.1, 0.0, 0, 0, 0, 'free',
      'Models/Props/Anvil_Log.mdl', NULL,
      'Iron anvil on a log stump. Place near forge for metalwork.'),
(127, 'Wall Torch',      'decoration', 1, 0.2, 0.2, 0.5, 40,  0.5, 5.0, 0, 0, 0, 'free',
      'Models/Props/Torch_Metal.mdl', NULL,
      'Metal wall torch. Permanent light source.'),
(128, 'Candle',          'decoration', 1, 0.1, 0.1, 0.2, 10,  0.5, 3.0, 0, 0, 0, 'free',
      'Models/Megakit/Candle_1.mdl', NULL,
      'Simple candle. Small light source for tables and shelves.'),
(129, 'Tall Candle',     'decoration', 1, 0.1, 0.1, 0.3, 10,  0.5, 3.0, 0, 0, 0, 'free',
      'Models/Megakit/Candle_2.mdl', NULL,
      'Tall candle. Slightly more light than a short candle.'),
(130, 'Candlestick',     'decoration', 2, 0.15, 0.15, 0.4, 30, 0.3, 4.0, 0, 0, 0, 'free',
      'Models/Megakit/CandleStick.mdl', NULL,
      'Brass candlestick. Elegant light source.'),
(131, 'Candelabra',      'decoration', 3, 0.2, 0.2, 0.5, 40,  0.3, 6.0, 0, 0, 0, 'free',
      'Models/Megakit/CandleStick_Triple.mdl', NULL,
      'Three-armed candelabra. Bright interior light.'),
(132, 'Floor Candelabra', 'decoration', 3, 0.3, 0.3, 1.5, 50, 0.3, 8.0, 0, 0, 0, 'free',
      'Models/Megakit/CandleStick_Stand.mdl', NULL,
      'Standing candelabra. Lights a whole room.'),
(133, 'Wall Lantern',    'decoration', 2, 0.2, 0.2, 0.4, 60,  0.3, 6.0, 0, 0, 0, 'free',
      'Models/Megakit/Lantern_Wall.mdl', NULL,
      'Enclosed lantern. Wind-proof. Mount on walls.'),
(134, 'Chandelier',      'decoration', 4, 1.0, 1.0, 0.8, 80,  0.2, 12.0, 0, 0, 0, 'free',
      'Models/Megakit/Chandelier.mdl', NULL,
      'Hanging chandelier. Lights a great hall.'),
(135, 'Coin Pile',       'decoration', 3, 0.3, 0.3, 0.1, 999, 0.0, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Coin_Pile.mdl', NULL,
      'Heap of gold coins. Settlement wealth display.'),
(136, 'Chalice',         'decoration', 3, 0.15, 0.15, 0.3, 60, 0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Chalice.mdl', NULL,
      'Ceremonial drinking cup. Prestige item.'),
(137, 'Wagon',           'decoration', 3, 3.0, 1.5, 1.5, 100, 0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Prop_Wagon.mdl', NULL,
      'Wooden wagon. Transport goods between settlements.'),
(138, 'Scroll',          'decoration', 2, 0.2, 0.1, 0.1, 20,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Scroll_1.mdl', NULL,
      'Written scroll. Knowledge or trade record.'),
(139, 'Book Stand',      'decoration', 3, 0.5, 0.5, 1.2, 40,  0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/BookStand.mdl', NULL,
      'Wooden book stand. Display knowledge.'),
(140, 'Training Dummy',  'military',  2, 0.8, 0.8, 1.8, 80,  0.3, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Dummy.mdl', NULL,
      'Straw dummy. NPCs practice combat skills here.'),
(141, 'Weapon Rack',     'military',  2, 1.0, 0.3, 1.5, 60,  0.3, 0.0, 4, 0, 0, 'free',
      'Models/Megakit/Peg_Rack.mdl', NULL,
      'Wall-mounted peg rack. Stores weapons and tools.'),
(142, 'War Banner',      'military',  2, 0.3, 0.3, 2.0, 40,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Banner_1.mdl', NULL,
      'Wooden banner pole. Marks territory. Morale +1.'),
(143, 'Clan Banner',     'military',  3, 0.3, 0.3, 2.0, 40,  0.5, 0.0, 0, 0, 0, 'free',
      'Models/Megakit/Banner_2.mdl', NULL,
      'Tall clan banner. Settlement identity. Morale +2.'),
(144, 'Wooden Tub',      'utility',   2, 1.0, 1.0, 0.8, 60,  0.3, 0.0, 6, 0, 0, 'free',
      'Models/Pond/Wooden_Tub.mdl', NULL,
      'Carved wooden tub. Water storage, hide washing, food prep.');

-- Interior doors and windows (placed as children of shelters via auto-furnish)
INSERT OR IGNORE INTO building_types VALUES
(145, 'Wooden Door',       'decoration', 2, 1.0, 0.3, 2.0, 60, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Megakit/Door_1_Flat.mdl', NULL,
     'Wooden plank door. Blocks wind and provides privacy.'),
(146, 'Round Door',        'decoration', 2, 1.0, 0.3, 2.0, 60, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Megakit/Door_1_Round.mdl', NULL,
     'Arched wooden door. Decorative entrance.'),
(147, 'Window Shutters',   'decoration', 2, 1.0, 0.1, 1.0, 40, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Megakit/WindowShutters_Thin_Flat_Open.mdl', NULL,
     'Wooden window shutters. Lets light in, keeps rain out.'),
(148, 'Round Window',      'decoration', 2, 1.0, 0.1, 1.0, 40, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Megakit/WindowShutters_Thin_Round_Open.mdl', NULL,
     'Round window with shutters. Decorative and functional.'),
(149, 'Wall Tapestry',    'decoration', 3, 1.0, 0.1, 2.0, 60, 0.3, 0.0, 0, 0, 0, 'free',
     'Models/Megakit/Banner_2.mdl', NULL,
     'Woven tapestry depicting settlement history. Morale +5.');

-- Master Builder unlock: Longhouse (Woodwork 8+ required)
INSERT OR IGNORE INTO building_types VALUES
(25, 'Longhouse',      'shelter', 3, 10.0, 6.0, 3.5, 400, 0.3, 25.0, 16, 8, 1, 'interior',
     'Models/Buildings/Longhouse.mdl', 'Models/Buildings/Longhouse_Ghost.mdl',
     'Grand communal hall. 8 beds, 16 storage. Master Builder only.');

-- Building Recipes
-- Tier 1: Stick & hide
INSERT OR IGNORE INTO building_recipes VALUES
(1,  2, 3),  (1,  21, 1),
(2,  2, 6),  (2,  21, 2), (2,  41, 2),
(3,  2, 8),  (3,  21, 4), (3,  41, 4), (3, 20, 2),
(10, 2, 6),  (10, 3, 8),
(11, 2, 4),  (11, 3, 4), (11, 41, 2);

-- Tier 2: Wood
INSERT OR IGNORE INTO building_recipes VALUES
(20, 11, 4), (20, 42, 1),
(21, 11, 3), (21, 42, 1), (21, 22, 1),
(22, 11, 2), (22, 42, 1),
(23, 11, 8), (23, 12, 6), (23, 42, 3), (23, 4, 10),
(24, 11,16), (24, 12,12), (24, 42, 6), (24, 4, 20),
(30, 11, 6), (30, 12, 4), (30, 42, 2),
(31, 11, 6), (31, 12, 8), (31, 42, 2), (31, 1, 10),
(32, 11, 6), (32, 12, 4), (32, 42, 3);

-- Tier 3: Stone
INSERT OR IGNORE INTO building_recipes VALUES
(40, 1, 20),
(41, 1, 15), (41, 11, 2), (41, 22, 1),
(42, 1, 40), (42, 11, 8), (42, 4, 20), (42, 42, 4);

-- Utility
INSERT OR IGNORE INTO building_recipes VALUES
(50, 1, 8),
(51, 2, 4), (51, 41, 2), (51, 22, 1),
(52, 2, 4), (52, 22, 1), (52, 42, 1),
(53, 1, 15),(53, 4, 10),
(54, 1, 12),(54, 4, 6),
(55, 1, 20),(55, 11, 4),
-- Phase 2b: Woodpile recipe — sticks + plant fiber for the lashing
(56, 2, 4), (56, 3, 2),
-- Water Phase 4: Well — 10 stone + 5 softwood (Woodwork 4+)
(57, 1, 10), (57, 15, 5),
-- Rain Collection: Water Barrel — 5 planks + 2 She-Oak Bark (Woodwork 3+)
(58, 12, 5), (58, 879, 2);

-- Transport (Phase 26): Dugout Canoe — 5 planks
INSERT OR IGNORE INTO building_recipes VALUES
(70, 12, 5);

-- Mining infrastructure (Phase 31): Mine Shaft — 20 planks + 10 rough stone
INSERT OR IGNORE INTO building_recipes VALUES
(80, 12, 20), (80, 1, 10);

-- Iron-age defences (Phase 33): Iron Gate (3 iron ingots), Stone Watchtower (30 stone + 8 planks)
INSERT OR IGNORE INTO building_recipes VALUES
(43, 807, 3),
(33, 1, 30), (33, 12, 8);

-- Phase 32: Forge — 20 stone + 5 iron ingots
INSERT OR IGNORE INTO building_recipes VALUES
(81, 1, 20), (81, 807, 5);

-- Phase 35: Workshop recipes
INSERT OR IGNORE INTO building_recipes VALUES
(82, 1, 10), (82, 12, 5),        -- Smithy: 10 stone + 5 planks (near Forge)
(83, 12, 10), (83, 22, 5),       -- Tannery: 10 planks + 5 leather
(84, 12, 20), (84, 1, 10),       -- Granary: 20 planks + 10 stone
(85, 12, 10), (85, 720, 5);      -- Herbalist Hut: 10 planks + 5 medicinal herbs

-- Phase 36: Garrison — 20 planks + 10 stone + 5 iron ingots
INSERT OR IGNORE INTO building_recipes VALUES
(90, 12, 20), (90, 1, 10), (90, 807, 5);

-- Loom — 6 planks + 4 cordage + 4 sticks
INSERT OR IGNORE INTO building_recipes VALUES
(91, 12, 6), (91, 41, 4), (91, 2, 4);

-- Storage Barrel — 5 planks + 2 cordage (Woodwork 2+)
INSERT OR IGNORE INTO building_recipes VALUES
(92, 12, 5), (92, 41, 2);

-- Farm buildings (Phase 15b):
-- Barn: 30 planks + 15 stone + 5 rope (large structure, Woodwork 5+)
INSERT OR IGNORE INTO building_recipes VALUES
(86, 12, 30), (86, 1, 15), (86, 42, 5);
-- Silo: 10 planks + 15 stone (tall cylinder, Knapping 4+)
INSERT OR IGNORE INTO building_recipes VALUES
(87, 12, 10), (87, 1, 15);
-- Chicken Coop: 8 planks + 4 sticks + 2 cordage (small, Woodwork 3+)
INSERT OR IGNORE INTO building_recipes VALUES
(88, 12, 8), (88, 2, 4), (88, 41, 2);
-- Windmill: 20 planks + 20 stone + 5 rope (complex, Woodwork 6+ Knapping 5+)
INSERT OR IGNORE INTO building_recipes VALUES
(89, 12, 20), (89, 1, 20), (89, 42, 5);

-- Settlement banners — identity markers
-- War Banner: 2 sticks + 1 leather + 1 cordage (basic)
INSERT OR IGNORE INTO building_recipes VALUES
(142, 2, 2), (142, 22, 1), (142, 41, 1);
-- Clan Banner: 4 sticks + 2 leather + 2 cordage + 1 dye (bronze age)
INSERT OR IGNORE INTO building_recipes VALUES
(143, 2, 4), (143, 22, 2), (143, 41, 2);
-- Wall Tapestry: 1 tapestry item (crafted at loom)
INSERT OR IGNORE INTO building_recipes VALUES
(149, 896, 1);

-- Palisade Wall — 8 planks + 2 cordage (Woodwork 3+)
INSERT OR IGNORE INTO building_recipes VALUES
(26, 12, 8), (26, 41, 2);

-- Deep Well — 15 stone + 8 planks (Woodwork 5+, Knapping 4+)
INSERT OR IGNORE INTO building_recipes VALUES
(59, 1, 15), (59, 12, 8);

-- Blacksmith Equipment — 5 iron ingots + 3 planks (Metalwork 4+)
INSERT OR IGNORE INTO building_recipes VALUES
(120, 807, 5), (120, 12, 3);

-- Master Builder: Longhouse — 30 planks + 20 stone + 10 leather
INSERT OR IGNORE INTO building_recipes VALUES
(25, 12, 30), (25, 1, 20), (25, 22, 10);

-- Snap rules
INSERT OR IGNORE INTO snap_rules VALUES
('wall',   'wall',   2.0, 0.0, 'end'),
('wall',   'corner', 0.2, 0.0, 'end'),
('wall',   'gate',   2.0, 0.0, 'end'),
('gate',   'wall',   2.0, 0.0, 'end'),
('corner', 'wall',   0.0, 0.0, 'corner');

-- Repair costs
INSERT OR IGNORE INTO repair_costs VALUES
(20, 11, 1, 25),
(23, 11, 1, 20), (23, 4, 2, 20),
(26, 12, 1, 25),
(40, 1, 3, 50);

-- Weather damage
INSERT OR IGNORE INTO weather_damage VALUES
('storm',      1, 5.0),
('storm',      2, 2.0),
('storm',      3, 0.0),
('blizzard',   1, 8.0),
('blizzard',   2, 3.0),
('heavy_rain', 1, 2.0);

-- Wall strength vs creatures
-- Tier 1 (stick, id=10): blocks small — rabbit, fox, shiba, husky, alpaca
-- Tier 2 (wood, id=20): blocks medium — all except bull
-- Tier 3 (stone, id=40): blocks everything
INSERT OR IGNORE INTO wall_strength VALUES
-- Stick walls (tier 1): small animals only
(10, 1, 1), (10, 3, 1), (10, 11, 1), (10, 12, 1), (10, 13, 1),
-- Wood walls (tier 2): everything except bull
(20, 1, 1), (20, 2, 1), (20, 3, 1), (20, 4, 1), (20, 5, 1),
(20, 7, 1), (20, 9, 1), (20, 10, 1), (20, 11, 1), (20, 12, 1), (20, 13, 1),
-- Palisade walls (tier 2, id=26): same blocking as wood walls
(26, 1, 1), (26, 2, 1), (26, 3, 1), (26, 4, 1), (26, 5, 1),
(26, 7, 1), (26, 9, 1), (26, 10, 1), (26, 11, 1), (26, 12, 1), (26, 13, 1),
-- Stone walls (tier 3): blocks everything (higher tier than wood)
(40, 1, 1), (40, 2, 1), (40, 3, 1), (40, 4, 1), (40, 5, 1),
(40, 6, 1), (40, 7, 1), (40, 9, 1), (40, 10, 1), (40, 11, 1), (40, 12, 1), (40, 13, 1),
-- Iron Gate (tier 4, id=43): blocks ALL species — impenetrable
(43, 1, 1), (43, 2, 1), (43, 3, 1), (43, 4, 1), (43, 5, 1),
(43, 6, 1), (43, 7, 1), (43, 9, 1), (43, 10, 1), (43, 11, 1), (43, 12, 1), (43, 13, 1);

-- ============================================================
-- MODULAR BUILDING KIT (Megakit)
-- 149 snap-together construction pieces for player buildings
-- Tier 4: requires Woodwork 6+ to unlock
-- IDs 1000-1148
-- ============================================================

-- Floor pieces (12)
INSERT OR IGNORE INTO building_types VALUES
(1000, 'Floor Brick', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_Brick.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1001, 'Floor RedBrick', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_RedBrick.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1002, 'Floor UnevenBrick', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_UnevenBrick.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1003, 'Floor WoodDark', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodDark.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1004, 'Floor WoodDark Half1', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodDark_Half1.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1005, 'Floor WoodDark Half2', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodDark_Half2.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1006, 'Floor WoodDark Half3', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodDark_Half3.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1007, 'Floor WoodDark OverhangCorner', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodDark_OverhangCorner.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1008, 'Floor WoodDark OverhangCorner2', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodDark_OverhangCorner2.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1009, 'Floor WoodLight', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodLight.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1010, 'Floor WoodLight OverhangCorner', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodLight_OverhangCorner.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.'),
(1011, 'Floor WoodLight OverhangCorner2', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/Floor_WoodLight_OverhangCorner2.mdl', NULL,
     'Modular floor tile. Snap to adjacent floors.');

-- Wall pieces (20)
INSERT OR IGNORE INTO building_types VALUES
(1012, 'Wall Arch', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Arch.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1013, 'Wall BottomCover', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_BottomCover.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1014, 'Wall Plaster Door Flat', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Door_Flat.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1015, 'Wall Plaster Door Round', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Door_Round.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1016, 'Wall Plaster Door RoundInset', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Door_RoundInset.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1017, 'Wall Plaster Straight', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Straight.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1018, 'Wall Plaster Straight Base', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Straight_Base.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1019, 'Wall Plaster Straight L', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Straight_L.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1020, 'Wall Plaster Straight R', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Straight_R.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1021, 'Wall Plaster Window Thin Round', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Window_Thin_Round.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1022, 'Wall Plaster Window Wide Flat', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Window_Wide_Flat.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1023, 'Wall Plaster Window Wide Flat2', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Window_Wide_Flat2.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1024, 'Wall Plaster Window Wide Round', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_Window_Wide_Round.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1025, 'Wall Plaster WoodGrid', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_Plaster_WoodGrid.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1026, 'Wall UnevenBrick Door Flat', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_UnevenBrick_Door_Flat.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1027, 'Wall UnevenBrick Door Round', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_UnevenBrick_Door_Round.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1028, 'Wall UnevenBrick Straight', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_UnevenBrick_Straight.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1029, 'Wall UnevenBrick Window Thin Round', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_UnevenBrick_Window_Thin_Round.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1030, 'Wall UnevenBrick Window Wide Flat', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_UnevenBrick_Window_Wide_Flat.mdl', NULL,
     'Modular wall section. Snap to floor edges.'),
(1031, 'Wall UnevenBrick Window Wide Round', 'modular', 4, 2.0, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_wall',
     'Models/Megakit/Wall_UnevenBrick_Window_Wide_Round.mdl', NULL,
     'Modular wall section. Snap to floor edges.');

-- Corner pieces (8)
INSERT OR IGNORE INTO building_types VALUES
(1032, 'Corner ExteriorWide Brick', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_ExteriorWide_Brick.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1033, 'Corner ExteriorWide Wood', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_ExteriorWide_Wood.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1034, 'Corner Exterior Brick', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_Exterior_Brick.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1035, 'Corner Exterior TopDown', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_Exterior_TopDown.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1036, 'Corner Exterior TopOnly', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_Exterior_TopOnly.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1037, 'Corner Exterior Wood', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_Exterior_Wood.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1038, 'Corner Interior Big', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_Interior_Big.mdl', NULL,
     'Modular corner piece. Joins wall sections.'),
(1039, 'Corner Interior Small', 'modular', 4, 0.3, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_corner',
     'Models/Megakit/Corner_Interior_Small.mdl', NULL,
     'Modular corner piece. Joins wall sections.');

-- Door pieces (8)
INSERT OR IGNORE INTO building_types VALUES
(1040, 'Door 1 Flat', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_1_Flat.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1041, 'Door 1 Round', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_1_Round.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1042, 'Door 2 Flat', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_2_Flat.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1043, 'Door 2 Round', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_2_Round.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1044, 'Door 4 Flat', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_4_Flat.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1045, 'Door 4 Round', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_4_Round.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1046, 'Door 8 Flat', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_8_Flat.mdl', NULL,
     'Modular door. Fits in doorframe.'),
(1047, 'Door 8 Round', 'modular', 4, 1.0, 0.3, 2.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_door',
     'Models/Megakit/Door_8_Round.mdl', NULL,
     'Modular door. Fits in doorframe.');

-- DoorFrame pieces (4)
INSERT OR IGNORE INTO building_types VALUES
(1048, 'DoorFrame Flat Brick', 'modular', 4, 1.2, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_doorframe',
     'Models/Megakit/DoorFrame_Flat_Brick.mdl', NULL,
     'Modular doorframe. Sits in wall opening.'),
(1049, 'DoorFrame Flat WoodDark', 'modular', 4, 1.2, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_doorframe',
     'Models/Megakit/DoorFrame_Flat_WoodDark.mdl', NULL,
     'Modular doorframe. Sits in wall opening.'),
(1050, 'DoorFrame Round Brick', 'modular', 4, 1.2, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_doorframe',
     'Models/Megakit/DoorFrame_Round_Brick.mdl', NULL,
     'Modular doorframe. Sits in wall opening.'),
(1051, 'DoorFrame Round WoodDark', 'modular', 4, 1.2, 0.3, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_doorframe',
     'Models/Megakit/DoorFrame_Round_WoodDark.mdl', NULL,
     'Modular doorframe. Sits in wall opening.');

-- Window pieces (6)
INSERT OR IGNORE INTO building_types VALUES
(1052, 'Window Roof Thin', 'modular', 4, 1.0, 0.3, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/Window_Roof_Thin.mdl', NULL,
     'Modular window. Fits in wall opening.'),
(1053, 'Window Roof Wide', 'modular', 4, 1.0, 0.3, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/Window_Roof_Wide.mdl', NULL,
     'Modular window. Fits in wall opening.'),
(1054, 'Window Thin Flat1', 'modular', 4, 1.0, 0.3, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/Window_Thin_Flat1.mdl', NULL,
     'Modular window. Fits in wall opening.'),
(1055, 'Window Thin Round1', 'modular', 4, 1.0, 0.3, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/Window_Thin_Round1.mdl', NULL,
     'Modular window. Fits in wall opening.'),
(1056, 'Window Wide Flat1', 'modular', 4, 1.0, 0.3, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/Window_Wide_Flat1.mdl', NULL,
     'Modular window. Fits in wall opening.'),
(1057, 'Window Wide Round1', 'modular', 4, 1.0, 0.3, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/Window_Wide_Round1.mdl', NULL,
     'Modular window. Fits in wall opening.');

-- WindowShutters pieces (8)
INSERT OR IGNORE INTO building_types VALUES
(1058, 'WindowShutters Thin Flat Closed', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Thin_Flat_Closed.mdl', NULL,
     'Window shutters. Attach to window.'),
(1059, 'WindowShutters Thin Flat Open', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Thin_Flat_Open.mdl', NULL,
     'Window shutters. Attach to window.'),
(1060, 'WindowShutters Thin Round Closed', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Thin_Round_Closed.mdl', NULL,
     'Window shutters. Attach to window.'),
(1061, 'WindowShutters Thin Round Open', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Thin_Round_Open.mdl', NULL,
     'Window shutters. Attach to window.'),
(1062, 'WindowShutters Wide Flat Closed', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Wide_Flat_Closed.mdl', NULL,
     'Window shutters. Attach to window.'),
(1063, 'WindowShutters Wide Flat Open', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Wide_Flat_Open.mdl', NULL,
     'Window shutters. Attach to window.'),
(1064, 'WindowShutters Wide Round Closed', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Wide_Round_Closed.mdl', NULL,
     'Window shutters. Attach to window.'),
(1065, 'WindowShutters Wide Round Open', 'modular', 4, 1.0, 0.1, 1.0, 200, 0.2, 0.0, 0, 0, 0, 'mod_window',
     'Models/Megakit/WindowShutters_Wide_Round_Open.mdl', NULL,
     'Window shutters. Attach to window.');

-- Roof pieces (39)
INSERT OR IGNORE INTO building_types VALUES
(1066, 'Roof 2x4 RoundTile', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_2x4_RoundTile.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1067, 'Roof Dormer RoundTile', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Dormer_RoundTile.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1068, 'Roof FrontSupports', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_FrontSupports.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1069, 'Roof Front Brick2', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick2.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1070, 'Roof Front Brick4', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick4.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1071, 'Roof Front Brick4 Half L', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick4_Half_L.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1072, 'Roof Front Brick4 Half R', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick4_Half_R.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1073, 'Roof Front Brick6', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick6.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1074, 'Roof Front Brick6 Half L', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick6_Half_L.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1075, 'Roof Front Brick6 Half R', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick6_Half_R.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1076, 'Roof Front Brick8', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick8.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1077, 'Roof Front Brick8 Half L', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick8_Half_L.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1078, 'Roof Front Brick8 Half R', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Front_Brick8_Half_R.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1079, 'Roof Log', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Log.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1080, 'Roof Modular RoundTiles', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Modular_RoundTiles.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1081, 'Roof RoundTile 2x1', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTile_2x1.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1082, 'Roof RoundTile 2x1 Long', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTile_2x1_Long.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1083, 'Roof RoundTiles 4x4', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_4x4.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1084, 'Roof RoundTiles 4x6', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_4x6.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1085, 'Roof RoundTiles 4x8', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_4x8.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1086, 'Roof RoundTiles 6x10', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_6x10.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1087, 'Roof RoundTiles 6x12', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_6x12.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1088, 'Roof RoundTiles 6x14', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_6x14.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1089, 'Roof RoundTiles 6x4', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_6x4.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1090, 'Roof RoundTiles 6x6', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_6x6.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1091, 'Roof RoundTiles 6x8', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_6x8.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1092, 'Roof RoundTiles 8x10', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_8x10.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1093, 'Roof RoundTiles 8x12', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_8x12.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1094, 'Roof RoundTiles 8x14', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_8x14.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1095, 'Roof RoundTiles 8x8', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_RoundTiles_8x8.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1096, 'Roof Support2', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Support2.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1097, 'Roof Tower RoundTiles', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Tower_RoundTiles.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1098, 'Roof Wooden 2x1', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1099, 'Roof Wooden 2x1 Center', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1_Center.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1100, 'Roof Wooden 2x1 Center Mirror', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1_Center_Mirror.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1101, 'Roof Wooden 2x1 Corner', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1_Corner.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1102, 'Roof Wooden 2x1 L', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1_L.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1103, 'Roof Wooden 2x1 Middle', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1_Middle.mdl', NULL,
     'Modular roof section. Sits atop walls.'),
(1104, 'Roof Wooden 2x1 R', 'modular', 4, 2.0, 2.0, 1.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_roof',
     'Models/Megakit/Roof_Wooden_2x1_R.mdl', NULL,
     'Modular roof section. Sits atop walls.');

-- Overhang pieces (20)
INSERT OR IGNORE INTO building_types VALUES
(1105, 'Overhang Plaster Corner', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Plaster_Corner.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1106, 'Overhang Plaster Corner Front', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Plaster_Corner_Front.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1107, 'Overhang Plaster Long', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Plaster_Long.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1108, 'Overhang Plaster Short', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Plaster_Short.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1109, 'Overhang RoofIncline Plaster', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_RoofIncline_Plaster.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1110, 'Overhang RoofIncline UnevenBricks', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_RoofIncline_UnevenBricks.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1111, 'Overhang Roof Plaster', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Roof_Plaster.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1112, 'Overhang Roof UnevenBricks', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Roof_UnevenBricks.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1113, 'Overhang Side Plaster Long L', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_Plaster_Long_L.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1114, 'Overhang Side Plaster Long R', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_Plaster_Long_R.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1115, 'Overhang Side Plaster Short L', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_Plaster_Short_L.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1116, 'Overhang Side Plaster Short R', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_Plaster_Short_R.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1117, 'Overhang Side UnevenBrick Long L', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_UnevenBrick_Long_L.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1118, 'Overhang Side UnevenBrick Long R', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_UnevenBrick_Long_R.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1119, 'Overhang Side UnevenBrick Short L', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_UnevenBrick_Short_L.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1120, 'Overhang Side UnevenBrick Short R', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_Side_UnevenBrick_Short_R.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1121, 'Overhang UnevenBrick Corner', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_UnevenBrick_Corner.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1122, 'Overhang UnevenBrick Corner Front', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_UnevenBrick_Corner_Front.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1123, 'Overhang UnevenBrick Long', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_UnevenBrick_Long.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.'),
(1124, 'Overhang UnevenBrick Short', 'modular', 4, 2.0, 0.5, 0.3, 200, 0.2, 0.0, 0, 0, 0, 'mod_overhang',
     'Models/Megakit/Overhang_UnevenBrick_Short.mdl', NULL,
     'Decorative overhang. Attaches to upper wall.');

-- HoleCover pieces (5)
INSERT OR IGNORE INTO building_types VALUES
(1125, 'HoleCover 90Angle', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/HoleCover_90Angle.mdl', NULL,
     'Floor hole cover. Stairwell access.'),
(1126, 'HoleCover 90Half', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/HoleCover_90Half.mdl', NULL,
     'Floor hole cover. Stairwell access.'),
(1127, 'HoleCover 90Stairs', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/HoleCover_90Stairs.mdl', NULL,
     'Floor hole cover. Stairwell access.'),
(1128, 'HoleCover Straight', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/HoleCover_Straight.mdl', NULL,
     'Floor hole cover. Stairwell access.'),
(1129, 'HoleCover StraightHalf', 'modular', 4, 2.0, 2.0, 0.2, 200, 0.2, 0.0, 0, 0, 0, 'mod_floor',
     'Models/Megakit/HoleCover_StraightHalf.mdl', NULL,
     'Floor hole cover. Stairwell access.');

-- Stair pieces (4)
INSERT OR IGNORE INTO building_types VALUES
(1130, 'Stair Interior Rails', 'modular', 4, 1.0, 2.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stair_Interior_Rails.mdl', NULL,
     'Interior staircase. Connects floors.'),
(1131, 'Stair Interior Simple', 'modular', 4, 1.0, 2.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stair_Interior_Simple.mdl', NULL,
     'Interior staircase. Connects floors.'),
(1132, 'Stair Interior Solid', 'modular', 4, 1.0, 2.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stair_Interior_Solid.mdl', NULL,
     'Interior staircase. Connects floors.'),
(1133, 'Stair Interior SolidExtended', 'modular', 4, 1.0, 2.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stair_Interior_SolidExtended.mdl', NULL,
     'Interior staircase. Connects floors.');

-- Stairs pieces (15)
INSERT OR IGNORE INTO building_types VALUES
(1134, 'Stairs Exterior NoFirstStep', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_NoFirstStep.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1135, 'Stairs Exterior Platform', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Platform.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1136, 'Stairs Exterior Platform45', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Platform45.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1137, 'Stairs Exterior Platform45Clean', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Platform45Clean.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1138, 'Stairs Exterior PlatformU', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_PlatformU.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1139, 'Stairs Exterior SidePlatform', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_SidePlatform.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1140, 'Stairs Exterior Sides', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Sides.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1141, 'Stairs Exterior Sides45', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Sides45.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1142, 'Stairs Exterior SidesU', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_SidesU.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1143, 'Stairs Exterior SingleSide', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_SingleSide.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1144, 'Stairs Exterior SingleSideThick', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_SingleSideThick.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1145, 'Stairs Exterior Straight', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Straight.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1146, 'Stairs Exterior Straight Center', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Straight_Center.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1147, 'Stairs Exterior Straight L', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Straight_L.mdl', NULL,
     'Exterior staircase. Ground to upper level.'),
(1148, 'Stairs Exterior Straight R', 'modular', 4, 1.5, 3.0, 2.5, 200, 0.2, 0.0, 0, 0, 0, 'mod_stair',
     'Models/Megakit/Stairs_Exterior_Straight_R.mdl', NULL,
     'Exterior staircase. Ground to upper level.');

-- Modular snap rules
INSERT OR IGNORE INTO snap_rules VALUES
('mod_wall',   'mod_wall',   2.0, 0.0, 'end'),
('mod_wall',   'mod_corner', 0.3, 0.0, 'corner'),
('mod_corner', 'mod_wall',   0.0, 0.0, 'corner'),
('mod_floor',  'mod_floor',  2.0, 0.0, 'end'),
('mod_wall',   'mod_floor',  1.0, 0.0, 'end'),
('mod_roof',   'mod_wall',   1.0, 0.0, 'end'),
('mod_stair',  'mod_floor',  1.0, 0.0, 'end');

-- ============================================================
-- MODULAR BUILDING CONSTRUCTION — recipes for snap-together pieces
-- ============================================================
-- One recipe per modular snap type (base variant). Tier 3 = Bronze Age+.
-- All require Woodwork 4+ (implicit via tier/DC check).

INSERT OR IGNORE INTO building_recipes VALUES
-- Floor: 4 planks + 2 stone
(1000, 12, 4), (1000, 1, 2),
-- Wall: 3 planks + 1 stone
(1012, 12, 3), (1012, 1, 1),
-- Corner: 2 planks + 1 stone
(1032, 12, 2), (1032, 1, 1),
-- Door: 3 planks + 1 cordage
(1040, 12, 3), (1040, 41, 1),
-- Doorframe: 2 planks
(1048, 12, 2),
-- Window: 2 planks + 1 stick
(1052, 12, 2), (1052, 2, 1),
-- Roof: 4 planks + 1 cordage
(1066, 12, 4), (1066, 41, 1),
-- Overhang: 2 planks
(1105, 12, 2),
-- Stair: 4 planks + 2 sticks
(1130, 12, 4), (1130, 2, 2);

-- Modular snap rules additions: walls↔floors, roofs↔walls
INSERT OR IGNORE INTO snap_rules VALUES
('mod_floor',  'mod_wall',   1.0, 0.0, 'end'),
('mod_wall',   'mod_door',   0.0, 0.0, 'end'),
('mod_wall',   'mod_doorframe', 0.0, 0.0, 'end'),
('mod_wall',   'mod_window', 0.0, 0.0, 'end'),
('mod_roof',   'mod_wall',   0.0, 2.5, 'end'),
('mod_stair',  'mod_floor',  0.0, 0.0, 'end'),
('mod_overhang','mod_wall',  2.0, 0.0, 'end');
