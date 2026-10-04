// WaterAnimal — aquatic creature base.

#include "WaterAnimal.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Math/MathDefs.h>

WaterAnimal::WaterAnimal(Context* context) :
    Creature(context)
{
    // Aquatic creatures use Update (frame-rate driven), not FixedUpdate
    SetUpdateEventMask(LogicComponentEvents::Update);
}

void WaterAnimal::Start()
{
    // Cache terrain but skip Creature::Start() auto-scaling (fish use manual scale)
    terrain_ = GetScene()->GetComponent<Terrain>(true);

    homePos_ = node_->GetWorldPosition();

    // Randomize per-creature feeding schedule
    feedPhaseOffset_ = Random(0.0f, 0.4f);
    restingDepthFrac_ = Random(0.1f, 0.4f);
    feedEagerness_ = Random(0.5f, 1.0f);
}

void WaterAnimal::ClampToWaterColumn()
{
    Vector3 pos = node_->GetWorldPosition();
    float terrainH = terrain_ ? terrain_->GetHeight(pos) : 0.0f;
    float floorY = terrainH + minDepth_;
    float ceilY = waterLevel_ - minDepth_;
    if (floorY > ceilY)
        floorY = ceilY;

    // Dawn/dusk feeding — fish rise toward the surface near horizon
    float feedBias = 0.0f;
    if (sunLightNode_)
    {
        Vector3 lightDir = sunLightNode_->GetDirection();
        float sunUp = -lightDir.y_;
        float horizonProximity = 1.0f - Abs(sunUp) - feedPhaseOffset_;
        horizonProximity = Clamp(horizonProximity, 0.0f, 1.0f);
        feedBias = horizonProximity * horizonProximity * feedEagerness_;
    }

    float restingY = Lerp(floorY, ceilY, restingDepthFrac_);
    float feedingY = ceilY;
    float preferredY = Lerp(restingY, feedingY, feedBias);

    pos.y_ = Lerp(pos.y_, preferredY, 0.05f);
    pos.y_ = Clamp(pos.y_, floorY, ceilY);
    node_->SetPosition(pos);
}

Vector3 WaterAnimal::GetShallowAvoidance(const Vector3& pos, const Vector3& forward, float probeDistance) const
{
    if (!terrain_)
        return Vector3::ZERO;

    Vector3 probe = pos + forward * probeDistance;
    float probeH = terrain_->GetHeight(probe);
    float probeDepth = waterLevel_ - probeH;

    if (probeDepth < 1.5f)
    {
        Vector3 toDeep = pos - probe;
        toDeep.y_ = 0.0f;
        if (toDeep.LengthSquared() > 0.001f)
            toDeep.Normalize();
        float shallowUrgency = Clamp(1.0f - (probeDepth / 1.5f), 0.0f, 1.0f);
        return toDeep * shallowUrgency;
    }

    return Vector3::ZERO;
}
