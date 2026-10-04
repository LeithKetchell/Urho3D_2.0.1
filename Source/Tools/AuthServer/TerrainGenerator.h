// TerrainGenerator.h — Procedural terrain heightmap generation
// Uses Perlin noise with ridged multifractal, terracing, erosion, and island mask
// MIT License

#pragma once

#include "PerlinNoise.h"
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Math/Vector2.h>

using namespace Urho3D;

/// Parameters for terrain heightmap generation.
struct TerrainGenParams
{
    unsigned seed{12345};
    int resolution{1025};           // Must be 2^n + 1
    float baseFrequency{0.004f};    // Low = broad hills, high = choppy
    int octaves{6};                 // Fractal detail layers
    float persistence{0.5f};        // Amplitude decay per octave
    float lacunarity{2.0f};         // Frequency multiplier per octave
    float ridgeWeight{0.3f};        // Blend: 0 = pure Perlin, 1 = pure ridged
    int terraceSteps{0};            // 0 = off, >0 = plateau levels
    int erosionPasses{0};           // 0 = skip, >0 = hydraulic erosion iterations
    float erosionStrength{0.3f};    // Sediment pickup rate
    float erosionDeposition{0.2f};  // Sediment drop rate
    float islandFalloff{0.0f};      // 0 = off, >0 = radial falloff strength
    float waterLevel{0.04f};        // Heights below this become flat water floor
    float heightScale{1.0f};        // Overall height multiplier before normalization
    float heightGamma{1.4f};        // Power curve after normalization — >1 lowers terrain, creating valleys/water
};

/// Generates procedural terrain heightmap images.
class TerrainGenerator
{
public:
    TerrainGenParams params;

    /// Generate a heightmap image with current parameters.
    SharedPtr<Image> Generate(Context* context) const;

    /// Generate with edge constraints from neighbouring terrains.
    /// Each edge array (if non-null) has `resolution` floats [0-1] representing
    /// the neighbour's adjacent edge heights.
    SharedPtr<Image> GenerateWithEdges(Context* context,
        const float* northEdge,   // neighbour's south edge (our z=0 row)
        const float* southEdge,   // neighbour's north edge (our z=max row)
        const float* westEdge,    // neighbour's east edge (our x=0 col)
        const float* eastEdge     // neighbour's west edge (our x=max col)
    ) const;

private:
    /// Fill height buffer with blended Perlin + ridged noise.
    void GenerateNoise(float* heights, int res, const PerlinNoise& noise) const;

    /// Apply terrace quantization.
    void ApplyTerrace(float* heights, int res, int steps) const;

    /// Apply radial island falloff mask.
    void ApplyIslandMask(float* heights, int res, float strength) const;

    /// Apply CPU hydraulic erosion.
    void ApplyErosion(float* heights, int res, int passes, float strength, float deposition) const;

    /// Enforce minimum water level.
    void ApplyWaterFloor(float* heights, int res, float level) const;

    /// Blend edges to match neighbours.
    void BlendEdges(float* heights, int res,
        const float* northEdge, const float* southEdge,
        const float* westEdge, const float* eastEdge) const;

    /// Normalize height buffer to [0, 1].
    void Normalize(float* heights, int res) const;

    /// Convert float buffer to RGBA Image.
    SharedPtr<Image> HeightsToImage(Context* context, const float* heights, int res) const;
};
