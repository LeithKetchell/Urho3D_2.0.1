// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

namespace Urho3D
{

/// Client->server: send VariantMap of identity and authentication data.
static const int MSG_IDENTITY = 0x87;
/// Client->server: send controls (buttons and mouse movement).
static const int MSG_CONTROLS = 0x88;
/// Client->server: scene has been loaded and client is ready to proceed.
static const int MSG_SCENELOADED = 0x89;
/// Client->server: request a package file.
static const int MSG_REQUESTPACKAGE = 0x8A;

/// Server->client: package file data fragment.
static const int MSG_PACKAGEDATA = 0x8B;
/// Server->client: load new scene. In case of empty filename the client should just empty the scene.
static const int MSG_LOADSCENE = 0x8C;
/// Server->client: wrong scene checksum, can not participate.
static const int MSG_SCENECHECKSUMERROR = 0x8D;
/// Server->client: create new node.
static const int MSG_CREATENODE = 0x8E;
/// Server->client: node delta update.
static const int MSG_NODEDELTAUPDATE = 0x8F;
/// Server->client: node latest data update.
static const int MSG_NODELATESTDATA = 0x90;
/// Server->client: remove node.
static const int MSG_REMOVENODE = 0x91;
/// Server->client: create new component.
static const int MSG_CREATECOMPONENT = 0x92;
/// Server->client: component delta update.
static const int MSG_COMPONENTDELTAUPDATE = 0x93;
/// Server->client: component latest data update.
static const int MSG_COMPONENTLATESTDATA = 0x94;
/// Server->client: remove component.
static const int MSG_REMOVECOMPONENT = 0x95;

/// Client->server and server->client: remote event.
static const int MSG_REMOTEEVENT = 0x96;
/// Client->server and server->client: remote node event.
static const int MSG_REMOTENODEEVENT = 0x97;
/// Server->client: info about package.
static const int MSG_PACKAGEINFO = 0x98;

/// Packet that includes all the above messages
static const int MSG_PACKED_MESSAGE = 0x99;

// ─── ENGINE SERVICES (0x9A–0xBF) ──────────────────────────────────────────────
// Core engine protocol: auth, terrain sync, water streaming, peer management.
// MSG_USER (0x200+) is reserved for game developers — engine never touches it.
// ──────────────────────────────────────────────────────────────────────────────

/// Client->server: send client public key for key exchange.
static const int MSG_KEY_EXCHANGE = 0x9A;
/// Server->client: send server public key for key exchange.
static const int MSG_KEY_EXCHANGE_REPLY = 0x9B;

/// AuthServer->client: introduce a peer for NAT punchthrough (GUID + token + patch coords).
static const int MSG_PEER_INTRODUCE = 0x9C;
/// Client->AuthServer: peer connection established, reporting assigned role.
static const int MSG_PEER_READY = 0x9D;
/// Client->AuthServer: NAT punchthrough to peer failed.
static const int MSG_PEER_CONNECT_FAILED = 0x9E;
/// Client->AuthServer: peer connection lost.
static const int MSG_PEER_DISCONNECTED = 0x9F;
/// Subclient->subserver: relay an inner message to AuthServer.
static const int MSG_RELAY_TO_AUTH = 0xA0;
/// Subserver->subclient: relay an AuthServer response to subclient.
static const int MSG_RELAY_FROM_AUTH = 0xA1;

/// Client->server: terrain brush edit request (optimistic — client applies first).
static const int MSG_EDIT_TERRAIN = 0xA2;
/// Client->server: object create/delete/transform edit request.
static const int MSG_EDIT_OBJECT = 0xA3;
/// Server->client: edit rejected, client should rollback.
static const int MSG_EDIT_REJECT = 0xA4;
/// Server->client(s): validated edit broadcast to replay.
static const int MSG_EDIT_BROADCAST = 0xA5;

// ─── TRADE (0xA6–0xAE) ────────────────────────────────────────────────────────
/// Client->server: initiate trade with nearby player (target_player_id).
static const int MSG_TRADE_REQUEST  = 0xA6;
/// Server->client: another player wants to trade (requester_player_id, name).
static const int MSG_TRADE_INCOMING = 0xA7;
/// Client->server: accept incoming trade request.
static const int MSG_TRADE_ACCEPT   = 0xA8;
/// Client->server: reject incoming trade request.
static const int MSG_TRADE_REJECT   = 0xA9;
/// Client->server: add/remove item in trade offer (item_id, qty, add_or_remove).
static const int MSG_TRADE_OFFER    = 0xAA;
/// Server->client: other player's offer snapshot (items[]).
static const int MSG_TRADE_UPDATE   = 0xAB;
/// Client->server: lock in current offer (both locked = swap).
static const int MSG_TRADE_LOCK     = 0xAC;
/// Server->both: trade completed, items swapped.
static const int MSG_TRADE_COMPLETE = 0xAD;
/// Either->server: cancel trade session.
static const int MSG_TRADE_CANCEL   = 0xAE;

/// Server->client: full water heightmap sent on connect.
static const int MSG_WATER_MAP = 0xB0;
/// Client->server or server->client(s): incremental water edit (brush stroke).
static const int MSG_WATER_EDIT = 0xB1;
/// Server->client: resource data for one terrain patch region.
static const int MSG_RESOURCE_PATCH = 0xB2;
/// Client->server: player's current patch position (triggers streaming).
static const int MSG_PATCH_POSITION = 0xB3;

/// Client->server: terrain sync request (version + hash for journal replay).
static const int MSG_TERRAIN_SYNC = 0xB4;
/// Server->client: journal of terrain edits to replay (incremental sync).
static const int MSG_TERRAIN_JOURNAL = 0xB5;
/// Server->client: full heightmap transfer (hash mismatch or journal gap).
static const int MSG_TERRAIN_FULLSYNC = 0xB6;

/// Server->broadcast: new terrain generated at grid cell (gridX i32, gridZ i32, heightmap PNG bytes).
static const int MSG_NEW_TERRAIN = 0xBB;

// ─── MINING (0xB7–0xBA) ──────────────────────────────────────────────────────
/// Client->server: mine deposit at terrain position (grid_x, grid_z, tool_item_id).
static const int MSG_MINE_REQUEST    = 0xB7;
/// Server->client: mine result (ore_type, amount, remaining_qty, tool_durability).
static const int MSG_MINE_RESULT     = 0xB8;
/// Server->broadcast: deposit exhausted at position (grid_x, grid_z) — remove outcrop.
static const int MSG_DEPOSIT_EXHAUSTED = 0xB9;
/// Server->client: deposit discovered via prospecting (grid_x, grid_z, type).
static const int MSG_DEPOSIT_DISCOVERED = 0xBA;

// ─── RESOURCE MAP (0xBC–0xBF) ────────────────────────────────────────────────
/// Client->server: harvest resource at world position (worldX f32, worldZ f32, resourceType u8).
/// Server validates against its authoritative ResourceMap, decrements, replies with inventory delta.
static const int MSG_RESOURCE_HARVEST  = 0xBC;
/// Server->broadcast: resource depleted at position (worldX f32, worldZ f32, newQty u8, resourceType u8).
/// Clients update their local ResourceMap image and remove/update pickup nodes.
static const int MSG_RESOURCE_DEPLETED = 0xBD;

/// Client->server: eat item from inventory (item_id).
static const int MSG_EAT = 0xC0;
/// Client->server: drink from water source (source type).
static const int MSG_DRINK = 0xC1;
/// Server->client: vital stats update (hp, hunger, thirst, stamina, warmth).
static const int MSG_VITAL_UPDATE = 0xC2;

// ─── TRAPS & HARVEST (0xC3–0xC8) ─────────────────────────────────────────────
/// Client->server: place a trap from inventory (item_id, pos_x, pos_y, pos_z, rot).
static const int MSG_PLACE_TRAP     = 0xC3;
/// Server->broadcast: trap node spawned (node_id, item_id, pos, rot).
static const int MSG_TRAP_SPAWNED   = 0xC4;
/// Server->broadcast: trap caught a creature (trap_node_id, creature_node_id).
static const int MSG_TRAP_TRIGGERED = 0xC5;
/// Server->broadcast: placed trap removed (node_id).
static const int MSG_TRAP_REMOVED   = 0xC6;
/// Client->server: harvest a dead or trapped creature (target_node_id).
static const int MSG_HARVEST        = 0xC7;
/// Server->client: harvest yielded items (target_node_id, count, [item_id, qty]...).
static const int MSG_HARVEST_RESULT = 0xC8;
/// Client->server: report a creature near a placed trap so the server can roll
/// d20 vs hold_strength and decide a catch. Client-driven detection because the
/// server has no shared world view of LOCAL creature positions. Resource Chain
/// Phase 2 closure. Format: trap_node_id, creature_node_id, creature_id, pos_x, pos_y, pos_z.
static const int MSG_TRAP_CHECK     = 0xC9;

// ─── FARMING (0xCA–0xCD) ────────────────────────────────────────────────────
/// Client->server: plant seed at position (seed_item_id i32, pos vec3).
static const int MSG_PLANT_CROP     = 0xCA;
/// Server->broadcast: crop spawned or growth updated (crop_id i32, seed_item_id i32, pos vec3, growth_stage u8).
static const int MSG_CROP_SPAWNED   = 0xCB;
/// Client->server: harvest mature crop (crop_id i32).
static const int MSG_HARVEST_CROP   = 0xCC;
/// Server->broadcast: crop removed (crop_id i32).
static const int MSG_CROP_REMOVED   = 0xCD;

// ─── OFFLINE-MODE RESERVED LOGIN ─────────────────────────────────────────────
// Used by Sample 60's Offline button + AuthServer's HandleKeyExchangeAuth. The
// client supplies these credentials automatically (the user types nothing), and
// the server auto-provisions the account on first use. Only valid from 127.0.0.1
// or ::1 — the server rejects this username from any other address. Both sides
// MUST agree on these strings; keep them here as the single source of truth.
static const char* const OFFLINE_RESERVED_USERNAME = "Offline";
static const char* const OFFLINE_RESERVED_PASSWORD = "OFFLINE_LOCAL_RESERVED_2026";

/// Client->server: pick up world item (node_id).
static const int MSG_PICKUP = 0xD0;
/// Client->server: drop item from inventory (item_id, quantity).
static const int MSG_DROP = 0xD1;
/// Server->client: full inventory snapshot on connect.
static const int MSG_INVENTORY_UPDATE = 0xD2;
/// Server->client: single item add/remove delta.
static const int MSG_INVENTORY_DELTA = 0xD3;
/// Client->server: equip item to slot (item_id, slot_name).
static const int MSG_EQUIP = 0xD4;
/// Client->server: unequip slot (slot_name).
static const int MSG_UNEQUIP = 0xD5;
/// Client->server: craft recipe (recipe_id).
static const int MSG_CRAFT = 0xD6;
/// Client->server: open storage container (building_id).
static const int MSG_OPEN_STORAGE = 0xD7;
/// Client->server: close storage container (building_id).
static const int MSG_CLOSE_STORAGE = 0xD8;
/// Client->server: transfer item between inventory and storage (item_id, qty, direction).
static const int MSG_TRANSFER = 0xD9;
/// Server->client: storage contents for a building (building_id, items[]).
static const int MSG_STORAGE_CONTENTS = 0xDA;
/// Client->server: request recent death log entries (u16 maxEntries).
static const int MSG_QUERY_DEATH_LOG  = 0xDB;
/// Server->client: death log response (u16 count, then per-entry: name, species, f32 x, f32 z, u8 cause, killer, i32 game_day).
static const int MSG_DEATH_LOG_RESULT = 0xDC;
/// Client->server: request death analytics (population trends, aggregated stats).
static const int MSG_QUERY_DEATH_ANALYTICS = 0xDD;
/// Server->client: death analytics result (see HandleDeathAnalyticsResult for format).
static const int MSG_DEATH_ANALYTICS_RESULT = 0xDE;

/// Client->server: melee or ranged attack (target_node_id u32, weapon_item_id i32).
static const int MSG_ATTACK          = 0xE0;
/// Server->client(s): combat round result (attacker u32, target u32, hit bool, damage i32, target_hp i32, crit bool).
static const int MSG_COMBAT_RESULT   = 0xE1;
/// Server->broadcast: creature died (node_id u32, killer_node_id u32).
static const int MSG_CREATURE_DEATH  = 0xE2;
/// Server->client: player was killed (killer_node_id u32).
static const int MSG_PLAYER_DEATH    = 0xE3;
/// Server->broadcast: replacement creature spawn — Death System Phase 1.
/// Sent after a successful harvest causes the per-species birth accumulator
/// to cross 1.0. Format: (region_id i32, creature_id i32, x f32, y f32, z f32, spawn_id u32).
/// Y is sent as 0.0 — clients snap to terrain height on receive.
/// spawn_id is server-assigned tracking ID for AI state correlation. Optional field (backward compat).
static const int MSG_SPAWN_CREATURE  = 0xE4;
/// Client->server: possess an NPC (npc_node_id u32).
/// Server->client: possession confirmed (npc_node_id u32, npc_player_id i32, success bool).
static const int MSG_POSSESS         = 0xE5;
/// Client->server: release possession. Server returns NPC to AI.
static const int MSG_UNPOSSESS       = 0xE6;
/// Server->client: NPC AI state broadcast (node_id u32, state u8, pos vec3, target_id u32, speed f32).
static const int MSG_CREATURE_AI_STATE = 0xE7;

/// Admin client->server: override server time (float hour 0-24, int dayOfYear). hour < 0 = clear.
static const int MSG_ADMIN_TIME_OVERRIDE = 0xE8;

/// Admin client->server: request all AI tuning parameters.
static const int MSG_TUNING_REQUEST = 0xE9;
/// Server->admin client: full dump of AI tuning key-value pairs.
static const int MSG_TUNING_DATA = 0xEA;
/// Admin client->server: update one AI tuning parameter (key, value).
static const int MSG_TUNING_UPDATE = 0xEB;

/// Server->client: spawn a tree (species u8, pos_x f32, pos_z f32, scale f32, treeId u32).
static const int MSG_SPAWN_TREE = 0xEC;
/// Server->client: remove a tree (treeId u32). Sent when tree is harvested.
static const int MSG_REMOVE_TREE = 0xED;
/// Client->server: chop a tree (treeId u32). Server validates tool, deducts HP, yields wood.
static const int MSG_CHOP_TREE = 0xEE;
/// Client->server: god directive for NPC (spawnId, directive_type, params).
static const int MSG_GOD_DIRECTIVE = 0xEF;

/// Client->server: place building (building_type_id, pos_x, pos_y, pos_z, rotation).
static const int MSG_BUILD = 0xF0;
/// Client->server: demolish building (placed_building_id).
static const int MSG_DEMOLISH = 0xF1;
/// Server->client: build result (success, placed_building_id or error string).
static const int MSG_BUILD_RESULT = 0xF2;
/// Server->broadcast: new building spawned (placed_id, type_id, pos, rotation, hp).
static const int MSG_BUILDING_SPAWN = 0xF3;
/// Server->broadcast: building removed (placed_building_id).
static const int MSG_BUILDING_REMOVE = 0xF4;
/// Client->server: toggle gate open/closed (placed_building_id).
static const int MSG_GATE_TOGGLE = 0xF5;
/// Server->broadcast: gate state changed (placed_building_id, gate_open).
static const int MSG_GATE_STATE = 0xF6;
/// Client->server: repair building (placed_building_id, item_id).
static const int MSG_REPAIR = 0xF7;
/// Server->broadcast: building HP changed (placed_building_id, new_hp).
static const int MSG_BUILDING_HP = 0xF8;
/// Client->server: sleep at shelter (placed_building_id).
static const int MSG_SLEEP = 0xF9;
/// Client->server: set respawn point at shelter (placed_building_id).
static const int MSG_SET_RESPAWN = 0xFA;
/// Server->client: respawn point set (placed_building_id, pos).
static const int MSG_RESPAWN_SET = 0xFB;
/// Client->server: tap tree for resin (tree_id).
static const int MSG_TAP_TREE = 0xFC;

/// Client->server: mount a tamed horse (horse_spawnId u32).
static const int MSG_MOUNT = 0xAF;
/// Client->server: dismount current horse.
static const int MSG_DISMOUNT = 0xBE;

/// Server->client: fish spawn points from water body analysis.
/// Format: u16 count, then count × (f32 x, f32 z, f32 depth).
static const int MSG_FISH_SPAWNS = 0xFD;

/// Server->client: settlement patch ownership.
/// Format: u16 count, then count × (u8 spatch_x, u8 spatch_z, u16 settlement_id).
static const int MSG_SETTLEMENT_CLAIMS = 0xFE;

/// Server->client: phenomenon observed near NPC (spawnId u32, phenomenonType i32, pos vec3, visualHint string).
/// Client routes to NPC's ObservePhenomenon() for WIS-gated insight accumulation and spawns visual effect.
static const int MSG_PHENOMENON = 0xFF;

/// Server->client: settlement epoch advancement (campfireId u32, newEpoch i32, epochName string).
/// Client shows advancement notification and updates local epoch state.
static const int MSG_EPOCH_CHANGED = 0x100;

/// Server->client: NPC received a name at maturity (spawnId u32, npcName string).
/// Client creates floating name label above the NPC's head.
static const int MSG_NPC_NAMED = 0x101;

/// Server->client: NPC growth stage changed (spawnId u32, growthProgress float, stage u8).
/// Client updates NPC model scale and visual appearance for the new growth stage.
static const int MSG_NPC_GROWTH = 0x102;

/// Server->client: Chieftain changed in a settlement (campfireId u32, newChiefSpawnId u32,
/// chieftainName String, topSkillName String, reason u8: 0=first, 1=death, 2=surpassed).
static const int MSG_CHIEFTAIN_CHANGED = 0x103;

/// Server->broadcast: NPC healed another NPC (healerSpawnId u32, patientSpawnId u32,
/// healAmount i32, curedPoison bool).
static const int MSG_NPC_HEAL = 0x104;

/// Server->broadcast: settlement discovered an innovation (campfireId u32, innovationType i32,
/// discovererName String, innovationName String).
static const int MSG_INNOVATION_DISCOVERED = 0x105;

/// Client->server: claim a terrain patch for the player's settlement (patchX i32, patchZ i32).
static const int MSG_PATCH_CLAIM = 0x106;
/// Server->client: result of patch claim (success bool, message String).
static const int MSG_PATCH_RESULT = 0x107;
/// Client->server: query patch ownership (patchX i32, patchZ i32).
static const int MSG_PATCH_QUERY = 0x108;

/// Server->client: tapestry inscription text (inscription String).
/// Sent when a tapestry is crafted or on connect for settlements with tapestries.
static const int MSG_TAPESTRY_INSCRIPTION = 0x109;

/// Client->server: install a lock on a building (buildingId i32, lockItemId i32).
static const int MSG_INSTALL_LOCK = 0x10A;
/// Client->server: remove a lock from a building (buildingId i32).
static const int MSG_REMOVE_LOCK = 0x10B;

// ─── WEATHER & CELESTIAL (0x78–0x79) ─────────────────────────────────────────
/// Server->client: weather forecast (cloudCover f32, precipitation f32, windSpeed f32,
/// windAngle f32, temperature f32, humidity f32, condition String).
static const int MSG_WEATHER_UPDATE = 0x78;
/// Server->client: celestial state (moonPhase f32, eclipseActive bool).
static const int MSG_CELESTIAL_STATE = 0x79;

// ─── CANOE TRANSPORT (0x10C–0x10D) ───────────────────────────────────────────
/// Client->server: board a canoe (placedBuildingId i32).
static const int MSG_BOARD_CANOE = 0x10C;
/// Client->server: disembark from canoe.
static const int MSG_DISEMBARK_CANOE = 0x10D;

/// Used to define custom messages, usually of the form MSG_USER + x, where x is an integer value.
static const int MSG_USER = 0x200;

/// Fixed content ID for client controls update.
static const unsigned CONTROLS_CONTENT_ID = 1;
/// Package file fragment size.
static const unsigned PACKAGE_FRAGMENT_SIZE = 1024;

}
