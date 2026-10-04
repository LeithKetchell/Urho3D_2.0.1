// Karen — RUDP Telemetry Viewer (Tracy replacement)
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT:  Real-time telemetry viewer. Connects to any Urho3D app running
//        KarenClient and displays live zones, frame times, plots, and messages.
//        Tracy's instrumentation model on Urho3D's RUDP — no TCP.
//        Karen can't keep a secret, but she can collect them.
//
// HOW:   Discovers KarenClient emitters on the LAN via Network::DiscoverHosts().
//        Connects as a client. Receives telemetry messages (fire-and-forget UDP).
//        Multiple viewers can connect to one emitter simultaneously (mesh).
//
// DATA:  Zones — scoped timing with nesting depth (flame chart).
//        Frames — boundary markers with timestamps (frame time graph).
//        Plots — named scalar series (sparkline display).
//        Messages — timestamped log strings (scrolling log).
//
// PROTO: MSG_USER+300 range (matches KarenClient.h):
//        WELCOME (300) — app name, start time
//        FRAME (301) — frame number, timestamp
//        ZONE_BEGIN (302) — name, timestamp, depth
//        ZONE_END (303) — timestamp
//        PLOT (304) — name, float value, timestamp
//        MESSAGE (305) — text, color, timestamp
//        All unreliable unordered — telemetry tolerates loss.
//
// UI:    Text-based display: frame time bar chart (| < 16ms, ! < 33ms, # > 33ms),
//        zone tree with indented nesting and ms durations, plot sparklines,
//        scrolling message log. Urho3D UI, no external renderer.
//
// DEPS:  Network (SLikeNet RUDP), UI, KarenClient (protocol constants).

#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Container/HashMap.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UIElement.h>

using namespace Urho3D;

/// One received zone (begin + end paired).
struct KarenZoneData
{
    String name;
    unsigned long long startUs;
    unsigned long long endUs;
    unsigned char depth;
};

/// A received plot series.
struct KarenPlotSeries
{
    Vector<float> values;      ///< Rolling buffer of recent values.
    float minVal{0.0f};
    float maxVal{100.0f};
};

class Karen : public Application
{
    URHO3D_OBJECT(Karen, Application);

public:
    explicit Karen(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleHostDiscovered(StringHash eventType, VariantMap& eventData);
    void HandleConnectionStatus(StringHash eventType, VariantMap& eventData);
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);

    void UpdateDisplay();

    /// UI elements.
    SharedPtr<Text> statusText_;
    SharedPtr<Text> frameText_;
    SharedPtr<Text> zoneText_;
    SharedPtr<Text> plotText_;
    SharedPtr<Text> messageText_;

    /// Last received frame data.
    unsigned lastFrameNumber_{};
    unsigned long long lastFrameTimeUs_{};

    /// Current frame's zones.
    Vector<KarenZoneData> zones_;
    /// Open zone stack (for pairing begin/end).
    Vector<KarenZoneData> openZones_;

    /// Plot series.
    HashMap<String, KarenPlotSeries> plots_;

    /// Recent messages.
    Vector<String> messages_;

    /// Connection state.
    bool connected_{};
    float retryTimer_{};
    String emitterName_;

    /// Frame time history for bar display.
    Vector<float> frameTimeHistory_;

    // ── P3 test-emitter mode (run with -emit) ──
    // Instead of viewing, DIAL the Manager collector and stream telemetry, proving the
    // client-dial inversion end-to-end. Must dial 31337 (the collector rides Manager's
    // server), NOT the 25771 ConnectToCollector default. Headless — no window needed.
    bool emitMode_{false};
    String collectorAddress_{"127.0.0.1"};
    unsigned short collectorPort_{31337};
    float emitElapsed_{0.0f};
    float emitPhase_{0.0f};
    float lastEmitMsg_{0.0f};
    unsigned emitSeq_{0};
};
