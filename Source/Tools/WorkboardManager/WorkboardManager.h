// WorkboardManager — GUI dashboard for workboard viewing, plan browsing,
// and bidirectional IPC with Claude Code instances via TTY injection.

#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#define WORKBOARD_MANAGER_VERSION "0.3.11"

#include "WorkboardBase.h"
#include "WorkboardDB.h"
#include "WorkboardLLM.h"
#include "YukiMemoryDB.h"

#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/DropDownList.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/MultiLineEdit.h>
#include <Urho3D/UI/ScrollBar.h>

using namespace Urho3D;

class WorkboardManager : public WorkboardBase
{
    URHO3D_OBJECT(WorkboardManager, WorkboardBase);

public:
    explicit WorkboardManager(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    // ── UI creation ──
    void CreateUI();
    void CreateInstanceStatusBar(UIElement* parent, float minX, float minY, float maxX, float maxY);
    void CreateComposer(UIElement* parent, float minX, float minY, float maxX, float maxY);
    void CreateMessageLog(UIElement* parent, float minX, float minY, float maxX, float maxY);
    void CreateYukiChatPanel(UIElement* parent, float minX, float minY, float maxX, float maxY);
    void AppendYukiChat(const String& sender, const String& message);

    // ── Workboard ──
    void LoadWorkboard();
    String SerializeSectionsToMarkdown();

    // ── Workboard mutations (Manager is single authority) ──
    bool HandleWorkboardCommand(const String& message);
    WorkboardSection* FindSection(const String& keyword);
    void AddReadyRow(const Vector<String>& fields);
    void AddInProgressRow(const Vector<String>& fields);
    void AddDoneRow(const Vector<String>& fields);
    void MoveToDone(const String& taskName);
    void RemoveRow(const String& matchText);
    void AddSharedFile(const Vector<String>& fields);
    void UpdateReview(const String& taskName, const String& newReview);
    bool AssignTask(const String& taskName, const String& coderRole);
    void WriteWorkboard();
    void EmitTableRows(String& output, const WorkboardSection* sec);

    // ── Plans ──
    void ScanPlanFiles();
    void LoadPlanContent(const String& filename);
    void OnPlanSelected(const String& filename) override;

    // ── IPC ──
    void CreateIPCPaths();
    void SendMessage(const String& target, const String& message);
    bool SendToSocket(const String& role, const String& message, const String& excludeRole = String::EMPTY);
    bool IsInstanceAlive(const String& role);
    String ReadBuildStatus();  // Read active build status from /tmp/urho_claude/build_active.json
    void RefreshInstanceStatus();

    // ── Relay socket (message broker) ──
    void StartRelaySocket();
    void StopRelaySocket();
    void PollRelaySocket();
#ifndef _WIN32
    int relayListenFd_{-1};
#else
    HANDLE relayPipeHandle_{INVALID_HANDLE_VALUE};
    OVERLAPPED relayOverlapped_{};
    bool relayConnectPending_{false};
    String PipeName(const String& role) const;  ///< Convert role to \\.\pipe\urho_claude_{role}
#endif

    // ── Embedded Yuki (LLM inference) ──
    WorkboardLLM yukiLLM_;
    YukiMemoryDB yukiMemoryDB_;
    void PollYukiInference();
    String FindYukiModel();  ///< Locate best available GGUF model

    // ── Beacon ──
    void UpdateBeacon();
    void CleanupLegacyPIDFiles();
    float GetLastActivity(const String& role);

    void HandleSendCoder(StringHash eventType, VariantMap& eventData);
    void HandleSendUnassigned(StringHash eventType, VariantMap& eventData);

    void HandleSendBroadcast(StringHash eventType, VariantMap& eventData);
    void HandleClearFileLocks(StringHash eventType, VariantMap& eventData);
    void HandleSpawnCoder(StringHash eventType, VariantMap& eventData);
    void HandleLaunchYuki(StringHash eventType, VariantMap& eventData);
    void HandleToggleScreenshots(StringHash eventType, VariantMap& eventData);
    void HandleToggleYuki(StringHash eventType, VariantMap& eventData);

    // ── Workboard task lookup ──
    String GetCurrentTask(const String& owner);

    // ── Multi-Coder discovery ──
    Vector<String> DiscoverCoderRoles();
    Vector<String> DiscoverUnassignedRoles();
    String GetSelectedCoderRole();

    // ── Download ──
    void HandleDownload(StringHash eventType, VariantMap& eventData);
    void CheckDownloadProgress();

    // ── Message log ──
    void AppendLog(const String& source, const String& message);
    Color LogColorForSource(const String& source);

    // ── Tools popup (download) ──
    void CreateToolsPopup();
    void HandleToolsToggle(StringHash eventType, VariantMap& eventData);

    // ── Settings popup (theme) ──
    void CreateSettingsPopup();
    void HandleSettingsToggle(StringHash eventType, VariantMap& eventData);
    void HandleResetWorld(StringHash eventType, VariantMap& eventData);
    void HandleFontSelected(StringHash eventType, VariantMap& eventData);
    void HandleFontSizeChanged(StringHash eventType, VariantMap& eventData);
    void ApplyFont(const String& fontName, int fontSize);
    void LoadThemePrefs();
    void SaveThemePrefs();
    void RebuildAllUI();

    // ── Remote workboard sync (Phase 2a) ──
    void RegisterWorkboardRemoteEvents();
    void PushWorkboardToClient(Connection* conn);
    void PushPlanListToClient(Connection* conn);
    void PushClientListToAll();
    void PushWorkboardToAllClients();
    String BuildPlanListString();
    String BuildClientListString();

    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleClientIdentity(StringHash eventType, VariantMap& eventData);
    void HandleKeyExchangeAuth(StringHash eventType, VariantMap& eventData);
    void HandleClientAuthenticated(StringHash eventType, VariantMap& eventData);
    void HandleWbRequestPlan(StringHash eventType, VariantMap& eventData);
    void HandleWbMutation(StringHash eventType, VariantMap& eventData);
    void HandleWbSetIdentity(StringHash eventType, VariantMap& eventData);
    void HandleWbInstanceStatus(StringHash eventType, VariantMap& eventData);

    // ── Events ──
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);
    void HandleScreenMode(StringHash eventType, VariantMap& eventData);

    // ── Font / Theme ──
    String currentFontName_{"Anonymous Pro"};
    int currentFontSize_{11};
    Vector<String> availableFonts_;
    DropDownList* fontSelector_{};
    DropDownList* fontSizeSelector_{};

    // ── Relay socket polling ──
    float relayPollAccumulator_{};
    static constexpr float RELAY_POLL_INTERVAL = 0.1f;  // 100ms — responsive but not every frame

    // ── Workboard state (Manager-specific) ──
    float refreshAccumulator_{};
    static constexpr float REFRESH_INTERVAL = 2.5f;
    unsigned lastWriteMtime_{0};

    // ── Yuki training lump collection ──
    float trainingCheckAccumulator_{0.0f};
    static constexpr float TRAINING_CHECK_INTERVAL = 120.0f;
    static constexpr unsigned TRAINING_LUMP_THRESHOLD = 50;
    void CheckTrainingLump();

    // ── Yuki memory WAL upkeep ──
    // Manager is the long-lived holder of yuki_memory.db, so it owns WAL upkeep.
    // A periodic PASSIVE checkpoint flushes committed frames into the main .db so
    // durability never depends on the -wal surviving a copy/restart; the -wal is
    // truncated (zeroed) at shutdown. See YukiMemoryDB::Checkpoint.
    float walCheckpointAccumulator_{0.0f};
    static constexpr float WAL_CHECKPOINT_INTERVAL = 60.0f;

    // ── Karen telemetry listener (phase 1: receive only) ──
    // Manager acts as a Karen *viewer*: discovers KarenTelemetry emitters on the
    // LAN via the Network client peer (independent of the workboard server),
    // connects, and surfaces their plots/messages. Speakers get grafted into apps
    // (Yuki etc.) later. See StartKarenListener.
    void StartKarenListener();
    void HandleKarenHostDiscovered(StringHash eventType, VariantMap& eventData);
    void HandleKarenConnectionStatus(StringHash eventType, VariantMap& eventData);
    void HandleKarenMessage(StringHash eventType, VariantMap& eventData);
    float karenDiscoverAccumulator_{0.0f};
    static constexpr float KAREN_DISCOVER_INTERVAL = 5.0f;
    bool karenConnected_{false};
    String karenEmitter_;
    HashMap<String, float> karenPlots_;   // latest value per plot name
    // Periodic one-line summary of the latest plot values to the log, so the
    // telemetry is actually visible (not just "Attached"). Throttled to avoid spam.
    float karenPlotLogAccumulator_{0.0f};
    static constexpr float KAREN_PLOT_LOG_INTERVAL = 5.0f;

    // ── Karen collector (inversion / phase 2): emitters DIAL IN to our existing 31337 server ──
    // Additive to the phase-1 discover-and-dial listener above (nothing torn out). An injected
    // Karen client Connect()s to Manager and sends MSG_KAREN_HELLO(307) with its app name; we tag
    // the source connection here so telemetry is labelled per-emitter and the connection isn't
    // mistaken for a workboard coder (the LAN-open handshake auto-registers every client during
    // E_CLIENTIDENTITY; HELLO arrives AFTER, so HandleKarenMessage reclassifies it out of
    // wbClients_). Keyed by Connection* (same as wbClients_); cleaned on disconnect.
    // NOTE: the collector currently RIDES the 31337 workboard server (Karen msg IDs 300-307 are
    // disjoint from the WB named remote events). KAREN_PORT (25771) stays the canonical/aspirational
    // constant for a future dedicated collector port (engine multi-socket bind) — not this pass.
    HashMap<Connection*, String> karenEmitters_;   // dialed-in emitter conn → app name

    // ── SQL backing (Phase 4) ──
    WorkboardDB workboardDB_;

    // ── Instance status UI ──
    DropDownList* coderStatusDropdown_{};
    Vector<String> knownCoderRoles_;
    Text* yukiStatusText_{};
    Text* buildStatusText_{};  // Active build indicator
    DropDownList* localsDropdown_{};
    DropDownList* remotesDropdown_{};
    DropDownList* unassignedStatusDropdown_{};
    Vector<String> knownUnassignedRoles_;

    // ── Composer UI ──
    LineEdit* messageInput_{};
    DropDownList* coderDropdown_{};
    Button* sendCoderBtn_{};
    Button* sendUnassignedBtn_{};
    Button* sendYukiBtn_{};
    Button* sendBroadcastBtn_{};
    Button* clearFileLocksBtn_{};
    Button* spawnCoderBtn_{};
    Button* launchYukiBtn_{};
    Button* screenshotToggleBtn_{};
    bool screenshotsBlocked_{false};

    // ── Collapsible status bar sections ──
    UIElement* statsContainer_{};
    UIElement* controlsContainer_{};
    Window* statusBar_{};
    bool statsCollapsed_{false};
    bool controlsCollapsed_{false};
    void HandleToggleStats(StringHash eventType, VariantMap& eventData);
    void HandleToggleControls(StringHash eventType, VariantMap& eventData);
    void RelayoutPanels();

    // ── Tools popup UI ──
    Window* toolsPopup_{};
    Button* toolsBtn_{};

    // ── Settings popup UI ──
    Window* settingsPopup_{};
    Button* settingsBtn_{};

    // ── Reset World UI (inside settings popup) ──
    Button* resetWorldBtn_{};
    Text* resetWorldStatus_{};

    // ── Download UI (inside settings popup) ──
    LineEdit* downloadUrlInput_{};
    Button* downloadBtn_{};
    Text* downloadStatusText_{};
    bool downloadInProgress_{false};
    String downloadOutputPath_;
    unsigned curlRequestId_{0};
    float downloadCheckTimer_{0.0f};

    // ── Message log UI ──
    Window* logPanel_{};
    ListView* logListView_{};
    Text* logTitleText_{};
    bool logCollapsed_{false};
    static const unsigned MAX_LOG_LINES = 500;

    void HandleLogTitleClick(StringHash eventType, VariantMap& eventData);
    void HandleLogCopy(StringHash eventType, VariantMap& eventData);

    // ── Yuki chat UI ──
    Window* yukiChatPanel_{};
    ListView* yukiChatLog_{};
    Button* yukiToggleBtn_{};
    Text* yukiToggleBtnText_{};

    // ── IPC state ──
    String ipcDir_;
    String ttySockDir_;

    // ── Instance registry (Manager owns role assignment) ──
    struct CoderInstance {
        String role;            // assigned role: coder, coder2, coder3, ...
        String sessionId;       // stable TTY identifier (_dev_pts_0, etc.)
        int pid{0};             // Claude process PID
        unsigned long long startTime{0}; // /proc/<pid>/starttime (clock ticks) — pins pid against reuse
        int relayFd{-1};        // persistent relay connection fd (kept alive for push delivery)
        unsigned registeredAt{0}; // epoch seconds when first registered (succession by seniority)
        unsigned unhealthySince{0}; // epoch seconds of first failed liveness check (0 = healthy); grace before prune
        int contextPct{0};      // context window usage percentage (0-100)
        int contextTokens{0};   // current input tokens used
        int contextMax{0};      // context window capacity
    };
    HashMap<String, CoderInstance> coderInstances_; // keyed by sessionId

    // Departed-elder memory: lets a returning elder reclaim "coder" at its
    // original seniority within a succession window, instead of being treated
    // as a brand-new junior. Cleared once the window lapses or it returns.
    struct VacantElder {
        String sessionId;       // the elder's stable session identity
        int pid{0};             // its Claudette PID (for lineage/identity match)
        unsigned long long startTime{0}; // /proc/<pid>/starttime — reject reclaim if pid was reused
        unsigned registeredAt{0}; // original seniority, preserved across the gap
        unsigned vacatedAt{0};  // epoch seconds the elder slot fell vacant
    };
    VacantElder vacantElder_;   // sessionId empty == no vacancy pending

    HashMap<String, Connection*> claudetteConnections_;  // role → PAKE-authenticated Claudette connection
    String AssignCoderRole(const String& sessionId, int pid);
    void EnforceRoleInvariants();  // guarantee single elder + no duplicate roles
    // Peer-credential hardening for the relay socket (Unix-only no-op elsewhere).
    bool IsTrustedPeerUid(int uid);
    bool VerifyPeerLineage(int peerPid, int claimedPid);
    /// Read /proc/<pid>/starttime (field 22 of /proc/<pid>/stat, clock ticks since
    /// boot). Returns 0 if unreadable (process gone). Pins a pid against reuse:
    /// a reclaimed/reconnected pid whose starttime changed is a different process.
    unsigned long long ReadProcStartTime(int pid);

    // ── Succession tuning (seconds) ──
    static const unsigned PRUNE_GRACE_SECS = 15;       // consecutive unhealthy time before pruning
    static const unsigned ELDER_RECLAIM_SECS = 120;    // window for a departed elder to reclaim "coder"

    // ── Build queue ──
    struct BuildQueueEntry {
        String target;
        String requester;
    };
    void EnqueueBuild(const String& target, const String& requester);
    void ProcessBuildQueue();
    Vector<BuildQueueEntry> buildQueue_;

    // ── Broadcast-with-reply collection ──
    struct BroadcastCollect {
        String tag;
        String requester;
        float timeout;
        Vector<String> expectedFrom;
        Vector<String> replies;
    };
    Vector<BroadcastCollect> pendingCollects_;
    void ProcessPendingCollects(float timeStep);
    pid_t activeBuildPid_{0};
    String activeBuildTarget_;
    String activeBuildRequester_;

    // ── System monitor ──
    Text* cpuText_{};
    Text* gpuText_{};
    Text* ramText_{};
    Text* swapText_{};
    Text* diskText_{};
    Text* diskReadText_{};
    Text* diskWriteText_{};
    Text* yukiCpuText_{};
    // Progress bar fills behind stat text
    BorderImage* cpuBar_{};
    BorderImage* gpuBar_{};
    BorderImage* ramBar_{};
    BorderImage* swapBar_{};
    BorderImage* diskBar_{};
    /// Create a stat cell: container with a progress bar behind a text label
    UIElement* CreateStatCell(UIElement* parent, const String& name, Text*& textOut, BorderImage*& barOut,
                              const Color& barColor, int minW, int fixedH);
    /// Set a progress bar fill width to match a percentage (0–100)
    void SetBarPercent(BorderImage* bar, UIElement* cell, int pct);
    void SampleSystemStats();
#ifdef __linux__
    unsigned long long prevCpuTotal_{0};
    unsigned long long prevCpuIdle_{0};
    unsigned long long prevDiskReadSectors_{0};
    unsigned long long prevDiskWriteSectors_{0};
#endif

    // ── Singleton lock (held for process lifetime) ──
#ifndef _WIN32
    int singletonLockFd_{-1};
#else
    HANDLE singletonMutex_{nullptr};
#endif

    // ── Beacon liveness ──
    HashMap<String, float> coderActivityTimers_;
    float lastUnassignedActivity_{999.0f};
    static constexpr float LIVENESS_TIMEOUT = 300.0f;
    static constexpr unsigned short BEACON_PORT = 31337;

    // ── Auto-spawn (doorkeeper) ──
    float autoSpawnCooldown_{60.0f};  // Grace period on startup — let existing instances re-register
    static constexpr float AUTO_SPAWN_COOLDOWN = 30.0f;
    unsigned maxLocalCoders_{4};
    bool spawnPending_{};              // true between spawn command and __HELLO__ registration
    float spawnPendingTimer_{};        // timeout to clear stale pending flag

    // ── Coder cap UI ──
    Button* coderCapMinusBtn_{};
    Text* coderCapText_{};
    Button* coderCapPlusBtn_{};
    void HandleCoderCapMinus(StringHash eventType, VariantMap& eventData);
    void HandleCoderCapPlus(StringHash eventType, VariantMap& eventData);
    void UpdateCoderCapText();

    // ── Remote workboard sync (Phase 2a) ──
    HashMap<Connection*, WbClientInfo> wbClients_;
    String wbSecret_;
    unsigned char pakeSecretHash_[32]{};
    bool pakeSecretValid_{false};
    unsigned lastPlanListHash_{0};

};

