// ScentRegistry — Death System Phase 3.
//
// Single-instance store of death scent markers. There is one terrain and one
// scent space per game session, so global free-function state is appropriate
// here. Lives in its own tiny header so LandAnimal.cpp can use the API
// without dragging in TerrainNode.h (which transitively pulls Sample.inl
// definitions and breaks linking).
//
// Producer: TerrainNode::HandleAnimalDied calls ScentRegistry::Register on
//           every E_CREATUREDIED.
// Consumer: LandAnimal::TryEnterScavenge calls ScentRegistry::FindNearest to
//           pick the closest fresh kill within detection radius.
// Tick:     TerrainNode::HandleUpdate calls ScentRegistry::Tick(timeStep)
//           every frame to age + expire markers.

#pragma once

#include <Urho3D/Math/Vector3.h>

using namespace Urho3D;

/// Death-site scent marker. Plain data, no behavior.
struct ScentMarker
{
    Vector3 position{Vector3::ZERO};
    int     speciesId{0};   ///< the dying species — for future "no cannibal" filters
    float   ageSeconds{0.0f};
};

namespace ScentRegistry
{
    /// Register a death scent. FIFO drops the oldest marker if at cap.
    void Register(const Vector3& worldPos, int speciesId);

    /// Return the nearest live marker within maxRadius, or nullptr if none.
    /// Pointer is valid until the next Tick() call.
    const ScentMarker* FindNearest(const Vector3& queryPos, float maxRadius);

    /// Age all markers, expire those past SCENT_DURATION.
    void Tick(float timeStep);

    /// Drop all markers (used when leaving the world / switching maps).
    void Clear();
}
