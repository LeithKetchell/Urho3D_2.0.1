// Karen — standalone telemetry collector + display hub.
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// Karen is the single, standalone app that every KarenClient dials into. Each Urho app embeds one
// KarenClient that client-dials KAREN_PORT and streams telemetry OUT; Karen binds that port, collects
// the streams, and (next phase) displays them. Singleton by role: she binds the fixed port, so a
// second Karen fails the bind and exits rather than compete. One hub, many clients.
//
// Scaffold status: brings up the collector server, accepts KarenClient connections, records emitter
// app-names from HELLO, and logs the telemetry it receives. Full per-message decode + the ProfilerUI
// display lift from WorkboardManager::HandleKarenMessage next.

#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Container/Ptr.h>
#include <Urho3D/Container/Str.h>

namespace Urho3D { class Connection; class Text; }

using namespace Urho3D;

/// The one Karen. Collector server now; display hub next.
class Karen : public Application
{
    URHO3D_OBJECT(Karen, Application);

public:
    explicit Karen(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    /// Bind KAREN_PORT and become the collector. Returns false if the port is already held (another
    /// Karen is running) — the caller then refuses to start a second.
    bool StartCollector();
    /// Minimal status line (listening port, emitter/message counts). Full display lands next.
    void CreateStatusUi();

    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    /// Receive one telemetry message. Scaffold: record HELLO app-names, count + refresh status.
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);

    /// Connected emitters, keyed by connection, valued by the app name sent in HELLO.
    HashMap<Connection*, String> emitters_;
    /// Status text (owned by the UI tree; weak so teardown order is safe).
    WeakPtr<Text> statusText_;
    /// Total Karen-protocol messages seen since start.
    unsigned messagesSeen_{};
};
