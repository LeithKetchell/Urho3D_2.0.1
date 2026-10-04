// Karen — RUDP telemetry emitter.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "../Precompiled.h"

#include "../Core/KarenClient.h"
#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../IO/Log.h"
#include "../IO/VectorBuffer.h"
#include "../Network/Network.h"
#include "../Network/NetworkEvents.h"
#include "../Network/Connection.h"
#include "../Network/Protocol.h"

#include "../DebugNew.h"

namespace Urho3D
{

// Karen message IDs
static const int MSG_KAREN_WELCOME    = MSG_USER + 300;
static const int MSG_KAREN_FRAME      = MSG_USER + 301;
static const int MSG_KAREN_ZONE_BEGIN = MSG_USER + 302;
static const int MSG_KAREN_ZONE_END   = MSG_USER + 303;
static const int MSG_KAREN_PLOT       = MSG_USER + 304;
static const int MSG_KAREN_MESSAGE    = MSG_USER + 305;
// Consumer -> emitter: "I am a Karen viewer, send me telemetry." Gates collection + delivery on a real
// consumer (Manager is the common link). Disjoint from any host protocol, so non-Karen peers never send it.
static const int MSG_KAREN_SUBSCRIBE  = MSG_USER + 306;
// Client -> collector (inversion): "I am Karen for app <name>", sent on dial-connect so the Manager collector
// tags this outbound stream with its source app. The client-dial counterpart of MSG_KAREN_SUBSCRIBE.
static const int MSG_KAREN_HELLO      = MSG_USER + 307;

// Client-dial reconnect (P4) backoff bounds: first retry after BASE, doubling each attempt up to MAX.
static const unsigned KAREN_RECONNECT_BASE_MS = 1000;
static const unsigned KAREN_RECONNECT_MAX_MS  = 30000;

KarenClient::KarenClient(Context* context) :
    Object(context)
{
}

KarenClient::~KarenClient()
{
    if (active_)
        Stop();
}

bool KarenClient::Start(const String& appName, const VariantMap& hostBeacon)
{
    if (active_)
        return true;

    auto* network = GetSubsystem<Network>();
    if (!network)
    {
        URHO3D_LOGERROR("KarenClient: Network subsystem not available");
        return false;
    }

    // Parasite: ride the host's existing server if it has one (Urho's Network has a
    // single server peer, so we cannot bind a second port anyway); only open our own
    // on KAREN_PORT when the host has none. Telemetry rides whatever server is up —
    // Karen message IDs are disjoint from any host protocol, so other clients ignore it.
    ownsServer_ = !network->IsServerRunning();
    if (ownsServer_ && !network->StartServer(KAREN_PORT))
    {
        URHO3D_LOGERROR("KarenClient: no host server and failed to start own on port " + String(KAREN_PORT));
        ownsServer_ = false;
        return false;
    }

    // Advertise a KarenTelemetry capability WITHOUT clobbering the host's own beacon
    // (e.g. its chat-client discovery). Merge into the map the host passed; a viewer
    // finds any karen-bearing host by this flag plus the port discovery reports.
    VariantMap beacon = hostBeacon;
    beacon["Karen"] = "1";
    beacon["KarenName"] = appName.Empty() ? "Karen" : appName;
    if (!beacon.Contains("Type"))
        beacon["Type"] = "KarenTelemetry";   // standalone fallback when riding nothing
    network->SetDiscoveryBeacon(beacon);

    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(KarenClient, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(KarenClient, HandleClientDisconnected));
    // Listen for MSG_KAREN_SUBSCRIBE. Both this and the host's own message handler fire per message; each
    // filters by ID, so riding a shared server (e.g. Yuki) is safe — we ignore all non-Karen traffic.
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(KarenClient, HandleNetworkMessage));

    appName_ = appName;
    frameNumber_ = 0;
    zoneDepth_ = 0;
    timer_.Reset();
    active_ = true;

    URHO3D_LOGINFO("KarenClient: " + String(ownsServer_
        ? "emitting on own port " + String(KAREN_PORT)
        : "riding host server") + " as '" + (appName.Empty() ? "Karen" : appName) + "'");
    return true;
}

void KarenClient::Stop()
{
    if (!active_)
        return;

    auto* network = GetSubsystem<Network>();
    if (network && ownsServer_ && network->IsServerRunning())
        network->StopServer();   // only a server WE created — never the host's
    if (network && clientMode_ && network->GetServerConnection())
        network->Disconnect();   // client-dial: close the outbound connection to the collector

    UnsubscribeFromAllEvents();
    subscribedViewers_.Clear();
    ownsServer_ = false;
    clientMode_ = false;
    collectorConnected_ = false;
    reconnectDelayMs_ = 0;   // client-dial reconnect (P4): clear backoff state
    active_ = false;

    URHO3D_LOGINFO("KarenClient: Stopped");
}

bool KarenClient::ConnectToCollector(const String& appName, const String& address, unsigned short port)
{
    if (active_)
    {
        // Idempotent only when we're ALREADY client-dialing — return true so a repeat dial is a harmless no-op.
        // But if we're active in SERVER mode (Start() was called), the dial did NOT happen: report false + warn
        // rather than silently claiming success, so the caller doesn't believe telemetry is streaming to a
        // collector it never reached. One mode at a time until the inversion fully lands.
        if (clientMode_)
            return true;
        URHO3D_LOGWARNING("KarenClient: ConnectToCollector ignored — already active in server mode "
            "(call Stop() first to switch to client-dial)");
        return false;
    }

    auto* network = GetSubsystem<Network>();
    if (!network)
    {
        URHO3D_LOGERROR("KarenClient: Network subsystem not available");
        return false;
    }

    // Dial the collector over Urho's SEPARATE client peer (rakPeerClient_) — independent of any server the
    // host app runs on rakPeer_, so this never conflicts with the host's own networking.
    if (!network->Connect(address, port, nullptr))
    {
        URHO3D_LOGERROR("KarenClient: failed to dial collector " + address + ":" + String(port));
        return false;
    }

    // Track the outbound handshake; we identify to the collector once E_SERVERCONNECTED fires.
    SubscribeToEvent(E_SERVERCONNECTED,    URHO3D_HANDLER(KarenClient, HandleServerConnected));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(KarenClient, HandleServerDisconnected));
    SubscribeToEvent(E_CONNECTFAILED,      URHO3D_HANDLER(KarenClient, HandleServerDisconnected));
    // Client-dial reconnect (P4): tick each frame so a dropped/failed dial is re-attempted on backoff.
    // The handler is a no-op while connected, so this costs one early-out per frame in steady state.
    SubscribeToEvent(E_BEGINFRAME,         URHO3D_HANDLER(KarenClient, HandleReconnectTick));

    appName_ = appName;
    collectorAddress_ = address;
    collectorPort_ = port;
    frameNumber_ = 0;
    zoneDepth_ = 0;
    reconnectDelayMs_ = 0;   // no backoff until the first drop/fail
    timer_.Reset();
    clientMode_ = true;
    active_ = true;

    URHO3D_LOGINFO("KarenClient: dialing collector " + address + ":" + String(port) +
        " as '" + (appName.Empty() ? "Karen" : appName) + "'");
    return true;
}

void KarenClient::HandleServerConnected(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    collectorConnected_ = true;
    // A successful (re)connect clears the backoff so the NEXT drop starts retrying from base again.
    reconnectDelayMs_ = KAREN_RECONNECT_BASE_MS;
    reconnectTimer_.Reset();
    // Identify ourselves so the collector can tag this stream with its source app. Reliable+ordered — a
    // dropped HELLO would leave the stream unlabelled at the collector. Re-sent on every reconnect so the
    // collector can re-label a source that dropped and came back.
    auto* network = GetSubsystem<Network>();
    if (network)
        if (Connection* c = network->GetServerConnection())
        {
            VectorBuffer msg;
            msg.WriteString(appName_.Empty() ? String("Karen") : appName_);
            c->SendMessage(MSG_KAREN_HELLO, true, true, msg);
        }
    URHO3D_LOGINFO("KarenClient: connected to collector " + collectorAddress_ + ":" + String(collectorPort_));
}

void KarenClient::HandleServerDisconnected(StringHash eventType, VariantMap& /*eventData*/)
{
    collectorConnected_ = false;
    // Client-dial reconnect (P4): arm the backoff and restart the clock so HandleReconnectTick spaces the
    // next dial. Keep any already-doubled interval (only seed it to base on the first drop).
    if (reconnectDelayMs_ == 0)
        reconnectDelayMs_ = KAREN_RECONNECT_BASE_MS;
    reconnectTimer_.Reset();
    URHO3D_LOGINFO((eventType == E_CONNECTFAILED
        ? String("KarenClient: collector dial failed (") + collectorAddress_ + ":" + String(collectorPort_) + ")"
        : String("KarenClient: collector disconnected"))
        + " — retrying in " + String(reconnectDelayMs_) + "ms");
}

void KarenClient::HandleReconnectTick(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    // Only act while dialing out AND currently disconnected — a cheap early-out the rest of the time.
    if (!clientMode_ || collectorConnected_)
        return;
    if (reconnectDelayMs_ == 0)
        reconnectDelayMs_ = KAREN_RECONNECT_BASE_MS;
    if (reconnectTimer_.GetMSec(false) < reconnectDelayMs_)
        return;   // backoff interval not elapsed yet

    auto* network = GetSubsystem<Network>();
    if (!network)
        return;
    // If a dial is already in flight (Urho holds a not-yet-connected server connection), wait for
    // E_SERVERCONNECTED / E_CONNECTFAILED to resolve it rather than stacking Connect() calls.
    // PRECONDITION (inherited from P1's client-dial): this keys on the host's single serverConnection_, so the
    // redial only works when the host app isn't itself a Network client — if it were, GetServerConnection() would
    // never be null and reconnect would never fire. True for every current host (servers, e.g. Yuki).
    if (network->GetServerConnection())
        return;

    URHO3D_LOGINFO("KarenClient: re-dialing collector " + collectorAddress_ + ":" + String(collectorPort_) +
        " (backoff " + String(reconnectDelayMs_) + "ms)");
    network->Connect(collectorAddress_, collectorPort_, nullptr);
    reconnectTimer_.Reset();
    // Exponential backoff toward the cap; reset to base happens only on a successful connect.
    reconnectDelayMs_ *= 2;
    if (reconnectDelayMs_ > KAREN_RECONNECT_MAX_MS)
        reconnectDelayMs_ = KAREN_RECONNECT_MAX_MS;
}

void KarenClient::MarkFrame()
{
    if (!active_ || GetViewerCount() == 0)
        return;

    VectorBuffer msg;
    msg.WriteU32(frameNumber_++);
    msg.WriteU64((unsigned long long)timer_.GetUSec(false));

    SendToViewers(MSG_KAREN_FRAME, msg);
}

void KarenClient::BeginZone(const char* name)
{
    if (!active_ || GetViewerCount() == 0)
        return;

    VectorBuffer msg;
    msg.WriteString(String(name));
    msg.WriteU64((unsigned long long)timer_.GetUSec(false));
    msg.WriteU8((unsigned char)zoneDepth_);

    zoneDepth_++;
    SendToViewers(MSG_KAREN_ZONE_BEGIN, msg);
}

void KarenClient::EndZone()
{
    if (!active_ || GetViewerCount() == 0)
        return;

    if (zoneDepth_ > 0)
        zoneDepth_--;

    VectorBuffer msg;
    msg.WriteU64((unsigned long long)timer_.GetUSec(false));

    SendToViewers(MSG_KAREN_ZONE_END, msg);
}

void KarenClient::Plot(const char* name, float value)
{
    if (!active_ || GetViewerCount() == 0)
        return;

    VectorBuffer msg;
    msg.WriteString(String(name));
    msg.WriteFloat(value);
    msg.WriteU64((unsigned long long)timer_.GetUSec(false));

    SendToViewers(MSG_KAREN_PLOT, msg);
}

void KarenClient::Message(const String& text, unsigned color)
{
    if (!active_ || GetViewerCount() == 0)
        return;

    VectorBuffer msg;
    msg.WriteString(text);
    msg.WriteU32(color);
    msg.WriteU64((unsigned long long)timer_.GetUSec(false));

    SendToViewers(MSG_KAREN_MESSAGE, msg);
}

unsigned KarenClient::GetViewerCount() const
{
    // SUBSCRIBED consumers only — NOT raw client connections. A host peer that merely connected (e.g. a Yuki
    // chat client) is not a Karen viewer, so it must not make emitters think a consumer is present. Count only
    // NON-EXPIRED weak refs so a connection killed by a silent StopServer() stops counting immediately.
    unsigned n = 0;
    for (Vector<WeakPtr<Connection> >::ConstIterator it = subscribedViewers_.Begin(); it != subscribedViewers_.End(); ++it)
        if (!it->Expired())
            ++n;
    // Client-dial: the connected Manager collector is a live consumer too, so callers that gate on
    // GetViewerCount() > 0 still emit when we're dialed out (even with no inbound viewers).
    if (clientMode_ && collectorConnected_)
        ++n;
    return n;
}

void KarenClient::HandleClientConnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientConnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    // Greet the new connection. This does NOT make it a viewer — it counts only once it sends MSG_KAREN_SUBSCRIBE.
    VectorBuffer msg;
    msg.WriteString(appName_);
    msg.WriteU64((unsigned long long)timer_.GetUSec(false));

    conn->SendMessage(MSG_KAREN_WELCOME, true, true, msg);
}

void KarenClient::HandleClientDisconnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientDisconnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    // Drop the disconnecting connection, and compact any already-expired refs while we're here (back-to-front
    // so Erase doesn't shift indices we still need to visit).
    for (unsigned i = subscribedViewers_.Size(); i-- > 0; )
        if (subscribedViewers_[i].Expired() || subscribedViewers_[i].Get() == conn)
            subscribedViewers_.Erase(i);
    URHO3D_LOGINFO("KarenClient: Viewer disconnected (" + String(GetViewerCount()) + " subscribed)");
}

void KarenClient::HandleNetworkMessage(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkMessage;
    if (eventData[P_MESSAGEID].GetI32() != MSG_KAREN_SUBSCRIBE)
        return;   // not ours — the host's own handler deals with its protocol
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (!conn)
        return;
    // Compact expired refs before (re)adding: if telemetry isn't flowing, SendToViewers() never runs to clean
    // them, so a subscribe-only path would still leak dead WeakPtrs across silent-StopServer cycles. Back-to-front.
    for (unsigned i = subscribedViewers_.Size(); i-- > 0; )
        if (subscribedViewers_[i].Expired())
            subscribedViewers_.Erase(i);
    for (Vector<WeakPtr<Connection> >::ConstIterator it = subscribedViewers_.Begin(); it != subscribedViewers_.End(); ++it)
        if (it->Get() == conn)
            return;   // already subscribed
    subscribedViewers_.Push(WeakPtr<Connection>(conn));
    URHO3D_LOGINFO("KarenClient: Karen viewer subscribed (" + String(GetViewerCount()) + " total)");
}

void KarenClient::SendToViewers(int msgID, const VectorBuffer& msg)
{
    // Deliver to SUBSCRIBED viewers only (unreliable, unordered) — never BroadcastMessage across the whole
    // server, which would spray telemetry at the host's non-Karen clients (e.g. Yuki chat peers). Send to live
    // viewers and DROP expired weak refs in the same pass: a silent Network::StopServer() frees connections
    // WITHOUT firing E_CLIENTDISCONNECTED, so without this compaction the dead refs accumulate unbounded across
    // server up/down cycles. Back-to-front so Erase() doesn't shift indices we still need to visit.
    for (unsigned i = subscribedViewers_.Size(); i-- > 0; )
    {
        if (Connection* c = subscribedViewers_[i].Get())
            c->SendMessage(msgID, false, false, msg);
        else
            subscribedViewers_.Erase(i);
    }

    // Client-dial (inversion): also stream out to the single Manager collector over the outbound connection.
    if (clientMode_ && collectorConnected_)
        if (auto* network = GetSubsystem<Network>())
            if (Connection* c = network->GetServerConnection())
                c->SendMessage(msgID, false, false, msg);
}

// ─── KarenZone RAII ──────────────────────────────────────────────────────────

KarenZone::KarenZone(KarenClient* client, const char* name) :
    client_(client)
{
    if (client_)
        client_->BeginZone(name);
}

KarenZone::~KarenZone()
{
    if (client_)
        client_->EndZone();
}

}
