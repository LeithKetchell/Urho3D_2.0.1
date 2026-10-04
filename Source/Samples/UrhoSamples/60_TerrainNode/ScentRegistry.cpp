// ScentRegistry implementation — see header for design notes.

#include "ScentRegistry.h"

#include <Urho3D/Container/Vector.h>

using namespace Urho3D;

namespace
{
    // SCENT_DURATION must stay below CORPSE_DURATION (LandAnimal.cpp = 120s)
    // so the scent fades before the corpse vanishes — a scavenger walking
    // toward a missing body looks broken.
    const float SCENT_DURATION = 90.0f;

    // FIFO cap. Mass-kill scenarios drop the OLDEST marker so the freshest
    // kill is always investigatable. Bounds the FindNearest scan cost.
    const unsigned MAX_ACTIVE_SCENTS = 16;

    Vector<ScentMarker> g_markers;
}

namespace ScentRegistry
{

void Register(const Vector3& worldPos, int speciesId)
{
    if (g_markers.Size() >= MAX_ACTIVE_SCENTS)
        g_markers.Erase(0);

    ScentMarker m;
    m.position   = worldPos;
    m.speciesId  = speciesId;
    m.ageSeconds = 0.0f;
    g_markers.Push(m);
}

const ScentMarker* FindNearest(const Vector3& queryPos, float maxRadius)
{
    const ScentMarker* best = nullptr;
    float bestDistSq = maxRadius * maxRadius;
    for (unsigned i = 0; i < g_markers.Size(); ++i)
    {
        const ScentMarker& m = g_markers[i];
        float dx = m.position.x_ - queryPos.x_;
        float dy = m.position.y_ - queryPos.y_;
        float dz = m.position.z_ - queryPos.z_;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            best = &m;
        }
    }
    return best;
}

void Tick(float timeStep)
{
    // Iterate from the back so in-place erase doesn't shift unread indices.
    for (unsigned i = g_markers.Size(); i > 0; --i)
    {
        unsigned idx = i - 1;
        g_markers[idx].ageSeconds += timeStep;
        if (g_markers[idx].ageSeconds >= SCENT_DURATION)
            g_markers.Erase(idx);
    }
}

void Clear()
{
    g_markers.Clear();
}

} // namespace ScentRegistry
