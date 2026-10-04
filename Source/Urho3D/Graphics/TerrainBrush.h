// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "../Graphics/Terrain.h"
#include "../Resource/Image.h"

namespace Urho3D
{

/// Shared terrain brush — pure logic, no UI/networking/scene.
/// Usable by client (Sample 60), upstream server, and AuthServer.
class URHO3D_API TerrainBrush : public Object
{
    URHO3D_OBJECT(TerrainBrush, Object);

public:
    explicit TerrainBrush(Context* context);

    /// Set the terrain and heightmap to operate on.
    void SetTerrain(Terrain* terrain, Image* heightMap);
    /// Set optional water map (same resolution as heightmap, single-channel grayscale).
    void SetWaterMap(Image* waterMap);

    /// Apply brush at world position. Returns true if terrain was modified.
    bool Apply(const Vector3& worldPos, float timeStep);

    /// Brush parameters.
    void SetMode(int mode) { mode_ = mode; }
    void SetShape(int shape) { shape_ = shape; }
    void SetRadius(float r) { radius_ = r; }
    void SetStrength(float s) { strength_ = s; }
    void SetSmoothStrength(float s) { smoothStrength_ = s; }
    void SetRotation(float deg) { rotation_ = deg; }
    void SetFlattenHeight(float h) { flattenHeight_ = h; }
    void SetRiverCarveDepth(float d) { riverCarveDepth_ = d; }

    int GetMode() const { return mode_; }
    int GetShape() const { return shape_; }
    float GetRadius() const { return radius_; }
    float GetStrength() const { return strength_; }
    float GetSmoothStrength() const { return smoothStrength_; }
    float GetRotation() const { return rotation_; }
    float GetFlattenHeight() const { return flattenHeight_; }
    float GetRiverCarveDepth() const { return riverCarveDepth_; }

    /// Shape falloff at (dx, dz) from brush center. Returns -1 if outside shape, 0..1 inside.
    float ShapeFalloff(float dx, float dz, float radius) const;

    /// Clone the current heightmap for undo.
    SharedPtr<Image> CloneHeightMap() const;
    /// Restore heightmap from a saved clone.
    void RestoreHeightMap(Image* src);

    /// Clone the current water map for undo.
    SharedPtr<Image> CloneWaterMap() const;
    /// Restore water map from a saved clone.
    void RestoreWaterMap(Image* src);

private:
    /// Read height at pixel (px, py) with 32-bit RGBA precision.
    float ReadH(int px, int py) const;
    /// Write height at pixel (px, py) with 32-bit RGBA precision.
    void WriteH(int px, int py, float h);

    WeakPtr<Terrain> terrain_;
    Image* heightMap_{};
    Image* waterMap_{};

    int mode_{0};              // 0=off, 1=raise, 2=lower, 3=smooth, 4=flatten, 5=erosion, 6=river
    int shape_{0};             // 0=circle, 1=square, 2=tri, 3=star, 4=pent, 5=hex, 6=oct
    float radius_{5.0f};
    float strength_{0.05f};
    float smoothStrength_{0.3f};
    float rotation_{0.0f};
    float flattenHeight_{-1.0f};
    float riverCarveDepth_{0.02f};
};

}
