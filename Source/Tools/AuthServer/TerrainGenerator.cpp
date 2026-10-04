// TerrainGenerator.cpp — Procedural terrain heightmap generation
// MIT License

#include "TerrainGenerator.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/Log.h>

#include <cmath>
#include <cstring>
#include <algorithm>

SharedPtr<Image> TerrainGenerator::Generate(Context* context) const
{
    return GenerateWithEdges(context, nullptr, nullptr, nullptr, nullptr);
}

SharedPtr<Image> TerrainGenerator::GenerateWithEdges(Context* context,
    const float* northEdge, const float* southEdge,
    const float* westEdge, const float* eastEdge) const
{
    int res = params.resolution;
    int total = res * res;

    // Allocate float height buffer
    float* heights = new float[total];

    // Step 1: Generate noise
    PerlinNoise noise(params.seed);
    GenerateNoise(heights, res, noise);

    // Step 2: Terrace
    if (params.terraceSteps > 0)
        ApplyTerrace(heights, res, params.terraceSteps);

    // Step 3: Island mask
    if (params.islandFalloff > 0.0f)
        ApplyIslandMask(heights, res, params.islandFalloff);

    // Step 4: Normalize to [0, 1]
    Normalize(heights, res);

    // Step 5: Height gamma — lowers terrain mean, creating valleys and water areas.
    // Ridged noise skews the distribution high; gamma >1 corrects this so ~15-20%
    // of terrain sits below the water threshold, matching natural coastline coverage.
    if (params.heightGamma != 1.0f && params.heightGamma > 0.0f)
    {
        int total = res * res;
        for (int i = 0; i < total; ++i)
            heights[i] = powf(heights[i], params.heightGamma);
    }

    // Step 6: Erosion (operates on normalized [0,1] data)
    if (params.erosionPasses > 0)
        ApplyErosion(heights, res, params.erosionPasses, params.erosionStrength, params.erosionDeposition);

    // Step 7: Edge blending (after erosion so edges stay matched)
    if (northEdge || southEdge || westEdge || eastEdge)
        BlendEdges(heights, res, northEdge, southEdge, westEdge, eastEdge);

    // Step 8: Water floor
    if (params.waterLevel > 0.0f)
        ApplyWaterFloor(heights, res, params.waterLevel);

    // Convert to Image
    SharedPtr<Image> image = HeightsToImage(context, heights, res);

    delete[] heights;

    URHO3D_LOGINFOF("TerrainGenerator: Generated %dx%d heightmap (seed=%u, octaves=%d, ridgeW=%.2f, erosion=%d)",
        res, res, params.seed, params.octaves, params.ridgeWeight, params.erosionPasses);

    return image;
}

void TerrainGenerator::GenerateNoise(float* heights, int res, const PerlinNoise& noise) const
{
    float freq = params.baseFrequency;
    float ridgeW = params.ridgeWeight;
    float perlinW = 1.0f - ridgeW;
    float scale = params.heightScale;

    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            float nx = (float)x * freq;
            float nz = (float)z * freq;

            float perlin = noise.OctaveNoise(nx, nz, params.octaves, params.persistence, params.lacunarity);
            // Map from [-1,1] to [0,1]
            perlin = (perlin + 1.0f) * 0.5f;

            float ridged = noise.RidgedNoise(nx, nz, params.octaves, params.persistence, params.lacunarity);
            // Already [0,1]

            float h = (perlinW * perlin + ridgeW * ridged) * scale;
            heights[z * res + x] = h;
        }
    }
}

void TerrainGenerator::ApplyTerrace(float* heights, int res, int steps) const
{
    float fSteps = (float)steps;
    int total = res * res;
    for (int i = 0; i < total; ++i)
    {
        heights[i] = floorf(heights[i] * fSteps) / fSteps;
    }
}

void TerrainGenerator::ApplyIslandMask(float* heights, int res, float strength) const
{
    float cx = (float)(res - 1) * 0.5f;
    float cz = (float)(res - 1) * 0.5f;
    float maxDist = sqrtf(cx * cx + cz * cz);

    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            float dx = (float)x - cx;
            float dz = (float)z - cz;
            float dist = sqrtf(dx * dx + dz * dz) / maxDist;  // [0, 1]

            // Smooth falloff: 1 at center, 0 at edges
            float mask = 1.0f - powf(dist, strength);
            if (mask < 0.0f) mask = 0.0f;

            heights[z * res + x] *= mask;
        }
    }
}

void TerrainGenerator::Normalize(float* heights, int res) const
{
    int total = res * res;
    float minH = heights[0];
    float maxH = heights[0];

    for (int i = 1; i < total; ++i)
    {
        if (heights[i] < minH) minH = heights[i];
        if (heights[i] > maxH) maxH = heights[i];
    }

    float range = maxH - minH;
    if (range < 0.0001f) range = 1.0f;

    for (int i = 0; i < total; ++i)
        heights[i] = (heights[i] - minH) / range;
}

void TerrainGenerator::ApplyErosion(float* heights, int res, int passes, float strength, float deposition) const
{
    // CPU hydraulic erosion — simplified droplet simulation
    // Each pass: rain drops, flow downhill, pick up sediment, deposit when slowing

    int total = res * res;
    float* water = new float[total];
    float* sediment = new float[total];
    float* tmpH = new float[total];

    for (int pass = 0; pass < passes; ++pass)
    {
        // Reset water and sediment
        memset(water, 0, total * sizeof(float));
        memset(sediment, 0, total * sizeof(float));

        // Rainfall — uniform light rain
        float rainfall = 0.01f;
        for (int i = 0; i < total; ++i)
            water[i] = rainfall;

        // Simulate flow (4 iterations per pass for stability)
        for (int iter = 0; iter < 4; ++iter)
        {
            memcpy(tmpH, heights, total * sizeof(float));

            for (int z = 1; z < res - 1; ++z)
            {
                for (int x = 1; x < res - 1; ++x)
                {
                    int idx = z * res + x;
                    float h = heights[idx] + water[idx];

                    // Find lowest neighbour
                    int neighbours[4] = {
                        idx - res,      // north
                        idx + res,      // south
                        idx - 1,        // west
                        idx + 1         // east
                    };

                    float lowestH = h;
                    int lowestIdx = -1;
                    for (int n = 0; n < 4; ++n)
                    {
                        float nh = heights[neighbours[n]] + water[neighbours[n]];
                        if (nh < lowestH)
                        {
                            lowestH = nh;
                            lowestIdx = neighbours[n];
                        }
                    }

                    if (lowestIdx >= 0)
                    {
                        float diff = h - lowestH;
                        float flow = diff * 0.25f;  // Transfer fraction
                        if (flow > water[idx]) flow = water[idx];

                        water[idx] -= flow;
                        water[lowestIdx] += flow;

                        // Erosion: pick up sediment proportional to flow speed
                        float erode = flow * strength;
                        if (erode > tmpH[idx]) erode = tmpH[idx] * 0.1f;
                        tmpH[idx] -= erode;
                        sediment[idx] += erode;

                        // Move sediment downstream
                        float sedFlow = sediment[idx] * 0.5f;
                        sediment[idx] -= sedFlow;
                        sediment[lowestIdx] += sedFlow;
                    }
                    else
                    {
                        // No downhill — deposit sediment
                        float deposit = sediment[idx] * deposition;
                        tmpH[idx] += deposit;
                        sediment[idx] -= deposit;
                    }
                }
            }

            memcpy(heights, tmpH, total * sizeof(float));
        }

        // Deposit remaining sediment
        for (int i = 0; i < total; ++i)
            heights[i] += sediment[i] * deposition;
    }

    delete[] water;
    delete[] sediment;
    delete[] tmpH;
}

void TerrainGenerator::ApplyWaterFloor(float* heights, int res, float level) const
{
    int total = res * res;
    for (int i = 0; i < total; ++i)
    {
        if (heights[i] < level)
            heights[i] = level;
    }
}

void TerrainGenerator::BlendEdges(float* heights, int res,
    const float* northEdge, const float* southEdge,
    const float* westEdge, const float* eastEdge) const
{
    // Blend zone width in pixels
    const int blendWidth = 16;

    // North edge (z = 0): blend our row 0..blendWidth with neighbour's edge
    if (northEdge)
    {
        for (int z = 0; z < blendWidth; ++z)
        {
            float t = (float)z / (float)blendWidth;  // 0 at edge, 1 at interior
            for (int x = 0; x < res; ++x)
            {
                int idx = z * res + x;
                heights[idx] = northEdge[x] * (1.0f - t) + heights[idx] * t;
            }
        }
    }

    // South edge (z = res-1): blend our row (res-1-blendWidth)..res-1
    if (southEdge)
    {
        for (int z = 0; z < blendWidth; ++z)
        {
            float t = (float)z / (float)blendWidth;
            int row = res - 1 - z;
            for (int x = 0; x < res; ++x)
            {
                int idx = row * res + x;
                heights[idx] = southEdge[x] * (1.0f - t) + heights[idx] * t;
            }
        }
    }

    // West edge (x = 0): blend our col 0..blendWidth
    if (westEdge)
    {
        for (int x = 0; x < blendWidth; ++x)
        {
            float t = (float)x / (float)blendWidth;
            for (int z = 0; z < res; ++z)
            {
                int idx = z * res + x;
                heights[idx] = westEdge[z] * (1.0f - t) + heights[idx] * t;
            }
        }
    }

    // East edge (x = res-1): blend our col (res-1-blendWidth)..res-1
    if (eastEdge)
    {
        for (int x = 0; x < blendWidth; ++x)
        {
            float t = (float)x / (float)blendWidth;
            int col = res - 1 - x;
            for (int z = 0; z < res; ++z)
            {
                int idx = z * res + col;
                heights[idx] = eastEdge[z] * (1.0f - t) + heights[idx] * t;
            }
        }
    }
}

SharedPtr<Image> TerrainGenerator::HeightsToImage(Context* context, const float* heights, int res) const
{
    SharedPtr<Image> image(new Image(context));
    image->SetSize(res, res, 4);  // RGBA for 32-bit heightmap precision

    unsigned char* data = image->GetData();
    int total = res * res;

    for (int i = 0; i < total; ++i)
    {
        float h = heights[i];
        if (h < 0.0f) h = 0.0f;
        if (h > 1.0f) h = 1.0f;

        // Encode height as 4-channel RGBA for maximum precision
        // R = integer part, G = first fractional byte, B = second, A = third
        float scaled = h * 255.0f;
        unsigned char r = (unsigned char)scaled;
        float rem1 = (scaled - (float)r) * 256.0f;
        unsigned char g = (unsigned char)rem1;
        float rem2 = (rem1 - (float)g) * 256.0f;
        unsigned char b = (unsigned char)rem2;
        float rem3 = (rem2 - (float)b) * 256.0f;
        unsigned char a = (unsigned char)rem3;

        int idx = i * 4;
        data[idx] = r;
        data[idx + 1] = g;
        data[idx + 2] = b;
        data[idx + 3] = a;
    }

    return image;
}
