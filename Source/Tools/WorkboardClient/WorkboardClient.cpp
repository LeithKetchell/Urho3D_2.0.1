// WorkboardClient — thin LAN client for remote workboard viewing
// Connects to WorkboardManager via SLikeNet on port 31337.

#include "WorkboardClient.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/UI/ScrollBar.h>

#include "../WorkboardManager/PlatformUtils.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/File.h>

URHO3D_DEFINE_APPLICATION_MAIN(WorkboardClient);

// ============================================================================
// Application lifecycle
// ============================================================================

WorkboardClient::WorkboardClient(Context* context) : WorkboardBase(context) {}

void WorkboardClient::Setup()
{
    // Parse command-line args
    const Vector<String>& args = GetArguments();
    for (unsigned i = 0; i < args.Size(); ++i)
    {
        if (args[i] == "--server" && i + 1 < args.Size())
        {
            serverAddress_ = args[++i];
            serverExplicit_ = true;
        }
        else if (args[i] == "--port" && i + 1 < args.Size())
            serverPort_ = static_cast<unsigned short>(Urho3D::ToI32(args[++i]));
        else if (args[i] == "--secret" && i + 1 < args.Size())
            wbSecret_ = args[++i];
        else if (args[i] == "--name" && i + 1 < args.Size())
            clientName_ = args[++i];
    }

    // Default client name to hostname
    if (clientName_.Empty())
    {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0)
            clientName_ = hostname;
        else
            clientName_ = "client";
    }

    engineParameters_[EP_WINDOW_TITLE] = "Workboard Client";
    engineParameters_[EP_WINDOW_WIDTH] = 1200;
    engineParameters_[EP_WINDOW_HEIGHT] = 700;
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
    engineParameters_[EP_SOUND] = false;
    engineParameters_[EP_LOG_NAME] = "WorkboardClient.log";
}

void WorkboardClient::Start()
{
    engine_->SetMaxFps(30);
    engine_->SetMaxInactiveFps(10);

    // Initialize IPC paths from platform temp directory
    ipcDir_ = GetSubsystem<FileSystem>()->GetTemporaryDir() + "urho_claude/";

    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(style);

    font_ = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    CreateUI();
    RegisterRemoteEvents();

    // Subscribe to connection lifecycle events
    SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(WorkboardClient, HandleServerConnected));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(WorkboardClient, HandleServerDisconnected));
    SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(WorkboardClient, HandleConnectFailed));

    // LAN discovery
    SubscribeToEvent(E_NETWORKHOSTDISCOVERED, URHO3D_HANDLER(WorkboardClient, HandleHostDiscovered));

    // Subscribe to server → client remote events
    SubscribeToEvent(E_WB_WELCOME, URHO3D_HANDLER(WorkboardClient, HandleWbWelcome));
    SubscribeToEvent(E_WB_WORKBOARD_FULL, URHO3D_HANDLER(WorkboardClient, HandleWbWorkboardFull));
    SubscribeToEvent(E_WB_PLAN_LIST, URHO3D_HANDLER(WorkboardClient, HandleWbPlanList));
    SubscribeToEvent(E_WB_PLAN_CONTENT, URHO3D_HANDLER(WorkboardClient, HandleWbPlanContent));
    SubscribeToEvent(E_WB_CLIENT_LIST, URHO3D_HANDLER(WorkboardClient, HandleWbClientList));
    SubscribeToEvent(E_WB_MUTATION_ACK, URHO3D_HANDLER(WorkboardClient, HandleWbMutationAck));

    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(WorkboardClient, HandleUpdate));
    SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(WorkboardClient, HandleKeyDown));

    GetSubsystem<Input>()->SetMouseVisible(true);

    RefreshInstanceStatus();

    if (serverExplicit_)
    {
        // Direct connect when --server was provided
        ConnectToServer();
    }
    else
    {
        // Try localhost first (same-machine case), fall back to LAN discovery on failure
        URHO3D_LOGINFO("No --server specified, trying localhost first...");
        ConnectToServer();  // serverAddress_ defaults to "localhost"
    }
}

void WorkboardClient::Stop()
{
    auto* network = GetSubsystem<Network>();
    if (network)
        network->Disconnect(100);
}

// ============================================================================
// UI Creation
// ============================================================================

void WorkboardClient::CreateUI()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    auto* graphics = GetSubsystem<Graphics>();
    int w = graphics->GetWidth();
    int h = graphics->GetHeight();

    int statusH = 28;
    int instanceH = 28;
    int mutationH = 36;
    int gap = 4;
    int topH = statusH + instanceH + gap;
    int contentH = h - topH - mutationH - gap * 3;
    int wbW = (w * 60) / 100;  // 60% for workboard
    int planW = w - wbW - gap * 3;

    CreateStatusBar(uiRoot, w, statusH);
    CreateInstanceStatusBar(uiRoot, 0, statusH + gap, w, instanceH);
    CreateWorkboardPanel(uiRoot, gap, topH + gap, wbW, contentH);
    CreatePlanPanel(uiRoot, wbW + gap * 2, topH + gap, planW, contentH);
    CreateMutationBar(uiRoot, gap, topH + contentH + gap * 2, w - gap * 2, mutationH);
}

void WorkboardClient::CreateStatusBar(UIElement* parent, int w, int h)
{
    auto* bar = parent->CreateChild<BorderImage>("StatusBar");
    bar->SetStyle("Window");
    bar->SetPosition(0, 0);
    bar->SetFixedSize(w, h);
    bar->SetLayout(LM_HORIZONTAL, 8, IntRect(8, 2, 8, 2));

    connectionDot_ = bar->CreateChild<Text>("ConnDot");
    connectionDot_->SetFont(font_, fontSize_ + 2);
    connectionDot_->SetText("*");  // bullet
    connectionDot_->SetColor(Color(1.0f, 0.3f, 0.3f));  // red = disconnected
    connectionDot_->SetAlignment(HA_LEFT, VA_CENTER);

    statusText_ = bar->CreateChild<Text>("StatusText");
    statusText_->SetFont(font_, fontSize_);
    statusText_->SetText("Connecting to " + serverAddress_ + ":" + String(serverPort_) + "...");
    statusText_->SetColor(Color(0.8f, 0.8f, 0.8f));
    statusText_->SetAlignment(HA_LEFT, VA_CENTER);

    clientCountText_ = bar->CreateChild<Text>("ClientCount");
    clientCountText_->SetFont(font_, fontSize_);
    clientCountText_->SetText("");
    clientCountText_->SetColor(Color(0.6f, 0.6f, 0.6f));
    clientCountText_->SetAlignment(HA_LEFT, VA_CENTER);
}

void WorkboardClient::CreateInstanceStatusBar(UIElement* parent, int x, int y, int w, int h)
{
    auto* bar = parent->CreateChild<BorderImage>("InstanceBar");
    bar->SetStyle("Window");
    bar->SetPosition(x, y);
    bar->SetFixedSize(w, h);
    bar->SetLayoutMode(LM_FREE);

    auto* label = bar->CreateChild<Text>();
    label->SetFont(font_, fontSize_);
    label->SetText("Local Coders:");
    label->SetColor(Color(0.6f, 0.6f, 0.6f));
    label->SetPosition(8, 4);

    coderStatusDropdown_ = bar->CreateChild<DropDownList>("CoderStatusDropdown");
    coderStatusDropdown_->SetStyleAuto();
    coderStatusDropdown_->SetFixedSize(w / 2 - 20, 22);
    coderStatusDropdown_->SetResizePopup(true);
    coderStatusDropdown_->SetPosition(120, 3);

    // Initial state
    auto* item = new Text(context_);
    item->SetFont(font_, fontSize_);
    item->SetText("Coders: none");
    item->SetColor(Color(0.5f, 0.5f, 0.5f));
    item->SetMinSize(170, 20);
    coderStatusDropdown_->AddItem(item);
}

// CreateWorkboardPanel() and CreatePlanPanel() are in WorkboardBase

void WorkboardClient::CreateMutationBar(UIElement* parent, int x, int y, int w, int h)
{
    auto* bar = parent->CreateChild<BorderImage>("MutationBar");
    bar->SetStyle("Window");
    bar->SetPosition(x, y);
    bar->SetFixedSize(w, h);
    bar->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));

    auto* label = bar->CreateChild<Text>("MutLabel");
    label->SetFont(font_, fontSize_);
    label->SetText("Task:");
    label->SetColor(Color(0.7f, 0.7f, 0.7f));
    label->SetAlignment(HA_LEFT, VA_CENTER);

    taskNameInput_ = bar->CreateChild<LineEdit>("TaskInput");
    taskNameInput_->SetStyleAuto();
    taskNameInput_->SetMinWidth(300);

    claimBtn_ = bar->CreateChild<Button>("ClaimBtn");
    claimBtn_->SetStyleAuto();
    claimBtn_->SetFixedSize(90, 28);
    auto* ct = claimBtn_->CreateChild<Text>();
    ct->SetFont(font_, fontSize_);
    ct->SetText("Claim Task");
    ct->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(claimBtn_, "Released", URHO3D_HANDLER(WorkboardClient, HandleClaimTask));

    doneBtn_ = bar->CreateChild<Button>("DoneBtn");
    doneBtn_->SetStyleAuto();
    doneBtn_->SetFixedSize(90, 28);
    auto* dt = doneBtn_->CreateChild<Text>();
    dt->SetFont(font_, fontSize_);
    dt->SetText("Mark Done");
    dt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(doneBtn_, "Released", URHO3D_HANDLER(WorkboardClient, HandleMarkDone));

    addReadyBtn_ = bar->CreateChild<Button>("AddReadyBtn");
    addReadyBtn_->SetStyleAuto();
    addReadyBtn_->SetFixedSize(90, 28);
    auto* rt = addReadyBtn_->CreateChild<Text>();
    rt->SetFont(font_, fontSize_);
    rt->SetText("Add Ready");
    rt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(addReadyBtn_, "Released", URHO3D_HANDLER(WorkboardClient, HandleAddReady));

    spawnCoderBtn_ = bar->CreateChild<Button>("SpawnCoder");
    spawnCoderBtn_->SetStyleAuto();
    spawnCoderBtn_->SetFixedSize(100, 28);
    auto* sct = spawnCoderBtn_->CreateChild<Text>();
    sct->SetFont(font_, fontSize_);
    sct->SetText("Spawn Coder");
    sct->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(spawnCoderBtn_, "Released", URHO3D_HANDLER(WorkboardClient, HandleSpawnCoder));
}

// ============================================================================
// Networking
// ============================================================================

void WorkboardClient::RegisterRemoteEvents()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // All 10 remote events from WorkboardProtocol.h
    network->RegisterRemoteEvent(E_WB_WELCOME);
    network->RegisterRemoteEvent(E_WB_WORKBOARD_FULL);
    network->RegisterRemoteEvent(E_WB_PLAN_LIST);
    network->RegisterRemoteEvent(E_WB_PLAN_CONTENT);
    network->RegisterRemoteEvent(E_WB_MUTATION_ACK);
    network->RegisterRemoteEvent(E_WB_CLIENT_LIST);
    network->RegisterRemoteEvent(E_WB_REQUEST_PLAN);
    network->RegisterRemoteEvent(E_WB_MUTATION);
    network->RegisterRemoteEvent(E_WB_SET_IDENTITY);
    network->RegisterRemoteEvent(E_WB_INSTANCE_STATUS);
}

void WorkboardClient::StartDiscovery()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    discovering_ = true;
    discoveryTimer_ = 0.0f;
    URHO3D_LOGINFO("Starting LAN discovery for WorkboardManager on port " + String(serverPort_) + "...");

    if (statusText_)
        statusText_->SetText("Discovering WorkboardManager on LAN...");

    network->DiscoverHosts(serverPort_);
}

void WorkboardClient::HandleHostDiscovered(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkHostDiscovered;
    String address = eventData[P_ADDRESS].GetString();
    int port = eventData[P_PORT].GetI32();

    URHO3D_LOGINFOF("Discovered host at %s:%d", address.CString(), port);

    // Use the discovered host
    serverAddress_ = address;
    serverPort_ = static_cast<unsigned short>(port);
    discovering_ = false;

    ConnectToServer();
}

void WorkboardClient::ConnectToServer()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // Set PAKE credentials if a secret was provided — password is hashed with
    // BLAKE2b and mixed into the key exchange, never sent in plaintext
    if (!wbSecret_.Empty())
        network->SetCredentials("workboard", wbSecret_);
    else
        network->ClearCredentials();

    VariantMap identity;
    identity["Name"] = clientName_;
    identity["Role"] = String("client");

    URHO3D_LOGINFOF("Connecting to %s:%u as '%s'%s...",
        serverAddress_.CString(), serverPort_, clientName_.CString(),
        wbSecret_.Empty() ? "" : " [PAKE]");

    network->Connect(serverAddress_, serverPort_, nullptr, identity);
    UpdateConnectionStatus();
}

void WorkboardClient::SendMutation(const String& command, const String& args)
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    auto* conn = network->GetServerConnection();
    if (!conn)
    {
        URHO3D_LOGWARNING("Cannot send mutation — not connected");
        return;
    }

    VariantMap data;
    data[WbMutation::P_COMMAND] = command;
    data[WbMutation::P_ARGS] = args;
    conn->SendRemoteEvent(E_WB_MUTATION, true, data);
}

// ============================================================================
// Connection lifecycle handlers
// ============================================================================

void WorkboardClient::HandleServerConnected(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    connected_ = true;
    reconnectTimer_ = 0.0f;
    URHO3D_LOGINFO("Connected to WorkboardManager");

    // Send identity
    auto* network = GetSubsystem<Network>();
    auto* conn = network ? network->GetServerConnection() : nullptr;
    if (conn)
    {
        VariantMap data;
        data[WbSetIdentity::P_NAME] = clientName_;
        data[WbSetIdentity::P_ROLE] = String("client");
        conn->SendRemoteEvent(E_WB_SET_IDENTITY, true, data);
    }

    UpdateConnectionStatus();
}

void WorkboardClient::HandleServerDisconnected(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    connected_ = false;
    reconnectTimer_ = 0.0f;
    URHO3D_LOGINFO("Disconnected from WorkboardManager");
    UpdateConnectionStatus();
}

void WorkboardClient::HandleConnectFailed(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    connected_ = false;
    reconnectTimer_ = 0.0f;
    URHO3D_LOGWARNING("Connection to WorkboardManager failed");

    // If we were trying localhost implicitly, fall back to LAN discovery
    if (!serverExplicit_ && !discovering_)
    {
        URHO3D_LOGINFO("Localhost failed, falling back to LAN discovery...");
        StartDiscovery();
    }

    UpdateConnectionStatus();
}

// ============================================================================
// Server → Client event handlers
// ============================================================================

void WorkboardClient::HandleWbWelcome(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace WbWelcome;
    String serverName = eventData[P_SERVERNAME].GetString();
    String version = eventData[P_VERSION].GetString();
    int clientCount = eventData[P_CLIENTCOUNT].GetI32();

    URHO3D_LOGINFOF("Welcome from '%s' v%s (%d clients)",
        serverName.CString(), version.CString(), clientCount);

    if (clientCountText_)
        clientCountText_->SetText(String(clientCount) + " client(s)");
}

void WorkboardClient::HandleWbWorkboardFull(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace WbWorkboardFull;
    String markdown = eventData[P_MARKDOWN].GetString();

    ParseWorkboard(markdown);
    RenderWorkboardUI();

    URHO3D_LOGINFOF("Received workboard (%u sections)", sections_.Size());
}

void WorkboardClient::HandleWbPlanList(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace WbPlanList;
    String filenames = eventData[P_FILENAMES].GetString();

    planFiles_.Clear();
    if (!filenames.Empty())
        planFiles_ = filenames.Split('\n');

    // Populate plan list view
    if (planListView_)
    {
        planListView_->RemoveAllItems();
        for (unsigned i = 0; i < planFiles_.Size(); ++i)
        {
            auto* item = new Text(context_);
            item->SetFont(font_, fontSize_);
            item->SetText(planFiles_[i]);
            item->SetColor(Color(0.8f, 0.9f, 1.0f));
            planListView_->AddItem(item);
        }
    }

    URHO3D_LOGINFOF("Received plan list (%u files)", planFiles_.Size());
}

void WorkboardClient::HandleWbPlanContent(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace WbPlanContent;
    String filename = eventData[P_FILENAME].GetString();
    String content = eventData[P_CONTENT].GetString();

    currentPlanFile_ = filename;
    if (planContentText_)
        planContentText_->SetText(content);

    URHO3D_LOGINFOF("Received plan: %s (%u chars)", filename.CString(), content.Length());
}

void WorkboardClient::HandleWbClientList(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace WbClientList;
    String clients = eventData[P_CLIENTS].GetString();

    // Count lines
    unsigned count = 0;
    if (!clients.Empty())
    {
        Vector<String> lines = clients.Split('\n');
        count = lines.Size();
    }

    if (clientCountText_)
        clientCountText_->SetText(String(count) + " client(s)");
}

void WorkboardClient::HandleWbMutationAck(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace WbMutationAck;
    bool success = eventData[P_SUCCESS].GetBool();
    String reason = eventData[P_REASON].GetString();

    if (success)
        URHO3D_LOGINFOF("Mutation OK: %s", reason.CString());
    else
        URHO3D_LOGWARNINGF("Mutation failed: %s", reason.CString());
}

// ParseWorkboard(), RenderWorkboardUI(), AddSectionToUI(), HandleSectionToggle()
// are in WorkboardBase.

// HandlePlanSelected() is in WorkboardBase — calls virtual OnPlanSelected().
void WorkboardClient::OnPlanSelected(const String& filename)
{
    // Request plan content from server
    auto* network = GetSubsystem<Network>();
    auto* conn = network ? network->GetServerConnection() : nullptr;
    if (conn)
    {
        VariantMap data;
        data[WbRequestPlan::P_FILENAME] = filename;
        conn->SendRemoteEvent(E_WB_REQUEST_PLAN, true, data);

        if (planContentText_)
            planContentText_->SetText("Loading " + filename + "...");
    }
}

// ============================================================================
// Event handlers
// ============================================================================

void WorkboardClient::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    float timeStep = eventData[Update::P_TIMESTEP].GetFloat();

    // Refresh local coder instances periodically
    instanceRefreshTimer_ += timeStep;
    if (instanceRefreshTimer_ >= INSTANCE_REFRESH_INTERVAL)
    {
        instanceRefreshTimer_ = 0.0f;
        RefreshInstanceStatus();
    }

    // Auto-reconnect / discovery retry
    if (!connected_)
    {
        if (discovering_)
        {
            // Retry LAN discovery periodically
            discoveryTimer_ += timeStep;
            if (discoveryTimer_ >= DISCOVERY_INTERVAL)
            {
                discoveryTimer_ = 0.0f;
                auto* network = GetSubsystem<Network>();
                if (network)
                    network->DiscoverHosts(serverPort_);
            }
        }
        else
        {
            reconnectTimer_ += timeStep;
            if (reconnectTimer_ >= RECONNECT_INTERVAL)
            {
                reconnectTimer_ = 0.0f;
                ConnectToServer();
            }
        }
    }
}

void WorkboardClient::HandleKeyDown(StringHash /*eventType*/, VariantMap& eventData)
{
    int key = eventData[KeyDown::P_KEY].GetI32();

    if (key == KEY_ESCAPE)
        engine_->Exit();
    else if (key == KEY_F5)
    {
        // Manual reconnect
        if (!connected_)
            ConnectToServer();
    }
}

// HandleSectionToggle() and HandlePlanSelected() are in WorkboardBase

// ============================================================================
// Mutation button handlers
// ============================================================================

void WorkboardClient::HandleClaimTask(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    String taskName = taskNameInput_ ? taskNameInput_->GetText().Trimmed() : String::EMPTY;
    if (taskName.Empty())
        return;

    SendMutation("claim", taskName);
}

void WorkboardClient::HandleMarkDone(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    String taskName = taskNameInput_ ? taskNameInput_->GetText().Trimmed() : String::EMPTY;
    if (taskName.Empty())
        return;

    SendMutation("move-done", taskName);
}

void WorkboardClient::HandleSpawnCoder(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    // Spawn a coder locally on this machine — derive project root from binary location
    String programDir = GetSubsystem<FileSystem>()->GetProgramDir();
    // build/bin/ → strip two levels to get project root
    if (programDir.EndsWith("/"))
        programDir = programDir.Substring(0, programDir.Length() - 1);
    unsigned pos = programDir.FindLast('/');
    if (pos != String::NPOS)
        programDir = programDir.Substring(0, pos);
    pos = programDir.FindLast('/');
    if (pos != String::NPOS)
        programDir = programDir.Substring(0, pos);
    String scriptPath = programDir + "/.claude/hooks/claude_ipc.sh";

    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->FileExists(scriptPath))
    {
        URHO3D_LOGWARNINGF("Cannot spawn coder: %s not found", scriptPath.CString());
        return;
    }

    int ret = fs->SystemCommand(scriptPath + " spawn-coder");
    if (ret == 0)
        URHO3D_LOGINFO("Spawn Coder command executed");
    else
        URHO3D_LOGWARNINGF("Spawn Coder failed (exit code %d)", ret);
}

void WorkboardClient::HandleAddReady(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    String taskName = taskNameInput_ ? taskNameInput_->GetText().Trimmed() : String::EMPTY;
    if (taskName.Empty())
        return;

    SendMutation("add-ready", taskName);
}

// ============================================================================
// Local instance tracking
// ============================================================================

Vector<String> WorkboardClient::DiscoverCoderRoles()
{
    // Coder roles are reported by Manager via network beacon.
    // Remote clients don't scan local files — they receive updates.
    // Return whatever Manager last told us.
    return knownCoderRoles_;
}

bool WorkboardClient::IsYukiAlive()
{
    // Yuki is embedded in Manager — if we're connected, Yuki is available.
    return connected_;
}

void WorkboardClient::RefreshInstanceStatus()
{
    Vector<String> liveCoders = DiscoverCoderRoles();
    knownCoderRoles_ = liveCoders;

    if (!coderStatusDropdown_)
        return;

    unsigned prevSel = coderStatusDropdown_->GetSelection();
    coderStatusDropdown_->RemoveAllItems();

    if (knownCoderRoles_.Empty())
    {
        auto* item = new Text(context_);
        item->SetFont(font_, fontSize_);
        item->SetText("Coders: none");
        item->SetColor(Color(0.5f, 0.5f, 0.5f));
        item->SetMinSize(170, 20);
        coderStatusDropdown_->AddItem(item);
    }
    else
    {
        // Summary header if multiple
        if (knownCoderRoles_.Size() > 1)
        {
            auto* header = new Text(context_);
            header->SetFont(font_, fontSize_);
            header->SetMinSize(170, 20);
            header->SetText("Coders: " + String(knownCoderRoles_.Size()) + " online");
            header->SetColor(Color(0.3f, 1.0f, 0.5f));
            coderStatusDropdown_->AddItem(header);
        }

        for (const String& role : knownCoderRoles_)
        {
            String label = role;
            if (!label.Empty())
                label[0] = (char)toupper(label[0]);

            auto* item = new Text(context_);
            item->SetFont(font_, fontSize_);
            item->SetMinSize(170, 20);
            item->SetText(label + "  ONLINE");
            item->SetColor(Color(0.3f, 1.0f, 0.5f));
            coderStatusDropdown_->AddItem(item);
        }
    }

    if (knownCoderRoles_.Size() > 1)
        coderStatusDropdown_->SetSelection(0);
    else if (prevSel < coderStatusDropdown_->GetNumItems())
        coderStatusDropdown_->SetSelection(prevSel);
    else if (coderStatusDropdown_->GetNumItems() > 0)
        coderStatusDropdown_->SetSelection(0);

    // Report local instance liveness to Manager
    if (connected_)
    {
        auto* network = GetSubsystem<Network>();
        auto* conn = network ? network->GetServerConnection() : nullptr;
        if (conn)
        {
            String coderRolesStr;
            for (unsigned i = 0; i < knownCoderRoles_.Size(); ++i)
            {
                if (i > 0)
                    coderRolesStr += ",";
                coderRolesStr += knownCoderRoles_[i];
            }

            VariantMap data;
            data[WbInstanceStatus::P_YUKI_ALIVE] = IsYukiAlive();
            data[WbInstanceStatus::P_CODER_COUNT] = static_cast<int>(knownCoderRoles_.Size());
            data[WbInstanceStatus::P_CODER_ROLES] = coderRolesStr;
            conn->SendRemoteEvent(E_WB_INSTANCE_STATUS, true, data);
        }
    }
}

// ============================================================================
// Helpers
// ============================================================================

void WorkboardClient::UpdateConnectionStatus()
{
    if (connectionDot_)
    {
        if (connected_)
            connectionDot_->SetColor(Color(0.3f, 1.0f, 0.3f));  // green
        else
            connectionDot_->SetColor(Color(1.0f, 0.3f, 0.3f));  // red
    }

    if (statusText_)
    {
        if (connected_)
            statusText_->SetText("Connected to " + serverAddress_ + ":" + String(serverPort_));
        else
            statusText_->SetText("Disconnected — reconnecting to " + serverAddress_ + ":" + String(serverPort_) + "...");
    }
}
