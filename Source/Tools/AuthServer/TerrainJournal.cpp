// TerrainJournal — versioned edit journal for terrain heightmap sync

#include "TerrainJournal.h"
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/IO/Log.h>

#include <ctime>

namespace Urho3D
{

// ---- TerrainJournal ----

void TerrainJournal::AppendEdit(const TerrainEdit& edit)
{
    edits_.Push(edit);
    currentVersion_ = edit.version;
}

void TerrainJournal::UpdateHash(Image* heightMap)
{
    if (heightMap)
        heightmapHash_ = ComputeHeightmapHash(heightMap);
}

void TerrainJournal::Trim(unsigned maxAgeSec, unsigned maxEntries)
{
    if (edits_.Empty())
        return;

    unsigned now = (unsigned)time(nullptr);
    unsigned cutoffTime = (now > maxAgeSec) ? now - maxAgeSec : 0;

    // Find first edit to keep: must be within time AND within entry count
    unsigned keepFromIndex = 0;

    // Time-based: find first edit newer than cutoff
    for (unsigned i = 0; i < edits_.Size(); ++i)
    {
        if (edits_[i].timestamp >= cutoffTime)
        {
            keepFromIndex = i;
            break;
        }
        keepFromIndex = i + 1;
    }

    // Entry-count-based: keep at most maxEntries
    if (edits_.Size() - keepFromIndex > maxEntries)
        keepFromIndex = edits_.Size() - maxEntries;

    if (keepFromIndex > 0)
    {
        baselineVersion_ = edits_[keepFromIndex].version;
        edits_.Erase(0, keepFromIndex);
        URHO3D_LOGINFOF("TerrainJournal: trimmed %u old entries, baseline now v%u", keepFromIndex, baselineVersion_);
    }
}

bool TerrainJournal::CanReplay(unsigned clientVersion) const
{
    // Client is already current
    if (clientVersion >= currentVersion_)
        return true;

    // Journal has been trimmed past what client needs
    if (clientVersion < baselineVersion_)
        return false;

    // Check that we have the edit right after client's version
    if (edits_.Empty())
        return clientVersion >= currentVersion_;

    return edits_[0].version <= clientVersion + 1;
}

void TerrainJournal::WriteJournal(VectorBuffer& buf, int gridX, int gridZ, unsigned clientVersion) const
{
    buf.WriteI32(gridX);
    buf.WriteI32(gridZ);

    // Collect edits to send
    Vector<const TerrainEdit*> toSend;
    for (unsigned i = 0; i < edits_.Size(); ++i)
    {
        if (edits_[i].version > clientVersion)
            toSend.Push(&edits_[i]);
    }

    buf.WriteU32(toSend.Size());
    for (unsigned i = 0; i < toSend.Size(); ++i)
        toSend[i]->Write(buf);

    buf.WriteU32(currentVersion_);
    buf.WriteU32(heightmapHash_);
}

void TerrainJournal::WriteFullSync(VectorBuffer& buf, int gridX, int gridZ,
                                    unsigned version, unsigned hash, Image* heightMap)
{
    buf.WriteI32(gridX);
    buf.WriteI32(gridZ);
    buf.WriteU32(version);
    buf.WriteU32(hash);

    // Save heightmap to memory (PNG format via Image::Save)
    VectorBuffer pngBuf;
    if (heightMap)
        heightMap->Save(pngBuf);

    buf.WriteU32(pngBuf.GetSize());
    if (pngBuf.GetSize() > 0)
        buf.Write(pngBuf.GetData(), pngBuf.GetSize());
}

unsigned TerrainJournal::ComputeHeightmapHash(Image* heightMap)
{
    if (!heightMap)
        return 0;

    // CRC32 on raw pixel data
    const unsigned char* data = heightMap->GetData();
    unsigned size = (unsigned)(heightMap->GetWidth() * heightMap->GetHeight() * heightMap->GetComponents());

    // Standard CRC32 (ISO 3309)
    unsigned crc = 0xFFFFFFFF;
    for (unsigned i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ---- TerrainJournalManager ----

TerrainJournal& TerrainJournalManager::GetJournal(int gridX, int gridZ)
{
    IntVector2 key(gridX, gridZ);
    return journals_[key];
}

void TerrainJournalManager::RecordEdit(int gridX, int gridZ, const TerrainEdit& edit)
{
    TerrainJournal& journal = GetJournal(gridX, gridZ);
    journal.AppendEdit(edit);
}

void TerrainJournalManager::HandleSyncRequest(Connection* connection, MemoryBuffer& msg,
                                                const HashMap<IntVector2, WeakPtr<Terrain>>& terrainGrid)
{
    int gridX = msg.ReadI32();
    int gridZ = msg.ReadI32();
    unsigned clientVersion = msg.ReadU32();
    unsigned clientHash = msg.ReadU32();

    IntVector2 key(gridX, gridZ);
    TerrainJournal& journal = GetJournal(gridX, gridZ);

    // Check if client is already current
    if (clientVersion == journal.GetVersion() && clientHash == journal.GetHash())
    {
        URHO3D_LOGINFOF("TerrainJournal: client %s already current for grid (%d,%d) v%u",
            connection->ToString().CString(), gridX, gridZ, clientVersion);
        return;
    }

    // Try incremental journal replay
    if (clientHash == 0 || !journal.CanReplay(clientVersion))
    {
        // Hash mismatch or journal gap — send full heightmap
        auto it = terrainGrid.Find(key);
        Image* heightMap = nullptr;
        if (it != terrainGrid.End() && it->second_)
            heightMap = it->second_->GetHeightMap();

        if (!heightMap)
        {
            URHO3D_LOGERRORF("TerrainJournal: no heightmap for grid (%d,%d), cannot full sync", gridX, gridZ);
            return;
        }

        // Update hash before sending
        journal.UpdateHash(heightMap);

        VectorBuffer buf;
        TerrainJournal::WriteFullSync(buf, gridX, gridZ, journal.GetVersion(), journal.GetHash(), heightMap);
        connection->SendMessage(MSG_TERRAIN_FULLSYNC, true, true, buf.GetData(), buf.GetSize());

        URHO3D_LOGINFOF("TerrainJournal: full sync to %s for grid (%d,%d) — %u bytes, v%u",
            connection->ToString().CString(), gridX, gridZ, buf.GetSize(), journal.GetVersion());
    }
    else
    {
        // Incremental replay
        VectorBuffer buf;
        journal.WriteJournal(buf, gridX, gridZ, clientVersion);
        connection->SendMessage(MSG_TERRAIN_JOURNAL, true, true, buf.GetData(), buf.GetSize());

        unsigned editCount = journal.GetVersion() - clientVersion;
        URHO3D_LOGINFOF("TerrainJournal: journal replay to %s for grid (%d,%d) — %u edits (v%u→v%u)",
            connection->ToString().CString(), gridX, gridZ, editCount, clientVersion, journal.GetVersion());
    }
}

void TerrainJournalManager::TrimAll(unsigned maxAgeSec, unsigned maxEntries)
{
    for (auto it = journals_.Begin(); it != journals_.End(); ++it)
        it->second_.Trim(maxAgeSec, maxEntries);
}

}
