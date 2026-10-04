// WorkboardClient — thin LAN client for viewing and interacting with
// the workboard served by WorkboardManager over SLikeNet UDP.

#pragma once

#include "../WorkboardManager/WorkboardBase.h"

#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/DropDownList.h>
#include <Urho3D/UI/LineEdit.h>

using namespace Urho3D;

class WorkboardClient : public WorkboardBase
{
    URHO3D_OBJECT(WorkboardClient, WorkboardBase);

public:
    explicit WorkboardClient(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    // ── UI creation ──
    void CreateUI();
    void CreateStatusBar(UIElement* parent, int w, int h);
    void CreateInstanceStatusBar(UIElement* parent, int x, int y, int w, int h);
    void CreateMutationBar(UIElement* parent, int x, int y, int w, int h);

    // ── Network ──
    void RegisterRemoteEvents();
    void ConnectToServer();
    void StartDiscovery();
    void SendMutation(const String& command, const String& args);

    // ── Discovery ──
    void HandleHostDiscovered(StringHash eventType, VariantMap& eventData);

    // ── Plan (virtual override) ──
    void OnPlanSelected(const String& filename) override;

    // ── Event handlers ──
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);

    // Server → Client events
    void HandleWbWelcome(StringHash eventType, VariantMap& eventData);
    void HandleWbWorkboardFull(StringHash eventType, VariantMap& eventData);
    void HandleWbPlanList(StringHash eventType, VariantMap& eventData);
    void HandleWbPlanContent(StringHash eventType, VariantMap& eventData);
    void HandleWbClientList(StringHash eventType, VariantMap& eventData);
    void HandleWbMutationAck(StringHash eventType, VariantMap& eventData);
    /// Phase 2c: server-initiated graceful shutdown notice. Distinguishes a
    /// planned server stop from a network drop or mutation failure.
    void HandleWbServerShutdown(StringHash eventType, VariantMap& eventData);

    // Connection lifecycle
    void HandleServerConnected(StringHash eventType, VariantMap& eventData);
    void HandleServerDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleConnectFailed(StringHash eventType, VariantMap& eventData);

    // Mutation buttons
    void HandleClaimTask(StringHash eventType, VariantMap& eventData);
    void HandleMarkDone(StringHash eventType, VariantMap& eventData);
    void HandleAddReady(StringHash eventType, VariantMap& eventData);
    void HandleSpawnCoder(StringHash eventType, VariantMap& eventData);

    // ── Local instance tracking ──
    Vector<String> DiscoverCoderRoles();
    bool IsYukiAlive();
    void RefreshInstanceStatus();

    // ── Helpers ──
    void UpdateConnectionStatus();

    // ── Network config ──
    String serverAddress_{"localhost"};
    unsigned short serverPort_{31337};
    String wbSecret_;
    String clientName_;
    bool serverExplicit_{false};  // true if --server was provided
    bool discovering_{false};
    float discoveryTimer_{0.0f};
    static constexpr float DISCOVERY_INTERVAL = 3.0f;

    // ── Connection state ──
    bool connected_{false};
    float reconnectTimer_{0.0f};
    static constexpr float RECONNECT_INTERVAL = 5.0f;

    // ── Status bar UI ──
    Text* statusText_{};
    Text* connectionDot_{};
    Text* clientCountText_{};

    // ── Instance status UI ──
    DropDownList* coderStatusDropdown_{};
    Vector<String> knownCoderRoles_;
    float instanceRefreshTimer_{0.0f};
    static constexpr float INSTANCE_REFRESH_INTERVAL = 5.0f;
    String ipcDir_;

    // ── Auto-spawn (doorkeeper) ──
    float autoSpawnCooldown_{0.0f};
    static constexpr float AUTO_SPAWN_COOLDOWN = 30.0f;

    // ── Mutation bar UI ──
    LineEdit* taskNameInput_{};
    Button* claimBtn_{};
    Button* doneBtn_{};
    Button* addReadyBtn_{};
    Button* spawnCoderBtn_{};
};
