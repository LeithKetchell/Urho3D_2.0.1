// Copyright (c) 2008-2025 the Urho3D project
// License: MIT
//
// Phase 1.3: Structure-of-Arrays (SOA) Particle Buffer Implementation
// SIMD-optimized batch processing for particles

#include "../Precompiled.h"
#include "ParticleBuffer.h"
#include "../Math/Random.h"

#ifdef URHO3D_SSE
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

namespace Urho3D
{

ParticleBuffer::ParticleBuffer(uint32_t initialCapacity)
    : capacity_(initialCapacity), count_(0), allocated_(0)
{
    AllocateArrays(initialCapacity);
}

void ParticleBuffer::AllocateArrays(uint32_t newCapacity)
{
    capacity_ = newCapacity;
    positions_.Resize(capacity_);
    velocities_.Resize(capacity_);
    sizes_.Resize(capacity_);
    colors_.Resize(capacity_);
    lifetimes_.Resize(capacity_);
    timers_.Resize(capacity_);
    scales_.Resize(capacity_, 1.0f);
    rotationSpeeds_.Resize(capacity_);
    rotations_.Resize(capacity_, 0.0f);
    colorIndices_.Resize(capacity_, 0);
    texIndices_.Resize(capacity_, 0);
    enabled_.Resize(capacity_, false);

    // Initialize free list with all indices
    freeList_.Clear();
    freeList_.Reserve(capacity_);
    for (uint32_t i = capacity_; i > 0; --i)
        freeList_.Push(i - 1);

    allocated_ = 0;
}

void ParticleBuffer::Reserve(uint32_t capacity)
{
    if (capacity > capacity_)
        AllocateArrays(capacity);
}

uint32_t ParticleBuffer::AllocateParticle()
{
    if (freeList_.Empty())
        return NINDEX;

    uint32_t index = freeList_.Back();
    freeList_.Pop();
    enabled_[index] = true;
    ++count_;
    ++allocated_;
    return index;
}

void ParticleBuffer::ReleaseParticle(uint32_t index)
{
    if (index < capacity_ && enabled_[index])
    {
        enabled_[index] = false;
        freeList_.Push(index);
        if (count_ > 0)
            --count_;
    }
}

void ParticleBuffer::UpdatePositions(float dt)
{
#ifdef URHO3D_SSE
    // SIMD-optimized path: process 4 particles at once
    __m128 dtime = _mm_set_ps1(dt);
    uint32_t simdCount = (count_ / 4) * 4;

    for (uint32_t i = 0; i < simdCount; i += 4)
    {
        // Load 4 positions and velocities
        __m128 px = _mm_loadu_ps(&positions_[i].x_);
        __m128 py = _mm_loadu_ps(&positions_[i + 1].x_);
        __m128 pz = _mm_loadu_ps(&positions_[i + 2].x_);

        __m128 vx = _mm_loadu_ps(&velocities_[i].x_);
        __m128 vy = _mm_loadu_ps(&velocities_[i + 1].x_);
        __m128 vz = _mm_loadu_ps(&velocities_[i + 2].x_);

        // p += v * dt
        __m128 newPx = _mm_add_ps(px, _mm_mul_ps(vx, dtime));
        __m128 newPy = _mm_add_ps(py, _mm_mul_ps(vy, dtime));
        __m128 newPz = _mm_add_ps(pz, _mm_mul_ps(vz, dtime));

        // Store updated positions
        _mm_storeu_ps(&positions_[i].x_, newPx);
        _mm_storeu_ps(&positions_[i + 1].x_, newPy);
        _mm_storeu_ps(&positions_[i + 2].x_, newPz);
    }

    // Scalar fallback for remaining particles
    for (uint32_t i = simdCount; i < count_; ++i)
    {
        if (enabled_[i])
            positions_[i] += velocities_[i] * dt;
    }
#else
    // Scalar path
    for (uint32_t i = 0; i < count_; ++i)
    {
        if (enabled_[i])
            positions_[i] += velocities_[i] * dt;
    }
#endif
}

void ParticleBuffer::UpdateLifetimes(float dt)
{
    for (uint32_t i = 0; i < count_; ++i)
    {
        if (!enabled_[i])
            continue;

        timers_[i] += dt;
        if (timers_[i] >= lifetimes_[i])
            enabled_[i] = false;
    }
}

void ParticleBuffer::UpdateScales(float dt, float sizeAdd, float sizeMul)
{
#ifdef URHO3D_SSE
    // SIMD: process 4 scales at once
    __m128 dtime = _mm_set_ps1(dt);
    __m128 add = _mm_set_ps1(sizeAdd * dt);
    __m128 mulFactor = _mm_set_ps1((sizeMul - 1.0f) * dt + 1.0f);

    uint32_t simdCount = (count_ / 4) * 4;

    for (uint32_t i = 0; i < simdCount; i += 4)
    {
        __m128 scale = _mm_loadu_ps(&scales_[i]);

        // scale += sizeAdd * dt
        scale = _mm_add_ps(scale, add);

        // scale *= ((sizeMul - 1.0) * dt + 1.0)
        if (sizeMul != 1.0f)
            scale = _mm_mul_ps(scale, mulFactor);

        _mm_storeu_ps(&scales_[i], scale);

        // Update billboard sizes: sizes_ = originalSizes_ * scales_
        Vector2 size0 = sizes_[i];
        Vector2 size1 = sizes_[i + 1];
        Vector2 size2 = sizes_[i + 2];
        Vector2 size3 = sizes_[i + 3];

        float s0 = scales_[i];
        float s1 = scales_[i + 1];
        float s2 = scales_[i + 2];
        float s3 = scales_[i + 3];

        sizes_[i] = size0 * s0;
        sizes_[i + 1] = size1 * s1;
        sizes_[i + 2] = size2 * s2;
        sizes_[i + 3] = size3 * s3;
    }

    // Scalar fallback
    for (uint32_t i = simdCount; i < count_; ++i)
    {
        if (!enabled_[i])
            continue;

        scales_[i] += sizeAdd * dt;
        if (sizeMul != 1.0f)
            scales_[i] *= ((sizeMul - 1.0f) * dt + 1.0f);

        sizes_[i] *= scales_[i];
    }
#else
    // Scalar path
    for (uint32_t i = 0; i < count_; ++i)
    {
        if (!enabled_[i])
            continue;

        scales_[i] += sizeAdd * dt;
        if (sizeMul != 1.0f)
            scales_[i] *= ((sizeMul - 1.0f) * dt + 1.0f);

        sizes_[i] *= scales_[i];
    }
#endif
}

void ParticleBuffer::ApplyForces(float dt, const Vector3& constantForce, float dampingForce)
{
    if (constantForce == Vector3::ZERO && dampingForce == 0.0f)
        return;

#ifdef URHO3D_SSE
    // SIMD: process 4 velocities at once
    __m128 force = _mm_set_ps(0.0f, constantForce.z_ * dt, constantForce.y_ * dt, constantForce.x_ * dt);
    __m128 damping = _mm_set_ps1(dampingForce * dt);

    uint32_t simdCount = (count_ / 4) * 4;

    for (uint32_t i = 0; i < simdCount; i += 4)
    {
        __m128 vel = _mm_loadu_ps(&velocities_[i].x_);

        // v += constantForce * dt
        vel = _mm_add_ps(vel, force);

        // v -= dampingForce * dt * v
        __m128 dampingTerm = _mm_mul_ps(_mm_mul_ps(damping, vel), _mm_set_ps1(-1.0f));
        vel = _mm_add_ps(vel, dampingTerm);

        _mm_storeu_ps(&velocities_[i].x_, vel);
    }

    // Scalar fallback
    for (uint32_t i = simdCount; i < count_; ++i)
    {
        if (!enabled_[i])
            continue;

        velocities_[i] += constantForce * dt;
        velocities_[i] -= velocities_[i] * dampingForce * dt;
    }
#else
    // Scalar path
    for (uint32_t i = 0; i < count_; ++i)
    {
        if (!enabled_[i])
            continue;

        velocities_[i] += constantForce * dt;
        velocities_[i] -= velocities_[i] * dampingForce * dt;
    }
#endif
}

uint32_t ParticleBuffer::EmitBatch(
    uint32_t count,
    const Vector3& initialPosition,
    const Vector3& initialVelocity,
    const Vector3& velocityVariance,
    float lifetime,
    float size,
    float rotationSpeed,
    const Color& color)
{
    uint32_t emitted = 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t index = AllocateParticle();
        if (index == NINDEX)
            break;

        positions_[index] = initialPosition;
        velocities_[index] = initialVelocity + Vector3(
            Random(-velocityVariance.x_, velocityVariance.x_),
            Random(-velocityVariance.y_, velocityVariance.y_),
            Random(-velocityVariance.z_, velocityVariance.z_)
        );
        sizes_[index] = Vector2(size, size);
        colors_[index] = color;
        lifetimes_[index] = lifetime;
        timers_[index] = 0.0f;
        scales_[index] = 1.0f;
        rotationSpeeds_[index] = rotationSpeed;
        rotations_[index] = 0.0f;
        colorIndices_[index] = 0;
        texIndices_[index] = 0;

        ++emitted;
    }

    return emitted;
}

void ParticleBuffer::Clear()
{
    count_ = 0;
    allocated_ = 0;
    freeList_.Clear();
    freeList_.Reserve(capacity_);

    for (uint32_t i = 0; i < capacity_; ++i)
    {
        enabled_[i] = false;
        freeList_.Push(i);
    }
}

uint32_t ParticleBuffer::Compact()
{
    // Simple compaction: move active particles to front, rebuild free list
    uint32_t writePos = 0;

    for (uint32_t i = 0; i < allocated_; ++i)
    {
        if (enabled_[i])
        {
            if (writePos != i)
            {
                positions_[writePos] = positions_[i];
                velocities_[writePos] = velocities_[i];
                sizes_[writePos] = sizes_[i];
                colors_[writePos] = colors_[i];
                lifetimes_[writePos] = lifetimes_[i];
                timers_[writePos] = timers_[i];
                scales_[writePos] = scales_[i];
                rotationSpeeds_[writePos] = rotationSpeeds_[i];
                rotations_[writePos] = rotations_[i];
                colorIndices_[writePos] = colorIndices_[i];
                texIndices_[writePos] = texIndices_[i];
                enabled_[writePos] = true;
                enabled_[i] = false;
            }
            ++writePos;
        }
    }

    count_ = writePos;
    allocated_ = writePos;

    // Rebuild free list
    freeList_.Clear();
    freeList_.Reserve(capacity_);
    for (uint32_t i = allocated_; i < capacity_; ++i)
        freeList_.Push(i);

    return count_;
}

} // namespace Urho3D
