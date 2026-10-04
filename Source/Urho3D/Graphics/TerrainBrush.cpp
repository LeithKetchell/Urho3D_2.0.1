// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "TerrainBrush.h"
#include "../Math/MathDefs.h"
#include "../IO/Log.h"

namespace Urho3D
{

TerrainBrush::TerrainBrush(Context* context) :
    Object(context)
{
}

void TerrainBrush::SetTerrain(Terrain* terrain, Image* heightMap)
{
    terrain_ = terrain;
    heightMap_ = heightMap;
}

void TerrainBrush::SetWaterMap(Image* waterMap)
{
    waterMap_ = waterMap;
}

float TerrainBrush::ReadH(int px, int py) const
{
    unsigned char* data = heightMap_->GetData();
    int comps = heightMap_->GetComponents();
    int hmW = heightMap_->GetWidth();
    int idx = (py * hmW + px) * comps;
    return ((float)data[idx] + (float)data[idx + 1] / 256.0f
            + (float)data[idx + 2] / 65536.0f + (float)data[idx + 3] / 16777216.0f) / 255.0f;
}

void TerrainBrush::WriteH(int px, int py, float h)
{
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    unsigned char* data = heightMap_->GetData();
    int comps = heightMap_->GetComponents();
    int hmW = heightMap_->GetWidth();
    float scaled = h * 255.0f;
    int idx = (py * hmW + px) * comps;
    data[idx] = (unsigned char)scaled;
    float rem = (scaled - (float)data[idx]) * 256.0f;
    data[idx + 1] = (unsigned char)rem;
    float rem2 = (rem - (float)data[idx + 1]) * 256.0f;
    data[idx + 2] = (unsigned char)rem2;
    data[idx + 3] = (unsigned char)((rem2 - (float)data[idx + 2]) * 256.0f);
}

bool TerrainBrush::Apply(const Vector3& worldPos, float timeStep)
{
    if (!terrain_ || !heightMap_ || mode_ == 0)
        return false;

    IntVector2 center = terrain_->WorldToHeightMap(worldPos);
    int hmW = heightMap_->GetWidth();
    int hmH = heightMap_->GetHeight();
    int radius = (int)radius_;
    float baseStrength = (mode_ == 3) ? smoothStrength_ : strength_;
    float strength = baseStrength * timeStep * 2.5f;

    float flattenHeight = 0.0f;
    if (mode_ == 4)
    {
        if (flattenHeight_ < 0.0f)
        {
            int cx = Clamp(center.x_, 0, hmW - 1);
            int cy = Clamp(center.y_, 0, hmH - 1);
            flattenHeight_ = ReadH(cx, cy);
        }
        flattenHeight = flattenHeight_;
    }

    bool modified = false;

    for (int dz = -radius; dz <= radius; ++dz)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            float falloff = ShapeFalloff((float)dx, (float)dz, radius_);
            if (falloff < 0.0f)
                continue;

            int px = center.x_ + dx;
            int py = center.y_ + dz;
            if (px < 0 || px >= hmW || py < 0 || py >= hmH)
                continue;

            float h = ReadH(px, py);

            switch (mode_)
            {
            case 1: // Raise
                h += strength * falloff;
                break;

            case 2: // Lower
                h -= strength * falloff;
                break;

            case 3: // Smooth — average with neighbours
            {
                float sum = 0.0f;
                int count = 0;
                for (int ny = -1; ny <= 1; ++ny)
                {
                    for (int nx = -1; nx <= 1; ++nx)
                    {
                        int npx = px + nx, npy = py + ny;
                        if (npx >= 0 && npx < hmW && npy >= 0 && npy < hmH)
                        {
                            sum += ReadH(npx, npy);
                            ++count;
                        }
                    }
                }
                float avg = sum / (float)count;
                h = h + (avg - h) * falloff * baseStrength;
                break;
            }

            case 4: // Flatten
                h = h + (flattenHeight - h) * falloff * baseStrength;
                break;

            case 5: // Erosion — move material downhill to lowest neighbor
            {
                float lowest = h;
                int lx = px, ly = py;
                for (int ny = -1; ny <= 1; ++ny)
                {
                    for (int nx = -1; nx <= 1; ++nx)
                    {
                        if (nx == 0 && ny == 0) continue;
                        int npx = px + nx, npy = py + ny;
                        if (npx < 0 || npx >= hmW || npy < 0 || npy >= hmH) continue;
                        float nh = ReadH(npx, npy);
                        if (nh < lowest) { lowest = nh; lx = npx; ly = npy; }
                    }
                }
                float diff = h - lowest;
                if (diff > 0.0001f)
                {
                    float transfer = diff * falloff * baseStrength;
                    h -= transfer;
                    WriteH(lx, ly, lowest + transfer);
                }
                break;
            }

            case 6: // River — carve terrain + paint water map + bank erosion
            {
                // 1. Carve the channel
                h -= riverCarveDepth_ * falloff;

                // 2. Paint water map (increment water channel, cap at 255)
                if (waterMap_)
                {
                    unsigned char* wdata = waterMap_->GetData();
                    int wIdx = py * hmW + px;  // single-channel
                    int wval = (int)wdata[wIdx] + (int)(falloff * 30.0f);
                    if (wval > 255) wval = 255;
                    wdata[wIdx] = (unsigned char)wval;
                }

                // 3. Bank erosion at edges (falloff 0.2–0.8): smooth with neighbours
                if (falloff >= 0.2f && falloff <= 0.8f)
                {
                    float sum = 0.0f;
                    int count = 0;
                    for (int ny = -1; ny <= 1; ++ny)
                    {
                        for (int nx = -1; nx <= 1; ++nx)
                        {
                            int npx = px + nx, npy = py + ny;
                            if (npx >= 0 && npx < hmW && npy >= 0 && npy < hmH)
                            {
                                sum += ReadH(npx, npy);
                                ++count;
                            }
                        }
                    }
                    float avg = sum / (float)count;
                    float edgeWeight = 1.0f - Abs(falloff - 0.5f) / 0.3f;
                    edgeWeight = Clamp(edgeWeight, 0.0f, 1.0f);
                    h = h + (avg - h) * edgeWeight * 0.3f;
                }
                break;
            }
            }

            WriteH(px, py, h);
            modified = true;
        }
    }

    if (modified)
        terrain_->ApplyHeightMap();

    return modified;
}

float TerrainBrush::ShapeFalloff(float dx, float dz, float radius) const
{
    if (radius < 0.001f)
        return -1.0f;

    // Apply inverse rotation to test point
    if (rotation_ != 0.0f)
    {
        float ra = -rotation_ * M_DEGTORAD;
        float cs = cosf(ra), sn = sinf(ra);
        float rdx = dx * cs - dz * sn;
        float rdz = dx * sn + dz * cs;
        dx = rdx;
        dz = rdz;
    }

    // Circle
    if (shape_ == 0)
    {
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > radius) return -1.0f;
        return 1.0f - dist / radius;
    }

    // Square
    if (shape_ == 1)
    {
        float adx = fabsf(dx), adz = fabsf(dz);
        if (adx > radius || adz > radius) return -1.0f;
        float edgeDist = Max(adx, adz);
        return 1.0f - edgeDist / radius;
    }

    // All other shapes: build vertex list, test point-in-polygon
    float vx[20], vz[20];
    int nv = 0;

    switch (shape_)
    {
    case 2: // Triangle (point-up)
    {
        float angles[3] = {90.0f * M_DEGTORAD, 210.0f * M_DEGTORAD, 330.0f * M_DEGTORAD};
        for (int i = 0; i < 3; ++i)
        {
            vx[i] = radius * cosf(angles[i]);
            vz[i] = radius * sinf(angles[i]);
        }
        nv = 3;
        break;
    }
    case 3: // Star (5-pointed, 10 vertices)
    {
        float innerR = radius * 0.38f;
        for (int i = 0; i < 10; ++i)
        {
            float a = (float)(M_PI / 2.0) + (float)i / 10.0f * 2.0f * (float)M_PI;
            float r = (i % 2 == 0) ? radius : innerR;
            vx[i] = r * cosf(a);
            vz[i] = r * sinf(a);
        }
        nv = 10;
        break;
    }
    default: // 4=pentagon(5), 5=hexagon(6), 6=octagon(8)
    {
        int sides;
        switch (shape_) { case 4: sides = 5; break; case 5: sides = 6; break; default: sides = 8; break; }
        for (int i = 0; i < sides; ++i)
        {
            float a = (float)(M_PI / 2.0) + (float)i / (float)sides * 2.0f * (float)M_PI;
            vx[i] = radius * cosf(a);
            vz[i] = radius * sinf(a);
        }
        nv = sides;
        break;
    }
    }

    // Point-in-polygon (ray casting / crossing number)
    int crossings = 0;
    for (int i = 0; i < nv; ++i)
    {
        int j = (i + 1) % nv;
        if ((vz[i] <= dz && vz[j] > dz) || (vz[j] <= dz && vz[i] > dz))
        {
            float t = (dz - vz[i]) / (vz[j] - vz[i]);
            if (dx < vx[i] + t * (vx[j] - vx[i]))
                crossings++;
        }
    }
    if ((crossings & 1) == 0)
        return -1.0f;

    // Falloff: distance to nearest edge, normalized
    float minDist = 1e9f;
    for (int i = 0; i < nv; ++i)
    {
        int j = (i + 1) % nv;
        float ex = vx[j] - vx[i], ez = vz[j] - vz[i];
        float len = sqrtf(ex * ex + ez * ez);
        if (len < 0.001f) continue;
        float d = fabsf((dx - vx[i]) * (-ez / len) + (dz - vz[i]) * (ex / len));
        if (d < minDist) minDist = d;
    }
    float inradius = radius * 0.4f;
    return Clamp(minDist / inradius, 0.0f, 1.0f);
}

SharedPtr<Image> TerrainBrush::CloneHeightMap() const
{
    if (!heightMap_)
        return SharedPtr<Image>();

    SharedPtr<Image> clone(new Image(context_));
    clone->SetSize(heightMap_->GetWidth(), heightMap_->GetHeight(), heightMap_->GetComponents());
    memcpy(clone->GetData(), heightMap_->GetData(),
           heightMap_->GetWidth() * heightMap_->GetHeight() * heightMap_->GetComponents());
    return clone;
}

void TerrainBrush::RestoreHeightMap(Image* src)
{
    if (!src || !heightMap_ || !terrain_)
        return;

    memcpy(heightMap_->GetData(), src->GetData(),
           src->GetWidth() * src->GetHeight() * src->GetComponents());
    terrain_->ApplyHeightMap();
}

SharedPtr<Image> TerrainBrush::CloneWaterMap() const
{
    if (!waterMap_)
        return SharedPtr<Image>();

    SharedPtr<Image> clone(new Image(context_));
    clone->SetSize(waterMap_->GetWidth(), waterMap_->GetHeight(), waterMap_->GetComponents());
    memcpy(clone->GetData(), waterMap_->GetData(),
           waterMap_->GetWidth() * waterMap_->GetHeight() * waterMap_->GetComponents());
    return clone;
}

void TerrainBrush::RestoreWaterMap(Image* src)
{
    if (!src || !waterMap_)
        return;

    memcpy(waterMap_->GetData(), src->GetData(),
           src->GetWidth() * src->GetHeight() * src->GetComponents());
}

}
