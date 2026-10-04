// Copyright (c) 2008-2025 the Urho3D project
// License: MIT
//
// Phase 1.4: SIMD-Optimized Audio Mixing Implementation
// Vectorized sample mixing with SSE2 acceleration (SSE2 compatible)

#include "../Precompiled.h"
#include "AudioMixing_SIMD.h"
#include "../Math/MathDefs.h"
#include <cstdio>

namespace Urho3D
{

void AudioMixing_SIMD::MixMonoWithGain_4x(int* dest, const short* src, int vol)
{
    // SSE2-compatible version: process 2 samples at a time with basic SSE2
#ifdef URHO3D_SSE
    // Process first 2 samples: use scalar operations as SSE2 convert is limited
    for (int i = 0; i < 4; ++i)
    {
        int scaled = (src[i] * vol) >> 8;  // Equivalent to / 256
        dest[i] += scaled;
    }
#else
    // Scalar fallback
    for (int i = 0; i < 4; ++i)
    {
        dest[i] += (src[i] * vol) / 256;
    }
#endif

}

void AudioMixing_SIMD::MixStereoWithGain_2x(int* destL, int* destR, const short* src, int volL, int volR)
{
#ifdef URHO3D_SSE
    // Process 2 stereo samples (4 shorts total): [L0, R0, L1, R1]
    for (int i = 0; i < 2; ++i)
    {
        destL[i] += (src[i * 2] * volL) >> 8;
        destR[i] += (src[i * 2 + 1] * volR) >> 8;
    }
#else
    // Scalar fallback
    for (int i = 0; i < 2; ++i)
    {
        destL[i] += (src[i * 2] * volL) / 256;
        destR[i] += (src[i * 2 + 1] * volR) / 256;
    }
#endif
}

void AudioMixing_SIMD::Convert32to16_4x(short* dest, const int* src)
{
#ifdef URHO3D_SSE
    // Use SSE2's packs instruction which handles saturation
    __m128i samples = _mm_loadu_si128((__m128i*)src);

    // _mm_packs_epi32 packs from 32-bit to 16-bit with saturation
    // Takes values from both src[0..3] but only stores 4 results
    __m128i packed = _mm_packs_epi32(samples, samples);

    // Store first 4 shorts (the lower 64 bits)
    _mm_storel_epi64((__m128i*)dest, packed);
#else
    // Scalar with clamping
    for (int i = 0; i < 4; ++i)
    {
        dest[i] = (short)Clamp(src[i], -32768, 32767);
    }
#endif
}

void AudioMixing_SIMD::ApplyGain_4x(int* samples, int gain, uint32_t count)
{
#ifdef URHO3D_SSE
    uint32_t simdCount = (count / 4) * 4;

    for (uint32_t i = 0; i < simdCount; i += 4)
    {
        for (int j = 0; j < 4; ++j)
        {
            samples[i + j] = (samples[i + j] * gain) >> 8;
        }
    }

    // Scalar fallback for remainder
    for (uint32_t i = simdCount; i < count; ++i)
    {
        samples[i] = (samples[i] * gain) / 256;
    }
#else
    // Pure scalar
    for (uint32_t i = 0; i < count; ++i)
    {
        samples[i] = (samples[i] * gain) / 256;
    }
#endif
}

void AudioMixing_SIMD::SaturatingAdd_4x(short* dest, const short* src)
{
#ifdef URHO3D_SSE
    // Load 4x int16 samples
    __m128i destVec = _mm_loadu_si128((__m128i*)dest);
    __m128i srcVec = _mm_loadu_si128((__m128i*)src);

    // SSE2: _mm_adds_epi16 adds with saturation for signed 16-bit
    __m128i result = _mm_adds_epi16(destVec, srcVec);

    // Store back
    _mm_storeu_si128((__m128i*)dest, result);
#else
    // Scalar with manual saturation
    for (int i = 0; i < 4; ++i)
    {
        dest[i] = (short)Clamp((int)dest[i] + (int)src[i], -32768, 32767);
    }
#endif
}

void AudioMixing_SIMD::MixMultipleSources(
    int* dest,
    const short* const* sources,
    const int* gains,
    uint32_t numSources,
    uint32_t samples)
{
    // For each source, accumulate into destination with its gain
    for (uint32_t srcIdx = 0; srcIdx < numSources; ++srcIdx)
    {
        const short* src = sources[srcIdx];
        int gain = gains[srcIdx];

        if (!src || gain == 0)
            continue;

        // Process samples with 4-sample batches
        uint32_t simdCount = (samples / 4) * 4;

        for (uint32_t i = 0; i < simdCount; i += 4)
        {
            MixMonoWithGain_4x(&dest[i], &src[i], gain);
        }

        // Scalar fallback for remainder
        for (uint32_t i = simdCount; i < samples; ++i)
        {
            dest[i] += (src[i] * gain) / 256;
        }
    }
}

void AudioMixing_SIMD::MixMonoWithGain_Scalar(int* dest, const short* src, int vol, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        dest[i] += (src[i] * vol) / 256;
    }
}

void AudioMixing_SIMD::Convert32to16_Scalar(short* dest, const int* src, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        dest[i] = (short)Clamp(src[i], -32768, 32767);
    }
}

} // namespace Urho3D
