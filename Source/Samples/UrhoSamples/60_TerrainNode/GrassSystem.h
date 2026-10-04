// GrassSystem — GPU-driven grass rendering: single mesh, single draw call.
// Vertex shader does all per-blade work via VTF heightmap placement.

#pragma once

#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>

using namespace Urho3D;

class GrassSystem : public LogicComponent
{
    URHO3D_OBJECT(GrassSystem, LogicComponent);

public:
    explicit GrassSystem(Context* context);
    static void RegisterObject(Context* context);

    /// Build the grass grid mesh and set up material. Call after terrain is ready.
    void Initialize(Terrain* terrain, float gridSize = 200.0f, float cellSize = 0.4f, float radius = 100.0f);

    /// Per-frame update: snap grid to camera, update uniforms.
    void Update(float timeStep) override;

    /// Set wind parameters.
    void SetWind(float strength, const Vector2& direction);

    /// Set a physics shape (index 0-3). xyz=world center, w=radius. Set w<=0 to disable.
    void SetPhysicsShape(int index, const Vector4& shape);

    /// Set seasonal tint color.
    void SetSeasonalTint(const Color& tint);

    /// Set grass visible radius and fade width.
    void SetRadius(float radius, float fadeWidth = 10.0f);

    /// Set a custom density map (overrides terrain weight map). Red channel = density 0-1.
    void SetDensityMap(Texture2D* texture);

    /// Get the grass node (for visibility control etc).
    Node* GetGrassNode() const { return grassNode_; }

private:
    /// Build the grid mesh as a Model.
    SharedPtr<Model> BuildGridMesh(float gridSize, float cellSize);

    /// Upload terrain heightmap as a GPU texture for VTF.
    SharedPtr<Texture2D> CreateHeightmapTexture(Terrain* terrain);

    WeakPtr<Terrain> terrain_;
    Node* grassNode_{};
    SharedPtr<Material> grassMat_;
    SharedPtr<Texture2D> heightmapTex_;
    SharedPtr<Texture2D> densityMapTex_;

    float gridSize_{200.0f};
    float cellSize_{0.4f};
    float grassRadius_{100.0f};
    float grassFadeWidth_{10.0f};
    float windStrength_{0.5f};
    Vector2 windDirection_{1.0f, 0.0f};
    Vector4 physShapes_[4]{};
    Color seasonalTint_{1.0f, 1.0f, 1.0f, 1.0f};
};
