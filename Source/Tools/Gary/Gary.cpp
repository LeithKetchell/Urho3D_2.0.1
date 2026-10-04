// Gary — LAN file transfer (Warpinator replacement)
// Copyright (c) 2026 Urho3D project. License: MIT.

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/FileWatcher.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "Gary.h"

// Gary can carry.

#ifdef SendMessage
#undef SendMessage
#endif

static const int MSG_FB_FILE_OFFER    = MSG_USER + 200;
static const int MSG_FB_FILE_DATA     = MSG_USER + 201;
static const int MSG_FB_FILE_COMPLETE = MSG_USER + 202;

static const unsigned short GARY_PORT = 4567;
static const unsigned CHUNK_SIZE = 1024;

URHO3D_DEFINE_APPLICATION_MAIN(Gary)

Gary::Gary(Context* context) : Application(context) {}

void Gary::Setup()
{
    engineParameters_[EP_WINDOW_TITLE] = "Gary";
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_WINDOW_WIDTH] = 800;
    engineParameters_[EP_WINDOW_HEIGHT] = 600;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;
    engineParameters_[EP_LOG_NAME] = "Gary.log";
    engineParameters_[EP_SOUND] = false;
}

void Gary::Start()
{
    GetSubsystem<Input>()->SetMouseVisible(true);
    GetSubsystem<Input>()->SetMouseGrabbed(false);
    GetSubsystem<Input>()->SetMouseMode(MM_FREE);

    // UI
    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    auto* root = GetSubsystem<UI>()->GetRoot();
    root->SetDefaultStyle(uiStyle);

    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    statusText_ = root->CreateChild<Text>();
    statusText_->SetFont(font, 14);
    statusText_->SetColor(Color::YELLOW);
    statusText_->SetPosition(10, 10);
    statusText_->SetText("Gary starting...");

    transferText_ = root->CreateChild<Text>();
    transferText_->SetFont(font, 11);
    transferText_->SetColor(Color(0.7f, 0.7f, 0.7f));
    transferText_->SetPosition(10, 30);

    hintText_ = root->CreateChild<Text>();
    hintText_->SetFont(font, 12);
    hintText_->SetColor(Color(0.5f, 0.5f, 0.5f));
    hintText_->SetPosition(10, 270);
    hintText_->SetText("Drop files onto a peer to send");

    // Peer panel container
    peerContainer_ = root->CreateChild<UIElement>();
    peerContainer_->SetPosition(10, 55);
    peerContainer_->SetFixedWidth(380);
    peerContainer_->SetLayoutMode(LM_VERTICAL);
    peerContainer_->SetLayoutSpacing(5);

    GetSubsystem<Renderer>()->GetDefaultZone()->SetFogColor(Color(0.12f, 0.12f, 0.14f));

    // Directories
    auto* fs = GetSubsystem<FileSystem>();
    String userDir = fs->GetUserDocumentsDir();
    String garyDir = userDir + "Gary/";
    outboxPath_ = garyDir + "outbox/";
    inboxPath_ = garyDir + "inbox/";
    sentPath_ = garyDir + "sent/";

    fs->CreateDir(garyDir);
    fs->CreateDir(outboxPath_);
    fs->CreateDir(inboxPath_);
    fs->CreateDir(sentPath_);

    URHO3D_LOGINFO("Gary: inbox=" + inboxPath_);

    // File watcher
    outboxWatcher_ = new FileWatcher(context_);
    outboxWatcher_->StartWatching(outboxPath_, true);
    outboxWatcher_->SetDelay(1.0f);

    SubscribeToEvents();

    // Network — every Gary is a beacon. Mesh, not hub.
    auto* network = GetSubsystem<Network>();

    network->StartServer(GARY_PORT);
    if (network->IsServerRunning())
    {
        VariantMap beacon;
        beacon["Name"] = "Gary";
        network->SetDiscoveryBeacon(beacon);
        URHO3D_LOGINFO("[GARY] Listening on port " + String(GARY_PORT));
    }

    // Discover other Garys on the LAN
    network->DiscoverHosts(GARY_PORT);
    statusText_->SetText("Searching for peers...");
    statusText_->SetColor(Color::YELLOW);
    URHO3D_LOGINFO("Gary running.");
}

void Gary::Stop()
{
    if (outboxWatcher_)
        outboxWatcher_->StopWatching();

    auto* network = GetSubsystem<Network>();
    if (network->IsServerRunning())
        network->StopServer();
    if (network->GetServerConnection())
        network->Disconnect();

    URHO3D_LOGINFO("Gary stopped.");
}

void Gary::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(Gary, HandleUpdate));
    SubscribeToEvent(E_NETWORKHOSTDISCOVERED, URHO3D_HANDLER(Gary, HandleHostDiscovered));
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(Gary, HandleNetworkMessage));
    SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(Gary, HandleConnectionStatus));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(Gary, HandleConnectionStatus));
    SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(Gary, HandleConnectionStatus));
    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(Gary, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(Gary, HandleClientDisconnected));
    SubscribeToEvent(E_DROPFILE, URHO3D_HANDLER(Gary, HandleDropFile));
}

// ─── Peer Panels ─────────────────────────────────────────────────────────────

void Gary::AddPeerPanel(Connection* conn, const String& address)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    PeerPanel pp;
    pp.connection = conn;
    pp.address = address;

    auto* bi = new BorderImage(context_);
    peerContainer_->AddChild(bi);
    bi->SetFixedHeight(80);
    bi->SetFixedWidth(370);
    auto* panelTex = cache->GetResource<Texture2D>("Textures/StoneDiffuse.dds");
    if (panelTex)
    {
        bi->SetTexture(panelTex);
        bi->SetImageRect(IntRect(0, 0, panelTex->GetWidth(), panelTex->GetHeight()));
    }
    bi->SetColor(Color(0.7f, 0.75f, 0.8f));
    pp.panel = bi;

    pp.nameText = pp.panel->CreateChild<Text>();
    pp.nameText->SetFont(font, 13);
    pp.nameText->SetColor(Color::WHITE);
    pp.nameText->SetPosition(8, 4);
    pp.nameText->SetText(address);

    pp.statusText = pp.panel->CreateChild<Text>();
    pp.statusText->SetFont(font, 10);
    pp.statusText->SetColor(Color::GREEN);
    pp.statusText->SetPosition(8, 22);
    pp.statusText->SetText("Connected");

    peerPanels_.Push(pp);
    URHO3D_LOGINFO("Gary: Added peer panel for " + address);
}

void Gary::RemovePeerPanel(Connection* conn)
{
    for (unsigned i = 0; i < peerPanels_.Size(); ++i)
    {
        if (peerPanels_[i].connection == conn)
        {
            peerContainer_->RemoveChild(peerPanels_[i].panel);
            peerPanels_.Erase(i);
            break;
        }
    }
}

Connection* Gary::FindPeerUnderCursor()
{
    auto* ui = GetSubsystem<UI>();
    IntVector2 pos = ui->GetCursorPosition();

    for (unsigned i = 0; i < peerPanels_.Size(); ++i)
    {
        PeerPanel& pp = peerPanels_[i];
        if (!pp.panel || !pp.connection)
            continue;

        IntVector2 panelPos = pp.panel->GetScreenPosition();
        IntVector2 panelSize = pp.panel->GetSize();

        if (pos.x_ >= panelPos.x_ && pos.x_ <= panelPos.x_ + panelSize.x_ &&
            pos.y_ >= panelPos.y_ && pos.y_ <= panelPos.y_ + panelSize.y_)
        {
            return pp.connection;
        }
    }
    return nullptr;
}

// ─── Network Events ──────────────────────────────────────────────────────────

void Gary::HandleHostDiscovered(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkHostDiscovered;
    String addr = eventData[P_ADDRESS].GetString();

    // Already have this peer in any direction? Skip.
    for (unsigned i = 0; i < peerPanels_.Size(); ++i)
    {
        if (peerPanels_[i].address.Contains(addr))
            return;
    }

    // Already have an outbound connection? Skip.
    if (GetSubsystem<Network>()->GetServerConnection())
        return;

    URHO3D_LOGINFO("[GARY] Discovered peer at " + addr + ", connecting");
    GetSubsystem<Network>()->Connect(addr, GARY_PORT, nullptr);
}

void Gary::HandleClientConnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientConnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    peerCount_++;
    AddPeerPanel(conn, conn->ToString());
    statusText_->SetText("Gary (" + String(peerCount_) + " peers)");
    statusText_->SetColor(Color::GREEN);
    URHO3D_LOGINFO("[GARY] Peer connected: " + conn->ToString());
}

void Gary::HandleClientDisconnected(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ClientDisconnected;
    auto* conn = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    if (peerCount_ > 0)
        peerCount_--;
    RemovePeerPanel(conn);
    statusText_->SetText(peerCount_ > 0
        ? "Gary (" + String(peerCount_) + " peers)"
        : "Waiting for peers...");
    statusText_->SetColor(peerCount_ > 0 ? Color::GREEN : Color::YELLOW);
    URHO3D_LOGINFO("[GARY] Peer left");
}

void Gary::HandleConnectionStatus(StringHash eventType, VariantMap& /*eventData*/)
{
    if (eventType == E_SERVERCONNECTED)
    {
        // We connected to another Gary as a client. Add them as a peer.
        auto* network = GetSubsystem<Network>();
        Connection* serverConn = network->GetServerConnection();
        if (serverConn)
        {
            peerCount_++;
            AddPeerPanel(serverConn, serverConn->ToString());
            URHO3D_LOGINFO("[GARY] Connected to peer: " + serverConn->ToString());
        }
        statusText_->SetText("Gary (" + String(peerPanels_.Size()) + " peers)");
        statusText_->SetColor(Color::GREEN);
    }
    else if (eventType == E_SERVERDISCONNECTED)
    {
        // Lost outbound connection — remove that peer panel
        auto* network = GetSubsystem<Network>();
        // ServerConnection is already gone, find the panel with no valid connection
        for (unsigned i = 0; i < peerPanels_.Size(); ++i)
        {
            if (!peerPanels_[i].connection)
            {
                peerContainer_->RemoveChild(peerPanels_[i].panel);
                peerPanels_.Erase(i);
                if (peerCount_ > 0) peerCount_--;
                break;
            }
        }
        statusText_->SetText(peerPanels_.Empty()
            ? "Waiting for peers..."
            : "Gary (" + String(peerPanels_.Size()) + " peers)");
        statusText_->SetColor(peerPanels_.Empty() ? Color::YELLOW : Color::GREEN);
        URHO3D_LOGINFO("[GARY] Peer disconnected");
    }
    else if (eventType == E_CONNECTFAILED)
    {
        // Couldn't reach that peer — no big deal in a mesh
        retryTimer_ = 0.0f;
    }
}

// ─── File Drop ───────────────────────────────────────────────────────────────

void Gary::HandleDropFile(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace DropFile;
    String path = eventData[P_FILENAME].GetString();

    URHO3D_LOGINFO("[GARY] DROP EVENT: " + path);
    statusText_->SetText("Dropped: " + GetFileNameAndExtension(path));
    statusText_->SetColor(Color::CYAN);

    Connection* target = FindPeerUnderCursor();
    if (!target)
    {
        transferText_->SetText("Drop onto a peer panel to send");
        transferText_->SetColor(Color::RED);
        return;
    }

    URHO3D_LOGINFO("Gary: File dropped for " + target->ToString() + ": " + path);
    QueueFileForPeer(path, target);
}

void Gary::QueueFileForPeer(const String& path, Connection* target)
{
    auto* fs = GetSubsystem<FileSystem>();

    if (fs->DirExists(path))
    {
        Vector<String> files;
        fs->ScanDir(files, path, "*", SCAN_FILES, true);
        String dirName = GetFileNameAndExtension(RemoveTrailingSlash(path));

        for (const String& file : files)
        {
            if (file.StartsWith("."))
                continue;

            String fullPath = AddTrailingSlash(path) + file;
            String relativePath = dirName + "/" + file;

            File f(context_, fullPath, FILE_READ);
            if (!f.IsOpen() || f.GetSize() == 0)
                continue;

            OutTransfer transfer;
            transfer.relativePath = relativePath;
            transfer.fullPath = fullPath;
            transfer.fileSize = f.GetSize();
            transfer.offset = 0;
            transfer.fileId = nextFileId_++;
            transfer.target = target;
            f.Close();

            outTransfers_.Push(transfer);
            SendFileOffer(transfer);
        }
        transferText_->SetText("Sending folder: " + dirName);
        transferText_->SetColor(Color::CYAN);
    }
    else if (fs->FileExists(path))
    {
        String fileName = GetFileNameAndExtension(path);

        File f(context_, path, FILE_READ);
        if (!f.IsOpen() || f.GetSize() == 0)
            return;

        OutTransfer transfer;
        transfer.relativePath = fileName;
        transfer.fullPath = path;
        transfer.fileSize = f.GetSize();
        transfer.offset = 0;
        transfer.fileId = nextFileId_++;
        transfer.target = target;
        f.Close();

        outTransfers_.Push(transfer);
        SendFileOffer(transfer);

        transferText_->SetText("Sending: " + fileName);
        transferText_->SetColor(Color::CYAN);
    }
}

// ─── Outbox Watcher ──────────────────────────────────────────────────────────

void Gary::ScanOutbox()
{
    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->DirExists(outboxPath_))
        return;

    Vector<String> files;
    fs->ScanDir(files, outboxPath_, "*", SCAN_FILES, true);

    for (const String& relativePath : files)
    {
        if (relativePath.StartsWith(".") || knownOutboxFiles_.Contains(relativePath))
            continue;

        String fullPath = outboxPath_ + relativePath;
        if (!fs->FileExists(fullPath))
            continue;

        knownOutboxFiles_.Insert(relativePath);

        // Outbox sends to ALL peers — every panel is a peer
        for (unsigned i = 0; i < peerPanels_.Size(); ++i)
        {
            if (peerPanels_[i].connection)
                QueueFileForPeer(fullPath, peerPanels_[i].connection);
        }
    }
}

// ─── Transfer Engine ─────────────────────────────────────────────────────────

void Gary::SendFileOffer(const OutTransfer& transfer)
{
    if (!transfer.target)
        return;

    VectorBuffer msg;
    msg.WriteU32(transfer.fileId);
    msg.WriteString(transfer.relativePath);
    msg.WriteU32(transfer.fileSize);

    transfer.target->SendMessage(MSG_FB_FILE_OFFER, true, true, msg);
}

void Gary::ProcessOutbound(float /*timeStep*/)
{
    for (unsigned i = 0; i < outTransfers_.Size(); )
    {
        OutTransfer& transfer = outTransfers_[i];

        if (!transfer.target)
        {
            outTransfers_.Erase(i);
            continue;
        }

        if (transfer.offset >= transfer.fileSize)
        {
            VectorBuffer msg;
            msg.WriteU32(transfer.fileId);
            msg.WriteU32(transfer.fileSize);
            transfer.target->SendMessage(MSG_FB_FILE_COMPLETE, true, true, msg);

            URHO3D_LOGINFO("Gary: Sent " + transfer.relativePath);
            outTransfers_.Erase(i);
            continue;
        }

        SendNextChunk(transfer);
        ++i;
    }
}

void Gary::SendNextChunk(OutTransfer& transfer)
{
    if (!transfer.target)
        return;

    File file(context_, transfer.fullPath, FILE_READ);
    if (!file.IsOpen())
        return;

    file.Seek(transfer.offset);

    unsigned remaining = transfer.fileSize - transfer.offset;
    unsigned toRead = Min(CHUNK_SIZE, remaining);

    Vector<unsigned char> buffer(toRead);
    unsigned bytesRead = file.Read(buffer.Buffer(), toRead);
    if (bytesRead == 0)
        return;

    VectorBuffer msg;
    msg.WriteU32(transfer.fileId);
    msg.WriteU32(transfer.offset);
    msg.WriteU32(bytesRead);
    msg.Write(buffer.Buffer(), bytesRead);

    transfer.target->SendMessage(MSG_FB_FILE_DATA, true, true, msg);
    transfer.offset += bytesRead;
}

// ─── Receive ─────────────────────────────────────────────────────────────────

void Gary::HandleNetworkMessage(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace NetworkMessage;

    int msgID = eventData[P_MESSAGEID].GetI32();
    const Vector<byte>& data = eventData[P_DATA].GetBuffer();
    MemoryBuffer msg(data);
    auto* sender = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());

    switch (msgID)
    {
    case MSG_FB_FILE_OFFER:
        HandleFileOffer(sender, msg);
        break;
    case MSG_FB_FILE_DATA:
        HandleFileData(sender, msg);
        break;
    case MSG_FB_FILE_COMPLETE:
        HandleFileComplete(sender, msg);
        break;
    default:
        break;
    }
}

void Gary::HandleFileOffer(Connection* /*sender*/, MemoryBuffer& msg)
{
    unsigned fileId = msg.ReadU32();
    String relativePath = msg.ReadString();
    unsigned fileSize = msg.ReadU32();

    URHO3D_LOGINFO("Gary: Receiving " + relativePath + " (" + String(fileSize) + " bytes)");

    String fullPath = inboxPath_ + relativePath;
    String dir = GetPath(fullPath);
    auto* fs = GetSubsystem<FileSystem>();
    if (!fs->DirExists(dir))
        fs->CreateDir(dir);

    InTransfer transfer;
    transfer.relativePath = relativePath;
    transfer.fileSize = fileSize;
    transfer.received = 0;
    transfer.fileId = fileId;
    transfer.file = new File(context_, fullPath, FILE_WRITE);

    if (!transfer.file->IsOpen())
    {
        URHO3D_LOGERROR("Gary: Failed to create " + fullPath);
        return;
    }

    inTransfers_[fileId] = transfer;
    transferText_->SetText("Receiving: " + relativePath);
    transferText_->SetColor(Color::CYAN);
}

void Gary::HandleFileData(Connection* /*sender*/, MemoryBuffer& msg)
{
    unsigned fileId = msg.ReadU32();
    unsigned offset = msg.ReadU32();
    unsigned chunkSize = msg.ReadU32();

    auto it = inTransfers_.Find(fileId);
    if (it == inTransfers_.End())
        return;

    InTransfer& transfer = it->second_;
    if (!transfer.file || !transfer.file->IsOpen())
        return;

    unsigned remaining = msg.GetSize() - msg.GetPosition();
    unsigned toRead = Min(chunkSize, remaining);

    Vector<unsigned char> buffer(toRead);
    msg.Read(buffer.Buffer(), toRead);

    transfer.file->Seek(offset);
    transfer.file->Write(buffer.Buffer(), toRead);
    transfer.received += toRead;
}

void Gary::HandleFileComplete(Connection* /*sender*/, MemoryBuffer& msg)
{
    unsigned fileId = msg.ReadU32();
    unsigned expectedSize = msg.ReadU32();

    auto it = inTransfers_.Find(fileId);
    if (it == inTransfers_.End())
        return;

    InTransfer& transfer = it->second_;
    if (transfer.file)
        transfer.file->Close();

    if (transfer.received >= expectedSize)
    {
        URHO3D_LOGINFO("Gary: Received " + transfer.relativePath + " OK");
        transferText_->SetText("Received: " + transfer.relativePath);
        transferText_->SetColor(Color::GREEN);
    }
    else
    {
        URHO3D_LOGWARNING("Gary: Incomplete " + transfer.relativePath);
        transferText_->SetText("Incomplete: " + transfer.relativePath);
        transferText_->SetColor(Color::RED);
    }

    inTransfers_.Erase(it);
}

// ─── Update ──────────────────────────────────────────────────────────────────

void Gary::HandleUpdate(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Periodic discovery — find new Garys on the LAN
    retryTimer_ += timeStep;
    if (retryTimer_ >= 5.0f)
    {
        retryTimer_ = 0.0f;
        GetSubsystem<Network>()->DiscoverHosts(GARY_PORT);
    }

    // File watcher
    if (outboxWatcher_)
    {
        String changedFile;
        while (outboxWatcher_->GetNextChange(changedFile))
            ScanOutbox();
    }

    // Process outbound
    if (!outTransfers_.Empty())
    {
        ProcessOutbound(timeStep);
        if (!outTransfers_.Empty())
        {
            const OutTransfer& t = outTransfers_[0];
            unsigned pct = t.fileSize > 0 ? (t.offset * 100 / t.fileSize) : 0;
            transferText_->SetText("Sending: " + t.relativePath + " " + String(pct) + "%");
        }
    }

    // Highlight peer panel under cursor
    auto* ui = GetSubsystem<UI>();
    IntVector2 cursorPos = ui->GetCursorPosition();
    for (unsigned i = 0; i < peerPanels_.Size(); ++i)
    {
        PeerPanel& pp = peerPanels_[i];
        if (!pp.panel)
            continue;

        IntVector2 panelPos = pp.panel->GetScreenPosition();
        IntVector2 panelSize = pp.panel->GetSize();
        bool hover = (cursorPos.x_ >= panelPos.x_ && cursorPos.x_ <= panelPos.x_ + panelSize.x_ &&
                      cursorPos.y_ >= panelPos.y_ && cursorPos.y_ <= panelPos.y_ + panelSize.y_);

        pp.panel->SetColor(hover ? Color(0.9f, 0.95f, 1.0f) : Color(0.7f, 0.75f, 0.8f));
    }
}
