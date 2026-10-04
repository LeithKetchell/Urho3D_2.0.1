// Copyright (c) 2008-2025 the Urho3D project
// License: MIT
//
// Phase 1.4: SIMD-Optimized Audio Mixing
// Vectorized mixing, gain multiplication, and format conversion

#pragma once

#include "../Audio/Audio.h"

#ifdef URHO3D_SSE
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

namespace Urho3D
{

/// SIMD-optimized audio mixing functions
class URHO3D_API AudioMixing_SIMD
{
public:
    /// Mix 4 samples with gain (SSE optimized)
    /// dest[i] += src[i] * (vol/256) for i in [0..3]
    static void MixMonoWithGain_4x(int* dest, const short* src, int vol);

    /// Mix 4 samples from stereo source to stereo output
    /// Interleaved L/R channels
    static void MixStereoWithGain_2x(int* destL, int* destR, const short* src, int volL, int volR);

    /// Batch convert 4 samples from 32-bit to 16-bit with saturation
    /// Converts accumulation buffer output to final PCM samples
    static void Convert32to16_4x(short* dest, const int* src);

    /// Apply gain to multiple samples in batch (SSE)
    /// output[i] = input[i] * (gain/256) for i in [0..3]
    static void ApplyGain_4x(int* samples, int gain, uint32_t count);

    /// Saturating add: accumulate with clipping protection
    /// dest[i] = Clamp(dest[i] + src[i], -32768, 32767)
    static void SaturatingAdd_4x(short* dest, const short* src);

    /// Batch mixing for multiple sources (SSE + fallback)
    /// Mix up to N sources into common buffer with separate gains
    static void MixMultipleSources(
        int* dest,
        const short* const* sources,
        const int* gains,
        uint32_t numSources,
        uint32_t samples
    );

private:
    /// Scalar fallback implementations
    static void MixMonoWithGain_Scalar(int* dest, const short* src, int vol, uint32_t count);
    static void Convert32to16_Scalar(short* dest, const int* src, uint32_t count);
};

} // namespace Urho3D
