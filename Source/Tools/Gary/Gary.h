// Gary — LAN File Transfer (Warpinator Replacement)
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT:  Cross-platform LAN file transfer tool built on Urho3D 2.0.1.
//        Drag files from your desktop onto a peer panel to send.
//        Also watches ~/Documents/Gary/outbox/ via FileWatcher (inotify/FSEvents).
//        Incoming files land in ~/Documents/Gary/inbox/.
//
// HOW:   Every Gary instance starts a server and discovers other Garys on the LAN
//        via SLikeNet broadcast. One connection per peer pair — no duplicates.
//        Files are chunked at 1024 bytes, sent reliably ordered over RUDP.
//        Directory structure is preserved via relative paths in the offer message.
//
// NET:   Mesh topology — every Gary is equal. No hub, no relay.
//        Discovery: Network::DiscoverHosts() broadcasts on UDP.
//        Transport: SLikeNet RUDP (reliable ordered for file data).
//        Encryption: Automatic X25519 + ChaCha20-Poly1305 key exchange on connect.
//        PAKE auth: Optional — place shared secret at /etc/urho3d/pake.key (>= 32 bytes).
//
// PROTO: MSG_USER+200 range:
//        MSG_FB_FILE_OFFER (200) — filename, relative path, size
//        MSG_FB_FILE_DATA  (201) — file_id, offset, chunk data (1024 bytes)
//        MSG_FB_FILE_COMPLETE (202) — file_id, expected size (receiver verifies)
//
// UI:    Each connected peer appears as a panel with address and status.
//        Panels highlight on hover. Drop target detection via cursor position.
//        Status bar shows connection state. Transfer text shows progress %.
//
// DEPS:  Network (SLikeNet RUDP), FileSystem, FileWatcher, File, UI.
//        No external dependencies beyond SDL2 and Vulkan/OpenGL drivers.

#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Container/HashSet.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileWatcher.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UIElement.h>

using namespace Urho3D;

/// Active outbound transfer — targeted to a specific peer.
struct OutTransfer
{
    String relativePath;
    String fullPath;
    unsigned fileSize;
    unsigned offset;
    unsigned fileId;
    WeakPtr<Connection> target;  ///< Who gets this file.
};

/// Active inbound transfer.
struct InTransfer
{
    String relativePath;
    unsigned fileSize;
    unsigned received;
    SharedPtr<File> file;
    unsigned fileId;
};

/// UI panel for one connected peer.
struct PeerPanel
{
    WeakPtr<Connection> connection;
    SharedPtr<UIElement> panel;
    SharedPtr<Text> nameText;
    SharedPtr<Text> statusText;
    String address;
};

class Gary : public Application
{
    URHO3D_OBJECT(Gary, Application);

public:
    explicit Gary(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandleHostDiscovered(StringHash eventType, VariantMap& eventData);
    void HandleConnectionStatus(StringHash eventType, VariantMap& eventData);
    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);
    void HandleDropFile(StringHash eventType, VariantMap& eventData);

    /// Add a peer panel to the UI.
    void AddPeerPanel(Connection* conn, const String& address);
    /// Remove a peer panel.
    void RemovePeerPanel(Connection* conn);
    /// Find which peer panel the cursor is over.
    Connection* FindPeerUnderCursor();

    /// Queue file to a specific peer.
    void QueueFileForPeer(const String& path, Connection* target);
    /// Scan outbox — sends to all peers.
    void ScanOutbox();
    void ProcessOutbound(float timeStep);
    void HandleFileOffer(Connection* sender, MemoryBuffer& msg);
    void HandleFileData(Connection* sender, MemoryBuffer& msg);
    void HandleFileComplete(Connection* sender, MemoryBuffer& msg);
    void SendFileOffer(const OutTransfer& transfer);
    void SendNextChunk(OutTransfer& transfer);

    String outboxPath_;
    String inboxPath_;
    String sentPath_;

    SharedPtr<FileWatcher> outboxWatcher_;
    HashSet<String> knownOutboxFiles_;

    Vector<OutTransfer> outTransfers_;
    HashMap<unsigned, InTransfer> inTransfers_;
    unsigned nextFileId_{1};

    /// Peer panels in the UI.
    Vector<PeerPanel> peerPanels_;
    /// Container for peer panels.
    SharedPtr<UIElement> peerContainer_;

    unsigned peerCount_{};
    float retryTimer_{};

    SharedPtr<Text> statusText_;
    SharedPtr<Text> transferText_;
    SharedPtr<Text> hintText_;
};
