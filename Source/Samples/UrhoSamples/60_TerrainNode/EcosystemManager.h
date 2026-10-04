// EcosystemManager — texture-based world database (Phase 1: vegetation map).
// Generates a 1024x1024 RGBA vegetation texture from terrain shape.
// R = grass density, G = shrub density, B = tree maturity, A = biome type.
// Feeds GrassSystem density and provides spatial queries for animals/spawning.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Resource/Image.h>

using namespace Urho3D;

/// Biome classification stored in vegetation map alpha channel.
enum EcoBiome : unsigned char
{
    ECO_BARREN = 0,
    ECO_GRASSLAND = 1,
    ECO_FOREST = 2,
    ECO_WETLAND = 3,
    ECO_ALPINE = 4
};

/// Manages ecosystem texture layers. Phase 1: static vegetation map from heightmap.
class EcosystemManager : public Object
{
    URHO3D_OBJECT(EcosystemManager, Object);

public:
    explicit EcosystemManager(Context* context);

    /// Generate vegetation map from terrain heightmap and upload to GPU.
    void Initialize(Terrain* terrain, float waterLevel);

    /// Get vegetation texture for GPU binding (GrassSystem density source).
    Texture2D* GetVegetationTexture() const { return vegetationTex_; }

    /// Sample grass density at a world position (0.0-1.0), scaled by season.
    float SampleGrassDensity(float worldX, float worldZ) const;

    /// Sample shrub density at a world position (0.0-1.0).
    float SampleShrubDensity(float worldX, float worldZ) const;

    /// Sample tree maturity at a world position (0-255).
    unsigned char SampleTreeMaturity(float worldX, float worldZ) const;

    /// Sample biome type at a world position.
    EcoBiome SampleBiome(float worldX, float worldZ) const;

    /// Set seasonal growth multiplier (0.0 = winter dormant, 1.5 = spring peak).
    void SetSeasonMultiplier(float mult) { seasonMultiplier_ = Clamp(mult, 0.0f, 2.0f); }
    float GetSeasonMultiplier() const { return seasonMultiplier_; }

    /// Phase 2: connect rainfall accumulation map as moisture input.
    /// rainfallMap is RAINFALL_MAP_SIZE x RAINFALL_MAP_SIZE RGBA (R=surface water, G=soil moisture).
    void SetRainfallMap(Image* rainfallMap, int rainfallSize) { rainfallMap_ = rainfallMap; rainfallMapSize_ = rainfallSize; }

    /// Phase 2: update vegetation growth/decay based on current moisture. Call periodically.
    void UpdateGrowth();

    // --- Phase 4: Animal Food ---

    /// Sample food value at a world position with species-specific weights.
    /// grassWeight + shrubWeight should sum to ~1.0.
    float SampleFoodValue(float worldX, float worldZ, float grassWeight, float shrubWeight) const;

    /// Reduce grass density at a position (grazing). Amount in 0-255 scale.
    void ReduceGrassDensity(float worldX, float worldZ, float amount);

    /// Get raw vegetation image for debug/save.
    Image* GetVegetationImage() const { return vegetationImage_; }

    // --- Phase 3: Soil Layer ---
    // R=fertility (0-255), G=composition (fixed), B=erosion (0-255), A=trampling (0-255)

    /// Sample soil fertility at a world position (0.0-1.0).
    float SampleFertility(float worldX, float worldZ) const;

    /// Sample soil composition at a world position (0=sand, 128=loam, 255=clay).
    unsigned char SampleComposition(float worldX, float worldZ) const;

    /// Sample erosion state (0=bedrock, 255=deep topsoil).
    unsigned char SampleErosion(float worldX, float worldZ) const;

    /// Sample trampling (0=pristine, 255=packed dirt).
    unsigned char SampleTrampling(float worldX, float worldZ) const;

    /// Record foot traffic at a world position (increments trampling).
    void RecordFootstep(float worldX, float worldZ);

    /// Degrade fertility at a world position (farming harvest).
    void DegradeFertility(float worldX, float worldZ, float amount);

    /// Update soil dynamics: trampling recovery, erosion from surface water. Call periodically.
    void UpdateSoil();

    /// Get raw soil image for debug/save.
    Image* GetSoilImage() const { return soilImage_; }

    /// Phase 19: upload soil trampling data to GPU texture. Returns texture for shader binding.
    Texture2D* GetOrCreateSoilTexture();
    /// Phase 19: sync soil image data to GPU texture (call after RecordFootstep/UpdateSoil).
    void UploadSoilToGPU();

private:
    /// Seed vegetation data from terrain geometry.
    void SeedFromHeightmap(Terrain* terrain, float waterLevel);

    /// Seed soil data from terrain geometry (Phase 3).
    void SeedSoilFromHeightmap(Terrain* terrain, float waterLevel);

    /// Convert world XZ to pixel coordinates. Returns (-1,-1) if out of bounds.
    IntVector2 WorldToPixel(float worldX, float worldZ) const;

    SharedPtr<Image> vegetationImage_;      ///< CPU-side RGBA data
    SharedPtr<Texture2D> vegetationTex_;    ///< GPU texture for shader binding

    SharedPtr<Image> soilImage_;            ///< Phase 3: soil layer RGBA
    SharedPtr<Texture2D> soilTex_;          ///< Phase 19: GPU soil texture for shader

    float terrainOriginX_{0.0f};
    float terrainOriginZ_{0.0f};
    float terrainSizeX_{1.0f};
    float terrainSizeZ_{1.0f};
    float seasonMultiplier_{1.0f};

    /// Phase 2: rainfall map reference (owned by TerrainNode)
    WeakPtr<Image> rainfallMap_;
    int rainfallMapSize_{0};

    static constexpr int MAP_SIZE = 1024;
};
