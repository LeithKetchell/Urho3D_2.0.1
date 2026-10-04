// WorkboardManager — GUI dashboard for workboard, plans, and Claude IPC

// Required (before any libc header) so glibc exposes struct ucred / SO_PEERCRED,
// used to capture kernel-verified peer credentials on the relay socket.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "WorkboardManager.h"

#include <Urho3D/Container/Sort.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/KarenClient.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Network/SHA256.h>
#include <Urho3D/Network/ChaCha20Poly1305.h>
#include <Urho3D/Network/HttpRequest.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>

#include "PlatformUtils.h"

#include <SQLite/sqlite3.h>

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <climits>

#ifndef _WIN32
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#else
#include <windows.h>
#endif

URHO3D_DEFINE_APPLICATION_MAIN(WorkboardManager);

// Status bar title colors: reverse ROYGBIV
// Alpha explicitly 1.0 — never use * scalar to dim (it kills alpha)
static const Color COL_VIOLET(0.56f, 0.0f, 1.0f, 1.0f);
static const Color COL_INDIGO(0.29f, 0.0f, 0.82f, 1.0f);
static const Color COL_BLUE(0.0f, 0.47f, 1.0f, 1.0f);
static const Color COL_GREEN(0.0f, 1.0f, 0.0f, 1.0f);
static const Color COL_YELLOW(1.0f, 1.0f, 0.0f, 1.0f);
static const Color COL_ORANGE(1.0f, 0.5f, 0.0f, 1.0f);
static const Color COL_RED(1.0f, 0.0f, 0.0f, 1.0f);

// Dimmed versions for inactive/empty states (half brightness, full alpha)
static const Color COL_VIOLET_DIM(0.28f, 0.0f, 0.5f, 1.0f);
static const Color COL_INDIGO_DIM(0.15f, 0.0f, 0.41f, 1.0f);
static const Color COL_BLUE_DIM(0.0f, 0.24f, 0.5f, 1.0f);
static const Color COL_YELLOW_DIM(0.5f, 0.5f, 0.0f, 1.0f);
static const Color COL_ORANGE_DIM(0.5f, 0.25f, 0.0f, 1.0f);
static const Color COL_RED_DIM(0.5f, 0.0f, 0.0f, 1.0f);

// ============================================================================
// Application lifecycle
// ============================================================================

WorkboardManager::WorkboardManager(Context* context) : WorkboardBase(context), yukiLLM_(context) {}

void WorkboardManager::Setup()
{
    // Resolve project root early — needed for IPC directory hashing
    projectRoot_ = GetProjectRoot();

    // Derive project-specific IPC directory using StringHash (SDBM)
    // Must match the SDBM hash in .claude/hooks/ipc_dir.sh
    {
        StringHash projHash(projectRoot_.CString());
        char hashBuf[16];
        snprintf(hashBuf, sizeof(hashBuf), "%08x", projHash.Value());
        auto* setupFs = GetSubsystem<FileSystem>();
        String tempDir = setupFs ? setupFs->GetTemporaryDir() : "/tmp/";
        ipcDir_ = tempDir + "claude_" + String(hashBuf) + "/";
        ttySockDir_ = ipcDir_ + "tty/";

        if (setupFs)
        {
            setupFs->CreateDir(ipcDir_);
            setupFs->CreateDir(ipcDir_ + "instances/");
            setupFs->CreateDir(ttySockDir_);
        }
    }

    // ── Singleton guard ──
    {
#ifndef _WIN32
        // Linux: abstract socket with project-specific name.
        // bind() is atomic, namespace is kernel-managed (no filesystem
        // file to accidentally delete), and the socket auto-closes
        // when the process dies.
        singletonLockFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (singletonLockFd_ >= 0)
        {
            // Include IPC dir hash in abstract name so different projects
            // can each run their own Manager instance.
            StringHash projHash(projectRoot_.CString());
            char hashBuf[16];
            snprintf(hashBuf, sizeof(hashBuf), "%08x", projHash.Value());
            String abstractStr = String("\0claude_wbm_", 11) + String(hashBuf);

            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            memcpy(addr.sun_path, abstractStr.CString(), abstractStr.Length());
            socklen_t addrLen = offsetof(struct sockaddr_un, sun_path) + abstractStr.Length();

            if (bind(singletonLockFd_, (struct sockaddr*)&addr, addrLen) != 0)
            {
                close(singletonLockFd_);
                singletonLockFd_ = -1;
                URHO3D_LOGERROR("WorkboardManager already running (singleton socket bound). Exiting.");
                exitCode_ = EXIT_FAILURE;
                return;
            }
            listen(singletonLockFd_, 1);
        }
        else
        {
            URHO3D_LOGERROR("Failed to create singleton socket. Exiting.");
            exitCode_ = EXIT_FAILURE;
            return;
        }
#else
        // Windows: named mutex with project-specific name.
        StringHash projHash(projectRoot_.CString());
        char hashBuf[16];
        snprintf(hashBuf, sizeof(hashBuf), "%08x", projHash.Value());
        String mutexName = String("Global\\claude_wbm_") + String(hashBuf);
        singletonMutex_ = CreateMutexA(nullptr, TRUE, mutexName.CString());
        if (!singletonMutex_ || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (singletonMutex_)
            {
                ReleaseMutex(singletonMutex_);
                CloseHandle(singletonMutex_);
                singletonMutex_ = nullptr;
            }
            URHO3D_LOGERROR("WorkboardManager already running (singleton mutex held). Exiting.");
            exitCode_ = EXIT_FAILURE;
            return;
        }
#endif

    }

    engineParameters_[EP_WINDOW_TITLE] = String("Workboard Manager v") + WORKBOARD_MANAGER_VERSION;
    engineParameters_[EP_WINDOW_ICON] = "Icons/WorkboardManager.png";
    engineParameters_[EP_WINDOW_WIDTH] = 1600;
    engineParameters_[EP_WINDOW_HEIGHT] = 800;
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_LOG_NAME] = "WorkboardManager.log";
    engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
    engineParameters_[EP_SOUND] = false;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
}

void WorkboardManager::Start()
{
    IgnoreSigPipe();

    // Cap present rate — this is a static dashboard, not a game
    engine_->SetMaxFps(1);
    engine_->SetMaxInactiveFps(1);

    // ipcDir_, ttySockDir_, projectRoot_ already set in Setup()

    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    uiRoot->SetDefaultStyle(style);

    // Claudette-style warm dark background
    // Warm near-black — slightly warmer than pure black, less purple than Claudette's VTE palette
    GetSubsystem<Renderer>()->GetDefaultZone()->SetFogColor(Color(0.08f, 0.08f, 0.09f));

    // Scan available fonts
    auto* fs = GetSubsystem<FileSystem>();
    Vector<String> fontFiles;
    fs->ScanDir(fontFiles, projectRoot_ + "bin/Data/Fonts/", "*.ttf", SCAN_FILES, false);
    for (unsigned i = 0; i < fontFiles.Size(); ++i)
    {
        String name = fontFiles[i].Substring(0, fontFiles[i].FindLast('.'));
        // Skip SDF companion files
        if (!name.Empty())
            availableFonts_.Push(name);
    }
    Urho3D::Sort(availableFonts_.Begin(), availableFonts_.End());

    LoadThemePrefs();
    font_ = cache->GetResource<Font>("Fonts/" + currentFontName_ + ".ttf");
    if (!font_)
        font_ = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    fontSize_ = currentFontSize_;  // sync base class font size

    CreateUI();

    // Open SQL backing — stored in project .claude/ dir (survives reboots)
    {
        String dbPath = projectRoot_ + ".claude/workboard.db";
        String schemaPath = projectRoot_ + "bin/Data/Workboard/workboard_schema.sql";
        if (workboardDB_.Open(dbPath, schemaPath))
        {
            // Check if DB is empty — if so, bootstrap from markdown
            Vector<WorkboardSection> dbSections = workboardDB_.LoadAllSections();
            bool dbEmpty = true;
            for (unsigned i = 0; i < dbSections.Size(); ++i)
            {
                if (!dbSections[i].rows.Empty())
                { dbEmpty = false; break; }
            }
            if (dbEmpty)
                AppendLog("System", "Workboard DB is empty — add tasks via IPC or Manager UI");

            // Boot cleanup — prune stale shared memories
            workboardDB_.PruneStaleMemories(7);
        }
    }

    LoadWorkboard();
    ScanPlanFiles();
    CreateIPCPaths();
    CleanupLegacyPIDFiles();  // Remove leftover coder .pid/.role/.heartbeat files
    UpdateCoderCapText();  // Write initial cap file
    StartRelaySocket();
    RefreshInstanceStatus();

    // Notify any surviving instances that Manager is back online.
    // Deliver once per unique PID — dedup by process, not socket path.
    {
        const String onlineMsg = "=== WORKBOARD MANAGER ONLINE === The WorkboardManager is back. "
            "Message delivery restored. Resume normal operations.";
        HashSet<String> delivered;

        auto deliverOnce = [&](const String& role)
        {
            if (!IsInstanceAlive(role) || delivered.Contains(role))
                return;
            delivered.Insert(role);
            SendToSocket(role, onlineMsg);
        };

        for (const String& role : knownCoderRoles_)
            deliverOnce(role);

        for (const String& role : knownUnassignedRoles_)
            deliverOnce(role);
    }

    // Parse --secret from command line for workboard sync auth
    {
        const Vector<String>& args = GetArguments();
        for (unsigned i = 0; i < args.Size(); ++i)
        {
            if (args[i] == "--secret" && i + 1 < args.Size())
            {
                wbSecret_ = args[i + 1];
                break;
            }
        }
    }

    // Load PAKE secret from external file (4096 raw bytes, rotated by cron)
    {
        static constexpr unsigned PAKE_KEY_SIZE = 4096;
        String keyPath = "/etc/urho3d/pake.key";
        auto* fs = GetSubsystem<FileSystem>();
        if (fs && fs->FileExists(keyPath))
        {
            File keyFile(context_, keyPath, FILE_READ);
            if (keyFile.IsOpen() && keyFile.GetSize() >= PAKE_KEY_SIZE)
            {
                Vector<unsigned char> raw(PAKE_KEY_SIZE);
                keyFile.Read(raw.Buffer(), PAKE_KEY_SIZE);
                SHA256Hash(raw.Buffer(), PAKE_KEY_SIZE, pakeSecretHash_);
                pakeSecretValid_ = true;
                URHO3D_LOGINFO("PAKE secret loaded from file (4096 bytes, SHA-256 hashed)");
            }
        }
        else if (!wbSecret_.Empty())
        {
            SHA256Hash(reinterpret_cast<const unsigned char*>(wbSecret_.CString()),
                       wbSecret_.Length(), pakeSecretHash_);
            pakeSecretValid_ = true;
            URHO3D_LOGINFO("PAKE secret hash computed from --secret (SHA-256)");
        }
        else
        {
            URHO3D_LOGINFO("No PAKE key — LAN-only mode (no WAN auth)");
        }
    }

    // Start beacon server on UDP 31337
    auto* network = GetSubsystem<Network>();
    if (network)
    {
        network->StartServer(BEACON_PORT);
        UpdateBeacon();
        AppendLog("System", "Beacon active on UDP port " + String(BEACON_PORT));

        // Start listening for Karen telemetry emitters on the LAN (client peer).
        StartKarenListener();

        // Register workboard remote events and subscribe to connection lifecycle
        RegisterWorkboardRemoteEvents();
        SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(WorkboardManager, HandleClientConnected));
        SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(WorkboardManager, HandleClientDisconnected));
        SubscribeToEvent(E_CLIENTIDENTITY, URHO3D_HANDLER(WorkboardManager, HandleClientIdentity));
        // PAKE authentication events
        SubscribeToEvent(E_KEYEXCHANGEAUTH, URHO3D_HANDLER(WorkboardManager, HandleKeyExchangeAuth));
        SubscribeToEvent(E_CLIENTAUTHENTICATED, URHO3D_HANDLER(WorkboardManager, HandleClientAuthenticated));
        // Client → Server remote events
        SubscribeToEvent(E_WB_REQUEST_PLAN, URHO3D_HANDLER(WorkboardManager, HandleWbRequestPlan));
        SubscribeToEvent(E_WB_MUTATION, URHO3D_HANDLER(WorkboardManager, HandleWbMutation));
        SubscribeToEvent(E_WB_SET_IDENTITY, URHO3D_HANDLER(WorkboardManager, HandleWbSetIdentity));
        SubscribeToEvent(E_WB_INSTANCE_STATUS, URHO3D_HANDLER(WorkboardManager, HandleWbInstanceStatus));

        if (!wbSecret_.Empty())
            AppendLog("System", "Workboard sync ready (LAN open, WAN PAKE auth enabled)");
        else
            AppendLog("System", "Workboard sync ready (LAN open, WAN blocked — use --secret to allow WAN)");
    }

    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(WorkboardManager, HandleUpdate));
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(WorkboardManager, HandleKeyDown));
    SubscribeToEvent(E_SCREENMODE, URHO3D_HANDLER(WorkboardManager, HandleScreenMode));

    GetSubsystem<Input>()->SetMouseVisible(true);
    GetSubsystem<Input>()->SetMouseGrabbed(false);

    AppendLog("System", "WorkboardManager started. Project root: " + projectRoot_);

    // ── Embedded Yuki: prepare wiring, but do NOT load model automatically ──
    // Model is unstable and runs hot — Leith loads it manually when needed.
    {
        String dbPath = projectRoot_ + "/bin/Data/GameDB/yuki_memory.db";
        if (yukiMemoryDB_.Open(dbPath))
        {
            yukiMemoryDB_.RecoverStaleTraining();
            yukiMemoryDB_.PruneConsumed(30);
        }

        yukiLLM_.SetProjectRoot(projectRoot_ + "/");
        yukiLLM_.SetIpcDir(ipcDir_);
        yukiLLM_.SetMemoryDB(&yukiMemoryDB_);
        AppendLog("Yuki", "Ready (model not loaded — use UI to activate)");
    }
}

void WorkboardManager::Stop()
{
    // Broadcast shutdown notice to all live instances via TTY injection
    const String shutdownMsg = "=== WORKBOARD MANAGER SHUTTING DOWN === The WorkboardManager is temporarily offline. "
        "Continue your current task. TTY injection will resume when Manager restarts. "
        "Do NOT attempt to send messages to Manager until you receive a 'Manager back online' notice.";

    // Deliver once per role via socket.
    {
        HashSet<String> delivered;

        auto deliverOnce = [&](const String& role)
        {
            if (!IsInstanceAlive(role) || delivered.Contains(role))
                return;
            delivered.Insert(role);
            SendToSocket(role, shutdownMsg);
        };

        for (const String& role : knownCoderRoles_)
            deliverOnce(role);

        for (const String& role : knownUnassignedRoles_)
            deliverOnce(role);
    }

    // Notify remote workboard clients of graceful shutdown via the dedicated
    // event. Phase 2c: replaces the earlier shoehorn through MUTATION_ACK,
    // which clients couldn't distinguish from a regular mutation failure.
    for (auto it = wbClients_.Begin(); it != wbClients_.End(); ++it)
    {
        if (it->second_.authenticated_ && it->first_)
        {
            VariantMap data;
            data[WbServerShutdown::P_REASON] = String("Server shutting down");
            it->first_->SendRemoteEvent(E_WB_SERVER_SHUTDOWN, true, data);
            it->first_->Disconnect();
        }
    }
    wbClients_.Clear();

    auto* network = GetSubsystem<Network>();
    if (network)
        network->StopServer();

    StopRelaySocket();

    // Shut down embedded Yuki
    yukiLLM_.UnloadModel();
    // Final WAL flush + truncate while we still hold the connection. At full
    // shutdown the other side is typically gone or idle, so the -wal zeroes here
    // and the on-disk .db is left fully self-contained.
    if (yukiMemoryDB_.IsOpen())
        yukiMemoryDB_.Checkpoint(true);  // TRUNCATE
    yukiMemoryDB_.Close();

    // Release singleton lock
#ifndef _WIN32
    if (singletonLockFd_ >= 0)
    {
        close(singletonLockFd_);
        singletonLockFd_ = -1;
    }
#else
    if (singletonMutex_)
    {
        ReleaseMutex(singletonMutex_);
        CloseHandle(singletonMutex_);
        singletonMutex_ = nullptr;
    }
#endif
}

#ifdef _WIN32
String WorkboardManager::PipeName(const String& role) const
{
    return "\\\\.\\pipe\\urho_claude_" + role;
}
#endif

// ============================================================================
// Helpers
// ============================================================================

// GetProjectRoot() and GetClaudeDir() are in WorkboardBase

String WorkboardManager::FindYukiModel()
{
    auto* fs = GetSubsystem<FileSystem>();
    String modelsDir = projectRoot_ + "/Source/Tools/YukiHoho/models/";

    // DeepSeek family only — Qwen models can't learn
    const char* candidates[] = {
        "deepseek-r1-7b-q4_k_m.gguf",
        "deepcoder-1.5b.gguf",
        "deepcoder-planner.gguf",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i)
    {
        String path = modelsDir + candidates[i];
        if (fs->FileExists(path))
            return path;
    }

    // Fallback: first .gguf in the directory
    Vector<String> files;
    fs->ScanDir(files, modelsDir, "*.gguf", SCAN_FILES, false);
    if (!files.Empty())
        return modelsDir + files[0];

    return String::EMPTY;
}

// ============================================================================
// UI Creation
// ============================================================================

void WorkboardManager::CreateUI()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();

    // UV-based layout — all panels expressed as fractions of window size.
    // Anchors recalculate automatically on resize. No pixel math needed.
    // All gaps use the same pad value — no overlap.
    //
    //   Row 0:  Status bar    0.000 – 0.100  (3 rows: instances, stats, buttons)
    //   Row 1:  Content       0.103 – 0.630  (workboard left, plans+yuki right)
    //   Row 2:  Composer      0.633 – 0.673
    //   Row 3:  Message log   0.676 – 0.997

    const float pad = 0.003f;  // ~4px at 1280

    const float row0Top = 0.0f;
    const float row0Bot = 0.130f;  // 3 rows: dropdowns+controls, stats header, stats x2
    const float row1Top = row0Bot + pad;
    const float row1Bot = 0.615f;
    const float row2Top = row1Bot + pad;
    const float row2Bot = 0.695f;   // Composer needs ~56px for two 24px rows + padding (~64px at 800)
    const float row3Top = row2Bot + pad;
    const float row3Bot = 1.0f - pad;

    const float midX = 0.5f;  // left/right split
    const float rightSplit = 0.40f;  // plans/yuki split within right half

    // ── Instance status bar (top, full width) ──
    CreateInstanceStatusBar(uiRoot, pad, row0Top, 1.0f - pad, row0Bot);

    // ── Workboard window (left half) ──
    CreateWorkboardPanel(uiRoot, pad, row1Top, midX - pad, row1Bot);

    // ── Plans window (right half, upper) ──
    CreatePlanPanel(uiRoot, midX + pad, row1Top, 1.0f - pad, rightSplit);

    // ── Yuki chat panel (right half, lower) ──
    CreateYukiChatPanel(uiRoot, midX + pad, rightSplit + pad, 1.0f - pad, row1Bot);

    // ── Composer bar (full width) ──
    CreateComposer(uiRoot, pad, row2Top, 1.0f - pad, row2Bot);

    // ── Message log (full width, fills bottom) ──
    CreateMessageLog(uiRoot, pad, row3Top, 1.0f - pad, row3Bot);

    // ── Popups (hidden, toggled from status bar buttons) ──
    CreateToolsPopup();
    CreateSettingsPopup();
}

void WorkboardManager::CreateInstanceStatusBar(UIElement* parent, float minX, float minY, float maxX, float maxY)
{
    statusBar_ = parent->CreateChild<Window>("StatusBar");
    auto* bar = statusBar_;
    bar->SetStyle("Window");
    bar->SetOpacity(0.6f);
    bar->SetEnableAnchor(true);
    bar->SetMinAnchor(minX, minY);
    bar->SetMaxAnchor(maxX, maxY);
    bar->SetMovable(false);
    bar->SetResizable(false);
    bar->SetLayout(LM_VERTICAL, 4, IntRect(6, 4, 6, 4));
    bar->SetMinHeight(40);

    // ── Row 1: Controls + Instance dropdowns + build status ──
    auto* row1 = bar->CreateChild<UIElement>("StatusRow1");
    row1->SetLayout(LM_HORIZONTAL, 6);
    row1->SetFixedHeight(22);

    toolsBtn_ = row1->CreateChild<Button>("ToolsBtn");
    toolsBtn_->SetStyleAuto();
    toolsBtn_->SetLayout(LM_HORIZONTAL, 4, IntRect(8, 2, 8, 2));
    toolsBtn_->SetFixedHeight(20);
    toolsBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    auto* toolsBtnText = toolsBtn_->CreateChild<Text>();
    toolsBtnText->SetFont(font_, currentFontSize_);
    toolsBtnText->SetText("Tools");
    SubscribeToEvent(toolsBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleToolsToggle));

    settingsBtn_ = row1->CreateChild<Button>("SettingsBtn");
    settingsBtn_->SetStyleAuto();
    settingsBtn_->SetLayout(LM_HORIZONTAL, 4, IntRect(8, 2, 8, 2));
    settingsBtn_->SetFixedHeight(20);
    settingsBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    auto* settingsBtnText = settingsBtn_->CreateChild<Text>();
    settingsBtnText->SetFont(font_, currentFontSize_);
    settingsBtnText->SetText("Settings");
    SubscribeToEvent(settingsBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleSettingsToggle));

    localsDropdown_ = row1->CreateChild<DropDownList>("RolesDropdown");
    localsDropdown_->SetStyleAuto();
    localsDropdown_->SetResizePopup(true);
    localsDropdown_->SetMinSize(120, 20);
    localsDropdown_->SetLayoutFlexScale(Vector2(2.0f, 0.0f));

    remotesDropdown_ = row1->CreateChild<DropDownList>("RemotesDropdown");
    remotesDropdown_->SetStyleAuto();
    remotesDropdown_->SetResizePopup(true);
    remotesDropdown_->SetMinSize(100, 20);
    remotesDropdown_->SetLayoutFlexScale(Vector2(1.5f, 0.0f));

    unassignedStatusDropdown_ = row1->CreateChild<DropDownList>("UnassignedDropdown");
    unassignedStatusDropdown_->SetStyleAuto();
    unassignedStatusDropdown_->SetResizePopup(true);
    unassignedStatusDropdown_->SetMinSize(100, 20);
    unassignedStatusDropdown_->SetLayoutFlexScale(Vector2(1.5f, 0.0f));

    buildStatusText_ = row1->CreateChild<Text>("BuildStatus");
    buildStatusText_->SetFont(font_, currentFontSize_);
    buildStatusText_->SetText("");
    buildStatusText_->SetColor(COL_GREEN);
    buildStatusText_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));

    // ── Stats section (collapsible, platform-dependent) ──
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
    auto* statsHeader = bar->CreateChild<Button>("StatsHeader");
    statsHeader->SetStyleAuto();
    statsHeader->SetFixedHeight(16);
    statsHeader->SetClipChildren(true);
    auto* statsHeaderText = statsHeader->CreateChild<Text>();
    statsHeaderText->SetFont(font_, currentFontSize_ - 1);
    statsHeaderText->SetText("v Stats");
    statsHeaderText->SetAlignment(HA_LEFT, VA_CENTER);
    SubscribeToEvent(statsHeader, "Released", URHO3D_HANDLER(WorkboardManager, HandleToggleStats));

    statsContainer_ = bar->CreateChild<UIElement>("StatsContainer");
    statsContainer_->SetLayout(LM_VERTICAL, 2);

    auto* row2a = statsContainer_->CreateChild<UIElement>("StatusRow2a");
    row2a->SetLayout(LM_HORIZONTAL, 8);
    row2a->SetFixedHeight(20);
    row2a->SetClipChildren(true);

#ifdef __linux__
    CreateStatCell(row2a, "Cpu", cpuText_, cpuBar_, Color(0.2f, 0.6f, 1.0f, 0.35f), 90, 20);
    CreateStatCell(row2a, "Gpu", gpuText_, gpuBar_, Color(0.2f, 0.8f, 0.3f, 0.35f), 90, 20);
    CreateStatCell(row2a, "Ram", ramText_, ramBar_, Color(0.9f, 0.6f, 0.1f, 0.35f), 180, 20);
    // Swap — only shown if the OS has swap configured (checked at runtime)
    CreateStatCell(row2a, "Swap", swapText_, swapBar_, Color(0.7f, 0.3f, 0.9f, 0.35f), 0, 20);
#elif defined(_WIN32)
    CreateStatCell(row2a, "Cpu", cpuText_, cpuBar_, Color(0.2f, 0.6f, 1.0f, 0.35f), 90, 20);
    CreateStatCell(row2a, "Gpu", gpuText_, gpuBar_, Color(0.2f, 0.8f, 0.3f, 0.35f), 90, 20);
    CreateStatCell(row2a, "Ram", ramText_, ramBar_, Color(0.9f, 0.6f, 0.1f, 0.35f), 180, 20);
    // No swap cell on Windows — pagefile stats require different API
#elif defined(__APPLE__)
    CreateStatCell(row2a, "Cpu", cpuText_, cpuBar_, Color(0.2f, 0.6f, 1.0f, 0.35f), 90, 20);
    CreateStatCell(row2a, "Ram", ramText_, ramBar_, Color(0.9f, 0.6f, 0.1f, 0.35f), 180, 20);
    // No GPU utilization or swap on macOS without IOKit
#endif

    auto* row2b = statsContainer_->CreateChild<UIElement>("StatusRow2b");
    row2b->SetLayout(LM_HORIZONTAL, 8);
    row2b->SetFixedHeight(18);
    row2b->SetClipChildren(true);

#ifdef __linux__
    CreateStatCell(row2b, "Disk", diskText_, diskBar_, Color(0.9f, 0.3f, 0.3f, 0.35f), 0, 18);

    diskReadText_ = row2b->CreateChild<Text>("DiskRead");
    diskReadText_->SetFont(font_, currentFontSize_);
    diskReadText_->SetText("R: --");
    diskReadText_->SetColor(Color(0.5f, 0.9f, 0.5f));
    diskReadText_->SetVerticalAlignment(VA_CENTER);

    diskWriteText_ = row2b->CreateChild<Text>("DiskWrite");
    diskWriteText_->SetFont(font_, currentFontSize_);
    diskWriteText_->SetText("W: --");
    diskWriteText_->SetColor(Color(0.9f, 0.5f, 0.5f));
    diskWriteText_->SetVerticalAlignment(VA_CENTER);
#endif

    auto* diskSpacer = row2b->CreateChild<UIElement>("DiskSpacer");
    diskSpacer->SetLayoutFlexScale(Vector2(1.0f, 0.0f));

    yukiCpuText_ = row2b->CreateChild<Text>("YukiCpu");
    yukiCpuText_->SetFont(font_, currentFontSize_);
    yukiCpuText_->SetText("Yuki: --");
    yukiCpuText_->SetColor(Color(1.0f, 0.5f, 0.8f));
    yukiCpuText_->SetVerticalAlignment(VA_CENTER);
#endif // platform stats

}

// CreateWorkboardPanel() is in WorkboardBase

// CreatePlanPanel() is in WorkboardBase

UIElement* WorkboardManager::CreateStatCell(UIElement* parent, const String& name, Text*& textOut,
                                            BorderImage*& barOut, const Color& barColor, int minW, int fixedH)
{
    auto* cell = parent->CreateChild<UIElement>(name + "Cell");
    cell->SetLayout(LM_FREE);
    cell->SetFixedHeight(fixedH);
    if (minW > 0)
        cell->SetMinWidth(minW);
    cell->SetClipChildren(true);

    // Bar fill — anchor-based so it scales with cell width automatically
    barOut = cell->CreateChild<BorderImage>(name + "Bar");
    barOut->SetColor(barColor);
    barOut->SetEnableAnchor(true);
    barOut->SetMinAnchor(0.0f, 0.0f);
    barOut->SetMaxAnchor(0.0f, 1.0f);  // starts empty
    barOut->SetPriority(0);

    // Text label — rendered on top
    textOut = cell->CreateChild<Text>(name + "Status");
    textOut->SetFont(font_, currentFontSize_);
    textOut->SetText(name + ": --");
    textOut->SetColor(COL_YELLOW);
    textOut->SetVerticalAlignment(VA_CENTER);
    textOut->SetPriority(1);

    return cell;
}

void WorkboardManager::SetBarPercent(BorderImage* bar, UIElement* /*cell*/, int pct)
{
    if (!bar)
        return;
    pct = Clamp(pct, 0, 100);
    bar->SetMaxAnchor(pct / 100.0f, 1.0f);
}

void WorkboardManager::CreateComposer(UIElement* parent, float minX, float minY, float maxX, float maxY)
{
    auto* bar = parent->CreateChild<BorderImage>("ComposerBar");
    bar->SetStyle("Window");
    bar->SetOpacity(0.6f);
    bar->SetEnableAnchor(true);
    bar->SetMinAnchor(minX, minY);
    bar->SetMaxAnchor(maxX, maxY);
    bar->SetLayout(LM_VERTICAL, 2, IntRect(4, 2, 4, 2));

    // ── Row 1: Message input + receiver + send ──
    auto* row1 = bar->CreateChild<UIElement>("ComposerRow1");
    row1->SetLayout(LM_HORIZONTAL, 4);
    row1->SetFixedHeight(24);

    // Message input
    messageInput_ = row1->CreateChild<LineEdit>("MsgInput");
    messageInput_->SetStyle("LineEdit");
    messageInput_->SetMinSize(100, 24);
    messageInput_->SetLayoutFlexScale(Vector2(3.0f, 0.0f));
    messageInput_->SetVerticalAlignment(VA_CENTER);

    // Receiver dropdown
    coderDropdown_ = row1->CreateChild<DropDownList>("ReceiverDropdown");
    coderDropdown_->SetStyleAuto();
    coderDropdown_->SetMinSize(80, 24);
    coderDropdown_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    coderDropdown_->SetResizePopup(true);
    coderDropdown_->SetVerticalAlignment(VA_CENTER);

    // Send
    sendCoderBtn_ = row1->CreateChild<Button>("SendBtn");
    sendCoderBtn_->SetStyleAuto();
    sendCoderBtn_->SetMinSize(60, 24);
    sendCoderBtn_->SetMaxWidth(140);
    sendCoderBtn_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    sendCoderBtn_->SetVerticalAlignment(VA_CENTER);
    sendCoderBtn_->SetClipChildren(true);
    auto* cl = sendCoderBtn_->CreateChild<Text>();
    cl->SetFont(font_, currentFontSize_ - 1);
    cl->SetText("Send");
    cl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(sendCoderBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleSendCoder));

    // ── Row 2: Action buttons ──
    auto* row2 = bar->CreateChild<UIElement>("ComposerRow2");
    row2->SetLayout(LM_HORIZONTAL, 4);
    row2->SetFixedHeight(24);

    // Clear Locks
    clearFileLocksBtn_ = row2->CreateChild<Button>("ClearLocks");
    clearFileLocksBtn_->SetStyleAuto();
    clearFileLocksBtn_->SetMinSize(80, 24);
    clearFileLocksBtn_->SetMaxWidth(160);
    clearFileLocksBtn_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    clearFileLocksBtn_->SetVerticalAlignment(VA_CENTER);
    clearFileLocksBtn_->SetClipChildren(true);
    auto* cfl = clearFileLocksBtn_->CreateChild<Text>();
    cfl->SetFont(font_, currentFontSize_ - 1);
    cfl->SetText("Break Locks");
    cfl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(clearFileLocksBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleClearFileLocks));

    // Spawn
    spawnCoderBtn_ = row2->CreateChild<Button>("SpawnCoder");
    spawnCoderBtn_->SetStyleAuto();
    spawnCoderBtn_->SetMinSize(60, 24);
    spawnCoderBtn_->SetMaxWidth(140);
    spawnCoderBtn_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    spawnCoderBtn_->SetVerticalAlignment(VA_CENTER);
    spawnCoderBtn_->SetClipChildren(true);
    auto* sc = spawnCoderBtn_->CreateChild<Text>();
    sc->SetFont(font_, currentFontSize_ - 1);
    sc->SetText("Spawn");
    sc->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(spawnCoderBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleSpawnCoder));

    // Launch Yuki
    launchYukiBtn_ = row2->CreateChild<Button>("LaunchYuki");
    launchYukiBtn_->SetStyleAuto();
    launchYukiBtn_->SetMinSize(60, 24);
    launchYukiBtn_->SetMaxWidth(100);
    launchYukiBtn_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    launchYukiBtn_->SetVerticalAlignment(VA_CENTER);
    launchYukiBtn_->SetClipChildren(true);
    auto* ykText = launchYukiBtn_->CreateChild<Text>();
    ykText->SetFont(font_, currentFontSize_ - 1);
    ykText->SetText("Yuki");
    ykText->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(launchYukiBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleLaunchYuki));

    // Coder cap controls: [−] count/max [+]
    coderCapMinusBtn_ = row2->CreateChild<Button>("CoderCapMinus");
    coderCapMinusBtn_->SetStyleAuto();
    coderCapMinusBtn_->SetFixedSize(24, 24);
    coderCapMinusBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    coderCapMinusBtn_->SetVerticalAlignment(VA_CENTER);
    coderCapMinusBtn_->SetClipChildren(true);
    auto* capM = coderCapMinusBtn_->CreateChild<Text>();
    capM->SetFont(font_, currentFontSize_);
    capM->SetText("-");
    capM->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(coderCapMinusBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleCoderCapMinus));

    coderCapText_ = row2->CreateChild<Text>("CoderCapText");
    coderCapText_->SetFont(font_, currentFontSize_ - 1);
    coderCapText_->SetFixedWidth(30);
    coderCapText_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    coderCapText_->SetAlignment(HA_CENTER, VA_CENTER);
    UpdateCoderCapText();

    coderCapPlusBtn_ = row2->CreateChild<Button>("CoderCapPlus");
    coderCapPlusBtn_->SetStyleAuto();
    coderCapPlusBtn_->SetFixedSize(24, 24);
    coderCapPlusBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    coderCapPlusBtn_->SetVerticalAlignment(VA_CENTER);
    coderCapPlusBtn_->SetClipChildren(true);
    auto* capP = coderCapPlusBtn_->CreateChild<Text>();
    capP->SetFont(font_, currentFontSize_);
    capP->SetText("+");
    capP->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(coderCapPlusBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleCoderCapPlus));

    // Screenshot toggle — blocks all Claude instances from taking screenshots
    screenshotToggleBtn_ = row2->CreateChild<Button>("ScreenshotToggle");
    screenshotToggleBtn_->SetStyleAuto();
    screenshotToggleBtn_->SetMinSize(80, 24);
    screenshotToggleBtn_->SetMaxWidth(160);
    screenshotToggleBtn_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    screenshotToggleBtn_->SetVerticalAlignment(VA_CENTER);
    screenshotToggleBtn_->SetClipChildren(true);
    auto* ssText = screenshotToggleBtn_->CreateChild<Text>();
    ssText->SetFont(font_, currentFontSize_ - 1);
    // Check initial state
    // Default: screenshots blocked. Create flag file if absent.
    String ssFlag = ipcDir_ + "screenshots_blocked";
    if (!GetSubsystem<FileSystem>()->FileExists(ssFlag))
    {
        File flagFile(context_, ssFlag, FILE_WRITE);
        if (flagFile.IsOpen())
            flagFile.WriteLine("blocked");
    }
    screenshotsBlocked_ = GetSubsystem<FileSystem>()->FileExists(ssFlag);
    ssText->SetText(screenshotsBlocked_ ? "Snoop: OFF" : "Snoop: ON");
    ssText->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(screenshotToggleBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleToggleScreenshots));
}

void WorkboardManager::CreateToolsPopup()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();

    toolsPopup_ = uiRoot->CreateChild<Window>("ToolsPopup");
    toolsPopup_->SetStyle("Window");
    toolsPopup_->SetColor(Color(0.4f, 0.5f, 0.6f));  // Light border color
    toolsPopup_->SetOpacity(0.80f);
    toolsPopup_->SetEnableAnchor(true);
    toolsPopup_->SetMinAnchor(0.005f, 0.135f);
    toolsPopup_->SetMaxAnchor(0.995f, 0.21f);
    toolsPopup_->SetMovable(false);
    toolsPopup_->SetResizable(false);
    toolsPopup_->SetLayout(LM_VERTICAL, 0, IntRect(2, 2, 2, 2));
    toolsPopup_->SetVisible(false);
    toolsPopup_->SetBringToFront(true);
    toolsPopup_->SetPriority(200);

    // Inner panel — dark content area, border gap = visible frame
    auto* toolsInner = toolsPopup_->CreateChild<BorderImage>("ToolsInner");
    toolsInner->SetStyle("Window");
    toolsInner->SetColor(Color(0.10f, 0.09f, 0.15f));
    toolsInner->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));
    toolsInner->SetLayoutFlexScale(Vector2(1.0f, 1.0f));

    // ── Download row ──
    auto* dlRow = toolsInner->CreateChild<UIElement>("DLRow");
    dlRow->SetLayout(LM_HORIZONTAL, 4);
    dlRow->SetFixedHeight(28);

    auto* dlLabel = dlRow->CreateChild<Text>();
    dlLabel->SetFont(font_, currentFontSize_);
    dlLabel->SetText("curl:");
    dlLabel->SetColor(Color(0.7f, 0.7f, 0.7f));
    dlLabel->SetVerticalAlignment(VA_CENTER);

    downloadUrlInput_ = dlRow->CreateChild<LineEdit>("DLUrl");
    downloadUrlInput_->SetStyleAuto();
    downloadUrlInput_->SetMinWidth(100);
    downloadUrlInput_->SetMinHeight(24);
    downloadUrlInput_->SetLayoutFlexScale(Vector2(4.0f, 0.0f));

    downloadBtn_ = dlRow->CreateChild<Button>("DLBtn");
    downloadBtn_->SetStyleAuto();
    downloadBtn_->SetLayout(LM_HORIZONTAL, 4, IntRect(8, 2, 8, 2));
    downloadBtn_->SetFixedHeight(24);
    downloadBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    auto* btnText = downloadBtn_->CreateChild<Text>();
    btnText->SetFont(font_, currentFontSize_);
    btnText->SetText("Download");
    SubscribeToEvent(downloadBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleDownload));

    downloadStatusText_ = dlRow->CreateChild<Text>("DLStatus");
    downloadStatusText_->SetFont(font_, currentFontSize_ - 1);
    downloadStatusText_->SetText("");
    downloadStatusText_->SetColor(Color(0.5f, 0.8f, 0.5f));
    downloadStatusText_->SetAlignment(HA_RIGHT, VA_CENTER);
}

void WorkboardManager::HandleToggleStats(StringHash, VariantMap&)
{
    if (!statsContainer_)
        return;
    statsCollapsed_ = !statsCollapsed_;
    statsContainer_->SetVisible(!statsCollapsed_);

    auto* btn = statsContainer_->GetParent()->GetChild("StatsHeader", false);
    if (btn)
    {
        auto* text = btn->GetChildStaticCast<Text>(0);
        if (text)
            text->SetText(statsCollapsed_ ? "> Stats" : "v Stats");
    }
    RelayoutPanels();
}

void WorkboardManager::HandleToggleControls(StringHash, VariantMap&)
{
    // Controls section removed — buttons now live in row 1
}

void WorkboardManager::RelayoutPanels()
{
    // Recompute status bar bottom anchor based on collapsed state.
    const float pad = 0.005f;
    const float fullRow0Bot = 0.130f;  // Smaller: no Controls section
    const float statsHeight = 0.055f;  // ~44px: row2a(20) + row2b(18) + spacing

    float row0Bot = fullRow0Bot;
    if (statsCollapsed_)
        row0Bot -= statsHeight;

    const float row1Top = row0Bot + pad;
    const float row1Bot = 0.615f;
    const float row2Top = row1Bot + pad;
    const float row2Bot = 0.695f;
    const float row3Top = row2Bot + pad;
    const float row3Bot = 1.0f - pad;
    const float midX = 0.5f;
    const float rightSplit = 0.40f;

    URHO3D_LOGINFOF("[Relayout] stats=%s controls=%s row0Bot=%.3f row1Top=%.3f",
        statsCollapsed_ ? "collapsed" : "expanded",
        controlsCollapsed_ ? "collapsed" : "expanded",
        row0Bot, row1Top);

    // Resize status bar
    if (statusBar_)
        statusBar_->SetMaxAnchor(1.0f - pad, row0Bot);

    // Reanchor all panels below
    if (workboardPanel_)
    {
        workboardPanel_->SetMinAnchor(pad, row1Top);
        workboardPanel_->SetMaxAnchor(midX - pad, row1Bot);
    }
    else
        URHO3D_LOGERROR("[Relayout] workboardPanel_ is null");

    if (planPanel_)
    {
        planPanel_->SetMinAnchor(midX + pad, row1Top);
        planPanel_->SetMaxAnchor(1.0f - pad, rightSplit);
    }
    else
        URHO3D_LOGERROR("[Relayout] planPanel_ is null");

    if (yukiChatPanel_)
    {
        yukiChatPanel_->SetMinAnchor(midX + pad, rightSplit + pad);
        yukiChatPanel_->SetMaxAnchor(1.0f - pad, row1Bot);
    }
    else
        URHO3D_LOGERROR("[Relayout] yukiChatPanel_ is null");

    // Composer bar
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    auto* composer = uiRoot->GetChild("ComposerBar", false);
    if (composer)
    {
        composer->SetEnableAnchor(true);
        composer->SetMinAnchor(pad, row2Top);
        composer->SetMaxAnchor(1.0f - pad, row2Bot);
    }
    else
        URHO3D_LOGERROR("[Relayout] ComposerBar not found");

    // Log panel
    if (logPanel_)
    {
        logPanel_->SetMinAnchor(pad, row3Top);
        logPanel_->SetMaxAnchor(1.0f - pad, row3Bot);
    }
    else
        URHO3D_LOGERROR("[Relayout] logPanel_ is null");
}

void WorkboardManager::HandleToolsToggle(StringHash, VariantMap&)
{
    if (toolsPopup_)
    {
        toolsPopup_->SetVisible(!toolsPopup_->IsVisible());
        if (settingsPopup_ && settingsPopup_->IsVisible())
            settingsPopup_->SetVisible(false);
    }
}

void WorkboardManager::CreateSettingsPopup()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();

    settingsPopup_ = uiRoot->CreateChild<Window>("SettingsPopup");
    settingsPopup_->SetStyle("Window");
    settingsPopup_->SetColor(Color(0.4f, 0.5f, 0.6f));  // Light border color
    settingsPopup_->SetOpacity(0.80f);
    settingsPopup_->SetEnableAnchor(true);
    settingsPopup_->SetMinAnchor(0.005f, 0.135f);
    settingsPopup_->SetMaxAnchor(0.995f, 0.21f);
    settingsPopup_->SetMovable(false);
    settingsPopup_->SetResizable(false);
    settingsPopup_->SetLayout(LM_VERTICAL, 0, IntRect(2, 2, 2, 2));
    settingsPopup_->SetVisible(false);
    settingsPopup_->SetBringToFront(true);
    settingsPopup_->SetPriority(200);

    // Inner panel — dark content area
    auto* settingsInner = settingsPopup_->CreateChild<BorderImage>("SettingsInner");
    settingsInner->SetStyle("Window");
    settingsInner->SetColor(Color(0.10f, 0.09f, 0.15f));
    settingsInner->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));
    settingsInner->SetLayoutFlexScale(Vector2(1.0f, 1.0f));

    // ── Reset World row ──
    auto* resetRow = settingsInner->CreateChild<UIElement>("ResetRow");
    resetRow->SetLayout(LM_HORIZONTAL, 8);
    resetRow->SetFixedHeight(28);

    resetWorldBtn_ = resetRow->CreateChild<Button>("ResetWorldBtn");
    resetWorldBtn_->SetStyleAuto();
    resetWorldBtn_->SetLayout(LM_HORIZONTAL, 4, IntRect(8, 2, 8, 2));
    resetWorldBtn_->SetFixedHeight(24);
    resetWorldBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    auto* resetBtnText = resetWorldBtn_->CreateChild<Text>();
    resetBtnText->SetFont(font_, currentFontSize_);
    resetBtnText->SetText("Reset World (Danger!)");
    SubscribeToEvent(resetWorldBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleResetWorld));

    resetWorldStatus_ = resetRow->CreateChild<Text>("ResetStatus");
    resetWorldStatus_->SetFont(font_, currentFontSize_ - 1);
    resetWorldStatus_->SetText("Deletes game_world.db — next run recreates from schema");
    resetWorldStatus_->SetColor(Color(0.5f, 0.5f, 0.5f));
    resetWorldStatus_->SetVerticalAlignment(VA_CENTER);

    // ── Theme row ──
    auto* themeRow = settingsInner->CreateChild<UIElement>("ThemeRow");
    themeRow->SetLayout(LM_HORIZONTAL, 8);
    themeRow->SetFixedHeight(28);

    auto* fontLabel = themeRow->CreateChild<Text>();
    fontLabel->SetFont(font_, currentFontSize_);
    fontLabel->SetText("Font:");
    fontLabel->SetColor(Color(0.7f, 0.7f, 0.7f));
    fontLabel->SetVerticalAlignment(VA_CENTER);

    fontSelector_ = themeRow->CreateChild<DropDownList>();
    fontSelector_->SetStyle("DropDownList");
    fontSelector_->SetMinSize(100, 22);
    fontSelector_->SetLayoutFlexScale(Vector2(2.0f, 0.0f));
    fontSelector_->SetResizePopup(true);

    int selectedIdx = 0;
    for (unsigned i = 0; i < availableFonts_.Size(); ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, currentFontSize_ + 1);
        item->SetText(availableFonts_[i]);
        item->SetColor(Color(0.85f, 0.85f, 0.85f));
        item->SetMinSize(200, 20);
        fontSelector_->AddItem(item);
        if (availableFonts_[i] == currentFontName_)
            selectedIdx = i;
    }
    fontSelector_->SetSelection(selectedIdx);
    SubscribeToEvent(fontSelector_, E_ITEMSELECTED, URHO3D_HANDLER(WorkboardManager, HandleFontSelected));

    auto* sizeLabel = themeRow->CreateChild<Text>();
    sizeLabel->SetFont(font_, currentFontSize_);
    sizeLabel->SetText("Size:");
    sizeLabel->SetColor(Color(0.7f, 0.7f, 0.7f));
    sizeLabel->SetVerticalAlignment(VA_CENTER);

    fontSizeSelector_ = themeRow->CreateChild<DropDownList>();
    fontSizeSelector_->SetStyle("DropDownList");
    fontSizeSelector_->SetMinSize(50, 22);
    fontSizeSelector_->SetMaxWidth(100);
    fontSizeSelector_->SetLayoutFlexScale(Vector2(1.0f, 0.0f));
    fontSizeSelector_->SetResizePopup(true);

    int sizes[] = {9, 10, 11, 12, 13, 14, 16, 18};
    int sizeSelectedIdx = 2;
    for (int i = 0; i < 8; ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, currentFontSize_ + 1);
        item->SetText(String(sizes[i]));
        item->SetColor(Color(0.85f, 0.85f, 0.85f));
        item->SetMinSize(60, 20);
        fontSizeSelector_->AddItem(item);
        if (sizes[i] == currentFontSize_)
            sizeSelectedIdx = i;
    }
    fontSizeSelector_->SetSelection(sizeSelectedIdx);
    SubscribeToEvent(fontSizeSelector_, E_ITEMSELECTED, URHO3D_HANDLER(WorkboardManager, HandleFontSizeChanged));
}

void WorkboardManager::HandleSettingsToggle(StringHash, VariantMap&)
{
    if (settingsPopup_)
    {
        settingsPopup_->SetVisible(!settingsPopup_->IsVisible());
        if (toolsPopup_ && toolsPopup_->IsVisible())
            toolsPopup_->SetVisible(false);
    }
}

void WorkboardManager::HandleDownload(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (downloadInProgress_)
    {
        AppendLog("Download", "Download already in progress");
        return;
    }

    String url = downloadUrlInput_->GetText().Trimmed();
    if (url.Empty())
    {
        AppendLog("Download", "No URL entered");
        return;
    }

    // Derive filename from URL
    String filename = url;
    unsigned lastSlash = filename.FindLast('/');
    if (lastSlash != String::NPOS)
        filename = filename.Substring(lastSlash + 1);
    // Strip query params
    unsigned qmark = filename.Find('?');
    if (qmark != String::NPOS)
        filename = filename.Substring(0, qmark);
    if (filename.Empty())
        filename = "download";

    // Download to project root's downloads/ dir
    String downloadDir = projectRoot_ + "/downloads";
    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->DirExists(downloadDir))
        fs->CreateDir(downloadDir);

    downloadOutputPath_ = downloadDir + "/" + filename;

    // Run curl in background via Urho3D async process
    {
        auto* fs = GetSubsystem<FileSystem>();
        Vector<String> args;
        args.Push("-L");
        args.Push("-o");
        args.Push(downloadOutputPath_);
        args.Push(url);
        curlRequestId_ = fs->SystemRunAsync("curl", args);
    }

    downloadInProgress_ = (curlRequestId_ > 0);
    downloadCheckTimer_ = 0.0f;
    if (downloadStatusText_)
    {
        downloadStatusText_->SetText("Downloading...");
        downloadStatusText_->SetColor(Color(1.0f, 0.9f, 0.3f));
    }
    AppendLog("Download", "Started: " + url + " → " + downloadOutputPath_);
}

void WorkboardManager::CheckDownloadProgress()
{
    if (!downloadInProgress_)
        return;

    // Throttle checks to once per second — no need to hammer the filesystem every frame
    downloadCheckTimer_ += GetSubsystem<Engine>()->GetNextTimeStep();
    if (downloadCheckTimer_ < 1.0f)
        return;
    downloadCheckTimer_ = 0.0f;

    auto* fs = GetSubsystem<FileSystem>();

    // Non-blocking check: if output file doesn't exist yet, curl is still running
    if (curlRequestId_ > 0)
    {
        if (!fs->FileExists(downloadOutputPath_))
            return;  // still running
        curlRequestId_ = 0;
    }

    // curl finished (or was never tracked) — check result
    {
        // curl finished
        downloadInProgress_ = false;

        if (fs->FileExists(downloadOutputPath_))
        {
            // Get file size
            File f(context_, downloadOutputPath_);
            unsigned size = f.GetSize();
            f.Close();

            String sizeStr;
            if (size > 1048576)
                sizeStr = String((int)(size / 1048576)) + " MB";
            else if (size > 1024)
                sizeStr = String((int)(size / 1024)) + " KB";
            else
                sizeStr = String(size) + " bytes";

            if (downloadStatusText_)
            {
                downloadStatusText_->SetText("Done: " + sizeStr);
                downloadStatusText_->SetColor(Color(0.3f, 1.0f, 0.5f));
            }
            AppendLog("Download", "Complete: " + downloadOutputPath_ + " (" + sizeStr + ")");
        }
        else
        {
            if (downloadStatusText_)
            {
                downloadStatusText_->SetText("Failed");
                downloadStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
            }
            AppendLog("Download", "Failed — file not created. Check /tmp/urho_curl.log");
        }
    }
}

// ============================================================================
// Reset World
// ============================================================================

void WorkboardManager::HandleResetWorld(StringHash, VariantMap&)
{
    auto* fs = GetSubsystem<FileSystem>();
    String dbPath = projectRoot_ + "bin/Data/GameDB/game_world.db";
    String texturesDir = projectRoot_ + "bin/Data/Textures/";

    if (!fs->FileExists(dbPath))
    {
        if (resetWorldStatus_)
        {
            resetWorldStatus_->SetText("No game_world.db found");
            resetWorldStatus_->SetColor(Color(1.0f, 0.6f, 0.2f));
        }
        AppendLog("Reset", "game_world.db not found — nothing to delete");
        return;
    }

    // Restore backed-up map BLOBs before deleting the database
    int mapsRestored = 0;
    {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(dbPath.CString(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK)
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT filename, data FROM map_backups", -1, &stmt, nullptr) == SQLITE_OK)
            {
                while (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    const char* filename = (const char*)sqlite3_column_text(stmt, 0);
                    const void* blob = sqlite3_column_blob(stmt, 1);
                    int blobSize = sqlite3_column_bytes(stmt, 1);

                    if (filename && blob && blobSize > 0)
                    {
                        String outPath = texturesDir + String(filename);
                        File outFile(context_, outPath, FILE_WRITE);
                        if (outFile.IsOpen())
                        {
                            outFile.Write(blob, blobSize);
                            outFile.Close();
                            ++mapsRestored;
                            AppendLog("Reset", "Restored map: " + String(filename) + " (" + String(blobSize) + " bytes)");
                        }
                        else
                            AppendLog("Reset", "Failed to write: " + outPath);
                    }
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }
    }

    // Delete the database and WAL/SHM companions
    bool ok = fs->Delete(dbPath);
    fs->Delete(dbPath + "-shm");
    fs->Delete(dbPath + "-wal");

    if (ok)
    {
        String msg = "World reset";
        if (mapsRestored > 0)
            msg += " — " + String(mapsRestored) + " map(s) restored";
        msg += " — next run creates fresh DB";

        if (resetWorldStatus_)
        {
            resetWorldStatus_->SetText(msg);
            resetWorldStatus_->SetColor(Color(0.3f, 1.0f, 0.3f));
        }
        AppendLog("Reset", "game_world.db deleted — world will regenerate on next run");
    }
    else
    {
        if (resetWorldStatus_)
        {
            resetWorldStatus_->SetText("Failed to delete game_world.db");
            resetWorldStatus_->SetColor(Color(1.0f, 0.3f, 0.3f));
        }
        AppendLog("Reset", "Failed to delete game_world.db — file may be locked");
    }
}

// ============================================================================
// Theme
// ============================================================================

void WorkboardManager::HandleFontSelected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ItemSelected;
    unsigned sel = eventData[P_SELECTION].GetU32();
    if (sel < availableFonts_.Size())
    {
        currentFontName_ = availableFonts_[sel];
        ApplyFont(currentFontName_, currentFontSize_);
        SaveThemePrefs();
    }
}

void WorkboardManager::HandleFontSizeChanged(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ItemSelected;
    unsigned sel = eventData[P_SELECTION].GetU32();
    int sizes[] = {9, 10, 11, 12, 13, 14, 16, 18};
    if (sel < 8)
    {
        currentFontSize_ = sizes[sel];
        ApplyFont(currentFontName_, currentFontSize_);
        SaveThemePrefs();
    }
}

static void UpdateFontsRecursive(UIElement* element, Font* font, int oldBase, int newBase);

void WorkboardManager::ApplyFont(const String& fontName, int fontSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* newFont = cache->GetResource<Font>("Fonts/" + fontName + ".ttf");
    if (!newFont)
    {
        AppendLog("System", "Font not found: " + fontName);
        return;
    }
    int oldBase = currentFontSize_;
    font_ = newFont;
    currentFontName_ = fontName;
    currentFontSize_ = fontSize;

    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    UpdateFontsRecursive(uiRoot, font_, oldBase, currentFontSize_);
    // Update ListView items explicitly (they may not be in direct child tree)
    if (logListView_)
    {
        for (unsigned i = 0; i < logListView_->GetNumItems(); ++i)
        {
            auto* item = dynamic_cast<Text*>(logListView_->GetItem(i));
            if (item)
                item->SetFont(font_, currentFontSize_ - 1);
        }
    }
    if (planListView_)
    {
        for (unsigned i = 0; i < planListView_->GetNumItems(); ++i)
        {
            auto* item = dynamic_cast<Text*>(planListView_->GetItem(i));
            if (item)
                item->SetFont(font_, currentFontSize_);
        }
    }

    SaveThemePrefs();
    // Force workboard rebuild with new font
    RenderWorkboardUI();
    AppendLog("System", "Font changed to " + fontName + " " + String(fontSize) + "pt");
}

static void UpdateFontsRecursive(UIElement* element, Font* font, int oldBase, int newBase)
{
    auto* text = dynamic_cast<Text*>(element);
    if (text && text->GetFont())
    {
        int oldSize = text->GetFontSize();
        int offset = oldSize - oldBase;
        int newSize = newBase + offset;
        if (newSize < 7) newSize = 7;
        text->SetFont(font, newSize);
    }
    for (unsigned i = 0; i < element->GetNumChildren(); ++i)
        UpdateFontsRecursive(element->GetChild(i), font, oldBase, newBase);
}

void WorkboardManager::RebuildAllUI()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    UpdateFontsRecursive(uiRoot, font_, currentFontSize_, currentFontSize_);
}

void WorkboardManager::LoadThemePrefs()
{
    String path = projectRoot_ + "bin/Data/UI/theme.json";
    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->FileExists(path))
        return;

    File file(context_, path, FILE_READ);
    if (!file.IsOpen())
        return;

    unsigned size = file.GetSize();
    String content;
    content.Resize(size);
    file.Read(&content[0], size);
    file.Close();

    // Simple key:value parsing (no JSON lib needed)
    Vector<String> lines = content.Split('\n');
    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        String line = lines[i].Trimmed();
        if (line.StartsWith("\"font\""))
        {
            unsigned colon = line.Find(':');
            if (colon != String::NPOS)
            {
                String val = line.Substring(colon + 1).Trimmed();
                val.Replace("\"", "");
                val.Replace(",", "");
                if (!val.Empty())
                    currentFontName_ = val.Trimmed();
            }
        }
        else if (line.StartsWith("\"fontSize\""))
        {
            unsigned colon = line.Find(':');
            if (colon != String::NPOS)
            {
                String val = line.Substring(colon + 1).Trimmed();
                val.Replace(",", "");
                int sz = atoi(val.Trimmed().CString());
                if (sz >= 8 && sz <= 24)
                    currentFontSize_ = sz;
            }
        }
    }
}

void WorkboardManager::SaveThemePrefs()
{
    String path = projectRoot_ + "bin/Data/UI/theme.json";

    // Ensure directory exists
    auto* fs = GetSubsystem<FileSystem>();
    String dir = path.Substring(0, path.FindLast('/'));
    if (!fs->DirExists(dir))
        fs->CreateDir(dir);

    File file(context_, path, FILE_WRITE);
    if (!file.IsOpen())
        return;

    String json = "{\n";
    json += "  \"font\": \"" + currentFontName_ + "\",\n";
    json += "  \"fontSize\": " + String(currentFontSize_) + "\n";
    json += "}\n";

    file.Write(json.CString(), json.Length());
    file.Close();
}

void WorkboardManager::CreateMessageLog(UIElement* parent, float minX, float minY, float maxX, float maxY)
{
    logPanel_ = parent->CreateChild<Window>("LogPanel");
    logPanel_->SetStyle("Window");
    logPanel_->SetOpacity(0.6f);
    logPanel_->SetEnableAnchor(true);
    logPanel_->SetMinAnchor(minX, minY);
    logPanel_->SetMaxAnchor(maxX, maxY);
    logPanel_->SetMovable(false);
    logPanel_->SetResizable(false);
    logPanel_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));

    // Clickable title for collapse/expand
    auto* titleBtn = logPanel_->CreateChild<Button>("LogTitleBtn");
    titleBtn->SetStyleAuto();
    titleBtn->SetFixedHeight(22);
    titleBtn->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    titleBtn->SetOpacity(0.0f);  // Invisible button, text shows through
    logTitleText_ = titleBtn->CreateChild<Text>("LogTitle");
    logTitleText_->SetFont(font_, currentFontSize_ + 1);
    logTitleText_->SetText("[-] MESSAGE LOG");
    logTitleText_->SetColor(Color(0.6f, 0.6f, 0.6f));
    logTitleText_->SetAlignment(HA_LEFT, VA_CENTER);
    SubscribeToEvent(titleBtn, "Released", URHO3D_HANDLER(WorkboardManager, HandleLogTitleClick));

    // Copy button — pushes the visible log to the clipboard so it can be selected/pasted
    // elsewhere. Sits at the right of the title bar; its own hit-area takes the click, so the
    // rest of the bar still collapses/expands. Purely additive — no existing log logic touched.
    auto* copyBtn = titleBtn->CreateChild<Button>("LogCopyBtn");
    copyBtn->SetStyleAuto();
    copyBtn->SetFixedSize(52, 18);
    copyBtn->SetAlignment(HA_RIGHT, VA_CENTER);
    copyBtn->SetPosition(-2, 0);
    auto* copyTxt = copyBtn->CreateChild<Text>("LogCopyTxt");
    copyTxt->SetFont(font_, currentFontSize_ - 1);
    copyTxt->SetText("Copy");
    copyTxt->SetColor(Color(0.8f, 0.8f, 0.8f));
    copyTxt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(copyBtn, "Released", URHO3D_HANDLER(WorkboardManager, HandleLogCopy));

    logListView_ = logPanel_->CreateChild<ListView>("LogList");
    logListView_->SetStyleAuto();
    logListView_->SetLayoutFlexScale(Vector2(1.0f, 1.0f));  // Fill remaining space
}

void WorkboardManager::CreateYukiChatPanel(UIElement* parent, float minX, float minY, float maxX, float maxY)
{
    yukiChatPanel_ = parent->CreateChild<Window>("YukiChat");
    yukiChatPanel_->SetStyle("Window");
    yukiChatPanel_->SetOpacity(0.6f);
    yukiChatPanel_->SetEnableAnchor(true);
    yukiChatPanel_->SetMinAnchor(minX, minY);
    yukiChatPanel_->SetMaxAnchor(maxX, maxY);
    yukiChatPanel_->SetMovable(false);
    yukiChatPanel_->SetResizable(false);
    yukiChatPanel_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));

    // Title row with load/unload button
    auto* titleRow = yukiChatPanel_->CreateChild<UIElement>("YukiTitleRow");
    titleRow->SetLayout(LM_HORIZONTAL, 6);
    titleRow->SetFixedHeight(24);

    auto* title = titleRow->CreateChild<Text>("YukiTitle");
    title->SetFont(font_, currentFontSize_ + 1);
    title->SetText("YUKI");
    title->SetColor(Color(1.0f, 0.5f, 0.8f));
    title->SetVerticalAlignment(VA_CENTER);
    title->SetLayoutFlexScale(Vector2(0.0f, 0.0f));

    yukiToggleBtn_ = titleRow->CreateChild<Button>("YukiToggle");
    yukiToggleBtn_->SetStyleAuto();
    yukiToggleBtn_->SetLayout(LM_HORIZONTAL, 4, IntRect(8, 2, 8, 2));
    yukiToggleBtn_->SetFixedHeight(22);
    yukiToggleBtn_->SetVerticalAlignment(VA_CENTER);
    yukiToggleBtn_->SetLayoutFlexScale(Vector2(0.0f, 0.0f));
    yukiToggleBtnText_ = yukiToggleBtn_->CreateChild<Text>();
    yukiToggleBtnText_->SetFont(font_, currentFontSize_ - 1);
    yukiToggleBtnText_->SetText("Load AI");
    SubscribeToEvent(yukiToggleBtn_, "Released", URHO3D_HANDLER(WorkboardManager, HandleToggleYuki));

    // Spacer absorbs remaining width so title+button stay packed left
    auto* titleSpacer = titleRow->CreateChild<UIElement>("TitleSpacer");
    titleSpacer->SetLayoutFlexScale(Vector2(1.0f, 0.0f));

    yukiChatLog_ = yukiChatPanel_->CreateChild<ListView>("YukiLog");
    yukiChatLog_->SetStyleAuto();
    yukiChatLog_->SetMinHeight(60);
    yukiChatLog_->SetScrollBarsVisible(false, true);
}

void WorkboardManager::AppendYukiChat(const String& sender, const String& message)
{
    if (!yukiChatLog_) return;

    auto* item = new Text(context_);
    item->SetFont(font_, currentFontSize_ - 1);
    item->SetWordwrap(true);

    // Determine max width for word wrap — use panel width if available, fallback to 300
    int maxW = 300;
    if (yukiChatPanel_)
    {
        int w = yukiChatPanel_->GetWidth();
        if (w > 40)
            maxW = w - 20;
    }
    item->SetMaxWidth(maxW);

    if (sender == "Yuki" || sender == "yuki")
    {
        item->SetText("Yuki: " + message);
        item->SetColor(Color(1.0f, 0.5f, 0.8f));
    }
    else
    {
        item->SetText(sender + ": " + message);
        item->SetColor(Color(0.9f, 0.9f, 0.9f));
    }

    yukiChatLog_->AddItem(item);

    while (yukiChatLog_->GetNumItems() > 200)
        yukiChatLog_->RemoveItem((i32)0);

    yukiChatLog_->EnsureItemVisibility(yukiChatLog_->GetNumItems() - 1);
}

void WorkboardManager::HandleLogTitleClick(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    logCollapsed_ = !logCollapsed_;
    if (logListView_)
        logListView_->SetVisible(!logCollapsed_);
    if (logTitleText_)
        logTitleText_->SetText(String(logCollapsed_ ? "[+]" : "[-]") + " MESSAGE LOG");
}

void WorkboardManager::HandleLogCopy(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (!logListView_)
        return;

    // Concatenate every visible log line (each item is a Text added by AppendLog) and push it
    // to the system clipboard — copies exactly what's on screen, ready to paste.
    String out;
    for (unsigned i = 0; i < (unsigned)logListView_->GetNumItems(); ++i)
    {
        UIElement* el = logListView_->GetItem(i);
        if (el && el->GetType() == Text::GetTypeStatic())
            out += static_cast<Text*>(el)->GetText() + "\n";
    }

    GetSubsystem<UI>()->SetClipboardText(out);
    AppendLog("System", "Message log copied to clipboard (" + String(out.Length()) + " chars)");
}

// ============================================================================
// Workboard Loading & Parsing
// ============================================================================

void WorkboardManager::LoadWorkboard()
{
    if (!workboardDB_.IsOpen())
        return;

    sections_ = workboardDB_.LoadAllSections();
    RenderWorkboardUI();
}

// ParseWorkboard(), RenderWorkboardUI(), AddSectionToUI(), HandleSectionToggle()
// are all in WorkboardBase.

// HandlePlanSelected() is in WorkboardBase — calls virtual OnPlanSelected().
void WorkboardManager::OnPlanSelected(const String& filename)
{
    LoadPlanContent(filename);
}

// (ParseWorkboard, RenderWorkboardUI, AddSectionToUI, HandleSectionToggle
//  removed — now in WorkboardBase)

// ============================================================================
// Workboard Mutations (Manager is single authority)
// ============================================================================

WorkboardSection* WorkboardManager::FindSection(const String& keyword)
{
    for (unsigned i = 0; i < sections_.Size(); ++i)
    {
        if (sections_[i].title.Contains(keyword))
            return &sections_[i];
    }
    return nullptr;
}

void WorkboardManager::AddReadyRow(const Vector<String>& fields)
{
    WorkboardSection* sec = FindSection("Ready");
    if (!sec) { AppendLog("System", "WB: Ready section not found"); return; }

    WorkboardRow row;
    for (unsigned i = 0; i < fields.Size(); ++i)
        row.cells.Push(fields[i].Trimmed());
    sec->rows.Push(row);
    AppendLog("System", "WB: Added to Ready: " + fields[1].Trimmed());
}

void WorkboardManager::AddInProgressRow(const Vector<String>& fields)
{
    WorkboardSection* sec = FindSection("In Progress");
    if (!sec) { AppendLog("System", "WB: In Progress section not found"); return; }

    WorkboardRow row;
    for (unsigned i = 0; i < fields.Size(); ++i)
        row.cells.Push(fields[i].Trimmed());
    sec->rows.Push(row);
    AppendLog("System", "WB: Added to In Progress: " + fields[0].Trimmed());
}

void WorkboardManager::AddDoneRow(const Vector<String>& fields)
{
    WorkboardSection* sec = FindSection("Done");
    if (!sec) { AppendLog("System", "WB: Done section not found"); return; }

    WorkboardRow row;
    for (unsigned i = 0; i < fields.Size(); ++i)
        row.cells.Push(fields[i].Trimmed());
    sec->rows.Push(row);
    AppendLog("System", "WB: Added to Done: " + fields[0].Trimmed());
}

void WorkboardManager::MoveToDone(const String& taskName)
{
    WorkboardSection* inProg = FindSection("In Progress");
    WorkboardSection* done = FindSection("Done");
    if (!inProg || !done) { AppendLog("System", "WB: Section not found for move-done"); return; }

    for (unsigned i = 0; i < inProg->rows.Size(); ++i)
    {
        if (inProg->rows[i].cells.Size() > 0 && inProg->rows[i].cells[0].Contains(taskName))
        {
            WorkboardRow moved = inProg->rows[i];
            inProg->rows.Erase(i);
            done->rows.Push(moved);
            AppendLog("System", "WB: Moved to Done: " + taskName);
            return;
        }
    }
    AppendLog("System", "WB: Task not found in In Progress: " + taskName);
}

void WorkboardManager::RemoveRow(const String& matchText)
{
    for (unsigned s = 0; s < sections_.Size(); ++s)
    {
        for (unsigned r = 0; r < sections_[s].rows.Size(); ++r)
        {
            for (unsigned c = 0; c < sections_[s].rows[r].cells.Size(); ++c)
            {
                if (sections_[s].rows[r].cells[c].Contains(matchText))
                {
                    AppendLog("System", "WB: Removed row matching: " + matchText);
                    sections_[s].rows.Erase(r);
                    return;
                }
            }
        }
    }
    AppendLog("System", "WB: No row found matching: " + matchText);
}

void WorkboardManager::AddSharedFile(const Vector<String>& fields)
{
    // fields[0] = file path, fields[1] = reason
    WorkboardSection* sec = FindSection("Shared Files");
    if (!sec)
    {
        AppendLog("System", "WB: Shared Files section not found");
        return;
    }
    WorkboardRow row;
    row.cells.Push("`" + fields[0].Trimmed() + "`");
    row.cells.Push(fields[1].Trimmed());
    sec->rows.Push(row);
    AppendLog("System", "WB: Added shared file: " + fields[0].Trimmed());
}

void WorkboardManager::UpdateReview(const String& taskName, const String& newReview)
{
    for (unsigned s = 0; s < sections_.Size(); ++s)
    {
        int reviewCol = -1;
        for (unsigned h = 0; h < sections_[s].headers.Size(); ++h)
        {
            if (sections_[s].headers[h].ToLower().Trimmed() == "review")
            { reviewCol = h; break; }
        }
        if (reviewCol < 0) continue;

        for (unsigned r = 0; r < sections_[s].rows.Size(); ++r)
        {
            if (sections_[s].rows[r].cells.Size() > 0 &&
                sections_[s].rows[r].cells[0].Contains(taskName))
            {
                if ((unsigned)reviewCol < sections_[s].rows[r].cells.Size())
                {
                    sections_[s].rows[r].cells[reviewCol] = newReview;
                    AppendLog("System", "WB: Updated review for " + taskName + " -> " + newReview);
                    return;
                }
            }
        }
    }
    AppendLog("System", "WB: Task not found for review update: " + taskName);
}

bool WorkboardManager::AssignTask(const String& taskName, const String& coderRole)
{
    // Validate: coder must be alive
    if (!IsInstanceAlive(coderRole))
    {
        AppendLog("System", "WB assign REJECTED: Coder '" + coderRole + "' is not alive");
        return false;
    }

    // Validate: task must NOT already be in In Progress (first-wins conflict resolution)
    WorkboardSection* inProg = FindSection("In Progress");
    if (inProg)
    {
        for (unsigned i = 0; i < inProg->rows.Size(); ++i)
        {
            if (inProg->rows[i].cells.Size() > 0 && inProg->rows[i].cells[0].Contains(taskName))
            {
                AppendLog("System", "WB assign REJECTED: '" + taskName + "' already in In Progress");
                return false;
            }
        }
    }

    // Phase 2c: search Ready first, then Planned. Mirrors the shell wb-assign
    // behavior. The original code only searched Planned, so any client trying
    // to claim a Ready task via remote mutation would silently fail.
    WorkboardSection* sourceSec = nullptr;
    int foundIdx = -1;
    const char* sectionNames[] = { "Ready", "Planned" };
    for (const char* sname : sectionNames)
    {
        WorkboardSection* sec = FindSection(sname);
        if (!sec) continue;
        for (unsigned i = 0; i < sec->rows.Size(); ++i)
        {
            // Match by checking each cell — the task name typically lives in
            // the second cell ("Plan" column) for Ready/Planned, but tolerate
            // any column for forward compatibility.
            const WorkboardRow& row = sec->rows[i];
            for (unsigned c = 0; c < row.cells.Size(); ++c)
            {
                if (row.cells[c].Contains(taskName))
                {
                    sourceSec = sec;
                    foundIdx = (int)i;
                    break;
                }
            }
            if (foundIdx >= 0) break;
        }
        if (foundIdx >= 0) break;
    }

    if (foundIdx < 0 || !sourceSec)
    {
        AppendLog("System", "WB assign REJECTED: '" + taskName + "' not found in Ready or Planned");
        return false;
    }

    // Remove from source section (Ready or Planned)
    sourceSec->rows.Erase((unsigned)foundIdx);

    // Add to In Progress: | Task | Owner | Started | Review | Notes |
    if (!inProg)
    {
        AppendLog("System", "WB assign REJECTED: In Progress section not found");
        return false;
    }

    // Get today's date
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char dateBuf[16];
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", t);

    WorkboardRow row;
    row.cells.Push(taskName);
    row.cells.Push(coderRole);
    row.cells.Push(String(dateBuf));
    row.cells.Push("Assigned via wb-assign");
    inProg->rows.Push(row);

    AppendLog("System", "WB ASSIGNED: " + taskName + " -> " + coderRole);

    // Send TTY notification to the assigned coder
    String msg = "TASK ASSIGNED: " + taskName + " -- You own this. Check the workboard and start working.";
    SendToSocket(coderRole, msg);
    return true;
}

void WorkboardManager::EmitTableRows(String& output, const WorkboardSection* sec)
{
    for (unsigned r = 0; r < sec->rows.Size(); ++r)
    {
        output += "|";
        for (unsigned c = 0; c < sec->rows[r].cells.Size(); ++c)
        {
            String cell = sec->rows[r].cells[c];
            // Re-add backticks for File column
            if (c < sec->headers.Size() && sec->headers[c].ToLower().Trimmed() == "file"
                && !cell.StartsWith("`") && cell != "\u2014" && !cell.Empty())
                cell = "`" + cell + "`";
            output += " " + cell + " |";
        }
        output += "\n";
    }
}

String WorkboardManager::SerializeSectionsToMarkdown()
{
    String output;
    output += "# Workboard\n";

    for (unsigned s = 0; s < sections_.Size(); ++s)
    {
        const WorkboardSection& sec = sections_[s];
        output += "## " + sec.title + "\n";

        if (sec.headers.Size() > 0)
        {
            // Header row
            output += "|";
            for (unsigned h = 0; h < sec.headers.Size(); ++h)
                output += " " + sec.headers[h] + " |";
            output += "\n";

            // Separator
            output += "|";
            for (unsigned h = 0; h < sec.headers.Size(); ++h)
                output += "------|";
            output += "\n";

            // Data rows
            EmitTableRows(output, const_cast<WorkboardSection*>(&sec));
        }
        output += "\n";
    }
    return output;
}

void WorkboardManager::WriteWorkboard()
{
    // SQL is the sole authority — no markdown write-back needed.
    // Remote clients receive serialized sections via SerializeSectionsToMarkdown().
}

bool WorkboardManager::HandleWorkboardCommand(const String& message)
{
    URHO3D_LOGINFOF("HandleWorkboardCommand: '%s' (len=%u)", message.CString(), message.Length());
    if (!message.StartsWith("WB:"))
        return false;

    // Split: "WB:add-ready:1|Plan|..." -> command, payload
    unsigned firstColon = message.Find(':');
    unsigned secondColon = message.Find(':', firstColon + 1);

    String command;
    String payload;
    if (secondColon != String::NPOS)
    {
        command = message.Substring(firstColon + 1, secondColon - firstColon - 1).Trimmed();
        payload = message.Substring(secondColon + 1);
    }
    else
    {
        command = message.Substring(firstColon + 1).Trimmed();
    }

    Vector<String> fields = payload.Split('|');

    URHO3D_LOGINFOF("  command='%s', payload='%s', fields=%u", command.CString(), payload.CString(), fields.Size());

    // Download command — doesn't mutate workboard
    if (command == "download" && fields.Size() >= 2)
    {
        String url = fields[0].Trimmed();
        String dest = fields[1].Trimmed();

        if (downloadInProgress_)
        {
            AppendLog("Download", "Busy — download already in progress, queued: " + url);
            return true;
        }

        // Resolve destination relative to project root
        String fullDest = projectRoot_ + dest;

        // Ensure parent directory exists
        auto* fs = GetSubsystem<FileSystem>();
        String parentDir = fullDest.Substring(0, fullDest.FindLast('/'));
        if (!fs->DirExists(parentDir))
            fs->CreateDir(parentDir);

        downloadOutputPath_ = fullDest;

        // Non-blocking async curl via Urho3D
        {
            Vector<String> args;
            args.Push("-L");
            args.Push("-o");
            args.Push(downloadOutputPath_);
            args.Push(url);
            curlRequestId_ = fs->SystemRunAsync("curl", args);
        }

        downloadInProgress_ = (curlRequestId_ > 0);
        downloadCheckTimer_ = 0.0f;
        if (downloadStatusText_)
        {
            downloadStatusText_->SetText("Downloading...");
            downloadStatusText_->SetColor(Color(1.0f, 0.9f, 0.3f));
        }
        AppendLog("Download", "Started: " + url + " -> " + fullDest);
        return true;
    }

    bool mutationOk = true;

    if (command == "add-ready" && fields.Size() >= 6)
        AddReadyRow(fields);
    else if (command == "add-inprogress" && fields.Size() >= 5)
        AddInProgressRow(fields);
    else if (command == "add-done" && fields.Size() >= 5)
        AddDoneRow(fields);
    else if (command == "move-done" && !payload.Trimmed().Empty())
        MoveToDone(payload.Trimmed());
    else if (command == "remove" && !payload.Trimmed().Empty())
        RemoveRow(payload.Trimmed());
    else if (command == "add-shared" && fields.Size() >= 2)
        AddSharedFile(fields);
    else if (command == "assign" && fields.Size() >= 2)
        mutationOk = AssignTask(fields[0].Trimmed(), fields[1].Trimmed());
    else if (command == "update-review" && fields.Size() >= 2)
        UpdateReview(fields[0].Trimmed(), fields[1].Trimmed());
    else
    {
        AppendLog("System", "Unknown or malformed WB command: " + message);
        return false;
    }

    if (mutationOk)
    {
        // Persist in-memory state to SQL (incremental sync)
        if (workboardDB_.IsOpen())
            workboardDB_.SyncFromSections(sections_);

        RenderWorkboardUI();
    }
    return mutationOk;
}

// ============================================================================
// Plan Files
// ============================================================================

void WorkboardManager::ScanPlanFiles()
{
    String claudeDir = GetClaudeDir();
    auto* fs = GetSubsystem<FileSystem>();

    Vector<String> files;
    fs->ScanDir(files, claudeDir, "PLAN_*.md", SCAN_FILES, false);
    Urho3D::Sort(files.Begin(), files.End());

    // Skip rebuild if file list hasn't changed — avoids scroll reset
    if (files == planFiles_)
        return;

    planFiles_ = files;

    if (planListView_)
    {
        // Preserve selection
        unsigned prevSel = planListView_->GetSelection();

        planListView_->RemoveAllItems();
        for (unsigned i = 0; i < planFiles_.Size(); ++i)
        {
            auto* item = new Text(context_);
            item->SetFont(font_, currentFontSize_);
            item->SetText(planFiles_[i]);
            item->SetColor(Color(0.7f, 0.85f, 1.0f));
            planListView_->AddItem(item);
        }

        // Restore selection if still valid
        if (prevSel < planListView_->GetNumItems())
            planListView_->SetSelection(prevSel);
    }
}

// HandlePlanSelected() is in WorkboardBase — calls virtual OnPlanSelected()

void WorkboardManager::LoadPlanContent(const String& filename)
{
    String path = GetClaudeDir() + filename;
    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->FileExists(path))
    {
        if (planContentText_)
            planContentText_->SetText("File not found: " + path);
        return;
    }

    File file(context_, path, FILE_READ);
    if (!file.IsOpen())
    {
        if (planContentText_)
            planContentText_->SetText("Failed to open: " + filename);
        return;
    }

    unsigned size = file.GetSize();
    String content;
    content.Resize(size);
    file.Read(&content[0], size);
    file.Close();

    currentPlanFile_ = filename;
    if (planContentText_)
    {
        if (planPanel_)
            planContentText_->SetFixedWidth(planPanel_->GetWidth() - 20);
        planContentText_->SetText(content);
    }
}

// ============================================================================
// IPC — Drop-files (outgoing) + FIFOs (incoming)
// ============================================================================

void WorkboardManager::CreateIPCPaths()
{
    auto* fs = GetSubsystem<FileSystem>();
    fs->CreateDir(ipcDir_);
    fs->CreateDir(ipcDir_ + "instances");
    fs->CreateDir(ttySockDir_);

    // Make IPC dirs group-writable (setgid) so the demoted claude user
    // can register, write role files, and connect to the relay socket.
    // Group 'users' includes both leith and claude.
    chmod(ipcDir_.CString(), 02775);
    chmod((ipcDir_ + "instances").CString(), 02775);
    chmod(ttySockDir_.CString(), 02775);
}



// ============================================================================
// Instance discovery & wake-up
// ============================================================================

bool WorkboardManager::IsInstanceAlive(const String& role)
{
    // AUTHORITATIVE process-liveness first: a locally-pinned coder whose process is gone
    // is DEAD, no matter what a stale socket or a still-"connected" PAKE object reports.
    // Writing to a dead peer's fd succeeds locally (poll POLLOUT stays true) and a PAKE
    // connection reads "connected" until a write fails — so both socket checks below LIE
    // for a /bury'd process. That lie is why a buried elder was never pruned and
    // succession never ran. Re-use the (pid, startTime) pin the HELLO gate already keeps:
    // if /proc says the pinned process is gone or its PID was recycled, the coder is dead.
    // Grace (PRUNE_GRACE_SECS in the tick loop) still guards against a transient /proc
    // read flap — a single false-dead here does not prune. Only applied to genuinely
    // local coders (pid>0 && startTime!=0); remote/PAKE coders have no local pin and fall
    // through to the connection check unchanged.
    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
    {
        if (it->second_.role == role && it->second_.pid > 0 && it->second_.startTime != 0)
        {
            if (ReadProcStartTime(it->second_.pid) != it->second_.startTime)
                return false;   // pinned process gone or PID recycled — authoritatively dead
            break;
        }
    }

    // Check PAKE network connection first (remote coders)
    auto connIt = claudetteConnections_.Find(role);
    if (connIt != claudetteConnections_.End() && connIt->second_ && connIt->second_->IsConnected())
        return true;

    // Check relay fd (local coders) — poll for writability without blocking
    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
    {
        if (it->second_.role == role)
        {
            int fd = it->second_.relayFd;
            if (fd < 0)
                return false;
#ifndef _WIN32
            // Use poll() to check if fd is still valid and writable
            struct pollfd pfd = { fd, POLLOUT, 0 };
            int ret = poll(&pfd, 1, 0);
            return ret > 0 && !(pfd.revents & (POLLERR | POLLHUP | POLLNVAL));
#else
            return true;  // Windows named pipes — assume alive if fd exists
#endif
        }
    }
    return false;
}


String WorkboardManager::ReadBuildStatus()
{
    auto* fs = GetSubsystem<FileSystem>();
    String statusPath = ipcDir_ + "/build_active.json";
    if (!fs->FileExists(statusPath))
        return String::EMPTY;

    File file(context_, statusPath, FILE_READ);
    if (!file.IsOpen())
        return String::EMPTY;

    String content;
    while (!file.IsEof())
        content += file.ReadLine() + "\n";

    // Extract role and target from JSON
    String role, target;
    unsigned pos = content.Find("\"role\"");
    if (pos != String::NPOS)
    {
        unsigned q1 = content.Find("\"", content.Find(":", pos) + 1);
        unsigned q2 = (q1 != String::NPOS) ? content.Find("\"", q1 + 1) : String::NPOS;
        if (q1 != String::NPOS && q2 != String::NPOS)
            role = content.Substring(q1 + 1, q2 - q1 - 1);
    }
    pos = content.Find("\"target\"");
    if (pos != String::NPOS)
    {
        unsigned q1 = content.Find("\"", content.Find(":", pos) + 1);
        unsigned q2 = (q1 != String::NPOS) ? content.Find("\"", q1 + 1) : String::NPOS;
        if (q1 != String::NPOS && q2 != String::NPOS)
            target = content.Substring(q1 + 1, q2 - q1 - 1);
    }

    if (target.Empty())
        return String::EMPTY;

    return "BUILD: " + role + " -> " + target;
}

void WorkboardManager::RefreshInstanceStatus()
{
    // Self-repair: if our PID file or relay socket was swept by a janitor,
    // restore them. We're alive — the janitor was wrong to remove them.
    auto* fs = GetSubsystem<FileSystem>();
    {
#ifndef _WIN32
        String sockPath = ttySockDir_ + "manager_relay.sock";
        if (relayListenFd_ >= 0 && !fs->FileExists(sockPath))
        {
            // Socket FD is still valid but the filesystem entry was deleted.
            // Must close and re-bind to recreate the socket file.
            URHO3D_LOGINFO("Self-repair: relay socket swept — rebinding");
            close(relayListenFd_);
            relayListenFd_ = -1;
            StartRelaySocket();
        }
        else if (relayListenFd_ < 0)
        {
            // Socket was never started or failed — try again
            URHO3D_LOGINFO("Self-repair: relay socket down — restarting");
            StartRelaySocket();
        }
#else
        // Windows named pipes live in kernel namespace — no filesystem entry
        // to sweep. Just check if the handle went bad.
        if (relayPipeHandle_ == INVALID_HANDLE_VALUE)
        {
            URHO3D_LOGINFO("Self-repair: relay pipe down — restarting");
            StartRelaySocket();
        }
#endif
    }

    bool anyChanged = false;

    // Update build status indicator
    if (buildStatusText_)
    {
        String buildStatus = ReadBuildStatus();
        if (buildStatus != buildStatusText_->GetText())
        {
            buildStatusText_->SetText(buildStatus);
            buildStatusText_->SetColor(COL_GREEN);
        }
    }

    // --- Liveness grace, elder succession-with-reclaim, role compaction ---
    {
        const unsigned now = (unsigned)time(NULL);

        // 1) Grace-based liveness. A single failed poll must NOT evict anyone:
        //    transient relay-fd unwritability was what let a junior usurp the
        //    elder. Prune only after a coder stays unhealthy past the grace.
        Vector<String> deadSessions;
        bool elderPruned = false;
        for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
        {
            CoderInstance& inst = it->second_;
            if (IsInstanceAlive(inst.role))
            {
                inst.unhealthySince = 0;  // healthy — reset the grace timer
                continue;
            }
            if (inst.unhealthySince == 0)
                inst.unhealthySince = now;  // first failure — start the clock
            else if (now - inst.unhealthySince >= PRUNE_GRACE_SECS)
                deadSessions.Push(it->first_);  // persistently gone — prune
        }

        for (const String& sid : deadSessions)
        {
            CoderInstance& dead = coderInstances_[sid];
            AppendLog("Prune", dead.role + " departed (PID " + String(dead.pid) +
                " unhealthy > " + String(PRUNE_GRACE_SECS) + "s)");
            // Elder leaving: open a reclaim window instead of instantly handing
            // "coder" to a junior. Remember its identity + original seniority.
            if (dead.role == "coder")
            {
                vacantElder_.sessionId   = sid;
                vacantElder_.pid         = dead.pid;
                vacantElder_.startTime   = dead.startTime;
                vacantElder_.registeredAt = dead.registeredAt;
                vacantElder_.vacatedAt   = now;
                elderPruned = true;
                AppendLog("Succession", "Elder slot vacant — reserved " + String(ELDER_RECLAIM_SECS) +
                    "s for " + sid + " (PID " + String(dead.pid) + ") to reclaim");
            }
            // Close the persistent relay fd before dropping the registry entry.
            // Erase loses our only handle to it otherwise, leaking a descriptor
            // per departed coder (Manager owns the socket — it must close it).
            // Mirrors the supersede-close at the register path. A genuinely-gone
            // peer's end is already dead, so this severs nothing live; a reclaiming
            // elder reconnects with a fresh fd regardless.
            if (dead.relayFd >= 0)
            {
                close(dead.relayFd);
                dead.relayFd = -1;
            }
            coderInstances_.Erase(sid);
        }

        // 2) Expire a stale reclaim window — elder never returned, allow promotion.
        bool windowExpired = false;
        if (!vacantElder_.sessionId.Empty() &&
            now - vacantElder_.vacatedAt >= ELDER_RECLAIM_SECS)
        {
            AppendLog("Succession", "Elder reclaim window expired for " +
                vacantElder_.sessionId + " — promoting by seniority");
            vacantElder_.sessionId = String::EMPTY;
            vacantElder_.pid = 0;
            windowExpired = true;
        }

        // 3) Re-derive roles deterministically; enforce single-elder + no dupes.
        if (!deadSessions.Empty() || elderPruned || windowExpired)
            EnforceRoleInvariants();
    }

    // --- Unified roles dropdown (all instances with PIDs) ---
    if (localsDropdown_)
    {
        unsigned prevSel = localsDropdown_->GetSelection();
        localsDropdown_->RemoveAllItems();

        auto addRoleItem = [&](const String& role, int pid, bool alive, const Color& color) {
            auto* item = new Text(context_);
            item->SetFont(font_, currentFontSize_);
            String label = role + " (" + String(pid) + ")";
            item->SetText(label);
            item->SetColor(alive ? color : Color(0.4f, 0.4f, 0.4f));
            item->SetHorizontalAlignment(HA_LEFT);
            item->SetMinSize(180, 18);
            localsDropdown_->AddItem(item);
        };

        // Display registered instances from in-memory registry
        // Collect and sort roles for consistent ordering
        Vector<String> sortedSessions;
        for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
            sortedSessions.Push(it->first_);
        Sort(sortedSessions.Begin(), sortedSessions.End());

        unsigned aliveCount = 0;
        for (const String& sid : sortedSessions)
        {
            const CoderInstance& inst = coderInstances_[sid];

            bool alive = IsInstanceAlive(inst.role);
            if (alive) ++aliveCount;

            Color color;
            if (inst.role == "yuki")
                color = Color(1.0f, 0.5f, 0.8f);
            else if (inst.role.StartsWith("coder"))
                color = Color(0.3f, 0.9f, 1.0f);
            else if (inst.role.StartsWith("unassigned"))
                color = COL_RED;
            else
                color = COL_YELLOW;

            addRoleItem(inst.role, inst.pid, alive, color);
        }

        // Insert header at position 0 showing alive instance count
        {
            auto* header = new Text(context_);
            header->SetFont(font_, currentFontSize_);
            header->SetText("Coders: " + String(aliveCount));
            header->SetColor(aliveCount > 0 ? Color(0.3f, 0.9f, 1.0f) : Color(0.4f, 0.4f, 0.4f));
            header->SetHorizontalAlignment(HA_LEFT);
            header->SetMinSize(120, 18);
            localsDropdown_->InsertItem(0, header);
        }

        // Always select header (item 0) so banner shows coder count
        localsDropdown_->SetSelection(0);
    }

    // --- Remotes (network WorkboardClient connections) ---
    if (remotesDropdown_)
    {
        unsigned prevSel = remotesDropdown_->GetSelection();
        remotesDropdown_->RemoveAllItems();

        auto* header = new Text(context_);
        header->SetFont(font_, currentFontSize_);
        header->SetText("Remotes: " + String(wbClients_.Size()));
        header->SetColor(wbClients_.Empty() ? COL_ORANGE_DIM : COL_ORANGE);
        header->SetMinSize(95, 18);
        remotesDropdown_->AddItem(header);

        for (auto it = wbClients_.Begin(); it != wbClients_.End(); ++it)
        {
            const WbClientInfo& info = it->second_;
            auto* item = new Text(context_);
            item->SetFont(font_, currentFontSize_);
            item->SetMinSize(95, 18);

            String label = info.name_.Empty() ? "unknown" : info.name_;
            // Show remote instance counts if available
            if (info.remoteCoderCount_ > 0 || info.remoteYukiAlive_)
            {
                label += " (Y:" + String(info.remoteYukiAlive_ ? 1 : 0) +
                         " C:" + String(info.remoteCoderCount_) + ")";
            }
            else if (!info.role_.Empty())
                label += " (" + info.role_ + ")";
            if (info.authenticated_)
            {
                item->SetText(label);
                item->SetColor(Color(0.3f, 1.0f, 0.5f));
            }
            else
            {
                item->SetText(label + " [auth...]");
                item->SetColor(Color(0.8f, 0.6f, 0.2f));
            }
            remotesDropdown_->AddItem(item);
        }

        if (prevSel < remotesDropdown_->GetNumItems())
            remotesDropdown_->SetSelection(prevSel);
        else if (remotesDropdown_->GetNumItems() > 0)
            remotesDropdown_->SetSelection(0);
    }

    // --- Unassigned (dynamic, multiple) ---
    Vector<String> liveUnassigned = DiscoverUnassignedRoles();

    if (liveUnassigned != knownUnassignedRoles_)
    {
        anyChanged = true;

        knownUnassignedRoles_ = liveUnassigned;
    }

    if (unassignedStatusDropdown_)
    {
        unsigned prevSel = unassignedStatusDropdown_->GetSelection();
        unassignedStatusDropdown_->RemoveAllItems();

        if (knownUnassignedRoles_.Empty())
        {
            auto* item = new Text(context_);
            item->SetFont(font_, currentFontSize_);
            item->SetText("Unassigned: 0");
            item->SetColor(COL_RED_DIM);
            item->SetMinSize(95, 18);
            unassignedStatusDropdown_->AddItem(item);
        }
        else
        {
            auto* countItem = new Text(context_);
            countItem->SetFont(font_, currentFontSize_);
            countItem->SetText("Unassigned: " + String(knownUnassignedRoles_.Size()));
            countItem->SetColor(COL_RED);
            countItem->SetMinSize(95, 18);
            unassignedStatusDropdown_->AddItem(countItem);

            for (const String& role : knownUnassignedRoles_)
            {
                if (!IsInstanceAlive(role))
                    continue;

                String label = role;
                if (!label.Empty())
                    label[0] = (char)toupper(label[0]);

                auto* item = new Text(context_);
                item->SetFont(font_, currentFontSize_);
                item->SetMinSize(95, 18);
                item->SetText(label);
                item->SetColor(COL_RED);
                unassignedStatusDropdown_->AddItem(item);
            }
        }

        if (prevSel < unassignedStatusDropdown_->GetNumItems())
            unassignedStatusDropdown_->SetSelection(prevSel);
        else if (unassignedStatusDropdown_->GetNumItems() > 0)
            unassignedStatusDropdown_->SetSelection(0);
    }

    // --- Coders (dynamic, multiple) ---
    Vector<String> liveCoders = DiscoverCoderRoles();

    // Check if the set of coder roles changed
    if (liveCoders != knownCoderRoles_)
    {
        anyChanged = true;

        knownCoderRoles_ = liveCoders;
        UpdateCoderCapText();

        // Rebuild unified receiver dropdown
        if (coderDropdown_)
        {
            unsigned prevSelection = coderDropdown_->GetSelection();
            coderDropdown_->RemoveAllItems();

            // Helper to add a centered, colored dropdown item
            auto addReceiverItem = [&](const String& label, const Color& color) {
                auto* item = new Text(context_);
                item->SetFont(font_, currentFontSize_);
                item->SetText(label);
                item->SetColor(color);
                item->SetHorizontalAlignment(HA_CENTER);
                item->SetStyleAuto();
                coderDropdown_->AddItem(item);
            };

            // Dynamic: live coders
            for (const String& role : knownCoderRoles_)
                addReceiverItem(role, Color(0.3f, 0.9f, 1.0f));
            // Static entries
            addReceiverItem("yuki", Color(1.0f, 0.5f, 0.8f));
            // Unassigned excluded from send targets — ghosts of living PIDs
            addReceiverItem("broadcast", Color(1.0f, 0.55f, 0.45f));

            if (prevSelection < coderDropdown_->GetNumItems())
                coderDropdown_->SetSelection(prevSelection);
            else if (coderDropdown_->GetNumItems() > 0)
                coderDropdown_->SetSelection(0);
        }
    }

    // Rebuild status bar dropdown with current PID info
    if (coderStatusDropdown_)
    {
        unsigned prevSel = coderStatusDropdown_->GetSelection();
        coderStatusDropdown_->RemoveAllItems();

        if (knownCoderRoles_.Empty())
        {
            auto* item = new Text(context_);
            item->SetFont(font_, currentFontSize_);
            item->SetText("Coders: 0");
            item->SetColor(COL_INDIGO_DIM);
            item->SetMinSize(95, 18);
            coderStatusDropdown_->AddItem(item);
        }
        else
        {
            // Header: count only
            auto* header = new Text(context_);
            header->SetFont(font_, currentFontSize_);
            header->SetMinSize(95, 18);
            header->SetText("Coders: " + String(knownCoderRoles_.Size()));
            header->SetColor(COL_INDIGO);
            coderStatusDropdown_->AddItem(header);

            for (const String& role : knownCoderRoles_)
            {
                if (!IsInstanceAlive(role))
                    continue;  // dead ones don't show

                String label = role;
                if (!label.Empty())
                    label[0] = (char)toupper(label[0]);

                auto* item = new Text(context_);
                item->SetFont(font_, currentFontSize_);
                item->SetMinSize(95, 18);
                item->SetText(label);

                item->SetColor(COL_INDIGO);

                coderStatusDropdown_->AddItem(item);
            }
        }

        // When multiple coders, select the summary header (index 0)
        if (knownCoderRoles_.Size() > 1)
            coderStatusDropdown_->SetSelection(0);
        else if (prevSel < coderStatusDropdown_->GetNumItems())
            coderStatusDropdown_->SetSelection(prevSel);
        else if (coderStatusDropdown_->GetNumItems() > 0)
            coderStatusDropdown_->SetSelection(0);
    }

    // Auto-spawn disabled — Leith spawns coders manually

    if (anyChanged)
        UpdateBeacon();
}


// Re-derive every live coder's role from seniority and guarantee the two hard
// invariants: at most one elder ("coder"), and no role held by two sessions.
// Ordering is (registeredAt asc, sessionId asc) so equal timestamps can never
// non-deterministically flip the elder. While a departed elder may still
// reclaim (vacantElder_ set), the "coder" slot is held vacant and everyone is a
// junior. Notifications go over each instance's own relay fd — never a role
// lookup — so a role mid-rename can't misdeliver.
void WorkboardManager::EnforceRoleInvariants()
{
    if (coderInstances_.Empty())
        return;

    struct Ord { String sid; unsigned reg; };
    Vector<Ord> order;
    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
        order.Push({ it->first_, it->second_.registeredAt });
    for (unsigned i = 0; i < order.Size(); ++i)
        for (unsigned j = i + 1; j < order.Size(); ++j)
        {
            bool swap = (order[j].reg < order[i].reg) ||
                        (order[j].reg == order[i].reg && order[j].sid < order[i].sid);
            if (swap)
                Swap(order[i], order[j]);
        }

    const bool elderVacant = !vacantElder_.sessionId.Empty();

    // Phase 1: assign unique target roles in the map (no notify yet).
    struct Change { String sid; String role; };
    Vector<Change> changed;
    int junior = 2;
    for (unsigned i = 0; i < order.Size(); ++i)
    {
        String desired = (!elderVacant && i == 0) ? String("coder")
                                                  : "coder" + String(junior++);
        CoderInstance& inst = coderInstances_[order[i].sid];
        if (inst.role != desired)
        {
            inst.role = desired;
            changed.Push({ order[i].sid, desired });
        }
    }

    // Phase 2: notify each changed instance over ITS OWN relay fd.
    for (unsigned i = 0; i < changed.Size(); ++i)
    {
        CoderInstance& inst = coderInstances_[changed[i].sid];
        AppendLog("Role", "Reassigned " + changed[i].sid + " -> " + changed[i].role);
        URHO3D_LOGINFOF("[Role] %s -> %s", changed[i].sid.CString(), changed[i].role.CString());

        bool delivered = false;
#ifndef _WIN32
        if (inst.relayFd >= 0)
        {
            String line = "__ROLE__:" + changed[i].role + "\n";
            write(inst.relayFd, line.CString(), line.Length());
            delivered = true;
        }
#endif
        if (!delivered)
            SendToSocket(changed[i].role, "__ROLE__:" + changed[i].role);

        // A session newly promoted to elder receives the shared memory.
        if (changed[i].role == "coder" && workboardDB_.IsOpen())
        {
            String memories = workboardDB_.GetAllMemories();
            if (!memories.Empty())
            {
#ifndef _WIN32
                if (inst.relayFd >= 0)
                {
                    String flat = "[SHARED MEMORY] " + memories;
                    flat.Replace("\n", " ");
                    flat += "\n";
                    write(inst.relayFd, flat.CString(), flat.Length());
                }
                else
#endif
                    SendToSocket("coder", "[SHARED MEMORY]\n" + memories);
                AppendLog("Succession", "New elder promoted by seniority (" + changed[i].sid + ")");
            }
        }
    }

    // Phase 3: persist EVERY instance's role to the file the hook's get_role()
    // actually reads ($IPC_DIR/instances/<sid>.role) — not just the ones that
    // changed this pass. The relay-fd push above goes down a socket the instance
    // never drains (inbound relay delivery is disabled), so the authoritative
    // file is the ONLY channel the self-label heals from on the next get_role().
    // Writing it for all instances (this runs on every registration and prune)
    // closes two gaps the change-only write left open:
    //   1. Initial __HELLO__: a fresh instance's assigned role == its seniority
    //      rank, so inst.role == desired and Phase 1 records NO change — without
    //      this the file is never written and get_role() returns "unassigned".
    //   2. Post-restart: CleanupLegacyPIDFiles() wipes *.role at boot; a
    //      re-registering instance is a fresh HELLO with no role change, so only
    //      an all-instances write restores its file. (A promotion still heals via
    //      the same path — this is a superset of the old change-only behaviour.)
    for (unsigned i = 0; i < order.Size(); ++i)
    {
        const String& sid = order[i].sid;
        String roleFilePath = ipcDir_ + "instances/" + sid + ".role";
        File rf(context_);
        if (rf.Open(roleFilePath, FILE_WRITE))
        {
            String body = coderInstances_[sid].role + "\n";
            rf.Write(body.CString(), body.Length());
            rf.Close();
        }
    }
}

bool WorkboardManager::IsTrustedPeerUid(int uid)
{
#ifndef _WIN32
    // Always trust the user running Manager (Leith).
    if (uid == (int)geteuid())
        return true;
    // Trust the sandboxed "claude" user, if it resolves on this box.
    static int claudeUid = -2;  // -2 = not yet resolved, -1 = unresolvable
    if (claudeUid == -2)
    {
        struct passwd* pw = getpwnam("claude");
        claudeUid = pw ? (int)pw->pw_uid : -1;
    }
    if (claudeUid >= 0 && uid == claudeUid)
        return true;
    // If we couldn't resolve "claude", don't let UID alone reject — lineage is
    // the real gate. Only fail UID when we positively know it's neither.
    return claudeUid < 0;
#else
    (void)uid;
    return true;
#endif
}

// Walk the peer process's ancestry (kernel-verified peerPid from SO_PEERCRED)
// up the /proc tree and confirm claimedPid is the connecting process or one of
// its ancestors. This binds a "__HELLO__:pid_N:N" claim to genuine lineage:
// only a descendant of Claudette N can legitimately claim session pid_N.
bool WorkboardManager::VerifyPeerLineage(int peerPid, int claimedPid)
{
#ifndef _WIN32
    if (peerPid <= 0 || claimedPid <= 0)
        return false;

    int cur = peerPid;
    // Cap hops to avoid pathological loops; real trees are shallow.
    for (int hops = 0; hops < 64 && cur > 1; ++hops)
    {
        if (cur == claimedPid)
            return true;

        File f(context_);
        if (!f.Open("/proc/" + String(cur) + "/status", FILE_READ))
            return false;  // peer (or an ancestor) already gone — cannot verify

        int ppid = 0;
        while (!f.IsEof())
        {
            String line = f.ReadLine();
            if (line.StartsWith("PPid:"))
            {
                ppid = atoi(line.Substring(5).Trimmed().CString());
                break;
            }
        }
        f.Close();

        if (ppid <= 0)
            return false;
        cur = ppid;
    }
    return cur == claimedPid;
#else
    (void)peerPid; (void)claimedPid;
    return true;
#endif
}

unsigned long long WorkboardManager::ReadProcStartTime(int pid)
{
#ifndef _WIN32
    if (pid <= 0)
        return 0;

    File f(context_);
    if (!f.Open("/proc/" + String(pid) + "/stat", FILE_READ))
        return 0;  // process gone — cannot pin

    unsigned size = f.GetSize();
    String stat;
    if (size > 0)
    {
        stat.Resize(size);
        f.Read(&stat[0], size);
    }
    f.Close();

    // /proc/<pid>/stat: "pid (comm) state ppid ... starttime ...". comm can hold
    // spaces and parens, so split AFTER the last ')'. starttime is field 22 — the
    // 20th token after the ')'  (token[0]=field3=state … token[19]=field22).
    unsigned closeParen = stat.FindLast(')');
    if (closeParen == String::NPOS)
        return 0;
    Vector<String> fields = stat.Substring(closeParen + 1).Trimmed().Split(' ');
    if (fields.Size() <= 19)
        return 0;
    return strtoull(fields[19].CString(), nullptr, 10);
#else
    (void)pid;
    return 0;
#endif
}

String WorkboardManager::AssignCoderRole(const String& sessionId, int pid)
{
    // Pin the caller's process identity against pid reuse: starttime + pid uniquely
    // identifies a process. Captured here, carried into vacantElder_ on prune, and
    // rechecked by the HELLO gate. 0 if the process is already gone.
    const unsigned long long startTime = ReadProcStartTime(pid);

    // Returning-elder reclaim: while the elder slot is held vacant for this exact
    // identity (same session, or same Claudette PID), restore it as elder at its
    // ORIGINAL seniority — never as a fresh junior. This is what stops a blip
    // from costing the elder its role permanently.
    if (!vacantElder_.sessionId.Empty() &&
        (sessionId == vacantElder_.sessionId || (pid > 0 && pid == vacantElder_.pid)))
    {
        CoderInstance inst;
        inst.role = "coder";
        inst.sessionId = sessionId;
        inst.pid = pid;
        inst.startTime = startTime;
        inst.registeredAt = vacantElder_.registeredAt ? vacantElder_.registeredAt
                                                       : (unsigned)time(NULL);
        coderInstances_[sessionId] = inst;
        AppendLog("Succession", "Elder reclaimed by " + sessionId + " (PID " + String(pid) + ")");
        vacantElder_.sessionId = String::EMPTY;
        vacantElder_.pid = 0;
        return "coder";
    }

    // If this session already has a role, update PID and return existing role
    if (coderInstances_.Contains(sessionId))
    {
        coderInstances_[sessionId].pid = pid;
        coderInstances_[sessionId].startTime = startTime;
        return coderInstances_[sessionId].role;
    }

    // Deduplicate by PID: if an existing instance has the same PID but a different
    // session ID (e.g. session ID changed between re-registrations), migrate it
    // to the new session ID and return the existing role.
    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
    {
        if (it->second_.pid == pid)
        {
            CoderInstance migrated = it->second_;
            migrated.sessionId = sessionId;
            String oldSession = it->first_;
            coderInstances_.Erase(it);
            coderInstances_[sessionId] = migrated;
            URHO3D_LOGINFOF("[Register] PID %d migrated from session %s to %s (role %s)",
                pid, oldSession.CString(), sessionId.CString(), migrated.role.CString());
            return migrated.role;
        }
    }

    // Find next available coder slot.
    // First instance gets "coder", subsequent get "coder2", "coder3", etc.
    HashSet<String> takenRoles;
    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
        takenRoles.Insert(it->second_.role);

    // While a departed elder still has a live reclaim window, the "coder" slot is
    // reserved — a different newcomer must NOT step into it. Start them at coder2.
    if (!vacantElder_.sessionId.Empty())
        takenRoles.Insert("coder");

    String role = "coder";
    if (takenRoles.Contains(role))
    {
        for (int n = 2; n <= 20; ++n)
        {
            role = "coder" + String(n);
            if (!takenRoles.Contains(role))
                break;
        }
    }

    CoderInstance inst;
    inst.role = role;
    inst.sessionId = sessionId;
    inst.pid = pid;
    inst.startTime = startTime;
    inst.registeredAt = (unsigned)time(NULL);
    coderInstances_[sessionId] = inst;

    return role;
}

bool WorkboardManager::SendToSocket(const String& role, const String& message, const String& excludeRole)
{
#ifndef _WIN32
    // Broadcast to all coders via persistent relay fds
    if (role == "coders")
    {
        int delivered = 0;
        for (auto& ci : coderInstances_)
        {
            if (!excludeRole.Empty() && ci.second_.role == excludeRole)
                continue;
            if (SendToSocket(ci.second_.role, message))
                ++delivered;
        }

        if (delivered > 0)
        {
            URHO3D_LOGINFOF("SendToSocket: broadcast 'coders' delivered to %d instance(s)", delivered);
            return true;
        }
        URHO3D_LOGWARNING("SendToSocket: broadcast 'coders' found no live instances");
        return false;
    }

    // Prefer PAKE-authenticated Claudette connection (encrypted network channel)
    if (claudetteConnections_.Contains(role))
    {
        Connection* conn = claudetteConnections_[role];
        if (conn)
        {
            VectorBuffer buf;
            buf.WriteString(message);
            conn->SendMessage(MSG_USER, true, true, buf);
            URHO3D_LOGINFOF("SendToSocket: sent %d bytes to %s via PAKE", (int)message.Length(), role.CString());
            return true;
        }
    }

    // Fallback: persistent relay fd (Unix socket from __HELLO__)
    int fd = -1;
    String foundSession;
    for (auto& ci : coderInstances_)
    {
        if (ci.second_.role == role)
        {
            fd = ci.second_.relayFd;
            foundSession = ci.first_;
            break;
        }
    }

    if (fd < 0)
    {
        URHO3D_LOGWARNINGF("SendToSocket: no connection for '%s' (no PAKE, no relay fd)", role.CString());
        return false;
    }

    String flat = message;
    flat.Replace("\n", " ");
    flat.Replace("\r", "");
    flat += "\n";  // newline-delimited for receiver framing

    ssize_t written = write(fd, flat.CString(), flat.Length());
    if (written < 0)
    {
        // Connection dead — clean up
        URHO3D_LOGWARNINGF("SendToSocket: write failed for '%s' fd=%d, closing", role.CString(), fd);
        close(fd);
        if (!foundSession.Empty() && coderInstances_.Contains(foundSession))
            coderInstances_[foundSession].relayFd = -1;
        return false;
    }

    URHO3D_LOGINFOF("SendToSocket: sent %d bytes to %s (fd=%d)", (int)written, role.CString(), fd);
    return true;
#else
    // ── Windows: named pipes ──
    // Pipe names map 1:1 with Unix socket files: \\.\pipe\urho_claude_{role}

    auto sendToPipe = [&](const String& pipeName, const String& msg) -> bool
    {
        HANDLE hPipe = CreateFileA(
            pipeName.CString(),
            GENERIC_WRITE,
            0, nullptr,
            OPEN_EXISTING,
            0, nullptr);
        if (hPipe == INVALID_HANDLE_VALUE)
            return false;

        String flat = msg;
        flat.Replace("\n", " ");
        flat.Replace("\r", "");

        DWORD written = 0;
        BOOL ok = WriteFile(hPipe, flat.CString(), (DWORD)flat.Length(), &written, nullptr);
        CloseHandle(hPipe);
        return ok && written > 0;
    };

    if (role == "coders")
    {
        // Broadcast: try coder, coder2, coder3, ... coder16
        int delivered = 0;
        const char* names[] = { "coder", "coder2", "coder3", "coder4",
                                "coder5", "coder6", "coder7", "coder8",
                                "coder9", "coder10", "coder11", "coder12",
                                "coder13", "coder14", "coder15", "coder16" };
        for (int i = 0; i < 16; ++i)
        {
            if (!excludeRole.Empty() && excludeRole == names[i])
                continue;
            if (sendToPipe(PipeName(names[i]), message))
            {
                ++delivered;
                URHO3D_LOGINFOF("SendToSocket [broadcast]: %s", names[i]);
            }
        }
        if (delivered > 0)
        {
            URHO3D_LOGINFOF("SendToSocket: broadcast 'coders' delivered to %d pipe(s)", delivered);
            return true;
        }
        URHO3D_LOGWARNING("SendToSocket: broadcast 'coders' found no live pipes");
        return false;
    }

    // Single target
    if (sendToPipe(PipeName(role), message))
    {
        URHO3D_LOGINFOF("SendToSocket: sent to %s", role.CString());
        return true;
    }
    URHO3D_LOGWARNINGF("SendToSocket: no pipe for '%s'", role.CString());
    return false;
#endif
}


// ============================================================================
// Relay socket — message broker for inter-instance communication
// ============================================================================

void WorkboardManager::StartRelaySocket()
{
#ifndef _WIN32
    String sockPath = ttySockDir_ + "manager_relay.sock";
    unlink(sockPath.CString());

    relayListenFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (relayListenFd_ < 0)
    {
        URHO3D_LOGERROR("Failed to create relay socket");
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.CString(), sizeof(addr.sun_path) - 1);

    if (bind(relayListenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(relayListenFd_, 8) < 0)
    {
        URHO3D_LOGERROR("Failed to bind relay socket");
        close(relayListenFd_);
        relayListenFd_ = -1;
        return;
    }

    // Make the relay socket reachable by the sandboxed 'claude' user.
    // A bound AF_UNIX socket inherits Manager's primary group (leith) and the
    // process umask, so 'other' gets no write bit and connect() is denied with
    // EACCES. The tty dir is setgid 'users', but a socket does not inherit the
    // dir's group, so we re-group it explicitly. 'users' (which claude is in)
    // then has rwx via mode 0775; 'other' stays r-x (no connect).
    {
        struct group* grp = getgrnam("users");
        if (grp)
            chown(sockPath.CString(), (uid_t)-1, grp->gr_gid);
        chmod(sockPath.CString(), 0775);
    }

    URHO3D_LOGINFO("Relay socket listening: " + sockPath);
#else
    // Windows: create a named pipe instance for the relay.
    // PIPE_ACCESS_INBOUND — clients write, we read.
    // FILE_FLAG_OVERLAPPED — non-blocking via overlapped I/O.
    String pipeName = PipeName("manager_relay");
    relayPipeHandle_ = CreateNamedPipeA(
        pipeName.CString(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096, 4096,
        0, nullptr);

    if (relayPipeHandle_ == INVALID_HANDLE_VALUE)
    {
        URHO3D_LOGERROR("Failed to create relay named pipe");
        return;
    }

    // Start async connect — completes when a client connects
    memset(&relayOverlapped_, 0, sizeof(relayOverlapped_));
    relayOverlapped_.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    relayConnectPending_ = !ConnectNamedPipe(relayPipeHandle_, &relayOverlapped_);
    if (!relayConnectPending_ && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        URHO3D_LOGERROR("Failed to start relay pipe connect");
        CloseHandle(relayPipeHandle_);
        relayPipeHandle_ = INVALID_HANDLE_VALUE;
        return;
    }

    URHO3D_LOGINFO("Relay pipe listening: " + pipeName);
#endif
}

void WorkboardManager::StopRelaySocket()
{
#ifndef _WIN32
    if (relayListenFd_ >= 0)
    {
        close(relayListenFd_);
        relayListenFd_ = -1;
    }
    String sockPath = ttySockDir_ + "manager_relay.sock";
    unlink(sockPath.CString());
#else
    if (relayPipeHandle_ != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(relayPipeHandle_);
        CloseHandle(relayPipeHandle_);
        relayPipeHandle_ = INVALID_HANDLE_VALUE;
    }
    if (relayOverlapped_.hEvent)
    {
        CloseHandle(relayOverlapped_.hEvent);
        relayOverlapped_.hEvent = nullptr;
    }
#endif
}

void WorkboardManager::PollRelaySocket()
{
#ifdef _WIN32
    if (relayPipeHandle_ == INVALID_HANDLE_VALUE)
        return;
#else
    if (relayListenFd_ < 0)
        return;
#endif

    // Accept all pending connections (non-blocking) and read messages.
    // Transport differs per platform, message parsing is shared.
    for (;;)
    {
        char buf[4096];
        int n = 0;

#ifndef _WIN32
        int clientFd = accept(relayListenFd_, nullptr, nullptr);
        if (clientFd < 0)
            break;

        // Capture kernel-verified peer credentials (unspoofable, unlike the
        // self-reported SID/PID in the message body). Used to reject relay-fd
        // theft and role-migration hijacks on the identity-changing paths.
        struct ucred peerCred = {};
        socklen_t credLen = sizeof(peerCred);
        bool havePeerCred =
            (getsockopt(clientFd, SOL_SOCKET, SO_PEERCRED, &peerCred, &credLen) == 0);
        int peerPid = havePeerCred ? (int)peerCred.pid : 0;
        int peerUid = havePeerCred ? (int)peerCred.uid : -1;

        ssize_t r = read(clientFd, buf, sizeof(buf) - 1);
        // Don't close yet — __HELLO__ may promote this to a persistent connection.
        // Fire-and-forget connections are closed after message dispatch below.
        bool keepFd = false;

        if (r <= 0)
        {
            close(clientFd);
            continue;
        }
        n = (int)r;
#else
        // Check if a client has connected (non-blocking via overlapped)
        if (relayConnectPending_)
        {
            DWORD dummy;
            if (!GetOverlappedResult(relayPipeHandle_, &relayOverlapped_, &dummy, FALSE))
                break;  // No client yet
            relayConnectPending_ = false;
        }

        // Client connected — read the message
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(relayPipeHandle_, buf, sizeof(buf) - 1, &bytesRead, nullptr);
        // Done with this client — disconnect and re-arm for next
        DisconnectNamedPipe(relayPipeHandle_);
        ResetEvent(relayOverlapped_.hEvent);
        relayConnectPending_ = !ConnectNamedPipe(relayPipeHandle_, &relayOverlapped_);

        if (!ok || bytesRead == 0)
            continue;
        n = (int)bytesRead;
#endif
        buf[n] = '\0';

        // ── Shared message parsing (platform-independent) ──

        String payload(buf, (unsigned)n);
        // relay_send frames every message with a trailing '\n' (printf '%s\n'). String::Trimmed()
        // strips ONLY spaces/tabs, never '\n'/'\r' (Str.cpp:542), so that newline otherwise rides
        // on the LAST colon-field. For __WB_MOVE_DONE__/__WB_REMOVE__ the last field IS the task
        // name, so it arrived as "TASK\n" and the exact-match "WHERE task_name=?" silently matched
        // 0 rows — the move/remove no-opped while the client still echoed success. (INSERT commands
        // were immune: their last field is owner/notes, so the row was created with a clean name but
        // a dirty owner stored as "coderN\n".) Strip trailing CR/LF here, once, so every handler and
        // every field is clean.
        {
            i32 pl = payload.Length();
            while (pl > 0 && (payload.At(pl - 1) == '\n' || payload.At(pl - 1) == '\r'))
                --pl;
            if (pl != payload.Length())
                payload.Resize(pl);
        }
        unsigned firstColon = payload.Find(':');
        if (firstColon == String::NPOS || firstColon == 0)
        {
            URHO3D_LOGWARNING("Relay: malformed message (no target separator)");
            continue;
        }

        String target = payload.Substring(0, firstColon).Trimmed();
        String rest = payload.Substring(firstColon + 1);

        // Try to extract sender from "sender:message" — if second colon exists
        String sender;
        String message;
        unsigned secondColon = rest.Find(':');
        if (secondColon != String::NPOS && secondColon < rest.Length() - 1)
        {
            String maybeSender = rest.Substring(0, secondColon).Trimmed();
            // Sender roles are short alphanumeric (coder, coder2, coder3, etc.)
            // If maybeSender looks like a role (no spaces, short), treat it as sender
            if (!maybeSender.Empty() && maybeSender.Length() <= 20 && !maybeSender.Contains(' '))
            {
                sender = maybeSender;
                message = rest.Substring(secondColon + 1).Trimmed();
            }
            else
            {
                // Not a role — treat entire rest as message (legacy format)
                message = rest.Trimmed();
            }
        }
        else
        {
            message = rest.Trimmed();
        }

        if (target.Empty() || message.Empty())
            continue;

        // ── CODER CAP query: reply with the current rubber coder limit ──
        // Manager is the single source of truth for the user-set cap
        // (maxLocalCoders_, driven by the [-]/[+] UI, default 4). Shell hooks
        // (safe_build.sh) query this live via relay so nothing has to hard-code
        // a build "sprint" threshold — the sanctioned fleet size follows the
        // user's dial. Fire-and-forget: reply the integer and close.
        if (message == "__CODER_CAP__")
        {
#ifndef _WIN32
            String reply = String(maxLocalCoders_) + "\n";
            write(clientFd, reply.CString(), reply.Length());
            close(clientFd);
#endif
            continue;
        }

        // ── HELLO: instance announces itself, Manager assigns role ──
        // New format: manager:unassigned:__HELLO__:SID:PID
        if (message.StartsWith("__HELLO__:"))
        {
            String helloPayload = message.Substring(10); // SID:PID
            unsigned pidSep = helloPayload.Find(':');
            String sessionId;
            int instancePid = 0;
            if (pidSep != String::NPOS)
            {
                sessionId = helloPayload.Substring(0, pidSep).Trimmed();
                instancePid = atoi(helloPayload.Substring(pidSep + 1).Trimmed().CString());
            }
            else
            {
                // Legacy format: __HELLO__:PID (no SID)
                instancePid = atoi(helloPayload.Trimmed().CString());
                sessionId = "pid_" + String(instancePid);
            }

            if (instancePid > 0 && !sessionId.Empty())
            {
                // Detect reconnect: same SID already known, OR same PID under a different SID
                // (hook invocations can produce varying SIDs for the same process).
                bool isReconnect = coderInstances_.Contains(sessionId);
                unsigned long long incumbentStart = 0;  // pinned starttime of the matched incumbent
                if (isReconnect)
                {
                    incumbentStart = coderInstances_[sessionId].startTime;
                }
                else
                {
                    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
                    {
                        if (it->second_.pid == instancePid)
                        {
                            isReconnect = true;
                            incumbentStart = it->second_.startTime;
                            break;
                        }
                    }
                }

#ifndef _WIN32
                // An elder-reclaim grants "coder" off the vacantElder_ record, but the
                // departed elder was ERASED from coderInstances_ — so isReconnect is
                // false for it and the reconnect gate below would skip it. THAT was the
                // usurpation hole: during the reclaim window any process claiming the
                // ex-elder's SID/PID got the elder slot unverified. Detect the reclaim
                // here and gate it with the SAME peer check — bound to the EX-ELDER's
                // PID, not the claimed instancePid. (The reclaim in AssignCoderRole can
                // match on sessionId alone, so verifying the claimed PID would let an
                // attacker pass by presenting the ex-elder's SID while descending only
                // from its own PID. Only a peer that genuinely descends from the
                // ex-elder process — i.e. the real elder after a transient blip — may
                // reclaim.)
                const bool isElderReclaim = !vacantElder_.sessionId.Empty() &&
                    (sessionId == vacantElder_.sessionId ||
                     (instancePid > 0 && instancePid == vacantElder_.pid));

                // Hardening: a reconnect reuses an existing session/PID, which would
                // either update an incumbent's recorded PID, migrate its role, or
                // (below) steal its relay fd; an elder-reclaim restores the "coder"
                // slot. These identity-changing paths require a peer whose kernel-
                // verified credentials match the protected identity: a trusted UID and a
                // process lineage that descends from the protected PID. Fresh
                // registrations stay loose — only the hijack paths are gated.
                if (isReconnect || isElderReclaim)
                {
                    // Bind verification to the PROTECTED identity, never the claimed
                    // pid: VerifyPeerLineage(peerPid, instancePid) is vacuous — a
                    // process always descends from the pid it supplies. For an
                    // elder-reclaim that's vacantElder_.pid; for a known-session
                    // reconnect it's the incumbent's STORED pid — otherwise a junior
                    // that learns the elder's (logged) sessionId could HELLO as that
                    // SID with its own pid and steal the role/relay fd. A pid-matched
                    // reconnect already has stored pid == claimed pid, so binding to
                    // the claim there is safe.
                    const int requirePid =
                        isElderReclaim ? vacantElder_.pid
                        : (coderInstances_.Contains(sessionId) ? coderInstances_[sessionId].pid
                                                               : instancePid);
                    // Pin against pid reuse: the protected pid's CURRENT starttime must
                    // still equal what we recorded. If the original process died and the
                    // pid was recycled (or it's simply gone), the starttime differs or
                    // reads 0 — reject. A genuine transient blip keeps the same live
                    // process, so its starttime is unchanged.
                    const unsigned long long requireStart =
                        isElderReclaim ? vacantElder_.startTime : incumbentStart;
                    bool peerVerified = havePeerCred &&
                                        IsTrustedPeerUid(peerUid) &&
                                        VerifyPeerLineage(peerPid, requirePid) &&
                                        requireStart != 0 &&
                                        ReadProcStartTime(requirePid) == requireStart;
                    if (!peerVerified)
                    {
                        const char* what = isElderReclaim ? "elder-reclaim" : "reconnect";
                        AppendLog("Security", String("REJECTED relay ") + what + " for SID " + sessionId +
                            " — peer PID " + String(peerPid) + " (uid " + String(peerUid) +
                            ") is not in the lineage of protected PID " + String(requirePid) +
                            ". Possible role/fd hijack; incumbent left untouched.");
                        URHO3D_LOGWARNINGF("[Security] REJECTED relay %s SID %s: peer PID %d uid %d "
                            "not descended from protected PID %d (possible hijack)",
                            what, sessionId.CString(), peerPid, peerUid, requirePid);
                        close(clientFd);
                        continue;  // no mutation, no role reply, incumbent fd preserved
                    }
                }
#endif

                String assignedRole = AssignCoderRole(sessionId, instancePid);
                spawnPending_ = false;  // new instance registered — release spawn lock
                AppendLog("Register", assignedRole + (isReconnect ? " reconnected" : " registered") +
                    " (SID " + sessionId + ", PID " + String(instancePid) + ")");
                URHO3D_LOGINFOF("[Register] %s %s (SID %s, PID %d)", assignedRole.CString(),
                    isReconnect ? "reconnected" : "registered", sessionId.CString(), instancePid);

#ifndef _WIN32
                // Store this fd as the persistent relay connection for this coder.
                // Close any previous fd for this session first.
                for (auto& ci : coderInstances_)
                {
                    if (ci.second_.role == assignedRole && ci.second_.relayFd >= 0 && ci.second_.relayFd != clientFd)
                    {
                        close(ci.second_.relayFd);
                        ci.second_.relayFd = -1;
                    }
                }
                if (coderInstances_.Contains(sessionId))
                {
                    coderInstances_[sessionId].relayFd = clientFd;
                    // Set non-blocking for future reads
                    int flags = fcntl(clientFd, F_GETFL, 0);
                    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
                    keepFd = true;
                    URHO3D_LOGINFOF("[Register] %s persistent relay fd=%d", assignedRole.CString(), clientFd);
                }
#endif

                // Send role assignment directly via clientFd
                // (bypass SendToSocket lookup — role was just assigned, use the fd we have)
                // Only send memories/tribal knowledge on FIRST registration, not reconnect.
#ifndef _WIN32
                {
                    auto writeDirect = [&](const String& msg) {
                        String flat = msg;
                        flat.Replace("\n", " ");
                        flat.Replace("\r", "");
                        flat += "\n";
                        write(clientFd, flat.CString(), flat.Length());
                    };
                    writeDirect("__ROLE__:" + assignedRole);

                    if (!isReconnect && workboardDB_.IsOpen())
                    {
                        String memories = workboardDB_.GetAllMemories();
                        if (!memories.Empty())
                            writeDirect("[SHARED MEMORY] " + memories);
                    }
                }
#else
                SendToSocket(assignedRole, "__ROLE__:" + assignedRole);

                if (!isReconnect && workboardDB_.IsOpen())
                {
                    String memories = workboardDB_.GetAllMemories();
                    if (!memories.Empty())
                        SendToSocket(assignedRole, "[SHARED MEMORY]\n" + memories);
                }
#endif
            }
            // Race fix: re-derive every role from a deterministic seniority rank on
            // each registration, so a racy duplicate from AssignCoderRole collapses at
            // once (the orphan -> next free coderN) instead of persisting as a twin.
            EnforceRoleInvariants();
            continue;
        }

        // ── GOODBYE: instance is shutting down ──
        if (message.StartsWith("__GOODBYE__:"))
        {
            String goodbyePayload = message.Substring(12);
            unsigned pidSep = goodbyePayload.Find(':');
            String sessionId;
            int instancePid = 0;
            if (pidSep != String::NPOS)
            {
                sessionId = goodbyePayload.Substring(0, pidSep).Trimmed();
                instancePid = atoi(goodbyePayload.Substring(pidSep + 1).Trimmed().CString());
            }

            if (!sessionId.Empty() && coderInstances_.Contains(sessionId))
            {
#ifndef _WIN32
                // Hardening: a GOODBYE force-removes a session, so an impostor could
                // unregister a live coder by sending its SID. Honor it only from a
                // peer that descends from the claimed PID. A genuine departing
                // instance that can't prove lineage is left to natural fd-death
                // pruning — so failing closed here can never silently drop a coder.
                bool peerVerified = havePeerCred &&
                                    IsTrustedPeerUid(peerUid) &&
                                    VerifyPeerLineage(peerPid, instancePid);
                if (!peerVerified)
                {
                    AppendLog("Security", "IGNORED GOODBYE for SID " + sessionId +
                        " — peer PID " + String(peerPid) + " (uid " + String(peerUid) +
                        ") not in lineage of claimed PID " + String(instancePid) +
                        ". Possible forced-unregister; left for fd-death pruning.");
                    URHO3D_LOGWARNINGF("[Security] IGNORED GOODBYE SID %s: peer PID %d uid %d "
                        "not descended from claimed PID %d (possible forced-unregister)",
                        sessionId.CString(), peerPid, peerUid, instancePid);
                    continue;
                }
#endif
                String role = coderInstances_[sessionId].role;
                coderInstances_.Erase(sessionId);
                AppendLog("Unregister", role + " departed (SID " + sessionId + ")");
                URHO3D_LOGINFOF("[Unregister] %s (SID %s)", role.CString(), sessionId.CString());
            }
            continue;
        }

        // __CONTEXT__:PCT:TOKENS:MAX — a coder reports its context-window usage
        if (message.StartsWith("__CONTEXT__:"))
        {
            Vector<String> cf = message.Substring(12).Split(':');  // PCT:TOKENS:MAX
            if (cf.Size() >= 1 && !sender.Empty())
            {
                int pct = atoi(cf[0].Trimmed().CString());
                for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
                {
                    if (it->second_.role == sender)
                    {
                        if (it->second_.contextPct != pct)
                        {
                            it->second_.contextPct = pct;
                            String detail = (cf.Size() >= 3) ? (" (" + cf[1] + "/" + cf[2] + ")") : "";
                            AppendLog("Context", sender + " " + String(pct) + "%" + detail);
                        }
                        break;
                    }
                }
            }
            continue;
        }

        // __BROADCAST_COLLECT__:tag:message — broadcast and collect replies
        if (message.StartsWith("__BROADCAST_COLLECT__:"))
        {
            String body = message.Substring(22);   // strip "__BROADCAST_COLLECT__:" (22 chars incl. colon)
            unsigned tagSep = body.Find(':');
            if (tagSep != String::NPOS)
            {
                String tag = body.Substring(0, tagSep).Trimmed();
                String broadcastMsg = body.Substring(tagSep + 1).Trimmed();

                // Store the pending collection
                BroadcastCollect bc;
                bc.tag = tag;
                bc.requester = sender;   // exclude the true sender only; an empty sender must NOT
                                         // default to "coder" (that wrongly excluded the elder and
                                         // failed to exclude the real requester)
                bc.timeout = 15.0f;  // 15 second collection window

                // Broadcast to all coders
                Vector<String> liveCoders = DiscoverCoderRoles();
                for (unsigned c = 0; c < liveCoders.Size(); ++c)
                {
                    if (liveCoders[c] != bc.requester)
                    {
                        SendToSocket(liveCoders[c], "__COLLECT_REQUEST__:" + tag + ":" + broadcastMsg);
                        bc.expectedFrom.Push(liveCoders[c]);
                    }
                }

                pendingCollects_.Push(bc);
                AppendLog("Collect", "Broadcast '" + tag + "' to " + String(bc.expectedFrom.Size()) + " coders, waiting " + String((int)bc.timeout) + "s");
            }
            continue;
        }

        // __COLLECT_REPLY__:tag:response — coder responding to a broadcast-collect
        if (message.StartsWith("__COLLECT_REPLY__:"))
        {
            String body = message.Substring(18);
            unsigned tagSep = body.Find(':');
            if (tagSep != String::NPOS)
            {
                String tag = body.Substring(0, tagSep).Trimmed();
                String response = body.Substring(tagSep + 1).Trimmed();
                String from = sender.Empty() ? "unknown" : sender;

                // Find matching pending collect
                for (unsigned c = 0; c < pendingCollects_.Size(); ++c)
                {
                    if (pendingCollects_[c].tag == tag)
                    {
                        pendingCollects_[c].replies.Push(from + ": " + response);
                        AppendLog("Collect", "Reply from " + from + " for '" + tag + "'");
                        break;
                    }
                }
            }
            continue;
        }

        // __ALL__ broadcast: deliver to every instance except the sender.
        if (target == "__ALL__")
        {
            AppendLog("Broadcast", message);

            // Deliver to each instance via persistent relay fds
#ifndef _WIN32
            SendToSocket("coders", message, sender);
#else
            // Windows: try all known coder pipes
            const char* names[] = { "coder", "coder2", "coder3", "coder4",
                                    "coder5", "coder6", "coder7", "coder8",
                                    "coder9", "coder10", "coder11", "coder12",
                                    "coder13", "coder14", "coder15", "coder16" };
            for (int i = 0; i < 16; ++i)
                SendToSocket(names[i], message);
#endif
            continue;
        }

        // ── Workboard mutations via relay (SQL-backed) ──

        // __WB_ADD_DONE__:task:owner:date:review:notes
        if (message.StartsWith("__WB_ADD_DONE__:"))
        {
            String body = message.Substring(16);
            Vector<String> fields = body.Split(':');
            if (fields.Size() >= 2 && workboardDB_.IsOpen())
            {
                String task = fields[0].Trimmed();
                String owner = fields[1].Trimmed();
                String summary = fields.Size() >= 5 ? fields[4].Trimmed() : String::EMPTY;
                workboardDB_.InsertTask("done", task, 0, "", "", "", summary, owner);
                AppendLog("Workboard", "Added to Done: " + task + " (" + owner + ")");
                LoadWorkboard();
            }
            continue;
        }

        // __WB_ADD_READY__:pri:plan:file:owner:review:summary
        if (message.StartsWith("__WB_ADD_READY__:"))
        {
            String body = message.Substring(17);
            Vector<String> fields = body.Split(':');
            if (fields.Size() >= 2 && workboardDB_.IsOpen())
            {
                int pri = fields[0].Trimmed().Length() > 0 ? atoi(fields[0].Trimmed().CString()) : 0;
                String task = fields[1].Trimmed();
                String summary = fields.Size() >= 6 ? fields[5].Trimmed() : String::EMPTY;
                String owner = fields.Size() >= 4 ? fields[3].Trimmed() : String::EMPTY;
                workboardDB_.InsertTask("planned", task, pri, "", "", "", summary, owner);
                AppendLog("Workboard", "Added to Planned: " + task);
                LoadWorkboard();
            }
            continue;
        }

        // __WB_ADD_INPROGRESS__:task:owner:started:review:notes
        if (message.StartsWith("__WB_ADD_INPROGRESS__:"))
        {
            String body = message.Substring(22);
            Vector<String> fields = body.Split(':');
            if (fields.Size() >= 2 && workboardDB_.IsOpen())
            {
                String task = fields[0].Trimmed();
                String owner = fields[1].Trimmed();
                String started = fields.Size() >= 3 ? fields[2].Trimmed() : String::EMPTY;
                String summary = fields.Size() >= 5 ? fields[4].Trimmed() : String::EMPTY;
                workboardDB_.InsertTask("in_progress", task, 0, "", "", "", summary, owner, started);
                AppendLog("Workboard", "Added to In Progress: " + task + " (" + owner + ")");
                LoadWorkboard();
            }
            continue;
        }

        // __WB_MOVE_UNVERIFIED__:task  (move a completed-but-untested task into Unverified)
        if (message.StartsWith("__WB_MOVE_UNVERIFIED__:"))
        {
            String task = message.Substring(23).Trimmed();
            if (!task.Empty() && workboardDB_.IsOpen())
            {
                int n = workboardDB_.MoveTask(task, "unverified");
                if (n > 0)
                {
                    AppendLog("Workboard", "Moved to Unverified: " + task);
                    LoadWorkboard();
                }
                else
                    AppendLog("Workboard", "MISS: no task matched '" + task + "' (move-unverified) — exact task_name required, nothing changed");
            }
            continue;
        }

        // __WB_MOVE_DONE__:task
        if (message.StartsWith("__WB_MOVE_DONE__:"))
        {
            String task = message.Substring(17).Trimmed();
            if (!task.Empty() && workboardDB_.IsOpen())
            {
                int n = workboardDB_.MoveTask(task, "done");
                if (n > 0)
                {
                    AppendLog("Workboard", "Moved to Done: " + task);
                    LoadWorkboard();
                }
                else
                    AppendLog("Workboard", "MISS: no task matched '" + task + "' (move-done) — exact task_name required, nothing changed");
            }
            continue;
        }

        // __WB_ASSIGN__:task:coder
        if (message.StartsWith("__WB_ASSIGN__:"))
        {
            String body = message.Substring(14);
            unsigned sep = body.Find(':');
            if (sep != String::NPOS && workboardDB_.IsOpen())
            {
                String task = body.Substring(0, sep).Trimmed();
                String coder = body.Substring(sep + 1).Trimmed();

                // Liveness rule (Leith): a task owned by a DIFFERENT, LIVE coder cannot
                // be taken. Only a dead owner's task — or an unowned / self-owned one —
                // is free to assume. Blocks stealing an active coder's active work;
                // lets a younger model pick up a fallen coder's task.
                String currentOwner = workboardDB_.GetTaskOwner(task);
                if (!currentOwner.Empty() && currentOwner != coder &&
                    currentOwner != sender && IsInstanceAlive(currentOwner))
                {
                    AppendLog("Workboard", "ASSIGN REJECTED: '" + task + "' owned by live coder '" + currentOwner + "'");
                    SendToSocket(sender, "ASSIGN REJECTED: '" + task + "' is owned by live coder '" + currentOwner + "' — cannot take an active coder's task.");
                    continue;
                }
                workboardDB_.MoveTask(task, "in_progress");
                // Update owner on the moved task
                workboardDB_.InsertTask("in_progress", task, 0, "", "", "", "", coder);
                AppendLog("Workboard", "ASSIGNED: " + task + " -> " + coder);
                LoadWorkboard();
                // Notify the assigned coder
                SendToSocket(coder, "TASK ASSIGNED: " + task + " — check the workboard and start working.");
            }
            continue;
        }

        // __WB_REMOVE__:match
        if (message.StartsWith("__WB_REMOVE__:"))
        {
            String match = message.Substring(14).Trimmed();
            if (!match.Empty() && workboardDB_.IsOpen())
            {
                int n = workboardDB_.RemoveTask(match);
                if (n > 0)
                {
                    AppendLog("Workboard", "Removed: " + match);
                    LoadWorkboard();
                }
                else
                    AppendLog("Workboard", "MISS: no task matched '" + match + "' (remove) — exact task_name required, nothing changed");
            }
            continue;
        }

        // __REMEMBER__:fact — coder stores a shared memory
        if (message.StartsWith("__REMEMBER__:"))
        {
            String fact = message.Substring(13).Trimmed();
            String source = sender.Empty() ? "unknown" : sender;
            if (!fact.Empty() && workboardDB_.IsOpen())
                workboardDB_.Remember(source, fact);
            continue;
        }

        // __HELLO__ handled above in the registration block

        // __BUILD_REQUEST__:target — coder requests a managed build
        if (message.StartsWith("__BUILD_REQUEST__:"))
        {
            String buildTarget = message.Substring(18).Trimmed();
            EnqueueBuild(buildTarget, target);
            continue;
        }

        // !reload — hot-reload finetuned model (can be sent to manager or yuki)
        if (message.Trimmed() == "!reload" && (target == "manager" || target == "yuki"))
        {
            String result = yukiLLM_.ReloadModel();
            URHO3D_LOGINFOF("LLM reload: %s", result.CString());
            AppendLog("Yuki", result);
            AppendYukiChat("System", result);

            // Notify the sender if known
            if (!sender.Empty())
                SendToSocket(sender, "Yuki reload: " + result);
            continue;
        }

        // Yuki — route to embedded LLM, not a standalone process
        if (target == "yuki")
        {
            if (yukiLLM_.IsModelLoaded() && !yukiLLM_.IsTrainingInProgress())
            {
                String from = sender.Empty() ? "relay" : sender;
                AppendYukiChat(from, message);
                yukiLLM_.QueueInference(message);
            }
            else
                AppendLog("Yuki", "Message dropped — model not ready");
            continue;
        }

        // Log the relay
        AppendLog(String("Relay \xe2\x86\x92 ") + target, message);

        // Deliver — sender excluded from broadcast (no echo-back)
        SendToSocket(target, message, sender);

#ifndef _WIN32
        // Close fire-and-forget connections (hooks). Persistent connections
        // (Claudette via __HELLO__) were already claimed above with keepFd=true.
        if (!keepFd)
            close(clientFd);
#endif
    }
}

// ============================================================================
// Broadcast Reply Collection
// ============================================================================

void WorkboardManager::ProcessPendingCollects(float timeStep)
{
    for (int i = (int)pendingCollects_.Size() - 1; i >= 0; --i)
    {
        pendingCollects_[i].timeout -= timeStep;

        // All replies received or timeout expired — deliver results
        bool allIn = pendingCollects_[i].replies.Size() >= pendingCollects_[i].expectedFrom.Size();
        if (allIn || pendingCollects_[i].timeout <= 0.0f)
        {
            BroadcastCollect& bc = pendingCollects_[i];

            String result = "[COLLECT:" + bc.tag + "] " +
                String(bc.replies.Size()) + "/" + String(bc.expectedFrom.Size()) + " replies";
            if (bc.timeout <= 0.0f && !allIn)
                result += " (timed out)";
            result += "\n";

            for (unsigned r = 0; r < bc.replies.Size(); ++r)
                result += "  " + bc.replies[r] + "\n";

            SendToSocket(bc.requester, result);
            AppendLog("Collect", "Delivered " + String(bc.replies.Size()) + " replies for '" + bc.tag + "' to " + bc.requester);

            pendingCollects_.Erase(i);
        }
    }
}

// ============================================================================
// Build Queue
// ============================================================================

void WorkboardManager::EnqueueBuild(const String& target, const String& requester)
{
    // Dedup — reject if already queued or currently building
    if (activeBuildTarget_ == target)
    {
        AppendLog("Build", target + " already building (requested by " + requester + ")");
        SendToSocket(requester, "Build REJECTED: " + target + " already building");
        return;
    }
    for (unsigned i = 0; i < buildQueue_.Size(); ++i)
    {
        if (buildQueue_[i].target == target)
        {
            AppendLog("Build", target + " already queued (requested by " + requester + ")");
            SendToSocket(requester, "Build REJECTED: " + target + " already queued at position " + String(i + 1));
            return;
        }
    }

    BuildQueueEntry entry;
    entry.target = target;
    entry.requester = requester;

    // Dependency: Urho3D always goes to front of queue
    if (target == "Urho3D" && !buildQueue_.Empty())
        buildQueue_.Insert(0, entry);
    else
        buildQueue_.Push(entry);

    unsigned pos = 0;
    for (unsigned i = 0; i < buildQueue_.Size(); ++i)
        if (buildQueue_[i].target == target) { pos = i + 1; break; }

    AppendLog("Build", "Queued " + target + " at position " + String(pos) + " (from " + requester + ")");
    SendToSocket(requester, "Build QUEUED: " + target + " at position " + String(pos) +
        (buildQueue_.Size() > 1 ? " (" + String(buildQueue_.Size()) + " in queue)" : ""));

    // If nothing building, start immediately
    if (activeBuildPid_ == 0)
        ProcessBuildQueue();
}

void WorkboardManager::ProcessBuildQueue()
{
#ifndef _WIN32
    // Check if active build finished
    if (activeBuildPid_ > 0)
    {
        int status = 0;
        pid_t result = waitpid(activeBuildPid_, &status, WNOHANG);
        if (result == 0)
            return;  // Still running

        // Build finished
        bool success = (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        String resultStr = success ? "SUCCESS" : "FAILED (exit " + String(WEXITSTATUS(status)) + ")";
        AppendLog("Build", activeBuildTarget_ + " " + resultStr);

        if (!activeBuildRequester_.Empty())
            SendToSocket(activeBuildRequester_, "Build DONE: " + activeBuildTarget_ + " " + resultStr);

        activeBuildPid_ = 0;
        activeBuildTarget_.Clear();
        activeBuildRequester_.Clear();
    }

    // Start next build
    if (buildQueue_.Empty())
        return;

    BuildQueueEntry next = buildQueue_[0];
    buildQueue_.Erase(0);

    auto* fs = GetSubsystem<FileSystem>();
    String scriptPath = fs->GetProgramDir() + "../../.claude/hooks/safe_build.sh";

    AppendLog("Build", "Starting " + next.target + " (requested by " + next.requester + ")");

    pid_t pid = fork();
    if (pid == 0)
    {
        // Child — exec safe_build.sh
        // Redirect stdout/stderr to /dev/null so build output doesn't pollute Manager
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
        execl("/bin/bash", "bash", scriptPath.CString(), next.target.CString(), (char*)nullptr);
        _exit(127);
    }
    else if (pid > 0)
    {
        activeBuildPid_ = pid;
        activeBuildTarget_ = next.target;
        activeBuildRequester_ = next.requester;
    }
    else
    {
        AppendLog("Build", "fork() failed for " + next.target);
        if (!next.requester.Empty())
            SendToSocket(next.requester, "Build FAILED: fork() error for " + next.target);
    }
#endif
}

void WorkboardManager::SendMessage(const String& target, const String& message)
{
    URHO3D_LOGINFOF("SendMessage: target=[%s] message=[%s]", target.CString(), message.CString());
    if (message.Empty())
        return;

    // Yuki is embedded — route directly to LLM
    if (target == "yuki")
    {
        if (yukiLLM_.IsModelLoaded())
            yukiLLM_.QueueInference(message);
        else
            AppendLog("Yuki", "Message dropped — model not ready");
        return;
    }

    bool injected = SendToSocket(target, message);

    if (!injected)
        AppendLog(String("Manager \xe2\x86\x92 ") + target + " [FAILED]", "TTY injection failed — no socket for " + target);
    else
        AppendLog(String("Manager \xe2\x86\x92 ") + target + " [TTY]", message);
}

void WorkboardManager::HandleSendCoder(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (!messageInput_) return;
    String text = messageInput_->GetText().Trimmed();
    if (text.Empty()) return;

    // Unified send — route based on receiver dropdown selection
    String target = GetSelectedCoderRole();  // Returns selected item text (lowercase)
    if (target.Empty())
    {
        AppendLog("System", "No receiver selected");
        return;
    }

    if (target == "broadcast")
    {
        for (const String& role : knownCoderRoles_)
        {
            if (IsInstanceAlive(role))
                SendMessage(role, text);
        }
        for (const String& role : knownUnassignedRoles_)
        {
            if (IsInstanceAlive(role))
                SendMessage(role, text);
        }
        // CC Yuki on broadcasts via embedded LLM
        if (yukiLLM_.IsModelLoaded())
            yukiLLM_.QueueInference("cc:" + text);
    }
    else if (target == "yuki")
    {
        // Direct to embedded Yuki
        if (yukiLLM_.IsModelLoaded())
        {
            AppendYukiChat("Leith", text);
            yukiLLM_.QueueInference(text);
        }
        else
            AppendYukiChat("System", "Model not loaded");
    }
    else if (target == "unassigned")
    {
        for (const String& role : knownUnassignedRoles_)
        {
            if (IsInstanceAlive(role))
                SendMessage(role, text);
        }
    }
    else
    {
        SendMessage(target, text);
    }
    messageInput_->SetText("");
}

// Legacy handlers kept as stubs — routing now goes through unified HandleSendCoder
// HandleSendPlanner removed — planner role no longer exists
void WorkboardManager::HandleSendUnassigned(StringHash, VariantMap&) {}
void WorkboardManager::HandleSendBroadcast(StringHash, VariantMap&) {}

void WorkboardManager::HandleClearFileLocks(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    String lockDir = ipcDir_ + "locks";
    int cleared = 0;

    auto* fs = GetSubsystem<FileSystem>();
    if (fs)
    {
        Vector<String> entries;
        fs->ScanDir(entries, lockDir, "*", SCAN_FILES | SCAN_DIRS, false);
        for (const String& entry : entries)
        {
            if (entry == "." || entry == "..")
                continue;
            String fullPath = lockDir + "/" + entry;
            if (fs->DirExists(fullPath))
            {
                // Remove contents first
                Vector<String> inner;
                fs->ScanDir(inner, fullPath, "*", SCAN_FILES, false);
                for (const String& f : inner)
                {
                    if (f != "." && f != "..")
                        fs->Delete(fullPath + "/" + f);
                }
                fs->SystemCommand("rmdir \"" + fullPath + "\"");
                cleared++;
            }
            else
            {
                fs->Delete(fullPath);
                cleared++;
            }
        }
    }

    AppendLog("Manager", cleared > 0
        ? String("Cleared ") + String(cleared) + " file lock(s)"
        : "No file locks to clear");

    // ── Break the ACL-mask layer too — "all locks must break" (Leith's directive) ──
    // Clearing flocks above only releases the /tmp lock dir. The other lock layer is
    // the POSIX ACL mask: claude has granted "user:claude:rw-" entries, but a tightened
    // "mask::r--" leaves them effective read-only. Lift the masks so the grants take.
    //
    // This is the universal hammer. It re-applies the claude grant + recalculates the
    // mask across the work tree (Source, bin, .claude) — INCLUDING .claude/hooks, the
    // sandbox-enforcement scripts. It therefore re-arms claude's ability to edit its
    // own guards. That is intentional and Leith-only: this handler runs as the Manager's
    // user (leith) — the sole user who can change ACLs on leith-owned files — and it
    // fires only when Leith clicks Break Locks. The manual click is the control.
    //
    // rwX = read+write always, execute only where the bit already exists (preserves
    // script exec bits; never makes a source file executable).
    {
        auto* fsAcl = GetSubsystem<FileSystem>();
        String root = projectRoot_;  // ends with '/'
        String cmd = "setfacl -R -m u:claude:rwX "
                     "\"" + root + "Source\" "
                     "\"" + root + "bin\" "
                     "\"" + root + ".claude\" 2>&1";
        int rc = fsAcl ? fsAcl->SystemCommand(cmd) : -1;
        AppendLog("Manager", rc == 0
            ? "Lock break: ACL masks lifted across Source/bin/.claude (incl hooks) — claude write restored"
            : String("Lock break: setfacl returned ") + String(rc) + " — ACL masks may not be fully lifted");
    }
}

void WorkboardManager::HandleSpawnCoder(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    // Block if a spawn is already in flight (prevents double-tap race)
    if (spawnPending_)
    {
        AppendLog("Manager", "Spawn refused: spawn already pending");
        return;
    }

    // Enforce local instance cap — count pending as +1
    Vector<String> liveCoders = DiscoverCoderRoles();
    if (liveCoders.Size() >= maxLocalCoders_)
    {
        AppendLog("Manager", "Spawn refused: " + String(liveCoders.Size()) + "/" + String(maxLocalCoders_) + " local coders already running");
        return;
    }

    String scriptPath = GetProjectRoot() + "/.claude/hooks/claude_ipc.sh";

    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->FileExists(scriptPath))
    {
        AppendLog("Manager", "Cannot spawn coder: " + scriptPath + " not found");
        return;
    }

    // Lock out further spawns until the new instance registers
    spawnPending_ = true;
    spawnPendingTimer_ = 30.0f;  // timeout: 30s max wait for registration

    // Capture output from spawn script for diagnostics
    String spawnLog = ipcDir_ + "spawn_stderr.log";
    int ret = fs->SystemCommand(scriptPath + " spawn-coder 2>" + spawnLog);
    if (ret == 0)
    {
        AppendLog("Manager", "Spawn Coder command executed (" + String(liveCoders.Size() + 1) + "/" + String(maxLocalCoders_) + ")");
    }
    else
    {
        // Read the spawn error log for diagnostics
        String errMsg;
        if (fs->FileExists(spawnLog))
        {
            File errFile(context_, spawnLog, FILE_READ);
            if (errFile.IsOpen())
            {
                while (!errFile.IsEof())
                    errMsg += errFile.ReadLine() + " ";
                errMsg = errMsg.Trimmed();
            }
        }
        if (errMsg.Empty())
            errMsg = "exit code " + String(ret);
        AppendLog("Manager", "Spawn Coder FAILED: " + errMsg);
        spawnPending_ = false;  // release lock on failure
    }
}

void WorkboardManager::HandleLaunchYuki(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    // Yuki self-guards as a single instance at its own startup, and the OS
    // releases that lock when Yuki exits — so it never goes stale. Manager just
    // attempts the launch; a duplicate Yuki refuses to start and exits on its
    // own. (The old in-memory yukiRunning_ latch never reset, so it wedged on
    // "already running" forever once set.)
    auto* fs = GetSubsystem<FileSystem>();
    String yukiPath = fs->GetProgramDir() + "Yuki";

    if (!fs->FileExists(yukiPath))
    {
        AppendLog("Manager", "Yuki binary not found: " + yukiPath);
        return;
    }

    int ret = fs->SystemCommand(yukiPath + " &");
    if (ret == 0)
        AppendLog("Manager", "Yuki launched");
    else
        AppendLog("Manager", "Yuki launch failed (exit code " + String(ret) + ")");
}

void WorkboardManager::HandleCoderCapMinus(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (maxLocalCoders_ > 1)
    {
        --maxLocalCoders_;
        UpdateCoderCapText();
    }
}

void WorkboardManager::HandleCoderCapPlus(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (maxLocalCoders_ < 8)
    {
        ++maxLocalCoders_;
        UpdateCoderCapText();
    }
}

void WorkboardManager::UpdateCoderCapText()
{
    if (!coderCapText_)
        return;
    Vector<String> liveCoders = DiscoverCoderRoles();
    coderCapText_->SetText(String(liveCoders.Size()) + "/" + String(maxLocalCoders_));

    // Write cap to file so shell scripts can enforce it
    String capPath = ipcDir_ + "coder_cap";
    File capFile(context_, capPath, FILE_WRITE);
    if (capFile.IsOpen())
        capFile.WriteLine(String(maxLocalCoders_));
}

void WorkboardManager::HandleToggleScreenshots(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    auto* fs = GetSubsystem<FileSystem>();
    String flagPath = ipcDir_ + "screenshots_blocked";

    screenshotsBlocked_ = !screenshotsBlocked_;

    if (screenshotsBlocked_)
    {
        // Create the block flag — all Claudette hooks check for this
        File flagFile(context_, flagPath, FILE_WRITE);
        if (flagFile.IsOpen())
            flagFile.WriteLine("blocked");
        AppendLog("Manager", "Screenshots BLOCKED — all instances blinded");
    }
    else
    {
        fs->Delete(flagPath);
        AppendLog("Manager", "Screenshots ALLOWED");
    }

    // Update button label
    if (screenshotToggleBtn_)
    {
        auto* text = screenshotToggleBtn_->GetChildStaticCast<Text>(0);
        if (text)
            text->SetText(screenshotsBlocked_ ? "Snoop: OFF" : "Snoop: ON");
    }
}

void WorkboardManager::HandleToggleYuki(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    if (yukiLLM_.IsModelLoaded())
    {
        yukiLLM_.UnloadModel();
        AppendLog("Yuki", "Model unloaded");
        AppendYukiChat("System", "AI offline");
        if (yukiToggleBtnText_)
            yukiToggleBtnText_->SetText("Load AI");
    }
    else
    {
        String modelPath = FindYukiModel();
        if (modelPath.Empty())
        {
            AppendLog("Yuki", "No GGUF model found in YukiHoho/models/");
            return;
        }
        yukiLLM_.LoadModelAsync(modelPath);
        AppendLog("Yuki", "Loading: " + GetFileNameAndExtension(modelPath));
        AppendYukiChat("System", "Loading AI...");
        if (yukiToggleBtnText_)
            yukiToggleBtnText_->SetText("Unload");
    }
}

// ============================================================================
// Message Log
// ============================================================================

void WorkboardManager::AppendLog(const String& source, const String& message)
{
    if (!logListView_)
        return;

    // Skip noisy turn-complete messages from the Stop hook
    if (message.Contains("Turn complete at"))
        return;

    // Timestamp
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char ts[16];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);

    // Format: "HH:MM:SS [Source] message"
    String prefix = String(ts) + "  ";

    String srcPad = source;
    while (srcPad.Length() < 10)
        srcPad += " ";
    prefix += "[" + srcPad + "] ";

    auto* item = new Text(context_);
    item->SetFont(font_, currentFontSize_ - 1);
    item->SetText(prefix + message);
    item->SetColor(LogColorForSource(source));
    item->SetWordwrap(true);
    if (logPanel_)
        item->SetMaxWidth(logPanel_->GetWidth() - 30);
    logListView_->AddItem(item);

    while (logListView_->GetNumItems() > MAX_LOG_LINES)
        logListView_->RemoveItem((i32)0);

    logListView_->EnsureItemVisibility(logListView_->GetNumItems() - 1);
}

Color WorkboardManager::LogColorForSource(const String& source)
{
    // Color by sender (first word before arrow)
    if (source.Contains("Yuki") || source.Contains("yuki"))
        return Color(1.0f, 0.5f, 0.8f);          // pink/magenta
    if (source.Contains("Coder"))
        return Color(0.3f, 0.9f, 1.0f);         // cyan
    if (source == "Coder" || source == "coder")
        return Color(1.0f, 0.8f, 0.3f);          // amber — elder coder
    if (source.Contains("Manager"))
        return Color(0.6f, 1.0f, 0.6f);          // light green
    if (source.Contains("Unassigned"))
        return Color(0.7f, 0.7f, 1.0f);          // light blue
    if (source == "Broadcast")
        return Color(1.0f, 0.55f, 0.45f);        // salmon
    if (source == "Download")
        return Color(0.5f, 0.85f, 1.0f);         // sky blue
    return Color(0.5f, 0.5f, 0.5f);              // gray (System)
}

// ============================================================================
// Beacon & Liveness
// ============================================================================

void WorkboardManager::UpdateBeacon()
{
    auto* network = GetSubsystem<Network>();
    if (!network || !network->IsServerRunning())
        return;

    VariantMap beacon;
    beacon["Service"]    = String("WorkboardManager");
    beacon["Version"]    = String("1.0");
    beacon["Yuki"]       = yukiLLM_.IsModelLoaded() ? String("ONLINE") : String("OFFLINE");
    // Report all known unassigned roles
    for (const String& role : knownUnassignedRoles_)
    {
        String key = role;
        if (!key.Empty()) key[0] = (char)toupper(key[0]);
        beacon[key] = IsInstanceAlive(role) ? String("ONLINE") : String("OFFLINE");
    }
    if (knownUnassignedRoles_.Empty())
        beacon["Unassigned"] = String("OFFLINE");
    // Report all known coder roles
    for (const String& role : knownCoderRoles_)
    {
        String key = role;
        if (!key.Empty()) key[0] = (char)toupper(key[0]);
        beacon[key] = IsInstanceAlive(role) ? String("ONLINE") : String("OFFLINE");
    }
    network->SetDiscoveryBeacon(beacon);
}

void WorkboardManager::PollYukiInference()
{
    if (!yukiLLM_.IsInferenceComplete())
        return;

    String result = yukiLLM_.TakeResult();
    if (result.Empty())
        return;

    // Handle remember mode — extract training pairs from Yuki's Q&A output
    if (yukiLLM_.IsRememberInFlight())
    {
        yukiLLM_.ExtractAndSaveTrainingPairs(result);
        AppendYukiChat("Yuki", "[Remembered]");
        return;
    }

    // Show response in chat panel
    AppendYukiChat("Yuki", result);
    AppendLog("Yuki", result.Length() > 120 ? result.Substring(0, 120) + "..." : result);

    // Execute any tool commands in the output
    String toolResults = yukiLLM_.ExecuteTools(result);
    if (!toolResults.Empty())
        AppendLog("Yuki-Tools", toolResults.Length() > 200 ? toolResults.Substring(0, 200) + "..." : toolResults);

    // Auto-continue if tools produced results or remember is pending
    if (yukiLLM_.ShouldAutoContinue())
        yukiLLM_.AutoContinue();
}

float WorkboardManager::GetLastActivity(const String& role)
{
    if (role == "unassigned")
        return lastUnassignedActivity_;
    if (role.StartsWith("coder"))
    {
        auto it = coderActivityTimers_.Find(role);
        if (it != coderActivityTimers_.End())
            return it->second_;
    }
    return 999.0f;
}

void WorkboardManager::CleanupLegacyPIDFiles()
{
    // One-shot: delete all legacy .pid, .role, and .heartbeat files.
    // No PID files are used anymore — singleton uses abstract socket,
    // instance tracking is in-memory.
    auto* fs = GetSubsystem<FileSystem>();
    String instDir = ipcDir_ + "instances/";

    Vector<String> files;
    fs->ScanDir(files, instDir, "*.pid", SCAN_FILES, false);
    for (const String& f : files)
        fs->Delete(instDir + f);

    files.Clear();
    fs->ScanDir(files, instDir, "*.role", SCAN_FILES, false);
    for (const String& f : files)
        fs->Delete(instDir + f);

    files.Clear();
    fs->ScanDir(files, instDir, "*.heartbeat", SCAN_FILES, false);
    for (const String& f : files)
        fs->Delete(instDir + f);
}

// ============================================================================
// Yuki Training Lump Collection
// ============================================================================

void WorkboardManager::CheckTrainingLump()
{
#ifndef _WIN32
    String collectedPath = projectRoot_ + "/Source/Tools/YukiHoho/training/yuki_collected.jsonl";
    auto* fs = GetSubsystem<FileSystem>();

    if (!fs->FileExists(collectedPath))
        return;

    // Check if a fine-tune is already running (lock file exists while active,
    // finetune.sh deletes it on completion)
    String lockPath = projectRoot_ + "/Source/Tools/YukiHoho/training/.finetune.lock";
    if (fs->FileExists(lockPath))
        return;  // Fine-tune still running

    // Count lines
    File f(context_, collectedPath, FILE_READ);
    if (!f.IsOpen())
        return;

    unsigned lineCount = 0;
    while (!f.IsEof())
    {
        String line = f.ReadLine().Trimmed();
        if (!line.Empty())
            ++lineCount;
    }
    f.Close();

    if (lineCount < TRAINING_LUMP_THRESHOLD)
        return;

    // Threshold reached — archive and kick digest
    time_t now = time(nullptr);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", localtime(&now));
    String archivePath = projectRoot_ + "/Source/Tools/YukiHoho/training/yuki_collected_" + String(ts) + ".jsonl";
    fs->Rename(collectedPath, archivePath);
    AppendLog("Yuki", "Training lump: " + String(lineCount) + " pairs — digesting");

    String scriptPath = projectRoot_ + "/Source/Tools/YukiHoho/scripts/finetune.sh";
    if (fs->FileExists(scriptPath))
    {
        // finetune.sh creates .finetune.lock on start, removes on completion
        String cmd = "nohup " + scriptPath + " " + archivePath + " > /tmp/yuki_finetune.log 2>&1 &";
        fs->SystemCommand(cmd);
        AppendLog("Yuki", "Fine-tune kicked in background");
    }
    else
    {
        AppendLog("Yuki", "Training data archived — no finetune.sh yet");
    }
#endif
}

// ============================================================================
// Workboard Task Lookup
// ============================================================================

String WorkboardManager::GetCurrentTask(const String& owner)
{
    // Search In Progress table for rows where the Owner column matches
    for (const auto& sec : sections_)
    {
        if (!sec.title.Contains("In Progress"))
            continue;

        // Find the Owner column index
        int ownerCol = -1;
        for (unsigned i = 0; i < sec.headers.Size(); ++i)
        {
            if (sec.headers[i].Trimmed().ToLower().Contains("owner"))
            {
                ownerCol = (int)i;
                break;
            }
        }
        if (ownerCol < 0)
            break;

        // Find the Task column (usually column 0)
        int taskCol = 0;
        for (unsigned i = 0; i < sec.headers.Size(); ++i)
        {
            if (sec.headers[i].Trimmed().ToLower().Contains("task"))
            {
                taskCol = (int)i;
                break;
            }
        }

        // Collect all tasks for this owner
        String tasks;
        for (const auto& row : sec.rows)
        {
            if (ownerCol < (int)row.cells.Size() &&
                row.cells[ownerCol].Trimmed().ToLower() == owner.ToLower())
            {
                if (taskCol < (int)row.cells.Size())
                {
                    if (!tasks.Empty()) tasks += ", ";
                    tasks += row.cells[taskCol].Trimmed();
                }
            }
        }
        return tasks;
    }
    return String::EMPTY;
}

// ============================================================================
// Multi-Coder Discovery
// ============================================================================

Vector<String> WorkboardManager::DiscoverCoderRoles()
{
    Vector<String> roles;

    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
    {
        const CoderInstance& inst = it->second_;
        if (!inst.role.StartsWith("coder"))
            continue;
        if (!IsInstanceAlive(inst.role))
            continue;

        if (!roles.Contains(inst.role))
        {
            roles.Push(inst.role);
            if (coderActivityTimers_.Find(inst.role) == coderActivityTimers_.End())
                coderActivityTimers_[inst.role] = 0.0f;
        }
    }

    Sort(roles.Begin(), roles.End());
    return roles;
}

// ============================================================================
// Multi-Unassigned Discovery
// ============================================================================

Vector<String> WorkboardManager::DiscoverUnassignedRoles()
{
    Vector<String> roles;

    for (auto it = coderInstances_.Begin(); it != coderInstances_.End(); ++it)
    {
        const CoderInstance& inst = it->second_;
        if (!inst.role.StartsWith("unassigned"))
            continue;
        if (!IsInstanceAlive(inst.role))
            continue;

        if (!roles.Contains(inst.role))
            roles.Push(inst.role);
    }

    Sort(roles.Begin(), roles.End());
    return roles;
}

String WorkboardManager::GetSelectedCoderRole()
{
    if (!coderDropdown_ || coderDropdown_->GetNumItems() == 0)
        return String::EMPTY;

    unsigned sel = coderDropdown_->GetSelection();
    auto* item = coderDropdown_->GetItem(sel);
    if (!item)
        return String::EMPTY;

    auto* text = dynamic_cast<Text*>(item);
    if (!text)
        return String::EMPTY;

    return text->GetText().ToLower().Trimmed();
}

// ============================================================================
// Event Handlers
// ============================================================================

void WorkboardManager::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Increment liveness timers
    lastUnassignedActivity_ += timeStep;
    for (auto it = coderActivityTimers_.Begin(); it != coderActivityTimers_.End(); ++it)
        it->second_ += timeStep;

    if (autoSpawnCooldown_ > 0.0f)
        autoSpawnCooldown_ -= timeStep;

    // Spawn pending timeout — if the spawned coder never registered, release the lock
    if (spawnPending_)
    {
        spawnPendingTimer_ -= timeStep;
        if (spawnPendingTimer_ <= 0.0f)
        {
            spawnPending_ = false;
            AppendLog("Manager", "Spawn pending timeout — lock released");
        }
    }

    CheckDownloadProgress();

    relayPollAccumulator_ += timeStep;
    if (relayPollAccumulator_ >= RELAY_POLL_INTERVAL)
    {
        relayPollAccumulator_ = 0.0f;
        PollRelaySocket();
    }

    ProcessBuildQueue();

    refreshAccumulator_ += timeStep;
    if (refreshAccumulator_ >= REFRESH_INTERVAL)
    {
        refreshAccumulator_ = 0.0f;

        LoadWorkboard();
        ScanPlanFiles();
        RefreshInstanceStatus();
        SampleSystemStats();

        // Check for PAKE key — pick up new key, detect rotation, or stay quiet
        {
            static constexpr unsigned PAKE_KEY_SIZE = 4096;
            auto* fs = GetSubsystem<FileSystem>();
            if (fs && fs->FileExists("/etc/urho3d/pake.key"))
            {
                File keyFile(context_, "/etc/urho3d/pake.key", FILE_READ);
                if (keyFile.IsOpen() && keyFile.GetSize() >= PAKE_KEY_SIZE)
                {
                    Vector<unsigned char> raw(PAKE_KEY_SIZE);
                    keyFile.Read(raw.Buffer(), PAKE_KEY_SIZE);
                    unsigned char newHash[32];
                    SHA256Hash(raw.Buffer(), PAKE_KEY_SIZE, newHash);
                    if (!pakeSecretValid_ || memcmp(newHash, pakeSecretHash_, 32) != 0)
                    {
                        memcpy(pakeSecretHash_, newHash, 32);
                        bool wasNew = !pakeSecretValid_;
                        pakeSecretValid_ = true;
                        URHO3D_LOGINFO(wasNew ? "PAKE key found — encryption enabled" : "PAKE key rotated");
                        AppendLog("Security", wasNew ? "PAKE key activated" : "PAKE key rotated");
                    }
                }
            }
        }
    }

    // SQL is the sole authority — no markdown reconciliation.

    // ── Yuki training lump check ──
    trainingCheckAccumulator_ += timeStep;
    if (trainingCheckAccumulator_ >= TRAINING_CHECK_INTERVAL)
    {
        trainingCheckAccumulator_ = 0.0f;
        CheckTrainingLump();
    }

    // ── Yuki memory: periodic WAL flush ──
    // PASSIVE is non-blocking and moves committed frames into the main .db, so the
    // file stays self-contained even while Yuki keeps writing. Truncation is left
    // for shutdown (a periodic TRUNCATE could stall this loop on busy_timeout).
    walCheckpointAccumulator_ += timeStep;
    if (walCheckpointAccumulator_ >= WAL_CHECKPOINT_INTERVAL)
    {
        walCheckpointAccumulator_ = 0.0f;
        if (yukiMemoryDB_.IsOpen())
            yukiMemoryDB_.Checkpoint(false);  // PASSIVE
    }

    // ── Karen: keep scanning for a telemetry emitter until one attaches ──
    karenDiscoverAccumulator_ += timeStep;
    if (karenDiscoverAccumulator_ >= KAREN_DISCOVER_INTERVAL)
    {
        karenDiscoverAccumulator_ = 0.0f;
        if (!karenConnected_)
            if (auto* net = GetSubsystem<Network>())
                // keep this port set in sync with KAREN_SCAN_PORTS (declared below)
                for (unsigned short p : { (unsigned short)KAREN_PORT, (unsigned short)7879 })
                    net->DiscoverHosts(p);
    }

    // ── Karen: surface the latest plot values so telemetry is actually visible ──
    // (HandleKarenMessage only stores them; without this you'd see "Attached" but
    // never the numbers). One throttled summary line while attached.
    karenPlotLogAccumulator_ += timeStep;
    if (karenPlotLogAccumulator_ >= KAREN_PLOT_LOG_INTERVAL)
    {
        karenPlotLogAccumulator_ = 0.0f;
        if (karenConnected_ && !karenPlots_.Empty())
        {
            String line;
            for (auto it = karenPlots_.Begin(); it != karenPlots_.End(); ++it)
                line += (line.Empty() ? "" : "  ") + it->first_ + "=" + String(it->second_);
            AppendLog("Karen", "[" + karenEmitter_ + "] " + line);
        }
    }



    // ── Broadcast reply collection timeout ──
    ProcessPendingCollects(timeStep);

    // ── Embedded Yuki: tick cooldowns and poll inference ──
    yukiLLM_.Tick(timeStep);
    PollYukiInference();
}

static void UpdateAnchoringRecursive(UIElement* element)
{
    if (!element)
        return;

    if (element->GetEnableAnchor())
        element->UpdateAnchoring();

    const auto& children = element->GetChildren();
    for (unsigned i = 0; i < children.Size(); ++i)
        UpdateAnchoringRecursive(children[i]);
}

void WorkboardManager::HandleScreenMode(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    // Window resized — recursively force all anchored elements to recompute
    auto* root = GetSubsystem<UI>()->GetRoot();
    if (!root)
        return;

    UpdateAnchoringRecursive(root);
}

void WorkboardManager::HandleKeyDown(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace KeyDown;
    int key = eventData[P_KEY].GetI32();

    if (key == KEY_ESCAPE)
        engine_->Exit();
    else if (key == KEY_F5)
    {
        LoadWorkboard();
        ScanPlanFiles();
        RefreshInstanceStatus();
        AppendLog("System", "Refreshed.");
    }
}

// ============================================================================
// Karen telemetry listener (phase 1: receive only)
// ============================================================================
// Manager is a Karen viewer. These mirror the message IDs in
// Source/Urho3D/Core/KarenClient.cpp (the speaker side).
static const int MSG_KAREN_WELCOME    = MSG_USER + 300;
static const int MSG_KAREN_FRAME      = MSG_USER + 301;
static const int MSG_KAREN_ZONE_BEGIN = MSG_USER + 302;
static const int MSG_KAREN_ZONE_END   = MSG_USER + 303;
static const int MSG_KAREN_PLOT       = MSG_USER + 304;
static const int MSG_KAREN_MESSAGE    = MSG_USER + 305;
// We send this to the emitter on attach to identify as a real Karen consumer, so it collects/sends telemetry.
static const int MSG_KAREN_SUBSCRIBE  = MSG_USER + 306;
// Collector (inversion): an injected Karen client sends this on connect to tag its outbound stream with
// its source app name (WriteString appName). The client-dial counterpart of MSG_KAREN_SUBSCRIBE.
static const int MSG_KAREN_HELLO      = MSG_USER + 307;

// Urho's DiscoverHosts pings ONE port, so we scan the known karen-host ports:
// KAREN_PORT for a standalone Karen emitter, plus the server ports of parasitized
// hosts (Yuki rides its own 7879). Extend this list as more apps host the parasite.
static const unsigned short KAREN_SCAN_PORTS[] = { KAREN_PORT, 7879 };

void WorkboardManager::StartKarenListener()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // Client-peer events: discovery + the connection to the emitter we attach to.
    // Distinct from the server-side E_CLIENTCONNECTED the workboard uses, so no
    // collision. HandleKarenMessage filters by message ID, leaving other traffic
    // untouched.
    SubscribeToEvent(E_NETWORKHOSTDISCOVERED, URHO3D_HANDLER(WorkboardManager, HandleKarenHostDiscovered));
    SubscribeToEvent(E_SERVERCONNECTED,       URHO3D_HANDLER(WorkboardManager, HandleKarenConnectionStatus));
    SubscribeToEvent(E_SERVERDISCONNECTED,    URHO3D_HANDLER(WorkboardManager, HandleKarenConnectionStatus));
    SubscribeToEvent(E_CONNECTFAILED,         URHO3D_HANDLER(WorkboardManager, HandleKarenConnectionStatus));
    SubscribeToEvent(E_NETWORKMESSAGE,        URHO3D_HANDLER(WorkboardManager, HandleKarenMessage));

    for (unsigned short p : KAREN_SCAN_PORTS)
        network->DiscoverHosts(p);
    AppendLog("Karen", "Listener active — scanning for KarenTelemetry-capable hosts");
}

void WorkboardManager::HandleKarenHostDiscovered(StringHash, VariantMap& eventData)
{
    using namespace NetworkHostDiscovered;
    if (karenConnected_)
        return;  // already attached to an emitter

    VariantMap beacon = eventData[P_BEACON].GetVariantMap();
    if (beacon["Karen"].GetString() != "1")
        return;  // host doesn't advertise the KarenTelemetry capability — ignore

    // Connect to the port discovery reported, not a hardcoded one: a parasitized host
    // rides its own server port (Yuki = 7879), a standalone emitter uses KAREN_PORT.
    String addr = eventData[P_ADDRESS].GetString();
    int port = eventData[P_PORT].GetI32();
    karenEmitter_ = beacon["KarenName"].GetString();
    if (karenEmitter_.Empty())
        karenEmitter_ = beacon["Name"].GetString();
    GetSubsystem<Network>()->Connect(addr, (unsigned short)port, nullptr);
    AppendLog("Karen", "Emitter '" + karenEmitter_ + "' found at " + addr + ":" + String(port) + " — connecting");
}

void WorkboardManager::HandleKarenConnectionStatus(StringHash eventType, VariantMap&)
{
    if (eventType == E_SERVERCONNECTED)
    {
        karenConnected_ = true;
        // Identify as a real Karen consumer so the emitter starts collecting + streaming telemetry. Reliable:
        // a dropped subscribe would leave the emitter silent. Until this lands, the emitter treats us as a
        // plain peer and sends nothing — which is exactly the "no listener, don't collect" behavior we want.
        if (auto* conn = GetSubsystem<Network>()->GetServerConnection())
        {
            VectorBuffer sub;
            conn->SendMessage(MSG_KAREN_SUBSCRIBE, true, true, sub);
        }
        AppendLog("Karen", "Attached to emitter '" + karenEmitter_ + "' — subscribed");
    }
    else  // E_SERVERDISCONNECTED or E_CONNECTFAILED
    {
        if (karenConnected_)
            AppendLog("Karen", "Emitter '" + karenEmitter_ + "' detached");
        karenConnected_ = false;
        karenPlots_.Clear();
        karenEmitter_ = String::EMPTY;
        // HandleUpdate re-scans on its cadence to find the next emitter.
    }
}

void WorkboardManager::HandleKarenMessage(StringHash, VariantMap& eventData)
{
    using namespace NetworkMessage;
    const int msgID = eventData[P_MESSAGEID].GetI32();
    // Karen telemetry occupies MSG_USER+300..307 (WELCOME..HELLO), disjoint from the workboard's
    // named remote events — so this handler receives BOTH the phase-1 discover-and-dial stream (over
    // our client peer) AND the phase-2 collector stream (emitters that dial IN to our 31337 server).
    if (msgID < MSG_KAREN_WELCOME || msgID > MSG_KAREN_HELLO)
        return;  // not Karen telemetry — leave other handlers untouched

    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    const Vector<byte>& data = eventData[P_DATA].GetBuffer();
    MemoryBuffer msg(data);

    // Prefer the per-connection emitter label (collector/inbound path); fall back to the single
    // karenEmitter_ used by the phase-1 dial-out path.
    String emitterLabel = karenEmitter_;
    if (conn)
    {
        auto eit = karenEmitters_.Find(conn);
        if (eit != karenEmitters_.End())
            emitterLabel = eit->second_;
    }

    switch (msgID)
    {
    case MSG_KAREN_HELLO:
    {
        // Collector (inversion) path: an injected Karen client dialed IN to our 31337 server and
        // identifies its source app. Tag the connection so its telemetry is labelled, and reclassify
        // it OUT of the workboard-client roster — a Karen emitter is not a coder, even though the
        // LAN-open handshake auto-registered it during E_CLIENTIDENTITY. HELLO arrives AFTER identity,
        // so we correct here rather than requiring identity-first (don't gate on prior WB identity).
        String appName = msg.ReadString();
        if (conn)
        {
            karenEmitters_[conn] = appName;
            auto wit = wbClients_.Find(conn);
            if (wit != wbClients_.End())
            {
                wbClients_.Erase(wit);
                PushClientListToAll();
            }
        }
        karenEmitter_ = appName;   // latest emitter, for existing display paths
        AppendLog("Karen", "Emitter '" + appName + "' dialed into collector (31337)");
        break;
    }

    case MSG_KAREN_WELCOME:
        karenEmitter_ = msg.ReadString();
        AppendLog("Karen", "Welcome from '" + karenEmitter_ + "'");
        break;

    case MSG_KAREN_PLOT:
    {
        String name = msg.ReadString();
        float value = msg.ReadFloat();
        karenPlots_[name] = value;  // latest value, kept for a future UI panel
        break;
    }

    case MSG_KAREN_MESSAGE:
    {
        String text = msg.ReadString();
        AppendLog("Karen", "[" + emitterLabel + "] " + text);
        break;
    }

    default:
        // FRAME / ZONE_* are profiling detail — not surfaced. SUBSCRIBE(306) is outbound-only.
        break;
    }
}

// ============================================================================
// Remote Workboard Sync (Phase 2a)
// ============================================================================

void WorkboardManager::RegisterWorkboardRemoteEvents()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // Register all workboard events so they pass the remote event whitelist
    network->RegisterRemoteEvent(E_WB_WELCOME);
    network->RegisterRemoteEvent(E_WB_WORKBOARD_FULL);
    network->RegisterRemoteEvent(E_WB_PLAN_LIST);
    network->RegisterRemoteEvent(E_WB_PLAN_CONTENT);
    network->RegisterRemoteEvent(E_WB_MUTATION_ACK);
    network->RegisterRemoteEvent(E_WB_CLIENT_LIST);
    network->RegisterRemoteEvent(E_WB_SERVER_SHUTDOWN);  // Phase 2c
    network->RegisterRemoteEvent(E_WB_REQUEST_PLAN);
    network->RegisterRemoteEvent(E_WB_MUTATION);
    network->RegisterRemoteEvent(E_WB_SET_IDENTITY);
    network->RegisterRemoteEvent(E_WB_INSTANCE_STATUS);
}

void WorkboardManager::HandleClientConnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientConnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    AppendLog("Network", "Client connected: " + conn->ToString());
}

void WorkboardManager::HandleClientDisconnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientDisconnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    // Clean up Claudette PAKE connection if this was one
    for (auto cit = claudetteConnections_.Begin(); cit != claudetteConnections_.End(); ++cit)
    {
        if (cit->second_ == conn)
        {
            AppendLog("Network", "Claudette disconnected: " + cit->first_);
            claudetteConnections_.Erase(cit);
            break;
        }
    }

    // Collector: if this was a dialed-in Karen emitter, drop its label. It was reclassified out of
    // wbClients_ on HELLO, so it would otherwise fall through to the "unauthenticated" branch below.
    {
        auto kit = karenEmitters_.Find(conn);
        if (kit != karenEmitters_.End())
        {
            AppendLog("Karen", "Emitter '" + kit->second_ + "' disconnected from collector");
            karenEmitters_.Erase(kit);
            return;
        }
    }

    auto it = wbClients_.Find(conn);
    if (it != wbClients_.End())
    {
        AppendLog("Network", "Workboard client disconnected: " + it->second_.name_);
        wbClients_.Erase(it);
        PushClientListToAll();
    }
    else
    {
        AppendLog("Network", "Client disconnected (unauthenticated)");
    }
}

void WorkboardManager::HandleClientIdentity(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientIdentity;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    // LAN clients are trusted (no auth). WAN clients must provide --secret password.
    String addr = conn->GetAddress();
    bool isLan = addr.StartsWith("127.") || addr.StartsWith("10.") ||
                 addr.StartsWith("192.168.") || addr == "::1";
    // 172.16.0.0 – 172.31.255.255
    if (!isLan && addr.StartsWith("172."))
    {
        unsigned dot1 = addr.Find('.');
        if (dot1 != String::NPOS)
        {
            int second = atoi(addr.Substring(dot1 + 1).CString());
            if (second >= 16 && second <= 31)
                isLan = true;
        }
    }

    if (!isLan)
    {
        // WAN — require PAKE authentication (password never sent in plaintext)
        if (wbSecret_.Empty())
        {
            AppendLog("Network", "WAN client rejected — no --secret configured: " + conn->ToString());
            eventData[P_ALLOW] = false;
            return;
        }
        if (!conn->IsPakeAuthenticated())
        {
            AppendLog("Network", "WAN client rejected — PAKE auth failed from " + conn->ToString());
            eventData[P_ALLOW] = false;
            return;
        }
        AppendLog("Network", "WAN client PAKE-authenticated: " + conn->ToString());
    }

    // Accept the connection
    eventData[P_ALLOW] = true;

    // Register as authenticated workboard client
    const VariantMap& ident = conn->GetIdentity();
    auto nameIt = ident.Find(StringHash("Name"));
    auto roleIt = ident.Find(StringHash("Role"));

    WbClientInfo info;
    info.connection_ = conn;
    info.name_ = (nameIt != ident.End()) ? nameIt->second_.GetString() : String::EMPTY;
    info.role_ = (roleIt != ident.End()) ? roleIt->second_.GetString() : String::EMPTY;
    info.authenticated_ = true;
    if (info.name_.Empty())
        info.name_ = conn->ToString();
    wbClients_[conn] = info;

    AppendLog("Network", "Workboard client authenticated: " + info.name_ + " (" + info.role_ + ")");

    // Send welcome
    {
        VariantMap data;
        data["ServerName"] = String("WorkboardManager");
        data["Version"] = String("1.0");
        data["ClientCount"] = (int)wbClients_.Size();
        conn->SendRemoteEvent(E_WB_WELCOME, true, data);
    }

    // Push initial state
    PushWorkboardToClient(conn);
    PushPlanListToClient(conn);
    PushClientListToAll();
}

void WorkboardManager::HandleKeyExchangeAuth(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace KeyExchangeAuth;
    String username = eventData[P_USERNAME].GetString();

    if (!pakeSecretValid_)
    {
        eventData[P_FOUND] = false;
        AppendLog("Network", "PAKE: no secret hash available for user '" + username + "'");
        return;
    }

    eventData[P_PASSWORDHASH].SetBuffer(pakeSecretHash_, sizeof(pakeSecretHash_));
    eventData[P_FOUND] = true;
    AppendLog("Network", "PAKE: password hash provided for '" + username + "'");
}

void WorkboardManager::HandleClientAuthenticated(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientAuthenticated;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    String username = eventData[P_USERNAME].GetString();
    AppendLog("Network", "PAKE: client authenticated — user '" + username + "' from " + conn->ToString());

    // Claudette PAKE auth accepted — role mapping happens when Claudette
    // sends E_WB_SET_IDENTITY with its session ID (handled in HandleWbSetIdentity).
    if (username == "claudette")
        AppendLog("Network", "PAKE: Claudette authenticated — awaiting identity");
}

void WorkboardManager::PushWorkboardToClient(Connection* conn)
{
    String content = SerializeSectionsToMarkdown();
    if (content.Empty())
        return;

    VariantMap data;
    data["Markdown"] = content;
    conn->SendRemoteEvent(E_WB_WORKBOARD_FULL, true, data);
}

void WorkboardManager::PushPlanListToClient(Connection* conn)
{
    VariantMap data;
    data["Filenames"] = BuildPlanListString();
    conn->SendRemoteEvent(E_WB_PLAN_LIST, true, data);
}

void WorkboardManager::PushClientListToAll()
{
    VariantMap data;
    data["Clients"] = BuildClientListString();

    for (auto it = wbClients_.Begin(); it != wbClients_.End(); ++it)
        it->first_->SendRemoteEvent(E_WB_CLIENT_LIST, true, data);
}

void WorkboardManager::PushWorkboardToAllClients()
{
    if (wbClients_.Empty())
        return;

    String content = SerializeSectionsToMarkdown();

    VariantMap data;
    data["Markdown"] = content;

    for (auto it = wbClients_.Begin(); it != wbClients_.End(); ++it)
        it->first_->SendRemoteEvent(E_WB_WORKBOARD_FULL, true, data);

    AppendLog("Network", "Pushed workboard update to " + String(wbClients_.Size()) + " client(s)");
}

String WorkboardManager::BuildPlanListString()
{
    String result;
    for (unsigned i = 0; i < planFiles_.Size(); ++i)
    {
        if (i > 0)
            result += "\n";
        result += planFiles_[i];
    }
    return result;
}

String WorkboardManager::BuildClientListString()
{
    String result;
    unsigned idx = 0;
    for (auto it = wbClients_.Begin(); it != wbClients_.End(); ++it)
    {
        if (idx > 0)
            result += "\n";
        result += it->second_.name_ + ":" + it->second_.role_;
        ++idx;
    }
    return result;
}

void WorkboardManager::HandleWbRequestPlan(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace RemoteEventData;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (wbClients_.Find(conn) == wbClients_.End())
        return;  // not authenticated

    String filename = eventData["Filename"].GetString();
    if (filename.Empty())
        return;

    // Sanitize — only allow PLAN_*.md files from Claude dir
    if (!filename.StartsWith("PLAN_") || !filename.EndsWith(".md") || filename.Contains(".."))
    {
        AppendLog("Network", "Rejected plan request: " + filename);
        return;
    }

    String path = GetClaudeDir() + filename;
    File file(context_, path, FILE_READ);
    if (!file.IsOpen())
    {
        AppendLog("Network", "Plan not found: " + filename);
        return;
    }

    unsigned size = file.GetSize();
    String content;
    content.Resize(size);
    file.Read(&content[0], size);
    file.Close();

    VariantMap data;
    data["Filename"] = filename;
    data["Content"] = content;
    conn->SendRemoteEvent(E_WB_PLAN_CONTENT, true, data);

    AppendLog("Network", "Sent plan " + filename + " to " + wbClients_[conn].name_);
}

void WorkboardManager::HandleWbMutation(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace RemoteEventData;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (wbClients_.Find(conn) == wbClients_.End())
        return;  // not authenticated

    String command = eventData["Command"].GetString();
    String args = eventData["Args"].GetString();
    String clientName = wbClients_[conn].name_;

    AppendLog("Network", "Mutation from " + clientName + ": " + command + " " + args);

    // Construct the wb-* command string and run it through existing handler
    String fullCommand = "wb-" + command + " " + args;
    bool success = HandleWorkboardCommand(fullCommand);

    // Send ack
    VariantMap ack;
    ack["Success"] = success;
    ack["Reason"] = success ? String("OK") : String("Command failed: " + fullCommand);
    conn->SendRemoteEvent(E_WB_MUTATION_ACK, true, ack);

    // If mutation succeeded, push updated workboard to all clients
    if (success)
    {
        LoadWorkboard();
        PushWorkboardToAllClients();
    }
}

void WorkboardManager::HandleWbSetIdentity(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace RemoteEventData;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    String name = eventData["Name"].GetString();  // session ID for Claudettes
    String role = eventData["Role"].GetString();

    // Map Claudette connection to coder role by matching session ID
    if (!name.Empty())
    {
        for (auto& ci : coderInstances_)
        {
            if (ci.second_.sessionId == name)
            {
                // Found the coder registered from this session
                String coderRole = ci.second_.role;
                claudetteConnections_[coderRole] = conn;
                AppendLog("Network", "Claudette bound to '" + coderRole + "' via session " + name);
                URHO3D_LOGINFOF("Claudette PAKE connection mapped: %s -> %s", name.CString(), coderRole.CString());
                break;
            }
        }
    }

    auto it = wbClients_.Find(conn);
    if (it != wbClients_.End())
    {
        if (!name.Empty())
            it->second_.name_ = name;
        if (!role.Empty())
            it->second_.role_ = role;
        PushClientListToAll();
    }

    AppendLog("Network", "Client identity: " + name + " (" + role + ")");
}

void WorkboardManager::HandleWbInstanceStatus(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace RemoteEventData;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    auto it = wbClients_.Find(conn);
    if (it == wbClients_.End())
        return;

    it->second_.remoteYukiAlive_ = eventData["YukiAlive"].GetBool();
    it->second_.remoteCoderCount_ = eventData["CoderCount"].GetI32();
    it->second_.remoteCoderRoles_ = eventData["CoderRoles"].GetString();
}

void WorkboardManager::SampleSystemStats()
{
#ifdef __linux__
    // ── CPU usage from /proc/stat ──
    {
        File procStat(context_);
        if (procStat.Open("/proc/stat", FILE_READ))
        {
            String line = procStat.ReadLine().Trimmed();

            Vector<String> parts = line.Split(' ');
            // Split may produce empty strings from consecutive spaces — filter
            Vector<String> fields;
            for (unsigned i = 0; i < parts.Size(); ++i)
                if (!parts[i].Trimmed().Empty())
                    fields.Push(parts[i].Trimmed());

            if (fields.Size() >= 5 && fields[0] == "cpu")
            {
                unsigned long long user = strtoull(fields[1].CString(), nullptr, 10);
                unsigned long long nice = strtoull(fields[2].CString(), nullptr, 10);
                unsigned long long system = strtoull(fields[3].CString(), nullptr, 10);
                unsigned long long idle = strtoull(fields[4].CString(), nullptr, 10);
                unsigned long long iowait = fields.Size() > 5 ? strtoull(fields[5].CString(), nullptr, 10) : 0;
                unsigned long long idleAll = idle + iowait;  // iowait is idle time spent waiting on I/O
                unsigned long long total = user + nice + system + idleAll;
                if (fields.Size() > 6) total += strtoull(fields[6].CString(), nullptr, 10);  // irq
                if (fields.Size() > 7) total += strtoull(fields[7].CString(), nullptr, 10);  // softirq
                if (fields.Size() > 8) total += strtoull(fields[8].CString(), nullptr, 10);  // steal

                if (prevCpuTotal_ > 0)
                {
                    unsigned long long dTotal = total - prevCpuTotal_;
                    unsigned long long dIdle = idleAll - prevCpuIdle_;
                    int pct = (dTotal > 0) ? (int)(100 * (dTotal - dIdle) / dTotal) : 0;
                    if (cpuText_)
                    {
                        cpuText_->SetText("CPU: " + String(pct) + "%");
                        SetBarPercent(cpuBar_, cpuText_->GetParent(), pct);
                    }
                }
                prevCpuTotal_ = total;
                prevCpuIdle_ = idleAll;
            }
        }
    }

    // ── GPU usage ──
    {
        bool found = false;
        // Try AMD sysfs — scan card0..card7
        for (int card = 0; card < 8 && !found; ++card)
        {
            String path = "/sys/class/drm/card" + String(card) + "/device/gpu_busy_percent";
            auto* fs = GetSubsystem<FileSystem>();
            if (!fs->FileExists(path))
                continue;
            File gpuFile(context_);
            if (gpuFile.Open(path, FILE_READ))
            {
                String val = gpuFile.ReadLine().Trimmed();
                if (!val.Empty())
                {
                    if (gpuText_)
                    {
                        gpuText_->SetText("GPU: " + val + "%");
                        SetBarPercent(gpuBar_, gpuText_->GetParent(), atoi(val.CString()));
                    }
                    found = true;
                }
            }
        }
        // Try NVIDIA via nvidia-smi — redirect to temp file, read with Urho File
        if (!found)
        {
            auto* fs = GetSubsystem<FileSystem>();
            if (fs)
            {
                String tmpPath = fs->GetTemporaryDir() + "urho_gpu_query.tmp";
                int ret = fs->SystemCommand("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits > " + tmpPath + " 2>/dev/null");
                if (ret == 0)
                {
                    File tmpFile(context_);
                    if (tmpFile.Open(tmpPath, FILE_READ))
                    {
                        String val = tmpFile.ReadLine().Trimmed();
                        if (!val.Empty() && val != "N/A")
                        {
                            if (gpuText_)
                            {
                                gpuText_->SetText("GPU: " + val + "%");
                                SetBarPercent(gpuBar_, gpuText_->GetParent(), atoi(val.CString()));
                            }
                            found = true;
                        }
                    }
                }
                fs->Delete(tmpPath);
            }
        }
        // Try Intel sysfs
        if (!found)
        {
            File intelGpu(context_);
            if (intelGpu.Open("/sys/class/drm/card0/gt/gt0/rps_act_freq_mhz", FILE_READ))
            {
                String val = intelGpu.ReadLine().Trimmed();
                if (!val.Empty())
                {
                    if (gpuText_)
                        gpuText_->SetText("GPU: " + val + " MHz");
                    found = true;
                }
            }
        }
        if (!found && gpuText_)
            gpuText_->SetText("GPU: N/A");
    }

    // ── RAM from /proc/meminfo ──
    {
        File memFile(context_);
        if (memFile.Open("/proc/meminfo", FILE_READ))
        {
            unsigned long long memTotal = 0, memAvail = 0, swapTotal = 0, swapFree = 0;
            while (!memFile.IsEof())
            {
                String line = memFile.ReadLine();
                if (line.StartsWith("MemTotal:"))
                    memTotal = strtoull(line.CString() + 9, nullptr, 10);
                else if (line.StartsWith("MemAvailable:"))
                    memAvail = strtoull(line.CString() + 13, nullptr, 10);
                else if (line.StartsWith("SwapTotal:"))
                    swapTotal = strtoull(line.CString() + 10, nullptr, 10);
                else if (line.StartsWith("SwapFree:"))
                    swapFree = strtoull(line.CString() + 9, nullptr, 10);
            }

            if (memTotal > 0)
            {
                unsigned long long memUsed = memTotal - memAvail;
                int ramPct = (int)(100 * memUsed / memTotal);
                if (ramText_)
                {
                    ramText_->SetText("RAM: " + String(memUsed / 1024) + "/" + String(memTotal / 1024) + "MB (" + String(ramPct) + "%)");
                    SetBarPercent(ramBar_, ramText_->GetParent(), ramPct);
                }
            }
            if (swapTotal > 0)
            {
                unsigned long long swapUsed = swapTotal - swapFree;
                int swapPct = (int)(100 * swapUsed / swapTotal);
                if (swapText_)
                {
                    swapText_->SetText("Swap: " + String(swapUsed / 1024) + "/" + String(swapTotal / 1024) + "MB (" + String(swapPct) + "%)");
                    SetBarPercent(swapBar_, swapText_->GetParent(), swapPct);
                }
            }
            else if (swapText_)
                swapText_->SetText("Swap: none");
        }
    }

    // ── Yuki CPU ──
    {
        yukiLLM_.SampleCpuUsage();
        float yukiPct = yukiLLM_.GetCpuUsage();
        if (yukiCpuText_)
        {
            if (yukiLLM_.IsModelLoaded() || yukiLLM_.IsModelLoading())
                yukiCpuText_->SetText("Yuki: " + String((int)yukiPct) + "%");
            else
                yukiCpuText_->SetText("Yuki: off");
        }
    }

    // ── Disk usage via statvfs ──
    {
        struct statvfs stat;
        if (statvfs(projectRoot_.CString(), &stat) == 0)
        {
            unsigned long long totalGB = (stat.f_blocks * stat.f_frsize) / (1024ULL * 1024 * 1024);
            unsigned long long freeGB = (stat.f_bavail * stat.f_frsize) / (1024ULL * 1024 * 1024);
            unsigned long long usedGB = totalGB - freeGB;
            int diskPct = totalGB > 0 ? (int)(100 * usedGB / totalGB) : 0;
            if (diskText_)
            {
                diskText_->SetText("Disk: " + String((unsigned)usedGB) + "/" + String((unsigned)totalGB) + "GB (" + String(diskPct) + "%)");
                SetBarPercent(diskBar_, diskText_->GetParent(), diskPct);
            }
        }
    }

    // ── Disk I/O rates from /proc/diskstats ──
    {
        File diskStats(context_);
        if (diskStats.Open("/proc/diskstats", FILE_READ))
        {
            unsigned long long totalReadSectors = 0, totalWriteSectors = 0;
            while (!diskStats.IsEof())
            {
                String line = diskStats.ReadLine().Trimmed();
                if (line.Empty())
                    continue;
                Vector<String> parts = line.Split(' ');
                Vector<String> fields;
                for (unsigned i = 0; i < parts.Size(); ++i)
                    if (!parts[i].Trimmed().Empty())
                        fields.Push(parts[i].Trimmed());
                // diskstats: major minor name reads_completed _ sectors_read _ writes_completed _ sectors_written ...
                // We want whole-disk devices (sda, nvme0n1, vda) not partitions
                if (fields.Size() < 10)
                    continue;
                const String& devName = fields[2];
                // Skip partitions (sda1, nvme0n1p1) and loop/ram devices
                if (devName.StartsWith("loop") || devName.StartsWith("ram"))
                    continue;
                bool isPartition = false;
                for (unsigned c = 0; c < devName.Length(); ++c)
                {
                    if (IsDigit(devName[c]) && c + 1 < devName.Length() && devName[c + 1] == 'p')
                    { isPartition = true; break; }
                }
                // sda1, sdb2 etc — digit at end after letters
                if (!isPartition && devName.Length() > 3)
                {
                    char last = devName[devName.Length() - 1];
                    char prev = devName[devName.Length() - 2];
                    if (IsDigit(last) && IsAlpha(prev))
                        isPartition = true;
                }
                if (isPartition)
                    continue;

                totalReadSectors += strtoull(fields[5].CString(), nullptr, 10);
                totalWriteSectors += strtoull(fields[9].CString(), nullptr, 10);
            }

            if (prevDiskReadSectors_ > 0)
            {
                // Sectors are 512 bytes. Divide by REFRESH_INTERVAL to get per-second rate.
                unsigned long long dRead = totalReadSectors - prevDiskReadSectors_;
                unsigned long long dWrite = totalWriteSectors - prevDiskWriteSectors_;
                // Convert sectors to KB/s (512 bytes per sector, divide by interval)
                unsigned long long readKB = (unsigned long long)(dRead / 2 / REFRESH_INTERVAL);
                unsigned long long writeKB = (unsigned long long)(dWrite / 2 / REFRESH_INTERVAL);

                if (diskReadText_)
                {
                    if (readKB > 1024)
                        diskReadText_->SetText("R: " + String((unsigned)(readKB / 1024)) + " MB/s");
                    else
                        diskReadText_->SetText("R: " + String((unsigned)readKB) + " KB/s");
                }
                if (diskWriteText_)
                {
                    if (writeKB > 1024)
                        diskWriteText_->SetText("W: " + String((unsigned)(writeKB / 1024)) + " MB/s");
                    else
                        diskWriteText_->SetText("W: " + String((unsigned)writeKB) + " KB/s");
                }
            }
            prevDiskReadSectors_ = totalReadSectors;
            prevDiskWriteSectors_ = totalWriteSectors;
        }
    }
#else
    if (cpuText_)
        cpuText_->SetText("CPU: N/A");
    if (gpuText_)
        gpuText_->SetText("GPU: N/A");
    if (ramText_)
        ramText_->SetText("RAM: N/A");
    if (swapText_)
        swapText_->SetText("Swap: N/A");
    if (diskText_)
        diskText_->SetText("Disk: N/A");
#endif
}
