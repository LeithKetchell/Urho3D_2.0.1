// Copyright (c) 2008-2025 the Urho3D project
// License: MIT
//
// MeshGenerator — creates .mdl files from XML mesh descriptions.
// Usage: MeshGenerator <input.xml> <output.mdl>
//
// XML format example (capsule):
//   <mesh type="capsule" radius="0.5" height="2.0" segments="16" rings="8" />
//
// Supported types: capsule, sphere, cylinder, cone, box
// Can be extended to accept arbitrary vertex/index data in XML/JSON.

#include <Urho3D/Container/ArrayPtr.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Math/BoundingBox.h>
#include <Urho3D/Math/Vector2.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Resource/XMLElement.h>

#ifdef WIN32
#include <Urho3D/Engine/WinWrapped.h>
#endif

#include <Urho3D/DebugNew.h>

#include <cstdio>
#include <cmath>

using namespace Urho3D;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Vertex
{
    Vector3 position;
    Vector3 normal;
    Vector2 uv;
};

static void GenerateCapsule(float radius, float height, int segments, int rings,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices);
static void GenerateSphere(float radius, int segments, int rings,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices);
static void GenerateCylinder(float radius, float height, int segments,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices);
static void GenerateCone(float radius, float height, int segments,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices);
static void GenerateBox(float sizeX, float sizeY, float sizeZ,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices);
static bool WriteMDL(const String& path, const Vector<Vertex>& vertices,
    const Vector<unsigned short>& indices, Context* context);

int main(int argc, char** argv)
{
    Vector<String> arguments;
#ifdef WIN32
    arguments = ParseArguments(GetCommandLineW());
#else
    arguments = ParseArguments(argc, argv);
#endif

    if (arguments.Size() < 2)
    {
        PrintLine("Usage: MeshGenerator <input.xml> <output.mdl>");
        PrintLine("");
        PrintLine("XML format:");
        PrintLine("  <mesh type=\"capsule\" radius=\"0.5\" height=\"2.0\" segments=\"16\" rings=\"8\" />");
        PrintLine("  <mesh type=\"sphere\" radius=\"0.5\" segments=\"16\" rings=\"8\" />");
        PrintLine("  <mesh type=\"cylinder\" radius=\"0.5\" height=\"1.0\" segments=\"16\" />");
        PrintLine("  <mesh type=\"cone\" radius=\"0.5\" height=\"1.0\" segments=\"16\" />");
        PrintLine("  <mesh type=\"box\" sizeX=\"1.0\" sizeY=\"1.0\" sizeZ=\"1.0\" />");
        return 1;
    }

    SharedPtr<Context> context(new Context());
    String inputPath = arguments[0];
    String outputPath = arguments[1];

    // Load XML input
    File inputFile(context, inputPath);
    if (!inputFile.IsOpen())
    {
        PrintLine("ERROR: Could not open " + inputPath);
        return 1;
    }

    XMLFile xmlFile(context);
    if (!xmlFile.Load(inputFile))
    {
        PrintLine("ERROR: Could not parse XML from " + inputPath);
        return 1;
    }

    XMLElement root = xmlFile.GetRoot();
    if (root.IsNull())
    {
        PrintLine("ERROR: Empty XML file");
        return 1;
    }

    String type = root.GetAttribute("type").ToLower();
    Vector<Vertex> vertices;
    Vector<unsigned short> indices;

    if (type == "capsule")
    {
        float radius = root.GetFloat("radius");
        float height = root.GetFloat("height");
        int segments = root.HasAttribute("segments") ? root.GetI32("segments") : 16;
        int rings = root.HasAttribute("rings") ? root.GetI32("rings") : 8;
        if (radius <= 0.0f) radius = 0.5f;
        if (height <= 0.0f) height = 2.0f;
        PrintLine("Generating capsule: radius=" + String(radius) + " height=" + String(height) +
            " segments=" + String(segments) + " rings=" + String(rings));
        GenerateCapsule(radius, height, segments, rings, vertices, indices);
    }
    else if (type == "sphere")
    {
        float radius = root.GetFloat("radius");
        int segments = root.HasAttribute("segments") ? root.GetI32("segments") : 16;
        int rings = root.HasAttribute("rings") ? root.GetI32("rings") : 8;
        if (radius <= 0.0f) radius = 0.5f;
        PrintLine("Generating sphere: radius=" + String(radius) +
            " segments=" + String(segments) + " rings=" + String(rings));
        GenerateSphere(radius, segments, rings, vertices, indices);
    }
    else if (type == "cylinder")
    {
        float radius = root.GetFloat("radius");
        float height = root.GetFloat("height");
        int segments = root.HasAttribute("segments") ? root.GetI32("segments") : 16;
        if (radius <= 0.0f) radius = 0.5f;
        if (height <= 0.0f) height = 1.0f;
        PrintLine("Generating cylinder: radius=" + String(radius) + " height=" + String(height) +
            " segments=" + String(segments));
        GenerateCylinder(radius, height, segments, vertices, indices);
    }
    else if (type == "cone")
    {
        float radius = root.GetFloat("radius");
        float height = root.GetFloat("height");
        int segments = root.HasAttribute("segments") ? root.GetI32("segments") : 16;
        if (radius <= 0.0f) radius = 0.5f;
        if (height <= 0.0f) height = 1.0f;
        PrintLine("Generating cone: radius=" + String(radius) + " height=" + String(height) +
            " segments=" + String(segments));
        GenerateCone(radius, height, segments, vertices, indices);
    }
    else if (type == "box")
    {
        float sizeX = root.HasAttribute("sizeX") ? root.GetFloat("sizeX") : 1.0f;
        float sizeY = root.HasAttribute("sizeY") ? root.GetFloat("sizeY") : 1.0f;
        float sizeZ = root.HasAttribute("sizeZ") ? root.GetFloat("sizeZ") : 1.0f;
        PrintLine("Generating box: " + String(sizeX) + "x" + String(sizeY) + "x" + String(sizeZ));
        GenerateBox(sizeX, sizeY, sizeZ, vertices, indices);
    }
    else
    {
        PrintLine("ERROR: Unknown mesh type '" + type + "'");
        return 1;
    }

    PrintLine("Vertices: " + String(vertices.Size()) + ", Triangles: " + String(indices.Size() / 3));

    if (!WriteMDL(outputPath, vertices, indices, context))
    {
        PrintLine("ERROR: Failed to write " + outputPath);
        return 1;
    }

    PrintLine("Wrote " + outputPath);
    return 0;
}

// Write Urho3D .mdl binary format (UMDL — legacy bitmask format, compatible with all Urho3D versions)
bool WriteMDL(const String& path, const Vector<Vertex>& vertices,
    const Vector<unsigned short>& indices, Context* context)
{
    File file(context, path, FILE_WRITE);
    if (!file.IsOpen())
        return false;

    // Compute bounding box
    BoundingBox bbox;
    for (unsigned i = 0; i < vertices.Size(); ++i)
        bbox.Merge(vertices[i].position);

    // File ID — UMDL uses bitmask vertex elements, the standard Urho3D format
    file.WriteFileID("UMDL");

    // Vertex buffers: 1
    file.WriteU32(1);
    {
        file.WriteU32(vertices.Size()); // vertex count

        // Element mask: Position(1) | Normal(2) | TexCoord1(8) = 0x0B
        file.WriteU32(0x0B);

        // Morph range start, count
        file.WriteU32(0);
        file.WriteU32(0);

        // Vertex data: position(3f) + normal(3f) + texcoord(2f) = 32 bytes per vertex
        for (unsigned i = 0; i < vertices.Size(); ++i)
        {
            file.WriteVector3(vertices[i].position);
            file.WriteVector3(vertices[i].normal);
            file.WriteVector2(vertices[i].uv);
        }
    }

    // Index buffers: 1
    file.WriteU32(1);
    {
        file.WriteU32(indices.Size()); // index count
        file.WriteU32(sizeof(unsigned short)); // index size = 2 bytes

        for (unsigned i = 0; i < indices.Size(); ++i)
            file.WriteU16(indices[i]);
    }

    // Geometries: 1
    file.WriteU32(1);
    {
        // Bone mappings: none
        file.WriteU32(0);

        // LOD levels: 1
        file.WriteU32(1);
        {
            file.WriteFloat(0.0f); // LOD distance
            file.WriteU32(0);      // primitive type: TRIANGLE_LIST
            file.WriteU32(0);      // vertex buffer index
            file.WriteU32(0);      // index buffer index
            file.WriteU32(0);      // index start
            file.WriteU32(indices.Size()); // index count
        }
    }

    // Morphs: none
    file.WriteU32(0);

    // Skeleton: 0 bones
    file.WriteU32(0);

    // Bounding box
    file.WriteVector3(bbox.min_);
    file.WriteVector3(bbox.max_);

    // Geometry centers: 1
    for (unsigned i = 0; i < 1; ++i)
        file.WriteVector3(bbox.Center());

    return true;
}

// Generate a capsule mesh: cylinder body with hemisphere caps
// Centered at origin, extends from -height/2 to +height/2 along Y
void GenerateCapsule(float radius, float height, int segments, int rings,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices)
{
    float cylinderHeight = height - 2.0f * radius;
    if (cylinderHeight < 0.0f) cylinderHeight = 0.0f;

    float halfCyl = cylinderHeight * 0.5f;
    float centerY = 0.0f; // centered at origin

    // Top hemisphere cap
    // From pole (top) down to equator
    for (int ring = 0; ring <= rings / 2; ++ring)
    {
        float latitude = (float)M_PI * 0.5f * (1.0f - (float)ring / (rings / 2)); // pi/2 down to 0
        float y = sinf(latitude) * radius + centerY + halfCyl;
        float ringRadius = cosf(latitude) * radius;

        for (int seg = 0; seg <= segments; ++seg)
        {
            float longitude = 2.0f * (float)M_PI * (float)seg / segments;
            float x = cosf(longitude) * ringRadius;
            float z = sinf(longitude) * ringRadius;

            Vertex v;
            v.position = Vector3(x, y, z);
            v.normal = Vector3(cosf(longitude) * cosf(latitude), sinf(latitude), sinf(longitude) * cosf(latitude));
            v.uv = Vector2((float)seg / segments, 1.0f - (float)ring / rings);
            vertices.Push(v);
        }
    }

    // Cylinder body (just top and bottom rings, sharing positions with hemisphere equators)
    // Bottom ring of cylinder
    {
        float y = centerY - halfCyl;
        for (int seg = 0; seg <= segments; ++seg)
        {
            float longitude = 2.0f * (float)M_PI * (float)seg / segments;
            float x = cosf(longitude) * radius;
            float z = sinf(longitude) * radius;

            Vertex v;
            v.position = Vector3(x, y, z);
            v.normal = Vector3(cosf(longitude), 0.0f, sinf(longitude));
            v.uv = Vector2((float)seg / segments, 0.5f + (float)(rings / 2) / rings);
            vertices.Push(v);
        }
    }

    // Bottom hemisphere cap
    // From equator down to pole (bottom)
    for (int ring = 1; ring <= rings / 2; ++ring)
    {
        float latitude = -(float)M_PI * 0.5f * (float)ring / (rings / 2); // 0 down to -pi/2
        float y = sinf(latitude) * radius + centerY - halfCyl;
        float ringRadius = cosf(latitude) * radius;

        for (int seg = 0; seg <= segments; ++seg)
        {
            float longitude = 2.0f * (float)M_PI * (float)seg / segments;
            float x = cosf(longitude) * ringRadius;
            float z = sinf(longitude) * ringRadius;

            Vertex v;
            v.position = Vector3(x, y, z);
            v.normal = Vector3(cosf(longitude) * cosf(latitude), sinf(latitude), sinf(longitude) * cosf(latitude));
            v.uv = Vector2((float)seg / segments, (float)(rings / 2 + ring) / rings);
            vertices.Push(v);
        }
    }

    // Generate indices
    // Total rings: (rings/2 + 1) top hemisphere + 1 cylinder bottom + (rings/2) bottom hemisphere
    int totalRings = rings / 2 + 1 + 1 + rings / 2;
    int vertsPerRing = segments + 1;

    for (int ring = 0; ring < totalRings - 1; ++ring)
    {
        for (int seg = 0; seg < segments; ++seg)
        {
            unsigned short a = (unsigned short)(ring * vertsPerRing + seg);
            unsigned short b = (unsigned short)(ring * vertsPerRing + seg + 1);
            unsigned short c = (unsigned short)((ring + 1) * vertsPerRing + seg);
            unsigned short d = (unsigned short)((ring + 1) * vertsPerRing + seg + 1);

            indices.Push(a); indices.Push(c); indices.Push(b);
            indices.Push(b); indices.Push(c); indices.Push(d);
        }
    }
}

void GenerateSphere(float radius, int segments, int rings,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices)
{
    for (int ring = 0; ring <= rings; ++ring)
    {
        float latitude = (float)M_PI * ((float)ring / rings - 0.5f);
        float y = sinf(latitude) * radius;
        float ringRadius = cosf(latitude) * radius;

        for (int seg = 0; seg <= segments; ++seg)
        {
            float longitude = 2.0f * (float)M_PI * (float)seg / segments;
            float x = cosf(longitude) * ringRadius;
            float z = sinf(longitude) * ringRadius;

            Vertex v;
            v.position = Vector3(x, y, z);
            v.normal = Vector3(x, y, z).Normalized();
            v.uv = Vector2((float)seg / segments, (float)ring / rings);
            vertices.Push(v);
        }
    }

    int vertsPerRing = segments + 1;
    for (int ring = 0; ring < rings; ++ring)
    {
        for (int seg = 0; seg < segments; ++seg)
        {
            unsigned short a = (unsigned short)(ring * vertsPerRing + seg);
            unsigned short b = (unsigned short)(ring * vertsPerRing + seg + 1);
            unsigned short c = (unsigned short)((ring + 1) * vertsPerRing + seg);
            unsigned short d = (unsigned short)((ring + 1) * vertsPerRing + seg + 1);

            indices.Push(a); indices.Push(c); indices.Push(b);
            indices.Push(b); indices.Push(c); indices.Push(d);
        }
    }
}

void GenerateCylinder(float radius, float height, int segments,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices)
{
    // Two rings: bottom and top
    for (int ring = 0; ring <= 1; ++ring)
    {
        float y = ring * height;
        for (int seg = 0; seg <= segments; ++seg)
        {
            float angle = 2.0f * (float)M_PI * (float)seg / segments;
            Vertex v;
            v.position = Vector3(cosf(angle) * radius, y, sinf(angle) * radius);
            v.normal = Vector3(cosf(angle), 0.0f, sinf(angle));
            v.uv = Vector2((float)seg / segments, (float)ring);
            vertices.Push(v);
        }
    }

    int vertsPerRing = segments + 1;
    for (int seg = 0; seg < segments; ++seg)
    {
        unsigned short a = (unsigned short)seg;
        unsigned short b = (unsigned short)(seg + 1);
        unsigned short c = (unsigned short)(vertsPerRing + seg);
        unsigned short d = (unsigned short)(vertsPerRing + seg + 1);

        indices.Push(a); indices.Push(c); indices.Push(b);
        indices.Push(b); indices.Push(c); indices.Push(d);
    }

    // Top cap
    unsigned short topCenter = (unsigned short)vertices.Size();
    {
        Vertex v;
        v.position = Vector3(0, height, 0);
        v.normal = Vector3(0, 1, 0);
        v.uv = Vector2(0.5f, 0.5f);
        vertices.Push(v);
    }
    for (int seg = 0; seg <= segments; ++seg)
    {
        float angle = 2.0f * (float)M_PI * (float)seg / segments;
        Vertex v;
        v.position = Vector3(cosf(angle) * radius, height, sinf(angle) * radius);
        v.normal = Vector3(0, 1, 0);
        v.uv = Vector2(cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f);
        vertices.Push(v);
    }
    for (int seg = 0; seg < segments; ++seg)
    {
        indices.Push(topCenter);
        indices.Push((unsigned short)(topCenter + 1 + seg + 1));
        indices.Push((unsigned short)(topCenter + 1 + seg));
    }

    // Bottom cap
    unsigned short botCenter = (unsigned short)vertices.Size();
    {
        Vertex v;
        v.position = Vector3(0, 0, 0);
        v.normal = Vector3(0, -1, 0);
        v.uv = Vector2(0.5f, 0.5f);
        vertices.Push(v);
    }
    for (int seg = 0; seg <= segments; ++seg)
    {
        float angle = 2.0f * (float)M_PI * (float)seg / segments;
        Vertex v;
        v.position = Vector3(cosf(angle) * radius, 0, sinf(angle) * radius);
        v.normal = Vector3(0, -1, 0);
        v.uv = Vector2(cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f);
        vertices.Push(v);
    }
    for (int seg = 0; seg < segments; ++seg)
    {
        indices.Push(botCenter);
        indices.Push((unsigned short)(botCenter + 1 + seg));
        indices.Push((unsigned short)(botCenter + 1 + seg + 1));
    }
}

void GenerateCone(float radius, float height, int segments,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices)
{
    // Apex
    unsigned short apex = 0;
    {
        Vertex v;
        v.position = Vector3(0, height, 0);
        v.normal = Vector3(0, 1, 0);
        v.uv = Vector2(0.5f, 0.0f);
        vertices.Push(v);
    }

    // Base ring
    float slopeAngle = atanf(radius / height);
    float ny = sinf(slopeAngle);
    float nxz = cosf(slopeAngle);

    for (int seg = 0; seg <= segments; ++seg)
    {
        float angle = 2.0f * (float)M_PI * (float)seg / segments;
        Vertex v;
        v.position = Vector3(cosf(angle) * radius, 0, sinf(angle) * radius);
        v.normal = Vector3(cosf(angle) * nxz, ny, sinf(angle) * nxz);
        v.uv = Vector2((float)seg / segments, 1.0f);
        vertices.Push(v);
    }

    // Side triangles
    for (int seg = 0; seg < segments; ++seg)
    {
        indices.Push(apex);
        indices.Push((unsigned short)(1 + seg));
        indices.Push((unsigned short)(1 + seg + 1));
    }

    // Bottom cap
    unsigned short botCenter = (unsigned short)vertices.Size();
    {
        Vertex v;
        v.position = Vector3(0, 0, 0);
        v.normal = Vector3(0, -1, 0);
        v.uv = Vector2(0.5f, 0.5f);
        vertices.Push(v);
    }
    for (int seg = 0; seg <= segments; ++seg)
    {
        float angle = 2.0f * (float)M_PI * (float)seg / segments;
        Vertex v;
        v.position = Vector3(cosf(angle) * radius, 0, sinf(angle) * radius);
        v.normal = Vector3(0, -1, 0);
        v.uv = Vector2(cosf(angle) * 0.5f + 0.5f, sinf(angle) * 0.5f + 0.5f);
        vertices.Push(v);
    }
    for (int seg = 0; seg < segments; ++seg)
    {
        indices.Push(botCenter);
        indices.Push((unsigned short)(botCenter + 1 + seg));
        indices.Push((unsigned short)(botCenter + 1 + seg + 1));
    }
}

void GenerateBox(float sizeX, float sizeY, float sizeZ,
    Vector<Vertex>& vertices, Vector<unsigned short>& indices)
{
    float hx = sizeX * 0.5f, hy = sizeY * 0.5f, hz = sizeZ * 0.5f;

    // 6 faces, 4 vertices each = 24 vertices
    struct Face { Vector3 normal; Vector3 corners[4]; };
    Face faces[6] = {
        { Vector3(0,0,1),  { Vector3(-hx,-hy,hz), Vector3(hx,-hy,hz), Vector3(hx,hy,hz), Vector3(-hx,hy,hz) } },
        { Vector3(0,0,-1), { Vector3(hx,-hy,-hz), Vector3(-hx,-hy,-hz), Vector3(-hx,hy,-hz), Vector3(hx,hy,-hz) } },
        { Vector3(1,0,0),  { Vector3(hx,-hy,hz), Vector3(hx,-hy,-hz), Vector3(hx,hy,-hz), Vector3(hx,hy,hz) } },
        { Vector3(-1,0,0), { Vector3(-hx,-hy,-hz), Vector3(-hx,-hy,hz), Vector3(-hx,hy,hz), Vector3(-hx,hy,-hz) } },
        { Vector3(0,1,0),  { Vector3(-hx,hy,hz), Vector3(hx,hy,hz), Vector3(hx,hy,-hz), Vector3(-hx,hy,-hz) } },
        { Vector3(0,-1,0), { Vector3(-hx,-hy,-hz), Vector3(hx,-hy,-hz), Vector3(hx,-hy,hz), Vector3(-hx,-hy,hz) } },
    };
    Vector2 uvs[4] = { Vector2(0,1), Vector2(1,1), Vector2(1,0), Vector2(0,0) };

    for (int f = 0; f < 6; ++f)
    {
        unsigned short base = (unsigned short)vertices.Size();
        for (int c = 0; c < 4; ++c)
        {
            Vertex v;
            v.position = faces[f].corners[c];
            v.normal = faces[f].normal;
            v.uv = uvs[c];
            vertices.Push(v);
        }
        indices.Push(base); indices.Push(base + 1); indices.Push(base + 2);
        indices.Push(base); indices.Push(base + 2); indices.Push(base + 3);
    }
}
