// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "SoilMap.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Resource/ResourceCache.h>

SoilMap::SoilMap(Context* context) :
    LogicComponent(context)
{
}

void SoilMap::RegisterObject(Context* context)
{
    context->RegisterFactory<SoilMap>();
}

bool SoilMap::LoadMap(const String& path, int expectedSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    soilImage_ = cache->GetResource<Image>(path);
    if (!soilImage_)
    {
        URHO3D_LOGWARNINGF("SoilMap: could not load '%s'", path.CString());
        return false;
    }

    mapSize_ = soilImage_->GetWidth();
    if (mapSize_ != expectedSize)
    {
        URHO3D_LOGWARNINGF("SoilMap: map size %d != expected %d", mapSize_, expectedSize);
        soilImage_.Reset();
        mapSize_ = 0;
        return false;
    }

    URHO3D_LOGINFOF("SoilMap: loaded %dx%d soil map", mapSize_, mapSize_);
    return true;
}

void SoilMap::GenerateDefault(Terrain* terrain, float waterLevel)
{
    if (!terrain || !terrain->GetHeightMap())
        return;

    int size = terrain->GetHeightMap()->GetWidth();
    soilImage_ = new Image(context_);
    soilImage_->SetSize(size, size, 4);  // RGBA
    mapSize_ = size;

    Vector3 spacing = terrain->GetSpacing();

    for (int z = 0; z < size; ++z)
    {
        for (int x = 0; x < size; ++x)
        {
            IntVector2 hmPos(x, z);
            Vector3 worldPos = terrain->HeightMapToWorld(hmPos);
            float height = worldPos.y_;
            Vector3 normal = terrain->GetNormal(worldPos);
            float slope = 1.0f - normal.y_;  // 0 = flat, ~1 = vertical

            // --- R: Soil type ---
            // Rock on steep slopes and high altitude; peat near water; loam on flat lowlands
            unsigned char soilType;
            if (slope > 0.4f || height > waterLevel + 40.0f)
                soilType = SOIL_ROCK;
            else if (height < waterLevel + 1.0f)
                soilType = SOIL_PEAT;
            else if (slope > 0.2f)
                soilType = SOIL_SAND;
            else if (height < waterLevel + 5.0f)
                soilType = SOIL_CLAY;
            else
                soilType = SOIL_LOAM;

            // --- G: Fertility ---
            // High on flat, low ground with good soil; low on rock and steep
            float fertility = 0.0f;
            if (soilType == SOIL_LOAM)
                fertility = Clamp(0.7f + (1.0f - slope) * 0.3f, 0.0f, 1.0f);
            else if (soilType == SOIL_PEAT)
                fertility = 0.85f;
            else if (soilType == SOIL_CLAY)
                fertility = 0.5f;
            else if (soilType == SOIL_SAND)
                fertility = 0.2f;
            // Rock stays at 0

            // --- B: Mineral density ---
            // Simple noise-like distribution using grid position hash
            // Denser in rock and deep areas
            unsigned hash = (unsigned)(x * 73856093u ^ z * 19349663u);
            float noise = (float)(hash % 256) / 255.0f;
            float mineralBase = 0.0f;
            if (soilType == SOIL_ROCK)
                mineralBase = 0.3f + noise * 0.7f;  // rock is mineral-rich
            else if (soilType == SOIL_SAND)
                mineralBase = noise * 0.3f;          // alluvial deposits
            else if (soilType == SOIL_CLAY)
                mineralBase = noise * 0.15f;
            else
                mineralBase = noise * 0.1f;

            // --- A: Moisture retention ---
            // High for clay/peat, low for sand/rock
            float moisture = 0.0f;
            if (soilType == SOIL_PEAT)
                moisture = 0.9f;
            else if (soilType == SOIL_CLAY)
                moisture = 0.75f;
            else if (soilType == SOIL_LOAM)
                moisture = 0.5f + (1.0f - slope) * 0.2f;
            else if (soilType == SOIL_SAND)
                moisture = 0.15f;
            // Rock stays at 0

            Color pixel(
                (float)soilType / 255.0f,
                Clamp(fertility, 0.0f, 1.0f),
                Clamp(mineralBase, 0.0f, 1.0f),
                Clamp(moisture, 0.0f, 1.0f)
            );
            soilImage_->SetPixel(x, z, pixel);
        }
    }

    URHO3D_LOGINFOF("SoilMap: generated %dx%d default soil map", size, size);
}

bool SoilMap::SaveMap(const String& path) const
{
    if (!soilImage_)
        return false;
    return soilImage_->SavePNG(path);
}

SoilType SoilMap::SampleSoilType(int x, int z) const
{
    if (!InBounds(x, z))
        return SOIL_ROCK;
    Color c = soilImage_->GetPixel(x, z);
    return ClassifySoilByte((unsigned char)(c.r_ * 255.0f));
}

unsigned char SoilMap::SampleFertility(int x, int z) const
{
    if (!InBounds(x, z))
        return 0;
    Color c = soilImage_->GetPixel(x, z);
    return (unsigned char)(c.g_ * 255.0f);
}

unsigned char SoilMap::SampleMineralDensity(int x, int z) const
{
    if (!InBounds(x, z))
        return 0;
    Color c = soilImage_->GetPixel(x, z);
    return (unsigned char)(c.b_ * 255.0f);
}

unsigned char SoilMap::SampleMoisture(int x, int z) const
{
    if (!InBounds(x, z))
        return 0;
    Color c = soilImage_->GetPixel(x, z);
    return (unsigned char)(c.a_ * 255.0f);
}
