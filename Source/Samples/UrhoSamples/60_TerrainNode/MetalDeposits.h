// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Math/Vector2.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Scene/LogicComponent.h>

namespace Urho3D { class Terrain; }
using namespace Urho3D;

/// Metal type encoded in G channel.
enum MetalType : unsigned char
{
    METAL_NONE    = 0,
    METAL_COPPER  = 1,
    METAL_TIN     = 2,
    METAL_IRON    = 3,
    METAL_GOLD    = 4,
    METAL_SILVER  = 5,
    METAL_COAL    = 6,
    METAL_OBSIDIAN = 7,
    METAL_FLINT   = 8
};

/// Per-terrain metal deposit map manager.
/// RGBA texture: R=quantity, G=type, B=purity, A=depth.
class MetalDeposits : public LogicComponent
{
    URHO3D_OBJECT(MetalDeposits, LogicComponent);

public:
    explicit MetalDeposits(Context* context);
    static void RegisterObject(Context* context);

    /// Load deposit texture. Returns false if missing or wrong size.
    bool LoadMap(const String& path, int expectedSize);

    /// Sample deposit at terrain grid (x, z). Returns false if no deposit or exhausted.
    bool Sample(int x, int z, unsigned char& outType, unsigned char& outPurity,
                unsigned char& outQuantity, unsigned char& outDepth) const;

    /// Decrement quantity at (x, z) by amount. Returns actual amount removed.
    int Mine(int x, int z, int amount);

    /// Save modified texture to disk (autosave).
    bool SaveMap(const String& path) const;

    /// Spawn surface outcrop nodes for deposits with depth <= maxDepth.
    void SpawnOutcrops(Scene* scene, Terrain* terrain, float waterLevel, int maxDepth = 5);

    /// Remove outcrop node at grid position (called when deposit exhausted).
    void RemoveOutcrop(int x, int z);

    /// Spawn a single outcrop at grid (x, z) for a newly discovered deposit.
    void SpawnSingleOutcrop(int x, int z, int metalType, Terrain* terrain);

    /// Get map size (0 if not loaded).
    int GetMapSize() const { return mapSize_; }

    /// Get ore type at grid position (0 = none).
    int GetOreType(int x, int z) const
    {
        unsigned char type, purity, qty, depth;
        if (Sample(x, z, type, purity, qty, depth)) return type;
        return 0;
    }

    /// Get ore quantity at grid position (0 = exhausted).
    int GetOreQty(int x, int z) const
    {
        unsigned char type, purity, qty, depth;
        if (Sample(x, z, type, purity, qty, depth)) return qty;
        return 0;
    }

private:
    SharedPtr<Image> depositMap_;
    int mapSize_{0};
    /// Grid pos -> outcrop scene node ID (for removal).
    HashMap<IntVector2, unsigned> outcropNodes_;
};
