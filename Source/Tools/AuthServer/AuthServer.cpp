// AuthServer — private central authority for TerrainNode network

#include "AuthServer.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Skybox.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/GraphicsAPI/TextureCube.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/UI/Slider.h>

#include <Urho3D/Network/SHA256.h>
#include <Urho3D/Network/CryptoRNG.h>
#include <Urho3D/Resource/JSONFile.h>
#include <SQLite/sqlite3.h>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#endif

URHO3D_DEFINE_APPLICATION_MAIN(AuthServer);

// Singleton bridge for CombatResolver external RNG callback
static AuthServer* g_authServerInstance = nullptr;
static int QuantumDiceRollBridge(int sides)
{
    if (g_authServerInstance)
        return g_authServerInstance->DiceRoll(sides);
    return (rand() % sides) + 1;
}

// Roll one ability score: 4d6, drop lowest (standard DnD method 1)
static int Roll4d6DropLowest()
{
    int rolls[4];
    for (int i = 0; i < 4; ++i)
        rolls[i] = (rand() % 6) + 1;
    int minVal = rolls[0];
    for (int i = 1; i < 4; ++i)
        if (rolls[i] < minVal) minVal = rolls[i];
    int total = 0;
    for (int i = 0; i < 4; ++i)
        total += rolls[i];
    return total - minVal;  // 3-18 range, weighted toward 12-13
}

// Controls duplicate-instance behavior:
//   true  — new instance kills the old one and takes over (hot-replace during development)
//   false — new instance dies immediately if an old one is already running (production safety)
#define SERVER_REPLACE_AT_RUNTIME true

// Cross-platform single-instance lock using a lock file with PID.
// Returns true if this instance acquired the lock and may proceed.
#ifdef _WIN32
static HANDLE lockFileHandle_ = INVALID_HANDLE_VALUE;
static bool AcquireInstanceLock()
{
    if (SERVER_REPLACE_AT_RUNTIME)
    {
        // Read old PID and kill it
        HANDLE h = CreateFileA("authserver.lock", GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            char buf[16] = {};
            DWORD bytesRead = 0;
            ReadFile(h, buf, sizeof(buf) - 1, &bytesRead, nullptr);
            CloseHandle(h);
            DWORD oldPid = (DWORD)atoi(buf);
            if (oldPid > 0)
            {
                HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, oldPid);
                if (proc)
                {
                    TerminateProcess(proc, 0);
                    CloseHandle(proc);
                    Sleep(500);  // give it time to release resources
                }
            }
        }
    }
    lockFileHandle_ = CreateFileA("authserver.lock", GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (lockFileHandle_ == INVALID_HANDLE_VALUE)
        return false;
    // Write our PID
    char pidBuf[16];
    int pidLen = snprintf(pidBuf, sizeof(pidBuf), "%lu", (unsigned long)GetCurrentProcessId());
    DWORD written;
    WriteFile(lockFileHandle_, pidBuf, pidLen, &written, nullptr);
    FlushFileBuffers(lockFileHandle_);
    return true;
}
static void ReleaseInstanceLock()
{
    if (lockFileHandle_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(lockFileHandle_);
        lockFileHandle_ = INVALID_HANDLE_VALUE;
    }
}
#else
#include <signal.h>
static int lockFileFd_ = -1;
// Lock file path — computed at runtime, but the POSIX lock code needs a C string
// initialized early. This is fine since /tmp/ exists on all POSIX platforms.
static const char* lockFilePath_ = "/tmp/urho3d_authserver.lock";
static bool AcquireInstanceLock()
{
    // Robust flock pattern: after acquiring the lock, verify the fd's inode
    // matches the on-disk path's inode. If someone deleted+recreated the file
    // between our open() and flock(), we'd hold a lock on a deleted inode while
    // another process locks the new file. Retry from scratch if inodes diverge.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        lockFileFd_ = open(lockFilePath_, O_CREAT | O_RDWR, 0600);
        if (lockFileFd_ < 0)
            return false;

        if (flock(lockFileFd_, LOCK_EX | LOCK_NB) != 0)
        {
            if (!SERVER_REPLACE_AT_RUNTIME)
            {
                close(lockFileFd_);
                lockFileFd_ = -1;
                return false;
            }
            // Kill old instance: read its PID from the lock file
            char buf[16] = {};
            ssize_t n = pread(lockFileFd_, buf, sizeof(buf) - 1, 0); (void)n;
            pid_t oldPid = (pid_t)atoi(buf);
            if (oldPid > 0)
            {
                kill(oldPid, SIGTERM);
                usleep(500000);  // 500ms for graceful shutdown
            }
            // Retry the lock
            if (flock(lockFileFd_, LOCK_EX | LOCK_NB) != 0)
            {
                // Still held — force kill
                if (oldPid > 0)
                    kill(oldPid, SIGKILL);
                usleep(200000);
                if (flock(lockFileFd_, LOCK_EX | LOCK_NB) != 0)
                {
                    close(lockFileFd_);
                    lockFileFd_ = -1;
                    return false;
                }
            }
        }

        // Verify inode: the file we locked must still be the file on disk.
        // If someone deleted the lock file, our fd points to a deleted inode
        // and another process can create+lock a new file at the same path.
        struct stat fdStat = {};
        struct stat pathStat = {};
        if (fstat(lockFileFd_, &fdStat) == 0 && ::stat(lockFilePath_, &pathStat) == 0
            && fdStat.st_dev == pathStat.st_dev && fdStat.st_ino == pathStat.st_ino)
        {
            // Inodes match — we hold the real lock
            break;
        }
        // Inode mismatch — close stale fd and retry
        close(lockFileFd_);
        lockFileFd_ = -1;
    }

    if (lockFileFd_ < 0)
        return false;

    // Write our PID
    int rc = ftruncate(lockFileFd_, 0); (void)rc;
    char pidBuf[16];
    int pidLen = snprintf(pidBuf, sizeof(pidBuf), "%d", (int)getpid());
    ssize_t w = pwrite(lockFileFd_, pidBuf, pidLen, 0); (void)w;
    return true;
}
static void ReleaseInstanceLock()
{
    if (lockFileFd_ >= 0)
    {
        flock(lockFileFd_, LOCK_UN);
        close(lockFileFd_);
        // Do NOT unlink — the file must persist so the next instance detects
        // the same inode via flock. If we unlink, the next instance creates a
        // new inode and flock succeeds without contention, allowing duplicates.
        lockFileFd_ = -1;
    }
}
#endif

// Custom message IDs (must match TerrainNode client)
static const int MSG_AUTH_LOGIN      = 100;
static const int MSG_AUTH_REGISTER   = 101;
static const int MSG_AUTH_RESULT     = 102;
// MSG_PATCH_CLAIM (0x106), MSG_PATCH_QUERY (0x108), MSG_PATCH_RESULT (0x107) — in Protocol.h
static const int MSG_LOAD_SCENE      = 103;
static const int MSG_REGISTER_GUID   = 104;  // Client → AuthServer: register NAT GUID after auth
// MSG_WEATHER_UPDATE (0x78) and MSG_CELESTIAL_STATE (0x79) now in Protocol.h
// Edit messages: MSG_EDIT_TERRAIN (0xA2), MSG_EDIT_OBJECT (0xA3),
// MSG_EDIT_REJECT (0xA4), MSG_EDIT_BROADCAST (0xA5) — defined in Protocol.h
// MSG_PEER_INTRODUCE (0x9C), MSG_PEER_READY (0x9D), MSG_PEER_CONNECT_FAILED (0x9E),
// MSG_PEER_DISCONNECTED (0x9F), MSG_RELAY_TO_AUTH (0xA0), MSG_RELAY_FROM_AUTH (0xA1)
// are defined in Protocol.h

// Remote event for telling clients which avatar node they control
static const StringHash E_CLIENTOBJECTID("ClientObjectID");
static const StringHash P_ID("ID");

// Fire System Phase 3 — pit state replication via remote event (no Protocol.h opcode).
// Same string used by client side (TerrainNode.cpp) — keep in sync.
static const StringHash E_PIT_STATE_CHANGED("PitStateChanged");
static const StringHash P_PIT_ID("PitId");
static const StringHash P_PIT_STATE("State");
static const StringHash P_PIT_BURN_UNITS("BurnUnits");
static const StringHash P_PIT_BURN_RATE("BurnRate");
static const StringHash P_PIT_WETNESS("Wetness");
static const StringHash P_PIT_POS_X("PosX");
static const StringHash P_PIT_POS_Z("PosZ");
static const StringHash P_PIT_UTC_MS("UtcMs");
static const StringHash P_PIT_MAX_FUEL("MaxFuel");
static const StringHash P_PIT_SIGNAL("Signal");

// Fire System Phase 4b — player tend request (client→server). Reuses
// E_PIT_STATE_CHANGED for the resulting broadcast back to all clients.
// Embers revival semantics: only Softwood (item 15) is accepted, only when
// the pit is in EMBERS state. Validation lives in the handler.
static const StringHash E_PIT_TEND_REQUEST("PitTendRequest");
static const StringHash P_PIT_TEND_ITEM("ItemId");
static const StringHash P_PIT_TEND_QTY("Quantity");

// Fire System Phase 4a — friction ignition (client→server + server→client status).
static const StringHash E_PIT_IGNITE_REQUEST("PitIgniteRequest");
static const StringHash E_PIT_IGNITION_STATUS("PitIgnitionStatus");
static const StringHash P_PIT_IGNITION_ACTIVE("Active");
static const StringHash P_PIT_IGNITION_PROGRESS("Progress");

// Control bit flags — must match HumanNPC.h (client-side).
static const unsigned CTRL_FORWARD = 1;
static const unsigned CTRL_BACK    = 2;
static const unsigned CTRL_LEFT    = 4;
static const unsigned CTRL_RIGHT   = 8;
static const unsigned CTRL_JUMP    = 16;
static const unsigned CTRL_SPRINT  = 32;

// Fire System Phase 4c — torch: light from fire, ignite cold pit.
static const StringHash E_TORCH_LIGHT_REQUEST("TorchLightRequest");
static const StringHash E_TORCH_IGNITE_REQUEST("TorchIgniteRequest");

// Woodpile server sync — deposit request (client→server) + state broadcast (server→clients).
static const StringHash E_WOODPILE_DEPOSIT("WoodpileDeposit");
static const StringHash E_WOODPILE_STATE("WoodpileState");
static const StringHash P_PILE_BUILDING_ID("BuildingId");
static const StringHash P_PILE_SOFTWOOD("Softwood");
static const StringHash P_PILE_HARDWOOD("Hardwood");
static const StringHash P_PILE_CAPACITY("Capacity");

// XOR each 4-byte chunk with 0x31337337 (self-inverse: apply twice = original)
static void XorObfuscate(unsigned char* data, unsigned size)
{
    static const unsigned char key[4] = { 0x31, 0x33, 0x73, 0x37 };
    for (unsigned i = 0; i < size; ++i)
        data[i] ^= key[i % 4];
}

static String HexEncode(const unsigned char* data, unsigned size)
{
    String result;
    result.Reserve(size * 2);
    for (unsigned i = 0; i < size; ++i)
    {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", data[i]);
        result += String(buf);
    }
    return result;
}

static Vector<unsigned char> HexDecode(const String& hex)
{
    Vector<unsigned char> result;
    result.Resize(hex.Length() / 2);
    for (unsigned i = 0; i < result.Size(); ++i)
    {
        unsigned byte = 0;
        sscanf(hex.CString() + i * 2, "%02x", &byte);
        result[i] = (unsigned char)byte;
    }
    return result;
}

// Hash password with SHA-256, XOR-obfuscate, return hex string (used for storage)
static String HashPasswordSHA256(const String& password)
{
    unsigned char hash[32];
    Urho3D::SHA256Hash(reinterpret_cast<const unsigned char*>(password.CString()),
                       password.Length(), hash);
    XorObfuscate(hash, 32);
    return HexEncode(hash, 32);
}

AuthServer::AuthServer(Context* context) :
    Application(context)
{
}

void AuthServer::Setup()
{
    engineParameters_[EP_WINDOW_TITLE] = "AuthServer";
    engineParameters_[EP_WINDOW_ICON] = "Icons/AuthServer.png";
    engineParameters_[EP_WINDOW_WIDTH] = 720;
    engineParameters_[EP_WINDOW_HEIGHT] = 512;
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
    engineParameters_[EP_LOG_NAME] = "AuthServer.log";
    engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
    engineParameters_[EP_FRAME_LIMITER] = true;
}

void AuthServer::Start()
{
    // Prevent duplicate instances on the same machine
    if (!AcquireInstanceLock())
    {
        URHO3D_LOGERROR("[DUPLICATE INSTANCE] AuthServer is already running — this instance will now exit");
        engine_->Exit();
        return;
    }
    URHO3D_LOGINFOF("Instance lock acquired (PID %d, replace=%s)",
#ifdef _WIN32
                    (int)GetCurrentProcessId(),
#else
                    (int)getpid(),
#endif
                    SERVER_REPLACE_AT_RUNTIME ? "true" : "false");

    // Cap FPS — AuthServer renders UI only, no need for high frame rates
    engine_->SetMaxFps(30);
    engine_->SetMaxInactiveFps(10);

    // Create debug UI first
    CreateUI();
    LogMessage("AuthServer starting...");

    // Initialize database
    InitDatabase();

    // Initialize game rules database (static — read-only at runtime)
    InitGameDB();

    // Initialize world database (dynamic — read-write, WAL mode)
    InitWorldDB();

    // Initialize server-side woodpile tracking from placed buildings
    InitServerWoodpiles();

    // Cache crop type rules from GameDB
    CacheCropTypes();

    // Load shared scene file as LOCAL — gives the server collision geometry (terrain, boxes)
    // for physics simulation without replicating any of it to clients.
    // Only REPLICATED nodes (avatars, AI entities) get sent to clients.
    LoadScene();
    RegisterExistingTerrain();
    InitTerrainBrush();
    LoadOrGenerateDepositMap();
    InitResourceMap();

    // Phase 19: Initialize ecosystem for NPC trampling/worn paths
    {
        auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
        if (terrain)
        {
            ecosystem_ = new EcosystemManager(context_);
            ecosystem_->Initialize(terrain, AI_WATER_LEVEL);
            URHO3D_LOGINFO("[Phase 19] Ecosystem initialized for NPC trampling");
        }
    }

    SpawnInitialCreatures();
    SpawnInitialTrees();

    // Seed settlement epoch cache from NPC skills
    for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
    {
        int epoch = GetSettlementEpoch(cfIt->first_);
        settlementEpochs_[cfIt->first_] = epoch;
        if (epoch > 0)
        {
            static const char* epochNames[] = {"Stone Age", "Bronze Age", "Iron Age", "Steel Age"};
            LogMessage("[Epoch] Settlement campfire " + String(cfIt->first_) +
                       " at " + String(epochNames[epoch]) + " (tier " + String(epoch) + ")");
        }
    }

    // Load campfire burn curve (non-linear fuel→burn rate mapping)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        auto* burnJson = cache->GetResource<JSONFile>("DrivenKeys/campfire_burn.json");
        if (burnJson)
        {
            DrivenKeySet burnSet;
            if (burnSet.LoadJSON(burnJson->GetRoot()) && !burnSet.keys.Empty())
            {
                burnCurveKey_ = burnSet.keys[0];
                URHO3D_LOGINFOF("[Campfire] Burn curve loaded: %d control points", burnCurveKey_.points.Size());
            }
            else
                URHO3D_LOGWARNING("[Campfire] Failed to parse campfire_burn.json — using flat burn rate");
        }
        else
            URHO3D_LOGWARNING("[Campfire] campfire_burn.json not found — using flat burn rate");
    }

    // Generate initial terrain at grid (1,0)
    GenerateTerrainHeightmap(1, 0);

    // Start network server
    auto* network = GetSubsystem<Network>();
    if (!network->StartServer(listenPort_))
    {
        LogMessage("[ERROR] Failed to start server on port " + String(listenPort_));
        return;
    }
    LogMessage("Listening on port " + String(listenPort_));

    // Register remote events that can be sent to clients
    network->RegisterRemoteEvent(E_CLIENTOBJECTID);
    network->RegisterRemoteEvent(E_PIT_STATE_CHANGED);  // Fire System Phase 3
    network->RegisterRemoteEvent(E_PIT_TEND_REQUEST);   // Fire System Phase 4b
    SubscribeToEvent(E_PIT_TEND_REQUEST, URHO3D_HANDLER(AuthServer, HandlePitTendRequest));
    network->RegisterRemoteEvent(E_PIT_IGNITE_REQUEST);  // Fire System Phase 4a
    network->RegisterRemoteEvent(E_PIT_IGNITION_STATUS); // Fire System Phase 4a (server→client)
    SubscribeToEvent(E_PIT_IGNITE_REQUEST, URHO3D_HANDLER(AuthServer, HandlePitIgniteRequest));
    network->RegisterRemoteEvent(E_TORCH_LIGHT_REQUEST);   // Fire System Phase 4c
    network->RegisterRemoteEvent(E_TORCH_IGNITE_REQUEST);  // Fire System Phase 4c
    SubscribeToEvent(E_TORCH_LIGHT_REQUEST, URHO3D_HANDLER(AuthServer, HandleTorchLightRequest));
    SubscribeToEvent(E_TORCH_IGNITE_REQUEST, URHO3D_HANDLER(AuthServer, HandleTorchIgniteRequest));
    network->RegisterRemoteEvent(E_WOODPILE_DEPOSIT);  // Woodpile sync
    network->RegisterRemoteEvent(E_WOODPILE_STATE);    // Woodpile sync (server→client)
    SubscribeToEvent(E_WOODPILE_DEPOSIT, URHO3D_HANDLER(AuthServer, HandleWoodpileDeposit));

    // Set LAN discovery beacon so clients can auto-find us
    VariantMap beacon;
    beacon["ServerName"] = String("AuthServer");
    beacon["Port"] = (int)listenPort_;
    beacon["Version"] = String("1.0");
    network->SetDiscoveryBeacon(beacon);
    LogMessage("LAN discovery beacon active");

    // Seed entropy pool from /dev/urandom immediately, then attempt QRNG top-up
    g_authServerInstance = this;
    FillEntropyFromURandom(1024);
    FetchQuantumEntropy();
    LogMessage("Entropy pool seeded: " + String(entropyPoolSize_) + " values from " + entropySource_);

    // Subscribe to events
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(AuthServer, HandleKeyDown));
    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(AuthServer, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(AuthServer, HandleClientDisconnected));
    SubscribeToEvent(E_CLIENTIDENTITY, URHO3D_HANDLER(AuthServer, HandleClientIdentity));
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(AuthServer, HandleNetworkMessage));
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(AuthServer, HandleUpdate));
    // PAKE events
    SubscribeToEvent(E_KEYEXCHANGEAUTH, URHO3D_HANDLER(AuthServer, HandleKeyExchangeAuth));
    SubscribeToEvent(E_CLIENTAUTHENTICATED, URHO3D_HANDLER(AuthServer, HandleClientAuthenticated));
    // Physics pre-step: apply client controls to server-side avatars
    SubscribeToEvent(E_PHYSICSPRESTEP, URHO3D_HANDLER(AuthServer, HandlePhysicsPreStep));

    statusText_->SetText("ONLINE — port " + String(listenPort_));
    statusText_->SetColor(Color(0.2f, 1.0f, 0.2f));

    // Melbourne clock — lower right corner
    auto* clockFont = GetSubsystem<ResourceCache>()->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    melbourneClock_ = new MelbourneClock(context_);
    melbourneClock_->Initialize(GetSubsystem<UI>()->GetRoot(), clockFont, 14);

    InitIPC();

    LogMessage("AuthServer ready.");
}

void AuthServer::Stop()
{
    g_authServerInstance = nullptr;
    StopIPC();
    SaveWaterMap();

    // Save all connected player states to world database
#ifdef URHO3D_DATABASE_SQLITE
    if (worldDB_ && worldDB_->IsOpen())
    {
        for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
        {
            ClientSession& s = it->second_;
            if (!s.authenticated)
                continue;

            int playerId = GetPlayerId(s.username);
            if (playerId <= 0)
                continue;

            PlayerState ps;
            ps.playerId = playerId;
            ps.hp = s.hp;
            ps.maxHp = s.maxHp;
            ps.hunger = s.hunger;
            ps.thirst = s.thirst;
            ps.stamina = s.stamina;
            ps.warmth = s.warmth;
            ps.alive = s.alive;
            ps.shelterId = s.respawnBuildingId;

            // Get position from server-side avatar node if available
            auto nodeIt = serverObjects_.Find(it->first_);
            if (nodeIt != serverObjects_.End() && nodeIt->second_)
                ps.position = nodeIt->second_->GetWorldPosition();

            worldDB_->SavePlayer(ps);
        }

        // currentGameDay_ tracks elapsed days, gameTimeScale_ is the speed multiplier
        worldDB_->SaveGameTime(static_cast<float>(currentGameDay_), 0.0f, 0.0f);

        // Save fire pit state
        {
            Vector<FirePitDBInfo> pitSnapshot;
            for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
            {
                FirePitDBInfo p;
                p.pitId    = cfIt->first_;
                p.position = cfIt->second_.position;
                p.fuel     = cfIt->second_.fuelSeconds;
                p.maxFuel  = cfIt->second_.maxFuelSeconds;
                p.burnRate = cfIt->second_.burnRate;
                p.wetness  = cfIt->second_.wetness;
                p.state    = static_cast<int>(cfIt->second_.state);
                p.regionId = cfIt->second_.regionId;
                pitSnapshot.Push(p);
            }
            worldDB_->SaveFirePits(pitSnapshot);
        }

        LogMessage("[WorldDB] All player and fire pit states saved on shutdown");
    }
#endif

    auto* network = GetSubsystem<Network>();
    network->StopServer();

    if (db_)
    {
        auto* database = GetSubsystem<Database>();
        database->Disconnect(db_);
        db_ = nullptr;
    }

#ifdef URHO3D_DATABASE_SQLITE
    // WorldDB closes itself in destructor (with final checkpoint)
    worldDB_.Reset();
#endif

    ReleaseInstanceLock();
}

// ============================================================
// Debug GUI
// ============================================================

void AuthServer::CreateUI()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();

    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    root->SetDefaultStyle(style);

    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    // Dark background — fills window, resizes on E_SCREENMODE
    auto* bg = root->CreateChild<BorderImage>("Background");
    bg->SetStyle("Window");
    bg->SetSize(root->GetWidth(), root->GetHeight());
    bg->SetColor(Color(0.12f, 0.12f, 0.15f));
    bg->SetLayout(LM_VERTICAL, 4, IntRect(8, 8, 8, 8));
    uiBg_ = bg;
    SubscribeToEvent(E_SCREENMODE, URHO3D_HANDLER(AuthServer, HandleScreenMode));

    // Title bar
    auto* title = bg->CreateChild<Text>("Title");
    title->SetFont(font, 16);
    title->SetText("AuthServer");
    title->SetColor(Color(0.8f, 0.8f, 1.0f));

    CreateMenuBar(bg, font);
    CreateNetworkingPanel(bg, font);
    CreateDatabasePanel(bg, font);
    CreateWeatherPanel(bg, font);
    CreateWorldPanel(bg, font);

    SwitchTab(0);  // Networking tab active by default

    GetSubsystem<Input>()->SetMouseVisible(true);
}

void AuthServer::CreateMenuBar(BorderImage* bg, Font* font)
{
    auto* menuBar = bg->CreateChild<BorderImage>("MenuBar");
    menuBar->SetStyle("Window");
    menuBar->SetColor(Color(0.18f, 0.18f, 0.22f));
    menuBar->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
    menuBar->SetFixedHeight(28);

    networkingTab_ = menuBar->CreateChild<Button>("NetworkingTab");
    networkingTab_->SetStyle("Button");
    networkingTab_->SetFixedSize(100, 24);
    auto* netLabel = networkingTab_->CreateChild<Text>("Label");
    netLabel->SetFont(font, 12);
    netLabel->SetText("Networking");
    netLabel->SetAlignment(HA_CENTER, VA_CENTER);

    databaseTab_ = menuBar->CreateChild<Button>("DatabaseTab");
    databaseTab_->SetStyle("Button");
    databaseTab_->SetFixedSize(100, 24);
    auto* dbLabel = databaseTab_->CreateChild<Text>("Label");
    dbLabel->SetFont(font, 12);
    dbLabel->SetText("Database");
    dbLabel->SetAlignment(HA_CENTER, VA_CENTER);

    weatherTab_ = menuBar->CreateChild<Button>("WeatherTab");
    weatherTab_->SetStyle("Button");
    weatherTab_->SetFixedSize(100, 24);
    auto* wxLabel = weatherTab_->CreateChild<Text>("Label");
    wxLabel->SetFont(font, 12);
    wxLabel->SetText("Weather");
    wxLabel->SetAlignment(HA_CENTER, VA_CENTER);

    worldTab_ = menuBar->CreateChild<Button>("WorldTab");
    worldTab_->SetStyle("Button");
    worldTab_->SetFixedSize(100, 24);
    auto* worldLabel = worldTab_->CreateChild<Text>("Label");
    worldLabel->SetFont(font, 12);
    worldLabel->SetText("World");
    worldLabel->SetAlignment(HA_CENTER, VA_CENTER);

    SubscribeToEvent(networkingTab_, "Released", URHO3D_HANDLER(AuthServer, HandleTabClicked));
    SubscribeToEvent(databaseTab_, "Released", URHO3D_HANDLER(AuthServer, HandleTabClicked));
    SubscribeToEvent(weatherTab_, "Released", URHO3D_HANDLER(AuthServer, HandleTabClicked));
    SubscribeToEvent(worldTab_, "Released", URHO3D_HANDLER(AuthServer, HandleTabClicked));
}

void AuthServer::CreateNetworkingPanel(BorderImage* bg, Font* font)
{
    networkingPanel_ = bg->CreateChild<BorderImage>("NetworkingPanel");
    networkingPanel_->SetColor(Color(0.12f, 0.12f, 0.15f, 0.0f));  // transparent bg
    networkingPanel_->SetLayout(LM_VERTICAL, 6, IntRect(0, 4, 0, 0));

    // Status line
    statusText_ = networkingPanel_->CreateChild<Text>("Status");
    statusText_->SetFont(font, 13);
    statusText_->SetText("STARTING...");
    statusText_->SetColor(Color(1.0f, 1.0f, 0.4f));

    // Client count
    clientCountText_ = networkingPanel_->CreateChild<Text>("ClientCount");
    clientCountText_->SetFont(font, 12);
    clientCountText_->SetText("Clients: 0");
    clientCountText_->SetColor(Color(0.7f, 0.7f, 0.7f));

    // Section: Connected Clients
    auto* clientHeader = networkingPanel_->CreateChild<Text>("ClientHeader");
    clientHeader->SetFont(font, 12);
    clientHeader->SetText("--- Connected Clients ---");
    clientHeader->SetColor(Color(0.5f, 0.7f, 1.0f));

    clientList_ = networkingPanel_->CreateChild<ListView>("ClientList");
    clientList_->SetStyle("ListView");
    clientList_->SetMinHeight(100);
    clientList_->SetMaxHeight(140);

    // Section: Activity Log
    auto* logHeader = networkingPanel_->CreateChild<Text>("LogHeader");
    logHeader->SetFont(font, 12);
    logHeader->SetText("--- Activity Log ---");
    logHeader->SetColor(Color(0.5f, 0.7f, 1.0f));

    // Activity Log — Claudette output pattern (ScrollView + per-line Text + selection overlay)
    logScrollView_ = networkingPanel_->CreateChild<ScrollView>("LogScroll");
    logScrollView_->SetStyleAuto();
    logScrollView_->SetScrollBarsVisible(false, true);
    logScrollView_->SetFocusMode(FM_NOTFOCUSABLE);
    logScrollView_->SetScrollStep(0.02f);

    logContent_ = new UIElement(context_);
    logScrollView_->SetContentElement(logContent_);

    logPanel_ = logContent_->CreateChild<UIElement>("LogPanel");
    logPanel_->SetPosition(0, 0);

    logSelectionOverlay_ = logContent_->CreateChild<UIElement>("LogSelOverlay");
    logSelectionOverlay_->SetPosition(0, 0);
    logSelectionOverlay_->SetClipChildren(true);

    // Mouse events for text selection
    SubscribeToEvent(E_MOUSEBUTTONDOWN, URHO3D_HANDLER(AuthServer, HandleLogMouseDown));
    SubscribeToEvent(E_MOUSEBUTTONUP, URHO3D_HANDLER(AuthServer, HandleLogMouseUp));
    SubscribeToEvent(E_MOUSEMOVE, URHO3D_HANDLER(AuthServer, HandleLogMouseMove));
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(AuthServer, HandleLogKeyDown));
}

void AuthServer::CreateDatabasePanel(BorderImage* bg, Font* font)
{
    databasePanel_ = bg->CreateChild<BorderImage>("DatabasePanel");
    databasePanel_->SetColor(Color(0.12f, 0.12f, 0.15f, 0.0f));
    databasePanel_->SetLayout(LM_VERTICAL, 6, IntRect(0, 4, 0, 0));

    // Top bar: table selector + buttons
    auto* topBar = databasePanel_->CreateChild<BorderImage>("TopBar");
    topBar->SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
    topBar->SetLayout(LM_HORIZONTAL, 6, IntRect(0, 0, 0, 0));
    topBar->SetFixedHeight(28);

    auto* tableLabel = topBar->CreateChild<Text>("TableLabel");
    tableLabel->SetFont(font, 12);
    tableLabel->SetText("Table:");
    tableLabel->SetColor(Color(0.7f, 0.7f, 0.7f));

    tableSelector_ = topBar->CreateChild<DropDownList>("TableSelector");
    tableSelector_->SetStyle("DropDownList");
    tableSelector_->SetFixedSize(160, 24);

    auto* addBtn = topBar->CreateChild<Button>("AddRowBtn");
    addBtn->SetStyle("Button");
    addBtn->SetFixedSize(70, 24);
    auto* addLabel = addBtn->CreateChild<Text>("Label");
    addLabel->SetFont(font, 11);
    addLabel->SetText("Add Row");
    addLabel->SetAlignment(HA_CENTER, VA_CENTER);

    auto* delBtn = topBar->CreateChild<Button>("DeleteRowBtn");
    delBtn->SetStyle("Button");
    delBtn->SetFixedSize(80, 24);
    auto* delLabel = delBtn->CreateChild<Text>("Label");
    delLabel->SetFont(font, 11);
    delLabel->SetText("Delete Row");
    delLabel->SetAlignment(HA_CENTER, VA_CENTER);

    auto* refreshBtn = topBar->CreateChild<Button>("RefreshBtn");
    refreshBtn->SetStyle("Button");
    refreshBtn->SetFixedSize(70, 24);
    auto* refreshLabel = refreshBtn->CreateChild<Text>("Label");
    refreshLabel->SetFont(font, 11);
    refreshLabel->SetText("Refresh");
    refreshLabel->SetAlignment(HA_CENTER, VA_CENTER);

    // Column headers
    tableSchemaText_ = databasePanel_->CreateChild<Text>("TableSchema");
    tableSchemaText_->SetFont(font, 11);
    tableSchemaText_->SetColor(Color(0.5f, 0.7f, 1.0f));

    // Table data view
    tableView_ = databasePanel_->CreateChild<ListView>("TableView");
    tableView_->SetStyle("ListView");
    tableView_->SetMinHeight(120);
    tableView_->SetMaxHeight(200);

    // SQL Console section
    auto* sqlLabel = databasePanel_->CreateChild<Text>("SqlLabel");
    sqlLabel->SetFont(font, 12);
    sqlLabel->SetText("--- SQL Console ---");
    sqlLabel->SetColor(Color(0.5f, 0.7f, 1.0f));

    auto* sqlBar = databasePanel_->CreateChild<BorderImage>("SqlBar");
    sqlBar->SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
    sqlBar->SetLayout(LM_HORIZONTAL, 4, IntRect(0, 0, 0, 0));
    sqlBar->SetFixedHeight(28);

    sqlInput_ = sqlBar->CreateChild<LineEdit>("SqlInput");
    sqlInput_->SetStyle("LineEdit");
    sqlInput_->SetFixedHeight(24);
    sqlInput_->SetMinWidth(500);

    auto* runBtn = sqlBar->CreateChild<Button>("RunBtn");
    runBtn->SetStyle("Button");
    runBtn->SetFixedSize(50, 24);
    auto* runLabel = runBtn->CreateChild<Text>("Label");
    runLabel->SetFont(font, 11);
    runLabel->SetText("Run");
    runLabel->SetAlignment(HA_CENTER, VA_CENTER);

    // SQL results
    sqlResultView_ = databasePanel_->CreateChild<ListView>("SqlResultView");
    sqlResultView_->SetStyle("ListView");
    sqlResultView_->SetMinHeight(100);

    // Wire events
    SubscribeToEvent(tableSelector_, "ItemSelected", URHO3D_HANDLER(AuthServer, HandleTableSelected));
    SubscribeToEvent(addBtn, "Released", URHO3D_HANDLER(AuthServer, HandleAddRow));
    SubscribeToEvent(delBtn, "Released", URHO3D_HANDLER(AuthServer, HandleDeleteRow));
    SubscribeToEvent(refreshBtn, "Released", URHO3D_HANDLER(AuthServer, HandleTableSelected));  // reuse to refresh
    SubscribeToEvent(runBtn, "Released", URHO3D_HANDLER(AuthServer, HandleSqlExecute));
    SubscribeToEvent(tableView_, "ItemDoubleClicked", URHO3D_HANDLER(AuthServer, HandleEditRow));
}

void AuthServer::CreateWeatherPanel(BorderImage* bg, Font* font)
{
    weatherPanel_ = bg->CreateChild<BorderImage>("WeatherPanel");
    weatherPanel_->SetColor(Color(0.12f, 0.12f, 0.15f, 0.0f));
    weatherPanel_->SetLayout(LM_VERTICAL, 8, IntRect(8, 8, 8, 8));

    // Title
    auto* title = weatherPanel_->CreateChild<Text>("WeatherTitle");
    title->SetFont(font, 14);
    title->SetText("Melbourne Weather (BOM)");
    title->SetColor(Color(0.5f, 0.8f, 1.0f));

    // Condition
    weatherConditionText_ = weatherPanel_->CreateChild<Text>("Condition");
    weatherConditionText_->SetFont(font, 16);
    weatherConditionText_->SetText("Awaiting first fetch...");
    weatherConditionText_->SetColor(Color(1.0f, 1.0f, 0.8f));

    // Temperature
    weatherTempText_ = weatherPanel_->CreateChild<Text>("Temperature");
    weatherTempText_->SetFont(font, 13);
    weatherTempText_->SetText("Temperature: --");
    weatherTempText_->SetColor(Color(0.9f, 0.9f, 0.9f));

    // Humidity
    weatherHumidityText_ = weatherPanel_->CreateChild<Text>("Humidity");
    weatherHumidityText_->SetFont(font, 13);
    weatherHumidityText_->SetText("Humidity: --");
    weatherHumidityText_->SetColor(Color(0.9f, 0.9f, 0.9f));

    // Wind
    weatherWindText_ = weatherPanel_->CreateChild<Text>("Wind");
    weatherWindText_->SetFont(font, 13);
    weatherWindText_->SetText("Wind: --");
    weatherWindText_->SetColor(Color(0.9f, 0.9f, 0.9f));

    // Cloud cover
    weatherCloudText_ = weatherPanel_->CreateChild<Text>("CloudCover");
    weatherCloudText_->SetFont(font, 13);
    weatherCloudText_->SetText("Cloud Cover: --");
    weatherCloudText_->SetColor(Color(0.9f, 0.9f, 0.9f));

    // Precipitation
    weatherPrecipText_ = weatherPanel_->CreateChild<Text>("Precipitation");
    weatherPrecipText_->SetFont(font, 13);
    weatherPrecipText_->SetText("Precipitation: --");
    weatherPrecipText_->SetColor(Color(0.9f, 0.9f, 0.9f));

    // Separator
    auto* sep = weatherPanel_->CreateChild<Text>("Sep");
    sep->SetFont(font, 10);
    sep->SetText("---");
    sep->SetColor(Color(0.4f, 0.4f, 0.4f));

    // Last fetch time
    weatherFetchTimeText_ = weatherPanel_->CreateChild<Text>("FetchTime");
    weatherFetchTimeText_->SetFont(font, 11);
    weatherFetchTimeText_->SetText("Last Fetch: never");
    weatherFetchTimeText_->SetColor(Color(0.6f, 0.6f, 0.6f));

    // Broadcast info
    weatherBroadcastText_ = weatherPanel_->CreateChild<Text>("Broadcast");
    weatherBroadcastText_->SetFont(font, 11);
    weatherBroadcastText_->SetText("Broadcast: on join + every 3 hours");
    weatherBroadcastText_->SetColor(Color(0.6f, 0.6f, 0.6f));
}

void AuthServer::RefreshWeatherPanel()
{
    if (!weatherReady_)
        return;

    // Pretty-print the condition
    String condition = weatherCondition_;
    condition.Replace("_", " ");
    // Capitalize first letter
    if (condition.Length() > 0)
        condition[0] = (char)toupper(condition[0]);

    if (weatherConditionText_)
        weatherConditionText_->SetText(condition);

    char buf[64];

    if (weatherTempText_)
    {
        snprintf(buf, sizeof(buf), "Temperature: %.1f C", weatherTemperature_);
        weatherTempText_->SetText(buf);
    }

    if (weatherHumidityText_)
    {
        snprintf(buf, sizeof(buf), "Humidity: %.0f%%", weatherHumidity_);
        weatherHumidityText_->SetText(buf);
    }

    if (weatherWindText_)
    {
        // Convert angle back to compass direction for display
        const char* dirs[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                              "S","SSW","SW","WSW","W","WNW","NW","NNW"};
        int dirIdx = ((int)(weatherWindAngle_ / 22.5f + 0.5f)) % 16;
        if (weatherWindSpeed_ < 0.5f)
            snprintf(buf, sizeof(buf), "Wind: Calm");
        else
            snprintf(buf, sizeof(buf), "Wind: %.0f km/h %s", weatherWindSpeed_, dirs[dirIdx]);
        weatherWindText_->SetText(buf);
    }

    if (weatherCloudText_)
    {
        snprintf(buf, sizeof(buf), "Cloud Cover: %d%%", (int)(weatherCloudCover_ * 100));
        weatherCloudText_->SetText(buf);
    }

    if (weatherPrecipText_)
    {
        snprintf(buf, sizeof(buf), "Precipitation: %d%%", (int)(weatherPrecipitation_ * 100));
        weatherPrecipText_->SetText(buf);
    }

    if (weatherFetchTimeText_)
    {
        int mins = (int)(uptime_ / 60.0f);
        int secs = (int)uptime_ % 60;
        snprintf(buf, sizeof(buf), "Last Fetch: %dm %ds uptime", mins, secs);
        weatherFetchTimeText_->SetText(buf);
    }

    if (weatherBroadcastText_)
    {
        float nextBroadcast = weatherBroadcastTimer_;
        int nextMins = (int)(nextBroadcast / 60.0f);
        snprintf(buf, sizeof(buf), "Next Broadcast: %d min (on join + every 3h)", nextMins);
        weatherBroadcastText_->SetText(buf);
    }
}

void AuthServer::HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
    using namespace KeyDown;
    int key = eventData[P_KEY].GetI32();
    if (key == KEY_ESCAPE)
        engine_->Exit();
}

void AuthServer::HandleTabClicked(StringHash eventType, VariantMap& eventData)
{
    auto* element = static_cast<UIElement*>(eventData["Element"].GetPtr());
    if (element == networkingTab_)
        SwitchTab(0);
    else if (element == databaseTab_)
        SwitchTab(1);
    else if (element == weatherTab_)
        SwitchTab(2);
    else if (element == worldTab_)
        SwitchTab(3);
}

void AuthServer::SwitchTab(int tab)
{
    activeTab_ = tab;
    networkingPanel_->SetVisible(tab == 0);
    databasePanel_->SetVisible(tab == 1);
    if (weatherPanel_)
        weatherPanel_->SetVisible(tab == 2);
    if (worldPanel_)
        worldPanel_->SetVisible(tab == 3);

    // Update tab button colors
    auto* netLabel = static_cast<Text*>(networkingTab_->GetChild("Label", false));
    auto* dbLabel = static_cast<Text*>(databaseTab_->GetChild("Label", false));
    auto* wxLabel = weatherTab_ ? static_cast<Text*>(weatherTab_->GetChild("Label", false)) : nullptr;
    auto* worldLabel = worldTab_ ? static_cast<Text*>(worldTab_->GetChild("Label", false)) : nullptr;
    if (netLabel)
        netLabel->SetColor(tab == 0 ? Color(0.8f, 0.8f, 1.0f) : Color(0.5f, 0.5f, 0.5f));
    if (dbLabel)
        dbLabel->SetColor(tab == 1 ? Color(0.8f, 0.8f, 1.0f) : Color(0.5f, 0.5f, 0.5f));
    if (wxLabel)
        wxLabel->SetColor(tab == 2 ? Color(0.8f, 0.8f, 1.0f) : Color(0.5f, 0.5f, 0.5f));
    if (worldLabel)
        worldLabel->SetColor(tab == 3 ? Color(0.8f, 0.8f, 1.0f) : Color(0.5f, 0.5f, 0.5f));

    // Refresh weather panel when switching to it
    if (tab == 2)
        RefreshWeatherPanel();

    // Populate table list on first switch to Database
    if (tab == 1 && tableSelector_->GetNumItems() == 0 && db_)
        RefreshTableList();
}

void AuthServer::CreateWorldPanel(BorderImage* bg, Font* font)
{
    worldPanel_ = bg->CreateChild<BorderImage>("WorldPanel");
    worldPanel_->SetColor(Color(0.12f, 0.12f, 0.15f, 0.0f));
    worldPanel_->SetLayout(LM_VERTICAL, 4, IntRect(8, 8, 8, 8));

    auto* title = worldPanel_->CreateChild<Text>("WorldTitle");
    title->SetFont(font, 14);
    title->SetText("World Expansion");
    title->SetColor(Color(0.5f, 0.8f, 1.0f));

    // Helper: create a labelled slider row
    auto makeSlider = [&](const String& label, float minVal, float maxVal, float value, Text*& valueText) -> Slider*
    {
        auto* row = worldPanel_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetFixedHeight(20);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font, 10);
        lbl->SetText(label);
        lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
        lbl->SetFixedWidth(90);

        auto* slider = row->CreateChild<Slider>();
        slider->SetStyleAuto();
        slider->SetFixedHeight(16);
        slider->SetMinWidth(180);
        slider->SetRange(maxVal - minVal);
        slider->SetValue(value - minVal);

        valueText = row->CreateChild<Text>();
        valueText->SetFont(font, 10);
        valueText->SetColor(Color(0.9f, 0.9f, 0.6f));
        valueText->SetFixedWidth(60);

        SubscribeToEvent(slider, "SliderChanged", URHO3D_HANDLER(AuthServer, HandleTerrainSliderChanged));
        return slider;
    };

    // Seed (display current value — randomize button changes it)
    {
        auto* row = worldPanel_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetFixedHeight(20);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font, 10);
        lbl->SetText("Seed:");
        lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
        lbl->SetFixedWidth(90);

        seedValueText_ = row->CreateChild<Text>();
        seedValueText_->SetFont(font, 10);
        seedValueText_->SetColor(Color(0.9f, 0.9f, 0.6f));

        auto* randBtn = row->CreateChild<Button>("RandSeedBtn");
        randBtn->SetStyle("Button");
        randBtn->SetFixedSize(70, 18);
        auto* rl = randBtn->CreateChild<Text>("Label");
        rl->SetFont(font, 9);
        rl->SetText("Randomize");
        rl->SetAlignment(HA_CENTER, VA_CENTER);
        SubscribeToEvent(randBtn, "Released", [this](StringHash, VariantMap&) {
            terrainGen_.params.seed = (unsigned)Random(1, 999999);
            char buf[16]; snprintf(buf, sizeof(buf), "%u", terrainGen_.params.seed);
            if (seedValueText_) seedValueText_->SetText(buf);
        });
    }

    // Parameter sliders
    makeSlider("Frequency", 0.001f, 0.02f, terrainGen_.params.baseFrequency, freqValueText_);
    makeSlider("Octaves", 1.0f, 8.0f, (float)terrainGen_.params.octaves, octavesValueText_);
    makeSlider("Persistence", 0.1f, 0.9f, terrainGen_.params.persistence, persistValueText_);
    makeSlider("Ridge Wt", 0.0f, 1.0f, terrainGen_.params.ridgeWeight, ridgeValueText_);
    makeSlider("Water Lvl", 0.0f, 0.2f, terrainGen_.params.waterLevel, waterValueText_);
    makeSlider("Height Gamma", 0.5f, 3.0f, terrainGen_.params.heightGamma, gammaValueText_);

    // Update all value labels
    HandleTerrainSliderChanged({}, GetEventDataMap());

    // Grid coordinate inputs
    {
        auto* row = worldPanel_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetFixedHeight(22);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font, 10);
        lbl->SetText("Grid cell:");
        lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
        lbl->SetFixedWidth(90);

        auto* xlbl = row->CreateChild<Text>();
        xlbl->SetFont(font, 10);
        xlbl->SetText("X");
        xlbl->SetColor(Color(0.7f, 0.7f, 0.7f));

        gridXInput_ = row->CreateChild<LineEdit>();
        gridXInput_->SetStyle("LineEdit");
        gridXInput_->SetFixedSize(40, 18);
        gridXInput_->SetText("");

        auto* zlbl = row->CreateChild<Text>();
        zlbl->SetFont(font, 10);
        zlbl->SetText("Z");
        zlbl->SetColor(Color(0.7f, 0.7f, 0.7f));

        gridZInput_ = row->CreateChild<LineEdit>();
        gridZInput_->SetStyle("LineEdit");
        gridZInput_->SetFixedSize(40, 18);
        gridZInput_->SetText("");

        auto* hint = row->CreateChild<Text>();
        hint->SetFont(font, 9);
        hint->SetText("(blank = random)");
        hint->SetColor(Color(0.5f, 0.5f, 0.5f));
    }

    // Preview button (replaces old generate-and-commit)
    auto* generateBtn = worldPanel_->CreateChild<Button>("GenerateBtn");
    generateBtn->SetStyle("Button");
    generateBtn->SetFixedSize(220, 32);
    auto* genLabel = generateBtn->CreateChild<Text>("Label");
    genLabel->SetFont(font, 12);
    genLabel->SetText("Preview Terrain");
    genLabel->SetAlignment(HA_CENTER, VA_CENTER);

    SubscribeToEvent(generateBtn, "Released", URHO3D_HANDLER(AuthServer, HandleGenerateTerrainPressed));

    // Preview thumbnail (hidden until preview is generated)
    previewImage_ = worldPanel_->CreateChild<BorderImage>("PreviewImage");
    previewImage_->SetFixedSize(200, 200);
    previewImage_->SetVisible(false);

    // Confirm / Discard row (hidden until preview is generated)
    auto* confirmRow = worldPanel_->CreateChild<UIElement>("ConfirmRow");
    confirmRow->SetLayout(LM_HORIZONTAL, 8);
    confirmRow->SetFixedHeight(32);
    confirmRow->SetVisible(false);

    confirmBtn_ = confirmRow->CreateChild<Button>("ConfirmBtn");
    confirmBtn_->SetStyle("Button");
    confirmBtn_->SetFixedSize(100, 28);
    auto* confirmLabel = confirmBtn_->CreateChild<Text>("Label");
    confirmLabel->SetFont(font, 11);
    confirmLabel->SetText("Confirm");
    confirmLabel->SetAlignment(HA_CENTER, VA_CENTER);
    confirmLabel->SetColor(Color(0.3f, 1.0f, 0.3f));
    SubscribeToEvent(confirmBtn_, "Released", URHO3D_HANDLER(AuthServer, HandleConfirmTerrain));

    discardBtn_ = confirmRow->CreateChild<Button>("DiscardBtn");
    discardBtn_->SetStyle("Button");
    discardBtn_->SetFixedSize(100, 28);
    auto* discardLabel = discardBtn_->CreateChild<Text>("Label");
    discardLabel->SetFont(font, 11);
    discardLabel->SetText("Discard");
    discardLabel->SetAlignment(HA_CENTER, VA_CENTER);
    discardLabel->SetColor(Color(1.0f, 0.4f, 0.3f));
    SubscribeToEvent(discardBtn_, "Released", URHO3D_HANDLER(AuthServer, HandleDiscardTerrain));

    terrainStatusText_ = worldPanel_->CreateChild<Text>("TerrainStatus");
    terrainStatusText_->SetFont(font, 11);
    terrainStatusText_->SetText("No terrain generated this session.");
    terrainStatusText_->SetColor(Color(0.6f, 0.6f, 0.6f));
}

void AuthServer::HandleTerrainSliderChanged(StringHash, VariantMap&)
{
    // Read slider values back into terrainGen_.params and update labels
    auto readSlider = [&](const String& name) -> float {
        auto* slider = worldPanel_ ? static_cast<Slider*>(worldPanel_->GetChild(name, true)) : nullptr;
        return slider ? slider->GetValue() : 0.0f;
    };

    // Find sliders by walking rows — each slider is the second child of its row
    // Simpler: just read all Slider children
    Vector<Slider*> sliders;
    if (worldPanel_)
    {
        for (unsigned i = 0; i < worldPanel_->GetNumChildren(); i++)
        {
            UIElement* row = worldPanel_->GetChild(i);
            for (unsigned j = 0; j < row->GetNumChildren(); j++)
            {
                auto* s = dynamic_cast<Slider*>(row->GetChild(j));
                if (s) sliders.Push(s);
            }
        }
    }

    char buf[32];
    if (sliders.Size() >= 6)
    {
        terrainGen_.params.baseFrequency = 0.001f + sliders[0]->GetValue();
        snprintf(buf, sizeof(buf), "%.4f", terrainGen_.params.baseFrequency);
        if (freqValueText_) freqValueText_->SetText(buf);

        terrainGen_.params.octaves = 1 + (int)(sliders[1]->GetValue() + 0.5f);
        snprintf(buf, sizeof(buf), "%d", terrainGen_.params.octaves);
        if (octavesValueText_) octavesValueText_->SetText(buf);

        terrainGen_.params.persistence = 0.1f + sliders[2]->GetValue();
        snprintf(buf, sizeof(buf), "%.2f", terrainGen_.params.persistence);
        if (persistValueText_) persistValueText_->SetText(buf);

        terrainGen_.params.ridgeWeight = sliders[3]->GetValue();
        snprintf(buf, sizeof(buf), "%.2f", terrainGen_.params.ridgeWeight);
        if (ridgeValueText_) ridgeValueText_->SetText(buf);

        terrainGen_.params.waterLevel = sliders[4]->GetValue();
        snprintf(buf, sizeof(buf), "%.3f", terrainGen_.params.waterLevel);
        if (waterValueText_) waterValueText_->SetText(buf);

        terrainGen_.params.heightGamma = 0.5f + sliders[5]->GetValue();
        snprintf(buf, sizeof(buf), "%.2f", terrainGen_.params.heightGamma);
        if (gammaValueText_) gammaValueText_->SetText(buf);
    }

    snprintf(buf, sizeof(buf), "%u", terrainGen_.params.seed);
    if (seedValueText_) seedValueText_->SetText(buf);
}

void AuthServer::HandleGenerateTerrainPressed(StringHash eventType, VariantMap& eventData)
{
    int gridX = 0, gridZ = 0;
    bool manualCell = false;

    // Check if user specified grid coordinates
    if (gridXInput_ && gridZInput_ &&
        !gridXInput_->GetText().Trimmed().Empty() &&
        !gridZInput_->GetText().Trimmed().Empty())
    {
        gridX = atoi(gridXInput_->GetText().Trimmed().CString());
        gridZ = atoi(gridZInput_->GetText().Trimmed().CString());
        manualCell = true;

        // Check if cell is already claimed
        if (terrainGrid_.Find(IntVector2(gridX, gridZ)) != terrainGrid_.End())
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "Cell (%d, %d) is already claimed.", gridX, gridZ);
            if (terrainStatusText_) terrainStatusText_->SetText(buf);
            return;
        }
    }
    else
    {
        // Pick a random unclaimed grid cell within [-5, 5]^2
        const int range = 5;
        bool found = false;
        for (int attempt = 0; attempt < 24 && !found; ++attempt)
        {
            int gx = (int)Random(-range, range + 1);
            int gz = (int)Random(-range, range + 1);
            if (terrainGrid_.Find(IntVector2(gx, gz)) == terrainGrid_.End())
            {
                gridX = gx;
                gridZ = gz;
                found = true;
            }
        }
        if (!found)
        {
            if (terrainStatusText_)
                terrainStatusText_->SetText("All cells in [-5,5]^2 are claimed.");
            return;
        }
    }

    // Generate heightmap preview (without committing)
    unsigned cellSeed = worldSeed_ ^ ((unsigned)(gridX * 73856093) ^ (unsigned)(gridZ * 19349663));
    terrainGen_.params.seed = cellSeed;

    // Extract edges from neighbours
    float* northEdge = nullptr;
    float* southEdge = nullptr;
    float* westEdge = nullptr;
    float* eastEdge = nullptr;
    int res = terrainGen_.params.resolution;

    auto extractEdge = [&](IntVector2 neighbourGrid, int edgeType) -> float*
    {
        auto it = terrainGrid_.Find(neighbourGrid);
        if (it == terrainGrid_.End() || it->second_.Expired())
            return nullptr;
        Image* hm = it->second_->GetHeightMap();
        if (!hm || hm->GetWidth() != res)
            return nullptr;
        float* edge = new float[res];
        unsigned char* data = hm->GetData();
        int comps = hm->GetComponents();
        for (int i = 0; i < res; ++i)
        {
            int px, py;
            if (edgeType == 0)      { px = i; py = res - 1; }
            else if (edgeType == 1) { px = i; py = 0; }
            else if (edgeType == 2) { px = res - 1; py = i; }
            else                    { px = 0; py = i; }
            int idx = (py * res + px) * comps;
            edge[i] = (float)data[idx] / 255.0f;
            if (comps >= 2)
                edge[i] += (float)data[idx + 1] / 65280.0f;
        }
        return edge;
    };

    northEdge = extractEdge(IntVector2(gridX, gridZ - 1), 0);
    southEdge = extractEdge(IntVector2(gridX, gridZ + 1), 1);
    westEdge  = extractEdge(IntVector2(gridX - 1, gridZ), 2);
    eastEdge  = extractEdge(IntVector2(gridX + 1, gridZ), 3);

    previewHeightmap_ = terrainGen_.GenerateWithEdges(context_, northEdge, southEdge, westEdge, eastEdge);

    delete[] northEdge;
    delete[] southEdge;
    delete[] westEdge;
    delete[] eastEdge;

    if (!previewHeightmap_)
    {
        if (terrainStatusText_) terrainStatusText_->SetText("Preview generation failed — check log.");
        return;
    }

    previewGridX_ = gridX;
    previewGridZ_ = gridZ;

    // Create preview texture from the generated image
    previewTexture_ = new Texture2D(context_);
    previewTexture_->SetData(previewHeightmap_);
    previewTexture_->SetFilterMode(FILTER_BILINEAR);

    // Show preview thumbnail
    if (previewImage_)
    {
        previewImage_->SetTexture(previewTexture_);
        previewImage_->SetImageRect(IntRect(0, 0, previewHeightmap_->GetWidth(), previewHeightmap_->GetHeight()));
        previewImage_->SetVisible(true);
    }

    // Show confirm/discard buttons
    auto* confirmRow = worldPanel_ ? worldPanel_->GetChild("ConfirmRow", false) : nullptr;
    if (confirmRow) confirmRow->SetVisible(true);

    char buf[128];
    snprintf(buf, sizeof(buf), "Preview: cell (%d, %d), seed %u — Confirm or Discard",
             gridX, gridZ, cellSeed);
    if (terrainStatusText_) terrainStatusText_->SetText(buf);
}

void AuthServer::HandleConfirmTerrain(StringHash, VariantMap&)
{
    if (!previewHeightmap_)
        return;

    CommitTerrainHeightmap(previewGridX_, previewGridZ_, previewHeightmap_);

    char buf[128];
    snprintf(buf, sizeof(buf), "Committed terrain at (%d, %d). Total cells: %u",
             previewGridX_, previewGridZ_, terrainGrid_.Size());
    if (terrainStatusText_) terrainStatusText_->SetText(buf);

    // Hide preview UI
    previewHeightmap_.Reset();
    previewTexture_.Reset();
    if (previewImage_) previewImage_->SetVisible(false);
    auto* confirmRow = worldPanel_ ? worldPanel_->GetChild("ConfirmRow", false) : nullptr;
    if (confirmRow) confirmRow->SetVisible(false);
}

void AuthServer::HandleDiscardTerrain(StringHash, VariantMap&)
{
    previewHeightmap_.Reset();
    previewTexture_.Reset();
    if (previewImage_) previewImage_->SetVisible(false);
    auto* confirmRow = worldPanel_ ? worldPanel_->GetChild("ConfirmRow", false) : nullptr;
    if (confirmRow) confirmRow->SetVisible(false);

    if (terrainStatusText_) terrainStatusText_->SetText("Preview discarded.");
}

void AuthServer::RefreshClientList()
{
    if (!clientList_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    clientList_->RemoveAllItems();

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        auto* line = new Text(context_);
        line->SetFont(font, 11);

        String label = it->first_->ToString();
        if (!it->second_.username.Empty())
            label += "  [" + it->second_.username + "]";
        if (it->second_.authenticated)
            label += "  (auth)";

        line->SetText(label);
        line->SetColor(it->second_.authenticated ? Color(0.3f, 1.0f, 0.3f) : Color(0.8f, 0.8f, 0.8f));
        clientList_->AddItem(line);
    }

    clientCountText_->SetText("Clients: " + String(sessions_.Size()));
}

void AuthServer::LogMessage(const String& msg)
{
    URHO3D_LOGINFO(msg);

    if (!logPanel_)
        return;

    // Timestamp
    Time* time = GetSubsystem<Time>();
    unsigned secs = time->GetTimeSinceEpoch();
    unsigned h = (secs / 3600) % 24;
    unsigned m = (secs / 60) % 60;
    unsigned s = secs % 60;
    char tsBuf[16];
    snprintf(tsBuf, sizeof(tsBuf), "[%02u:%02u:%02u] ", h, m, s);

    String line = String(tsBuf) + msg;
    logLines_.Push(line);

    // Add Text child — Claudette scrollback pattern
    auto* cache = GetSubsystem<ResourceCache>();
    auto* lineText = logPanel_->CreateChild<Text>();
    lineText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 11);
    lineText->SetColor(Color(0.8f, 0.8f, 0.8f));
    lineText->SetText(line);
    lineText->SetPosition(0, (int)(logLineCount_ * LOG_LINE_HEIGHT));
    int lineW = logScrollView_ ? logScrollView_->GetWidth() : 400;
    if (lineW < 20) lineW = 400;
    lineText->SetSize(lineW, (int)(LOG_LINE_HEIGHT + 0.5f));
    logLineCount_++;

    // Trim oldest lines if over limit
    if (logLineCount_ > MAX_LOG_LINES)
    {
        unsigned trimCount = logLineCount_ - MAX_LOG_LINES;
        for (unsigned t = 0; t < trimCount; t++)
            logPanel_->RemoveChildAtIndex(0);
        logLineCount_ -= trimCount;
        logLines_.Erase(0, trimCount);
        for (unsigned j = 0; j < logPanel_->GetNumChildren(); j++)
            logPanel_->GetChild(j)->SetPosition(0, (int)(j * LOG_LINE_HEIGHT));
    }

    // Update panel + content + overlay size
    // Width must come from scroll view (content elements start at 0 width)
    int panelW = logScrollView_ ? logScrollView_->GetWidth() : 400;
    if (panelW < 20) panelW = 400;  // fallback before layout runs
    int panelH = (int)(logLineCount_ * LOG_LINE_HEIGHT);
    logPanel_->SetSize(panelW, panelH);
    if (logSelectionOverlay_)
        logSelectionOverlay_->SetSize(panelW, panelH);
    if (logContent_)
        logContent_->SetSize(panelW, panelH);

    // Auto-scroll to bottom
    if (logScrollView_)
    {
        int viewH = logScrollView_->GetHeight();
        int maxScroll = panelH - viewH;
        if (maxScroll > 0)
            logScrollView_->SetViewPosition(IntVector2(0, maxScroll));
    }
}

// ============================================================
// Activity Log — Selection (Claudette output pattern)
// ============================================================

int AuthServer::LogScreenToRow(int screenY)
{
    if (!logScrollView_)
        return -1;
    IntVector2 viewPos = logScrollView_->GetViewPosition();
    IntVector2 scrollScreenPos = logScrollView_->GetScreenPosition();
    int contentY = screenY - scrollScreenPos.y_ + viewPos.y_;
    int row = (int)(contentY / LOG_LINE_HEIGHT);
    if (row < 0) row = 0;
    if (row >= (int)logLineCount_) row = (int)logLineCount_ - 1;
    return row;
}

void AuthServer::HandleLogMouseDown(StringHash, VariantMap& eventData)
{
    using namespace MouseButtonDown;
    if (eventData[P_BUTTON].GetI32() != MOUSEB_LEFT)
        return;

    // Only process on networking tab
    if (activeTab_ != 0 || !logScrollView_ || !logScrollView_->IsVisible())
        return;

    auto* input = GetSubsystem<Input>();
    IntVector2 pos = input->GetMousePosition();

    // Only start selection if click is inside the log scroll view
    IntVector2 svPos = logScrollView_->GetScreenPosition();
    IntVector2 svSize = logScrollView_->GetSize();
    if (pos.x_ < svPos.x_ || pos.x_ > svPos.x_ + svSize.x_ ||
        pos.y_ < svPos.y_ || pos.y_ > svPos.y_ + svSize.y_)
        return;

    int row = LogScreenToRow(pos.y_);
    if (row < 0) return;

    logSelecting_ = true;
    logHasSelection_ = false;
    logSelStartRow_ = row;
    logSelEndRow_ = row;
}

void AuthServer::HandleLogMouseUp(StringHash, VariantMap& eventData)
{
    using namespace MouseButtonUp;
    if (eventData[P_BUTTON].GetI32() != MOUSEB_LEFT)
        return;

    if (!logSelecting_)
        return;

    logSelecting_ = false;
    if (logSelStartRow_ == logSelEndRow_)
        logHasSelection_ = false;
    // Auto-copy on mouse up (like terminal select)
    if (logHasSelection_)
        GetSubsystem<UI>()->SetClipboardText(GetLogSelectedText());
}

void AuthServer::HandleLogMouseMove(StringHash, VariantMap&)
{
    if (!logSelecting_)
        return;

    auto* input = GetSubsystem<Input>();
    IntVector2 pos = input->GetMousePosition();
    int row = LogScreenToRow(pos.y_);
    if (row >= 0 && row != logSelEndRow_)
    {
        logSelEndRow_ = row;
        logHasSelection_ = true;
        RenderLogSelectionOverlay();
    }
}

void AuthServer::HandleLogKeyDown(StringHash, VariantMap& eventData)
{
    using namespace KeyDown;
    int key = eventData[P_KEY].GetI32();
    int qual = eventData[P_QUALIFIERS].GetI32();

    // Ctrl+C: copy selection to clipboard
    if (key == KEY_C && (qual & QUAL_CTRL))
    {
        if (logHasSelection_)
        {
            GetSubsystem<UI>()->SetClipboardText(GetLogSelectedText());
            logHasSelection_ = false;
            RenderLogSelectionOverlay();
        }
    }
}

String AuthServer::GetLogSelectedText()
{
    if (!logHasSelection_ || logLines_.Empty())
        return String::EMPTY;

    int r1 = logSelStartRow_, r2 = logSelEndRow_;
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }
    r1 = Max(r1, 0);
    r2 = Min(r2, (int)logLines_.Size() - 1);

    String result;
    for (int r = r1; r <= r2; r++)
    {
        result += logLines_[r];
        if (r < r2)
            result += "\n";
    }
    return result;
}

void AuthServer::RenderLogSelectionOverlay()
{
    if (!logSelectionOverlay_)
        return;

    if (!logHasSelection_)
    {
        if (logSelectionOverlay_->GetNumChildren() > 0)
            logSelectionOverlay_->RemoveAllChildren();
        return;
    }

    logSelectionOverlay_->RemoveAllChildren();

    int r1 = logSelStartRow_, r2 = logSelEndRow_;
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }

    int panelW = logScrollView_ ? logScrollView_->GetWidth() : 400;
    if (panelW < 20) panelW = 400;
    for (int r = r1; r <= r2; r++)
    {
        auto* hl = logSelectionOverlay_->CreateChild<BorderImage>();
        hl->SetPosition(0, (int)(r * LOG_LINE_HEIGHT));
        hl->SetSize(panelW, (int)(LOG_LINE_HEIGHT + 0.5f));
        hl->SetColor(Color(0.2f, 0.4f, 0.7f, 0.7f));
    }
}

// ============================================================
void AuthServer::HandleScreenMode(StringHash, VariantMap& eventData)
{
    using namespace ScreenMode;
    int w = eventData[P_WIDTH].GetI32();
    int h = eventData[P_HEIGHT].GetI32();
    if (uiBg_)
        uiBg_->SetSize(w, h);

    // Refresh log element widths after layout recalculates
    if (logScrollView_ && logPanel_ && logLineCount_ > 0)
    {
        int panelW = logScrollView_->GetWidth();
        if (panelW > 0)
        {
            int panelH = (int)(logLineCount_ * LOG_LINE_HEIGHT);
            logPanel_->SetSize(panelW, panelH);
            if (logSelectionOverlay_)
                logSelectionOverlay_->SetSize(panelW, panelH);
            if (logContent_)
                logContent_->SetSize(panelW, panelH);
        }
    }
}

// Database Editor
// ============================================================

sqlite3* AuthServer::GetDbForIndex(int dbIndex)
{
    switch (dbIndex)
    {
    case 1: return gameDB_ ? gameDB_->GetHandle() : nullptr;
    case 2: return worldDB_ ? worldDB_->GetHandle() : nullptr;
    default: return db_ ? const_cast<sqlite3*>(db_->GetConnectionImpl()) : nullptr;
    }
}

void AuthServer::RefreshTableList()
{
    if (!tableSelector_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    tableSelector_->RemoveAllItems();
    tableEntries_.Clear();

    // Helper: scan tables from a sqlite3 handle and add with prefix
    auto addTablesFrom = [&](sqlite3* handle, int dbIdx, const String& prefix, Color color)
    {
        if (!handle) return;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
                               -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                String name = (const char*)sqlite3_column_text(stmt, 0);
                TableEntry entry;
                entry.db = dbIdx;
                entry.name = name;
                tableEntries_.Push(entry);

                auto* item = new Text(context_);
                item->SetFont(font, 11);
                item->SetText(prefix + name);
                item->SetColor(color);
                tableSelector_->AddItem(item);
            }
            sqlite3_finalize(stmt);
        }
    };

    // Auth DB (white)
    if (db_)
        addTablesFrom(const_cast<sqlite3*>(db_->GetConnectionImpl()), 0, "[Auth] ", Color(0.9f, 0.9f, 0.9f));

    // GameDB (cyan — read-only rules)
    if (gameDB_ && gameDB_->IsOpen())
        addTablesFrom(gameDB_->GetHandle(), 1, "[Game] ", Color(0.5f, 0.9f, 0.9f));

    // WorldDB (yellow — mutable state)
    if (worldDB_ && worldDB_->IsOpen())
        addTablesFrom(GetDbForIndex(2), 2, "[World] ", Color(0.9f, 0.9f, 0.5f));

    if (tableSelector_->GetNumItems() > 0)
    {
        tableSelector_->SetSelection(0);
        HandleTableSelected({}, GetEventDataMap());
    }
}

void AuthServer::LoadTableData(const String& tableName)
{
    if (tableName.Empty())
        return;

    sqlite3* handle = GetDbForIndex(currentTableDb_);
    if (!handle)
        return;

    currentTable_ = tableName;
    currentColumns_.Clear();
    primaryKeyIndices_.Clear();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    // Get column info via PRAGMA (raw sqlite3 — works on any handle)
    Vector<String> colTypes;
    {
        sqlite3_stmt* stmt = nullptr;
        String pragmaSql = "PRAGMA table_info(" + tableName + ")";
        if (sqlite3_prepare_v2(handle, pragmaSql.CString(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                String colName = (const char*)sqlite3_column_text(stmt, 1);
                const char* typeText = (const char*)sqlite3_column_text(stmt, 2);
                String colType = typeText ? typeText : "TEXT";
                int pk = sqlite3_column_int(stmt, 5);

                currentColumns_.Push(colName);
                colTypes.Push(colType);
                if (pk > 0)
                    primaryKeyIndices_.Push((int)currentColumns_.Size() - 1);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Load data rows (raw sqlite3)
    struct RowData { Vector<String> cells; };
    Vector<RowData> rows;
    {
        sqlite3_stmt* stmt = nullptr;
        String selectSql = "SELECT * FROM " + tableName;
        if (sqlite3_prepare_v2(handle, selectSql.CString(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            int numCols = sqlite3_column_count(stmt);
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                RowData rd;
                for (int j = 0; j < numCols; j++)
                {
                    const char* val = (const char*)sqlite3_column_text(stmt, j);
                    rd.cells.Push(val ? String(val) : "NULL");
                }
                rows.Push(rd);
            }
            sqlite3_finalize(stmt);
        }
    }
    unsigned numCols = currentColumns_.Size();

    // Compute per-column character widths (monospace: char count = alignment)
    Vector<unsigned> colChars(numCols, 0);
    for (unsigned j = 0; j < numCols; ++j)
    {
        unsigned headerLen = currentColumns_[j].Length() + colTypes[j].Length() + 3;
        bool isPK = false;
        for (unsigned k = 0; k < primaryKeyIndices_.Size(); ++k)
            if (primaryKeyIndices_[k] == (int)j) { isPK = true; break; }
        if (isPK) headerLen += 3;
        colChars[j] = headerLen;
    }
    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        for (unsigned j = 0; j < rows[i].cells.Size() && j < numCols; ++j)
        {
            unsigned len = rows[i].cells[j].Length();
            if (len > 24) len = 24;
            if (len > colChars[j])
                colChars[j] = len;
        }
    }
    // Add 2 chars padding between columns
    for (unsigned j = 0; j < numCols; ++j)
        colChars[j] += 2;

    // Helper: pad string to fixed width (monospace)
    auto padTo = [](const String& s, unsigned width) -> String
    {
        if (s.Length() >= width)
            return s.Substring(0, width);
        String result = s;
        for (unsigned i = s.Length(); i < width; ++i)
            result += " ";
        return result;
    };

    // Build header line
    String headerLine;
    for (unsigned j = 0; j < numCols; ++j)
    {
        String label = currentColumns_[j] + " (" + colTypes[j] + ")";
        bool isPK = false;
        for (unsigned k = 0; k < primaryKeyIndices_.Size(); ++k)
            if (primaryKeyIndices_[k] == (int)j) { isPK = true; break; }
        if (isPK) label += " PK";
        headerLine += padTo(label, colChars[j]);
    }

    if (tableSchemaText_)
        tableSchemaText_->SetText(headerLine);

    // Load data rows
    if (!tableView_)
        return;

    tableView_->RemoveAllItems();

    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        String line;
        for (unsigned j = 0; j < rows[i].cells.Size() && j < numCols; ++j)
        {
            String val = rows[i].cells[j];
            if (val.Length() > 24)
                val = val.Substring(0, 21) + "...";
            line += padTo(val, colChars[j]);
        }

        auto* item = new Text(context_);
        item->SetFont(font, 11);
        item->SetText(line);
        item->SetColor(i % 2 == 0 ? Color(0.85f, 0.85f, 0.85f) : Color(0.7f, 0.7f, 0.75f));
        tableView_->AddItem(item);
    }
}

void AuthServer::HandleTableSelected(StringHash eventType, VariantMap& eventData)
{
    if (!tableSelector_)
        return;

    unsigned sel = tableSelector_->GetSelection();
    if (sel < tableEntries_.Size())
    {
        currentTableDb_ = tableEntries_[sel].db;
        LoadTableData(tableEntries_[sel].name);
    }
}

void AuthServer::HandleSqlExecute(StringHash eventType, VariantMap& eventData)
{
    if (!sqlInput_ || !sqlResultView_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    String sql = sqlInput_->GetText().Trimmed();
    if (sql.Empty())
        return;

    sqlResultView_->RemoveAllItems();

    // Route SQL to the currently selected database
    sqlite3* handle = GetDbForIndex(currentTableDb_);
    if (!handle)
        return;

    // Execute via raw sqlite3 for cross-DB compatibility
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql.CString(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        auto* errItem = new Text(context_);
        errItem->SetFont(font, 11);
        errItem->SetText(String("Error: ") + sqlite3_errmsg(handle));
        errItem->SetColor(Color(1.0f, 0.3f, 0.3f));
        sqlResultView_->AddItem(errItem);
        return;
    }

    // Collect results
    int numCols = sqlite3_column_count(stmt);
    StringVector cols;
    for (int i = 0; i < numCols; i++)
        cols.Push(sqlite3_column_name(stmt, i));

    Vector<Vector<String>> resultRows;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        Vector<String> row;
        for (int j = 0; j < numCols; j++)
        {
            const char* val = (const char*)sqlite3_column_text(stmt, j);
            row.Push(val ? String(val) : "NULL");
        }
        resultRows.Push(row);
    }
    int changes = sqlite3_changes(handle);
    sqlite3_finalize(stmt);

    // Show column headers
    if (!cols.Empty())
    {
        String headerLine;
        for (unsigned i = 0; i < cols.Size(); ++i)
        {
            if (i > 0)
                headerLine += " | ";
            headerLine += cols[i];
        }
        auto* header = new Text(context_);
        header->SetFont(font, 11);
        header->SetText(headerLine);
        header->SetColor(Color(0.5f, 0.7f, 1.0f));
        sqlResultView_->AddItem(header);
    }

    // Show rows
    for (unsigned i = 0; i < resultRows.Size(); ++i)
    {
        String line;
        for (unsigned j = 0; j < resultRows[i].Size(); ++j)
        {
            if (j > 0)
                line += " | ";
            line += resultRows[i][j];
        }
        auto* item = new Text(context_);
        item->SetFont(font, 11);
        item->SetText(line);
        item->SetColor(Color(0.85f, 0.85f, 0.85f));
        sqlResultView_->AddItem(item);
    }

    // Show affected rows for non-SELECT queries
    if (resultRows.Empty() && cols.Empty())
    {
        auto* info = new Text(context_);
        info->SetFont(font, 11);
        info->SetText(changes >= 0 ? String(changes) + " row(s) affected" : "Query executed");
        info->SetColor(Color(0.3f, 1.0f, 0.3f));
        sqlResultView_->AddItem(info);
    }

    // Refresh table view if current table might have been modified
    if (!currentTable_.Empty())
        LoadTableData(currentTable_);
}

void AuthServer::HandleAddRow(StringHash eventType, VariantMap& eventData)
{
    sqlite3* handle = GetDbForIndex(currentTableDb_);
    if (!handle || currentTable_.Empty())
        return;

    // Try INSERT with DEFAULT VALUES first
    char* errMsg = nullptr;
    String sql = "INSERT INTO " + currentTable_ + " DEFAULT VALUES";
    int rc = sqlite3_exec(handle, sql.CString(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK && !currentColumns_.Empty())
    {
        if (errMsg) sqlite3_free(errMsg);
        // Fallback: insert with empty strings for each non-PK column
        String cols, vals;
        bool first = true;
        for (unsigned i = 0; i < currentColumns_.Size(); ++i)
        {
            bool isPK = false;
            for (unsigned k = 0; k < primaryKeyIndices_.Size(); ++k)
            {
                if (primaryKeyIndices_[k] == (int)i)
                {
                    isPK = true;
                    break;
                }
            }
            if (isPK)
                continue;

            if (!first)
            {
                cols += ", ";
                vals += ", ";
            }
            cols += currentColumns_[i];
            vals += "''";
            first = false;
        }
        if (!cols.Empty())
        {
            sql = "INSERT INTO " + currentTable_ + " (" + cols + ") VALUES (" + vals + ")";
            sqlite3_exec(handle, sql.CString(), nullptr, nullptr, nullptr);
        }
    }
    else if (errMsg)
        sqlite3_free(errMsg);

    LoadTableData(currentTable_);
    LogMessage("Added row to " + currentTable_);
}

void AuthServer::HandleDeleteRow(StringHash eventType, VariantMap& eventData)
{
    sqlite3* handle = GetDbForIndex(currentTableDb_);
    if (!handle || currentTable_.Empty() || !tableView_ || primaryKeyIndices_.Empty())
        return;

    unsigned sel = tableView_->GetSelection();
    if (sel == M_MAX_UNSIGNED)
        return;

    // Re-query to get the actual data for the selected row
    sqlite3_stmt* stmt = nullptr;
    String selectSql = "SELECT * FROM " + currentTable_;
    if (sqlite3_prepare_v2(handle, selectSql.CString(), -1, &stmt, nullptr) != SQLITE_OK)
        return;

    // Skip to the selected row
    for (unsigned i = 0; i <= sel; i++)
    {
        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return;
        }
    }

    // Build WHERE clause from PKs
    String where;
    for (unsigned i = 0; i < primaryKeyIndices_.Size(); ++i)
    {
        int pkIdx = primaryKeyIndices_[i];
        const char* val = (const char*)sqlite3_column_text(stmt, pkIdx);
        if (!val) continue;

        if (i > 0)
            where += " AND ";
        where += currentColumns_[pkIdx] + " = '" + String(val) + "'";
    }
    sqlite3_finalize(stmt);

    if (!where.Empty())
    {
        String delSql = "DELETE FROM " + currentTable_ + " WHERE " + where;
        sqlite3_exec(handle, delSql.CString(), nullptr, nullptr, nullptr);
        LoadTableData(currentTable_);
        LogMessage("Deleted row from " + currentTable_);
    }
}

void AuthServer::HandleEditRow(StringHash eventType, VariantMap& eventData)
{
    if (currentTable_.Empty() || !tableView_ || !GetDbForIndex(currentTableDb_))
        return;

    unsigned sel = tableView_->GetSelection();
    if (sel == M_MAX_UNSIGNED)
        return;

    CreateEditDialog(sel);
}

void AuthServer::CreateEditDialog(unsigned rowIndex)
{
    sqlite3* handle = GetDbForIndex(currentTableDb_);
    if (!handle || currentTable_.Empty())
        return;

    // Get current row data via raw sqlite3
    sqlite3_stmt* fetchStmt = nullptr;
    String fetchSql = "SELECT * FROM " + currentTable_;
    if (sqlite3_prepare_v2(handle, fetchSql.CString(), -1, &fetchStmt, nullptr) != SQLITE_OK)
        return;

    Vector<String> rowData;
    for (unsigned i = 0; i <= rowIndex; i++)
    {
        if (sqlite3_step(fetchStmt) != SQLITE_ROW)
        {
            sqlite3_finalize(fetchStmt);
            return;
        }
    }
    int fetchCols = sqlite3_column_count(fetchStmt);
    for (int j = 0; j < fetchCols; j++)
    {
        const char* val = (const char*)sqlite3_column_text(fetchStmt, j);
        rowData.Push(val ? String(val) : "");
    }
    sqlite3_finalize(fetchStmt);
    editRowIndex_ = rowIndex;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();

    // Remove existing dialog
    if (editDialog_)
    {
        editDialog_->Remove();
        editDialog_.Reset();
    }
    editFields_.Clear();

    // Create modal window
    editDialog_ = new Window(context_);
    root->AddChild(editDialog_);
    editDialog_->SetStyle("Window");
    editDialog_->SetColor(Color(0.15f, 0.15f, 0.2f));
    editDialog_->SetLayout(LM_VERTICAL, 6, IntRect(10, 10, 10, 10));
    editDialog_->SetFixedWidth(400);
    editDialog_->SetPosition(160, 80);
    editDialog_->SetMovable(true);
    editDialog_->SetBringToFront(true);
    editDialog_->SetModal(true);

    auto* titleText = editDialog_->CreateChild<Text>("EditTitle");
    titleText->SetFont(font, 13);
    titleText->SetText("Edit Row — " + currentTable_);
    titleText->SetColor(Color(0.8f, 0.8f, 1.0f));

    // One field per column
    for (unsigned i = 0; i < currentColumns_.Size() && i < rowData.Size(); ++i)
    {
        auto* fieldRow = editDialog_->CreateChild<BorderImage>("FieldRow_" + String(i));
        fieldRow->SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
        fieldRow->SetLayout(LM_HORIZONTAL, 4, IntRect(0, 0, 0, 0));
        fieldRow->SetFixedHeight(26);

        auto* label = fieldRow->CreateChild<Text>("Label");
        label->SetFont(font, 11);
        label->SetText(currentColumns_[i]);
        label->SetColor(Color(0.6f, 0.6f, 0.7f));
        label->SetFixedWidth(120);

        auto* field = fieldRow->CreateChild<LineEdit>("Field_" + String(i));
        field->SetStyle("LineEdit");
        field->SetFixedHeight(22);
        field->SetMinWidth(240);
        field->SetText(rowData[i]);

        editFields_.Push(field);
    }

    // Button row
    auto* btnRow = editDialog_->CreateChild<BorderImage>("BtnRow");
    btnRow->SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));
    btnRow->SetLayout(LM_HORIZONTAL, 8, IntRect(0, 4, 0, 0));
    btnRow->SetFixedHeight(30);

    auto* okBtn = btnRow->CreateChild<Button>("OkBtn");
    okBtn->SetStyle("Button");
    okBtn->SetFixedSize(60, 24);
    auto* okLabel = okBtn->CreateChild<Text>("Label");
    okLabel->SetFont(font, 11);
    okLabel->SetText("Save");
    okLabel->SetAlignment(HA_CENTER, VA_CENTER);

    auto* cancelBtn = btnRow->CreateChild<Button>("CancelBtn");
    cancelBtn->SetStyle("Button");
    cancelBtn->SetFixedSize(60, 24);
    auto* cancelLabel = cancelBtn->CreateChild<Text>("Label");
    cancelLabel->SetFont(font, 11);
    cancelLabel->SetText("Cancel");
    cancelLabel->SetAlignment(HA_CENTER, VA_CENTER);

    SubscribeToEvent(okBtn, "Released", URHO3D_HANDLER(AuthServer, HandleEditOK));
    SubscribeToEvent(cancelBtn, "Released", URHO3D_HANDLER(AuthServer, HandleEditCancel));
}

void AuthServer::HandleEditOK(StringHash eventType, VariantMap& eventData)
{
    sqlite3* handle = GetDbForIndex(currentTableDb_);
    if (!handle || currentTable_.Empty() || !editDialog_ || primaryKeyIndices_.Empty())
        return;

    // Re-query to get the original PK values
    sqlite3_stmt* fetchStmt = nullptr;
    String fetchSql = "SELECT * FROM " + currentTable_;
    if (sqlite3_prepare_v2(handle, fetchSql.CString(), -1, &fetchStmt, nullptr) != SQLITE_OK)
        return;

    Vector<String> origRow;
    for (unsigned i = 0; i <= editRowIndex_; i++)
    {
        if (sqlite3_step(fetchStmt) != SQLITE_ROW)
        {
            sqlite3_finalize(fetchStmt);
            return;
        }
    }
    int numCols = sqlite3_column_count(fetchStmt);
    for (int j = 0; j < numCols; j++)
    {
        const char* val = (const char*)sqlite3_column_text(fetchStmt, j);
        origRow.Push(val ? String(val) : "");
    }
    sqlite3_finalize(fetchStmt);

    // Build SET clause
    String setClause;
    for (unsigned i = 0; i < editFields_.Size() && i < currentColumns_.Size(); ++i)
    {
        if (i > 0)
            setClause += ", ";
        String val = editFields_[i]->GetText();
        val.Replace("'", "''");
        setClause += currentColumns_[i] + " = '" + val + "'";
    }

    // Build WHERE from PKs using original values
    String where;
    for (unsigned i = 0; i < primaryKeyIndices_.Size(); ++i)
    {
        int pkIdx = primaryKeyIndices_[i];
        if (pkIdx >= (int)origRow.Size())
            continue;
        if (i > 0)
            where += " AND ";
        String val = origRow[pkIdx];
        val.Replace("'", "''");
        where += currentColumns_[pkIdx] + " = '" + val + "'";
    }

    if (!setClause.Empty() && !where.Empty())
    {
        String updateSql = "UPDATE " + currentTable_ + " SET " + setClause + " WHERE " + where;
        sqlite3_exec(handle, updateSql.CString(), nullptr, nullptr, nullptr);
        LoadTableData(currentTable_);
        LogMessage("Updated row in " + currentTable_);
    }

    editDialog_->Remove();
    editDialog_.Reset();
    editFields_.Clear();
}

void AuthServer::HandleEditCancel(StringHash eventType, VariantMap& eventData)
{
    if (editDialog_)
    {
        editDialog_->Remove();
        editDialog_.Reset();
    }
    editFields_.Clear();
}

// ============================================================
// Database
// ============================================================

void AuthServer::InitDatabase()
{
    auto* database = GetSubsystem<Database>();
    if (!database)
    {
        LogMessage("[ERROR] Database subsystem not available");
        return;
    }

    db_ = database->Connect("authserver.db");
    if (!db_ || !db_->IsConnected())
    {
        LogMessage("[ERROR] Failed to open authserver.db");
        return;
    }

    db_->Execute(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  pake_hash TEXT NOT NULL DEFAULT '',"
        "  admin_level INTEGER NOT NULL DEFAULT 0,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );

    // pake_hash column already in CREATE TABLE above — no migration needed

    db_->Execute(
        "CREATE TABLE IF NOT EXISTS patches ("
        "  patch_x INTEGER NOT NULL,"
        "  patch_z INTEGER NOT NULL,"
        "  owner_name TEXT NOT NULL,"
        "  claimed_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (patch_x, patch_z),"
        "  FOREIGN KEY (owner_name) REFERENCES users(username)"
        ")"
    );

    // Seed default superuser if DB is fresh (no users yet)
    {
        DbResult check = db_->Execute("SELECT COUNT(*) FROM users");
        if (!check.GetRows().Empty() && check.GetRows()[0][0].GetI32() == 0)
        {
            String hashHex = HashPasswordSHA256("admin");
            db_->Execute(
                "INSERT INTO users (username, password_hash, pake_hash, admin_level) "
                "VALUES ('admin', '" + hashHex + "', '" + hashHex + "', 25773)"
            );
            LogMessage("Created default superuser 'admin' (level 25773) — password hashed");
        }
    }

    // Migration: hash any existing plaintext passwords (pake_hash is empty)
    {
        DbResult unhashed = db_->Execute(
            "SELECT username, password_hash FROM users WHERE pake_hash = ''"
        );
        for (unsigned i = 0; i < unhashed.GetNumRows(); ++i)
        {
            String user = unhashed.GetRows()[i][0].GetString();
            String plaintext = unhashed.GetRows()[i][1].GetString();
            String hashHex = HashPasswordSHA256(plaintext);
            db_->Execute(
                "UPDATE users SET password_hash = '" + SqlEscape(hashHex) +
                "', pake_hash = '" + SqlEscape(hashHex) +
                "' WHERE username = '" + SqlEscape(user) + "'"
            );
            LogMessage("Migrated password hash for user '" + user + "'");
        }
    }

    // Migration: allocate random patches to users who don't have one
    {
        DbResult unpatched = db_->Execute(
            "SELECT username FROM users WHERE username NOT IN (SELECT owner_name FROM patches)"
        );
        for (unsigned i = 0; i < unpatched.GetNumRows(); ++i)
        {
            String user = unpatched.GetRows()[i][0].GetString();
            int px, pz;
            if (AllocateRandomPatch(user, px, pz))
                LogMessage("Allocated patch (" + String(px) + "," + String(pz) + ") to " + user);
            else
                LogMessage("[WARN] No free patches for user " + user);
        }
    }

    // Report existing data
    DbResult userCount = db_->Execute("SELECT COUNT(*) FROM users");
    DbResult patchCount = db_->Execute("SELECT COUNT(*) FROM patches");
    String users = !userCount.GetRows().Empty() ? String(userCount.GetRows()[0][0].GetI32()) : "0";
    String patches = !patchCount.GetRows().Empty() ? String(patchCount.GetRows()[0][0].GetI32()) : "0";

    LogMessage("Database: " + users + " users, " + patches + " patches");
}

// ============================================================
// Scene loading
// ============================================================

void AuthServer::LoadScene()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* sceneFile = cache->GetResource<XMLFile>(sceneName_, false);

    if (sceneFile)
    {
        // Load existing scene directly — no wrapper node
        scene_ = new Scene(context_);
        // Use File-based LoadXML (not XMLElement) so scene->GetFileName() is set.
        // Connection::SetScene() sends GetFileName() in MSG_LOADSCENE to clients.
        SharedPtr<File> sceneFileStream = cache->GetFile(sceneName_);
        if (sceneFileStream && scene_->LoadXML(*sceneFileStream))
            LogMessage("Loaded scene: " + sceneName_ + " (fileName=" + scene_->GetFileName() + ")");
        else
        {
            LogMessage("[ERROR] Failed to load scene: " + sceneName_);
            scene_.Reset();
        }
        return;
    }

    // --- Bootstrap: generate a new scene from scratch ---
    LogMessage("[BOOTSTRAP] Scene '" + sceneName_ + "' not found — generating new world");

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>(LOCAL);
    scene_->CreateComponent<PhysicsWorld>(LOCAL);

    // Zone — fog and ambient
    Node* zoneNode = scene_->CreateChild("Zone", LOCAL);
    auto* zone = zoneNode->CreateComponent<Zone>(LOCAL);
    zone->SetBoundingBox(BoundingBox(-2000.0f, 2000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.5f, 0.6f, 0.7f));
    zone->SetFogStart(500.0f);
    zone->SetFogEnd(750.0f);

    // Directional light (sun)
    Node* lightNode = scene_->CreateChild("DirectionalLight", LOCAL);
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode->CreateComponent<Light>(LOCAL);
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(1.0f, 0.95f, 0.8f));
    light->SetBrightness(1.0f);
    light->SetCastShadows(true);
    light->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    light->SetShadowCascade(CascadeParameters(20.0f, 60.0f, 180.0f, 560.0f, 0.1f));

    // Moon light
    Node* moonNode = scene_->CreateChild("MoonLight", LOCAL);
    moonNode->SetDirection(Vector3(-0.6f, -1.0f, -0.8f));
    auto* moonLight = moonNode->CreateComponent<Light>(LOCAL);
    moonLight->SetLightType(LIGHT_DIRECTIONAL);
    moonLight->SetColor(Color(0.3f, 0.35f, 0.5f));
    moonLight->SetBrightness(0.0f);  // off by default, night cycle turns it on

    // Skybox
    Node* skyNode = scene_->CreateChild("Sky", LOCAL);
    auto* skybox = skyNode->CreateComponent<Skybox>(LOCAL);
    skybox->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    skybox->SetMaterial(cache->GetResource<Material>("Materials/Skybox.xml"));

    // Generate terrain heightmap
    SharedPtr<Image> heightMap = terrainGen_.GenerateWithEdges(context_, nullptr, nullptr, nullptr, nullptr);
    if (!heightMap)
    {
        LogMessage("[ERROR] Failed to generate bootstrap heightmap");
        return;
    }

    // Save heightmap to Data/Textures/ so both server and client can find it
    String hmResourcePath = "Textures/TerrainHeightMap.png";
    String hmFullPath = cache->GetResourceDirs()[0] + hmResourcePath;
    heightMap->SavePNG(hmFullPath);
    LogMessage("[BOOTSTRAP] Saved heightmap: " + hmFullPath);

    // Register in ResourceCache so Terrain::GetHeightMapAttr() can serialize the reference
    heightMap->SetName(hmResourcePath);
    cache->AddManualResource(heightMap);

    // Terrain node — offset Y=-20 so terrain valleys dip below water level (Y=5),
    // matching the client's procedural path in CreateSceneGraph().
    Node* terrainNode = scene_->CreateChild("Terrain", LOCAL);
    terrainNode->SetPosition(Vector3(0.0f, -20.0f, 0.0f));
    auto* terrain = terrainNode->CreateComponent<Terrain>(LOCAL);
    terrain->SetSpacing(Vector3(2.0f, 0.5f, 2.0f));
    terrain->SetHeightMap(heightMap);
    auto* terrainMat = cache->GetResource<Material>("Materials/Terrain.xml", false);
    if (terrainMat)
        terrain->SetMaterial(terrainMat);
    terrain->SetCastShadows(true);

    // Terrain collision — static body so avatars don't fall through
    auto* terrainBody = terrainNode->CreateComponent<RigidBody>(LOCAL);
    terrainBody->SetCollisionLayer(2);
    auto* terrainShape = terrainNode->CreateComponent<CollisionShape>(LOCAL);
    terrainShape->SetTerrain();

    // Load or create water heightmap (same resolution as terrain)
    LoadOrCreateWaterMap();

    // Water plane (flat quad at y=5.0)
    Node* waterNode = scene_->CreateChild("Water", LOCAL);
    waterNode->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    waterNode->SetScale(Vector3(2048.0f, 1.0f, 2048.0f));
    auto* waterModel = waterNode->CreateComponent<StaticModel>(LOCAL);
    waterModel->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    auto* waterMat = cache->GetResource<Material>("Materials/Water.xml", false);
    if (waterMat)
        waterModel->SetMaterial(waterMat);

    // Save the bootstrapped scene to disk so it persists
    auto* fs = GetSubsystem<FileSystem>();
    String scenesDir = cache->GetResourceDirs()[0] + "Scenes";
    if (!fs->DirExists(scenesDir))
        fs->CreateDir(scenesDir);

    String sceneFullPath = cache->GetResourceDirs()[0] + sceneName_;
    File saveFile(context_, sceneFullPath, FILE_WRITE);
    if (scene_->SaveXML(saveFile))
        LogMessage("[BOOTSTRAP] Saved scene: " + sceneFullPath);
    else
        LogMessage("[ERROR] Failed to save bootstrap scene");
}

void AuthServer::InitTerrainBrush()
{
    if (!scene_)
        return;

    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain)
    {
        LogMessage("[WARN] No terrain in scene — server brush not available");
        return;
    }

    auto* heightMap = terrain->GetHeightMap();
    if (!heightMap)
    {
        LogMessage("[WARN] Terrain has no heightmap — server brush not available");
        return;
    }

    terrainBrush_ = new TerrainBrush(context_);
    terrainBrush_->SetTerrain(terrain, heightMap);
    if (waterHeightMap_)
        terrainBrush_->SetWaterMap(waterHeightMap_);

    LogMessage("Server terrain brush initialized (" +
               String(heightMap->GetWidth()) + "x" + String(heightMap->GetHeight()) + ")");
}

void AuthServer::LoadOrGenerateDepositMap()
{
    auto* fileSystem = GetSubsystem<FileSystem>();
    auto* cache = GetSubsystem<ResourceCache>();
    String path = fileSystem->GetProgramDir() + "Data/Textures/MetalDeposits.png";

    if (fileSystem->FileExists(path))
    {
        File file(context_, path, FILE_READ);
        if (file.IsOpen())
        {
            depositMap_ = new Image(context_);
            if (depositMap_->BeginLoad(file))
            {
                depositMapSize_ = depositMap_->GetWidth();
                LogMessage("[Deposits] Loaded " + String(depositMapSize_) + "x" + String(depositMapSize_) + " deposit map");
                return;
            }
        }
    }

    // Generate procedural deposit map from terrain
    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
    if (!terrain || !terrain->GetHeightMap())
    {
        LogMessage("[Deposits] No terrain — skipping deposit generation");
        return;
    }

    int size = terrain->GetHeightMap()->GetWidth();
    depositMap_ = new Image(context_);
    depositMap_->SetSize(size, size, 4);  // RGBA
    depositMap_->Clear(Color::TRANSPARENT_BLACK);
    depositMapSize_ = size;

    // Procedural: scatter metal veins as Perlin-noise blobs
    SetRandomSeed(terrainGen_.params.seed + 3333);

    auto placeVein = [&](int metalType, int count, float minHeight, float maxHeight, int minDepth, int maxDepth)
    {
        for (int n = 0; n < count; ++n)
        {
            int cx = Random(50, size - 50);
            int cz = Random(50, size - 50);
            int radius = Random(8, 20);

            for (int dz = -radius; dz <= radius; ++dz)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    int px = cx + dx, pz = cz + dz;
                    if (px < 0 || px >= size || pz < 0 || pz >= size)
                        continue;

                    float dist2 = (float)(dx * dx + dz * dz);
                    float r2 = (float)(radius * radius);
                    if (dist2 > r2)
                        continue;

                    // Check terrain height at this pixel
                    Vector3 world = terrain->HeightMapToWorld(IntVector2(px, pz));
                    float normalizedH = world.y_ / 60.0f;
                    if (normalizedH < minHeight || normalizedH > maxHeight)
                        continue;

                    float falloff = 1.0f - dist2 / r2;
                    unsigned char qty = (unsigned char)(100.0f * falloff + Random(0, 50));
                    unsigned char purity = (unsigned char)(128.0f * falloff + Random(0, 64));
                    unsigned char depth = (unsigned char)(minDepth + Random(0, maxDepth - minDepth));

                    Color c;
                    c.r_ = qty / 255.0f;
                    c.g_ = metalType / 255.0f;
                    c.b_ = purity / 255.0f;
                    c.a_ = depth / 255.0f;
                    depositMap_->SetPixel(px, pz, c);
                }
            }
        }
    };

    // Copper — common, shallow, lowland
    placeVein(1, 8, 0.1f, 0.5f, 0, 30);
    // Tin — near copper areas, mid elevation
    placeVein(2, 5, 0.15f, 0.55f, 5, 40);
    // Iron — widespread, deeper
    placeVein(3, 10, 0.2f, 0.8f, 20, 80);
    // Gold — rare, near water (low elevation)
    placeVein(4, 3, 0.05f, 0.25f, 0, 10);
    // Coal — lowland flat areas
    placeVein(6, 6, 0.1f, 0.4f, 10, 50);
    // Flint — shallow, widespread
    placeVein(8, 12, 0.1f, 0.7f, 0, 5);

    // Phase 37: Trace elements co-located with iron veins (30% chance per iron cell)
    // Scan the map for iron deposits and scatter trace elements nearby
    struct TraceInfo { int type; int minDepth; int maxDepth; float chance; };
    TraceInfo traces[] = {
        { 7, 80, 120, 0.30f},   // Manganese — 8m+ depth, common
        {13, 100, 150, 0.15f},  // Chromium — 10m+, uncommon (type 13, not 8=flint)
        { 9, 120, 180, 0.08f},  // Tungsten — 12m+, rare
        {10, 100, 150, 0.15f},  // Nickel — 10m+, uncommon
        {11, 60, 100, 0.30f},   // Rich Carbon — 6m+, common
    };
    for (int pz = 0; pz < size; ++pz)
    {
        for (int px = 0; px < size; ++px)
        {
            Color existing = depositMap_->GetPixel(px, pz);
            int existingType = (int)(existing.g_ * 255.0f + 0.5f);
            if (existingType != 3) continue;  // only co-locate with iron

            for (const auto& tr : traces)
            {
                if (Random(1.0f) >= tr.chance) continue;
                // Place trace element in a nearby cell (offset 1-3 pixels)
                int tx = px + Random(-3, 4);
                int tz = pz + Random(-3, 4);
                if (tx < 0 || tx >= size || tz < 0 || tz >= size) continue;
                Color tc = depositMap_->GetPixel(tx, tz);
                if ((int)(tc.g_ * 255.0f + 0.5f) != 0) continue;  // don't overwrite

                unsigned char qty = (unsigned char)(30 + Random(0, 70));
                unsigned char purity = (unsigned char)(25 + Random(0, 180));
                unsigned char depth = (unsigned char)(tr.minDepth + Random(0, tr.maxDepth - tr.minDepth));
                tc.r_ = qty / 255.0f;
                tc.g_ = tr.type / 255.0f;
                tc.b_ = purity / 255.0f;
                tc.a_ = depth / 255.0f;
                depositMap_->SetPixel(tx, tz, tc);
                break;  // one trace element per iron cell
            }
        }
    }

    depositMap_->SavePNG(path);
    LogMessage("[Deposits] Generated and saved " + String(size) + "x" + String(size) + " deposit map (with trace elements)");
}

void AuthServer::HandleMineRequest(Connection* connection, MemoryBuffer& msg)
{
    auto sIt = sessions_.Find(connection);
    if (sIt == sessions_.End() || !sIt->second_.authenticated)
        return;

    float worldX = msg.ReadFloat();
    float worldZ = msg.ReadFloat();

    int playerId = GetPlayerId(sIt->second_.username);
    if (playerId < 0)
        return;

    MineForOwner(playerId, worldX, worldZ, sIt->second_.username, connection);
}

int AuthServer::MineForOwner(int playerId, float worldX, float worldZ,
                              const String& ownerName, Connection* connection)
{
    if (!depositMap_ || !scene_)
        return 0;

    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain)
        return 0;

    // Convert world position to deposit map pixel
    IntVector2 pixel = terrain->WorldToHeightMap(Vector3(worldX, 0, worldZ));
    if (pixel.x_ < 0 || pixel.x_ >= depositMapSize_ || pixel.y_ < 0 || pixel.y_ >= depositMapSize_)
        return 0;

    // Read deposit data
    Color c = depositMap_->GetPixel(pixel.x_, pixel.y_);
    int qty = (int)(c.r_ * 255.0f + 0.5f);
    int type = (int)(c.g_ * 255.0f + 0.5f);
    int purity = (int)(c.b_ * 255.0f + 0.5f);
    int depth = (int)(c.a_ * 255.0f + 0.5f);

    if (type == 0 || qty == 0)
        return 0;

    // Check if terrain has been dug deep enough to expose the deposit
    float terrainHeight = terrain->GetHeight(Vector3(worldX, 0, worldZ));
    float originalHeight = 60.0f;
    float depthInWorld = depth * 0.1f;
    float exposedAtHeight = originalHeight - depthInWorld;

    // Phase 31: Mine Shaft grants access to deeper deposits (+10m depth tolerance)
    float depthTolerance = 2.0f;
#ifdef URHO3D_DATABASE_SQLITE
    if (worldDB_)
    {
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned b = 0; b < buildings.Size(); ++b)
        {
            if (buildings[b].buildingId == 80)
            {
                float dx = buildings[b].posX - worldX;
                float dz = buildings[b].posZ - worldZ;
                if (dx * dx + dz * dz < 225.0f)
                { depthTolerance = 12.0f; break; }
            }
        }
    }
#endif
    if (terrainHeight > exposedAtHeight + depthTolerance)
        return 0;

    // Phase 31: tool tier bonus (stone=1.0, copper=1.3, bronze=1.6, iron=2.0)
    float toolMultiplier = 1.0f;  // bare-hands baseline

#ifdef URHO3D_DATABASE_SQLITE
    if (worldDB_)
    {
        int equippedTool = worldDB_->GetEquippedItem(playerId, "hand");
        if (equippedTool > 0 && gameDB_)
        {
            ItemInfo info;
            if (gameDB_->GetItem(equippedTool, info))
            {
                if (info.tier >= 5) toolMultiplier = 2.0f;       // iron pick
                else if (info.tier >= 4) toolMultiplier = 1.6f;   // bronze pick
                else if (info.tier >= 3) toolMultiplier = 1.3f;   // copper pick
                else if (info.tier >= 1) toolMultiplier = 1.0f;   // stone pick
            }
        }
        // Deduct durability from mining tool
        if (equippedTool > 0)
        {
            int remaining = worldDB_->DeductDurability(playerId, "hand");
            if (remaining == 0)
            {
                LogMessage("[Item] " + ownerName + "'s mining tool broke (item " + String(equippedTool) + ")");
                if (connection)
                    SendInventoryUpdate(connection, playerId);
            }
        }
    }
#endif

    // Phase 31: skill-scaled yield — Knapping skill adds 10% per level
    int miningSkill = 0;
    if (gameDB_)
        miningSkill = gameDB_->GetSkillLevel(playerId, SKILL_KNAPPING);
    float skillMultiplier = 1.0f + miningSkill * 0.1f;

    // Phase 31: mine shaft proximity bonus — +50% if Mine Shaft within 15m
    float shaftBonus = 1.0f;
    if (worldDB_)
    {
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned b = 0; b < buildings.Size(); ++b)
        {
            if (buildings[b].buildingId == 80)  // Mine Shaft
            {
                float dx = buildings[b].posX - worldX;
                float dz = buildings[b].posZ - worldZ;
                if (dx * dx + dz * dz < 225.0f)  // 15m
                { shaftBonus = 1.5f; break; }
            }
        }
    }

    // Calculate yield: base * purity * tool * skill * shaft
    int oreAmount = Max(1, (int)(1.0f * (purity / 255.0f) * toolMultiplier * skillMultiplier * shaftBonus));

    // Decrement quantity
    int removed = Min(qty, oreAmount);
    qty -= removed;
    c.r_ = qty / 255.0f;
    depositMap_->SetPixel(pixel.x_, pixel.y_, c);

    // Map metal type to item ID
    HashMap<int, int> metalToItem;
    metalToItem[1] = 30;  // copper
    metalToItem[2] = 31;  // tin
    metalToItem[3] = 32;  // iron
    metalToItem[4] = 33;  // gold
    metalToItem[6] = 34;  // coal
    metalToItem[8] = 5;   // flint
    // Phase 37: Trace elements (type 8=flint already taken, chromium uses 13)
    metalToItem[7]  = 840;  // manganese
    metalToItem[9]  = 842;  // tungsten
    metalToItem[10] = 843;  // nickel
    metalToItem[11] = 844;  // rich carbon
    metalToItem[13] = 841;  // chromium

    int itemId = metalToItem.Contains(type) ? metalToItem[type] : 0;
    if (itemId > 0)
        AddItemToWorldInventory(playerId, itemId, removed);

    // Send result to connected player (null for NPCs)
    if (connection)
    {
        VectorBuffer buf;
        buf.WriteI32(type);
        buf.WriteI32(removed);
        buf.WriteI32(qty);
        connection->SendMessage(MSG_MINE_RESULT, false, false, buf);
        SendInventoryUpdate(connection, playerId);
    }

    // If exhausted, broadcast removal to all clients + log to settlement history
    if (qty <= 0)
    {
        VectorBuffer exhaustBuf;
        exhaustBuf.WriteFloat(worldX);
        exhaustBuf.WriteFloat(worldZ);
        for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
        {
            if (it->second_.authenticated)
                it->first_->SendMessage(MSG_DEPOSIT_EXHAUSTED, false, false, exhaustBuf);
        }

        // Phase 31: record exhaustion in settlement history for the nearest NPC
        if (playerId >= 10000)
        {
            for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
            {
                if (GetNPCPlayerId(aiIt->second_.spawnId) == playerId && aiIt->second_.campfireId != 0)
                {
                    RecordSettlementFirst(aiIt->second_.campfireId, "mine_exhausted", aiIt->second_.spawnId);
                    break;
                }
            }
        }
    }

    LogMessage(ownerName + " mined " + String(removed) + " ore (type " + String(type) +
        ") at " + String(worldX) + "," + String(worldZ));
    return removed;
}

void AuthServer::CheckExposedDeposits(const Vector3& editPos, float editRadius)
{
    if (!depositMap_ || !scene_)
        return;

    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain)
        return;

    // Scan deposit pixels in the edit area
    IntVector2 center = terrain->WorldToHeightMap(editPos);
    float cellSize = terrain->GetSpacing().x_;
    int pixelRadius = (int)(editRadius / cellSize) + 2;

    for (int dz = -pixelRadius; dz <= pixelRadius; ++dz)
    {
        for (int dx = -pixelRadius; dx <= pixelRadius; ++dx)
        {
            int px = center.x_ + dx;
            int pz = center.y_ + dz;
            if (px < 0 || px >= depositMapSize_ || pz < 0 || pz >= depositMapSize_)
                continue;

            Color c = depositMap_->GetPixel(px, pz);
            int type = (int)(c.g_ * 255.0f + 0.5f);
            int qty = (int)(c.r_ * 255.0f + 0.5f);
            int depth = (int)(c.a_ * 255.0f + 0.5f);

            if (type == 0 || qty == 0)
                continue;

            // Check if terrain is now low enough to expose this deposit
            Vector3 world = terrain->HeightMapToWorld(IntVector2(px, pz));
            float terrainHeight = terrain->GetHeight(world);
            float depthInWorld = depth * 0.1f;
            float exposedAtHeight = 60.0f - depthInWorld;

            if (terrainHeight <= exposedAtHeight + 2.0f)
            {
                // Deposit newly exposed — notify nearby clients
                VectorBuffer buf;
                buf.WriteFloat(world.x_);
                buf.WriteFloat(world.z_);
                buf.WriteI32(type);
                for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
                {
                    if (it->second_.authenticated)
                        it->first_->SendMessage(MSG_DEPOSIT_DISCOVERED, false, false, buf);
                }
            }
        }
    }
}

void AuthServer::SpawnInitialCreatures()
{
    if (creaturesSpawned_ || !scene_)
        return;
    creaturesSpawned_ = true;

    auto* cache = GetSubsystem<ResourceCache>();

    // Full spawn table — server creates real scene nodes that Urho3D replicates.
    struct ServerSpawnEntry {
        const char* name;
        const char* modelPath;
        const char* matList;
        int creatureId;
        int defaultCount;
    };
    static const ServerSpawnEntry entries[] = {
        {"Rabbit",    "Models/Animals/Rabbit.mdl",             "Models/Animals/Rabbit.txt",             1,  5},
        {"Deer",      "Models/Animals/Deer.mdl",               "Models/Animals/Deer.txt",               2,  8},
        {"Fox",       "Models/Animals/Fox.mdl",                "Models/Animals/Fox.txt",                3,  3},
        {"Stag",      "Models/Animals/Stag.mdl",               "Models/Animals/Stag.txt",               4,  3},
        {"Wolf",      "Models/Animals/Wolf.mdl",               "Models/Animals/Wolf.txt",               5,  2},
        {"Bull",      "Models/Animals/Bull.mdl",               "Models/Animals/Bull.txt",               6,  2},
        {"Cow",       "Models/Animals/Cow.mdl",                "Models/Animals/Cow.txt",                7,  3},
        {"Donkey",    "Models/Animals/Donkey.mdl",             "Models/Animals/Donkey.txt",             9,  2},
        {"Horse",     "Models/Animals/Horse.mdl",              "Models/Animals/Horse.txt",             10,  2},
        {"Alpaca",    "Models/Animals/Alpaca.mdl",             "Models/Animals/Alpaca.txt",            11,  3},
        {"Husky",     "Models/Animals/Husky.mdl",              "Models/Animals/Husky.txt",             12,  2},
        {"ShibaInu",  "Models/Animals/ShibaInu.mdl",           "Models/Animals/ShibaInu.txt",          13,  2},
        {"CaveMan",   "Models/Characters/CavemanMan.mdl",      "Models/Characters/CavemanMan.txt",     20,  3},
        {"CaveWoman", "Models/Characters/CavemanWoman.mdl",    "Models/Characters/CavemanWoman.txt",   21,  3},
    };

    auto* terrain = scene_->GetComponent<Terrain>(true);
    float halfSize = 500.0f;
    Vector3 origin(Vector3::ZERO);
    if (terrain)
    {
        Vector3 spacing = terrain->GetSpacing();
        IntVector2 numVerts = terrain->GetNumVertices();
        halfSize = (float)(numVerts.x_ - 1) * spacing.x_ * 0.5f;
        origin = terrain->GetNode()->GetWorldPosition();
    }

    int regionId = 1;
    int totalSpawned = 0;

    for (const auto& entry : entries)
    {
        int count = entry.defaultCount;
        if (populationManager_ && populationManager_->IsReady())
        {
            int pop = populationManager_->GetPopulation(regionId, entry.creatureId);
            if (pop > 0)
                count = pop;
        }

        // Phase 36b: Load habitat from DB for biome-filtered spawning
        String habitat = "any";
        if (gameDB_)
        {
            CreatureInfo ci;
            if (gameDB_->GetCreature(entry.creatureId, ci))
                habitat = ci.habitat;
        }

        for (int i = 0; i < count; ++i)
        {
            Vector3 pos(Vector3::ZERO);
            bool found = false;
            for (int attempt = 0; attempt < 20; ++attempt)
            {
                float x = origin.x_ + Random(-halfSize * 0.8f, halfSize * 0.8f);
                float z = origin.z_ + Random(-halfSize * 0.8f, halfSize * 0.8f);
                float y = GetTerrainHeightAI(x, z);
                if (y <= AI_WATER_LEVEL + 1.0f)
                    continue;

                // Biome-habitat filtering: skip positions that don't match creature's habitat
                if (ecosystem_ && habitat != "any" && habitat != "inland")
                {
                    EcoBiome biome = ecosystem_->SampleBiome(x, z);
                    bool compatible = false;
                    if (habitat == "grassland")
                        compatible = (biome == ECO_GRASSLAND);
                    else if (habitat == "forest")
                        compatible = (biome == ECO_FOREST || biome == ECO_GRASSLAND);
                    else if (habitat == "water")
                        compatible = (biome == ECO_WETLAND);
                    else if (habitat == "mountain")
                        compatible = (biome == ECO_ALPINE || biome == ECO_BARREN);
                    else if (habitat == "desert")
                        compatible = (biome == ECO_BARREN);
                    if (!compatible)
                        continue;
                }

                pos = Vector3(x, y, z);
                found = true;
                break;
            }
            if (!found)
                continue;

            // Create a real REPLICATED scene node — Urho3D sends it to clients
            // automatically when they connect and SetScene is called.
            Node* node = scene_->CreateChild(entry.name);
            node->SetPosition(pos);

            // Model on child node — Urho3D replicates the AnimatedModel component
            // to clients who render it. Server is headless but loads the model
            // metadata (bones, bounding box) for replication.
            Node* modelNode = node->CreateChild(String(entry.name) + "Model");
            modelNode->SetRotation(Quaternion(180.0f, Vector3::UP));
            auto* model = modelNode->CreateComponent<AnimatedModel>();
            auto* mdl = cache->GetResource<Model>(entry.modelPath);
            if (mdl)
            {
                model->SetModel(mdl, true, true);
                model->ApplyMaterialList(entry.matList);
                model->SetCastShadows(false);
            }
            modelNode->CreateComponent<AnimationController>();

            // Tag with species ID so client attaches the right LogicComponent.
            node->SetVar("CreatureId", entry.creatureId);

            // Server-side AI tracking (same as BroadcastSpawnCreature did)
            unsigned spawnId = ++nextSpawnId_;
            node->SetVar("SpawnId", spawnId);

            ServerCreatureAI ai;
            ai.position = pos;
            ai.targetPosition = pos;
            ai.homePosition = pos;
            ai.creatureId = entry.creatureId;
            ai.regionId = regionId;
            ai.isHuman = IsHumanSpecies(entry.creatureId);
            ai.isPredator = IsPredatorSpecies(entry.creatureId);
            ai.isMale = ai.isHuman ? (entry.creatureId == 20) : (Random(1.0f) < 0.5f);
            ai.moveSpeed = ai.isHuman ? 2.0f : 1.5f;
            ai.hunger = 50.0f + Random(30.0f);
            ai.thirst = 60.0f + Random(30.0f);
            ai.stamina = 80.0f + Random(20.0f);
            ai.warmth = GetEffectiveTemperature();
            ai.currentTask = STASK_IDLE;
            ai.spawnId = spawnId;
            ai.stats.strength = Roll4d6DropLowest();
            ai.stats.dexterity = Roll4d6DropLowest();
            ai.stats.constitution = Roll4d6DropLowest();
            ai.stats.intelligence = Roll4d6DropLowest();
            ai.stats.wisdom = Roll4d6DropLowest();
            ai.stats.charisma = Roll4d6DropLowest();
            if (ai.isHuman)
            {
                ai.campfireId = AssignCampfireForNPC(pos, regionId);
                ai.settlementId = ai.campfireId;
                ai.npcName = GenerateNPCName(ai.campfireId);

                // Born dressed — parents killed something, made hide wrap
                int npcPid = GetNPCPlayerId(spawnId);
                if (npcPid > 0 && worldDB_)
                {
                    worldDB_->AddItemToInventory(npcPid, 300, 1, 1, 1.5f, 0, 30.0f, 10);  // Hide Wrap
                    worldDB_->EquipItem(npcPid, 300, "body");
                    worldDB_->AddItemToInventory(npcPid, 303, 1, 1, 1.0f, 0, 30.0f, 10);  // Hide Boots
                    worldDB_->EquipItem(npcPid, 303, "feet");
                }
            }
            creatureAI_[spawnId] = ai;

            // Combat state
            ServerCreatureState cs;
            cs.regionId = regionId;
            cs.position = pos;
            if (!LoadCreatureCombat(entry.creatureId, cs))
            {
                cs.creatureId = entry.creatureId;
                cs.hp = cs.maxHp = 10;
                cs.defense = 10;
            }
            creatureStates_[spawnId] = cs;
            creatureNodes_[spawnId] = node;
            ++totalSpawned;
        }
    }

    LogMessage("[SpawnInit] Spawned " + String(totalSpawned) + " creatures across " +
               String(sizeof(entries) / sizeof(entries[0])) + " species");

    // Note: do NOT call scene_->SaveXML() here — it would overwrite GetFileName()
    // which MSG_LOADSCENE sends to clients.
}

void AuthServer::SpawnInitialTrees()
{
    if (treesSpawned_ || !scene_)
        return;
    treesSpawned_ = true;

    // Load any persisted trees from world DB
#ifdef URHO3D_DATABASE_SQLITE
    if (worldDB_ && worldDB_->IsOpen())
    {
        sqlite3* db = worldDB_->GetHandle();
        sqlite3_stmt* stmt = nullptr;
        int loaded = 0;
        if (sqlite3_prepare_v2(db, "SELECT tree_id, species, pos_x, pos_z, scale FROM trees WHERE hp > 0", -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                int treeId = sqlite3_column_int(stmt, 0);
                if (treeId >= nextTreeId_)
                    nextTreeId_ = treeId + 1;
                ++loaded;
            }
            sqlite3_finalize(stmt);
        }
        if (loaded > 0)
        {
            LogMessage("[Trees] Loaded " + String(loaded) + " persisted trees from world DB");
            return;  // Already have trees — don't regenerate
        }
    }
#endif

    // No persisted trees — generate via Poisson disk sampling.
    // Server terrain is 1025x1025 heightmap at default spacing (0.5 units per pixel),
    // giving ~512 world units per side. We sample tree positions on this terrain.
    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain)
    {
        LogMessage("[Trees] No terrain found — skipping tree generation");
        return;
    }

    float waterLevel = terrainGen_.params.waterLevel * 255.0f * terrain->GetSpacing().y_;
    float terrainSizeX = (terrain->GetNumVertices().x_ - 1) * terrain->GetSpacing().x_;
    float terrainSizeZ = (terrain->GetNumVertices().y_ - 1) * terrain->GetSpacing().z_;
    float halfX = terrainSizeX * 0.5f;
    float halfZ = terrainSizeZ * 0.5f;

    // Poisson disk sampling — rejection-based with grid acceleration
    const float minSpacing = 8.0f;
    const int maxTrees = 500;
    const int maxAttempts = maxTrees * 30;
    float cellSize = minSpacing / 1.414f;
    int gridW = (int)(terrainSizeX / cellSize) + 1;
    int gridH = (int)(terrainSizeZ / cellSize) + 1;

    Vector<int> grid(gridW * gridH, -1);
    struct TreeCandidate { float x, z; int species; float scale; };
    Vector<TreeCandidate> accepted;

    SetRandomSeed(terrainGen_.params.seed + 7777);

    for (int attempt = 0; attempt < maxAttempts && (int)accepted.Size() < maxTrees; ++attempt)
    {
        float px = Random(-halfX, halfX);
        float pz = Random(-halfZ, halfZ);

        // Height check — no trees underwater
        float height = terrain->GetHeight(Vector3(px, 0, pz));
        if (height < waterLevel + 1.0f)
            continue;

        // Grid-based Poisson rejection
        int gx = (int)((px + halfX) / cellSize);
        int gz = (int)((pz + halfZ) / cellSize);
        gx = Clamp(gx, 0, gridW - 1);
        gz = Clamp(gz, 0, gridH - 1);

        bool tooClose = false;
        for (int dz = -2; dz <= 2 && !tooClose; ++dz)
        {
            for (int dx = -2; dx <= 2 && !tooClose; ++dx)
            {
                int nx = gx + dx, nz = gz + dz;
                if (nx < 0 || nx >= gridW || nz < 0 || nz >= gridH)
                    continue;
                int idx = grid[nz * gridW + nx];
                if (idx >= 0)
                {
                    float ddx = accepted[idx].x - px;
                    float ddz = accepted[idx].z - pz;
                    if (ddx * ddx + ddz * ddz < minSpacing * minSpacing)
                        tooClose = true;
                }
            }
        }
        if (tooClose)
            continue;

        // Biome-aware species selection based on elevation
        float normalizedHeight = (height - waterLevel) / (60.0f - waterLevel);
        normalizedHeight = Clamp(normalizedHeight, 0.0f, 1.0f);

        int species;
        bool nearWater = (normalizedHeight < 0.08f);  // very close to water line
        if (nearWater && Random(1.0f) < 0.3f)
        {
            species = 4;  // willow — grows near water edges
        }
        else if (normalizedHeight < 0.35f)
        {
            // Lowland: mostly oak, 12% chance of acacia (rare resin tree — discovery takes effort)
            species = (Random(1.0f) < 0.12f) ? 3 : 0;
        }
        else if (normalizedHeight < 0.65f)
        {
            // Mid elevation: mostly eucalyptus, 15% chance she-oak (rocky terrain, bark source)
            species = (Random(1.0f) < 0.15f) ? 5 : 2;
        }
        else
            species = 1;  // pine — highland

        // Scale variation
        float scale = 0.8f + Random(0.0f, 0.6f);

        TreeCandidate tc;
        tc.x = px;
        tc.z = pz;
        tc.species = species;
        tc.scale = scale;
        accepted.Push(tc);
        grid[gz * gridW + gx] = (int)accepted.Size() - 1;
    }

    // Persist to DB and prepare for broadcast
#ifdef URHO3D_DATABASE_SQLITE
    if (worldDB_ && worldDB_->IsOpen())
    {
        for (unsigned i = 0; i < accepted.Size(); ++i)
        {
            int treeId = nextTreeId_++;
            int yieldsResin = (accepted[i].species == 3) ? 1 : 0;  // acacia yields resin
            worldDB_->Execute("INSERT INTO trees (tree_id, species, pos_x, pos_z, scale, region_id, hp, yields_resin) VALUES ("
                + String(treeId) + ", " + String(accepted[i].species) + ", "
                + String(accepted[i].x) + ", " + String(accepted[i].z) + ", "
                + String(accepted[i].scale) + ", 1, 100, " + String(yieldsResin) + ")");
        }
    }
#endif

    LogMessage("[Trees] Generated " + String(accepted.Size()) + " trees via Poisson disk sampling");
}

void AuthServer::BroadcastSpawnTree(int treeId, int species, float posX, float posZ, float scale, int growthStage)
{
    if (growthStage < 2)
        return;  // stumps and saplings are invisible to clients

    VectorBuffer buf;
    buf.WriteByte(static_cast<std::byte>(species));
    buf.WriteFloat(posX);
    buf.WriteFloat(posZ);
    buf.WriteFloat(scale);
    buf.WriteU32(static_cast<unsigned>(treeId));
    buf.WriteU8(static_cast<unsigned char>(growthStage));

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_SPAWN_TREE, true, true, buf);
    }
}

void AuthServer::SendTreesTo(Connection* connection)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int count = 0;

    if (sqlite3_prepare_v2(db, "SELECT tree_id, species, pos_x, pos_z, scale, growth_stage FROM trees WHERE hp > 0 AND growth_stage >= 2",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int treeId = sqlite3_column_int(stmt, 0);
            int species = sqlite3_column_int(stmt, 1);
            float px = (float)sqlite3_column_double(stmt, 2);
            float pz = (float)sqlite3_column_double(stmt, 3);
            float scale = (float)sqlite3_column_double(stmt, 4);
            int growthStage = sqlite3_column_int(stmt, 5);

            VectorBuffer buf;
            buf.WriteByte(static_cast<std::byte>(species));
            buf.WriteFloat(px);
            buf.WriteFloat(pz);
            buf.WriteFloat(scale);
            buf.WriteU32(static_cast<unsigned>(treeId));
            buf.WriteU8(static_cast<unsigned char>(growthStage));
            connection->SendMessage(MSG_SPAWN_TREE, true, true, buf);
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    LogMessage("[Trees] Sent " + String(count) + " trees to " + connection->ToString());
#endif
}

void AuthServer::SendFishSpawnsTo(Connection* connection)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    // Collect all spawn points across all water bodies for terrain 0
    auto bodies = worldDB_->GetWaterBodies(0);
    Vector<WorldDB::FishSpawnPoint> allSpawns;
    for (unsigned b = 0; b < bodies.Size(); ++b)
    {
        auto spawns = worldDB_->GetFishSpawnPoints(bodies[b].bodyId);
        for (unsigned s = 0; s < spawns.Size(); ++s)
            allSpawns.Push(spawns[s]);
    }

    if (allSpawns.Empty())
        return;

    // Pack into a single message: u16 count, then count × (f32 x, f32 z, f32 depth)
    VectorBuffer buf;
    unsigned short count = (unsigned short)Min((int)allSpawns.Size(), 65535);
    buf.WriteU16(count);
    for (unsigned i = 0; i < count; ++i)
    {
        buf.WriteFloat(allSpawns[i].posX);
        buf.WriteFloat(allSpawns[i].posZ);
        buf.WriteFloat(allSpawns[i].depth);
    }
    connection->SendMessage(MSG_FISH_SPAWNS, true, true, buf);

    LogMessage("[FishSpawns] Sent " + String(count) + " spawn points to " + connection->ToString());
#endif
}

void AuthServer::SendSettlementClaimsTo(Connection* connection)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    auto claims = worldDB_->GetWaterBodies(0);  // reuse pattern — but need settlement claims
    // Query settlement_patches directly
    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    VectorBuffer buf;

    // Count first
    Vector<unsigned char> sxList, szList;
    Vector<unsigned short> sidList;

    if (sqlite3_prepare_v2(db,
        "SELECT spatch_x, spatch_z, settlement_id FROM settlement_patches WHERE terrain_id = 0",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            sxList.Push((unsigned char)sqlite3_column_int(stmt, 0));
            szList.Push((unsigned char)sqlite3_column_int(stmt, 1));
            sidList.Push((unsigned short)sqlite3_column_int(stmt, 2));
        }
        sqlite3_finalize(stmt);
    }

    if (sxList.Empty())
        return;

    unsigned short count = (unsigned short)Min((int)sxList.Size(), 65535);
    buf.WriteU16(count);
    for (unsigned i = 0; i < count; ++i)
    {
        buf.WriteU8(sxList[i]);
        buf.WriteU8(szList[i]);
        buf.WriteU16(sidList[i]);
    }
    connection->SendMessage(MSG_SETTLEMENT_CLAIMS, true, true, buf);

    LogMessage("[Settlement] Sent " + String(count) + " patch claims to " + connection->ToString());
#endif
}

void AuthServer::HandleChopTree(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    auto sIt = sessions_.Find(connection);
    if (sIt == sessions_.End() || !sIt->second_.authenticated)
        return;

    unsigned treeId = msg.ReadU32();
    int playerId = GetPlayerId(sIt->second_.username);
    if (playerId < 0)
        return;

    // Check equipped tool is an axe-type (item category 'tool')
    int equippedTool = 0;
    int toolTier = 0;
    if (worldDB_)
    {
        equippedTool = worldDB_->GetEquippedItem(playerId, "hand");
        if (equippedTool <= 0)
        {
            LogMessage("[Trees] " + sIt->second_.username + " tried to chop with no tool equipped");
            return;
        }
        // Verify it's an axe (item IDs 100, 810, 812, 820)
        if (gameDB_)
        {
            ItemInfo info;
            if (gameDB_->GetItem(equippedTool, info))
            {
                if (info.category != "tool")
                {
                    LogMessage("[Trees] " + sIt->second_.username + " tried to chop with non-tool item " + String(equippedTool));
                    return;
                }
                toolTier = info.tier;
            }
        }
    }

    // Look up tree in DB
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int treeHp = 0;
    int species = 0;
    int growthStage = 3;

    if (sqlite3_prepare_v2(db, "SELECT hp, species, growth_stage FROM trees WHERE tree_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)treeId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            treeHp = sqlite3_column_int(stmt, 0);
            species = sqlite3_column_int(stmt, 1);
            growthStage = sqlite3_column_int(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }

    if (treeHp <= 0)
        return;  // already dead

    if (growthStage < 2)
        return;  // can't chop saplings

    // Damage based on tool tier: tier 0-1 = 20, tier 2 = 33, tier 3 = 50, tier 4+ = 100
    int chopDamage = 20;
    if (toolTier >= 4) chopDamage = 100;
    else if (toolTier >= 3) chopDamage = 50;
    else if (toolTier >= 2) chopDamage = 33;

    treeHp = Max(0, treeHp - chopDamage);

    // Deduct tool durability
    if (equippedTool > 0 && worldDB_)
    {
        int remaining = worldDB_->DeductDurability(playerId, "hand");
        if (remaining == 0)
        {
            LogMessage("[Trees] " + sIt->second_.username + "'s axe broke");
            SendInventoryUpdate(connection, playerId);
        }
    }

    // Update tree HP in DB
    if (sqlite3_prepare_v2(db, "UPDATE trees SET hp = ? WHERE tree_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, treeHp);
        sqlite3_bind_int(stmt, 2, (int)treeId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (treeHp <= 0)
    {
        // Tree felled — convert to stump, yield wood
        if (sqlite3_prepare_v2(db, "UPDATE trees SET growth_stage = 0, planted_day = ? WHERE tree_id = ?",
                               -1, &stmt, nullptr) == SQLITE_OK)
        {
            int gameDay = (int)(GetSubsystem<Time>()->GetElapsedTime() / 300.0f);  // ~5min game days
            sqlite3_bind_int(stmt, 1, gameDay);
            sqlite3_bind_int(stmt, 2, (int)treeId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        // Yield: Pine → softwood(15), Oak/Eucalyptus → hardwood(16)
        int woodItemId = (species == 1 || species == 3 || species == 4) ? 15 : 16;  // pine/acacia/willow→soft
        int yield = 2 + (toolTier >= 2 ? 1 : 0) + (toolTier >= 4 ? 1 : 0);  // 2-4 based on tool
        AddItemToWorldInventory(playerId, woodItemId, yield);
        SendInventoryUpdate(connection, playerId);

        // Award Woodwork XP
        if (gameDB_)
            gameDB_->AwardXP(playerId, "chop_tree");

        // Broadcast removal to all clients
        VectorBuffer buf;
        buf.WriteU32(treeId);
        for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
        {
            if (it->second_.authenticated)
                it->first_->SendMessage(MSG_REMOVE_TREE, true, true, buf);
        }

        LogMessage("[Trees] " + sIt->second_.username + " felled tree " + String(treeId) +
                   " (species=" + String(species) + ") yield=" + String(yield) + " wood");
    }
    else
    {
        LogMessage("[Trees] " + sIt->second_.username + " chopped tree " + String(treeId) +
                   " hp=" + String(treeHp) + "/100");
    }
#endif
}

// ---------------------------------------------------------------------------
// Fire Carrying Phase 1: Tree Resin — HandleTapTree
// ---------------------------------------------------------------------------

void AuthServer::HandleTapTree(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    auto sIt = sessions_.Find(connection);
    if (sIt == sessions_.End() || !sIt->second_.authenticated)
        return;

    unsigned treeId = msg.ReadU32();
    int playerId = GetPlayerId(sIt->second_.username);
    if (playerId < 0)
        return;

    // Require knife equipped
    if (worldDB_)
    {
        int equipped = worldDB_->GetEquippedItem(playerId, "hand");
        if (equipped <= 0)
        {
            LogMessage("[Botanical] " + sIt->second_.username + " tried to tap with no tool");
            return;
        }
        if (gameDB_)
        {
            ItemInfo info;
            if (gameDB_->GetItem(equipped, info) && info.category != "weapon")
            {
                LogMessage("[Botanical] " + sIt->second_.username + " tried to tap with non-knife");
                return;
            }
        }
    }

    // Try all known botanical properties on this tree (resin first, others as added)
    // The generic handler checks skill gates and cooldowns per property
    bool harvested = HarvestTreeProperty(playerId, treeId, "resin")
                  || HarvestTreeProperty(playerId, treeId, "growth")
                  || HarvestTreeProperty(playerId, treeId, "bark")
                  || HarvestTreeProperty(playerId, treeId, "medicine");
    // Future: || HarvestTreeProperty(playerId, treeId, "dye")

    if (harvested)
    {
        LogMessage("[Botanical] " + sIt->second_.username + " harvested tree " + String(treeId));
        SendInventoryUpdate(connection, playerId);
    }
    else
    {
        LogMessage("[Botanical] " + sIt->second_.username + " tapped tree " + String(treeId) + " — nothing useful");
    }
#endif
}

void AuthServer::TickTreeGrowth()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    int gameDay = (int)(GetSubsystem<Time>()->GetElapsedTime() / 300.0f);
    int season = gameDay % 4;  // crude season approximation

    // No growth in winter
    if (season == 3)
        return;

    float growthMult = 1.0f;
    if (season == 0) growthMult = 1.5f;      // spring
    else if (season == 2) growthMult = 0.5f;  // autumn

    // Willow Extract: +30% tree growth if any settlement has extract
    for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
    {
        if (aiIt->second_.isHuman)
        {
            int pid = GetNPCPlayerId(aiIt->first_);
            if (pid > 0 && worldDB_->GetItemCount(pid, 875) > 0)
            { growthMult *= 1.3f; break; }
        }
    }

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;

    // --- Stump regrowth: dead trees (hp=0, stage=0) sprout a sapling after threshold ---
    {
        const int stumpRegrowthDays = (int)(5 / growthMult);
        sqlite3_stmt* stumpStmt = nullptr;
        if (sqlite3_prepare_v2(db,
            "SELECT tree_id, planted_day FROM trees WHERE hp = 0 AND growth_stage = 0",
            -1, &stumpStmt, nullptr) == SQLITE_OK)
        {
            Vector<unsigned> reviveIds;
            while (sqlite3_step(stumpStmt) == SQLITE_ROW)
            {
                int id = sqlite3_column_int(stumpStmt, 0);
                int plantedDay = sqlite3_column_int(stumpStmt, 1);
                if (gameDay - plantedDay >= stumpRegrowthDays)
                    reviveIds.Push((unsigned)id);
            }
            sqlite3_finalize(stumpStmt);

            for (unsigned i = 0; i < reviveIds.Size(); ++i)
            {
                sqlite3_stmt* upd = nullptr;
                if (sqlite3_prepare_v2(db,
                    "UPDATE trees SET hp = 100, growth_stage = 1, planted_day = ? WHERE tree_id = ?",
                    -1, &upd, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(upd, 1, gameDay);
                    sqlite3_bind_int(upd, 2, (int)reviveIds[i]);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);
                }
                LogMessage("[Trees] Stump " + String(reviveIds[i]) + " sprouted (sapling)");
            }
        }
    }

    // Find saplings/young trees ready to advance
    // Advance if enough days have passed since planted_day
    // Sapling→young: 10 days, young→mature: 20 days (stumps handled above)
    const int stageThresholds[] = {5, 10, 20};

    if (sqlite3_prepare_v2(db,
        "SELECT tree_id, growth_stage, planted_day, species, pos_x, pos_z, scale FROM trees WHERE growth_stage BETWEEN 1 AND 2 AND hp > 0",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        Vector<unsigned> advanceIds;
        Vector<int> advanceStages;
        Vector<int> advanceSpecies;
        Vector<float> advancePosX, advancePosZ, advanceScale;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int id = sqlite3_column_int(stmt, 0);
            int stage = sqlite3_column_int(stmt, 1);
            int plantedDay = sqlite3_column_int(stmt, 2);
            int sp = sqlite3_column_int(stmt, 3);
            float px = (float)sqlite3_column_double(stmt, 4);
            float pz = (float)sqlite3_column_double(stmt, 5);
            float sc = (float)sqlite3_column_double(stmt, 6);

            int daysNeeded = (int)(stageThresholds[stage] / growthMult);
            if (gameDay - plantedDay >= daysNeeded)
            {
                advanceIds.Push((unsigned)id);
                advanceStages.Push(stage + 1);
                advanceSpecies.Push(sp);
                advancePosX.Push(px);
                advancePosZ.Push(pz);
                advanceScale.Push(sc);
            }
        }
        sqlite3_finalize(stmt);

        // Apply advances
        for (unsigned i = 0; i < advanceIds.Size(); ++i)
        {
            int newStage = advanceStages[i];
            if (sqlite3_prepare_v2(db,
                "UPDATE trees SET growth_stage = ?, planted_day = ?, hp = 100 WHERE tree_id = ?",
                -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, newStage);
                sqlite3_bind_int(stmt, 2, gameDay);
                sqlite3_bind_int(stmt, 3, (int)advanceIds[i]);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            // When reaching mature(3), broadcast spawn to clients
            if (newStage == 3)
            {
                BroadcastSpawnTree((int)advanceIds[i], advanceSpecies[i],
                                   advancePosX[i], advancePosZ[i], advanceScale[i]);
            }

            LogMessage("[Trees] Tree " + String(advanceIds[i]) + " grew to stage " + String(newStage));
        }
    }
#endif
}

void AuthServer::HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData)
{
    // Apply each connected client's controls to their possessed NPC's ServerCreatureAI.
    // Controls arrive via Urho3D's built-in connection->GetControls() each physics tick.
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        ClientSession& s = it->second_;
        if (!s.authenticated || s.possessedNodeId == 0)
            continue;

        auto aiIt = creatureAI_.Find(s.possessedNodeId);
        if (aiIt == creatureAI_.End())
            continue;

        ServerCreatureAI& ai = aiIt->second_;
        const Controls& controls = it->first_->GetControls();

        // Derive movement direction from controls + yaw
        Vector3 moveDir(Vector3::ZERO);
        Quaternion rot(0.0f, controls.yaw_, 0.0f);
        if (controls.IsDown(CTRL_FORWARD)) moveDir += rot * Vector3::FORWARD;
        if (controls.IsDown(CTRL_BACK))    moveDir += rot * Vector3::BACK;
        if (controls.IsDown(CTRL_LEFT))    moveDir += rot * Vector3::LEFT;
        if (controls.IsDown(CTRL_RIGHT))   moveDir += rot * Vector3::RIGHT;

        float speed = controls.IsDown(CTRL_SPRINT) ? 6.0f : 3.0f;

        if (moveDir.LengthSquared() > 0.0f)
        {
            moveDir.Normalize();
            ai.targetPosition = ai.position + moveDir * 5.0f; // ahead target
            ai.moveSpeed = speed;
            ai.state = 1; // CREATURE_WANDER (walk/run animation)
        }
        else
        {
            ai.targetPosition = ai.position;
            ai.moveSpeed = 0.0f;
            if (ai.state == 1) // was walking
                ai.state = 0; // CREATURE_IDLE
        }

        // Face the camera direction — the possessed NPC is the player's avatar
        auto nodeIt = creatureNodes_.Find(s.possessedNodeId);
        if (nodeIt != creatureNodes_.End() && nodeIt->second_)
        {
            Node* npcNode = nodeIt->second_;
            npcNode->SetWorldDirection(rot * Vector3::FORWARD);
        }

        // Suppress AI task evaluation while possessed
        ai.taskDecisionTimer = 0.0f;
        ai.taskTimer = 1.0f; // keep task alive so OnCreatureTaskComplete doesn't fire
    }
}

// ============================================================
// Network events
// ============================================================

String AuthServer::GetConnectionIP(Connection* connection) const
{
    String addr = connection->GetAddress();
    // Strip port if present (address may be "ip:port")
    unsigned colonPos = addr.FindLast(':');
    if (colonPos != String::NPOS)
        addr = addr.Substring(0, colonPos);
    return addr;
}

void AuthServer::SweepUnauthConnections()
{
    Vector<Connection*> toKick;
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (!it->second_.authenticated && (uptime_ - it->second_.connectTime) > UNAUTH_TIMEOUT)
            toKick.Push(it->first_);
    }
    for (unsigned i = 0; i < toKick.Size(); ++i)
    {
        LogMessage("Kicking unauthenticated connection after " + String((int)UNAUTH_TIMEOUT) + "s: " + toKick[i]->ToString());
        toKick[i]->Disconnect();
    }
}

void AuthServer::HandleClientConnected(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientConnected;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    // Per-IP connection cap
    String ip = GetConnectionIP(connection);
    IPRecord& rec = ipRecords_[ip];
    if (rec.activeConnections >= MAX_CONNECTIONS_PER_IP)
    {
        LogMessage("Rejecting connection from " + ip + ": max " + String(MAX_CONNECTIONS_PER_IP) + " connections per IP");
        connection->Disconnect();
        return;
    }
    rec.activeConnections++;

    ClientSession session;
    session.connectTime = uptime_;
    sessions_[connection] = session;
    LogMessage("Client connected: " + connection->ToString() + " (IP " + ip + ", " + String(rec.activeConnections) + " active)");
    RefreshClientList();
}

void AuthServer::HandleClientDisconnected(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientDisconnected;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    // Decrement per-IP connection counter
    String ip = GetConnectionIP(connection);
    auto ipIt = ipRecords_.Find(ip);
    if (ipIt != ipRecords_.End())
    {
        ipIt->second_.activeConnections = Max(0, ipIt->second_.activeConnections - 1);
        if (ipIt->second_.activeConnections == 0 && ipIt->second_.failedAttempts == 0)
            ipRecords_.Erase(ipIt);  // clean up IPs with no state
    }

    // Remove avatar from scene
    auto objIt = serverObjects_.Find(connection);
    if (objIt != serverObjects_.End())
    {
        Node* avatar = objIt->second_;
        if (avatar)
        {
            LogMessage("Removing avatar node " + String(avatar->GetID()));
            avatar->Remove();
        }
        serverObjects_.Erase(objIt);
    }

    auto it = sessions_.Find(connection);
    if (it != sessions_.End())
    {
        String who = it->second_.username.Empty() ? "<unauthenticated>" : it->second_.username;
        bool wasAuthed = it->second_.authenticated;
        URHO3D_LOGINFOF("[NetDebug] CLIENT DISCONNECTED: %s (%s) wasAuthenticated=%d uptime=%.1fs",
            connection->ToString().CString(), who.CString(), (int)wasAuthed, uptime_);
        LogMessage("Client disconnected: " + connection->ToString() + " (" + who + ")");

        // Clean up torch burn timer for this player (prevents leak)
        if (wasAuthed && !it->second_.username.Empty())
        {
            int playerId = GetPlayerId(it->second_.username);
            if (playerId > 0)
                torchTimers_.Erase(playerId);
        }

        // Cancel any active friction ignition owned by this connection
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->second_.ignitionActive && !cfIt->second_.ignitionByNPC &&
                cfIt->second_.ignitionPlayerConn == connection)
            {
                RuinIgnition(cfIt->first_, cfIt->second_, "player disconnected");
            }
        }

        // Release NPC possession — stale entry prevents future possess of that NPC
        if (it->second_.possessedNodeId != 0)
            npcPossessors_.Erase(it->second_.possessedNodeId);

        // Cancel active trade session on disconnect
        if (wasAuthed && !it->second_.username.Empty())
        {
            int pid = GetPlayerId(it->second_.username);
            if (pid > 0 && FindTradeSession(pid))
                CleanupTradeSession(pid);
        }

        sessions_.Erase(it);
    }
    else
        URHO3D_LOGWARNING("[NetDebug] CLIENT DISCONNECTED: unknown connection " + connection->ToString());
    RefreshClientList();
}

void AuthServer::HandleClientIdentity(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientIdentity;
    using namespace ClientIdentity;  // P_CONNECTION, P_ALLOW
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    // Accept identity. P_USERNAME is only set in the PAKE key exchange path;
    // non-PAKE connections (e.g. localhost offline) send identity without a
    // username and auth separately via MSG_AUTH_LOGIN. Don't reject on empty
    // P_USERNAME — that blocks all non-encrypted connections.
    if (!connection)
    {
        eventData[P_ALLOW] = false;
        return;
    }

    eventData[P_ALLOW] = true;
}

String AuthServer::SqlEscape(const String& s)
{
    // Strip null bytes, escape single quotes for SQLite
    String result;
    result.Reserve(s.Length());
    for (unsigned i = 0; i < s.Length(); ++i)
    {
        char c = s[i];
        if (c == '\0')
            continue;  // strip nulls
        if (c == '\'')
            result += "''";
        else
            result += c;
    }
    return result;
}

void AuthServer::HandleNetworkMessage(StringHash eventType, VariantMap& eventData)
{
    using namespace NetworkMessage;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    int msgID = eventData[P_MESSAGEID].GetI32();
    const auto& data = eventData[P_DATA].GetBuffer();

    MemoryBuffer msg(data);

    switch (msgID)
    {
    case MSG_AUTH_LOGIN:
    {
        String username = msg.ReadString();
        String passwordHash = msg.ReadString();

        // Boundary validation — reject oversized or empty credentials
        if (username.Trimmed().Empty() || passwordHash.Trimmed().Empty() ||
            username.Length() > 64 || passwordHash.Length() > 128)
        {
            connection->Disconnect();
            break;
        }

        // Auto-provision reserved offline account from localhost
        if (username == String(OFFLINE_RESERVED_USERNAME))
        {
            const String addr = GetConnectionIP(connection);
            const bool isLocal = (addr == "127.0.0.1" || addr == "::1" || addr == "localhost");
            if (isLocal)
            {
                DbResult exists = db_->Execute(
                    "SELECT id FROM users WHERE username = '" +
                    SqlEscape(String(OFFLINE_RESERVED_USERNAME)) + "'");
                if (exists.GetRows().Empty())
                {
                    if (RegisterUser(String(OFFLINE_RESERVED_USERNAME),
                                     String(OFFLINE_RESERVED_PASSWORD)))
                    {
                        db_->Execute("UPDATE users SET admin_level = 25772 WHERE username = '" +
                            SqlEscape(String(OFFLINE_RESERVED_USERNAME)) + "'");
                        LogMessage("Auto-provisioned reserved Offline account (admin 25772) for localhost");
                    }
                }
            }
            else
            {
                LogMessage("Rejected reserved Offline login from non-local: " + addr);
                connection->Disconnect();
                break;
            }
        }

        // Per-IP rate limiting — reset window if expired, then check threshold
        {
            String ip = GetConnectionIP(connection);
            IPRecord& rec = ipRecords_[ip];
            if (uptime_ - rec.windowStart > LOGIN_RATE_WINDOW)
            {
                rec.failedAttempts = 0;
                rec.windowStart = uptime_;
            }
            if (rec.failedAttempts >= MAX_FAILED_LOGINS_PER_WINDOW)
            {
                LogMessage("Rate limit: " + ip + " exceeded " + String(MAX_FAILED_LOGINS_PER_WINDOW) + " failed logins in " + String((int)LOGIN_RATE_WINDOW) + "s — disconnecting");
                connection->Disconnect();
                break;
            }
        }

        int adminLevel = 0;
        bool ok = AuthenticateUser(username, passwordHash, adminLevel);

        // Reject if this username is already logged in on another connection (admins exempt)
        if (ok && adminLevel == 0)
        {
            Connection* existing = FindSessionByUsername(username);
            if (existing && existing != connection)
            {
                ok = false;
                LogMessage("Login rejected for '" + SqlEscape(username) + "': already logged in");
            }
        }

        VectorBuffer reply;
        reply.WriteI32(MSG_AUTH_LOGIN);
        reply.WriteBool(ok);
        reply.WriteString(ok ? "OK" : "Login failed");  // generic — no hints
        reply.WriteI32(ok ? adminLevel : 0);
        reply.WriteString(ok ? sceneName_ : String::EMPTY);
        // Append ALL owned patch coordinates
        if (ok && db_)
        {
            DbResult patchResult = db_->Execute(
                "SELECT patch_x, patch_z FROM patches WHERE owner_name = '" + SqlEscape(username) + "'"
            );
            const auto& rows = patchResult.GetRows();
            reply.WriteI32((int)rows.Size());
            for (unsigned i = 0; i < rows.Size(); ++i)
            {
                reply.WriteI32(rows[i][0].GetI32());
                reply.WriteI32(rows[i][1].GetI32());
            }
        }
        else
        {
            reply.WriteI32(0);
        }
        connection->SendMessage(MSG_AUTH_RESULT, true, true, reply);

        if (ok)
        {
            sessions_[connection].username = username;
            sessions_[connection].authenticated = true;
            sessions_[connection].adminLevel = adminLevel;
            LogMessage("User '" + SqlEscape(username) + "' authenticated (level " + String(adminLevel) + ")");
            RefreshClientList();

            // Replicate scene to client — server owns all creature/building spawning.
            if (scene_)
            {
                URHO3D_LOGINFOF("[NetDebug] SetScene for %s — scene has %u children",
                    username.CString(), scene_->GetNumChildren());
                connection->SetScene(scene_);
            }

            // Tracker node for position-based range checks
            Node* tracker = scene_ ? scene_->CreateChild("ClientTracker") : nullptr;
            if (tracker)
            {
                serverObjects_[connection] = tracker;
                VariantMap remoteEventData;
                remoteEventData[P_ID] = tracker->GetID();
                connection->SendRemoteEvent(E_CLIENTOBJECTID, true, remoteEventData);
            }

            // Send post-auth data (same as PAKE path in HandleClientAuthenticated)
#ifdef URHO3D_DATABASE_SQLITE
            if (gameDB_)
            {
                int playerId = GetPlayerId(username);
                SendInventoryUpdate(connection, playerId);
                SendExistingBuildings(connection);
                SendExistingCrops(connection);
                SendTreesTo(connection);
                SendFishSpawnsTo(connection);
                SendSettlementClaimsTo(connection);
            }
#endif
            if (weatherReady_)
                SendWeatherToClient(connection);
        }
        else
        {
            LogMessage("Login failed for '" + SqlEscape(username) + "'");
            // Increment per-IP and per-session failure counters
            String ip = GetConnectionIP(connection);
            ipRecords_[ip].failedAttempts++;
            sessions_[connection].failedLogins++;
        }
        break;
    }

    case MSG_AUTH_REGISTER:
    {
        String username = msg.ReadString();
        String passwordHash = msg.ReadString();

        // Boundary validation — reject before touching DB
        if (username.Trimmed().Empty() || passwordHash.Trimmed().Empty() ||
            username.Length() > 64 || passwordHash.Length() > 128)
        {
            connection->Disconnect();
            break;
        }

        bool ok = RegisterUser(username, passwordHash);

        VectorBuffer reply;
        reply.WriteI32(MSG_AUTH_REGISTER);  // echo original msg type
        reply.WriteBool(ok);
        reply.WriteString(ok ? "Registration successful" : "Username already taken");
        connection->SendMessage(MSG_AUTH_RESULT, true, true, reply);

        LogMessage(ok ? "Registered user '" + username + "'" : "Registration failed: '" + username + "' taken");
        break;
    }

    case MSG_REGISTER_GUID:
    {
        String guid = msg.ReadString();
        auto it = sessions_.Find(connection);
        if (it != sessions_.End() && it->second_.authenticated)
        {
            it->second_.guid = guid;
            LogMessage("GUID registered for '" + it->second_.username + "': " + guid);
            RefreshClientList();
        }
        else
            LogMessage("[WARN] GUID registration from unauthenticated client — ignoring");
        break;
    }

    case MSG_PATCH_CLAIM:
    {
        auto it = sessions_.Find(connection);
        if (it == sessions_.End() || !it->second_.authenticated)
        {
            VectorBuffer reply;
            reply.WriteBool(false);
            reply.WriteString("Not authenticated");
            connection->SendMessage(MSG_PATCH_RESULT, true, true, reply);
            break;
        }

        int px = msg.ReadI32();
        int pz = msg.ReadI32();
        bool ok = ClaimPatch(px, pz, it->second_.username);

        VectorBuffer reply;
        reply.WriteBool(ok);
        reply.WriteString(ok ? "Patch claimed" : "Patch already owned");
        connection->SendMessage(MSG_PATCH_RESULT, true, true, reply);

        LogMessage(ok
            ? it->second_.username + " claimed patch (" + String(px) + "," + String(pz) + ")"
            : "Patch claim denied (" + String(px) + "," + String(pz) + ")");
        break;
    }

    case MSG_PATCH_QUERY:
    {
        // Auth gate — unauthenticated clients must not query patch ownership
        auto pqIt = sessions_.Find(connection);
        if (pqIt == sessions_.End() || !pqIt->second_.authenticated)
        {
            LogMessage("[WARN] MSG_PATCH_QUERY from unauthenticated client — ignoring");
            break;
        }

        int px = msg.ReadI32();
        int pz = msg.ReadI32();
        PatchInfo info = QueryPatchOwner(px, pz);

        // Send normal patch result regardless
        VectorBuffer reply;
        reply.WriteI32(px);
        reply.WriteI32(pz);
        reply.WriteString(info.ownerName);
        reply.WriteString(info.ownerAddress);
        reply.WriteU16(info.ownerPort);
        connection->SendMessage(MSG_PATCH_RESULT, true, true, reply);

        // If the patch is owned by another online, authenticated client, introduce peers
        if (!info.ownerName.Empty())
        {
            auto reqIt = sessions_.Find(connection);
            if (reqIt != sessions_.End() && reqIt->second_.authenticated)
            {
                // Don't introduce to yourself
                if (info.ownerName != reqIt->second_.username)
                {
                    Connection* owner = FindSessionByUsername(info.ownerName);
                    if (owner)
                        IntroducePeers(connection, owner, px, pz);
                }
            }
        }
        break;
    }

    case MSG_RELAY_TO_AUTH:
    {
        HandleRelayToAuth(connection, msg);
        break;
    }

    case MSG_EDIT_TERRAIN:
    {
        HandleEditTerrain(connection, msg);
        break;
    }

    case MSG_EDIT_OBJECT:
    {
        HandleEditObject(connection, msg);
        break;
    }

    case MSG_WATER_EDIT:
    {
        HandleWaterEdit(connection, msg);
        break;
    }

    case MSG_PATCH_POSITION:
    {
        HandlePatchPosition(connection, msg);
        break;
    }

    case MSG_TERRAIN_SYNC:
    {
        HandleTerrainSync(connection, msg);
        break;
    }

    case MSG_EAT:
    {
        HandleEat(connection, msg);
        break;
    }

    case MSG_DRINK:
    {
        HandleDrink(connection, msg);
        break;
    }

    case MSG_PICKUP:
    {
        HandlePickup(connection, msg);
        break;
    }

    case MSG_RESOURCE_HARVEST:
    {
        HandleResourceHarvest(connection, msg);
        break;
    }

    case MSG_DROP:
    {
        HandleDrop(connection, msg);
        break;
    }



    case MSG_CRAFT:
    {
        HandleCraft(connection, msg);
        break;
    }

    case MSG_EQUIP:
    {
        HandleEquip(connection, msg);
        break;
    }

    case MSG_UNEQUIP:
    {
        HandleUnequip(connection, msg);
        break;
    }

    case MSG_BUILD:
    {
        HandleBuild(connection, msg);
        break;
    }

    case MSG_DEMOLISH:
    {
        HandleDemolish(connection, msg);
        break;
    }

    case MSG_GATE_TOGGLE:
    {
        HandleGateToggle(connection, msg);
        break;
    }

    case MSG_REPAIR:
    {
        HandleRepair(connection, msg);
        break;
    }

    case MSG_SLEEP:
    {
        HandleSleep(connection, msg);
        break;
    }

    case MSG_SET_RESPAWN:
    {
        HandleSetRespawn(connection, msg);
        break;
    }

    case MSG_QUERY_DEATH_LOG:
    {
        HandleQueryDeathLog(connection, msg);
        break;
    }

    case MSG_QUERY_DEATH_ANALYTICS:
    {
        HandleQueryDeathAnalytics(connection, msg);
        break;
    }

    case MSG_ATTACK:
    {
        HandleAttack(connection, msg);
        break;
    }

    case MSG_PLACE_TRAP:
    {
        HandlePlaceTrap(connection, msg);
        break;
    }

    case MSG_HARVEST:
    {
        HandleHarvest(connection, msg);
        break;
    }

    case MSG_TRAP_CHECK:
    {
        HandleTrapCheck(connection, msg);
        break;
    }

    case MSG_PLANT_CROP:
    {
        HandlePlantCrop(connection, msg);
        break;
    }

    case MSG_HARVEST_CROP:
    {
        HandleHarvestCrop(connection, msg);
        break;
    }

    case MSG_POSSESS:
    {
        HandlePossess(connection, msg);
        break;
    }

    case MSG_UNPOSSESS:
    {
        HandleUnpossess(connection, msg);
        break;
    }

    case MSG_ADMIN_TIME_OVERRIDE:
    {
        // Only admin sessions can override time
        auto sIt = sessions_.Find(connection);
        if (sIt == sessions_.End() || sIt->second_.adminLevel < 1)
        {
            LogMessage("REJECTED: time override from non-admin " + connection->ToString());
            break;
        }
        float hour = msg.ReadFloat();
        int doy = msg.ReadI32();
        // New format: if message has more data, read the UTC epoch override
        if (msg.GetSize() - msg.GetPosition() >= 8)
        {
            long long epoch = (long long)msg.ReadI64();
            if (epoch < 0)
            {
                utcEpochOverride_ = -1;
                timeOverrideHour_ = -1.0f;
                timeOverrideDOY_ = -1;
                LogMessage("Admin time override CLEARED (epoch) by " + sIt->second_.username);
            }
            else
            {
                utcEpochOverride_ = epoch;
                timeOverrideHour_ = -1.0f;  // epoch takes precedence
                timeOverrideDOY_ = -1;
                LogMessage("Admin UTC epoch override: " + String((long long)epoch) +
                    " (darkness=" + String(GetDarkness(), 2) + ") by " + sIt->second_.username);
            }
        }
        else
        {
            // Legacy format: absolute hour + doy
            timeOverrideHour_ = hour;
            timeOverrideDOY_ = doy;
            utcEpochOverride_ = -1;
            if (hour < 0.0f)
                LogMessage("Admin time override CLEARED by " + sIt->second_.username);
            else
                LogMessage("Admin time override: hour=" + String(hour, 1) + " doy=" + String(doy) +
                    " (darkness=" + String(GetDarkness(), 2) + ") by " + sIt->second_.username);
        }
        break;
    }

    case MSG_TUNING_REQUEST:
    {
        HandleTuningRequest(connection);
        break;
    }

    case MSG_TUNING_UPDATE:
    {
        HandleTuningUpdate(connection, msg);
        break;
    }

    case MSG_MINE_REQUEST:
    {
        HandleMineRequest(connection, msg);
        break;
    }

    case MSG_CHOP_TREE:
    {
        HandleChopTree(connection, msg);
        break;
    }
    case MSG_TAP_TREE:
    {
        HandleTapTree(connection, msg);
        break;
    }
    case MSG_GOD_DIRECTIVE:
    {
        HandleGodDirective(connection, msg);
        break;
    }
    case MSG_OPEN_STORAGE:
    {
        HandleOpenStorage(connection, msg);
        break;
    }
    case MSG_CLOSE_STORAGE:
    {
        HandleCloseStorage(connection, msg);
        break;
    }
    case MSG_TRANSFER:
    {
        HandleTransfer(connection, msg);
        break;
    }
    case MSG_INSTALL_LOCK:
    {
        HandleInstallLock(connection, msg);
        break;
    }
    case MSG_REMOVE_LOCK:
    {
        HandleRemoveLock(connection, msg);
        break;
    }
    case MSG_TRADE_REQUEST:
    {
        HandleTradeRequest(connection, msg);
        break;
    }
    case MSG_MOUNT:
    {
        HandleMount(connection, msg);
        break;
    }
    case MSG_DISMOUNT:
    {
        HandleDismount(connection, msg);
        break;
    }
    case MSG_TRADE_ACCEPT:
    {
        HandleTradeAccept(connection, msg);
        break;
    }
    case MSG_TRADE_REJECT:
    {
        HandleTradeReject(connection, msg);
        break;
    }
    case MSG_TRADE_OFFER:
    {
        HandleTradeOffer(connection, msg);
        break;
    }
    case MSG_TRADE_LOCK:
    {
        HandleTradeLock(connection, msg);
        break;
    }
    case MSG_TRADE_CANCEL:
    {
        HandleTradeCancel(connection, msg);
        break;
    }

    default:
        LogMessage("Unknown msg ID " + String(msgID) + " from " + connection->ToString());
        break;
    }
}

void AuthServer::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float dt = eventData[P_TIMESTEP].GetFloat();
    uptime_ += dt;

    // Claudette IPC — non-blocking poll
    PollIPC();

    // Melbourne clock
    if (melbourneClock_)
        melbourneClock_->Update();

    // Phase 4 hardening: kick unauthenticated connections past timeout
    SweepUnauthConnections();

    // BOM weather fetch (every 10 minutes, first fetch at startup)
    weatherFetchTimer_ -= dt;
    if (weatherFetchTimer_ <= 0.0f)
    {
        weatherFetchTimer_ = WEATHER_FETCH_INTERVAL;
        FetchBOMWeather();
    }

    // Check for completed HTTP responses
    ProcessBOMResponse();

    // Quantum entropy pool: process in-flight QRNG response and refill when low
    ProcessQRNGResponse();
    if (entropyPoolSize_ < ENTROPY_REFILL_THRESHOLD)
    {
        qrngFetchTimer_ -= dt;
        if (qrngFetchTimer_ <= 0.0f)
        {
            FetchQuantumEntropy();
            qrngFetchTimer_ = QRNG_FETCH_COOLDOWN;
        }
    }

    // Broadcast weather to all clients periodically
    if (weatherReady_)
    {
        weatherBroadcastTimer_ -= dt;
        if (weatherBroadcastTimer_ <= 0.0f)
        {
            weatherBroadcastTimer_ = WEATHER_BROADCAST_INTERVAL;
            BroadcastWeather();
        }
    }

    // Celestial state broadcast (lunar phase, eclipse)
    celestialBroadcastTimer_ -= dt;
    if (celestialBroadcastTimer_ <= 0.0f)
    {
        celestialBroadcastTimer_ = CELESTIAL_BROADCAST_INTERVAL;
        BroadcastCelestialState();
    }

    // Trim terrain edit journals periodically
    journalTrimTimer_ -= dt;
    if (journalTrimTimer_ <= 0.0f)
    {
        journalTrimTimer_ = JOURNAL_TRIM_INTERVAL;
        journalManager_.TrimAll();
    }

    // Batch-dispatch NPC priority evaluation via compute shader (or CPU fallback)
    DispatchNPCPriorityCompute();

    // Server-authoritative creature AI tick
    TickCreatureAI(dt);

    // Phase 4c: burn down lit torches
    TickTorchTimers(dt);

    // Water fish traps: passive catch on a timer
    TickWaterTraps(dt);

    // Land traps: server-side proactive catch (Combat Phase 2)
    landTrapTickTimer_ -= dt;
    if (landTrapTickTimer_ <= 0.0f)
    {
        landTrapTickTimer_ = LAND_TRAP_TICK_INTERVAL;
        TickLandTraps(dt);
    }

    // Rain collection: barrels fill during rain
    TickBarrelRainCollection(dt);

    // World phenomena: fire-near-ore, seed-on-fertile, etc. (Plan 10)
    PhenomenaTick(dt);

    // Agriculture: advance crop growth stages
    CropGrowthTick(dt);

    // Survival pressure tick
#ifdef URHO3D_DATABASE_SQLITE
    SurvivalTick(dt);

    // Building decay tick
    buildingDecayTimer_ -= dt;
    if (buildingDecayTimer_ <= 0.0f)
    {
        buildingDecayTimer_ = BUILDING_DECAY_INTERVAL;
        // Game time is UTC — real seconds, no gameTimeScale_ multiplier.
        float gameDayFraction = BUILDING_DECAY_INTERVAL / 86400.0f;
        BuildingDecayTick(gameDayFraction);
    }

    // Track game days for decay grace periods
    int prevDay = currentGameDay_;
    // Game time is UTC — derive day counter from the real calendar, not from
    // a synthetic uptime accumulator. Unix days since epoch gives a monotonic
    // counter that survives restarts and doesn't need persistence.
    currentGameDay_ = (int)(time(nullptr) / 86400);

    // Economy daily tick — runs once per game-day transition
    if (currentGameDay_ > lastEconomyDay_)
    {
        EconomyDailyTick();
        TickTreeGrowth();
        TickFoodDecay(1.0f);  // 1 game day elapsed
        TickWellRefill();     // Water Phase 4: wells refill overnight
        TickDrought();        // Water Phase 5: drought tracking + effects
        CheckInnovations();   // Phase 34: philosopher mechanic
    }
#endif

    // Resource map respawn tick
    if (resourceMap_ && resourceMap_->GetImage())
    {
        resourceRespawnTimer_ -= dt;
        if (resourceRespawnTimer_ <= 0.f)
        {
            resourceRespawnTimer_ = RESOURCE_RESPAWN_INTERVAL;

            Vector<ResourceMap::RespawnEvent> respawned;
            resourceMap_->TickRespawn(&respawned);

            // Broadcast respawns to clients so pickup nodes reappear
            for (unsigned i = 0; i < respawned.Size(); ++i)
            {
                const auto& ev = respawned[i];
                BroadcastResourceDepleted(ev.worldX, ev.worldZ, ev.qty, ev.type);
            }

            if (!respawned.Empty())
            {
                resourceMapDirty_ = true;
                resourceMapSaveTimer_ = RESOURCE_SAVE_AFTER_CHANGE;
            }
        }

        // Dual-trigger save: after changes (30s cooldown) and periodic (5min)
        if (resourceMapDirty_)
        {
            resourceMapSaveTimer_ -= dt;
            if (resourceMapSaveTimer_ <= 0.f)
                SaveResourceMapIfDirty();
        }
        resourceMapPeriodicSaveTimer_ -= dt;
        if (resourceMapPeriodicSaveTimer_ <= 0.f)
        {
            resourceMapPeriodicSaveTimer_ = RESOURCE_PERIODIC_SAVE;
            if (resourceMapDirty_)
                SaveResourceMapIfDirty();
        }
    }

    // World database WAL checkpoint (every 5 minutes)
    WorldCheckpointTick(dt);
}

void AuthServer::HandleKeyExchangeAuth(StringHash eventType, VariantMap& eventData)
{
    using namespace KeyExchangeAuth;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    String username = eventData[P_USERNAME].GetString();

    if (!db_)
    {
        eventData[P_FOUND] = false;
        return;
    }

    // Reserved offline-mode login
    if (username == String(OFFLINE_RESERVED_USERNAME))
    {
        const String addr = connection ? connection->GetAddress() : String::EMPTY;
        const bool isLocal = (addr == "127.0.0.1" || addr == "::1" || addr == "localhost");
        if (!isLocal)
        {
            eventData[P_FOUND] = false;
            LogMessage("PAKE: rejected reserved '" + String(OFFLINE_RESERVED_USERNAME) +
                       "' login from non-local address '" + addr + "'");
            return;
        }

        DbResult exists = db_->Execute(
            "SELECT id FROM users WHERE username = '" +
            SqlEscape(String(OFFLINE_RESERVED_USERNAME)) + "'"
        );
        if (exists.GetRows().Empty())
        {
            if (!RegisterUser(String(OFFLINE_RESERVED_USERNAME),
                              String(OFFLINE_RESERVED_PASSWORD)))
            {
                eventData[P_FOUND] = false;
                LogMessage("PAKE: failed to auto-provision reserved '" +
                           String(OFFLINE_RESERVED_USERNAME) + "' account");
                return;
            }
            LogMessage("PAKE: auto-provisioned reserved '" +
                       String(OFFLINE_RESERVED_USERNAME) + "' account for localhost client");
        }
    }

    // Look up stored PAKE hash for this user
    DbResult result = db_->Execute(
        "SELECT pake_hash FROM users WHERE username = '" + SqlEscape(username) + "'"
    );

    if (result.GetRows().Empty())
    {
        eventData[P_FOUND] = false;
        LogMessage("PAKE: unknown user '" + username + "' (random hash will be mixed)");
        return;
    }

    String storedHex = result.GetRows()[0][0].GetString();
    if (storedHex.Empty())
    {
        eventData[P_FOUND] = false;
        LogMessage("PAKE: user '" + username + "' has no PAKE hash (migration needed)");
        return;
    }

    // Hex-decode and XOR-decode to recover the raw 32-byte hash
    Vector<unsigned char> hash = HexDecode(storedHex);
    XorObfuscate(hash.Buffer(), hash.Size());

    eventData[P_PASSWORDHASH].SetBuffer(reinterpret_cast<const void*>(hash.Buffer()), hash.Size());
    eventData[P_FOUND] = true;
    LogMessage("PAKE: password hash provided for user '" + username + "'");
}

void AuthServer::HandleClientAuthenticated(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientAuthenticated;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    String username = eventData[P_USERNAME].GetString();

    // Look up admin level
    int adminLevel = 0;
    if (db_)
    {
        DbResult result = db_->Execute(
            "SELECT admin_level FROM users WHERE username = '" + SqlEscape(username) + "'"
        );
        if (!result.GetRows().Empty())
            adminLevel = result.GetRows()[0][0].GetI32();
    }

    // Reject if this username is already logged in on another connection (admins exempt)
    Connection* existing = FindSessionByUsername(username);
    if (existing && existing != connection && adminLevel == 0)
    {
        LogMessage("PAKE: Login rejected for '" + username + "': already logged in");
        VectorBuffer reply;
        reply.WriteI32(MSG_AUTH_LOGIN);
        reply.WriteBool(false);
        reply.WriteString("Already logged in");
        reply.WriteI32(0);
        reply.WriteString(String::EMPTY);
        reply.WriteI32(0);  // 0 patches
        connection->SendMessage(MSG_AUTH_RESULT, true, true, reply);
        return;
    }

    // Update session
    sessions_[connection].username = username;
    sessions_[connection].authenticated = true;
    sessions_[connection].adminLevel = adminLevel;

    LogMessage("PAKE: User '" + username + "' authenticated (level " + String(adminLevel) + ")");
    URHO3D_LOGINFOF("[NetDebug] AUTH COMPLETE for %s — beginning post-auth sequence", username.CString());
    RefreshClientList();

    // Send auth result to client
    VectorBuffer reply;
    reply.WriteI32(MSG_AUTH_LOGIN);  // echo MSG_AUTH_LOGIN so client handles it the same way
    reply.WriteBool(true);
    reply.WriteString("Login successful");
    reply.WriteI32(adminLevel);
    reply.WriteString(sceneName_);
    // Append ALL owned patch coordinates
    int homePatchX = 0, homePatchZ = 0;
    if (db_)
    {
        DbResult patchResult = db_->Execute(
            "SELECT patch_x, patch_z FROM patches WHERE owner_name = '" + SqlEscape(username) + "'"
        );
        const auto& rows = patchResult.GetRows();
        reply.WriteI32((int)rows.Size());
        for (unsigned i = 0; i < rows.Size(); ++i)
        {
            int px = rows[i][0].GetI32();
            int pz = rows[i][1].GetI32();
            reply.WriteI32(px);
            reply.WriteI32(pz);
            if (i == 0) { homePatchX = px; homePatchZ = pz; }
        }
    }
    else
    {
        reply.WriteI32(0);
    }
    connection->SendMessage(MSG_AUTH_RESULT, true, true, reply);
    URHO3D_LOGINFOF("[NetDebug] Sent MSG_AUTH_RESULT to %s (success=%d)", username.CString(), (int)true);

    // Replicate scene to client — but NOT for localhost connections. In offline
    // mode the client loads its own scene (from TestScene.xml or procedural).
    // Server-side SetScene sends MSG_LOADSCENE which triggers scene_->Clear()
    // on the client, destroying the world the client just set up. Localhost
    // doesn't need replication — the server is auth/inventory/combat only.
    // All clients get SetScene — server owns creature spawning and the client
    // receives replicated nodes. No special-casing for localhost.
    if (scene_)
    {
        URHO3D_LOGINFOF("[NetDebug] SetScene for %s — scene has %u children", username.CString(), scene_->GetNumChildren());
        connection->SetScene(scene_);
    }

    // Lightweight tracker node — no capsule, no physics. Player starts in god cam
    // and possesses NPCs to interact. Tracker provides position for range checks.
    Node* tracker = scene_ ? scene_->CreateChild("ClientTracker") : nullptr;
    if (tracker)
    {
        const float patchWorldSize = 128.0f;
        float spawnX = (homePatchX + 0.5f) * patchWorldSize;
        float spawnZ = (homePatchZ + 0.5f) * patchWorldSize;
        tracker->SetPosition(Vector3(spawnX, 20.0f, spawnZ));
        serverObjects_[connection] = tracker;

        VariantMap remoteEventData;
        remoteEventData[P_ID] = tracker->GetID();
        connection->SendRemoteEvent(E_CLIENTOBJECTID, true, remoteEventData);
        URHO3D_LOGINFOF("[NetDebug] Tracker node %u assigned to %s (god cam)", tracker->GetID(), username.CString());
    }

    // Send current weather to newly connected client
    if (weatherReady_)
    {
        URHO3D_LOGINFOF("[NetDebug] Sending weather to %s", username.CString());
        SendWeatherToClient(connection);
    }

    // Send celestial state (moon phase) on login
    SendCelestialToClient(connection);

    // Per-patch resource streaming — send 3×3 neighbourhood around home patch
    URHO3D_LOGINFOF("[NetDebug] Sending patch neighbourhood to %s", username.CString());
    sessions_[connection].lastPatchPos = IntVector2(homePatchX, homePatchZ);
    sessions_[connection].sentPatches.Clear();
    SendPatchNeighbourhood(connection, homePatchX, homePatchZ);
    URHO3D_LOGINFOF("[NetDebug] Post-auth sequence COMPLETE for %s", username.CString());

    // Send inventory snapshot on connect
#ifdef URHO3D_DATABASE_SQLITE
    if (gameDB_)
    {
        int playerId = GetPlayerId(username);
        SendInventoryUpdate(connection, playerId);

        // Send all existing buildings and crops to newly connected client
        SendExistingBuildings(connection);
        SendExistingCrops(connection);

        // Send all existing trees
        SendTreesTo(connection);

        // Send fish spawn points from water body analysis
        SendFishSpawnsTo(connection);

        // Send settlement patch claims
        SendSettlementClaimsTo(connection);
    }
#endif

    // Creatures are REPLICATED scene nodes — Urho3D sends them automatically
    // when SetScene is called. No custom message needed.

    // Send server-authoritative resource map to client
    SendResourceMapToClient(connection);
}

// ============================================================
// Auth / Patch logic
// ============================================================

bool AuthServer::AuthenticateUser(const String& username, const String& password, int& adminLevel)
{
    adminLevel = 0;
    if (!db_ || username.Trimmed().Empty() || password.Empty())
        return false;

    DbResult result = db_->Execute(
        "SELECT password_hash, admin_level FROM users WHERE username = '" + SqlEscape(username) + "'"
    );
    if (result.GetRows().Empty())
        return false;

    String storedHex = result.GetRows()[0][0].GetString();
    Vector<unsigned char> storedBytes = HexDecode(storedHex);

    // XOR-decode to recover the stored SHA-256 hash
    XorObfuscate(storedBytes.Buffer(), storedBytes.Size());

    // Hash the provided password and compare
    unsigned char candidateHash[32];
    Urho3D::SHA256Hash(reinterpret_cast<const unsigned char*>(password.CString()),
                       password.Length(), candidateHash);

    // Constant-time comparison
    if (storedBytes.Size() != 32)
        return false;
    unsigned diff = 0;
    for (int i = 0; i < 32; ++i)
        diff |= storedBytes[i] ^ candidateHash[i];
    if (diff != 0)
        return false;

    adminLevel = result.GetRows()[0][1].GetI32();
    return true;
}

bool AuthServer::RegisterUser(const String& username, const String& password)
{
    if (!db_ || username.Trimmed().Empty() || password.Empty())
        return false;

    DbResult check = db_->Execute(
        "SELECT id FROM users WHERE username = '" + SqlEscape(username) + "'"
    );
    if (!check.GetRows().Empty())
        return false;

    String hashHex = HashPasswordSHA256(password);

    db_->Execute(
        "INSERT INTO users (username, password_hash, pake_hash) VALUES ('" +
        SqlEscape(username) + "', '" + SqlEscape(hashHex) + "', '" + SqlEscape(hashHex) + "')"
    );

    // Auto-allocate a random patch to the new user
    int px, pz;
    if (AllocateRandomPatch(username, px, pz))
        LogMessage("Allocated patch (" + String(px) + "," + String(pz) + ") to new user " + username);
    else
        LogMessage("[WARN] No free patches for new user " + username);

    return true;
}

bool AuthServer::ClaimPatch(int patchX, int patchZ, const String& username)
{
    if (!db_)
        return false;

    DbResult check = db_->Execute(
        "SELECT owner_name FROM patches WHERE patch_x = " + String(patchX) +
        " AND patch_z = " + String(patchZ)
    );
    if (!check.GetRows().Empty())
        return false;

    db_->Execute(
        "INSERT INTO patches (patch_x, patch_z, owner_name) VALUES (" +
        String(patchX) + ", " + String(patchZ) + ", '" + SqlEscape(username) + "')"
    );

    // Generate terrain on demand if not already loaded
    if (!terrainGrid_.Contains(IntVector2(patchX, patchZ)))
        GenerateTerrainHeightmap(patchX, patchZ);

    return true;
}

bool AuthServer::AllocateRandomPatch(const String& username, int& outPatchX, int& outPatchZ)
{
    if (!db_)
        return false;

    // Collect already-claimed patches
    DbResult claimed = db_->Execute("SELECT patch_x, patch_z FROM patches");
    Vector<IntVector2> taken;
    for (unsigned i = 0; i < claimed.GetNumRows(); ++i)
    {
        int cx = claimed.GetRows()[i][0].GetI32();
        int cz = claimed.GetRows()[i][1].GetI32();
        taken.Push(IntVector2(cx, cz));
    }

    // Build list of free patches in valid range -8..7
    Vector<IntVector2> freePatch;
    for (int x = -8; x <= 7; ++x)
    {
        for (int z = -8; z <= 7; ++z)
        {
            IntVector2 candidate(x, z);
            bool isTaken = false;
            for (unsigned i = 0; i < taken.Size(); ++i)
            {
                if (taken[i] == candidate)
                {
                    isTaken = true;
                    break;
                }
            }
            if (!isTaken)
                freePatch.Push(candidate);
        }
    }

    if (freePatch.Empty())
        return false;

    // Pick one at random
    SetRandomSeed(Time::GetTimeSinceEpoch() ^ (unsigned)freePatch.Size());
    unsigned idx = Rand() % freePatch.Size();
    outPatchX = freePatch[idx].x_;
    outPatchZ = freePatch[idx].y_;

    // Insert into patches table
    db_->Execute(
        "INSERT INTO patches (patch_x, patch_z, owner_name) VALUES (" +
        String(outPatchX) + ", " + String(outPatchZ) + ", '" + SqlEscape(username) + "')"
    );

    // Generate terrain on demand if not already loaded
    if (!terrainGrid_.Contains(IntVector2(outPatchX, outPatchZ)))
        GenerateTerrainHeightmap(outPatchX, outPatchZ);

    return true;
}

AuthServer::PatchInfo AuthServer::QueryPatchOwner(int patchX, int patchZ)
{
    PatchInfo info{patchX, patchZ, String::EMPTY, String::EMPTY, 0};

    if (!db_)
        return info;

    DbResult result = db_->Execute(
        "SELECT owner_name FROM patches WHERE patch_x = " + String(patchX) +
        " AND patch_z = " + String(patchZ)
    );

    if (!result.GetRows().Empty())
    {
        info.ownerName = result.GetRows()[0][0].GetString();

        for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
        {
            if (it->second_.username == info.ownerName && it->second_.authenticated)
            {
                info.ownerAddress = it->first_->ToString();
                break;
            }
        }
    }

    return info;
}

// ============================================================
// Peer brokering
// ============================================================

Connection* AuthServer::FindSessionByUsername(const String& username)
{
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.username == username && it->second_.authenticated)
            return it->first_;
    }
    return nullptr;
}

Connection* AuthServer::FindSessionByGuid(const String& guid)
{
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.guid == guid)
            return it->first_;
    }
    return nullptr;
}

void AuthServer::IntroducePeers(Connection* requester, Connection* owner, int patchX, int patchZ)
{
    auto reqIt = sessions_.Find(requester);
    auto ownIt = sessions_.Find(owner);
    if (reqIt == sessions_.End() || ownIt == sessions_.End())
        return;

    // Both clients need GUIDs registered
    if (reqIt->second_.guid.Empty() || ownIt->second_.guid.Empty())
    {
        LogMessage("Peer introduction skipped — one or both clients have no GUID registered");
        return;
    }

    // Generate a 32-byte random token
    Vector<unsigned char> token(32);
    Urho3D::CryptoRandomBytes(token.Buffer(), 32);

    // Send MSG_PEER_INTRODUCE to requester
    {
        VectorBuffer intro;
        intro.WriteString(ownIt->second_.guid);
        intro.Write(token.Buffer(), token.Size());
        intro.WriteI32(patchX);
        intro.WriteI32(patchZ);
        requester->SendMessage(MSG_PEER_INTRODUCE, true, true, intro);
    }

    // Send MSG_PEER_INTRODUCE to owner
    {
        VectorBuffer intro;
        intro.WriteString(reqIt->second_.guid);
        intro.Write(token.Buffer(), token.Size());
        intro.WriteI32(patchX);
        intro.WriteI32(patchZ);
        owner->SendMessage(MSG_PEER_INTRODUCE, true, true, intro);
    }

    LogMessage("Introduced peers: " + reqIt->second_.username + " <-> " + ownIt->second_.username +
        " for patch (" + String(patchX) + "," + String(patchZ) + ")");
}

void AuthServer::HandleRelayToAuth(Connection* connection, MemoryBuffer& msg)
{
    // A subserver is relaying a message from its subclient to us
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
    {
        LogMessage("[WARN] MSG_RELAY_TO_AUTH from unauthenticated client — ignoring");
        return;
    }

    String subClientUsername = msg.ReadString();
    if (subClientUsername.Length() > 64)
        return;
    int innerMsgID = msg.ReadI32();
    unsigned innerSize = msg.ReadU32();
    if (innerSize > 65536)  // 64KB cap — no legitimate message is larger
    {
        LogMessage("[WARN] Relay innerSize " + String(innerSize) + " exceeds cap — dropping");
        return;
    }

    // Read the inner message payload
    Vector<unsigned char> innerData(innerSize);
    if (innerSize > 0)
        msg.Read(innerData.Buffer(), innerSize);

    MemoryBuffer innerMsg(innerData.Buffer(), innerData.Size());

    LogMessage("Relay from subclient '" + subClientUsername + "' via subserver '" +
        it->second_.username + "': msgID=" + String(innerMsgID));

    // Process the inner message as if it came from the subclient directly
    // For now, handle MSG_PATCH_QUERY relayed from subclients
    switch (innerMsgID)
    {
    case MSG_PATCH_QUERY:
    {
        int px = innerMsg.ReadI32();
        int pz = innerMsg.ReadI32();
        PatchInfo info = QueryPatchOwner(px, pz);

        // Build the inner reply
        VectorBuffer innerReply;
        innerReply.WriteI32(px);
        innerReply.WriteI32(pz);
        innerReply.WriteString(info.ownerName);
        innerReply.WriteString(info.ownerAddress);
        innerReply.WriteU16(info.ownerPort);

        // Wrap in MSG_RELAY_FROM_AUTH for the subserver to forward to subclient
        VectorBuffer relay;
        relay.WriteString(subClientUsername);
        relay.WriteI32(MSG_PATCH_RESULT);
        relay.WriteU32(innerReply.GetSize());
        relay.Write(innerReply.GetData(), innerReply.GetSize());
        connection->SendMessage(MSG_RELAY_FROM_AUTH, true, true, relay);

        // If patch is owned by a different online client, introduce peers
        // (the introduction goes to subclient via relay, and directly to owner)
        if (!info.ownerName.Empty() && info.ownerName != subClientUsername)
        {
            Connection* ownerConn = FindSessionByUsername(info.ownerName);
            if (ownerConn)
            {
                // Find subclient's GUID through the subserver's session
                // The subclient's GUID was stored when we introduced them originally
                LogMessage("Relay: subclient '" + subClientUsername +
                    "' needs introduction to patch owner '" + info.ownerName + "'");
                // New introductions for migrating subclients would be handled here
                // For now, log it — full migration will be handled in a follow-up
            }
        }
        break;
    }
    default:
        LogMessage("Relay: unhandled inner msgID " + String(innerMsgID));
        break;
    }
}

// ---------------------------------------------------------------------------
// Server-authoritative edits
// ---------------------------------------------------------------------------

bool AuthServer::ValidateEditLocation(const String& username, int adminLevel, const Vector3& worldPos)
{
    // Admins can edit anywhere
    if (adminLevel > 0)
        return true;

    // Convert world position to patch coordinates
    // Terrain spacing = 2.0, patch size = 64 cells → 128 world units per patch
    // Terrain is centered at origin, offset by half-terrain-size
    const float patchWorldSize = 128.0f;  // 64 cells * 2.0 spacing
    int patchX = (int)floorf(worldPos.x_ / patchWorldSize);
    int patchZ = (int)floorf(worldPos.z_ / patchWorldSize);

    PatchInfo info = QueryPatchOwner(patchX, patchZ);
    if (info.ownerName.Empty())
        return false;  // unclaimed patch — no one can edit

    return info.ownerName == username;
}

void AuthServer::HandleEditTerrain(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
    {
        // Not authenticated — reject
        unsigned editID = msg.ReadU32();
        VectorBuffer reject;
        reject.WriteU32(editID);
        reject.WriteString("Not authenticated");
        connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
        return;
    }

    const String& username = it->second_.username;
    int adminLevel = it->second_.adminLevel;

    // Read the full payload
    unsigned editID = msg.ReadU32();
    Vector3 worldPos = msg.ReadVector3();
    int brushMode = msg.ReadI32();
    int brushShape = msg.ReadI32();
    float brushRadius = msg.ReadFloat();
    float brushStrength = msg.ReadFloat();
    float smoothStrength = msg.ReadFloat();
    float brushRotation = msg.ReadFloat();
    float timeStep = msg.ReadFloat();
    float lockedFlattenHeight = msg.ReadFloat();

    // Validate patch ownership
    if (!ValidateEditLocation(username, adminLevel, worldPos))
    {
        VectorBuffer reject;
        reject.WriteU32(editID);
        reject.WriteString("No permission to edit this patch");
        connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
        LogMessage("Edit rejected: " + username + " has no permission at (" +
            String((int)worldPos.x_) + "," + String((int)worldPos.z_) + ")");
        return;
    }

    // Sanity checks
    if (brushRadius < 0.25f || brushRadius > 50.0f ||
        brushStrength < 0.0f || brushStrength > 5.0f)
    {
        VectorBuffer reject;
        reject.WriteU32(editID);
        reject.WriteString("Invalid brush parameters");
        connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
        return;
    }

    // Apply edit to server's authoritative terrain
    if (terrainBrush_)
    {
        terrainBrush_->SetMode(brushMode);
        terrainBrush_->SetShape(brushShape);
        terrainBrush_->SetRadius(brushRadius);
        terrainBrush_->SetStrength(brushStrength);
        terrainBrush_->SetSmoothStrength(smoothStrength);
        terrainBrush_->SetRotation(brushRotation);
        terrainBrush_->SetFlattenHeight(lockedFlattenHeight);
        terrainBrush_->Apply(worldPos, timeStep);

        // Check if terrain lowering exposed any metal deposits
        if (brushMode == 2)  // lower brush only
            CheckExposedDeposits(worldPos, brushRadius);
    }

    // Record edit in journal for reconnect sync
    {
        // Determine which grid cell this edit belongs to (currently only grid 0,0)
        int gridX = 0, gridZ = 0;
        TerrainJournal& journal = journalManager_.GetJournal(gridX, gridZ);
        TerrainEdit terrainEdit;
        terrainEdit.version = journal.GetVersion() + 1;
        terrainEdit.worldPos = worldPos;
        terrainEdit.brushMode = brushMode;
        terrainEdit.brushShape = brushShape;
        terrainEdit.brushRadius = brushRadius;
        terrainEdit.brushStrength = brushStrength;
        terrainEdit.smoothStrength = smoothStrength;
        terrainEdit.brushRotation = brushRotation;
        terrainEdit.flattenHeight = lockedFlattenHeight;
        terrainEdit.timeStep = timeStep;
        terrainEdit.timestamp = (unsigned)time(nullptr);
        journalManager_.RecordEdit(gridX, gridZ, terrainEdit);

        // Update hash periodically (every 50 edits to avoid per-edit cost)
        if (terrainEdit.version % 50 == 0)
        {
            auto* terrain = scene_->GetComponent<Terrain>(true);
            if (terrain && terrain->GetHeightMap())
                journal.UpdateHash(terrain->GetHeightMap());
        }
    }

    // Broadcast to all other authenticated clients
    VectorBuffer broadcastPayload;
    broadcastPayload.WriteU32(editID);
    broadcastPayload.WriteVector3(worldPos);
    broadcastPayload.WriteI32(brushMode);
    broadcastPayload.WriteI32(brushShape);
    broadcastPayload.WriteFloat(brushRadius);
    broadcastPayload.WriteFloat(brushStrength);
    broadcastPayload.WriteFloat(smoothStrength);
    broadcastPayload.WriteFloat(brushRotation);
    broadcastPayload.WriteFloat(timeStep);
    broadcastPayload.WriteFloat(lockedFlattenHeight);

    BroadcastEdit(MSG_EDIT_TERRAIN, broadcastPayload, username, connection);
}

void AuthServer::HandleEditObject(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
    {
        unsigned editID = msg.ReadU32();
        VectorBuffer reject;
        reject.WriteU32(editID);
        reject.WriteString("Not authenticated");
        connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
        return;
    }

    const String& username = it->second_.username;
    int adminLevel = it->second_.adminLevel;

    unsigned editID = msg.ReadU32();
    unsigned char subtype = msg.ReadU8();

    // Read position for ownership validation
    Vector3 validationPos;
    VectorBuffer broadcastPayload;
    broadcastPayload.WriteU32(editID);
    broadcastPayload.WriteU8(subtype);

    switch (subtype)
    {
    case 0:  // create
    {
        String xmlData = msg.ReadString();
        Vector3 position = msg.ReadVector3();
        Vector3 surfaceNormal = msg.ReadVector3();
        validationPos = position;

        // Sanity: XML size limit (64KB)
        if (xmlData.Length() > 65536)
        {
            VectorBuffer reject;
            reject.WriteU32(editID);
            reject.WriteString("Object XML too large");
            connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
            return;
        }

        broadcastPayload.WriteString(xmlData);
        broadcastPayload.WriteVector3(position);
        broadcastPayload.WriteVector3(surfaceNormal);

        // Apply to server scene
        if (scene_)
        {
            XMLFile xmlFile(context_);
            if (xmlFile.FromString(xmlData))
            {
                Quaternion surfaceRot;
                surfaceRot.FromRotationTo(Vector3::UP, surfaceNormal);
                scene_->InstantiateXML(xmlFile.GetRoot(), position, surfaceRot);
            }
        }
        break;
    }
    case 1:  // delete
    {
        unsigned nodeID = msg.ReadU32();
        broadcastPayload.WriteU32(nodeID);

        // Use node's position for validation
        if (scene_)
        {
            Node* node = scene_->GetNode(nodeID);
            if (node)
            {
                validationPos = node->GetPosition();
                node->Remove();
            }
            else
            {
                VectorBuffer reject;
                reject.WriteU32(editID);
                reject.WriteString("Node not found");
                connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
                return;
            }
        }
        break;
    }
    case 2:  // transform
    {
        unsigned nodeID = msg.ReadU32();
        Vector3 pos = msg.ReadVector3();
        Quaternion rot = msg.ReadQuaternion();
        Vector3 scale = msg.ReadVector3();
        validationPos = pos;

        broadcastPayload.WriteU32(nodeID);
        broadcastPayload.WriteVector3(pos);
        broadcastPayload.WriteQuaternion(rot);
        broadcastPayload.WriteVector3(scale);

        // Apply to server scene
        if (scene_)
        {
            Node* node = scene_->GetNode(nodeID);
            if (node)
            {
                node->SetPosition(pos);
                node->SetRotation(rot);
                node->SetScale(scale);
            }
        }
        break;
    }
    default:
    {
        VectorBuffer reject;
        reject.WriteU32(editID);
        reject.WriteString("Unknown object edit subtype");
        connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
        return;
    }
    }

    // Validate ownership
    if (!ValidateEditLocation(username, adminLevel, validationPos))
    {
        VectorBuffer reject;
        reject.WriteU32(editID);
        reject.WriteString("No permission to edit objects in this patch");
        connection->SendMessage(MSG_EDIT_REJECT, true, true, reject);
        return;
    }

    BroadcastEdit(MSG_EDIT_OBJECT, broadcastPayload, username, connection);
}

void AuthServer::BroadcastEdit(int editMsgType, const VectorBuffer& data, const String& username, Connection* excludeConnection)
{
    VectorBuffer broadcast;
    broadcast.WriteString(username);
    broadcast.WriteI32(editMsgType);
    broadcast.Write(data.GetData(), data.GetSize());

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->first_ == excludeConnection)
            continue;
        if (!it->second_.authenticated)
            continue;

        it->first_->SendMessage(MSG_EDIT_BROADCAST, true, true, broadcast);
    }
}

// ---------------------------------------------------------------------------
// Terrain generation
// ---------------------------------------------------------------------------

void AuthServer::RegisterExistingTerrain()
{
    if (!scene_)
        return;

    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (terrain)
    {
        terrainGrid_[IntVector2(0, 0)] = terrain;
        LogMessage("Registered existing terrain as grid (0,0)");

        // Initialize journal hash from current heightmap
        if (terrain->GetHeightMap())
        {
            TerrainJournal& journal = journalManager_.GetJournal(0, 0);
            journal.UpdateHash(terrain->GetHeightMap());
            LogMessage("Terrain journal initialized — hash: " + String(journal.GetHash()));
        }
    }
}

void AuthServer::HandleTerrainSync(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
    {
        LogMessage("[WARN] MSG_TERRAIN_SYNC from unauthenticated client — ignoring");
        return;
    }

    journalManager_.HandleSyncRequest(connection, msg, terrainGrid_);
    LogMessage("Terrain sync request from " + it->second_.username);
}

SharedPtr<Image> AuthServer::GenerateTerrainHeightmap(int gridX, int gridZ)
{
    // Derive unique seed for this grid cell
    unsigned cellSeed = worldSeed_ ^ ((unsigned)(gridX * 73856093) ^ (unsigned)(gridZ * 19349663));
    terrainGen_.params.seed = cellSeed;

    // Check for neighbour terrains and extract edge data for stitching
    float* northEdge = nullptr;
    float* southEdge = nullptr;
    float* westEdge = nullptr;
    float* eastEdge = nullptr;
    int res = terrainGen_.params.resolution;

    // Helper: extract edge heights from an existing terrain's heightmap
    auto extractEdge = [&](IntVector2 neighbourGrid, int edgeType) -> float*
    {
        auto it = terrainGrid_.Find(neighbourGrid);
        if (it == terrainGrid_.End() || it->second_.Expired())
            return nullptr;

        Image* hm = it->second_->GetHeightMap();
        if (!hm || hm->GetWidth() != res)
            return nullptr;

        float* edge = new float[res];
        unsigned char* data = hm->GetData();
        int comps = hm->GetComponents();

        for (int i = 0; i < res; ++i)
        {
            int px, py;
            if (edgeType == 0)      { px = i; py = res - 1; }  // neighbour's south edge
            else if (edgeType == 1) { px = i; py = 0; }        // neighbour's north edge
            else if (edgeType == 2) { px = res - 1; py = i; }  // neighbour's east edge
            else                    { px = 0; py = i; }        // neighbour's west edge

            int idx = (py * res + px) * comps;
            edge[i] = (float)data[idx] / 255.0f;
            if (comps >= 2)
                edge[i] += (float)data[idx + 1] / 65280.0f;
        }
        return edge;
    };

    // North neighbour (gridZ - 1) provides our north edge (their south edge)
    northEdge = extractEdge(IntVector2(gridX, gridZ - 1), 0);
    // South neighbour (gridZ + 1) provides our south edge (their north edge)
    southEdge = extractEdge(IntVector2(gridX, gridZ + 1), 1);
    // West neighbour (gridX - 1) provides our west edge (their east edge)
    westEdge = extractEdge(IntVector2(gridX - 1, gridZ), 2);
    // East neighbour (gridX + 1) provides our east edge (their west edge)
    eastEdge = extractEdge(IntVector2(gridX + 1, gridZ), 3);

    SharedPtr<Image> image = terrainGen_.GenerateWithEdges(context_, northEdge, southEdge, westEdge, eastEdge);

    delete[] northEdge;
    delete[] southEdge;
    delete[] westEdge;
    delete[] eastEdge;

    if (image)
        CommitTerrainHeightmap(gridX, gridZ, image);

    return image;
}

void AuthServer::CommitTerrainHeightmap(int gridX, int gridZ, SharedPtr<Image> image)
{
    unsigned cellSeed = worldSeed_ ^ ((unsigned)(gridX * 73856093) ^ (unsigned)(gridZ * 19349663));

    // Save to disk
    String filename = "Data/Terrains/terrain_" + String(gridX) + "_" + String(gridZ) + ".png";
    auto* cache = GetSubsystem<ResourceCache>();
    String fullPath = cache->GetResourceDirs()[1] + "../" + filename;

    image->SavePNG(fullPath);
    LogMessage("Generated terrain heightmap: " + filename + " (seed=" + String(cellSeed) + ")");

    // Register in ResourceCache so Terrain serialization works
    String resName = "Terrains/terrain_" + String(gridX) + "_" + String(gridZ) + ".png";
    image->SetName(resName);
    cache->AddManualResource(image);

    // Create terrain node in scene
    if (scene_)
    {
        const float terrainWorldSize = 2048.0f;  // 1025 verts * 2.0 spacing
        float posX = gridX * terrainWorldSize - (terrainWorldSize * 0.5f);
        float posZ = gridZ * terrainWorldSize - (terrainWorldSize * 0.5f);

        Node* terrainNode = scene_->CreateChild("Terrain_" + String(gridX) + "_" + String(gridZ), LOCAL);
        terrainNode->SetPosition(Vector3(posX, 0.0f, posZ));

        auto* terrain = terrainNode->CreateComponent<Terrain>(LOCAL);
        terrain->SetSpacing(Vector3(2.0f, 0.5f, 2.0f));
        terrain->SetHeightMap(image);

        // Use same material as the original terrain
        auto* origTerrain = terrainGrid_.Contains(IntVector2(0, 0)) ?
            terrainGrid_[IntVector2(0, 0)].Get() : nullptr;
        if (origTerrain && origTerrain->GetMaterial())
            terrain->SetMaterial(origTerrain->GetMaterial());

        // Terrain collision — static body so avatars don't fall through
        auto* gridBody = terrainNode->CreateComponent<RigidBody>(LOCAL);
        gridBody->SetCollisionLayer(2);
        auto* gridShape = terrainNode->CreateComponent<CollisionShape>(LOCAL);
        gridShape->SetTerrain();

        terrainGrid_[IntVector2(gridX, gridZ)] = terrain;
        LogMessage("Created terrain node at grid (" + String(gridX) + "," + String(gridZ) +
            ") world pos (" + String(posX) + ", 0, " + String(posZ) + ")");
    }

    // Broadcast to all connected clients
    VectorBuffer pngBuf;
    image->Save(pngBuf);
    VectorBuffer msg;
    msg.WriteI32(gridX);
    msg.WriteI32(gridZ);
    msg.WriteBuffer(pngBuf.GetBuffer());
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_NEW_TERRAIN, true, true, msg);
    }
    LogMessage("Broadcast MSG_NEW_TERRAIN (" + String(gridX) + "," + String(gridZ) +
        ") to " + String(sessions_.Size()) + " client(s), " +
        String(pngBuf.GetSize()) + " bytes");
}

// ============================================================
// BOM Weather
// ============================================================

static const String BOM_OBS_URL = "https://api.weather.bom.gov.au/v1/locations/r1r143/observations";
static const String BOM_FORECAST_URL = "https://api.weather.bom.gov.au/v1/locations/r1r143/forecasts/daily";

void AuthServer::FetchBOMWeather()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // Don't start new requests while previous ones are in-flight
    if (bomObsRequest_ && bomObsRequest_->GetState() == HTTP_INITIALIZING)
        return;
    if (bomForecastRequest_ && bomForecastRequest_->GetState() == HTTP_INITIALIZING)
        return;

    bomObsData_.Clear();
    bomForecastData_.Clear();
    bomObsRequest_ = network->MakeHttpRequest(BOM_OBS_URL);
    bomForecastRequest_ = network->MakeHttpRequest(BOM_FORECAST_URL);

    LogMessage("BOM: Fetching weather from api.weather.bom.gov.au...");
}

void AuthServer::ProcessBOMResponse()
{
    // Read observation data as it arrives
    if (bomObsRequest_)
    {
        if (bomObsRequest_->GetState() == HTTP_INITIALIZING)
            return; // still connecting

        if (bomObsRequest_->GetState() == HTTP_ERROR)
        {
            LogMessage("BOM: Observation fetch error: " + bomObsRequest_->GetError());
            bomObsRequest_.Reset();
            return;
        }

        // Read available data
        while (bomObsRequest_->GetAvailableSize() > 0)
            bomObsData_ += bomObsRequest_->ReadLine() + "\n";

        // Check if complete
        if (bomObsRequest_->GetState() == HTTP_CLOSED && bomObsRequest_->GetAvailableSize() == 0)
            bomObsRequest_.Reset();  // done reading
        else
            return; // still reading
    }

    // Read forecast data as it arrives
    if (bomForecastRequest_)
    {
        if (bomForecastRequest_->GetState() == HTTP_INITIALIZING)
            return;

        if (bomForecastRequest_->GetState() == HTTP_ERROR)
        {
            LogMessage("BOM: Forecast fetch error: " + bomForecastRequest_->GetError());
            bomForecastRequest_.Reset();
            return;
        }

        while (bomForecastRequest_->GetAvailableSize() > 0)
            bomForecastData_ += bomForecastRequest_->ReadLine() + "\n";

        if (bomForecastRequest_->GetState() == HTTP_CLOSED && bomForecastRequest_->GetAvailableSize() == 0)
            bomForecastRequest_.Reset();
        else
            return;
    }

    // Both requests complete — parse
    if (bomObsData_.Empty() && bomForecastData_.Empty())
        return;

    // Parse observations
    if (!bomObsData_.Empty())
    {
        SharedPtr<JSONFile> json(new JSONFile(context_));
        if (json->FromString(bomObsData_))
        {
            const JSONValue& root = json->GetRoot();
            const JSONValue& data = root.Get("data");

            if (!data.IsNull())
            {
                weatherTemperature_ = data.Get("temp").GetFloat();
                weatherHumidity_ = data.Get("humidity").GetFloat();

                const JSONValue& wind = data.Get("wind");
                if (!wind.IsNull())
                {
                    weatherWindSpeed_ = wind.Get("speed_kilometre").GetFloat();
                    // Convert wind direction string to angle
                    String dir = wind.Get("direction").GetString();
                    if (dir == "N") weatherWindAngle_ = 0.0f;
                    else if (dir == "NNE") weatherWindAngle_ = 22.5f;
                    else if (dir == "NE") weatherWindAngle_ = 45.0f;
                    else if (dir == "ENE") weatherWindAngle_ = 67.5f;
                    else if (dir == "E") weatherWindAngle_ = 90.0f;
                    else if (dir == "ESE") weatherWindAngle_ = 112.5f;
                    else if (dir == "SE") weatherWindAngle_ = 135.0f;
                    else if (dir == "SSE") weatherWindAngle_ = 157.5f;
                    else if (dir == "S") weatherWindAngle_ = 180.0f;
                    else if (dir == "SSW") weatherWindAngle_ = 202.5f;
                    else if (dir == "SW") weatherWindAngle_ = 225.0f;
                    else if (dir == "WSW") weatherWindAngle_ = 247.5f;
                    else if (dir == "W") weatherWindAngle_ = 270.0f;
                    else if (dir == "WNW") weatherWindAngle_ = 292.5f;
                    else if (dir == "NW") weatherWindAngle_ = 315.0f;
                    else if (dir == "NNW") weatherWindAngle_ = 337.5f;
                    // CALM = 0 angle, 0 speed
                }

                const JSONValue& rainSince9am = data.Get("rain_since_9am");
                if (!rainSince9am.IsNull())
                    weatherPrecipitation_ = Clamp(rainSince9am.GetFloat() / 20.0f, 0.0f, 1.0f); // 20mm = heavy
            }
        }
        bomObsData_.Clear();
    }

    // Parse forecast for cloud cover / conditions
    if (!bomForecastData_.Empty())
    {
        SharedPtr<JSONFile> json(new JSONFile(context_));
        if (json->FromString(bomForecastData_))
        {
            const JSONValue& root = json->GetRoot();
            const JSONValue& data = root.Get("data");

            if (data.IsArray() && data.GetArray().Size() > 0)
            {
                const JSONValue& today = data.GetArray()[0];
                weatherCondition_ = today.Get("icon_descriptor").GetString();

                // Map icon_descriptor to cloud cover
                if (weatherCondition_ == "sunny" || weatherCondition_ == "clear")
                    weatherCloudCover_ = 0.05f;
                else if (weatherCondition_ == "mostly_sunny")
                    weatherCloudCover_ = 0.2f;
                else if (weatherCondition_ == "partly_cloudy")
                    weatherCloudCover_ = 0.4f;
                else if (weatherCondition_ == "mostly_cloudy" || weatherCondition_ == "cloudy")
                    weatherCloudCover_ = 0.7f;
                else if (weatherCondition_ == "hazy")
                    weatherCloudCover_ = 0.5f;
                else if (weatherCondition_ == "fog")
                    weatherCloudCover_ = 0.9f;
                else if (weatherCondition_ == "shower" || weatherCondition_ == "light_shower")
                    weatherCloudCover_ = 0.7f;
                else if (weatherCondition_ == "rain" || weatherCondition_ == "light_rain")
                    weatherCloudCover_ = 0.8f;
                else if (weatherCondition_ == "heavy_rain" || weatherCondition_ == "storm" || weatherCondition_ == "thunderstorm")
                    weatherCloudCover_ = 0.95f;
                else if (weatherCondition_ == "snow" || weatherCondition_ == "hail")
                    weatherCloudCover_ = 0.85f;
                else if (weatherCondition_ == "wind" || weatherCondition_ == "windy")
                    weatherCloudCover_ = 0.3f;
                else
                    weatherCloudCover_ = 0.3f; // default fallback

                // Rain chance from forecast if observations don't show rain yet
                const JSONValue& rain = today.Get("rain");
                if (!rain.IsNull())
                {
                    float chance = rain.Get("chance").GetFloat() / 100.0f;
                    float amount = rain.Get("amount").Get("max").GetFloat();
                    // Use forecast rain if observed rain is 0
                    if (weatherPrecipitation_ < 0.01f && chance > 0.5f)
                        weatherPrecipitation_ = Clamp(chance * amount / 20.0f, 0.0f, 1.0f);
                }
            }
        }
        bomForecastData_.Clear();
    }

    // Normalize wind to 0-1 range (0-80 km/h scale)
    float normalizedWind = Clamp(weatherWindSpeed_ / 80.0f, 0.0f, 1.0f);

    weatherReady_ = true;
    weatherBroadcastTimer_ = 0.0f; // broadcast immediately after first fetch

    LogMessage("BOM: Weather updated — " + weatherCondition_ +
        ", " + String((int)weatherTemperature_) + "C" +
        ", cloud " + String((int)(weatherCloudCover_ * 100)) + "%" +
        ", wind " + String((int)weatherWindSpeed_) + "km/h" +
        ", precip " + String((int)(weatherPrecipitation_ * 100)) + "%");

    RefreshWeatherPanel();
}

void AuthServer::SendWeatherToClient(Connection* connection)
{
    VectorBuffer msg;
    msg.WriteFloat(weatherCloudCover_);
    msg.WriteFloat(weatherPrecipitation_);
    msg.WriteFloat(Clamp(weatherWindSpeed_ / 80.0f, 0.0f, 1.0f)); // normalized 0-1
    msg.WriteFloat(weatherWindAngle_ * 3.14159f / 180.0f);         // degrees to radians
    msg.WriteFloat(GetEffectiveTemperature());
    msg.WriteFloat(weatherHumidity_);
    msg.WriteString(weatherCondition_);
    connection->SendMessage(MSG_WEATHER_UPDATE, true, true, msg);
}

void AuthServer::BroadcastWeather(Connection* singleClient)
{
    if (singleClient)
    {
        SendWeatherToClient(singleClient);
        return;
    }

    int count = 0;
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (!it->second_.authenticated)
            continue;
        SendWeatherToClient(it->first_);
        count++;
    }

    if (count > 0)
        LogMessage("BOM: Weather broadcast to " + String(count) + " client(s)");
}

// ============================================================
// Celestial state (server-authoritative lunar ephemeris)
// ============================================================

float AuthServer::CalculateMoonAge() const
{
    // Lunar synodic month: 29.53 days.
    // Reference new moon: January 6, 2000 (Julian day 2451550.1).
    // Calculate days since reference, mod by synodic period.
    time_t now = time(nullptr);
    // Days since Unix epoch
    double daysSinceEpoch = (double)now / 86400.0;
    // Reference: Jan 6 2000 = Unix day 10957.1 (approx)
    double daysSinceRef = daysSinceEpoch - 10957.1;
    double synodicMonth = 29.530588853;
    double moonAge = fmod(daysSinceRef, synodicMonth);
    if (moonAge < 0.0) moonAge += synodicMonth;
    return (float)moonAge;
}

bool AuthServer::IsLunarEclipse() const
{
    // Simplified: a lunar eclipse occurs near full moon (age ~14.76 days)
    // when the moon is near a lunar node (orbital plane crossing).
    // Real eclipses occur ~2-3 times per year.
    // Approximate by checking if moon age is within 0.5 days of full
    // AND the day-of-year modulo roughly aligns with eclipse seasons.
    float age = CalculateMoonAge();
    float fullMoonDist = fabsf(age - 14.765f);
    if (fullMoonDist > 0.5f) return false;

    // Eclipse seasons repeat roughly every 173 days (half draconic year)
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    int doy = utc->tm_yday + 1;
    // Approximate eclipse season windows: around day 15, 188, 361 (±10 days)
    int season1 = abs(doy - 15);
    int season2 = abs(doy - 188);
    int season3 = abs(doy - 361);
    int minDist = Min(season1, Min(season2, season3));
    return minDist <= 10;
}

void AuthServer::SendCelestialToClient(Connection* connection)
{
    VectorBuffer msg;
    float moonAge = CalculateMoonAge();
    bool eclipse = IsLunarEclipse();
    msg.WriteFloat(moonAge);          // 0..29.53 days
    msg.WriteBool(eclipse);           // blood moon active
    connection->SendMessage(MSG_CELESTIAL_STATE, true, true, msg);
}

void AuthServer::BroadcastCelestialState(Connection* singleClient)
{
    if (singleClient)
    {
        SendCelestialToClient(singleClient);
        return;
    }

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (!it->second_.authenticated)
            continue;
        SendCelestialToClient(it->first_);
    }
}

// ============================================================
// Water heightmap (server-authoritative)
// ============================================================

void AuthServer::LoadOrCreateWaterMap()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Try to load existing water heightmap from disk
    String resourcePath = "Textures/WaterHeightMap.png";
    auto* existing = cache->GetResource<Image>(resourcePath, false);
    if (existing)
    {
        waterHeightMap_ = existing;
        LogMessage("Water map loaded: " + resourcePath +
                   " (" + String(waterHeightMap_->GetWidth()) + "x" + String(waterHeightMap_->GetHeight()) + ")");
        return;
    }

    // Create empty water heightmap (same resolution as terrain: 1025x1025, single-channel)
    waterHeightMap_ = new Image(context_);
    waterHeightMap_->SetSize(1025, 1025, 1);  // single-channel grayscale

    // Zero-fill — no water anywhere
    for (int y = 0; y < 1025; ++y)
        for (int x = 0; x < 1025; ++x)
            waterHeightMap_->SetPixel(x, y, Color(0.0f, 0.0f, 0.0f));

    waterHeightMap_->SetName(resourcePath);
    cache->AddManualResource(waterHeightMap_);
    LogMessage("Water map created: 1025x1025 (empty)");
}

void AuthServer::SaveWaterMap()
{
    if (!waterHeightMap_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    String fullPath = cache->GetResourceDirs()[0] + "Textures/WaterHeightMap.png";
    waterHeightMap_->SavePNG(fullPath);
    LogMessage("Water map saved: " + fullPath);
}

// ============================================================
// Per-patch resource streaming
// ============================================================

static const float PATCH_WORLD_SIZE = 128.0f;  // 64 cells × 2.0 spacing
static const int PATCH_PIXELS = 64;             // pixels per patch in resource map

void AuthServer::SendResourcePatch(Connection* connection, const String& resourceID, Image* resourceMap, int patchX, int patchZ)
{
    if (!resourceMap)
        return;

    int imgW = resourceMap->GetWidth();
    int imgH = resourceMap->GetHeight();
    int components = resourceMap->GetComponents();

    // Patch pixel origin in the resource image (0,0 patch is centered)
    int pixelX = imgW / 2 + patchX * PATCH_PIXELS;
    int pixelZ = imgH / 2 + patchZ * PATCH_PIXELS;

    // Clamp to image bounds
    int x0 = Clamp(pixelX, 0, imgW);
    int z0 = Clamp(pixelZ, 0, imgH);
    int x1 = Clamp(pixelX + PATCH_PIXELS, 0, imgW);
    int z1 = Clamp(pixelZ + PATCH_PIXELS, 0, imgH);
    int pixelW = x1 - x0;
    int pixelH = z1 - z0;

    if (pixelW <= 0 || pixelH <= 0)
        return;  // patch is entirely outside the image

    // Copy raw pixel data
    unsigned dataSize = pixelW * pixelH * components;
    const unsigned char* src = resourceMap->GetData();
    int stride = imgW * components;

    VectorBuffer msg;
    msg.WriteString(resourceID);
    msg.WriteI32(patchX);
    msg.WriteI32(patchZ);
    msg.WriteI32(x0);
    msg.WriteI32(z0);
    msg.WriteI32(pixelW);
    msg.WriteI32(pixelH);
    msg.WriteI32(components);
    msg.WriteU32(dataSize);

    for (int row = 0; row < pixelH; ++row)
        msg.Write(src + (z0 + row) * stride + x0 * components, pixelW * components);

    connection->SendMessage(MSG_RESOURCE_PATCH, true, true, msg);
}

void AuthServer::SendPatchNeighbourhood(Connection* connection, int centerX, int centerZ)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End())
        return;

    int sent = 0;
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int px = centerX + dx;
            int pz = centerZ + dz;
            unsigned long long key = PatchKey(px, pz);

            if (it->second_.sentPatches.Contains(key))
                continue;  // already sent this patch

            // Send water heightmap patch
            if (waterHeightMap_)
                SendResourcePatch(connection, "water_heightmap", waterHeightMap_, px, pz);

            // Future: send other resource maps here (moisture, vegetation, etc.)

            it->second_.sentPatches.Insert(key);
            ++sent;
        }
    }

    if (sent > 0)
        LogMessage("Streamed " + String(sent) + " resource patch(es) to " + connection->ToString() +
                   " around (" + String(centerX) + "," + String(centerZ) + ")");
}

void AuthServer::HandlePatchPosition(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int patchX = msg.ReadI32();
    int patchZ = msg.ReadI32();

    it->second_.lastPatchPos = IntVector2(patchX, patchZ);
    SendPatchNeighbourhood(connection, patchX, patchZ);
}

void AuthServer::BroadcastAffectedPatch(int editPatchX, int editPatchZ, const String& resourceID, Image* resourceMap)
{
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (!it->second_.authenticated)
            continue;

        IntVector2 clientPatch = it->second_.lastPatchPos;
        // Check if editPatch falls within client's 3×3 window
        if (Abs(editPatchX - clientPatch.x_) <= 1 && Abs(editPatchZ - clientPatch.y_) <= 1)
            SendResourcePatch(it->first_, resourceID, resourceMap, editPatchX, editPatchZ);
    }
}

void AuthServer::HandleWaterEdit(Connection* connection, MemoryBuffer& msg)
{
    // Verify sender is authenticated
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
    {
        LogMessage("Water edit rejected: unauthenticated client");
        return;
    }

    // Read brush parameters from the message
    float worldX = msg.ReadFloat();
    float worldZ = msg.ReadFloat();
    float radius = msg.ReadFloat();
    float strength = msg.ReadFloat();
    bool raise = msg.ReadBool();

    if (!waterHeightMap_)
        return;

    int width = waterHeightMap_->GetWidth();
    int height = waterHeightMap_->GetHeight();

    // Convert world coords to pixel coords
    float terrainSpacingX = 2.0f;
    float terrainSpacingZ = 2.0f;
    float halfSize = (width - 1) * terrainSpacingX * 0.5f;

    int centerPX = (int)((worldX + halfSize) / terrainSpacingX);
    int centerPZ = (int)((worldZ + halfSize) / terrainSpacingZ);
    int pixelRadius = (int)(radius / terrainSpacingX);

    // Track which patches are touched by this edit
    HashSet<unsigned long long> touchedPatches;

    int modified = 0;
    for (int pz = Max(0, centerPZ - pixelRadius); pz <= Min(height - 1, centerPZ + pixelRadius); ++pz)
    {
        for (int px = Max(0, centerPX - pixelRadius); px <= Min(width - 1, centerPX + pixelRadius); ++px)
        {
            float dx = (float)(px - centerPX);
            float dz = (float)(pz - centerPZ);
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > pixelRadius)
                continue;

            float falloff = 1.0f - (dist / (float)pixelRadius);
            float delta = strength * falloff;

            Color c = waterHeightMap_->GetPixel(px, pz);
            float val = c.r_;
            if (raise)
                val = Min(val + delta, 1.0f);
            else
                val = Max(val - delta, 0.0f);
            waterHeightMap_->SetPixel(px, pz, Color(val, val, val));
            ++modified;

            // Record which patch this pixel belongs to
            int patchPX = (px - width / 2) / PATCH_PIXELS;
            int patchPZ = (pz - height / 2) / PATCH_PIXELS;
            if (px < width / 2) patchPX--;  // negative side rounds down
            if (pz < height / 2) patchPZ--;
            touchedPatches.Insert(PatchKey(patchPX, patchPZ));
        }
    }

    if (modified == 0)
        return;

    // Broadcast affected patches to all clients who can see them
    for (auto pit = touchedPatches.Begin(); pit != touchedPatches.End(); ++pit)
    {
        int tpx = (int)(*pit >> 32);
        int tpz = (int)(*pit & 0xFFFFFFFF);
        BroadcastAffectedPatch(tpx, tpz, "water_heightmap", waterHeightMap_);
    }
}

// ─── AI TUNING ─────────────────────────────────────────────────────────────

void AuthServer::LoadAITuning()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !gameDB_->IsOpen())
        return;

    sqlite3* db = gameDB_->GetHandle();
    if (!db)
        return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT key, value FROM ai_tuning", -1, &stmt, nullptr) != SQLITE_OK)
    {
        LogMessage("[AI Tuning] Failed to query ai_tuning table");
        return;
    }

    aiTuning_.Clear();
    unsigned count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        float value = static_cast<float>(sqlite3_column_double(stmt, 1));
        if (key)
        {
            aiTuning_[String(key)] = value;
            ++count;
        }
    }
    sqlite3_finalize(stmt);

    LogMessage("[AI Tuning] Loaded " + String(count) + " tuning parameters from GameDB");
#endif
}

void AuthServer::SendTuningData(Connection* connection)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !gameDB_->IsOpen())
        return;

    sqlite3* db = gameDB_->GetHandle();
    if (!db)
        return;

    // Query full tuning data including UI metadata
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT key, value, label, category, min_val, max_val FROM ai_tuning",
        -1, &stmt, nullptr) != SQLITE_OK)
        return;

    // Collect rows first (need count for header)
    struct TuningRow { String key; float value; String label; String category; float minVal; float maxVal; };
    Vector<TuningRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        TuningRow r;
        const char* k = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.key = k ? String(k) : String::EMPTY;
        r.value = static_cast<float>(sqlite3_column_double(stmt, 1));
        const char* l = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.label = l ? String(l) : r.key;
        const char* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.category = c ? String(c) : "general";
        r.minVal = static_cast<float>(sqlite3_column_double(stmt, 4));
        r.maxVal = static_cast<float>(sqlite3_column_double(stmt, 5));
        rows.Push(r);
    }
    sqlite3_finalize(stmt);

    VectorBuffer buf;
    buf.WriteU16(static_cast<unsigned short>(rows.Size()));
    for (const auto& r : rows)
    {
        buf.WriteString(r.key);
        buf.WriteFloat(r.value);
        buf.WriteString(r.label);
        buf.WriteString(r.category);
        buf.WriteFloat(r.minVal);
        buf.WriteFloat(r.maxVal);
    }
    connection->SendMessage(MSG_TUNING_DATA, true, true, buf);
#endif
}

void AuthServer::HandleTuningRequest(Connection* connection)
{
    auto sIt = sessions_.Find(connection);
    if (sIt == sessions_.End() || sIt->second_.adminLevel < 1)
    {
        LogMessage("REJECTED: tuning request from non-admin " + connection->ToString());
        return;
    }
    LogMessage("[AI Tuning] Sending tuning data to " + sIt->second_.username);
    SendTuningData(connection);
}

void AuthServer::HandleTuningUpdate(Connection* connection, MemoryBuffer& msg)
{
    auto sIt = sessions_.Find(connection);
    if (sIt == sessions_.End() || sIt->second_.adminLevel < 1)
    {
        LogMessage("REJECTED: tuning update from non-admin " + connection->ToString());
        return;
    }

    String key = msg.ReadString();
    float value = msg.ReadFloat();

    // Update runtime HashMap
    aiTuning_[key] = value;

    // Persist to DB
#ifdef URHO3D_DATABASE_SQLITE
    if (gameDB_ && gameDB_->IsOpen())
    {
        sqlite3* db = gameDB_->GetHandle();
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "UPDATE ai_tuning SET value=? WHERE key=?", -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_double(stmt, 1, static_cast<double>(value));
            sqlite3_bind_text(stmt, 2, key.CString(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
#endif

    LogMessage("[AI Tuning] " + sIt->second_.username + " set " + key + " = " + String(value));

    // Broadcast updated tuning to all admin clients
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.adminLevel > 0)
            SendTuningData(it->first_);
    }
}

// ─── SURVIVAL PRESSURE ──────────────────────────────────────────────────────

void AuthServer::InitGameDB()
{
#ifdef URHO3D_DATABASE_SQLITE
    gameDB_ = new GameDB(context_);

    // Build path relative to executable
    auto* fileSystem = GetSubsystem<FileSystem>();
    String dbPath = fileSystem->GetProgramDir() + "Data/GameDB/game_rules.db";
    String schemaPath = fileSystem->GetProgramDir() + "Data/GameDB/schema.sql";
    String seedPath = fileSystem->GetProgramDir() + "Data/GameDB/seed_data.sql";
    String survSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/survival_schema.sql";
    String survSeedPath = fileSystem->GetProgramDir() + "Data/GameDB/survival_seed.sql";

    if (!gameDB_->Open(dbPath))
    {
        LogMessage("[GameDB] FAILED to open " + dbPath);
        return;
    }

    // Apply schemas and seed data (IF NOT EXISTS / INSERT OR IGNORE — safe to re-apply)
    gameDB_->ExecuteFile(schemaPath);
    gameDB_->ExecuteFile(seedPath);
    gameDB_->ExecuteFile(survSchemaPath);
    gameDB_->ExecuteFile(survSeedPath);

    String invSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/inventory_schema.sql";
    gameDB_->ExecuteFile(invSchemaPath);

    // Building system schema and seed data
    String buildSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/buildings_schema.sql";
    String buildSeedPath = fileSystem->GetProgramDir() + "Data/GameDB/buildings_seed.sql";
    gameDB_->ExecuteFile(buildSchemaPath);
    gameDB_->ExecuteFile(buildSeedPath);

    // Skill system schema
    String skillsSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/skills_schema.sql";
    gameDB_->ExecuteFile(skillsSchemaPath);
    gameDB_->CacheSkillRules();

    // Technique discovery chains (depends on skills)
    String techSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/technique_schema.sql";
    gameDB_->ExecuteFile(techSchemaPath);
    gameDB_->CacheTechniqueDiscovery();

    // Population dynamics schema
    String popSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/population_schema.sql";
    gameDB_->ExecuteFile(popSchemaPath);

    // Economic doctrine schema (resource types, breeding rules, trade values, constants)
    String econPath = fileSystem->GetProgramDir() + "Data/GameDB/economic_doctrine.sql";
    gameDB_->ExecuteFile(econPath);

    // Trade system schema
    String tradePath = fileSystem->GetProgramDir() + "Data/GameDB/trade_schema.sql";
    gameDB_->ExecuteFile(tradePath);

    // AI tuning schema (runtime-configurable NPC parameters)
    String aiTuningPath = fileSystem->GetProgramDir() + "Data/GameDB/ai_tuning_schema.sql";
    gameDB_->ExecuteFile(aiTuningPath);
    LoadAITuning();

    // Phenomena rules schema (Plan 10 — world phenomena generator)
    String phenomenaPath = fileSystem->GetProgramDir() + "Data/GameDB/phenomena_rules.sql";
    gameDB_->ExecuteFile(phenomenaPath);
    CachePhenomenaRules();

    // Profession schedule system
    String profPath = fileSystem->GetProgramDir() + "Data/GameDB/profession_schema.sql";
    gameDB_->ExecuteFile(profPath);
    LoadProfessionSchedules();

    // Apply balance patches from Data/GameDB/patches/ (alphabetical order)
    {
        String patchDir = fileSystem->GetProgramDir() + "Data/GameDB/patches";
        if (fileSystem->DirExists(patchDir))
        {
            Vector<String> patchFiles;
            fileSystem->ScanDir(patchFiles, patchDir, "*.sql", SCAN_FILES, false);
            Sort(patchFiles.Begin(), patchFiles.End());
            for (unsigned i = 0; i < patchFiles.Size(); ++i)
            {
                String patchPath = patchDir + "/" + patchFiles[i];
                if (gameDB_->ApplyPatch(patchPath))
                    LogMessage("[GameDB] Applied balance patch: " + patchFiles[i]);
                else
                    LogMessage("[GameDB] WARNING: failed to apply patch: " + patchFiles[i]);
            }
            if (patchFiles.Size() > 0)
                LogMessage("[GameDB] " + String(patchFiles.Size()) + " balance patch(es) applied");
        }
    }

    // Initialize population manager
    populationManager_ = new PopulationManager(context_);
    populationManager_->Initialize(gameDB_);
    if (populationManager_->IsReady())
        LogMessage("[PopulationManager] Initialized");
    else
        LogMessage("[PopulationManager] WARNING: failed to initialize");

    // Initialize NPC priority compute (GPU-accelerated task selection)
    npcPriorityCompute_ = new NPCPriorityCompute(context_);
    npcPriorityCompute_->Initialize();
    SetupDefaultPriorityCurves();

    // Cache building type info for fast lookup during ticks
    CacheBuildingTypes();

    // Cache survival rules
    if (gameDB_->GetHungerRules(hungerRules_) && gameDB_->GetThirstRules(thirstRules_))
    {
        survivalRulesLoaded_ = true;
        LogMessage("[GameDB] Survival rules loaded — hunger drain " +
            String(hungerRules_.drainPerDay) + "/day, thirst drain " +
            String(thirstRules_.drainPerDay) + "/day");
    }
    else
        LogMessage("[GameDB] WARNING: survival rules not found in database");

    // Cache warmth rules
    if (gameDB_->GetWarmthRules(warmthRules_))
    {
        warmthRulesLoaded_ = true;
        LogMessage("[GameDB] Warmth rules loaded — night_multiplier=" +
            String(warmthRules_.nightMultiplier) + ", fire_warmth=" +
            String(warmthRules_.fireWarmth));
    }
    else
        LogMessage("[GameDB] WARNING: warmth rules not found in database");

    // Cache death/respawn rules
    if (gameDB_->GetDeathRules(deathRules_))
    {
        deathRulesLoaded_ = true;
        LogMessage("[GameDB] Death rules loaded — respawn " + String(deathRules_.respawnDelay) +
            "s, hp=" + String(deathRules_.hpOnRespawn) +
            ", dropInventory=" + String(deathRules_.dropInventory ? "yes" : "no"));
    }
    else
        LogMessage("[GameDB] WARNING: death rules not found in database");

    // Cache inventory rules
    if (gameDB_->GetInventoryRules(inventoryRules_))
    {
        inventoryRulesLoaded_ = true;
        LogMessage("[GameDB] Inventory rules loaded — " + String(inventoryRules_.baseSlots) +
            " slots, max weight " + String(inventoryRules_.maxWeight) + " kg");
    }
    else
        LogMessage("[GameDB] WARNING: inventory rules not found in database");

    LogMessage("[GameDB] Ready");
#endif
}

void AuthServer::InitWorldDB()
{
#ifdef URHO3D_DATABASE_SQLITE
    worldDB_ = new WorldDB(context_);

    auto* fileSystem = GetSubsystem<FileSystem>();
    String worldDbPath = fileSystem->GetProgramDir() + "Data/GameDB/game_world.db";
    String worldSchemaPath = fileSystem->GetProgramDir() + "Data/GameDB/world_schema.sql";

    if (!worldDB_->Open(worldDbPath))
    {
        LogMessage("[WorldDB] FAILED to open " + worldDbPath);
        return;
    }

    if (!worldDB_->EnsureSchema(worldSchemaPath))
    {
        LogMessage("[WorldDB] FAILED to apply schema");
        return;
    }

    // Backup pristine terrain maps on first run (before any edits)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        auto* heightMap = cache->GetResource<Image>("Textures/HeightMap.png");
        if (heightMap)
            worldDB_->BackupMap("HeightMap.png", heightMap);

        auto* waterMap = cache->GetResource<Image>("Textures/WaterHeightMap.png");
        if (waterMap)
            worldDB_->BackupMap("WaterHeightMap.png", waterMap);
    }

    // Load game time if a saved state exists
    // Game time is UTC — currentGameDay_ is Unix days since epoch.
    // Initialize from the real clock so the economy tick doesn't re-fire
    // the entire history on first startup.
    currentGameDay_ = (int)(time(nullptr) / 86400);
    lastEconomyDay_ = currentGameDay_;

    GameTimeState gts;
    if (worldDB_->LoadGameTime(gts))
        LogMessage("[WorldDB] Loaded saved state (day field " + String((int)gts.gameDay) +
                   ", current UTC day " + String(currentGameDay_) + ")");
    else
        LogMessage("[WorldDB] Fresh world — UTC day " + String(currentGameDay_));

    // Verify WAL mode is active
    {
        sqlite3_stmt* stmt = nullptr;
        // Use Execute to verify — the PRAGMA was set in Open()
        LogMessage("[WorldDB] WAL mode enabled, synchronous=NORMAL");
    }

    // Restore fire pits from last shutdown
    {
        Vector<FirePitDBInfo> saved = worldDB_->LoadFirePits();
        for (unsigned i = 0; i < saved.Size(); ++i)
        {
            const FirePitDBInfo& p = saved[i];
            ServerCampfire cf;
            cf.position       = p.position;
            cf.fuelSeconds    = p.fuel;
            cf.maxFuelSeconds = p.maxFuel;
            cf.burnRate       = p.burnRate;
            cf.wetness        = p.wetness;
            cf.state          = static_cast<FirePitState>(p.state);
            cf.regionId       = p.regionId;
            serverCampfires_[p.pitId] = cf;
            if (p.pitId >= nextCampfireId_)
                nextCampfireId_ = p.pitId + 1;
        }
        if (!saved.Empty())
            LogMessage("[WorldDB] Restored " + String(saved.Size()) + " fire pits");
    }

    // Retroactively claim settlement patches for existing campfires
    MigrateExistingSettlementPatches();

    // One-time water body analysis: flood-fill heightmap, cache fish spawn points
    DiscoverWaterBodies();

    LogMessage("[WorldDB] Ready — " + worldDbPath);
#else
    LogMessage("[WorldDB] SQLite not enabled — world persistence disabled");
#endif
}

void AuthServer::WorldCheckpointTick(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    worldCheckpointTimer_ += dt;
    if (worldCheckpointTimer_ < WORLD_CHECKPOINT_INTERVAL)
        return;

    worldCheckpointTimer_ = 0.0f;
    worldDB_->Checkpoint();
#endif
}

#ifdef URHO3D_DATABASE_SQLITE

void AuthServer::SurvivalTick(float dt)
{
    if (!survivalRulesLoaded_)
        return;

    survivalTickTimer_ -= dt;
    if (survivalTickTimer_ > 0.0f)
        return;
    survivalTickTimer_ = SURVIVAL_TICK_INTERVAL;

    // Game time is UTC — real seconds, no gameTimeScale_ multiplier.
    float gameDayFraction = SURVIVAL_TICK_INTERVAL / 86400.0f;

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        ClientSession& s = it->second_;
        if (!s.authenticated)
            continue;

        // Respawn tick — dead players count down to revival
        if (!s.alive)
        {
            if (s.respawnTimer > 0.0f)
            {
                s.respawnTimer -= SURVIVAL_TICK_INTERVAL;
                if (s.respawnTimer <= 0.0f)
                {
                    // Revive with DB-driven values (or hardcoded fallbacks)
                    s.alive = true;
                    s.respawnTimer = 0.0f;
                    s.hp = deathRulesLoaded_ ? deathRules_.hpOnRespawn : 10;
                    s.hunger = (float)(deathRulesLoaded_ ? deathRules_.hungerOnRespawn : 50);
                    s.thirst = (float)(deathRulesLoaded_ ? deathRules_.thirstOnRespawn : 50);
                    s.stamina = (float)(deathRulesLoaded_ ? deathRules_.staminaOnRespawn : 50);
                    s.speedMult = 1.0f;
                    SendVitalUpdate(it->first_, s, true);
                    LogMessage(s.username + " respawned (hp=" + String(s.hp) +
                        " hunger=" + String((int)s.hunger) +
                        " thirst=" + String((int)s.thirst) + ")");
                }
            }
            continue;
        }

        // Hunger drain
        float hungerDrain = hungerRules_.drainPerDay * gameDayFraction;
        s.hunger = Max(0.0f, s.hunger - hungerDrain);

        // Starvation damage
        if (s.hunger <= 0.0f)
            s.hp = Max(0, s.hp - (int)(hungerRules_.starveHpDay * gameDayFraction + 0.5f));

        // Thirst drain (faster than hunger)
        float thirstDrain = thirstRules_.drainPerDay * gameDayFraction;
        s.thirst = Max(0.0f, s.thirst - thirstDrain);

        // Dehydration damage (faster than starvation)
        if (s.thirst <= 0.0f)
            s.hp = Max(0, s.hp - (int)(thirstRules_.dehydrateHpDay * gameDayFraction + 0.5f));

        // Shelter warmth contribution — use authoritative avatar position
        Vector3 playerPos;
        auto nodeIt = serverObjects_.Find(it->first_);
        if (nodeIt != serverObjects_.End() && nodeIt->second_)
            playerPos = nodeIt->second_->GetWorldPosition();
        float shelterWarmth = GetShelterWarmth(playerPos.x_, playerPos.y_, playerPos.z_);

        // Clothing warmth — sum equipped items (same as NPC path in UpdateCreatureVitals)
        float clothingWarmth = 0.0f;
#ifdef URHO3D_DATABASE_SQLITE
        if (worldDB_ && gameDB_)
        {
            int playerId = GetPlayerId(s.username);
            if (playerId >= 0)
            {
                static const char* warmSlots[] = {"body", "back", "feet", "head"};
                for (int ws = 0; ws < 4; ++ws)
                {
                    int itemId = worldDB_->GetEquippedItem(playerId, warmSlots[ws]);
                    if (itemId > 0)
                        clothingWarmth += gameDB_->GetClothingWarmth(itemId);
                }
            }
        }
#endif
        s.warmth = weatherTemperature_ + shelterWarmth + clothingWarmth;

        // Night warmth drain — same formula as NPC path, DB-driven multiplier
        {
            float darkness = GetDarkness();
            float nightDrain = warmthRulesLoaded_ ? warmthRules_.nightMultiplier * 0.05f : 0.40f;
            s.warmth -= darkness * nightDrain * SURVIVAL_TICK_INTERVAL;
        }

        // Speed penalty
        if (s.hunger < (float)hungerRules_.criticalThreshold || s.thirst < (float)thirstRules_.criticalThreshold)
            s.speedMult = 0.75f;
        else if (s.hunger < (float)hungerRules_.lowThreshold || s.thirst < (float)thirstRules_.lowThreshold)
            s.speedMult = 0.9f;
        else
            s.speedMult = 1.0f;

        // Death check — use DB-driven respawn delay
        if (s.hp <= 0)
        {
            s.alive = false;
            s.hp = 0;
            s.respawnTimer = deathRulesLoaded_ ? deathRules_.respawnDelay : 5.0f;
            SendVitalUpdate(it->first_, s, true);
            LogMessage(s.username + " died — respawning in " +
                String(s.respawnTimer) + "s");
            continue;
        }

        // Send-on-change: only send if a value crossed a display threshold
        SendVitalUpdate(it->first_, s);
    }
}

void AuthServer::SendVitalUpdate(Connection* connection, ClientSession& s, bool force)
{
    // Quantize to integers for comparison (client displays integers anyway)
    int hp = s.hp;
    int hunger = (int)s.hunger;
    int thirst = (int)s.thirst;
    int stamina = (int)s.stamina;

    if (!force)
    {
        // Only send if a value changed by at least 1 integer unit
        if (hp == s.sentHp && hunger == s.sentHunger &&
            thirst == s.sentThirst && stamina == s.sentStamina)
            return;
    }

    s.sentHp = hp;
    s.sentHunger = hunger;
    s.sentThirst = thirst;
    s.sentStamina = stamina;

    VectorBuffer buf;
    buf.WriteI32(hp);
    buf.WriteI32(s.maxHp);
    buf.WriteI32(hunger);
    buf.WriteI32(thirst);
    buf.WriteI32(stamina);
    buf.WriteFloat(s.warmth);
    buf.WriteBool(s.alive);
    buf.WriteFloat(s.speedMult);
    connection->SendMessage(MSG_VITAL_UPDATE, true, true, buf);
}

void AuthServer::HandleEat(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int itemId = msg.ReadI32();
    ClientSession& s = it->second_;

    if (!gameDB_)
        return;

    // Get food properties
    FoodInfo food;
    if (!gameDB_->GetFoodProperties(itemId, food))
    {
        LogMessage(s.username + " tried to eat non-food item " + String(itemId));
        return;
    }

    // Validate and consume from inventory
    int playerId = GetPlayerId(s.username);
    if (playerId >= 0 && worldDB_ && worldDB_->GetItemCount(playerId, itemId) < 1)
    {
        LogMessage(s.username + " tried to eat item " + String(itemId) + " but doesn't have it");
        return;
    }
    if (playerId >= 0 && worldDB_)
        worldDB_->RemoveItemFromInventory(playerId, itemId, 1);

    // Apply food effects — cooking skill improves nourishment
    float hungerRestore = (float)food.hunger;
    if (gameDB_ && playerId >= 0)
    {
        int cookLevel = gameDB_->GetSkillLevel(playerId, SKILL_COOKING);
        hungerRestore *= (1.0f + 0.05f * cookLevel);
    }
    s.hunger = Min(100.0f, s.hunger + hungerRestore);
    s.hp = Min(s.maxHp, s.hp + food.health);

    // Poison roll
    if (food.poisonChance > 0.0f)
    {
        float roll = (float)(rand() % 1000) / 1000.0f;
        if (roll < food.poisonChance)
        {
            s.hp = Max(1, s.hp - 5);  // poison damage
            LogMessage(s.username + " got food poisoning");
        }
    }

    if (gameDB_ && playerId >= 0)
        gameDB_->AwardXP(playerId, "cook");

    LogMessage(s.username + " ate item " + String(itemId) +
        " (hunger +" + String((int)hungerRestore) + ", hp +" + String(food.health) + ")");

    // Force send updated vitals + inventory delta
    SendVitalUpdate(connection, s, true);
    SendInventoryDelta(connection, itemId, 1, false);
}

void AuthServer::HandleDrink(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    String sourceType = msg.ReadString();
    ClientSession& s = it->second_;

    if (!gameDB_)
        return;

    // Get water source info
    WaterSourceInfo source;
    if (!gameDB_->GetWaterSource(sourceType, source))
    {
        LogMessage(s.username + " tried to drink from unknown source: " + sourceType);
        return;
    }

    // Proximity check: player must be near water (river/lake) or a water container
    int playerId = GetPlayerId(s.username);
    if (sourceType == "river" || sourceType == "lake" || sourceType == "stream")
    {
        auto avIt = serverObjects_.Find(connection);
        if (avIt != serverObjects_.End() && avIt->second_)
        {
            Vector3 playerPos = avIt->second_->GetWorldPosition();
            if (playerPos.y_ > AI_WATER_LEVEL + 2.0f)
            {
                LogMessage(s.username + " tried to drink from " + sourceType + " but not near water");
                return;
            }
        }
    }

    // Well source: deduct from nearest well's water reserve
    if (sourceType == "well")
    {
        auto avIt = serverObjects_.Find(connection);
        if (avIt != serverObjects_.End() && avIt->second_)
        {
            Vector3 playerPos = avIt->second_->GetWorldPosition();
            if (!TryDrawFromNearbyWell(playerPos.x_, playerPos.z_, 6.0f))
            {
                LogMessage(s.username + " tried to drink from well but no water available");
                return;
            }
        }
    }

    // Required item check (e.g. clay pot for well water)
    if (source.requires > 0 && playerId > 0 && worldDB_)
    {
        if (worldDB_->GetItemCount(playerId, source.requires) <= 0)
        {
            LogMessage(s.username + " needs item " + String(source.requires) + " to drink from " + sourceType);
            return;
        }
    }

    // Apply thirst restoration
    s.thirst = Min(100.0f, s.thirst + (float)source.thirstRestore);

    // Disease roll
    if (source.diseaseChance > 0.0f)
    {
        float roll = (float)(rand() % 1000) / 1000.0f;
        if (roll < source.diseaseChance)
        {
            s.hp = Max(1, s.hp - 3);
            LogMessage(s.username + " got waterborne illness from " + sourceType);
        }
    }

    LogMessage(s.username + " drank from " + sourceType +
        " (thirst +" + String(source.thirstRestore) + ")");

    SendVitalUpdate(connection, s, true);
}

int AuthServer::GetPlayerId(const String& username)
{
    if (!gameDB_ || !gameDB_->IsOpen())
        return -1;

    // Use username hash as player_id (stable, deterministic)
    return (int)(username.ToHash() & 0x7FFFFFFF);
}

// Cross-DB inventory helpers — bridge WorldDB (state) and GameDB (rules)

float AuthServer::ComputePlayerWeight(int playerId)
{
    if (!worldDB_ || !gameDB_) return 0.0f;

    Vector<InventorySlot> items = worldDB_->GetPlayerInventory(playerId);
    float total = 0.0f;
    for (unsigned i = 0; i < items.Size(); ++i)
    {
        ItemInfo info;
        if (gameDB_->GetItem(items[i].itemId, info))
            total += info.weight * items[i].quantity;
    }
    return total;
}

void AuthServer::ComputeContainerBonus(int playerId, int& extraSlots, float& extraWeight)
{
    extraSlots = 0;
    extraWeight = 0.0f;
    if (!gameDB_ || !worldDB_)
        return;

    sqlite3* db = gameDB_->GetHandle();
    if (!db)
        return;

    // Backpack/pouch/basket/cargo-net all grant capacity when equipped in one of
    // these carrying slots. container_bonus holds the per-item extra_slots/weight.
    static const char* containerSlots[] = {"back", "hand", "offhand"};
    for (int cs = 0; cs < 3; ++cs)
    {
        int equipped = worldDB_->GetEquippedItem(playerId, containerSlots[cs]);
        if (equipped <= 0)
            continue;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db,
            "SELECT extra_slots, extra_weight FROM container_bonus WHERE item_id = ?",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, equipped);
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                extraSlots += sqlite3_column_int(stmt, 0);
                extraWeight += (float)sqlite3_column_double(stmt, 1);
            }
            sqlite3_finalize(stmt);
        }
    }
}

float AuthServer::ComputeMaxWeight(int playerId)
{
    float maxWeight = inventoryRulesLoaded_ ? inventoryRules_.maxWeightAbsolute : 60.0f;
    int extraSlots = 0;
    float extraWeight = 0.0f;
    ComputeContainerBonus(playerId, extraSlots, extraWeight);
    return maxWeight + extraWeight;
}

int AuthServer::ComputeAvailableSlots(int playerId)
{
    if (!worldDB_) return 0;

    int maxSlots = inventoryRulesLoaded_ ? inventoryRules_.baseSlots : 10;

    // Container bonus from equipped back/hand/offhand items (Leather Bag,
    // Basket, Cargo Net, Hide Pouch, Leather Backpack).
    int extraSlots = 0;
    float extraWeight = 0.0f;
    ComputeContainerBonus(playerId, extraSlots, extraWeight);
    maxSlots += extraSlots;

    int occupied = worldDB_->GetOccupiedBagSlots(playerId);
    return maxSlots - occupied;
}

bool AuthServer::AddItemToWorldInventory(int playerId, int itemId, int qty)
{
    if (!worldDB_ || !gameDB_) return false;

    ItemInfo item;
    if (!gameDB_->GetItem(itemId, item))
        return false;

    float maxWeight = ComputeMaxWeight(playerId);
    float currentWeight = ComputePlayerWeight(playerId);
    float addedWeight = item.weight * qty;
    if (currentWeight + addedWeight > maxWeight)
        return false;

    int avail = ComputeAvailableSlots(playerId);
    return worldDB_->AddItemToInventory(playerId, itemId, qty,
        item.stackMax, item.weight, item.durability, maxWeight, avail);
}

bool AuthServer::UnequipFromWorld(int playerId, const String& slot, int& outItemId)
{
    if (!worldDB_ || !gameDB_) return false;

    int equippedId = worldDB_->GetEquippedItem(playerId, slot);
    if (equippedId == 0)
    {
        outItemId = 0;
        return false;
    }

    ItemInfo item;
    if (!gameDB_->GetItem(equippedId, item))
    {
        // Fallback: item not in rules DB, use safe defaults
        item.stackMax = 1;
        item.weight = 0.0f;
        item.durability = -1;
    }

    float maxWeight = ComputeMaxWeight(playerId);
    int avail = ComputeAvailableSlots(playerId);
    return worldDB_->UnequipItem(playerId, slot, outItemId,
        item.stackMax, item.weight, item.durability, maxWeight, avail);
}

void AuthServer::HandlePickup(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    unsigned nodeId = msg.ReadU32();
    ClientSession& s = it->second_;

    if (!worldDB_ || !scene_)
        return;

    // Find the world node
    Node* itemNode = scene_->GetNode(nodeId);
    if (!itemNode)
    {
        LogMessage(s.username + " tried to pick up non-existent node " + String(nodeId));
        return;
    }

    // Get item_id from node variable (set when item was spawned)
    int itemId = itemNode->GetVar("ItemID").GetI32();
    int qty = itemNode->GetVar("ItemQty").GetI32();
    if (itemId <= 0) itemId = 1;
    if (qty <= 0) qty = 1;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    // Try to add to inventory (validates weight + slots via cross-DB helper)
    if (!AddItemToWorldInventory(playerId, itemId, qty))
    {
        LogMessage(s.username + " can't pick up item " + String(itemId) + " — inventory full or too heavy");
        return;
    }

    // Remove world node
    itemNode->Remove();

    // Economic doctrine: deduct from regional resource pool with scarcity check
    if (gameDB_ && populationManager_)
    {
        Vector3 pos = itemNode->GetWorldPosition();
        int regionId = populationManager_->FindRegion(pos.x_, pos.z_);
        if (regionId >= 0)
        {
            // Find which resource type yields this item
            Vector<ResourceTypeInfo> resTypes = gameDB_->GetAllResourceTypes();
            for (unsigned r = 0; r < resTypes.Size(); ++r)
            {
                if (resTypes[r].itemId == itemId)
                {
                    float scarcity = gameDB_->GetScarcityModifier(regionId, resTypes[r].id);
                    ExtractionResult result = gameDB_->ExtractResource(playerId, regionId, resTypes[r].id, currentGameDay_);
                    if (result.success)
                    {
                        RegionResourceInfo pool;
                        if (gameDB_->GetRegionResource(regionId, resTypes[r].id, pool))
                            URHO3D_LOGDEBUGF("[Economy] Pickup %s: scarcity=%.2f remaining=%.0f/%.0f",
                                resTypes[r].name.CString(), scarcity, pool.currentAmount, pool.maxAmount);
                    }
                    break;
                }
            }
        }
    }

    LogMessage(s.username + " picked up item " + String(itemId) + " x" + String(qty));

    // Send inventory delta to this client
    SendInventoryDelta(connection, itemId, qty, true);
}

// ============================================================================
// Combat — Phase 1: server rolls dice, client owns animal HP
// ============================================================================

void AuthServer::HandleAttack(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    ClientSession& s = it->second_;
    unsigned targetSpawnId = msg.ReadU32();  // server-assigned spawnId (client sends this now)
    int weaponItemId       = msg.ReadI32();  // 0 = bare hands
    // Client also sends creatureId for lazy-register fallback.
    int payloadCreatureId = msg.IsEof() ? 0 : msg.ReadI32();

    // Lazy-init combat resolver
    if (!combatResolver_)
        combatResolver_ = new CombatResolver(context_);
        combatResolver_->SetExternalRNG(QuantumDiceRollBridge);

    // Get weapon stats from GameDB (bare hands fallback if not found)
    int attackMod  = 0;
    int baseDamage = 1;
    int damageVar  = 2;
    float range    = 1.5f;

    if (gameDB_ && weaponItemId > 0)
    {
        CombatInfo ci;
        if (gameDB_->GetCombatStats(weaponItemId, ci))
        {
            attackMod  = ci.attackMod;
            baseDamage = ci.damage;
            damageVar  = ci.damageVar;
            range      = ci.range;
        }

        // Alloy properties: tier 7+ weapons get alloy bonuses from their recipe material
        ItemInfo weaponInfo;
        if (gameDB_->GetItem(weaponItemId, weaponInfo) && weaponInfo.tier >= 7)
        {
            // Check each alloy type — apply the first matching one for this tier
            static const int alloyIds[] = {851, 852, 853, 854, 855, 860};
            for (int aid : alloyIds)
            {
                AlloyPropertiesInfo alloy;
                if (gameDB_->GetAlloyProperties(aid, alloy))
                {
                    // Match tier to alloy: tier 7 = single alloy, tier 8 = hybrid
                    if ((weaponInfo.tier == 7 && aid != 860) || (weaponInfo.tier == 8 && aid == 860))
                    {
                        baseDamage += alloy.damageMod;
                        break;
                    }
                }
            }
        }
    }

    // Ranged vs melee — weapons with range > 3m are ranged
    bool isRanged = (range > 3.0f);

    // Ranged weapons consume 1 arrow (item 202) per shot
    if (isRanged && worldDB_)
    {
        int playerId = GetPlayerId(s.username);
        if (playerId >= 0)
        {
            if (worldDB_->GetItemCount(playerId, 202) < 1)
            {
                LogMessage(s.username + " tried to shoot with no arrows");
                return;
            }
            worldDB_->RemoveItemFromInventory(playerId, 202, 1);
            SendInventoryUpdate(connection, playerId);
        }
    }

    // Skill bonus: ranged or melee depending on weapon type
    int attackerSkillLevel = 0;
    if (gameDB_)
    {
        int playerId = GetPlayerId(s.username);
        if (playerId >= 0)
        {
            attackerSkillLevel = gameDB_->GetSkillLevel(playerId, isRanged ? SKILL_RANGED : SKILL_MELEE);
            attackMod += attackerSkillLevel / 2;  // +1 per 2 levels (level 10 = +5)
        }
    }

    // Combat Phase 2: server-authoritative creature HP tracking.
    // Lazy-register on first hit against an unknown nodeId.
    auto stateIt = creatureStates_.Find(targetSpawnId);
    if (stateIt == creatureStates_.End())
    {
        ServerCreatureState fresh;
        fresh.creatureId = payloadCreatureId;
        if (!LoadCreatureCombat(payloadCreatureId, fresh))
        {
            // Either the client didn't send a creatureId or GameDB doesn't know
            // the species — fall back to defaults so the attack still resolves.
            // Loud log so this stands out in dev.
            LogMessage("[Combat] WARN: HandleAttack target spawnId=" + String(targetSpawnId) +
                       " creatureId=" + String(payloadCreatureId) +
                       " not in GameDB — using fallback hp=10 def=10");
            fresh.hp = fresh.maxHp = 10;
            fresh.defense = 10;
            fresh.damage = 1;
            fresh.damageVar = 2;
        }
        // Anchor a region from the attacker's avatar position so RecordKill on
        // death can charge the right population. -1 means PopulationManager will
        // skip the kill, which is acceptable for unknown-region edge cases.
        auto avatarIt = serverObjects_.Find(connection);
        if (avatarIt != serverObjects_.End() && avatarIt->second_ && populationManager_)
        {
            Vector3 ap = avatarIt->second_->GetWorldPosition();
            fresh.regionId = populationManager_->FindRegion(ap.x_, ap.z_);
            fresh.position = ap;  // best proxy until Phase 2.5 carries real positions
        }
        creatureStates_[targetSpawnId] = fresh;
        stateIt = creatureStates_.Find(targetSpawnId);
        LogMessage("[Combat] Registered creatureState spawnId=" + String(targetSpawnId) +
                   " species=" + fresh.species +
                   " hp=" + String(fresh.hp) + "/" + String(fresh.maxHp) +
                   " def=" + String(fresh.defense) +
                   " region=" + String(fresh.regionId));
    }
    ServerCreatureState& cs = stateIt->second_;

    // Defender's defense skill bonus (NPC creatures with player IDs)
    int defenderDefLevel = GetNPCSkillLevel(targetSpawnId, SKILL_DEFENSE);
    int skillDefenseAdj = defenderDefLevel / 2;  // +1 per 2 levels (level 10 = +5)

    // Combat Phase 3: situational modifiers
    int sitAttackAdj = 0, sitDefenseAdj = 0;
    String sitLog;
    ComputeSituationalMods(connection, targetSpawnId, sitAttackAdj, sitDefenseAdj, sitLog);
    attackMod += sitAttackAdj;
    int effectiveDefense = cs.defense + skillDefenseAdj + sitDefenseAdj;

    // Master Hunter: +3 defence for settlement NPCs
    {
        auto defAi = creatureAI_.Find(targetSpawnId);
        if (defAi != creatureAI_.End() && defAi->second_.isHuman &&
            HasMasterHunter(defAi->second_.campfireId))
            effectiveDefense += 3;
    }

    // Ranged distance penalty: -1 per 5m from attacker to target
    if (isRanged)
    {
        auto avatarIt = serverObjects_.Find(connection);
        if (avatarIt != serverObjects_.End() && avatarIt->second_)
        {
            Vector3 attackerPos = avatarIt->second_->GetWorldPosition();
            float dist = (cs.position - attackerPos).Length();
            int distPenalty = (int)(dist / 5.0f);
            attackMod -= distPenalty;
        }
    }

    AttackResult result = combatResolver_->ResolveAttack(
        attackMod, baseDamage, damageVar, effectiveDefense, cs.hp);

    // Damage scaling from combat skill: 1.0x at level 0, 1.5x at level 10
    if (result.hit && attackerSkillLevel > 0)
        result.damage = (int)(result.damage * (1.0f + 0.05f * attackerSkillLevel));

    if (result.hit)
    {
        cs.hp = Max(0, cs.hp - result.damage);
        CancelTradeIfPossessedDamaged(targetSpawnId);

        // Deduct weapon durability on hit
        if (worldDB_ && weaponItemId > 0)
        {
            int playerId2 = GetPlayerId(s.username);
            if (playerId2 >= 0)
            {
                int remaining = worldDB_->DeductDurability(playerId2, "hand");
                if (remaining == 0)
                {
                    LogMessage("[Item] " + s.username + "'s weapon broke (item " + String(weaponItemId) + ")");
                    SendInventoryUpdate(connection, playerId2);
                }
            }
        }
    }

    LogMessage(String(s.username) + (isRanged ? " shoots" : " attacks") +
        " spawnId " + String(targetSpawnId) +
        " with item " + String(weaponItemId) +
        " — roll " + String(result.roll) +
        " atk=" + String(attackMod) +
        (attackerSkillLevel > 0 ? String(" (") + (isRanged ? "ranged" : "melee") + "+" + String(attackerSkillLevel / 2) + ")" : "") +
        " vs def=" + String(effectiveDefense) +
        (defenderDefLevel > 0 ? " (def+" + String(defenderDefLevel / 2) + ")" : "") +
        (sitLog.Empty() ? "" : sitLog) +
        (result.hit ? " HIT" : " MISS") +
        (result.crit ? " CRIT" : "") +
        " dmg=" + String(result.damage) +
        (attackerSkillLevel > 0 && result.hit ? " (x" + String(1.0f + 0.05f * attackerSkillLevel) + ")" : "") +
        " hp=" + String(cs.hp) + "/" + String(cs.maxHp));

    // Award combat XP — melee or ranged depending on weapon
    if (gameDB_)
    {
        int playerId = GetPlayerId(s.username);
        if (playerId >= 0)
        {
            if (isRanged)
            {
                gameDB_->AwardXP(playerId, result.hit ? "ranged_hit" : "ranged_miss");
                if (result.hit && cs.hp == 0)
                    gameDB_->AwardXP(playerId, "ranged_kill");
            }
            else
            {
                gameDB_->AwardXP(playerId, result.hit ? "melee_hit" : "melee_miss");
                if (result.hit && cs.hp == 0)
                    gameDB_->AwardXP(playerId, "melee_kill");
            }
        }
    }

    // Send authoritative result to attacker + nearby spectators.
    // Phase 2 extends the payload with hpRemaining (trailing field).
    VectorBuffer buf;
    buf.WriteU32(targetSpawnId);  // spawnId — client resolves via spawnIdToNode_
    buf.WriteBool(result.hit);
    buf.WriteBool(result.crit);
    buf.WriteBool(result.fumble);
    buf.WriteI32(result.damage);
    buf.WriteI32(result.roll);
    buf.WriteI32(cs.hp);  // server-authoritative HP after this hit
    connection->SendMessage(MSG_COMBAT_RESULT, true, false, buf);

    // Spectator broadcast: all authenticated clients within 50m of the target
    // except the attacker (who already got the direct send above).
    for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
    {
        if (!sIt->second_.authenticated || sIt->first_ == connection)
            continue;
        auto avIt = serverObjects_.Find(sIt->first_);
        if (avIt == serverObjects_.End() || !avIt->second_)
            continue;
        if ((avIt->second_->GetWorldPosition() - cs.position).Length() > 50.0f)
            continue;
        sIt->first_->SendMessage(MSG_COMBAT_RESULT, true, false, buf);
    }

    // Fumble (natural 1): weapon slips from hand back to bag.
    // Player must re-equip before next attack hits effectively.
    if (result.fumble && weaponItemId > 0 && worldDB_)
    {
        int playerId = GetPlayerId(s.username);
        if (playerId >= 0)
        {
            int removedId = 0;
            if (UnequipFromWorld(playerId, "hand", removedId))
            {
                LogMessage("[Combat] FUMBLE: " + s.username +
                           " dropped weapon " + String(removedId) + " (nat 1)");
                SendInventoryUpdate(connection, playerId);
            }
        }
    }

    // Death decision lives on the server now. RecordKill fires from here —
    // not from HandleHarvest, which is looting a corpse, not a death event.
    if (cs.hp == 0)
    {
        BroadcastCreatureDeath(targetSpawnId, cs, connection);
        if (populationManager_ && populationManager_->IsReady() && cs.regionId >= 0)
        {
            Vector<ReplacementSpawn> replacements =
                populationManager_->RecordKill(cs.regionId, cs.creatureId);
            for (unsigned i = 0; i < replacements.Size(); ++i)
            {
                const ReplacementSpawn& rep = replacements[i];
                Vector3 spawnPos = populationManager_->PickSpawnPositionInRegion(rep.regionId);
                BroadcastSpawnCreature(rep.regionId, rep.creatureId, spawnPos, 0.0f);
                LogMessage("[Population] Replacement (combat death): region " +
                           String(rep.regionId) + " creature " + String(rep.creatureId));
            }
        }
        creatureStates_.Erase(stateIt);
    }
}

bool AuthServer::LoadCreatureCombat(int creatureId, ServerCreatureState& out)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || creatureId <= 0)
        return false;
    CreatureInfo ci;
    if (!gameDB_->GetCreature(creatureId, ci))
        return false;
    out.creatureId = creatureId;
    out.species    = ci.name;
    out.hp = out.maxHp = ci.hp;
    out.defense   = ci.defense;
    out.attackMod = ci.attack;
    out.damage    = ci.damage;
    out.damageVar = ci.damageVar;
    return true;
#else
    (void)creatureId; (void)out;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Combat Phase 3: Situational Modifiers
// ---------------------------------------------------------------------------

float AuthServer::GetGameHour() const
{
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    // Melbourne UTC+11 — matches client's timeOfDay_ derivation
    int hour = utc->tm_hour + 11;
    if (hour >= 24) hour -= 24;
    return (float)hour + utc->tm_min / 60.0f + utc->tm_sec / 3600.0f;
}

bool AuthServer::IsNightTime() const
{
    float h = GetGameHour();
    return h >= 20.0f || h < 6.0f;
}

void AuthServer::ComputeSituationalMods(Connection* attacker, unsigned targetSpawnId,
                                        int& outAttackAdj, int& outDefenseAdj,
                                        String& outLog)
{
    outAttackAdj = 0;
    outDefenseAdj = 0;
    outLog.Clear();

    // Attacker position
    auto avIt = serverObjects_.Find(attacker);
    if (avIt == serverObjects_.End() || !avIt->second_)
        return;
    Vector3 attackerPos = avIt->second_->GetWorldPosition();

    // Target creature state
    auto csIt = creatureStates_.Find(targetSpawnId);
    if (csIt == creatureStates_.End())
        return;
    const ServerCreatureState& cs = csIt->second_;
    Vector3 targetPos = cs.position;

    // 1. High ground: attacker significantly above target
    if (attackerPos.y_ - targetPos.y_ > HIGH_GROUND_THRESHOLD)
    {
        outAttackAdj += MOD_HIGH_GROUND_ATTACK;
        outLog += " [HIGH_GROUND+" + String(MOD_HIGH_GROUND_ATTACK) + "]";
    }

    // 2. Water: combatant below water level
    if (attackerPos.y_ < AI_WATER_LEVEL)
    {
        outAttackAdj += MOD_WATER_ATTACK;
        outLog += " [IN_WATER" + String(MOD_WATER_ATTACK) + "]";
    }
    if (targetPos.y_ < AI_WATER_LEVEL)
    {
        outDefenseAdj += MOD_WATER_DEFENSE;
        outLog += " [TARGET_WATER" + String(MOD_WATER_DEFENSE) + "]";
    }

    // 3. Night blindness: no burning torch equipped
    if (IsNightTime())
    {
        bool hasTorch = false;
#ifdef URHO3D_DATABASE_SQLITE
        auto sIt = sessions_.Find(attacker);
        if (sIt != sessions_.End() && sIt->second_.authenticated && worldDB_)
        {
            int playerId = GetPlayerId(sIt->second_.username);
            if (playerId >= 0)
            {
                int mainhand = worldDB_->GetEquippedItem(playerId, "hand");
                int offhand  = worldDB_->GetEquippedItem(playerId, "offhand");
                hasTorch = (mainhand == ITEM_BURNING_TORCH || offhand == ITEM_BURNING_TORCH);
            }
        }
#endif
        if (!hasTorch)
        {
            outAttackAdj += MOD_NIGHT_BLIND_ATTACK;
            outLog += " [NIGHT" + String(MOD_NIGHT_BLIND_ATTACK) + "]";
        }
    }

    // 4. Ambush: target was unaware (idle, wandering, sleeping, eating, sitting)
    auto aiIt = creatureAI_.Find(targetSpawnId);
    if (aiIt != creatureAI_.End())
    {
        int task = aiIt->second_.currentTask;
        if (task == STASK_IDLE || task == STASK_WANDER || task == STASK_SLEEP ||
            task == STASK_EAT || task == STASK_EAT_FROM_INVENTORY || task == STASK_SIT_FIRE)
        {
            outAttackAdj += MOD_AMBUSH_ATTACK;
            outLog += " [AMBUSH+" + String(MOD_AMBUSH_ATTACK) + "]";
        }
    }

    // 5. Wounded target: below 25% HP → reduced defense (sluggish dodging)
    if (cs.maxHp > 0 && cs.hp > 0 && cs.hp <= cs.maxHp / 4)
    {
        outDefenseAdj += MOD_WOUNDED_ATTACK;  // negative value reduces defense
        outLog += " [WOUNDED" + String(MOD_WOUNDED_ATTACK) + "]";
    }
}

void AuthServer::BroadcastCreatureDeath(unsigned spawnId, const ServerCreatureState& cs,
                                        Connection* killer, DeathCause cause)
{
    // Range-based broadcast to all clients within 100m.
    VectorBuffer dbuf;
    dbuf.WriteU32(spawnId);  // client resolves via spawnIdToNode_
    dbuf.WriteString(cs.species);
    dbuf.WriteVector3(cs.position);
    dbuf.WriteI32(cs.creatureId);
    dbuf.WriteU8(static_cast<u8>(cause));  // Phase 5c: death cause for client-side visual selection

    String killerName("?");
    if (killer)
    {
        auto sIt = sessions_.Find(killer);
        if (sIt != sessions_.End())
            killerName = sIt->second_.username;
    }

    int sent = 0;
    for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
    {
        if (!sIt->second_.authenticated)
            continue;
        auto avIt = serverObjects_.Find(sIt->first_);
        if (avIt == serverObjects_.End() || !avIt->second_)
            continue;
        if ((avIt->second_->GetWorldPosition() - cs.position).Length() > 100.0f)
            continue;
        sIt->first_->SendMessage(MSG_CREATURE_DEATH, true, true, dbuf);
        ++sent;
    }

    static const char* causeNames[] = {
        "none", "combat", "drown", "starve", "age", "scavenge", "fall", "fire", "dehydrate", "freeze"
    };
    const char* causeName = (cause < 10) ? causeNames[cause] : "unknown";

    // Toe tag: who, what, where, when, why
    String npcName;
    auto aiIt = creatureAI_.Find(spawnId);
    if (aiIt != creatureAI_.End())
        npcName = aiIt->second_.npcName;
    if (npcName.Empty())
        npcName = cs.species;

    // For freeze deaths, sum equipped clothing warmth (same as UpdateCreatureVitals)
    String freezeExtra;
    if (cause == DEATH_FREEZE && aiIt != creatureAI_.End() && aiIt->second_.isHuman && worldDB_ && gameDB_)
    {
        float clothingWarmth = 0.0f;
        int npcPid = GetNPCPlayerId(spawnId);
        if (npcPid > 0)
        {
            static const char* warmSlots[] = {"body", "back", "feet", "head"};
            for (int ws = 0; ws < 4; ++ws)
            {
                int itemId = worldDB_->GetEquippedItem(npcPid, warmSlots[ws]);
                if (itemId > 0)
                    clothingWarmth += gameDB_->GetClothingWarmth(itemId);
            }
        }
        freezeExtra = " | clothingWarmth: " + String(clothingWarmth, 1);
    }

    LogMessage("[DEATH] " + npcName + " (" + cs.species + " #" + String(spawnId) + ")"
               " | cause: " + causeName +
               " | killer: " + killerName +
               " | hp: " + String(cs.hp) + "/" + String(cs.maxHp) +
               " | pos: (" + String((int)cs.position.x_) + "," +
               String((int)cs.position.y_) + "," + String((int)cs.position.z_) + ")"
               " | region: " + String(cs.regionId) +
               " | day: " + String(currentGameDay_) +
               freezeExtra);

    // Persist death to death_log
#ifdef URHO3D_DATABASE_SQLITE
    if (worldDB_ && worldDB_->IsOpen())
    {
        sqlite3* db = worldDB_->GetHandle();
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db,
            "INSERT INTO death_log (npc_name, species, spawn_id, creature_id, pos_x, pos_z, cause, killer_name, game_day, timestamp) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            // Get NPC name if available
            String npcName;
            auto aiIt = creatureAI_.Find(spawnId);
            if (aiIt != creatureAI_.End())
                npcName = aiIt->second_.npcName;

            sqlite3_bind_text(stmt, 1, npcName.CString(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, cs.species.CString(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, spawnId);
            sqlite3_bind_int(stmt, 4, cs.creatureId);
            sqlite3_bind_double(stmt, 5, cs.position.x_);
            sqlite3_bind_double(stmt, 6, cs.position.z_);
            sqlite3_bind_int(stmt, 7, static_cast<int>(cause));
            sqlite3_bind_text(stmt, 8, killerName.CString(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 9, currentGameDay_);
            sqlite3_bind_double(stmt, 10, GetSubsystem<Time>()->GetElapsedTime());
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
#endif

    // Phase 5b: register death scent so server-side scavengers can investigate
    RegisterServerScent(cs.position, cs.creatureId, spawnId);

    // Phase 18: Knowledge transfer — skilled NPC's knowledge spreads to settlement
    if (IsHumanSpecies(cs.creatureId))
    {
        TransferKnowledge(spawnId, cs.position);
        // Phase 21: chieftain succession — re-evaluate immediately on human death
        EvaluateChieftains();

#ifdef URHO3D_DATABASE_SQLITE
        // Bark Vessel fragility — 50% chance to break on death
        if (worldDB_)
        {
            int npcPid = GetNPCPlayerId(spawnId);
            if (npcPid > 0 && worldDB_->GetItemCount(npcPid, ITEM_BARK_VESSEL) > 0)
            {
                if (Random(1.0f) < 0.5f)
                {
                    worldDB_->RemoveItemFromInventory(npcPid, ITEM_BARK_VESSEL, 1);
                    torchTimers_.Erase(npcPid);
                    URHO3D_LOGINFOF("[BarkVessel] NPC %u's bark vessel shattered on death", spawnId);
                }
                else
                {
                    URHO3D_LOGINFOF("[BarkVessel] NPC %u's bark vessel survived death (50%% luck)", spawnId);
                }
            }
        }
#endif
    }

    // Loot scatter — roll loot table and spawn pickup nodes in a circle around death site
#ifdef URHO3D_DATABASE_SQLITE
    if (gameDB_ && scene_)
    {
        Vector<LootDrop> drops = gameDB_->GetLoot(cs.creatureId);
        auto* cache = GetSubsystem<ResourceCache>();
        for (unsigned i = 0; i < drops.Size(); ++i)
        {
            const LootDrop& d = drops[i];
            if (d.chance < 1.0f && Random() >= d.chance)
                continue;

            // Random position in ~2m circle around death site
            float angle = Random(6.2831853f);
            float radius = 0.5f + Random(1.5f);
            Vector3 lootPos = cs.position + Vector3(cosf(angle) * radius, 0.5f, sinf(angle) * radius);

            // Snap Y to terrain if available
            if (scene_->GetComponent<Terrain>())
            {
                float terrainY = scene_->GetComponent<Terrain>()->GetHeight(lootPos);
                if (terrainY > 0.0f)
                    lootPos.y_ = terrainY + 0.3f;
            }

            ItemInfo item;
            if (gameDB_->GetItem(d.itemId, item))
            {
                Node* lootNode = scene_->CreateChild("Loot");
                lootNode->SetPosition(lootPos);
                lootNode->SetVar("ItemID", d.itemId);
                lootNode->SetVar("ItemQty", d.quantity);

                if (!item.model.Empty())
                {
                    auto* sm = lootNode->CreateComponent<StaticModel>();
                    auto* model = cache->GetResource<Model>(item.model);
                    if (model)
                        sm->SetModel(model);
                }
            }
        }
    }
#endif
}

void AuthServer::HandleDrop(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int itemId = msg.ReadI32();
    int qty = msg.ReadI32();
    ClientSession& s = it->second_;

    if (!worldDB_ || qty <= 0)
        return;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    if (!worldDB_->RemoveItemFromInventory(playerId, itemId, qty))
    {
        LogMessage(s.username + " tried to drop item " + String(itemId) + " x" + String(qty) + " — insufficient");
        return;
    }

    // Spawn world node at player position
    auto nodeIt = serverObjects_.Find(connection);
    Vector3 dropPos;
    if (nodeIt != serverObjects_.End() && nodeIt->second_)
        dropPos = nodeIt->second_->GetWorldPosition() + Vector3(0, 0.5f, 1.0f);

    // Get item info for model
    ItemInfo item;
    if (gameDB_->GetItem(itemId, item) && scene_ && !item.model.Empty())
    {
        Node* dropNode = scene_->CreateChild("DroppedItem");
        dropNode->SetPosition(dropPos);
        dropNode->SetVar("ItemID", itemId);
        dropNode->SetVar("ItemQty", qty);

        auto* cache = GetSubsystem<ResourceCache>();
        auto* staticModel = dropNode->CreateComponent<StaticModel>();
        auto* model = cache->GetResource<Model>(item.model);
        if (model)
            staticModel->SetModel(model);
    }

    LogMessage(s.username + " dropped item " + String(itemId) + " x" + String(qty));
    SendInventoryDelta(connection, itemId, qty, false);
}

void AuthServer::HandleCraft(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int recipeId = msg.ReadI32();
    ClientSession& s = it->second_;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    // Get player position for station proximity checks
    Vector3 playerPos;
    auto avIt = serverObjects_.Find(connection);
    if (avIt != serverObjects_.End() && avIt->second_)
        playerPos = avIt->second_->GetWorldPosition();

    CraftForOwner(playerId, recipeId, playerPos, s.username, connection);
}

bool AuthServer::CraftForOwner(int playerId, int recipeId, const Vector3& position,
                                const String& ownerName, Connection* connection)
{
    if (!gameDB_ || !worldDB_)
        return false;

    // Build inventory map for CanCraft validation
    Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(playerId);
    HashMap<int, int> invMap;
    for (unsigned i = 0; i < inv.Size(); ++i)
        invMap[inv[i].itemId] += inv[i].quantity;

    if (!gameDB_->CanCraft(recipeId, invMap))
    {
        LogMessage(ownerName + " craft failed — insufficient materials (recipe " + String(recipeId) + ")");
        return false;
    }

    RecipeInfo recipe;
    if (!gameDB_->GetRecipe(recipeId, recipe))
    {
        LogMessage(ownerName + " craft failed — unknown recipe " + String(recipeId));
        return false;
    }

    // Station proximity check
    if (recipe.stationReq > 0)
    {
        bool nearStation = false;
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned i = 0; i < buildings.Size(); ++i)
        {
            if (buildings[i].buildingId == recipe.stationReq)
            {
                Vector3 bPos(buildings[i].posX, buildings[i].posY, buildings[i].posZ);
                if ((bPos - position).Length() <= CRAFTING_STATION_RANGE)
                {
                    nearStation = true;
                    break;
                }
            }
        }
        if (!nearStation)
        {
            LogMessage(ownerName + " craft failed — not near required station (building " +
                String(recipe.stationReq) + ")");
            return false;
        }

        // Kiln (604) and Charcoal Kiln (608) require a LIT campfire nearby
        if (recipe.stationReq == 604 || recipe.stationReq == 608)
        {
            bool hasLitFire = false;
            for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
            {
                if (cfIt->second_.state == PIT_LIT &&
                    (cfIt->second_.position - position).Length() <= CRAFTING_STATION_RANGE)
                {
                    hasLitFire = true;
                    break;
                }
            }
            if (!hasLitFire)
            {
                LogMessage(ownerName + " craft failed — kiln not lit (no LIT fire within range)");
                return false;
            }
        }
    }

    // Consume inputs FIRST — materials are spent on the attempt, success or failure
    for (unsigned i = 0; i < recipe.inputs.Size(); ++i)
    {
        if (recipe.inputs[i].consumed)
            worldDB_->RemoveItemFromInventory(playerId, recipe.inputs[i].itemId, recipe.inputs[i].quantity);
    }

    // INNOV_CHARCOAL_EFF: refund 1 charcoal if recipe used charcoal and settlement has innovation
    if (playerId >= 10000)
    {
        for (unsigned i = 0; i < recipe.inputs.Size(); ++i)
        {
            if (recipe.inputs[i].itemId == 43 && recipe.inputs[i].quantity > 1)  // charcoal
            {
                for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
                {
                    if (GetNPCPlayerId(aiIt->second_.spawnId) == playerId &&
                        HasInnovation(aiIt->second_.campfireId, INNOV_CHARCOAL_EFF))
                    { AddItemToWorldInventory(playerId, 43, 1); break; }
                }
                break;
            }
        }
    }

    // Determine crafting XP action from output category and name
    String craftAction = "craft_wood";
    {
        ItemInfo outputItem;
        if (gameDB_->GetItem(recipe.outputId, outputItem))
        {
            const String& cat = outputItem.category;
            const String& name = outputItem.name;
            // Textiles: weaving products → craft_fiber → SKILL_WEAVING
            if (name.Contains("Cloth") || name.Contains("Thread") || name.Contains("Woven") ||
                name.Contains("Reed Basket") || name.Contains("Net") || name.Contains("Bandage") ||
                name.Contains("Canvas") || name.Contains("Sail") || name.Contains("Tapestry"))
                craftAction = "craft_fiber";
            else if (cat == "armor" || cat == "clothing")
            {
                // Cloth clothing uses weaving, leather uses leatherwork
                if (name.Contains("Cloth"))
                    craftAction = "craft_fiber";
                else
                    craftAction = "craft_leather";
            }
            else if (cat == "material" && name.Contains("Stone"))
                craftAction = "craft_stone";
            else if (cat == "material" && (name.Contains("Cord") || name.Contains("Rope")))
                craftAction = "craft_fiber";
            else if (cat == "material" && name.Contains("Leather"))
                craftAction = "craft_leather";
            else if (cat == "material" && name.Contains("Clay"))
                craftAction = "craft_clay";
            else if (cat == "material" && name.Contains("Ingot"))
                craftAction = "craft_smelt";
            else if (cat == "weapon" || cat == "tool")
                craftAction = "craft_metal";
            else if (cat == "consumable")
                craftAction = "craft_herb";
        }
    }

    // Skill check: d20 + skill >= DC.  DC = 5 + tier * 3.
    if (!combatResolver_)
        combatResolver_ = new CombatResolver(context_);
        combatResolver_->SetExternalRNG(QuantumDiceRollBridge);

    int skillId = SKILL_WOODWORK;
    if (craftAction == "craft_stone")        skillId = SKILL_KNAPPING;
    else if (craftAction == "craft_leather") skillId = 12;  // Leatherwork
    else if (craftAction == "craft_fiber")   skillId = 13;  // Weaving
    else if (craftAction == "craft_clay")    skillId = 14;  // Pottery
    else if (craftAction == "craft_smelt")   skillId = 15;  // Smelting
    else if (craftAction == "craft_metal")   skillId = 16;  // Smithing
    else if (craftAction == "craft_herb")    skillId = SKILL_HERBALISM;

    int craftSkill = gameDB_->GetSkillLevel(playerId, skillId);
    int dc = 5 + recipe.tier * 3;
    int roll = combatResolver_->RollD20();

    // Phase 35: workshop proximity bonus to craft roll
    int workshopBonus = 0;
    if (worldDB_)
    {
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned b = 0; b < buildings.Size(); ++b)
        {
            Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
            if ((bPos - position).Length() > CRAFTING_STATION_RANGE)
                continue;
            if (buildings[b].buildingId == 82 && (craftAction == "craft_metal" || craftAction == "craft_smelt"))
                workshopBonus = Max(workshopBonus, 3);  // Smithy +3 metal
            else if (buildings[b].buildingId == 83 && craftAction == "craft_leather")
                workshopBonus = Max(workshopBonus, 2);  // Tannery +2 leather
            else if (buildings[b].buildingId == 81 && (craftAction == "craft_metal" || craftAction == "craft_smelt"))
                workshopBonus = Max(workshopBonus, 2);  // Forge +2 metal
            else if (buildings[b].buildingId == 89 && craftAction == "craft_food")
                workshopBonus = Max(workshopBonus, 2);  // Windmill +2 cooking/grinding
            else if (buildings[b].buildingId == 85 && craftAction == "craft_herb")
                workshopBonus = Max(workshopBonus, 3);  // Herbalist Hut +3 alchemy
            else if (buildings[b].buildingId == 91 && craftAction == "craft_fiber")
                workshopBonus = Max(workshopBonus, 2);  // Loom +2 weaving/textiles
        }
    }

    // INNOV_ALLOY_KNOWLEDGE: +1 smelting DC
    if (playerId >= 10000 && craftAction == "craft_smelt")
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
            if (GetNPCPlayerId(aiIt->second_.spawnId) == playerId &&
                HasInnovation(aiIt->second_.campfireId, INNOV_ALLOY_KNOWLEDGE))
            { workshopBonus += 1; break; }

    // INNOV_WEAPON_HARDENING: +10% durability handled at item creation (no callsite here)

    bool success = (roll == 20) || (roll != 1 && roll + craftSkill + workshopBonus >= dc);

    // XP always awarded — you learn from every attempt
    gameDB_->AwardXP(playerId, craftAction);

    if (!success)
    {
        LogMessage(ownerName + " craft FAILED " + recipe.name +
            " (roll " + String(roll) + "+" + String(craftSkill) +
            (workshopBonus > 0 ? "+" + String(workshopBonus) + "ws" : "") +
            " vs DC " + String(dc) + ")");
        if (connection)
            SendInventoryUpdate(connection, playerId);
        return false;
    }

    // Success — add output
    if (!AddItemToWorldInventory(playerId, recipe.outputId, recipe.outputQty))
    {
        // Inventory full — refund consumed inputs
        for (unsigned i = 0; i < recipe.inputs.Size(); ++i)
        {
            if (recipe.inputs[i].consumed)
                AddItemToWorldInventory(playerId, recipe.inputs[i].itemId, recipe.inputs[i].quantity);
        }
        LogMessage(ownerName + " craft failed — inventory full (recipe " + String(recipeId) + ")");
        if (connection)
            SendInventoryUpdate(connection, playerId);
        return false;
    }

    LogMessage(ownerName + " crafted " + recipe.name + " x" + String(recipe.outputQty) +
        " (roll " + String(roll) + "+" + String(craftSkill) +
        (workshopBonus > 0 ? "+" + String(workshopBonus) + "ws" : "") +
        " vs DC " + String(dc) + ")");

    // Alloy property effects — log alloy bonuses when crafting with alloy ingots
    for (unsigned i = 0; i < recipe.inputs.Size(); ++i)
    {
        AlloyPropertiesInfo alloy;
        if (gameDB_->GetAlloyProperties(recipe.inputs[i].itemId, alloy))
        {
            URHO3D_LOGINFOF("[Alloy] %s crafted with alloy %d — damage+%d defense+%d durability+%d%% special=%s",
                ownerName.CString(), alloy.alloyItemId, alloy.damageMod, alloy.defenseMod,
                alloy.durabilityPct, alloy.special.CString());
            break;
        }
    }

    // Spawn visual drop at crafter's position so crafted items appear in the world
    {
        ItemInfo outputItem;
        if (gameDB_->GetItem(recipe.outputId, outputItem) && scene_ && !outputItem.model.Empty())
        {
            // Offset slightly forward and up from crafter
            float angle = Random(6.2831853f);
            Vector3 dropPos = position + Vector3(cosf(angle) * 0.8f, 0.3f, sinf(angle) * 0.8f);

            // Snap Y to terrain if available
            if (scene_->GetComponent<Terrain>())
            {
                float terrainY = scene_->GetComponent<Terrain>()->GetHeight(dropPos);
                if (terrainY > 0.0f)
                    dropPos.y_ = terrainY + 0.3f;
            }

            Node* craftNode = scene_->CreateChild("CraftedItem");
            craftNode->SetPosition(dropPos);
            craftNode->SetVar("ItemID", recipe.outputId);
            craftNode->SetVar("ItemQty", recipe.outputQty);

            auto* cache = GetSubsystem<ResourceCache>();
            auto* sm = craftNode->CreateComponent<StaticModel>();
            auto* model = cache->GetResource<Model>(outputItem.model);
            if (model)
                sm->SetModel(model);
        }
    }

    // Phase 30: record settlement firsts for NPC crafters
    if (playerId >= 10000)
    {
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            if (GetNPCPlayerId(aiIt->second_.spawnId) == playerId && aiIt->second_.campfireId != 0)
            {
                String cat = recipe.outputId >= 820 ? "first_iron" :
                             recipe.outputId >= 806 ? "first_bronze" :
                             recipe.outputId >= 800 ? "first_copper" :
                             (recipe.outputId >= 100 && recipe.outputId < 200) ? "first_tool" : "";
                if (!cat.Empty())
                    RecordSettlementFirst(aiIt->second_.campfireId, cat, aiIt->second_.spawnId);
                break;
            }
        }
    }

    // Fire Carrying: start burn timers when fire items are crafted
    if (recipe.outputId == ITEM_FIRE_BUNDLE)
        torchTimers_[playerId] = FIRE_BUNDLE_BURN_TIME;
    else if (recipe.outputId == ITEM_BARK_VESSEL)
    {
        torchTimers_[playerId] = BARK_VESSEL_BURN_TIME;
        // Set vessel state to FIRE for the owner
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            if (GetNPCPlayerId(aiIt->first_) == playerId)
            { aiIt->second_.vesselContents = ServerCreatureAI::VESSEL_FIRE; break; }
        }
    }
    else if (recipe.outputId == ITEM_RESIN_TORCH)
        torchTimers_[playerId] = RESIN_TORCH_BURN_TIME;
    else if (recipe.outputId == ITEM_BURNING_TORCH && recipeId == 143)
    {
        // Unwrap Fire Bundle → fresh basic torch timer
        torchTimers_[playerId] = TORCH_BURN_TIME;
    }

    // Tapestry inscription: read first settlement_history event as the tapestry's description
    if (recipe.outputId == ITEM_TAPESTRY && worldDB_)
    {
        unsigned campfireId = 0;
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            if (GetNPCPlayerId(aiIt->second_.spawnId) == playerId)
            { campfireId = aiIt->second_.campfireId; break; }
        }
        if (campfireId != 0)
        {
            sqlite3* db = worldDB_->GetHandle();
            sqlite3_stmt* stmt = nullptr;
            String inscription;
            if (sqlite3_prepare_v2(db,
                "SELECT category, npc_name, game_day FROM settlement_history "
                "WHERE campfire_id = ? ORDER BY game_day ASC LIMIT 1",
                -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, (int)campfireId);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    String cat = (const char*)sqlite3_column_text(stmt, 0);
                    String npc = (const char*)sqlite3_column_text(stmt, 1);
                    int day = sqlite3_column_int(stmt, 2);
                    inscription = npc + " — " + cat + " (day " + String(day) + ")";
                }
                sqlite3_finalize(stmt);
            }
            if (!inscription.Empty())
            {
                worldDB_->Execute(
                    "INSERT OR REPLACE INTO tapestry_inscriptions (owner_id, inscription) "
                    "VALUES (" + String(playerId) + ", '" + inscription + "')");
                URHO3D_LOGINFOF("[Tapestry] Settlement %u tapestry depicts: %s",
                    campfireId, inscription.CString());

                // Broadcast inscription to all clients
                VectorBuffer ibuf;
                ibuf.WriteString(inscription);
                for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
                {
                    if (sIt->second_.authenticated)
                        sIt->first_->SendMessage(MSG_TAPESTRY_INSCRIPTION, true, true, ibuf);
                }
            }
        }
    }

    if (connection)
        SendInventoryUpdate(connection, playerId);
    return true;
}

void AuthServer::HandleEquip(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int itemId = msg.ReadI32();
    String slot = msg.ReadString();
    ClientSession& s = it->second_;

    if (!worldDB_ || slot.Empty())
        return;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    // Validate slot name
    if (slot != "hand" && slot != "offhand" && slot != "body" &&
        slot != "head" && slot != "feet" && slot != "back")
    {
        LogMessage(s.username + " equip failed — invalid slot: " + slot);
        return;
    }

    if (!worldDB_->EquipItem(playerId, itemId, slot))
    {
        LogMessage(s.username + " equip failed — item " + String(itemId) + " to " + slot);
        return;
    }

    LogMessage(s.username + " equipped item " + String(itemId) + " to " + slot);
    SendInventoryUpdate(connection, playerId);
}

void AuthServer::HandleUnequip(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    String slot = msg.ReadString();
    ClientSession& s = it->second_;

    if (!worldDB_ || slot.Empty())
        return;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    int removedItemId = 0;
    if (!UnequipFromWorld(playerId, slot, removedItemId))
    {
        LogMessage(s.username + " unequip failed — slot " + slot);
        return;
    }

    LogMessage(s.username + " unequipped item " + String(removedItemId) + " from " + slot);
    SendInventoryUpdate(connection, playerId);
}

void AuthServer::SendInventoryUpdate(Connection* connection, int playerId)
{
    if (!worldDB_)
        return;

    Vector<InventorySlot> inventory = worldDB_->GetPlayerInventory(playerId);
    float weight = ComputePlayerWeight(playerId);

    // Fold equipped backpack/pouch container bonuses into the capacities the
    // client displays, so an equipped Leather Backpack (+8 slots/+8 kg) or Hide
    // Pouch (+2 slots/+1 kg) actually shows more room and lets the player carry
    // more before the encumbrance threshold bites. Enforcement uses the same
    // augmented figures (ComputeMaxWeight / ComputeAvailableSlots).
    int extraSlots = 0;
    float extraWeight = 0.0f;
    ComputeContainerBonus(playerId, extraSlots, extraWeight);

    float baseSoftWeight = inventoryRulesLoaded_ ? inventoryRules_.maxWeight : 30.0f;
    float baseAbsWeight  = inventoryRulesLoaded_ ? inventoryRules_.maxWeightAbsolute : 60.0f;
    int   baseSlots      = inventoryRulesLoaded_ ? inventoryRules_.baseSlots : 10;

    VectorBuffer buf;
    buf.WriteI32((int)inventory.Size());
    buf.WriteFloat(weight);
    buf.WriteFloat(baseSoftWeight + extraWeight);
    buf.WriteFloat(baseAbsWeight + extraWeight);
    buf.WriteI32(baseSlots + extraSlots);

    for (unsigned i = 0; i < inventory.Size(); ++i)
    {
        const InventorySlot& slot = inventory[i];
        buf.WriteI32(slot.itemId);
        buf.WriteI32(slot.quantity);
        buf.WriteI32(slot.durability);
        buf.WriteString(slot.slotType);
    }

    connection->SendMessage(MSG_INVENTORY_UPDATE, true, true, buf);
}

void AuthServer::SendInventoryDelta(Connection* connection, int itemId, int quantity, bool added)
{
    VectorBuffer buf;
    buf.WriteI32(itemId);
    buf.WriteI32(quantity);
    buf.WriteBool(added);

    // Also send updated weight
    auto it = sessions_.Find(connection);
    if (it != sessions_.End() && worldDB_)
    {
        int playerId = GetPlayerId(it->second_.username);
        buf.WriteFloat(ComputePlayerWeight(playerId));
    }
    else
        buf.WriteFloat(0.0f);

    connection->SendMessage(MSG_INVENTORY_DELTA, true, true, buf);
}

// ─── BUILDING SYSTEM ────────────────────────────────────────────────────────

void AuthServer::CacheBuildingTypes()
{
    if (!gameDB_) return;

    Vector<BuildingTypeDBInfo> types = gameDB_->GetAllBuildingTypes();
    for (unsigned i = 0; i < types.Size(); ++i)
        cachedBuildingTypes_[types[i].id] = types[i];

    LogMessage("[Buildings] Cached " + String(types.Size()) + " building types");
}

void AuthServer::SetupDefaultPriorityCurves()
{
    if (!npcPriorityCompute_)
        return;

    Vector<PriorityCurveData> curves;

    // FLEE: hp low → highest priority (driver 5 = hpFraction)
    {
        PriorityCurveData c{};
        c.driverIndex = 5;  // hpFraction
        c.taskId = STASK_FLEE;
        c.numPoints = 3;
        c.points[0] = {0.0f, 30.0f};   // 0% hp → priority 30
        c.points[1] = {0.3f, 20.0f};   // 30% hp → priority 20
        c.points[2] = {0.5f, 0.0f};    // 50%+ hp → no flee
        curves.Push(c);
    }

    // EAT: hunger low → high priority (driver 0 = hunger)
    {
        PriorityCurveData c{};
        c.driverIndex = 0;  // hunger
        c.taskId = STASK_EAT;
        c.numPoints = 3;
        c.points[0] = {0.0f, 25.0f};   // starving → priority 25
        c.points[1] = {25.0f, 18.0f};  // very hungry → 18
        c.points[2] = {60.0f, 0.0f};   // not hungry → 0
        curves.Push(c);
    }

    // HUNT: moderate hunger, no food in hand (driver 0 = hunger)
    {
        PriorityCurveData c{};
        c.driverIndex = 0;  // hunger
        c.taskId = STASK_HUNT;
        c.numPoints = 3;
        c.points[0] = {0.0f, 15.0f};   // starving → 15 (but EAT will outrank)
        c.points[1] = {30.0f, 12.0f};  // hungry → 12
        c.points[2] = {50.0f, 0.0f};   // satisfied → 0
        curves.Push(c);
    }

    // DRINK: thirst low → high priority (driver 1 = thirst)
    // Curve starts competing at thirst=60 so NPCs seek water before it's urgent.
    // At thirst=40 it outbids tend_fire (12) and most other tasks.
    {
        PriorityCurveData c{};
        c.driverIndex = 1;  // thirst
        c.taskId = STASK_DRINK;
        c.numPoints = 3;
        c.points[0] = {0.0f, 30.0f};   // dehydrated → 30 (top priority)
        c.points[1] = {40.0f, 14.0f};  // thirsty → 14 (outbids tend_fire)
        c.points[2] = {60.0f, 0.0f};   // comfortable → 0
        curves.Push(c);
    }

    // WARM: warmth low → high priority (driver 2 = warmth)
    {
        PriorityCurveData c{};
        c.driverIndex = 2;  // warmth
        c.taskId = STASK_WARM;
        c.numPoints = 3;
        c.points[0] = {0.0f, 22.0f};   // freezing → 22
        c.points[1] = {15.0f, 12.0f};  // cold → 12
        c.points[2] = {25.0f, 0.0f};   // ok → 0
        curves.Push(c);
    }

    // SLEEP: stamina low → priority (driver 3 = stamina)
    {
        PriorityCurveData c{};
        c.driverIndex = 3;  // stamina
        c.taskId = STASK_SLEEP;
        c.numPoints = 3;
        c.points[0] = {0.0f, 20.0f};   // exhausted → 20
        c.points[1] = {20.0f, 10.0f};  // tired → 10
        c.points[2] = {40.0f, 0.0f};   // rested → 0
        curves.Push(c);
    }

    // TEND_FIRE: darkness high → priority (driver 4 = darkness)
    {
        PriorityCurveData c{};
        c.driverIndex = 4;  // darkness
        c.taskId = STASK_TEND_FIRE;
        c.numPoints = 3;
        c.points[0] = {0.0f, 0.0f};    // daytime → 0
        c.points[1] = {0.5f, 5.0f};    // dusk → 5
        c.points[2] = {1.0f, 12.0f};   // night → 12
        curves.Push(c);
    }

    // GATHER: moderate hunger + daytime (driver 0 = hunger)
    {
        PriorityCurveData c{};
        c.driverIndex = 0;  // hunger
        c.taskId = STASK_GATHER;
        c.numPoints = 3;
        c.points[0] = {0.0f, 10.0f};   // starving → 10 (EAT/HUNT outrank)
        c.points[1] = {40.0f, 8.0f};   // hungry-ish → 8
        c.points[2] = {60.0f, 0.0f};   // full → 0
        curves.Push(c);
    }

    // CRAFT/WORK: low-urgency, no survival need (driver 3 = stamina as proxy)
    {
        PriorityCurveData c{};
        c.driverIndex = 3;  // stamina — well-rested = productive
        c.taskId = STASK_CRAFT;
        c.numPoints = 3;
        c.points[0] = {30.0f, 0.0f};   // tired → don't work
        c.points[1] = {50.0f, 6.0f};   // moderate → 6
        c.points[2] = {100.0f, 10.0f}; // fully rested → 10
        curves.Push(c);
    }

    npcPriorityCompute_->SetCurves(curves);
    URHO3D_LOGINFOF("[NPCPriority] Loaded %u default priority curves", curves.Size());
}

void AuthServer::DispatchNPCPriorityCompute()
{
    if (!npcPriorityCompute_)
        return;

    // Collect vitals from all human NPCs
    Vector<GPUNPCDrivers> npcData;
    Vector<unsigned> spawnIds;  // parallel array for mapping results back

    float darkness = GetDarkness();

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& ai = it->second_;
        if (!ai.isHuman)
            continue;

        GPUNPCDrivers d{};
        d.hunger = ai.hunger;
        d.thirst = ai.thirst;
        d.warmth = ai.warmth;
        d.stamina = ai.stamina;
        d.darkness = darkness;

        // HP fraction
        auto csIt = creatureStates_.Find(ai.spawnId);
        if (csIt != creatureStates_.End() && csIt->second_.maxHp > 0)
            d.hpFraction = (float)csIt->second_.hp / (float)csIt->second_.maxHp;
        else
            d.hpFraction = 1.0f;

        d.pad0 = 0.0f;
        d.pad1 = 0.0f;

        npcData.Push(d);
        spawnIds.Push(ai.spawnId);
    }

    if (npcData.Empty())
        return;

    if (npcPriorityCompute_->IsComputeReady())
    {
        // GPU path: dispatch and readback
        npcPriorityCompute_->Dispatch(npcData);

        Vector<GPUNPCResult> results;
        npcPriorityCompute_->Readback(results);

        gpuPriorityCache_.Clear();
        for (unsigned i = 0; i < results.Size() && i < spawnIds.Size(); ++i)
            gpuPriorityCache_[spawnIds[i]] = results[i].taskId;
    }
    else
    {
        // CPU fallback: evaluate each NPC using the same curve logic
        gpuPriorityCache_.Clear();
        for (unsigned i = 0; i < npcData.Size(); ++i)
            gpuPriorityCache_[spawnIds[i]] = npcPriorityCompute_->EvaluateCPU(npcData[i]);
    }
}

void AuthServer::HandleBuild(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int buildingTypeId = msg.ReadI32();
    float px = msg.ReadFloat();
    float py = msg.ReadFloat();
    float pz = msg.ReadFloat();
    float rotation = msg.ReadFloat();
    int snappedTo = msg.ReadI32();

    ClientSession& s = it->second_;
    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    BuildForOwner(playerId, buildingTypeId, Vector3(px, py, pz), rotation, snappedTo,
                  s.username, connection);
}

void AuthServer::HandleDemolish(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int placedId = msg.ReadI32();
    ClientSession& s = it->second_;
    if (!worldDB_ || !gameDB_)
        return;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    // Verify ownership (placed buildings in worldDB)
    int ownerId = worldDB_->GetPlacedBuildingOwner(placedId);
    if (ownerId != playerId)
    {
        LogMessage(s.username + " tried to demolish building " + String(placedId) + " — not owner");
        return;
    }

    // Get building info for salvage
    PlacedBuildingDBInfo placed;
    if (!worldDB_->GetPlacedBuilding(placedId, placed))
        return;

    auto typeIt = cachedBuildingTypes_.Find(placed.buildingId);

    // Return 50% materials (salvage via cross-DB helper)
    Vector<BuildingRecipeInput> recipe = gameDB_->GetBuildingRecipe(placed.buildingId);
    for (unsigned i = 0; i < recipe.Size(); ++i)
    {
        int salvage = Max(1, recipe[i].quantity / 2);
        AddItemToWorldInventory(playerId, recipe[i].itemId, salvage);
    }

    // Remove from world database
    worldDB_->RemovePlacedBuilding(placedId);

    // Broadcast removal
    BroadcastBuildingRemove(placedId);

    // Send updated inventory
    SendInventoryUpdate(connection, playerId);

    String name = typeIt != cachedBuildingTypes_.End() ? typeIt->second_.name : "building";
    LogMessage(s.username + " demolished " + name + " (id=" + String(placedId) + ")");
}

void AuthServer::HandleGateToggle(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int placedId = msg.ReadI32();
    if (!worldDB_)
        return;

    PlacedBuildingDBInfo placed;
    if (!worldDB_->GetPlacedBuilding(placedId, placed))
        return;

    // Check it's actually a gate
    auto typeIt = cachedBuildingTypes_.Find(placed.buildingId);
    if (typeIt == cachedBuildingTypes_.End() || typeIt->second_.snapType != "gate")
        return;

    bool newState = !placed.gateOpen;
    worldDB_->SetGateOpen(placedId, newState);
    BroadcastGateState(placedId, newState);

    LogMessage("Gate " + String(placedId) + (newState ? " opened" : " closed"));
}

void AuthServer::HandleRepair(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int placedId = msg.ReadI32();
    ClientSession& s = it->second_;
    if (!worldDB_)
        return;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    // Verify ownership
    int ownerId = worldDB_->GetPlacedBuildingOwner(placedId);
    if (ownerId != playerId)
        return;

    if (!RepairForOwner(playerId, placedId, s.username, connection))
        LogMessage(s.username + " tried to repair building " + String(placedId) + " — no materials or full HP");
}

void AuthServer::HandleGodDirective(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    unsigned spawnId = msg.ReadU32();
    int directiveType = msg.ReadI32();
    int param = msg.ReadI32();
    float px = msg.ReadFloat();
    float py = msg.ReadFloat();
    float pz = msg.ReadFloat();

    auto aiIt = creatureAI_.Find(spawnId);
    if (aiIt == creatureAI_.End() || !aiIt->second_.isHuman)
    {
        LogMessage(it->second_.username + " god directive failed — invalid NPC " + String(spawnId));
        return;
    }

    ServerCreatureAI& ai = aiIt->second_;

    // Validate directive type
    if (directiveType < ServerCreatureAI::DIRECTIVE_NONE ||
        directiveType > ServerCreatureAI::DIRECTIVE_FORBID_TASK)
    {
        LogMessage(it->second_.username + " god directive failed — invalid type " + String(directiveType));
        return;
    }

    ai.directive = static_cast<ServerCreatureAI::GodDirective>(directiveType);
    ai.directiveParam = param;
    ai.directivePos = Vector3(px, py, pz);

    // Force task re-evaluation on next tick
    ai.taskDecisionTimer = 999.0f;

    const char* dirNames[] = { "NONE", "FOCUS_TASK", "PRIORITY_AREA", "FORBID_TASK" };
    LogMessage(it->second_.username + " set god directive on NPC " + String(spawnId) +
        ": " + dirNames[directiveType] + " param=" + String(param));
}

void AuthServer::HandleOpenStorage(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated) return;
    int buildingId = msg.ReadI32();
    if (!worldDB_) return;
    PlacedBuildingDBInfo info;
    if (!worldDB_->GetPlacedBuilding(buildingId, info)) return;

    // Lock check: if building has a lock, player must carry matching key
    {
        sqlite3* db = worldDB_->GetHandle();
        if (db)
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db,
                "SELECT key_item_id, owner_id FROM building_locks WHERE building_id = ?",
                -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, buildingId);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    int keyItemId = sqlite3_column_int(stmt, 0);
                    int ownerId = sqlite3_column_int(stmt, 1);
                    int playerId = GetPlayerId(it->second_.username);
                    // Owner always has access; others need the key in inventory
                    if (playerId != ownerId &&
                        (playerId < 0 || worldDB_->GetItemCount(playerId, keyItemId) <= 0))
                    {
                        sqlite3_finalize(stmt);
                        LogMessage(it->second_.username + " tried to open locked building " + String(buildingId));
                        return;
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    it->second_.openBuildingId = buildingId;
    String storageJson = worldDB_->GetBuildingStorageJson(buildingId);
    VectorBuffer reply;
    reply.WriteI32(buildingId);
    if (storageJson.Empty() || storageJson == "[]")
    {
        reply.WriteI32(0);
    }
    else
    {
        JSONFile json(context_);
        json.FromString(storageJson);
        const JSONArray& arr = json.GetRoot().GetArray();
        reply.WriteI32((int)arr.Size());
        for (unsigned i = 0; i < arr.Size(); ++i)
        {
            int iid = arr[i].Get("itemId").GetI32();
            int q = arr[i].Get("qty").GetI32();
            String name;
            if (gameDB_) { ItemInfo item; if (gameDB_->GetItem(iid, item)) name = item.name; }
            reply.WriteI32(iid);
            reply.WriteI32(q);
            reply.WriteString(name);
        }
    }
    connection->SendMessage(MSG_STORAGE_CONTENTS, true, true, reply);
}

void AuthServer::HandleCloseStorage(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End()) return;
    msg.ReadI32();
    it->second_.openBuildingId = -1;
}

void AuthServer::HandleTransfer(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated) return;
    int itemId = msg.ReadI32();
    int qty = msg.ReadI32();
    bool toStorage = msg.ReadBool();
    ClientSession& s = it->second_;
    if (!worldDB_ || s.openBuildingId < 0 || s.playerId < 0) return;
    int buildingId = s.openBuildingId;
    String storageJson = worldDB_->GetBuildingStorageJson(buildingId);
    JSONFile json(context_);
    if (!storageJson.Empty() && storageJson != "[]") json.FromString(storageJson);
    JSONArray items = json.GetRoot().IsArray() ? json.GetRoot().GetArray() : JSONArray();
    if (toStorage)
    {
        if (!worldDB_->RemoveItemFromInventory(s.playerId, itemId, qty)) return;
        bool found = false;
        for (unsigned i = 0; i < items.Size(); ++i)
        {
            if (items[i].Get("itemId").GetI32() == itemId)
            { items[i].Set("qty", JSONValue(items[i].Get("qty").GetI32() + qty)); found = true; break; }
        }
        if (!found)
        { JSONValue e; e.Set("itemId", JSONValue(itemId)); e.Set("qty", JSONValue(qty)); items.Push(e); }
    }
    else
    {
        bool removed = false;
        for (unsigned i = 0; i < items.Size(); ++i)
        {
            if (items[i].Get("itemId").GetI32() == itemId)
            {
                int have = items[i].Get("qty").GetI32();
                int take = Min(qty, have);
                if (take <= 0) return;
                AddItemToWorldInventory(s.playerId, itemId, take);
                if (have - take <= 0) items.Erase(i); else items[i].Set("qty", JSONValue(have - take));
                removed = true; break;
            }
        }
        if (!removed) return;
    }
    // Serialize items array back to JSON string
    String newJson = "[";
    for (unsigned i = 0; i < items.Size(); ++i)
    {
        if (i > 0) newJson += ",";
        newJson += "{\"itemId\":" + String(items[i].Get("itemId").GetI32()) +
                   ",\"qty\":" + String(items[i].Get("qty").GetI32()) + "}";
    }
    newJson += "]";
    worldDB_->UpdateBuildingStorage(buildingId, newJson);

    // Resend storage contents to client
    VectorBuffer rb; rb.WriteI32(buildingId);
    MemoryBuffer rm(rb.GetData(), rb.GetSize());
    HandleOpenStorage(connection, rm);

    // Send inventory delta for the transferred item
    SendInventoryDelta(connection, itemId, qty, !toStorage);
}

void AuthServer::HandleInstallLock(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated) return;
    int buildingId = msg.ReadI32();
    int lockItemId = msg.ReadI32();
    ClientSession& s = it->second_;
    if (!worldDB_ || s.playerId < 0) return;

    // Validate lock item is actually a lock (932 = Bronze Lock, 933 = Iron Lock)
    if (lockItemId != 932 && lockItemId != 933) return;

    // Must have the lock item in inventory
    if (worldDB_->GetItemCount(s.playerId, lockItemId) < 1) return;

    // Determine matching key item
    int keyItemId = (lockItemId == 932) ? 930 : 931;

    // Insert lock — fails silently if building already locked (PRIMARY KEY constraint)
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO building_locks (building_id, lock_item_id, owner_id, key_item_id) VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, buildingId);
        sqlite3_bind_int(stmt, 2, lockItemId);
        sqlite3_bind_int(stmt, 3, s.playerId);
        sqlite3_bind_int(stmt, 4, keyItemId);
        if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0)
        {
            worldDB_->RemoveItemFromInventory(s.playerId, lockItemId, 1);
            SendInventoryUpdate(connection, s.playerId);
            LogMessage(s.username + " installed lock on building " + String(buildingId));
        }
        sqlite3_finalize(stmt);
    }
#endif
}

void AuthServer::HandleRemoveLock(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated) return;
    int buildingId = msg.ReadI32();
    ClientSession& s = it->second_;
    if (!worldDB_ || s.playerId < 0) return;

    // Only the lock owner can remove it
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return;
    sqlite3_stmt* stmt = nullptr;
    int lockItemId = 0;
    if (sqlite3_prepare_v2(db,
        "SELECT lock_item_id FROM building_locks WHERE building_id = ? AND owner_id = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, buildingId);
        sqlite3_bind_int(stmt, 2, s.playerId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            lockItemId = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (lockItemId == 0) return;  // not owner or not locked

    // Remove lock and return item
    stmt = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM building_locks WHERE building_id = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, buildingId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    AddItemToWorldInventory(s.playerId, lockItemId, 1);
    SendInventoryUpdate(connection, s.playerId);
    LogMessage(s.username + " removed lock from building " + String(buildingId));
#endif
}

void AuthServer::HandleMount(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    unsigned horseSpawnId = msg.ReadU32();

    // Validate horse exists, is tamed, is a horse (creatureId 10)
    auto horseIt = creatureAI_.Find(horseSpawnId);
    if (horseIt == creatureAI_.End() || horseIt->second_.creatureId != 10 ||
        horseIt->second_.tamerId == 0)
    {
        LogMessage("Mount failed — invalid or untamed horse " + String(horseSpawnId));
        return;
    }

    // Find the NPC closest to this horse that belongs to the player's settlement
    unsigned possessedNode = it->second_.possessedNodeId;
    if (possessedNode == 0)
        return; // no NPC to mount

    auto npcIt = creatureAI_.Find(possessedNode);
    if (npcIt == creatureAI_.End() || !npcIt->second_.isHuman)
        return;

    // Already mounted?
    if (npcIt->second_.mountedHorse != 0)
        return;

    // Mount: boost NPC speed, link to horse
    npcIt->second_.mountedHorse = horseSpawnId;
    npcIt->second_.moveSpeed = 6.0f;  // gallop speed

    // Broadcast mounted state to all clients via AI state
    BroadcastCreatureAIState(possessedNode, npcIt->second_);

    LogMessage("NPC " + String(possessedNode) + " mounted horse " + String(horseSpawnId));
}

void AuthServer::HandleDismount(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    unsigned possessedNode = it->second_.possessedNodeId;
    if (possessedNode == 0) return;

    auto npcIt = creatureAI_.Find(possessedNode);
    if (npcIt == creatureAI_.End() || npcIt->second_.mountedHorse == 0)
        return;

    npcIt->second_.mountedHorse = 0;
    npcIt->second_.moveSpeed = 2.0f;  // back to walk speed

    BroadcastCreatureAIState(possessedNode, npcIt->second_);

    LogMessage("NPC " + String(possessedNode) + " dismounted");
}

void AuthServer::HandleSleep(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int placedId = msg.ReadI32();
    ClientSession& s = it->second_;
    if (!worldDB_)
        return;

    PlacedBuildingDBInfo placed;
    if (!worldDB_->GetPlacedBuilding(placedId, placed))
        return;

    auto typeIt = cachedBuildingTypes_.Find(placed.buildingId);
    if (typeIt == cachedBuildingTypes_.End())
        return;

    // Must be a shelter with sleep capacity
    if (typeIt->second_.sleepCapacity <= 0)
        return;

    // Heal the player (sleep restores HP and stamina)
    s.hp = Min(s.maxHp, s.hp + 5);
    s.stamina = Min(100.0f, s.stamina + 30.0f);

    // Warmth bonus from shelter
    if (typeIt->second_.warmth > 0.0f)
        s.warmth = Max(s.warmth, typeIt->second_.warmth);

    SendVitalUpdate(connection, s, true);
    LogMessage(s.username + " slept in building " + String(placedId));
}

void AuthServer::HandleSetRespawn(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    int placedId = msg.ReadI32();
    ClientSession& s = it->second_;
    if (!worldDB_)
        return;

    PlacedBuildingDBInfo placed;
    if (!worldDB_->GetPlacedBuilding(placedId, placed))
        return;

    auto typeIt = cachedBuildingTypes_.Find(placed.buildingId);
    if (typeIt == cachedBuildingTypes_.End())
        return;

    // Must be a shelter with respawn flag
    if (!typeIt->second_.respawn)
        return;

    // Must be owner
    int playerId = GetPlayerId(s.username);
    if (placed.ownerId != playerId)
        return;

    s.respawnBuildingId = placedId;

    VectorBuffer reply;
    reply.WriteI32(placedId);
    reply.WriteFloat(placed.posX);
    reply.WriteFloat(placed.posY);
    reply.WriteFloat(placed.posZ);
    connection->SendMessage(MSG_RESPAWN_SET, true, true, reply);

    LogMessage(s.username + " set respawn at building " + String(placedId));
}

int AuthServer::GetNPCPlayerId(unsigned nodeId)
{
    auto it = npcPlayerIds_.Find(nodeId);
    if (it != npcPlayerIds_.End())
        return it->second_;

    // Allocate new ID (stable: same node always gets the same ID once assigned)
    int id = npcPlayerIdCounter_++;
    npcPlayerIds_[nodeId] = id;
    return id;
}

void AuthServer::HandlePossess(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    unsigned npcNodeId = msg.ReadU32();
    ClientSession& s = it->second_;

    // Already possessing something? Unpossess first
    if (s.possessedNodeId != 0)
    {
        npcPossessors_.Erase(s.possessedNodeId);
        s.possessedNodeId = 0;
        s.possessedNPCPlayerId = -1;
    }

    // Check if another player already possesses this NPC
    auto possIt = npcPossessors_.Find(npcNodeId);
    if (possIt != npcPossessors_.End() && possIt->second_ != connection)
    {
        // Already possessed by someone else — reject
        VectorBuffer reply;
        reply.WriteU32(npcNodeId);
        reply.WriteI32(-1);
        reply.WriteBool(false);
        connection->SendMessage(MSG_POSSESS, true, true, reply);
        LogMessage(s.username + " possess rejected: NPC " + String(npcNodeId) + " already possessed");
        return;
    }

    // Grant possession
    int npcPlayerId = GetNPCPlayerId(npcNodeId);
    s.possessedNodeId = npcNodeId;
    s.possessedNPCPlayerId = npcPlayerId;
    npcPossessors_[npcNodeId] = connection;

    // Send success response with NPC's playerId
    VectorBuffer reply;
    reply.WriteU32(npcNodeId);
    reply.WriteI32(npcPlayerId);
    reply.WriteBool(true);
    connection->SendMessage(MSG_POSSESS, true, true, reply);

    // Send NPC's inventory to the client
    SendInventoryUpdate(connection, npcPlayerId);

    LogMessage(s.username + " possessed NPC " + String(npcNodeId) + " (playerId " + String(npcPlayerId) + ")");
}

void AuthServer::HandleUnpossess(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    ClientSession& s = it->second_;

    if (s.possessedNodeId == 0)
        return;  // Nothing to unpossess

    unsigned oldNodeId = s.possessedNodeId;
    npcPossessors_.Erase(oldNodeId);
    s.possessedNodeId = 0;
    s.possessedNPCPlayerId = -1;

    // Confirm to client
    VectorBuffer reply;
    reply.WriteU32(oldNodeId);
    connection->SendMessage(MSG_UNPOSSESS, true, true, reply);

    LogMessage(s.username + " unpossessed NPC " + String(oldNodeId));
}

void AuthServer::BroadcastBuildingSpawn(int placedId, int typeId, float px, float py, float pz,
                                         float rotation, int hp)
{
    VectorBuffer buf;
    buf.WriteI32(placedId);
    buf.WriteI32(typeId);
    buf.WriteFloat(px);
    buf.WriteFloat(py);
    buf.WriteFloat(pz);
    buf.WriteFloat(rotation);
    buf.WriteI32(hp);

    // Append storage JSON for buildings that carry metadata (e.g. graves store deceased name)
    String storage;
    if (typeId == 60 && worldDB_)  // Grave
        storage = worldDB_->GetBuildingStorageJson(placedId);
    buf.WriteString(storage);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_BUILDING_SPAWN, true, true, buf);
    }
}

void AuthServer::BroadcastBuildingRemove(int placedId)
{
    VectorBuffer buf;
    buf.WriteI32(placedId);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_BUILDING_REMOVE, true, true, buf);
    }
}

void AuthServer::BroadcastBuildingHp(int placedId, int newHp)
{
    VectorBuffer buf;
    buf.WriteI32(placedId);
    buf.WriteI32(newHp);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_BUILDING_HP, true, true, buf);
    }
}

void AuthServer::BroadcastGateState(int placedId, bool open)
{
    VectorBuffer buf;
    buf.WriteI32(placedId);
    buf.WriteBool(open);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_GATE_STATE, true, true, buf);
    }
}

// ─── Trap system (Resource Chain Phase 2) ───────────────────────────────────
//
// Phase 2 ships PLACEMENT (server-authoritative) and HARVEST (client-trusted
// creature_id — server rolls loot). The catch tick that would set creatures
// to CREATURE_TRAPPED via d20 vs hold_strength requires `creatureStates_`
// ============================================================================
// Trade System Phase 1 — Server-side barter session management
// ============================================================================

Connection* AuthServer::FindConnectionByPlayerId(int playerId)
{
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated && GetPlayerId(it->second_.username) == playerId)
            return it->first_;
    }
    return nullptr;
}

AuthServer::TradeSession* AuthServer::FindTradeSession(int playerId)
{
    for (auto it = tradeSessions_.Begin(); it != tradeSessions_.End(); ++it)
    {
        if (it->second_.playerA == playerId || it->second_.playerB == playerId)
            return &it->second_;
    }
    return nullptr;
}

void AuthServer::CleanupTradeSession(int playerId)
{
    for (auto it = tradeSessions_.Begin(); it != tradeSessions_.End(); ++it)
    {
        if (it->second_.playerA == playerId || it->second_.playerB == playerId)
        {
            tradeSessions_.Erase(it);
            return;
        }
    }
}

Vector3 AuthServer::GetPossessedNPCPosition(int playerId)
{
    // Walk sessions to find who possesses this playerId's NPC
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (!it->second_.authenticated)
            continue;
        if (GetPlayerId(it->second_.username) != playerId)
            continue;
        unsigned nodeId = it->second_.possessedNodeId;
        if (nodeId == 0)
            break;
        // Look up in creatureAI_ by scanning for matching spawnId
        for (auto ai = creatureAI_.Begin(); ai != creatureAI_.End(); ++ai)
        {
            if (ai->second_.spawnId == nodeId)
                return ai->second_.position;
        }
        break;
    }
    return Vector3::ZERO;
}

bool AuthServer::CheckTradeProximity(TradeSession& session)
{
    Vector3 posA = GetPossessedNPCPosition(session.playerA);
    Vector3 posB = GetPossessedNPCPosition(session.playerB);
    if (posA == Vector3::ZERO || posB == Vector3::ZERO)
        return true;  // can't determine position — don't cancel
    float dist = (posA - posB).Length();
    if (dist > TRADE_MAX_DISTANCE)
    {
        VectorBuffer buf;
        buf.WriteString("Too far apart");
        if (session.connA)
            session.connA->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
        if (session.connB)
            session.connB->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
        LogMessage("[Trade] Cancelled: players too far apart (" + String((int)dist) + "m)");
        CleanupTradeSession(session.playerA);
        return false;
    }
    return true;
}

void AuthServer::CancelTradeIfPossessedDamaged(unsigned spawnId)
{
    // Find which player possesses the NPC that just took damage
    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (!it->second_.authenticated || it->second_.possessedNodeId != spawnId)
            continue;
        int playerId = GetPlayerId(it->second_.username);
        TradeSession* session = FindTradeSession(playerId);
        if (!session)
            return;
        VectorBuffer buf;
        buf.WriteString("Combat interrupted trade");
        if (session->connA)
            session->connA->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
        if (session->connB)
            session->connB->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
        LogMessage("[Trade] Cancelled: combat interrupted (spawnId " + String(spawnId) + ")");
        // Record cooldown for both players
        float uptime = GetSubsystem<Time>() ? GetSubsystem<Time>()->GetElapsedTime() : 0.0f;
        tradeCooldowns_[session->playerA] = uptime;
        tradeCooldowns_[session->playerB] = uptime;
        CleanupTradeSession(playerId);
        return;
    }
}

void AuthServer::HandleTradeRequest(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated || !it->second_.alive)
        return;

    unsigned targetNodeId = msg.ReadU32();
    int myPlayerId = GetPlayerId(it->second_.username);

    // Phase 3: cooldown check
    auto coolIt = tradeCooldowns_.Find(myPlayerId);
    if (coolIt != tradeCooldowns_.End())
    {
        float uptime = GetSubsystem<Time>() ? GetSubsystem<Time>()->GetElapsedTime() : 0.0f;
        if (uptime - coolIt->second_ < TRADE_COOLDOWN_SECONDS)
        {
            VectorBuffer buf;
            buf.WriteString("Please wait before requesting another trade");
            connection->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
            return;
        }
        tradeCooldowns_.Erase(coolIt);
    }

    // Resolve target NPC node to the player who possesses it
    auto possIt = npcPossessors_.Find(targetNodeId);
    if (possIt == npcPossessors_.End())
    {
        LogMessage(it->second_.username + " trade request: target NPC " + String(targetNodeId) + " not possessed");
        return;
    }
    auto targetSessIt = sessions_.Find(possIt->second_);
    if (targetSessIt == sessions_.End() || !targetSessIt->second_.authenticated)
        return;
    int targetPlayerId = GetPlayerId(targetSessIt->second_.username);

    if (targetPlayerId == myPlayerId)
        return;  // no self-trade

    if (FindTradeSession(myPlayerId))
    {
        LogMessage(it->second_.username + " already in a trade session");
        return;
    }

    Connection* targetConn = FindConnectionByPlayerId(targetPlayerId);
    if (!targetConn)
    {
        LogMessage(it->second_.username + " trade target not online");
        return;
    }

    // Store pending request as a session with no offers yet — playerB not confirmed
    TradeSession session;
    session.playerA = myPlayerId;
    session.playerB = targetPlayerId;
    session.connA = connection;
    session.connB = targetConn;
    tradeSessions_[++tradeSessionSeq_] = session;

    // Notify target
    VectorBuffer buf;
    buf.WriteI32(myPlayerId);
    buf.WriteString(it->second_.username);
    targetConn->SendMessage(MSG_TRADE_INCOMING, false, false, buf);

    LogMessage(it->second_.username + " sent trade request to player " + String(targetPlayerId));
}

void AuthServer::HandleTradeAccept(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int myPlayerId = GetPlayerId(it->second_.username);
    TradeSession* session = FindTradeSession(myPlayerId);
    if (!session || session->playerB != myPlayerId)
    {
        LogMessage(it->second_.username + " no pending trade to accept");
        return;
    }

    // Notify requester that trade was accepted — both sides open trade window
    VectorBuffer buf;
    buf.WriteBool(true);  // accepted
    session->connA->SendMessage(MSG_TRADE_ACCEPT, false, false, buf);
    session->connB->SendMessage(MSG_TRADE_ACCEPT, false, false, buf);

    LogMessage(it->second_.username + " accepted trade with player " + String(session->playerA));
}

void AuthServer::HandleTradeReject(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int myPlayerId = GetPlayerId(it->second_.username);
    TradeSession* session = FindTradeSession(myPlayerId);
    if (!session)
        return;

    // Notify other party
    Connection* other = (session->playerA == myPlayerId) ? session->connB : session->connA;
    if (other)
    {
        VectorBuffer buf;
        buf.WriteString("Trade rejected");
        other->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
    }

    LogMessage(it->second_.username + " rejected trade");
    CleanupTradeSession(myPlayerId);
}

void AuthServer::HandleTradeOffer(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int myPlayerId = GetPlayerId(it->second_.username);
    TradeSession* session = FindTradeSession(myPlayerId);
    if (!session)
        return;

    // Phase 1: proximity check on every offer change
    if (!CheckTradeProximity(*session))
        return;

    int itemId = msg.ReadI32();
    int qty = msg.ReadI32();
    bool adding = msg.ReadBool();

    // Determine which side I am
    bool isA = (session->playerA == myPlayerId);
    HashMap<int, int>& myOffer = isA ? session->offerA : session->offerB;
    bool& myLock = isA ? session->lockedA : session->lockedB;
    bool& otherLock = isA ? session->lockedB : session->lockedA;

    // Can't modify after locking
    if (myLock)
        return;

    if (adding)
    {
        // Validate player actually has this item
        if (!worldDB_ || worldDB_->GetItemCount(myPlayerId, itemId) < qty)
            return;

        // Max 6 distinct items
        if (!myOffer.Contains(itemId) && myOffer.Size() >= 6)
            return;

        myOffer[itemId] = myOffer.Contains(itemId) ? myOffer[itemId] + qty : qty;
    }
    else
    {
        if (myOffer.Contains(itemId))
        {
            myOffer[itemId] -= qty;
            if (myOffer[itemId] <= 0)
                myOffer.Erase(itemId);
        }
    }

    // If other player was locked, unlock them (offer changed)
    if (otherLock)
    {
        otherLock = false;
        Connection* otherConn = isA ? session->connB : session->connA;
        if (otherConn)
        {
            VectorBuffer unlockBuf;
            unlockBuf.WriteBool(false);  // unlocked
            otherConn->SendMessage(MSG_TRADE_LOCK, false, false, unlockBuf);
        }
    }

    // Send update to the other player showing our offer
    Connection* otherConn = isA ? session->connB : session->connA;
    if (otherConn)
    {
        VectorBuffer buf;
        buf.WriteI32((int)myOffer.Size());
        for (auto oi = myOffer.Begin(); oi != myOffer.End(); ++oi)
        {
            buf.WriteI32(oi->first_);
            buf.WriteI32(oi->second_);
        }
        otherConn->SendMessage(MSG_TRADE_UPDATE, false, false, buf);
    }
}

void AuthServer::HandleTradeLock(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int myPlayerId = GetPlayerId(it->second_.username);
    TradeSession* session = FindTradeSession(myPlayerId);
    if (!session)
        return;

    // Phase 1: proximity check on lock attempt
    if (!CheckTradeProximity(*session))
        return;

    bool isA = (session->playerA == myPlayerId);
    bool& myLock = isA ? session->lockedA : session->lockedB;
    myLock = true;

    // Notify other player of lock state
    Connection* otherConn = isA ? session->connB : session->connA;
    if (otherConn)
    {
        VectorBuffer buf;
        buf.WriteBool(true);  // locked
        otherConn->SendMessage(MSG_TRADE_LOCK, false, false, buf);
    }

    // If both locked, execute swap
    if (session->lockedA && session->lockedB)
        ExecuteTradeSwap(*session);
}

void AuthServer::ExecuteTradeSwap(TradeSession& session)
{
    if (!worldDB_ || !gameDB_)
        return;

    // Validate both players still have their offered items
    for (auto oi = session.offerA.Begin(); oi != session.offerA.End(); ++oi)
    {
        if (worldDB_->GetItemCount(session.playerA, oi->first_) < oi->second_)
        {
            // Insufficient — cancel
            VectorBuffer buf;
            buf.WriteString("Trade failed: insufficient items");
            if (session.connA) session.connA->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
            if (session.connB) session.connB->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
            CleanupTradeSession(session.playerA);
            return;
        }
    }
    for (auto oi = session.offerB.Begin(); oi != session.offerB.End(); ++oi)
    {
        if (worldDB_->GetItemCount(session.playerB, oi->first_) < oi->second_)
        {
            VectorBuffer buf;
            buf.WriteString("Trade failed: insufficient items");
            if (session.connA) session.connA->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
            if (session.connB) session.connB->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
            CleanupTradeSession(session.playerA);
            return;
        }
    }

    // Remove offered items from both players
    for (auto oi = session.offerA.Begin(); oi != session.offerA.End(); ++oi)
        worldDB_->RemoveItemFromInventory(session.playerA, oi->first_, oi->second_);
    for (auto oi = session.offerB.Begin(); oi != session.offerB.End(); ++oi)
        worldDB_->RemoveItemFromInventory(session.playerB, oi->first_, oi->second_);

    // Add received items to both players
    for (auto oi = session.offerB.Begin(); oi != session.offerB.End(); ++oi)
        AddItemToWorldInventory(session.playerA, oi->first_, oi->second_);
    for (auto oi = session.offerA.Begin(); oi != session.offerA.End(); ++oi)
        AddItemToWorldInventory(session.playerB, oi->first_, oi->second_);

    // Award trade XP
    if (gameDB_)
    {
        gameDB_->AwardXP(session.playerA, "complete_trade");
        gameDB_->AwardXP(session.playerB, "complete_trade");

        // Log trade value from DB for balance tracking
        float totalA = 0.0f, totalB = 0.0f;
        for (auto oi = session.offerA.Begin(); oi != session.offerA.End(); ++oi)
        {
            TradeValue tv;
            if (gameDB_->GetTradeValue(oi->first_, tv))
                totalA += tv.baseValue * tv.scarcityMult * oi->second_;
        }
        for (auto oi = session.offerB.Begin(); oi != session.offerB.End(); ++oi)
        {
            TradeValue tv;
            if (gameDB_->GetTradeValue(oi->first_, tv))
                totalB += tv.baseValue * tv.scarcityMult * oi->second_;
        }
        if (totalA > 0.0f || totalB > 0.0f)
            URHO3D_LOGINFOF("[Trade] Value: A=%.1f, B=%.1f (ratio %.2f)",
                totalA, totalB, totalB > 0.0f ? totalA / totalB : 0.0f);
    }

    // Notify both
    VectorBuffer buf;
    buf.WriteBool(true);  // success
    if (session.connA)
    {
        session.connA->SendMessage(MSG_TRADE_COMPLETE, false, false, buf);
        SendInventoryUpdate(session.connA, session.playerA);
    }
    if (session.connB)
    {
        session.connB->SendMessage(MSG_TRADE_COMPLETE, false, false, buf);
        SendInventoryUpdate(session.connB, session.playerB);
    }

    LogMessage("Trade complete: player " + String(session.playerA) + " <-> player " + String(session.playerB));
    float uptime = GetSubsystem<Time>() ? GetSubsystem<Time>()->GetElapsedTime() : 0.0f;
    tradeCooldowns_[session.playerA] = uptime;
    tradeCooldowns_[session.playerB] = uptime;
    CleanupTradeSession(session.playerA);
}

void AuthServer::HandleTradeCancel(Connection* connection, MemoryBuffer& msg)
{
    auto it = sessions_.Find(connection);
    if (it == sessions_.End() || !it->second_.authenticated)
        return;

    int myPlayerId = GetPlayerId(it->second_.username);
    TradeSession* session = FindTradeSession(myPlayerId);
    if (!session)
        return;

    Connection* other = (session->playerA == myPlayerId) ? session->connB : session->connA;
    if (other)
    {
        VectorBuffer buf;
        buf.WriteString("Trade cancelled");
        other->SendMessage(MSG_TRADE_CANCEL, false, false, buf);
    }

    LogMessage(it->second_.username + " cancelled trade");
    float uptime = GetSubsystem<Time>() ? GetSubsystem<Time>()->GetElapsedTime() : 0.0f;
    tradeCooldowns_[session->playerA] = uptime;
    tradeCooldowns_[session->playerB] = uptime;
    CleanupTradeSession(myPlayerId);
}

// Resource Chain Phase 2 — Trap system (placement, catch, harvest).
// Player-placed traps: client ScanTrapsForCatches() detects proximity, server validates.
// NPC-placed traps: server TickLandTraps() proactively scans creature positions.
// Water fish traps: server TickWaterTraps() passive catch on 60s timer.

void AuthServer::HandlePlaceTrap(Connection* connection, MemoryBuffer& msg)
{
    auto sessIt = sessions_.Find(connection);
    if (sessIt == sessions_.End() || !sessIt->second_.authenticated || !sessIt->second_.alive)
        return;

    int   itemId = msg.ReadI32();
    float px     = msg.ReadFloat();
    float py     = msg.ReadFloat();
    float pz     = msg.ReadFloat();
    float rot    = msg.ReadFloat();

    ClientSession& s = sessIt->second_;
    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    unsigned nodeId = PlaceTrapForOwner(playerId, itemId, Vector3(px, py, pz), rot);
    if (nodeId == 0)
        return;

    // Player-specific feedback: inventory delta to the connection
    SendInventoryDelta(connection, itemId, -1, false);

    LogMessage(s.username + " placed trap (item " + String(itemId) +
               ") at (" + String(px) + ", " + String(py) + ", " + String(pz) +
               ") nodeId=" + String(nodeId));
}

unsigned AuthServer::PlaceTrapForOwner(int ownerPlayerId, int itemId, const Vector3& pos, float rot)
{
    if (!worldDB_ || !gameDB_)
        return 0;

    // Verify item exists and is a trap
    ItemInfo info;
    if (!gameDB_->GetItem(itemId, info) || info.category != "trap")
    {
        LogMessage("[Trap] PlaceTrapForOwner: item " + String(itemId) + " is not a trap");
        return 0;
    }

    // Verify owner has at least 1 of this trap
    if (worldDB_->GetItemCount(ownerPlayerId, itemId) < 1)
    {
        LogMessage("[Trap] PlaceTrapForOwner: playerId " + String(ownerPlayerId) +
                   " has no item " + String(itemId));
        return 0;
    }

    // Decrement inventory
    worldDB_->RemoveItemFromInventory(ownerPlayerId, itemId, 1);

    // Generate a unique trap node ID. We don't have a server scene for placed traps,
    // so we synthesise an ID from a counter and store the position in trapStates_.
    static unsigned nextTrapNodeId = 0xC0000000u;  // high range, won't collide with replicated IDs
    unsigned nodeId = ++nextTrapNodeId;

    ServerTrapState st;
    st.itemId = itemId;
    st.ownerPlayerId = ownerPlayerId;
    st.position = pos;
    st.placedAt = uptime_;
    st.armed = true;
    // Cage traps (406-408) are live-capture traps
    st.isLiveCapture = (itemId >= 406 && itemId <= 408);
    trapStates_[nodeId] = st;

    // Award trapping XP for placement
    gameDB_->AwardXP(ownerPlayerId, "trap_place");

    // Broadcast spawn to all clients
    BroadcastTrapSpawned(nodeId, itemId, pos, rot);
    return nodeId;
}

void AuthServer::HandleHarvest(Connection* connection, MemoryBuffer& msg)
{
    auto sessIt = sessions_.Find(connection);
    if (sessIt == sessions_.End() || !sessIt->second_.authenticated || !sessIt->second_.alive)
        return;

    unsigned targetNodeId = msg.ReadU32();
    int      creatureId   = msg.ReadI32();

    // Harvest dedup — each creature can only be looted once
    if (harvestedCreatures_.Contains(targetNodeId))
    {
        LogMessage(sessIt->second_.username + " tried to re-harvest creature " + String(targetNodeId) + " — already looted");
        return;
    }
    harvestedCreatures_.Insert(targetNodeId);

    ClientSession& s = sessIt->second_;
    if (!worldDB_ || !gameDB_)
        return;

    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    Vector<LootDrop> drops = gameDB_->GetLoot(creatureId);
    if (drops.Empty())
    {
        LogMessage(s.username + " tried to harvest creature " + String(creatureId) +
                   " — no loot table entry");
        return;
    }

    // Roll each drop chance, build the awarded list
    Vector<LootDrop> awarded;
    for (unsigned i = 0; i < drops.Size(); ++i)
    {
        const LootDrop& d = drops[i];
        if (d.chance >= 1.0f || Random() < d.chance)
            awarded.Push(d);
    }

    // Deduct durability from equipped tool (if any)
    if (worldDB_)
    {
        int toolId = worldDB_->GetEquippedItem(playerId, "hand");
        if (toolId > 0)
        {
            int remaining = worldDB_->DeductDurability(playerId, "hand");
            if (remaining == 0)
            {
                LogMessage("[Item] " + s.username + "'s tool broke (item " + String(toolId) + ")");
                SendInventoryUpdate(connection, playerId);
            }
        }
    }

    // Add to inventory and build the result message
    VectorBuffer reply;
    reply.WriteU32(targetNodeId);
    reply.WriteI32((int)awarded.Size());
    for (unsigned i = 0; i < awarded.Size(); ++i)
    {
        const LootDrop& d = awarded[i];
        AddItemToWorldInventory(playerId, d.itemId, d.quantity);
        SendInventoryDelta(connection, d.itemId, d.quantity, true);
        reply.WriteI32(d.itemId);
        reply.WriteI32(d.quantity);
    }
    connection->SendMessage(MSG_HARVEST_RESULT, true, true, reply);

    LogMessage(s.username + " harvested creature " + String(creatureId) +
               " — " + String(awarded.Size()) + " loot items");

    // Award foraging XP for harvesting
    if (gameDB_ && !awarded.Empty())
        gameDB_->AwardXP(playerId, "forage");

    // ── Population accounting moved to Combat Phase 2 ─────────────────────────
    // RecordKill now fires from HandleAttack on the server-authoritative HP=0
    // path (sim-first decision Apr 8: all deaths count, harvest is looting a
    // corpse, not a death event). The earlier harvest-side RecordKill move was
    // rejected by Leith on the same day. See PLAN_DEATH_SYSTEM.md and
    // TASK_COMBAT_SYSTEM_PHASE_2.md for the full reasoning.

    // If the harvested creature was held in a trap, remove the trap.
    // Scan trapStates_ for any triggered trap near the harvested creature's position.
    auto csIt = creatureStates_.Find(targetNodeId);
    if (csIt != creatureStates_.End())
    {
        for (auto trapIt = trapStates_.Begin(); trapIt != trapStates_.End(); ++trapIt)
        {
            if (trapIt->second_.armed)
                continue; // Still armed — not triggered
            Vector3 diff = csIt->second_.position - trapIt->second_.position;
            if (diff.LengthSquared() < 4.0f * 4.0f)
            {
                BroadcastTrapRemoved(trapIt->first_);
                trapStates_.Erase(trapIt);
                break;
            }
        }
    }
}

// ============================================================================
// Resource Map — server-authoritative spatial resource database
// ============================================================================

void AuthServer::InitResourceMap()
{
    if (!scene_)
        return;

    ResourceMap::RegisterObject(context_);
    resourceMap_ = new ResourceMap(context_);

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    String savedPath = cache->GetResourceDirs()[0] + "GameDB/resource_map.png";

    // Try to load a previously saved resource map (bypasses ResourceCache caching)
    if (fs->FileExists(savedPath) && resourceMap_->LoadMapFromFile(savedPath))
    {
        // Restore terrain bounds so WorldToPixel works correctly
        auto* terrain = scene_->GetComponent<Terrain>(true);
        if (terrain)
            resourceMap_->SetTerrainBounds(terrain);

        LogMessage("ResourceMap: loaded " + String(resourceMap_->GetResourceCount()) +
                   " resources from " + savedPath);
        return;
    }

    // No saved map — generate from terrain
    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain)
    {
        LogMessage("[WARN] No terrain in scene — resource map not generated");
        return;
    }

    const float waterLevel = 5.0f;
    resourceMap_->Generate(terrain, nullptr, waterLevel, gameDB_);
    LogMessage("ResourceMap: generated " + String(resourceMap_->GetResourceCount()) +
               " resource pixels (server-authoritative)");

    // Save for next startup
    String gameDBDir = cache->GetResourceDirs()[0] + "GameDB";
    if (!fs->DirExists(gameDBDir))
        fs->CreateDir(gameDBDir);
    resourceMap_->SaveMap(savedPath);
    LogMessage("ResourceMap: saved to " + savedPath);
}

void AuthServer::HandleResourceHarvest(Connection* connection, MemoryBuffer& msg)
{
    auto sessIt = sessions_.Find(connection);
    if (sessIt == sessions_.End() || !sessIt->second_.authenticated || !sessIt->second_.alive)
        return;

    if (!resourceMap_ || !resourceMap_->GetImage())
        return;

    float worldX = msg.ReadFloat();
    float worldZ = msg.ReadFloat();
    unsigned char requestedType = msg.ReadU8();

    ClientSession& s = sessIt->second_;

    // Validate: sample the server's authoritative map at this position
    unsigned char serverQty = 0;
    unsigned char serverVariant = 0;
    ResourceType serverType = resourceMap_->Sample(worldX, worldZ, serverQty, serverVariant);

    if (serverType == RES_NONE || serverQty == 0)
    {
        LogMessage(s.username + " harvest rejected at (" + String(worldX) + ", " +
                   String(worldZ) + ") — empty on server");
        return;
    }

    // Type mismatch — client is stale or cheating
    if ((unsigned char)serverType != requestedType)
    {
        LogMessage(s.username + " harvest type mismatch at (" + String(worldX) + ", " +
                   String(worldZ) + ") — client says " + String((int)requestedType) +
                   ", server has " + String((int)serverType));
        return;
    }

    // Harvest 1 unit from the server's map
    int taken = resourceMap_->Harvest(worldX, worldZ, 1);
    if (taken <= 0)
        return;

    // Map resource type to item ID and add to player inventory
    int itemId = ResourceTypeToItemId(serverType);
    if (itemId < 0)
        return;

#ifdef URHO3D_DATABASE_SQLITE
    int playerId = GetPlayerId(s.username);
    if (playerId < 0)
        return;

    if (!AddItemToWorldInventory(playerId, itemId, taken))
    {
        LogMessage(s.username + " can't harvest — inventory full");
        return;
    }

    SendInventoryDelta(connection, itemId, taken, true);

    // Economic doctrine: deduct from regional resource pool with scarcity check
    if (gameDB_ && populationManager_)
    {
        int regionId = populationManager_->FindRegion(worldX, worldZ);
        if (regionId >= 0)
        {
            Vector<ResourceTypeInfo> resTypes = gameDB_->GetAllResourceTypes();
            for (unsigned r = 0; r < resTypes.Size(); ++r)
            {
                if (resTypes[r].itemId == itemId)
                {
                    float scarcity = gameDB_->GetScarcityModifier(regionId, resTypes[r].id);
                    ExtractionResult result = gameDB_->ExtractResource(playerId, regionId, resTypes[r].id, currentGameDay_);
                    if (result.success)
                    {
                        RegionResourceInfo pool;
                        if (gameDB_->GetRegionResource(regionId, resTypes[r].id, pool))
                            URHO3D_LOGDEBUGF("[Economy] Harvest %s: scarcity=%.2f remaining=%.0f/%.0f",
                                resTypes[r].name.CString(), scarcity, pool.currentAmount, pool.maxAmount);
                    }
                    break;
                }
            }
        }
    }

    // Award gathering XP
    if (gameDB_)
        gameDB_->AwardXP(playerId, "forage");
#endif

    // Read back new quantity for broadcast
    unsigned char newQty = 0;
    unsigned char dummyVariant = 0;
    resourceMap_->Sample(worldX, worldZ, newQty, dummyVariant);

    // Broadcast depletion to all clients
    BroadcastResourceDepleted(worldX, worldZ, newQty, (unsigned char)serverType);

    // Mark dirty for persistence
    resourceMapDirty_ = true;
    resourceMapSaveTimer_ = RESOURCE_SAVE_AFTER_CHANGE;

    LogMessage(s.username + " harvested " + String((int)serverType) + " at (" +
               String(worldX) + ", " + String(worldZ) + ") — " + String((int)newQty) + " remaining");
}

void AuthServer::SaveResourceMapIfDirty()
{
    if (!resourceMapDirty_ || !resourceMap_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    String savedPath = cache->GetResourceDirs()[0] + "GameDB/resource_map.png";
    if (resourceMap_->SaveMap(savedPath))
    {
        resourceMapDirty_ = false;
        LogMessage("ResourceMap: saved (" + String(resourceMap_->GetResourceCount()) + " resources)");
    }
}

void AuthServer::BroadcastResourceDepleted(float worldX, float worldZ, unsigned char newQty, unsigned char resourceType)
{
    VectorBuffer buf;
    buf.WriteFloat(worldX);
    buf.WriteFloat(worldZ);
    buf.WriteU8(newQty);
    buf.WriteU8(resourceType);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_RESOURCE_DEPLETED, true, true, buf);
    }
}

void AuthServer::SendResourceMapToClient(Connection* connection)
{
    if (!resourceMap_ || !resourceMap_->GetImage())
        return;

    Image* img = resourceMap_->GetImage();

    // Compress as PNG (16MB raw → ~1-2MB compressed)
    VectorBuffer pngBuf;
    if (!img->Save(pngBuf))
    {
        LogMessage("[WARN] Failed to compress resource map PNG");
        return;
    }

    // Use existing MSG_RESOURCE_PATCH format with components=0 as PNG flag
    VectorBuffer buf;
    buf.WriteString("resource_map");
    buf.WriteI32(0);  // patchX
    buf.WriteI32(0);  // patchZ
    buf.WriteI32(0);  // pixelX
    buf.WriteI32(0);  // pixelZ
    buf.WriteI32(img->GetWidth());   // pixelW
    buf.WriteI32(img->GetHeight());  // pixelH
    buf.WriteI32(0);  // components=0 signals PNG-compressed data
    buf.WriteU32((unsigned)pngBuf.GetSize());
    buf.Write(pngBuf.GetData(), pngBuf.GetSize());

    connection->SendMessage(MSG_RESOURCE_PATCH, true, true, buf);
    LogMessage("Sent resource map to client (" + String(buf.GetSize()) + " bytes, PNG compressed from " +
               String(img->GetWidth() * img->GetHeight() * img->GetComponents()) + " raw)");
}

void AuthServer::BroadcastTrapSpawned(unsigned nodeId, int itemId, const Vector3& pos, float rotation)
{
    VectorBuffer buf;
    buf.WriteU32(nodeId);
    buf.WriteI32(itemId);
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(pos.y_);
    buf.WriteFloat(pos.z_);
    buf.WriteFloat(rotation);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_TRAP_SPAWNED, true, true, buf);
    }
}

void AuthServer::BroadcastTrapRemoved(unsigned nodeId)
{
    VectorBuffer buf;
    buf.WriteU32(nodeId);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_TRAP_REMOVED, true, true, buf);
    }
}

void AuthServer::BroadcastTrapTriggered(unsigned trapNodeId, unsigned creatureNodeId, const Vector3& pos)
{
    VectorBuffer buf;
    buf.WriteU32(trapNodeId);
    buf.WriteU32(creatureNodeId);
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(pos.y_);
    buf.WriteFloat(pos.z_);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_TRAP_TRIGGERED, true, true, buf);
    }
}

void AuthServer::HandleTrapCheck(Connection* connection, MemoryBuffer& msg)
{
    auto sessIt = sessions_.Find(connection);
    if (sessIt == sessions_.End() || !sessIt->second_.authenticated)
        return;

    const unsigned trapNodeId     = msg.ReadU32();
    const unsigned creatureNodeId = msg.ReadU32();
    const int      creatureId     = msg.ReadI32();
    const float    cx             = msg.ReadFloat();
    const float    cy             = msg.ReadFloat();
    const float    cz             = msg.ReadFloat();
    const Vector3  creaturePos(cx, cy, cz);

    // Trap must exist and still be armed.
    auto trapIt = trapStates_.Find(trapNodeId);
    if (trapIt == trapStates_.End())
        return;
    ServerTrapState& trap = trapIt->second_;
    if (!trap.armed)
        return;

#ifdef URHO3D_DATABASE_SQLITE
    // Catchability is per (trapItemId, creatureId): if there's no rule row, this
    // creature simply isn't catchable by this trap.
    if (!gameDB_)
        return;
    TrapRule rule;
    if (!gameDB_->GetTrapRule(trap.itemId, creatureId, rule))
        return;

    // Authoritative range check (the client uses a conservative TRAP_CHECK_RADIUS
    // and lets the server enforce the per-rule attractRange).
    const Vector3 d = creaturePos - trap.position;
    if (d.LengthSquared() > rule.attractRange * rule.attractRange)
        return;

    if (!combatResolver_)
    {
        combatResolver_ = new CombatResolver(context_);
        combatResolver_->SetExternalRNG(QuantumDiceRollBridge);
    }

    // Trapping skill: experienced trappers build better traps (bonus to roll)
    int trapSkillBonus = 0;
    if (gameDB_ && trap.ownerPlayerId > 0)
        trapSkillBonus = gameDB_->GetSkillLevel(trap.ownerPlayerId, SKILL_TRAPPING) / 2;
    const int roll = combatResolver_->RollD20() + trapSkillBonus;
    LogMessage("[Trap] check trap=" + String(trapNodeId) +
               " creature=" + String(creatureNodeId) +
               " (creatureId=" + String(creatureId) + ")" +
               " roll=" + String(roll) + " (bonus+" + String(trapSkillBonus) +
               ") vs hold=" + String(rule.holdStrength));
    if (roll < rule.holdStrength)
        return;  // creature escaped — trap stays armed

    // CATCH. Disarm the trap so further checks bail and the visual stays once-only.
    trap.armed = false;

    // Award trapping XP to the trap owner (the player who placed it)
    if (gameDB_)
        gameDB_->AwardXP(trap.ownerPlayerId, "trap_catch");

    // Register the creature in creatureStates_ so a follow-up MSG_HARVEST works.
    // The harvest path is state-agnostic; we just need a sensible row to exist.
    auto stateIt = creatureStates_.Find(creatureNodeId);
    if (stateIt == creatureStates_.End())
    {
        ServerCreatureState fresh;
        fresh.creatureId = creatureId;
        if (!LoadCreatureCombat(creatureId, fresh))
        {
            // Unknown species — accept defaults so harvest still resolves.
            fresh.hp = fresh.maxHp = 1;
            fresh.defense = 10;
        }
        fresh.position = creaturePos;
        if (populationManager_)
            fresh.regionId = populationManager_->FindRegion(creaturePos.x_, creaturePos.z_);
        creatureStates_[creatureNodeId] = fresh;
    }

    BroadcastTrapTriggered(trapNodeId, creatureNodeId, creaturePos);
    LogMessage("[Trap] CAUGHT trap=" + String(trapNodeId) +
               " creature=" + String(creatureNodeId) +
               " species=" + creatureStates_[creatureNodeId].species);
#else
    (void)creatureId; (void)creaturePos;
#endif
}

void AuthServer::BroadcastSpawnCreature(int regionId, int creatureId, const Vector3& pos, float growthProgress)
{
    // Format must match Protocol.h MSG_SPAWN_CREATURE comment + the client
    // handler in TerrainNode. Y is intentionally 0.0 — clients snap to
    // terrain height on receive (server has no terrain to query).

    // Assign a server-side tracking ID for AI state correlation.
    unsigned spawnId = ++nextSpawnId_;

    // Generate NPC name before sending so client gets it in the spawn message
    String npcName;
    if (IsHumanSpecies(creatureId))
        npcName = GenerateNPCName(AssignCampfireForNPC(pos, regionId));

    VectorBuffer buf;
    buf.WriteI32(regionId);
    buf.WriteI32(creatureId);
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(0.0f);
    buf.WriteFloat(pos.z_);
    buf.WriteU32(spawnId);
    buf.WriteString(npcName);  // trailing string — empty for animals
    // Phase 25: selective breeding traits (defaults for fresh spawns)
    buf.WriteFloat(1.0f);  // traitSize
    buf.WriteFloat(1.0f);  // traitSpeed
    buf.WriteFloat(1.0f);  // traitYield

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_SPAWN_CREATURE, true, true, buf);
    }

    // Create a server-side REPLICATED scene node so the AI tick can sync
    // position + state Vars, which Urho3D replicates to clients automatically.
    // Without this, dynamically spawned creatures look frozen on clients.
    if (scene_)
    {
        static const struct { const char* name; const char* model; const char* mat; int id; } spawnModels[] = {
            {"Rabbit",    "Models/Animals/Rabbit.mdl",            "Models/Animals/Rabbit.txt",            1},
            {"Deer",      "Models/Animals/Deer.mdl",              "Models/Animals/Deer.txt",              2},
            {"Fox",       "Models/Animals/Fox.mdl",               "Models/Animals/Fox.txt",               3},
            {"Stag",      "Models/Animals/Stag.mdl",              "Models/Animals/Stag.txt",              4},
            {"Wolf",      "Models/Animals/Wolf.mdl",              "Models/Animals/Wolf.txt",              5},
            {"Bull",      "Models/Animals/Bull.mdl",              "Models/Animals/Bull.txt",              6},
            {"Cow",       "Models/Animals/Cow.mdl",               "Models/Animals/Cow.txt",               7},
            {"Donkey",    "Models/Animals/Donkey.mdl",            "Models/Animals/Donkey.txt",             9},
            {"Horse",     "Models/Animals/Horse.mdl",             "Models/Animals/Horse.txt",             10},
            {"Alpaca",    "Models/Animals/Alpaca.mdl",            "Models/Animals/Alpaca.txt",            11},
            {"Husky",     "Models/Animals/Husky.mdl",             "Models/Animals/Husky.txt",             12},
            {"ShibaInu",  "Models/Animals/ShibaInu.mdl",          "Models/Animals/ShibaInu.txt",          13},
            {"CaveMan",   "Models/Characters/CavemanMan.mdl",     "Models/Characters/CavemanMan.txt",     20},
            {"CaveWoman", "Models/Characters/CavemanWoman.mdl",   "Models/Characters/CavemanWoman.txt",   21},
        };
        const char* nodeName = "Creature";
        const char* modelPath = nullptr;
        const char* matPath = nullptr;
        for (const auto& sm : spawnModels)
        {
            if (sm.id == creatureId)
            {
                nodeName = sm.name;
                modelPath = sm.model;
                matPath = sm.mat;
                break;
            }
        }

        Vector3 nodePos = pos;
        nodePos.y_ = GetTerrainHeightAI(pos.x_, pos.z_);

        Node* node = scene_->CreateChild(nodeName);
        node->SetPosition(nodePos);
        node->SetVar("CreatureId", creatureId);
        node->SetVar("SpawnId", spawnId);

        if (modelPath)
        {
            auto* cache = GetSubsystem<ResourceCache>();
            Node* modelNode = node->CreateChild(String(nodeName) + "Model");
            modelNode->SetRotation(Quaternion(180.0f, Vector3::UP));
            auto* model = modelNode->CreateComponent<AnimatedModel>();
            auto* mdl = cache->GetResource<Model>(modelPath);
            if (mdl)
            {
                model->SetModel(mdl, true, true);
                if (matPath)
                    model->ApplyMaterialList(matPath);
                model->SetCastShadows(false);
            }
            modelNode->CreateComponent<AnimationController>();
        }

        creatureNodes_[spawnId] = node;
    }

    // Register server-side AI tracking for ALL creatures.
    // Humans (20, 21) get full task AI. Animals get simpler wander+flee+hunt.
    // This is the "mini-Phase 8" that lets Phase 3 hunters target real prey.
    {
        // Snap Y to terrain height on the server side — the network message
        // sends Y=0 for clients to snap locally, but the server AI needs real Y
        // to avoid false drowning detection.
        Vector3 aiPos = pos;
        aiPos.y_ = GetTerrainHeightAI(pos.x_, pos.z_);

        ServerCreatureAI ai;
        ai.position = aiPos;
        ai.targetPosition = aiPos;
        ai.homePosition = aiPos;
        ai.creatureId = creatureId;
        ai.regionId = regionId;
        ai.isHuman = IsHumanSpecies(creatureId);
        ai.isPredator = IsPredatorSpecies(creatureId);
        ai.isMale = ai.isHuman ? (creatureId == 20) : (Random(1.0f) < 0.5f);
        ai.moveSpeed = ai.isHuman ? 2.0f : 1.5f;
        // Start with slightly depleted vitals so NPCs begin working immediately
        // instead of idling for minutes waiting for hunger/thirst to decay.
        ai.hunger = 50.0f + Random(30.0f);     // 50-80: will gather soon
        ai.thirst = 60.0f + Random(30.0f);     // 60-90: will drink soon
        ai.stamina = 80.0f + Random(20.0f);    // 80-100: rested but not full
        ai.warmth = GetEffectiveTemperature();
        ai.currentTask = STASK_IDLE;
        ai.spawnId = spawnId;  // Self-reference for inventory ops (Phase 4)
        ai.stats.strength = Roll4d6DropLowest();
        ai.stats.dexterity = Roll4d6DropLowest();
        ai.stats.constitution = Roll4d6DropLowest();
        ai.stats.intelligence = Roll4d6DropLowest();
        ai.stats.wisdom = Roll4d6DropLowest();
        ai.stats.charisma = Roll4d6DropLowest();
        ai.growthProgress = growthProgress;  // 0.0 = newborn baby, 1.0 = adult
        // Look up species-specific maturity from breeding_rules DB
        if (gameDB_)
        {
            BreedingRules rules;
            if (gameDB_->GetBreedingRules(creatureId, rules) && rules.maturityDays > 0)
                ai.maturityDays = rules.maturityDays;
        }
        // Phase 6: humans get assigned to a shared campfire for night cycle
        // Name + campfire already resolved above for the wire message
        if (ai.isHuman)
        {
            ai.campfireId = AssignCampfireForNPC(pos, regionId);
            ai.settlementId = ai.campfireId;
            ai.npcName = npcName;

            // Born dressed — parents killed something, made hide wrap
            int npcPid = GetNPCPlayerId(spawnId);
            if (npcPid > 0 && worldDB_)
            {
                worldDB_->AddItemToInventory(npcPid, 300, 1, 1, 1.5f, 0, 30.0f, 10);  // Hide Wrap
                worldDB_->EquipItem(npcPid, 300, "body");
                worldDB_->AddItemToInventory(npcPid, 303, 1, 1, 1.0f, 0, 30.0f, 10);  // Hide Boots
                worldDB_->EquipItem(npcPid, 303, "feet");
            }
        }
        creatureAI_[spawnId] = ai;

        URHO3D_LOGINFOF("[CreatureAI] Registered species %d (spawnId %u, %s%s) in region %d at (%.1f, %.1f)",
            creatureId, spawnId,
            ai.isHuman ? "human" : (ai.isPredator ? "predator" : "prey"),
            ai.isHuman ? "" : " animal",
            regionId, pos.x_, pos.z_);
    }

    // Proactive combat state registration: load HP/defense/attack from GameDB at spawn
    // so the server knows creature stats before the first hit. Eliminates lazy-register
    // and enables future non-combat death checks (drowning, age, etc).
    {
        ServerCreatureState cs;
        cs.regionId = regionId;
        cs.position = pos;
        if (LoadCreatureCombat(creatureId, cs))
        {
            creatureStates_[spawnId] = cs;
        }
        else
        {
            // GameDB doesn't know this species — use defaults
            cs.creatureId = creatureId;
            cs.hp = cs.maxHp = 10;
            cs.defense = 10;
            creatureStates_[spawnId] = cs;
        }
    }
}

void AuthServer::SendExistingBuildings(Connection* connection)
{
    if (!worldDB_)
        return;

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        const PlacedBuildingDBInfo& b = buildings[i];
        VectorBuffer buf;
        buf.WriteI32(b.id);
        buf.WriteI32(b.buildingId);
        buf.WriteFloat(b.posX);
        buf.WriteFloat(b.posY);
        buf.WriteFloat(b.posZ);
        buf.WriteFloat(b.rotation);
        buf.WriteI32(b.hp);
        connection->SendMessage(MSG_BUILDING_SPAWN, true, true, buf);

        // Send gate states for open gates
        if (b.gateOpen)
        {
            VectorBuffer gateBuf;
            gateBuf.WriteI32(b.id);
            gateBuf.WriteBool(true);
            connection->SendMessage(MSG_GATE_STATE, true, true, gateBuf);
        }
    }
}

void AuthServer::BuildingDecayTick(float gameDayFraction)
{
    if (!worldDB_ || !gameDB_)
        return;

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        PlacedBuildingDBInfo& b = buildings[i];
        auto typeIt = cachedBuildingTypes_.Find(b.buildingId);
        if (typeIt == cachedBuildingTypes_.End())
            continue;

        const BuildingTypeDBInfo& btype = typeIt->second_;

        // Grace period: no decay until grace_period game days after last repair
        // Tier 1: 3 days, Tier 2: 7 days, Tier 3: 30 days
        int gracePeriod = 3;
        if (btype.tier == 2) gracePeriod = 7;
        else if (btype.tier >= 3) gracePeriod = 30;

        int daysSinceRepair = currentGameDay_ - b.lastRepair;
        if (daysSinceRepair <= gracePeriod)
            continue;

        // Apply decay
        float decay = btype.decayRate * gameDayFraction;

        // Weather bonus damage (rules from gameDB)
        if (!weatherCondition_.Empty())
        {
            float weatherDmg = gameDB_->GetWeatherDamage(weatherCondition_, btype.tier);
            decay += weatherDmg * gameDayFraction;
        }

        int newHp = Max(0, b.hp - (int)(decay + 0.5f));
        if (newHp == b.hp)
            continue;

        if (newHp <= 0)
        {
            // Building collapsed — remove and return 25% salvage to owner
            Vector<BuildingRecipeInput> recipe = gameDB_->GetBuildingRecipe(b.buildingId);
            for (unsigned r = 0; r < recipe.Size(); ++r)
            {
                int salvage = Max(1, recipe[r].quantity / 4);
                AddItemToWorldInventory(b.ownerId, recipe[r].itemId, salvage);
            }

            worldDB_->RemovePlacedBuilding(b.id);
            BroadcastBuildingRemove(b.id);
            LogMessage("Building " + String(b.id) + " (" + btype.name + ") collapsed from decay");
        }
        else
        {
            worldDB_->UpdateBuildingHp(b.id, newHp);
            BroadcastBuildingHp(b.id, newHp);
        }

        // Chicken Coop (88): passive egg production — 1 egg per game day
        if (b.buildingId == 88 && b.hp > 0)
        {
            static constexpr int ITEM_EGG = 940;
            if (Random(1.0f) < gameDayFraction)  // ~1 egg per game day
            {
                AddItemToWorldInventory(b.ownerId, ITEM_EGG, 1);
                URHO3D_LOGDEBUGF("[Farm] Chicken coop %d produced egg for owner %d", b.id, b.ownerId);
            }
        }
    }
}

float AuthServer::GetShelterWarmth(float px, float py, float pz) const
{
    if (!worldDB_)
        return 0.0f;

    float bestWarmth = 0.0f;

    // Check all placed buildings for warmth contribution
    // Shelters: player must be within footprint range
    // Utility (stone ring): warmth within 5m radius
    Vector<PlacedBuildingDBInfo> buildings = const_cast<WorldDB*>(worldDB_.Get())->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        const PlacedBuildingDBInfo& b = buildings[i];
        auto typeIt = cachedBuildingTypes_.Find(b.buildingId);
        if (typeIt == cachedBuildingTypes_.End())
            continue;

        const BuildingTypeDBInfo& btype = typeIt->second_;
        if (btype.warmth <= 0.0f)
            continue;

        float dx = px - b.posX;
        float dz = pz - b.posZ;
        float dist = sqrtf(dx * dx + dz * dz);

        // Shelter buildings: within footprint
        float range = Max(btype.footprintX, btype.footprintZ) * 0.75f;
        // Utility/fireplace: 5m radius
        if (btype.category == "utility")
            range = 5.0f;

        if (dist <= range)
            bestWarmth = Max(bestWarmth, btype.warmth);
    }

    return bestWarmth;
}

void AuthServer::EconomyDailyTick()
{
    lastEconomyDay_ = currentGameDay_;

    if (!gameDB_ || !gameDB_->IsOpen())
        return;

    // 1. Regenerate resources in all regions
    // Query all distinct region IDs from region_resources
    sqlite3_stmt* stmt = nullptr;
    sqlite3* db = gameDB_->GetHandle();
    if (db && sqlite3_prepare_v2(db,
        "SELECT DISTINCT region_id FROM region_resources",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        Vector<int> regionIds;
        while (sqlite3_step(stmt) == SQLITE_ROW)
            regionIds.Push(sqlite3_column_int(stmt, 0));
        sqlite3_finalize(stmt);

        for (unsigned i = 0; i < regionIds.Size(); ++i)
        {
            gameDB_->RegenerateResources(regionIds[i], currentGameDay_);

            // Log resource state after regeneration
            Vector<RegionResourceInfo> pools = gameDB_->GetRegionResources(regionIds[i]);
            for (unsigned p = 0; p < pools.Size(); ++p)
            {
                ResourceTypeInfo rtype;
                if (gameDB_->GetResourceType(pools[p].resourceId, rtype))
                {
                    float scarcity = gameDB_->GetScarcityModifier(regionIds[i], pools[p].resourceId);
                    if (scarcity < 0.5f)
                        URHO3D_LOGWARNINGF("[Economy] Region %d %s depleted: %.0f/%.0f (scarcity %.2f)",
                            regionIds[i], rtype.name.CString(),
                            pools[p].currentAmount, pools[p].maxAmount, scarcity);
                }
            }
        }
    }

    // 2. Population breeding tick
    if (populationManager_ && populationManager_->IsReady())
    {
        // ── Day-Tick Bridge Guard (Death System Phase 1 follow-up) ────────
        // Snapshot the population table INCLUDING killed_today BEFORE calling
        // PopulationManager::DailyTick(), which resets killed_today to 0 as
        // part of the daily reset cycle. The snapshot lets the legacy day-tick
        // breeding loop skip any (region, species) pair that already had kills
        // processed by the Combat Phase 2 / Death System Phase 1 kill-driven
        // accumulator — that path already added replacement births, so the
        // day-tick breed would double-count.
        //
        // DELETE WHEN PHASE 5 LANDS — server tracks all deaths, every cause
        // of death feeds RecordKill, and the legacy day-tick breeding loop
        // becomes pure dead code (the kill-driven path is the only path).
        // Until then, day-tick is the only thing keeping ecosystems alive
        // between idle sessions, so we keep it gated rather than deleted.
        struct BreedCandidate {
            int regionId; int creatureId; int count; int maxCount; int killedToday;
        };
        Vector<BreedCandidate> candidates;
        if (db)
        {
            stmt = nullptr;
            if (sqlite3_prepare_v2(db,
                "SELECT region_id, creature_id, count, max_count, killed_today FROM population",
                -1, &stmt, nullptr) == SQLITE_OK)
            {
                while (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    BreedCandidate c;
                    c.regionId    = sqlite3_column_int(stmt, 0);
                    c.creatureId  = sqlite3_column_int(stmt, 1);
                    c.count       = sqlite3_column_int(stmt, 2);
                    c.maxCount    = sqlite3_column_int(stmt, 3);
                    c.killedToday = sqlite3_column_int(stmt, 4);
                    candidates.Push(c);
                }
                sqlite3_finalize(stmt);
            }
        }

        // Run standard age-death tick — this also resets killed_today to 0,
        // which is why we snapshotted above.
        populationManager_->DailyTick();

        // Breeding: check each species in each region
        if (db)
        {
            int gatedCount = 0;
            for (unsigned i = 0; i < candidates.Size(); ++i)
            {
                const BreedCandidate& c = candidates[i];

                // ── BRIDGE GUARD ──────────────────────────────────────────
                // Skip the legacy day-tick breed if the kill-driven
                // accumulator already processed kills for this pair this
                // day. DELETE WHEN PHASE 5 LANDS — server tracks all deaths.
                if (c.killedToday > 0)
                {
                    ++gatedCount;
                    continue;
                }

                BreedingRules rules;
                if (!gameDB_->GetBreedingRules(c.creatureId, rules))
                    continue;

                // Check minimum population for breeding
                if (c.count < rules.minPopBreed)
                    continue;

                // Check carrying capacity ratio
                if (c.maxCount > 0 && (float)c.count / (float)c.maxCount >= rules.maxPopRatio)
                    continue;

                // Breed on interval: breed if game day is divisible by breed_interval
                if (currentGameDay_ % rules.breedInterval != 0)
                    continue;

                // Seasonal breeding modifier — spring is breeding season,
                // winter is harsh. 0=spring, 1=summer, 2=autumn, 3=winter.
                int season = GetCurrentSeasonIndex();
                float seasonMultiplier = (season == 0) ? 2.0f :   // spring: double births
                                         (season == 1) ? 1.0f :   // summer: normal
                                         (season == 2) ? 0.5f :   // autumn: reduced
                                                         0.25f;   // winter: scarce

                // Overhunt penalty: halve litter if population below 40%
                int litter = Max(1, (int)((float)rules.litterSize * seasonMultiplier));
                float overhuntPenalty = gameDB_->GetEconomicConstant("overhunt_penalty", 0.5f);
                if (c.maxCount > 0 && (float)c.count / (float)c.maxCount < 0.4f)
                    litter = Max(1, (int)((float)litter * overhuntPenalty));

                // Don't exceed max
                int newCount = Min(c.count + litter, c.maxCount);
                int born = newCount - c.count;
                if (born <= 0)
                    continue;

                // Update population table
                sqlite3_stmt* upd = nullptr;
                if (sqlite3_prepare_v2(db,
                    "UPDATE population SET count = ?, born_today = born_today + ? "
                    "WHERE region_id = ? AND creature_id = ?",
                    -1, &upd, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(upd, 1, newCount);
                    sqlite3_bind_int(upd, 2, born);
                    sqlite3_bind_int(upd, 3, c.regionId);
                    sqlite3_bind_int(upd, 4, c.creatureId);
                    sqlite3_step(upd);
                    sqlite3_finalize(upd);
                }
            }
            if (gatedCount > 0)
                LogMessage("[Population] Day-tick bridge guard: skipped " +
                    String(gatedCount) + " (region, species) breed pairs — kill-driven accumulator already handled them");

            // Winter starvation: extra deaths when population > 70% capacity
            int season = GetCurrentSeasonIndex();
            if (season == 3)  // winter
            {
                int winterKills = 0;
                for (unsigned i = 0; i < candidates.Size(); ++i)
                {
                    const BreedCandidate& c = candidates[i];
                    if (c.maxCount <= 0 || c.count <= 0)
                        continue;
                    float ratio = (float)c.count / (float)c.maxCount;
                    if (ratio < 0.7f)
                        continue;
                    // Kill 5% of excess above 70% capacity
                    int excess = c.count - (int)(c.maxCount * 0.7f);
                    int deaths = Max(1, excess / 20);
                    int newCount = Max(0, c.count - deaths);
                    sqlite3_stmt* upd = nullptr;
                    if (sqlite3_prepare_v2(db,
                        "UPDATE population SET count = ?, died_today = died_today + ? "
                        "WHERE region_id = ? AND creature_id = ?",
                        -1, &upd, nullptr) == SQLITE_OK)
                    {
                        sqlite3_bind_int(upd, 1, newCount);
                        sqlite3_bind_int(upd, 2, deaths);
                        sqlite3_bind_int(upd, 3, c.regionId);
                        sqlite3_bind_int(upd, 4, c.creatureId);
                        sqlite3_step(upd);
                        sqlite3_finalize(upd);
                        winterKills += deaths;
                    }
                }
                if (winterKills > 0)
                    LogMessage("[Population] Winter starvation: " + String(winterKills) + " deaths from overpopulation");
            }
        }
    }

    // 3. Update trade values periodically
    gameDB_->UpdateTradeValues(currentGameDay_);

    LogMessage("[Economy] Day " + String(currentGameDay_) + " — regen + breeding + trade values updated");
}

#endif // URHO3D_DATABASE_SQLITE

// ── Server-Authoritative Creature AI (NPC AI Phase 1) ────────────────────

void AuthServer::TickCreatureAI(float dt)
{
    // Phase 6: decay server-side campfire fuel before NPC evaluation
    // Phase 4a: TickCampfires also advances active friction ignitions
    TickCampfires(dt);
    // Phase 5b: age and expire death scent markers
    TickServerScents(dt);
    // Phase 16: tick tamed animal production and cooldowns
    TickTamedAnimals(dt);

    // Diagnostic: log effective temperature once per minute for scrub verification
    static float tempLogTimer = 0.0f;
    tempLogTimer += dt;
    if (tempLogTimer >= 60.0f)
    {
        URHO3D_LOGINFOF("[Temperature] effective=%.1fC BOM=%.1fC scrub=%s",
            GetEffectiveTemperature(), weatherTemperature_,
            (utcEpochOverride_ >= 0 || timeOverrideHour_ >= 0.0f) ? "ON" : "OFF");
        tempLogTimer = 0.0f;
    }

    // Phase 19: periodic soil update — trampling decay + seasonal regrowth
    if (ecosystem_)
    {
        soilTickTimer_ += dt;
        if (soilTickTimer_ >= 10.0f)  // every 10 seconds
        {
            ecosystem_->UpdateSoil();
            soilTickTimer_ = 0.0f;
        }
    }

    // Phase 21: periodic chieftain evaluation (every 30s)
    chieftainEvalTimer_ += dt;
    if (chieftainEvalTimer_ >= 30.0f)
    {
        EvaluateChieftains();
        EvaluateMasters();
        // Update profession assignments from skill levels
        for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        {
            if (it->second_.isHuman)
                it->second_.professionId = DeriveProfession(it->first_);
        }
        chieftainEvalTimer_ = 0.0f;
    }

    // Phase 24: decay signal fire timers
    for (auto sIt = serverCampfires_.Begin(); sIt != serverCampfires_.End(); ++sIt)
    {
        if (sIt->second_.signalTimer > 0.0f)
        {
            sIt->second_.signalTimer -= dt;
            if (sIt->second_.signalTimer <= 0.0f)
            {
                sIt->second_.signalType = ServerCampfire::SIGNAL_NONE;
                sIt->second_.signalTimer = 0.0f;
                URHO3D_LOGINFOF("[Signal] Signal fire expired at campfire %u", sIt->first_);
                BroadcastPitState(sIt->first_, sIt->second_);
            }
        }
    }

    // Phase 23: food decay tick (every 60s)
    foodDecayTimer_ += dt;
    if (foodDecayTimer_ >= 60.0f)
    {
        TickFoodDecay(foodDecayTimer_);
        foodDecayTimer_ = 0.0f;
    }

    // Phase 25: selective breeding tick (every 120s)
    breedingTimer_ += dt;
    if (breedingTimer_ >= 120.0f)
    {
        TickBreeding(breedingTimer_);
        breedingTimer_ = 0.0f;
    }

    // Vitals run on a round timer — rate-based math, accumulated dt
    vitalsTickAccum_ += dt;
    bool vitalsThisTick = (vitalsTickAccum_ >= VITALS_TICK_INTERVAL);
    float vitalsDt = vitalsThisTick ? vitalsTickAccum_ : 0.0f;
    if (vitalsThisTick)
        vitalsTickAccum_ = 0.0f;

    // Fish population recovery — slowly decrement catch pressure counters
    fishRecoveryTimer_ += dt;
    if (fishRecoveryTimer_ >= FISH_RECOVERY_INTERVAL)
    {
        fishRecoveryTimer_ = 0.0f;
        for (auto it = fishCatchPressure_.Begin(); it != fishCatchPressure_.End();)
        {
            it->second_--;
            if (it->second_ <= 0)
                it = fishCatchPressure_.Erase(it);
            else
                ++it;
        }
    }

    Vector<Pair<unsigned, DeathCause>> vitalsDeaths;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;

        // Vital decay — rate-based, runs on round timer (not per-frame)
        if (vitalsThisTick)
        {
            DeathCause vitalsDeath = UpdateCreatureVitals(ai, vitalsDt);
            if (vitalsDeath != DEATH_NONE)
            {
                vitalsDeaths.Push(MakePair(it->first_, vitalsDeath));
                continue;  // skip AI tick for dead creature
            }
        }
        MoveCreature(ai, dt);

        // Phase 3: Hunt sub-state machine (runs every frame while hunting)
        if (ai.currentTask == STASK_HUNT)
            TickHunt(ai, dt);

        // Human-only sub-state machines
        if (ai.isHuman)
        {
            // Phase 5: Defense sub-state machine
            if (ai.currentTask == STASK_DEFEND)
                TickDefense(ai, dt);

            // Torch patrol: advance angle and set next waypoint when near current one
            if ((ai.currentTask == STASK_PATROL || ai.currentTask == STASK_GUARD) && ai.taskTimer > 0.0f)
            {
                auto cfIt = serverCampfires_.Find(ai.campfireId);
                if (cfIt != serverCampfires_.End())
                {
                    Vector3 diff = ai.targetPosition - ai.position;
                    diff.y_ = 0.0f;
                    if (diff.Length() < 3.0f)
                    {
                        // Arrived at waypoint — advance angle
                        ai.patrolAngle += PATROL_WAYPOINT_STEP;
                        if (ai.patrolAngle > 360.0f)
                            ai.patrolAngle -= 360.0f;
                        Vector3 cfPos = cfIt->second_.position;
                        ai.targetPosition.x_ = cfPos.x_ + Cos(ai.patrolAngle) * PATROL_RADIUS;
                        ai.targetPosition.z_ = cfPos.z_ + Sin(ai.patrolAngle) * PATROL_RADIUS;
                        ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                    }
                }

                // Phase 36: Guards actively scan for threats mid-patrol (every frame).
                // Extended detection range. Interrupt patrol to engage.
                if (ai.currentTask == STASK_GUARD)
                {
                    unsigned threatId = FindDefenseTarget(ai, GUARD_DETECTION_RANGE);
                    if (threatId != 0)
                    {
                        ai.prevTask = STASK_GUARD;
                        ai.currentTask = STASK_DEFEND;
                        ai.targetId = threatId;
                        ai.defensePhase = ServerCreatureAI::DEFENSE_ALERT;
                        ai.defenseTimer = 0.0f;
                        ai.state = 16;  // CREATURE_ALERT
                        URHO3D_LOGINFOF("[Guard] NPC %u spotted threat %u — engaging",
                            ai.spawnId, threatId);
                    }
                }
            }

            // Phase 6: tend-fire arrival check
            if (ai.currentTask == STASK_TEND_FIRE && ai.taskTimer > 0.0f)
            {
                auto cfIt = serverCampfires_.Find(ai.campfireId);
                if (cfIt != serverCampfires_.End() &&
                    (ai.position - cfIt->second_.position).Length() < GetTuning("campfire_tend_range", CAMPFIRE_TEND_RANGE))
                {
                    TendCampfire(ai);
                    ai.taskTimer = 0.0f;
                    StartCreatureTask(ai, STASK_SIT_FIRE);
                }
            }

            // Phase 6: warm-home arrival check
            if (ai.currentTask == STASK_WARM && ai.taskTimer > 0.0f)
            {
                if ((ai.position - ai.homePosition).Length() < 2.0f)
                {
                    ai.taskTimer = 0.0f;
                    OnCreatureTaskComplete(ai);
                }
            }

            // Drink arrival check — NPC reached water's edge
            if (ai.currentTask == STASK_DRINK && ai.taskTimer > 0.0f)
            {
                if (ai.position.y_ < AI_WATER_LEVEL + 1.0f)
                {
                    ai.taskTimer = 0.0f;
                    OnCreatureTaskComplete(ai);
                }
            }

            // Fish wading/swim transition — NPC walks into water to fish.
            // Switch to CREATURE_FISH (sitting) when arrived at fishing spot.
            if (ai.currentTask == STASK_FISH && ai.taskTimer > 0.0f)
            {
                float terrainY = GetTerrainHeightAI(ai.position.x_, ai.position.z_);
                float depth = AI_WATER_LEVEL - terrainY;  // positive = submerged

                // Still walking to water — adjust speed based on depth
                if (ai.state == 1)  // CREATURE_WANDER (walking phase)
                {
                    if (depth > 0.0f)
                        ai.moveSpeed = 1.2f;  // slower wading

                    // Arrived at fishing spot — sit and fish
                    Vector3 diff = ai.targetPosition - ai.position;
                    diff.y_ = 0.0f;
                    if (diff.Length() < 2.0f && depth > 0.0f)
                    {
                        ai.state = 20;  // CREATURE_FISH — sit at water's edge
                        ai.moveSpeed = 0.0f;
                    }
                }
                // Already fishing (sitting) — timer counts down normally
            }

            // Phase 6: sleep interrupt (cold/dawn wakes NPC)
            if (ai.currentTask == STASK_SLEEP && ai.taskTimer > 0.0f)
            {
                bool wakeCold = ai.warmth < 25.0f;
                bool wakeDawn = GetDarkness() < 0.4f;
                if (wakeCold || wakeDawn)
                {
                    URHO3D_LOGINFOF("[CreatureAI] NPC spawnId=%u woke early (%s)",
                        ai.spawnId, wakeCold ? "cold" : "dawn");
                    ai.taskTimer = 0.0f;
                    ai.taskDecisionTimer = GetTuning("task_eval_interval", TASK_EVAL_INTERVAL);
                }
            }
        }

        // Task timer countdown — all creatures
        ai.stateTimer += dt;
        if (ai.taskTimer > 0.0f)
        {
            ai.taskTimer -= dt;
            if (ai.taskTimer <= 0.0f)
                OnCreatureTaskComplete(ai);
        }

        // Periodic task re-evaluation — branch by species type
        ai.taskDecisionTimer += dt;
        if (ai.taskDecisionTimer >= GetTuning("task_eval_interval", TASK_EVAL_INTERVAL) && ai.taskTimer <= 0.0f)
        {
            ai.taskDecisionTimer = 0.0f;

            // Orphaned NPC at dusk/night — lost their campfire. Adopt the nearest
            // existing one or build a new one so they don't stand around idling.
            if (ai.campfireId == 0 && ai.isHuman && GetDarkness() > 0.3f)
            {
                unsigned adopted = AssignCampfireForNPC(ai.position, ai.regionId);
                if (adopted != 0)
                {
                    ai.campfireId = adopted;
                    ai.settlementId = adopted;
                    auto cfIt = serverCampfires_.Find(adopted);
                    if (cfIt != serverCampfires_.End())
                        ai.homePosition = cfIt->second_.position;
                }
            }

            int newTask = ai.isHuman ? PickCreatureTask(ai) : PickAnimalTask(ai);

            // Phase 13: FORBID_TASK directive — if the god forbade this task,
            // fall back to IDLE (survival tasks like FLEE/DEFEND/EAT are exempt)
            if (ai.isHuman && ai.directive == ServerCreatureAI::DIRECTIVE_FORBID_TASK &&
                newTask == ai.directiveParam && newTask >= STASK_CRAFT)
                newTask = STASK_IDLE;

            // Only restart if task changed, OR if current task is IDLE (IDLE→IDLE
            // needs a fresh timer and is the normal idle/wander cycle).
            if (newTask != ai.currentTask || newTask == STASK_IDLE)
                StartCreatureTask(ai, newTask);
        }

        // Sync AI state to the scene node — Urho3D replicates Vars + position.
        auto nodeIt = creatureNodes_.Find(it->first_);
        if (nodeIt != creatureNodes_.End() && nodeIt->second_)
        {
            Node* node = nodeIt->second_;
            node->SetPosition(ai.position);
            // Replicate AI state, moveSpeed, and target position as node Vars —
            // clients read these to drive animation and movement lerp.
            node->SetVar("AIState", (int)ai.state);
            node->SetVar("MoveSpeed", ai.moveSpeed);
            node->SetVar("TargetPos", ai.targetPosition);
            if (ai.growthProgress < 1.0f)
                node->SetVar("GrowthProgress", ai.growthProgress);
        }
    }

    // Process vitals deaths (starvation, dehydration, freezing) — deferred to avoid
    // iterator invalidation. Same pattern as drowning deaths below.
    for (unsigned i = 0; i < vitalsDeaths.Size(); ++i)
    {
        unsigned spawnId = vitalsDeaths[i].first_;
        DeathCause cause = vitalsDeaths[i].second_;

        auto aiIt = creatureAI_.Find(spawnId);
        if (aiIt == creatureAI_.End())
            continue;
        const ServerCreatureAI& ai = aiIt->second_;

        auto csIt = creatureStates_.Find(spawnId);
        if (csIt != creatureStates_.End())
        {
            ServerCreatureState& cs = csIt->second_;
            cs.hp = 0;
            BroadcastCreatureDeath(spawnId, cs, nullptr, cause);
            // Spawn replacements for ALL death causes. The population manager's
            // maxCount cap prevents overspawning. Without this, vitals deaths
            // (starvation, freezing, dehydration) during unattended server runtime
            // drive the world to extinction with no recovery path.
            if (populationManager_ && populationManager_->IsReady() && ai.regionId >= 0)
            {
                Vector<ReplacementSpawn> replacements =
                    populationManager_->RecordKill(ai.regionId, ai.creatureId);
                for (unsigned j = 0; j < replacements.Size(); ++j)
                {
                    Vector3 spawnPos = populationManager_->PickSpawnPositionInRegion(replacements[j].regionId);
                    BroadcastSpawnCreature(replacements[j].regionId, replacements[j].creatureId, spawnPos, 0.0f);
                }
            }
            creatureStates_.Erase(csIt);
        }
        creatureAI_.Erase(aiIt);
    }

    // Phase 14: Social bonds + breeding (throttled to 1Hz)
    static float bondTimer = 0.0f;
    bondTimer += dt;
    if (bondTimer >= 1.0f)
    {
        UpdateNPCBonds(bondTimer);
        CheckNPCBreeding();
        CheckSettlementExpansion();
        bondTimer = 0.0f;
    }

    // Death System Phase 5a: server-side non-combat death checks.
    // Collected outside the iterator loop to avoid invalidation.
    Vector<unsigned> drownDeaths;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;
        float terrainY = GetTerrainHeightAI(ai.position.x_, ai.position.z_);
        bool inWater = (terrainY < AI_WATER_LEVEL) && IsLandSpecies(ai.creatureId);

        if (inWater)
        {
            ai.swimTimer += dt;

            // Humans swim until stamina runs out, then get a grace period
            if (ai.isHuman)
            {
                if (ai.stamina <= 0.0f && ai.swimTimer > SWIM_DROWN_GRACE)
                    drownDeaths.Push(it->first_);
            }
            else
            {
                // Non-human land animals: drown after grace period (can't swim)
                if (ai.swimTimer > SWIM_DROWN_GRACE)
                    drownDeaths.Push(it->first_);
            }
        }
        else
        {
            ai.swimTimer = 0.0f;
        }
    }

    for (unsigned i = 0; i < drownDeaths.Size(); ++i)
    {
        unsigned spawnId = drownDeaths[i];
        auto aiIt = creatureAI_.Find(spawnId);
        if (aiIt == creatureAI_.End())
            continue;
        const ServerCreatureAI& ai = aiIt->second_;

        auto csIt = creatureStates_.Find(spawnId);
        if (csIt != creatureStates_.End())
        {
            ServerCreatureState& cs = csIt->second_;
            cs.hp = 0;
            LogMessage("[DROWN] " + cs.species + " #" + String(spawnId) +
                " | Y=" + String(ai.position.y_, 1) +
                " | pos: (" + String((int)ai.position.x_) + "," +
                String((int)ai.position.y_) + "," + String((int)ai.position.z_) + ")"
                " | region: " + String(ai.regionId) +
                " | day: " + String(currentGameDay_));
            BroadcastCreatureDeath(spawnId, cs, nullptr, DEATH_DROWN);
            if (populationManager_ && populationManager_->IsReady() && ai.regionId >= 0)
            {
                Vector<ReplacementSpawn> replacements =
                    populationManager_->RecordKill(ai.regionId, ai.creatureId);
                for (unsigned j = 0; j < replacements.Size(); ++j)
                {
                    Vector3 spawnPos = populationManager_->PickSpawnPositionInRegion(replacements[j].regionId);
                    BroadcastSpawnCreature(replacements[j].regionId, replacements[j].creatureId, spawnPos, 0.0f);
                }
            }
            creatureStates_.Erase(csIt);
        }
        creatureAI_.Erase(aiIt);
    }
}

float AuthServer::GetTerrainHeightAI(float x, float z)
{
    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
    if (!terrain)
        return AI_WATER_LEVEL;
    return terrain->GetHeight(Vector3(x, 0.0f, z));
}

void AuthServer::DiscoverWaterBodies()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    // Skip if already computed
    if (worldDB_->HasWaterBodies(0))
    {
        auto bodies = worldDB_->GetWaterBodies(0);
        int spawnCount = worldDB_->GetTotalFishSpawnCount(0);
        URHO3D_LOGINFOF("[WaterBodies] Already cached: %d bodies, %d spawn points",
            bodies.Size(), spawnCount);
        return;
    }

    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
    if (!terrain)
        return;

    IntVector2 numVerts = terrain->GetNumVertices();
    int width = numVerts.x_;
    int height = numVerts.y_;
    Vector3 spacing = terrain->GetSpacing();
    Vector3 terrainPos = terrain->GetNode()->GetPosition();  // (0, -20, 0)
    float halfSizeX = (width - 1) * spacing.x_ * 0.5f;
    float halfSizeZ = (height - 1) * spacing.z_ * 0.5f;

    URHO3D_LOGINFOF("[WaterBodies] Analyzing %dx%d heightmap...", width, height);

    // Build water mask: true if pixel is below water level
    Vector<bool> isWater(width * height, false);
    Vector<float> depths(width * height, 0.0f);
    int waterCount = 0;
    for (int z = 0; z < height; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            // World position of this heightmap pixel
            float wx = -halfSizeX + x * spacing.x_;
            float wz = -halfSizeZ + z * spacing.z_;
            float wy = terrain->GetHeight(Vector3(wx, 0.0f, wz));
            float depth = AI_WATER_LEVEL - wy;
            if (depth > 0.0f)
            {
                isWater[z * width + x] = true;
                depths[z * width + x] = depth;
                ++waterCount;
            }
        }
    }
    URHO3D_LOGINFOF("[WaterBodies] %d water pixels (%.1f%%)", waterCount,
        100.0f * waterCount / (width * height));

    // Flood-fill to identify connected components
    Vector<int> bodyMap(width * height, -1);  // -1 = unvisited
    int nextBodyId = 0;

    // Per-body accumulators
    struct BodyAccum
    {
        int pixelCount{0};
        float sumX{0.0f}, sumZ{0.0f};
        float minDepth{9999.0f}, maxDepth{0.0f};
    };
    Vector<BodyAccum> bodies;

    // BFS flood fill
    Vector<int> queue;
    for (int z = 0; z < height; ++z)
    {
        for (int x = 0; x < width; ++x)
        {
            int idx = z * width + x;
            if (!isWater[idx] || bodyMap[idx] >= 0)
                continue;

            // Start new body
            int bodyIdx = nextBodyId++;
            bodies.Resize(bodyIdx + 1);
            BodyAccum& acc = bodies[bodyIdx];

            queue.Clear();
            queue.Push(idx);
            bodyMap[idx] = bodyIdx;

            while (!queue.Empty())
            {
                int ci = queue.Back();
                queue.Pop();
                int cx = ci % width;
                int cz = ci / width;

                float wx = -halfSizeX + cx * spacing.x_;
                float wz = -halfSizeZ + cz * spacing.z_;

                acc.pixelCount++;
                acc.sumX += wx;
                acc.sumZ += wz;
                float d = depths[ci];
                if (d < acc.minDepth) acc.minDepth = d;
                if (d > acc.maxDepth) acc.maxDepth = d;

                // 4-connected neighbors
                static const int dx[] = {-1, 1, 0, 0};
                static const int dz[] = {0, 0, -1, 1};
                for (int n = 0; n < 4; ++n)
                {
                    int nx = cx + dx[n];
                    int nz = cz + dz[n];
                    if (nx < 0 || nx >= width || nz < 0 || nz >= height)
                        continue;
                    int ni = nz * width + nx;
                    if (isWater[ni] && bodyMap[ni] < 0)
                    {
                        bodyMap[ni] = bodyIdx;
                        queue.Push(ni);
                    }
                }
            }
        }
    }

    URHO3D_LOGINFOF("[WaterBodies] Found %d disconnected water bodies", bodies.Size());

    // Persist bodies and generate fish spawn points
    float pixelArea = spacing.x_ * spacing.z_;  // area per heightmap pixel in world units²
    int totalSpawns = 0;

    // Begin transaction for bulk inserts
    worldDB_->Execute("BEGIN TRANSACTION");

    for (int b = 0; b < (int)bodies.Size(); ++b)
    {
        const BodyAccum& acc = bodies[b];

        // Skip tiny puddles (< 20 pixels ≈ < 80 m²)
        if (acc.pixelCount < 20)
            continue;

        float cx = acc.sumX / acc.pixelCount;
        float cz = acc.sumZ / acc.pixelCount;
        float areaWorld = acc.pixelCount * pixelArea;

        int dbBodyId = worldDB_->InsertWaterBody(0, acc.pixelCount, areaWorld,
            cx, cz, acc.minDepth, acc.maxDepth);
        if (dbBodyId < 0)
            continue;

        URHO3D_LOGINFOF("[WaterBodies] Body %d: %d pixels (%.0f m²), depth %.1f-%.1f, center (%.0f, %.0f)",
            dbBodyId, acc.pixelCount, areaWorld, acc.minDepth, acc.maxDepth, cx, cz);

        // Generate fish spawn points: grid sample every ~5 pixels (10m), depth > 1m
        int spawnSpacing = 5;  // pixels ≈ 10m
        int bodySpawns = 0;
        for (int z = 0; z < height; z += spawnSpacing)
        {
            for (int x = 0; x < width; x += spawnSpacing)
            {
                int idx = z * width + x;
                if (bodyMap[idx] != b)
                    continue;
                float depth = depths[idx];
                if (depth < 1.0f)
                    continue;  // too shallow for fish

                // Check not on edge (all 4 neighbors must also be water)
                int cx2 = x, cz2 = z;
                bool onEdge = false;
                for (int n = 0; n < 4; ++n)
                {
                    static const int edx[] = {-1, 1, 0, 0};
                    static const int edz[] = {0, 0, -1, 1};
                    int enx = cx2 + edx[n];
                    int enz = cz2 + edz[n];
                    if (enx < 0 || enx >= width || enz < 0 || enz >= height ||
                        !isWater[enz * width + enx])
                    { onEdge = true; break; }
                }
                if (onEdge)
                    continue;

                float wx = -halfSizeX + x * spacing.x_;
                float wz = -halfSizeZ + z * spacing.z_;
                worldDB_->InsertFishSpawnPoint(dbBodyId, wx, wz, depth);
                ++bodySpawns;
            }
        }
        totalSpawns += bodySpawns;
    }

    worldDB_->Execute("COMMIT");

    URHO3D_LOGINFOF("[WaterBodies] Cached %d fish spawn points across all bodies", totalSpawns);
#endif
}

IntVector2 AuthServer::WorldPosToSettlementPatch(const Vector3& pos)
{
    // Terrain: 1025 verts, spacing 2.0, centered at origin → [-1024, 1024]
    // Settlement patch: 128x128 world units, 16x16 grid
    static const float TERRAIN_HALF_SIZE = 1024.0f;
    static const float SPATCH_SIZE = 128.0f;
    int sx = Clamp((int)((pos.x_ + TERRAIN_HALF_SIZE) / SPATCH_SIZE), 0, 15);
    int sz = Clamp((int)((pos.z_ + TERRAIN_HALF_SIZE) / SPATCH_SIZE), 0, 15);
    return IntVector2(sx, sz);
}

void AuthServer::MigrateExistingSettlementPatches()
{
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    // Retroactively claim patches for existing campfire-based settlements
    int migrated = 0;
    for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
    {
        unsigned campfireId = cfIt->first_;
        const Vector3& cfPos = cfIt->second_.position;

        // Check if this settlement already has a patch claim
        int tid, sx, sz;
        if (worldDB_->GetSettlementPatch((int)campfireId, tid, sx, sz))
            continue;  // already claimed

        IntVector2 spatch = WorldPosToSettlementPatch(cfPos);
        if (worldDB_->ClaimSettlementPatch(0, spatch.x_, spatch.y_, (int)campfireId))
        {
            ++migrated;
            URHO3D_LOGINFOF("[Settlement] Migrated campfire %u to patch (%d, %d)",
                campfireId, spatch.x_, spatch.y_);
        }
        else
        {
            // Patch already claimed by another settlement — log conflict
            int existing = worldDB_->GetSettlementAtPatch(0, spatch.x_, spatch.y_);
            URHO3D_LOGWARNINGF("[Settlement] Campfire %u at patch (%d, %d) CONFLICTS with settlement %d",
                campfireId, spatch.x_, spatch.y_, existing);
        }
    }
    if (migrated > 0)
        URHO3D_LOGINFOF("[Settlement] Migrated %d existing settlements to patch claims", migrated);
}

Vector3 AuthServer::PickWanderTarget(const Vector3& center, float radius)
{
    // Random point within radius, above water
    for (int tries = 0; tries < 8; ++tries)
    {
        float angleDeg = Random(0.0f, 360.0f);
        float dist = Random(5.0f, radius);
        float x = center.x_ + Cos(angleDeg) * dist;
        float z = center.z_ + Sin(angleDeg) * dist;
        float y = GetTerrainHeightAI(x, z);
        if (y > AI_WATER_LEVEL + 0.5f)
            return Vector3(x, y, z);
    }
    // Fallback: stay near home
    float y = GetTerrainHeightAI(center.x_, center.z_);
    return Vector3(center.x_, y, center.z_);
}

Vector3 AuthServer::FindWaterEdge(const Vector3& from, float maxRange)
{
    // Sample radial directions, find the nearest point where terrain meets water.
    // Walk outward in each direction until we hit a point just above water level.
    Vector3 best = Vector3::ZERO;
    float bestDist = maxRange + 1.0f;

    for (int angle = 0; angle < 360; angle += 30)
    {
        float dx = Cos((float)angle);
        float dz = Sin((float)angle);
        // Step outward in 3m increments
        for (float r = 3.0f; r <= maxRange; r += 3.0f)
        {
            float x = from.x_ + dx * r;
            float z = from.z_ + dz * r;
            float y = GetTerrainHeightAI(x, z);
            // Water's edge: just above to just below water level
            if (y < AI_WATER_LEVEL + 1.0f && y > AI_WATER_LEVEL - 2.0f)
            {
                float dist = r;
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best = Vector3(x, y, z);
                }
                break; // Found water in this direction, try next angle
            }
        }
    }

    // Water Phase 4: if no natural water found, try wells as fallback
    if (best == Vector3::ZERO && worldDB_)
    {
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned b = 0; b < buildings.Size(); ++b)
        {
            if (buildings[b].buildingId == BUILDING_WELL)
            {
                float dx = from.x_ - buildings[b].posX;
                float dz = from.z_ - buildings[b].posZ;
                float dist = sqrtf(dx * dx + dz * dz);
                if (dist <= maxRange && dist < bestDist)
                {
                    bestDist = dist;
                    best = Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                }
            }
        }
    }

    return best;
}

AuthServer::DeathCause AuthServer::UpdateCreatureVitals(ServerCreatureAI& ai, float dt)
{
    // Hunger: steady drain
    ai.hunger = Max(0.0f, ai.hunger - GetTuning("hunger_decay_rate", 0.15f) * dt);

    // Thirst: faster drain — accelerated during drought (+50% at full severity)
    float thirstRate = GetTuning("thirst_decay_rate", 0.20f) * (1.0f + droughtSeverity_ * 0.5f);
    ai.thirst = Max(0.0f, ai.thirst - thirstRate * dt);

    // Stamina: drains when active, recovers when resting/sleeping
    bool resting = (ai.currentTask == STASK_IDLE || ai.currentTask == STASK_SIT_FIRE
                    || ai.currentTask == STASK_SLEEP);
    if (resting)
        ai.stamina = Min(100.0f, ai.stamina + GetTuning("stamina_regen_rest", 0.30f) * dt);
    else
        ai.stamina = Max(0.0f, ai.stamina - GetTuning("stamina_drain_active", 0.10f) * dt);

    // Warmth: base from weather, shelter bonus, clothing bonus
    // Night penalty: additional drain proportional to darkness (Phase 6)
    float darkness = GetDarkness();
    float shelterWarmth = GetShelterWarmth(ai.position.x_, ai.position.y_, ai.position.z_);

    // Clothing warmth — sum equipped items' warmth values from clothing_warmth table
    float clothingWarmth = 0.0f;
    if (ai.isHuman && worldDB_ && gameDB_)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0)
        {
            static const char* warmSlots[] = {"body", "back", "feet", "head"};
            for (int ws = 0; ws < 4; ++ws)
            {
                int itemId = worldDB_->GetEquippedItem(npcPid, warmSlots[ws]);
                if (itemId > 0)
                    clothingWarmth += gameDB_->GetClothingWarmth(itemId);
            }
        }
    }

    ai.warmth = GetEffectiveTemperature() + shelterWarmth + clothingWarmth;
    float nightDrain = warmthRulesLoaded_ ? warmthRules_.nightMultiplier * 0.05f : 0.40f;
    ai.warmth -= darkness * nightDrain * dt;  // Night warmth drain from DB (night_multiplier * base rate)

    // Sleeping near home provides warmth bonus — but ONLY when the shared
    // campfire is actually alive (Phase 6). A dead fire = NPC eventually wakes.
    if (ai.currentTask == STASK_SIT_FIRE || ai.currentTask == STASK_SLEEP)
    {
        float distHome = (ai.position - ai.homePosition).Length();
        if (distHome < 10.0f)
        {
            bool fireAlive = false;
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End() && cfIt->second_.fuelSeconds > 0.0f)
                fireAlive = true;
            if (fireAlive)
                ai.warmth = Min(100.0f, ai.warmth + 15.0f);

            // Bedroll bonus: NPC sleeping near fire with a bedroll gets extra warmth
            if (ai.currentTask == STASK_SLEEP)
            {
                int npcPid = GetNPCPlayerId(ai.spawnId);
                if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, 893) > 0)
                    ai.warmth = Min(100.0f, ai.warmth + 8.0f);
            }
        }
    }
    ai.warmth = Clamp(ai.warmth, 0.0f, 100.0f);

    // Phase 12: Vitals consequences — HP damage from critical conditions
    auto csIt = creatureStates_.Find(ai.spawnId);
    if (ai.isHuman && csIt != creatureStates_.End() && csIt->second_.hp > 0)
    {
        ServerCreatureState& cs = csIt->second_;
        float dmgInterval = GetTuning("vitals_dmg_interval", 5.0f);

        // Starvation: hunger == 0 → 1 HP per interval
        if (ai.hunger <= 0.0f)
        {
            ai.starveDmgTimer += dt;
            if (ai.starveDmgTimer >= dmgInterval)
            {
                ai.starveDmgTimer -= dmgInterval;
                cs.hp = Max(0, cs.hp - 1);
                if (cs.hp <= 0)
                    return DEATH_STARVE;
            }
        }
        else
            ai.starveDmgTimer = 0.0f;

        // Dehydration: thirst == 0 → 1 HP per interval
        if (ai.thirst <= 0.0f)
        {
            ai.thirstDmgTimer += dt;
            if (ai.thirstDmgTimer >= dmgInterval)
            {
                ai.thirstDmgTimer -= dmgInterval;
                cs.hp = Max(0, cs.hp - 1);
                if (cs.hp <= 0)
                    return DEATH_DEHYDRATE;
            }
        }
        else
            ai.thirstDmgTimer = 0.0f;

        // Hypothermia: warmth below threshold + no fire/shelter nearby → 1 HP per interval
        float coldThreshold = warmthRulesLoaded_ ? (float)warmthRules_.lowThreshold : 5.0f;
        if (ai.warmth < coldThreshold)
        {
            bool hasProtection = false;
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End() && cfIt->second_.fuelSeconds > 0.0f &&
                (ai.position - cfIt->second_.position).Length() < 10.0f)
                hasProtection = true;

            if (!hasProtection)
            {
                ai.coldDmgTimer += dt;
                if (ai.coldDmgTimer >= dmgInterval)
                {
                    ai.coldDmgTimer -= dmgInterval;
                    cs.hp = Max(0, cs.hp - 1);
                    if (cs.hp <= 0)
                        return DEATH_FREEZE;
                }
            }
            else
                ai.coldDmgTimer = 0.0f;
        }
        else
            ai.coldDmgTimer = 0.0f;

        // Exhaustion: stamina < 10 → halve move speed, double task timer
        if (ai.stamina < 10.0f)
        {
            if (!ai.exhaustionApplied)
            {
                ai.moveSpeed *= 0.5f;
                ai.taskTimer *= 2.0f;
                ai.exhaustionApplied = true;
            }
        }
        else
            ai.exhaustionApplied = false;

        // Master Chef: food poisoning tick — speed penalty + timer countdown
        if (ai.illnessTimer > 0.0f)
        {
            ai.illnessTimer -= dt;
            if (ai.illnessTimer <= 0.0f)
            {
                ai.illnessTimer = 0.0f;
                ai.illnessActive = false;
            }
            else if (!ai.illnessActive)
            {
                ai.illnessActive = true;
            }
            // -20% speed applied via moveSpeed scaling in MoveCreature
        }

        // Phase 27: herbalist settlement bonus — +5% max HP regen per vitals tick
        if (cs.hp < cs.maxHp)
        {
            bool hasHerbalist = false;
            for (auto hbIt = creatureAI_.Begin(); hbIt != creatureAI_.End(); ++hbIt)
            {
                if (!hbIt->second_.isHuman || hbIt->second_.settlementId != ai.settlementId)
                    continue;
                if (GetNPCSkillLevel(hbIt->second_.spawnId, SKILL_HERBALISM) >= 3)
                {
                    int hbPid = GetNPCPlayerId(hbIt->second_.spawnId);
                    if (hbPid > 0 && worldDB_ && worldDB_->GetItemCount(hbPid, ITEM_MEDICINAL_HERBS) > 0)
                    { hasHerbalist = true; break; }
                }
            }
            if (hasHerbalist)
            {
                int regenPct = 5;  // base herbalist bonus
                if (HasMasterHerbalist(ai.campfireId))
                    regenPct += 10;  // Master Herbalist stacks
                cs.hp = Min(cs.maxHp, cs.hp + Max(1, cs.maxHp * regenPct / 100));
            }
        }
    }

    // Phase 29: Morale — compute from conditions, affects task speed
    if (ai.isHuman)
    {
        float targetMorale = 50.0f;  // base
        if (ai.hunger > 60.0f) targetMorale += 10.0f;       // fed
        if (ai.hunger < 20.0f) targetMorale -= 10.0f;       // hungry
        if (ai.warmth > 50.0f) targetMorale += 10.0f;       // warm (near fire/shelter)
        if (ai.warmth < 20.0f) targetMorale -= 15.0f;       // cold
        // Shelter: near campfire with fuel
        auto cfMorale = serverCampfires_.Find(ai.campfireId);
        if (cfMorale != serverCampfires_.End() && cfMorale->second_.fuelSeconds > 0.0f &&
            (ai.position - cfMorale->second_.position).Length() < 15.0f)
            targetMorale += 10.0f;
        // Music boost active
        if (ai.musicBoostTimer > 0.0f)
        {
            targetMorale += 5.0f;
            ai.musicBoostTimer -= dt;
        }
        // Clothing: +5 if wearing anything in body/feet/head slots
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0 && worldDB_)
            {
                Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);
                for (unsigned ci = 0; ci < inv.Size(); ++ci)
                {
                    if (inv[ci].slotType == "body" || inv[ci].slotType == "feet" || inv[ci].slotType == "head")
                    { targetMorale += 5.0f; break; }
                }
            }
        }
        // Death witnessed: -20 if a settlement NPC died recently (scent within 300s)
        {
            for (int si = 0; si < (int)serverScents_.Size(); ++si)
            {
                if (serverScents_[si].ageSeconds < 300.0f &&
                    IsHumanSpecies(serverScents_[si].speciesId) &&
                    (serverScents_[si].position - ai.homePosition).Length() < 30.0f)
                { targetMorale -= 20.0f; break; }
            }
        }
        // Food poisoning morale penalty
        if (ai.illnessActive)
            targetMorale -= 10.0f;
        // Banner morale bonus: any NPC at a settlement with a placed banner gets +2
        if (ai.campfireId != 0 && worldDB_)
        {
            // Check if any NPC at this settlement owns a banner (item 894)
            for (auto bIt = creatureAI_.Begin(); bIt != creatureAI_.End(); ++bIt)
            {
                if (bIt->second_.campfireId == ai.campfireId && bIt->second_.isHuman)
                {
                    int bPid = GetNPCPlayerId(bIt->first_);
                    if (bPid > 0 && worldDB_->GetItemCount(bPid, 894) > 0)
                    { targetMorale += 2.0f; break; }
                }
            }
            // Tapestry morale bonus: +5 if any NPC at settlement owns a tapestry (item 896)
            for (auto tIt = creatureAI_.Begin(); tIt != creatureAI_.End(); ++tIt)
            {
                if (tIt->second_.campfireId == ai.campfireId && tIt->second_.isHuman)
                {
                    int tPid = GetNPCPlayerId(tIt->first_);
                    if (tPid > 0 && worldDB_->GetItemCount(tPid, ITEM_TAPESTRY) > 0)
                    { targetMorale += 5.0f; break; }
                }
            }
            // Guard dog morale bonus: +2 per tamed dog at settlement (max +6)
            {
                int guardDogs = 0;
                for (auto dIt = creatureAI_.Begin(); dIt != creatureAI_.End(); ++dIt)
                {
                    if (dIt->second_.tamerId != 0 && dIt->second_.campfireId == ai.campfireId &&
                        (dIt->second_.creatureId == 12 || dIt->second_.creatureId == 13))
                        ++guardDogs;
                }
                targetMorale += Min(guardDogs * 2.0f, 6.0f);
            }
            // Companion ShibaInu morale bonus: +3 if a tamed ShibaInu is within 10m of this NPC
            for (auto dIt = creatureAI_.Begin(); dIt != creatureAI_.End(); ++dIt)
            {
                if (dIt->second_.tamerId == ai.spawnId && dIt->second_.creatureId == 13)
                {
                    float dist = (dIt->second_.position - ai.position).Length();
                    if (dist < 10.0f)
                    { targetMorale += 3.0f; break; }
                }
            }
            // Grave morale bonus: +3 if settlement has graves (respect for the dead)
            {
                Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
                for (unsigned gi = 0; gi < buildings.Size(); ++gi)
                {
                    if (buildings[gi].buildingId == 60 &&
                        (Vector3(buildings[gi].posX, buildings[gi].posY, buildings[gi].posZ) - ai.homePosition).Length() < 40.0f)
                    { targetMorale += 3.0f; break; }
                }
            }
        }
        // Clamp and lerp toward target
        targetMorale = Clamp(targetMorale, 0.0f, 100.0f);
        ai.morale = Lerp(ai.morale, targetMorale, Min(1.0f, dt * 0.1f));
    }

    // Growth — all creatures grow from baby to adult over maturityDays game days.
    // Rate comes from breeding_rules DB (default 20 for humans, varies per species).
    if (ai.growthProgress < 1.0f)
    {
        float prevGrowth = ai.growthProgress;
        float growthRate = dt / ((float)ai.maturityDays * 300.0f);
        ai.growthProgress = Min(1.0f, ai.growthProgress + growthRate);

        // Phase 30: assign name at maturity (humans only)
        if (ai.isHuman && prevGrowth < 1.0f && ai.growthProgress >= 1.0f && ai.npcName.Empty())
        {
            ai.npcName = GenerateNPCName(ai.campfireId);
            URHO3D_LOGINFOF("[Naming] NPC %u named '%s' at maturity (campfire %u)",
                ai.spawnId, ai.npcName.CString(), ai.campfireId);
        }

        // Scale HP with growth
        auto csGrowIt = creatureStates_.Find(ai.spawnId);
        if (csGrowIt != creatureStates_.End())
            csGrowIt->second_.maxHp = 10 + (int)(10.0f * ai.growthProgress);
    }

    return DEATH_NONE;
}

void AuthServer::MoveCreature(ServerCreatureAI& ai, float dt)
{
    Vector3 diff = ai.targetPosition - ai.position;
    diff.y_ = 0.0f; // Horizontal only
    float dist = diff.Length();

    if (dist < 1.5f)
        return; // Arrived

    // Walk toward target
    Vector3 dir = diff / dist;
    float speed = ai.moveSpeed;

    // Phase 19: 20% speed bonus on established paths (trampling >= 128)
    if (ecosystem_)
    {
        unsigned char trampling = ecosystem_->SampleTrampling(ai.position.x_, ai.position.z_);
        if (trampling >= 128)
            speed *= 1.2f;
    }

    // Horse mount: +50% speed on long-distance tasks when settlement has tamed horse
    if (ai.isHuman && (ai.currentTask == STASK_TRADE_ROUTE || ai.currentTask == STASK_CARRY_FIRE ||
        ai.currentTask == STASK_CARRY_WATER || ai.currentTask == STASK_DELIVER_FIRE))
    {
        for (auto hIt = creatureAI_.Begin(); hIt != creatureAI_.End(); ++hIt)
        {
            if (hIt->second_.tamerId != 0 && hIt->second_.creatureId == 10 &&
                hIt->second_.campfireId == ai.campfireId)
            { speed *= 1.5f; break; }
        }
    }

    // Food poisoning: -20% speed
    if (ai.illnessActive)
        speed *= 0.8f;

    // Determine terrain height at new position before moving
    float nextTerrainY = GetTerrainHeightAI(
        ai.position.x_ + dir.x_ * speed * dt,
        ai.position.z_ + dir.z_ * speed * dt);
    bool enteringWater = (nextTerrainY < AI_WATER_LEVEL);

    // Humans can swim across water. Land animals cannot — they bounce at the edge.
    if (enteringWater && !ai.isHuman && IsLandSpecies(ai.creatureId))
    {
        // Land animal: abort movement, pick a new target on dry land
        ai.targetPosition = ai.position;
        ai.position.y_ = Max(ai.position.y_, AI_WATER_LEVEL + 0.3f);
        return;
    }

    // Swimming: 60% speed penalty, no path bonus
    if (enteringWater)
        speed *= 0.6f;

    float step = speed * dt;
    if (step > dist)
        step = dist;

    ai.position.x_ += dir.x_ * step;
    ai.position.z_ += dir.z_ * step;

    // Snap Y: float at water surface when swimming, terrain height otherwise
    float terrainY = GetTerrainHeightAI(ai.position.x_, ai.position.z_);
    bool inWater = (terrainY < AI_WATER_LEVEL);

    if (inWater)
    {
        ai.position.y_ = AI_WATER_LEVEL;
        ai.state = 10;  // CREATURE_SWIM

        // Swim stamina drain — faster than normal active drain
        ai.stamina = Max(0.0f, ai.stamina - SWIM_STAMINA_DRAIN_RATE * dt);
    }
    else
    {
        ai.position.y_ = terrainY;

        // Phase 19: record footstep for worn path accumulation (land only)
        if (ecosystem_)
            ecosystem_->RecordFootstep(ai.position.x_, ai.position.z_);
    }

    // Water-edge tasks (DRINK, FISH) still approach water but don't need to swim
    if (!inWater && (ai.currentTask == STASK_DRINK || ai.currentTask == STASK_FISH) &&
        terrainY < AI_WATER_LEVEL + 0.3f)
    {
        // Allow standing at edge without bouncing
    }
}

int AuthServer::PickCreatureTask(const ServerCreatureAI& ai)
{
    // GPU/CPU compute pre-evaluation: if the priority shader already determined
    // a clear survival winner, use it as a fast-path hint for vitals-driven tasks.
    // Combat/spatial decisions (FLEE, DEFEND, HEAL) still need CPU validation below.
    auto gpuIt = gpuPriorityCache_.Find(ai.spawnId);
    int gpuHint = (gpuIt != gpuPriorityCache_.End()) ? gpuIt->second_ : 0;

    // Priority-based task selection matching the plan's priority list.

    // 1. FLEE — human flees when HP critical (<30%) or unarmed facing a predator.
    // Takes priority over DEFEND when dying — survival over heroics.
    auto csFleeIt = creatureStates_.Find(ai.spawnId);
    if (ai.isHuman && csFleeIt != creatureStates_.End() && csFleeIt->second_.hp > 0)
    {
        float hpFraction = (float)csFleeIt->second_.hp / Max(1.0f, (float)csFleeIt->second_.maxHp);
        if (hpFraction < 0.3f)
        {
            // Check for nearby threat to flee from
            unsigned threatId = FindDefenseTarget(ai);
            if (threatId != 0)
                return STASK_FLEE;
        }
    }

    // 2. DEFEND — predator threatens the campfire (Phase 5). Humans only.
    // Checked ahead of hunger so starving NPCs still drop food to fight a wolf.
    if (ai.isHuman)
    {
        unsigned threatId = FindDefenseTarget(ai);
        if (threatId != 0)
        {
            // Phase 28: track predator attacks for fortification trigger
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End())
                cfIt->second_.predatorAttackCount++;
            return STASK_DEFEND;
        }
    }

    // 2a. HEAL — has herbs, nearby NPC injured (HP < 70%). Skill affects success, not access.
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_MEDICINAL_HERBS) > 0)
        {
            if (FindInjuredNPC(ai) != 0)
                return STASK_HEAL;
        }
    }

    // 2b. COOK — has raw meat + fire lit nearby (better nutrition than raw eat)
    if (ai.isHuman && ai.hunger < GetTuning("hunger_eat_inv_threshold", 60.0f))
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            // Check for raw meat in inventory (item categories aren't ideal — check by name)
            Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);
            bool hasRawMeat = false;
            for (unsigned i = 0; i < inv.Size(); ++i)
            {
                if (inv[i].slotType == "bag" && inv[i].itemId > 0 && gameDB_)
                {
                    ItemInfo item;
                    if (gameDB_->GetItem(inv[i].itemId, item) &&
                        item.category == "food" && item.name.Contains("Raw"))
                    {
                        hasRawMeat = true;
                        break;
                    }
                }
            }
            if (hasRawMeat)
            {
                // Check if near a LIT campfire
                for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
                {
                    if (cfIt->second_.state == PIT_LIT &&
                        (ai.position - cfIt->second_.position).Length() < 10.0f)
                    {
                        return STASK_COOK;
                    }
                }
            }
        }
    }

    // 3. DRINK or EAT — whichever is more urgent (lower value = more critical)
    {
        bool thirsty = ai.thirst < GetTuning("thirst_drink_threshold", 50.0f);
        bool hungry = ai.hunger < GetTuning("hunger_eat_inv_threshold", 60.0f);

        // Drink first if thirst is more critical than hunger
        if (thirsty && (!hungry || ai.thirst <= ai.hunger))
        {
            if (ai.isHuman && ai.vesselContents == ServerCreatureAI::VESSEL_WATER)
                return STASK_DRINK;

            if (ai.isHuman && ai.campfireId != 0)
            {
                auto cfIt = serverCampfires_.Find(ai.campfireId);
                if (cfIt != serverCampfires_.End() && cfIt->second_.waterReserve >= 1.0f)
                    return STASK_DRINK;
            }

            if (ai.isHuman)
            {
                int npcPid = GetNPCPlayerId(ai.spawnId);
                if (npcPid > 0 && worldDB_)
                {
                    bool hasVessel = (worldDB_->GetItemCount(npcPid, ITEM_BARK_VESSEL) > 0);
                    bool vesselEmpty = (ai.vesselContents == ServerCreatureAI::VESSEL_EMPTY);
                    if (hasVessel && vesselEmpty)
                        return STASK_FETCH_WATER;
                    if (worldDB_->GetItemCount(npcPid, ITEM_CLAY_POT) > 0)
                        return STASK_FETCH_WATER;
                }
            }
            return STASK_DRINK;
        }
    }

    // 4. EAT FROM INVENTORY — hungry AND has real food (Phase 4)
    if (ai.hunger < GetTuning("hunger_eat_inv_threshold", 60.0f))
    {
        int npcPlayerId = npcPlayerIds_.Find(ai.spawnId) != npcPlayerIds_.End()
            ? npcPlayerIds_[ai.spawnId] : 0;
        if (npcPlayerId > 0 && FindFoodInNPCInventory(npcPlayerId) > 0)
            return STASK_EAT_FROM_INVENTORY;
    }

    // 3. EAT — abstract fallback (no inventory food, very hungry)
    if (ai.hunger < GetTuning("hunger_eat_abstract_threshold", 25.0f))
        return STASK_EAT;

    // 4. HUNT — hunger moderate, go kill something (Phase 3)
    // Skilled NPCs also hunt foxes/wolves for pelts (target selected in StartCreatureTask)
    if (ai.hunger < GetTuning("hunger_hunt_threshold", 50.0f))
        return STASK_HUNT;

    // 4a. COLLECT TRAP — owned trap has triggered, prey is waiting (Phase 4)
    {
        int npcPlayerId = npcPlayerIds_.Find(ai.spawnId) != npcPlayerIds_.End()
            ? npcPlayerIds_[ai.spawnId] : 0;
        if (npcPlayerId > 0 && FindTriggeredTrapOwnedBy(npcPlayerId) > 0)
            return STASK_TRAP_COLLECT;
    }

    // 4b. PLACE TRAP — has a trap in inventory and not desperate (Phase 4)
    if (ai.hunger < GetTuning("hunger_trap_threshold", 70.0f))
    {
        int npcPlayerId = npcPlayerIds_.Find(ai.spawnId) != npcPlayerIds_.End()
            ? npcPlayerIds_[ai.spawnId] : 0;
        if (npcPlayerId > 0 && FindTrapInNPCInventory(npcPlayerId) > 0)
            return STASK_TRAP_PLACE;
    }

    // 4. WARM — warmth critical
    if (ai.warmth < GetTuning("warmth_critical_threshold", 25.0f))
        return STASK_WARM;

    // (DRINK moved above EAT — dehydration kills faster than starvation)

    // Phase 27: HEAL — has herbs, injured settlement NPC (HP < 70%). Skill affects success.
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_MEDICINAL_HERBS) > 0)
        {
            for (auto hIt = creatureAI_.Begin(); hIt != creatureAI_.End(); ++hIt)
            {
                if (!hIt->second_.isHuman || hIt->first_ == ai.spawnId)
                    continue;
                if (hIt->second_.settlementId != ai.settlementId)
                    continue;
                auto csIt = creatureStates_.Find(hIt->first_);
                if (csIt != creatureStates_.End() && csIt->second_.hp > 0 &&
                    csIt->second_.hp < csIt->second_.maxHp * 7 / 10)
                    return STASK_HEAL;
            }
        }
    }

    // Phase 6: Night-fire-sleep cycle — darkness-driven priorities
    float darkness = GetDarkness();

    // TEND_FIRE — shared campfire is dying and NPC is close enough to add wood.
    // Ahead of SIT/SLEEP so the NPC tops off the fire before settling in.
    if (ai.campfireId != 0)
    {
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            const ServerCampfire& cf = cfIt->second_;
            float dist = (ai.position - cf.position).Length();
            // Phase 4a/5: COLD/UNLIT pit needs friction ignition — but only if NPC has wood
            if ((cf.state == PIT_COLD || cf.state == PIT_UNLIT) && !cf.ignitionActive && dist < 30.0f)
            {
                if (NPCHasFirewood(ai.spawnId))
                    return STASK_MAKE_FIRE;
                else
                    return STASK_GATHER_WOOD; // Go get wood first
            }
            // Phase 5: tend requires softwood in inventory
            if (cf.fuelSeconds < GetTuning("campfire_tend_threshold", CAMPFIRE_TEND_THRESHOLD) && dist < 30.0f)
            {
                if (NPCHasSoftwood(ai.spawnId))
                    return STASK_TEND_FIRE;
                else
                    return STASK_GATHER_WOOD; // Go get wood first
            }
        }
    }

    // 4c. TORCH — night, has unlit torch in inventory, fire nearby LIT
    if (ai.isHuman && darkness > 0.5f)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_TORCH) >= 1)
        {
            for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
            {
                if (cfIt->second_.state == PIT_LIT &&
                    (ai.position - cfIt->second_.position).Length() < 10.0f)
                {
                    return STASK_TORCH;
                }
            }
        }
    }

    // 5. SLEEP — stamina critical OR nighttime near home
    if (ai.stamina < GetTuning("stamina_critical_threshold", 20.0f))
        return STASK_SLEEP;

    // 5a. NIGHT FISHING — hungry NPC with a rod fishes before sleeping.
    // Conditions: night (darkness > 0.6), hunger < 50, has fishing rod (item 105),
    // water within 15m. Preferred over sleep when the tribe needs food.
    if (ai.isHuman && darkness > 0.6f && ai.hunger < 50.0f)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, 105) > 0)
        {
            Vector3 water = FindWaterEdge(ai.position, 15.0f);
            if (water != Vector3::ZERO)
                return STASK_FISH;
        }
    }

    if (darkness > GetTuning("darkness_deep_night", 0.80f))
    {
        // Deep night: prefer sleep if near campfire, or walk home first
        float distHome = (ai.position - ai.homePosition).Length();
        if (distHome < 10.0f)
            return (Random(1.0f) < 0.6f) ? STASK_SLEEP : STASK_SIT_FIRE;
        return STASK_WARM; // Walk home
    }

    // Dusk: head home early, or settle in if already there
    if (darkness > GetTuning("darkness_dusk", 0.60f))
    {
        float distHome = (ai.position - ai.homePosition).Length();
        if (distHome > 15.0f)
            return STASK_WARM; // Walk home before dark
        if (distHome < 10.0f)
            return STASK_SIT_FIRE; // Already home — settle in for the night
    }

    // GPU priority hint: if compute shader identified a clear work-category winner
    // and we've passed all survival checks above without returning, trust the hint
    // for non-combat low-urgency tasks to skip expensive inventory/spatial scans.
    if (gpuHint == STASK_CRAFT && ai.hunger >= 60.0f && ai.thirst >= 50.0f &&
        ai.warmth >= 25.0f && ai.stamina >= 30.0f && GetDarkness() < 0.6f)
    {
        if (ai.isHuman && ai.growthProgress >= 1.0f && NPCFindCraftableRecipe(ai) >= 0)
            return STASK_CRAFT;
    }

    // 6. GATHER — resource within range (simplified: periodic need)
    if (ai.hunger < GetTuning("hunger_gather_threshold", 60.0f))
        return STASK_GATHER;

    // ── Child behavior gate (Family Life Phase 3) ──────────────────────
    // Children can't craft, build, fight, mine, trade, or do skilled work.
    // Young children (< 0.5) only survive: wander near parent, eat, drink, sleep.
    // Older children (0.5–1.0) can also gather and eat from inventory.
    // All survival tasks above this point (flee, defend, eat, drink, sleep, fire)
    // are already handled — if we reach here, no survival need is pressing.
    if (ai.isHuman && ai.growthProgress < 1.0f)
    {
        // Older children can gather
        if (ai.growthProgress >= 0.5f && ai.hunger < 60.0f)
            return STASK_GATHER;

        // Follow nearest parent — bias wander toward parent position
        Vector3 followTarget = ai.homePosition;
        if (ai.parentA != 0 || ai.parentB != 0)
        {
            unsigned parentId = ai.parentA != 0 ? ai.parentA : ai.parentB;
            auto parentIt = creatureAI_.Find(parentId);
            if (parentIt != creatureAI_.End())
                followTarget = parentIt->second_.position;
            else if (ai.parentB != 0 && ai.parentA != 0)
            {
                // First parent dead, try second
                parentIt = creatureAI_.Find(ai.parentB);
                if (parentIt != creatureAI_.End())
                    followTarget = parentIt->second_.position;
            }
        }

        // Wander radius scales: 5m at birth → 10m at 0.5 → full at 1.0
        float childWander = 5.0f + 15.0f * ai.growthProgress;
        float distToParent = (ai.position - followTarget).Length();
        if (distToParent > childWander)
            return STASK_WARM;  // walk toward parent/home

        return STASK_IDLE;  // stay near parent, play
    }

    // ── Profession Schedule (Phase 34b) ────────────────────────────────────
    // If NPC has a profession (skill level 3+ in a profession's primary skill),
    // use the profession's daily schedule to pick the task for this hour.
    if (ai.isHuman && !professionSchedule_.Empty())
    {
        int profId = ai.professionId;
        if (profId == 0)
            profId = DeriveProfession(ai.spawnId);

        if (profId > 0)
        {
            int hour = (int)GetGameHour() % 24;
            int scheduled = GetScheduledTask(profId, hour);
            if (scheduled >= 0)
                return scheduled;
        }
    }

    // ── Division of Labor (Phase 11) ─────────────────────────────────────
    // Fallback when no profession schedule applies: NPC gravitates toward their
    // emerging specialty. Even level 2 creates a bias — you don't need mastery to
    // prefer what you're getting good at. The d20 check on completion handles failure.
    if (ai.isHuman)
    {
        struct SkillTask { int skill; int task; };
        static const SkillTask skillMap[] = {
            { SKILL_FARMING,     STASK_FARM_HARVEST },
            { SKILL_FARMING,     STASK_FARM_PLANT },
            { SKILL_FISHING,     STASK_FISH },
            { SKILL_WOODWORK,    STASK_CHOP },
            { SKILL_WOODWORK,    STASK_BUILD },
            { SKILL_WOODWORK,    STASK_REPAIR },
            { SKILL_KNAPPING,    STASK_CRAFT },
            { SKILL_KNAPPING,    STASK_MINE },
            { SKILL_SMELTING,    STASK_SMELT },
            { SKILL_FIREMAKING,  STASK_BURN_CHARCOAL },
            { SKILL_FIREMAKING,  STASK_PATROL },
            { SKILL_LEATHERWORK, STASK_DRESS },
            { SKILL_COOKING,     STASK_COOK },
            { SKILL_HERBALISM,   STASK_GATHER_HERBS },
            { SKILL_HERBALISM,   STASK_TAP_TREE },
            { SKILL_HERBALISM,   STASK_STRIP_BARK },
            { SKILL_TRACKING,    STASK_SCAVENGE },
            { SKILL_TRADE,       STASK_TRADE },
            { SKILL_ANIMAL_LORE, STASK_TAME },
            { SKILL_ANIMAL_LORE, STASK_SHEAR },
            { SKILL_ANIMAL_LORE, STASK_HERD },
            { SKILL_WEAVING,     STASK_WEAVE },
            { SKILL_MELEE,       STASK_GUARD },
        };

        // Find NPC's highest skill
        int bestSkill = -1, bestLevel = 0;
        static const int allSkills[] = {
            SKILL_FARMING, SKILL_FISHING, SKILL_WOODWORK, SKILL_KNAPPING,
            SKILL_SMELTING, SKILL_FIREMAKING, SKILL_LEATHERWORK, SKILL_COOKING,
            SKILL_HERBALISM, SKILL_TRACKING, SKILL_TRADE, SKILL_ANIMAL_LORE,
            SKILL_WEAVING, SKILL_MELEE
        };
        for (int sk : allSkills)
        {
            int lvl = GetNPCSkillLevel(ai.spawnId, sk);
            if (lvl > bestLevel) { bestLevel = lvl; bestSkill = sk; }
        }

        // Emerging specialty at level 2+ — you gravitate toward what you're learning
        if (bestLevel >= 2 && bestSkill >= 0)
        {
            for (const auto& st : skillMap)
            {
                if (st.skill != bestSkill)
                    continue;
                return st.task;
            }
        }
    }

    // ── Phase 21: Chieftain settlement bias ──────────────────────────────
    if (ai.isHuman && !ai.isChieftain)
    {
        for (auto cIt = creatureAI_.Begin(); cIt != creatureAI_.End(); ++cIt)
        {
            const ServerCreatureAI& chief = cIt->second_;
            if (!chief.isChieftain || chief.settlementId != ai.settlementId)
                continue;
            int csk = chief.chieftainSkillId;
            int preferredTask = -1;
            if (csk == SKILL_SMELTING || csk == SKILL_SMITHING || csk == SKILL_KNAPPING)
                preferredTask = STASK_MINE;
            else if (csk == SKILL_MELEE || csk == SKILL_TRACKING || csk == SKILL_TRAPPING)
                preferredTask = STASK_HUNT;
            else if (csk == SKILL_FARMING || csk == SKILL_FISHING || csk == SKILL_COOKING)
                preferredTask = STASK_FARM_HARVEST;
            else if (csk == SKILL_WOODWORK)
                preferredTask = STASK_BUILD;
            if (preferredTask >= 0)
                return preferredTask;
            break;
        }
    }

    // Phase 13: God directive override — if the god-player set a FOCUS_TASK,
    // try that task first (skip if prerequisites aren't met).
    // FORBID_TASK is checked inline where each task returns.
    if (ai.isHuman && ai.directive == ServerCreatureAI::DIRECTIVE_FOCUS_TASK)
    {
        int focusTask = ai.directiveParam;
        // Validate task is in productive range (not FLEE/DEFEND/survival)
        if (focusTask >= STASK_CRAFT && focusTask <= STASK_IDLE)
            return focusTask;
    }

    // 6a. EQUIP — has better weapon/tool in bag than currently equipped
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);
            int equippedTier = -1;
            for (unsigned i = 0; i < inv.Size(); ++i)
            {
                if (inv[i].slotType == "hand" && inv[i].itemId > 0)
                {
                    ItemInfo item;
                    if (gameDB_ && gameDB_->GetItem(inv[i].itemId, item))
                        equippedTier = item.tier;
                }
            }
            for (unsigned i = 0; i < inv.Size(); ++i)
            {
                if (inv[i].slotType != "bag" || inv[i].itemId <= 0)
                    continue;
                ItemInfo item;
                if (gameDB_ && gameDB_->GetItem(inv[i].itemId, item) &&
                    (item.category == "weapon" || item.category == "tool") &&
                    item.tier > equippedTier)
                {
                    return STASK_EQUIP;
                }
            }
        }
    }

    // 6a2. DRESS — has better clothing/armor in bag than currently equipped
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && gameDB_)
        {
            Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);
            // Collect tiers of currently equipped clothing/armor in body/head/feet/back
            HashMap<String, int> equippedTiers;
            for (unsigned i = 0; i < inv.Size(); ++i)
            {
                if (inv[i].itemId <= 0)
                    continue;
                if (inv[i].slotType == "body" || inv[i].slotType == "head" ||
                    inv[i].slotType == "feet" || inv[i].slotType == "back")
                {
                    ItemInfo item;
                    if (gameDB_->GetItem(inv[i].itemId, item))
                        equippedTiers[inv[i].slotType] = item.tier;
                }
            }
            // Scan bag for clothing/armor better than current
            for (unsigned i = 0; i < inv.Size(); ++i)
            {
                if (inv[i].slotType != "bag" || inv[i].itemId <= 0)
                    continue;
                ItemInfo item;
                if (!gameDB_->GetItem(inv[i].itemId, item))
                    continue;
                if (item.category != "clothing" && item.category != "armor")
                    continue;
                // Determine target slot
                String slot;
                if (item.category == "armor" && item.id == 304)  // Leather Cap
                    slot = "head";
                else if (item.category == "armor")
                    slot = "body";
                else if (item.id == 303)  // Hide Boots
                    slot = "feet";
                else if (item.id == 302)  // Fur Cloak
                    slot = "back";
                else
                    slot = "body";
                auto it = equippedTiers.Find(slot);
                int currentTier = (it != equippedTiers.End()) ? it->second_ : -1;
                if (item.tier > currentTier)
                    return STASK_DRESS;
            }
        }
    }

    // 6b. REPAIR — damaged owned building nearby
    if (ai.isHuman && worldDB_)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetPlacedBuildingsByOwner(npcPid);
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                auto typeIt = cachedBuildingTypes_.Find(buildings[b].buildingId);
                if (typeIt == cachedBuildingTypes_.End())
                    continue;
                if (buildings[b].hp < typeIt->second_.maxHp / 2)
                {
                    Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    if ((ai.position - bPos).Length() < 30.0f)
                    {
                        return STASK_REPAIR;
                    }
                }
            }
        }
    }

    // 6b2. BUILD WALLS — settlement under repeated attack, chieftain or builder responds
    if (ai.isHuman)
    {
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End() && cfIt->second_.predatorAttackCount >= FORTIFICATION_THRESHOLD)
        {
            if (FindDefenseBuildingToBuild(ai) >= 0)
                return STASK_BUILD;
        }
    }

    // 6c. CRAFT — can make something useful (skill-gated by Knapping/Woodwork)
    if (ai.isHuman && NPCFindCraftableRecipe(ai) >= 0)
        return STASK_CRAFT;

    // 6c2. BURN_CHARCOAL — has wood + Charcoal Kiln nearby
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, 11 /* Log */) >= 3)
        {
            // Check Charcoal Kiln (608) nearby
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                if (buildings[b].buildingId == 608)
                {
                    Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    if ((ai.position - bPos).Length() <= 30.0f)
                    {
                        return STASK_BURN_CHARCOAL;
                    }
                }
            }
        }
    }

    // 6d. CHOP — needs wood, has axe, tree within range (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            int equippedTool = worldDB_->GetEquippedItem(npcPid, "hand");
            if (equippedTool > 0 && gameDB_)
            {
                ItemInfo toolInfo;
                if (gameDB_->GetItem(equippedTool, toolInfo) && toolInfo.category == "tool")
                {
                    if (NPCFindNearestTree(ai.position, 30.0f) != 0)
                        return STASK_CHOP;
                }
            }
        }
    }

    // 6d2. PROSPECT — settlement knows at least one trace element (skill check on completion)
    if (ai.isHuman)
    {
        int knownElement = FindKnownTraceElement(ai.campfireId);
        if (knownElement != 0)
            return STASK_PROSPECT;
    }

    // 6e. BUILD — has materials, tribe needs shelter (skill check on completion)
    if (ai.isHuman)
    {
        // Only build if NPC can afford at least one building type
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0)
        {
            for (auto it = cachedBuildingTypes_.Begin(); it != cachedBuildingTypes_.End(); ++it)
            {
                Vector<BuildingRecipeInput> recipe = gameDB_->GetBuildingRecipe(it->first_);
                bool canAfford = !recipe.Empty();
                for (unsigned r = 0; r < recipe.Size() && canAfford; ++r)
                {
                    if (worldDB_->GetItemCount(npcPid, recipe[r].itemId) < recipe[r].quantity)
                        canAfford = false;
                }
                if (canAfford)
                {
                    return STASK_BUILD;
                }
            }
        }
    }

    // 6e2. DIG_CHANNEL — farmland too far from water, has digging tool (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, 102 /* Digging Stick */) > 0)
        {
            if (FindDryFarmland(ai) != Vector3::ZERO)
                return STASK_DIG_CHANNEL;
        }
    }

    // 6f. MINE — exposed ore deposit nearby, has pick (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            int equippedTool = worldDB_->GetEquippedItem(npcPid, "hand");
            if (equippedTool > 0 && gameDB_)
            {
                ItemInfo toolInfo;
                if (gameDB_->GetItem(equippedTool, toolInfo) && toolInfo.category == "tool")
                {
                    // Quick check: any exposed deposit within 30m?
                    // Full scan done in NPCMine; here just check depositMap_ is populated
                    if (depositMap_)
                        return STASK_MINE;
                }
            }
        }
    }

    // 6f. FARM_HARVEST — mature crop ready (skill check on completion)
    if (ai.isHuman && worldDB_)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0)
        {
            int season = GetCurrentSeasonIndex();
            Vector<WorldDB::PlacedCropInfo> crops = worldDB_->GetAllPlacedCrops();
            for (unsigned c = 0; c < crops.Size(); ++c)
            {
                if (crops[c].ownerId == npcPid && crops[c].growthStage >= 3)
                {
                    auto typeIt = cachedCropTypes_.Find(crops[c].seedItemId);
                    if (typeIt != cachedCropTypes_.End() &&
                        IsSeasonMatch(typeIt->second_.harvestSeason, season))
                    {
                        return STASK_FARM_HARVEST;
                    }
                }
            }
        }
    }

    // 6g. FARM_PLANT — spring, has seeds, land available (skill check on completion)
    if (ai.isHuman)
    {
        int season = GetCurrentSeasonIndex();
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);
            for (unsigned i = 0; i < inv.Size(); ++i)
            {
                if (inv[i].slotType != "bag" || inv[i].itemId <= 0) continue;
                auto typeIt = cachedCropTypes_.Find(inv[i].itemId);
                if (typeIt != cachedCropTypes_.End() &&
                    IsSeasonMatch(typeIt->second_.plantSeason, season))
                {
                    return STASK_FARM_PLANT;
                }
            }
        }
    }

    // 6h. FISH — near water, hungry or low food (skill check on completion)
    if (ai.isHuman && ai.hunger < 70.0f)
    {
        Vector3 water = FindWaterEdge(ai.position, 30.0f);
        if (water != Vector3::ZERO)
            return STASK_FISH;
    }

    // 6h. SMELT — has ore+charcoal, LIT kiln nearby (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && gameDB_)
        {
            bool hasOre = worldDB_->GetItemCount(npcPid, 800) > 0 ||
                          worldDB_->GetItemCount(npcPid, 801) > 0 ||
                          worldDB_->GetItemCount(npcPid, 802) > 0;
            bool hasCharcoal = worldDB_->GetItemCount(npcPid, 43) > 0;
            if (hasOre && hasCharcoal)
                return STASK_SMELT;
        }
    }

    // 6i. TAME — has food, wild tameable animal nearby (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, 6) > 0)
        {
            if (FindNearestTameableAnimal(ai, 20.0f) != 0)
                return STASK_TAME;
        }
    }

    // 6i-cull. CULL — Animal Lore 3+, 4+ tamed animals in settlement → cull weakest
    if (ai.isHuman && GetNPCSkillLevel(ai.spawnId, SKILL_ANIMAL_LORE) >= 3)
    {
        int tamedCount = 0;
        for (auto aIt = creatureAI_.Begin(); aIt != creatureAI_.End(); ++aIt)
        {
            const ServerCreatureAI& animal = aIt->second_;
            if (animal.tamerId != 0 && !animal.isHuman && animal.growthProgress >= 1.0f)
            {
                auto tamerIt = creatureAI_.Find(animal.tamerId);
                if (tamerIt != creatureAI_.End() && tamerIt->second_.campfireId == ai.campfireId)
                    ++tamedCount;
            }
        }
        if (tamedCount >= 4)
            return STASK_CULL;
    }

    // 6i2. BUILD_BOAT — has planks, near water, no existing boat (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_PLANK) >= 5)
        {
            Vector3 water = FindWaterEdge(ai.position, 30.0f);
            if (water != Vector3::ZERO)
            {
                // Only build if no canoe exists near this settlement
                bool hasBoat = false;
                Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
                for (unsigned b = 0; b < buildings.Size(); ++b)
                {
                    if (buildings[b].buildingId == BUILDING_CANOE &&
                        (Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ) - ai.homePosition).Length() < 50.0f)
                    { hasBoat = true; break; }
                }
                if (!hasBoat)
                    return STASK_BUILD_BOAT;
            }
        }
    }

    // 6i3. GATHER_HERBS — stockpile < 5, proactive gathering (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_MEDICINAL_HERBS) < 5)
            return STASK_GATHER_HERBS;
    }

    // 6i4. TAP_TREE — resin/growth stockpile < 5, find nearby harvestable tree (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            if (worldDB_->GetItemCount(npcPid, ITEM_TREE_RESIN) < 5 && FindHarvestableTree(ai, "resin") > 0)
                return STASK_TAP_TREE;
            if (worldDB_->GetItemCount(npcPid, 875) < 5 && FindHarvestableTree(ai, "growth") > 0)
                return STASK_TAP_TREE;
        }
    }

    // 6i5. PREPARE_FIRE_BUNDLE — camp fire is lit, has resin, no bundle yet (skill check on completion)
    if (ai.isHuman && ai.campfireId != 0)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && gameDB_)
        {
            bool hasBundle = (worldDB_->GetItemCount(npcPid, ITEM_FIRE_BUNDLE) > 0);
            bool hasPot = (worldDB_->GetItemCount(npcPid, ITEM_BARK_VESSEL) > 0);
            if (!hasBundle && !hasPot)
            {
                bool hasResin = (worldDB_->GetItemCount(npcPid, ITEM_TREE_RESIN) >= 2);
                auto cfIt = serverCampfires_.Find(ai.campfireId);
                bool campLit = (cfIt != serverCampfires_.End() && cfIt->second_.state == PIT_LIT);
                if (hasResin && campLit)
                    return STASK_PREPARE_FIRE_BUNDLE;
            }
        }
    }

    // 6i6. CARRY_FIRE — NPC has fire bundle/pot + an allied camp is COLD/UNLIT
    if (ai.isHuman && ai.campfireId != 0)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            bool hasFire = (worldDB_->GetItemCount(npcPid, ITEM_FIRE_BUNDLE) > 0 ||
                            worldDB_->GetItemCount(npcPid, ITEM_BARK_VESSEL) > 0);
            if (hasFire)
            {
                for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
                {
                    if (cfIt->first_ != ai.campfireId &&
                        (cfIt->second_.state == PIT_COLD || cfIt->second_.state == PIT_UNLIT))
                    {
                        float dist = (cfIt->second_.position - ai.position).Length();
                        if (dist < 120.0f)
                            return STASK_CARRY_FIRE;
                    }
                }
            }
        }
    }

    // 6i7. CARRY_WATER — NPC has water-filled vessel + allied camp has low reserve
    if (ai.isHuman && ai.campfireId != 0 && ai.vesselContents == ServerCreatureAI::VESSEL_WATER)
    {
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->first_ != ai.campfireId &&
                cfIt->second_.waterReserve < 5.0f)
            {
                float dist = (cfIt->second_.position - ai.position).Length();
                if (dist < 120.0f)
                    return STASK_CARRY_WATER;
            }
        }
    }

    // 6i8. STRIP_BARK — bark/growth stockpile < 5, find nearby tree (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            if (worldDB_->GetItemCount(npcPid, ITEM_SHEOAK_BARK) < 5 && FindHarvestableTree(ai, "bark") > 0)
                return STASK_STRIP_BARK;
            if (worldDB_->GetItemCount(npcPid, ITEM_WILLOW_BARK) < 5 &&
                FindHarvestableTree(ai, "growth") > 0)
                return STASK_STRIP_BARK;
        }
    }

    // 6i7. GATHER_LEAVES — eucalyptus leaves stockpile < 5 (skill check on completion)
    if (ai.isHuman)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_EUCALYPTUS_LEAVES) < 5)
        {
            if (FindHarvestableTree(ai, "medicine") > 0)
                return STASK_GATHER_LEAVES;
        }
    }

    // 6j. SCAVENGE — detect nearby death scent, harvest corpse (skill check on completion)
    if (ai.isHuman)
    {
        if (FindNearestServerScent(ai.position, SCAVENGE_DETECTION_RADIUS) >= 0)
            return STASK_SCAVENGE;
    }

    // (Phase 27 GATHER_HERBS check at ~9811 above)

    // Phase 24: SIGNAL — chieftain lights signal fire when predators attacked recently
    if (ai.isHuman && ai.isChieftain)
    {
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End() && cfIt->second_.signalType == ServerCampfire::SIGNAL_NONE
            && cfIt->second_.predatorAttackCount >= 3)
        {
            // Need a watchtower in the settlement
            bool hasWatchtower = false;
            if (worldDB_)
            {
                Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
                for (unsigned b = 0; b < buildings.Size(); ++b)
                {
                    if (buildings[b].buildingId == 32 &&  // Watchtower
                        (Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ) - ai.homePosition).Length() < 50.0f)
                    { hasWatchtower = true; break; }
                }
            }
            // Need a second settlement to signal TO
            bool hasOtherSettlement = false;
            for (auto sIt = serverCampfires_.Begin(); sIt != serverCampfires_.End(); ++sIt)
            {
                if (sIt->first_ != ai.campfireId && sIt->second_.state != PIT_COLD)
                { hasOtherSettlement = true; break; }
            }
            if (hasWatchtower && hasOtherSettlement)
                return STASK_SIGNAL;
        }
    }

    // Phase 24: react to incoming signal from other settlement
    if (ai.isHuman && ai.isChieftain)
    {
        for (auto sIt = serverCampfires_.Begin(); sIt != serverCampfires_.End(); ++sIt)
        {
            if (sIt->first_ == ai.campfireId)
                continue;
            if (sIt->second_.signalType == ServerCampfire::SIGNAL_DANGER && sIt->second_.signalTimer > 0.0f)
                return STASK_DEFEND;  // danger signal → prioritize defense
            if (sIt->second_.signalType == ServerCampfire::SIGNAL_TRADE && sIt->second_.signalTimer > 0.0f)
                return STASK_TRADE;   // trade signal → send trader
        }
    }

    // Phase 29: PLAY_MUSIC — night, near campfire, anyone can try (skill check on completion)
    if (ai.isHuman && darkness > 0.5f)
    {
        float distHome = (ai.position - ai.homePosition).Length();
        if (distHome < 15.0f && Random(1.0f) < 0.15f)
            return STASK_PLAY_MUSIC;
    }

    // Phase 36: GUARD — combat NPCs patrol perimeter
    // Triggered by: (a) chieftain directive, or (b) Melee 2+ after 3+ predator attacks
    if (ai.isHuman && ai.hunger > 25.0f)
    {
        bool wantGuard = false;

        // (a) Chieftain directive
        if (ai.directive == ServerCreatureAI::DIRECTIVE_FOCUS_TASK &&
            ai.directiveParam == STASK_GUARD)
            wantGuard = true;

        // (b) Auto-guard: Melee 2+ and settlement has suffered 3+ predator attacks
        if (!wantGuard && GetNPCSkillLevel(ai.spawnId, SKILL_MELEE) >= 2)
        {
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End() && cfIt->second_.predatorAttackCount >= 3)
            {
                // Limit to 2 guards per settlement
                int guardCount = 0;
                for (auto gIt = creatureAI_.Begin(); gIt != creatureAI_.End(); ++gIt)
                {
                    if (gIt->second_.currentTask == STASK_GUARD &&
                        gIt->second_.campfireId == ai.campfireId)
                        ++guardCount;
                }
                if (guardCount < 2)
                    wantGuard = true;
            }
        }

        if (wantGuard)
        {
            // Danger signal overrides guard patrol with active defense
            auto cfSig = serverCampfires_.Find(ai.campfireId);
            if (cfSig != serverCampfires_.End() &&
                cfSig->second_.signalType == ServerCampfire::SIGNAL_DANGER)
                return STASK_DEFEND;
            return STASK_GUARD;
        }
    }

    // 7. SIT_FIRE — near home and moderate warmth need or random
    float distHome = (ai.position - ai.homePosition).Length();
    if (distHome < 15.0f && (ai.warmth < GetTuning("warmth_sit_threshold", 70.0f) || Random(1.0f) < 0.4f))
        return STASK_SIT_FIRE;

    // 7a. PATROL — night torch perimeter walk. Has wood, campfire LIT/EMBERS,
    //     at least one other NPC sleeping, max 1 patroller per campfire.
    if (ai.isHuman && darkness > 0.6f)
    {
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ &&
            (worldDB_->GetItemCount(npcPid, ITEM_SOFTWOOD) > 0 ||
             worldDB_->GetItemCount(npcPid, ITEM_HARDWOOD) > 0))
        {
            // Check campfire state and that someone else is sleeping + no existing patroller
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End() &&
                (cfIt->second_.state == PIT_LIT || cfIt->second_.state == PIT_EMBERS))
            {
                bool hasSleeper = false;
                bool hasPatroller = false;
                for (auto pIt = creatureAI_.Begin(); pIt != creatureAI_.End(); ++pIt)
                {
                    if (pIt->first_ == ai.spawnId || !pIt->second_.isHuman)
                        continue;
                    if (pIt->second_.campfireId != ai.campfireId)
                        continue;
                    if (pIt->second_.currentTask == STASK_SLEEP)
                        hasSleeper = true;
                    if (pIt->second_.currentTask == STASK_PATROL)
                        hasPatroller = true;
                }
                if (hasSleeper && !hasPatroller)
                    return STASK_PATROL;
            }
        }
    }

    // 7b. TRADE — nearby NPC with surplus/need match (skill check on completion)
    if (ai.isHuman)
    {
        if (NPCFindTradePartner(ai) != 0)
            return STASK_TRADE;
    }

    // 7b2. TRADE_ROUTE — walk to foreign settlement with surplus goods (Trade 3+)
    // High-CHA NPCs are more likely to attempt this — they see the opportunity.
    if (ai.isHuman && serverCampfires_.Size() > 1)
    {
        int tradeSkill = GetNPCSkillLevel(ai.spawnId, SKILL_TRADE);
        if (tradeSkill >= 3 && FindNearestForeignCampfire(ai, 200.0f) != 0)
        {
            // CHA-driven motivation: higher CHA = more likely to set off trading
            float chance = 0.1f + ai.stats.ChaMod() * 0.05f;
            if (chance > 0.0f && Random(1.0f) < chance)
                return STASK_TRADE_ROUTE;
        }
    }

    // 7c. BURY — human NPC buries a dead human nearby (cultural act)
    if (ai.isHuman && FindBuriableCorpse(ai) != 0)
        return STASK_BURY;

    // 7d. TEACH — skill 5+ teacher, idle student at same campfire with skill < 3
    if (ai.isHuman && ai.currentTask != STASK_TEACH)
    {
        int teachSkill = 0;
        if (NPCFindApprentice(ai, teachSkill) != 0)
            return STASK_TEACH;
    }

    // 7e. RESEARCH — INT 12+ NPCs with any skill 3+ study to discover new techniques
    if (ai.isHuman && ai.stats.intelligence >= 12)
    {
        // Check if NPC has any skill at 3+ that could trigger a discovery
        bool hasResearchableSkill = false;
        static const int researchSkills[] = {
            SKILL_FORAGING, SKILL_TRACKING, SKILL_TRAPPING, SKILL_FIREMAKING,
            SKILL_KNAPPING, SKILL_LEATHERWORK, SKILL_POTTERY, SKILL_SMELTING,
            SKILL_ANIMAL_LORE, SKILL_MELEE, SKILL_WOODWORK
        };
        for (int sk : researchSkills)
        {
            if (GetNPCSkillLevel(ai.spawnId, sk) >= 3)
            { hasResearchableSkill = true; break; }
        }
        if (hasResearchableSkill && Random(1.0f) < 0.08f)  // 8% chance per task pick
            return STASK_RESEARCH;
    }

    // 7f. SOCIALIZE — idle NPCs greet nearby settlement members (15% chance)
    if (ai.isHuman && Random(1.0f) < 0.15f)
    {
        for (auto sIt = creatureAI_.Begin(); sIt != creatureAI_.End(); ++sIt)
        {
            if (sIt->first_ != ai.spawnId && sIt->second_.isHuman &&
                sIt->second_.campfireId == ai.campfireId &&
                (sIt->second_.position - ai.position).Length() < 20.0f)
                return STASK_SOCIALIZE;
        }
    }

    // 8. WANDER — 10% chance (day only)
    if (darkness < 0.3f && Random(1.0f) < GetTuning("wander_chance", 0.10f))
        return STASK_WANDER;

    // 9. IDLE — default
    return STASK_IDLE;
}

void AuthServer::StartCreatureTask(ServerCreatureAI& ai, int task)
{
    ai.currentTask = task;
    ai.stateTimer = 0.0f;

    switch (task)
    {
    case STASK_FLEE:
    {
        ai.state = 3; // CREATURE_FLEE
        ai.moveSpeed = 5.0f;
        ai.taskTimer = 6.0f;

        // Flee AWAY from the nearest threat — animals use FindFleeTarget,
        // humans use FindDefenseTarget (predators near campfire)
        unsigned threatId = ai.isHuman ? FindDefenseTarget(ai) : FindFleeTarget(ai);
        if (threatId != 0)
        {
            auto threatIt = creatureAI_.Find(threatId);
            if (threatIt != creatureAI_.End())
            {
                Vector3 away = ai.position - threatIt->second_.position;
                away.y_ = 0.0f;
                if (away.LengthSquared() > 0.01f)
                {
                    away.Normalize();
                    ai.targetPosition = ai.position + away * 30.0f;
                    ai.targetPosition.y_ = GetTerrainHeightAI(
                        ai.targetPosition.x_, ai.targetPosition.z_);
                    break;
                }
            }
        }
        // Fallback: random direction
        ai.targetPosition = PickWanderTarget(ai.position, 30.0f);
        break;
    }

    case STASK_DEFEND:
    {
        // Phase 5: Defend campfire. Enter ALERT, find/lock threat. The defense
        // tick handles the sub-state machine.
        ai.defensePhase = ServerCreatureAI::DEFENSE_ALERT;
        ai.defenseTimer = 0.0f;
        ai.state = 16; // CREATURE_ALERT
        ai.moveSpeed = 0.0f;
        ai.targetId = FindDefenseTarget(ai);
        if (ai.targetId != 0)
        {
            ai.targetPosition = creatureAI_[ai.targetId].position;
            URHO3D_LOGINFOF("[CreatureAI] Defender spawnId=%d ALERT — threat spawnId=%u species=%d",
                ai.creatureId, ai.targetId, creatureAI_[ai.targetId].creatureId);
            // Phase 28: track predator attacks per settlement for fortification trigger
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End())
                cfIt->second_.predatorAttackCount++;
        }
        ai.taskTimer = 25.0f; // Max defense duration before giving up
        break;
    }

    case STASK_EAT:
        ai.state = 2; // CREATURE_EAT
        ai.moveSpeed = 2.0f;
        ai.targetPosition = ai.position; // Eat in place
        ai.taskTimer = 5.0f;
        break;

    case STASK_EAT_FROM_INVENTORY:
        // Phase 4: Stand in place and eat real food from inventory
        ai.state = 2; // CREATURE_EAT
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = 3.0f;
        break;

    case STASK_TRAP_PLACE:
        // Phase 4: Walk out to hunting territory, place trap on arrival
        ai.state = 1; // CREATURE_WANDER (walk)
        ai.moveSpeed = 2.0f;
        ai.targetPosition = PickWanderTarget(ai.homePosition, 25.0f);
        ai.taskTimer = 12.0f;
        break;

    case STASK_TRAP_COLLECT:
    {
        // Phase 4: Walk to the triggered trap and harvest on arrival
        int npcPlayerId = GetNPCPlayerId(ai.spawnId);
        unsigned trapNodeId = FindTriggeredTrapOwnedBy(npcPlayerId);
        if (trapNodeId > 0 && trapStates_.Find(trapNodeId) != trapStates_.End())
        {
            ai.targetPosition = trapStates_[trapNodeId].position;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        else
        {
            // No triggered trap found — fall through to short wander
            ai.targetPosition = ai.position;
        }
        ai.state = 1; // CREATURE_WANDER (walk briskly)
        ai.moveSpeed = 3.0f;
        ai.taskTimer = 20.0f;
        break;
    }

    case STASK_HUNT:
    {
        // Phase 3: Hunt prey. Find a real target from creatureAI_ — falls
        // back to an abstract hunting ground walk if no prey in reach.
        ai.huntPhase = ServerCreatureAI::HUNT_APPROACH;
        ai.huntTimer = 0.0f;
        ai.state = 15; // CREATURE_HUNT
        ai.moveSpeed = 4.0f; // Run speed

        // Skilled NPCs prefer predator targets (fox/wolf) — higher-value pelts
        unsigned preyId = 0;
        if (ai.isHuman)
            preyId = FindPredatorHuntTarget(ai);

        // Fox hunting: foxes hunt rabbits when hungry
        if (!ai.isHuman && ai.creatureId == 3)
        {
            // Fox-specific prey: rabbits only
            float bestDist = HUNT_RANGE * HUNT_RANGE;
            for (auto fIt = creatureAI_.Begin(); fIt != creatureAI_.End(); ++fIt)
            {
                if (fIt->second_.creatureId != 1)  // rabbit only
                    continue;
                if (fIt->second_.state == 5 || fIt->second_.state == 13)
                    continue;
                if (fIt->second_.regionId != ai.regionId)
                    continue;
                Vector3 diff = fIt->second_.position - ai.position;
                diff.y_ = 0.0f;
                float dSq = diff.LengthSquared();
                if (dSq < bestDist)
                { bestDist = dSq; preyId = fIt->first_; }
            }
        }

        // Normal prey targeting (fallback)
        if (preyId == 0)
            preyId = FindHuntTarget(ai);

        if (preyId != 0)
        {
            ai.targetId = preyId;
            ai.targetPosition = creatureAI_[preyId].position;
            URHO3D_LOGINFOF("[CreatureAI] Hunter spawnId=%d targeting prey spawnId=%u species=%d",
                ai.creatureId, preyId, creatureAI_[preyId].creatureId);
        }
        else
        {
            // No prey in reach — fall back to speculative wander toward hunting grounds.
            ai.targetId = 0;
            ai.targetPosition = PickWanderTarget(ai.homePosition, HUNT_RANGE);
        }
        ai.taskTimer = 30.0f; // Max hunt duration before giving up
        ai.packHuntLeader = 0; // Leader of the pack

        // Pack recruitment — wolves recruit idle/wandering same-species nearby
        if (ai.targetId != 0 && (ai.creatureId == 5)) // Wolf species ID
        {
            Vector<unsigned> pack = FindPackMembers(ai.spawnId, ai.creatureId, 40.0f);
            int flankIdx = 1;
            for (unsigned i = 0; i < pack.Size(); ++i)
            {
                auto memberIt = creatureAI_.Find(pack[i]);
                if (memberIt == creatureAI_.End())
                    continue;
                ServerCreatureAI& member = memberIt->second_;
                // Only recruit idle or wandering wolves
                if (member.state != 0 && member.state != 1) // not IDLE or WANDER
                    continue;
                // Already hunting something else — leave it
                if (member.currentTask == STASK_HUNT)
                    continue;

                member.currentTask = STASK_HUNT;
                member.huntPhase = ServerCreatureAI::HUNT_APPROACH;
                member.huntTimer = 0.0f;
                member.state = 15; // CREATURE_HUNT
                member.moveSpeed = 4.0f;
                member.targetId = ai.targetId;
                member.targetPosition = ai.targetPosition;
                member.taskTimer = 30.0f;
                member.packHuntLeader = ai.spawnId;
                member.packFlankIndex = flankIdx++;

                URHO3D_LOGINFOF("[PackHunt] Wolf spawnId=%u recruited into pack (leader=%u, flank=%d)",
                    pack[i], ai.spawnId, member.packFlankIndex);
            }
            if (flankIdx > 1)
                URHO3D_LOGINFOF("[PackHunt] Pack formed: leader=%u, %d members, prey=%u",
                    ai.spawnId, flankIdx - 1, ai.targetId);
        }
        break;
    }

    case STASK_WARM:
        ai.state = 1; // CREATURE_WANDER (walk home first)
        ai.moveSpeed = 2.5f;
        ai.targetPosition = ai.homePosition;
        ai.targetPosition.y_ = GetTerrainHeightAI(ai.homePosition.x_, ai.homePosition.z_);
        ai.taskTimer = 15.0f;
        break;

    case STASK_SLEEP:
    {
        ai.taskTimer = Random(20.0f, 40.0f);

        // Try to find a shelter with a bed to sleep in
        bool foundBed = false;
        if (ai.isHuman && worldDB_)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            float bestDistSq = 30.0f * 30.0f;  // max 30m search
            Vector3 bestBedPos;

            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                // Beds: type 101 (Bed), 102 (Bed Large)
                if (buildings[b].buildingId != 101 && buildings[b].buildingId != 102)
                    continue;
                Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                // Must be near NPC's settlement
                if ((bPos - ai.homePosition).Length() > 25.0f)
                    continue;
                float dSq = (bPos - ai.position).LengthSquared();
                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    bestBedPos = bPos;
                    foundBed = true;
                }
            }

            if (foundBed)
            {
                // Walk to bed first, then sleep
                ai.state = 1;  // CREATURE_WANDER (walk to bed)
                ai.moveSpeed = 1.5f;
                ai.targetPosition = bestBedPos;
                ai.taskTimer += 10.0f;  // extra time for walking
                break;
            }
        }

        // No bed found — sleep in place (outdoors)
        ai.state = 7; // CREATURE_SLEEP
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        break;
    }

    case STASK_GATHER:
        ai.state = 1; // CREATURE_WANDER (walk to resource area)
        ai.moveSpeed = 2.0f;
        ai.targetPosition = PickWanderTarget(ai.homePosition, 20.0f);
        ai.taskTimer = 10.0f;
        break;

    case STASK_GATHER_WOOD:
        // Phase 5: walk out, gather firewood, return. Same pattern as GATHER
        // but targeted at wood instead of food.
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.targetPosition = PickWanderTarget(ai.homePosition, 15.0f);
        ai.taskTimer = 8.0f;
        break;

    case STASK_SIT_FIRE:
        ai.state = 6; // CREATURE_SIT
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = Random(8.0f, 20.0f);
        break;

    case STASK_TEND_FIRE:
    {
        // Phase 6: Walk to the shared campfire and add virtual fuel on arrival.
        // OnCreatureTaskComplete handles the fuel-add when we get there.
        ai.state = 1; // CREATURE_WANDER (walk briskly)
        ai.moveSpeed = 2.5f;
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            ai.targetPosition = cfIt->second_.position;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        else
        {
            // No campfire assigned — degrade to home position
            ai.targetPosition = ai.homePosition;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.homePosition.x_, ai.homePosition.z_);
        }
        ai.taskTimer = 12.0f; // max walk time
        break;
    }

    case STASK_MAKE_FIRE:
    {
        // Phase 4a: Walk to COLD/UNLIT pit, begin friction ignition on arrival.
        // Same approach as TEND — walk there, then OnCreatureTaskComplete starts the ignition.
        ai.state = 1; // CREATURE_WANDER (walk to pit)
        ai.moveSpeed = 2.5f;
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            ai.targetPosition = cfIt->second_.position;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        else
        {
            ai.targetPosition = ai.homePosition;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.homePosition.x_, ai.homePosition.z_);
        }
        ai.taskTimer = 15.0f; // max walk time (a bit more slack for friction start)
        break;
    }

    case STASK_SCAVENGE:
    {
        // Phase 5b: scavenger walks to nearest death scent, eats on arrival.
        ai.state = 1; // CREATURE_WANDER (walk toward scent)
        ai.moveSpeed = 2.5f;
        int scentIdx = FindNearestServerScent(ai.position, SCAVENGE_DETECTION_RADIUS);
        if (scentIdx >= 0)
            ai.targetPosition = serverScents_[scentIdx].position;
        else
            ai.targetPosition = ai.position; // no scent found — stay put
        ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        ai.taskTimer = 15.0f; // max travel time before giving up
        break;
    }

    case STASK_DRINK:
    {
        if (ai.isHuman && ai.vesselContents == ServerCreatureAI::VESSEL_WATER)
        {
            // Drink from personal vessel — instant, no walking
            ai.state = 0; // CREATURE_IDLE
            ai.taskTimer = 2.0f;
        }
        else if (ai.isHuman && ai.campfireId != 0)
        {
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End() && cfIt->second_.waterReserve >= 1.0f)
            {
                // Walk to campfire to drink from water cache
                ai.state = 1; // CREATURE_WANDER
                ai.moveSpeed = 2.0f;
                ai.targetPosition = cfIt->second_.position;
                ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                ai.taskTimer = 15.0f;
                break;
            }
            // Fall through to water edge
            ai.state = 1;
            ai.moveSpeed = 2.0f;
            Vector3 water = FindWaterEdge(ai.position, 60.0f);
            if (water != Vector3::ZERO)
                ai.targetPosition = water;
            else
                ai.targetPosition = ai.position;
            ai.taskTimer = 20.0f;
        }
        else
        {
            // Walk to nearest water's edge
            ai.state = 1; // CREATURE_WANDER
            ai.moveSpeed = 2.0f;
            Vector3 water = FindWaterEdge(ai.position, 60.0f);
            if (water != Vector3::ZERO)
                ai.targetPosition = water;
            else
                ai.targetPosition = ai.position;
            ai.taskTimer = 20.0f;
        }
        break;
    }

    case STASK_FETCH_WATER:
    {
        // Phase 23: walk to water with Clay Pot, fill, walk home
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        Vector3 water = FindWaterEdge(ai.position, 80.0f);
        if (water != Vector3::ZERO)
            ai.targetPosition = water;
        else
            ai.targetPosition = ai.position;
        ai.taskTimer = 30.0f;  // longer than DRINK — round trip
        break;
    }

    case STASK_CRAFT:
    {
        // Stand in place and craft (uses existing animation — crouch/tend)
        ai.state = 2; // CREATURE_EAT (gather/crouch animation reused for crafting)
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        // Use recipe craft_time if available, otherwise default 5s
        float craftTime = 5.0f;
        if (gameDB_)
        {
            int recipeId = NPCFindCraftableRecipe(ai);
            if (recipeId >= 0)
            {
                RecipeInfo recipe;
                if (gameDB_->GetRecipe(recipeId, recipe) && recipe.craftTime > 0.0f)
                    craftTime = recipe.craftTime;
            }
        }
        ai.taskTimer = craftTime;
        break;
    }

    case STASK_COOK:
    {
        // Walk to nearest LIT campfire, cook on arrival
        ai.state = 1; // CREATURE_WANDER (walk to fire)
        ai.moveSpeed = 2.5f;
        ai.taskTimer = 12.0f;
        // Find nearest LIT campfire
        float bestDist = 999.0f;
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->second_.state == PIT_LIT)
            {
                float d = (ai.position - cfIt->second_.position).Length();
                if (d < bestDist)
                {
                    bestDist = d;
                    ai.targetPosition = cfIt->second_.position;
                    ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                }
            }
        }
        break;
    }

    case STASK_CHOP:
    {
        // Walk to nearest choppable tree
        ai.state = 1; // CREATURE_WANDER (walk to tree)
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 15.0f;
        unsigned nearTree = NPCFindNearestTree(ai.position, 30.0f);
        if (nearTree != 0)
        {
            // Look up tree position from DB
            sqlite3* db = worldDB_->GetHandle();
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT pos_x, pos_y, pos_z FROM trees WHERE tree_id = ?",
                                   -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, (int)nearTree);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    ai.targetPosition.x_ = (float)sqlite3_column_double(stmt, 0);
                    ai.targetPosition.y_ = (float)sqlite3_column_double(stmt, 1);
                    ai.targetPosition.z_ = (float)sqlite3_column_double(stmt, 2);
                }
                sqlite3_finalize(stmt);
            }
            ai.targetId = nearTree;  // reuse targetId to remember which tree
        }
        break;
    }

    case STASK_TORCH:
    {
        // Walk to nearest LIT campfire to light torch
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.5f;
        ai.taskTimer = 10.0f;
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->second_.state == PIT_LIT)
            {
                float d = (ai.position - cfIt->second_.position).Length();
                if (d < 10.0f)
                {
                    ai.targetPosition = cfIt->second_.position;
                    ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                    break;
                }
            }
        }
        break;
    }

    case STASK_EQUIP:
    case STASK_DRESS:
        // Instant — equip in place, brief pause
        ai.state = 0; // CREATURE_IDLE
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = 1.0f;
        break;

    case STASK_BUILD:
        // Stand near home and build (crouch/tend animation)
        ai.state = 2; // CREATURE_EAT (crouch animation reused)
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = 8.0f;
        break;

    case STASK_REPAIR:
    {
        // Walk to damaged building, repair on arrival
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 12.0f;
        // Find nearest damaged owned building
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetPlacedBuildingsByOwner(npcPid);
            float bestDist = 999.0f;
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                auto typeIt = cachedBuildingTypes_.Find(buildings[b].buildingId);
                if (typeIt == cachedBuildingTypes_.End())
                    continue;
                if (buildings[b].hp >= typeIt->second_.maxHp)
                    continue;
                Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                float d = (ai.position - bPos).Length();
                if (d < bestDist)
                {
                    bestDist = d;
                    ai.targetPosition = bPos;
                }
            }
        }
        break;
    }

    case STASK_MINE:
        // Walk toward deposit area, mine on arrival
        ai.state = 2; // CREATURE_EAT (crouch/dig animation reused)
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = 6.0f;
        break;

    case STASK_SMELT:
        // Stand at kiln and smelt
        ai.state = 2; // CREATURE_EAT (tend animation reused)
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = 10.0f;
        break;

    case STASK_TAME:
    {
        ai.state = 1; // slow approach
        ai.moveSpeed = 1.0f;
        ai.taskTimer = 15.0f;
        unsigned tgtId = FindNearestTameableAnimal(ai, 20.0f);
        if (tgtId != 0)
        {
            auto tgtIt = creatureAI_.Find(tgtId);
            if (tgtIt != creatureAI_.End())
                ai.targetPosition = tgtIt->second_.position;
        }
        break;
    }

    case STASK_SHEAR:
    {
        // Walk to nearest tamed alpaca (creatureId 11), shear for wool
        ai.state = 1; // approach
        ai.moveSpeed = 1.5f;
        ai.taskTimer = 12.0f;
        // Find tamed alpaca within range (not recently sheared)
        for (auto aIt = creatureAI_.Begin(); aIt != creatureAI_.End(); ++aIt)
        {
            if (aIt->second_.creatureId == 11 && aIt->second_.tamerId != 0 &&
                aIt->second_.growthProgress >= 1.0f &&  // adults only
                aIt->second_.shearCooldown <= 0.0f &&   // not recently sheared
                (aIt->second_.position - ai.position).Length() < 25.0f)
            {
                ai.targetPosition = aIt->second_.position;
                ai.targetId = aIt->first_;
                break;
            }
        }
        break;
    }

    case STASK_HERD:
    {
        // Walk to the most distant stray tamed animal and nudge it back toward settlement
        ai.state = 1; // CREATURE_WANDER (walking)
        ai.moveSpeed = 3.0f;
        ai.taskTimer = 20.0f;
        float farthestDistSq = 0.0f;
        unsigned farthestId = 0;
        for (auto aIt = creatureAI_.Begin(); aIt != creatureAI_.End(); ++aIt)
        {
            const ServerCreatureAI& animal = aIt->second_;
            if (animal.tamerId == 0 || animal.campfireId != ai.campfireId)
                continue;
            if (animal.creatureId == 12 || animal.creatureId == 13)
                continue;  // dogs manage themselves
            Vector3 diff = animal.position - animal.homePosition;
            diff.y_ = 0.0f;
            float dSq = diff.LengthSquared();
            if (dSq > farthestDistSq)
            {
                farthestDistSq = dSq;
                farthestId = aIt->first_;
            }
        }
        if (farthestId != 0 && farthestDistSq > 12.0f * 12.0f)
        {
            auto strayIt = creatureAI_.Find(farthestId);
            if (strayIt != creatureAI_.End())
            {
                ai.targetPosition = strayIt->second_.position;
                ai.targetId = farthestId;
            }
        }
        else
        {
            ai.taskTimer = 0.0f;  // no stray — cancel
        }
        break;
    }

    case STASK_CULL:
    {
        // Walk to weakest tamed animal in settlement herd and cull it for meat/hide
        ai.state = 1;  // approach
        ai.moveSpeed = 1.5f;
        ai.taskTimer = 10.0f;
        // Find weakest tamed animal (lowest trait score) in settlement
        unsigned weakestId = 0;
        float weakestScore = 999.0f;
        for (auto aIt = creatureAI_.Begin(); aIt != creatureAI_.End(); ++aIt)
        {
            const ServerCreatureAI& animal = aIt->second_;
            if (animal.tamerId == 0 || animal.isHuman || animal.growthProgress < 1.0f)
                continue;
            auto tamerIt = creatureAI_.Find(animal.tamerId);
            if (tamerIt == creatureAI_.End() || tamerIt->second_.campfireId != ai.campfireId)
                continue;
            float score = animal.traitSize + animal.traitSpeed + animal.traitYield;
            if (score < weakestScore)
            {
                weakestScore = score;
                weakestId = aIt->first_;
            }
        }
        if (weakestId != 0)
        {
            ai.targetPosition = creatureAI_[weakestId].position;
            ai.targetId = weakestId;
        }
        break;
    }

    case STASK_WEAVE:
    {
        // Walk to Loom (building type 91), weave wool thread into cloth on arrival
        ai.state = 1;
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 12.0f;
        if (worldDB_)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                if (buildings[b].buildingId == 91)
                {
                    ai.targetPosition = Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    break;
                }
            }
        }
        break;
    }

    case STASK_BURN_CHARCOAL:
    {
        // Walk to Charcoal Kiln (608), burn on arrival
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 12.0f;
        if (worldDB_)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                if (buildings[b].buildingId == 608)
                {
                    ai.targetPosition = Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    break;
                }
            }
        }
        break;
    }

    case STASK_FARM_PLANT:
        // Walk to planting area near home, plant on arrival
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.targetPosition = PickWanderTarget(ai.homePosition, 15.0f);
        ai.taskTimer = 10.0f;
        break;

    case STASK_FARM_HARVEST:
    {
        // Walk to mature crop, harvest on arrival
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 12.0f;
        // Find nearest mature crop
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            Vector<WorldDB::PlacedCropInfo> crops = worldDB_->GetAllPlacedCrops();
            float bestDist = 999.0f;
            for (unsigned c = 0; c < crops.Size(); ++c)
            {
                if (crops[c].ownerId == npcPid && crops[c].growthStage >= 3)
                {
                    Vector3 cPos(crops[c].posX, crops[c].posY, crops[c].posZ);
                    float d = (ai.position - cPos).Length();
                    if (d < bestDist)
                    {
                        bestDist = d;
                        ai.targetPosition = cPos;
                    }
                }
            }
        }
        break;
    }

    case STASK_DIG_CHANNEL:
    {
        // Walk to midpoint between dry farmland and nearest water
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 20.0f;
        Vector3 dryFarm = FindDryFarmland(ai);
        if (dryFarm != Vector3::ZERO)
        {
            Vector3 waterEdge = FindWaterEdge(dryFarm, 60.0f);
            if (waterEdge != Vector3::ZERO)
            {
                // Walk to midpoint — that's where the channel goes
                ai.targetPosition = (dryFarm + waterEdge) * 0.5f;
                ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
            }
            else
            {
                ai.targetPosition = dryFarm;
                ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
            }
        }
        break;
    }

    case STASK_FISH:
    {
        // Wade into shallow water to fish. Target a point 4m past the water's
        // edge so the NPC enters the water. The per-frame wading check (below
        // in the AI tick) switches to CREATURE_FISH (sitting) when arrived.
        ai.state = 1; // CREATURE_WANDER — walk to water's edge first
        ai.moveSpeed = 1.5f;  // slower wading pace

        // Fishing timer: skilled fishers land catches faster.
        // Formula: Random(10,30) / (1 + fishing_rating * 0.1) / popRatio
        // Low fish population → longer waits (popRatio < 1.0 → timer increases).
        int fishingRating = GetNPCSkillLevel(ai.spawnId, SKILL_FISHING);
        float baseTimer = 10.0f + Random(20.0f);  // Random(10,30)
        float skillMod = 1.0f + fishingRating * 0.1f;
        float popRatio = GetFishPopRatio(ai.position.x_, ai.position.z_);
        ai.taskTimer = baseTimer / (skillMod * Max(popRatio, 0.15f));

        Vector3 water = FindWaterEdge(ai.position, 30.0f);
        if (water != Vector3::ZERO)
        {
            // Push target 4m past the edge, deeper into the water
            Vector3 dir = water - ai.position;
            dir.y_ = 0.0f;
            if (dir.LengthSquared() > 0.01f)
                dir.Normalize();
            ai.targetPosition = water + dir * 4.0f;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        break;
    }

    case STASK_TRADE_ROUTE:
    {
        // Walk to nearest foreign settlement to trade
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.5f;  // traders walk with purpose
        ai.taskTimer = 60.0f; // long journey

        // Pack donkey bonus: tamed donkey at settlement boosts trade speed
        bool hasDonkey = false;
        for (auto dIt = creatureAI_.Begin(); dIt != creatureAI_.End(); ++dIt)
        {
            if (dIt->second_.tamerId != 0 && dIt->second_.creatureId == 9 &&
                dIt->second_.campfireId == ai.campfireId)
            { hasDonkey = true; break; }
        }
        if (hasDonkey)
        {
            ai.moveSpeed = 3.5f;
            ai.taskTimer = 90.0f;
        }

        unsigned foreignCf = FindNearestForeignCampfire(ai, 200.0f);
        if (foreignCf != 0)
        {
            auto cfIt = serverCampfires_.Find(foreignCf);
            if (cfIt != serverCampfires_.End())
            {
                ai.targetPosition = cfIt->second_.position;
                ai.targetId = foreignCf;

                // Caravan system: recruit idle/wandering NPCs at same settlement as escorts.
                // Escorts walk alongside trader to the destination (safety in numbers).
                // Trade skill 5+ enables caravan formation. Max 2 escorts.
                int tradeSkill = GetNPCSkillLevel(ai.spawnId, SKILL_TRADE);
                if (tradeSkill >= 5)
                {
                    int escorts = 0;
                    for (auto eIt = creatureAI_.Begin(); eIt != creatureAI_.End() && escorts < 2; ++eIt)
                    {
                        if (eIt->first_ == ai.spawnId || !eIt->second_.isHuman)
                            continue;
                        if (eIt->second_.campfireId != ai.campfireId)
                            continue;
                        if (eIt->second_.currentTask != STASK_IDLE && eIt->second_.currentTask != STASK_WANDER)
                            continue;
                        // Recruit escort — set them on same trade route
                        eIt->second_.currentTask = STASK_TRADE_ROUTE;
                        eIt->second_.targetPosition = cfIt->second_.position;
                        eIt->second_.targetId = foreignCf;
                        eIt->second_.state = 1;
                        eIt->second_.moveSpeed = ai.moveSpeed;
                        eIt->second_.taskTimer = ai.taskTimer;
                        ++escorts;
                    }
                    if (escorts > 0)
                    {
                        URHO3D_LOGINFOF("[Caravan] NPC %u formed caravan with %d escorts to settlement %u",
                            ai.spawnId, escorts, foreignCf);
                    }
                }
            }
        }
        break;
    }

    case STASK_TRADE:
    {
        // Walk to trade partner
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 15.0f;
        unsigned partnerId = NPCFindTradePartner(ai);
        if (partnerId != 0)
        {
            auto partnerIt = creatureAI_.Find(partnerId);
            if (partnerIt != creatureAI_.End())
            {
                ai.targetPosition = partnerIt->second_.position;
                ai.targetId = partnerId;
            }
        }
        break;
    }

    case STASK_BURY:
    {
        // Walk to human corpse, solemn pace
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 1.5f;
        ai.taskTimer = 20.0f;
        unsigned corpseId = FindBuriableCorpse(ai);
        if (corpseId != 0)
        {
            // Find corpse position from scent registry
            for (unsigned s = 0; s < serverScents_.Size(); ++s)
            {
                if (serverScents_[s].spawnId == corpseId)
                {
                    ai.targetPosition = serverScents_[s].position;
                    ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                    ai.targetId = corpseId;
                    break;
                }
            }
        }
        break;
    }

    case STASK_PATROL:
    {
        // Night torch patrol: craft torch from wood, light it, walk the perimeter.
        // Phase 1: consume wood → create torch → light it
        int npcPid = GetNPCPlayerId(ai.spawnId);
        bool craftedTorch = false;
        if (npcPid > 0 && worldDB_)
        {
            // Prefer softwood, fall back to hardwood
            if (worldDB_->GetItemCount(npcPid, ITEM_SOFTWOOD) > 0)
                worldDB_->RemoveItemFromInventory(npcPid, ITEM_SOFTWOOD, 1);
            else if (worldDB_->GetItemCount(npcPid, ITEM_HARDWOOD) > 0)
                worldDB_->RemoveItemFromInventory(npcPid, ITEM_HARDWOOD, 1);
            else
                break;  // no wood — bail

            AddItemToWorldInventory(npcPid, ITEM_BURNING_TORCH, 1);
            // Use resin torch burn time if NPC has resin torch in inventory
            bool npcHasResin = (worldDB_->GetItemCount(npcPid, ITEM_RESIN_TORCH) > 0);
            if (npcHasResin)
            {
                worldDB_->RemoveItemFromInventory(npcPid, ITEM_RESIN_TORCH, 1);
                torchTimers_[npcPid] = RESIN_TORCH_BURN_TIME;
            }
            else
                torchTimers_[npcPid] = TORCH_BURN_TIME;
            craftedTorch = true;
        }
        if (!craftedTorch)
            break;

        // Phase 2: begin circular patrol around campfire
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 1.5f;
        ai.taskTimer = 120.0f;  // 2-minute patrol

        // Initialize patrol angle based on current position relative to campfire
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            Vector3 diff = ai.position - cfIt->second_.position;
            ai.patrolAngle = Atan2(diff.z_, diff.x_);  // degrees (Urho convention)
            // Set first waypoint on the circle
            Vector3 cfPos = cfIt->second_.position;
            ai.targetPosition.x_ = cfPos.x_ + Cos(ai.patrolAngle) * PATROL_RADIUS;
            ai.targetPosition.z_ = cfPos.z_ + Sin(ai.patrolAngle) * PATROL_RADIUS;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        break;
    }

    case STASK_TEACH:
    {
        // Sit by campfire with apprentice — 10s teaching session
        ai.state = 6; // CREATURE_SIT
        ai.moveSpeed = 0.0f;
        ai.taskTimer = 10.0f;
        int teachSkill = 0;
        unsigned apprenticeId = NPCFindApprentice(ai, teachSkill);
        ai.targetId = apprenticeId;
        // Walk to campfire if not already near
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            float distToFire = (ai.position - cfIt->second_.position).Length();
            if (distToFire > 5.0f)
            {
                ai.state = 1; // walk first
                ai.moveSpeed = 2.0f;
                ai.targetPosition = cfIt->second_.position;
                ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                ai.taskTimer = 15.0f; // walk + teach time
            }
        }
        break;
    }

    case STASK_RESEARCH:
    {
        // Study at campfire/workshop — sit and think. Triggers technique discovery rolls.
        ai.state = 6; // CREATURE_SIT (studying)
        ai.moveSpeed = 0.0f;
        ai.taskTimer = 15.0f;
        // Walk to campfire if not near
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            float distToFire = (ai.position - cfIt->second_.position).Length();
            if (distToFire > 5.0f)
            {
                ai.state = 1;
                ai.moveSpeed = 2.0f;
                ai.targetPosition = cfIt->second_.position;
                ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                ai.taskTimer = 20.0f;
            }
        }
        break;
    }

    case STASK_SOCIALIZE:
    {
        // Walk to a nearby NPC and greet them — boosts familiarity
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 15.0f;
        // Find nearest idle/wandering human at same settlement
        float bestDist = 20.0f * 20.0f;
        unsigned bestTarget = 0;
        for (auto sIt = creatureAI_.Begin(); sIt != creatureAI_.End(); ++sIt)
        {
            if (sIt->first_ == ai.spawnId || !sIt->second_.isHuman)
                continue;
            if (sIt->second_.campfireId != ai.campfireId)
                continue;
            if (sIt->second_.growthProgress < 0.4f)
                continue;  // skip infants
            Vector3 diff = sIt->second_.position - ai.position;
            diff.y_ = 0.0f;
            float dSq = diff.LengthSquared();
            if (dSq < bestDist)
            { bestDist = dSq; bestTarget = sIt->first_; }
        }
        if (bestTarget != 0)
        {
            auto targetIt = creatureAI_.Find(bestTarget);
            if (targetIt != creatureAI_.End())
            {
                ai.targetPosition = targetIt->second_.position;
                ai.targetId = bestTarget;
            }
        }
        else
        {
            ai.taskTimer = 0.0f;  // nobody to socialize with
        }
        break;
    }

    case STASK_BUILD_BOAT:
    {
        // Walk to waterside to build canoe
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 20.0f;
        Vector3 water = FindWaterEdge(ai.position, 30.0f);
        if (water != Vector3::ZERO)
        {
            ai.targetPosition = water;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        break;
    }

    case STASK_TAP_TREE:
    {
        // Determine which botanical property to harvest based on inventory need
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 30.0f;
        int npcPid = GetNPCPlayerId(ai.spawnId);
        String prop = "resin";  // default
        if (npcPid > 0 && worldDB_)
        {
            // Prefer resin if low, else try growth (willow extract)
            if (worldDB_->GetItemCount(npcPid, ITEM_TREE_RESIN) >= 5 &&
                worldDB_->GetItemCount(npcPid, 875) < 5)
                prop = "growth";
        }
        ai.harvestProperty = prop;
        unsigned harvestTreeId = FindHarvestableTree(ai, prop);
        if (harvestTreeId > 0 && worldDB_ && worldDB_->IsOpen())
        {
            sqlite3* db = worldDB_->GetHandle();
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT pos_x, pos_z FROM trees WHERE tree_id = ?",
                                   -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, (int)harvestTreeId);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    float tx = (float)sqlite3_column_double(stmt, 0);
                    float tz = (float)sqlite3_column_double(stmt, 1);
                    ai.targetPosition = Vector3(tx, GetTerrainHeightAI(tx, tz), tz);
                }
                sqlite3_finalize(stmt);
            }
        }
        break;
    }

    case STASK_GATHER_LEAVES:
    {
        // Walk to nearest eucalyptus for medicinal leaves
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 25.0f;
        unsigned eucId = FindHarvestableTree(ai, "medicine");
        if (eucId > 0 && worldDB_ && worldDB_->IsOpen())
        {
            sqlite3* db = worldDB_->GetHandle();
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT pos_x, pos_z FROM trees WHERE tree_id = ?",
                                   -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, (int)eucId);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    float tx = (float)sqlite3_column_double(stmt, 0);
                    float tz = (float)sqlite3_column_double(stmt, 1);
                    ai.targetPosition = Vector3(tx, GetTerrainHeightAI(tx, tz), tz);
                }
                sqlite3_finalize(stmt);
            }
        }
        break;
    }

    case STASK_STRIP_BARK:
    {
        // Walk to nearest bark/growth tree (She-Oak for bark, Willow for growth)
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 25.0f;
        unsigned willowId = FindHarvestableTree(ai, "bark");
        if (willowId == 0) willowId = FindHarvestableTree(ai, "growth");
        if (willowId > 0 && worldDB_ && worldDB_->IsOpen())
        {
            sqlite3* db = worldDB_->GetHandle();
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT pos_x, pos_z FROM trees WHERE tree_id = ?",
                                   -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, (int)willowId);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    float tx = (float)sqlite3_column_double(stmt, 0);
                    float tz = (float)sqlite3_column_double(stmt, 1);
                    ai.targetPosition = Vector3(tx, GetTerrainHeightAI(tx, tz), tz);
                }
                sqlite3_finalize(stmt);
            }
        }
        break;
    }

    case STASK_PREPARE_FIRE_BUNDLE:
    {
        // Stay at camp — crafting happens on task complete
        ai.state = 0; // CREATURE_IDLE
        ai.taskTimer = 15.0f;  // gathering + wrapping takes time
        break;
    }

    case STASK_CARRY_FIRE:
    case STASK_DELIVER_FIRE:
    {
        // Walk to nearest cold/unlit allied camp to deliver fire
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.5f;  // urgent pace
        ai.taskTimer = 60.0f; // long walk allowed
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->first_ != ai.campfireId &&
                (cfIt->second_.state == PIT_COLD || cfIt->second_.state == PIT_UNLIT))
            {
                float dist = (cfIt->second_.position - ai.position).Length();
                if (dist < 120.0f)
                {
                    ai.targetPosition = cfIt->second_.position;
                    ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                    break;
                }
            }
        }
        break;
    }

    case STASK_CARRY_WATER:
    {
        // Walk to nearest allied camp with low water reserve
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.5f;
        ai.taskTimer = 60.0f;
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->first_ != ai.campfireId && cfIt->second_.waterReserve < 5.0f)
            {
                float dist = (cfIt->second_.position - ai.position).Length();
                if (dist < 120.0f)
                {
                    ai.targetPosition = cfIt->second_.position;
                    ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                    break;
                }
            }
        }
        break;
    }

    case STASK_PROSPECT:
    {
        // Walk to nearest Mine Shaft to prospect for known trace element
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 20.0f;
        ai.targetDepositType = FindKnownTraceElement(ai.campfireId);
        // Find nearest Mine Shaft
        if (worldDB_)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            float bestDist = 9999.0f;
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                if (buildings[b].buildingId == 80)
                {
                    Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    float d = (bPos - ai.position).Length();
                    if (d < bestDist) { bestDist = d; ai.targetPosition = bPos; }
                }
            }
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        break;
    }

    case STASK_SIGNAL:
    {
        // Phase 24: chieftain walks to watchtower to light signal fire
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 20.0f;
        // Find nearest watchtower
        if (worldDB_)
        {
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            float bestDist = 9999.0f;
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                if (buildings[b].buildingId == 32)
                {
                    Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    float d = (bPos - ai.position).Length();
                    if (d < bestDist) { bestDist = d; ai.targetPosition = bPos; }
                }
            }
        }
        break;
    }

    case STASK_GATHER_HERBS:
    {
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 15.0f;
        ai.targetPosition = PickWanderTarget(ai.homePosition, ai.wanderRadius);
        break;
    }

    case STASK_HEAL:
    {
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.5f;
        ai.taskTimer = 15.0f;
        unsigned injuredId = FindInjuredNPC(ai);
        if (injuredId != 0)
        {
            auto injIt = creatureAI_.Find(injuredId);
            if (injIt != creatureAI_.End())
            {
                ai.targetPosition = injIt->second_.position;
                ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                ai.targetId = injuredId;
            }
        }
        break;
    }

    case STASK_GUARD:
    {
        // Permanent perimeter patrol — reuses PATROL circle mechanic, no torch needed
        // Phase 36: auto-equip best weapon before starting guard duty
        NPCEquipBest(ai);
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.0f;
        ai.taskTimer = 120.0f;  // 2-minute patrol loops (re-evaluated at end)
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            Vector3 diff = ai.position - cfIt->second_.position;
            ai.patrolAngle = Atan2(diff.z_, diff.x_);
            Vector3 cfPos = cfIt->second_.position;
            ai.targetPosition.x_ = cfPos.x_ + Cos(ai.patrolAngle) * PATROL_RADIUS;
            ai.targetPosition.z_ = cfPos.z_ + Sin(ai.patrolAngle) * PATROL_RADIUS;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
        }
        break;
    }

    case STASK_WANDER:
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 1.5f;
        ai.targetPosition = PickWanderTarget(ai.homePosition, ai.wanderRadius);
        ai.taskTimer = 12.0f;
        break;

    case STASK_PLAY_MUSIC:
        // Phase 29: sit by campfire and play music
        ai.state = 6; // CREATURE_SIT
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.homePosition;
        ai.taskTimer = 30.0f;  // 30s music session
        break;

    case STASK_IDLE:
    default:
    {
        ai.state = 0; // CREATURE_IDLE
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position;
        ai.taskTimer = Random(3.0f, 8.0f);

        // Family grouping: mated NPCs drift toward mate when idle
        if (ai.isHuman && worldDB_)
        {
            for (auto mIt = creatureAI_.Begin(); mIt != creatureAI_.End(); ++mIt)
            {
                if (mIt->first_ == ai.spawnId || !mIt->second_.isHuman)
                    continue;
                String bond = worldDB_->GetBondType(ai.spawnId, mIt->first_);
                if (bond == "mate")
                {
                    // Bias home position toward mate (30% pull)
                    ai.homePosition = ai.homePosition.Lerp(mIt->second_.position, 0.3f);
                    break;
                }
            }
        }
        break;
    }
    }

    // Phase 29: morale affects task speed — high morale = faster, low = slower
    // Exclude survival/combat tasks from morale penalty (always urgent)
    if (ai.isHuman && task != STASK_FLEE && task != STASK_DEFEND && task != STASK_IDLE)
    {
        float moraleMul = Clamp(ai.morale / 50.0f, 0.5f, 2.0f);
        ai.moveSpeed *= moraleMul;
        // Master Chef: illness speed penalty -20%
        if (ai.illnessActive)
            ai.moveSpeed *= 0.8f;
    }
}

void AuthServer::NPCAwardXP(ServerCreatureAI& ai, const String& action)
{
    // D&D rule: you get better at what you practice. NPCs earn XP the same
    // way players do — by doing things. spawnId → stable playerId → same DB path.
    if (!ai.isHuman || !gameDB_)
        return;
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return;

    // Get settlement epoch for level cap gating
    auto epochIt = settlementEpochs_.Find(ai.campfireId);
    int epoch = (epochIt != settlementEpochs_.End()) ? epochIt->second_ : GetSettlementEpoch(ai.campfireId);

    // INT modifier scales XP gain: +10% per point of INT mod.
    // High-INT NPCs learn faster, low-INT NPCs learn slower.
    float multiplier = 1.0f + ai.stats.IntMod() * 0.1f;
    if (multiplier < 0.5f) multiplier = 0.5f;  // floor: even dull NPCs learn something

    // Master Trader: +10% craft XP for all NPCs in the settlement
    if (HasMasterTrader(ai.campfireId))
        multiplier *= 1.1f;

    // Phase 35: Workshop proximity bonus — +20% XP when crafting near specialized workshop
    static const int workshopSkills[] = {
        SKILL_SMELTING, SKILL_SMITHING, SKILL_LEATHERWORK, SKILL_WEAVING,
        SKILL_FARMING, SKILL_COOKING, SKILL_HERBALISM
    };
    for (int sk : workshopSkills)
    {
        if (gameDB_->ActionRewardsSkill(action, sk))
        {
            multiplier *= GetWorkshopProximityBonus(ai, sk);
            break;
        }
    }

    // Set INT mod for technique discovery rolls (d20 + INT mod vs DC)
    gameDB_->SetDiscoveryIntMod(npcPlayerId, ai.stats.IntMod());

    gameDB_->AwardXP(npcPlayerId, action, multiplier, epoch);

    // Drain pending technique discoveries and broadcast to clients
    while (!gameDB_->pendingDiscoveries_.Empty())
    {
        auto disc = gameDB_->pendingDiscoveries_.Front();
        gameDB_->pendingDiscoveries_.Erase(0);

        // Broadcast as MSG_PHENOMENON type 100 = skill discovery
        VectorBuffer buf;
        buf.WriteU32(ai.spawnId);
        buf.WriteI32(100);  // phenomenonType 100 = technique discovered
        buf.WriteVector3(ai.position);
        buf.WriteString(disc.skillName);
        for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
        {
            if (!sIt->second_.authenticated)
                continue;
            sIt->first_->SendMessage(MSG_PHENOMENON, true, true, buf);
        }

        // Check if this discovery advanced the settlement epoch
        UpdateSettlementEpoch(ai.campfireId);
    }
}

int AuthServer::GetNPCSkillLevel(unsigned spawnId, int skillId)
{
    if (!gameDB_)
        return 0;
    int npcPlayerId = GetNPCPlayerId(spawnId);
    return (npcPlayerId > 0) ? gameDB_->GetSkillLevel(npcPlayerId, skillId) : 0;
}

bool AuthServer::NPCAttemptSkill(ServerCreatureAI& ai, int skillId, int dc, const String& xpAction)
{
    // Non-humans always succeed (animals don't have skills to check)
    if (!ai.isHuman)
        return true;

    if (!combatResolver_)
        combatResolver_ = new CombatResolver(context_);
        combatResolver_->SetExternalRNG(QuantumDiceRollBridge);

    int skill = GetNPCSkillLevel(ai.spawnId, skillId);
    int roll = combatResolver_->RollD20();
    // WIS mod adds to skill checks — wise NPCs read situations better
    int wisMod = ai.stats.WisMod();
    bool success = (roll == 20) || (roll != 1 && roll + skill + wisMod >= dc);

    if (!success)
    {
        // Failure still teaches — half XP for trying.
        // INT mod still applies via NPCAwardXP path.
        int npcPlayerId = GetNPCPlayerId(ai.spawnId);
        if (npcPlayerId > 0 && gameDB_)
        {
            float multiplier = 0.5f + ai.stats.IntMod() * 0.05f;
            if (multiplier < 0.25f) multiplier = 0.25f;
            if (HasMasterTrader(ai.campfireId))
                multiplier *= 1.1f;
            gameDB_->AwardXP(npcPlayerId, xpAction, multiplier);
        }
        URHO3D_LOGDEBUGF("[NPC %u] %s FAILED (d20=%d +%d vs DC %d)",
            ai.spawnId, xpAction.CString(), roll, skill, dc);
    }

    return success;
}

void AuthServer::OnCreatureTaskComplete(ServerCreatureAI& ai)
{
    switch (ai.currentTask)
    {
    case STASK_EAT:
    {
        // Restore hunger — foraging skill finds better wild food
        int forageLevel = ai.isHuman ? GetNPCSkillLevel(ai.spawnId, SKILL_FORAGING) : 0;
        float restore = 30.0f * (1.0f + 0.05f * forageLevel);
        ai.hunger = Min(100.0f, ai.hunger + restore);
        NPCAwardXP(ai, "forage");
        break;
    }

    case STASK_EAT_FROM_INVENTORY:
        // Phase 4: Consume a real food item, restore hunger by item's food value
        NPCEatFromInventory(ai);
        break;

    case STASK_TRAP_PLACE:
        // Phase 4: Arrived at site, place the trap from inventory
        NPCPlaceTrap(ai);
        NPCAwardXP(ai, "trap_place");
        break;

    case STASK_TRAP_COLLECT:
        // Phase 4: Arrived at triggered trap, harvest the prey into inventory
        NPCCollectTrap(ai);
        NPCAwardXP(ai, "trap_catch");
        break;

    case STASK_GATHER:
        // Phase 4: Real inventory write. Falls back to abstract restore if DB write fails.
        if (!NPCGatherToInventory(ai))
            ai.hunger = Min(100.0f, ai.hunger + 15.0f);
        NPCAwardXP(ai, "forage");
        break;

    case STASK_GATHER_WOOD:
        // Phase 5: NPC gathered firewood — add to inventory and re-evaluate fire tasks
        NPCGatherWoodToInventory(ai);
        NPCAwardXP(ai, "forage");
        break;

    case STASK_HUNT:
        // Hunt timed out without success — still practiced the skill
        NPCAwardXP(ai, "melee_miss");
        break;

    case STASK_SCAVENGE:
    {
        // Phase 5b: scavenger arrived at death scent. Check for live prey nearby.
        // If a prey creature is near the scent, the scavenger kills it (no combat dice —
        // wolves don't fight rabbits, they catch and eat them).
        unsigned preyId = FindPreyNearScent(ai.position, SCAVENGER_ARRIVAL_DIST * 2.0f);
        if (preyId != 0)
        {
            auto csIt = creatureStates_.Find(preyId);
            if (csIt != creatureStates_.End())
            {
                ServerCreatureState& prey = csIt->second_;
                prey.hp = 0;
                URHO3D_LOGINFOF("[Death5b] Scavenger spawnId=%u killed prey spawnId=%u (%s)",
                    ai.spawnId, preyId, prey.species.CString());
                BroadcastCreatureDeath(preyId, prey, nullptr, DEATH_SCAVENGE);
                if (populationManager_ && populationManager_->IsReady() && prey.regionId >= 0)
                {
                    Vector<ReplacementSpawn> replacements =
                        populationManager_->RecordKill(prey.regionId, prey.creatureId);
                    for (unsigned j = 0; j < replacements.Size(); ++j)
                    {
                        Vector3 spawnPos = populationManager_->PickSpawnPositionInRegion(replacements[j].regionId);
                        BroadcastSpawnCreature(replacements[j].regionId, replacements[j].creatureId, spawnPos, 0.0f);
                    }
                }
                creatureStates_.Erase(csIt);
                creatureAI_.Erase(preyId);
            }
        }
        // Scavenger ate (or found nothing) — restore some hunger
        ai.hunger = Min(100.0f, ai.hunger + 20.0f);

        // Human scavengers harvest corpse — skill check determines loot
        if (ai.isHuman && preyId != 0)
        {
            if (NPCAttemptSkill(ai, SKILL_TRACKING, 8, "track_scent"))
            {
                int npcPid = GetNPCPlayerId(ai.spawnId);
                if (npcPid > 0)
                {
                    auto csIt2 = creatureStates_.Find(preyId);
                    int creatureId = (csIt2 != creatureStates_.End()) ? csIt2->second_.creatureId : 0;
                    if (creatureId > 0)
                        HarvestForOwner(npcPid, creatureId);
                }
                NPCAwardXP(ai, "track_scent");
            }
        }
        break;
    }

    case STASK_DRINK:
        if (ai.isHuman && ai.vesselContents == ServerCreatureAI::VESSEL_WATER)
        {
            // Drank from personal bark vessel — full restore, vessel now empty
            ai.thirst = 100.0f;
            ai.vesselContents = ServerCreatureAI::VESSEL_EMPTY;
            URHO3D_LOGINFOF("[Water] NPC %u drank from bark vessel", ai.spawnId);
        }
        else if (ai.isHuman && ai.campfireId != 0)
        {
            auto cfIt = serverCampfires_.Find(ai.campfireId);
            if (cfIt != serverCampfires_.End() && cfIt->second_.waterReserve >= 1.0f)
            {
                // Drank from campfire water cache
                cfIt->second_.waterReserve -= 1.0f;
                ai.thirst = 100.0f;
                URHO3D_LOGINFOF("[Water] NPC %u drank from campfire %u cache (%.1f remaining)",
                                ai.spawnId, ai.campfireId, cfIt->second_.waterReserve);
            }
            else if (TryDrawFromNearbyWell(ai.position.x_, ai.position.z_, 8.0f))
            {
                // Drank from nearby well
                ai.thirst = 100.0f;
                URHO3D_LOGINFOF("[Water] NPC %u drank from well", ai.spawnId);
            }
            else if (DrawFromBarrel(ai.position.x_, ai.position.z_, 8.0f))
            {
                // Drank from nearby water barrel
                ai.thirst = 100.0f;
                URHO3D_LOGINFOF("[Water] NPC %u drank from water barrel", ai.spawnId);
            }
            else
            {
                // Drank at water's edge
                ai.thirst = Min(100.0f, ai.thirst + GetTuning("thirst_drink_restore", 40.0f));
            }
        }
        else
        {
            // Try well first, then barrel, then water's edge (animal or no campfire)
            if (ai.isHuman && TryDrawFromNearbyWell(ai.position.x_, ai.position.z_, 8.0f))
            {
                ai.thirst = 100.0f;
                URHO3D_LOGINFOF("[Water] NPC %u drank from well", ai.spawnId);
            }
            else if (ai.isHuman && DrawFromBarrel(ai.position.x_, ai.position.z_, 8.0f))
            {
                ai.thirst = 100.0f;
                URHO3D_LOGINFOF("[Water] NPC %u drank from water barrel", ai.spawnId);
            }
            else
                ai.thirst = Min(100.0f, ai.thirst + GetTuning("thirst_drink_restore", 40.0f));
        }
        break;

    case STASK_FETCH_WATER:
    {
        // Fill vessel/pot at water — full thirst restore
        bool filledVessel = false;
        if (ai.isHuman && ai.vesselContents == ServerCreatureAI::VESSEL_EMPTY)
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_BARK_VESSEL) > 0)
            {
                ai.vesselContents = ServerCreatureAI::VESSEL_WATER;
                filledVessel = true;
                URHO3D_LOGINFOF("[Water] NPC %u filled bark vessel with water", ai.spawnId);

                // If NPC's thirst is already OK, deposit into campfire cache instead of drinking
                if (ai.thirst >= 70.0f && ai.campfireId != 0)
                {
                    auto cfIt = serverCampfires_.Find(ai.campfireId);
                    if (cfIt != serverCampfires_.End() &&
                        cfIt->second_.waterReserve < ServerCampfire::MAX_WATER_RESERVE)
                    {
                        cfIt->second_.waterReserve += 2.0f;
                        ai.vesselContents = ServerCreatureAI::VESSEL_EMPTY;
                        URHO3D_LOGINFOF("[Water] NPC %u deposited water at campfire %u (%.1f total)",
                                        ai.spawnId, ai.campfireId, cfIt->second_.waterReserve);
                    }
                }
            }
        }
        ai.thirst = 100.0f;
        NPCAwardXP(ai, "craft_clay");
        if (!filledVessel)
            URHO3D_LOGINFOF("[Phase 23] NPC %u fetched water with Clay Pot", ai.spawnId);
        break;
    }

    case STASK_GATHER_HERBS:
    {
        if (!NPCAttemptSkill(ai, SKILL_HERBALISM, 10, "gather_herb"))
            break;
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_)
        {
            int qty = ai.isMasterHerbalist ? 3 : (1 + (GetNPCSkillLevel(ai.spawnId, SKILL_HERBALISM) >= 5 ? 1 : 0));
            // Phase 35: Herbalist Hut +50% yield
            if (worldDB_)
            {
                Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
                for (unsigned b = 0; b < buildings.Size(); ++b)
                {
                    if (buildings[b].buildingId == 85 &&
                        (Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ) - ai.position).Length() < 15.0f)
                    { qty = qty * 3 / 2; break; }  // +50%
                }
            }
            AddItemToWorldInventory(npcPid, ITEM_MEDICINAL_HERBS, qty);
            NPCAwardXP(ai, "gather_herb");
        }
        break;
    }

    case STASK_HEAL:
    {
        // Phase 27: apply herbs to injured NPC — restore HP, consume 1 herb, broadcast to clients
        if (ai.targetId != 0)
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0 && worldDB_ && worldDB_->GetItemCount(npcPid, ITEM_MEDICINAL_HERBS) > 0)
            {
                auto csIt = creatureStates_.Find(ai.targetId);
                if (csIt != creatureStates_.End() && csIt->second_.hp > 0)
                {
                    int healAmount = 20;
                    // Phase 35: Herbalist Hut +10 HP
                    Vector<PlacedBuildingDBInfo> blds = worldDB_->GetAllPlacedBuildings();
                    for (unsigned b = 0; b < blds.Size(); ++b)
                    {
                        if (blds[b].buildingId == 85 &&
                            (Vector3(blds[b].posX, blds[b].posY, blds[b].posZ) - ai.position).Length() < 15.0f)
                        { healAmount += 10; break; }
                    }
                    csIt->second_.hp = Min(csIt->second_.hp + healAmount, csIt->second_.maxHp);
                    worldDB_->RemoveItemFromInventory(npcPid, ITEM_MEDICINAL_HERBS, 1);
                    NPCAwardXP(ai, "use_herb");

                    // Master Herbalist: instant food poisoning cure
                    bool curedPoison = false;
                    auto patientAI = creatureAI_.Find(ai.targetId);
                    if (patientAI != creatureAI_.End() && patientAI->second_.illnessActive && ai.isMasterHerbalist)
                    {
                        patientAI->second_.illnessTimer = 0.0f;
                        patientAI->second_.illnessActive = false;
                        curedPoison = true;
                        URHO3D_LOGINFOF("[Herbalism] Master NPC %u cured NPC %u's food poisoning", ai.spawnId, ai.targetId);
                    }

                    URHO3D_LOGINFOF("[Herbalism] NPC %u healed NPC %u (+%d HP, now %d/%d)",
                        ai.spawnId, ai.targetId, healAmount, csIt->second_.hp, csIt->second_.maxHp);
                    BroadcastNPCHeal(ai.spawnId, ai.targetId, healAmount, curedPoison, ai.position);
                }
            }
        }
        ai.targetId = 0;
        break;
    }

    case STASK_SIGNAL:
    {
        // Phase 24: chieftain arrived at watchtower — activate signal fire
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End())
        {
            // Danger signal if predator attacks triggered this
            cfIt->second_.signalType = ServerCampfire::SIGNAL_DANGER;
            cfIt->second_.signalTimer = 300.0f;  // signal lasts 5 minutes
            cfIt->second_.predatorAttackCount = 0;  // reset — signal acknowledged the threat
            URHO3D_LOGINFOF("[Signal] Chieftain %u lit DANGER signal at campfire %u",
                ai.spawnId, ai.campfireId);
            // Broadcast updated pit state so clients see the signal immediately
            BroadcastPitState(ai.campfireId, cfIt->second_);
        }
        break;
    }

    case STASK_PLAY_MUSIC:
    {
        // Phase 29: boost morale for all settlement NPCs within earshot
        // Even bad music is practice — skill check determines morale boost
        bool goodMusic = NPCAttemptSkill(ai, SKILL_TRADE, 10, "complete_trade");
        if (goodMusic)
        {
            for (auto mIt = creatureAI_.Begin(); mIt != creatureAI_.End(); ++mIt)
            {
                if (!mIt->second_.isHuman || mIt->second_.campfireId != ai.campfireId)
                    continue;
                if ((mIt->second_.position - ai.position).Length() < 20.0f)
                    mIt->second_.musicBoostTimer = 1440.0f;  // 1 game day
            }
            NPCAwardXP(ai, "complete_trade");
        }
        URHO3D_LOGINFOF("[Morale] NPC %u %s music at campfire %u",
            ai.spawnId, goodMusic ? "played" : "attempted", ai.campfireId);
        break;
    }

    case STASK_CRAFT:
    {
        int recipeId = NPCFindCraftableRecipe(ai);
        if (recipeId >= 0)
        {
            NPCCraft(ai, recipeId);
            RecordSettlementFirst(ai.campfireId, "first_craft", ai.spawnId);
        }
        break;
    }

    case STASK_COOK:
    {
        // Cook raw meat: find a cooking recipe the NPC can make near this fire
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && gameDB_ && worldDB_)
        {
            // Scan recipes for cooking-type outputs (cooked food)
            Vector<RecipeInfo> recipes = gameDB_->GetRecipesForTier(10);
            Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);
            HashMap<int, int> invMap;
            for (unsigned i = 0; i < inv.Size(); ++i)
                invMap[inv[i].itemId] += inv[i].quantity;

            for (unsigned i = 0; i < recipes.Size(); ++i)
            {
                if (!gameDB_->CanCraft(recipes[i].id, invMap))
                    continue;
                ItemInfo outputItem;
                if (gameDB_->GetItem(recipes[i].outputId, outputItem) &&
                    outputItem.category == "food" && !outputItem.name.Contains("Raw"))
                {
                    CraftForOwner(npcPid, recipes[i].id, ai.position, "NPC_" + String(ai.spawnId));
                    NPCAwardXP(ai, "cook_food");
                    // Master Chef: track cook skill at this campfire
                    int cookSkill = GetNPCSkillLevel(ai.spawnId, SKILL_COOKING);
                    auto cfIt = serverCampfires_.Find(ai.campfireId);
                    if (cfIt != serverCampfires_.End())
                        cfIt->second_.lastCookSkill = cookSkill;
                    // Apply food quality to the cook themselves
                    ApplyFoodQuality(ai, cookSkill);
                    break;
                }
            }
        }
        break;
    }

    case STASK_CHOP:
    {
        unsigned treeId = ai.targetId;
        if (treeId != 0 && NPCAttemptSkill(ai, SKILL_WOODWORK, 8, "chop_tree"))
            NPCChopTree(ai, treeId);
        ai.targetId = 0;
        break;
    }

    case STASK_TORCH:
        NPCLightTorch(ai);
        break;

    case STASK_PATROL:
        if (NPCAttemptSkill(ai, SKILL_FIREMAKING, 8, "fire_tend"))
            NPCDepositTorch(ai);
        break;

    case STASK_GUARD:
        // Guard patrol loop complete — practice combat readiness
        if (NPCAttemptSkill(ai, SKILL_MELEE, 10, "melee_hit"))
            NPCAwardXP(ai, "melee_hit");
        break;

    case STASK_EQUIP:
        NPCEquipBest(ai);
        break;

    case STASK_DRESS:
        NPCDressBest(ai);
        NPCAwardXP(ai, "craft_leather");  // practicing material awareness
        break;

    case STASK_BUILD:
        if (NPCAttemptSkill(ai, SKILL_WOODWORK, 12, "craft_wood"))
        {
            NPCBuild(ai);
            RecordSettlementFirst(ai.campfireId, "first_building", ai.spawnId);
        }
        break;

    case STASK_REPAIR:
        if (NPCAttemptSkill(ai, SKILL_WOODWORK, 10, "craft_wood"))
            NPCRepair(ai);
        break;

    case STASK_FARM_PLANT:
        if (NPCAttemptSkill(ai, SKILL_FARMING, 10, "farm_plant"))
            NPCPlantCrop(ai);
        break;

    case STASK_FARM_HARVEST:
        if (NPCAttemptSkill(ai, SKILL_FARMING, 8, "farm_harvest"))
            NPCHarvestCrop(ai);
        break;

    case STASK_DIG_CHANNEL:
        if (NPCAttemptSkill(ai, SKILL_FARMING, 14, "farm_plant"))
            NPCDigChannel(ai);
        break;

    case STASK_TEACH:
        NPCTeach(ai);
        ai.targetId = 0;
        break;

    case STASK_SOCIALIZE:
    {
        // Socializing complete — boost familiarity with target NPC
        if (ai.targetId != 0 && worldDB_)
        {
            // Greet animation
            ai.state = 22;  // CREATURE_GREET

            // CHA mod bonus: charismatic NPCs build bonds faster
            int chaBonus = Max(0, ai.stats.ChaMod());
            float famGain = 5.0f + chaBonus * 2.0f;

            // Boost familiarity (normal proximity does 0.5/sec, so +5 = 10s of proximity)
            worldDB_->IncrementFamiliarity(ai.spawnId, ai.targetId, famGain);

            // Small morale boost for both parties
            ai.morale = Min(100.0f, ai.morale + 2.0f);
            auto targetIt = creatureAI_.Find(ai.targetId);
            if (targetIt != creatureAI_.End())
                targetIt->second_.morale = Min(100.0f, targetIt->second_.morale + 2.0f);

            URHO3D_LOGINFOF("[Social] NPC %u greeted NPC %u (familiarity +%.0f, CHA bonus +%d)",
                ai.spawnId, ai.targetId, famGain, chaBonus * 2);
        }
        ai.targetId = 0;
        break;
    }

    case STASK_RESEARCH:
    {
        // Research complete — award XP in the NPC's highest researchable skill.
        // This triggers CheckTechniqueDiscovery in GameDB::AwardXP, using INT mod.
        static const int researchSkills[] = {
            SKILL_FORAGING, SKILL_TRACKING, SKILL_TRAPPING, SKILL_FIREMAKING,
            SKILL_KNAPPING, SKILL_LEATHERWORK, SKILL_POTTERY, SKILL_SMELTING,
            SKILL_ANIMAL_LORE, SKILL_MELEE, SKILL_WOODWORK
        };
        int bestSkill = SKILL_KNAPPING;
        int bestLvl = 0;
        for (int sk : researchSkills)
        {
            int lvl = GetNPCSkillLevel(ai.spawnId, sk);
            if (lvl > bestLvl)
            { bestLvl = lvl; bestSkill = sk; }
        }
        // Award bonus XP in best skill — research is deliberate practice
        NPCAwardXP(ai, "forage");  // generic XP action, triggers discovery check
        NPCAwardXP(ai, "forage");  // double XP for deliberate study
        URHO3D_LOGINFOF("[Research] NPC %u studied (INT %d, best skill %d at lvl %d)",
            ai.spawnId, ai.stats.intelligence, bestSkill, bestLvl);
        break;
    }

    case STASK_BUILD_BOAT:
        if (NPCAttemptSkill(ai, SKILL_WOODWORK, 16, "craft_wood"))
            NPCBuildBoat(ai);
        break;

    // (STASK_GATHER_HERBS and STASK_HEAL completion cases already handled by coder2 above)

    case STASK_TAP_TREE:
        if (NPCAttemptSkill(ai, SKILL_HERBALISM, 10, "gather_herb"))
            NPCTapTree(ai);
        break;

    case STASK_STRIP_BARK:
    case STASK_GATHER_LEAVES:
    {
        // Skill check for botanical harvest
        int herbDC = (ai.currentTask == STASK_GATHER_LEAVES) ? 14 : 10;
        if (!NPCAttemptSkill(ai, SKILL_HERBALISM, herbDC, "gather_herb"))
            break;
        // Generic botanical harvest — try each property this task could yield
        String prop;
        unsigned treeId = 0;
        if (ai.currentTask == STASK_STRIP_BARK)
        {
            // Try bark first (She-Oak), then growth (Willow)
            treeId = FindHarvestableTree(ai, "bark");
            if (treeId > 0) prop = "bark";
            else { treeId = FindHarvestableTree(ai, "growth"); prop = "growth"; }
        }
        else
        {
            treeId = FindHarvestableTree(ai, "medicine");
            prop = "medicine";
        }
        if (treeId > 0)
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0 && HarvestTreeProperty(npcPid, treeId, prop))
            {
                // Check for botanical discovery
                sqlite3* db = worldDB_->GetHandle();
                sqlite3_stmt* stmt = nullptr;
                int species = -1;
                if (sqlite3_prepare_v2(db, "SELECT species FROM trees WHERE tree_id = ?",
                                       -1, &stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(stmt, 1, (int)treeId);
                    if (sqlite3_step(stmt) == SQLITE_ROW)
                        species = sqlite3_column_int(stmt, 0);
                    sqlite3_finalize(stmt);
                }
                if (species >= 0 && !SettlementKnowsProperty(ai.campfireId, species, prop))
                    RecordBotanicalDiscovery(ai.campfireId, species, prop, ai);
                NPCAwardXP(ai, "gather_herb");
            }
        }
        break;
    }

    case STASK_PREPARE_FIRE_BUNDLE:
    {
        // Craft fire bundle (recipe 141) at camp with lit fire
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid > 0 && worldDB_ && gameDB_)
        {
            String ownerName = "NPC_" + String(ai.spawnId);
            if (CraftForOwner(npcPid, 141, ai.position, ownerName))
            {
                // Start the burn timer for the new fire bundle
                torchTimers_[npcPid] = FIRE_BUNDLE_BURN_TIME;
                NPCAwardXP(ai, "fire_tend");
                URHO3D_LOGINFOF("[FireLogistics] NPC %u (%s) prepared a Fire Bundle",
                                ai.spawnId, ai.npcName.CString());
            }
        }
        break;
    }

    case STASK_CARRY_FIRE:
    case STASK_DELIVER_FIRE:
    {
        // NPC arrived at cold camp — use fire item to ignite the pit
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid <= 0 || !worldDB_) break;

        bool hasBarkVessel = (worldDB_->GetItemCount(npcPid, ITEM_BARK_VESSEL) > 0);
        bool hasFireBundle = (worldDB_->GetItemCount(npcPid, ITEM_FIRE_BUNDLE) > 0);
        if (!hasBarkVessel && !hasFireBundle) break;

        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if ((cfIt->second_.state == PIT_COLD || cfIt->second_.state == PIT_UNLIT) &&
                (cfIt->second_.position - ai.position).Length() < 8.0f)
            {
                if (hasBarkVessel)
                {
                    // Bark Vessel lights campfire directly — pot is NOT consumed
                    URHO3D_LOGINFOF("[CarryFire] NPC %u lit camp %u with Bark Vessel (pot kept)",
                                    ai.spawnId, cfIt->first_);
                }
                else
                {
                    // Fire Bundle is consumed on use
                    worldDB_->RemoveItemFromInventory(npcPid, ITEM_FIRE_BUNDLE, 1);
                    torchTimers_.Erase(npcPid);
                    URHO3D_LOGINFOF("[CarryFire] NPC %u delivered fire bundle to camp %u",
                                    ai.spawnId, cfIt->first_);
                }

                cfIt->second_.fuelSeconds = Min(TORCH_INITIAL_FUEL, cfIt->second_.maxFuelSeconds);
                cfIt->second_.state = PIT_LIT;
                BroadcastPitState(cfIt->first_, cfIt->second_);
                NPCAwardXP(ai, "fire_tend");
                break;
            }
        }
        break;
    }

    case STASK_CARRY_WATER:
    {
        // NPC arrived at low-reserve camp — deposit water from vessel
        if (ai.vesselContents != ServerCreatureAI::VESSEL_WATER) break;

        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->second_.waterReserve < ServerCampfire::MAX_WATER_RESERVE &&
                (cfIt->second_.position - ai.position).Length() < 8.0f)
            {
                cfIt->second_.waterReserve += 2.0f;
                ai.vesselContents = ServerCreatureAI::VESSEL_EMPTY;
                URHO3D_LOGINFOF("[Water] NPC %u delivered water to camp %u (%.1f total)",
                                ai.spawnId, cfIt->first_, cfIt->second_.waterReserve);
                break;
            }
        }
        break;
    }

    case STASK_FISH:
        if (NPCAttemptSkill(ai, SKILL_FISHING, 8, "forage"))
            NPCFish(ai);
        break;

    case STASK_MINE:
        if (NPCAttemptSkill(ai, SKILL_KNAPPING, 14, "forage"))
            NPCMine(ai);
        break;

    case STASK_PROSPECT:
        if (NPCAttemptSkill(ai, SKILL_KNAPPING, 16, "forage"))
            NPCProspect(ai);
        ai.targetDepositType = 0;
        break;

    case STASK_SMELT:
        if (NPCAttemptSkill(ai, SKILL_SMELTING, 8, "craft_smelt"))
        {
            NPCSmelt(ai);
            RecordSettlementFirst(ai.campfireId, "first_smelt", ai.spawnId);
        }
        break;

    case STASK_BURN_CHARCOAL:
        if (NPCAttemptSkill(ai, SKILL_FIREMAKING, 12, "fire_tend"))
            NPCBurnCharcoal(ai);
        break;

    case STASK_TAME:
        if (NPCAttemptSkill(ai, SKILL_ANIMAL_LORE, 12, "forage"))
            NPCTame(ai);
        break;

    case STASK_SHEAR:
    {
        // Shear tamed alpaca for wool — Animal Lore 2+ skill check
        // Show shearing animation on client (CREATURE_SHEAR = 20)
        ai.state = 20;  // CREATURE_SHEAR — client plays kneeling gather anim
        if (ai.targetId != 0 && NPCAttemptSkill(ai, SKILL_ANIMAL_LORE, 8, "shear_alpaca"))
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0)
            {
                // Base 2-3 wool, scaled by alpaca's traitYield
                float yieldMult = 1.0f;
                auto alpIt = creatureAI_.Find(ai.targetId);
                if (alpIt != creatureAI_.End())
                    yieldMult = alpIt->second_.traitYield;
                int woolQty = Max(1, (int)((2 + Random(2)) * yieldMult));
                AddItemToWorldInventory(npcPid, 880, woolQty);  // 880 = Wool
                URHO3D_LOGINFOF("[Shear] NPC %u sheared alpaca %u → %d wool (yield=%.2f)",
                    ai.spawnId, ai.targetId, woolQty, yieldMult);

                // Cooldown: mark alpaca so it can't be re-sheared immediately
                auto alpacaIt = creatureAI_.Find(ai.targetId);
                if (alpacaIt != creatureAI_.End())
                    alpacaIt->second_.shearCooldown = 300.0f;  // 1 game day
            }
        }
        ai.targetId = 0;
        break;
    }

    case STASK_HERD:
    {
        // Herder reached the stray animal — redirect it back toward settlement
        if (ai.targetId != 0)
        {
            auto strayIt = creatureAI_.Find(ai.targetId);
            if (strayIt != creatureAI_.End())
            {
                // Push the animal's home target back toward campfire
                strayIt->second_.targetPosition = strayIt->second_.homePosition;
                strayIt->second_.wanderRadius = 12.0f;  // tighten up after herding
                URHO3D_LOGINFOF("[Herd] NPC %u herded stray animal %u back to settlement %u",
                    ai.spawnId, ai.targetId, ai.campfireId);
            }
            NPCAttemptSkill(ai, SKILL_ANIMAL_LORE, 6, "observe_animal");
        }
        ai.targetId = 0;
        break;
    }

    case STASK_CULL:
    {
        // Cull weakest tamed animal — Animal Lore 3+ skill check
        ai.state = 2;  // CREATURE_EAT animation (butchering action)
        if (NPCAttemptSkill(ai, SKILL_ANIMAL_LORE, 10, "observe_animal"))
            NPCCullWeakest(ai);
        ai.targetId = 0;
        break;
    }

    case STASK_WEAVE:
    {
        // Weave at Loom — Weaving 3+ skill check
        // Priority: garments first (if enough cloth), then cloth, then spin wool
        if (NPCAttemptSkill(ai, SKILL_WEAVING, 10, "craft_fiber"))
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0 && worldDB_)
            {
                int clothCount = worldDB_->GetItemCount(npcPid, 882);
                int threadCount = worldDB_->GetItemCount(npcPid, 881);
                int woolCount = worldDB_->GetItemCount(npcPid, 880);
                int hideCount = worldDB_->GetItemCount(npcPid, 21);

                // Wool Cloak: 3 wool cloth + 1 hide + 1 wool thread
                if (clothCount >= 3 && hideCount >= 1 && threadCount >= 1)
                {
                    worldDB_->RemoveItemFromInventory(npcPid, 882, 3);
                    worldDB_->RemoveItemFromInventory(npcPid, 21, 1);
                    worldDB_->RemoveItemFromInventory(npcPid, 881, 1);
                    AddItemToWorldInventory(npcPid, 884, 1);  // Wool Cloak
                    URHO3D_LOGINFOF("[Weave] NPC %u sewed wool cloak (3 cloth + 1 hide + 1 thread)", ai.spawnId);
                    NPCAwardXP(ai, "craft_fiber");
                }
                // Wool Tunic: 2 wool cloth + 1 wool thread
                else if (clothCount >= 2 && threadCount >= 1)
                {
                    worldDB_->RemoveItemFromInventory(npcPid, 882, 2);
                    worldDB_->RemoveItemFromInventory(npcPid, 881, 1);
                    AddItemToWorldInventory(npcPid, 883, 1);  // Wool Tunic
                    URHO3D_LOGINFOF("[Weave] NPC %u sewed wool tunic (2 cloth + 1 thread)", ai.spawnId);
                    NPCAwardXP(ai, "craft_fiber");
                }
                // Wool Cloth: 4 wool thread → 1 cloth
                else if (threadCount >= 4)
                {
                    worldDB_->RemoveItemFromInventory(npcPid, 881, 4);
                    AddItemToWorldInventory(npcPid, 882, 1);  // Wool Cloth
                    URHO3D_LOGINFOF("[Weave] NPC %u wove 4 wool thread -> 1 wool cloth", ai.spawnId);
                    NPCAwardXP(ai, "craft_fiber");
                }
                // Spin: 3 wool → 2 wool thread
                else if (woolCount >= 3)
                {
                    worldDB_->RemoveItemFromInventory(npcPid, 880, 3);
                    AddItemToWorldInventory(npcPid, 881, 2);  // Wool Thread
                    URHO3D_LOGINFOF("[Weave] NPC %u spun 3 wool -> 2 wool thread", ai.spawnId);
                }
            }
        }
        break;
    }

    case STASK_TRADE:
        if (ai.targetId != 0 && NPCAttemptSkill(ai, SKILL_TRADE, 8, "complete_trade"))
            NPCTrade(ai, ai.targetId);
        ai.targetId = 0;
        break;

    case STASK_TRADE_ROUTE:
        if (ai.targetId != 0)
            NPCTradeRoute(ai);
        ai.targetId = 0;
        break;

    case STASK_BURY:
        NPCBury(ai);
        ai.targetId = 0;
        break;

    case STASK_DEFEND:
        // Defense resolved (threat killed, driven off, or timed out) — no
        // vital restoration needed; the drama is its own reward.
        ai.targetId = 0;
        NPCAwardXP(ai, "melee_hit");  // practiced combat under pressure
        break;

    case STASK_SLEEP:
        // Sleeping restores stamina
        ai.stamina = Min(100.0f, ai.stamina + 60.0f);
        break;

    case STASK_WARM:
    {
        // Arrived at home — check fire state and respond accordingly:
        //  COLD/UNLIT → friction ignition (Phase 4a)
        //  low fuel   → tend (add wood)
        //  otherwise  → sit
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End() &&
            (ai.position - cfIt->second_.position).Length() < GetTuning("campfire_tend_range", CAMPFIRE_TEND_RANGE) * 2.0f)
        {
            const ServerCampfire& cf = cfIt->second_;
            if ((cf.state == PIT_COLD || cf.state == PIT_UNLIT) && !cf.ignitionActive)
                StartCreatureTask(ai, STASK_MAKE_FIRE);
            else if (cf.fuelSeconds < GetTuning("campfire_tend_threshold", CAMPFIRE_TEND_THRESHOLD))
                StartCreatureTask(ai, STASK_TEND_FIRE);
            else
                StartCreatureTask(ai, STASK_SIT_FIRE);
        }
        else
        {
            StartCreatureTask(ai, STASK_SIT_FIRE);
        }
        return; // Don't fall through to idle
    }

    case STASK_TEND_FIRE:
        // Phase 5: consume softwood from NPC inventory, then add fuel.
        // Falls back to virtual tend if NPC has no wood (shouldn't happen —
        // PickCreatureTask gates on NPCHasSoftwood, but be resilient).
        NPCConsumeSoftwood(ai);
        TendCampfire(ai);
        NPCAwardXP(ai, "fire_tend");
        // Linger near the fire for a beat so the anim reads as "tending"
        StartCreatureTask(ai, STASK_SIT_FIRE);
        return;

    case STASK_MAKE_FIRE:
    {
        // Phase 4a/5: NPC arrived at the pit — consume wood and begin friction ignition.
        // Wood consumed immediately (lost on ruin, same as player path).
        NPCConsumeFirewood(ai);
        NPCBeginIgnition(ai);
        // Keep the NPC kneeling at the pit for the duration. The ignition timer
        // in TickCampfires will complete it; if the NPC gets interrupted (flee,
        // defend), TickCampfires will ruin the ignition when the NPC moves away.
        ai.state = 2; // CREATURE_EAT (gather/crouch animation)
        ai.moveSpeed = 0.0f;
        ai.targetPosition = ai.position; // stay put
        ai.taskTimer = FRICTION_IGNITION_TIME + 10.0f; // long hold — ignition completes via tick
        return; // Don't fall through to idle
    }

    case STASK_IDLE:
    case STASK_WANDER:
    {
        // Idle/wandering NPCs observe their surroundings and learn passively.
        // Small XP gain keeps learning flowing even when there's nothing to do.
        if (ai.isHuman && gameDB_)
        {
            int npcPid = GetNPCPlayerId(ai.spawnId);
            if (npcPid > 0 && Random(1.0f) < 0.3f)
            {
                // Pick a random basic skill based on environment
                static const char* idleActions[] = {
                    "forage", "forage", "chop_tree", "observe_animal",
                    "melee_miss", "craft_stone", "fire_tend"
                };
                int idx = Random(0, 7);
                gameDB_->AwardXP(npcPid, idleActions[idx], 0.25f);
            }
        }
        break;
    }

    default:
        break;
    }

    // Default: return to idle, let next evaluation pick a new task
    ai.currentTask = STASK_IDLE;
    ai.state = 0; // CREATURE_IDLE
    ai.moveSpeed = 0.0f;
    ai.taskTimer = 0.0f;
}

void AuthServer::BroadcastCreatureAIState(unsigned spawnId, const ServerCreatureAI& ai)
{
    // Wire format matches Protocol.h MSG_CREATURE_AI_STATE:
    //   spawnId u32, state u8, position Vec3, targetId u32, moveSpeed f32,
    //   hp f32, hunger f32, thirst f32, warmth f32, stamina f32, vesselContents u8,
    //   growthProgress f32
    VectorBuffer buf;
    buf.WriteU32(spawnId);
    buf.WriteByte(static_cast<std::byte>(ai.state));
    buf.WriteFloat(ai.position.x_);
    buf.WriteFloat(ai.position.y_);
    buf.WriteFloat(ai.position.z_);
    buf.WriteU32(ai.targetId);
    buf.WriteFloat(ai.moveSpeed);

    // Vitals for inspect HUD — look up HP from creatureStates_
    auto csIt = creatureStates_.Find(spawnId);
    float hp = csIt != creatureStates_.End() ? (float)csIt->second_.hp : 0.0f;
    buf.WriteFloat(hp);
    buf.WriteFloat(ai.hunger);
    buf.WriteFloat(ai.thirst);
    buf.WriteFloat(ai.warmth);
    buf.WriteFloat(ai.stamina);
    buf.WriteByte(static_cast<std::byte>(ai.vesselContents));  // 0=empty, 1=fire, 2=water
    buf.WriteFloat(ai.growthProgress);  // child scale: 0.0 (newborn) to 1.0 (adult)
    buf.WriteI32(ai.currentTask);       // STASK enum — client uses for prop selection
    buf.WriteU32(ai.mountedHorse);      // spawnId of horse being ridden (0 = on foot)

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_CREATURE_AI_STATE, false, false, buf);
    }
}

Vector<unsigned> AuthServer::FindPackMembers(unsigned spawnId, int species, float radius)
{
    Vector<unsigned> pack;
    auto leaderIt = creatureAI_.Find(spawnId);
    if (leaderIt == creatureAI_.End())
        return pack;

    const Vector3& leaderPos = leaderIt->second_.position;
    float r2 = radius * radius;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->first_ == spawnId)
            continue;
        if (it->second_.creatureId != species)
            continue;
        float d2 = (it->second_.position - leaderPos).LengthSquared();
        if (d2 <= r2)
            pack.Push(it->first_);
    }
    return pack;
}

void AuthServer::TickHunt(ServerCreatureAI& ai, float dt)
{
    ai.huntTimer += dt;

    switch (ai.huntPhase)
    {
    case ServerCreatureAI::HUNT_APPROACH:
    {
        // If we have a real prey target, keep targetPosition updated from its
        // AI entry so we chase it. Also broadcast FLEE to the prey once when
        // we get close — reactive prey-flee per Phase 3 scope.
        bool haveRealPrey = false;
        if (ai.targetId != 0)
        {
            auto preyIt = creatureAI_.Find(ai.targetId);
            if (preyIt != creatureAI_.End())
            {
                haveRealPrey = true;
                Vector3 preyPos = preyIt->second_.position;

                // Pack encirclement: flank members approach from assigned angles
                if (ai.packFlankIndex >= 0)
                {
                    // Count total pack members hunting this prey
                    int packSize = 1; // leader
                    for (auto pit = creatureAI_.Begin(); pit != creatureAI_.End(); ++pit)
                    {
                        if (pit->second_.targetId == ai.targetId &&
                            pit->second_.currentTask == STASK_HUNT &&
                            pit->first_ != ai.spawnId)
                            ++packSize;
                    }
                    float angle = (float)ai.packFlankIndex / (float)packSize * 360.0f * (float)M_PI / 180.0f;
                    float surroundRadius = 8.0f;
                    Vector3 flankOffset(cosf(angle) * surroundRadius, 0.0f, sinf(angle) * surroundRadius);
                    ai.targetPosition = preyPos + flankOffset;
                    ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                }
                else
                {
                    ai.targetPosition = preyPos;
                }

                // Trigger prey flee when hunter closes to 20m — reactive state push
                float closeDist = (ai.position - preyIt->second_.position).Length();
                if (closeDist < 20.0f && preyIt->second_.state != 3) // 3 = CREATURE_FLEE
                {
                    ServerCreatureAI& prey = preyIt->second_;
                    prey.state = 3; // CREATURE_FLEE
                    // Flee vector: away from hunter
                    Vector3 away = prey.position - ai.position;
                    away.y_ = 0.0f;
                    if (away.LengthSquared() > 0.01f)
                    {
                        away.Normalize();
                        prey.targetPosition = prey.position + away * 15.0f;
                        prey.targetPosition.y_ = GetTerrainHeightAI(
                            prey.targetPosition.x_, prey.targetPosition.z_);
                    }
                    prey.moveSpeed = 5.0f;
                    // One-shot broadcast — client picks up state change via ApplyServerState.
                    BroadcastCreatureAIState(preyIt->first_, prey);
                    URHO3D_LOGDEBUGF("[CreatureAI] Prey spawnId=%u flees from hunter spawnId=%d",
                        preyIt->first_, ai.creatureId);
                }
            }
            else
            {
                // Prey despawned between target lock and approach — drop it
                ai.targetId = 0;
            }
        }

        // Pack flank: arrived at flank position → drop offset, charge prey directly
        if (ai.packFlankIndex >= 0 && haveRealPrey)
        {
            float flankDist = (ai.position - ai.targetPosition).Length();
            if (flankDist < 4.0f || ai.huntTimer > 6.0f)
            {
                ai.packFlankIndex = -1; // Drop flank — charge straight at prey
                auto preyChk = creatureAI_.Find(ai.targetId);
                if (preyChk != creatureAI_.End())
                    ai.targetPosition = preyChk->second_.position;
                ai.huntTimer = 0.0f;
                URHO3D_LOGDEBUGF("[PackHunt] Wolf spawnId=%u flanked, now charging prey", ai.spawnId);
            }
        }

        // Phase 28: wall collision — predator damages nearest wall when approaching walled settlement
        if (haveRealPrey && ai.targetId != 0)
        {
            auto preyWall = creatureAI_.Find(ai.targetId);
            if (preyWall != creatureAI_.End() && preyWall->second_.isHuman && preyWall->second_.campfireId != 0)
            {
                if (IsSettlementWalled(preyWall->second_.campfireId, ai.creatureId))
                {
                    // Predator blocked by wall — attack it instead
                    auto csWall = creatureStates_.Find(ai.spawnId);
                    int dmg = (csWall != creatureStates_.End()) ? csWall->second_.damage : 2;
                    DamageNearestWall(ai.position, preyWall->second_.campfireId, dmg);
                    ai.taskTimer = 0.0f;  // give up this hunt cycle, re-evaluate
                    break;
                }
            }
        }

        // Arrived at target (or timed out trying)
        float dist = (ai.position - ai.targetPosition).Length();
        if (dist < 3.0f || ai.huntTimer > 8.0f)
        {
            if (haveRealPrey)
            {
                // Real prey engagement — register in creatureStates_ and run combat
                auto preyIt = creatureAI_.Find(ai.targetId);
                if (preyIt != creatureAI_.End() && combatResolver_)
                {
                    const ServerCreatureAI& prey = preyIt->second_;
                    // Lazy-register prey in creatureStates_ for combat HP tracking
                    auto stateIt = creatureStates_.Find(ai.targetId);
                    if (stateIt == creatureStates_.End())
                    {
                        ServerCreatureState fresh;
                        fresh.creatureId = prey.creatureId;
                        if (!LoadCreatureCombat(prey.creatureId, fresh))
                        {
                            fresh.hp = fresh.maxHp = 2;
                            fresh.defense = 10;
                            fresh.damage = 1;
                            fresh.damageVar = 2;
                        }
                        fresh.position = prey.position;
                        fresh.regionId = prey.regionId;
                        creatureStates_[ai.targetId] = fresh;
                        stateIt = creatureStates_.Find(ai.targetId);
                    }
                    ServerCreatureState& cs = stateIt->second_;

                    // Pack bonus: count how many wolves are hunting this prey
                    int packAttackers = 0;
                    for (auto pit = creatureAI_.Begin(); pit != creatureAI_.End(); ++pit)
                    {
                        if (pit->second_.targetId == ai.targetId &&
                            pit->second_.currentTask == STASK_HUNT &&
                            (pit->second_.position - cs.position).LengthSquared() < 100.0f) // within 10m
                            ++packAttackers;
                    }
                    // Surrounded prey gets defense penalty: -2 per extra attacker
                    int packDefensePenalty = Max(0, (packAttackers - 1) * 2);

                    // Hunter's attack — base 3, scales with melee skill
                    int meleeLevel = GetNPCSkillLevel(ai.spawnId, SKILL_MELEE);
                    int attackMod = 3 + meleeLevel / 2;   // level 0→3, 5→5, 10→8
                    if (ai.isHuman && HasMasterHunter(ai.campfireId))
                        attackMod += 2;  // Master Hunter settlement bonus
                    int dmgBase = 4 + meleeLevel / 3;     // level 0→4, 6→6, 10→7
                    int effectiveDefense = Max(0, cs.defense - packDefensePenalty);
                    AttackResult result = combatResolver_->ResolveAttack(
                        attackMod, dmgBase, 4, effectiveDefense, cs.hp);

                    if (result.hit)
                    {
                        cs.hp = Max(0, cs.hp - result.damage);
                        CancelTradeIfPossessedDamaged(ai.targetId);
                    }

                    // D&D: you learn from every swing, hit or miss
                    NPCAwardXP(ai, result.hit ? "melee_hit" : "melee_miss");

                    URHO3D_LOGINFOF("[CreatureAI] Hunt strike on spawnId=%u — roll=%d %s%s dmg=%d hp=%d/%d",
                        ai.targetId, result.roll,
                        result.hit ? "HIT" : "MISS",
                        result.crit ? " CRIT" : "",
                        result.damage, cs.hp, cs.maxHp);

                    if (cs.hp == 0)
                    {
                        // Prey killed — broadcast death, record kill for population,
                        // remove from both tracking maps. Hunter restores hunger from loot.
                        NPCAwardXP(ai, "melee_kill");
                        BroadcastCreatureDeath(ai.targetId, cs, nullptr);
                        if (populationManager_ && populationManager_->IsReady() && cs.regionId >= 0)
                        {
                            Vector<ReplacementSpawn> replacements =
                                populationManager_->RecordKill(cs.regionId, cs.creatureId);
                            for (unsigned i = 0; i < replacements.Size(); ++i)
                            {
                                const ReplacementSpawn& rep = replacements[i];
                                Vector3 spawnPos = populationManager_->PickSpawnPositionInRegion(rep.regionId);
                                BroadcastSpawnCreature(rep.regionId, rep.creatureId, spawnPos, 0.0f);
                            }
                        }
                        creatureStates_.Erase(stateIt);
                        creatureAI_.Erase(preyIt);

                        // Transition to close/fight animation then butcher
                        ai.huntPhase = ServerCreatureAI::HUNT_CLOSE;
                        ai.huntTimer = 0.0f;
                        ai.state = 4; // CREATURE_FIGHT
                        ai.moveSpeed = 0.0f;
                        ai.targetPosition = ai.position;
                        ai.targetId = 0;
                    }
                    else
                    {
                        // Missed or prey survived — reset timer to keep approaching
                        ai.huntTimer = 0.0f;
                    }
                }
                else
                {
                    // Prey vanished during last tick — bail out
                    ai.taskTimer = 0.0f;
                }
            }
            else if (RollHuntSuccess(ai))
            {
                // No real prey — fallback abstract catch (legacy path).
                // Kept so hunt still works in regions with no registered prey.
                ai.huntPhase = ServerCreatureAI::HUNT_CLOSE;
                ai.huntTimer = 0.0f;
                ai.state = 4; // CREATURE_FIGHT
                ai.moveSpeed = 0.0f;
                ai.targetPosition = ai.position;
                URHO3D_LOGDEBUG("[CreatureAI] Hunt success (abstract — no real prey)");
            }
            else
            {
                // Failed — prey escaped. Abandon hunt.
                ai.taskTimer = 0.0f;
                URHO3D_LOGDEBUG("[CreatureAI] Hunt failed — prey escaped");
            }
        }
        break;
    }

    case ServerCreatureAI::HUNT_CLOSE:
        // Fighting/killing animation — 2 seconds
        if (ai.huntTimer > 2.0f)
        {
            ai.huntPhase = ServerCreatureAI::HUNT_BUTCHER;
            ai.huntTimer = 0.0f;
            ai.state = 2; // CREATURE_EAT (butchering animation)

            // Victory animation briefly first
            ai.state = 17; // CREATURE_VICTORY
        }
        break;

    case ServerCreatureAI::HUNT_BUTCHER:
        // Victory (1s) then butchering (4s)
        if (ai.huntTimer > 1.0f && ai.state == 17)
            ai.state = 2; // CREATURE_EAT (butchering)

        if (ai.huntTimer > 5.0f)
        {
            // Butchering complete — skilled hunters waste less meat
            {
                int meleeLevel = ai.isHuman ? GetNPCSkillLevel(ai.spawnId, SKILL_MELEE) : 0;
                float huntRestore = 40.0f * (1.0f + 0.03f * meleeLevel);
                // Master Hunter: +50% meat yield
                if (ai.isHuman && ai.isMasterHunter)
                    huntRestore *= 1.5f;
                ai.hunger = Min(100.0f, ai.hunger + huntRestore);
            }
            ai.huntPhase = ServerCreatureAI::HUNT_RETURN;
            ai.huntTimer = 0.0f;
            ai.state = 1; // CREATURE_WANDER (walk home)
            ai.moveSpeed = 2.0f;
            ai.targetPosition = ai.homePosition;
            ai.targetPosition.y_ = GetTerrainHeightAI(ai.homePosition.x_, ai.homePosition.z_);
            URHO3D_LOGDEBUG("[CreatureAI] Hunt success — returning home, hunger restored");
        }
        break;

    case ServerCreatureAI::HUNT_RETURN:
    {
        // Walking home — complete when close
        float distHome = (ai.position - ai.homePosition).Length();
        if (distHome < 5.0f || ai.huntTimer > 15.0f)
        {
            ai.taskTimer = 0.0f; // Force task completion → idle
        }
        break;
    }
    }
}

bool AuthServer::RollHuntSuccess(const ServerCreatureAI& ai)
{
    // Success chance based on hunger desperation and stamina.
    // Base 40% chance, +20% if very hungry (<30), -20% if exhausted (<20 stamina).
    float chance = 0.40f;
    if (ai.hunger < 30.0f)
        chance += 0.20f;
    if (ai.stamina < 20.0f)
        chance -= 0.20f;

    return Random(1.0f) < chance;
}

// ── Species classification (Phase 3 / mini-Phase 8) ──────────────────────────

bool AuthServer::IsHumanSpecies(int creatureId)
{
    // CaveMan=20, CaveWoman=21. Future: other tribes/NPCs go here.
    return creatureId == 20 || creatureId == 21;
}

bool AuthServer::IsPredatorSpecies(int creatureId)
{
    // Matches GameDB seed_data.sql aggression='aggressive'/'defensive'/'territorial'.
    // Wolf=5, Bull=6, Husky=12. Humans are NOT predators in the AI-hunt sense
    // (they're separate via isHuman) — they use tools, not natural predation.
    switch (creatureId)
    {
    case 5:  // Wolf
    case 6:  // Bull
    case 12: // Husky
        return true;
    default:
        return false;
    }
}

bool AuthServer::IsPreySpecies(int creatureId)
{
    // Everything else that's alive and fleeable. Prey for humans AND for predators.
    // Rabbit=1, Deer=2, Fox=3, Cow=7, Fish=8, Donkey=9, Horse=10, Alpaca=11, ShibaInu=13.
    // Fish excluded — hunters don't fish (that's a different chain).
    switch (creatureId)
    {
    case 1:  // Rabbit
    case 2:  // Deer
    case 3:  // Fox
    case 7:  // Cow
    case 9:  // Donkey
    case 10: // Horse
    case 11: // Alpaca
    case 13: // ShibaInu
        return true;
    default:
        return false;
    }
}

bool AuthServer::IsLandSpecies(int creatureId)
{
    // Fish (8) are aquatic and client-local — never in creatureAI_.
    return creatureId != 8;
}

bool AuthServer::IsScavengerSpecies(int creatureId)
{
    // Wolf=5, Fox=3, Husky=12 — matches client-side IsScavenger() overrides.
    return creatureId == 3 || creatureId == 5 || creatureId == 12;
}

void AuthServer::RegisterServerScent(const Vector3& pos, int speciesId, unsigned spawnId)
{
    if (serverScents_.Size() >= MAX_SERVER_SCENTS)
        serverScents_.Erase(0);
    ServerScentMarker m;
    m.position = pos;
    m.speciesId = speciesId;
    m.ageSeconds = 0.0f;
    m.spawnId = spawnId;
    serverScents_.Push(m);
}

int AuthServer::FindNearestServerScent(const Vector3& pos, float maxRadius)
{
    int bestIdx = -1;
    float bestDistSq = maxRadius * maxRadius;
    for (unsigned i = 0; i < serverScents_.Size(); ++i)
    {
        Vector3 diff = serverScents_[i].position - pos;
        float distSq = diff.LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestIdx = (int)i;
        }
    }
    return bestIdx;
}

void AuthServer::TickServerScents(float dt)
{
    for (unsigned i = serverScents_.Size(); i > 0; --i)
    {
        unsigned idx = i - 1;
        serverScents_[idx].ageSeconds += dt;
        if (serverScents_[idx].ageSeconds >= SERVER_SCENT_DURATION)
            serverScents_.Erase(idx);
    }
}

unsigned AuthServer::FindPreyNearScent(const Vector3& scentPos, float radius)
{
    // Find a live prey creature near the scent position.
    float bestDistSq = radius * radius;
    unsigned bestId = 0;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& ai = it->second_;
        if (ai.isPredator || ai.isHuman)
            continue; // only prey can be scavenged
        Vector3 diff = ai.position - scentPos;
        diff.y_ = 0.0f;
        float distSq = diff.LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = it->first_;
        }
    }
    return bestId;
}

unsigned AuthServer::FindHuntTarget(const ServerCreatureAI& hunter)
{
    // Scan creatureAI_ for nearest prey in the hunter's region within HUNT_RANGE.
    // Returns spawnId of target, or 0 if no prey in reach.
    float bestDistSq = HUNT_RANGE * HUNT_RANGE;
    unsigned bestId = 0;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& candidate = it->second_;
        if (!IsPreySpecies(candidate.creatureId))
            continue;
        if (candidate.regionId != hunter.regionId)
            continue;
        // Skip prey already being hunted to ground (CREATURE_DIE/CORPSE/etc)
        // State codes: 5=DIE, 13=CORPSE — don't re-target corpses
        if (candidate.state == 5 || candidate.state == 13)
            continue;

        // Phase 28: skip prey inside walled settlements
        if (candidate.isHuman && candidate.campfireId != 0 &&
            IsSettlementWalled(candidate.campfireId, hunter.creatureId))
            continue;

        Vector3 diff = candidate.position - hunter.position;
        diff.y_ = 0.0f;
        float distSq = diff.LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = it->first_;
        }
    }

    return bestId;
}

unsigned AuthServer::FindPredatorHuntTarget(const ServerCreatureAI& hunter)
{
    // Skilled NPCs hunt foxes (3) and wolves (5) — dangerous prey.
    if (!hunter.isHuman)
        return 0;

    int trackLvl = GetNPCSkillLevel(hunter.spawnId, SKILL_TRACKING);
    int meleeLvl = GetNPCSkillLevel(hunter.spawnId, SKILL_MELEE);
    if (trackLvl < 3 && meleeLvl < 4)
        return 0;

    bool canHuntWolf = (trackLvl >= 5 || meleeLvl >= 6);

    float bestDistSq = HUNT_RANGE * HUNT_RANGE;
    unsigned bestId = 0;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& candidate = it->second_;
        if (candidate.state == 5 || candidate.state == 13)
            continue;
        if (candidate.tamerId != 0)
            continue;

        bool isTarget = false;
        if (candidate.creatureId == 3)
            isTarget = true;
        else if (candidate.creatureId == 5 && canHuntWolf)
            isTarget = true;
        if (!isTarget)
            continue;

        if (candidate.regionId != hunter.regionId)
            continue;

        Vector3 diff = candidate.position - hunter.position;
        diff.y_ = 0.0f;
        float distSq = diff.LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = it->first_;
        }
    }
    return bestId;
}

// ── Phase 5: Predator Defense ────────────────────────────────────────────────

unsigned AuthServer::FindDefenseTarget(const ServerCreatureAI& defender)
{
    return FindDefenseTarget(defender, DEFENSE_TRIGGER_RANGE);
}

unsigned AuthServer::FindDefenseTarget(const ServerCreatureAI& defender, float range)
{
    // Scan creatureAI_ for nearest predator within given range of the
    // defender's home (campfire area). Skips dying/corpse states.
    float bestDistSq = range * range;
    unsigned bestId = 0;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& candidate = it->second_;
        if (!IsPredatorSpecies(candidate.creatureId))
            continue;
        // Skip dying/corpse states
        if (candidate.state == 5 || candidate.state == 13)
            continue;

        Vector3 diff = candidate.position - defender.homePosition;
        diff.y_ = 0.0f;
        float distSq = diff.LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = it->first_;
        }
    }

    return bestId;
}

void AuthServer::TickDefense(ServerCreatureAI& ai, float dt)
{
    ai.defenseTimer += dt;

    // Re-verify threat each tick. If predator gone or dead, abort defense.
    auto threatIt = creatureAI_.Find(ai.targetId);
    if (ai.targetId == 0 || threatIt == creatureAI_.End())
    {
        // Threat vanished — return to campfire
        ai.defensePhase = ServerCreatureAI::DEFENSE_RETURN;
        ai.defenseTimer = 0.0f;
    }

    switch (ai.defensePhase)
    {
    case ServerCreatureAI::DEFENSE_ALERT:
    {
        // 1-2 second stare before charging. Cooperative: only the NEAREST
        // defender among ALL defenders in ALERT on this threat charges.
        // Others hold until the charger fumbles or dies.
        if (ai.defenseTimer < Random(1.0f, 2.0f))
        {
            // Face the threat while holding
            if (threatIt != creatureAI_.End())
                ai.targetPosition = threatIt->second_.position;
            break;
        }

        // Time to decide. Am I the nearest defender in ALERT on this threat?
        bool iAmNearest = true;
        if (threatIt != creatureAI_.End())
        {
            float myDistSq = (ai.position - threatIt->second_.position).LengthSquared();
            for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
            {
                if (it->first_ == ai.spawnId)
                    continue;
                const ServerCreatureAI& peer = it->second_;
                if (!peer.isHuman)
                    continue;
                if (peer.currentTask != STASK_DEFEND)
                    continue;
                if (peer.targetId != ai.targetId)
                    continue;
                // Only peers who've completed ALERT are candidates for charging
                if (peer.defensePhase == ServerCreatureAI::DEFENSE_ALERT)
                    continue;
                // If someone else is already charging/striking this threat, hold
                if (peer.defensePhase == ServerCreatureAI::DEFENSE_CHARGE ||
                    peer.defensePhase == ServerCreatureAI::DEFENSE_STRIKE)
                {
                    iAmNearest = false;
                    break;
                }
                float peerDistSq = (peer.position - threatIt->second_.position).LengthSquared();
                if (peerDistSq < myDistSq)
                {
                    iAmNearest = false;
                    break;
                }
            }
        }

        if (iAmNearest)
        {
            ai.defensePhase = ServerCreatureAI::DEFENSE_CHARGE;
            ai.defenseTimer = 0.0f;
            ai.state = 15; // CREATURE_HUNT (charging — same anim semantics)
            ai.moveSpeed = 5.0f;
        }
        else
        {
            // Hold — reset alert timer so we can pick up if the charger fails
            ai.defenseTimer = 0.0f;
            if (threatIt != creatureAI_.End())
                ai.targetPosition = threatIt->second_.position;
        }
        break;
    }

    case ServerCreatureAI::DEFENSE_CHARGE:
    {
        // Run toward predator at full speed
        if (threatIt != creatureAI_.End())
            ai.targetPosition = threatIt->second_.position;

        float strikeDist = (threatIt != creatureAI_.End())
            ? (ai.position - threatIt->second_.position).Length()
            : 999.0f;

        if (strikeDist < 2.5f)
        {
            ai.defensePhase = ServerCreatureAI::DEFENSE_STRIKE;
            ai.defenseTimer = 0.0f;
            ai.state = 4; // CREATURE_FIGHT
            ai.moveSpeed = 0.0f;
        }
        else if (ai.defenseTimer > 10.0f)
        {
            // Can't reach threat — abandon, return home. Peers can try again.
            ai.defensePhase = ServerCreatureAI::DEFENSE_RETURN;
            ai.defenseTimer = 0.0f;
        }
        break;
    }

    case ServerCreatureAI::DEFENSE_STRIKE:
    {
        // One swing. CombatResolver rolls. On hit: drive predator off (set flee
        // state + target far from campfire). On kill: full death path. On fumble:
        // return to ALERT so a peer can take over.
        if (threatIt == creatureAI_.End() || !combatResolver_)
        {
            ai.defensePhase = ServerCreatureAI::DEFENSE_RETURN;
            ai.defenseTimer = 0.0f;
            break;
        }

        // Lazy-register threat in creatureStates_ for HP tracking
        auto stateIt = creatureStates_.Find(ai.targetId);
        if (stateIt == creatureStates_.End())
        {
            ServerCreatureState fresh;
            fresh.creatureId = threatIt->second_.creatureId;
            if (!LoadCreatureCombat(threatIt->second_.creatureId, fresh))
            {
                fresh.hp = fresh.maxHp = 10;
                fresh.defense = 13;
                fresh.damage = 5;
                fresh.damageVar = 4;
            }
            fresh.position = threatIt->second_.position;
            fresh.regionId = threatIt->second_.regionId;
            creatureStates_[ai.targetId] = fresh;
            stateIt = creatureStates_.Find(ai.targetId);
        }
        ServerCreatureState& cs = stateIt->second_;

        // Defender's attack — base 4 (+1 charge momentum), scales with melee + defense.
        // Defense skill helps the NPC read the predator's movements (attack bonus).
        int meleeLevel = GetNPCSkillLevel(ai.spawnId, SKILL_MELEE);
        int defLevel = GetNPCSkillLevel(ai.spawnId, SKILL_DEFENSE);
        int attackMod = 4 + meleeLevel / 2 + defLevel / 3;
        int dmgBase = 5 + meleeLevel / 3;
        AttackResult result = combatResolver_->ResolveAttack(
            attackMod, dmgBase, 4, cs.defense, cs.hp);

        if (result.hit)
        {
            cs.hp = Max(0, cs.hp - result.damage);
            CancelTradeIfPossessedDamaged(ai.targetId);
        }

        // D&D: defending your home is the best combat training
        NPCAwardXP(ai, result.hit ? "melee_hit" : "melee_miss");

        URHO3D_LOGINFOF("[CreatureAI] Defense strike on spawnId=%u — roll=%d %s%s%s dmg=%d hp=%d/%d",
            ai.targetId, result.roll,
            result.hit ? "HIT" : "MISS",
            result.crit ? " CRIT" : "",
            result.fumble ? " FUMBLE" : "",
            result.damage, cs.hp, cs.maxHp);

        if (cs.hp == 0)
        {
            // Predator killed — full death path, feed PopulationManager,
            // erase from both maps, defender returns triumphant.
            NPCAwardXP(ai, "melee_kill");
            BroadcastCreatureDeath(ai.targetId, cs, nullptr);
            if (populationManager_ && populationManager_->IsReady() && cs.regionId >= 0)
            {
                Vector<ReplacementSpawn> replacements =
                    populationManager_->RecordKill(cs.regionId, cs.creatureId);
                for (unsigned i = 0; i < replacements.Size(); ++i)
                {
                    const ReplacementSpawn& rep = replacements[i];
                    Vector3 spawnPos = populationManager_->PickSpawnPositionInRegion(rep.regionId);
                    BroadcastSpawnCreature(rep.regionId, rep.creatureId, spawnPos, 0.0f);
                }
            }
            creatureStates_.Erase(stateIt);
            creatureAI_.Erase(threatIt);
            ai.targetId = 0;

            // Victory anim briefly, then return home
            ai.state = 17; // CREATURE_VICTORY
            ai.defensePhase = ServerCreatureAI::DEFENSE_RETURN;
            ai.defenseTimer = 0.0f;
        }
        else if (result.hit)
        {
            // Survived but hurt — drive it off. Set predator AI to flee toward
            // a point DEFENSE_DRIVEOFF_RANGE away from campfire.
            ServerCreatureAI& predator = threatIt->second_;
            Vector3 away = predator.position - ai.homePosition;
            away.y_ = 0.0f;
            if (away.LengthSquared() > 0.01f)
            {
                away.Normalize();
                predator.targetPosition = ai.homePosition + away * DEFENSE_DRIVEOFF_RANGE;
                predator.targetPosition.y_ = GetTerrainHeightAI(
                    predator.targetPosition.x_, predator.targetPosition.z_);
            }
            predator.state = 3; // CREATURE_FLEE
            predator.moveSpeed = 6.0f;
            BroadcastCreatureAIState(threatIt->first_, predator);

            ai.defensePhase = ServerCreatureAI::DEFENSE_DRIVE_OFF;
            ai.defenseTimer = 0.0f;
            ai.state = 17; // CREATURE_VICTORY (drove it off)
        }
        else if (result.fumble)
        {
            // Critical miss — step back to ALERT so a peer can take over
            ai.defensePhase = ServerCreatureAI::DEFENSE_ALERT;
            ai.defenseTimer = 0.0f;
            ai.state = 16; // CREATURE_ALERT
            URHO3D_LOGINFOF("[CreatureAI] Defender spawnId=%d fumbled — yielding", ai.creatureId);
        }
        else
        {
            // Regular miss — swing again next tick
            ai.defenseTimer = 0.0f;
        }
        break;
    }

    case ServerCreatureAI::DEFENSE_DRIVE_OFF:
    {
        // Verify predator has been driven far enough, then return home
        float distFromHome = (threatIt != creatureAI_.End())
            ? (threatIt->second_.position - ai.homePosition).Length()
            : DEFENSE_DRIVEOFF_RANGE + 1.0f;
        if (distFromHome > DEFENSE_DRIVEOFF_RANGE || ai.defenseTimer > 5.0f)
        {
            ai.defensePhase = ServerCreatureAI::DEFENSE_RETURN;
            ai.defenseTimer = 0.0f;
        }
        break;
    }

    case ServerCreatureAI::DEFENSE_RETURN:
    {
        // Walk back to campfire
        ai.state = 1; // CREATURE_WANDER
        ai.moveSpeed = 2.5f;
        ai.targetPosition = ai.homePosition;
        ai.targetPosition.y_ = GetTerrainHeightAI(ai.homePosition.x_, ai.homePosition.z_);

        float distHome = (ai.position - ai.homePosition).Length();
        if (distHome < 5.0f || ai.defenseTimer > 15.0f)
        {
            // Phase 36: Guards resume patrol after combat instead of re-evaluating
            if (ai.prevTask == STASK_GUARD)
            {
                ai.currentTask = STASK_GUARD;
                ai.prevTask = STASK_IDLE;
                StartCreatureTask(ai, STASK_GUARD);
                URHO3D_LOGINFOF("[Guard] NPC %u resuming guard patrol after combat",
                    ai.spawnId);
            }
            else
            {
                ai.taskTimer = 0.0f; // Force task re-evaluation
            }
        }
        break;
    }
    }
}

// ── Phase 9: Server-Side Animal AI ──────────────────────────────────────────

unsigned AuthServer::FindFleeTarget(const ServerCreatureAI& prey)
{
    // Scan for nearest predator or human within ANIMAL_FLEE_RANGE.
    // Prey flees from predators AND humans. Predators flee from humans only.
    // Phase 16: Tamed animals don't flee from humans — only from predators.
    float bestDistSq = ANIMAL_FLEE_RANGE * ANIMAL_FLEE_RANGE;
    unsigned bestId = 0;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& candidate = it->second_;
        if (&candidate == &prey)
            continue;
        // Skip dying/corpse
        if (candidate.state == 5 || candidate.state == 13)
            continue;

        bool isThreat = false;
        if (!prey.isPredator)
        {
            // Tamed animals only flee predators, not humans
            if (prey.tamerId != 0)
                isThreat = candidate.isPredator;
            else
                isThreat = candidate.isPredator || candidate.isHuman;
        }
        else
        {
            // Predators flee humans only
            isThreat = candidate.isHuman;
        }
        if (!isThreat)
            continue;

        Vector3 diff = candidate.position - prey.position;
        diff.y_ = 0.0f;
        float distSq = diff.LengthSquared();

        // Master Hunter: prey detects human threats 20% later (reduced flee range)
        float effectiveRangeSq = bestDistSq;
        if (candidate.isHuman && HasMasterHunter(candidate.campfireId))
            effectiveRangeSq = (ANIMAL_FLEE_RANGE * 0.8f) * (ANIMAL_FLEE_RANGE * 0.8f);

        if (distSq < effectiveRangeSq && distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = it->first_;
        }
    }

    return bestId;
}

int AuthServer::PickAnimalTask(const ServerCreatureAI& ai)
{
    // 1. FLEE — threat within range
    unsigned threatId = FindFleeTarget(ai);
    if (threatId != 0)
        return STASK_FLEE;

    // 2a. Fox hunting: foxes hunt rabbits when hungry (not classified as predator)
    if (ai.creatureId == 3 && ai.hunger < 50.0f)
        return STASK_HUNT;

    // 2. Predators: HUNT when hungry — but walled settlements deter approach
    if (ai.isPredator && ai.hunger < 60.0f)
    {
        // Phase 28: check if nearest prey is behind walls that block this species
        unsigned huntTarget = FindHuntTarget(ai);
        if (huntTarget != 0)
        {
            auto preyIt = creatureAI_.Find(huntTarget);
            if (preyIt != creatureAI_.End() && preyIt->second_.campfireId != 0)
            {
                if (IsSettlementWalled(preyIt->second_.campfireId, ai.creatureId))
                {
                    // Walls block this predator — damage the wall instead and give up
                    auto cfIt = serverCampfires_.Find(preyIt->second_.campfireId);
                    if (cfIt != serverCampfires_.End())
                        DamageNearestWall(ai.position, preyIt->second_.campfireId, WALL_ATTACK_DAMAGE);
                    return STASK_WANDER;  // deterred, wander away
                }
            }
        }
        return STASK_HUNT;
    }

    // 3. EAT — hungry (prey grazes, predators abstract-eat after hunt fails)
    if (ai.hunger < 30.0f)
        return STASK_EAT;

    // 3b. SCAVENGE — scavenger species detect nearby death scents
    if (IsScavengerSpecies(ai.creatureId) &&
        FindNearestServerScent(ai.position, SCAVENGE_DETECTION_RADIUS) >= 0)
        return STASK_SCAVENGE;

    // 4. SLEEP — exhausted or deep night
    if (ai.stamina < 20.0f)
        return STASK_SLEEP;
    if (GetDarkness() > 0.8f)
        return STASK_SLEEP;

    // 5. WANDER — 40% chance during daylight
    if (GetDarkness() < 0.3f && Random(1.0f) < 0.4f)
        return STASK_WANDER;

    // 6. IDLE — default
    return STASK_IDLE;
}

float AuthServer::GetDarkness() const
{
    // Determine the effective UTC time — either epoch override, legacy override, or wallclock
    time_t effectiveTime;
    if (utcEpochOverride_ >= 0)
    {
        // Privileged client sent their local UTC + scrub delta as the new time basis
        effectiveTime = (time_t)utcEpochOverride_;
    }
    else if (timeOverrideHour_ >= 0.0f)
    {
        // Legacy absolute hour+doy override
        float localHour = timeOverrideHour_;
        int dayOfYear = timeOverrideDOY_ > 0 ? timeOverrideDOY_ : 100;
        float decl = 23.44f * sinf((360.0f / 365.0f) * (dayOfYear - 81) * M_PI / 180.0f);
        float hourAngle = 15.0f * (localHour - 12.0f);
        float latRad = -37.8f * M_PI / 180.0f;
        float declRad = decl * M_PI / 180.0f;
        float haRad = hourAngle * M_PI / 180.0f;
        float sinAlt = sinf(latRad) * sinf(declRad) + cosf(latRad) * cosf(declRad) * cosf(haRad);
        float sunAltDeg = asinf(Clamp(sinAlt, -1.0f, 1.0f)) * 180.0f / M_PI;
        if (sunAltDeg >= 10.0f) return 0.0f;
        if (sunAltDeg <= -6.0f) return 1.0f;
        return 1.0f - (sunAltDeg + 6.0f) / 16.0f;
    }
    else
    {
        effectiveTime = time(nullptr);
    }

    // Derive sun altitude from effective UTC at Melbourne latitude (-37.8).
    struct tm* utc = gmtime(&effectiveTime);
    float utcHour = (float)utc->tm_hour + utc->tm_min / 60.0f + utc->tm_sec / 3600.0f;
    int dayOfYear = utc->tm_yday + 1;

    // Melbourne is UTC+10 (ignoring DST for simplicity)
    float localHour = utcHour + 10.0f;
    if (localHour >= 24.0f) localHour -= 24.0f;

    // Solar declination (degrees)
    float decl = 23.44f * sinf((360.0f / 365.0f) * (dayOfYear - 81) * M_PI / 180.0f);
    // Hour angle: 15 degrees per hour from solar noon (~12:00 local)
    float hourAngle = 15.0f * (localHour - 12.0f);

    float latRad = -37.8f * M_PI / 180.0f;
    float declRad = decl * M_PI / 180.0f;
    float haRad = hourAngle * M_PI / 180.0f;

    float sinAlt = sinf(latRad) * sinf(declRad) + cosf(latRad) * cosf(declRad) * cosf(haRad);
    float sunAltDeg = asinf(Clamp(sinAlt, -1.0f, 1.0f)) * 180.0f / M_PI;

    // Convert sun altitude to darkness: above 10° = full daylight, below -6° = full dark
    if (sunAltDeg >= 10.0f) return 0.0f;
    if (sunAltDeg <= -6.0f) return 1.0f;
    return 1.0f - (sunAltDeg + 6.0f) / 16.0f;
}

float AuthServer::GetEffectiveTemperature() const
{
    // BOM gives weatherTemperature_ at real wallclock time.
    // When time is scrubbed, interpolate temperature along a diurnal curve:
    // cosine centered on 15:00 (peak warmth), trough at 03:00 (coldest).
    // Diurnal amplitude = 5°C (range ~10°C for Melbourne).

    static constexpr float DIURNAL_AMPLITUDE = 5.0f;
    static constexpr float PEAK_HOUR = 15.0f;  // 3pm local = warmest

    // Get real wallclock Melbourne hour
    time_t now = time(nullptr);
    struct tm* utcNow = gmtime(&now);
    float realHour = (float)utcNow->tm_hour + utcNow->tm_min / 60.0f + 10.0f;  // UTC+10
    if (realHour >= 24.0f) realHour -= 24.0f;

    // Get effective (scrubbed) Melbourne hour — same logic as GetDarkness
    float effectiveHour;
    if (utcEpochOverride_ >= 0)
    {
        time_t eff = (time_t)utcEpochOverride_;
        struct tm* utcEff = gmtime(&eff);
        effectiveHour = (float)utcEff->tm_hour + utcEff->tm_min / 60.0f + 10.0f;
        if (effectiveHour >= 24.0f) effectiveHour -= 24.0f;
    }
    else if (timeOverrideHour_ >= 0.0f)
    {
        effectiveHour = timeOverrideHour_;
    }
    else
    {
        return weatherTemperature_;  // no scrub — use BOM directly
    }

    // Where on the diurnal curve is the real observation?
    float realPhase = (realHour - PEAK_HOUR) * M_PI / 12.0f;
    float realCurve = cosf(realPhase);  // +1 at peak, -1 at trough

    // BOM temp = midpoint + amplitude * realCurve → solve for midpoint
    float midpoint = weatherTemperature_ - DIURNAL_AMPLITUDE * realCurve;

    // Now evaluate at the scrubbed hour
    float scrubPhase = (effectiveHour - PEAK_HOUR) * M_PI / 12.0f;
    return midpoint + DIURNAL_AMPLITUDE * cosf(scrubPhase);
}

// ── Phase 6: Server-side campfire state ─────────────────────────────────

unsigned AuthServer::AssignCampfireForNPC(const Vector3& homePos, int regionId)
{
    // Reuse nearest existing campfire within CAMPFIRE_ADOPT_RADIUS — so
    // cavemen spawning in a cluster share one fire instead of one per body.
    unsigned bestId = 0;
    float bestDist = CAMPFIRE_ADOPT_RADIUS;
    for (auto it = serverCampfires_.Begin(); it != serverCampfires_.End(); ++it)
    {
        float d = (it->second_.position - homePos).Length();
        if (d < bestDist)
        {
            bestDist = d;
            bestId = it->first_;
        }
    }
    if (bestId != 0)
        return bestId;

    // No nearby fire — create one at this NPC's home position.
    unsigned id = ++nextCampfireId_;
    ServerCampfire cf;
    cf.position = homePos;
    cf.regionId = regionId;
    serverCampfires_[id] = cf;
    URHO3D_LOGINFOF("[CreatureAI] Campfire %u created at (%.1f, %.1f) region %d",
        id, homePos.x_, homePos.z_, regionId);
    // Phase 3: tell any already-connected clients the new pit exists.
    // Late joiners catch up via the heartbeat (PIT_HEARTBEAT_INTERVAL).
    BroadcastPitState(id, cf);
    return id;
}

void AuthServer::TickCampfires(float dt)
{
    // Real-wallclock fuel decay matches the client-side model (TerrainNode::UpdateCampfireFuel).
    // Phase 3: drives the FirePitState machine and broadcasts on transitions.
    for (auto it = serverCampfires_.Begin(); it != serverCampfires_.End(); ++it)
    {
        ServerCampfire& cf = it->second_;
        FirePitState prevState = cf.state;

        // Non-linear burn: DrivenKey curve maps fuel ratio to burn multiplier.
        // Full fire roars (mult≈1.0), low fuel glows as embers (mult≈0.1).
        float fuelRatio = (cf.maxFuelSeconds > 0.0f) ? cf.fuelSeconds / cf.maxFuelSeconds : 0.0f;
        float curveMultiplier = burnCurveKey_.points.Empty() ? 1.0f : burnCurveKey_.Evaluate(fuelRatio);
        float effectiveBurnRate = cf.burnRate * curveMultiplier * (1.0f - 0.7f * cf.wetness);
        if (effectiveBurnRate < 0.05f) effectiveBurnRate = 0.05f;
        if (cf.fuelSeconds > 0.0f)
        {
            cf.fuelSeconds -= effectiveBurnRate * dt;
            if (cf.fuelSeconds < 0.0f)
                cf.fuelSeconds = 0.0f;
        }

        // State transitions (one-way unless tended)
        if (cf.fuelSeconds <= 0.0f)
        {
            // LIT/EMBERS → COLD when burned out. UNLIT pits stay UNLIT (never lit).
            if (cf.state == PIT_LIT || cf.state == PIT_EMBERS)
                cf.state = PIT_COLD;
        }
        else if (cf.fuelSeconds <= PIT_EMBERS_THRESHOLD)
        {
            if (cf.state == PIT_LIT)
                cf.state = PIT_EMBERS;
        }
        else
        {
            // Plenty of fuel — stay LIT (or recover from EMBERS via tend, handled in TendCampfire)
            if (cf.state == PIT_EMBERS)
                cf.state = PIT_LIT;
        }

        // Phase 4a: tick active friction ignition on this pit
        if (cf.ignitionActive)
        {
            // Check ruin: owner must still be near the pit
            bool ruined = false;
            if (cf.ignitionByNPC)
            {
                auto aiIt = creatureAI_.Find(cf.ignitionNPCSpawnId);
                if (aiIt == creatureAI_.End() ||
                    aiIt->second_.currentTask != STASK_MAKE_FIRE ||
                    (aiIt->second_.position - cf.position).Length() > FRICTION_IGNITION_RANGE)
                    ruined = true;
            }
            else
            {
                // Player ignitor — check avatar proximity
                auto avIt = serverObjects_.Find(cf.ignitionPlayerConn);
                if (avIt == serverObjects_.End() || !avIt->second_ ||
                    (avIt->second_->GetWorldPosition() - cf.position).Length() > FRICTION_IGNITION_RANGE)
                    ruined = true;
            }

            if (ruined)
            {
                RuinIgnition(it->first_, cf, "owner moved away");
            }
            else
            {
                cf.ignitionProgress += dt;

                // Skill-adjusted ignition time: players AND NPCs benefit from FireMaking skill
                float requiredTime = FRICTION_IGNITION_TIME;
                if (cf.ignitionByNPC && cf.ignitionNPCSpawnId != 0)
                {
                    requiredTime = GetSkillAdjustedIgnitionTime(GetNPCPlayerId(cf.ignitionNPCSpawnId));
                }
                else if (!cf.ignitionByNPC && cf.ignitionPlayerConn)
                {
                    auto sessIt = sessions_.Find(cf.ignitionPlayerConn);
                    if (sessIt != sessions_.End() && !sessIt->second_.username.Empty())
                        requiredTime = GetSkillAdjustedIgnitionTime(GetPlayerId(sessIt->second_.username));
                }

                if (cf.ignitionProgress >= requiredTime)
                {
                    // Ignition complete — light the fire
                    cf.ignitionActive = false;
                    cf.ignitionProgress = 0.0f;
                    cf.fuelSeconds = Min(FRICTION_INITIAL_FUEL, cf.maxFuelSeconds);
                    cf.state = PIT_LIT;

                    // Award FireMaking XP to igniters (player or NPC)
                    if (!cf.ignitionByNPC && cf.ignitionPlayerConn)
                    {
                        auto sessIt = sessions_.Find(cf.ignitionPlayerConn);
                        if (sessIt != sessions_.End() && gameDB_)
                        {
                            int pid = GetPlayerId(sessIt->second_.username);
                            if (pid > 0)
                                gameDB_->AwardXP(pid, "fire_ignite");
                        }
                    }
                    else if (cf.ignitionByNPC && cf.ignitionNPCSpawnId != 0 && gameDB_)
                    {
                        int npcPid = GetNPCPlayerId(cf.ignitionNPCSpawnId);
                        if (npcPid > 0)
                            gameDB_->AwardXP(npcPid, "fire_ignite");
                    }

                    cf.ignitionPlayerConn = nullptr;
                    cf.ignitionNPCSpawnId = 0;
                    URHO3D_LOGINFOF("[Phase4a] Friction ignition COMPLETE on pit %u — now LIT (%.0f fuel, required %.0fs)",
                        it->first_, cf.fuelSeconds, requiredTime);

                    // Broadcast ignition-done status to all clients
                    VariantMap status;
                    status[P_PIT_ID] = it->first_;
                    status[P_PIT_IGNITION_ACTIVE] = false;
                    status[P_PIT_IGNITION_PROGRESS] = 1.0f;
                    auto* network = GetSubsystem<Network>();
                    if (network)
                    {
                        for (auto cIt = sessions_.Begin(); cIt != sessions_.End(); ++cIt)
                            cIt->first_->SendRemoteEvent(E_PIT_IGNITION_STATUS, true, status);
                    }
                }
            }
        }

        // Broadcast on state change immediately. Otherwise heartbeat every PIT_HEARTBEAT_INTERVAL
        // so clients re-sync extrapolation without growing drift.
        bool stateChanged = (cf.state != prevState);
        float& hb = pitHeartbeatTimers_[it->first_];
        hb += dt;
        if (stateChanged || hb >= PIT_HEARTBEAT_INTERVAL)
        {
            hb = 0.0f;
            BroadcastPitState(it->first_, cf);
        }
    }

    // Dead fire pits stay COLD long enough for NPCs to react (task eval is 2s).
    // After this, NPCs are orphaned and must adopt or build a new fire.
    static constexpr float PIT_COLD_LIFETIME = 30.0f;
    Vector<unsigned> deadPits;
    for (auto it = serverCampfires_.Begin(); it != serverCampfires_.End(); ++it)
    {
        if (it->second_.state == PIT_COLD)
        {
            it->second_.coldTimer += dt;
            if (it->second_.coldTimer >= PIT_COLD_LIFETIME)
                deadPits.Push(it->first_);
        }
    }
    for (unsigned i = 0; i < deadPits.Size(); ++i)
    {
        unsigned pitId = deadPits[i];
        URHO3D_LOGINFOF("[Campfire] Pit %u burned out — removing", pitId);

        // Orphan NPCs that belonged to this fire
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            if (aiIt->second_.campfireId == pitId)
            {
                aiIt->second_.campfireId = 0;
                aiIt->second_.settlementId = 0;
            }
        }

        serverCampfires_.Erase(pitId);
        pitHeartbeatTimers_.Erase(pitId);
    }
}

float AuthServer::ApplyWetnessToTendDelivery(float rawBu, float wetness)
{
    // Wet wood delivers fewer burn-units. k=0.5 — fully soaked wood gives half value.
    float k = Clamp(wetness, 0.0f, 1.0f);
    return rawBu * (1.0f - 0.5f * k);
}

float AuthServer::GetSkillAdjustedIgnitionTime(int playerId) const
{
#ifdef URHO3D_DATABASE_SQLITE
    if (gameDB_ && playerId > 0)
    {
        int level = gameDB_->GetSkillLevel(playerId, SKILL_FIREMAKING);
        // base / (1 + 0.1 * level): level 0 = 7200s, level 5 = 4800s, level 10 = 3600s
        return FRICTION_IGNITION_TIME / (1.0f + 0.1f * static_cast<float>(level));
    }
#endif
    return FRICTION_IGNITION_TIME;
}

void AuthServer::BroadcastPitState(unsigned pitId, const ServerCampfire& cf)
{
    VariantMap data;
    data[P_PIT_ID] = (unsigned)pitId;
    data[P_PIT_STATE] = (unsigned)cf.state;
    data[P_PIT_BURN_UNITS] = cf.fuelSeconds;
    float fuelRatio = (cf.maxFuelSeconds > 0.0f) ? cf.fuelSeconds / cf.maxFuelSeconds : 0.0f;
    float curveMultiplier = burnCurveKey_.points.Empty() ? 1.0f : burnCurveKey_.Evaluate(fuelRatio);
    data[P_PIT_BURN_RATE] = cf.burnRate * curveMultiplier * (1.0f - 0.7f * cf.wetness);
    data[P_PIT_MAX_FUEL] = cf.maxFuelSeconds;
    data[P_PIT_WETNESS] = cf.wetness;
    data[P_PIT_POS_X] = cf.position.x_;
    data[P_PIT_POS_Z] = cf.position.z_;
    data[P_PIT_SIGNAL] = (unsigned)cf.signalType;
    // UTC ms since epoch — client computes (now - utcMs)/1000 * burnRate to extrapolate.
    long long utcMs = (long long)time(nullptr) * 1000LL;
    data[P_PIT_UTC_MS] = utcMs;

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendRemoteEvent(E_PIT_STATE_CHANGED, true, data);
    }
}

void AuthServer::HandlePitTendRequest(StringHash, VariantMap& eventData)
{
    using namespace RemoteEventData;
    Connection* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (!connection)
        return;

    auto sit = sessions_.Find(connection);
    if (sit == sessions_.End() || !sit->second_.authenticated)
        return; // Unauth session — silently ignore

    unsigned pitId = eventData[P_PIT_ID].GetU32();
    int itemId     = (int)eventData[P_PIT_TEND_ITEM].GetI32();
    int quantity   = (int)eventData[P_PIT_TEND_QTY].GetI32();
    if (quantity <= 0)
        return;

    auto pit = serverCampfires_.Find(pitId);
    if (pit == serverCampfires_.End())
    {
        URHO3D_LOGINFOF("[Phase4b] Tend reject — unknown pitId=%u from %s",
            pitId, sit->second_.username.CString());
        return;
    }
    ServerCampfire& cf = pit->second_;

    // Phase 4b semantic: embers revival is the ONLY player-tend path right now.
    // LIT top-up needs no help (wait for it to drop to EMBERS); COLD/UNLIT need
    // Phase 4a friction. EMBERS = the cheap window.
    if (cf.state != PIT_EMBERS)
    {
        URHO3D_LOGINFOF("[Phase4b] Tend reject — pit %u not in EMBERS (state=%d)",
            pitId, cf.state);
        return;
    }

    // Embers revival: softwood only. Hardwood doesn't catch from embers — needs
    // friction or an existing flame.
    if (itemId != ITEM_SOFTWOOD)
    {
        URHO3D_LOGINFOF("[Phase4b] Tend reject — embers revival requires Softwood (got item %d)",
            itemId);
        return;
    }

    // Proximity check — player must be near the pit.
    auto avIt = serverObjects_.Find(connection);
    if (avIt == serverObjects_.End() || !avIt->second_)
        return;
    Vector3 playerPos = avIt->second_->GetWorldPosition();
    if ((playerPos - cf.position).Length() > 5.0f)
    {
        URHO3D_LOGINFOF("[Phase4b] Tend reject — player too far from pit %u", pitId);
        return;
    }

    // Inventory validation + consume.
#ifdef URHO3D_DATABASE_SQLITE
    int playerId = GetPlayerId(sit->second_.username);
    if (playerId < 0 || !worldDB_)
        return;
    if (worldDB_->GetItemCount(playerId, ITEM_SOFTWOOD) < quantity)
    {
        URHO3D_LOGINFOF("[Phase4b] Tend reject — player %d lacks %d Softwood",
            playerId, quantity);
        return;
    }
    worldDB_->RemoveItemFromInventory(playerId, ITEM_SOFTWOOD, quantity);
    SendInventoryUpdate(connection, playerId);
#endif

    // Apply tend. Each softwood = CAMPFIRE_STICK_BURN burn-units, modulated by wetness.
    float delivered = ApplyWetnessToTendDelivery(CAMPFIRE_STICK_BURN * (float)quantity, cf.wetness);
    float before = cf.fuelSeconds;
    cf.fuelSeconds = Min(cf.fuelSeconds + delivered, cf.maxFuelSeconds);

    // Embers revival → LIT (or stays EMBERS if delivery was tiny and didn't clear threshold)
    FirePitState prevState = cf.state;
    cf.state = (cf.fuelSeconds > PIT_EMBERS_THRESHOLD) ? PIT_LIT : PIT_EMBERS;

    // Award FireMaking XP for tending
#ifdef URHO3D_DATABASE_SQLITE
    if (gameDB_ && playerId > 0)
        gameDB_->AwardXP(playerId, "fire_tend");
#endif

    URHO3D_LOGINFOF("[Phase4b] Player %s revived pit %u with %d Softwood (%.0f -> %.0f, state %d->%d)",
        sit->second_.username.CString(), pitId, quantity, before, cf.fuelSeconds, prevState, cf.state);

    BroadcastPitState(pitId, cf);
}

// ---------------------------------------------------------------------------
// Phase 4a: Friction ignition — player-initiated
// ---------------------------------------------------------------------------
void AuthServer::HandlePitIgniteRequest(StringHash, VariantMap& eventData)
{
    using namespace NetworkMessage;
    auto* sender = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (!sender)
        return;
    auto sit = sessions_.Find(sender);
    if (sit == sessions_.End() || !sit->second_.authenticated)
        return;

    unsigned pitId = eventData[P_PIT_ID].GetU32();
    auto pit = serverCampfires_.Find(pitId);
    if (pit == serverCampfires_.End())
    {
        URHO3D_LOGINFOF("[Phase4a] Ignite reject — unknown pitId=%u", pitId);
        return;
    }
    ServerCampfire& cf = pit->second_;

    // Only COLD or UNLIT pits can be friction-lit. EMBERS → use Phase 4b tend.
    if (cf.state != PIT_COLD && cf.state != PIT_UNLIT)
    {
        URHO3D_LOGINFOF("[Phase4a] Ignite reject — pit %u state=%d (need COLD or UNLIT)", pitId, cf.state);
        return;
    }

    // Already igniting?
    if (cf.ignitionActive)
    {
        URHO3D_LOGINFOF("[Phase4a] Ignite reject — pit %u already has active ignition", pitId);
        return;
    }

    // Proximity check
    auto avIt = serverObjects_.Find(sender);
    if (avIt == serverObjects_.End() || !avIt->second_)
        return;
    Vector3 playerPos = avIt->second_->GetWorldPosition();
    if ((playerPos - cf.position).Length() > 5.0f)
    {
        URHO3D_LOGINFOF("[Phase4a] Ignite reject — player too far from pit %u", pitId);
        return;
    }

    // Inventory: need both softwood (15) AND hardwood (16)
#ifdef URHO3D_DATABASE_SQLITE
    int playerId = GetPlayerId(sit->second_.username);
    if (playerId < 0 || !worldDB_)
        return;
    if (worldDB_->GetItemCount(playerId, ITEM_SOFTWOOD) < 1)
    {
        URHO3D_LOGINFOF("[Phase4a] Ignite reject — player %d lacks Softwood", playerId);
        return;
    }
    if (worldDB_->GetItemCount(playerId, ITEM_HARDWOOD) < 1)
    {
        URHO3D_LOGINFOF("[Phase4a] Ignite reject — player %d lacks Hardwood", playerId);
        return;
    }
    // Consume wood immediately — lost on ruin (the punishment for letting fire die)
    worldDB_->RemoveItemFromInventory(playerId, ITEM_SOFTWOOD, 1);
    worldDB_->RemoveItemFromInventory(playerId, ITEM_HARDWOOD, 1);
    SendInventoryUpdate(sender, playerId);
#endif

    // Start ignition
    cf.ignitionActive = true;
    cf.ignitionProgress = 0.0f;
    cf.ignitionByNPC = false;
    cf.ignitionNPCSpawnId = 0;
    cf.ignitionPlayerConn = sender;

    URHO3D_LOGINFOF("[Phase4a] Player %s started friction ignition on pit %u (%.0fs to complete)",
        sit->second_.username.CString(), pitId, FRICTION_IGNITION_TIME);

    // Notify all clients that ignition is active
    VariantMap status;
    status[P_PIT_ID] = pitId;
    status[P_PIT_IGNITION_ACTIVE] = true;
    status[P_PIT_IGNITION_PROGRESS] = 0.0f;
    auto* network = GetSubsystem<Network>();
    if (network)
    {
        for (auto cIt = sessions_.Begin(); cIt != sessions_.End(); ++cIt)
            cIt->first_->SendRemoteEvent(E_PIT_IGNITION_STATUS, true, status);
    }
}

void AuthServer::RuinIgnition(unsigned pitId, ServerCampfire& cf, const char* reason)
{
    URHO3D_LOGINFOF("[Phase4a] Ignition RUINED on pit %u — %s (%.0fs wasted)",
        pitId, reason, cf.ignitionProgress);

    cf.ignitionActive = false;
    cf.ignitionProgress = 0.0f;
    cf.ignitionPlayerConn = nullptr;
    cf.ignitionNPCSpawnId = 0;

    // Notify clients
    VariantMap status;
    status[P_PIT_ID] = pitId;
    status[P_PIT_IGNITION_ACTIVE] = false;
    status[P_PIT_IGNITION_PROGRESS] = 0.0f;
    auto* network = GetSubsystem<Network>();
    if (network)
    {
        for (auto cIt = sessions_.Begin(); cIt != sessions_.End(); ++cIt)
            cIt->first_->SendRemoteEvent(E_PIT_IGNITION_STATUS, true, status);
    }
}

void AuthServer::NPCBeginIgnition(ServerCreatureAI& ai)
{
    if (ai.campfireId == 0)
        return;
    auto it = serverCampfires_.Find(ai.campfireId);
    if (it == serverCampfires_.End())
        return;
    ServerCampfire& cf = it->second_;

    // Only ignite COLD or UNLIT pits
    if (cf.state != PIT_COLD && cf.state != PIT_UNLIT)
        return;
    // Don't double-start
    if (cf.ignitionActive)
        return;

    cf.ignitionActive = true;
    cf.ignitionProgress = 0.0f;
    cf.ignitionByNPC = true;
    cf.ignitionNPCSpawnId = ai.spawnId;
    cf.ignitionPlayerConn = nullptr;

    URHO3D_LOGINFOF("[Phase4a] NPC spawnId=%u started friction ignition on pit %u",
        ai.spawnId, ai.campfireId);

    // Notify clients
    VariantMap status;
    status[P_PIT_ID] = ai.campfireId;
    status[P_PIT_IGNITION_ACTIVE] = true;
    status[P_PIT_IGNITION_PROGRESS] = 0.0f;
    auto* network = GetSubsystem<Network>();
    if (network)
    {
        for (auto cIt = sessions_.Begin(); cIt != sessions_.End(); ++cIt)
            cIt->first_->SendRemoteEvent(E_PIT_IGNITION_STATUS, true, status);
    }
}

// ---------------------------------------------------------------------------
// Phase 4c: Torch — light from fire, ignite cold pit, burn timer
// ---------------------------------------------------------------------------

void AuthServer::HandleTorchLightRequest(StringHash, VariantMap& eventData)
{
    using namespace NetworkMessage;
    auto* sender = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (!sender)
        return;
    auto sit = sessions_.Find(sender);
    if (sit == sessions_.End() || !sit->second_.authenticated)
        return;

#ifdef URHO3D_DATABASE_SQLITE
    int playerId = GetPlayerId(sit->second_.username);
    if (playerId < 0 || !worldDB_)
        return;

    // Must have an unlit torch
    if (worldDB_->GetItemCount(playerId, ITEM_TORCH) < 1)
    {
        URHO3D_LOGINFOF("[Phase4c] Torch-light reject — player %d has no Torch", playerId);
        return;
    }

    // Must be near a LIT campfire
    auto avIt = serverObjects_.Find(sender);
    if (avIt == serverObjects_.End() || !avIt->second_)
        return;
    Vector3 playerPos = avIt->second_->GetWorldPosition();

    bool nearLitFire = false;
    for (auto it = serverCampfires_.Begin(); it != serverCampfires_.End(); ++it)
    {
        if (it->second_.state == PIT_LIT &&
            (playerPos - it->second_.position).Length() < 5.0f)
        {
            nearLitFire = true;
            break;
        }
    }
    if (!nearLitFire)
    {
        URHO3D_LOGINFOF("[Phase4c] Torch-light reject — no LIT fire within reach");
        return;
    }

    // Determine which torch type to light (resin torch takes priority)
    bool hasResinTorch = (worldDB_->GetItemCount(playerId, ITEM_RESIN_TORCH) > 0);
    if (hasResinTorch)
    {
        // Light resin torch — long burn, rain resistant
        worldDB_->RemoveItemFromInventory(playerId, ITEM_RESIN_TORCH, 1);
        AddItemToWorldInventory(playerId, ITEM_BURNING_TORCH, 1);
        torchTimers_[playerId] = RESIN_TORCH_BURN_TIME;
        URHO3D_LOGINFOF("[FireCarrying] Player %s lit a RESIN torch (%.0fs burn time)",
            sit->second_.username.CString(), RESIN_TORCH_BURN_TIME);
    }
    else
    {
        // Light basic torch
        worldDB_->RemoveItemFromInventory(playerId, ITEM_TORCH, 1);
        AddItemToWorldInventory(playerId, ITEM_BURNING_TORCH, 1);
        torchTimers_[playerId] = TORCH_BURN_TIME;
        URHO3D_LOGINFOF("[Phase4c] Player %s lit a torch (%.0fs burn time)",
            sit->second_.username.CString(), TORCH_BURN_TIME);
    }
    SendInventoryUpdate(sender, playerId);

    // Award FireMaking XP for torch lighting
    if (gameDB_)
        gameDB_->AwardXP(playerId, "fire_torch");
#endif
}

void AuthServer::HandleTorchIgniteRequest(StringHash, VariantMap& eventData)
{
    using namespace NetworkMessage;
    auto* sender = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (!sender)
        return;
    auto sit = sessions_.Find(sender);
    if (sit == sessions_.End() || !sit->second_.authenticated)
        return;

    unsigned pitId = eventData[P_PIT_ID].GetU32();
    auto pit = serverCampfires_.Find(pitId);
    if (pit == serverCampfires_.End())
    {
        URHO3D_LOGINFOF("[Phase4c] Torch-ignite reject — unknown pitId=%u", pitId);
        return;
    }
    ServerCampfire& cf = pit->second_;

    // Only COLD or UNLIT pits
    if (cf.state != PIT_COLD && cf.state != PIT_UNLIT)
    {
        URHO3D_LOGINFOF("[Phase4c] Torch-ignite reject — pit %u state=%d", pitId, cf.state);
        return;
    }

    // Already igniting via friction? Torch overrides — cancel friction, use torch instead.
    if (cf.ignitionActive)
    {
        URHO3D_LOGINFOF("[Phase4c] Torch overrides active friction ignition on pit %u", pitId);
        cf.ignitionActive = false;
        cf.ignitionProgress = 0.0f;
        cf.ignitionPlayerConn = nullptr;
        cf.ignitionNPCSpawnId = 0;
    }

    // Proximity check
    auto avIt = serverObjects_.Find(sender);
    if (avIt == serverObjects_.End() || !avIt->second_)
        return;
    Vector3 playerPos = avIt->second_->GetWorldPosition();
    if ((playerPos - cf.position).Length() > 5.0f)
    {
        URHO3D_LOGINFOF("[Phase4c] Torch-ignite reject — player too far from pit %u", pitId);
        return;
    }

#ifdef URHO3D_DATABASE_SQLITE
    int playerId = GetPlayerId(sit->second_.username);
    if (playerId < 0 || !worldDB_)
        return;

    // Bark Vessel can light campfire directly (not consumed — pot keeps burning)
    bool usedBarkVessel = false;
    if (worldDB_->GetItemCount(playerId, ITEM_BARK_VESSEL) > 0)
    {
        usedBarkVessel = true;
        // Fire pot is NOT consumed — it just transfers fire
    }
    else if (worldDB_->GetItemCount(playerId, ITEM_BURNING_TORCH) > 0)
    {
        // Consume the burning torch
        worldDB_->RemoveItemFromInventory(playerId, ITEM_BURNING_TORCH, 1);
        SendInventoryUpdate(sender, playerId);
        torchTimers_.Erase(playerId);
    }
    else
    {
        URHO3D_LOGINFOF("[Phase4c] Torch-ignite reject — player %d has no fire source", playerId);
        return;
    }
#endif

    // Instant ignition — pit goes LIT
    cf.fuelSeconds = Min(TORCH_INITIAL_FUEL, cf.maxFuelSeconds);
    cf.state = PIT_LIT;

    URHO3D_LOGINFOF("[Phase4c] Player %s %s-ignited pit %u — now LIT (%.0f fuel)",
        sit->second_.username.CString(), usedBarkVessel ? "bark-vessel" : "torch", pitId, cf.fuelSeconds);

    BroadcastPitState(pitId, cf);
}

void AuthServer::TickTorchTimers(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return;

    // Rain check — resin torches extinguish in heavy rain, basic torches in any rain
    bool raining = (weatherPrecipitation_ > 0.0f);
    bool heavyRain = (weatherPrecipitation_ >= RESIN_TORCH_RAIN_THRESHOLD);

    Vector<int> expired;
    for (auto it = torchTimers_.Begin(); it != torchTimers_.End(); ++it)
    {
        int playerId = it->first_;

        // Check which fire item the player is carrying to determine rain behavior
        bool hasResinTorch = (worldDB_->GetItemCount(playerId, ITEM_RESIN_TORCH) > 0);
        bool hasFireBundle = (worldDB_->GetItemCount(playerId, ITEM_FIRE_BUNDLE) > 0);
        bool hasBarkVessel = (worldDB_->GetItemCount(playerId, ITEM_BARK_VESSEL) > 0);

        // Rain immunity: bark vessel and fire bundle ignore ALL rain.
        // Resin torch dies in heavy rain. Basic torch dies in any rain.
        if (hasBarkVessel || hasFireBundle)
        {
            // Waterproof — no rain effect
        }
        else if (raining && !hasResinTorch)
        {
            it->second_ = 0.0f;  // basic torch — rain kills it
        }
        else if (heavyRain && hasResinTorch)
        {
            it->second_ = 0.0f;  // resin torch — heavy rain kills it
        }

        it->second_ -= dt;
        if (it->second_ <= 0.0f)
            expired.Push(playerId);
    }

    for (unsigned i = 0; i < expired.Size(); ++i)
    {
        int playerId = expired[i];
        torchTimers_.Erase(playerId);

        // Remove whichever fire item burned out (check in priority order)
        // Bark vessel persists as empty — only fire goes out, vessel stays
        if (worldDB_->GetItemCount(playerId, ITEM_BARK_VESSEL) > 0)
        {
            // Vessel fire expired — mark empty, keep the item
            for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
            {
                if (GetNPCPlayerId(aiIt->first_) == playerId)
                { aiIt->second_.vesselContents = ServerCreatureAI::VESSEL_EMPTY; break; }
            }
            URHO3D_LOGINFOF("[FireCarrying] Bark Vessel fire expired for playerId=%d (vessel kept)", playerId);
        }
        else
        {
            // Consumable fire items are destroyed
            int fireItems[] = { ITEM_FIRE_BUNDLE, ITEM_RESIN_TORCH, ITEM_BURNING_TORCH };
            const char* fireNames[] = { "Fire Bundle", "Resin Torch", "Torch" };
            for (int f = 0; f < 3; ++f)
            {
                int count = worldDB_->GetItemCount(playerId, fireItems[f]);
                if (count > 0)
                {
                    worldDB_->RemoveItemFromInventory(playerId, fireItems[f], 1);
                    URHO3D_LOGINFOF("[FireCarrying] %s burned out for playerId=%d", fireNames[f], playerId);
                    break;
                }
            }
        }

        // Send inventory update to the player's client
        for (auto sit = sessions_.Begin(); sit != sessions_.End(); ++sit)
        {
            if (GetPlayerId(sit->second_.username) == playerId)
            {
                SendInventoryUpdate(sit->first_, playerId);
                break;
            }
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Water fish traps — server-side passive catch
// ---------------------------------------------------------------------------

void AuthServer::TickWaterTraps(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_ || trapStates_.Empty())
        return;

    if (!combatResolver_)
    {
        combatResolver_ = new CombatResolver(context_);
        combatResolver_->SetExternalRNG(QuantumDiceRollBridge);
    }

    for (auto it = trapStates_.Begin(); it != trapStates_.End(); ++it)
    {
        ServerTrapState& trap = it->second_;
        if (!trap.armed || trap.itemId != 403)  // only Fish Traps (403)
            continue;

        // Water trap must be below water level
        if (trap.position.y_ > AI_WATER_LEVEL + 0.5f)
            continue;

        // Check cooldown (60s between catch attempts)
        float age = uptime_ - trap.placedAt;
        // Use modular check: attempt every 60s
        float interval = 60.0f;
        float prevAge = age - dt;
        if (prevAge < 0.0f) prevAge = 0.0f;
        if ((int)(age / interval) == (int)(prevAge / interval))
            continue;  // not time yet

        // d20 roll: DC 8 base, trapping skill bonus
        int trapSkillBonus = 0;
        if (trap.ownerPlayerId > 0)
            trapSkillBonus = gameDB_->GetSkillLevel(trap.ownerPlayerId, SKILL_TRAPPING) / 2;
        int roll = combatResolver_->RollD20() + trapSkillBonus;

        if (roll < 8)
            continue;  // no catch this cycle

        // Caught a fish — disarm trap, award XP, add fish to owner inventory
        trap.armed = false;
        int fishItemId = 10;  // Small Fish
        if (roll >= 18)
            fishItemId = 7;   // Meat (big catch on high roll)

        if (trap.ownerPlayerId > 0)
        {
            AddItemToWorldInventory(trap.ownerPlayerId, fishItemId, 1);
            gameDB_->AwardXP(trap.ownerPlayerId, "trap_catch");
            URHO3D_LOGINFOF("[WaterTrap] Trap %u caught item %d for player %d (roll %d)",
                it->first_, fishItemId, trap.ownerPlayerId, roll);
        }

        // Broadcast trap triggered to clients (no creature for water traps)
        BroadcastTrapTriggered(it->first_, 0, trap.position);
    }
#endif
}

// ---------------------------------------------------------------------------
// Land traps — server-side proactive catch (Combat Phase 2 closure)
// ---------------------------------------------------------------------------
// Client-driven ScanTrapsForCatches() handles player-visible traps. This tick
// covers NPC-placed traps (and any trap when no client is nearby) by scanning
// server-tracked creature positions against armed land traps.

void AuthServer::TickLandTraps(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || trapStates_.Empty() || creatureAI_.Empty())
        return;

    if (!combatResolver_)
    {
        combatResolver_ = new CombatResolver(context_);
        combatResolver_->SetExternalRNG(QuantumDiceRollBridge);
    }

    // Collect catches in a separate list to avoid mutating trapStates_ during iteration.
    struct PendingCatch
    {
        unsigned trapNodeId;
        unsigned spawnId;
        unsigned creatureNodeId;
        int      creatureId;
        Vector3  creaturePos;
        int      ownerPlayerId;
        bool     liveCapture;
    };
    Vector<PendingCatch> catches;

    for (auto trapIt = trapStates_.Begin(); trapIt != trapStates_.End(); ++trapIt)
    {
        ServerTrapState& trap = trapIt->second_;
        if (!trap.armed)
            continue;

        // Skip water fish traps — handled by TickWaterTraps
        if (trap.itemId == 403)
            continue;

        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            const ServerCreatureAI& ai = aiIt->second_;

            // Only catch animals, not humans
            if (ai.isHuman)
                continue;

            // Skip dead or already-trapped creatures (5=DIE, 12=TRAPPED, 13=CORPSE)
            if (ai.state == 5 || ai.state == 12 || ai.state == 13)
                continue;

            // Look up the trap rule for this (trap, creature) pair
            TrapRule rule;
            if (!gameDB_->GetTrapRule(trap.itemId, ai.creatureId, rule))
                continue;

            // Range check
            Vector3 diff = ai.position - trap.position;
            if (diff.LengthSquared() > rule.attractRange * rule.attractRange)
                continue;

            // d20 + trapping skill bonus vs hold strength
            int trapSkillBonus = 0;
            if (trap.ownerPlayerId > 0)
                trapSkillBonus = gameDB_->GetSkillLevel(trap.ownerPlayerId, SKILL_TRAPPING) / 2;
            int roll = combatResolver_->RollD20() + trapSkillBonus;

            URHO3D_LOGINFOF("[LandTrap] check trap=%u creature spawnId=%u (id=%d) roll=%d (bonus+%d) vs hold=%d",
                trapIt->first_, aiIt->first_, ai.creatureId, roll, trapSkillBonus, rule.holdStrength);

            if (roll < rule.holdStrength)
                continue;  // escaped

            // Resolve the scene node ID for the broadcast
            unsigned creatureNodeId = 0;
            auto nodeIt = creatureNodes_.Find(aiIt->first_);
            if (nodeIt != creatureNodes_.End() && nodeIt->second_)
                creatureNodeId = nodeIt->second_->GetID();

            if (creatureNodeId == 0)
                continue;  // no scene node — can't broadcast

            PendingCatch c;
            c.trapNodeId = trapIt->first_;
            c.spawnId = aiIt->first_;
            c.creatureNodeId = creatureNodeId;
            c.creatureId = ai.creatureId;
            c.creaturePos = ai.position;
            c.ownerPlayerId = trap.ownerPlayerId;
            c.liveCapture = rule.liveCapture;
            catches.Push(c);

            break;  // one catch per trap per tick
        }
    }

    // Apply catches
    for (unsigned i = 0; i < catches.Size(); ++i)
    {
        const PendingCatch& c = catches[i];

        // Disarm the trap
        auto trapIt = trapStates_.Find(c.trapNodeId);
        if (trapIt != trapStates_.End())
        {
            trapIt->second_.armed = false;
            if (c.liveCapture)
            {
                trapIt->second_.isLiveCapture = true;
                trapIt->second_.capturedCreatureId = c.spawnId;
            }
        }

        // Set creature AI state to TRAPPED (freezes movement)
        auto aiIt = creatureAI_.Find(c.spawnId);
        if (aiIt != creatureAI_.End())
        {
            aiIt->second_.state = 12;  // CREATURE_TRAPPED
            aiIt->second_.moveSpeed = 0.0f;
            aiIt->second_.targetPosition = aiIt->second_.position;
        }

        if (!c.liveCapture)
        {
            // Kill traps: register in creatureStates_ so harvest works
            auto stateIt = creatureStates_.Find(c.creatureNodeId);
            if (stateIt == creatureStates_.End())
            {
                ServerCreatureState fresh;
                fresh.creatureId = c.creatureId;
                if (!LoadCreatureCombat(c.creatureId, fresh))
                {
                    fresh.hp = fresh.maxHp = 1;
                    fresh.defense = 10;
                }
                fresh.position = c.creaturePos;
                if (populationManager_)
                    fresh.regionId = populationManager_->FindRegion(c.creaturePos.x_, c.creaturePos.z_);
                creatureStates_[c.creatureNodeId] = fresh;
            }
        }
        // Live captures: creature stays alive in the cage — tamed on collection

        // Award XP
        if (c.ownerPlayerId > 0)
            gameDB_->AwardXP(c.ownerPlayerId, "trap_catch");

        // Broadcast to all clients
        BroadcastTrapTriggered(c.trapNodeId, c.creatureNodeId, c.creaturePos);

        const char* captureType = c.liveCapture ? "CAGED" : "CAUGHT";
        URHO3D_LOGINFOF("[LandTrap] %s trap=%u creature spawnId=%u nodeId=%u (species %d)",
            captureType, c.trapNodeId, c.spawnId, c.creatureNodeId, c.creatureId);
    }
#endif
}

// ---------------------------------------------------------------------------
// Woodpile server sync
// ---------------------------------------------------------------------------

void AuthServer::InitServerWoodpiles()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return;

    // Scan placed buildings for woodpiles (typeId 56) and create server tracking.
    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].buildingId == 56)  // BUILDING_TYPE_WOODPILE
        {
            ServerWoodpile wp;
            // Read capacity from GameDB building type (storageSlots field).
            BuildingTypeDBInfo btype;
            if (gameDB_->GetBuildingType(56, btype))
                wp.capacity = btype.storageSlots > 0 ? btype.storageSlots : 50;
            else
                wp.capacity = 50;
            serverWoodpiles_[buildings[i].id] = wp;
        }
    }
    if (!serverWoodpiles_.Empty())
        URHO3D_LOGINFOF("[Woodpile] Tracking %u woodpiles from placed buildings", serverWoodpiles_.Size());
#endif
}

void AuthServer::BroadcastWoodpileState(int buildingId, const ServerWoodpile& wp)
{
    VariantMap data;
    data[P_PILE_BUILDING_ID] = buildingId;
    data[P_PILE_SOFTWOOD]    = wp.softwoodBu;
    data[P_PILE_HARDWOOD]    = wp.hardwoodBu;
    data[P_PILE_CAPACITY]    = wp.capacity;

    for (auto sit = sessions_.Begin(); sit != sessions_.End(); ++sit)
    {
        if (sit->second_.authenticated)
            sit->first_->SendRemoteEvent(E_WOODPILE_STATE, true, data);
    }
}

void AuthServer::HandleWoodpileDeposit(StringHash, VariantMap& eventData)
{
    using namespace RemoteEventData;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (!connection)
        return;
    auto sit = sessions_.Find(connection);
    if (sit == sessions_.End() || !sit->second_.authenticated)
        return;

    int buildingId = eventData[P_PILE_BUILDING_ID].GetI32();

    // Ensure we're tracking this woodpile
    auto wpIt = serverWoodpiles_.Find(buildingId);
    if (wpIt == serverWoodpiles_.End())
    {
        URHO3D_LOGINFOF("[Woodpile] Deposit reject — unknown building %d", buildingId);
        return;
    }
    ServerWoodpile& wp = wpIt->second_;

    // Proximity check — player must be within 3m of the woodpile
    auto avIt = serverObjects_.Find(connection);
    if (avIt == serverObjects_.End() || !avIt->second_)
        return;
    Vector3 playerPos = avIt->second_->GetWorldPosition();

#ifdef URHO3D_DATABASE_SQLITE
    int playerId = GetPlayerId(sit->second_.username);
    if (playerId < 0 || !worldDB_)
        return;

    PlacedBuildingDBInfo bldg;
    if (!worldDB_->GetPlacedBuilding(buildingId, bldg))
        return;
    Vector3 pilePos(bldg.posX, bldg.posY, bldg.posZ);
    if ((playerPos - pilePos).Length() > 3.0f)
    {
        URHO3D_LOGINFOF("[Woodpile] Deposit reject — player too far from pile %d", buildingId);
        return;
    }

    static constexpr int ITEM_SOFTWOOD = 15;
    static constexpr int ITEM_HARDWOOD = 16;

    int swAvail = worldDB_->GetItemCount(playerId, ITEM_SOFTWOOD);
    int hwAvail = worldDB_->GetItemCount(playerId, ITEM_HARDWOOD);

    int swRoom = Max(0, wp.capacity - wp.softwoodBu);
    int hwRoom = Max(0, wp.capacity - wp.hardwoodBu);
    int swMove = Min(swAvail, swRoom);
    int hwMove = Min(hwAvail, hwRoom);

    if (swMove == 0 && hwMove == 0)
    {
        URHO3D_LOGINFOF("[Woodpile] Deposit reject — no wood or pile full (player %d)", playerId);
        return;
    }

    if (swMove > 0)
    {
        worldDB_->RemoveItemFromInventory(playerId, ITEM_SOFTWOOD, swMove);
        wp.softwoodBu += swMove;
    }
    if (hwMove > 0)
    {
        worldDB_->RemoveItemFromInventory(playerId, ITEM_HARDWOOD, hwMove);
        wp.hardwoodBu += hwMove;
    }

    SendInventoryUpdate(connection, playerId);

    URHO3D_LOGINFOF("[Woodpile] Player %s deposited SW+%d HW+%d to pile %d (now SW %d/%d HW %d/%d)",
        sit->second_.username.CString(), swMove, hwMove, buildingId,
        wp.softwoodBu, wp.capacity, wp.hardwoodBu, wp.capacity);

    BroadcastWoodpileState(buildingId, wp);
#endif
}

void AuthServer::TendCampfire(ServerCreatureAI& ai)
{
    auto it = serverCampfires_.Find(ai.campfireId);
    if (it == serverCampfires_.End())
        return;
    ServerCampfire& cf = it->second_;
    if ((ai.position - cf.position).Length() > GetTuning("campfire_tend_range", CAMPFIRE_TEND_RANGE))
        return; // Not close enough

    // Phase 3: wet wood delivers fewer burn-units.
    float delivered = ApplyWetnessToTendDelivery(CAMPFIRE_STICK_BURN, cf.wetness);
    float before = cf.fuelSeconds;
    cf.fuelSeconds = Min(cf.fuelSeconds + delivered, cf.maxFuelSeconds);

    // Phase 3: tending a COLD pit re-lights it; tending EMBERS bumps back to LIT
    // if we cleared the threshold. State machine handles the rest next tick.
    FirePitState prevState = cf.state;
    if (cf.state == PIT_COLD || cf.state == PIT_UNLIT)
        cf.state = (cf.fuelSeconds > PIT_EMBERS_THRESHOLD) ? PIT_LIT : PIT_EMBERS;
    else if (cf.state == PIT_EMBERS && cf.fuelSeconds > PIT_EMBERS_THRESHOLD)
        cf.state = PIT_LIT;

    URHO3D_LOGINFOF("[CreatureAI] NPC spawnId=%u tended campfire %u (%.0f -> %.0f, delivered %.0f, state %d->%d)",
        ai.spawnId, ai.campfireId, before, cf.fuelSeconds, delivered, prevState, cf.state);

    // Broadcast immediately — tend is a discrete event, clients want to react instantly.
    BroadcastPitState(ai.campfireId, cf);
}

// ── Phase 4: NPC Inventory Chain ─────────────────────────────────────────

int AuthServer::FindFoodInNPCInventory(int npcPlayerId)
{
    // Walk NPC inventory; return first food itemId. Same SQL path as players use.
    if (!worldDB_ || !gameDB_)
        return 0;

    Vector<InventorySlot> bag = worldDB_->GetPlayerBagItems(npcPlayerId);
    for (unsigned i = 0; i < bag.Size(); ++i)
    {
        FoodInfo food;
        if (gameDB_->GetFoodProperties(bag[i].itemId, food))
            return bag[i].itemId;
    }
    return 0;
}

bool AuthServer::NPCEatFromInventory(ServerCreatureAI& ai)
{
    if (!worldDB_ || !gameDB_)
        return false;

    // Allocate stable NPC playerId on demand (keyed by spawnId)
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    int itemId = FindFoodInNPCInventory(npcPlayerId);
    if (itemId == 0)
        return false;

    FoodInfo food;
    if (!gameDB_->GetFoodProperties(itemId, food))
        return false;

    // Same consume path as HandleEat
    if (worldDB_->GetItemCount(npcPlayerId, itemId) < 1)
        return false;
    worldDB_->RemoveItemFromInventory(npcPlayerId, itemId, 1);

    // Cooking skill: experienced cooks extract more nourishment
    int cookLevel = GetNPCSkillLevel(ai.spawnId, SKILL_COOKING);
    float hungerRestore = (float)food.hunger * (1.0f + 0.05f * cookLevel);
    ai.hunger = Min(100.0f, ai.hunger + hungerRestore);

    // Master Chef: apply food quality based on campfire's lastCookSkill
    if (food.cooked && ai.campfireId != 0)
    {
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        int quality = (cfIt != serverCampfires_.End()) ? cfIt->second_.lastCookSkill : cookLevel;
        ApplyFoodQuality(ai, quality);
    }

    // INNOV_MEDICINAL_FOOD: cooked meat also restores 5 HP
    if (food.cooked && HasInnovation(ai.campfireId, INNOV_MEDICINAL_FOOD))
    {
        auto csIt = creatureStates_.Find(ai.spawnId);
        if (csIt != creatureStates_.End())
            csIt->second_.hp = Min(csIt->second_.hp + 5, csIt->second_.maxHp);
    }

    NPCAwardXP(ai, "cook");
    return true;
}

bool AuthServer::NPCGatherToInventory(ServerCreatureAI& ai)
{
    // NPC gathered Berries (item 6) — real inventory write through the
    // same path players use. Weight/slot rules apply identically.
    // Foraging skill: experienced foragers sometimes find extra
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    int forageLevel = GetNPCSkillLevel(ai.spawnId, SKILL_FORAGING);
    int qty = 1;
    if (forageLevel >= 3 && Random(1.0f) < 0.3f) qty = 2;
    if (forageLevel >= 7 && Random(1.0f) < 0.2f) qty = 3;
    bool added = AddItemToWorldInventory(npcPlayerId, 6 /* Berries */, qty);

    if (added)
    {
        URHO3D_LOGINFOF("[CreatureAI] NPC spawnId=%u gathered Berries (now in inventory)",
            ai.spawnId);
    }
    return added;
}

// ---------------------------------------------------------------------------
// Phase 5: NPC firewood inventory helpers
// ---------------------------------------------------------------------------

bool AuthServer::NPCGatherWoodToInventory(ServerCreatureAI& ai)
{
    // Alternate between softwood and hardwood — NPC needs both for ignition.
    // Check which one is missing or lower, gather that.
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;

    int soft = worldDB_->GetItemCount(npcPlayerId, ITEM_SOFTWOOD);
    int hard = worldDB_->GetItemCount(npcPlayerId, ITEM_HARDWOOD);

    // Gather whichever is lower (or softwood if equal, since tending uses softwood)
    int gatherItem = (hard < soft) ? ITEM_HARDWOOD : ITEM_SOFTWOOD;
    const char* name = (gatherItem == ITEM_SOFTWOOD) ? "Softwood" : "Hardwood";

    bool added = AddItemToWorldInventory(npcPlayerId, gatherItem, 1);
    if (added)
    {
        URHO3D_LOGINFOF("[Phase5] NPC spawnId=%u gathered %s (soft=%d, hard=%d)",
            ai.spawnId, name, soft + (gatherItem == ITEM_SOFTWOOD ? 1 : 0),
            hard + (gatherItem == ITEM_HARDWOOD ? 1 : 0));
    }
    return added;
#else
    return false;
#endif
}

bool AuthServer::NPCHasFirewood(unsigned spawnId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;
    auto pidIt = npcPlayerIds_.Find(spawnId);
    if (pidIt == npcPlayerIds_.End())
        return false;
    int npcPlayerId = pidIt->second_;
    return worldDB_->GetItemCount(npcPlayerId, ITEM_SOFTWOOD) >= 1 &&
           worldDB_->GetItemCount(npcPlayerId, ITEM_HARDWOOD) >= 1;
#else
    return true; // No DB — allow virtual ignition
#endif
}

bool AuthServer::NPCHasSoftwood(unsigned spawnId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;
    auto pidIt = npcPlayerIds_.Find(spawnId);
    if (pidIt == npcPlayerIds_.End())
        return false;
    return worldDB_->GetItemCount(pidIt->second_, ITEM_SOFTWOOD) >= 1;
#else
    return true;
#endif
}

bool AuthServer::NPCConsumeFirewood(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (worldDB_->GetItemCount(npcPlayerId, ITEM_SOFTWOOD) < 1 ||
        worldDB_->GetItemCount(npcPlayerId, ITEM_HARDWOOD) < 1)
        return false;

    worldDB_->RemoveItemFromInventory(npcPlayerId, ITEM_SOFTWOOD, 1);
    worldDB_->RemoveItemFromInventory(npcPlayerId, ITEM_HARDWOOD, 1);
    URHO3D_LOGINFOF("[Phase5] NPC spawnId=%u consumed 1 Softwood + 1 Hardwood for ignition",
        ai.spawnId);
    return true;
#else
    return false;
#endif
}

bool AuthServer::NPCConsumeSoftwood(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (worldDB_->GetItemCount(npcPlayerId, ITEM_SOFTWOOD) < 1)
        return false;

    worldDB_->RemoveItemFromInventory(npcPlayerId, ITEM_SOFTWOOD, 1);
    URHO3D_LOGINFOF("[Phase5] NPC spawnId=%u consumed 1 Softwood for tending",
        ai.spawnId);
    return true;
#else
    return false;
#endif
}

int AuthServer::FindTrapInNPCInventory(int npcPlayerId)
{
    if (!worldDB_ || !gameDB_)
        return 0;

    Vector<InventorySlot> bag = worldDB_->GetPlayerBagItems(npcPlayerId);
    for (unsigned i = 0; i < bag.Size(); ++i)
    {
        ItemInfo info;
        if (gameDB_->GetItem(bag[i].itemId, info) && info.category == "trap")
            return bag[i].itemId;
    }
    return 0;
}

bool AuthServer::NPCPlaceTrap(ServerCreatureAI& ai)
{
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    int trapItemId = FindTrapInNPCInventory(npcPlayerId);
    if (trapItemId == 0)
        return false;

    unsigned nodeId = PlaceTrapForOwner(npcPlayerId, trapItemId, ai.position, 0.0f);
    if (nodeId == 0)
        return false;

    URHO3D_LOGINFOF("[CreatureAI] NPC spawnId=%u placed trap %d at (%.1f, %.1f, %.1f) nodeId=%u",
        ai.spawnId, trapItemId, ai.position.x_, ai.position.y_, ai.position.z_, nodeId);
    return true;
}

unsigned AuthServer::FindTriggeredTrapOwnedBy(int ownerPlayerId)
{
    // Scan trapStates_ for a triggered (!armed) trap owned by this player.
    for (auto it = trapStates_.Begin(); it != trapStates_.End(); ++it)
    {
        const ServerTrapState& trap = it->second_;
        if (!trap.armed && trap.ownerPlayerId == ownerPlayerId)
            return it->first_;
    }
    return 0;
}

bool AuthServer::HarvestForOwner(int ownerPlayerId, int creatureId)
{
    if (!worldDB_ || !gameDB_)
        return false;

    Vector<LootDrop> drops = gameDB_->GetLoot(creatureId);
    if (drops.Empty())
        return false;

    bool anyAwarded = false;
    for (unsigned i = 0; i < drops.Size(); ++i)
    {
        const LootDrop& d = drops[i];
        if (d.chance >= 1.0f || Random() < d.chance)
        {
            if (AddItemToWorldInventory(ownerPlayerId, d.itemId, d.quantity))
                anyAwarded = true;
        }
    }

    if (anyAwarded)
        gameDB_->AwardXP(ownerPlayerId, "forage");
    return anyAwarded;
}

bool AuthServer::NPCCollectTrap(ServerCreatureAI& ai)
{
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    unsigned trapNodeId = FindTriggeredTrapOwnedBy(npcPlayerId);
    if (trapNodeId == 0)
        return false;

    auto trapIt = trapStates_.Find(trapNodeId);
    if (trapIt == trapStates_.End())
        return false;

    ServerTrapState& trap = trapIt->second_;

    // Live capture (cage trap): auto-tame the captured creature
    if (trap.isLiveCapture && trap.capturedCreatureId != 0)
    {
        auto capturedIt = creatureAI_.Find(trap.capturedCreatureId);
        if (capturedIt != creatureAI_.End())
        {
            ServerCreatureAI& animal = capturedIt->second_;
            animal.tamerId = ai.spawnId;
            animal.state = 0;  // CREATURE_IDLE — release from trapped state
            animal.moveSpeed = 2.0f;
            animal.homePosition = ai.homePosition;  // follow NPC's settlement
            animal.campfireId = ai.campfireId;
            animal.wanderRadius = 15.0f;

            URHO3D_LOGINFOF("[CageTrap] NPC %u tamed creature spawnId=%u (species %d) via cage trap %u",
                ai.spawnId, trap.capturedCreatureId, animal.creatureId, trapNodeId);
        }

        // Remove the trap
        BroadcastTrapRemoved(trapNodeId);
        trapStates_.Erase(trapIt);
        return true;
    }

    // Kill trap: find and harvest creature near trap position
    int harvestedCreatureId = 0;
    unsigned trappedCreatureNodeId = 0;
    float bestDistSq = 4.0f * 4.0f;
    for (auto cit = creatureStates_.Begin(); cit != creatureStates_.End(); ++cit)
    {
        Vector3 diff = cit->second_.position - trap.position;
        float distSq = diff.LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            harvestedCreatureId = cit->second_.creatureId;
            trappedCreatureNodeId = cit->first_;
        }
    }

    if (harvestedCreatureId == 0)
    {
        BroadcastTrapRemoved(trapNodeId);
        trapStates_.Erase(trapIt);
        URHO3D_LOGWARNINGF("[CreatureAI] NPC spawnId=%u found triggered trap %u but no creature to harvest",
            ai.spawnId, trapNodeId);
        return false;
    }

    bool gotLoot = HarvestForOwner(npcPlayerId, harvestedCreatureId);

    BroadcastTrapRemoved(trapNodeId);
    trapStates_.Erase(trapIt);
    creatureStates_.Erase(trappedCreatureNodeId);

    URHO3D_LOGINFOF("[CreatureAI] NPC spawnId=%u harvested trap %u (creature %d, loot: %s)",
        ai.spawnId, trapNodeId, harvestedCreatureId, gotLoot ? "yes" : "no");
    return gotLoot;
}

// ===========================================================================
// Farming system
// ===========================================================================

void AuthServer::CacheCropTypes()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_)
        return;

    Vector<CropTypeInfo> types = gameDB_->GetAllCropTypes();
    for (unsigned i = 0; i < types.Size(); ++i)
        cachedCropTypes_[types[i].seedItemId] = types[i];

    URHO3D_LOGINFOF("[Farming] Cached %u crop types", types.Size());
#endif
}

int AuthServer::GetCurrentSeasonIndex() const
{
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    int dayOfYear = utc->tm_yday + 1;
    int hour = utc->tm_hour + 11;  // Melbourne UTC+11
    if (hour >= 24) dayOfYear++;
    float seasonAngle = fmodf((dayOfYear - 81) / 365.0f + 1.0f, 1.0f);
    float seasonPos = seasonAngle * 4.0f;
    return ((int)seasonPos) % 4;  // 0=spring,1=summer,2=autumn,3=winter
}

String AuthServer::GetSeasonName(int index) const
{
    switch (index)
    {
    case 0: return "spring";
    case 1: return "summer";
    case 2: return "autumn";
    case 3: return "winter";
    default: return "any";
    }
}

bool AuthServer::IsSeasonMatch(const String& allowed, int currentSeason) const
{
    if (allowed == "any")
        return true;
    return allowed == GetSeasonName(currentSeason);
}

bool AuthServer::IsNearWater(float px, float pz, float range) const
{
    // Check terrain water level in 8 radial directions at full and half range
    for (int i = 0; i < 8; ++i)
    {
        float angle = i * 45.0f * 0.0174533f;  // DEG_TO_RAD
        float probeX = px + cosf(angle) * range;
        float probeZ = pz + sinf(angle) * range;
        if (const_cast<AuthServer*>(this)->GetTerrainHeightAI(probeX, probeZ) <= AI_WATER_LEVEL)
            return true;
    }
    for (int i = 0; i < 8; ++i)
    {
        float angle = i * 45.0f * 0.0174533f;
        float probeX = px + cosf(angle) * (range * 0.5f);
        float probeZ = pz + sinf(angle) * (range * 0.5f);
        if (const_cast<AuthServer*>(this)->GetTerrainHeightAI(probeX, probeZ) <= AI_WATER_LEVEL)
            return true;
    }

    // Phase 17: also check irrigation channels — point-to-line-segment distance
    if (worldDB_)
    {
        Vector<WorldDB::IrrigationChannel> channels =
            const_cast<WorldDB*>(worldDB_.Get())->GetAllIrrigationChannels();
        for (unsigned c = 0; c < channels.Size(); ++c)
        {
            // Distance from point (px,pz) to line segment (water→farm)
            float ax = channels[c].waterX, az = channels[c].waterZ;
            float bx = channels[c].farmX, bz = channels[c].farmZ;
            float dx = bx - ax, dz = bz - az;
            float lenSq = dx * dx + dz * dz;
            if (lenSq < 1.0f) continue;  // degenerate channel
            float t = Clamp(((px - ax) * dx + (pz - az) * dz) / lenSq, 0.0f, 1.0f);
            float closestX = ax + t * dx;
            float closestZ = az + t * dz;
            float distSq = (px - closestX) * (px - closestX) + (pz - closestZ) * (pz - closestZ);
            if (distSq <= range * range)
                return true;
        }
    }

    // Water Phase 4: Wells count as water sources
    if (worldDB_)
    {
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned b = 0; b < buildings.Size(); ++b)
        {
            if (buildings[b].buildingId == BUILDING_WELL)
            {
                float dx = px - buildings[b].posX;
                float dz = pz - buildings[b].posZ;
                if (dx * dx + dz * dz <= range * range)
                    return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Water Phase 4: Wells
// ---------------------------------------------------------------------------

void AuthServer::InitializeWell(int placedBuildingId, float posX, float posZ)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    // Terrain-dependent yield: near rivers = high (15), hilltops = low (3)
    float height = GetTerrainHeightAI(posX, posZ);
    float normalizedHeight = (height - AI_WATER_LEVEL) / (60.0f - AI_WATER_LEVEL);
    normalizedHeight = Clamp(normalizedHeight, 0.0f, 1.0f);

    // Proximity to water boosts yield
    bool nearWater = false;
    for (int i = 0; i < 8; ++i)
    {
        float angle = i * 45.0f * 0.0174533f;
        float probeX = posX + cosf(angle) * 15.0f;
        float probeZ = posZ + sinf(angle) * 15.0f;
        if (GetTerrainHeightAI(probeX, probeZ) <= AI_WATER_LEVEL)
        { nearWater = true; break; }
    }

    // Near water: 12-15 draws/day. Low elevation: 8-12. High elevation: 3-6.
    int maxYield;
    if (nearWater)
        maxYield = 12 + Random(0, 4);
    else if (normalizedHeight < 0.35f)
        maxYield = 8 + Random(0, 5);
    else if (normalizedHeight < 0.65f)
        maxYield = 5 + Random(0, 4);
    else
        maxYield = 3 + Random(0, 4);

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO well_state (building_id, water_reserve, max_yield, last_refill) VALUES (?, ?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        double now = GetSubsystem<Time>()->GetElapsedTime();
        sqlite3_bind_int(stmt, 1, placedBuildingId);
        sqlite3_bind_int(stmt, 2, maxYield);  // starts full
        sqlite3_bind_int(stmt, 3, maxYield);
        sqlite3_bind_double(stmt, 4, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    URHO3D_LOGINFOF("[Well] Initialized well %d at (%.0f,%.0f) — max yield %d/day (nearWater=%d, height=%.2f)",
                    placedBuildingId, posX, posZ, maxYield, nearWater ? 1 : 0, normalizedHeight);
#endif
}

bool AuthServer::DrawFromWell(int placedBuildingId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return false;

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int reserve = 0;
    if (sqlite3_prepare_v2(db, "SELECT water_reserve FROM well_state WHERE building_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, placedBuildingId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            reserve = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (reserve <= 0)
        return false;

    stmt = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE well_state SET water_reserve = water_reserve - 1 WHERE building_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, placedBuildingId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return true;
#else
    return false;
#endif
}

bool AuthServer::TryDrawFromNearbyWell(float posX, float posZ, float range)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return false;
    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    float rangeSq = range * range;
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].buildingId != BUILDING_WELL) continue;
        float dx = buildings[i].posX - posX;
        float dz = buildings[i].posZ - posZ;
        if (dx * dx + dz * dz < rangeSq)
        {
            if (DrawFromWell(buildings[i].id))
                return true;
        }
    }
#endif
    return false;
}

void AuthServer::TickWellRefill()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return;

    sqlite3* db = worldDB_->GetHandle();
    double now = GetSubsystem<Time>()->GetElapsedTime();
    bool raining = weatherPrecipitation_ > 0.15f;

    // Per-well refill with building-type-aware drought resistance
    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned b = 0; b < buildings.Size(); ++b)
    {
        int bid = buildings[b].buildingId;
        if (bid != BUILDING_WELL && bid != 59) continue;

        sqlite3_stmt* chk = nullptr;
        if (sqlite3_prepare_v2(db,
            "SELECT water_reserve, max_yield, last_refill FROM well_state WHERE building_id = ?",
            -1, &chk, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_int(chk, 1, buildings[b].id);
        if (sqlite3_step(chk) != SQLITE_ROW) { sqlite3_finalize(chk); continue; }

        int reserve = sqlite3_column_int(chk, 0);
        int maxYield = sqlite3_column_int(chk, 1);
        double lastRefill = sqlite3_column_double(chk, 2);
        sqlite3_finalize(chk);

        if (now - lastRefill < 300.0) continue;

        // Deep Well (59): 25% drought penalty. Basic Well (57): 50%.
        float penalty = (bid == 59) ? 0.25f : 0.5f;
        int refill = (int)(maxYield * (1.0f - droughtSeverity_ * penalty));

        // Rain surplus cache: wells fill to 150% during rain
        int cap = raining ? (int)(maxYield * 1.5f) : maxYield;
        int newReserve = Min(Max(refill, reserve), cap);

        sqlite3_stmt* upd = nullptr;
        if (sqlite3_prepare_v2(db,
            "UPDATE well_state SET water_reserve = ?, last_refill = ? WHERE building_id = ?",
            -1, &upd, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(upd, 1, newReserve);
            sqlite3_bind_double(upd, 2, now);
            sqlite3_bind_int(upd, 3, buildings[b].id);
            sqlite3_step(upd);
            sqlite3_finalize(upd);
        }
    }
#endif
}

void AuthServer::InitializeBarrel(int placedBuildingId, float posX, float posZ)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen()) return;
    sqlite3* db = worldDB_->GetHandle();

    // Check if under a roof: any shelter building within 5m
    bool underRoof = false;
    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].buildingId >= 1 && buildings[i].buildingId <= 3)  // shelters
        {
            float dx = buildings[i].posX - posX;
            float dz = buildings[i].posZ - posZ;
            if (dx * dx + dz * dz < 25.0f)  // 5m radius
            { underRoof = true; break; }
        }
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO barrel_state (building_id, water_reserve, capacity, under_roof) VALUES (?, 0, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, placedBuildingId);
        sqlite3_bind_double(stmt, 2, BARREL_CAPACITY);
        sqlite3_bind_int(stmt, 3, underRoof ? 1 : 0);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    URHO3D_LOGINFOF("[Barrel] Initialized barrel %d at (%.0f,%.0f) — %s",
                    placedBuildingId, posX, posZ, underRoof ? "UNDER ROOF (no collection)" : "open sky");
#endif
}

void AuthServer::TickBarrelRainCollection(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen() || weatherPrecipitation_ < 0.01f)
        return;

    sqlite3* db = worldDB_->GetHandle();

    // Fill rate: precipitation (0-1) × capacity × dt / 3600 — full barrel in ~1hr of heavy rain
    float fillRate = weatherPrecipitation_ * BARREL_CAPACITY * dt / 3600.0f;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "UPDATE barrel_state SET water_reserve = MIN(capacity, water_reserve + ?) WHERE under_roof = 0",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_double(stmt, 1, (double)fillRate);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
#endif
}

bool AuthServer::DrawFromBarrel(float posX, float posZ, float range)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen()) return false;

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    sqlite3* db = worldDB_->GetHandle();

    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].buildingId != BUILDING_WATER_BARREL) continue;
        float dx = buildings[i].posX - posX;
        float dz = buildings[i].posZ - posZ;
        if (dx * dx + dz * dz > range * range) continue;

        // Check reserve
        sqlite3_stmt* stmt = nullptr;
        float reserve = 0.0f;
        if (sqlite3_prepare_v2(db, "SELECT water_reserve FROM barrel_state WHERE building_id = ?",
                               -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, buildings[i].id);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                reserve = (float)sqlite3_column_double(stmt, 0);
            sqlite3_finalize(stmt);
        }

        if (reserve >= 1.0f)
        {
            stmt = nullptr;
            if (sqlite3_prepare_v2(db,
                "UPDATE barrel_state SET water_reserve = water_reserve - 1 WHERE building_id = ?",
                -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, buildings[i].id);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            return true;
        }
    }
#endif
    return false;
}

void AuthServer::TickDrought()
{
    // Track consecutive dry days — precipitation < 0.05 counts as dry
    if (weatherPrecipitation_ < 0.05f)
        consecutiveDryDays_++;
    else
        consecutiveDryDays_ = 0;

    // Ramp severity from 0 at onset to 1.0 at severe
    if (consecutiveDryDays_ <= DROUGHT_ONSET_DAYS)
        droughtSeverity_ = 0.0f;
    else
    {
        float daysInto = (float)(consecutiveDryDays_ - DROUGHT_ONSET_DAYS);
        float range = (float)(DROUGHT_SEVERE_DAYS - DROUGHT_ONSET_DAYS);
        droughtSeverity_ = Clamp(daysInto / range, 0.0f, 1.0f);
    }

    // Shallow water shrinks — effective water level rises (less water visible)
    effectiveWaterLevel_ = AI_WATER_LEVEL - DROUGHT_WATER_DROP * droughtSeverity_;

    // Well refill reduction during drought (wells resist longest but still affected)
    // Handled via TickWellRefill checking droughtSeverity_ — wells refill at (1 - severity*0.5)

    // Campfire water cache evaporation during drought
    if (droughtSeverity_ > 0.0f)
    {
        float evapRate = 0.5f * droughtSeverity_;  // up to 0.5L per day at full severity
        for (auto it = serverCampfires_.Begin(); it != serverCampfires_.End(); ++it)
        {
            if (it->second_.waterReserve > 0.0f)
            {
                it->second_.waterReserve = Max(0.0f, it->second_.waterReserve - evapRate);
            }
        }
    }

    // Barrel evaporation during drought
    if (droughtSeverity_ > 0.0f && worldDB_ && worldDB_->IsOpen())
    {
        sqlite3* db = worldDB_->GetHandle();
        float barrelEvap = 1.0f * droughtSeverity_;  // up to 1L/day at full severity
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db,
            "UPDATE barrel_state SET water_reserve = MAX(0, water_reserve - ?)",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_double(stmt, 1, (double)barrelEvap);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Settlement dehydration cascade: thirst drains faster during drought
    // Applied in the per-tick vital decay (checked via droughtSeverity_)

    if (consecutiveDryDays_ > DROUGHT_ONSET_DAYS)
    {
        URHO3D_LOGINFOF("[Drought] Day %d dry (severity %.0f%%, water level %.2f)",
                        consecutiveDryDays_, droughtSeverity_ * 100.0f, effectiveWaterLevel_);
    }
}

void AuthServer::BroadcastCropSpawned(int cropId, int seedItemId, const Vector3& pos, unsigned char stage)
{
    VectorBuffer buf;
    buf.WriteI32(cropId);
    buf.WriteI32(seedItemId);
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(pos.y_);
    buf.WriteFloat(pos.z_);
    buf.WriteU8(stage);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_CROP_SPAWNED, true, true, buf);
    }
}

void AuthServer::BroadcastCropRemoved(int cropId)
{
    VectorBuffer buf;
    buf.WriteI32(cropId);

    for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
    {
        if (it->second_.authenticated)
            it->first_->SendMessage(MSG_CROP_REMOVED, true, true, buf);
    }
}

void AuthServer::SendExistingCrops(Connection* connection)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return;

    Vector<WorldDB::PlacedCropInfo> crops = worldDB_->GetAllPlacedCrops();
    for (unsigned i = 0; i < crops.Size(); ++i)
    {
        const WorldDB::PlacedCropInfo& c = crops[i];
        VectorBuffer buf;
        buf.WriteI32(c.cropId);
        buf.WriteI32(c.seedItemId);
        buf.WriteFloat(c.posX);
        buf.WriteFloat(c.posY);
        buf.WriteFloat(c.posZ);
        buf.WriteU8((unsigned char)c.growthStage);
        connection->SendMessage(MSG_CROP_SPAWNED, true, true, buf);
    }

    URHO3D_LOGINFOF("[Farming] Sent %u existing crops to client", crops.Size());
#endif
}

void AuthServer::HandlePlantCrop(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    auto sessIt = sessions_.Find(connection);
    if (sessIt == sessions_.End() || !sessIt->second_.authenticated)
        return;

    int seedItemId = msg.ReadI32();
    float px = msg.ReadFloat();
    float py = msg.ReadFloat();
    float pz = msg.ReadFloat();

    // Crop type lookup
    auto typeIt = cachedCropTypes_.Find(seedItemId);
    if (typeIt == cachedCropTypes_.End())
    {
        URHO3D_LOGWARNINGF("[Farming] Unknown seed item %d", seedItemId);
        return;
    }
    const CropTypeInfo& crop = typeIt->second_;

    int playerId = GetPlayerId(sessIt->second_.username);
    if (playerId <= 0) return;

    // Inventory check — seed
    if (worldDB_->GetItemCount(playerId, seedItemId) < 1)
    {
        URHO3D_LOGINFOF("[Farming] Player %d has no seed %d", playerId, seedItemId);
        return;
    }

    // Inventory check — tool
    if (worldDB_->GetItemCount(playerId, crop.toolReq) < 1 &&
        worldDB_->GetEquippedItem(playerId, "hand") != crop.toolReq)
    {
        URHO3D_LOGINFOF("[Farming] Player %d missing tool %d", playerId, crop.toolReq);
        return;
    }

    // Above water
    float height = GetTerrainHeightAI(px, pz);
    if (height <= AI_WATER_LEVEL)
    {
        URHO3D_LOGINFOF("[Farming] Cannot plant underwater at (%f, %f)", px, pz);
        return;
    }

    // Terrain flatness
    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
    if (terrain)
    {
        Vector3 normal = terrain->GetNormal(Vector3(px, 0.0f, pz));
        if (normal.y_ < crop.minFlat)
        {
            URHO3D_LOGINFOF("[Farming] Terrain too steep (%f < %f)", normal.y_, crop.minFlat);
            return;
        }
    }

    // Near water check
    if (!IsNearWater(px, pz, crop.nearWaterRange))
    {
        URHO3D_LOGINFOF("[Farming] No water within %f of (%f, %f)", crop.nearWaterRange, px, pz);
        return;
    }

    // Season check
    int season = GetCurrentSeasonIndex();
    if (!IsSeasonMatch(crop.plantSeason, season))
    {
        URHO3D_LOGINFOF("[Farming] Wrong season for planting (need %s, have %s)",
            crop.plantSeason.CString(), GetSeasonName(season).CString());
        return;
    }

    // Proximity check — no crop within 2m
    Vector<WorldDB::PlacedCropInfo> existing = worldDB_->GetAllPlacedCrops();
    for (unsigned i = 0; i < existing.Size(); ++i)
    {
        float dx = px - existing[i].posX;
        float dz = pz - existing[i].posZ;
        if (dx * dx + dz * dz < 4.0f)  // 2m squared
        {
            URHO3D_LOGINFOF("[Farming] Too close to existing crop %d", existing[i].cropId);
            return;
        }
    }

    // All checks passed — consume seed
    worldDB_->RemoveItemFromInventory(playerId, seedItemId, 1);
    SendInventoryDelta(connection, seedItemId, -1, false);

    // Compute game day for planted_day
    float gameDay = 0.0f;
    GameTimeState timeState;
    if (worldDB_->LoadGameTime(timeState))
        gameDay = timeState.gameDay;

    // Insert crop
    int cropId = worldDB_->InsertPlacedCrop(playerId, seedItemId, px, height, pz, gameDay, 0);
    if (cropId < 0)
    {
        URHO3D_LOGERROR("[Farming] Failed to insert crop into database");
        return;
    }

    // Award XP
    if (gameDB_)
        gameDB_->AwardXP(playerId, "farm_plant");

    // Broadcast to all clients
    BroadcastCropSpawned(cropId, seedItemId, Vector3(px, height, pz), 0);

    URHO3D_LOGINFOF("[Farming] Player %d planted seed %d at (%f,%f,%f) cropId=%d",
        playerId, seedItemId, px, height, pz, cropId);
#endif
}

void AuthServer::HandleHarvestCrop(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    auto sessIt = sessions_.Find(connection);
    if (sessIt == sessions_.End() || !sessIt->second_.authenticated)
        return;

    int cropId = msg.ReadI32();
    int playerId = GetPlayerId(sessIt->second_.username);
    if (playerId <= 0) return;

    // Lookup crop
    WorldDB::PlacedCropInfo cropInfo;
    if (!worldDB_->GetPlacedCrop(cropId, cropInfo))
    {
        URHO3D_LOGWARNINGF("[Farming] Crop %d not found", cropId);
        return;
    }

    // Ownership check
    if (cropInfo.ownerId != playerId)
    {
        URHO3D_LOGINFOF("[Farming] Player %d doesn't own crop %d", playerId, cropId);
        return;
    }

    // Maturity check
    if (cropInfo.growthStage < 3)
    {
        URHO3D_LOGINFOF("[Farming] Crop %d not mature (stage %d)", cropId, cropInfo.growthStage);
        return;
    }

    // Crop type lookup
    auto typeIt = cachedCropTypes_.Find(cropInfo.seedItemId);
    if (typeIt == cachedCropTypes_.End())
        return;
    const CropTypeInfo& crop = typeIt->second_;

    // Season check for harvest
    int season = GetCurrentSeasonIndex();
    if (!IsSeasonMatch(crop.harvestSeason, season))
    {
        URHO3D_LOGINFOF("[Farming] Wrong season for harvest (need %s, have %s)",
            crop.harvestSeason.CString(), GetSeasonName(season).CString());
        return;
    }

    // Tool check
    if (worldDB_->GetItemCount(playerId, crop.toolReq) < 1 &&
        worldDB_->GetEquippedItem(playerId, "hand") != crop.toolReq)
    {
        URHO3D_LOGINFOF("[Farming] Player %d missing tool %d for harvest", playerId, crop.toolReq);
        return;
    }

    // Crop rotation: check consecutive same-crop penalty
    int tileX = (int)(cropInfo.posX * 0.5f);  // snap to 2m grid
    int tileZ = (int)(cropInfo.posZ * 0.5f);
    WorldDB::CropHistoryEntry history;
    int yieldQty = crop.harvestQty;
    if (worldDB_->GetCropHistory(tileX, tileZ, history) &&
        history.lastSeedId == cropInfo.seedItemId)
    {
        // Same crop grown consecutively — 50% yield penalty (10% with Master Farmer)
        yieldQty = Max(1, yieldQty * 9 / 10);  // default to 10% for player path
    }
    // Master Farmer: +30% yield (player always benefits if any master farmer exists)
    yieldQty = Max(1, (int)(yieldQty * 1.3f));

    // Award harvest items (rotation-adjusted yield)
    AddItemToWorldInventory(playerId, crop.harvestItemId, yieldQty);
    SendInventoryDelta(connection, crop.harvestItemId, yieldQty, true);

    // Return seeds
    if (crop.seedReturn > 0)
    {
        AddItemToWorldInventory(playerId, cropInfo.seedItemId, crop.seedReturn);
        SendInventoryDelta(connection, cropInfo.seedItemId, crop.seedReturn, true);
    }

    // Record crop history for rotation tracking
    worldDB_->RecordCropHarvest(tileX, tileZ, cropInfo.seedItemId, currentGameDay_);

    // Degrade soil fertility at this location
    if (ecosystem_)
        ecosystem_->DegradeFertility(cropInfo.posX, cropInfo.posZ, 0.1f);

    // Award XP
    if (gameDB_)
        gameDB_->AwardXP(playerId, "farm_harvest");

    // Remove crop
    worldDB_->RemovePlacedCrop(cropId);
    BroadcastCropRemoved(cropId);

    URHO3D_LOGINFOF("[Farming] Player %d harvested crop %d: %d x item %d (rotation yield: %d/%d) + %d seeds",
        playerId, cropId, yieldQty, crop.harvestItemId, yieldQty, crop.harvestQty, crop.seedReturn);
#endif
}

void AuthServer::CropGrowthTick(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    cropTickTimer_ += dt;
    if (cropTickTimer_ < 60.0f)  // Check every 60 seconds
        return;
    cropTickTimer_ = 0.0f;

    if (!worldDB_)
        return;

    GameTimeState timeState;
    if (!worldDB_->LoadGameTime(timeState))
        return;
    float currentDay = timeState.gameDay;

    Vector<WorldDB::PlacedCropInfo> crops = worldDB_->GetAllPlacedCrops();
    for (unsigned i = 0; i < crops.Size(); ++i)
    {
        WorldDB::PlacedCropInfo& c = crops[i];

        auto typeIt = cachedCropTypes_.Find(c.seedItemId);
        if (typeIt == cachedCropTypes_.End())
            continue;

        float elapsed = currentDay - c.plantedDay;
        // Seasonal growth scaling: spring 1.5x, summer 1.0x, autumn 0.5x, winter 0x
        int season = GetCurrentSeasonIndex();
        float seasonMult = (season == 0) ? 1.5f :   // spring
                           (season == 1) ? 1.0f :   // summer
                           (season == 2) ? 0.5f :   // autumn
                                           0.0f;    // winter — no growth
        float effectiveElapsed = elapsed * seasonMult;
        // Master Farmer: +20% growth rate for settlement's crops
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            if (GetNPCPlayerId(aiIt->first_) == c.ownerId && HasMasterFarmer(aiIt->second_.campfireId))
            { effectiveElapsed *= 1.2f; break; }
        }
        // Willow Extract: +30% crop growth if settlement has any in stock
        for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
        {
            if (GetNPCPlayerId(aiIt->first_) == c.ownerId &&
                aiIt->second_.isHuman && worldDB_->GetItemCount(GetNPCPlayerId(aiIt->first_), 875) > 0)
            { effectiveElapsed *= 1.3f; break; }
        }
        // Drought: crops stop without irrigation (near water/channel/well)
        if (droughtSeverity_ > 0.3f)
        {
            bool irrigated = IsNearWater(c.posX, c.posZ, 15.0f);
            if (!irrigated)
                effectiveElapsed *= Max(0.0f, 1.0f - droughtSeverity_);  // scales down to 0 at full drought
        }
        int expectedStage = (int)Clamp(effectiveElapsed / (float)typeIt->second_.growDays * 4.0f, 0.0f, 3.0f);

        if (expectedStage > c.growthStage)
        {
            worldDB_->UpdateCropGrowthStage(c.cropId, expectedStage);
            BroadcastCropSpawned(c.cropId, c.seedItemId,
                Vector3(c.posX, c.posY, c.posZ), (unsigned char)expectedStage);
            URHO3D_LOGINFOF("[Farming] Crop %d advanced to stage %d", c.cropId, expectedStage);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 1: Crafting + Equip helpers
// ---------------------------------------------------------------------------

int AuthServer::NPCFindCraftableRecipe(const ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_)
        return -1;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return -1;

    // No tier gate — NPCs can attempt any recipe. The d20 skill check in
    // CraftForOwner handles success/failure. High-tier items are harder to make
    // but trying teaches you the skill.
    Vector<RecipeInfo> recipes = gameDB_->GetRecipesForTier(99);
    if (recipes.Empty())
        return -1;

    // Build NPC inventory map
    Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPlayerId);
    HashMap<int, int> invMap;
    for (unsigned i = 0; i < inv.Size(); ++i)
        invMap[inv[i].itemId] += inv[i].quantity;

    // Priority: weapon/tool if NPC has none equipped, then highest-tier recipe
    int bestWeaponRecipe = -1;
    int bestOtherRecipe = -1;
    int bestWeaponTier = -1;
    int bestOtherTier = -1;

    // Check what NPC has equipped
    bool hasWeapon = false;
    for (unsigned i = 0; i < inv.Size(); ++i)
    {
        if (inv[i].slotType == "hand" && inv[i].itemId > 0)
            hasWeapon = true;
    }

    for (unsigned i = 0; i < recipes.Size(); ++i)
    {
        const RecipeInfo& r = recipes[i];

        // Station check: skip recipes that need a station NPC isn't near
        if (r.stationReq > 0)
        {
            bool nearStation = false;
            Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
            for (unsigned b = 0; b < buildings.Size(); ++b)
            {
                if (buildings[b].buildingId == r.stationReq)
                {
                    Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
                    if ((bPos - ai.position).Length() <= CRAFTING_STATION_RANGE)
                    {
                        nearStation = true;
                        break;
                    }
                }
            }
            if (!nearStation)
                continue;
        }

        if (!gameDB_->CanCraft(r.id, invMap))
            continue;

        // Classify output
        ItemInfo outputItem;
        bool isWeaponOrTool = false;
        if (gameDB_->GetItem(r.outputId, outputItem))
        {
            if (outputItem.category == "weapon" || outputItem.category == "tool")
                isWeaponOrTool = true;
        }

        if (isWeaponOrTool && !hasWeapon && r.tier > bestWeaponTier)
        {
            bestWeaponRecipe = r.id;
            bestWeaponTier = r.tier;
        }
        else if (r.tier > bestOtherTier)
        {
            bestOtherRecipe = r.id;
            bestOtherTier = r.tier;
        }
    }

    // Prefer weapon if unarmed
    if (bestWeaponRecipe >= 0)
        return bestWeaponRecipe;
    return bestOtherRecipe;
#else
    return -1;
#endif
}

bool AuthServer::NPCCraft(ServerCreatureAI& ai, int recipeId)
{
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    String ownerName = "NPC_" + String(ai.spawnId);
    return CraftForOwner(npcPlayerId, recipeId, ai.position, ownerName);
}

bool AuthServer::NPCEquipBest(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPlayerId);
    bool anyEquipped = false;

    // Check all equipment slots: hand (weapon/tool), body, offhand, feet, head
    const char* slotNames[] = { "hand", "body", "offhand", "feet", "head" };

    for (int s = 0; s < 5; ++s)
    {
        const String slotName(slotNames[s]);

        // Find current equipped item tier for this slot
        int equippedTier = -1;
        int equippedItemId = 0;
        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            if (inv[i].slotType == slotName && inv[i].itemId > 0)
            {
                ItemInfo item;
                if (gameDB_->GetItem(inv[i].itemId, item))
                {
                    equippedTier = item.tier;
                    equippedItemId = inv[i].itemId;
                }
            }
        }

        // Scan bag for better item that fits this slot
        int bestItemId = 0;
        int bestTier = equippedTier;
        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            if (inv[i].slotType != "bag" || inv[i].itemId <= 0)
                continue;

            ItemInfo item;
            if (!gameDB_->GetItem(inv[i].itemId, item))
                continue;

            if (s == 0)
            {
                // Hand slot: weapon or tool
                if (item.category != "weapon" && item.category != "tool")
                    continue;
            }
            else
            {
                // Body/offhand/feet/head: armor or clothing, verify combat_stats slot
                if (item.category != "armor" && item.category != "clothing")
                    continue;
                CombatInfo cstat;
                if (!gameDB_->GetCombatStats(inv[i].itemId, cstat) || cstat.slot != slotName)
                    continue;
            }

            if (item.tier > bestTier)
            {
                bestItemId = inv[i].itemId;
                bestTier = item.tier;
            }
        }

        if (bestItemId <= 0)
            continue;

        // Unequip current if any
        if (equippedItemId > 0)
        {
            ItemInfo oldItem;
            if (gameDB_->GetItem(equippedItemId, oldItem))
            {
                int outId = 0;
                worldDB_->UnequipItem(npcPlayerId, slotName, outId,
                    oldItem.stackMax, oldItem.weight, oldItem.durability,
                    999.0f, 20);
            }
        }

        worldDB_->EquipItem(npcPlayerId, bestItemId, slotName);

        // Replicate equipped item to clients via node Var so visuals attach on connect.
        // Client reads "Equip_hand", "Equip_body", etc. and attaches the model.
        auto nodeIt = creatureNodes_.Find(ai.spawnId);
        if (nodeIt != creatureNodes_.End() && nodeIt->second_)
        {
            ItemInfo bestItem;
            if (gameDB_->GetItem(bestItemId, bestItem) && !bestItem.model.Empty())
                nodeIt->second_->SetVar("Equip_" + slotName, bestItem.model);
            else
                nodeIt->second_->SetVar("Equip_" + slotName, String::EMPTY);
        }

        URHO3D_LOGINFOF("[NPCEquip] NPC spawnId=%u equipped item %d in %s (tier %d, was %d)",
            ai.spawnId, bestItemId, slotName.CString(), bestTier, equippedTier);
        anyEquipped = true;
    }

    return anyEquipped;
#else
    return false;
#endif
}

bool AuthServer::NPCDressBest(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPlayerId);
    bool anyEquipped = false;

    // Check clothing/armor slots only (not hand/offhand — those are STASK_EQUIP)
    const char* slotNames[] = { "body", "head", "feet", "back" };

    for (int s = 0; s < 4; ++s)
    {
        const String slotName(slotNames[s]);

        int equippedTier = -1;
        int equippedItemId = 0;
        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            if (inv[i].slotType == slotName && inv[i].itemId > 0)
            {
                ItemInfo item;
                if (gameDB_->GetItem(inv[i].itemId, item))
                {
                    equippedTier = item.tier;
                    equippedItemId = inv[i].itemId;
                }
            }
        }

        int bestItemId = 0;
        int bestTier = equippedTier;
        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            if (inv[i].slotType != "bag" || inv[i].itemId <= 0)
                continue;

            ItemInfo item;
            if (!gameDB_->GetItem(inv[i].itemId, item))
                continue;
            if (item.category != "armor" && item.category != "clothing")
                continue;

            // Use combat_stats slot field to determine target slot
            CombatInfo cstat;
            if (gameDB_->GetCombatStats(inv[i].itemId, cstat) && cstat.slot == slotName)
            {
                if (item.tier > bestTier)
                {
                    bestItemId = inv[i].itemId;
                    bestTier = item.tier;
                }
            }
        }

        if (bestItemId <= 0)
            continue;

        if (equippedItemId > 0)
        {
            ItemInfo oldItem;
            if (gameDB_->GetItem(equippedItemId, oldItem))
            {
                int outId = 0;
                worldDB_->UnequipItem(npcPlayerId, slotName, outId,
                    oldItem.stackMax, oldItem.weight, oldItem.durability,
                    999.0f, 20);
            }
        }

        worldDB_->EquipItem(npcPlayerId, bestItemId, slotName);

        // Replicate equipped item visuals to clients
        auto nodeIt = creatureNodes_.Find(ai.spawnId);
        if (nodeIt != creatureNodes_.End() && nodeIt->second_)
        {
            ItemInfo bestItem;
            if (gameDB_->GetItem(bestItemId, bestItem) && !bestItem.model.Empty())
                nodeIt->second_->SetVar("Equip_" + slotName, bestItem.model);
            else
                nodeIt->second_->SetVar("Equip_" + slotName, String::EMPTY);
        }

        URHO3D_LOGINFOF("[NPCDress] NPC spawnId=%u equipped item %d in %s (tier %d, was %d)",
            ai.spawnId, bestItemId, slotName.CString(), bestTier, equippedTier);
        anyEquipped = true;
    }

    // Auto-equip best container in back slot (Leather Bag, Cargo Net, etc.)
    {
        int equippedBack = worldDB_->GetEquippedItem(npcPlayerId, "back");
        int bestContainerId = 0;
        int bestExtraSlots = 0;

        // Check what's already equipped
        if (equippedBack > 0)
        {
            sqlite3* db = gameDB_->GetHandle();
            if (db)
            {
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db,
                    "SELECT extra_slots FROM container_bonus WHERE item_id = ?",
                    -1, &stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(stmt, 1, equippedBack);
                    if (sqlite3_step(stmt) == SQLITE_ROW)
                        bestExtraSlots = sqlite3_column_int(stmt, 0);
                    sqlite3_finalize(stmt);
                }
            }
        }

        // Look for a better container in the bag
        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            if (inv[i].slotType != "bag" || inv[i].itemId <= 0)
                continue;

            ItemInfo item;
            if (!gameDB_->GetItem(inv[i].itemId, item) || item.category != "container")
                continue;

            sqlite3* db = gameDB_->GetHandle();
            if (!db) continue;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db,
                "SELECT extra_slots FROM container_bonus WHERE item_id = ? AND equip_slot = 'back'",
                -1, &stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(stmt, 1, inv[i].itemId);
                if (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    int extra = sqlite3_column_int(stmt, 0);
                    if (extra > bestExtraSlots)
                    {
                        bestContainerId = inv[i].itemId;
                        bestExtraSlots = extra;
                    }
                }
                sqlite3_finalize(stmt);
            }
        }

        if (bestContainerId > 0)
        {
            if (equippedBack > 0)
            {
                ItemInfo oldItem;
                if (gameDB_->GetItem(equippedBack, oldItem))
                {
                    int outId = 0;
                    worldDB_->UnequipItem(npcPlayerId, "back", outId,
                        oldItem.stackMax, oldItem.weight, oldItem.durability,
                        999.0f, 20);
                }
            }

            worldDB_->EquipItem(npcPlayerId, bestContainerId, "back");

            auto nodeIt = creatureNodes_.Find(ai.spawnId);
            if (nodeIt != creatureNodes_.End() && nodeIt->second_)
            {
                ItemInfo bestItem;
                if (gameDB_->GetItem(bestContainerId, bestItem) && !bestItem.model.Empty())
                    nodeIt->second_->SetVar("Equip_back", bestItem.model);
            }

            URHO3D_LOGINFOF("[NPCDress] NPC spawnId=%u equipped container %d in back (%d extra slots)",
                ai.spawnId, bestContainerId, bestExtraSlots);
            anyEquipped = true;
        }
    }

    return anyEquipped;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 2: Chop, Cook, Torch helpers
// ---------------------------------------------------------------------------

unsigned AuthServer::NPCFindNearestTree(const Vector3& position, float range)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return 0;

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    unsigned bestId = 0;
    float bestDistSq = range * range;

    if (sqlite3_prepare_v2(db,
        "SELECT tree_id, pos_x, pos_z FROM trees WHERE hp > 0 AND growth_stage >= 2",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            unsigned tid = (unsigned)sqlite3_column_int(stmt, 0);
            float tx = (float)sqlite3_column_double(stmt, 1);
            float tz = (float)sqlite3_column_double(stmt, 2);
            float dx = tx - position.x_;
            float dz = tz - position.z_;
            float distSq = dx * dx + dz * dz;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestId = tid;
            }
        }
        sqlite3_finalize(stmt);
    }
    return bestId;
#else
    return 0;
#endif
}

bool AuthServer::NPCChopTree(ServerCreatureAI& ai, unsigned treeId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    // Check NPC has a tool equipped
    int equippedTool = worldDB_->GetEquippedItem(npcPlayerId, "hand");
    int toolTier = 0;
    if (equippedTool > 0 && gameDB_)
    {
        ItemInfo info;
        if (gameDB_->GetItem(equippedTool, info))
            toolTier = info.tier;
    }

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int treeHp = 0;
    int species = 0;

    if (sqlite3_prepare_v2(db, "SELECT hp, species, growth_stage FROM trees WHERE tree_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)treeId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            treeHp = sqlite3_column_int(stmt, 0);
            species = sqlite3_column_int(stmt, 1);
            int stage = sqlite3_column_int(stmt, 2);
            if (stage < 2) treeHp = 0;  // can't chop saplings
        }
        sqlite3_finalize(stmt);
    }

    if (treeHp <= 0)
        return false;

    // Damage based on tool tier (same as HandleChopTree)
    int chopDamage = 20;
    if (toolTier >= 4) chopDamage = 100;
    else if (toolTier >= 3) chopDamage = 50;
    else if (toolTier >= 2) chopDamage = 33;

    treeHp = Max(0, treeHp - chopDamage);

    // Deduct tool durability
    if (equippedTool > 0)
        worldDB_->DeductDurability(npcPlayerId, "hand");

    // Update tree HP
    if (sqlite3_prepare_v2(db, "UPDATE trees SET hp = ? WHERE tree_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, treeHp);
        sqlite3_bind_int(stmt, 2, (int)treeId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (treeHp <= 0)
    {
        // Tree felled — stump + yield wood
        int gameDay = (int)(GetSubsystem<Time>()->GetElapsedTime() / 300.0f);
        if (sqlite3_prepare_v2(db, "UPDATE trees SET growth_stage = 0, planted_day = ? WHERE tree_id = ?",
                               -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, gameDay);
            sqlite3_bind_int(stmt, 2, (int)treeId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        int woodItemId = (species == 1 || species == 3 || species == 4) ? ITEM_SOFTWOOD : ITEM_HARDWOOD;  // pine/acacia/willow→soft
        int yield = 2 + (toolTier >= 2 ? 1 : 0) + (toolTier >= 4 ? 1 : 0);
        AddItemToWorldInventory(npcPlayerId, woodItemId, yield);

        // Broadcast removal
        VectorBuffer buf;
        buf.WriteU32(treeId);
        for (auto it = sessions_.Begin(); it != sessions_.End(); ++it)
        {
            if (it->second_.authenticated)
                it->first_->SendMessage(MSG_REMOVE_TREE, true, true, buf);
        }

        URHO3D_LOGINFOF("[NPCChop] NPC spawnId=%u felled tree %u, yield=%d wood",
            ai.spawnId, treeId, yield);
    }

    NPCAwardXP(ai, "chop_tree");
    return true;
#else
    return false;
#endif
}

bool AuthServer::NPCLightTorch(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    // Must have unlit torch
    if (worldDB_->GetItemCount(npcPlayerId, ITEM_TORCH) < 1)
        return false;

    // Must be near a LIT campfire
    bool nearLitFire = false;
    for (auto it = serverCampfires_.Begin(); it != serverCampfires_.End(); ++it)
    {
        if (it->second_.state == PIT_LIT &&
            (ai.position - it->second_.position).Length() < 5.0f)
        {
            nearLitFire = true;
            break;
        }
    }
    if (!nearLitFire)
        return false;

    // Convert: consume unlit, add lit
    worldDB_->RemoveItemFromInventory(npcPlayerId, ITEM_TORCH, 1);
    AddItemToWorldInventory(npcPlayerId, ITEM_BURNING_TORCH, 1);

    // Start burn timer
    torchTimers_[npcPlayerId] = TORCH_BURN_TIME;

    if (gameDB_)
        gameDB_->AwardXP(npcPlayerId, "fire_torch");

    URHO3D_LOGINFOF("[NPCTorch] NPC spawnId=%u lit a torch (%.0fs burn time)",
        ai.spawnId, TORCH_BURN_TIME);
    return true;
#else
    return false;
#endif
}

bool AuthServer::NPCDepositTorch(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return false;

    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0)
        return false;

    // Check if NPC still has a burning torch
    if (worldDB_->GetItemCount(npcPid, ITEM_BURNING_TORCH) < 1)
    {
        // Torch burned out during patrol — return empty-handed, no fuel deposit
        URHO3D_LOGINFOF("[NPCPatrol] NPC %u patrol ended — torch expired, no deposit", ai.spawnId);
        return false;
    }

    // Calculate remaining fuel from torch timer
    float torchRemaining = 0.0f;
    auto timerIt = torchTimers_.Find(npcPid);
    if (timerIt != torchTimers_.End())
        torchRemaining = timerIt->second_;

    // Remove the burning torch and cancel its timer
    worldDB_->RemoveItemFromInventory(npcPid, ITEM_BURNING_TORCH, 1);
    torchTimers_.Erase(npcPid);

    // Convert remaining torch time to campfire fuel
    float delivered = torchRemaining * TORCH_TO_CAMPFIRE_RATIO;

    // Add fuel to the NPC's campfire
    auto cfIt = serverCampfires_.Find(ai.campfireId);
    if (cfIt != serverCampfires_.End() && delivered > 0.0f)
    {
        ServerCampfire& cf = cfIt->second_;
        cf.fuelSeconds = Min(cf.fuelSeconds + delivered, cf.maxFuelSeconds);

        // Revive campfire if it was dying
        if (cf.state == PIT_COLD && cf.fuelSeconds > 0.0f)
            cf.state = PIT_EMBERS;
        if (cf.state == PIT_EMBERS && cf.fuelSeconds > PIT_EMBERS_THRESHOLD)
            cf.state = PIT_LIT;

        BroadcastPitState(ai.campfireId, cf);

        URHO3D_LOGINFOF("[NPCPatrol] NPC %u deposited torch — %.0fs remaining → %.1f BU delivered (campfire fuel=%.0f)",
            ai.spawnId, torchRemaining, delivered, cf.fuelSeconds);
    }

    // Award fire-tending XP
    NPCAwardXP(ai, "fire_tend");
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Shared Build + Repair helpers (used by both player handlers and NPC AI)
// ---------------------------------------------------------------------------

int AuthServer::BuildForOwner(int playerId, int buildingTypeId, const Vector3& pos,
                               float rotation, int snappedTo, const String& ownerName,
                               Connection* connection)
{
    if (!worldDB_ || !gameDB_)
        return -1;

    auto typeIt = cachedBuildingTypes_.Find(buildingTypeId);
    if (typeIt == cachedBuildingTypes_.End())
    {
        if (connection)
        {
            VectorBuffer reply;
            reply.WriteBool(false);
            reply.WriteString("Unknown building type");
            connection->SendMessage(MSG_BUILD_RESULT, true, true, reply);
        }
        return -1;
    }
    const BuildingTypeDBInfo& btype = typeIt->second_;

    // Check materials
    Vector<BuildingRecipeInput> recipe = gameDB_->GetBuildingRecipe(buildingTypeId);
    for (unsigned i = 0; i < recipe.Size(); ++i)
    {
        if (worldDB_->GetItemCount(playerId, recipe[i].itemId) < recipe[i].quantity)
        {
            if (connection)
            {
                VectorBuffer reply;
                reply.WriteBool(false);
                reply.WriteString("Insufficient materials");
                connection->SendMessage(MSG_BUILD_RESULT, true, true, reply);
            }
            return -1;
        }
    }

    // Deduct materials
    for (unsigned i = 0; i < recipe.Size(); ++i)
        worldDB_->RemoveItemFromInventory(playerId, recipe[i].itemId, recipe[i].quantity);

    // Master Builder: +20% HP on construction
    int buildHp = btype.maxHp;
    for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
    {
        if (GetNPCPlayerId(aiIt->first_) == playerId && aiIt->second_.isMasterBuilder)
        { buildHp = (int)(btype.maxHp * 1.2f); break; }
    }

    // Insert into world database
    int placedId = worldDB_->InsertPlacedBuilding(buildingTypeId, playerId,
        pos.x_, pos.y_, pos.z_, rotation, buildHp, currentGameDay_, snappedTo);

    if (placedId < 0)
    {
        // Refund materials on DB failure
        for (unsigned i = 0; i < recipe.Size(); ++i)
            AddItemToWorldInventory(playerId, recipe[i].itemId, recipe[i].quantity);

        if (connection)
        {
            VectorBuffer reply;
            reply.WriteBool(false);
            reply.WriteString("Database error");
            connection->SendMessage(MSG_BUILD_RESULT, true, true, reply);
        }
        return -1;
    }

    // Send success to player (if connected)
    if (connection)
    {
        VectorBuffer reply;
        reply.WriteBool(true);
        reply.WriteI32(placedId);
        connection->SendMessage(MSG_BUILD_RESULT, true, true, reply);
        SendInventoryUpdate(connection, playerId);
    }

    BroadcastBuildingSpawn(placedId, buildingTypeId, pos.x_, pos.y_, pos.z_, rotation, btype.maxHp);

    // Water Phase 4: Initialize well state when placed
    if (buildingTypeId == BUILDING_WELL)
        InitializeWell(placedId, pos.x_, pos.z_);
    // Rain Collection: Initialize barrel state when placed
    if (buildingTypeId == BUILDING_WATER_BARREL)
        InitializeBarrel(placedId, pos.x_, pos.z_);

    // Phase 15b: auto-furnish shelters and workshops
    if (btype.category == "shelter" || btype.category == "workshop")
        AutoFurnishBuilding(placedId, buildingTypeId, playerId, pos, rotation);

    LogMessage(ownerName + " built " + btype.name + " (id=" + String(placedId) +
        ") at " + String(pos.x_) + "," + String(pos.y_) + "," + String(pos.z_));
    return placedId;
}

void AuthServer::AutoFurnishBuilding(int parentPlacedId, int buildingTypeId, int ownerId,
                                      const Vector3& buildingPos, float buildingRotation)
{
    if (!worldDB_ || !gameDB_)
        return;

    // Furniture layout per building type — offset from building center, rotated by building rotation
    struct FurniturePlacement
    {
        int typeId;      // building type ID for furniture
        float offX;      // local X offset
        float offZ;      // local Z offset
    };

    Vector<FurniturePlacement> furniture;

    if (buildingTypeId == 23)  // Hut (5x5)
    {
        furniture.Push({101, -1.2f, -0.5f});   // Bed
        furniture.Push({100,  1.2f,  0.8f});   // Chest
        furniture.Push({145,  0.0f, -2.5f});   // Wooden Door (front)
        furniture.Push({147,  2.5f,  0.0f});   // Window Shutters (side)
    }
    else if (buildingTypeId == 24)  // Longhouse (10x5)
    {
        furniture.Push({101, -2.5f, -1.0f});   // Bed (left)
        furniture.Push({101, -2.5f,  1.0f});   // Bed (right)
        furniture.Push({102,  2.5f, -1.0f});   // Bed Large
        furniture.Push({104,  0.0f,  0.0f});   // Table (center)
        furniture.Push({100,  3.0f,  1.5f});   // Chest
        furniture.Push({146,  0.0f, -2.5f});   // Round Door (entrance)
        furniture.Push({147, -3.0f,  2.5f});   // Window (left)
        furniture.Push({147,  3.0f,  2.5f});   // Window (right)
    }
    else if (buildingTypeId == 42)  // Stone House (6x6)
    {
        furniture.Push({101, -1.5f, -1.0f});   // Bed
        furniture.Push({102,  1.5f, -1.0f});   // Bed Large
        furniture.Push({104,  0.0f,  0.5f});   // Table
        furniture.Push({100,  2.0f,  2.0f});   // Chest
        furniture.Push({145,  0.0f, -3.0f});   // Wooden Door
        furniture.Push({147, -3.0f,  0.0f});   // Window (left)
        furniture.Push({148,  3.0f,  0.0f});   // Round Window (right)
        furniture.Push({127,  0.0f,  2.5f});   // Wall Torch (interior)
    }
    else if (buildingTypeId == 31)  // Workshop (4x4)
    {
        furniture.Push({104, -0.5f,  0.0f});   // Table
        furniture.Push({100,  1.0f,  1.0f});   // Chest
        furniture.Push({113, -1.2f, -1.2f});   // Shelf
        furniture.Push({145,  0.0f, -2.0f});   // Wooden Door
    }
    else if (buildingTypeId == 86)  // Barn (8x6, crop/material storage)
    {
        furniture.Push({105, -2.5f, -1.5f});   // Barrel
        furniture.Push({105, -2.5f,  0.0f});   // Barrel
        furniture.Push({115, -2.5f,  1.5f});   // Crate
        furniture.Push({115,  2.5f, -1.5f});   // Crate
        furniture.Push({100,  2.5f,  0.0f});   // Chest
        furniture.Push({113,  2.5f,  1.5f});   // Shelf
    }
    else
    {
        return;  // no auto-furnish for this type
    }

    // Rotate offsets by building rotation
    float rad = buildingRotation * 0.0174533f;  // deg to rad
    float cosR = cosf(rad);
    float sinR = sinf(rad);

    for (unsigned i = 0; i < furniture.Size(); ++i)
    {
        const FurniturePlacement& fp = furniture[i];
        float rx = fp.offX * cosR - fp.offZ * sinR;
        float rz = fp.offX * sinR + fp.offZ * cosR;
        Vector3 fpos(buildingPos.x_ + rx, buildingPos.y_, buildingPos.z_ + rz);

        auto typeIt = cachedBuildingTypes_.Find(fp.typeId);
        if (typeIt == cachedBuildingTypes_.End())
            continue;

        int furnId = worldDB_->InsertPlacedBuilding(fp.typeId, ownerId,
            fpos.x_, fpos.y_, fpos.z_, buildingRotation, typeIt->second_.maxHp,
            currentGameDay_, 0, parentPlacedId);
        if (furnId >= 0)
            BroadcastBuildingSpawn(furnId, fp.typeId, fpos.x_, fpos.y_, fpos.z_,
                                  buildingRotation, typeIt->second_.maxHp);
    }

    URHO3D_LOGINFOF("[Furnish] Auto-furnished building %d (type %d) with %u pieces",
        parentPlacedId, buildingTypeId, furniture.Size());
}

bool AuthServer::RepairForOwner(int playerId, int placedId, const String& ownerName,
                                 Connection* connection)
{
    if (!worldDB_ || !gameDB_)
        return false;

    PlacedBuildingDBInfo placed;
    if (!worldDB_->GetPlacedBuilding(placedId, placed))
        return false;

    auto typeIt = cachedBuildingTypes_.Find(placed.buildingId);
    if (typeIt == cachedBuildingTypes_.End())
        return false;

    if (placed.hp >= typeIt->second_.maxHp)
        return false;  // already full HP

    Vector<RepairCostInfo> costs = gameDB_->GetRepairCosts(placed.buildingId);
    for (unsigned i = 0; i < costs.Size(); ++i)
    {
        if (worldDB_->GetItemCount(playerId, costs[i].itemId) >= costs[i].quantity)
        {
            worldDB_->RemoveItemFromInventory(playerId, costs[i].itemId, costs[i].quantity);
            // Master Builder: 2x repair effectiveness
            int repairAmount = costs[i].hpRestored;
            for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
            {
                if (GetNPCPlayerId(aiIt->first_) == playerId && aiIt->second_.isMasterBuilder)
                { repairAmount *= 2; break; }
            }
            int newHp = Min(placed.hp + repairAmount, typeIt->second_.maxHp);
            worldDB_->UpdateBuildingHp(placedId, newHp);
            worldDB_->SetLastRepair(placedId, currentGameDay_);
            BroadcastBuildingHp(placedId, newHp);

            if (connection)
                SendInventoryUpdate(connection, playerId);

            LogMessage(ownerName + " repaired building " + String(placedId) +
                " (" + String(placed.hp) + " -> " + String(newHp) + " HP)");
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 3: Build + Repair wrappers
// ---------------------------------------------------------------------------

int AuthServer::NPCBuild(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_)
        return -1;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return -1;

    // Scan building types: pick the highest-tier building the NPC can afford.
    // Priority: shelter > storage > defense (per plan).
    int bestTypeId = -1;
    int bestPriority = -1;

    for (auto it = cachedBuildingTypes_.Begin(); it != cachedBuildingTypes_.End(); ++it)
    {
        int typeId = it->first_;
        const BuildingTypeDBInfo& btype = it->second_;

        // No skill gates — NPCs can attempt any building. Skill check in
        // completion handler determines success/failure.
        if (typeId == 25 && !ai.isMasterBuilder) continue;  // Longhouse: Master Builder only (role gate, not skill)

        Vector<BuildingRecipeInput> recipe = gameDB_->GetBuildingRecipe(typeId);
        if (recipe.Empty())
            continue;

        bool canAfford = true;
        for (unsigned i = 0; i < recipe.Size(); ++i)
        {
            if (worldDB_->GetItemCount(npcPlayerId, recipe[i].itemId) < recipe[i].quantity)
            {
                canAfford = false;
                break;
            }
        }
        if (!canAfford)
            continue;

        int priority = 0;
        if (btype.category == "shelter")       priority = 30 + btype.tier;
        else if (btype.category == "storage")  priority = 20 + btype.tier;
        else if (btype.category == "workshop") priority = 15 + btype.tier;
        else if (btype.category == "utility")  priority = 12 + btype.tier;
        else if (btype.category == "defense")  priority = 10 + btype.tier;
        else                                   priority = btype.tier;

        if (priority > bestPriority)
        {
            bestPriority = priority;
            bestTypeId = typeId;
        }
    }

    if (bestTypeId < 0)
        return -1;

    // Place near campfire with slight random offset
    Vector3 buildPos = ai.homePosition;
    buildPos.x_ += Random(-5.0f, 5.0f);
    buildPos.z_ += Random(-5.0f, 5.0f);
    buildPos.y_ = GetTerrainHeightAI(buildPos.x_, buildPos.z_);

    String ownerName = "NPC_" + String(ai.spawnId);
    int placedId = BuildForOwner(npcPlayerId, bestTypeId, buildPos, Random(360.0f), 0, ownerName);
    if (placedId >= 0)
        NPCAwardXP(ai, "build_structure");
    return placedId;
#else
    return -1;
#endif
}

bool AuthServer::NPCRepair(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    // Find nearest damaged building owned by this NPC within reach
    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].ownerId != npcPlayerId)
            continue;

        auto typeIt = cachedBuildingTypes_.Find(buildings[i].buildingId);
        if (typeIt == cachedBuildingTypes_.End())
            continue;

        if (buildings[i].hp >= typeIt->second_.maxHp)
            continue;

        Vector3 bPos(buildings[i].posX, buildings[i].posY, buildings[i].posZ);
        if ((ai.position - bPos).Length() > 10.0f)
            continue;

        String ownerName = "NPC_" + String(ai.spawnId);
        if (RepairForOwner(npcPlayerId, buildings[i].id, ownerName))
        {
            NPCAwardXP(ai, "build_repair");
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

bool AuthServer::NPCMine(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!depositMap_ || !scene_ || !worldDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    // Verify NPC has a pick equipped
    int equippedTool = worldDB_->GetEquippedItem(npcPlayerId, "hand");
    if (equippedTool <= 0)
        return false;
    if (gameDB_)
    {
        ItemInfo info;
        if (!gameDB_->GetItem(equippedTool, info) || info.category != "tool")
            return false;
    }

    // Scan deposit map for nearest exposed deposit within 30m
    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain)
        return false;

    // Convert NPC position to heightmap coords and scan a local window
    IntVector2 center = terrain->WorldToHeightMap(ai.position);
    int searchRadius = 15;  // ~30m at spacing 2.0
    float bestDistSq = 999999.0f;
    int bestPx = -1, bestPz = -1;

    for (int dz = -searchRadius; dz <= searchRadius; ++dz)
    {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx)
        {
            int px = center.x_ + dx;
            int pz = center.y_ + dz;
            if (px < 0 || px >= depositMapSize_ || pz < 0 || pz >= depositMapSize_)
                continue;

            Color c = depositMap_->GetPixel(px, pz);
            int qty = (int)(c.r_ * 255.0f + 0.5f);
            int type = (int)(c.g_ * 255.0f + 0.5f);
            int depth = (int)(c.a_ * 255.0f + 0.5f);
            if (type == 0 || qty == 0)
                continue;

            // Check exposed (Phase 31: Mine Shaft grants +10m depth tolerance)
            Vector3 worldPos = terrain->HeightMapToWorld(IntVector2(px, pz));
            float terrainH = terrain->GetHeight(worldPos);
            float exposedAt = 60.0f - depth * 0.1f;
            float npcDepthTol = 2.0f;
            if (worldDB_)
            {
                Vector<PlacedBuildingDBInfo> blds = worldDB_->GetAllPlacedBuildings();
                for (unsigned bi = 0; bi < blds.Size(); ++bi)
                    if (blds[bi].buildingId == 80 &&
                        (Vector3(blds[bi].posX, 0, blds[bi].posZ) - Vector3(worldPos.x_, 0, worldPos.z_)).LengthSquared() < 225.0f)
                    { npcDepthTol = 12.0f; break; }
            }
            if (terrainH > exposedAt + npcDepthTol)
                continue;

            float distSq = (float)(dx * dx + dz * dz);
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestPx = px;
                bestPz = pz;
            }
        }
    }

    if (bestPx < 0)
        return false;

    Vector3 minePos = terrain->HeightMapToWorld(IntVector2(bestPx, bestPz));
    String ownerName = "NPC_" + String(ai.spawnId);
    int mined = MineForOwner(npcPlayerId, minePos.x_, minePos.z_, ownerName);
    if (mined > 0)
    {
        NPCAwardXP(ai, "knap_stone");
        return true;
    }
    return false;
#else
    return false;
#endif
}

bool AuthServer::NPCSmelt(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    // Need a LIT kiln (building 604) within CRAFTING_STATION_RANGE
    bool hasLitKiln = false;
    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned b = 0; b < buildings.Size(); ++b)
    {
        if (buildings[b].buildingId != 604)
            continue;
        Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
        if ((ai.position - bPos).Length() > CRAFTING_STATION_RANGE)
            continue;

        // Check for LIT campfire near kiln
        for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
        {
            if (cfIt->second_.state == PIT_LIT &&
                (cfIt->second_.position - bPos).Length() <= CRAFTING_STATION_RANGE)
            {
                hasLitKiln = true;
                break;
            }
        }
        if (hasLitKiln)
            break;
    }
    if (!hasLitKiln)
        return false;

    // Find a smelting recipe the NPC can afford (Kiln=604 or Forge=81 for alloys)
    Vector<RecipeInfo> recipes = gameDB_->GetRecipesForTier(10);
    int bestRecipeId = -1;
    int bestTier = -1;

    HashMap<int, int> invMap;
    Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPlayerId);
    for (unsigned i = 0; i < inv.Size(); ++i)
        invMap[inv[i].itemId] += inv[i].quantity;

    for (unsigned i = 0; i < recipes.Size(); ++i)
    {
        if (recipes[i].stationReq != 604 && recipes[i].stationReq != 81)
            continue;

        ItemInfo outItem;
        if (!gameDB_->GetItem(recipes[i].outputId, outItem))
            continue;
        if (!outItem.name.Contains("Ingot"))
            continue;

        if (!gameDB_->CanCraft(recipes[i].id, invMap))
            continue;

        if (recipes[i].tier > bestTier)
        {
            bestTier = recipes[i].tier;
            bestRecipeId = recipes[i].id;
        }
    }

    if (bestRecipeId < 0)
        return false;

    String ownerName = "NPC_" + String(ai.spawnId);
    if (CraftForOwner(npcPlayerId, bestRecipeId, ai.position, ownerName))
    {
        NPCAwardXP(ai, "craft_smelt");
        // Phase 38: chance to produce Mystery Alloy if trace ore in inventory
        TryMysteryAlloy(ai, npcPlayerId);
        // Phase 41: track alloy crafts for Master Smith recognition
        if (bestRecipeId >= 130 && bestRecipeId <= 134)
        {
            ai.alloyCraftCount++;
            CheckMasterSmith(ai);
        }
        return true;
    }
    return false;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 18: Burial + Knowledge Transfer
// ---------------------------------------------------------------------------

unsigned AuthServer::FindBuriableCorpse(const ServerCreatureAI& ai)
{
    // Scan death scents for a human corpse near this NPC's area.
    // Proximity filter (40m) naturally limits to same settlement region.
    float bestDist = 40.0f;
    unsigned bestId = 0;
    for (unsigned s = 0; s < serverScents_.Size(); ++s)
    {
        const ServerScentMarker& scent = serverScents_[s];
        if (!IsHumanSpecies(scent.speciesId))
            continue;
        float dist = (ai.position - scent.position).Length();
        if (dist < bestDist)
        {
            bestDist = dist;
            bestId = scent.spawnId;
        }
    }
    return bestId;
}

bool AuthServer::NPCBury(ServerCreatureAI& ai)
{
    if (!worldDB_ || !gameDB_)
        return false;

    // Find the scent marker for the corpse we walked to
    int scentIdx = -1;
    for (unsigned s = 0; s < serverScents_.Size(); ++s)
    {
        if (serverScents_[s].spawnId == ai.targetId)
        {
            scentIdx = static_cast<int>(s);
            break;
        }
    }
    if (scentIdx < 0)
        return false;  // corpse scent expired or already buried

    Vector3 corpsePos = serverScents_[scentIdx].position;

    // Remove scent first — prevents a second NPC from picking up the same corpse
    // if BuildForOwner fails (scent is the resource, grave is the receipt).
    serverScents_.Erase(scentIdx);

    // Place grave at corpse location (Grave building type = 60, tier 0, no recipe required)
    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0)
        return false;

    int placedId = BuildForOwner(npcPid, 60 /* Grave */, corpsePos, 0.0f, 0,
                                 "NPC_" + String(ai.spawnId), nullptr);

    if (placedId >= 0)
    {
        // Award Animal Lore XP for performing burial rites
        NPCAwardXP(ai, "bury_dead");

        // Persist deceased name in grave's storage JSON
        String deceasedName = "unknown";
        auto decIt = creatureAI_.Find(ai.targetId);
        if (decIt != creatureAI_.End() && !decIt->second_.npcName.Empty())
            deceasedName = decIt->second_.npcName;

        String graveJson = "{\"deceased\":\"" + deceasedName + "\",\"day\":" + String(currentGameDay_) + "}";
        worldDB_->UpdateBuildingStorage(placedId, graveJson);

        LogMessage("[Burial] " + (ai.npcName.Empty() ? String(ai.spawnId) : ai.npcName) +
                   " buried " + deceasedName + " (spawnId=" + String(ai.targetId) +
                   ") at " + corpsePos.ToString() + " — grave id=" + String(placedId));
    }
    return placedId >= 0;
}

void AuthServer::TransferKnowledge(unsigned deadSpawnId, const Vector3& position)
{
    // When a skilled NPC dies (any skill at level 7+), nearby settlement members
    // gain 10% of the dead NPC's top skill XP. Represents oral tradition — the
    // tribe remembers what the elder knew.
    if (!gameDB_)
        return;

    int deadPid = GetNPCPlayerId(deadSpawnId);
    if (deadPid <= 0)
        return;

    // Find the dead NPC's top skill at level 7+
    static const int allSkills[] = {
        1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16,
        20, 21, 22, 23, 24, 25, 26, 27, 28,
        30, 31, 32, 33
    };
    int topSkillId = 0;
    int topSkillLevel = 0;
    int topSkillXP = 0;
    for (int sk : allSkills)
    {
        int level = gameDB_->GetSkillLevel(deadPid, sk);
        if (level >= 7 && level > topSkillLevel)
        {
            topSkillLevel = level;
            topSkillId = sk;
            topSkillXP = gameDB_->GetSkillXP(deadPid, sk);
        }
    }

    if (topSkillId == 0 || topSkillXP == 0)
        return;  // no skill at 7+ or no XP to transfer

    int xpToGive = Max(1, topSkillXP / 10);  // 10% of top skill XP, minimum 1

    // Find nearby human NPCs (within 30m — same settlement area)
    int recipients = 0;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->first_ == deadSpawnId)
            continue;
        const ServerCreatureAI& other = it->second_;
        if (!other.isHuman)
            continue;
        if ((other.position - position).Length() > 30.0f)
            continue;

        int otherPid = GetNPCPlayerId(it->first_);
        if (otherPid > 0)
        {
            gameDB_->AddXPDirect(otherPid, topSkillId, xpToGive);
            ++recipients;
        }
    }

    if (recipients > 0)
    {
        LogMessage("[Knowledge] Elder NPC " + String(deadSpawnId) + " (skill " +
                   String(topSkillId) + " lv" + String(topSkillLevel) +
                   ") — " + String(xpToGive) + " XP transferred to " +
                   String(recipients) + " nearby NPCs");
    }
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 23: Food Decay
// ---------------------------------------------------------------------------

void AuthServer::TickFoodDecay(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_)
        return;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;
        if (!ai.isHuman)
            continue;
        int npcPid = GetNPCPlayerId(ai.spawnId);
        if (npcPid <= 0)
            continue;

        Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPid);

        // Check for preservation: Clay Jar (10x) and Granary (5x, stacks)
        bool hasJar = false;
        bool hasGranary = false;
        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            if (inv[i].itemId == ITEM_CLAY_JAR && inv[i].quantity > 0)
                hasJar = true;
        }
        // Phase 35: Granary proximity check
        Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
        for (unsigned b = 0; b < buildings.Size(); ++b)
        {
            if (buildings[b].buildingId == 84 &&
                (Vector3(buildings[b].posX, buildings[b].posY, buildings[b].posZ) - ai.homePosition).Length() < 25.0f)
            { hasGranary = true; break; }
        }

        for (unsigned i = 0; i < inv.Size(); ++i)
        {
            FoodInfo food;
            if (!gameDB_->GetFoodProperties(inv[i].itemId, food))
                continue;
            if (food.spoilTime <= 0.0f)
                continue;

            float effectiveSpoil = food.spoilTime;
            if (hasJar) effectiveSpoil *= 10.0f;
            if (hasGranary) effectiveSpoil *= 5.0f;

            if (Random(effectiveSpoil) < dt)
                worldDB_->RemoveItemFromInventory(npcPid, inv[i].itemId, 1);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 25: Selective Breeding
// ---------------------------------------------------------------------------

void AuthServer::CheckTamedBreeding()
{
#ifdef URHO3D_DATABASE_SQLITE
    int season = GetCurrentSeasonIndex();
    if (season != 0 && season != 1)
        return;  // spring/summer only

    HashMap<unsigned long long, Vector<unsigned>> herds;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& ai = it->second_;
        if (ai.tamerId == 0 || ai.isHuman) continue;
        if (ai.breedCooldown > 0.0f) continue;
        auto tamerIt = creatureAI_.Find(ai.tamerId);
        if (tamerIt == creatureAI_.End()) continue;
        unsigned campfire = tamerIt->second_.campfireId;
        unsigned long long key = ((unsigned long long)campfire << 32) | (unsigned)ai.creatureId;
        herds[key].Push(it->first_);
    }

    for (auto hIt = herds.Begin(); hIt != herds.End(); ++hIt)
    {
        if (hIt->second_.Size() < 2) continue;
        unsigned campfireId = (unsigned)(hIt->first_ >> 32);
        int speciesId = (int)(hIt->first_ & 0xFFFFFFFF);

        bool hasHerder = false;
        for (auto nIt = creatureAI_.Begin(); nIt != creatureAI_.End(); ++nIt)
        {
            if (!nIt->second_.isHuman || nIt->second_.campfireId != campfireId) continue;
            if (GetNPCSkillLevel(nIt->first_, SKILL_ANIMAL_LORE) >= 5)
            { hasHerder = true; break; }
        }
        if (!hasHerder) continue;

        unsigned parentA = hIt->second_[0];
        unsigned parentB = hIt->second_[1];
        auto paIt = creatureAI_.Find(parentA);
        auto pbIt = creatureAI_.Find(parentB);
        if (paIt == creatureAI_.End() || pbIt == creatureAI_.End()) continue;

        // INNOV_SELECTIVE_FEED: trait drift +0.03 (biased positive) instead of ±0.05
        float drift = 0.05f;
        auto tamerChk = creatureAI_.Find(paIt->second_.tamerId);
        if (tamerChk != creatureAI_.End() && HasInnovation(tamerChk->second_.campfireId, INNOV_SELECTIVE_FEED))
            drift = 0.03f;
        float childSize  = Clamp((paIt->second_.traitSize  + pbIt->second_.traitSize)  * 0.5f + Random(0.0f, drift), 0.8f, 1.2f);
        float childSpeed = Clamp((paIt->second_.traitSpeed + pbIt->second_.traitSpeed) * 0.5f + Random(0.0f, drift), 0.8f, 1.2f);
        float childYield = Clamp((paIt->second_.traitYield + pbIt->second_.traitYield) * 0.5f + Random(0.0f, drift), 0.8f, 1.2f);

        Vector3 spawnPos = paIt->second_.position;
        spawnPos.x_ += Random(-3.0f, 3.0f);
        spawnPos.z_ += Random(-3.0f, 3.0f);
        spawnPos.y_ = GetTerrainHeightAI(spawnPos.x_, spawnPos.z_);
        if (spawnPos.y_ <= AI_WATER_LEVEL) continue;

        unsigned childId = ++nextSpawnId_;
        ServerCreatureAI& child = creatureAI_[childId];
        child.position = spawnPos;
        child.targetPosition = spawnPos;
        child.creatureId = speciesId;
        child.regionId = paIt->second_.regionId;
        child.tamerId = paIt->second_.tamerId;
        child.homePosition = paIt->second_.homePosition;
        child.traitSize = childSize;
        child.traitSpeed = childSpeed;
        child.traitYield = childYield;
        child.moveSpeed = 2.0f * childSpeed;
        child.growthProgress = 0.3f;

        ServerCreatureState cs;
        cs.creatureId = speciesId;
        cs.position = spawnPos;
        cs.regionId = child.regionId;
        if (!LoadCreatureCombat(speciesId, cs))
        { cs.hp = cs.maxHp = 10; }
        creatureStates_[childId] = cs;

        paIt->second_.breedCooldown = 600.0f;
        pbIt->second_.breedCooldown = 600.0f;

        // Broadcast spawn to all clients (including breeding traits)
        {
            VectorBuffer buf;
            buf.WriteI32(child.regionId);
            buf.WriteI32(speciesId);
            buf.WriteFloat(spawnPos.x_);
            buf.WriteFloat(0.0f);  // clients snap Y to terrain
            buf.WriteFloat(spawnPos.z_);
            buf.WriteU32(childId);
            buf.WriteString(String::EMPTY);  // no name for animals
            buf.WriteFloat(childSize);
            buf.WriteFloat(childSpeed);
            buf.WriteFloat(childYield);
            for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
                if (sIt->second_.authenticated)
                    sIt->first_->SendMessage(MSG_SPAWN_CREATURE, true, true, buf);
        }

        URHO3D_LOGINFOF("[Breeding] Offspring spawnId=%u species=%d traits(%.2f,%.2f,%.2f) parents=%u+%u",
            childId, speciesId, childSize, childSpeed, childYield, parentA, parentB);
    }
#endif
}

void AuthServer::NPCCullWeakest(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return;

    unsigned weakestId = 0;
    float weakestScore = 999.0f;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& animal = it->second_;
        if (animal.tamerId == 0 || animal.isHuman) continue;
        auto tamerIt = creatureAI_.Find(animal.tamerId);
        if (tamerIt == creatureAI_.End() || tamerIt->second_.campfireId != ai.campfireId) continue;
        float score = animal.traitSize + animal.traitSpeed + animal.traitYield;
        if (score < weakestScore) { weakestScore = score; weakestId = it->first_; }
    }
    if (weakestId == 0) return;

    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid > 0)
    {
        auto weakIt = creatureAI_.Find(weakestId);
        if (weakIt != creatureAI_.End())
        {
            int yieldMeat = Max(1, (int)(3.0f * weakIt->second_.traitYield));
            AddItemToWorldInventory(npcPid, 7 /* Raw Meat */, yieldMeat);
            AddItemToWorldInventory(npcPid, 21 /* Hide */, 1);
        }
    }

    auto csIt = creatureStates_.Find(weakestId);
    if (csIt != creatureStates_.End())
    {
        BroadcastCreatureDeath(weakestId, csIt->second_, nullptr, DEATH_COMBAT);
        creatureStates_.Erase(csIt);
    }
    creatureAI_.Erase(weakestId);

    NPCAwardXP(ai, "observe_animal");
    URHO3D_LOGINFOF("[Breeding] NPC %u culled weakest animal %u (score=%.2f)",
        ai.spawnId, weakestId, weakestScore);
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 26: Boat Building
// ---------------------------------------------------------------------------

bool AuthServer::NPCBuildBoat(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return false;

    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0) return false;

    // Need 5 planks
    if (worldDB_->GetItemCount(npcPid, ITEM_PLANK) < 5) return false;

    // Place canoe at nearest water edge, snapped to water level
    Vector3 water = FindWaterEdge(ai.position, 30.0f);
    if (water == Vector3::ZERO) return false;
    water.y_ = AI_WATER_LEVEL;

    int placedId = BuildForOwner(npcPid, BUILDING_CANOE, water, 0.0f, 0,
                                 "NPC_" + String(ai.spawnId), nullptr);

    if (placedId >= 0)
    {
        NPCAwardXP(ai, "craft_wood");
        URHO3D_LOGINFOF("[BoatBuilding] NPC %u built canoe (id=%d) at (%.0f,%.0f)",
            ai.spawnId, placedId, water.x_, water.z_);
    }
    return placedId >= 0;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Metallurgy Phase 38: Accidental Discovery (Blind Alloys)
// ---------------------------------------------------------------------------

bool AuthServer::TryMysteryAlloy(ServerCreatureAI& ai, int npcPlayerId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return false;

    // Check inventory for any trace ore
    static const int traceOres[] = { 840, 841, 842, 843, 844 };
    static const int traceElements[] = { 7, 13, 9, 10, 11 };  // deposit types matching ore items
    int foundOre = 0;
    int foundElement = 0;

    for (int i = 0; i < 5; ++i)
    {
        if (worldDB_->GetItemCount(npcPlayerId, traceOres[i]) > 0)
        {
            foundOre = traceOres[i];
            foundElement = traceElements[i];
            break;
        }
    }
    if (foundOre == 0) return false;

    // 30% chance the trace ore gets consumed and transforms the output
    if (Random(1.0f) >= TRACE_ORE_CONSUME_CHANCE)
        return false;

    // Consume trace ore
    worldDB_->RemoveItemFromInventory(npcPlayerId, foundOre, 1);

    // Swap last-produced ingot for Mystery Alloy
    // Remove 1 iron or steel ingot, add 1 Mystery Alloy
    bool swapped = false;
    if (worldDB_->GetItemCount(npcPlayerId, 825) > 0)  // Steel Ingot
    { worldDB_->RemoveItemFromInventory(npcPlayerId, 825, 1); swapped = true; }
    else if (worldDB_->GetItemCount(npcPlayerId, 807) > 0)  // Iron Ingot
    { worldDB_->RemoveItemFromInventory(npcPlayerId, 807, 1); swapped = true; }

    if (swapped)
    {
        AddItemToWorldInventory(npcPlayerId, ITEM_MYSTERY_ALLOY, 1);

        // Store trace element tag in alloy_metadata
        sqlite3* db = worldDB_->GetHandle();
        if (db)
        {
            sqlite3_stmt* ins = nullptr;
            if (sqlite3_prepare_v2(db,
                "INSERT OR REPLACE INTO alloy_metadata (owner_id, trace_element, quantity) "
                "VALUES (?, ?, COALESCE((SELECT quantity FROM alloy_metadata WHERE owner_id=? AND trace_element=?), 0) + 1)",
                -1, &ins, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(ins, 1, npcPlayerId);
                sqlite3_bind_int(ins, 2, foundElement);
                sqlite3_bind_int(ins, 3, npcPlayerId);
                sqlite3_bind_int(ins, 4, foundElement);
                sqlite3_step(ins);
            }
            sqlite3_finalize(ins);
        }

        static const char* elementNames[] = { "", "", "", "", "", "", "",
            "manganese", "flint", "tungsten", "nickel", "carbon", "", "chromium" };
        const char* eName = (foundElement < 14) ? elementNames[foundElement] : "unknown";

        URHO3D_LOGINFOF("[Alloy] NPC %u accidentally produced Mystery Alloy (trace: %s, ore %d consumed)",
            ai.spawnId, eName, foundOre);
    }
    return swapped;
#else
    return false;
#endif
}

void AuthServer::RecordAlloyUse(unsigned campfireId, int traceElement, unsigned npcSpawnId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return;
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return;

    // Increment use count
    sqlite3_stmt* ups = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO alloy_observations (campfire_id, trace_element, use_count, discovered) "
        "VALUES (?, ?, 1, 0) ON CONFLICT(campfire_id, trace_element) DO UPDATE SET use_count = use_count + 1",
        -1, &ups, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(ups, 1, campfireId);
        sqlite3_bind_int(ups, 2, traceElement);
        sqlite3_step(ups);
    }
    sqlite3_finalize(ups);

    // Check if threshold reached
    sqlite3_stmt* chk = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT use_count, discovered FROM alloy_observations WHERE campfire_id=? AND trace_element=?",
        -1, &chk, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(chk, 1, campfireId);
        sqlite3_bind_int(chk, 2, traceElement);
        if (sqlite3_step(chk) == SQLITE_ROW)
        {
            int count = sqlite3_column_int(chk, 0);
            int discovered = sqlite3_column_int(chk, 1);
            if (count >= ALLOY_OBSERVATION_THRESHOLD && !discovered)
            {
                // Mark discovered
                sqlite3_finalize(chk);
                sqlite3_stmt* disc = nullptr;
                if (sqlite3_prepare_v2(db,
                    "UPDATE alloy_observations SET discovered=1 WHERE campfire_id=? AND trace_element=?",
                    -1, &disc, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(disc, 1, campfireId);
                    sqlite3_bind_int(disc, 2, traceElement);
                    sqlite3_step(disc);
                }
                sqlite3_finalize(disc);

                static const char* elementNames[] = { "", "", "", "", "", "", "",
                    "manganese", "flint", "tungsten", "nickel", "carbon", "", "chromium" };
                String eName = (traceElement < 14) ? elementNames[traceElement] : "unknown";
                RecordSettlementFirst(campfireId, "alloy_discovery_" + eName, npcSpawnId);
                IncrementAlloyKnowledge(campfireId, traceElement);
                return;
            }
        }
    }
    sqlite3_finalize(chk);
#endif
}

// ---------------------------------------------------------------------------
// Master Specialists — Chef + EvaluateMasters
// ---------------------------------------------------------------------------

void AuthServer::ApplyFoodQuality(ServerCreatureAI& ai, int cookSkill)
{
    if (cookSkill <= 2)
    {
        float illnessChance = 0.15f;
        if (HasMasterHerbalist(ai.campfireId))
            illnessChance *= 0.5f;  // Master Herbalist halves illness risk
        if (Random(1.0f) < illnessChance)
        {
            ai.illnessTimer = 300.0f;
            ai.illnessActive = true;
            ai.morale = Max(0.0f, ai.morale - 10.0f);
            auto csIt = creatureStates_.Find(ai.spawnId);
            if (csIt != creatureStates_.End())
                csIt->second_.hp = Max(1, csIt->second_.hp - 5);
            URHO3D_LOGINFOF("[Chef] NPC %u got food poisoning (cook skill %d)", ai.spawnId, cookSkill);
        }
    }
    else if (cookSkill >= 8)
    {
        ai.morale = Min(100.0f, ai.morale + 10.0f);
        for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        {
            if (it->second_.isHuman && it->second_.campfireId == ai.campfireId && it->first_ != ai.spawnId)
                it->second_.morale = Min(100.0f, it->second_.morale + 10.0f);
        }
    }
    else if (cookSkill >= 6)
    {
        ai.morale = Min(100.0f, ai.morale + 5.0f);
    }
}

void AuthServer::EvaluateMasters()
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;
        if (!ai.isHuman) continue;

        bool wasChef = ai.isMasterChef;
        ai.isMasterChef = (GetNPCSkillLevel(it->first_, SKILL_COOKING) >= 8);
        if (ai.isMasterChef && !wasChef)
        {
            RecordSettlementFirst(ai.campfireId, "master_chef", it->first_);
            URHO3D_LOGINFOF("[MasterChef] NPC %u (%s) earned Master Chef", it->first_, ai.npcName.CString());
        }

        bool wasHerb = ai.isMasterHerbalist;
        ai.isMasterHerbalist = (GetNPCSkillLevel(it->first_, SKILL_HERBALISM) >= 8);
        if (ai.isMasterHerbalist && !wasHerb)
        {
            RecordSettlementFirst(ai.campfireId, "master_herbalist", it->first_);
            URHO3D_LOGINFOF("[MasterHerbalist] NPC %u (%s) earned Master Herbalist", it->first_, ai.npcName.CString());
        }

        bool wasHunter = ai.isMasterHunter;
        ai.isMasterHunter = (GetNPCSkillLevel(it->first_, SKILL_MELEE) >= 8 ||
                             GetNPCSkillLevel(it->first_, SKILL_TRACKING) >= 8);
        if (ai.isMasterHunter && !wasHunter)
        {
            RecordSettlementFirst(ai.campfireId, "master_hunter", it->first_);
            URHO3D_LOGINFOF("[MasterHunter] NPC %u (%s) earned Master Hunter", it->first_, ai.npcName.CString());
        }

        bool wasFarmer = ai.isMasterFarmer;
        ai.isMasterFarmer = (GetNPCSkillLevel(it->first_, SKILL_FARMING) >= 8);
        if (ai.isMasterFarmer && !wasFarmer)
        {
            RecordSettlementFirst(ai.campfireId, "master_farmer", it->first_);
            URHO3D_LOGINFOF("[MasterFarmer] NPC %u (%s) earned Master Farmer", it->first_, ai.npcName.CString());
        }

        bool wasBuilder = ai.isMasterBuilder;
        ai.isMasterBuilder = (GetNPCSkillLevel(it->first_, SKILL_WOODWORK) >= 8);
        if (ai.isMasterBuilder && !wasBuilder)
        {
            RecordSettlementFirst(ai.campfireId, "master_builder", it->first_);
            URHO3D_LOGINFOF("[MasterBuilder] NPC %u (%s) earned Master Builder", it->first_, ai.npcName.CString());
        }

        bool wasTrader = ai.isMasterTrader;
        ai.isMasterTrader = (GetNPCSkillLevel(it->first_, SKILL_TRADE) >= 8);
        if (ai.isMasterTrader && !wasTrader)
        {
            RecordSettlementFirst(ai.campfireId, "master_trader", it->first_);
            URHO3D_LOGINFOF("[MasterTrader] NPC %u (%s) earned Master Trader", it->first_, ai.npcName.CString());
        }
    }
}

bool AuthServer::HasMasterFarmer(unsigned campfireId)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        if (it->second_.isHuman && it->second_.campfireId == campfireId && it->second_.isMasterFarmer)
            return true;
    return false;
}

bool AuthServer::HasMasterHerbalist(unsigned campfireId)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        if (it->second_.isHuman && it->second_.campfireId == campfireId && it->second_.isMasterHerbalist)
            return true;
    return false;
}

bool AuthServer::HasMasterHunter(unsigned campfireId)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        if (it->second_.isHuman && it->second_.campfireId == campfireId && it->second_.isMasterHunter)
            return true;
    return false;
}

bool AuthServer::HasMasterTrader(unsigned campfireId)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        if (it->second_.isHuman && it->second_.campfireId == campfireId && it->second_.isMasterTrader)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Metallurgy Phase 41: Master Smith
// ---------------------------------------------------------------------------

void AuthServer::CheckMasterSmith(ServerCreatureAI& ai)
{
    if (ai.isMasterSmith) return;
    if (ai.alloyCraftCount >= MASTER_SMITH_CRAFT_THRESHOLD)
    {
        ai.isMasterSmith = true;
        RecordSettlementFirst(ai.campfireId, "master_smith", ai.spawnId);
        URHO3D_LOGINFOF("[MasterSmith] NPC %u (%s) earned Master Smith status (%d alloy crafts)",
            ai.spawnId, ai.npcName.CString(), ai.alloyCraftCount);
    }
}

bool AuthServer::HasMasterSmith(unsigned campfireId)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->second_.isHuman && it->second_.campfireId == campfireId && it->second_.isMasterSmith)
            return true;
    }
    return false;
}

int AuthServer::CountKnownAlloys(unsigned campfireId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return 0;
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return 0;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM alloy_knowledge WHERE campfire_id=? AND effect_known=1",
        -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, campfireId);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Metallurgy Phase 39: Intentional Prospecting
// ---------------------------------------------------------------------------

int AuthServer::FindKnownTraceElement(unsigned campfireId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return 0;
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return 0;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT element_id FROM alloy_knowledge WHERE campfire_id=? AND effect_known=1 LIMIT 1",
        -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, campfireId);
    int result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
#else
    return 0;
#endif
}

void AuthServer::IncrementAlloyKnowledge(unsigned campfireId, int elementId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return;
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return;

    sqlite3_stmt* ups = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO alloy_knowledge (campfire_id, element_id, discovery_count, effect_known) "
        "VALUES (?, ?, 1, 0) ON CONFLICT(campfire_id, element_id) DO UPDATE SET discovery_count = discovery_count + 1",
        -1, &ups, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(ups, 1, campfireId);
        sqlite3_bind_int(ups, 2, elementId);
        sqlite3_step(ups);
    }
    sqlite3_finalize(ups);

    // Check if threshold reached (3 discoveries → effect_known)
    sqlite3_stmt* chk = nullptr;
    if (sqlite3_prepare_v2(db,
        "UPDATE alloy_knowledge SET effect_known=1 WHERE campfire_id=? AND element_id=? AND discovery_count>=3 AND effect_known=0",
        -1, &chk, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(chk, 1, campfireId);
        sqlite3_bind_int(chk, 2, elementId);
        if (sqlite3_step(chk) == SQLITE_DONE && sqlite3_changes(db) > 0)
        {
            URHO3D_LOGINFOF("[Prospecting] Settlement %u now understands element %d — intentional prospecting unlocked",
                campfireId, elementId);
        }
    }
    sqlite3_finalize(chk);
#endif
}

// ---------------------------------------------------------------------------
// Botanical Discovery System — generic species-property harvest
// ---------------------------------------------------------------------------

bool AuthServer::SettlementKnowsProperty(unsigned campfireId, int species, const String& property)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen()) return false;
    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    bool knows = false;
    if (sqlite3_prepare_v2(db,
        "SELECT 1 FROM settlement_botanical WHERE campfire_id = ? AND species = ? AND property = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)campfireId);
        sqlite3_bind_int(stmt, 2, species);
        sqlite3_bind_text(stmt, 3, property.CString(), -1, SQLITE_TRANSIENT);
        knows = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    return knows;
#else
    return false;
#endif
}

void AuthServer::RecordBotanicalDiscovery(unsigned campfireId, int species, const String& property, const ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen()) return;
    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int gameDay = (int)(GetSubsystem<Time>()->GetElapsedTime() / 300.0f);
    String npcName = ai.npcName.Empty() ? ("NPC_" + String(ai.spawnId)) : ai.npcName;

    if (sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO settlement_botanical (campfire_id, species, property, discoverer, game_day) VALUES (?, ?, ?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)campfireId);
        sqlite3_bind_int(stmt, 2, species);
        sqlite3_bind_text(stmt, 3, property.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, npcName.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, gameDay);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Also record in settlement_history for the milestone system
    String category = "first_" + property;
    RecordSettlementFirst(campfireId, category, ai.spawnId);

    URHO3D_LOGINFOF("[Botanical] Settlement %u discovered %s from species %d! Pioneer: %s",
                    campfireId, property.CString(), species, npcName.CString());
#endif
}

bool AuthServer::HarvestTreeProperty(int playerId, unsigned treeId, const String& property)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen() || !gameDB_)
        return false;

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int species = -1;

    // Get tree species
    if (sqlite3_prepare_v2(db, "SELECT species FROM trees WHERE tree_id = ? AND hp > 0 AND growth_stage >= 3",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)treeId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            species = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (species < 0) return false;

    // Look up species_properties for this (species, property)
    int yieldItemId = 0, skillId = 0, skillLevel = 0, yieldMin = 1, yieldMax = 2;
    float cooldown = 86400.0f;
    stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT yield_item_id, skill_id, skill_level, cooldown, yield_min, yield_max "
        "FROM species_properties WHERE species = ? AND property = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, species);
        sqlite3_bind_text(stmt, 2, property.CString(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            yieldItemId = sqlite3_column_int(stmt, 0);
            skillId = sqlite3_column_int(stmt, 1);
            skillLevel = sqlite3_column_int(stmt, 2);
            cooldown = (float)sqlite3_column_double(stmt, 3);
            yieldMin = sqlite3_column_int(stmt, 4);
            yieldMax = sqlite3_column_int(stmt, 5);
        }
        sqlite3_finalize(stmt);
    }

    // Species doesn't have this property — failed tap
    if (yieldItemId == 0)
        return false;

    // Check cooldown via tree_harvests table
    stmt = nullptr;
    double now = GetSubsystem<Time>()->GetElapsedTime();
    if (sqlite3_prepare_v2(db,
        "SELECT last_time FROM tree_harvests WHERE tree_id = ? AND property = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)treeId);
        sqlite3_bind_text(stmt, 2, property.CString(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            double lastTime = sqlite3_column_double(stmt, 0);
            if ((now - lastTime) < cooldown)
            {
                sqlite3_finalize(stmt);
                return false;  // still on cooldown
            }
        }
        sqlite3_finalize(stmt);
    }

    // Check player skill
    int playerSkill = gameDB_->GetSkillLevel(playerId, skillId);
    if (playerSkill < skillLevel)
        return false;

    // Yield items (higher skill = guaranteed max)
    int yield = (playerSkill >= skillLevel + 3) ? yieldMax
        : (Random(1.0f) < 0.4f ? yieldMax : yieldMin);
    AddItemToWorldInventory(playerId, yieldItemId, yield);

    // Record cooldown
    stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO tree_harvests (tree_id, property, last_time) VALUES (?, ?, ?)",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)treeId);
        sqlite3_bind_text(stmt, 2, property.CString(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    gameDB_->AwardXP(playerId, "gather_herb");
    return true;
#else
    return false;
#endif
}

unsigned AuthServer::FindHarvestableTree(const ServerCreatureAI& ai, const String& property)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen())
        return 0;

    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    unsigned bestId = 0;
    float bestDist = 60.0f;

    // Collect species that have this property
    Vector<int> knownSpecies;
    stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT species FROM species_properties WHERE property = ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, property.CString(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW)
            knownSpecies.Push(sqlite3_column_int(stmt, 0));
        sqlite3_finalize(stmt);
    }
    if (knownSpecies.Empty()) return 0;

    // Check settlement knowledge — do they know which species?
    bool hasKnowledge = false;
    for (unsigned k = 0; k < knownSpecies.Size(); ++k)
    {
        if (SettlementKnowsProperty(ai.campfireId, knownSpecies[k], property))
        { hasKnowledge = true; break; }
    }

    // Build species filter for SQL
    String speciesFilter;
    if (hasKnowledge)
    {
        speciesFilter = " AND species IN (";
        for (unsigned k = 0; k < knownSpecies.Size(); ++k)
        {
            if (k > 0) speciesFilter += ",";
            speciesFilter += String(knownSpecies[k]);
        }
        speciesFilter += ")";
    }

    String sql = "SELECT tree_id, pos_x, pos_z, species FROM trees WHERE hp > 0 AND growth_stage >= 3" + speciesFilter;

    double now = GetSubsystem<Time>()->GetElapsedTime();
    stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.CString(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            unsigned tId = (unsigned)sqlite3_column_int(stmt, 0);
            float tx = (float)sqlite3_column_double(stmt, 1);
            float tz = (float)sqlite3_column_double(stmt, 2);

            // Check per-tree cooldown via tree_harvests
            sqlite3_stmt* cdStmt = nullptr;
            bool onCooldown = false;
            if (sqlite3_prepare_v2(db,
                "SELECT last_time FROM tree_harvests WHERE tree_id = ? AND property = ?",
                -1, &cdStmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(cdStmt, 1, (int)tId);
                sqlite3_bind_text(cdStmt, 2, property.CString(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(cdStmt) == SQLITE_ROW)
                {
                    double lastTime = sqlite3_column_double(cdStmt, 0);
                    if ((now - lastTime) < RESIN_TAP_COOLDOWN) onCooldown = true;
                }
                sqlite3_finalize(cdStmt);
            }
            if (onCooldown) continue;

            float dist = (Vector3(tx, 0, tz) - Vector3(ai.position.x_, 0, ai.position.z_)).Length();
            if (dist < bestDist)
            {
                bestDist = dist;
                bestId = tId;
            }
        }
        sqlite3_finalize(stmt);
    }
    return bestId;
#else
    return 0;
#endif
}

unsigned AuthServer::FindResinTree(const ServerCreatureAI& ai)
{
    return FindHarvestableTree(ai, "resin");
}

bool AuthServer::NPCTapTree(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !worldDB_->IsOpen() || !gameDB_)
        return false;

    String prop = ai.harvestProperty.Empty() ? "resin" : ai.harvestProperty;
    unsigned treeId = FindHarvestableTree(ai, prop);
    if (treeId == 0) return false;

    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0) return false;

    bool success = HarvestTreeProperty(npcPid, treeId, prop);

    if (!success)
    {
        NPCAwardXP(ai, "gather_herb");
        return false;
    }

    // Check for botanical discovery
    sqlite3* db = worldDB_->GetHandle();
    sqlite3_stmt* stmt = nullptr;
    int species = -1;
    if (sqlite3_prepare_v2(db, "SELECT species FROM trees WHERE tree_id = ?",
                           -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)treeId);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            species = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (species >= 0 && !SettlementKnowsProperty(ai.campfireId, species, prop))
        RecordBotanicalDiscovery(ai.campfireId, species, prop, ai);

    return true;
#else
    return false;
#endif
}

bool AuthServer::NPCProspect(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!depositMap_ || !scene_ || !worldDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0) return false;

    int targetType = ai.targetDepositType;
    if (targetType == 0) return false;

    auto* terrain = scene_->GetComponent<Terrain>(true);
    if (!terrain) return false;

    // Scan deposit map for target trace element near NPC (similar to NPCMine but type-specific)
    IntVector2 center = terrain->WorldToHeightMap(ai.position);
    int searchRadius = 20;
    float bestDistSq = 999999.0f;
    int bestPx = -1, bestPz = -1;

    for (int dz = -searchRadius; dz <= searchRadius; ++dz)
    {
        for (int dx = -searchRadius; dx <= searchRadius; ++dx)
        {
            int px = center.x_ + dx, pz = center.y_ + dz;
            if (px < 0 || px >= depositMapSize_ || pz < 0 || pz >= depositMapSize_) continue;

            Color c = depositMap_->GetPixel(px, pz);
            int type = (int)(c.g_ * 255.0f + 0.5f);
            int qty = (int)(c.r_ * 255.0f + 0.5f);
            if (type != targetType || qty == 0) continue;

            float distSq = (float)(dx * dx + dz * dz);
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestPx = px;
                bestPz = pz;
            }
        }
    }

    if (bestPx < 0)
    {
        URHO3D_LOGDEBUGF("[Prospecting] NPC %u found no deposit type %d nearby", ai.spawnId, targetType);
        return false;
    }

    Vector3 minePos = terrain->HeightMapToWorld(IntVector2(bestPx, bestPz));
    String ownerName = "NPC_" + String(ai.spawnId);

    // Lower yield for prospecting (searching is harder than stumbling)
    int mined = MineForOwner(npcPlayerId, minePos.x_, minePos.z_, ownerName);
    if (mined > 0)
    {
        NPCAwardXP(ai, "prospect");
        URHO3D_LOGINFOF("[Prospecting] NPC %u prospected %d units of element %d",
            ai.spawnId, mined, targetType);
        return true;
    }
    return false;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Iron Age Phase 34: Innovation System (The Philosopher Mechanic)
// ---------------------------------------------------------------------------

bool AuthServer::HasInnovation(unsigned campfireId, int innovationId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return false;
    sqlite3* db = worldDB_->GetHandle();
    if (!db) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT 1 FROM settlement_innovations WHERE campfire_id=? AND innovation_id=?",
        -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, campfireId);
    sqlite3_bind_int(stmt, 2, innovationId);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Profession Schedule System
// ---------------------------------------------------------------------------

int AuthServer::TaskStringToSTASK(const String& s) const
{
    if (s == "sleep")        return STASK_SLEEP;
    if (s == "rest" || s == "idle" || s == "wander")
                             return STASK_SIT_FIRE;
    if (s == "gather" || s == "forage")
                             return STASK_GATHER;
    if (s == "hunt" || s == "track")
                             return STASK_HUNT;
    if (s == "craft")        return STASK_CRAFT;
    if (s == "build")        return STASK_BUILD;
    if (s == "repair")       return STASK_REPAIR;
    if (s == "cook" || s == "butcher")
                             return STASK_COOK;
    if (s == "fish")         return STASK_FISH;
    if (s == "gather_herbs") return STASK_GATHER_HERBS;
    if (s == "heal")         return STASK_HEAL;
    if (s == "farm")         return STASK_FARM_HARVEST;
    return -1;
}

void AuthServer::LoadProfessionSchedules()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_) return;
    sqlite3* db = gameDB_->GetHandle();
    if (!db) return;

    // Load profession → primary_skill mapping
    professionSkills_.Clear();
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT id, primary_skill FROM professions", -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                ProfessionSkillMap m;
                m.professionId = sqlite3_column_int(stmt, 0);
                m.skillId = sqlite3_column_int(stmt, 1);
                professionSkills_.Push(m);
            }
        }
        sqlite3_finalize(stmt);
    }

    // Load schedules
    professionSchedule_.Clear();
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db,
            "SELECT profession_id, hour, task_type FROM profession_schedule ORDER BY profession_id, hour",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                ScheduleEntry e;
                e.professionId = sqlite3_column_int(stmt, 0);
                e.hour = sqlite3_column_int(stmt, 1);
                e.stask = TaskStringToSTASK(String((const char*)sqlite3_column_text(stmt, 2)));
                professionSchedule_.Push(e);
            }
        }
        sqlite3_finalize(stmt);
    }

    URHO3D_LOGINFOF("[Professions] Loaded %u schedule entries for %u professions",
        professionSchedule_.Size(), professionSkills_.Size());
#endif
}

int AuthServer::GetScheduledTask(int professionId, int hour) const
{
    for (unsigned i = 0; i < professionSchedule_.Size(); ++i)
    {
        if (professionSchedule_[i].professionId == professionId &&
            professionSchedule_[i].hour == hour)
            return professionSchedule_[i].stask;
    }
    return -1;  // no schedule entry
}

int AuthServer::DeriveProfession(unsigned spawnId)
{
    if (!gameDB_ || professionSkills_.Empty()) return 0;

    int npcPid = GetNPCPlayerId(spawnId);
    if (npcPid <= 0) return 0;

    int bestProfId = 0;
    int bestLevel = 2;  // minimum level 3 to specialize (> 2)

    for (unsigned i = 0; i < professionSkills_.Size(); ++i)
    {
        int lvl = gameDB_->GetSkillLevel(npcPid, professionSkills_[i].skillId);
        if (lvl > bestLevel)
        {
            bestLevel = lvl;
            bestProfId = professionSkills_[i].professionId;
        }
    }
    return bestProfId;
}

void AuthServer::CheckInnovations()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_) return;

    // Innovation skill pairs: {skillA, skillB, innovationType}
    struct InnovPair { int skillA; int skillB; int type; };
    static const InnovPair pairs[] = {
        { SKILL_FARMING,    SKILL_HERBALISM,  INNOV_CROP_ROTATION },
        { SKILL_SMELTING,   SKILL_WOODWORK,   INNOV_CHARCOAL_EFF },
        { SKILL_HERBALISM,  SKILL_COOKING,    INNOV_MEDICINAL_FOOD },
        { SKILL_KNAPPING,   SKILL_SMELTING,   INNOV_ALLOY_KNOWLEDGE },
        { SKILL_FISHING,    SKILL_WOODWORK,   INNOV_BETTER_NETS },
        { SKILL_ANIMAL_LORE,SKILL_FARMING,    INNOV_SELECTIVE_FEED },
        { SKILL_MELEE,      SKILL_KNAPPING,   INNOV_WEAPON_HARDENING },
        { SKILL_WOODWORK,   SKILL_FARMING,    INNOV_IRRIGATION_INSIGHT },
        { SKILL_TRADE,      0,                INNOV_EFFICIENT_TRADE },  // Trade + any craft
    };
    static const int numPairs = 9;

    static const char* innovNames[] = {
        "Crop Rotation Insight", "Charcoal Efficiency", "Medicinal Food",
        "Alloy Knowledge", "Better Nets", "Selective Feed",
        "Weapon Hardening", "Irrigation Insight", "Efficient Trade"
    };

    // Per campfire: find highest-diversity NPC, roll for discovery
    HashSet<unsigned> checkedCampfires;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (!it->second_.isHuman || checkedCampfires.Contains(it->second_.campfireId))
            continue;
        unsigned cfId = it->second_.campfireId;
        if (cfId == 0) continue;
        checkedCampfires.Insert(cfId);

        // Find highest-diversity NPC at this campfire
        unsigned bestNpcId = 0;
        int bestDiversity = 0;
        for (auto nIt = creatureAI_.Begin(); nIt != creatureAI_.End(); ++nIt)
        {
            if (!nIt->second_.isHuman || nIt->second_.campfireId != cfId) continue;
            int npcPid = GetNPCPlayerId(nIt->first_);
            if (npcPid <= 0) continue;

            int diversity = 0;
            static const int allSkills[] = {1,2,3,4,10,11,12,13,14,15,16,20,21,22,23,24,25,26,27,28,30,31,32,33};
            for (int sk : allSkills)
                if (gameDB_->GetSkillLevel(npcPid, sk) >= 3) ++diversity;

            if (diversity > bestDiversity)
            {
                bestDiversity = diversity;
                bestNpcId = nIt->first_;
            }
        }
        if (bestDiversity < 2) continue;  // need at least 2 skills at 3+

        // Discovery chance: diversity * 2%
        float chance = bestDiversity * 0.02f;
        if (Random(1.0f) >= chance) continue;

        // Pick an undiscovered innovation from the NPC's skill pairs
        int bestNpcPid = GetNPCPlayerId(bestNpcId);
        for (int p = 0; p < numPairs; ++p)
        {
            if (HasInnovation(cfId, pairs[p].type)) continue;

            int lvA = gameDB_->GetSkillLevel(bestNpcPid, pairs[p].skillA);
            if (lvA < 3) continue;

            bool hasB = false;
            if (pairs[p].skillB == 0)
            {
                // "Trade + any craft" — check any craft skill at 3+
                static const int craftSkills[] = {10,11,12,13,14,15,16};
                for (int cs : craftSkills)
                    if (gameDB_->GetSkillLevel(bestNpcPid, cs) >= 3) { hasB = true; break; }
            }
            else
            {
                hasB = (gameDB_->GetSkillLevel(bestNpcPid, pairs[p].skillB) >= 3);
            }
            if (!hasB) continue;

            // Discovery!
            auto npcIt = creatureAI_.Find(bestNpcId);
            String name = (npcIt != creatureAI_.End() && !npcIt->second_.npcName.Empty())
                ? npcIt->second_.npcName : "Unknown";

            sqlite3* db = worldDB_->GetHandle();
            if (db)
            {
                sqlite3_stmt* ins = nullptr;
                if (sqlite3_prepare_v2(db,
                    "INSERT OR IGNORE INTO settlement_innovations (campfire_id, innovation_id, discoverer_name, game_day) "
                    "VALUES (?, ?, ?, ?)", -1, &ins, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(ins, 1, cfId);
                    sqlite3_bind_int(ins, 2, pairs[p].type);
                    sqlite3_bind_text(ins, 3, name.CString(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(ins, 4, currentGameDay_);
                    sqlite3_step(ins);
                }
                sqlite3_finalize(ins);
            }

            URHO3D_LOGINFOF("[Innovation] Settlement %u: %s discovered '%s' (diversity=%d, day %d)",
                cfId, name.CString(), innovNames[pairs[p].type], bestDiversity, currentGameDay_);

            // Broadcast to all authenticated clients
            {
                VectorBuffer buf;
                buf.WriteU32(cfId);
                buf.WriteI32(pairs[p].type);
                buf.WriteString(name);
                buf.WriteString(innovNames[pairs[p].type]);
                for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
                {
                    if (sIt->second_.authenticated)
                        sIt->first_->SendMessage(MSG_INNOVATION_DISCOVERED, true, true, buf);
                }
            }

            RecordSettlementFirst(cfId, String("innovation_") + innovNames[pairs[p].type], bestNpcId);
            break;  // one discovery per settlement per day
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 28: Walls + Fortification
// ---------------------------------------------------------------------------

bool AuthServer::IsSettlementWalled(unsigned campfireId, int predatorSpecies)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return false;
    auto cfIt = serverCampfires_.Find(campfireId);
    if (cfIt == serverCampfires_.End()) return false;

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned b = 0; b < buildings.Size(); ++b)
    {
        auto typeIt = cachedBuildingTypes_.Find(buildings[b].buildingId);
        if (typeIt == cachedBuildingTypes_.End()) continue;
        if (typeIt->second_.category != "wall" && typeIt->second_.category != "gate") continue;
        Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
        if ((bPos - cfIt->second_.position).Length() > 25.0f) continue;

        int wallId = buildings[b].buildingId;
        if (wallId == 43) return true;  // Iron Gate blocks all species
        if (wallId >= 40) return true;
        if (wallId >= 20 && predatorSpecies != 6) return true;
        if (wallId >= 10 && (predatorSpecies == 1 || predatorSpecies == 3 ||
            predatorSpecies == 11 || predatorSpecies == 12 || predatorSpecies == 13))
            return true;
    }
#endif
    return false;
}

int AuthServer::FindDefenseBuildingToBuild(const ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return -1;
    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0) return -1;

    // Phase 33: Iron Gate and Stone Watchtower added to defense priority
    static const int defensePriority[] = { 43, 40, 33, 20, 10, 32 };
    for (int buildId : defensePriority)
    {
        // No skill gates — NPCs attempt any defense building. Skill check on completion.

        Vector<BuildingRecipeInput> recipe = gameDB_->GetBuildingRecipe(buildId);
        bool canAfford = !recipe.Empty();
        for (unsigned r = 0; r < recipe.Size() && canAfford; ++r)
            if (worldDB_->GetItemCount(npcPid, recipe[r].itemId) < recipe[r].quantity)
                canAfford = false;
        if (canAfford)
            return buildId;
    }
#endif
    return -1;
}

float AuthServer::GetWorkshopProximityBonus(const ServerCreatureAI& ai, int skillId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return 1.0f;

    // Map skills to their specialized workshop building IDs
    // Smithy (82): Smelting, Smithing  |  Tannery (83): Leatherwork, Weaving
    // Granary (84): Farming, Cooking   |  Herbalist Hut (85): Herbalism
    // Forge (81): Smelting, Smithing   |  Windmill (89): Cooking, Farming
    int workshopId = 0;
    switch (skillId)
    {
    case SKILL_SMELTING: case SKILL_SMITHING:
        workshopId = 82;  // Smithy (or Forge 81)
        break;
    case SKILL_LEATHERWORK: case SKILL_WEAVING:
        workshopId = 83;  // Tannery
        break;
    case SKILL_FARMING: case SKILL_COOKING:
        workshopId = 84;  // Granary
        break;
    case SKILL_HERBALISM:
        workshopId = 85;  // Herbalist Hut
        break;
    default:
        return 1.0f;  // no workshop bonus for this skill
    }

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned b = 0; b < buildings.Size(); ++b)
    {
        if (buildings[b].buildingId != workshopId && !(workshopId == 82 && buildings[b].buildingId == 81))
            continue;
        Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
        if ((bPos - ai.position).Length() <= CRAFTING_STATION_RANGE * 2.0f)
            return 1.2f;  // +20% bonus when near specialized workshop
    }
#endif
    return 1.0f;
}

void AuthServer::DamageNearestWall(const Vector3& pos, unsigned campfireId, int damage)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return;

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    float bestDist = 15.0f;
    int bestPlacedId = -1;

    for (unsigned b = 0; b < buildings.Size(); ++b)
    {
        auto typeIt = cachedBuildingTypes_.Find(buildings[b].buildingId);
        if (typeIt == cachedBuildingTypes_.End()) continue;
        if (typeIt->second_.category != "wall" && typeIt->second_.category != "gate") continue;
        Vector3 bPos(buildings[b].posX, buildings[b].posY, buildings[b].posZ);
        float d = (bPos - pos).Length();
        if (d < bestDist) { bestDist = d; bestPlacedId = buildings[b].id; }
    }

    if (bestPlacedId >= 0)
    {
        PlacedBuildingDBInfo info;
        if (worldDB_->GetPlacedBuilding(bestPlacedId, info))
        {
            int newHp = Max(0, info.hp - damage);
            worldDB_->UpdateBuildingHP(bestPlacedId, newHp);
            if (newHp <= 0)
            {
                worldDB_->RemovePlacedBuilding(bestPlacedId);
                URHO3D_LOGINFOF("[Fortification] Wall %d destroyed by predator attack", bestPlacedId);
            }
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 27: Medicine + Herbalism
// ---------------------------------------------------------------------------

bool AuthServer::NPCGatherHerbs(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return false;
    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0) return false;
    if (ai.position.y_ <= AI_WATER_LEVEL) return false;

    int herbSkill = GetNPCSkillLevel(ai.spawnId, SKILL_HERBALISM);
    int qty = 1;
    if (herbSkill >= 8) qty = 3;       // Master Herbalist: 3x
    else if (herbSkill >= 5) qty = 2;  // Skilled: 2x
    AddItemToWorldInventory(npcPid, ITEM_MEDICINAL_HERBS, qty);
    NPCAwardXP(ai, "gather_herb");
    URHO3D_LOGINFOF("[Herbalism] NPC %u gathered %d medicinal herbs (skill %d)", ai.spawnId, qty, herbSkill);
    return true;
#else
    return false;
#endif
}

unsigned AuthServer::FindInjuredNPC(const ServerCreatureAI& ai)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->first_ == ai.spawnId || !it->second_.isHuman) continue;
        if (it->second_.campfireId != ai.campfireId) continue;
        auto csIt = creatureStates_.Find(it->first_);
        if (csIt == creatureStates_.End()) continue;
        float hpFrac = (float)csIt->second_.hp / Max(1.0f, (float)csIt->second_.maxHp);
        if (hpFrac < 0.7f && csIt->second_.hp > 0)
            return it->first_;
    }
    return 0;
}

bool AuthServer::NPCHeal(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return false;
    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0) return false;
    if (worldDB_->GetItemCount(npcPid, ITEM_MEDICINAL_HERBS) < 1) return false;

    unsigned patientId = ai.targetId;
    if (patientId == 0) return false;
    auto csIt = creatureStates_.Find(patientId);
    if (csIt == creatureStates_.End()) return false;

    csIt->second_.hp = Min(csIt->second_.hp + 20, csIt->second_.maxHp);
    worldDB_->RemoveItemFromInventory(npcPid, ITEM_MEDICINAL_HERBS, 1);
    NPCAwardXP(ai, "use_herb");

    // Master Herbalist: instant poison cure
    auto patientAI = creatureAI_.Find(patientId);
    if (patientAI != creatureAI_.End() && patientAI->second_.illnessActive && ai.isMasterHerbalist)
    {
        patientAI->second_.illnessTimer = 0.0f;
        patientAI->second_.illnessActive = false;
        URHO3D_LOGINFOF("[Herbalism] Master NPC %u cured NPC %u's food poisoning", ai.spawnId, patientId);
    }

    URHO3D_LOGINFOF("[Herbalism] NPC %u healed NPC %u (+20 HP, now %d/%d)",
        ai.spawnId, patientId, csIt->second_.hp, csIt->second_.maxHp);
    return true;
#else
    return false;
#endif
}

void AuthServer::BroadcastNPCHeal(unsigned healerSpawnId, unsigned patientSpawnId, int healAmount, bool curedPoison, const Vector3& pos)
{
    VectorBuffer buf;
    buf.WriteU32(healerSpawnId);
    buf.WriteU32(patientSpawnId);
    buf.WriteI32(healAmount);
    buf.WriteBool(curedPoison);

    for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
    {
        if (!sIt->second_.authenticated)
            continue;
        auto avIt = serverObjects_.Find(sIt->first_);
        if (avIt == serverObjects_.End() || !avIt->second_)
            continue;
        if ((avIt->second_->GetWorldPosition() - pos).Length() > 100.0f)
            continue;
        sIt->first_->SendMessage(MSG_NPC_HEAL, true, true, buf);
    }
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 22: Apprenticeship
// ---------------------------------------------------------------------------

unsigned AuthServer::NPCFindApprentice(const ServerCreatureAI& ai, int& outSkillId)
{
    // Find teacher's top skill at level 5+
    static const int allSkills[] = {
        1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 32, 33
    };
    int topSkillId = 0;
    int topSkillLevel = 0;
    for (int sk : allSkills)
    {
        int level = GetNPCSkillLevel(ai.spawnId, sk);
        if (level >= 5 && level > topSkillLevel)
        {
            topSkillLevel = level;
            topSkillId = sk;
        }
    }
    if (topSkillId == 0)
        return 0;  // no skill at 5+, can't teach

    // Find lowest-skilled NPC at same campfire with that skill < 3
    unsigned bestStudentId = 0;
    int bestStudentLevel = 3;  // must be strictly less than 3
    bool bestIsChild = false;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->first_ == ai.spawnId || !it->second_.isHuman)
            continue;
        if (it->second_.campfireId != ai.campfireId)
            continue;
        // Student must not be busy with survival tasks
        if (it->second_.currentTask != STASK_IDLE && it->second_.currentTask != STASK_WANDER &&
            it->second_.currentTask != STASK_SIT_FIRE)
            continue;
        // Only 1 teacher per student — check no other teacher is targeting this NPC
        bool alreadyBeingTaught = false;
        for (auto t = creatureAI_.Begin(); t != creatureAI_.End(); ++t)
        {
            if (t->second_.currentTask == STASK_TEACH && t->second_.targetId == it->first_)
            { alreadyBeingTaught = true; break; }
        }
        if (alreadyBeingTaught) continue;

        int studentLevel = GetNPCSkillLevel(it->first_, topSkillId);
        if (studentLevel >= 3)
            continue;

        // Prefer children of the teacher (family apprenticeship)
        bool isChild = (it->second_.parentA == ai.spawnId || it->second_.parentB == ai.spawnId);
        if (isChild && !bestIsChild)
        {
            bestStudentId = it->first_;
            bestStudentLevel = studentLevel;
            bestIsChild = true;
        }
        else if (isChild == bestIsChild && studentLevel < bestStudentLevel)
        {
            bestStudentId = it->first_;
            bestStudentLevel = studentLevel;
        }
    }

    outSkillId = topSkillId;
    return bestStudentId;
}

bool AuthServer::NPCTeach(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_) return false;

    unsigned studentId = ai.targetId;
    if (studentId == 0)
        return false;

    // Re-find the teacher's top skill
    static const int allSkills[] = {
        1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16,
        20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 32, 33
    };
    int teachSkill = 0;
    int topSkillLevel = 0;
    for (int sk : allSkills)
    {
        int level = GetNPCSkillLevel(ai.spawnId, sk);
        if (level >= 5 && level > topSkillLevel)
        {
            topSkillLevel = level;
            teachSkill = sk;
        }
    }
    if (teachSkill == 0) return false;

    // CHA mod scales teaching effectiveness: charismatic teachers transfer more knowledge.
    // Base 6 XP (2x normal), +2 per CHA mod point. Floor of 2 XP even for terrible teachers.
    int baseTeachXP = 6 + ai.stats.ChaMod() * 2;
    if (baseTeachXP < 2) baseTeachXP = 2;

    int studentPid = GetNPCPlayerId(studentId);
    if (studentPid > 0)
        gameDB_->AddXPDirect(studentPid, teachSkill, baseTeachXP);

    // Award Trade XP to teacher (knowledge exchange)
    NPCAwardXP(ai, "complete_trade");

    // Record apprentice bond if not already present
    if (worldDB_)
    {
        String existingBond = worldDB_->GetBondType(ai.spawnId, studentId);
        if (existingBond != "apprentice" && existingBond != "mate" && existingBond != "parent")
            worldDB_->SetBondType(ai.spawnId, studentId, "apprentice");
    }

    URHO3D_LOGINFOF("[Apprentice] NPC %u taught skill %d to NPC %u (student pid=%d, +6 XP)",
        ai.spawnId, teachSkill, studentId, studentPid);
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 16: Animal Domestication
// ---------------------------------------------------------------------------

int AuthServer::GetTameDC(int creatureId) const
{
    switch (creatureId)
    {
    case 7:  return 12;  // Cow
    case 9:  return 14;  // Donkey
    case 10: return 16;  // Horse
    case 11: return 18;  // Alpaca
    case 12: return 14;  // Husky (dog)
    case 13: return 14;  // ShibaInu (dog)
    default: return 0;   // not tameable
    }
}

unsigned AuthServer::FindNearestTameableAnimal(const ServerCreatureAI& ai, float range)
{
    float bestDistSq = range * range;
    unsigned bestId = 0;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& candidate = it->second_;
        if (candidate.isHuman || candidate.isPredator)
            continue;
        if (candidate.tamerId != 0)
            continue;  // already tamed
        if (candidate.tameCooldown > 0.0f)
            continue;  // on cooldown from failed attempt
        if (GetTameDC(candidate.creatureId) == 0)
            continue;  // not tameable

        float dx = ai.position.x_ - candidate.position.x_;
        float dz = ai.position.z_ - candidate.position.z_;
        float distSq = dx * dx + dz * dz;
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = it->first_;
        }
    }
    return bestId;
}

bool AuthServer::NPCTame(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_ || !combatResolver_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    // Need food to offer (Berries = item 6)
    if (worldDB_->GetItemCount(npcPlayerId, 6) <= 0)
        return false;

    // Find target animal
    unsigned targetId = FindNearestTameableAnimal(ai, 20.0f);
    if (targetId == 0)
        return false;

    auto targetIt = creatureAI_.Find(targetId);
    if (targetIt == creatureAI_.End())
        return false;

    ServerCreatureAI& animal = targetIt->second_;
    int dc = GetTameDC(animal.creatureId);
    if (dc == 0)
        return false;

    // Consume food offering regardless of outcome
    worldDB_->RemoveItemFromInventory(npcPlayerId, 6, 1);

    // Roll: d20 + Animal Lore skill vs DC
    int skill = GetNPCSkillLevel(ai.spawnId, SKILL_ANIMAL_LORE);
    int roll = combatResolver_->RollD20();
    bool success = (roll == 20) || (roll != 1 && roll + skill >= dc);

    NPCAwardXP(ai, "observe_animal");

    if (!success)
    {
        // Animal flees, 1 game-day cooldown (~600 real seconds at default time scale)
        animal.tameCooldown = 600.0f;
        animal.state = 3;  // CREATURE_FLEE
        animal.moveSpeed = 5.0f;
        animal.taskTimer = 4.0f;
        Vector3 away = animal.position - ai.position;
        away.y_ = 0.0f;
        if (away.LengthSquared() > 0.01f)
        {
            away.Normalize();
            animal.targetPosition = animal.position + away * 25.0f;
            animal.targetPosition.y_ = GetTerrainHeightAI(
                animal.targetPosition.x_, animal.targetPosition.z_);
        }
        URHO3D_LOGINFOF("[Tame] NPC %u failed to tame creature %u (roll %d+%d vs DC %d)",
            ai.spawnId, targetId, roll, skill, dc);
        return false;
    }

    // Success — animal is tamed
    animal.tamerId = ai.spawnId;
    animal.homePosition = ai.homePosition;  // follow tamer's campfire
    animal.wanderRadius = 15.0f;            // stay close to settlement

    // Tamed dogs join defense
    if (animal.creatureId == 12 || animal.creatureId == 13)
        animal.isPredator = false;  // no longer flees humans

    // Tamed cows start milk production timer (traitYield shortens interval)
    if (animal.creatureId == 7)
        animal.milkTimer = 600.0f / animal.traitYield;

    URHO3D_LOGINFOF("[Tame] NPC %u tamed creature %u (species %d, roll %d+%d vs DC %d)",
        ai.spawnId, targetId, animal.creatureId, roll, skill, dc);
    return true;
#else
    return false;
#endif
}

void AuthServer::TickTamedAnimals(float dt)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;

        // Decrement tame cooldown for all animals
        if (ai.tameCooldown > 0.0f)
            ai.tameCooldown = Max(0.0f, ai.tameCooldown - dt);

        // Tick shear cooldown for alpacas
        if (ai.shearCooldown > 0.0f)
            ai.shearCooldown = Max(0.0f, ai.shearCooldown - dt);

        // Tamed cows produce milk
        if (ai.tamerId != 0 && ai.creatureId == 7)
        {
            ai.milkTimer -= dt;
            if (ai.milkTimer <= 0.0f)
            {
                ai.milkTimer = 600.0f / ai.traitYield;  // higher yield = faster production
                // Add milk to tamer's inventory (yield-scaled quantity)
                auto tamerIt = creatureAI_.Find(ai.tamerId);
                if (tamerIt != creatureAI_.End())
                {
                    int tamerPid = GetNPCPlayerId(tamerIt->second_.spawnId);
                    if (tamerPid > 0)
                    {
                        int milkQty = Max(1, (int)(ai.traitYield + 0.5f));
                        AddItemToWorldInventory(tamerPid, ITEM_MILK, milkQty);
                        URHO3D_LOGINFOF("[Tame] Tamed cow %u produced milk for NPC %u",
                            it->first_, ai.tamerId);
                    }
                }
            }
        }

        // ── Pack donkeys: tight follow, trade capacity bonus (passive) ──
        if (ai.tamerId != 0 && ai.creatureId == 9)
        {
            // Donkeys stay close to settlement — tighter wander radius
            ai.wanderRadius = 10.0f;
            // Donkeys follow their tamer's position more closely
            auto tamerIt = creatureAI_.Find(ai.tamerId);
            if (tamerIt != creatureAI_.End())
            {
                float distToTamer = (ai.position - tamerIt->second_.position).Length();
                if (distToTamer > 15.0f && ai.currentTask != STASK_DEFEND)
                {
                    ai.targetPosition = tamerIt->second_.position;
                    ai.state = 1;  // walk toward tamer
                    ai.moveSpeed = 3.0f;
                }
            }
        }

        // ── Tamed horses: speed boost for settlement NPCs on long tasks ──
        if (ai.tamerId != 0 && ai.creatureId == 10)
        {
            ai.wanderRadius = 12.0f;
        }

        // ── Guard Dogs: enhanced defense, alert, and patrol ──
        if (ai.tamerId != 0 && (ai.creatureId == 12 || ai.creatureId == 13))
        {
            auto tamerIt = creatureAI_.Find(ai.tamerId);
            if (tamerIt != creatureAI_.End())
            {
                // Extended detection range for guard dogs:
                // Husky: 25m base (pack hunter senses), ShibaInu: 20m (alert, quick)
                float dogDetectRange = (ai.creatureId == 12) ? 25.0f : 20.0f;

                // Night bonus: dogs detect by scent/hearing — no night penalty
                // Tamer's Animal Lore improves dog effectiveness (+1m per level)
                int tamerLore = GetNPCSkillLevel(tamerIt->second_.spawnId, SKILL_ANIMAL_LORE);
                dogDetectRange += tamerLore * 1.0f;

                // Pack bonus: each additional tamed dog at same settlement adds +2m detect
                int dogCount = 0;
                for (auto dIt = creatureAI_.Begin(); dIt != creatureAI_.End(); ++dIt)
                {
                    if (dIt->first_ != it->first_ && dIt->second_.tamerId != 0 &&
                        (dIt->second_.creatureId == 12 || dIt->second_.creatureId == 13) &&
                        dIt->second_.campfireId == ai.campfireId)
                        ++dogCount;
                }
                dogDetectRange += dogCount * 2.0f;

                // Scan for predators within dog's extended detection range
                float detectSq = dogDetectRange * dogDetectRange;
                unsigned threatId = 0;
                float bestDistSq = detectSq;
                for (auto pIt = creatureAI_.Begin(); pIt != creatureAI_.End(); ++pIt)
                {
                    if (!IsPredatorSpecies(pIt->second_.creatureId))
                        continue;
                    if (pIt->second_.state == 5 || pIt->second_.state == 13)
                        continue;
                    if (pIt->second_.tamerId != 0)
                        continue;  // don't attack other tamed animals
                    Vector3 diff = pIt->second_.position - ai.homePosition;
                    diff.y_ = 0.0f;
                    float dSq = diff.LengthSquared();
                    if (dSq < bestDistSq)
                    {
                        bestDistSq = dSq;
                        threatId = pIt->first_;
                    }
                }

                if (threatId != 0 && ai.currentTask != STASK_DEFEND)
                {
                    // Alert bark: notify settlement of threat
                    auto cfIt = serverCampfires_.Find(ai.campfireId);
                    if (cfIt != serverCampfires_.End())
                        cfIt->second_.predatorAttackCount++;

                    ai.targetId = threatId;
                    ai.currentTask = STASK_DEFEND;
                    ai.state = 4;  // CREATURE_FIGHT
                    // Breed speed: Husky charges harder, ShibaInu is quicker to react
                    ai.moveSpeed = (ai.creatureId == 12) ? 5.5f : 6.0f;
                    ai.taskTimer = 15.0f;
                    auto threatAi = creatureAI_.Find(threatId);
                    if (threatAi != creatureAI_.End())
                        ai.targetPosition = threatAi->second_.position;

                    URHO3D_LOGINFOF("[GuardDog] %s (spawnId %u) detected predator %u at %.0fm, alerting settlement %u",
                        (ai.creatureId == 12) ? "Husky" : "ShibaInu", it->first_, threatId,
                        Sqrt(bestDistSq), ai.campfireId);
                }
                else if (ai.currentTask != STASK_DEFEND)
                {
                    // ShibaInu: companion pet — follows tamer NPC closely
                    if (ai.creatureId == 13)
                    {
                        float distToTamer = (ai.position - tamerIt->second_.position).Length();
                        if (distToTamer > 8.0f)
                        {
                            // Run to catch up with tamer
                            ai.targetPosition = tamerIt->second_.position;
                            ai.state = 1;  // CREATURE_WANDER
                            ai.moveSpeed = (distToTamer > 15.0f) ? 5.0f : 3.0f;
                        }
                        else if (distToTamer < 3.0f && ai.state == 1)
                        {
                            // Near tamer — idle beside them
                            ai.state = 0;  // CREATURE_IDLE
                        }
                        // Companion morale: tamer gets +3 morale when ShibaInu within 10m
                        // (applied in morale calculation section)
                    }
                    // Husky: settlement guard — night patrol
                    else if (IsNightTime())
                    {
                        if (ai.currentTask != STASK_PATROL)
                        {
                            ai.currentTask = STASK_PATROL;
                            ai.state = 1;
                            ai.moveSpeed = 2.5f;
                            ai.taskTimer = 60.0f;
                            Vector3 diff = ai.position - ai.homePosition;
                            ai.patrolAngle = Atan2(diff.z_, diff.x_);
                            ai.targetPosition.x_ = ai.homePosition.x_ + Cos(ai.patrolAngle) * PATROL_RADIUS;
                            ai.targetPosition.z_ = ai.homePosition.z_ + Sin(ai.patrolAngle) * PATROL_RADIUS;
                            ai.targetPosition.y_ = GetTerrainHeightAI(ai.targetPosition.x_, ai.targetPosition.z_);
                        }
                    }
                }
            }
        }
    }

    // ── Herd cohesion: tamed livestock drift toward herd center ──
    // Compute per-settlement herd centers, then nudge wandering animals toward them
    HashMap<unsigned long long, Vector3> herdCenters;   // (campfireId<<32 | creatureId) → avg pos
    HashMap<unsigned long long, int> herdCounts;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;
        if (ai.tamerId == 0 || ai.creatureId == 12 || ai.creatureId == 13)
            continue;  // skip wild + dogs (dogs have their own patrol)
        unsigned long long key = ((unsigned long long)ai.campfireId << 32) | (unsigned)ai.creatureId;
        herdCenters[key] += ai.position;
        herdCounts[key]++;
    }
    for (auto cIt = herdCenters.Begin(); cIt != herdCenters.End(); ++cIt)
    {
        int cnt = herdCounts[cIt->first_];
        if (cnt > 0)
            cIt->second_ /= (float)cnt;
    }

    // Nudge idle livestock toward herd center (gentle cohesion)
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;
        if (ai.tamerId == 0 || ai.creatureId == 12 || ai.creatureId == 13)
            continue;
        if (ai.currentTask == STASK_DEFEND || ai.state == 12 || ai.state == 5)
            continue;  // don't interfere with combat or trapped animals

        unsigned long long key = ((unsigned long long)ai.campfireId << 32) | (unsigned)ai.creatureId;
        auto centerIt = herdCenters.Find(key);
        if (centerIt == herdCenters.End())
            continue;

        int cnt = herdCounts[key];
        if (cnt < 2)
            continue;  // need at least 2 to herd

        Vector3 toCenter = centerIt->second_ - ai.position;
        toCenter.y_ = 0.0f;
        float distToCenter = toCenter.Length();

        // If animal is more than 10m from herd center and idle/wandering, nudge toward center
        if (distToCenter > 10.0f && (ai.state == 0 || ai.state == 1))
        {
            Vector3 nudge = ai.homePosition + (toCenter.Normalized() * Min(distToCenter * 0.5f, 8.0f));
            nudge.y_ = GetTerrainHeightAI(nudge.x_, nudge.z_);
            ai.targetPosition = nudge;
        }
    }
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 4: Farm + Fish helpers
// ---------------------------------------------------------------------------

bool AuthServer::NPCPlantCrop(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_)
        return false;

    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    int season = GetCurrentSeasonIndex();

    Vector<InventorySlot> inv = worldDB_->GetPlayerInventory(npcPlayerId);
    int seedItemId = 0;
    const CropTypeInfo* cropType = nullptr;
    int fallbackSeedId = 0;
    const CropTypeInfo* fallbackType = nullptr;

    // Prefer a seed different from the last crop at the home tile (crop rotation)
    int homeTileX = (int)(ai.homePosition.x_ * 0.5f);
    int homeTileZ = (int)(ai.homePosition.z_ * 0.5f);
    WorldDB::CropHistoryEntry tileHistory;
    bool hasTileHistory = worldDB_->GetCropHistory(homeTileX, homeTileZ, tileHistory);

    for (unsigned i = 0; i < inv.Size(); ++i)
    {
        if (inv[i].slotType != "bag" || inv[i].itemId <= 0)
            continue;
        auto typeIt = cachedCropTypes_.Find(inv[i].itemId);
        if (typeIt == cachedCropTypes_.End())
            continue;
        if (!IsSeasonMatch(typeIt->second_.plantSeason, season))
            continue;
        // First valid seed is fallback
        if (fallbackSeedId == 0)
        {
            fallbackSeedId = inv[i].itemId;
            fallbackType = &typeIt->second_;
        }
        // Prefer different crop than last harvest at this tile
        if (!hasTileHistory || tileHistory.lastSeedId != inv[i].itemId)
        {
            seedItemId = inv[i].itemId;
            cropType = &typeIt->second_;
            break;
        }
    }
    // Fall back to same crop if no alternative available
    if (seedItemId == 0 && fallbackSeedId != 0)
    {
        seedItemId = fallbackSeedId;
        cropType = fallbackType;
    }

    if (seedItemId == 0 || !cropType)
        return false;

    Vector3 plantPos = ai.homePosition;
    bool found = false;
    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;

    for (int attempt = 0; attempt < 20; ++attempt)
    {
        float px = ai.homePosition.x_ + Random(-15.0f, 15.0f);
        float pz = ai.homePosition.z_ + Random(-15.0f, 15.0f);
        float py = GetTerrainHeightAI(px, pz);
        if (py <= AI_WATER_LEVEL) continue;
        if (terrain && terrain->GetNormal(Vector3(px, 0.0f, pz)).y_ < cropType->minFlat) continue;
        if (!IsNearWater(px, pz, cropType->nearWaterRange)) continue;

        Vector<WorldDB::PlacedCropInfo> existing = worldDB_->GetAllPlacedCrops();
        bool tooClose = false;
        for (unsigned c = 0; c < existing.Size(); ++c)
        {
            float dx = px - existing[c].posX, dz = pz - existing[c].posZ;
            if (dx * dx + dz * dz < 4.0f) { tooClose = true; break; }
        }
        if (tooClose) continue;

        plantPos = Vector3(px, py, pz);
        found = true;
        break;
    }
    if (!found) return false;

    worldDB_->RemoveItemFromInventory(npcPlayerId, seedItemId, 1);

    float gameDay = 0.0f;
    GameTimeState timeState;
    if (worldDB_->LoadGameTime(timeState))
        gameDay = timeState.gameDay;

    int cropId = worldDB_->InsertPlacedCrop(npcPlayerId, seedItemId,
        plantPos.x_, plantPos.y_, plantPos.z_, gameDay, 0);
    if (cropId < 0) return false;

    if (gameDB_) gameDB_->AwardXP(npcPlayerId, "farm_plant");
    BroadcastCropSpawned(cropId, seedItemId, plantPos, 0);
    URHO3D_LOGINFOF("[NPCFarm] NPC spawnId=%u planted seed %d cropId=%d", ai.spawnId, seedItemId, cropId);
    return true;
#else
    return false;
#endif
}

bool AuthServer::NPCHarvestCrop(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return false;
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0) return false;
    int season = GetCurrentSeasonIndex();

    Vector<WorldDB::PlacedCropInfo> crops = worldDB_->GetAllPlacedCrops();
    for (unsigned i = 0; i < crops.Size(); ++i)
    {
        if (crops[i].ownerId != npcPlayerId || crops[i].growthStage < 3) continue;
        auto typeIt = cachedCropTypes_.Find(crops[i].seedItemId);
        if (typeIt == cachedCropTypes_.End()) continue;
        const CropTypeInfo& crop = typeIt->second_;
        if (!IsSeasonMatch(crop.harvestSeason, season)) continue;

        Vector3 cropPos(crops[i].posX, crops[i].posY, crops[i].posZ);
        if ((ai.position - cropPos).Length() > 10.0f) continue;

        // Crop rotation: consecutive same-crop penalty
        int tileX = (int)(crops[i].posX * 0.5f);
        int tileZ = (int)(crops[i].posZ * 0.5f);
        WorldDB::CropHistoryEntry history;
        int yieldQty = crop.harvestQty;
        if (worldDB_->GetCropHistory(tileX, tileZ, history) &&
            history.lastSeedId == crops[i].seedItemId)
        {
            if (HasMasterFarmer(ai.campfireId))
                yieldQty = Max(1, yieldQty * 9 / 10);    // Master Farmer: only 10% penalty
            else if (HasInnovation(ai.campfireId, INNOV_CROP_ROTATION))
                yieldQty = Max(1, yieldQty * 3 / 4);     // Innovation: 25% penalty
            else
                yieldQty = Max(1, yieldQty / 2);          // Default: 50% penalty
        }
        // Master Farmer: +30% base yield
        if (HasMasterFarmer(ai.campfireId))
            yieldQty = Max(1, (int)(yieldQty * 1.3f));

        AddItemToWorldInventory(npcPlayerId, crop.harvestItemId, yieldQty);
        if (crop.seedReturn > 0)
            AddItemToWorldInventory(npcPlayerId, crops[i].seedItemId, crop.seedReturn);
        worldDB_->RecordCropHarvest(tileX, tileZ, crops[i].seedItemId, currentGameDay_);
        if (ecosystem_)
            ecosystem_->DegradeFertility(crops[i].posX, crops[i].posZ, 0.1f);
        if (gameDB_) gameDB_->AwardXP(npcPlayerId, "farm_harvest");
        worldDB_->RemovePlacedCrop(crops[i].cropId);
        BroadcastCropRemoved(crops[i].cropId);
        URHO3D_LOGINFOF("[NPCFarm] NPC spawnId=%u harvested crop %d (rotation yield: %d/%d)",
            ai.spawnId, crops[i].cropId, yieldQty, crop.harvestQty);
        return true;
    }
    return false;
#else
    return false;
#endif
}

Vector3 AuthServer::FindDryFarmland(const ServerCreatureAI& ai)
{
    // Find a position near the NPC's home that's flat, above water, but NOT near water —
    // meaning it would benefit from irrigation.
    if (!worldDB_) return Vector3::ZERO;

    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;

    for (int attempt = 0; attempt < 15; ++attempt)
    {
        float px = ai.homePosition.x_ + Random(-20.0f, 20.0f);
        float pz = ai.homePosition.z_ + Random(-20.0f, 20.0f);
        float py = const_cast<AuthServer*>(this)->GetTerrainHeightAI(px, pz);
        if (py <= AI_WATER_LEVEL) continue;
        if (terrain && terrain->GetNormal(Vector3(px, 0.0f, pz)).y_ < 0.9f) continue;
        // Must NOT be near water — that's the point of irrigation
        if (const_cast<AuthServer*>(this)->IsNearWater(px, pz, 30.0f)) continue;
        return Vector3(px, py, pz);
    }
    return Vector3::ZERO;
}

bool AuthServer::NPCDigChannel(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return false;

    int npcPid = GetNPCPlayerId(ai.spawnId);
    if (npcPid <= 0) return false;

    // Find the dry farmland and nearest water
    Vector3 dryFarm = FindDryFarmland(ai);
    if (dryFarm == Vector3::ZERO) return false;

    float channelRange = HasInnovation(ai.campfireId, INNOV_IRRIGATION_INSIGHT) ? 72.0f : 60.0f;
    Vector3 waterEdge = FindWaterEdge(dryFarm, channelRange);
    if (waterEdge == Vector3::ZERO) return false;

    // Store the channel as a line segment in the database
    int channelId = worldDB_->InsertIrrigationChannel(npcPid,
        waterEdge.x_, waterEdge.z_, dryFarm.x_, dryFarm.z_);

    if (channelId >= 0)
    {
        NPCAwardXP(ai, "farm_plant");  // farming XP for irrigation work

        URHO3D_LOGINFOF("[Irrigation] NPC %u dug channel %d: water(%.0f,%.0f) → farm(%.0f,%.0f)",
            ai.spawnId, channelId, waterEdge.x_, waterEdge.z_, dryFarm.x_, dryFarm.z_);
    }
    return channelId >= 0;
#else
    return false;
#endif
}

bool AuthServer::NPCFish(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !worldDB_) return false;
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0) return false;
    if (!combatResolver_) { combatResolver_ = new CombatResolver(context_); combatResolver_->SetExternalRNG(QuantumDiceRollBridge); }

    int fishingSkill = GetNPCSkillLevel(ai.spawnId, SKILL_FISHING);
    // Phase 26: boat bonus — if a canoe exists within 15m, +3 to fishing
    int boatBonus = 0;
    Vector<PlacedBuildingDBInfo> boats = worldDB_->GetAllPlacedBuildings();
    for (unsigned b = 0; b < boats.Size(); ++b)
    {
        if (boats[b].buildingId == BUILDING_CANOE)
        {
            Vector3 bPos(boats[b].posX, boats[b].posY, boats[b].posZ);
            if ((ai.position - bPos).Length() < 15.0f)
            { boatBonus = FISHING_BOAT_BONUS; break; }
        }
    }
    // Sail upgrade: if settlement has a sail (item 895), double boat bonus
    if (boatBonus > 0 && ai.campfireId != 0)
    {
        for (auto sIt = creatureAI_.Begin(); sIt != creatureAI_.End(); ++sIt)
        {
            if (sIt->second_.campfireId == ai.campfireId && sIt->second_.isHuman)
            {
                int sPid = GetNPCPlayerId(sIt->first_);
                if (sPid > 0 && worldDB_->GetItemCount(sPid, ITEM_SAIL) > 0)
                { boatBonus *= 2; break; }
            }
        }
    }
    int dc = 10;
    int roll = combatResolver_->RollD20();
    bool success = (roll == 20) || (roll != 1 && roll + fishingSkill + boatBonus >= dc);
    NPCAwardXP(ai, "fish_attempt");

    if (!success)
    {
        URHO3D_LOGINFOF("[NPCFish] NPC spawnId=%u missed (roll %d+%d vs DC %d)",
            ai.spawnId, roll, fishingSkill, dc);
        return false;
    }

    int qty = 1 + (fishingSkill >= 5 ? 1 : 0) + (fishingSkill >= 8 ? 1 : 0);
    if (HasInnovation(ai.campfireId, INNOV_BETTER_NETS)) qty += 2;
    AddItemToWorldInventory(npcPlayerId, 10 /* Small Fish */, qty);
    // Award bonus fishing XP on successful catch (Plan 1 Phase 2)
    NPCAwardXP(ai, "fish_catch");
    URHO3D_LOGINFOF("[NPCFish] NPC spawnId=%u caught %d fish (roll %d+%d vs DC %d)",
        ai.spawnId, qty, roll, fishingSkill, dc);
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 7: Charcoal burning
// ---------------------------------------------------------------------------

bool AuthServer::NPCBurnCharcoal(ServerCreatureAI& ai)
{
    // Recipe 81 "Burn Charcoal": 3x Log → 3x Charcoal, station=608 (Charcoal Kiln)
    int npcPlayerId = GetNPCPlayerId(ai.spawnId);
    if (npcPlayerId <= 0)
        return false;

    String ownerName = "NPC_" + String(ai.spawnId);
    bool result = CraftForOwner(npcPlayerId, 81, ai.position, ownerName);
    if (result)
        NPCAwardXP(ai, "fire_tend");
    return result;
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 10: Trading
// ---------------------------------------------------------------------------

unsigned AuthServer::NPCFindTradePartner(const ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return 0;
    int myPid = GetNPCPlayerId(ai.spawnId);
    if (myPid <= 0) return 0;

    Vector<InventorySlot> myInv = worldDB_->GetPlayerInventory(myPid);
    bool myHasWeapon = false, myHasSurplus = false, myHasFireItem = false;
    for (unsigned i = 0; i < myInv.Size(); ++i)
    {
        if (myInv[i].slotType == "hand" && myInv[i].itemId > 0) myHasWeapon = true;
        if (myInv[i].slotType == "bag" && myInv[i].quantity > 3) myHasSurplus = true;
        // Fire items are tradeable at qty >= 2 (keep one for self)
        if (myInv[i].slotType == "bag" && myInv[i].quantity >= 2 &&
            (myInv[i].itemId == ITEM_FIRE_BUNDLE || myInv[i].itemId == ITEM_BARK_VESSEL))
            myHasFireItem = true;
    }

    // Check if my camp needs fire
    bool iNeedFire = false;
    {
        auto cfIt = serverCampfires_.Find(ai.campfireId);
        if (cfIt != serverCampfires_.End() &&
            (cfIt->second_.state == PIT_COLD || cfIt->second_.state == PIT_UNLIT))
            iNeedFire = true;
    }

    bool iNeedFood = ai.hunger < 50.0f;
    bool iNeedTool = !myHasWeapon;
    if (!iNeedFood && !iNeedTool && !iNeedFire && !myHasSurplus && !myHasFireItem) return 0;

    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->first_ == ai.spawnId || !it->second_.isHuman) continue;
        float tradeRange = ai.isMasterTrader ? 30.0f : 20.0f;  // Master Trader: +50% range
        if ((ai.position - it->second_.position).Length() > tradeRange) continue;
        if (it->second_.currentTask == STASK_FLEE || it->second_.currentTask == STASK_DEFEND) continue;

        int theirPid = GetNPCPlayerId(it->first_);
        if (theirPid <= 0) continue;

        Vector<InventorySlot> theirInv = worldDB_->GetPlayerInventory(theirPid);
        bool theirHasFood = false, theirHasTool = false, theirHasFireItem = false;
        for (unsigned j = 0; j < theirInv.Size(); ++j)
        {
            if (theirInv[j].slotType != "bag" || theirInv[j].quantity <= 1) continue;
            ItemInfo item;
            if (!gameDB_->GetItem(theirInv[j].itemId, item)) continue;
            if (item.category == "food") theirHasFood = true;
            if (item.category == "weapon" || item.category == "tool") theirHasTool = true;
            if (theirInv[j].itemId == ITEM_FIRE_BUNDLE || theirInv[j].itemId == ITEM_BARK_VESSEL)
                theirHasFireItem = true;
        }

        // Check if partner's camp needs fire
        bool theirNeedFire = false;
        {
            auto tcfIt = serverCampfires_.Find(it->second_.campfireId);
            if (tcfIt != serverCampfires_.End() &&
                (tcfIt->second_.state == PIT_COLD || tcfIt->second_.state == PIT_UNLIT))
                theirNeedFire = true;
        }

        if (ai.isMasterTrader)
        {
            // Master trader evaluates actual need — both sides benefit
            bool theirNeedFood = it->second_.hunger < 50.0f;
            bool theirNeedTool = true;
            for (unsigned j = 0; j < theirInv.Size(); ++j)
                if (theirInv[j].slotType == "hand" && theirInv[j].itemId > 0) { theirNeedTool = false; break; }

            bool mutualBenefit = (iNeedFood && theirHasFood) || (iNeedTool && theirHasTool)
                || (iNeedFire && theirHasFireItem) || (myHasFireItem && theirNeedFire)
                || (myHasSurplus && (theirNeedFood || theirNeedTool));
            if (mutualBenefit) return it->first_;
        }
        else
        {
            if ((iNeedFood && theirHasFood) || (iNeedTool && theirHasTool)
                || (iNeedFire && theirHasFireItem) || (myHasFireItem && theirNeedFire)
                || myHasSurplus)
                return it->first_;
        }
    }
    return 0;
#else
    return 0;
#endif
}

bool AuthServer::NPCTrade(ServerCreatureAI& ai, unsigned partnerSpawnId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return false;
    int myPid = GetNPCPlayerId(ai.spawnId);
    int theirPid = GetNPCPlayerId(partnerSpawnId);
    if (myPid <= 0 || theirPid <= 0) return false;

    Vector<InventorySlot> myInv = worldDB_->GetPlayerInventory(myPid);
    Vector<InventorySlot> theirInv = worldDB_->GetPlayerInventory(theirPid);

    int giveItemId = 0;
    for (unsigned i = 0; i < myInv.Size(); ++i)
        if (myInv[i].slotType == "bag" && myInv[i].quantity > 3)
        { giveItemId = myInv[i].itemId; break; }

    int receiveItemId = 0;
    for (unsigned i = 0; i < theirInv.Size(); ++i)
        if (theirInv[i].slotType == "bag" && theirInv[i].quantity > 3)
        { receiveItemId = theirInv[i].itemId; break; }

    if (giveItemId == 0 && receiveItemId == 0) return false;
    if (giveItemId == receiveItemId) return false;

    // Base trade quantity from skill/innovation
    int baseQty = 1;
    if (ai.isMasterTrader && HasInnovation(ai.campfireId, INNOV_EFFICIENT_TRADE)) baseQty = 4;
    else if (ai.isMasterTrader) baseQty = 3;
    else if (HasInnovation(ai.campfireId, INNOV_EFFICIENT_TRADE)) baseQty = 2;

    // Use DB trade values to compute fair exchange ratio
    int giveQty = baseQty;
    int receiveQty = baseQty;
    TradeValue giveVal, receiveVal;
    bool haveGiveVal = (giveItemId > 0 && gameDB_->GetTradeValue(giveItemId, giveVal));
    bool haveRecvVal = (receiveItemId > 0 && gameDB_->GetTradeValue(receiveItemId, receiveVal));
    if (haveGiveVal && haveRecvVal && giveVal.baseValue > 0.0f && receiveVal.baseValue > 0.0f)
    {
        // Fair ratio: give fewer of a high-value item, receive more of a low-value item
        float ratio = (receiveVal.baseValue * receiveVal.scarcityMult) /
                      (giveVal.baseValue * giveVal.scarcityMult);
        giveQty = Max(1, (int)(baseQty * ratio + 0.5f));
        receiveQty = baseQty;
    }

    if (giveItemId > 0)
    {
        worldDB_->RemoveItemFromInventory(myPid, giveItemId, giveQty);
        AddItemToWorldInventory(theirPid, giveItemId, giveQty);
    }
    if (receiveItemId > 0)
    {
        worldDB_->RemoveItemFromInventory(theirPid, receiveItemId, receiveQty);
        AddItemToWorldInventory(myPid, receiveItemId, receiveQty);
    }

    NPCAwardXP(ai, "trade_complete");
    auto partnerIt = creatureAI_.Find(partnerSpawnId);
    if (partnerIt != creatureAI_.End())
        NPCAwardXP(partnerIt->second_, "trade_complete");

    URHO3D_LOGINFOF("[NPCTrade] NPC %u gave %dx item %d, received %dx item %d from NPC %u",
        ai.spawnId, giveQty, giveItemId, receiveQty, receiveItemId, partnerSpawnId);
    return true;
#else
    return false;
#endif
}

unsigned AuthServer::FindNearestForeignCampfire(const ServerCreatureAI& ai, float maxRange) const
{
    float bestDistSq = maxRange * maxRange;
    unsigned bestId = 0;

    for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
    {
        if (cfIt->first_ == ai.campfireId)
            continue;  // skip home settlement

        float distSq = (cfIt->second_.position - ai.position).LengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestId = cfIt->first_;
        }
    }
    return bestId;
}

bool AuthServer::NPCTradeRoute(ServerCreatureAI& ai)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_)
        return false;

    int myPid = GetNPCPlayerId(ai.spawnId);
    if (myPid <= 0)
        return false;

    // Find an NPC at the destination settlement to trade with
    unsigned destCfId = ai.targetId;
    unsigned foreignTrader = 0;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (!it->second_.isHuman || it->second_.campfireId != destCfId)
            continue;
        if (it->first_ == ai.spawnId)
            continue;
        foreignTrader = it->first_;
        break;
    }

    if (foreignTrader == 0)
    {
        URHO3D_LOGINFOF("[TradeRoute] NPC %u arrived at campfire %u but no one home",
            ai.spawnId, destCfId);
        return false;
    }

    // CHA drives trade advantage: +1 extra quantity per CHA mod
    int chaMod = ai.stats.ChaMod();
    int bonusQty = (chaMod > 0) ? chaMod : 0;

    // Trade skill check — DC 12 for inter-settlement (harder than local DC 8)
    if (!NPCAttemptSkill(ai, SKILL_TRADE, 12, "complete_trade"))
    {
        URHO3D_LOGINFOF("[TradeRoute] NPC %u failed trade check at campfire %u — wasted trip",
            ai.spawnId, destCfId);
        return false;
    }

    // Find my surplus (quantity > 3) and their surplus
    int theirPid = GetNPCPlayerId(foreignTrader);
    if (theirPid <= 0)
        return false;

    Vector<InventorySlot> myInv = worldDB_->GetPlayerInventory(myPid);
    Vector<InventorySlot> theirInv = worldDB_->GetPlayerInventory(theirPid);

    int giveItemId = 0;
    for (unsigned i = 0; i < myInv.Size(); ++i)
        if (myInv[i].slotType == "bag" && myInv[i].quantity > 3)
        { giveItemId = myInv[i].itemId; break; }

    int receiveItemId = 0;
    for (unsigned i = 0; i < theirInv.Size(); ++i)
        if (theirInv[i].slotType == "bag" && theirInv[i].quantity > 3)
        { receiveItemId = theirInv[i].itemId; break; }

    if (giveItemId == 0 && receiveItemId == 0)
        return false;
    if (giveItemId == receiveItemId)
        return false;

    // Pack donkey bonus: +2 trade quantity when settlement has tamed donkey
    int donkeyBonus = 0;
    for (auto dIt = creatureAI_.Begin(); dIt != creatureAI_.End(); ++dIt)
    {
        if (dIt->second_.tamerId != 0 && dIt->second_.creatureId == 9 &&
            dIt->second_.campfireId == ai.campfireId)
        { donkeyBonus = 2; break; }
    }

    // Caravan bonus: count other NPCs from same settlement also on STASK_TRADE_ROUTE
    // to same destination. Each adds +1 trade quantity (max +2).
    int caravanBonus = 0;
    for (auto cIt = creatureAI_.Begin(); cIt != creatureAI_.End(); ++cIt)
    {
        if (cIt->first_ == ai.spawnId || !cIt->second_.isHuman)
            continue;
        if (cIt->second_.campfireId == ai.campfireId &&
            cIt->second_.currentTask == STASK_TRADE_ROUTE &&
            cIt->second_.targetId == destCfId)
            ++caravanBonus;
    }
    caravanBonus = Min(caravanBonus, 2);

    // Base 2 + CHA bonus + donkey bonus + caravan bonus
    int giveQty = 2 + donkeyBonus + caravanBonus;
    int receiveQty = 2 + bonusQty + donkeyBonus + caravanBonus;

    if (giveItemId > 0)
    {
        worldDB_->RemoveItemFromInventory(myPid, giveItemId, giveQty);
        AddItemToWorldInventory(theirPid, giveItemId, giveQty);
    }
    if (receiveItemId > 0)
    {
        worldDB_->RemoveItemFromInventory(theirPid, receiveItemId, receiveQty);
        AddItemToWorldInventory(myPid, receiveItemId, receiveQty);
    }

    NPCAwardXP(ai, "complete_trade");
    auto partnerIt = creatureAI_.Find(foreignTrader);
    if (partnerIt != creatureAI_.End())
        NPCAwardXP(partnerIt->second_, "complete_trade");

    // Knowledge sharing: trader's settlement innovations spread to destination
    // 30% chance per innovation not yet known at destination
    for (int innov = 0; innov < 9; ++innov)
    {
        if (HasInnovation(ai.campfireId, innov) && !HasInnovation(destCfId, innov))
        {
            if (Random(1.0f) < 0.3f)
            {
                worldDB_->Execute(
                    "INSERT OR IGNORE INTO settlement_innovations (campfire_id, innovation_id, discoverer_name, game_day) "
                    "VALUES (" + String(destCfId) + ", " + String(innov) + ", 'Trade from " +
                    String(ai.campfireId) + "', " + String(currentGameDay_) + ")");
                URHO3D_LOGINFOF("[Knowledge] Innovation %d spread from settlement %u to %u via trade",
                    innov, ai.campfireId, destCfId);
            }
        }
    }

    URHO3D_LOGINFOF("[TradeRoute] NPC %u traded %dx item %d for %dx item %d at campfire %u (CHA bonus +%d, donkey +%d)",
        ai.spawnId, giveQty, giveItemId, receiveQty, receiveItemId, destCfId, bonusQty, donkeyBonus);
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 14: Social Bonds + Family
// ---------------------------------------------------------------------------

void AuthServer::UpdateNPCBonds(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return;

    // For each pair of human NPCs at the same campfire, increment familiarity
    for (auto itA = creatureAI_.Begin(); itA != creatureAI_.End(); ++itA)
    {
        ServerCreatureAI& a = itA->second_;
        if (!a.isHuman || a.campfireId == 0) continue;

        for (auto itB = itA; itB != creatureAI_.End(); ++itB)
        {
            if (itB == itA) continue;
            ServerCreatureAI& b = itB->second_;
            if (!b.isHuman || b.campfireId != a.campfireId) continue;

            // Must be opposite sex (20=CaveMan, 21=CaveWoman)
            if (a.creatureId == b.creatureId) continue;

            // Must be within 15m of each other
            if ((a.position - b.position).Length() > 15.0f) continue;

            // Both must be doing productive tasks (not idle/wander/sleep)
            bool aActive = (a.currentTask != STASK_IDLE && a.currentTask != STASK_WANDER &&
                           a.currentTask != STASK_SLEEP);
            bool bActive = (b.currentTask != STASK_IDLE && b.currentTask != STASK_WANDER &&
                           b.currentTask != STASK_SLEEP);

            if (aActive || bActive)
            {
                // Increment familiarity: 0.5 per second when both near same fire
                worldDB_->IncrementFamiliarity(itA->first_, itB->first_, 0.5f * dt);
            }
        }
    }
#endif
}

void AuthServer::CheckNPCBreeding()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return;

    int season = GetCurrentSeasonIndex();
    // Only breed in spring (0) or summer (1)
    if (season > 1) return;

    for (auto itA = creatureAI_.Begin(); itA != creatureAI_.End(); ++itA)
    {
        ServerCreatureAI& a = itA->second_;
        if (!a.isHuman || a.growthProgress < 1.0f) continue;  // adults only
        if (a.creatureId != 20) continue;  // start from males to avoid double-check

        for (auto itB = creatureAI_.Begin(); itB != creatureAI_.End(); ++itB)
        {
            if (itB == itA) continue;
            ServerCreatureAI& b = itB->second_;
            if (!b.isHuman || b.growthProgress < 1.0f) continue;
            if (b.creatureId != 21) continue;  // female
            if (a.campfireId == 0 || a.campfireId != b.campfireId) continue;

            // Already mates? Check if they already have a recent child
            String bondType = worldDB_->GetBondType(itA->first_, itB->first_);
            if (bondType == "mate")
            {
                // Check if they already have a child in the world (cooldown)
                bool hasChild = false;
                for (auto itC = creatureAI_.Begin(); itC != creatureAI_.End(); ++itC)
                {
                    if (itC->second_.parentA == itA->first_ || itC->second_.parentB == itA->first_)
                        if (itC->second_.growthProgress < 1.0f)
                        { hasChild = true; break; }
                }
                if (hasChild) continue;
            }

            float familiarity = worldDB_->GetBondFamiliarity(itA->first_, itB->first_);
            if (familiarity < 40.0f) continue;  // threshold (lowered from 70 for early-game breeding)

            // Both fed (hunger > 60)
            if (a.hunger < 60.0f || b.hunger < 60.0f) continue;

            // All conditions met — become mates and spawn child
            worldDB_->SetBondType(itA->first_, itB->first_, "mate");
            SpawnChildNPC(a, b);

            URHO3D_LOGINFOF("[Family] NPCs %u + %u became mates, child spawned (familiarity=%.0f)",
                itA->first_, itB->first_, familiarity);
            return;  // one birth per tick max
        }
    }
#endif
}

void AuthServer::SpawnChildNPC(ServerCreatureAI& parentA, ServerCreatureAI& parentB)
{
#ifdef URHO3D_DATABASE_SQLITE
    // Pick sex randomly
    int childSpecies = (Random(1.0f) < 0.5f) ? 20 : 21;  // CaveMan or CaveWoman

    // Spawn position: between parents
    Vector3 childPos = (parentA.position + parentB.position) * 0.5f;
    childPos.y_ = GetTerrainHeightAI(childPos.x_, childPos.z_);

    // Create AI entry
    unsigned childSpawnId = ++nextSpawnId_;
    ServerCreatureAI& child = creatureAI_[childSpawnId];
    child.position = childPos;
    child.targetPosition = childPos;
    child.homePosition = parentA.homePosition;
    child.campfireId = parentA.campfireId;
    child.settlementId = parentA.campfireId;
    child.birthSettlement = parentA.campfireId;
    child.creatureId = childSpecies;
    child.isHuman = true;
    child.regionId = parentA.regionId;
    child.spawnId = childSpawnId;
    child.growthProgress = 0.0f;  // newborn
    child.parentA = parentA.spawnId;
    child.parentB = parentB.spawnId;
    child.wanderRadius = 10.0f;  // children stay close

    // Create creature state
    ServerCreatureState& cs = creatureStates_[childSpawnId];
    cs.creatureId = childSpecies;
    cs.hp = 10;
    cs.maxHp = 10;  // grows to 20
    cs.species = (childSpecies == 20) ? "CaveMan" : "CaveWoman";
    cs.regionId = parentA.regionId;

    // Assign NPC player ID for inventory
    int childPlayerId = 10000 + (int)childSpawnId;
    npcPlayerIds_[childSpawnId] = childPlayerId;

    // Inherit skill affinities — weighted average of parents' top 2 skills ± drift
    if (gameDB_)
    {
        int parentAPid = GetNPCPlayerId(parentA.spawnId);
        int parentBPid = GetNPCPlayerId(parentB.spawnId);

        // Find each parent's top 2 skills
        struct SkillEntry { int id; int level; };
        Vector<SkillEntry> parentASkills, parentBSkills;
        int allSkillIds[] = {1,2,3,10,11,12,13,14,15,16,20,21,22,23,24,25,26,27,28,33};
        for (int i = 0; i < 20; ++i)
        {
            int lvA = (parentAPid > 0) ? gameDB_->GetSkillLevel(parentAPid, allSkillIds[i]) : 0;
            int lvB = (parentBPid > 0) ? gameDB_->GetSkillLevel(parentBPid, allSkillIds[i]) : 0;
            if (lvA > 0) parentASkills.Push({allSkillIds[i], lvA});
            if (lvB > 0) parentBSkills.Push({allSkillIds[i], lvB});
        }

        // Sort by level descending, take top 2 from each
        auto cmp = [](const SkillEntry& a, const SkillEntry& b) { return a.level > b.level; };
        Sort(parentASkills.Begin(), parentASkills.End(), cmp);
        Sort(parentBSkills.Begin(), parentBSkills.End(), cmp);

        // Collect unique skill IDs from top 2 of each parent
        HashMap<int, float> inheritedXP;
        for (unsigned i = 0; i < Min((unsigned)2, parentASkills.Size()); ++i)
            inheritedXP[parentASkills[i].id] += parentASkills[i].level * 5.0f;  // seed XP
        for (unsigned i = 0; i < Min((unsigned)2, parentBSkills.Size()); ++i)
            inheritedXP[parentBSkills[i].id] += parentBSkills[i].level * 5.0f;

        // Average + random drift (±30%)
        for (auto it = inheritedXP.Begin(); it != inheritedXP.End(); ++it)
        {
            float xp = it->second_ * 0.5f;  // average
            xp *= (0.7f + Random(0.6f));     // ±30% drift
            if (xp >= 1.0f)
            {
                // Award XP to child (creates the skill entry in DB)
                // Use direct DB write since child has no XP triggers yet
                int startXP = (int)xp;
                for (int x = 0; x < startXP; ++x)
                {
                    String action = "craft_stone";  // generic XP trigger
                    gameDB_->AwardXP(childPlayerId, action);
                }
            }
        }
    }

    // Persist parent-child bonds in npc_bonds table
    if (worldDB_)
    {
        worldDB_->SetBondType(parentA.spawnId, childSpawnId, "parent");
        worldDB_->SetBondType(parentB.spawnId, childSpawnId, "parent");
    }

    // Broadcast child spawn to clients
    {
        VectorBuffer buf;
        buf.WriteI32(parentA.regionId);
        buf.WriteI32(childSpecies);
        buf.WriteFloat(childPos.x_);
        buf.WriteFloat(0.0f);
        buf.WriteFloat(childPos.z_);
        buf.WriteU32(childSpawnId);
        buf.WriteString(child.npcName);
        buf.WriteFloat(1.0f);  // traitSize (NPCs don't use animal traits)
        buf.WriteFloat(1.0f);  // traitSpeed
        buf.WriteFloat(1.0f);  // traitYield
        for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
            if (sIt->second_.authenticated)
                sIt->first_->SendMessage(MSG_SPAWN_CREATURE, true, true, buf);
    }

    URHO3D_LOGINFOF("[Family] Child NPC spawnId=%u species=%d spawned at (%.0f,%.0f,%.0f) parents=%u+%u",
        childSpawnId, childSpecies, childPos.x_, childPos.y_, childPos.z_,
        parentA.spawnId, parentB.spawnId);
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 15: Settlement Expansion
// ---------------------------------------------------------------------------

int AuthServer::CountSettlementPopulation(unsigned campfireId)
{
    int count = 0;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        if (it->second_.isHuman && it->second_.campfireId == campfireId) ++count;
    return count;
}

int AuthServer::CountSettlementBeds(unsigned campfireId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_) return 0;
    int beds = 0;
    auto cfIt = serverCampfires_.Find(campfireId);
    if (cfIt == serverCampfires_.End()) return 0;

    Vector<PlacedBuildingDBInfo> buildings = worldDB_->GetAllPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        Vector3 bPos(buildings[i].posX, buildings[i].posY, buildings[i].posZ);
        if ((bPos - cfIt->second_.position).Length() > 30.0f) continue;
        auto typeIt = cachedBuildingTypes_.Find(buildings[i].buildingId);
        if (typeIt != cachedBuildingTypes_.End())
            beds += typeIt->second_.sleepCapacity;
    }
    return beds;
#else
    return 999;
#endif
}

void AuthServer::CheckSettlementExpansion()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_ || !gameDB_) return;

    for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
    {
        unsigned campfireId = cfIt->first_;
        int pop = CountSettlementPopulation(campfireId);
        int beds = CountSettlementBeds(campfireId);
        if (pop <= beds || beds == 0) continue;

        // Find a mature NPC with 8 stones (any NPC can attempt to found a settlement)
        ServerCreatureAI* founder = nullptr;
        unsigned founderSpawnId = 0;
        for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        {
            ServerCreatureAI& ai = it->second_;
            if (!ai.isHuman || ai.campfireId != campfireId || ai.growthProgress < 1.0f) continue;
            int npcPid = GetNPCPlayerId(it->first_);
            if (npcPid <= 0 || worldDB_->GetItemCount(npcPid, 1) < 8) continue;
            founder = &ai;
            founderSpawnId = it->first_;
            break;
        }
        if (!founder) continue;

        // Pick new campfire site 40-80m away
        Vector3 currentPos = cfIt->second_.position;
        Vector3 newPos;
        bool foundSite = false;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            float angle = Random(360.0f);
            float dist = 40.0f + Random(40.0f);
            float nx = currentPos.x_ + Cos(angle) * dist;
            float nz = currentPos.z_ + Sin(angle) * dist;
            float ny = GetTerrainHeightAI(nx, nz);
            if (ny <= AI_WATER_LEVEL + 1.0f) continue;

            auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
            if (terrain && terrain->GetNormal(Vector3(nx, 0.0f, nz)).y_ < 0.8f) continue;

            bool tooClose = false;
            for (auto ocf = serverCampfires_.Begin(); ocf != serverCampfires_.End(); ++ocf)
                if ((Vector3(nx, ny, nz) - ocf->second_.position).Length() < 30.0f)
                { tooClose = true; break; }
            if (tooClose) continue;

            // Settlement patch exclusivity — reject if patch already claimed
            IntVector2 spatch = WorldPosToSettlementPatch(Vector3(nx, ny, nz));
            if (worldDB_ && !worldDB_->IsSettlementPatchFree(0, spatch.x_, spatch.y_))
                continue;

            newPos = Vector3(nx, ny, nz);
            foundSite = true;
            break;
        }
        if (!foundSite) continue;

        // Consume 8 stones from founder
        int founderPid = GetNPCPlayerId(founderSpawnId);
        worldDB_->RemoveItemFromInventory(founderPid, 1, 8);

        // Create new campfire (UNLIT — founder will need to ignite it)
        unsigned newPitId = ++nextCampfireId_;
        ServerCampfire& newCf = serverCampfires_[newPitId];
        newCf.position = newPos;
        newCf.fuelSeconds = 0.0f;
        newCf.state = PIT_UNLIT;
        newCf.regionId = cfIt->second_.regionId;

        // Persist
        Vector<FirePitDBInfo> pits = worldDB_->LoadFirePits();
        FirePitDBInfo pitDB;
        pitDB.pitId = newPitId;
        pitDB.position = newPos;
        pitDB.fuel = 0.0f;
        pitDB.state = PIT_UNLIT;
        pitDB.regionId = newCf.regionId;
        pits.Push(pitDB);
        worldDB_->SaveFirePits(pits);

        BroadcastPitState(newPitId, newCf);

        // Claim settlement patch for the new settlement + broadcast to clients
        {
            IntVector2 spatch = WorldPosToSettlementPatch(newPos);
            worldDB_->ClaimSettlementPatch(0, spatch.x_, spatch.y_, (int)newPitId);

            // Delta: tell all clients about the new claim
            VectorBuffer claimBuf;
            claimBuf.WriteU16(1);  // count = 1
            claimBuf.WriteU8((unsigned char)spatch.x_);
            claimBuf.WriteU8((unsigned char)spatch.y_);
            claimBuf.WriteU16((unsigned short)newPitId);
            for (auto sit = sessions_.Begin(); sit != sessions_.End(); ++sit)
                if (sit->second_.authenticated)
                    sit->first_->SendMessage(MSG_SETTLEMENT_CLAIMS, true, true, claimBuf);
        }

        // Move founder
        founder->campfireId = newPitId;
        founder->settlementId = newPitId;
        founder->homePosition = newPos;

        // Bonded NPCs follow (mate + children)
        for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
        {
            if (it->first_ == founderSpawnId || !it->second_.isHuman) continue;
            if (it->second_.campfireId != campfireId) continue;

            bool follow = false;
            if (it->second_.parentA == founderSpawnId || it->second_.parentB == founderSpawnId)
                follow = true;
            if (!follow && worldDB_->GetBondType(founderSpawnId, it->first_) == "mate")
                follow = true;

            if (follow)
            {
                it->second_.campfireId = newPitId;
                it->second_.settlementId = newPitId;
                it->second_.homePosition = newPos;
                URHO3D_LOGINFOF("[Settlement] NPC %u followed founder %u to campfire %u",
                    it->first_, founderSpawnId, newPitId);
            }
        }

        NPCAwardXP(*founder, "craft_wood");
        URHO3D_LOGINFOF("[Settlement] NPC %u founded new settlement at (%.0f,%.0f,%.0f) campfire=%u (pop %d > beds %d)",
            founderSpawnId, newPos.x_, newPos.y_, newPos.z_, newPitId, pop, beds);
        return;  // one expansion per tick
    }
#endif
}

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 21: Chieftain + Hierarchy
// ---------------------------------------------------------------------------

void AuthServer::EvaluateChieftains()
{
    if (!gameDB_)
        return;

    // All skills to scan for chieftain candidacy
    static const int allSkills[] = {
        SKILL_MELEE, SKILL_RANGED, SKILL_DEFENSE, SKILL_TRACKING, SKILL_TRAPPING,
        SKILL_FORAGING, SKILL_KNAPPING, SKILL_WOODWORK, SKILL_FISHING, SKILL_COOKING,
        SKILL_LEATHERWORK, SKILL_WEAVING, SKILL_SMELTING, SKILL_SMITHING, SKILL_FARMING,
        SKILL_FIREMAKING, SKILL_TRADE, SKILL_ANIMAL_LORE
    };

    // Group humans by settlementId, count population, clear stale chieftain flags
    HashMap<unsigned, Vector<unsigned>> settlements;  // settlementId → [spawnIds]
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        ServerCreatureAI& ai = it->second_;
        if (!ai.isHuman)
            continue;
        // Clear chieftain flag — will be re-assigned below
        ai.isChieftain = false;
        ai.chieftainSkillId = 0;
        settlements[ai.settlementId].Push(it->first_);
    }

    // For each settlement with pop >= 6, find highest-skilled NPC with any skill 8+
    for (auto sIt = settlements.Begin(); sIt != settlements.End(); ++sIt)
    {
        const Vector<unsigned>& members = sIt->second_;
        if (members.Size() < 6)
            continue;

        unsigned bestSpawnId = 0;
        int bestLevel = 0;
        int bestSkillId = 0;

        for (unsigned i = 0; i < members.Size(); ++i)
        {
            auto aiIt = creatureAI_.Find(members[i]);
            if (aiIt == creatureAI_.End())
                continue;

            int npcPid = GetNPCPlayerId(aiIt->second_.spawnId);
            if (npcPid <= 0)
                continue;

            for (int sk : allSkills)
            {
                int lvl = gameDB_->GetSkillLevel(npcPid, sk);
                if (lvl > bestLevel)
                {
                    bestLevel = lvl;
                    bestSkillId = sk;
                    bestSpawnId = members[i];
                }
            }
        }

        if (bestLevel >= 8 && bestSpawnId != 0)
        {
            auto chiefIt = creatureAI_.Find(bestSpawnId);
            if (chiefIt != creatureAI_.End())
            {
                chiefIt->second_.isChieftain = true;
                chiefIt->second_.chieftainSkillId = bestSkillId;

                // Broadcast MSG_CHIEFTAIN_CHANGED if chieftain changed
                unsigned campfireId = sIt->first_;
                auto prevIt = settlementChieftains_.Find(campfireId);
                unsigned prevChief = (prevIt != settlementChieftains_.End()) ? prevIt->second_ : 0;
                if (prevChief != bestSpawnId)
                {
                    // Determine reason: 0=first, 1=death, 2=surpassed
                    unsigned char reason = 0;
                    if (prevChief != 0)
                    {
                        auto deadIt = creatureAI_.Find(prevChief);
                        reason = (deadIt == creatureAI_.End()) ? 1 : 2;
                    }

                    // Skill name lookup
                    const char* skillStr = "Unknown";
                    switch (bestSkillId)
                    {
                    case SKILL_MELEE: skillStr = "Melee"; break;
                    case SKILL_RANGED: skillStr = "Ranged"; break;
                    case SKILL_DEFENSE: skillStr = "Defense"; break;
                    case SKILL_TRACKING: skillStr = "Tracking"; break;
                    case SKILL_TRAPPING: skillStr = "Trapping"; break;
                    case SKILL_FORAGING: skillStr = "Foraging"; break;
                    case SKILL_KNAPPING: skillStr = "Knapping"; break;
                    case SKILL_WOODWORK: skillStr = "Woodwork"; break;
                    case SKILL_FISHING: skillStr = "Fishing"; break;
                    case SKILL_COOKING: skillStr = "Cooking"; break;
                    case SKILL_LEATHERWORK: skillStr = "Leatherwork"; break;
                    case SKILL_WEAVING: skillStr = "Weaving"; break;
                    case SKILL_SMELTING: skillStr = "Smelting"; break;
                    case SKILL_SMITHING: skillStr = "Smithing"; break;
                    case SKILL_FARMING: skillStr = "Farming"; break;
                    case SKILL_FIREMAKING: skillStr = "Firemaking"; break;
                    case SKILL_TRADE: skillStr = "Trade"; break;
                    case SKILL_ANIMAL_LORE: skillStr = "Animal Lore"; break;
                    }

                    VectorBuffer buf;
                    buf.WriteU32(campfireId);
                    buf.WriteU32(bestSpawnId);
                    buf.WriteString(chiefIt->second_.npcName);
                    buf.WriteString(skillStr);
                    buf.WriteU8(reason);
                    for (auto sIt2 = sessions_.Begin(); sIt2 != sessions_.End(); ++sIt2)
                    {
                        if (sIt2->second_.authenticated)
                            sIt2->first_->SendMessage(MSG_CHIEFTAIN_CHANGED, true, true, buf);
                    }

                    settlementChieftains_[campfireId] = bestSpawnId;
                    URHO3D_LOGINFOF("[Chieftain] Settlement %u: %s became chieftain (Master %s, reason %d)",
                        campfireId, chiefIt->second_.npcName.CString(), skillStr, reason);

                    // Auto-place settlement banner on first chieftain election
                    if (reason == 0 && worldDB_)
                    {
                        Vector3 bannerPos = chiefIt->second_.homePosition;
                        bannerPos.x_ += 2.0f;  // offset from campfire center
                        int bannerId = worldDB_->InsertPlacedBuilding(142, GetNPCPlayerId(bestSpawnId),
                            bannerPos.x_, bannerPos.y_, bannerPos.z_, 0.0f, 40, currentGameDay_, 0);
                        if (bannerId >= 0)
                        {
                            BroadcastBuildingSpawn(bannerId, 142, bannerPos.x_, bannerPos.y_, bannerPos.z_, 0.0f, 40);
                            URHO3D_LOGINFOF("[Banner] Settlement %u got War Banner at first chieftain election", campfireId);
                        }
                    }
                }

                // Phase 28: auto-fortify directive when 3+ predator attacks
                auto cfIt = serverCampfires_.Find(sIt->first_);
                if (cfIt != serverCampfires_.End() && cfIt->second_.predatorAttackCount >= 3)
                {
                    int combatLvl = Max(GetNPCSkillLevel(bestSpawnId, SKILL_MELEE),
                        Max(GetNPCSkillLevel(bestSpawnId, SKILL_RANGED),
                            GetNPCSkillLevel(bestSpawnId, SKILL_DEFENSE)));
                    if (combatLvl >= 4)
                    {
                        for (unsigned m = 0; m < members.Size(); ++m)
                        {
                            auto mIt = creatureAI_.Find(members[m]);
                            if (mIt == creatureAI_.End() || mIt->second_.isChieftain)
                                continue;
                            if (mIt->second_.directive == ServerCreatureAI::DIRECTIVE_NONE)
                            {
                                mIt->second_.directive = ServerCreatureAI::DIRECTIVE_FOCUS_TASK;
                                mIt->second_.directiveParam = STASK_BUILD;
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// (Phase 23 TickFoodDecay implemented by coder at line ~14294)

// Phase 25: TickBreeding delegates to CheckTamedBreeding (canonical impl at ~14307)
// after decrementing breed cooldowns.
void AuthServer::TickBreeding(float dt)
{
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->second_.breedCooldown > 0.0f)
            it->second_.breedCooldown = Max(0.0f, it->second_.breedCooldown - dt);
    }
    CheckTamedBreeding();
    CheckWildBreeding();
}

void AuthServer::CheckWildBreeding()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!populationManager_ || !populationManager_->IsReady())
        return;

    // Spring/summer only
    int season = GetCurrentSeasonIndex();
    URHO3D_LOGINFOF("[WildBreeding-Diag] CheckWildBreeding called — season=%d (%s), creatureAI size=%u",
        season, season==0?"spring":season==1?"summer":season==2?"autumn":"winter",
        creatureAI_.Size());
    if (season != 0 && season != 1)
        return;

    // Group wild animals by (region, species) — key = region<<16 | species
    // Also track per-species rejection counts for diagnostics
    struct SpeciesFilterStats { int tamed; int cooldown; int juvenile; int starving; int total; };
    HashMap<unsigned, SpeciesFilterStats> filterStats;
    HashMap<unsigned, Vector<unsigned>> groups;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        const ServerCreatureAI& ai = it->second_;
        if (ai.isHuman) continue;
        unsigned key = ((unsigned)ai.regionId << 16) | (unsigned)(ai.creatureId & 0xFFFF);
        SpeciesFilterStats& fs = filterStats[key];
        fs.total++;
        if (ai.tamerId != 0)          { fs.tamed++; continue; }
        if (ai.breedCooldown > 0.0f)  { fs.cooldown++; continue; }
        if (ai.growthProgress < 1.0f) { fs.juvenile++; continue; }
        if (ai.hunger < 20.0f)        { fs.starving++; continue; }
        groups[key].Push(it->first_);
    }

    for (auto gIt = groups.Begin(); gIt != groups.End(); ++gIt)
    {
        Vector<unsigned>& members = gIt->second_;
        int speciesId = (int)(gIt->first_ & 0xFFFF);
        int regionId = (int)(gIt->first_ >> 16);
        SpeciesFilterStats& fs = filterStats[gIt->first_];

        // Count males and females, find closest male-female pair
        int maleCount = 0, femaleCount = 0;
        unsigned maleId = 0, femaleId = 0;
        Vector<unsigned> males, females;
        for (unsigned i = 0; i < members.Size(); ++i)
        {
            auto aiIt = creatureAI_.Find(members[i]);
            if (aiIt == creatureAI_.End()) continue;
            if (aiIt->second_.isMale) { maleCount++; males.Push(members[i]); }
            else                      { femaleCount++; females.Push(members[i]); }
        }

        // Find closest male-female distance
        float closestDist = 99999.0f;
        unsigned closestMale = 0, closestFemale = 0;
        for (unsigned m = 0; m < males.Size(); ++m)
        {
            auto mIt = creatureAI_.Find(males[m]);
            if (mIt == creatureAI_.End()) continue;
            for (unsigned f = 0; f < females.Size(); ++f)
            {
                auto fIt = creatureAI_.Find(females[f]);
                if (fIt == creatureAI_.End()) continue;
                float d = (mIt->second_.position - fIt->second_.position).Length();
                if (d < closestDist) { closestDist = d; closestMale = males[m]; closestFemale = females[f]; }
            }
        }

        // Determine rejection reason
        const char* gate = "NONE";
        if (members.Size() < 2)
            gate = "too_few_eligible";
        else if (maleCount == 0 || femaleCount == 0)
            gate = "no_male_female_pair";
        else
        {
            int currentPop = populationManager_->GetPopulation(regionId, speciesId);
            int maxPop = populationManager_->GetMaxPopulation(regionId, speciesId);
            if (maxPop > 0 && currentPop >= (int)(maxPop * 0.8f))
                gate = "pop_cap";
            else if (closestDist > 100.0f)
                gate = "proximity";
        }

        URHO3D_LOGINFOF("[WildBreeding-Diag] species=%d region=%d total=%d eligible=%u M=%d F=%d "
            "closest=%.1fm filtered(tamed=%d cooldown=%d juvenile=%d starving=%d) gate=%s",
            speciesId, regionId, fs.total, members.Size(), maleCount, femaleCount,
            closestDist < 99999.0f ? closestDist : -1.0f,
            fs.tamed, fs.cooldown, fs.juvenile, fs.starving, gate);

        if (members.Size() < 2) continue;

        // Check population cap
        int currentPop = populationManager_->GetPopulation(regionId, speciesId);
        int maxPop = populationManager_->GetMaxPopulation(regionId, speciesId);
        if (maxPop > 0 && currentPop >= (int)(maxPop * 0.8f))
            continue;  // at or above 80% carrying capacity

        if (maleCount == 0 || femaleCount == 0) continue;

        maleId = closestMale;
        femaleId = closestFemale;

        auto maleIt = creatureAI_.Find(maleId);
        auto femaleIt = creatureAI_.Find(femaleId);
        if (maleIt == creatureAI_.End() || femaleIt == creatureAI_.End()) continue;

        // Proximity check — must be within 100m of each other (roughly wander radius)
        if (closestDist > 100.0f)
            continue;

        // Trait inheritance with drift
        float drift = 0.05f;
        float childSize  = Clamp((maleIt->second_.traitSize  + femaleIt->second_.traitSize)  * 0.5f + Random(-drift, drift), 0.8f, 1.2f);
        float childSpeed = Clamp((maleIt->second_.traitSpeed + femaleIt->second_.traitSpeed) * 0.5f + Random(-drift, drift), 0.8f, 1.2f);
        float childYield = Clamp((maleIt->second_.traitYield + femaleIt->second_.traitYield) * 0.5f + Random(-drift, drift), 0.8f, 1.2f);

        // Spawn near the female
        Vector3 spawnPos = femaleIt->second_.position;
        spawnPos.x_ += Random(-3.0f, 3.0f);
        spawnPos.z_ += Random(-3.0f, 3.0f);
        spawnPos.y_ = GetTerrainHeightAI(spawnPos.x_, spawnPos.z_);
        if (spawnPos.y_ <= AI_WATER_LEVEL) continue;  // don't spawn in water

        unsigned childId = ++nextSpawnId_;
        ServerCreatureAI& child = creatureAI_[childId];
        child.position = spawnPos;
        child.targetPosition = spawnPos;
        child.homePosition = femaleIt->second_.homePosition;
        child.creatureId = speciesId;
        child.regionId = regionId;
        child.isPredator = IsPredatorSpecies(speciesId);
        child.moveSpeed = 1.5f * childSpeed;
        child.isMale = (Random(1.0f) < 0.5f);
        child.traitSize = childSize;
        child.traitSpeed = childSpeed;
        child.traitYield = childYield;
        child.growthProgress = 0.0f;
        child.maturityDays = 20;  // DB default, overridden below if available
        child.hunger = 80.0f;
        child.thirst = 80.0f;
        child.stamina = 100.0f;
        child.warmth = weatherTemperature_;
        child.parentA = maleId;
        child.parentB = femaleId;

        ServerCreatureState cs;
        cs.creatureId = speciesId;
        cs.position = spawnPos;
        cs.regionId = regionId;
        if (!LoadCreatureCombat(speciesId, cs))
        { cs.hp = cs.maxHp = 10; cs.defense = 5; }
        creatureStates_[childId] = cs;

        // Cooldown both parents
        maleIt->second_.breedCooldown = 600.0f;
        femaleIt->second_.breedCooldown = 600.0f;

        // Broadcast to clients
        BroadcastSpawnCreature(regionId, speciesId, spawnPos, 0.0f);

        URHO3D_LOGINFOF("[WildBreeding] Offspring spawnId=%u species=%d traits(%.2f,%.2f,%.2f) parents=%u+%u region=%d",
            childId, speciesId, childSize, childSpeed, childYield, maleId, femaleId, regionId);
    }
#endif
}

// (Phase 26 NPCBuildBoat implemented by coder at ~14523)

// ---------------------------------------------------------------------------
// NPC Bronze Age Phase 30: Oral History + Naming
// ---------------------------------------------------------------------------

String AuthServer::GenerateNPCName(unsigned campfireId)
{
    static const char* onsets[] = {
        "K", "T", "R", "M", "N", "S", "L", "D", "B", "G",
        "Th", "Sh", "Ch", "Z", "V", "F", "P", "H", "W", "J"
    };
    static const char* vowels[] = {
        "a", "e", "i", "o", "u", "ai", "ei", "ou", "ae", "ir"
    };
    static const char* codas[] = {
        "", "", "", "n", "r", "k", "s", "th", "l", "m"
    };

    HashSet<String> existing;
    for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
    {
        if (it->second_.isHuman && it->second_.campfireId == campfireId && !it->second_.npcName.Empty())
            existing.Insert(it->second_.npcName);
    }

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        String name;
        int syllables = 2 + (Random(3) == 0 ? 1 : 0);
        for (int s = 0; s < syllables; ++s)
        {
            name += onsets[Random(20)];
            name += vowels[Random(10)];
            name += codas[Random(10)];
        }
        if (!existing.Contains(name))
            return name;
    }
    return "Npc" + String(Random(9999));
}

void AuthServer::RecordSettlementFirst(unsigned campfireId, const String& category, unsigned npcSpawnId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return;

    String npcName = "Unknown";
    auto aiIt = creatureAI_.Find(npcSpawnId);
    if (aiIt != creatureAI_.End() && !aiIt->second_.npcName.Empty())
        npcName = aiIt->second_.npcName;

    worldDB_->Execute(
        "INSERT OR IGNORE INTO settlement_history (campfire_id, category, npc_name, npc_spawn_id, game_day) "
        "VALUES (" + String(campfireId) + ", '" + category + "', '" + npcName + "', " +
        String(npcSpawnId) + ", " + String(currentGameDay_) + ")");

    URHO3D_LOGINFOF("[History] Settlement %u first '%s' by %s (spawnId %u, day %d)",
        campfireId, category.CString(), npcName.CString(), npcSpawnId, currentGameDay_);
#endif
}

void AuthServer::HandleQueryDeathLog(Connection* connection, MemoryBuffer& msg)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return;

    unsigned short maxEntries = msg.ReadU16();
    if (maxEntries == 0 || maxEntries > 50)
        maxEntries = 20;

    sqlite3* db = worldDB_->GetHandle();
    if (!db)
        return;

    static const char* causeNames[] = {
        "combat", "drown", "starve", "age", "scavenge", "fall", "fire", "dehydrate", "freeze"
    };

    VectorBuffer buf;
    unsigned short count = 0;
    unsigned short countPos = buf.GetSize();
    buf.WriteU16(0);  // placeholder — overwritten after query

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT npc_name, species, pos_x, pos_z, cause, killer_name, game_day "
        "FROM death_log ORDER BY id DESC LIMIT ?",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, maxEntries);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            const char* species = (const char*)sqlite3_column_text(stmt, 1);
            float px = (float)sqlite3_column_double(stmt, 2);
            float pz = (float)sqlite3_column_double(stmt, 3);
            int cause = sqlite3_column_int(stmt, 4);
            const char* killer = (const char*)sqlite3_column_text(stmt, 5);
            int gameDay = sqlite3_column_int(stmt, 6);

            buf.WriteString(name ? name : "");
            buf.WriteString(species ? species : "");
            buf.WriteFloat(px);
            buf.WriteFloat(pz);
            buf.WriteByte(static_cast<std::byte>(cause < 9 ? cause : 0));
            buf.WriteString(killer ? killer : "");
            buf.WriteI32(gameDay);
            ++count;
        }
        sqlite3_finalize(stmt);
    }

    // Patch count at the start
    auto* data = const_cast<std::byte*>(buf.GetData());
    data[0] = static_cast<std::byte>(count & 0xFF);
    data[1] = static_cast<std::byte>(count >> 8);

    connection->SendMessage(MSG_DEATH_LOG_RESULT, false, false, buf);
    LogMessage("[DeathLog] Sent " + String(count) + " entries to client");
#endif
}

void AuthServer::HandleQueryDeathAnalytics(Connection* connection, MemoryBuffer& /*msg*/)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!worldDB_)
        return;

    auto sit = sessions_.Find(connection);
    if (sit == sessions_.End() || !sit->second_.authenticated)
        return;

    sqlite3* db = worldDB_->GetHandle();
    if (!db)
        return;

    // Wire format: 3 sections back-to-back.
    //   Section 1 — deaths by cause:  u8 count, then count × (u8 cause, u16 total)
    //   Section 2 — deaths by species: u8 count, then count × (string species, u16 total)
    //   Section 3 — deaths per game day (last 30 days): u8 count, then count × (i32 day, u16 deaths)
    VectorBuffer buf;
    sqlite3_stmt* stmt = nullptr;

    // Section 1: deaths by cause
    unsigned char byCauseCount = 0;
    unsigned byCausePos = buf.GetSize();
    buf.WriteU8(0);  // placeholder
    if (sqlite3_prepare_v2(db,
        "SELECT cause, COUNT(*) FROM death_log GROUP BY cause ORDER BY COUNT(*) DESC",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW && byCauseCount < 20)
        {
            buf.WriteU8((unsigned char)sqlite3_column_int(stmt, 0));
            buf.WriteU16((unsigned short)Min(sqlite3_column_int(stmt, 1), 65535));
            ++byCauseCount;
        }
        sqlite3_finalize(stmt);
    }
    const_cast<std::byte*>(buf.GetData())[byCausePos] = static_cast<std::byte>(byCauseCount);

    // Section 2: deaths by species (top 15)
    unsigned char bySpeciesCount = 0;
    unsigned bySpeciesPos = buf.GetSize();
    buf.WriteU8(0);  // placeholder
    stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT species, COUNT(*) FROM death_log GROUP BY species ORDER BY COUNT(*) DESC LIMIT 15",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW && bySpeciesCount < 15)
        {
            const char* sp = (const char*)sqlite3_column_text(stmt, 0);
            buf.WriteString(sp ? sp : "");
            buf.WriteU16((unsigned short)Min(sqlite3_column_int(stmt, 1), 65535));
            ++bySpeciesCount;
        }
        sqlite3_finalize(stmt);
    }
    const_cast<std::byte*>(buf.GetData())[bySpeciesPos] = static_cast<std::byte>(bySpeciesCount);

    // Section 3: deaths per game day (last 30 days)
    unsigned char byDayCount = 0;
    unsigned byDayPos = buf.GetSize();
    buf.WriteU8(0);  // placeholder
    stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT game_day, COUNT(*) FROM death_log "
        "WHERE game_day >= (SELECT MAX(game_day) FROM death_log) - 30 "
        "GROUP BY game_day ORDER BY game_day ASC",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW && byDayCount < 60)
        {
            buf.WriteI32(sqlite3_column_int(stmt, 0));
            buf.WriteU16((unsigned short)Min(sqlite3_column_int(stmt, 1), 65535));
            ++byDayCount;
        }
        sqlite3_finalize(stmt);
    }
    const_cast<std::byte*>(buf.GetData())[byDayPos] = static_cast<std::byte>(byDayCount);

    connection->SendMessage(MSG_DEATH_ANALYTICS_RESULT, false, false, buf);
    LogMessage("[DeathLog] Sent analytics to " + sit->second_.username +
               " (" + String((int)byCauseCount) + " causes, " +
               String((int)bySpeciesCount) + " species, " +
               String((int)byDayCount) + " days)");
#endif
}

// ── Claudette IPC (Unix domain socket) ──────────────────────────────────

void AuthServer::InitIPC()
{
#ifndef _WIN32
    // Ensure directory exists
    mkdir("/tmp/urho_claude", 0755);
    mkdir("/tmp/urho_claude/tty", 0755);

    // Check if another instance owns the socket — try connecting to it.
    // If the connect succeeds, someone else is listening — don't steal it.
    {
        int probe = socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe >= 0)
        {
            struct sockaddr_un probeAddr{};
            probeAddr.sun_family = AF_UNIX;
            strncpy(probeAddr.sun_path, IPC_SOCK_PATH, sizeof(probeAddr.sun_path) - 1);
            if (connect(probe, (struct sockaddr*)&probeAddr, sizeof(probeAddr)) == 0)
            {
                close(probe);
                URHO3D_LOGWARNING("[IPC] Socket already owned by another instance — skipping");
                return;
            }
            close(probe);
        }
        // Connect failed — socket is stale or absent, safe to replace
        unlink(IPC_SOCK_PATH);
    }

    ipcListenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipcListenFd_ < 0)
    {
        URHO3D_LOGWARNING("[IPC] Failed to create socket");
        return;
    }

    // Listen socket is non-blocking (accept returns immediately)
    fcntl(ipcListenFd_, F_SETFL, O_NONBLOCK);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(ipcListenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        URHO3D_LOGWARNING("[IPC] Failed to bind socket");
        close(ipcListenFd_);
        ipcListenFd_ = -1;
        return;
    }

    if (listen(ipcListenFd_, 8) < 0)
    {
        URHO3D_LOGWARNING("[IPC] Failed to listen");
        close(ipcListenFd_);
        unlink(IPC_SOCK_PATH);
        ipcListenFd_ = -1;
        return;
    }

    URHO3D_LOGINFOF("[IPC] Listening on %s", IPC_SOCK_PATH);
#endif
}

void AuthServer::StopIPC()
{
#ifndef _WIN32
    if (ipcListenFd_ >= 0)
    {
        close(ipcListenFd_);
        ipcListenFd_ = -1;
        unlink(IPC_SOCK_PATH);
        URHO3D_LOGINFO("[IPC] Socket closed");
    }
#endif
}

void AuthServer::PollIPC()
{
#ifndef _WIN32
    if (ipcListenFd_ < 0)
        return;

    // Drain all pending connections this frame (not just one)
    for (int handled = 0; handled < 8; ++handled)
    {
        int clientFd = accept(ipcListenFd_, nullptr, nullptr);
        if (clientFd < 0)
            break;

        // Client fd is blocking — poll with short timeout for data arrival
        struct pollfd pfd{};
        pfd.fd = clientFd;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 50);  // 50ms max wait

        if (ready <= 0 || !(pfd.revents & POLLIN))
        {
            close(clientFd);
            continue;
        }

        char buf[4096];
        ssize_t n = read(clientFd, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = '\0';
            if (n > 0 && buf[n - 1] == '\n')
                buf[--n] = '\0';

            String message(buf);
            URHO3D_LOGINFOF("[IPC] Received: %s", message.CString());

            String reply = HandleIPCCommand(message);
            if (!reply.Empty())
            {
                reply += "\n";
                const char* data = reply.CString();
                size_t remaining = reply.Length();
                while (remaining > 0)
                {
                    ssize_t written = write(clientFd, data, remaining);
                    if (written <= 0)
                        break;
                    data += written;
                    remaining -= (size_t)written;
                }
            }
        }

        close(clientFd);
    }
#endif
}

String AuthServer::HandleIPCCommand(const String& message)
{
    // Strip sender prefix if present (e.g. "coder2@12345:status")
    String cmd = message;
    unsigned colonPos = cmd.Find(':');
    String sender;
    if (colonPos != String::NPOS)
    {
        sender = cmd.Substring(0, colonPos);
        cmd = cmd.Substring(colonPos + 1);
    }
    cmd = cmd.Trimmed().ToLower();

    if (cmd == "status")
    {
        int clientCount = (int)sessions_.Size();
        int creatureCount = (int)creatureAI_.Size();
        int season = GetCurrentSeasonIndex();
        const char* seasonNames[] = {"spring", "summer", "autumn", "winter"};
        return String("AuthServer up ") + String((int)uptime_) + "s — " +
            String(clientCount) + " clients, " +
            String(creatureCount) + " creatures, " +
            seasonNames[season] + ", " +
            String(weatherTemperature_, 1) + "C " + weatherCondition_;
    }

    if (cmd == "ping")
        return "pong";

    if (cmd == "population")
    {
        String result;
        if (populationManager_ && populationManager_->IsReady())
        {
            // Count per-species totals from creatureAI_
            HashMap<int, int> counts;
            for (auto it = creatureAI_.Begin(); it != creatureAI_.End(); ++it)
            {
                if (!it->second_.isHuman)
                    counts[it->second_.creatureId]++;
            }
            for (auto it = counts.Begin(); it != counts.End(); ++it)
                result += "species=" + String(it->first_) + " count=" + String(it->second_) + "; ";
        }
        return result.Empty() ? "no population data" : result;
    }

    if (cmd == "entropy")
    {
        return "pool=" + String(entropyPoolSize_) + "/" + String(ENTROPY_POOL_CAPACITY)
               + " source=" + entropySource_ + " rolls=" + String(totalDiceRolls_);
    }

    // Unknown command — echo back
    return "AuthServer heard: " + message;
}

// ============================================================================
// Quantum Random Dice — ANU QRNG + /dev/urandom fallback
// ============================================================================

void AuthServer::FetchQuantumEntropy()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // Don't start a new request while one is in-flight
    if (qrngRequest_ && qrngRequest_->GetState() == HTTP_INITIALIZING)
        return;

    qrngResponseData_.Clear();
    qrngRequest_ = network->MakeHttpRequest("https://qrng.anu.edu.au/API/jsonI.php?length=1024&type=uint16");
    LogMessage("[QRNG] Fetching 1024 quantum random numbers from ANU...");
}

void AuthServer::ProcessQRNGResponse()
{
    if (!qrngRequest_)
        return;

    if (qrngRequest_->GetState() == HTTP_INITIALIZING)
        return;

    if (qrngRequest_->GetState() == HTTP_ERROR)
    {
        LogMessage("[QRNG] Fetch error: " + qrngRequest_->GetError() + " — using /dev/urandom fallback");
        qrngRequest_.Reset();
        if (entropyPoolSize_ < ENTROPY_REFILL_THRESHOLD)
            FillEntropyFromURandom(1024);
        return;
    }

    // Read available data
    while (qrngRequest_->GetAvailableSize() > 0)
        qrngResponseData_ += qrngRequest_->ReadLine() + "\n";

    // Not done reading yet
    if (qrngRequest_->GetState() != HTTP_CLOSED || qrngRequest_->GetAvailableSize() > 0)
        return;

    qrngRequest_.Reset();

    // Parse JSON response: {"type":"uint16","length":1024,"data":[...]}
    if (qrngResponseData_.Empty())
    {
        LogMessage("[QRNG] Empty response — falling back to /dev/urandom");
        if (entropyPoolSize_ < ENTROPY_REFILL_THRESHOLD)
            FillEntropyFromURandom(1024);
        return;
    }

    SharedPtr<JSONFile> json(new JSONFile(context_));
    if (!json->FromString(qrngResponseData_))
    {
        LogMessage("[QRNG] JSON parse error — falling back to /dev/urandom");
        if (entropyPoolSize_ < ENTROPY_REFILL_THRESHOLD)
            FillEntropyFromURandom(1024);
        return;
    }

    const JSONValue& root = json->GetRoot();
    if (!root.Get("success").GetBool())
    {
        LogMessage("[QRNG] API returned success=false — falling back to /dev/urandom");
        if (entropyPoolSize_ < ENTROPY_REFILL_THRESHOLD)
            FillEntropyFromURandom(1024);
        return;
    }

    const JSONArray& data = root.Get("data").GetArray();
    unsigned added = 0;
    for (unsigned i = 0; i < data.Size() && entropyPoolSize_ < ENTROPY_POOL_CAPACITY; ++i)
    {
        unsigned writePos = (entropyPoolHead_ + entropyPoolSize_) % ENTROPY_POOL_CAPACITY;
        entropyPool_[writePos] = (unsigned short)(int)data[i].GetDouble();
        ++entropyPoolSize_;
        ++added;
    }

    entropySource_ = "ANU QRNG";
    LogMessage("[QRNG] Added " + String(added) + " quantum values — pool now " + String(entropyPoolSize_));
}

void AuthServer::FillEntropyFromURandom(unsigned count)
{
    // Clamp to available space
    unsigned space = ENTROPY_POOL_CAPACITY - entropyPoolSize_;
    if (count > space)
        count = space;
    if (count == 0)
        return;

    // Read raw bytes via Urho's CryptoRNG (uses /dev/urandom on Unix, CryptGenRandom on Win)
    unsigned byteCount = count * 2;
    Vector<unsigned char> buf(byteCount);
    if (!CryptoRandomBytes(buf.Buffer(), byteCount))
    {
        URHO3D_LOGWARNING("[QRNG] CryptoRandomBytes failed — entropy pool not refilled");
        return;
    }

    for (unsigned i = 0; i < count; ++i)
    {
        unsigned short val = (unsigned short)(buf[i * 2] | (buf[i * 2 + 1] << 8));
        unsigned writePos = (entropyPoolHead_ + entropyPoolSize_) % ENTROPY_POOL_CAPACITY;
        entropyPool_[writePos] = val;
        ++entropyPoolSize_;
    }

    if (entropySource_ == "none")
        entropySource_ = "/dev/urandom";
}

unsigned short AuthServer::ConsumeEntropy()
{
    if (entropyPoolSize_ == 0)
    {
        // Emergency refill — should be rare
        FillEntropyFromURandom(512);
        if (entropyPoolSize_ == 0)
        {
            URHO3D_LOGERROR("[QRNG] Entropy pool empty and refill failed!");
            return 0;
        }
    }

    unsigned short val = entropyPool_[entropyPoolHead_];
    entropyPoolHead_ = (entropyPoolHead_ + 1) % ENTROPY_POOL_CAPACITY;
    --entropyPoolSize_;
    return val;
}

int AuthServer::DiceRoll(int sides)
{
    if (sides <= 0)
        return 0;
    ++totalDiceRolls_;
    unsigned short raw = ConsumeEntropy();
    return (int)(raw % (unsigned)sides) + 1;
}

int AuthServer::RollDice(int numDice, int sides)
{
    int total = 0;
    for (int i = 0; i < numDice; ++i)
        total += DiceRoll(sides);
    return total;
}

int AuthServer::Roll4d6DropLowest()
{
    int rolls[4];
    int lowest = 999;
    int total = 0;
    for (int i = 0; i < 4; ++i)
    {
        rolls[i] = DiceRoll(6);
        total += rolls[i];
        if (rolls[i] < lowest)
            lowest = rolls[i];
    }
    return total - lowest;
}

// ─── WORLD PHENOMENA (Plan 10) ─────────────────────────────────────────────

void AuthServer::CachePhenomenaRules()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_) return;

    cachedPhenomena_.Clear();

    sqlite3* db = gameDB_->GetHandle();
    if (!db) return;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, condition_type, result_type, visual_hint, "
                      "insight_category, epoch_tier_required FROM phenomena_rules";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        LogMessage("[Phenomena] WARNING: failed to query phenomena_rules");
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        PhenomenonRule rule;
        rule.id = sqlite3_column_int(stmt, 0);
        rule.conditionType = (const char*)sqlite3_column_text(stmt, 1);
        rule.resultType = (const char*)sqlite3_column_text(stmt, 2);
        rule.visualHint = (const char*)sqlite3_column_text(stmt, 3);
        rule.insightCategory = (const char*)sqlite3_column_text(stmt, 4);
        rule.epochTierRequired = sqlite3_column_int(stmt, 5);
        cachedPhenomena_.Push(rule);
    }
    sqlite3_finalize(stmt);

    LogMessage("[Phenomena] Cached " + String(cachedPhenomena_.Size()) + " phenomenon rules");
#endif
}

void AuthServer::PhenomenaTick(float dt)
{
#ifdef URHO3D_DATABASE_SQLITE
    phenomenaTickTimer_ += dt;
    if (phenomenaTickTimer_ < PHENOMENA_TICK_INTERVAL)
        return;
    phenomenaTickTimer_ = 0.0f;

    if (cachedPhenomena_.Empty() || !scene_)
        return;

    auto* terrain = scene_->GetComponent<Terrain>(true);

    // Evaluate each rule
    for (unsigned r = 0; r < cachedPhenomena_.Size(); ++r)
    {
        const PhenomenonRule& rule = cachedPhenomena_[r];

        // ── FIRE_NEAR_ORE: check each LIT campfire against deposit map ──
        if (rule.conditionType == "FIRE_NEAR_ORE")
        {
            if (!depositMap_ || !terrain)
                continue;

            for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
            {
                if (cfIt->second_.state != PIT_LIT && cfIt->second_.state != PIT_EMBERS)
                    continue;

                // Epoch gate: skip if settlement hasn't reached required tier
                if (rule.epochTierRequired > 0 &&
                    GetSettlementEpoch(cfIt->first_) < rule.epochTierRequired)
                    continue;

                const Vector3& firePos = cfIt->second_.position;

                // Sample deposit map in a radius around the fire
                IntVector2 center = terrain->WorldToHeightMap(Vector3(firePos.x_, 0, firePos.z_));
                // Convert world-radius to pixel-radius (approximate)
                float spacing = terrain->GetSpacing().x_;
                int pixelRadius = (spacing > 0.001f) ? (int)(FIRE_ORE_PROXIMITY / spacing) : 5;

                bool oreFound = false;
                for (int dz = -pixelRadius; dz <= pixelRadius && !oreFound; dz += 2)
                {
                    for (int dx = -pixelRadius; dx <= pixelRadius && !oreFound; dx += 2)
                    {
                        int px = center.x_ + dx;
                        int pz = center.y_ + dz;
                        if (px < 0 || px >= depositMapSize_ || pz < 0 || pz >= depositMapSize_)
                            continue;

                        Color c = depositMap_->GetPixel(px, pz);
                        int oreType = (int)(c.g_ * 255.0f + 0.5f);
                        int oreQty = (int)(c.r_ * 255.0f + 0.5f);
                        if (oreType > 0 && oreQty > 0)
                        {
                            oreFound = true;
                            BroadcastPhenomenon(firePos, rule.id);
                        }
                    }
                }
            }
        }

        // ── SEED_ON_FERTILE: check placed crops on highly fertile soil ──
        else if (rule.conditionType == "SEED_ON_FERTILE")
        {
            if (!ecosystem_ || !worldDB_)
                continue;

            Vector<WorldDB::PlacedCropInfo> crops = worldDB_->GetAllPlacedCrops();
            for (unsigned c = 0; c < crops.Size(); ++c)
            {
                // Only trigger for freshly planted (growthStage 0)
                if (crops[c].growthStage != 0)
                    continue;

                // Epoch gate: find nearest campfire to the crop for epoch check
                if (rule.epochTierRequired > 0)
                {
                    unsigned nearestCf = 0;
                    float bestDist = 1e9f;
                    Vector3 cropPos3(crops[c].posX, 0.0f, crops[c].posZ);
                    for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
                    {
                        float d = (cfIt->second_.position - cropPos3).Length();
                        if (d < bestDist) { bestDist = d; nearestCf = cfIt->first_; }
                    }
                    if (GetSettlementEpoch(nearestCf) < rule.epochTierRequired)
                        continue;
                }

                float fertility = ecosystem_->SampleFertility(crops[c].posX, crops[c].posZ);
                unsigned char fertByte = (unsigned char)(fertility * 255.0f);
                if (fertByte >= FERTILE_THRESHOLD)
                {
                    Vector3 cropPos(crops[c].posX, crops[c].posY, crops[c].posZ);
                    BroadcastPhenomenon(cropPos, rule.id);
                }
            }
        }

        // ── CLAY_NEAR_HEAT: check if any LIT campfire is near clay soil ──
        else if (rule.conditionType == "CLAY_NEAR_HEAT")
        {
            if (!ecosystem_)
                continue;

            for (auto cfIt = serverCampfires_.Begin(); cfIt != serverCampfires_.End(); ++cfIt)
            {
                if (cfIt->second_.state != PIT_LIT)
                    continue;

                // Epoch gate: CLAY_NEAR_HEAT requires tier 1 (Bronze Age)
                if (rule.epochTierRequired > 0 &&
                    GetSettlementEpoch(cfIt->first_) < rule.epochTierRequired)
                    continue;

                const Vector3& firePos = cfIt->second_.position;
                // SampleComposition: 255 = clay, 128 = loam, 0 = sand
                unsigned char comp = ecosystem_->SampleComposition(firePos.x_, firePos.z_);
                if (comp >= 200)  // clay-rich soil
                    BroadcastPhenomenon(firePos, rule.id);
            }
        }

        // ── LIGHTNING_STRIKE: triggered by weather + random chance ──
        else if (rule.conditionType == "LIGHTNING_STRIKE")
        {
            // Only during storm conditions with high precipitation
            if (weatherCondition_ != "storm" && weatherCondition_ != "thunderstorm")
                continue;

            // Low probability per tick (30s interval) — roughly once per storm
            if (Random(1.0f) > 0.05f)
                continue;

            // Pick a random world position (terrain bounds)
            if (!terrain)
                continue;

            float halfSize = terrain->GetSpacing().x_ * terrain->GetHeightMap()->GetWidth() * 0.5f;
            float rx = Random(-halfSize, halfSize);
            float rz = Random(-halfSize, halfSize);
            float ry = terrain->GetHeight(Vector3(rx, 0, rz));

            if (ry > AI_WATER_LEVEL)
                BroadcastPhenomenon(Vector3(rx, ry, rz), rule.id);
        }
    }
#endif
}

void AuthServer::BroadcastPhenomenon(const Vector3& pos, int phenomenonType)
{
    // Look up visual hint from cached rules
    String visualHint = "glow";
    for (unsigned i = 0; i < cachedPhenomena_.Size(); ++i)
    {
        if (cachedPhenomena_[i].id == phenomenonType)
        {
            visualHint = cachedPhenomena_[i].visualHint;
            break;
        }
    }

    // Find the single closest human NPC within observation range.
    // Only one NPC gains insight per occurrence — prevents duplicate visuals
    // and models realistic observation (one NPC notices first).
    static constexpr float NPC_OBSERVE_RADIUS = 30.0f;
    unsigned closestSpawnId = 0;
    float closestDist = NPC_OBSERVE_RADIUS + 1.0f;
    for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
    {
        if (!aiIt->second_.isHuman)
            continue;
        float dist = (aiIt->second_.position - pos).Length();
        if (dist < closestDist)
        {
            closestDist = dist;
            closestSpawnId = aiIt->first_;
        }
    }

    // Build message: spawnId (0 = visual-only, no NPC observation),
    // phenomenonType, position, visualHint
    VectorBuffer buf;
    buf.WriteU32(closestSpawnId);
    buf.WriteI32(phenomenonType);
    buf.WriteVector3(pos);
    buf.WriteString(visualHint);

    // Send to all authenticated clients within broadcast radius.
    // One message per client — players see the visual even without a nearby NPC.
    for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
    {
        if (!sIt->second_.authenticated)
            continue;

        auto avIt = serverObjects_.Find(sIt->first_);
        if (avIt == serverObjects_.End() || !avIt->second_)
            continue;
        if ((avIt->second_->GetWorldPosition() - pos).Length() > PHENOMENA_BROADCAST_RADIUS)
            continue;

        sIt->first_->SendMessage(MSG_PHENOMENON, true, true, buf);
    }
}

int AuthServer::GetSettlementEpoch(unsigned campfireId) const
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_)
        return 0;

    // Epoch is determined by critical mass of skills across the settlement,
    // not by a single NPC having a single qualifying skill.
    //
    // For each epoch tier, count total skill levels in epoch-gated skills
    // across all NPCs at this campfire. The settlement reaches epoch N when:
    //   - Total skill levels in tier-N skills >= threshold (scales with population)
    //   - At least one NPC has a tier-N skill at level 3+ (the pioneer)
    //
    // Thresholds per epoch:
    //   Epoch 1 (Bronze):      pioneer at 3+, settlement total >= 5
    //   Epoch 2 (Iron):        pioneer at 4+, settlement total >= 12
    //   Epoch 3 (Steel):       pioneer at 5+, settlement total >= 20
    //   Epoch 4 (Renaissance): pioneer at 6+, settlement total >= 35
    //   Epoch 5 (Modern):      pioneer at 7+, settlement total >= 60

    static const int NUM_EPOCHS = 6;
    static const int PIONEER_LEVEL[] = {0, 3, 4, 5, 6, 7};
    static const int MASS_THRESHOLD[] = {0, 5, 12, 20, 35, 60};

    int maxEpoch = 0;

    // Gather skill totals per epoch tier
    int tierTotals[NUM_EPOCHS] = {};
    int tierPioneer[NUM_EPOCHS] = {};

    for (auto aiIt = creatureAI_.Begin(); aiIt != creatureAI_.End(); ++aiIt)
    {
        if (!aiIt->second_.isHuman || aiIt->second_.campfireId != campfireId)
            continue;

        auto pidIt = npcPlayerIds_.Find(aiIt->second_.spawnId);
        int npcPlayerId = (pidIt != npcPlayerIds_.End()) ? pidIt->second_ : 0;
        if (npcPlayerId <= 0)
            continue;

        const auto& tiers = gameDB_->GetSkillEpochTiers();
        for (auto tIt = tiers.Begin(); tIt != tiers.End(); ++tIt)
        {
            int epoch = tIt->second_;
            if (epoch < 1 || epoch >= NUM_EPOCHS)
                continue;
            int level = gameDB_->GetSkillLevel(npcPlayerId, tIt->first_);
            if (level > 0)
            {
                tierTotals[epoch] += level;
                if (level > tierPioneer[epoch])
                    tierPioneer[epoch] = level;
            }
        }
    }

    // Check each epoch: need both a pioneer AND critical mass
    for (int e = 1; e < NUM_EPOCHS; ++e)
    {
        if (tierPioneer[e] >= PIONEER_LEVEL[e] && tierTotals[e] >= MASS_THRESHOLD[e])
            maxEpoch = e;
        else
            break;  // epochs must be sequential
    }

    return maxEpoch;
#else
    return 0;
#endif
}

bool AuthServer::UpdateSettlementEpoch(unsigned campfireId)
{
    int newEpoch = GetSettlementEpoch(campfireId);
    auto it = settlementEpochs_.Find(campfireId);
    int oldEpoch = (it != settlementEpochs_.End()) ? it->second_ : 0;

    settlementEpochs_[campfireId] = newEpoch;

    if (newEpoch > oldEpoch)
    {
        static const char* epochNames[] = {"Stone Age", "Bronze Age", "Iron Age", "Steel Age", "Renaissance", "Modern Age"};
        const char* name = (newEpoch >= 0 && newEpoch <= 5) ? epochNames[newEpoch] : "Unknown";
        LogMessage("[Epoch] Settlement campfire " + String(campfireId) +
                   " advanced to " + String(name) + " (tier " + String(newEpoch) + ")");
        BroadcastEpochChanged(campfireId, newEpoch);
        return true;
    }
    return false;
}

void AuthServer::BroadcastEpochChanged(unsigned campfireId, int newEpoch)
{
    static const char* epochNames[] = {"Stone Age", "Bronze Age", "Iron Age", "Steel Age", "Renaissance", "Modern Age"};
    const char* name = (newEpoch >= 0 && newEpoch <= 5) ? epochNames[newEpoch] : "Unknown";

    VectorBuffer buf;
    buf.WriteU32(campfireId);
    buf.WriteI32(newEpoch);
    buf.WriteString(String(name));

    for (auto sIt = sessions_.Begin(); sIt != sessions_.End(); ++sIt)
    {
        if (!sIt->second_.authenticated)
            continue;
        sIt->first_->SendMessage(MSG_EPOCH_CHANGED, true, true, buf);
    }
}

