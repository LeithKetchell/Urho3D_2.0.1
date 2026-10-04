// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Audio/Audio.h>
#include <Urho3D/Audio/BufferedSoundStream.h>
#include <Urho3D/Audio/SoundSource.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>

#include "VoiceChat.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

#ifdef SendMessage
#undef SendMessage
#endif

static const int MSG_VOICE_DATA = MSG_USER + 100;
static const unsigned short VOICE_PORT = 3457;
static const i32 CAPTURE_RATE = 16000;
static const unsigned DRAIN_SAMPLES = 1600;

URHO3D_DEFINE_APPLICATION_MAIN(VoiceChat)

VoiceChat::VoiceChat(Context* context) :
    Sample(context)
{
}

void VoiceChat::Start()
{
    Sample::Start();
    GetSubsystem<Input>()->SetMouseVisible(true);

    // UI
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    auto* root = GetSubsystem<UI>()->GetRoot();
    root->SetDefaultStyle(uiStyle);

    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    statusText_ = root->CreateChild<Text>();
    statusText_->SetFont(font, 16);
    statusText_->SetColor(Color::YELLOW);
    statusText_->SetPosition(20, 20);
    statusText_->SetText("Starting...");

    // Talk button
    talkButton_ = root->CreateChild<Button>();
    talkButton_->SetStyleAuto();
    talkButton_->SetFixedSize(200, 60);
    talkButton_->SetPosition(20, 60);

    auto* btnText = talkButton_->CreateChild<Text>();
    btnText->SetFont(font, 20);
    btnText->SetAlignment(HA_CENTER, VA_CENTER);
    btnText->SetText("TALK");

    SetLogoVisible(true);
    GetSubsystem<Renderer>()->GetDefaultZone()->SetFogColor(Color(0.0f, 0.0f, 0.15f));

    Sample::InitMouseMode(MM_FREE);

    // Profiler
    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler());
    profilerUI_->SetVisible(true);

    // Pre-open mic
    auto* audio = GetSubsystem<Audio>();
    unsigned numDevices = audio->GetNumCaptureDevices();
    if (numDevices > 0)
    {
        String devName = audio->GetCaptureDeviceName(0);
        micPreOpened_ = audio->PreOpenCapture(devName, CAPTURE_RATE);
        URHO3D_LOGINFO(String(micPreOpened_
            ? "[VOICE] Mic ready: " : "[VOICE] Mic failed: ") + devName);
    }

    // Playback stream
    i32 actualRate = audio->GetCaptureSampleRate();
    if (actualRate <= 0)
        actualRate = CAPTURE_RATE;
    playStream_ = new BufferedSoundStream();
    playStream_->SetFormat(actualRate, true, false);

    scene_ = new Scene(context_);
    auto* playbackNode = scene_->CreateChild("VoicePlayback");
    playSoundSource_ = playbackNode->CreateComponent<SoundSource>();
    playSoundSource_->SetAutoRemoveMode(REMOVE_DISABLED);
    playSoundSource_->Play(playStream_);

    drainBuffer_.Resize(DRAIN_SAMPLES);

    SubscribeToEvents();

    // Network — every instance is a peer. Mesh, not hub.
    auto* network = GetSubsystem<Network>();

    if (network->StartServer(VOICE_PORT))
    {
        VariantMap beacon;
        beacon["Name"] = "VoiceChat";
        network->SetDiscoveryBeacon(beacon);
        URHO3D_LOGINFO("[VOICE] Listening on port " + String(VOICE_PORT));
    }
    else
    {
        network->Connect("127.0.0.1", VOICE_PORT, nullptr);
        URHO3D_LOGINFO("[VOICE] Port taken, connecting locally");
    }

    network->DiscoverHosts(VOICE_PORT);
    statusText_->SetText("Searching for peers...");
}

void VoiceChat::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(VoiceChat, HandleUpdate));
    SubscribeToEvent(E_NETWORKHOSTDISCOVERED, URHO3D_HANDLER(VoiceChat, HandleHostDiscovered));
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(VoiceChat, HandleNetworkMessage));
    SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(VoiceChat, HandleConnectionStatus));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(VoiceChat, HandleConnectionStatus));
    SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(VoiceChat, HandleConnectionStatus));
    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(VoiceChat, HandleClientConnected));
}

void VoiceChat::HandleHostDiscovered(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkHostDiscovered;
    String addr = eventData[P_ADDRESS].GetString();

    // Already have a server connection? Skip.
    if (GetSubsystem<Network>()->GetServerConnection())
        return;

    URHO3D_LOGINFO("[VOICE] Discovered peer at " + addr);
    GetSubsystem<Network>()->Connect(addr, VOICE_PORT, nullptr);
}

void VoiceChat::HandleClientConnected(StringHash /*eventType*/, VariantMap& /*eventData*/)
{
    hasPeers_ = true;
    statusText_->SetText("Peer joined — hold TALK or 1+2+3");
    statusText_->SetColor(Color::GREEN);
    URHO3D_LOGINFO("[VOICE] Peer connected");
}

void VoiceChat::HandleConnectionStatus(StringHash eventType, VariantMap& /*eventData*/)
{
    if (eventType == E_SERVERCONNECTED)
    {
        hasPeers_ = true;
        statusText_->SetText("Connected — hold TALK or 1+2+3");
        statusText_->SetColor(Color::GREEN);
        URHO3D_LOGINFO("[VOICE] Connected to peer");
    }
    else if (eventType == E_SERVERDISCONNECTED)
    {
        auto* network = GetSubsystem<Network>();
        hasPeers_ = !network->GetClientConnections().Empty();
        transmitting_ = false;
        statusText_->SetText(hasPeers_ ? "Peer left" : "No peers");
        statusText_->SetColor(hasPeers_ ? Color::GREEN : Color::YELLOW);
        URHO3D_LOGINFO("[VOICE] Peer disconnected");
    }
    else if (eventType == E_CONNECTFAILED)
    {
        retryTimer_ = 0.0f;
    }
}

void VoiceChat::HandleNetworkMessage(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkMessage;

    int msgID = eventData[P_MESSAGEID].GetI32();
    if (msgID != MSG_VOICE_DATA)
        return;

    const Vector<byte>& data = eventData[P_DATA].GetBuffer();
    MemoryBuffer msg(data);

    unsigned sampleCount = msg.ReadU32();
    if (sampleCount == 0 || sampleCount > 65536)
        return;

    unsigned byteCount = sampleCount * sizeof(i16);
    if (msg.GetSize() - msg.GetPosition() < byteCount)
        return;

    // Play received audio
    const void* audioData = msg.GetData() + msg.GetPosition();
    playStream_->AddData(const_cast<void*>(audioData), byteCount);

    // Relay to all other clients (mesh relay)
    auto* sender = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    auto clients = GetSubsystem<Network>()->GetClientConnections();

    if (clients.Size() > 1)
    {
        VectorBuffer relay;
        relay.WriteU32(sampleCount);
        relay.Write(audioData, byteCount);

        for (unsigned i = 0; i < clients.Size(); ++i)
        {
            if (clients[i] != sender)
                clients[i]->SendMessage(MSG_VOICE_DATA, false, false, relay);
        }
    }
}

/// Send voice data to all peers — both server connections and client connections.
static void SendToAllPeers(Network* network, int msgID, bool reliable, bool inOrder, const VectorBuffer& msg)
{
    // Send to server connection (if we connected to someone)
    Connection* serverConn = network->GetServerConnection();
    if (serverConn)
        serverConn->SendMessage(msgID, reliable, inOrder, msg);

    // Broadcast to all clients (if anyone connected to us)
    if (network->IsServerRunning())
        network->BroadcastMessage(msgID, reliable, inOrder, msg);
}

void VoiceChat::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Periodic discovery
    retryTimer_ += timeStep;
    if (retryTimer_ >= 5.0f)
    {
        retryTimer_ = 0.0f;
        GetSubsystem<Network>()->DiscoverHosts(VOICE_PORT);
    }

    // Talk: button held or 1+2+3
    auto* input = GetSubsystem<Input>();
    bool talkHeld = talkButton_->IsPressed()
        || (input->GetKeyDown(KEY_1) && input->GetKeyDown(KEY_2) && input->GetKeyDown(KEY_3));

    if (talkHeld && !transmitting_ && hasPeers_)
    {
        GetSubsystem<Audio>()->StartCapture(String::EMPTY, CAPTURE_RATE);
        transmitting_ = true;
        statusText_->SetText("TRANSMITTING");
        statusText_->SetColor(Color::RED);
    }
    else if (!talkHeld && transmitting_)
    {
        auto* audio = GetSubsystem<Audio>();
        unsigned drained = audio->DrainCaptureSamples(drainBuffer_.Buffer(), DRAIN_SAMPLES);
        if (drained > 0)
        {
            VectorBuffer msg;
            msg.WriteU32(drained);
            msg.Write(drainBuffer_.Buffer(), drained * sizeof(i16));
            SendToAllPeers(GetSubsystem<Network>(), MSG_VOICE_DATA, false, false, msg);
        }

        audio->PauseCapture();
        audio->ClearCapture();
        transmitting_ = false;
        statusText_->SetText("Hold TALK or 1+2+3");
        statusText_->SetColor(Color::GREEN);
    }

    // Continuous drain while transmitting
    if (transmitting_)
    {
        auto* audio = GetSubsystem<Audio>();
        unsigned drained = audio->DrainCaptureSamples(drainBuffer_.Buffer(), DRAIN_SAMPLES);

        if (drained > 0)
        {
            VectorBuffer msg;
            msg.WriteU32(drained);
            msg.Write(drainBuffer_.Buffer(), drained * sizeof(i16));
            SendToAllPeers(GetSubsystem<Network>(), MSG_VOICE_DATA, false, false, msg);
        }
    }

    if (profilerUI_)
        profilerUI_->Update(timeStep);
}
