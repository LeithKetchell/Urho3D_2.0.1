// TerrainJournal — versioned edit journal for terrain heightmap sync
// Server maintains per-grid-cell journals. Reconnecting clients send their
// last version + hash; server replays only missing edits or sends full sync.

#pragma once

#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Graphics/Terrain.h>

namespace Urho3D
{

/// Single terrain brush edit, sufficient for replay.
struct TerrainEdit
{
    unsigned version;        // monotonic counter per grid cell
    Vector3 worldPos;        // brush center
    int brushMode;           // raise/lower/smooth/flatten/erosion
    int brushShape;          // circle/square/etc
    float brushRadius;
    float brushStrength;
    float smoothStrength;
    float brushRotation;
    float flattenHeight;
    float timeStep;          // for consistent replay
    unsigned timestamp;      // unix time (for journal trimming)

    /// Write edit to buffer for network transmission.
    void Write(VectorBuffer& buf) const
    {
        buf.WriteU32(version);
        buf.WriteVector3(worldPos);
        buf.WriteI32(brushMode);
        buf.WriteI32(brushShape);
        buf.WriteFloat(brushRadius);
        buf.WriteFloat(brushStrength);
        buf.WriteFloat(smoothStrength);
        buf.WriteFloat(brushRotation);
        buf.WriteFloat(flattenHeight);
        buf.WriteFloat(timeStep);
        buf.WriteU32(timestamp);
    }

    /// Read edit from buffer.
    void Read(MemoryBuffer& buf)
    {
        version = buf.ReadU32();
        worldPos = buf.ReadVector3();
        brushMode = buf.ReadI32();
        brushShape = buf.ReadI32();
        brushRadius = buf.ReadFloat();
        brushStrength = buf.ReadFloat();
        smoothStrength = buf.ReadFloat();
        brushRotation = buf.ReadFloat();
        flattenHeight = buf.ReadFloat();
        timeStep = buf.ReadFloat();
        timestamp = buf.ReadU32();
    }
};

/// Per-grid-cell edit journal with version tracking and hash validation.
class TerrainJournal
{
public:
    /// Append an edit and increment version.
    void AppendEdit(const TerrainEdit& edit);

    /// Update the heightmap hash (call after applying edits).
    void UpdateHash(Image* heightMap);

    /// Trim old entries. Keeps edits from last maxAgeSec seconds or last maxEntries, whichever is larger.
    void Trim(unsigned maxAgeSec = 604800, unsigned maxEntries = 1000000);

    /// Check if journal can serve edits from clientVersion to current.
    bool CanReplay(unsigned clientVersion) const;

    /// Write edits from clientVersion+1 to current into a MSG_TERRAIN_JOURNAL buffer.
    void WriteJournal(VectorBuffer& buf, int gridX, int gridZ, unsigned clientVersion) const;

    /// Write full heightmap sync into a MSG_TERRAIN_FULLSYNC buffer.
    static void WriteFullSync(VectorBuffer& buf, int gridX, int gridZ, unsigned version, unsigned hash, Image* heightMap);

    /// Compute CRC32 of raw heightmap pixel data.
    static unsigned ComputeHeightmapHash(Image* heightMap);

    unsigned GetVersion() const { return currentVersion_; }
    unsigned GetHash() const { return heightmapHash_; }
    unsigned GetBaselineVersion() const { return baselineVersion_; }
    const Vector<TerrainEdit>& GetEdits() const { return edits_; }

private:
    unsigned currentVersion_{0};
    unsigned heightmapHash_{0};
    unsigned baselineVersion_{0};  // oldest version in journal (after trimming)
    Vector<TerrainEdit> edits_;
};

/// Manager for all per-grid-cell journals.
class TerrainJournalManager
{
public:
    /// Get or create journal for a grid cell.
    TerrainJournal& GetJournal(int gridX, int gridZ);

    /// Record an edit to the appropriate grid cell's journal.
    void RecordEdit(int gridX, int gridZ, const TerrainEdit& edit);

    /// Handle MSG_TERRAIN_SYNC from a client.
    void HandleSyncRequest(Connection* connection, MemoryBuffer& msg,
                           const HashMap<IntVector2, WeakPtr<Terrain>>& terrainGrid);

    /// Trim all journals.
    void TrimAll(unsigned maxAgeSec = 604800, unsigned maxEntries = 1000000);

private:
    HashMap<IntVector2, TerrainJournal> journals_;

    static unsigned long long GridKey(int x, int z) { return ((unsigned long long)(unsigned)x << 32) | (unsigned)z; }
};

}
