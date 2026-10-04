// AuthServer — private central authority for TerrainNode network
// NOT a public sample. Handles login, patch ownership, peer brokering.
// Runs with a debug GUI for monitoring connections and activity.

#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Database/DbResult.h>
#include <Urho3D/Database/DbConnection.h>
#include <Urho3D/Database/Database.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/ListView.h>
#include <Urho3D/UI/MultiLineEdit.h>
#include <Urho3D/UI/ScrollView.h>
#include <Urho3D/UI/Window.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/DropDownList.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/DrivenKey.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Graphics/TerrainBrush.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/JSONValue.h>
#include <Urho3D/Network/HttpRequest.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/UI/MelbourneClock.h>
#include <Urho3D/Game/GameDB.h>
#include <Urho3D/Game/WorldDB.h>
#include <Urho3D/Game/CombatResolver.h>
#include <Urho3D/Game/PopulationManager.h>
#include "TerrainGenerator.h"
#include "TerrainJournal.h"
#include "ResourceMap.h"
#include "NPCPriorityCompute.h"
#include "EcosystemManager.h"

using namespace Urho3D;

class AuthServer : public Application
{
    URHO3D_OBJECT(AuthServer, Application);

public:
    explicit AuthServer(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

    /// Roll a single die with N sides (1..sides). Consumes from quantum entropy pool.
    int DiceRoll(int sides);

private:
    // Database
    void InitDatabase();
    DbConnection* db_{};

    // Input
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);

    // Network events
    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleClientIdentity(StringHash eventType, VariantMap& eventData);
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// PAKE: provide password hash during key exchange.
    void HandleKeyExchangeAuth(StringHash eventType, VariantMap& eventData);
    /// PAKE: first successful decrypt confirms authentication.
    void HandleClientAuthenticated(StringHash eventType, VariantMap& eventData);

    // Auth
    bool AuthenticateUser(const String& username, const String& passwordHash, int& adminLevel);
    bool RegisterUser(const String& username, const String& passwordHash);
    /// Escape a string for safe SQL interpolation (doubles single quotes).
    static String SqlEscape(const String& s);

    // Patch ownership
    struct PatchInfo
    {
        int patchX;
        int patchZ;
        String ownerName;
        String ownerAddress;
        unsigned short ownerPort;
    };
    bool ClaimPatch(int patchX, int patchZ, const String& username);
    bool AllocateRandomPatch(const String& username, int& outPatchX, int& outPatchZ);
    PatchInfo QueryPatchOwner(int patchX, int patchZ);

    // Connected clients
    struct ClientSession
    {
        String username;
        bool authenticated{false};
        int adminLevel{0};
        String guid;                           // client's NAT GUID (sent after auth)
        IntVector2 lastPatchPos{0x7FFFFFFF, 0x7FFFFFFF};  // last known patch position
        HashSet<unsigned long long> sentPatches;           // patchKey(x,z) already sent

        // Survival state (server-authoritative)
        int hp{20}, maxHp{20};
        float hunger{100.0f};
        float thirst{100.0f};
        float stamina{100.0f};
        float warmth{15.0f};
        bool alive{true};
        float speedMult{1.0f};
        float respawnTimer{0.0f};  // countdown to respawn (0 = not dead)
        bool poisoned{false};
        int combatBuff{0};         // temporary attack bonus from potions
        float combatBuffTimer{0.0f};  // seconds remaining on combat buff

        // Last-sent vitals for delta detection (send-on-change)
        int sentHp{-1};
        int sentHunger{-1};
        int sentThirst{-1};
        int sentStamina{-1};

        // Building system
        int respawnBuildingId{0};  // placed_building_id of respawn shelter (0 = default spawn)

        // Possession system (Phase 4)
        int possessedNPCPlayerId{-1};   // NPC's playerId while possessed (-1 = god cam)
        unsigned possessedNodeId{0};     // NPC's scene node ID (0 = none)

        // Storage system
        int openBuildingId{-1};         // placed_building_id of currently open storage (-1 = none)
        int playerId{-1};               // server-assigned player ID for inventory ops

        // Phase 4 hardening: connection lifecycle
        float connectTime{0.0f};     // server uptime when connection was established
        int failedLogins{0};         // consecutive failed login attempts this session
    };
    HashMap<Connection*, ClientSession> sessions_;

    // Phase 4 hardening: per-IP rate limiting and connection caps
    struct IPRecord
    {
        int activeConnections{0};    // current simultaneous connections from this IP
        int failedAttempts{0};       // failed login attempts in current window
        float windowStart{0.0f};     // uptime when current rate-limit window began
    };
    HashMap<String, IPRecord> ipRecords_;
    void SweepUnauthConnections();   // kick unauth sessions older than timeout
    String GetConnectionIP(Connection* connection) const;

    static constexpr float UNAUTH_TIMEOUT = 30.0f;         // seconds before unauth kick
    static constexpr int MAX_FAILED_LOGINS_PER_WINDOW = 3;  // attempts before IP ban
    static constexpr float LOGIN_RATE_WINDOW = 60.0f;       // rate limit window in seconds
    static constexpr int MAX_CONNECTIONS_PER_IP = 4;         // simultaneous connections per IP

    // Possession system
    void HandlePossess(Connection* connection, MemoryBuffer& msg);
    void HandleUnpossess(Connection* connection, MemoryBuffer& msg);
    /// Allocate a stable playerId for an NPC node. Same node always gets the same ID.
    int GetNPCPlayerId(unsigned nodeId);
    /// Track which NPC nodeIds are currently possessed (enforce one possessor per NPC)
    HashMap<unsigned, Connection*> npcPossessors_;  // nodeId → possessing connection
    /// NPC playerId counter (IDs start at 10000 to avoid collision with player hashes)
    int npcPlayerIdCounter_{10000};
    /// Cached NPC nodeId → playerId mapping (stable across possess/unpossess cycles)
    HashMap<unsigned, int> npcPlayerIds_;

    // AI Tuning (runtime-configurable NPC parameters from GameDB)
    HashMap<String, float> aiTuning_;
    void LoadAITuning();

    // Profession schedule system
    void LoadProfessionSchedules();
    int GetScheduledTask(int professionId, int hour) const;
    int TaskStringToSTASK(const String& taskType) const;
    int DeriveProfession(unsigned spawnId);  ///< Check NPC's highest skill, return matching profession ID (0 = generalist)
    struct ScheduleEntry { int professionId; int hour; int stask; };
    Vector<ScheduleEntry> professionSchedule_;
    struct ProfessionSkillMap { int professionId; int skillId; };
    Vector<ProfessionSkillMap> professionSkills_;  ///< profession → primary_skill mapping from DB
    float professionEvalTimer_{0.0f};
    float GetTuning(const String& key, float fallback) const
    {
        auto it = aiTuning_.Find(key);
        return it != aiTuning_.End() ? it->second_ : fallback;
    }
    void HandleTuningRequest(Connection* connection);
    void HandleTuningUpdate(Connection* connection, MemoryBuffer& msg);
    void SendTuningData(Connection* connection);

    // Survival system
    void InitGameDB();
    void InitWorldDB();
    void WorldCheckpointTick(float dt);
    float worldCheckpointTimer_{0.0f};
    static constexpr float WORLD_CHECKPOINT_INTERVAL = 300.0f;  // flush WAL every 5 minutes
    void SurvivalTick(float dt);
    void SendVitalUpdate(Connection* connection, ClientSession& session, bool force = false);
    void HandleEat(Connection* connection, MemoryBuffer& msg);
    void HandleDrink(Connection* connection, MemoryBuffer& msg);

    // Combat
    void HandleAttack(Connection* connection, MemoryBuffer& msg);

    // Inventory system
    void HandlePickup(Connection* connection, MemoryBuffer& msg);
    void HandleDrop(Connection* connection, MemoryBuffer& msg);
    void HandleCraft(Connection* connection, MemoryBuffer& msg);
    void HandleEquip(Connection* connection, MemoryBuffer& msg);
    void HandleUnequip(Connection* connection, MemoryBuffer& msg);
    void SendInventoryUpdate(Connection* connection, int playerId);
    void SendInventoryDelta(Connection* connection, int itemId, int quantity, bool added);
    int GetPlayerId(const String& username);

    // Cross-DB inventory helpers (WorldDB state + GameDB rules)
    /// Compute total carried weight by summing worldDB inventory * gameDB item weights.
    float ComputePlayerWeight(int playerId);
    /// Compute available bag slots from rules - occupied.
    int ComputeAvailableSlots(int playerId);
    /// Sum equipped-container bonuses (backpack/pouch/basket/etc.) across the
    /// back/hand/offhand slots into extra bag slots and extra weight capacity.
    void ComputeContainerBonus(int playerId, int& extraSlots, float& extraWeight);
    /// Absolute carry-weight cap including equipped-container extra_weight bonus.
    float ComputeMaxWeight(int playerId);
    /// Add item to world inventory with weight/slot validation from rules.
    bool AddItemToWorldInventory(int playerId, int itemId, int qty);
    /// Unequip from world inventory, bridging item lookup for bag add-back.
    bool UnequipFromWorld(int playerId, const String& slot, int& outItemId);

    // Trade system
    struct TradeSession
    {
        int playerA{0};
        int playerB{0};
        Connection* connA{};
        Connection* connB{};
        HashMap<int, int> offerA;   // item_id → quantity
        HashMap<int, int> offerB;
        bool lockedA{false};
        bool lockedB{false};
    };
    HashMap<int, TradeSession> tradeSessions_;  // key = playerA id
    int tradeSessionSeq_{0};
    void HandleTradeRequest(Connection* connection, MemoryBuffer& msg);
    void HandleTradeAccept(Connection* connection, MemoryBuffer& msg);
    void HandleTradeReject(Connection* connection, MemoryBuffer& msg);
    void HandleTradeOffer(Connection* connection, MemoryBuffer& msg);
    void HandleTradeLock(Connection* connection, MemoryBuffer& msg);
    void HandleTradeCancel(Connection* connection, MemoryBuffer& msg);
    TradeSession* FindTradeSession(int playerId);
    Connection* FindConnectionByPlayerId(int playerId);
    void ExecuteTradeSwap(TradeSession& session);
    void CleanupTradeSession(int playerId);
    bool CheckTradeProximity(TradeSession& session);
    void CancelTradeIfPossessedDamaged(unsigned spawnId);
    Vector3 GetPossessedNPCPosition(int playerId);
    static constexpr float TRADE_MAX_DISTANCE = 5.0f;
    static constexpr int TRADE_MAX_OFFER_SLOTS = 6;
    static constexpr float TRADE_COOLDOWN_SECONDS = 5.0f;
    HashMap<int, float> tradeCooldowns_;  // playerId → server uptime when trade ended

    // Building system
    void HandleBuild(Connection* connection, MemoryBuffer& msg);
    void HandleDemolish(Connection* connection, MemoryBuffer& msg);
    void HandleGateToggle(Connection* connection, MemoryBuffer& msg);
    void HandleRepair(Connection* connection, MemoryBuffer& msg);
    void HandleGodDirective(Connection* connection, MemoryBuffer& msg);
    void HandleSleep(Connection* connection, MemoryBuffer& msg);
    void HandleSetRespawn(Connection* connection, MemoryBuffer& msg);
    void HandleOpenStorage(Connection* connection, MemoryBuffer& msg);
    void HandleCloseStorage(Connection* connection, MemoryBuffer& msg);
    void HandleTransfer(Connection* connection, MemoryBuffer& msg);
    void HandleInstallLock(Connection* connection, MemoryBuffer& msg);
    void HandleRemoveLock(Connection* connection, MemoryBuffer& msg);
    void HandleMount(Connection* connection, MemoryBuffer& msg);
    void HandleDismount(Connection* connection, MemoryBuffer& msg);
    void HandleQueryDeathLog(Connection* connection, MemoryBuffer& msg);
    void HandleQueryDeathAnalytics(Connection* connection, MemoryBuffer& msg);
    void BroadcastBuildingSpawn(int placedId, int typeId, float px, float py, float pz,
                                float rotation, int hp);
    void BroadcastBuildingRemove(int placedId);
    void BroadcastBuildingHp(int placedId, int newHp);
    void BroadcastGateState(int placedId, bool open);
    /// Phase 15b: Auto-furnish a newly built shelter/workshop with appropriate furniture.
    void AutoFurnishBuilding(int parentPlacedId, int buildingTypeId, int ownerId,
                             const Vector3& buildingPos, float buildingRotation);
    void SendExistingBuildings(Connection* connection);
    void BuildingDecayTick(float gameDayFraction);
    float GetShelterWarmth(float px, float py, float pz) const;

    // Trap system (Resource Chain Phase 2)
    struct ServerTrapState
    {
        int      itemId{0};        // 400 = Simple Snare, etc.
        int      ownerPlayerId{0};
        Vector3  position{};
        float    placedAt{0.0f};   // server time
        bool     armed{true};      // false once tripped/removed
        unsigned capturedCreatureId{0};  // spawnId of live-captured creature (cage traps)
        bool     isLiveCapture{false};   // true for cage traps — don't kill on trigger
    };
    HashMap<unsigned, ServerTrapState> trapStates_;  // key: trap node ID

    // Combat Phase 2 — server-authoritative creature HP tracking.
    // Path C-lite: lazy-registers on first MSG_ATTACK against an unknown nodeId.
    // Key is the attacker's local nodeId — single-attacker correct, multi-client
    // spectator broadcast deferred to a Phase 2.5 architectural pivot.
    /// Death cause — appended to MSG_CREATURE_DEATH so clients can pick visuals.
    enum DeathCause : unsigned char
    {
        DEATH_NONE     = 0,
        DEATH_COMBAT   = 1,
        DEATH_DROWN    = 2,
        DEATH_STARVE   = 3,
        DEATH_AGE      = 4,
        DEATH_SCAVENGE = 5,
        DEATH_FALL     = 6,
        DEATH_FIRE     = 7,
        DEATH_DEHYDRATE = 8,
        DEATH_FREEZE   = 9
    };

    struct ServerCreatureState
    {
        int     creatureId{0};   // creatures table id
        int     hp{0};
        int     maxHp{0};
        int     defense{10};
        int     attackMod{0};
        int     damage{1};
        int     damageVar{2};
        int     regionId{-1};
        Vector3 position{};
        String  species;
    };
    HashMap<unsigned, ServerCreatureState> creatureStates_;  // key: server-assigned spawnId
    HashSet<unsigned> harvestedCreatures_;  // spawnIds already looted — prevents harvest spam
    /// Look up creature combat stats from GameDB. Returns true on success.
    bool LoadCreatureCombat(int creatureId, ServerCreatureState& out);
    /// Broadcast MSG_CREATURE_DEATH to all clients within 100m.
    void BroadcastCreatureDeath(unsigned spawnId, const ServerCreatureState& cs,
                                Connection* killer, DeathCause cause = DEATH_COMBAT);
    void HandlePlaceTrap(Connection* connection, MemoryBuffer& msg);
    void HandleHarvest(Connection* connection, MemoryBuffer& msg);
    /// Client-driven trap detection. Client reports a creature within range of a placed
    /// trap; server validates ownership/range, rolls d20 vs hold_strength, and on success
    /// disarms the trap, registers the creature in creatureStates_ (so harvest still
    /// works), and broadcasts MSG_TRAP_TRIGGERED. Resource Chain Phase 2 closure.
    void HandleTrapCheck(Connection* connection, MemoryBuffer& msg);
    void BroadcastTrapSpawned(unsigned nodeId, int itemId, const Vector3& pos, float rotation);
    void BroadcastTrapRemoved(unsigned nodeId);
    void BroadcastTrapTriggered(unsigned trapNodeId, unsigned creatureNodeId, const Vector3& pos);

    // ── Farming (server-authoritative crops) ──────────────────────────────────
    void HandlePlantCrop(Connection* connection, MemoryBuffer& msg);
    void HandleHarvestCrop(Connection* connection, MemoryBuffer& msg);
    void CropGrowthTick(float dt);
    void SendExistingCrops(Connection* connection);
    void BroadcastCropSpawned(int cropId, int seedItemId, const Vector3& pos, unsigned char stage);
    void BroadcastCropRemoved(int cropId);
    void CacheCropTypes();
    int GetCurrentSeasonIndex() const;
    String GetSeasonName(int index) const;
    bool IsSeasonMatch(const String& allowed, int currentSeason) const;
    bool IsNearWater(float px, float pz, float range) const;
    HashMap<int, CropTypeInfo> cachedCropTypes_;
    float cropTickTimer_{0.0f};

    // ── Resource Map (server-authoritative) ──────────────────────────────────
    /// Handle MSG_RESOURCE_HARVEST: validate position against server ResourceMap, decrement, reply.
    void HandleResourceHarvest(Connection* connection, MemoryBuffer& msg);
    /// Broadcast MSG_RESOURCE_DEPLETED to all connected clients.
    void BroadcastResourceDepleted(float worldX, float worldZ, unsigned char newQty, unsigned char resourceType);
    /// Send full resource map PNG to a newly connected client.
    void SendResourceMapToClient(Connection* connection);
    /// Initialize or load the server's authoritative resource map.
    void InitResourceMap();
    /// Server-authoritative resource map.
    SharedPtr<ResourceMap> resourceMap_;
    /// Save resource map if dirty.
    void SaveResourceMapIfDirty();
    /// Phase 19: server-side ecosystem for NPC trampling/paths.
    SharedPtr<EcosystemManager> ecosystem_;
    float soilTickTimer_{0.0f};
    /// Phase 21: periodic chieftain evaluation and succession.
    void EvaluateChieftains();
    void BroadcastChieftainChanged(unsigned campfireId, unsigned newChiefSpawnId, unsigned char reason);
    float chieftainEvalTimer_{0.0f};
    /// Phase 21: track current chieftain per settlement for change detection.
    HashMap<unsigned, unsigned> settlementChieftains_;  // campfireId → chieftain spawnId
    /// Phase 23: food decay — remove spoiled food from NPC inventories.
    void TickFoodDecay(float dt);
    float foodDecayTimer_{0.0f};
    /// Phase 25: selective breeding — tamed animal pairs produce offspring.
    void TickBreeding(float dt);
    float breedingTimer_{0.0f};
    // ── World Phenomena (Plan 10) ──────────────────────────────────────────────
    /// Cached phenomenon rule from phenomena_rules table.
    struct PhenomenonRule
    {
        int id;
        String conditionType;   // FIRE_NEAR_ORE, SEED_ON_FERTILE, etc.
        String resultType;
        String visualHint;
        String insightCategory;
        int epochTierRequired;
    };
    Vector<PhenomenonRule> cachedPhenomena_;
    /// Load phenomena_rules from GameDB at startup.
    void CachePhenomenaRules();
    /// Evaluate world conditions every 30s — fire vs ore, seeds vs fertility, etc.
    void PhenomenaTick(float dt);
    float phenomenaTickTimer_{0.0f};
    static constexpr float PHENOMENA_TICK_INTERVAL = 30.0f;
    /// Broadcast MSG_PHENOMENON to all authenticated clients within range.
    /// Picks the single closest observing NPC (avoids duplicate visuals).
    /// Clients with no NPC nearby still get the visual hint (spawnId=0).
    void BroadcastPhenomenon(const Vector3& pos, int phenomenonType);
    /// Radius within which clients receive a phenomenon broadcast.
    static constexpr float PHENOMENA_BROADCAST_RADIUS = 100.0f;
    /// Get the effective epoch tier for a settlement (campfire).
    /// Derived from the highest epoch-gated skill any NPC at that campfire has discovered.
    int GetSettlementEpoch(unsigned campfireId) const;
    /// Cached settlement epoch tiers. Updated on technique discovery.
    HashMap<unsigned, int> settlementEpochs_;
    /// Recompute and cache a settlement's epoch. Returns true if epoch changed.
    bool UpdateSettlementEpoch(unsigned campfireId);
    /// Broadcast MSG_EPOCH_CHANGED to all authenticated clients.
    void BroadcastEpochChanged(unsigned campfireId, int newEpoch);
    /// Distance between fire pit and deposit for FIRE_NEAR_ORE detection.
    static constexpr float FIRE_ORE_PROXIMITY = 15.0f;
    /// Minimum fertility for SEED_ON_FERTILE (0-255 scale from ecosystem).
    static constexpr unsigned char FERTILE_THRESHOLD = 180;

    /// Timer for periodic resource respawn ticks.
    float resourceRespawnTimer_{0.f};
    static constexpr float RESOURCE_RESPAWN_INTERVAL = 60.f;  // once per game-minute
    /// Dual-trigger persistence: time-based + change-based
    bool resourceMapDirty_{false};
    float resourceMapSaveTimer_{0.f};          // seconds since last change (save 30s after change)
    float resourceMapPeriodicSaveTimer_{0.f};  // periodic unconditional save
    static constexpr float RESOURCE_SAVE_AFTER_CHANGE = 30.f;
    static constexpr float RESOURCE_PERIODIC_SAVE = 300.f;  // 5 minutes

    /// Broadcast a replacement creature spawn after a kill triggers the
    /// per-species birth accumulator. Death System Phase 1.
    /// pos.y is sent as 0.0 — clients snap to terrain height on receive.
    void BroadcastSpawnCreature(int regionId, int creatureId, const Vector3& pos, float growthProgress = 1.0f);

    // ── Server-Authoritative Creature AI (NPC AI Phases 1-2) ──────────────────
    /// Task enum mirroring client-side NPCTask for server-side decision making.
    enum ServerNPCTask
    {
        STASK_FLEE,         // Run from danger (highest priority)
        STASK_DEFEND,       // Defend campfire from predator (Phase 5)
        STASK_EAT,          // Consume food (hunger critical, abstract)
        STASK_EAT_FROM_INVENTORY, // Consume real food item from NPC inventory (Phase 4)
        STASK_HUNT,         // Hunt prey for food (Phase 3)
        STASK_TRAP_PLACE,   // Place a trap from inventory (Phase 4)
        STASK_TRAP_COLLECT, // Walk to triggered trap and harvest prey (Phase 4)
        STASK_WARM,         // Move toward campfire (warmth critical)
        STASK_SLEEP,        // Rest (stamina critical or nighttime)
        STASK_GATHER,       // Walk to resource and collect
        STASK_GATHER_WOOD,  // Gather firewood specifically (Phase 5) — returns to fire area
        STASK_SIT_FIRE,     // Idle sit near campfire
        STASK_TEND_FIRE,    // Walk to campfire and add fuel from inventory (Phase 5)
        STASK_MAKE_FIRE,    // Friction ignition on COLD/UNLIT pit (Phase 4a) — 2hr base
        STASK_SCAVENGE,     // Scavenger walks to death scent, eats, may kill prey (Phase 5b)
        STASK_DRINK,        // Walk to water's edge and drink (thirst critical)
        STASK_CRAFT,        // Craft item from inventory at station (Knapping 1+)
        STASK_EQUIP,        // Equip better weapon/tool from inventory
        STASK_DRESS,        // Equip better clothing/armor to body/head/feet/back slots
        STASK_CHOP,         // Chop tree for wood (Woodwork 1+, axe equipped)
        STASK_COOK,         // Cook raw meat at fire (Cooking 1+)
        STASK_TORCH,        // Light torch from campfire at night (FireMaking 2+)
        STASK_BUILD,        // Place building from materials (Woodwork 3+)
        STASK_REPAIR,       // Repair damaged building (Woodwork 2+)
        STASK_FARM_PLANT,   // Plant crop on prepared land (Farming 2+, spring)
        STASK_FARM_HARVEST, // Harvest mature crop (Farming 1+)
        STASK_FISH,         // Fish at water's edge (Fishing 1+)
        STASK_MINE,         // Mine ore from exposed deposit (Knapping 4+, pick equipped)
        STASK_SMELT,        // Smelt ore at LIT kiln (Smelting 1+, ore+charcoal in inventory)
        STASK_BURN_CHARCOAL, // Burn wood to charcoal at Charcoal Kiln (FireMaking 3+)
        STASK_TRADE,        // NPC-to-NPC surplus/need inventory swap (Trade 1+)
        STASK_TAME,         // Tame a docile animal (Animal Lore 3+, food offering)
        STASK_BURY,         // Walk to corpse, remove it, place grave marker
        STASK_PATROL,       // Night torch perimeter walk around campfire
        STASK_DIG_CHANNEL,  // Dig irrigation channel between water and farmland (Farming 4+)
        STASK_TEACH,        // Teach apprentice at campfire (teacher skill 5+, student < 3)
        STASK_FETCH_WATER,  // Walk to water with Clay Pot, fill, restore thirst (Pottery 2+)
        STASK_BUILD_BOAT,   // Build dugout canoe at waterside (Woodwork 6+, 5 planks)
        STASK_GATHER_HERBS, // Gather medicinal herbs in forest/meadow (Herbalism 2+)
        STASK_HEAL,         // Heal injured NPC with herbs (Herbalism 3+, herbs in inventory)
        STASK_SIGNAL,       // Chieftain lights signal fire at watchtower (Phase 24)
        STASK_PLAY_MUSIC,   // Sit by campfire at night, boost settlement morale (Phase 29)
        STASK_GUARD,        // Permanent perimeter patrol — combat NPCs directed by chieftain
        STASK_PROSPECT,     // Directed mining for known trace element (Knapping 6+)
        STASK_TAP_TREE,     // Walk to resin-yielding tree, tap for Tree Resin (Herbalism 2+)
        STASK_STRIP_BARK,   // Walk to willow tree, strip bark for fiber (Herbalism 3+)
        STASK_GATHER_LEAVES,// Walk to eucalyptus, gather medicinal leaves (Herbalism 4+)
        STASK_CARRY_FIRE,   // Carry fire bundle to allied camp that has gone cold
        STASK_PREPARE_FIRE_BUNDLE, // Craft fire bundle when camp has fire + resin (FireMaking 4+)
        STASK_DELIVER_FIRE, // Directed: carry fire to specific cold camp (chieftain/god order)
        STASK_CARRY_WATER,  // Carry water-filled vessel to allied camp with low waterReserve
        STASK_SHEAR,        // Shear tamed alpaca for wool (Animal Lore 2+)
        STASK_HERD,         // Round up stray tamed animals back toward settlement (Animal Lore 2+)
        STASK_CULL,         // Cull weakest tamed animal for meat/hide (Animal Lore 3+)
        STASK_WEAVE,        // Weave wool thread into cloth at Loom (Weaving 3+)
        STASK_TRADE_ROUTE,  // Walk to neighbouring settlement with surplus goods (Trade 3+, CHA drives profit)
        STASK_SOCIALIZE,    // Walk to nearby NPC and greet/chat (boosts familiarity)
        STASK_SCOUT,        // Extended perimeter patrol with boosted detection (Tracking 3+ or Stealth 2+)
        STASK_RESEARCH,     // Study at workshop to attempt technique discovery (INT 12+, any skill 3+)
        STASK_WANDER,       // Short patrol near home
        STASK_IDLE          // Stand around (lowest priority)
    };

    /// Server-side AI state for tracked creatures. Generic — humans first,
    /// animals slot in via Part B. Keyed by the client-side node ID assigned
    /// at spawn time.
    struct ServerCreatureAI
    {
        Vector3 position;             // authoritative world position
        Vector3 targetPosition;       // where it's heading
        unsigned char state{0};       // CreatureState as u8 (0 = IDLE)
        unsigned targetId{0};         // target creature/node ID (hunt/defend)
        float moveSpeed{2.0f};        // current movement speed
        float hunger{100.0f};
        float thirst{100.0f};
        float stamina{100.0f};
        float warmth{15.0f};
        float taskTimer{0.0f};        // time remaining on current action
        float stateTimer{0.0f};       // accumulated time in current state
        float broadcastTimer{0.0f};   // time since last client update
        unsigned campfireId{0};       // associated campfire (humans)
        int creatureId{0};            // species ID (20=CaveMan, 21=CaveWoman)
        int regionId{0};              // owning region
        bool isPredator{false};
        bool isHuman{false};
        bool isMale{true};
        int currentTask{STASK_IDLE};  // current high-level task
        int prevTask{STASK_IDLE};     // task before combat interrupt (Phase 36: guards resume)
        int professionId{0};          // profession from DB (0 = unassigned)
        Vector3 homePosition;         // spawn origin / campfire area
        float wanderRadius{25.0f};    // max distance from home for wander
        float taskDecisionTimer{0.0f};// time since last task evaluation
        unsigned spawnId{0};          // self-reference for inventory ops (Phase 4)

        // Phase 3: Hunting sub-state
        enum HuntPhase { HUNT_APPROACH, HUNT_CLOSE, HUNT_BUTCHER, HUNT_RETURN };
        int huntPhase{HUNT_APPROACH};
        float huntTimer{0.0f};        // time spent in current hunt phase

        // Phase 5: Predator defense sub-state
        enum DefensePhase { DEFENSE_ALERT, DEFENSE_CHARGE, DEFENSE_STRIKE, DEFENSE_DRIVE_OFF, DEFENSE_RETURN };
        int defensePhase{DEFENSE_ALERT};
        float defenseTimer{0.0f};

        // Pack hunting
        unsigned packHuntLeader{0};   // spawnId of the wolf that initiated this hunt (0 = solo or leader)
        int packFlankIndex{-1};       // assigned flank position in the encirclement (-1 = none)

        // DnD Ability Scores — rolled at spawn, shape all NPC behavior
        struct AbilityScores
        {
            int strength{10};
            int dexterity{10};
            int constitution{10};
            int intelligence{10};
            int wisdom{10};
            int charisma{10};

            static int Mod(int score) { return (score - 10) / 2; }
            int StrMod() const { return Mod(strength); }
            int DexMod() const { return Mod(dexterity); }
            int ConMod() const { return Mod(constitution); }
            int IntMod() const { return Mod(intelligence); }
            int WisMod() const { return Mod(wisdom); }
            int ChaMod() const { return Mod(charisma); }
        };
        AbilityScores stats;

        // D&D alignment — two axes, each -1/0/+1
        // Moral: -1=evil, 0=neutral, +1=good
        // Order: -1=chaotic, 0=neutral, +1=lawful
        int alignMoral{0};
        int alignOrder{0};

        // Phase 12: Vitals damage timers
        float starveDmgTimer{0.0f};
        float thirstDmgTimer{0.0f};
        float coldDmgTimer{0.0f};
        bool exhaustionApplied{false};

        // Phase 13: God directives — player (as god) sets NPC behavior priority
        enum GodDirective : unsigned char {
            DIRECTIVE_NONE = 0,       // normal AI waterfall
            DIRECTIVE_FOCUS_TASK,     // prefer a specific STASK (param = task enum)
            DIRECTIVE_PRIORITY_AREA,  // stay near a world position (param = packed pos)
            DIRECTIVE_FORBID_TASK     // never pick a specific STASK (param = task enum)
        };
        GodDirective directive{DIRECTIVE_NONE};
        int directiveParam{0};        // task enum for FOCUS/FORBID, or area ID
        Vector3 directivePos;         // target position for PRIORITY_AREA

        // Phase 14: Family
        unsigned parentA{0};          // spawnId of parent (0 = no parent / adult)
        unsigned parentB{0};
        float growthProgress{1.0f};   // 0→1 over maturityDays game days. < 1 = child.
        int maturityDays{20};         // game days to reach adulthood (from breeding_rules DB)
        float bondCheckTimer{0.0f};   // throttle familiarity updates

        // Phase 14: Child growth state machine
        enum GrowthStage : unsigned char {
            GROWTH_INFANT = 0,        // 0.0–0.2: parent's side, eat/drink/sleep only
            GROWTH_TODDLER,           // 0.2–0.4: explores near parent, play-mimics tasks
            GROWTH_CHILD,             // 0.4–0.6: gathers, light carry, earns XP at 50%
            GROWTH_ADOLESCENT,        // 0.6–0.85: apprentices, most tasks, earns XP at 75%
            GROWTH_ADULT              // 0.85+: full capability
        };

        GrowthStage GetGrowthStage() const
        {
            if (growthProgress < 0.2f) return GROWTH_INFANT;
            if (growthProgress < 0.4f) return GROWTH_TODDLER;
            if (growthProgress < 0.6f) return GROWTH_CHILD;
            if (growthProgress < 0.85f) return GROWTH_ADOLESCENT;
            return GROWTH_ADULT;
        }

        /// XP earning rate for current growth stage (0.0 for infant/toddler)
        float GetGrowthXPMultiplier() const
        {
            switch (GetGrowthStage())
            {
            case GROWTH_INFANT:
            case GROWTH_TODDLER:    return 0.0f;
            case GROWTH_CHILD:      return 0.5f;
            case GROWTH_ADOLESCENT: return 0.75f;
            default:                return 1.0f;
            }
        }

        // Skills inherited from parents — child earns 1.25x XP in these
        Vector<int> inheritedSkillIds;

        // Phase 15: Settlement
        unsigned settlementId{0};     // campfire-based settlement index (0 = initial)
        unsigned birthSettlement{0};  // settlement where NPC was born

        // Phase 16: Domestication
        unsigned tamerId{0};          // spawnId of the NPC who tamed this animal (0 = wild)
        float tameCooldown{0.0f};     // game-seconds until next tame attempt allowed
        float milkTimer{0.0f};        // tamed cow: seconds until next milk production
        float shearCooldown{0.0f};    // tamed alpaca: seconds until next shear allowed

        // Mount system
        unsigned mountedHorse{0};     // spawnId of horse currently mounted (0 = on foot)

        // Pack animal cargo (donkey transport)
        String cargoJson;             // JSON array of {itemId, qty} — items carried by tamed pack animal
        unsigned packLeaderId{0};     // spawnId of NPC leading this pack animal (0 = idle)

        // Campfire torch patrol
        float patrolAngle{0.0f};      // current angle around campfire (degrees)

        // Phase 21: Chieftain
        bool isChieftain{false};      // highest-skilled NPC in settlement pop >= 6
        int chieftainSkillId{0};      // top skill that earned chieftain status

        // NPC stealth and scouting
        bool isScouting{false};       // currently on scout patrol (extended detection)
        bool isSneaking{false};       // crouched movement — harder to detect
        float scoutPatrolAngle{0.0f}; // current angle around perimeter (degrees)

        // Phase 25: Animal traits (selective breeding)
        float traitSize{1.0f};        // 0.8-1.2 — affects model scale, meat yield
        float traitSpeed{1.0f};       // 0.8-1.2 — affects movement speed
        float traitYield{1.0f};       // 0.8-1.2 — affects milk/food production
        float breedCooldown{0.0f};    // seconds until next breeding allowed

        // Phase 30: Naming
        String npcName;               // generated at maturity, unique within settlement

        // Phase 29: Morale
        float morale{50.0f};          // 0-100, base 50. Affects task speed (morale/50 multiplier)
        float musicBoostTimer{0.0f};  // seconds remaining of music morale boost

        // Phase 39: Prospecting
        int targetDepositType{0};     // specific trace element to seek (0 = none)

        // Botanical harvest: which property to tap (e.g. "resin", "growth")
        String harvestProperty;

        // Bark vessel contents: 0=empty, 1=fire, 2=water
        enum VesselContents : unsigned char { VESSEL_EMPTY = 0, VESSEL_FIRE = 1, VESSEL_WATER = 2 };
        VesselContents vesselContents{VESSEL_EMPTY};

        // Phase 41: Master Smith
        bool isMasterSmith{false};    // earned after 5+ alloy crafts
        int alloyCraftCount{0};       // number of named alloy items crafted

        // Master Specialists
        bool isMasterChef{false};     // Cooking 8+
        float illnessTimer{0.0f};     // seconds remaining of food poisoning (0 = healthy)
        bool illnessActive{false};    // true while poisoned — speed penalty applied
        bool isMasterHerbalist{false}; // Herbalism 8+
        bool isMasterHunter{false};    // Melee 8+ or Tracking 8+
        bool isMasterFarmer{false};    // Farming 8+
        bool isMasterBuilder{false};   // Woodwork 8+
        bool isMasterTrader{false};    // Trade 8+

        // Swimming and water traversal
        float swimTimer{0.0f};         // seconds spent swimming (resets on dry land)
    };
    HashMap<unsigned, ServerCreatureAI> creatureAI_;   // key: server-assigned spawnId

    /// Find same-species pack members within radius of a given creature.
    Vector<unsigned> FindPackMembers(unsigned spawnId, int species, float radius);

    /// Monotonic counter for server-assigned spawn tracking IDs.
    unsigned nextSpawnId_{0};

    /// Tick all tracked creature AI (called from HandleUpdate).
    void TickCreatureAI(float dt);
    /// Accumulated dt for vitals evaluation (rate-based, not per-frame).
    float vitalsTickAccum_{0.0f};
    static constexpr float VITALS_TICK_INTERVAL = 2.0f;
    /// Push creature state to all authenticated clients.
    void BroadcastCreatureAIState(unsigned nodeId, const ServerCreatureAI& ai);
    /// Map spawnId → scene node for syncing AI positions to replicated nodes.
    HashMap<unsigned, WeakPtr<Node>> creatureNodes_;
    /// Timer for creature AI broadcast rate limiting (5 Hz).
    static constexpr float CREATURE_AI_BROADCAST_INTERVAL = 0.2f;
    /// Task evaluation interval — NPCs reconsider priorities every 2s.
    static constexpr float TASK_EVAL_INTERVAL = 2.0f;
    /// Water level Y coordinate (matches client).
    static constexpr float AI_WATER_LEVEL = 5.0f;
    /// Swim stamina drain per second (faster than active drain — swimming is exhausting).
    static constexpr float SWIM_STAMINA_DRAIN_RATE = 0.5f;
    /// Max seconds an NPC can swim after stamina hits zero before drowning.
    static constexpr float SWIM_DROWN_GRACE = 10.0f;

    // --- Combat Phase 3: Situational Modifiers ---
    /// Modifier values (match PLAN_COMBAT_SYSTEM.md table).
    static constexpr int MOD_HIGH_GROUND_ATTACK = 1;   ///< Uphill advantage
    static constexpr int MOD_WATER_ATTACK       = -2;  ///< Waist-deep water penalty
    static constexpr int MOD_WATER_DEFENSE      = -2;  ///< Water makes dodging harder
    static constexpr int MOD_NIGHT_BLIND_ATTACK = -3;  ///< Can't see without torch
    static constexpr int MOD_AMBUSH_ATTACK      = 4;   ///< Target unaware, free advantage
    static constexpr int MOD_WOUNDED_ATTACK     = -2;  ///< Below 25% HP, sluggish
    /// Height diff (metres) required for high-ground bonus.
    static constexpr float HIGH_GROUND_THRESHOLD = 2.0f;
    /// Get current game hour (0.0–24.0) using Melbourne time (UTC+11).
    float GetGameHour() const;
    /// True if current game time is night (20:00–06:00).
    bool IsNightTime() const;
    /// Compute situational attack/defense adjustments for player→creature combat.
    void ComputeSituationalMods(Connection* attacker, unsigned targetSpawnId,
                                int& outAttackAdj, int& outDefenseAdj,
                                String& outLog);

    // Phase 3: Species classification (mini-Phase 8 prerequisite)
    /// True for species that are humans (cavemen). Currently 20, 21.
    static bool IsHumanSpecies(int creatureId);
    /// True for aggressive/defensive predator species (Wolf, Bull, Husky).
    static bool IsPredatorSpecies(int creatureId);
    /// True for prey species that humans hunt (Rabbit, Deer, Cow, etc).
    static bool IsPreySpecies(int creatureId);
    /// True for land creatures that can drown (everything except Fish=8).
    static bool IsLandSpecies(int creatureId);
    /// True for scavenger species (Wolf=5, Fox=3, Husky=12).
    static bool IsScavengerSpecies(int creatureId);
    /// Find nearest huntable prey for this NPC within HUNT_RANGE.
    /// Returns 0 if no prey found, else spawnId of target creature.
    unsigned FindHuntTarget(const ServerCreatureAI& hunter);
    /// Find nearest huntable predator (fox/wolf) for skilled NPCs.
    /// Requires Tracking 3+ or Melee 4+. Returns 0 if none in range.
    unsigned FindPredatorHuntTarget(const ServerCreatureAI& hunter);
    /// True for small prey that foxes can hunt (rabbit only).
    static bool IsFoxPrey(int creatureId) { return creatureId == 1; }
    /// Compute effective detection range for a detector vs a target.
    /// Factors: base range, target stealth skill + DEX mod, night penalty, scout bonus.
    float GetEffectiveDetectionRange(float baseRange, const ServerCreatureAI& detector,
                                      const ServerCreatureAI& target);

    // Death System Phase 5b: Server-side scent registry
    struct ServerScentMarker
    {
        Vector3 position;
        int speciesId{0};
        float ageSeconds{0.0f};
        unsigned spawnId{0};  // which creature died here (for burial)
    };
    Vector<ServerScentMarker> serverScents_;
    static constexpr unsigned MAX_SERVER_SCENTS = 16;
    static constexpr float SERVER_SCENT_DURATION = 90.0f;
    static constexpr float SCAVENGE_DETECTION_RADIUS = 50.0f;
    static constexpr float SCAVENGER_EAT_DURATION = 6.0f;
    static constexpr float SCAVENGER_ARRIVAL_DIST = 3.0f;
    /// Register a death scent at the given position.
    void RegisterServerScent(const Vector3& pos, int speciesId, unsigned spawnId = 0);
    /// Find nearest scent within radius. Returns index or -1.
    int FindNearestServerScent(const Vector3& pos, float maxRadius);
    /// Age and expire server scents.
    void TickServerScents(float dt);
    /// Find a live prey creature near a scent position. Returns spawnId or 0.
    unsigned FindPreyNearScent(const Vector3& scentPos, float radius);

    // Phase 2: AI tick helpers
    DeathCause UpdateCreatureVitals(ServerCreatureAI& ai, float dt);
    void MoveCreature(ServerCreatureAI& ai, float dt);
    int PickCreatureTask(const ServerCreatureAI& ai);
    void OnCreatureTaskComplete(ServerCreatureAI& ai);
    void StartCreatureTask(ServerCreatureAI& ai, int task);
    /// Award XP to an NPC via its spawnId-based playerId (D&D: learn by doing).
    void NPCAwardXP(ServerCreatureAI& ai, const String& action);
    /// Attempt a skill check: d20 + skill vs DC. Awards XP on both outcomes
    /// (full on success, half on failure). Returns true on success.
    bool NPCAttemptSkill(ServerCreatureAI& ai, int skillId, int dc, const String& xpAction);
    /// Get an NPC's level in a skill (0 if no DB or no XP earned yet).
    int GetNPCSkillLevel(unsigned spawnId, int skillId);
    /// Flood-fill heightmap to identify disconnected water bodies and cache fish spawn points.
    /// One-time cost at startup — results persisted in WorldDB.
    void DiscoverWaterBodies();
    /// Convert world position to settlement patch coords (0-15, 0-15).
    IntVector2 WorldPosToSettlementPatch(const Vector3& pos);
    /// Retroactively claim patches for existing settlements on startup.
    void MigrateExistingSettlementPatches();
    /// Get terrain height at world (x,z). Returns AI_WATER_LEVEL if no terrain.
    float GetTerrainHeightAI(float x, float z);
    /// Pick a random walkable position within radius of center, above water.
    Vector3 PickWanderTarget(const Vector3& center, float radius);
    /// Find nearest terrain point at water's edge within range. Returns zero vector if none found.
    Vector3 FindWaterEdge(const Vector3& from, float maxRange);

    // Phase 6: Night-fire-sleep cycle
    /// Get current darkness factor (0 = full daylight, 1 = deep night).
    /// Derived from UTC time at Melbourne latitude (-37.8).
    float GetDarkness() const;
    float GetEffectiveTemperature() const;

    /// Server-side campfire / fire-pit state. Phase 3 promoted this from a
    /// pure NPC-AI helper into the authoritative pit entity. Client receives
    /// updates via E_PIT_STATE_CHANGED remote event and extrapolates burn-down
    /// locally between broadcasts using utcMs delta * burnRate.
    enum FirePitState : unsigned char
    {
        PIT_UNLIT  = 0,  // no fire, no fuel
        PIT_LIT    = 1,  // burning normally
        PIT_EMBERS = 2,  // low fuel, dim light, cheap relight window
        PIT_COLD   = 3   // burned out completely, embers gone
    };
    struct ServerCampfire
    {
        Vector3 position;
        float fuelSeconds{0.0f};         // current burn-units on the pit (starts empty)
        float maxFuelSeconds{600.0f};    // hard cap
        float burnRate{1.0f};            // burn-units consumed per real second
        float wetness{0.0f};             // 0..1 — Phase 6/7 will drive this from weather
        int regionId{0};
        FirePitState state{PIT_UNLIT};   // initial — pits start cold. NPCs must ignite via friction.

        // Phase 4a: friction ignition tracking
        bool ignitionActive{false};      // true = someone is rubbing sticks on this pit
        float ignitionProgress{0.0f};    // seconds of friction accumulated
        bool ignitionByNPC{false};       // true = NPC ignitor, false = player ignitor
        unsigned ignitionNPCSpawnId{0};  // if NPC: their spawnId
        Connection* ignitionPlayerConn{nullptr}; // if player: their connection

        // Phase 24: Signal fires
        enum SignalType : unsigned char { SIGNAL_NONE = 0, SIGNAL_DANGER = 1, SIGNAL_TRADE = 2 };
        SignalType signalType{SIGNAL_NONE};
        float signalTimer{0.0f};  // seconds remaining for active signal (0 = inactive)

        // Phase 28: Fortification tracking
        int predatorAttackCount{0};  // cumulative predator attacks — triggers wall building at 3+

        // Master Chef: food quality tracking
        int lastCookSkill{0};        // Cooking skill of last NPC who cooked at this campfire

        // Water Phase 2: campfire water cache (bark vessels deposited as reserve)
        float waterReserve{0.0f};           // litres stored (each vessel deposit = 2.0)
        static constexpr float MAX_WATER_RESERVE = 20.0f;  // max cache capacity

        // Cold timer: seconds remaining before a dead pit is removed
        float coldTimer{0.0f};
    };
    HashMap<unsigned, ServerCampfire> serverCampfires_;  // key: campfire ID
    unsigned nextCampfireId_{0};
    DrivenKey burnCurveKey_;  // Non-linear burn rate from campfire_burn.json

    /// Fuel level at or below which the pit transitions LIT→EMBERS.
    static constexpr float PIT_EMBERS_THRESHOLD = 30.0f;
    /// Re-broadcast even if state hasn't changed every N seconds (anti-drift).
    static constexpr float PIT_HEARTBEAT_INTERVAL = 30.0f;
    /// Per-pit heartbeat timer to keep client extrapolation honest.
    HashMap<unsigned, float> pitHeartbeatTimers_;
    /// Send E_PIT_STATE_CHANGED to all authenticated clients.
    void BroadcastPitState(unsigned pitId, const ServerCampfire& cf);
    /// Compute burn-units delivered when adding raw wood, accounting for wetness.
    static float ApplyWetnessToTendDelivery(float rawBu, float wetness);
    /// Phase 4b: handle E_PIT_TEND_REQUEST from a connected player. Validates
    /// state (must be EMBERS), wood type (Softwood only for embers revival),
    /// proximity, and inventory. On success: consumes wood, restores pit to LIT,
    /// broadcasts new state.
    void HandlePitTendRequest(StringHash eventType, VariantMap& eventData);

    /// Fuel threshold below which a nearby NPC should add wood.
    static constexpr float CAMPFIRE_TEND_THRESHOLD = 60.0f;
    /// Seconds of fuel added per tend action (one abstract "stick").
    static constexpr float CAMPFIRE_STICK_BURN = 90.0f;
    /// NPCs spawning within this radius of an existing campfire adopt it.
    static constexpr float CAMPFIRE_ADOPT_RADIUS = 20.0f;
    /// Distance at which an NPC can tend a campfire (add fuel).
    static constexpr float CAMPFIRE_TEND_RANGE = 4.0f;
    /// Assign an NPC to a nearby existing campfire, or create a new one.
    /// Returns the campfire ID stored in ServerCreatureAI::campfireId.
    unsigned AssignCampfireForNPC(const Vector3& homePos, int regionId);
    /// Decay all server campfires' fuel each tick.
    void TickCampfires(float dt);
    /// NPC adds virtual fuel to its campfire (if alive and within reach).
    void TendCampfire(ServerCreatureAI& ai);

    // Phase 4a: Friction ignition
    /// Base time for friction fire start (2 real hours). Skill progression reduces this.
    static constexpr float FRICTION_IGNITION_TIME = 7200.0f;
    /// GameDB skill IDs (match skills_schema.sql).
    static constexpr int SKILL_MELEE      = 1;
    static constexpr int SKILL_RANGED     = 2;
    static constexpr int SKILL_DEFENSE    = 3;
    static constexpr int SKILL_STEALTH    = 4;
    static constexpr int SKILL_TRACKING   = 20;
    static constexpr int SKILL_TRAPPING   = 21;
    static constexpr int SKILL_FORAGING   = 23;
    static constexpr int SKILL_KNAPPING   = 10;
    static constexpr int SKILL_WOODWORK   = 11;
    static constexpr int SKILL_FISHING    = 22;
    static constexpr int SKILL_COOKING    = 24;
    static constexpr int SKILL_LEATHERWORK = 12;
    static constexpr int SKILL_WEAVING    = 13;
    static constexpr int SKILL_SMELTING   = 15;
    static constexpr int SKILL_SMITHING   = 16;
    static constexpr int SKILL_POTTERY    = 14;
    static constexpr int SKILL_FARMING    = 26;
    static constexpr int SKILL_FIREMAKING = 28;
    static constexpr int SKILL_HERBALISM  = 25;
    static constexpr int SKILL_TRADE     = 33;
    static constexpr int SKILL_ANIMAL_LORE = 30;
    /// Compute skill-adjusted ignition time: base / (1 + 0.1 * level). Level 10 → 720s.
    float GetSkillAdjustedIgnitionTime(int playerId) const;
    /// Must stay within this range of the pit during ignition or progress is ruined.
    static constexpr float FRICTION_IGNITION_RANGE = 3.0f;
    /// Maximum distance from a placed building to use it as a crafting station.
    static constexpr float CRAFTING_STATION_RANGE = 5.0f;
    /// Phase 35: check if NPC is near a specialized workshop for XP/quality bonus.
    /// Returns multiplier (1.0 = no bonus, 1.2 = near relevant workshop).
    float GetWorkshopProximityBonus(const ServerCreatureAI& ai, int skillId);
    /// Initial fuel (burn-seconds) granted when friction ignition completes.
    static constexpr float FRICTION_INITIAL_FUEL = 120.0f;
    /// Handle E_PIT_IGNITE_REQUEST from a player — start friction ignition on COLD/UNLIT pit.
    void HandlePitIgniteRequest(StringHash eventType, VariantMap& eventData);
    /// Cancel an active ignition on a pit (distance, predator, etc). Wood already consumed.
    void RuinIgnition(unsigned pitId, ServerCampfire& cf, const char* reason);
    /// NPC begins friction ignition on its campfire (called from OnCreatureTaskComplete).
    void NPCBeginIgnition(ServerCreatureAI& ai);

    // Phase 4c: Torch
    static constexpr int ITEM_TORCH = 108;
    static constexpr int ITEM_BURNING_TORCH = 109;
    /// Burning torch burn duration in real seconds (5 minutes).
    static constexpr float TORCH_BURN_TIME = 300.0f;
    /// Initial fuel granted when torch-igniting a pit (same as friction).
    static constexpr float TORCH_INITIAL_FUEL = 120.0f;
    /// Per-player torch burn timers. Key: playerId, value: seconds remaining.
    HashMap<int, float> torchTimers_;
    /// Handle E_TORCH_LIGHT_REQUEST — light a torch from a LIT fire.
    void HandleTorchLightRequest(StringHash eventType, VariantMap& eventData);
    /// Handle E_TORCH_IGNITE_REQUEST — use lit torch to ignite a COLD/UNLIT pit.
    void HandleTorchIgniteRequest(StringHash eventType, VariantMap& eventData);
    /// Tick torch burn timers — called from HandleUpdate.
    void TickTorchTimers(float dt);
    void TickWaterTraps(float dt);

    /// Server-side proactive land trap catch — scans creatureAI_ against armed traps.
    void TickLandTraps(float dt);
    float landTrapTickTimer_{0.0f};
    static constexpr float LAND_TRAP_TICK_INTERVAL = 5.0f;  // scan every 5s

    // Fire Carrying — resin torch, fire bundle, fire pot (burn times in real seconds)
    static constexpr int ITEM_RESIN_TORCH = 871;
    static constexpr int ITEM_FIRE_BUNDLE = 872;
    static constexpr int ITEM_BARK_VESSEL = 873;
    static constexpr float RESIN_TORCH_BURN_TIME = 7200.0f;   // 2 hours real-time
    static constexpr float FIRE_BUNDLE_BURN_TIME = 86400.0f;   // 24 hours real-time
    static constexpr float BARK_VESSEL_BURN_TIME = 259200.0f;   // 72 hours (3 days) real-time
    static constexpr float RESIN_TORCH_RAIN_THRESHOLD = 0.5f;  // survives precipitation < 0.5

    // Torch patrol — Phase 2+3 of campfire burn curve plan
    static constexpr float TORCH_TO_CAMPFIRE_RATIO = 0.5f;  // torch seconds → campfire BU efficiency
    static constexpr float PATROL_RADIUS = 15.0f;           // meters from campfire
    static constexpr float PATROL_WAYPOINT_STEP = 30.0f;    // degrees per waypoint (~12 waypoints per lap)
    /// Deposit a lit torch into the campfire as fuel. Called on patrol completion.
    bool NPCDepositTorch(ServerCreatureAI& ai);

    // Woodpile server sync — server-authoritative wood counters per placed building.
    struct ServerWoodpile
    {
        int softwoodBu{0};
        int hardwoodBu{0};
        int capacity{0};    // per-type cap (from BuildingTypeInfo::storageSlots)
    };
    HashMap<int, ServerWoodpile> serverWoodpiles_;  // key: placed building ID
    /// Handle E_WOODPILE_DEPOSIT from a player — validate + transfer inventory to pile.
    void HandleWoodpileDeposit(StringHash eventType, VariantMap& eventData);
    /// Broadcast current woodpile state to all authenticated clients.
    void BroadcastWoodpileState(int buildingId, const ServerWoodpile& wp);
    /// Initialize woodpile tracking from placed buildings DB on startup.
    void InitServerWoodpiles();

    // Phase 4: NPC inventory chain (gather → craft → trap → eat)
    /// Find a food item in NPC's inventory. Returns itemId or 0 if none.
    int FindFoodInNPCInventory(int npcPlayerId);
    /// Find a trap item in NPC's inventory. Returns itemId or 0 if none.
    int FindTrapInNPCInventory(int npcPlayerId);
    /// NPC consumes a food item from inventory, restoring hunger by item's food value.
    /// Returns true on success.
    bool NPCEatFromInventory(ServerCreatureAI& ai);
    /// NPC gathers a foraged item (Berries) into its inventory. Reuses existing
    /// AddItemToWorldInventory path so weight/slot rules apply identically to players.
    bool NPCGatherToInventory(ServerCreatureAI& ai);
    /// Phase 5: NPC gathers firewood (softwood or hardwood) into inventory.
    bool NPCGatherWoodToInventory(ServerCreatureAI& ai);
    /// Phase 23: Tick food decay — probabilistic removal of perishable items from NPC inventories.
    void TickFoodDecay();
    static constexpr int ITEM_CLAY_POT = 501;
    static constexpr int ITEM_CLAY_JAR = 502;
    /// Phase 5: Check if NPC has both wood types needed for friction ignition.
    bool NPCHasFirewood(unsigned spawnId);
    /// Phase 5: Check if NPC has softwood for tending.
    bool NPCHasSoftwood(unsigned spawnId);
    /// Phase 5: Consume wood from NPC inventory for ignition (1 softwood + 1 hardwood).
    bool NPCConsumeFirewood(ServerCreatureAI& ai);
    /// Phase 5: Consume softwood from NPC inventory for tending.
    bool NPCConsumeSoftwood(ServerCreatureAI& ai);
    static constexpr int ITEM_SOFTWOOD = 15;
    static constexpr int ITEM_HARDWOOD = 16;
    /// Shared craft logic for both players and NPCs. Validates materials, station
    /// proximity, kiln LIT check, d20 skill roll, XP award, inventory-full refund.
    /// Connection is nullable — if non-null, sends inventory update to the player client.
    bool CraftForOwner(int playerId, int recipeId, const Vector3& position,
                       const String& ownerName, Connection* connection = nullptr);
    /// NPC crafting: find best recipe this NPC can make (has ingredients + meets skill).
    /// Returns recipe ID, or -1 if nothing craftable.
    int NPCFindCraftableRecipe(const ServerCreatureAI& ai);
    /// NPC crafting: execute craft via CraftForOwner.
    bool NPCCraft(ServerCreatureAI& ai, int recipeId);
    /// NPC equip: scan inventory for weapon/tool better than current, equip it.
    bool NPCEquipBest(ServerCreatureAI& ai);
    bool NPCDressBest(ServerCreatureAI& ai);
    /// Bury a nearby corpse: remove corpse, place grave building, award XP.
    bool NPCBury(ServerCreatureAI& ai);
    /// Find a recent human corpse near the NPC's settlement. Returns spawnId or 0.
    unsigned FindBuriableCorpse(const ServerCreatureAI& ai);
    /// Transfer knowledge from dead NPC to nearby settlement members.
    void TransferKnowledge(unsigned deadSpawnId, const Vector3& position);
    /// Phase 25: Check tamed animal breeding conditions at each settlement.
    void CheckTamedBreeding();
    void CheckWildBreeding();
    /// Phase 26: Build a dugout canoe at the waterside.
    bool NPCBuildBoat(ServerCreatureAI& ai);
    /// Phase 30: Generate a unique NPC name from syllable pool. Gender- and epoch-aware. Unique within settlement.
    String GenerateNPCName(unsigned campfireId);
    /// Resolve quantity-based stack model. Returns stack variant path if available, otherwise base model.
    String ResolveStackModel(const String& baseModel, int quantity) const;
    /// Phase 34: Innovation types — emergent from skill diversity
    enum InnovationType
    {
        INNOV_CROP_ROTATION = 0,   // Farming + Herbalism → -25% consecutive penalty
        INNOV_CHARCOAL_EFF,        // Smelting + Woodwork → 1 fewer charcoal
        INNOV_MEDICINAL_FOOD,      // Herbalism + Cooking → cooked meat +5 HP
        INNOV_ALLOY_KNOWLEDGE,     // Knapping + Smelting → +1 smelting DC
        INNOV_BETTER_NETS,         // Fishing + Woodwork → +2 fishing yield
        INNOV_SELECTIVE_FEED,      // Animal Lore + Farming → trait drift +0.03
        INNOV_WEAPON_HARDENING,    // Melee + Knapping → +10% weapon durability
        INNOV_IRRIGATION_INSIGHT,  // Woodwork + Farming → channels 20% further
        INNOV_EFFICIENT_TRADE,     // Trade + any craft → trade 2 items
        INNOV_COUNT
    };
    /// Phase 34: Check for innovation discoveries (once per game day per settlement).
    void CheckInnovations();
    /// Phase 34: Query whether a settlement has a specific innovation.
    bool HasInnovation(unsigned campfireId, int innovationId);
    /// Phase 30: Record a settlement "first" achievement.
    void RecordSettlementFirst(unsigned campfireId, const String& category, unsigned npcSpawnId);
    /// Phase 28: Check if settlement has walls blocking a given species. Returns true if walled.
    bool IsSettlementWalled(unsigned campfireId, int predatorSpecies);
    /// Phase 28: Find best defensive building NPC can afford near campfire.
    int FindDefenseBuildingToBuild(const ServerCreatureAI& ai);
    /// Phase 28: Predator damages nearest wall during attack.
    void DamageNearestWall(const Vector3& pos, unsigned campfireId, int damage);
    static constexpr int WALL_ATTACK_DAMAGE = 5;  // HP per predator attack
    static constexpr int FORTIFICATION_THRESHOLD = 3;  // attacks before NPCs start building walls
    /// Phase 27: Gather herbs — adds Medicinal Herbs to NPC inventory.
    bool NPCGatherHerbs(ServerCreatureAI& ai);
    /// Phase 27: Heal injured NPC — consume herbs, restore 20 HP.
    bool NPCHeal(ServerCreatureAI& ai);
    /// Phase 27: Find injured NPC at same campfire (HP < 70%). Returns spawnId or 0.
    unsigned FindInjuredNPC(const ServerCreatureAI& ai);
    static constexpr int ITEM_MEDICINAL_HERBS = 720;
    static constexpr int ITEM_MYSTERY_ALLOY = 850;
    static constexpr float TRACE_ORE_CONSUME_CHANCE = 0.3f;
    static constexpr int ALLOY_OBSERVATION_THRESHOLD = 3;
    /// Phase 38: Check if NPC has trace ore and maybe produce Mystery Alloy instead.
    bool TryMysteryAlloy(ServerCreatureAI& ai, int npcPlayerId);
    /// Phase 38: Record a trace-alloy tool use. Triggers observation at 3 uses.
    void RecordAlloyUse(unsigned campfireId, int traceElement, unsigned npcSpawnId);
    /// Evaluate all master specialist flags (Chef, Smith, etc.) per settlement.
    void EvaluateMasters();
    /// Apply food quality effects after NPC eats cooked food.
    void ApplyFoodQuality(ServerCreatureAI& ai, int cookSkill);
    /// Phase 27: Broadcast NPC heal event to nearby clients.
    void BroadcastNPCHeal(unsigned healerSpawnId, unsigned patientSpawnId, int healAmount, bool curedPoison, const Vector3& pos);
    /// Check if settlement has a Master Herbalist.
    bool HasMasterHerbalist(unsigned campfireId);
    /// Check if settlement has a Master Hunter.
    bool HasMasterHunter(unsigned campfireId);
    /// Check if settlement has a Master Farmer.
    bool HasMasterFarmer(unsigned campfireId);
    /// Check if settlement has a Master Trader.
    bool HasMasterTrader(unsigned campfireId);
    /// Phase 41: Check if NPC qualifies as Master Smith (5+ alloy crafts).
    void CheckMasterSmith(ServerCreatureAI& ai);
    /// Phase 41: Check if settlement has a Master Smith. Returns true if present.
    bool HasMasterSmith(unsigned campfireId);
    /// Phase 41: Count known alloy types for a settlement.
    int CountKnownAlloys(unsigned campfireId);
    static constexpr int MASTER_SMITH_CRAFT_THRESHOLD = 5;
    static constexpr int ITEM_HYBRID_ALLOY = 860;
    static constexpr int ITEM_TREE_RESIN = 870;
    static constexpr int ITEM_WILLOW_BARK = 875;
    static constexpr int ITEM_EUCALYPTUS_LEAVES = 877;
    static constexpr int ITEM_SHEOAK_BARK = 879;
    static constexpr float RESIN_TAP_COOLDOWN = 86400.0f; // 24h real-time between taps
    /// Phase 39: Prospect for a specific trace element near Mine Shaft.
    bool NPCProspect(ServerCreatureAI& ai);
    /// Phase 39: Find a known trace element this settlement wants. Returns deposit type or 0.
    int FindKnownTraceElement(unsigned campfireId);
    /// Phase 39: Increment alloy_knowledge discovery count. Sets effect_known at 3.
    void IncrementAlloyKnowledge(unsigned campfireId, int elementId);
    static constexpr int BUILDING_GARRISON = 90;
    static constexpr int GARRISON_CAPACITY = 4;  // max guards per garrison
    static constexpr int BUILDING_CANOE = 70;
    static constexpr int BUILDING_WELL = 57;
    /// Initialize well state when placed. Terrain-dependent max yield.
    void InitializeWell(int placedBuildingId, float posX, float posZ);
    /// Refill wells overnight (called from daily tick).
    void TickWellRefill();
    /// Draw water from a well. Returns true if water available.
    bool DrawFromWell(int placedBuildingId);
    /// Find nearest well within range and draw from it. Returns true if successful.
    bool TryDrawFromNearbyWell(float posX, float posZ, float range);
    static constexpr int BUILDING_WATER_BARREL = 58;
    static constexpr float BARREL_CAPACITY = 20.0f;  // litres
    /// Initialize barrel state when placed. Check if under a roof.
    void InitializeBarrel(int placedBuildingId, float posX, float posZ);
    /// Tick rain collection for all barrels (called from HandleUpdate, not daily).
    void TickBarrelRainCollection(float dt);
    /// Draw water from nearest barrel. Returns true if water available.
    bool DrawFromBarrel(float posX, float posZ, float range);
    static constexpr int ITEM_PLANK = 12;
    static constexpr int ITEM_SAIL = 895;         // Canvas sail — doubles boat fishing bonus
    static constexpr int ITEM_TAPESTRY = 896;     // Woven tapestry — morale +5, shows settlement history
    static constexpr int FISHING_BOAT_BONUS = 3;  // +3 to fishing DC when near a boat
    /// Phase 25: NPC culls weakest tamed animal at their settlement for meat/hide.
    void NPCCullWeakest(ServerCreatureAI& ai);
    /// Dig irrigation channel: paint water between nearest water edge and farmland.
    bool NPCDigChannel(ServerCreatureAI& ai);
    /// Teach apprentice: award 2x XP to student, Trade XP to teacher.
    bool NPCTeach(ServerCreatureAI& ai);
    /// Find a suitable apprentice at the same campfire. Returns spawnId or 0.
    unsigned NPCFindApprentice(const ServerCreatureAI& ai, int& outSkillId);
    /// Find a farmland position owned by NPC that's too far from water. Returns position or ZERO.
    Vector3 FindDryFarmland(const ServerCreatureAI& ai);
    /// NPC chop: find nearest choppable tree in DB within range. Returns treeId or 0.
    unsigned NPCFindNearestTree(const Vector3& position, float range);
    /// NPC chop: hit tree, yield wood, award XP. Shared logic from HandleChopTree.
    bool NPCChopTree(ServerCreatureAI& ai, unsigned treeId);
    /// Phase 14: Update familiarity between nearby opposite-sex NPCs at same campfire.
    void UpdateNPCBonds(float dt);
    /// Phase 14: Check breeding conditions and spawn child if met.
    void CheckNPCBreeding();
    /// Phase 15: Count NPCs assigned to a campfire.
    int CountSettlementPopulation(unsigned campfireId);
    /// Phase 15: Count total bed capacity of buildings owned by NPCs at a campfire.
    int CountSettlementBeds(unsigned campfireId);
    /// Phase 15: Check for overcrowding and trigger settlement expansion.
    void CheckSettlementExpansion();
    /// Phase 14: Spawn child NPC with inherited skills from parents.
    void SpawnChildNPC(ServerCreatureAI& parentA, ServerCreatureAI& parentB);
    /// NPC torch: light torch from nearby fire. Shared logic from HandleTorchLightRequest.
    bool NPCLightTorch(ServerCreatureAI& ai);
    /// Shared building placement logic. Validates type, deducts materials, inserts into
    /// worldDB, broadcasts spawn. Connection is null for NPCs. Returns placed ID or -1.
    int BuildForOwner(int playerId, int buildingTypeId, const Vector3& pos, float rotation,
                      int snappedTo, const String& ownerName, Connection* connection = nullptr);
    /// Shared repair logic. Finds first affordable repair cost, deducts materials,
    /// updates HP, broadcasts. Connection is null for NPCs. Returns true if repaired.
    bool RepairForOwner(int playerId, int placedId, const String& ownerName,
                        Connection* connection = nullptr);
    /// NPC build: find best building type this NPC can afford and place it near campfire.
    /// Returns placed building ID, or -1 on failure.
    int NPCBuild(ServerCreatureAI& ai);
    /// NPC repair: find damaged owned building and repair it. Returns true if repaired.
    bool NPCRepair(ServerCreatureAI& ai);
    /// NPC burn charcoal: walk to Charcoal Kiln, craft recipe 81 via CraftForOwner.
    bool NPCBurnCharcoal(ServerCreatureAI& ai);
    /// NPC trade: find nearby NPC with surplus/need match, swap items. Returns partner spawnId or 0.
    unsigned NPCFindTradePartner(const ServerCreatureAI& ai);
    /// NPC trade: execute surplus→need swap between two NPCs. Awards Trade XP to both.
    bool NPCTrade(ServerCreatureAI& ai, unsigned partnerSpawnId);
    /// Find nearest campfire belonging to a different settlement. Returns campfire ID (0 if none).
    unsigned FindNearestForeignCampfire(const ServerCreatureAI& ai, float maxRange) const;
    /// NPC trade route: walk to foreign settlement, trade surplus for what home needs. CHA drives profit.
    bool NPCTradeRoute(ServerCreatureAI& ai);
    /// Shared mining logic. Validates deposit, calculates yield, deducts durability,
    /// grants ore. Connection is null for NPCs. Returns ore amount mined (0 on failure).
    int MineForOwner(int playerId, float worldX, float worldZ, const String& ownerName,
                     Connection* connection = nullptr);
    /// NPC mine: find nearest exposed ore deposit, mine it. Returns true if ore gained.
    bool NPCMine(ServerCreatureAI& ai);
    /// NPC smelt: find smelting recipe NPC can afford at LIT kiln, smelt via CraftForOwner.
    bool NPCSmelt(ServerCreatureAI& ai);
    /// NPC tame: approach docile animal, offer food, d20 + Animal Lore vs DC.
    /// On success sets tamerId, animal follows NPC home. Returns true if tamed.
    bool NPCTame(ServerCreatureAI& ai);
    /// Get taming DC for a creature species. Returns 0 if not tameable.
    int GetTameDC(int creatureId) const;
    /// Find nearest wild tameable animal within range. Returns spawnId or 0.
    unsigned FindNearestTameableAnimal(const ServerCreatureAI& ai, float range);
    /// Tick tamed cow milk production (called from TickCreatureAI).
    void TickTamedAnimals(float dt);
    /// Milk item ID
    static constexpr int ITEM_MILK = 13;
    /// NPC plant: find a seed in inventory, pick flat terrain near water, plant it.
    bool NPCPlantCrop(ServerCreatureAI& ai);
    /// NPC harvest: find a mature crop owned by this NPC, harvest it.
    bool NPCHarvestCrop(ServerCreatureAI& ai);
    /// NPC fish: d20 + Fishing skill vs DC at water's edge, fish to inventory.
    bool NPCFish(ServerCreatureAI& ai);
    /// NPC places a trap from inventory at its current position. Reuses
    /// PlaceTrapForOwner — same SQL/state path as HandlePlaceTrap.
    bool NPCPlaceTrap(ServerCreatureAI& ai);
    /// Core trap placement logic (extracted from HandlePlaceTrap). Validates item,
    /// decrements inventory, allocates trap nodeId, stores state, broadcasts spawn.
    /// Returns the new trap nodeId on success, or 0 on failure.
    unsigned PlaceTrapForOwner(int ownerPlayerId, int itemId, const Vector3& pos, float rot);
    /// Find a triggered (!armed) trap owned by this NPC. Returns trapNodeId or 0.
    unsigned FindTriggeredTrapOwnedBy(int ownerPlayerId);
    /// NPC harvests prey from a triggered trap. Uses HarvestForOwner — same loot
    /// rolls and inventory writes as HandleHarvest. Removes the trap from trapStates_.
    bool NPCCollectTrap(ServerCreatureAI& ai);
    /// Core harvest logic (extracted from HandleHarvest). Rolls loot, adds to inventory.
    /// Returns true if any loot was awarded.
    bool HarvestForOwner(int ownerPlayerId, int creatureId);

    // Phase 3: Hunting
    /// Tick hunting sub-state machine for a creature in STASK_HUNT.
    void TickHunt(ServerCreatureAI& ai, float dt);
    /// Roll a hunt success check. Returns true if NPC catches prey.
    bool RollHuntSuccess(const ServerCreatureAI& ai);
    /// Hunt range — maximum distance from home to search for prey.
    static constexpr float HUNT_RANGE = 50.0f;

    // Phase 5: Predator Defense
    /// Find nearest predator threatening this NPC's campfire/home.
    /// Returns spawnId of predator, or 0 if no threat within DEFENSE_TRIGGER_RANGE.
    unsigned FindDefenseTarget(const ServerCreatureAI& defender);
    /// Overload with custom detection range (Phase 36: guards have extended range).
    unsigned FindDefenseTarget(const ServerCreatureAI& defender, float range);
    /// Tick defense sub-state machine for a creature in STASK_DEFEND.
    void TickDefense(ServerCreatureAI& ai, float dt);
    /// Distance from home at which predators trigger defense response.
    static constexpr float DEFENSE_TRIGGER_RANGE = 15.0f;
    /// Extended detection range for NPCs on guard duty (Phase 36).
    static constexpr float GUARD_DETECTION_RANGE = 25.0f;
    /// Distance a predator must be driven away before defense completes.
    static constexpr float DEFENSE_DRIVEOFF_RANGE = 40.0f;

    // Phase 9: Server-side Animal AI
    /// Simplified task selection for non-human creatures. Prey flee+eat+sleep+wander.
    /// Predators hunt+eat+sleep+wander. No campfire, traps, crafting, or inventory.
    int PickAnimalTask(const ServerCreatureAI& ai);
    /// Find nearest threat to a prey animal (predators or humans within range).
    /// Returns spawnId of threat, or 0 if none found.
    unsigned FindFleeTarget(const ServerCreatureAI& prey);
    /// Distance at which prey detects threats and begins fleeing.
    static constexpr float ANIMAL_FLEE_RANGE = 20.0f;

#ifdef URHO3D_DATABASE_SQLITE
    SharedPtr<GameDB>          gameDB_;
    SharedPtr<WorldDB>         worldDB_;
    SharedPtr<CombatResolver>  combatResolver_;
    SharedPtr<PopulationManager> populationManager_;
    SharedPtr<NPCPriorityCompute> npcPriorityCompute_;
    /// Cached GPU priority results: spawnId → task category from last dispatch.
    HashMap<unsigned, int> gpuPriorityCache_;
    HungerRules hungerRules_{};
    ThirstRules thirstRules_{};
    WarmthRules warmthRules_{};
    DeathRules deathRules_{};
    InventoryRules inventoryRules_{};
    bool survivalRulesLoaded_{false};
    bool warmthRulesLoaded_{false};
    bool deathRulesLoaded_{false};
    bool inventoryRulesLoaded_{false};
    float survivalTickTimer_{0.0f};
    static constexpr float SURVIVAL_TICK_INTERVAL = 1.0f;  // check every second, drain is rate-based
    float gameTimeScale_{24.0f};  // 1 real hour = 1 game day (24 game-hours)

    // Building system
    float buildingDecayTimer_{0.0f};
    static constexpr float BUILDING_DECAY_INTERVAL = 60.0f;  // check decay every 60s
    int currentGameDay_{0};  // tracks elapsed game days for decay/repair grace periods
    int lastEconomyDay_{-1};  // last game-day when economy tick ran
    HashMap<int, BuildingTypeDBInfo> cachedBuildingTypes_;  // building_type_id → info
    void CacheBuildingTypes();
    /// Set up default priority curves for NPC compute shader evaluation.
    void SetupDefaultPriorityCurves();
    /// Batch-dispatch NPC priority evaluation via compute shader. Called once per tick.
    void DispatchNPCPriorityCompute();

    // Economic doctrine: daily resource regen, breeding, trade value updates
    void EconomyDailyTick();

    // Per-session respawn point
    // Key: connection, Value: placed_building_id of respawn shelter (0 = no respawn set)
#endif

    // Peer brokering
    void HandleRelayToAuth(Connection* connection, MemoryBuffer& msg);
    void IntroducePeers(Connection* requester, Connection* owner, int patchX, int patchZ);
    Connection* FindSessionByUsername(const String& username);
    Connection* FindSessionByGuid(const String& guid);

    // Server-authoritative edits
    void HandleEditTerrain(Connection* connection, MemoryBuffer& msg);
    void HandleEditObject(Connection* connection, MemoryBuffer& msg);
    bool ValidateEditLocation(const String& username, int adminLevel, const Vector3& worldPos);
    void BroadcastEdit(int editMsgType, const VectorBuffer& data, const String& username, Connection* excludeConnection);

    unsigned short listenPort_{63697};  // Game server (cosmic prime)

    // Authoritative scene + avatars
    SharedPtr<Scene> scene_;
    String sceneName_{"Scenes/TestScene.xml"};
    void LoadScene();
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    /// Spawn initial creature population. Called once after scene loads.
    void SpawnInitialCreatures();
    bool creaturesSpawned_{false};

    /// Spawn initial forest using Poisson disk sampling + biome awareness.
    void SpawnInitialTrees();
    /// Broadcast a tree spawn to all authenticated clients.
    void BroadcastSpawnTree(int treeId, int species, float posX, float posZ, float scale, int growthStage = 3);
    /// Send all existing trees to a newly connected client.
    void SendTreesTo(Connection* connection);
    void SendFishSpawnsTo(Connection* connection);
    void SendSettlementClaimsTo(Connection* connection);

    // Fish population tracking (Plan 1 Phase 4) — catch pressure per water body area.
    // Key: approximate area index (position hashed to 64m grid cell).
    // Value: cumulative catches in this cell. Decays slowly over time.
    HashMap<unsigned, int> fishCatchPressure_;
    float fishRecoveryTimer_{0.0f};
    static constexpr float FISH_RECOVERY_INTERVAL = 60.0f;  // every 60s, decrement all counters
    static constexpr int FISH_POP_CAPACITY = 15;             // assumed fish per area cell
    /// Get spatial hash key for fish population tracking (64m grid).
    unsigned GetFishAreaKey(float x, float z) const
    {
        int ix = (int)(x / 64.0f);
        int iz = (int)(z / 64.0f);
        return (unsigned)((ix & 0xFFFF) | ((iz & 0xFFFF) << 16));
    }
    /// Get population ratio for fishing timer scaling (1.0 = full, <1.0 = depleted).
    float GetFishPopRatio(float x, float z) const
    {
        unsigned key = GetFishAreaKey(x, z);
        auto it = fishCatchPressure_.Find(key);
        int caught = (it != fishCatchPressure_.End()) ? it->second_ : 0;
        int remaining = Max(FISH_POP_CAPACITY - caught, 2);  // min 2 — never fully depleted
        return (float)remaining / (float)FISH_POP_CAPACITY;
    }
    bool treesSpawned_{false};
    int nextTreeId_{1};
    HashMap<Connection*, WeakPtr<Node>> serverObjects_;

    // GUI
    void CreateUI();
    void CreateMenuBar(BorderImage* bg, Font* font);
    void CreateNetworkingPanel(BorderImage* bg, Font* font);
    void CreateDatabasePanel(BorderImage* bg, Font* font);
    void CreateWeatherPanel(BorderImage* bg, Font* font);
    void CreateWorldPanel(BorderImage* bg, Font* font);
    void RefreshWeatherPanel();
    void SwitchTab(int tab);
    void HandleTabClicked(StringHash eventType, VariantMap& eventData);
    void HandleGenerateTerrainPressed(StringHash eventType, VariantMap& eventData);
    void HandleConfirmTerrain(StringHash eventType, VariantMap& eventData);
    void HandleDiscardTerrain(StringHash eventType, VariantMap& eventData);

    void RefreshClientList();
    void LogMessage(const String& msg);

    // Menu bar
    Button* networkingTab_{};
    Button* databaseTab_{};
    Button* weatherTab_{};
    Button* worldTab_{};
    int activeTab_{0};

    // Panels
    BorderImage* uiBg_{};
    BorderImage* networkingPanel_{};
    BorderImage* databasePanel_{};
    BorderImage* weatherPanel_{};
    BorderImage* worldPanel_{};
    Text* terrainStatusText_{};

    // Phase 3b: Preview + Confirm
    BorderImage* previewImage_{};
    SharedPtr<Texture2D> previewTexture_;
    SharedPtr<Image> previewHeightmap_;
    int previewGridX_{};
    int previewGridZ_{};
    Button* confirmBtn_{};
    Button* discardBtn_{};
    LineEdit* gridXInput_{};
    LineEdit* gridZInput_{};

    // Phase 3b: Terrain generation parameter sliders
    Text* seedValueText_{};
    Text* freqValueText_{};
    Text* octavesValueText_{};
    Text* persistValueText_{};
    Text* ridgeValueText_{};
    Text* waterValueText_{};
    Text* gammaValueText_{};
    void HandleTerrainSliderChanged(StringHash eventType, VariantMap& eventData);
    Text* weatherConditionText_{};
    Text* weatherTempText_{};
    Text* weatherHumidityText_{};
    Text* weatherWindText_{};
    Text* weatherCloudText_{};
    Text* weatherPrecipText_{};
    Text* weatherFetchTimeText_{};
    Text* weatherBroadcastText_{};

    // Networking panel
    Text* statusText_{};
    Text* clientCountText_{};
    ListView* clientList_{};
    ListView* logList_{};
    ScrollView* logScrollView_{};
    UIElement* logContent_{};
    UIElement* logPanel_{};
    UIElement* logSelectionOverlay_{};
    Vector<String> logLines_;          // stored text for copy
    unsigned logLineCount_{0};
    static constexpr unsigned MAX_LOG_LINES = 500;
    static constexpr float LOG_LINE_HEIGHT = 14.0f;

    // Log selection (Claudette output pattern)
    bool logSelecting_{false};
    bool logHasSelection_{false};
    int logSelStartRow_{0}, logSelEndRow_{0};
    void HandleLogMouseDown(StringHash, VariantMap& eventData);
    void HandleLogMouseUp(StringHash, VariantMap& eventData);
    void HandleLogMouseMove(StringHash, VariantMap& eventData);
    void HandleLogKeyDown(StringHash, VariantMap& eventData);
    void HandleScreenMode(StringHash, VariantMap& eventData);
    int LogScreenToRow(int screenY);
    String GetLogSelectedText();
    void RenderLogSelectionOverlay();

    // Database panel
    DropDownList* tableSelector_{};
    ListView* tableView_{};
    Text* tableSchemaText_{};
    LineEdit* sqlInput_{};
    ListView* sqlResultView_{};
    String currentTable_;
    /// Which DB the current table belongs to: 0=auth, 1=gameDB, 2=worldDB
    int currentTableDb_{0};
    /// Maps table selector index → {db index, raw table name}
    struct TableEntry { int db; String name; };
    Vector<TableEntry> tableEntries_;
    /// Run a SELECT on the appropriate sqlite3 handle
    sqlite3* GetDbForIndex(int dbIndex);
    Vector<String> currentColumns_;
    Vector<int> primaryKeyIndices_;  // which columns are PKs

    void RefreshTableList();
    void LoadTableData(const String& tableName);
    void HandleTableSelected(StringHash eventType, VariantMap& eventData);
    void HandleSqlExecute(StringHash eventType, VariantMap& eventData);
    void HandleAddRow(StringHash eventType, VariantMap& eventData);
    void HandleDeleteRow(StringHash eventType, VariantMap& eventData);
    void HandleEditRow(StringHash eventType, VariantMap& eventData);

    // Edit dialog
    SharedPtr<Window> editDialog_;
    Vector<LineEdit*> editFields_;
    unsigned editRowIndex_{};
    void CreateEditDialog(unsigned rowIndex);
    void HandleEditOK(StringHash eventType, VariantMap& eventData);
    void HandleEditCancel(StringHash eventType, VariantMap& eventData);

    float uptime_{};

    // Melbourne clock display
    SharedPtr<MelbourneClock> melbourneClock_;

    // BOM Weather
    void FetchBOMWeather();
    void ProcessBOMResponse();
    void BroadcastWeather(Connection* singleClient = nullptr);
    void SendWeatherToClient(Connection* connection);

    // Celestial state (lunar ephemeris — server-authoritative)
    void BroadcastCelestialState(Connection* singleClient = nullptr);
    void SendCelestialToClient(Connection* connection);
    float CalculateMoonAge() const;  ///< Synodic month position from real date (0..29.53)
    bool IsLunarEclipse() const;     ///< True when sun-earth-moon alignment is close enough
    float celestialBroadcastTimer_{0.0f};
    static constexpr float CELESTIAL_BROADCAST_INTERVAL = 60.0f;  // every 60s

    SharedPtr<HttpRequest> bomObsRequest_;      // observations in-flight
    SharedPtr<HttpRequest> bomForecastRequest_;  // forecast in-flight
    String bomObsData_;
    String bomForecastData_;
    float weatherFetchTimer_{0.0f};
    float weatherBroadcastTimer_{0.0f};
    static constexpr float WEATHER_FETCH_INTERVAL = 21600.0f;   // fetch from BOM every 6 hours (4x/day)
    static constexpr float WEATHER_BROADCAST_INTERVAL = 10800.0f; // broadcast to clients every 3 hours
    bool weatherReady_{false};

    // Parsed weather state (broadcast to clients)
    float weatherCloudCover_{};
    float weatherPrecipitation_{};
    float weatherWindSpeed_{};
    float weatherWindAngle_{};
    float weatherTemperature_{20.0f};  // Melbourne ambient default until BOM updates
    float weatherHumidity_{};

    // Water Phase 5: Drought tracking
    int consecutiveDryDays_{0};          // days with no rain
    float droughtSeverity_{0.0f};        // 0.0=none, 1.0=severe (ramps over ~7 dry days)
    float effectiveWaterLevel_{5.0f};    // AI_WATER_LEVEL adjusted for drought (shallow water shrinks)
    void TickDrought();                  // called from daily tick
    static constexpr int DROUGHT_ONSET_DAYS = 3;     // dry days before effects begin
    static constexpr int DROUGHT_SEVERE_DAYS = 10;    // dry days for full severity
    static constexpr float DROUGHT_WATER_DROP = 1.5f; // max water level drop at full severity

    /// Admin time override — god-level only. -1 = use wallclock.
    float timeOverrideHour_{-1.0f};  // legacy, kept for compat
    int timeOverrideDOY_{-1};        // legacy, kept for compat
    /// UTC epoch override — privileged client sends their local UTC + scrub delta.
    /// When >= 0, replaces time(nullptr) in all server time calculations.
    long long utcEpochOverride_{-1};
    String weatherCondition_;  // "rain", "cloudy", "mostly_sunny", etc.

    // Water heightmap (server-authoritative, same resolution as terrain)
    SharedPtr<Image> waterHeightMap_;
    void LoadOrCreateWaterMap();
    void SaveWaterMap();
    void SendWaterMapToClient(Connection* connection);
    void HandleWaterEdit(Connection* connection, MemoryBuffer& msg);

    // Per-patch resource streaming
    void SendResourcePatch(Connection* connection, const String& resourceID, Image* resourceMap, int patchX, int patchZ);
    void SendPatchNeighbourhood(Connection* connection, int centerX, int centerZ);
    void HandlePatchPosition(Connection* connection, MemoryBuffer& msg);
    void BroadcastAffectedPatch(int patchX, int patchZ, const String& resourceID, Image* resourceMap);
    static unsigned long long PatchKey(int x, int z) { return ((unsigned long long)(unsigned)x << 32) | (unsigned)z; }

    // Metal deposit map (server-authoritative, RGBA: R=quantity, G=type, B=purity, A=depth)
    SharedPtr<Image> depositMap_;
    int depositMapSize_{0};
    void LoadOrGenerateDepositMap();
    void HandleMineRequest(Connection* connection, MemoryBuffer& msg);
    void HandleChopTree(Connection* connection, MemoryBuffer& msg);
    void HandleTapTree(Connection* connection, MemoryBuffer& msg);
    /// Generic tree harvest: query species_properties, validate skill, yield items.
    bool HarvestTreeProperty(int playerId, unsigned treeId, const String& property);
    /// NPC tap tree for resin. Returns true if resin obtained.
    bool NPCTapTree(ServerCreatureAI& ai);
    /// Find nearest harvestable tree for a given property. Returns tree_id or 0.
    unsigned FindHarvestableTree(const ServerCreatureAI& ai, const String& property);
    /// Legacy wrapper — calls FindHarvestableTree with "resin".
    unsigned FindResinTree(const ServerCreatureAI& ai);
    /// Check if settlement knows a botanical property for a species.
    bool SettlementKnowsProperty(unsigned campfireId, int species, const String& property);
    /// Record a botanical discovery for a settlement.
    void RecordBotanicalDiscovery(unsigned campfireId, int species, const String& property, const ServerCreatureAI& ai);
    void TickTreeGrowth();
    /// After a terrain edit, check if any deposits are newly exposed and notify client.
    void CheckExposedDeposits(const Vector3& editPos, float editRadius);

    // Server-authoritative terrain brush
    SharedPtr<TerrainBrush> terrainBrush_;
    void InitTerrainBrush();

    // Terrain generation
    TerrainGenerator terrainGen_;
    HashMap<IntVector2, WeakPtr<Terrain>> terrainGrid_;  // grid coords → terrain
    unsigned worldSeed_{42};  // base seed for all terrain generation
    SharedPtr<Image> GenerateTerrainHeightmap(int gridX, int gridZ);
    void CommitTerrainHeightmap(int gridX, int gridZ, SharedPtr<Image> image);
    void RegisterExistingTerrain();  // register the original TestScene terrain as grid (0,0)

    // Terrain edit journal (versioned sync for reconnecting clients)
    TerrainJournalManager journalManager_;
    void HandleTerrainSync(Connection* connection, MemoryBuffer& msg);
    float journalTrimTimer_{0.0f};
    static constexpr float JOURNAL_TRIM_INTERVAL = 3600.0f;  // trim journals every hour

    // ── Quantum Random Dice (ANU QRNG + /dev/urandom fallback) ──
    /// Fetch a batch of true random uint16s from ANU quantum RNG API.
    void FetchQuantumEntropy();
    /// Process completed QRNG HTTP response and fill the pool.
    void ProcessQRNGResponse();
    /// Fill entropy pool from /dev/urandom (kernel entropy fallback).
    void FillEntropyFromURandom(unsigned count);
    /// Consume one uint16 from the entropy pool. Refills from urandom if empty.
    unsigned short ConsumeEntropy();
    /// Roll multiple dice and sum: e.g. RollDice(3, 6) = 3d6.
    int RollDice(int numDice, int sides);
    /// Roll 4d6, drop lowest — standard D&D stat generation.
    int Roll4d6DropLowest();
    /// Get current pool size (for diagnostics / GUI).
    unsigned GetEntropyPoolSize() const { return entropyPoolSize_; }
    /// Get entropy source label ("ANU QRNG" or "/dev/urandom").
    const String& GetEntropySource() const { return entropySource_; }

    /// Ring buffer of true random uint16 values.
    static constexpr unsigned ENTROPY_POOL_CAPACITY = 2048;
    unsigned short entropyPool_[ENTROPY_POOL_CAPACITY]{};
    unsigned entropyPoolHead_{0};     ///< Next read position
    unsigned entropyPoolSize_{0};     ///< Number of valid entries
    /// Refill threshold — fetch more when pool drops below this.
    static constexpr unsigned ENTROPY_REFILL_THRESHOLD = 256;
    /// In-flight QRNG HTTP request.
    SharedPtr<HttpRequest> qrngRequest_;
    String qrngResponseData_;
    /// Timer between QRNG fetch attempts (rate limit protection).
    float qrngFetchTimer_{0.0f};
    static constexpr float QRNG_FETCH_COOLDOWN = 30.0f;  // min 30s between API calls
    /// Track entropy source for logging.
    String entropySource_{"none"};
    /// Total dice rolls served (diagnostic).
    unsigned totalDiceRolls_{0};

    // ── Claudette IPC (Unix domain socket) ──
    void InitIPC();
    void StopIPC();
    void PollIPC();
    String HandleIPCCommand(const String& message);
    int ipcListenFd_{-1};
    static constexpr const char* IPC_SOCK_PATH = "/tmp/urho_claude/tty/authserver.sock";
};
