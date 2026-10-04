// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Resource/Image.h>
#include <Urho3D/Scene/LogicComponent.h>

namespace Urho3D { class Terrain; }
using namespace Urho3D;

/// Soil type encoded in the R channel.
enum SoilType : unsigned char
{
    SOIL_ROCK  = 0,
    SOIL_SAND  = 64,
    SOIL_CLAY  = 128,
    SOIL_LOAM  = 192,
    SOIL_PEAT  = 255
};

/// Return the nearest SoilType enum value for a raw R channel byte.
inline SoilType ClassifySoilByte(unsigned char r)
{
    if (r < 32)  return SOIL_ROCK;
    if (r < 96)  return SOIL_SAND;
    if (r < 160) return SOIL_CLAY;
    if (r < 224) return SOIL_LOAM;
    return SOIL_PEAT;
}

/// Return a human-readable name for a soil type.
inline const char* SoilTypeName(SoilType type)
{
    switch (type)
    {
    case SOIL_ROCK: return "Rock";
    case SOIL_SAND: return "Sand";
    case SOIL_CLAY: return "Clay";
    case SOIL_LOAM: return "Loam";
    case SOIL_PEAT: return "Peat";
    default:        return "Unknown";
    }
}

/// Per-terrain soil and mineral property map.
/// RGBA texture at the same resolution as the heightmap.
/// R = soil type, G = fertility, B = mineral density, A = moisture retention.
class SoilMap : public LogicComponent
{
    URHO3D_OBJECT(SoilMap, LogicComponent);

public:
    explicit SoilMap(Context* context);
    static void RegisterObject(Context* context);

    /// Load soil texture from ResourceCache. Returns false if missing or wrong size.
    bool LoadMap(const String& path, int expectedSize);

    /// Generate a default soil map from terrain properties (height, slope, water proximity).
    void GenerateDefault(Terrain* terrain, float waterLevel);

    /// Save map to disk (PNG).
    bool SaveMap(const String& path) const;

    /// Sample soil type at terrain grid (x, z). Returns SOIL_ROCK if out of bounds.
    SoilType SampleSoilType(int x, int z) const;

    /// Sample fertility (0-255) at terrain grid (x, z).
    unsigned char SampleFertility(int x, int z) const;

    /// Sample mineral density (0-255) at terrain grid (x, z).
    unsigned char SampleMineralDensity(int x, int z) const;

    /// Sample moisture retention (0-255) at terrain grid (x, z).
    unsigned char SampleMoisture(int x, int z) const;

    /// Get map size (0 if not loaded).
    int GetMapSize() const { return mapSize_; }

    /// Get the underlying image (for debug visualization).
    Image* GetImage() const { return soilImage_; }

private:
    bool InBounds(int x, int z) const { return soilImage_ && x >= 0 && z >= 0 && x < mapSize_ && z < mapSize_; }

    SharedPtr<Image> soilImage_;
    int mapSize_{0};
};
