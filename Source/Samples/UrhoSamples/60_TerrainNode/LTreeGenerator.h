#pragma once

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Math/Quaternion.h>
#include <Urho3D/Container/Str.h>
#include <Urho3D/Container/Vector.h>

using namespace Urho3D;

/// L-system species preset.
struct TreePreset
{
    String name;
    String axiom;
    String ruleFrom;    // single character to replace
    String ruleTo;      // replacement string
    int iterations;
    float angle;        // branch angle in degrees
    float segmentLength;
    float trunkRadius;
    float taper;        // radius multiplier per depth level (0.6-0.8 typical)
    float leafSize;     // radius of leaf clusters
    Color barkColor;
    Color leafColor;
    bool evergreen;     // true = keeps leaves year-round (pine, eucalyptus)

    /// Leaf shape controls how clusters are generated:
    /// 0 = broad (oak — fat rounded blobs)
    /// 1 = conical (pine — tall narrow clusters, dense layers)
    /// 2 = droopy (eucalyptus — elongated downward-hanging clusters)
    int leafShape;
};

/// Generates a tree Model from L-system rules.
/// Output: Model with 2 geometries — trunk/branches (slot 0) and leaf clusters (slot 1).
class LTreeGenerator
{
public:
    /// Generate a tree model from a preset (full detail).
    static SharedPtr<Model> Generate(Context* context, const TreePreset& preset);

    /// Generate a reduced-detail model for LOD1 (fewer iterations, fewer leaf blobs).
    static SharedPtr<Model> GenerateLOD(Context* context, const TreePreset& preset);

    /// Built-in presets.
    static TreePreset Oak();
    static TreePreset Pine();
    static TreePreset Eucalyptus();
    static TreePreset Acacia();
    static TreePreset Willow();
    static TreePreset SheOak();
    static TreePreset FishingRod();

private:
    /// Turtle state for L-system interpretation.
    struct TurtleState
    {
        Vector3 position;
        Quaternion orientation;
        float radius;
        int depth;
    };

    /// Expand L-system string by applying production rules.
    static String Expand(const String& axiom, char ruleFrom, const String& ruleTo, int iterations);

    /// Interpret expanded string into geometry.
    /// sides: cylinder cross-section sides (6 = full detail, 4 = LOD).
    /// maxLeafBlobs: cap leaf blobs per cluster (0 = no cap).
    static void Interpret(const String& commands, const TreePreset& preset,
                          Vector<float>& trunkVerts, Vector<unsigned short>& trunkIdx,
                          Vector<float>& leafVerts, Vector<unsigned short>& leafIdx,
                          int sides = 6, int maxLeafBlobs = 0);

    /// Add a cylinder segment (trunk/branch piece).
    static void AddCylinder(const Vector3& base, const Vector3& top, float radiusBase, float radiusTop,
                            int sides, Vector<float>& verts, Vector<unsigned short>& idx);

    /// Add a leaf cluster (shape varies by species).
    /// maxBlobs: cap number of blobs per cluster (0 = no cap).
    static void AddLeafCluster(const Vector3& center, float size, const Quaternion& orient,
                               int leafShape, Vector<float>& verts, Vector<unsigned short>& idx,
                               int maxBlobs = 0);

    /// Build a Model from trunk + leaf vertex/index data.
    static SharedPtr<Model> BuildModel(Context* context,
                                        const Vector<float>& trunkVerts, const Vector<unsigned short>& trunkIdx,
                                        const Vector<float>& leafVerts, const Vector<unsigned short>& leafIdx);
};
