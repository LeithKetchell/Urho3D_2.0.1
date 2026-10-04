// GrassSystem — GPU-driven grass rendering implementation.

#include "GrassSystem.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/GraphicsAPI/IndexBuffer.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>

GrassSystem::GrassSystem(Context* context)
    : LogicComponent(context)
{
    SetUpdateEventMask(LogicComponentEvents::Update);
}

void GrassSystem::RegisterObject(Context* context)
{
    context->RegisterFactory<GrassSystem>();
}

void GrassSystem::Initialize(Terrain* terrain, float gridSize, float cellSize, float radius)
{
    if (!terrain)
    {
        URHO3D_LOGERROR("GrassSystem::Initialize — no terrain");
        return;
    }

    terrain_ = terrain;
    gridSize_ = gridSize;
    cellSize_ = cellSize;
    grassRadius_ = radius;

    auto* cache = GetSubsystem<ResourceCache>();

    // Build the grid mesh
    SharedPtr<Model> model = BuildGridMesh(gridSize, cellSize);
    if (!model)
    {
        URHO3D_LOGERROR("GrassSystem::Initialize — failed to build grid mesh");
        return;
    }

    // Upload terrain heightmap as GPU texture for vertex texture fetch
    heightmapTex_ = CreateHeightmapTexture(terrain);
    if (!heightmapTex_)
    {
        URHO3D_LOGERROR("GrassSystem::Initialize — failed to create heightmap texture");
        return;
    }

    // Create material
    grassMat_ = new Material(context_);
    auto* tech = cache->GetResource<Technique>("Techniques/GrassDiffAlpha.xml");
    if (!tech)
    {
        URHO3D_LOGERROR("GrassSystem::Initialize — GrassDiffAlpha.xml not found");
        return;
    }
    grassMat_->SetTechnique(0, tech);
    grassMat_->SetCullMode(CULL_NONE);

    // Texture unit 0 = diffuse (grass blade texture)
    auto* bladeTex = cache->GetResource<Texture2D>("Textures/GrassBlade.png");
    if (bladeTex)
    {
        grassMat_->SetTexture(TU_DIFFUSE, bladeTex);
    }
    else
    {
        // Generate a procedural blade texture as fallback
        URHO3D_LOGINFO("GrassSystem — generating procedural blade texture");
        SharedPtr<Image> bladeImg(new Image(context_));
        const int tw = 64, th = 256;
        bladeImg->SetSize(tw, th, 4);
        // Clear to transparent
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x)
                bladeImg->SetPixel(x, y, Color(0, 0, 0, 0));

        // Draw a tapered blade — narrow at top, wider at base
        float centerX = tw * 0.5f;
        for (int y = 0; y < th; ++y)
        {
            float t = (float)y / (float)th;  // 0=top, 1=bottom
            float halfW = 1.0f + t * (tw * 0.25f);  // 1px at tip, ~16px at base
            float shade = 0.5f + 0.5f * (1.0f - t);  // lighter at tip
            Color c(0.28f * shade, 0.62f * shade, 0.12f * shade, 1.0f);
            for (int x = (int)(centerX - halfW); x <= (int)(centerX + halfW); ++x)
            {
                if (x >= 0 && x < tw)
                    bladeImg->SetPixel(x, y, c);
            }
        }

        SharedPtr<Texture2D> procTex(new Texture2D(context_));
        procTex->SetFilterMode(FILTER_BILINEAR);
        procTex->SetData(bladeImg);
        grassMat_->SetTexture(TU_DIFFUSE, procTex);
    }

    // Texture unit 1 = heightmap (for VTF)
    grassMat_->SetTexture(TU_NORMAL, heightmapTex_);

    // Texture unit 2 = density map (terrain weight map — R channel = grass density)
    Material* terrainMat = terrain->GetMaterial();
    if (terrainMat)
    {
        Texture* weightTex = terrainMat->GetTexture(TU_DIFFUSE);  // unit 0 = TerrainWeights.dds
        if (weightTex)
        {
            grassMat_->SetTexture(TU_SPECULAR, weightTex);
            URHO3D_LOGINFO("GrassSystem — bound terrain weight map as density texture");
        }
    }

    // Compute terrain world size and offset for shader
    IntVector2 numVerts = terrain->GetNumVertices();
    Vector3 spacing = terrain->GetSpacing();
    Vector3 terrainPos = terrain->GetNode()->GetWorldPosition();
    float terrainSizeX = (numVerts.x_ - 1) * spacing.x_;
    float terrainSizeZ = (numVerts.y_ - 1) * spacing.z_;
    // Terrain is centered on its node — heightmap (0,0) corner is at node pos minus half size
    float terrainOffsetX = terrainPos.x_ - terrainSizeX * 0.5f;
    float terrainOffsetZ = terrainPos.z_ - terrainSizeZ * 0.5f;
    float terrainYOffset = terrainPos.y_;

    // Height scale: the heightmap stores 0-255 as 0.0-1.0 in texture.
    // World height = terrainYOffset + pixel_value * spacing.y * 255
    float heightScale = 255.0f * spacing.y_;

    // Set shader parameters
    grassMat_->SetShaderParameter("GrassOrigin", Vector3::ZERO);
    grassMat_->SetShaderParameter("GrassParams", Vector2(gridSize_, cellSize_));
    grassMat_->SetShaderParameter("TerrainSize", Vector2(terrainSizeX, terrainSizeZ));
    grassMat_->SetShaderParameter("TerrainOffset", Vector2(terrainOffsetX, terrainOffsetZ));
    grassMat_->SetShaderParameter("GrassRadius", grassRadius_);
    grassMat_->SetShaderParameter("GrassFadeWidth", grassFadeWidth_);
    grassMat_->SetShaderParameter("WindStrength", windStrength_);
    grassMat_->SetShaderParameter("WindDirection", windDirection_);
    grassMat_->SetShaderParameter("PhysShape0", Vector4::ZERO);
    grassMat_->SetShaderParameter("PhysShape1", Vector4::ZERO);
    grassMat_->SetShaderParameter("PhysShape2", Vector4::ZERO);
    grassMat_->SetShaderParameter("PhysShape3", Vector4::ZERO);
    grassMat_->SetShaderParameter("SeasonalTint", seasonalTint_.ToVector4());
    grassMat_->SetShaderParameter("HeightScale", heightScale);
    grassMat_->SetShaderParameter("TerrainYOffset", terrainYOffset);
    grassMat_->SetShaderParameter("WaterLevel", 5.5f);  // default water level

    // Create the grass node
    grassNode_ = GetNode()->CreateChild("GPUGrass", LOCAL);
    auto* sm = grassNode_->CreateComponent<StaticModel>();
    sm->SetModel(model, true);  // true = override bounding box size check
    sm->SetMaterial(grassMat_);
    sm->SetCastShadows(false);
    sm->SetOccludee(false);
    sm->SetOccluder(false);
    sm->SetDrawDistance(0.0f);      // no distance culling
    sm->SetShadowDistance(0.0f);    // no shadow distance culling

    // The model's bounding box is set large enough to prevent frustum culling
    // (set in BuildGridMesh). The shader handles per-blade visibility.

    unsigned bladeCount = (unsigned)((gridSize / cellSize) * (gridSize / cellSize));
    URHO3D_LOGINFOF("GrassSystem initialized: %u blades, grid %.0fm, cell %.2fm, radius %.0fm",
                    bladeCount, gridSize, cellSize, radius);
}

void GrassSystem::Update(float timeStep)
{
    if (!terrain_ || !grassNode_ || !grassMat_)
        return;

    // Get camera position
    Scene* scene = GetScene();
    if (!scene)
        return;

    // Find the active camera — use the first viewport's camera
    auto* renderer = GetSubsystem<Renderer>();
    if (!renderer || renderer->GetNumViewports() == 0)
        return;

    Viewport* vp = renderer->GetViewport(0);
    if (!vp || !vp->GetCamera())
        return;

    Vector3 camPos = vp->GetCamera()->GetNode()->GetWorldPosition();

    // Snap grass node to grid cell size so blades don't visibly shift
    float snappedX = floorf(camPos.x_ / cellSize_) * cellSize_;
    float snappedZ = floorf(camPos.z_ / cellSize_) * cellSize_;
    grassNode_->SetWorldPosition(Vector3(snappedX, 0.0f, snappedZ));

    // Update the origin uniform to match
    grassMat_->SetShaderParameter("GrassOrigin", Vector3(snappedX, 0.0f, snappedZ));

    // Update wind
    grassMat_->SetShaderParameter("WindStrength", windStrength_);
    grassMat_->SetShaderParameter("WindDirection", windDirection_);

    // Update physics shapes
    grassMat_->SetShaderParameter("PhysShape0", physShapes_[0]);
    grassMat_->SetShaderParameter("PhysShape1", physShapes_[1]);
    grassMat_->SetShaderParameter("PhysShape2", physShapes_[2]);
    grassMat_->SetShaderParameter("PhysShape3", physShapes_[3]);

    // Update seasonal tint
    grassMat_->SetShaderParameter("SeasonalTint", seasonalTint_.ToVector4());
}

void GrassSystem::SetWind(float strength, const Vector2& direction)
{
    windStrength_ = strength;
    windDirection_ = direction;
}

void GrassSystem::SetPhysicsShape(int index, const Vector4& shape)
{
    if (index >= 0 && index < 4)
        physShapes_[index] = shape;
}

void GrassSystem::SetSeasonalTint(const Color& tint)
{
    seasonalTint_ = tint;
}

void GrassSystem::SetRadius(float radius, float fadeWidth)
{
    grassRadius_ = radius;
    grassFadeWidth_ = fadeWidth;
    if (grassMat_)
    {
        grassMat_->SetShaderParameter("GrassRadius", grassRadius_);
        grassMat_->SetShaderParameter("GrassFadeWidth", grassFadeWidth_);
    }
}

void GrassSystem::SetDensityMap(Texture2D* texture)
{
    densityMapTex_ = texture;
    if (grassMat_ && texture)
        grassMat_->SetTexture(TU_SPECULAR, texture);
}

SharedPtr<Model> GrassSystem::BuildGridMesh(float gridSize, float cellSize)
{
    // Grid dimensions
    int gridCells = (int)(gridSize / cellSize);
    float halfGrid = gridSize * 0.5f;

    // Each blade = 1 quad (4 verts, 6 indices)
    // Blade height varies per-blade via iNormal.y
    unsigned bladeCount = (unsigned)(gridCells * gridCells);
    unsigned vertCount = bladeCount * 4;
    unsigned indexCount = bladeCount * 6;

    URHO3D_LOGINFOF("GrassSystem: building mesh — %u blades, %u verts, %u indices",
                    bladeCount, vertCount, indexCount);

    if (vertCount > 4000000)
    {
        URHO3D_LOGWARNING("GrassSystem: very large mesh, consider increasing cell size");
    }

    // Vertex format: Position (vec3) + Normal (vec3) + TexCoord (vec2) = 0x0B
    // We repurpose Normal for per-blade data:
    //   iNormal.x = random hash (0-1)
    //   iNormal.y = blade height scale (0.5-1.5)
    //   iNormal.z = unused (0)

    struct GrassVert
    {
        Vector3 pos;    // xz = grid offset from center, y = height along blade
        Vector3 norm;   // x = random hash, y = blade scale, z = 0
        Vector2 uv;     // x = U, y = height factor (0=base, 1=tip)
    };

    Vector<GrassVert> verts(vertCount);
    Vector<unsigned> indices(indexCount);  // always 32-bit — vertex count exceeds 16-bit range

    unsigned vi = 0;
    unsigned ii = 0;

    // Simple hash function for deterministic per-blade randomness
    auto hashCell = [](int cx, int cz) -> unsigned {
        unsigned h = (unsigned)(cx * 73856093) ^ (unsigned)(cz * 19349663);
        h ^= h >> 13;
        h *= 0x5bd1e995;
        h ^= h >> 15;
        return h;
    };

    const float bladeWidth = 0.15f;  // half-width of blade quad
    const float bladeHeight = 1.2f;  // base blade height (scaled per-blade)

    for (int cz = 0; cz < gridCells; ++cz)
    {
        for (int cx = 0; cx < gridCells; ++cx)
        {
            // Grid offset from center
            float ox = (cx * cellSize) - halfGrid;
            float oz = (cz * cellSize) - halfGrid;

            // Per-blade random
            unsigned h = hashCell(cx, cz);
            float rnd = (h & 0xFFFF) / 65535.0f;
            float scale = 0.5f + ((h >> 16) & 0xFFFF) / 65535.0f;  // 0.5 to 1.5

            // Slight random offset within cell
            float jitterX = ((h & 0xFF) / 255.0f - 0.5f) * cellSize * 0.8f;
            float jitterZ = (((h >> 8) & 0xFF) / 255.0f - 0.5f) * cellSize * 0.8f;
            ox += jitterX;
            oz += jitterZ;

            // Random rotation for blade facing
            float angle = rnd * 3.14159265f;  // 0 to pi
            float bx = sinf(angle) * bladeWidth;
            float bz = cosf(angle) * bladeWidth;

            unsigned base = vi;

            // Bottom-left
            verts[vi++] = {
                Vector3(ox - bx, 0.0f, oz - bz),
                Vector3(rnd, scale, 0.0f),
                Vector2(0.0f, 0.0f)
            };
            // Bottom-right
            verts[vi++] = {
                Vector3(ox + bx, 0.0f, oz + bz),
                Vector3(rnd, scale, 0.0f),
                Vector2(1.0f, 0.0f)
            };
            // Top-right
            verts[vi++] = {
                Vector3(ox + bx, bladeHeight, oz + bz),
                Vector3(rnd, scale, 0.0f),
                Vector2(1.0f, 1.0f)
            };
            // Top-left
            verts[vi++] = {
                Vector3(ox - bx, bladeHeight, oz - bz),
                Vector3(rnd, scale, 0.0f),
                Vector2(0.0f, 1.0f)
            };

            // Two triangles: BL-BR-TR, BL-TR-TL
            indices[ii++] = base + 0;
            indices[ii++] = base + 1;
            indices[ii++] = base + 2;
            indices[ii++] = base + 0;
            indices[ii++] = base + 2;
            indices[ii++] = base + 3;
        }
    }

    // Create vertex buffer
    SharedPtr<VertexBuffer> vb(new VertexBuffer(context_));
    vb->SetShadowed(true);  // required for model to accept the buffer
    Vector<VertexElement> velements;
    velements.Push(VertexElement(TYPE_VECTOR3, SEM_POSITION));
    velements.Push(VertexElement(TYPE_VECTOR3, SEM_NORMAL));
    velements.Push(VertexElement(TYPE_VECTOR2, SEM_TEXCOORD));
    vb->SetSize(vertCount, velements);
    vb->SetData(verts.Buffer());

    // Create index buffer — always 32-bit for grass (vertex count exceeds 16-bit)
    SharedPtr<IndexBuffer> ib(new IndexBuffer(context_));
    ib->SetShadowed(true);  // required for model to accept the buffer
    ib->SetSize(indexCount, true);  // true = 32-bit
    ib->SetData(indices.Buffer());

    // Create geometry
    SharedPtr<Geometry> geom(new Geometry(context_));
    geom->SetVertexBuffer(0, vb);
    geom->SetIndexBuffer(ib);
    geom->SetDrawRange(TRIANGLE_LIST, 0, indexCount);

    // Create model
    SharedPtr<Model> model(new Model(context_));
    Vector<SharedPtr<VertexBuffer>> vbVector;
    vbVector.Push(vb);
    Vector<SharedPtr<IndexBuffer>> ibVector;
    ibVector.Push(ib);
    Vector<i32> emptyMorphStarts;
    Vector<i32> emptyMorphCounts;
    model->SetVertexBuffers(vbVector, emptyMorphStarts, emptyMorphCounts);
    model->SetIndexBuffers(ibVector);
    model->SetNumGeometries(1);
    model->SetNumGeometryLodLevels(0, 1);
    model->SetGeometry(0, 0, geom);

    // Extra large bounding box — the mesh moves with camera, and blades are placed
    // by the vertex shader at terrain height. Make Y range generous.
    BoundingBox bb(Vector3(-halfGrid, -100.0f, -halfGrid), Vector3(halfGrid, 200.0f, halfGrid));
    model->SetBoundingBox(bb);

    return model;
}

SharedPtr<Texture2D> GrassSystem::CreateHeightmapTexture(Terrain* terrain)
{
    Image* heightMap = terrain->GetHeightMap();
    if (!heightMap)
    {
        URHO3D_LOGERROR("GrassSystem — terrain has no heightmap");
        return nullptr;
    }

    // Upload the heightmap image as a GPU texture
    SharedPtr<Texture2D> tex(new Texture2D(context_));
    tex->SetFilterMode(FILTER_BILINEAR);
    tex->SetAddressMode(COORD_U, ADDRESS_CLAMP);
    tex->SetAddressMode(COORD_V, ADDRESS_CLAMP);
    tex->SetData(heightMap);

    URHO3D_LOGINFOF("GrassSystem: heightmap texture %dx%d uploaded for VTF",
                    heightMap->GetWidth(), heightMap->GetHeight());
    return tex;
}
