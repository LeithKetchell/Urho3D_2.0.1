// WorkboardProtocol.h — Shared event types and data structures for
// WorkboardManager ↔ WorkboardClient reliable UDP protocol.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Network/Connection.h>

using namespace Urho3D;

// ── Server → Client remote events ──

/// Post-auth welcome. Payload: serverName, version, clientCount.
URHO3D_EVENT(E_WB_WELCOME, WbWelcome)
{
    URHO3D_PARAM(P_SERVERNAME, ServerName);   // String
    URHO3D_PARAM(P_VERSION, Version);         // String
    URHO3D_PARAM(P_CLIENTCOUNT, ClientCount); // int
}

/// Full workboard markdown pushed on connect and on change.
URHO3D_EVENT(E_WB_WORKBOARD_FULL, WbWorkboardFull)
{
    URHO3D_PARAM(P_MARKDOWN, Markdown);       // String
}

/// Plan file listing.
URHO3D_EVENT(E_WB_PLAN_LIST, WbPlanList)
{
    URHO3D_PARAM(P_FILENAMES, Filenames);     // String (newline-separated)
}

/// Single plan file content (response to client request).
URHO3D_EVENT(E_WB_PLAN_CONTENT, WbPlanContent)
{
    URHO3D_PARAM(P_FILENAME, Filename);       // String
    URHO3D_PARAM(P_CONTENT, Content);         // String
}

/// Mutation acknowledgement.
URHO3D_EVENT(E_WB_MUTATION_ACK, WbMutationAck)
{
    URHO3D_PARAM(P_SUCCESS, Success);         // bool
    URHO3D_PARAM(P_REASON, Reason);           // String
}

/// Connected remote client list.
URHO3D_EVENT(E_WB_CLIENT_LIST, WbClientList)
{
    URHO3D_PARAM(P_CLIENTS, Clients);         // String (newline-separated "name:role")
}

/// Server graceful-shutdown notice (Phase 2c). Sent before the server tears
/// down connections so clients can distinguish a server-initiated disconnect
/// from a network failure or a mutation error. Replaces the earlier shoehorn
/// of MUTATION_ACK with Reason="Server shutting down".
URHO3D_EVENT(E_WB_SERVER_SHUTDOWN, WbServerShutdown)
{
    URHO3D_PARAM(P_REASON, Reason);           // String — human-readable explanation
}

/// Karen telemetry relay (Manager → Client). A thin client can't attach to a Karen
/// emitter directly — its single serverConnection_ is spent on the workboard link —
/// so Manager (which IS attached) forwards each plot update over the existing link.
/// Hub topology: no competing emitter connection, no severed workboard.
URHO3D_EVENT(E_WB_KAREN_TELEMETRY, WbKarenTelemetry)
{
    URHO3D_PARAM(P_EMITTER, Emitter);         // String — emitter name (e.g. "Yuki")
    URHO3D_PARAM(P_PLOTNAME, PlotName);       // String — metric name
    URHO3D_PARAM(P_PLOTVALUE, PlotValue);     // float  — latest value
}

// ── Client → Server remote events ──

/// Client requests a specific plan file.
URHO3D_EVENT(E_WB_REQUEST_PLAN, WbRequestPlan)
{
    URHO3D_PARAM(P_FILENAME, Filename);       // String
}

/// Client sends a workboard mutation (wb-* command).
URHO3D_EVENT(E_WB_MUTATION, WbMutation)
{
    URHO3D_PARAM(P_COMMAND, Command);         // String — e.g. "add", "move", "done"
    URHO3D_PARAM(P_ARGS, Args);              // String — command arguments
}

/// Client sets its display identity.
URHO3D_EVENT(E_WB_SET_IDENTITY, WbSetIdentity)
{
    URHO3D_PARAM(P_NAME, Name);               // String
    URHO3D_PARAM(P_ROLE, Role);               // String
}

/// Client reports local instance liveness to Manager (every 5s).
URHO3D_EVENT(E_WB_INSTANCE_STATUS, WbInstanceStatus)
{
    URHO3D_PARAM(P_YUKI_ALIVE, YukiAlive);       // bool
    URHO3D_PARAM(P_CODER_COUNT, CoderCount);     // int
    URHO3D_PARAM(P_CODER_ROLES, CoderRoles);     // String — comma-separated role names
}

// ── Client info tracked by server ──

struct WbClientInfo
{
    Connection* connection_{};
    String name_;
    String role_;
    bool authenticated_{false};
    // Remote instance liveness (Phase 1-2)
    bool remoteYukiAlive_{false};
    int remoteCoderCount_{0};
    String remoteCoderRoles_;
};

// ── Shared workboard data structures ──

/// A parsed table row from the workboard markdown.
struct WorkboardRow
{
    Vector<String> cells;
};

/// A parsed table section (Ready, In Progress, Done, etc.)
struct WorkboardSection
{
    String title;
    Vector<String> headers;
    Vector<WorkboardRow> rows;
};
