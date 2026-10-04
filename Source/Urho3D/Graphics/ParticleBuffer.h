// Copyright (c) 2008-2025 the Urho3D project
// License: MIT
//
// Phase 1.3: Structure-of-Arrays (SOA) Particle Buffer
// Optimized for SIMD operations and cache-friendly access patterns

#pragma once

#include "../Container/Vector.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Color.h"

namespace Urho3D
{

/// Structure-of-Arrays particle buffer for efficient SIMD batch processing
/// Replaces AoS layout with contiguous arrays per field for better cache utilization
class URHO3D_API ParticleBuffer
{
public:
    /// Construct with initial capacity
    explicit ParticleBuffer(uint32_t initialCapacity = 256);

    /// Destruct
    ~ParticleBuffer() = default;

    /// Reserve capacity for particles
    void Reserve(uint32_t capacity);

    /// Allocate a new particle slot and return index
    /// Returns NINDEX if at capacity
    uint32_t AllocateParticle();

    /// Release a particle slot (mark as unused)
    void ReleaseParticle(uint32_t index);

    /// Update particle positions from velocities (SIMD optimized)
    /// Processes 4 particles at once when available
    void UpdatePositions(float dt);

    /// Update particle lifetimes and mark expired particles
    void UpdateLifetimes(float dt);

    /// Update particle scales/sizes
    void UpdateScales(float dt, float sizeAdd, float sizeMul);

    /// Apply constant force and damping to velocities
    void ApplyForces(float dt, const Vector3& constantForce, float dampingForce);

    /// Emit new particles in batch (up to 4 at once)
    /// Parameters: initialPosition, initialVelocity, velocity variance (per axis)
    /// Returns number of particles successfully emitted
    uint32_t EmitBatch(
        uint32_t count,
        const Vector3& initialPosition,
        const Vector3& initialVelocity,
        const Vector3& velocityVariance,
        float lifetime,
        float size,
        float rotationSpeed,
        const Color& color
    );

    /// Get current particle count
    uint32_t GetCount() const { return count_; }

    /// Get capacity
    uint32_t GetCapacity() const { return capacity_; }

    /// Direct access to particle data for rendering/analysis
    /// Positions array
    const Vector<Vector3>& GetPositions() const { return positions_; }
    Vector<Vector3>& GetPositions() { return positions_; }

    /// Velocities array
    const Vector<Vector3>& GetVelocities() const { return velocities_; }
    Vector<Vector3>& GetVelocities() { return velocities_; }

    /// Sizes array
    const Vector<Vector2>& GetSizes() const { return sizes_; }
    Vector<Vector2>& GetSizes() { return sizes_; }

    /// Colors array
    const Vector<Color>& GetColors() const { return colors_; }
    Vector<Color>& GetColors() { return colors_; }

    /// Lifetimes array (max lifetime for each particle)
    const Vector<float>& GetLifetimes() const { return lifetimes_; }
    Vector<float>& GetLifetimes() { return lifetimes_; }

    /// Timers array (current elapsed time)
    const Vector<float>& GetTimers() const { return timers_; }
    Vector<float>& GetTimers() { return timers_; }

    /// Scales array (size multiplier)
    const Vector<float>& GetScales() const { return scales_; }
    Vector<float>& GetScales() { return scales_; }

    /// Rotation speeds array
    const Vector<float>& GetRotationSpeeds() const { return rotationSpeeds_; }
    Vector<float>& GetRotationSpeeds() { return rotationSpeeds_; }

    /// Rotations array (current rotation angle)
    const Vector<float>& GetRotations() const { return rotations_; }
    Vector<float>& GetRotations() { return rotations_; }

    /// Color indices array (for animation)
    const Vector<int32_t>& GetColorIndices() const { return colorIndices_; }
    Vector<int32_t>& GetColorIndices() { return colorIndices_; }

    /// Texture indices array (for animation)
    const Vector<int32_t>& GetTexIndices() const { return texIndices_; }
    Vector<int32_t>& GetTexIndices() { return texIndices_; }

    /// Enabled flags array
    const Vector<bool>& GetEnabled() const { return enabled_; }
    Vector<bool>& GetEnabled() { return enabled_; }

    /// Get statistics
    uint32_t GetAllocatedCount() const { return allocated_; }

    /// Reset all particles and free list
    void Clear();

    /// Compact alive particles (removes gaps from released particles)
    /// Returns number of particles remaining
    uint32_t Compact();

private:
    /// Allocate internal arrays
    void AllocateArrays(uint32_t newCapacity);

    /// Get next free particle from free list, or allocate new
    uint32_t GetFreeParticle();

    // SOA arrays - one per field
    Vector<Vector3> positions_;          // 12 bytes per particle
    Vector<Vector3> velocities_;         // 12 bytes per particle
    Vector<Vector2> sizes_;              // 8 bytes per particle
    Vector<Color> colors_;               // 4 bytes per particle
    Vector<float> lifetimes_;            // 4 bytes per particle
    Vector<float> timers_;               // 4 bytes per particle
    Vector<float> scales_;               // 4 bytes per particle
    Vector<float> rotationSpeeds_;       // 4 bytes per particle
    Vector<float> rotations_;            // 4 bytes per particle
    Vector<int32_t> colorIndices_;       // 4 bytes per particle
    Vector<int32_t> texIndices_;         // 4 bytes per particle
    Vector<bool> enabled_;               // 1 byte per particle

    // Free list management
    Vector<uint32_t> freeList_;          // Stack of free indices

    /// Current capacity
    uint32_t capacity_;

    /// Currently active particles
    uint32_t count_;

    /// Total allocated particles (including free slots)
    uint32_t allocated_;
};

} // namespace Urho3D
