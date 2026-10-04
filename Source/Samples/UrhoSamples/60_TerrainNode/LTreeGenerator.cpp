#include "LTreeGenerator.h"

#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/GraphicsAPI/GraphicsDefs.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>
#include <Urho3D/GraphicsAPI/IndexBuffer.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Math/Random.h>
#include <Urho3D/Math/BoundingBox.h>

// ── Presets ──────────────────────────────────────────────────────────────────

TreePreset LTreeGenerator::Oak()
{
    TreePreset p;
    p.name = "Oak";
    p.axiom = "FFFA";
    p.ruleFrom = "A";
    p.ruleTo = "[+FLA][-FLA][&FLA][^FLA]";
    p.iterations = 3;
    p.angle = 28.0f;
    p.segmentLength = 1.2f;
    p.trunkRadius = 0.25f;
    p.taper = 0.65f;
    p.leafSize = 1.4f;
    p.barkColor = Color(0.35f, 0.2f, 0.1f);
    p.leafColor = Color(0.2f, 0.45f, 0.15f);
    p.evergreen = false;  // deciduous — loses leaves in winter
    p.leafShape = 0;      // broad rounded blobs
    return p;
}

TreePreset LTreeGenerator::Pine()
{
    TreePreset p;
    p.name = "Pine";
    p.axiom = "FFFFA";
    p.ruleFrom = "A";
    p.ruleTo = "F[+FL][-FL][&FL][^FL]A";
    p.iterations = 4;
    p.angle = 40.0f;
    p.segmentLength = 0.8f;
    p.trunkRadius = 0.18f;
    p.taper = 0.6f;
    p.leafSize = 0.8f;
    p.barkColor = Color(0.3f, 0.15f, 0.08f);
    p.leafColor = Color(0.1f, 0.35f, 0.12f);
    p.evergreen = true;  // conifers keep needles
    p.leafShape = 1;     // conical — tall narrow clusters
    return p;
}

TreePreset LTreeGenerator::Eucalyptus()
{
    TreePreset p;
    p.name = "Eucalyptus";
    p.axiom = "FFFFFA";
    p.ruleFrom = "A";
    p.ruleTo = "FF[+FLA][-FLA]";
    p.iterations = 3;
    p.angle = 22.0f;
    p.segmentLength = 1.5f;
    p.trunkRadius = 0.2f;
    p.taper = 0.7f;
    p.leafSize = 1.0f;
    p.barkColor = Color(0.5f, 0.4f, 0.3f);
    p.leafColor = Color(0.25f, 0.5f, 0.2f);
    p.evergreen = true;  // eucalyptus keeps leaves year-round
    p.leafShape = 2;     // droopy — elongated hanging clusters
    return p;
}

TreePreset LTreeGenerator::Acacia()
{
    // Acacia longiflora — flat-topped canopy, warm biome, resin source
    TreePreset p;
    p.name = "Acacia";
    p.axiom = "FFFFA";
    p.ruleFrom = "A";
    p.ruleTo = "F[+FLA][-FLA][&FLA]";
    p.iterations = 3;
    p.angle = 35.0f;       // wide branching angle — flat canopy
    p.segmentLength = 1.2f;
    p.trunkRadius = 0.18f;
    p.taper = 0.65f;
    p.leafSize = 1.4f;     // broad, spreading canopy
    p.barkColor = Color(0.35f, 0.25f, 0.15f);  // dark brown bark
    p.leafColor = Color(0.3f, 0.45f, 0.15f);   // olive green — Australian palette
    p.evergreen = true;    // keeps leaves year-round
    p.leafShape = 0;       // broad rounded clusters
    return p;
}

TreePreset LTreeGenerator::Willow()
{
    // Weeping willow — drooping branches, waterside, fiber/medicine source
    TreePreset p;
    p.name = "Willow";
    p.axiom = "FFFFA";
    p.ruleFrom = "A";
    p.ruleTo = "FF[+FLA][-FLA][&FLA][^FLA]";  // extra downward branching
    p.iterations = 4;
    p.angle = 40.0f;       // wide droop angle
    p.segmentLength = 0.9f;
    p.trunkRadius = 0.2f;
    p.taper = 0.6f;
    p.leafSize = 1.6f;     // long trailing curtains of leaves
    p.barkColor = Color(0.4f, 0.35f, 0.25f);   // pale grey-brown bark
    p.leafColor = Color(0.35f, 0.55f, 0.2f);   // bright yellow-green
    p.evergreen = false;   // deciduous — drops leaves in winter
    p.leafShape = 2;       // droopy elongated clusters
    return p;
}

TreePreset LTreeGenerator::SheOak()
{
    // She-Oak (Casuarina) — needle-like foliage, rough bark, mid-elevation rocky
    TreePreset p;
    p.name = "SheOak";
    p.axiom = "FFFFA";
    p.ruleFrom = "A";
    p.ruleTo = "FF[+FLA][-FLA]";  // sparse branching
    p.iterations = 4;
    p.angle = 25.0f;       // narrow upright form
    p.segmentLength = 1.0f;
    p.trunkRadius = 0.22f;
    p.taper = 0.7f;
    p.leafSize = 0.8f;     // small needle-like clusters
    p.barkColor = Color(0.3f, 0.22f, 0.15f);  // dark rough bark
    p.leafColor = Color(0.2f, 0.35f, 0.18f);  // dark green needles
    p.evergreen = true;    // keeps foliage year-round
    p.leafShape = 1;       // conical — like pine but sparser
    return p;
}

TreePreset LTreeGenerator::FishingRod()
{
    // Fishing rod — single tapered stick, no branching, no leaves.
    // Long thin taper from handle to tip. ~2m real-world length.
    TreePreset p;
    p.name = "FishingRod";
    p.axiom = "FFFFF";        // 5 segments = length
    p.ruleFrom = "X";         // unused rule — no expansion
    p.ruleTo = "X";
    p.iterations = 0;         // no expansion needed
    p.angle = 0.0f;
    p.segmentLength = 0.4f;   // 5 × 0.4 = 2.0 units
    p.trunkRadius = 0.02f;    // thin handle end
    p.taper = 0.55f;          // aggressive taper — tip is very thin
    p.leafSize = 0.0f;        // no leaves
    p.barkColor = Color(0.4f, 0.28f, 0.12f);  // light wood / bamboo
    p.leafColor = Color(0.0f, 0.0f, 0.0f, 0.0f);
    p.evergreen = false;
    p.leafShape = 0;
    return p;
}

// ── L-system expansion ───────────────────────────────────────────────────────

String LTreeGenerator::Expand(const String& axiom, char ruleFrom, const String& ruleTo, int iterations)
{
    String current = axiom;
    for (int i = 0; i < iterations; ++i)
    {
        String next;
        next.Reserve(current.Length() * 4);
        for (unsigned c = 0; c < current.Length(); ++c)
        {
            if (current[c] == ruleFrom)
                next += ruleTo;
            else
                next += current[c];
        }
        current = next;
    }
    return current;
}

// ── Geometry helpers ─────────────────────────────────────────────────────────

void LTreeGenerator::AddCylinder(const Vector3& base, const Vector3& top,
                                  float radiusBase, float radiusTop, int sides,
                                  Vector<float>& verts, Vector<unsigned short>& idx)
{
    Vector3 axis = top - base;
    float length = axis.Length();
    if (length < 0.001f)
        return;

    Vector3 up = axis.Normalized();

    // Find perpendicular vectors
    Vector3 perp = up.CrossProduct(Vector3::RIGHT);
    if (perp.LengthSquared() < 0.01f)
        perp = up.CrossProduct(Vector3::FORWARD);
    perp.Normalize();
    Vector3 perp2 = up.CrossProduct(perp).Normalized();

    unsigned short baseVert = (unsigned short)(verts.Size() / 8);  // 8 floats per vert (pos + normal + uv)

    for (int ring = 0; ring < 2; ++ring)
    {
        Vector3 center = ring == 0 ? base : top;
        float radius = ring == 0 ? radiusBase : radiusTop;
        float v = (float)ring;  // V: 0 at base, 1 at top — tiles per segment

        for (int i = 0; i < sides; ++i)
        {
            float angle = (float)i / (float)sides * 360.0f * (M_PI / 180.0f);
            float u = (float)i / (float)sides;  // U: wraps around circumference
            float cs = cosf(angle);
            float sn = sinf(angle);

            Vector3 offset = perp * cs + perp2 * sn;
            Vector3 pos = center + offset * radius;
            Vector3 normal = offset;

            verts.Push(pos.x_);
            verts.Push(pos.y_);
            verts.Push(pos.z_);
            verts.Push(normal.x_);
            verts.Push(normal.y_);
            verts.Push(normal.z_);
            verts.Push(u);
            verts.Push(v);
        }
    }

    // Triangles connecting the two rings
    for (int i = 0; i < sides; ++i)
    {
        unsigned short a = baseVert + i;
        unsigned short b = baseVert + (i + 1) % sides;
        unsigned short c = baseVert + sides + i;
        unsigned short d = baseVert + sides + (i + 1) % sides;

        idx.Push(a); idx.Push(c); idx.Push(b);
        idx.Push(b); idx.Push(c); idx.Push(d);
    }
}

void LTreeGenerator::AddLeafCluster(const Vector3& center, float size, const Quaternion& orient,
                                     int leafShape, Vector<float>& verts, Vector<unsigned short>& idx,
                                     int maxBlobs)
{
    // Place multiple overlapping octahedrons — shape varies by species
    int numBlobs;
    float sizeMin, sizeRange;
    float spreadH, spreadVmin, spreadVmax;
    float squash;

    switch (leafShape)
    {
    case 1: // Pine — dense conical layers, taller than wide
        numBlobs = 4 + Random(3);
        sizeMin = 0.3f; sizeRange = 0.3f;
        spreadH = 0.3f; spreadVmin = -0.1f; spreadVmax = 0.6f;
        squash = 0.8f;  // taller
        break;
    case 2: // Eucalyptus — droopy elongated clusters hanging down
        numBlobs = 3 + Random(2);
        sizeMin = 0.3f; sizeRange = 0.5f;
        spreadH = 0.6f; spreadVmin = -0.5f; spreadVmax = 0.1f;  // hang downward
        squash = 0.3f;  // very squashed = wide and flat
        break;
    default: // Oak — broad rounded blobs
        numBlobs = 3 + Random(3);
        sizeMin = 0.4f; sizeRange = 0.4f;
        spreadH = 0.5f; spreadVmin = -0.2f; spreadVmax = 0.4f;
        squash = 0.5f;
        break;
    }

    if (maxBlobs > 0 && numBlobs > maxBlobs)
        numBlobs = maxBlobs;

    for (int b = 0; b < numBlobs; ++b)
    {
        unsigned short baseVert = (unsigned short)(verts.Size() / 8);

        float sz = size * (sizeMin + Random(sizeRange));
        float halfH = sz * squash;

        Vector3 blobCenter = center + Vector3(
            Random(-size * spreadH, size * spreadH),
            Random(size * spreadVmin, size * spreadVmax),
            Random(-size * spreadH, size * spreadH));

        Vector3 offsets[6] = {
            orient * Vector3(0.0f,  halfH, 0.0f),
            orient * Vector3(0.0f, -halfH, 0.0f),
            orient * Vector3( sz, 0.0f, 0.0f),
            orient * Vector3(-sz, 0.0f, 0.0f),
            orient * Vector3(0.0f, 0.0f,  sz),
            orient * Vector3(0.0f, 0.0f, -sz),
        };

        // UVs for octahedron vertices: spherical projection
        float leafUVs[6][2] = {
            {0.5f, 0.0f},  // top
            {0.5f, 1.0f},  // bottom
            {1.0f, 0.5f},  // right
            {0.0f, 0.5f},  // left
            {0.5f, 0.5f},  // front
            {0.5f, 0.5f},  // back
        };

        for (int i = 0; i < 6; ++i)
        {
            Vector3 pos = blobCenter + offsets[i];
            pos += Vector3(Random(-sz * 0.2f, sz * 0.2f),
                           Random(-sz * 0.15f, sz * 0.15f),
                           Random(-sz * 0.2f, sz * 0.2f));

            Vector3 n = (pos - blobCenter).Normalized();

            verts.Push(pos.x_);
            verts.Push(pos.y_);
            verts.Push(pos.z_);
            verts.Push(n.x_);
            verts.Push(n.y_);
            verts.Push(n.z_);
            verts.Push(leafUVs[i][0]);
            verts.Push(leafUVs[i][1]);
        }

        const unsigned short faces[8][3] = {
            {0, 2, 4}, {0, 4, 3}, {0, 3, 5}, {0, 5, 2},
            {1, 4, 2}, {1, 3, 4}, {1, 5, 3}, {1, 2, 5},
        };
        for (int f = 0; f < 8; ++f)
        {
            idx.Push(baseVert + faces[f][0]);
            idx.Push(baseVert + faces[f][1]);
            idx.Push(baseVert + faces[f][2]);
        }
    }
}

// ── L-system interpretation ──────────────────────────────────────────────────

void LTreeGenerator::Interpret(const String& commands, const TreePreset& preset,
                                Vector<float>& trunkVerts, Vector<unsigned short>& trunkIdx,
                                Vector<float>& leafVerts, Vector<unsigned short>& leafIdx,
                                int sides, int maxLeafBlobs)
{
    Vector<TurtleState> stack;
    TurtleState turtle;
    turtle.position = Vector3::ZERO;
    turtle.orientation = Quaternion::IDENTITY;
    turtle.radius = preset.trunkRadius;
    turtle.depth = 0;

    const int SIDES = sides;

    for (unsigned i = 0; i < commands.Length(); ++i)
    {
        char c = commands[i];

        switch (c)
        {
        case 'F':
        {
            // Move forward, draw trunk/branch segment
            float jitter = 1.0f + Random(-0.15f, 0.15f);  // length variation
            float len = preset.segmentLength * jitter;
            Vector3 forward = turtle.orientation * Vector3::UP * len;
            Vector3 newPos = turtle.position + forward;
            float newRadius = turtle.radius * (0.92f + Random(0.06f));  // slight per-segment taper

            AddCylinder(turtle.position, newPos, turtle.radius, newRadius, SIDES,
                        trunkVerts, trunkIdx);

            turtle.position = newPos;
            turtle.radius = newRadius;
            break;
        }
        case '+':
        {
            float a = preset.angle + Random(-5.0f, 5.0f);
            turtle.orientation = turtle.orientation * Quaternion(a, Vector3::FORWARD);
            break;
        }
        case '-':
        {
            float a = preset.angle + Random(-5.0f, 5.0f);
            turtle.orientation = turtle.orientation * Quaternion(-a, Vector3::FORWARD);
            break;
        }
        case '&':
        {
            float a = preset.angle + Random(-5.0f, 5.0f);
            turtle.orientation = turtle.orientation * Quaternion(a, Vector3::RIGHT);
            break;
        }
        case '^':
        {
            float a = preset.angle + Random(-5.0f, 5.0f);
            turtle.orientation = turtle.orientation * Quaternion(-a, Vector3::RIGHT);
            break;
        }
        case '/':
        {
            float a = 30.0f + Random(-10.0f, 10.0f);
            turtle.orientation = turtle.orientation * Quaternion(a, Vector3::UP);
            break;
        }
        case '\\':
        {
            float a = 30.0f + Random(-10.0f, 10.0f);
            turtle.orientation = turtle.orientation * Quaternion(-a, Vector3::UP);
            break;
        }
        case '[':
        {
            stack.Push(turtle);
            turtle.depth++;
            turtle.radius *= preset.taper;
            // Add roll variation at each branch point
            float roll = Random(-45.0f, 45.0f);
            turtle.orientation = turtle.orientation * Quaternion(roll, Vector3::UP);
            break;
        }
        case ']':
        {
            if (!stack.Empty())
            {
                turtle = stack.Back();
                stack.Pop();
            }
            break;
        }
        case 'L':
        {
            // Place leaf cluster at current position — shape varies by species
            AddLeafCluster(turtle.position, preset.leafSize, turtle.orientation,
                           preset.leafShape, leafVerts, leafIdx, maxLeafBlobs);
            break;
        }
        case 'A':
            // Growth point — consumed during expansion, ignore at runtime
            break;
        default:
            break;
        }
    }
}

// ── Model building ───────────────────────────────────────────────────────────

SharedPtr<Model> LTreeGenerator::BuildModel(Context* context,
                                             const Vector<float>& trunkVerts, const Vector<unsigned short>& trunkIdx,
                                             const Vector<float>& leafVerts, const Vector<unsigned short>& leafIdx)
{
    SharedPtr<Model> model(new Model(context));

    // Compute bounding box from all vertices (8 floats per vert: pos + normal + uv)
    BoundingBox bbox;
    for (unsigned i = 0; i < trunkVerts.Size(); i += 8)
        bbox.Merge(Vector3(trunkVerts[i], trunkVerts[i + 1], trunkVerts[i + 2]));
    for (unsigned i = 0; i < leafVerts.Size(); i += 8)
        bbox.Merge(Vector3(leafVerts[i], leafVerts[i + 1], leafVerts[i + 2]));

    int numGeometries = 0;
    if (!trunkIdx.Empty()) numGeometries++;
    if (!leafIdx.Empty()) numGeometries++;
    if (numGeometries == 0)
        return model;

    model->SetNumGeometries(numGeometries);
    model->SetBoundingBox(bbox);

    Vector<SharedPtr<VertexBuffer>> allVB;
    Vector<SharedPtr<IndexBuffer>> allIB;

    int geoIndex = 0;

    // Trunk geometry (material slot 0)
    if (!trunkIdx.Empty())
    {
        SharedPtr<VertexBuffer> vb(new VertexBuffer(context));
        vb->SetShadowed(true);
        vb->SetSize(trunkVerts.Size() / 8, VertexElements::Position | VertexElements::Normal | VertexElements::TexCoord1);
        vb->SetData(trunkVerts.Buffer());

        SharedPtr<IndexBuffer> ib(new IndexBuffer(context));
        ib->SetShadowed(true);
        ib->SetSize(trunkIdx.Size(), false);
        ib->SetData(trunkIdx.Buffer());

        SharedPtr<Geometry> geom(new Geometry(context));
        geom->SetVertexBuffer(0, vb);
        geom->SetIndexBuffer(ib);
        geom->SetDrawRange(TRIANGLE_LIST, 0, trunkIdx.Size());

        model->SetGeometry(geoIndex, 0, geom);

        allVB.Push(vb);
        allIB.Push(ib);
        geoIndex++;
    }

    // Leaf geometry (material slot 1)
    if (!leafIdx.Empty())
    {
        SharedPtr<VertexBuffer> vb(new VertexBuffer(context));
        vb->SetShadowed(true);
        vb->SetSize(leafVerts.Size() / 8, VertexElements::Position | VertexElements::Normal | VertexElements::TexCoord1);
        vb->SetData(leafVerts.Buffer());

        SharedPtr<IndexBuffer> ib(new IndexBuffer(context));
        ib->SetShadowed(true);
        ib->SetSize(leafIdx.Size(), false);
        ib->SetData(leafIdx.Buffer());

        SharedPtr<Geometry> geom(new Geometry(context));
        geom->SetVertexBuffer(0, vb);
        geom->SetIndexBuffer(ib);
        geom->SetDrawRange(TRIANGLE_LIST, 0, leafIdx.Size());

        model->SetGeometry(geoIndex, 0, geom);

        allVB.Push(vb);
        allIB.Push(ib);
    }

    // Register buffers on model to keep them alive
    Vector<i32> morphStarts(allVB.Size(), 0);
    Vector<i32> morphCounts(allVB.Size(), 0);
    model->SetVertexBuffers(allVB, morphStarts, morphCounts);
    model->SetIndexBuffers(allIB);

    return model;
}

// ── Public API ───────────────────────────────────────────────────────────────

SharedPtr<Model> LTreeGenerator::Generate(Context* context, const TreePreset& preset)
{
    if (preset.ruleFrom.Empty())
        return SharedPtr<Model>();

    String expanded = Expand(preset.axiom, preset.ruleFrom[0], preset.ruleTo, preset.iterations);

    Vector<float> trunkVerts, leafVerts;
    Vector<unsigned short> trunkIdx, leafIdx;

    Interpret(expanded, preset, trunkVerts, trunkIdx, leafVerts, leafIdx);

    URHO3D_LOGINFOF("LTreeGenerator [%s]: %u trunk tris, %u leaf tris, string length %u",
                    preset.name.CString(),
                    trunkIdx.Size() / 3, leafIdx.Size() / 3,
                    expanded.Length());

    return BuildModel(context, trunkVerts, trunkIdx, leafVerts, leafIdx);
}

SharedPtr<Model> LTreeGenerator::GenerateLOD(Context* context, const TreePreset& preset)
{
    if (preset.ruleFrom.Empty())
        return SharedPtr<Model>();

    // Keep full iterations to preserve branching silhouette.
    // Reduce polygon count by cutting cylinder sides (6 → 4) and
    // capping leaf blobs per cluster (2 max). This looks far better
    // at distance than the old approach of dropping an iteration
    // (which collapsed the tree into a featureless blob).
    String expanded = Expand(preset.axiom, preset.ruleFrom[0], preset.ruleTo, preset.iterations);

    Vector<float> trunkVerts, leafVerts;
    Vector<unsigned short> trunkIdx, leafIdx;

    Interpret(expanded, preset, trunkVerts, trunkIdx, leafVerts, leafIdx,
              4,   // sides: square cross-section (was 6)
              2);  // maxLeafBlobs: 2 per cluster (was 3-7)

    URHO3D_LOGINFOF("LTreeGenerator [%s LOD1]: %u trunk tris, %u leaf tris",
                    preset.name.CString(),
                    trunkIdx.Size() / 3, leafIdx.Size() / 3);

    return BuildModel(context, trunkVerts, trunkIdx, leafVerts, leafIdx);
}
