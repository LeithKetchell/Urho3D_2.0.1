// OOFO — a dyslexic NASA analyst's flying saucer easter egg.
// Hides behind clouds, darts between them, only visible in daylight.

#pragma once

#include <Urho3D/Container/RefCounted.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/Image.h>

using namespace Urho3D;

class OOFO : public RefCounted
{
public:
    /// Construct. Call Init() after construction.
    OOFO() = default;

    /// Build shared cloud position table (call once, share result with all instances).
    static Vector<Vector3> BuildCloudPositions(ResourceCache* cache);

    /// Create the billboard node in the scene.
    void Init(Scene* scene, ResourceCache* cache, const Vector<Vector3>* cloudPositions);

    /// Per-frame update. Needs camera position, cloud rotation angle, and night factor.
    void Update(float timeStep, const Vector3& camPos, float cloudAngle, float nightFactor);

    /// Get the scene node (for debug ray drawing, etc).
    Node* GetNode() const { return node_; }

private:
    void DartToCloud();

    Node* node_{};
    SharedPtr<Material> mat_;
    const Vector<Vector3>* cloudPositions_{};

    Vector3 dir_;
    Vector3 target_;
    float dartTimer_{0.0f};
    bool darting_{false};
    float idleTimer_{0.0f};
    float nextIdleDuration_{5.0f}; // seconds until next voluntary dart
};
