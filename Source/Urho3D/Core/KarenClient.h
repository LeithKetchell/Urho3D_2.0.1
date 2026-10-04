// KarenClient — RUDP Telemetry Emitter
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT:  Lightweight telemetry subsystem. Register in any Urho3D app to stream
//        profiling data to Karen viewers on the LAN. Zero overhead when no viewer
//        is connected — all emit calls check GetViewerCount() first.
//
// USE:   context_->RegisterSubsystem(new KarenClient(context_));
//        GetSubsystem<KarenClient>()->Start("MyApp");
//        Then: MarkFrame() each frame, BeginZone/EndZone for scoped timing,
//        Plot() for scalar values, Message() for log strings.
//        Or use macros: KAREN_ZONE(name), KAREN_PLOT(name, val), KAREN_MESSAGE(text).
//
// NET:   Starts server on KAREN_PORT (25771). Sets discovery beacon with
//        Type="KarenTelemetry" so Karen viewer only connects to emitters.
//        All telemetry sent unreliable unordered — fire-and-forget.
//        Multiple viewers can connect simultaneously.
//
// RAII:  KarenZone class wraps BeginZone/EndZone for automatic scope exit.
//
// DEPS:  Network (SLikeNet), HiresTimer, VectorBuffer.

#pragma once

#include "../Core/Object.h"
#include "../Core/Timer.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Container/Ptr.h"

namespace Urho3D
{

class Network;
class Connection;

/// Canonical Karen telemetry port (prime). Today the emitter serves on it; post-inversion this is the
/// fixed port of the single Manager-side collector that every injected Karen client dials into.
static const unsigned short KAREN_PORT = 25771;

/// RUDP telemetry emitter. Register as a subsystem, call BeginZone/EndZone/Plot/Message.
/// Streams to all connected Karen viewers. Zero overhead when no viewer is connected.
class URHO3D_API KarenClient : public Object
{
    URHO3D_OBJECT(KarenClient, Object);

public:
    explicit KarenClient(Context* context);
    ~KarenClient() override;

    /// Start emitting. Parasite model: if the host app already runs a Network server,
    /// RIDE it (telemetry is broadcast over the host's connections, disjoint message
    /// IDs); only stand up our own server on KAREN_PORT when the host has none. Pass
    /// the host's discovery-beacon map so a "KarenTelemetry" capability flag can be
    /// MERGED in without clobbering the host's own beacon (empty map = none).
    bool Start(const String& appName = String::EMPTY, const VariantMap& hostBeacon = Variant::emptyVariantMap);
    /// Stop emitting. Only tears down the server if WE created it (never the host's).
    void Stop();

    /// INVERSION PATH (client-dial). Instead of hosting/riding a server for viewers to discover, DIAL OUT to
    /// the single Manager-side collector at `address:port` and stream telemetry over that one outbound
    /// connection. Uses Urho's SEPARATE client peer (`rakPeerClient_`), so it coexists with any server the host
    /// app runs on `rakPeer_`. Additive to Start() — an app uses one mode or the other; the emitter-as-server
    /// model stays until the inversion fully lands. Default address is the local Manager.
    bool ConnectToCollector(const String& appName = String::EMPTY, const String& address = "127.0.0.1",
                            unsigned short port = KAREN_PORT);

    /// Mark frame boundary.
    void MarkFrame();
    /// Begin a named zone (scoped timing).
    void BeginZone(const char* name);
    /// End the current zone.
    void EndZone();
    /// Emit a named plot data point.
    void Plot(const char* name, float value);
    /// Emit a log message.
    void Message(const String& text, unsigned color = 0xFFFFFFFF);

    /// Whether emitter is active.
    bool IsActive() const { return active_; }
    /// Number of SUBSCRIBED Karen viewers — connections that identified with MSG_KAREN_SUBSCRIBE (Manager, or a
    /// standalone viewer). A plain host-protocol peer (e.g. a Yuki chat client) does NOT count, so callers can
    /// gate telemetry collection on a real Karen consumer being present.
    unsigned GetViewerCount() const;

private:
    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    /// Receives MSG_KAREN_SUBSCRIBE from a consumer and records that connection as a real viewer.
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);
    /// Client-dial (inversion) outbound status: connected to the Manager collector / dropped or failed.
    void HandleServerConnected(StringHash eventType, VariantMap& eventData);
    void HandleServerDisconnected(StringHash eventType, VariantMap& eventData);
    /// Client-dial reconnect (P4): re-dial the collector on an exponential backoff after a drop or dial-fail.
    /// Driven by E_BEGINFRAME while in clientMode_; a no-op when connected. Backoff resets on a successful dial.
    void HandleReconnectTick(StringHash eventType, VariantMap& eventData);

    /// Send to SUBSCRIBED viewers only (unreliable, unordered) — never spray telemetry across the host's
    /// other clients. In client-dial mode ALSO streams out to the Manager collector.
    void SendToViewers(int msgID, const VectorBuffer& msg);

    HiresTimer timer_;
    String appName_;
    unsigned frameNumber_{};
    unsigned zoneDepth_{};
    bool active_{};
    bool ownsServer_{};   // true only if WE called StartServer (we then own teardown)
    /// Connections that sent MSG_KAREN_SUBSCRIBE — the real Karen consumers. Held as WeakPtr, NOT raw pointers:
    /// Network::StopServer() clears its connections WITHOUT firing E_CLIENTDISCONNECTED, so a raw pointer would
    /// dangle (UAF on the next send/count) if the host tears down its server while we ride it. A WeakPtr expires
    /// when the Connection is destroyed, so both SendToViewers() (skip expired) and GetViewerCount() (count
    /// non-expired) stay correct regardless of how a connection dies. Normal disconnect still erases eagerly.
    Vector<WeakPtr<Connection> > subscribedViewers_;

    /// Client-dial (inversion) state. When clientMode_, telemetry is streamed OUT to the single Manager
    /// collector over Urho's outbound server-connection, in addition to any subscribed inbound viewers.
    /// collectorConnected_ tracks the dial handshake so GetViewerCount() reports a live consumer and gating
    /// callers still emit; address/port are kept for logging (and a future reconnect).
    bool clientMode_{false};
    bool collectorConnected_{false};
    String collectorAddress_;
    unsigned short collectorPort_{KAREN_PORT};

    /// Client-dial reconnect (P4). reconnectTimer_ measures wall-clock since the last dial attempt;
    /// reconnectDelayMs_ is the current backoff interval — 0 before the first dial, seeded to base on connect and
    /// on the first drop, then base→max doubling per retry, reset to base on each successful connect. Dormant while
    /// connected; only drives redials while clientMode_ && !collectorConnected_.
    Timer reconnectTimer_;
    unsigned reconnectDelayMs_{0};
};

/// RAII scoped zone for KarenClient.
class URHO3D_API KarenZone
{
public:
    KarenZone(KarenClient* client, const char* name);
    ~KarenZone();

private:
    KarenClient* client_;
};

/// Convenience macro — use in any function with a KarenClient subsystem.
#define KAREN_ZONE(name) \
    KarenZone karenZone_##name(GetSubsystem<KarenClient>(), #name)

#define KAREN_PLOT(name, value) \
    if (auto* kc = GetSubsystem<KarenClient>()) kc->Plot(name, value)

#define KAREN_MESSAGE(text) \
    if (auto* kc = GetSubsystem<KarenClient>()) kc->Message(text)

}
