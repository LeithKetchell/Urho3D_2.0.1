// EcosystemManager — Phases 1-3: vegetation map, moisture-driven growth, soil layer.

#include "EcosystemManager.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Math/Random.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>

EcosystemManager::EcosystemManager(Context* context)
    : Object(context)
{
}

void EcosystemManager::Initialize(Terrain* terrain, float waterLevel)
{
    if (!terrain)
    {
        URHO3D_LOGERROR("EcosystemManager::Initialize — no terrain");
        return;
    }

    SeedFromHeightmap(terrain, waterLevel);
    SeedSoilFromHeightmap(terrain, waterLevel);

    if (!vegetationImage_)
    {
        URHO3D_LOGERROR("EcosystemManager::Initialize — SeedFromHeightmap failed");
        return;
    }

    // Upload to GPU
    vegetationTex_ = new Texture2D(context_);
    vegetationTex_->SetFilterMode(FILTER_BILINEAR);
    vegetationTex_->SetAddressMode(COORD_U, ADDRESS_CLAMP);
    vegetationTex_->SetAddressMode(COORD_V, ADDRESS_CLAMP);
    vegetationTex_->SetData(vegetationImage_);

    URHO3D_LOGINFOF("EcosystemManager: vegetation map %dx%d generated and uploaded",
                    MAP_SIZE, MAP_SIZE);
}

void EcosystemManager::SeedFromHeightmap(Terrain* terrain, float waterLevel)
{
    vegetationImage_ = new Image(context_);
    vegetationImage_->SetSize(MAP_SIZE, MAP_SIZE, 4);  // RGBA

    // Compute terrain world bounds
    IntVector2 numVerts = terrain->GetNumVertices();
    Vector3 spacing = terrain->GetSpacing();
    Vector3 terrainPos = terrain->GetNode()->GetWorldPosition();
    float totalX = (numVerts.x_ - 1) * spacing.x_;
    float totalZ = (numVerts.y_ - 1) * spacing.z_;

    terrainOriginX_ = terrainPos.x_ - totalX * 0.5f;
    terrainOriginZ_ = terrainPos.z_ - totalZ * 0.5f;
    terrainSizeX_ = totalX;
    terrainSizeZ_ = totalZ;

    for (int py = 0; py < MAP_SIZE; ++py)
    {
        for (int px = 0; px < MAP_SIZE; ++px)
        {
            // Map pixel to world position
            float wx = terrainOriginX_ + (float(px) + 0.5f) / MAP_SIZE * terrainSizeX_;
            float wz = terrainOriginZ_ + (float(py) + 0.5f) / MAP_SIZE * terrainSizeZ_;
            float height = terrain->GetHeight(Vector3(wx, 0.0f, wz));
            Vector3 normal = terrain->GetNormal(Vector3(wx, 0.0f, wz));
            float slope = 1.0f - normal.y_;  // 0 = flat, ~1 = vertical

            float aboveWater = height - waterLevel;

            unsigned char grassDensity = 0;
            unsigned char shrubDensity = 0;
            unsigned char treeMaturity = 0;
            unsigned char biomeType = ECO_BARREN;

            if (aboveWater < 0.0f)
            {
                // Underwater — no land vegetation
                biomeType = ECO_BARREN;
            }
            else if (slope > 0.6f)
            {
                // Steep cliff — sparse grass at best
                biomeType = ECO_BARREN;
                grassDensity = (unsigned char)(30.0f * Max(0.0f, 1.0f - slope));
            }
            else if (aboveWater > 80.0f)
            {
                // High altitude — alpine thinning
                biomeType = ECO_ALPINE;
                float fade = Clamp(1.0f - (aboveWater - 80.0f) / 40.0f, 0.0f, 1.0f);
                grassDensity = (unsigned char)(80.0f * fade);
            }
            else if (aboveWater < 5.0f && slope < 0.2f)
            {
                // Low flat near water — wetland
                biomeType = ECO_WETLAND;
                grassDensity = 150;
                shrubDensity = 60;
            }
            else if (aboveWater < 30.0f && slope < 0.3f)
            {
                // Low/mid altitude, gentle slope — forest
                biomeType = ECO_FOREST;
                grassDensity = 120;
                shrubDensity = 80;
                treeMaturity = 200;
            }
            else if (slope < 0.4f)
            {
                // Mid altitude, moderate slope — grassland
                biomeType = ECO_GRASSLAND;
                grassDensity = 200;
                shrubDensity = 30;
            }
            else
            {
                // Rocky slope — sparse
                biomeType = ECO_BARREN;
                grassDensity = (unsigned char)(60.0f * Max(0.0f, 1.0f - slope * 1.5f));
            }

            vegetationImage_->SetPixel(px, py, Color(
                grassDensity / 255.0f,
                shrubDensity / 255.0f,
                treeMaturity / 255.0f,
                biomeType / 255.0f
            ));
        }
    }
}

// ============================================================================
// Phase 3: Soil Layer
// ============================================================================

void EcosystemManager::SeedSoilFromHeightmap(Terrain* terrain, float waterLevel)
{
    soilImage_ = new Image(context_);
    soilImage_->SetSize(MAP_SIZE, MAP_SIZE, 4);  // RGBA

    for (int py = 0; py < MAP_SIZE; ++py)
    {
        for (int px = 0; px < MAP_SIZE; ++px)
        {
            float wx = terrainOriginX_ + (float(px) + 0.5f) / MAP_SIZE * terrainSizeX_;
            float wz = terrainOriginZ_ + (float(py) + 0.5f) / MAP_SIZE * terrainSizeZ_;
            float height = terrain->GetHeight(Vector3(wx, 0.0f, wz));
            Vector3 normal = terrain->GetNormal(Vector3(wx, 0.0f, wz));
            float slope = 1.0f - normal.y_;
            float aboveWater = height - waterLevel;

            // R: Fertility — high everywhere except steep/rocky/underwater
            unsigned char fertility = 220;
            if (aboveWater < 0.0f)
                fertility = 0;
            else if (slope > 0.5f)
                fertility = (unsigned char)(60.0f * Max(0.0f, 1.0f - slope));
            else if (aboveWater > 80.0f)
                fertility = (unsigned char)(120.0f * Clamp(1.0f - (aboveWater - 80.0f) / 40.0f, 0.0f, 1.0f));

            // G: Composition — geological, derived from altitude
            // Low + flat = clay (255), mid = loam (128), high/steep = sand (0)
            unsigned char composition = 128;  // default loam
            if (aboveWater < 10.0f && slope < 0.2f)
                composition = 200;  // clay near water
            else if (aboveWater > 60.0f || slope > 0.4f)
                composition = 50;   // sandy/rocky at height

            // B: Erosion state — starts as deep topsoil, less on steep/high
            unsigned char erosion = 230;
            if (slope > 0.4f)
                erosion = (unsigned char)(100.0f * Max(0.0f, 1.0f - slope));
            else if (aboveWater < 0.0f)
                erosion = 0;

            // A: Trampling — starts pristine
            unsigned char trampling = 0;

            soilImage_->SetPixel(px, py, Color(
                fertility / 255.0f,
                composition / 255.0f,
                erosion / 255.0f,
                trampling / 255.0f
            ));
        }
    }

    URHO3D_LOGINFOF("EcosystemManager: soil map %dx%d seeded", MAP_SIZE, MAP_SIZE);
}

float EcosystemManager::SampleFertility(float worldX, float worldZ) const
{
    if (!soilImage_) return 0.8f;
    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0) return 0.0f;
    return soilImage_->GetPixel(p.x_, p.y_).r_;
}

unsigned char EcosystemManager::SampleComposition(float worldX, float worldZ) const
{
    if (!soilImage_) return 128;
    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0) return 128;
    return (unsigned char)(soilImage_->GetPixel(p.x_, p.y_).g_ * 255.0f + 0.5f);
}

unsigned char EcosystemManager::SampleErosion(float worldX, float worldZ) const
{
    if (!soilImage_) return 200;
    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0) return 0;
    return (unsigned char)(soilImage_->GetPixel(p.x_, p.y_).b_ * 255.0f + 0.5f);
}

unsigned char EcosystemManager::SampleTrampling(float worldX, float worldZ) const
{
    if (!soilImage_) return 0;
    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0) return 0;
    return (unsigned char)(soilImage_->GetPixel(p.x_, p.y_).a_ * 255.0f + 0.5f);
}

void EcosystemManager::RecordFootstep(float worldX, float worldZ)
{
    if (!soilImage_) return;
    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0) return;

    Color c = soilImage_->GetPixel(p.x_, p.y_);
    c.a_ = Min(c.a_ + 2.0f / 255.0f, 1.0f);  // increment trampling
    soilImage_->SetPixel(p.x_, p.y_, c);
}

void EcosystemManager::DegradeFertility(float worldX, float worldZ, float amount)
{
    if (!soilImage_) return;
    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0) return;

    Color c = soilImage_->GetPixel(p.x_, p.y_);
    c.r_ = Max(c.r_ - amount / 255.0f, 0.0f);
    soilImage_->SetPixel(p.x_, p.y_, c);
}

void EcosystemManager::UpdateSoil()
{
    if (!soilImage_) return;

    unsigned char* data = soilImage_->GetData();

    for (int y = 0; y < MAP_SIZE; ++y)
    {
        for (int x = 0; x < MAP_SIZE; ++x)
        {
            int idx = (y * MAP_SIZE + x) * 4;

            // Trampling recovery: slowly returns to pristine
            float trampling = (float)data[idx + 3];
            if (trampling > 0.0f)
            {
                trampling = Max(trampling - 0.1f, 0.0f);
                data[idx + 3] = (unsigned char)trampling;
            }

            // Erosion: surface water + no vegetation = erosion
            float erosion = (float)data[idx + 2];
            if (rainfallMap_ && rainfallMapSize_ > 0)
            {
                int rx = x * rainfallMapSize_ / MAP_SIZE;
                int ry = y * rainfallMapSize_ / MAP_SIZE;
                rx = Clamp(rx, 0, rainfallMapSize_ - 1);
                ry = Clamp(ry, 0, rainfallMapSize_ - 1);
                int rainIdx = (ry * rainfallMapSize_ + rx) * 4;
                float surfaceWater = (float)rainfallMap_->GetData()[rainIdx] / 255.0f;

                // Check vegetation cover at this point
                float vegCover = 0.0f;
                if (vegetationImage_)
                {
                    int vegIdx = (y * MAP_SIZE + x) * 4;
                    vegCover = (float)vegetationImage_->GetData()[vegIdx] / 255.0f;
                }

                // High water + low vegetation = erosion
                if (surfaceWater > 0.3f && vegCover < 0.3f)
                {
                    float erosionRate = surfaceWater * (1.0f - vegCover) * 0.2f;
                    erosion = Max(erosion - erosionRate, 0.0f);
                    data[idx + 2] = (unsigned char)erosion;

                    // Erosion also degrades fertility
                    float fertility = (float)data[idx];
                    fertility = Max(fertility - erosionRate * 0.5f, 0.0f);
                    data[idx] = (unsigned char)fertility;
                }
            }
        }
    }
}

void EcosystemManager::UpdateGrowth()
{
    if (!vegetationImage_ || !rainfallMap_ || rainfallMapSize_ <= 0)
        return;

    unsigned char* vegData = vegetationImage_->GetData();
    const unsigned char* rainData = rainfallMap_->GetData();
    int vegStride = MAP_SIZE * 4;
    int rainStride = rainfallMapSize_ * 4;

    // Growth/decay rates per update tick
    float growthRate = 0.5f;   // grass density gain per tick at full moisture
    float decayRate = 0.3f;    // grass density loss per tick at zero moisture

    for (int vy = 0; vy < MAP_SIZE; ++vy)
    {
        for (int vx = 0; vx < MAP_SIZE; ++vx)
        {
            int vegIdx = (vy * MAP_SIZE + vx) * 4;
            unsigned char biome = vegData[vegIdx + 3];

            // Skip barren — nothing grows on rock
            if (biome == ECO_BARREN)
                continue;

            // Sample moisture from rainfall map (different resolution — scale coordinates)
            int rx = vx * rainfallMapSize_ / MAP_SIZE;
            int ry = vy * rainfallMapSize_ / MAP_SIZE;
            rx = Clamp(rx, 0, rainfallMapSize_ - 1);
            ry = Clamp(ry, 0, rainfallMapSize_ - 1);
            int rainIdx = (ry * rainfallMapSize_ + rx) * 4;
            float soilMoisture = (float)rainData[rainIdx + 1] / 255.0f;  // G channel

            float grassDensity = (float)vegData[vegIdx];      // R (0-255 raw byte)
            float shrubDensity = (float)vegData[vegIdx + 1];  // G

            // Phase 3: soil fertility and trampling modulate growth
            float fertilityFactor = 1.0f;
            float tramplingPenalty = 0.0f;
            if (soilImage_)
            {
                int soilIdx = (vy * MAP_SIZE + vx) * 4;
                const unsigned char* soilData = soilImage_->GetData();
                fertilityFactor = (float)soilData[soilIdx] / 255.0f;       // R = fertility
                tramplingPenalty = (float)soilData[soilIdx + 3] / 255.0f;  // A = trampling
            }

            // Growth: moisture * fertility * season, suppressed by trampling
            float moistureFactor = soilMoisture;  // 0-1
            float growth = growthRate * moistureFactor * fertilityFactor * seasonMultiplier_
                         * Max(0.0f, 1.0f - tramplingPenalty * 0.8f);

            // Biome caps — maximum density per biome type
            float grassCap = 200.0f;
            float shrubCap = 80.0f;
            if (biome == ECO_FOREST)      { grassCap = 120.0f; shrubCap = 80.0f; }
            else if (biome == ECO_WETLAND) { grassCap = 150.0f; shrubCap = 60.0f; }
            else if (biome == ECO_ALPINE)  { grassCap = 80.0f;  shrubCap = 20.0f; }

            if (growth > 0.0f)
            {
                grassDensity = Min(grassDensity + growth, grassCap);
                shrubDensity = Min(shrubDensity + growth * 0.4f, shrubCap);
            }

            // Decay: no moisture → vegetation dies back
            if (soilMoisture < 0.1f)
            {
                grassDensity = Max(grassDensity - decayRate, 0.0f);
                shrubDensity = Max(shrubDensity - decayRate * 0.3f, 0.0f);
            }

            vegData[vegIdx]     = (unsigned char)grassDensity;
            vegData[vegIdx + 1] = (unsigned char)shrubDensity;
        }
    }

    // Re-upload to GPU
    if (vegetationTex_)
        vegetationTex_->SetData(vegetationImage_);
}

// ============================================================================
// Phase 4: Animal Food System
// ============================================================================

float EcosystemManager::SampleFoodValue(float worldX, float worldZ, float grassWeight, float shrubWeight) const
{
    if (!vegetationImage_)
        return 0.0f;

    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0)
        return 0.0f;

    Color c = vegetationImage_->GetPixel(p.x_, p.y_);
    return c.r_ * grassWeight + c.g_ * shrubWeight;
}

void EcosystemManager::ReduceGrassDensity(float worldX, float worldZ, float amount)
{
    if (!vegetationImage_)
        return;

    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0)
        return;

    Color c = vegetationImage_->GetPixel(p.x_, p.y_);
    c.r_ = Max(c.r_ - amount / 255.0f, 0.0f);
    vegetationImage_->SetPixel(p.x_, p.y_, c);
}

IntVector2 EcosystemManager::WorldToPixel(float worldX, float worldZ) const
{
    if (terrainSizeX_ <= 0.0f || terrainSizeZ_ <= 0.0f)
        return IntVector2(-1, -1);

    int px = (int)((worldX - terrainOriginX_) / terrainSizeX_ * MAP_SIZE);
    int py = (int)((worldZ - terrainOriginZ_) / terrainSizeZ_ * MAP_SIZE);

    if (px < 0 || px >= MAP_SIZE || py < 0 || py >= MAP_SIZE)
        return IntVector2(-1, -1);

    return IntVector2(px, py);
}

float EcosystemManager::SampleGrassDensity(float worldX, float worldZ) const
{
    if (!vegetationImage_)
        return 0.0f;

    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0)
        return 0.0f;

    Color c = vegetationImage_->GetPixel(p.x_, p.y_);
    return c.r_ * seasonMultiplier_;
}

float EcosystemManager::SampleShrubDensity(float worldX, float worldZ) const
{
    if (!vegetationImage_)
        return 0.0f;

    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0)
        return 0.0f;

    return vegetationImage_->GetPixel(p.x_, p.y_).g_;
}

unsigned char EcosystemManager::SampleTreeMaturity(float worldX, float worldZ) const
{
    if (!vegetationImage_)
        return 0;

    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0)
        return 0;

    return (unsigned char)(vegetationImage_->GetPixel(p.x_, p.y_).b_ * 255.0f + 0.5f);
}

EcoBiome EcosystemManager::SampleBiome(float worldX, float worldZ) const
{
    if (!vegetationImage_)
        return ECO_BARREN;

    IntVector2 p = WorldToPixel(worldX, worldZ);
    if (p.x_ < 0)
        return ECO_BARREN;

    unsigned char val = (unsigned char)(vegetationImage_->GetPixel(p.x_, p.y_).a_ * 255.0f + 0.5f);
    if (val > ECO_ALPINE)
        return ECO_BARREN;
    return static_cast<EcoBiome>(val);
}

Texture2D* EcosystemManager::GetOrCreateSoilTexture()
{
    if (soilTex_)
        return soilTex_;
    if (!soilImage_)
        return nullptr;

    soilTex_ = new Texture2D(context_);
    soilTex_->SetFilterMode(FILTER_BILINEAR);
    soilTex_->SetAddressMode(COORD_U, ADDRESS_CLAMP);
    soilTex_->SetAddressMode(COORD_V, ADDRESS_CLAMP);
    soilTex_->SetData(soilImage_);
    return soilTex_;
}

void EcosystemManager::UploadSoilToGPU()
{
    if (soilTex_ && soilImage_)
        soilTex_->SetData(soilImage_);
}
