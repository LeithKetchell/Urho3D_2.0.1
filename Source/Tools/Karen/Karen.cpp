// Karen — standalone telemetry collector + display hub.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include "Karen.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/KarenClient.h>          // KAREN_PORT
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Protocol.h>          // MSG_USER
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

// KarenClient's message ids are file-local to KarenClient.cpp; Karen only needs to recognise them on
// the receive side. Mirrored here by hand, verified against KarenClient.cpp, and kept in sync until
// they're promoted to a shared header (a good follow-up).
static const int MSG_KAREN_WELCOME = MSG_USER + 300;
static const int MSG_KAREN_HELLO   = MSG_USER + 307;   // carries the emitter's app name

Karen::Karen(Context* context) :
    Application(context)
{
}

void Karen::Setup()
{
    engineParameters_[EP_WINDOW_TITLE]     = "Karen";
    engineParameters_[EP_WINDOW_WIDTH]     = 1100;
    engineParameters_[EP_WINDOW_HEIGHT]    = 680;
    engineParameters_[EP_FULL_SCREEN]      = false;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
    engineParameters_[EP_SOUND]            = false;
    engineParameters_[EP_LOG_NAME]         = "Karen.log";
}

void Karen::Start()
{
    if (!StartCollector())
    {
        // Singleton guard: another Karen already holds the port. Refuse to compete — there is one Karen.
        URHO3D_LOGERROR("Karen is already running on port " + String((int)KAREN_PORT) +
                        " — refusing to start a second. There is only one Karen.");
        engine_->Exit();
        return;
    }

    CreateStatusUi();

    SubscribeToEvent(E_CLIENTCONNECTED,    URHO3D_HANDLER(Karen, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(Karen, HandleClientDisconnected));
    SubscribeToEvent(E_NETWORKMESSAGE,     URHO3D_HANDLER(Karen, HandleNetworkMessage));

    URHO3D_LOGINFO("Karen up — collecting on port " + String((int)KAREN_PORT) + ". One hub, many clients.");
}

void Karen::Stop()
{
    if (auto* network = GetSubsystem<Network>())
        network->StopServer();
}

bool Karen::StartCollector()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return false;
    // StartServer binds KAREN_PORT. Returns false if the port is taken — our singleton guard.
    return network->StartServer(KAREN_PORT);
}

void Karen::CreateStatusUi()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    Text* status = ui->GetRoot()->CreateChild<Text>();
    if (font)
        status->SetFont(font, 15);
    status->SetText(String("Karen — listening on ") + String((int)KAREN_PORT) + "\n0 emitters, 0 messages");
    status->SetPosition(12, 12);
    status->SetColor(Color(0.6f, 0.85f, 1.0f));
    statusText_ = status;
}

void Karen::HandleClientConnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientConnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (conn && !emitters_.Contains(conn))
        emitters_[conn] = String::EMPTY;   // app name filled in on HELLO
    URHO3D_LOGINFO("Karen: emitter connected (" + String(emitters_.Size()) + " total)");
}

void Karen::HandleClientDisconnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientDisconnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (conn)
        emitters_.Erase(conn);
}

void Karen::HandleNetworkMessage(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkMessage;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    const int msgID = eventData[P_MESSAGEID].GetI32();

    // Only Karen's own id range (WELCOME..HELLO); ignore anything else on the wire.
    if (msgID < MSG_KAREN_WELCOME || msgID > MSG_KAREN_HELLO)
        return;

    ++messagesSeen_;

    // HELLO carries the emitter's app name — record who's reporting.
    if (msgID == MSG_KAREN_HELLO && conn)
    {
        const Vector<byte>& data = eventData[P_DATA].GetBuffer();
        MemoryBuffer buf(data);
        const String appName = buf.ReadString();
        emitters_[conn] = appName;
        URHO3D_LOGINFO("Karen: HELLO from '" + appName + "'");
    }

    // Refresh the status line. Full per-message decode + the ProfilerUI display lift from
    // WorkboardManager::HandleKarenMessage next.
    if (statusText_)
        statusText_->SetText(String("Karen — listening on ") + String((int)KAREN_PORT) + "\n" +
                             String(emitters_.Size()) + " emitters, " + String(messagesSeen_) + " messages");
}

URHO3D_DEFINE_APPLICATION_MAIN(Karen);
