// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Graphics/Terrain.h>

using namespace Urho3D;

class EcosystemManager;
namespace Urho3D { class GameDB; }

/// Resource type encoded in the R channel of the resource map texture.
/// New entries APPEND ONLY — existing serialized maps store the byte value
/// directly, renumbering would silently corrupt every saved resource_map.png.
enum ResourceType : unsigned char
{
    RES_NONE       = 0,
    RES_STONE      = 1,   // Rough Stone (item 1)
    RES_STICK      = 2,   // Fallen Stick (item 2)
    RES_FIBER      = 3,   // Plant Fiber (item 3)
    RES_CLAY       = 4,   // Clay (item 4)
    RES_FLINT      = 5,   // Flint (item 5)
    RES_BERRIES    = 6,   // Berries (item 6)
    RES_LOG        = 7,   // Log (item 11) — from tree stumps
    RES_MUSHROOM   = 8,   // Future: foraging
    RES_HERB       = 9,   // Future: herbalism
    RES_SHELL      = 10,  // Future: beach gathering
    RES_REEDS      = 11,  // Future: wetland fiber
    RES_SOFTWOOD   = 12,  // Softwood (Fire system Phase 1b — item 15, set by Phase 1a)
    RES_HARDWOOD   = 13,  // Hardwood (Fire system Phase 1b — item 16, set by Phase 1a)
};

/// Flags bitfield stored in the A channel.
enum ResourceFlags : unsigned char
{
    RFLAG_RESPAWNABLE = 1 << 0,
    RFLAG_SEASONAL    = 1 << 1,
    RFLAG_DEPLETED    = 1 << 2,
    RFLAG_RESERVED    = 1 << 3,
};

/// Map resource type to game item ID. Returns -1 if unknown.
inline int ResourceTypeToItemId(ResourceType type)
{
    switch (type)
    {
    case RES_STONE:    return 1;
    case RES_STICK:    return 2;
    case RES_FIBER:    return 3;
    case RES_CLAY:     return 4;
    case RES_FLINT:    return 5;
    case RES_BERRIES:  return 6;
    case RES_LOG:      return 11;
    case RES_MUSHROOM: return 730;
    case RES_SOFTWOOD: return 15;  // confirmed by coder2 (Phase 1a)
    case RES_HARDWOOD: return 16;  // confirmed by coder2 (Phase 1a)
    default:           return -1;
    // Note: Rock for fire-pit construction reuses item 1 'Rough Stone' (RES_STONE),
    // per Phase 1a's seed_data.sql comment. No separate RES_ROCK type.
    }
}

/// Map item ID to ResourceType. Returns RES_NONE if no mapping.
inline ResourceType ItemIdToResourceType(int itemId)
{
    switch (itemId)
    {
    case 1:  return RES_STONE;
    case 2:  return RES_STICK;
    case 3:  return RES_FIBER;
    case 4:  return RES_CLAY;
    case 5:  return RES_FLINT;
    case 6:  return RES_BERRIES;
    case 11: return RES_LOG;
    case 15: return RES_SOFTWOOD;
    case 16: return RES_HARDWOOD;
    case 730: return RES_MUSHROOM;
    default: return RES_NONE;
    }
}

/// Texture-based spatial resource database.
/// One 2048x2048 RGBA image encodes every harvestable resource on the terrain.
/// R = type, G = quantity, B = variant, A = flags.
class ResourceMap : public LogicComponent
{
    URHO3D_OBJECT(ResourceMap, LogicComponent);

public:
    explicit ResourceMap(Context* context);
    static void RegisterObject(Context* context);

    /// Generate resource map from terrain + ecosystem data.
    /// If gameDB is provided, placement rules come from gather_sources table.
    void Generate(Terrain* terrain, EcosystemManager* eco, float waterLevel, GameDB* gameDB = nullptr);

    /// Load pre-generated map from ResourceCache (PNG).
    bool LoadMap(const String& path);

    /// Load map from absolute filesystem path (bypasses ResourceCache caching).
    bool LoadMapFromFile(const String& absolutePath);

    /// Save map to disk (PNG) for persistence.
    bool SaveMap(const String& path) const;

    /// Sample resource at world position. Returns RES_NONE if empty.
    ResourceType Sample(float worldX, float worldZ,
                        unsigned char& outQty,
                        unsigned char& outVariant) const;

    /// Harvest resource at world position. Decrements quantity, returns actual amount taken.
    int Harvest(float worldX, float worldZ, int amount);

    /// Find nearest resource of given type within maxRadius. Returns false if none found.
    bool FindNearest(float worldX, float worldZ, float maxRadius,
                     ResourceType typeFilter,
                     float& outX, float& outZ) const;

    /// Respawned pixel info for broadcasting to clients.
    struct RespawnEvent
    {
        float worldX, worldZ;
        unsigned char qty;
        unsigned char type;
    };

    /// Tick respawn timers. Call periodically (e.g., once per game-minute).
    /// Returns list of respawned positions for network broadcast.
    void TickRespawn(Vector<RespawnEvent>* outRespawned = nullptr);

    /// Get the underlying image for debug visualization / minimap overlay.
    Image* GetImage() const { return resourceImage_; }

    /// Get total non-empty resource pixel count (debug stat).
    unsigned GetResourceCount() const { return resourceCount_; }

    /// Set terrain world bounds (required after LoadMapFromFile if terrain wasn't passed to Generate).
    void SetTerrainBounds(Terrain* terrain);

    static constexpr int MAP_SIZE = 2048;

private:
    IntVector2 WorldToPixel(float worldX, float worldZ) const;
    Vector2 PixelToWorld(int px, int pz) const;

    SharedPtr<Image> resourceImage_;
    float terrainOriginX_{};
    float terrainOriginZ_{};
    float terrainSizeX_{1.f};
    float terrainSizeZ_{1.f};
    unsigned resourceCount_{};
};
