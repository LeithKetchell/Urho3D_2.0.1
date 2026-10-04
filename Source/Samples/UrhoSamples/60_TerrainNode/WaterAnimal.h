// WaterAnimal — aquatic creature base.
// Adds: water column clamping, shallow avoidance, depth/feeding cycle.

#pragma once

#include "Creature.h"

class WaterAnimal : public Creature
{
    URHO3D_OBJECT(WaterAnimal, Creature);

public:
    explicit WaterAnimal(Context* context);

    void Start() override;

    /// Set cached sun light node (avoids per-frame scene search).
    void SetSunLightNode(Node* node) { sunLightNode_ = node; }

protected:
    /// Clamp position to water column (above terrain floor, below water surface).
    void ClampToWaterColumn();

    /// Check if a probe position ahead is too shallow, return avoidance direction.
    /// Returns zero vector if water is deep enough.
    Vector3 GetShallowAvoidance(const Vector3& pos, const Vector3& forward, float probeDistance) const;

    float waterLevel_{5.0f};
    float minDepth_{0.3f};
    float maxDepth_{4.0f};

    // Dawn/dusk feeding cycle — per-creature randomized
    float feedPhaseOffset_{0.0f};
    float restingDepthFrac_{0.2f};
    float feedEagerness_{1.0f};

    Node* sunLightNode_{};
};
