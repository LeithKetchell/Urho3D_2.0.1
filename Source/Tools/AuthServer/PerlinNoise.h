// PerlinNoise.h — Header-only Perlin noise implementation
// Based on Ken Perlin's improved noise (2002)
// MIT License

#pragma once

#include <cmath>
#include <cstring>

class PerlinNoise
{
public:
    PerlinNoise() { SetSeed(0); }
    explicit PerlinNoise(unsigned seed) { SetSeed(seed); }

    /// Reshuffle permutation table with a new seed.
    void SetSeed(unsigned seed)
    {
        // Initialize with standard permutation
        for (int i = 0; i < 256; ++i)
            p_[i] = i;

        // Fisher-Yates shuffle seeded by a simple LCG
        unsigned s = seed;
        for (int i = 255; i > 0; --i)
        {
            s = s * 1664525u + 1013904223u;  // Knuth LCG
            int j = (int)((s >> 16) % (unsigned)(i + 1));
            int tmp = p_[i];
            p_[i] = p_[j];
            p_[j] = tmp;
        }

        // Duplicate for wrapping
        for (int i = 0; i < 256; ++i)
            p_[256 + i] = p_[i];
    }

    /// Single 2D Perlin noise sample, returns approximately [-1, 1].
    float Noise2D(float x, float y) const
    {
        // Find unit grid cell
        int xi = FloorInt(x);
        int yi = FloorInt(y);

        // Relative position within cell
        float xf = x - (float)xi;
        float yf = y - (float)yi;

        // Wrap to 0-255
        xi &= 255;
        yi &= 255;

        // Fade curves
        float u = Fade(xf);
        float v = Fade(yf);

        // Hash corners
        int aa = p_[p_[xi] + yi];
        int ab = p_[p_[xi] + yi + 1];
        int ba = p_[p_[xi + 1] + yi];
        int bb = p_[p_[xi + 1] + yi + 1];

        // Gradient dot products and bilinear interpolation
        float x1 = Lerp(Grad(aa, xf, yf), Grad(ba, xf - 1.0f, yf), u);
        float x2 = Lerp(Grad(ab, xf, yf - 1.0f), Grad(bb, xf - 1.0f, yf - 1.0f), u);

        return Lerp(x1, x2, v);
    }

    /// Fractal Brownian motion — layered octaves of Perlin noise.
    float OctaveNoise(float x, float y, int octaves, float persistence = 0.5f, float lacunarity = 2.0f) const
    {
        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxAmplitude = 0.0f;

        for (int i = 0; i < octaves; ++i)
        {
            total += Noise2D(x * frequency, y * frequency) * amplitude;
            maxAmplitude += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return total / maxAmplitude;  // Normalize to [-1, 1]
    }

    /// Ridged multifractal noise — sharp ridges and peaks.
    float RidgedNoise(float x, float y, int octaves, float persistence = 0.5f, float lacunarity = 2.0f) const
    {
        float total = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxAmplitude = 0.0f;

        for (int i = 0; i < octaves; ++i)
        {
            float n = Noise2D(x * frequency, y * frequency);
            n = 1.0f - fabsf(n);  // Create ridges
            n = n * n;             // Sharpen
            total += n * amplitude;
            maxAmplitude += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return total / maxAmplitude;  // Normalize to [0, 1]
    }

    /// Terraced noise — quantized to discrete levels for plateaus.
    float TerracedNoise(float x, float y, int octaves, int steps, float persistence = 0.5f, float lacunarity = 2.0f) const
    {
        float n = OctaveNoise(x, y, octaves, persistence, lacunarity);
        // Map from [-1,1] to [0,1]
        n = (n + 1.0f) * 0.5f;
        // Quantize
        n = floorf(n * (float)steps) / (float)steps;
        return n;  // Returns [0, 1)
    }

private:
    int p_[512];

    static int FloorInt(float x)
    {
        int xi = (int)x;
        return (x < (float)xi) ? xi - 1 : xi;
    }

    static float Fade(float t)
    {
        // 6t^5 - 15t^4 + 10t^3
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    static float Lerp(float a, float b, float t)
    {
        return a + t * (b - a);
    }

    /// Gradient function — dot product of pseudorandom gradient with distance vector.
    static float Grad(int hash, float x, float y)
    {
        // Use 4 gradient directions for 2D
        int h = hash & 3;
        switch (h)
        {
        case 0: return  x + y;
        case 1: return -x + y;
        case 2: return  x - y;
        case 3: return -x - y;
        }
        return 0.0f;
    }
};
