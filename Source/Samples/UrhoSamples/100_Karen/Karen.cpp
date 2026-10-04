// Karen — RUDP telemetry viewer.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/KarenClient.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "Karen.h"

// Karen message IDs — must match KarenClient
static const int MSG_KAREN_WELCOME    = MSG_USER + 300;
static const int MSG_KAREN_FRAME      = MSG_USER + 301;
static const int MSG_KAREN_ZONE_BEGIN = MSG_USER + 302;
static const int MSG_KAREN_ZONE_END   = MSG_USER + 303;
static const int MSG_KAREN_PLOT       = MSG_USER + 304;
static const int MSG_KAREN_MESSAGE    = MSG_USER + 305;
// Sent to the emitter on connect to identify as a real Karen consumer — emitters only stream to subscribers.
static const int MSG_KAREN_SUBSCRIBE  = MSG_USER + 306;

static const unsigned MAX_MESSAGES = 20;
static const unsigned MAX_FRAME_HISTORY = 120;
static const unsigned MAX_PLOT_VALUES = 120;

URHO3D_DEFINE_APPLICATION_MAIN(Karen)

Karen::Karen(Context* context) : Application(context) {}

void Karen::Setup()
{
    engineParameters_[EP_WINDOW_TITLE] = "Karen";
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_WINDOW_WIDTH] = 800;
    engineParameters_[EP_WINDOW_HEIGHT] = 600;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
    engineParameters_[EP_LOG_NAME] = "Karen.log";
    engineParameters_[EP_SOUND] = false;

    // P3 test-emitter mode: `Karen -emit [-collector <host>] [-port <n>]`. Runs HEADLESS and,
    // instead of viewing, dials the Manager collector to prove the client-dial pipe end-to-end.
    // Default port 31337 (the collector rides Manager's workboard server) — NOT the 25771 default.
    const Vector<String>& args = GetArguments();
    if (args.Contains("-emit") || args.Contains("--emit"))
    {
        emitMode_ = true;
        engineParameters_[EP_HEADLESS] = true;
        engineParameters_[EP_LOG_NAME] = "KarenEmitter.log";
        for (unsigned i = 0; i + 1 < args.Size(); ++i)
        {
            if (args[i] == "-collector")
                collectorAddress_ = args[i + 1];
            else if (args[i] == "-port")
                collectorPort_ = (unsigned short)ToI32(args[i + 1]);
        }
    }
}

void Karen::Start()
{
    // ── P3 test-emitter path: register KarenClient, DIAL the collector, stream telemetry ──
    if (emitMode_)
    {
        context_->RegisterSubsystem(new KarenClient(context_));
        auto* karen = GetSubsystem<KarenClient>();
        if (!karen->ConnectToCollector("TestEmitter", collectorAddress_, collectorPort_))
            URHO3D_LOGERROR("KarenEmitter: ConnectToCollector failed for " +
                            collectorAddress_ + ":" + String(collectorPort_));
        else
            URHO3D_LOGINFO("KarenEmitter: dialing collector " + collectorAddress_ + ":" +
                           String(collectorPort_) + " — streaming plot 'wave' + heartbeat once connected");
        SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(Karen, HandleUpdate));
        return;   // headless emitter — skip all viewer UI
    }

    GetSubsystem<Input>()->SetMouseVisible(true);
    GetSubsystem<Input>()->SetMouseMode(MM_FREE);

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    auto* root = GetSubsystem<UI>()->GetRoot();
    root->SetDefaultStyle(uiStyle);

    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    int y = 10;

    statusText_ = root->CreateChild<Text>();
    statusText_->SetFont(font, 14);
    statusText_->SetColor(Color::YELLOW);
    statusText_->SetPosition(10, y);
    statusText_->SetText("Karen — searching for emitters...");
    y += 22;

    frameText_ = root->CreateChild<Text>();
    frameText_->SetFont(font, 12);
    frameText_->SetColor(Color::GREEN);
    frameText_->SetPosition(10, y);
    y += 20;

    zoneText_ = root->CreateChild<Text>();
    zoneText_->SetFont(font, 11);
    zoneText_->SetColor(Color(0.8f, 0.9f, 1.0f));
    zoneText_->SetPosition(10, y);
    y += 120;

    plotText_ = root->CreateChild<Text>();
    plotText_->SetFont(font, 11);
    plotText_->SetColor(Color::CYAN);
    plotText_->SetPosition(10, y);
    y += 80;

    messageText_ = root->CreateChild<Text>();
    messageText_->SetFont(font, 10);
    messageText_->SetColor(Color(0.6f, 0.6f, 0.6f));
    messageText_->SetPosition(10, y);

    GetSubsystem<Renderer>()->GetDefaultZone()->SetFogColor(Color(0.08f, 0.08f, 0.1f));

    // Subscribe to events
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(Karen, HandleUpdate));
    SubscribeToEvent(E_NETWORKHOSTDISCOVERED, URHO3D_HANDLER(Karen, HandleHostDiscovered));
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(Karen, HandleNetworkMessage));
    SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(Karen, HandleConnectionStatus));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(Karen, HandleConnectionStatus));
    SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(Karen, HandleConnectionStatus));

    // Start discovering
    GetSubsystem<Network>()->DiscoverHosts(KAREN_PORT);
}

void Karen::Stop()
{
    auto* network = GetSubsystem<Network>();
    if (network->GetServerConnection())
        network->Disconnect();
}

void Karen::HandleHostDiscovered(StringHash /*eventType*/, VariantMap& eventData)
{
    if (connected_)
        return;

    using namespace NetworkHostDiscovered;
    String addr = eventData[P_ADDRESS].GetString();
    VariantMap beacon = eventData[P_BEACON].GetVariantMap();

    // Only connect to Karen telemetry emitters
    if (beacon["Type"].GetString() != "KarenTelemetry")
        return;

    String name = beacon["Name"].GetString();
    URHO3D_LOGINFO("Karen: Found emitter '" + name + "' at " + addr);
    statusText_->SetText("Connecting to " + name + " at " + addr + "...");

    GetSubsystem<Network>()->Connect(addr, KAREN_PORT, nullptr);
}

void Karen::HandleConnectionStatus(StringHash eventType, VariantMap& /*eventData*/)
{
    if (eventType == E_SERVERCONNECTED)
    {
        connected_ = true;
        // Identify as a Karen consumer so the emitter starts streaming (it sends to subscribers only, never a
        // blind broadcast). Without this the viewer would attach but receive nothing.
        if (auto* conn = GetSubsystem<Network>()->GetServerConnection())
        {
            VectorBuffer sub;
            conn->SendMessage(MSG_KAREN_SUBSCRIBE, true, true, sub);
        }
        statusText_->SetText("Karen — connected to " + (emitterName_.Empty() ? "emitter" : emitterName_));
        statusText_->SetColor(Color::GREEN);
        URHO3D_LOGINFO("Karen: Connected");
    }
    else if (eventType == E_SERVERDISCONNECTED)
    {
        connected_ = false;
        statusText_->SetText("Karen — disconnected, searching...");
        statusText_->SetColor(Color::RED);
        retryTimer_ = 0.0f;
        URHO3D_LOGINFO("Karen: Disconnected");
    }
    else if (eventType == E_CONNECTFAILED)
    {
        connected_ = false;
        statusText_->SetText("Karen — connection failed, retrying...");
        statusText_->SetColor(Color::YELLOW);
        retryTimer_ = 0.0f;
    }
}

void Karen::HandleNetworkMessage(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkMessage;

    int msgID = eventData[P_MESSAGEID].GetI32();
    const Vector<byte>& data = eventData[P_DATA].GetBuffer();
    MemoryBuffer msg(data);

    switch (msgID)
    {
    case MSG_KAREN_WELCOME:
    {
        emitterName_ = msg.ReadString();
        unsigned long long startTime = msg.ReadU64();
        statusText_->SetText("Karen — watching '" + emitterName_ + "'");
        statusText_->SetColor(Color::GREEN);
        URHO3D_LOGINFO("Karen: Welcome from '" + emitterName_ + "'");
        break;
    }

    case MSG_KAREN_FRAME:
    {
        unsigned frameNum = msg.ReadU32();
        unsigned long long timeUs = msg.ReadU64();

        if (lastFrameTimeUs_ > 0)
        {
            float frameMs = (float)(timeUs - lastFrameTimeUs_) / 1000.0f;
            frameTimeHistory_.Push(frameMs);
            if (frameTimeHistory_.Size() > MAX_FRAME_HISTORY)
                frameTimeHistory_.Erase(0);
        }

        lastFrameNumber_ = frameNum;
        lastFrameTimeUs_ = timeUs;

        // Clear zones for new frame
        zones_.Clear();
        break;
    }

    case MSG_KAREN_ZONE_BEGIN:
    {
        KarenZoneData z;
        z.name = msg.ReadString();
        z.startUs = msg.ReadU64();
        z.depth = msg.ReadU8();
        z.endUs = 0;
        openZones_.Push(z);
        break;
    }

    case MSG_KAREN_ZONE_END:
    {
        unsigned long long endUs = msg.ReadU64();
        if (!openZones_.Empty())
        {
            KarenZoneData z = openZones_.Back();
            openZones_.Pop();
            z.endUs = endUs;
            zones_.Push(z);
        }
        break;
    }

    case MSG_KAREN_PLOT:
    {
        String name = msg.ReadString();
        float value = msg.ReadFloat();
        // timestamp not used for display currently

        KarenPlotSeries& series = plots_[name];
        series.values.Push(value);
        if (series.values.Size() > MAX_PLOT_VALUES)
            series.values.Erase(0);
        if (value < series.minVal) series.minVal = value;
        if (value > series.maxVal) series.maxVal = value;
        break;
    }

    case MSG_KAREN_MESSAGE:
    {
        String text = msg.ReadString();
        unsigned color = msg.ReadU32();

        messages_.Push(text);
        if (messages_.Size() > MAX_MESSAGES)
            messages_.Erase(0);
        break;
    }

    default:
        break;
    }
}

void Karen::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // P3 test-emitter: stream telemetry to the collector. MarkFrame/Plot/Message auto-gate on
    // GetViewerCount()>0, which counts the connected collector — so nothing sends until the dial
    // handshake completes, then it flows and the collector logs it per-emitter (labelled TestEmitter).
    if (emitMode_)
    {
        auto* karen = GetSubsystem<KarenClient>();
        if (!karen)
            return;
        emitElapsed_ += timeStep;
        emitPhase_ += timeStep;
        if (emitPhase_ > 2.0f)
            emitPhase_ -= 2.0f;
        const float wave = (emitPhase_ < 1.0f ? emitPhase_ : 2.0f - emitPhase_) * 100.0f;  // triangle 0..100
        karen->MarkFrame();
        karen->Plot("wave", wave);
        if (emitElapsed_ - lastEmitMsg_ >= 1.0f)
        {
            lastEmitMsg_ = emitElapsed_;
            ++emitSeq_;
            karen->Message("TestEmitter heartbeat #" + String(emitSeq_) +
                           (karen->GetViewerCount() > 0 ? " (collector live)" : " (waiting for collector)"));
        }
        return;
    }

    // Retry discovery if not connected
    if (!connected_)
    {
        retryTimer_ += timeStep;
        if (retryTimer_ >= 2.0f)
        {
            retryTimer_ = 0.0f;
            GetSubsystem<Network>()->DiscoverHosts(KAREN_PORT);
        }
    }

    UpdateDisplay();
}

void Karen::UpdateDisplay()
{
    // Frame info
    if (!frameTimeHistory_.Empty())
    {
        float lastMs = frameTimeHistory_.Back();
        float avgMs = 0.0f;
        for (unsigned i = 0; i < frameTimeHistory_.Size(); ++i)
            avgMs += frameTimeHistory_[i];
        avgMs /= (float)frameTimeHistory_.Size();
        float fps = avgMs > 0.0f ? 1000.0f / avgMs : 0.0f;

        // Simple text bar chart of recent frames
        String bars;
        unsigned start = frameTimeHistory_.Size() > 60 ? frameTimeHistory_.Size() - 60 : 0;
        for (unsigned i = start; i < frameTimeHistory_.Size(); ++i)
        {
            float ms = frameTimeHistory_[i];
            if (ms < 16.67f) bars += "|";
            else if (ms < 33.33f) bars += "!";
            else bars += "#";
        }

        frameText_->SetText("Frame " + String(lastFrameNumber_) +
                            "  " + String(lastMs, 1) + "ms" +
                            "  avg:" + String(avgMs, 1) + "ms" +
                            "  ~" + String((int)fps) + "fps" +
                            "\n" + bars);
    }

    // Zones
    if (!zones_.Empty())
    {
        String zoneStr;
        for (unsigned i = 0; i < zones_.Size(); ++i)
        {
            const KarenZoneData& z = zones_[i];
            float durationUs = (float)(z.endUs - z.startUs);
            String indent;
            for (unsigned d = 0; d < z.depth; ++d)
                indent += "  ";
            zoneStr += indent + z.name + " " + String(durationUs / 1000.0f, 2) + "ms\n";
        }
        zoneText_->SetText(zoneStr);
    }

    // Plots
    if (!plots_.Empty())
    {
        String plotStr;
        for (auto it = plots_.Begin(); it != plots_.End(); ++it)
        {
            const KarenPlotSeries& s = it->second_;
            if (!s.values.Empty())
            {
                float last = s.values.Back();
                plotStr += it->first_ + ": " + String(last, 1);

                // Mini sparkline
                unsigned start = s.values.Size() > 30 ? s.values.Size() - 30 : 0;
                float range = s.maxVal - s.minVal;
                if (range > 0.0f)
                {
                    plotStr += " [";
                    for (unsigned i = start; i < s.values.Size(); ++i)
                    {
                        float norm = (s.values[i] - s.minVal) / range;
                        if (norm < 0.25f) plotStr += "_";
                        else if (norm < 0.5f) plotStr += "-";
                        else if (norm < 0.75f) plotStr += "=";
                        else plotStr += "#";
                    }
                    plotStr += "]";
                }
                plotStr += "\n";
            }
        }
        plotText_->SetText(plotStr);
    }

    // Messages
    if (!messages_.Empty())
    {
        String msgStr;
        for (unsigned i = 0; i < messages_.Size(); ++i)
            msgStr += messages_[i] + "\n";
        messageText_->SetText(msgStr);
    }
}
