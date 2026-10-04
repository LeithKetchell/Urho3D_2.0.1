// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Core/WorkQueue.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/GraphicsAPI/IndexBuffer.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#ifdef URHO3D_PHYSICS
#include <Urho3D/Physics/PhysicsWorld.h>
#endif
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>

#ifdef WIN32
#include <Urho3D/Engine/WinWrapped.h>
#endif

#include <assimp/config.h>
#include <assimp/cimport.h>
#include <assimp/cexport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/DefaultLogger.hpp>

#include <Urho3D/DebugNew.h>

using namespace Urho3D;

struct OutModel
{
    String outName_;
    aiNode* rootNode_{};
    HashSet<unsigned> meshIndices_;
    Vector<aiMesh*> meshes_;
    Vector<aiNode*> meshNodes_;
    Vector<aiNode*> bones_;
    Vector<aiNode*> pivotlessBones_;
    Vector<aiAnimation*> animations_;
    Vector<float> boneRadii_;
    Vector<BoundingBox> boneHitboxes_;
    aiNode* rootBone_{};
    unsigned totalVertices_{};
    unsigned totalIndices_{};
};

struct OutScene
{
    String outName_;
    aiNode* rootNode_{};
    Vector<OutModel> models_;
    Vector<aiNode*> nodes_;
    Vector<unsigned> nodeModelIndices_;
};

// FBX transform chain
enum TransformationComp
{
    TransformationComp_Translation = 0,
    TransformationComp_RotationOffset,
    TransformationComp_RotationPivot,
    TransformationComp_PreRotation,
    TransformationComp_Rotation,
    TransformationComp_PostRotation,
    TransformationComp_RotationPivotInverse,

    TransformationComp_ScalingOffset,
    TransformationComp_ScalingPivot,
    TransformationComp_Scaling,

    // Not checking these
    // They are typically flushed out in the fbxconverter, but there
    // might be cases where they're not, hence, leaving them.
    #ifdef EXT_TRANSFORMATION_CHECK
    TransformationComp_ScalingPivotInverse,
    TransformationComp_GeometricTranslation,
    TransformationComp_GeometricRotation,
    TransformationComp_GeometricScaling,
    #endif

    TransformationComp_MAXIMUM
};

const char *transformSuffix[TransformationComp_MAXIMUM] =
{
    "Translation",          // TransformationComp_Translation = 0,
    "RotationOffset",       // TransformationComp_RotationOffset,
    "RotationPivot",        // TransformationComp_RotationPivot,
    "PreRotation",          // TransformationComp_PreRotation,
    "Rotation",             // TransformationComp_Rotation,
    "PostRotation",         // TransformationComp_PostRotation,
    "RotationPivotInverse", // TransformationComp_RotationPivotInverse,

    "ScalingOffset",        // TransformationComp_ScalingOffset,
    "ScalingPivot",         // TransformationComp_ScalingPivot,
    "Scaling",              // TransformationComp_Scaling,

    #ifdef EXT_TRANSFORMATION_CHECK
    "ScalingPivotInverse",  // TransformationComp_ScalingPivotInverse,
    "GeometricTranslation", // TransformationComp_GeometricTranslation,
    "GeometricRotation",    // TransformationComp_GeometricRotation,
    "GeometricScaling",     // TransformationComp_GeometricScaling,
    #endif
};

static const unsigned MAX_CHANNELS = 4;

SharedPtr<Context> context_(new Context());
const aiScene* scene_ = nullptr;
aiNode* rootNode_ = nullptr;
String inputName_;
String resourcePath_;
String outPath_;
String outName_;
bool useSubdirs_ = true;
bool localIDs_ = false;
bool saveBinary_ = false;
bool saveJson_ = false;
bool createZone_ = true;
bool noAnimations_ = false;
bool noHierarchy_ = false;
bool noMaterials_ = false;
bool noTextures_ = false;
bool noMaterialDiffuseColor_ = false;
bool noEmptyNodes_ = false;
bool saveMaterialList_ = false;
bool includeNonSkinningBones_ = false;
bool verboseLog_ = false;
bool emissiveAO_ = false;
bool noOverwriteMaterial_ = false;
bool noOverwriteTexture_ = false;
bool noOverwriteNewerTexture_ = false;
bool checkUniqueModel_ = true;
bool moveToBindPose_ = false;
unsigned maxBones_ = 64;
Vector<String> nonSkinningBoneIncludes_;
Vector<String> nonSkinningBoneExcludes_;

HashSet<aiAnimation*> allAnimations_;
Vector<aiAnimation*> sceneAnimations_;

float defaultTicksPerSecond_ = 4800.0f;
// For subset animation import usage
float importStartTime_ = 0.0f;
float importEndTime_ = 0.0f;
bool suppressFbxPivotNodes_ = true;
float importScale_ = 1.0f;
float normalizeTarget_ = 0.0f;   // 0 = disabled, >0 = target size in units
int normalizeAxis_ = -1;         // -1 = largest dimension, 0 = X, 1 = Y, 2 = Z
Vector<String> exportAnimPaths_;
bool exportAllAnims_ = false;
bool bakeScale_ = false;
bool forceZForwards_ = false;
// Per-bone original uniform scale factors, recorded before bake for animation correction
HashMap<String, float> originalBoneScales_;
// Per-bone original local transforms, saved before bake so animation fallback uses unbaked positions
HashMap<String, aiMatrix4x4> originalBoneTransforms_;
float rootBoneScale_ = 1.0f;

int main(int argc, char** argv);
void Run(const Vector<String>& arguments);
void DumpNodes(aiNode* rootNode, unsigned level);

void ExportModel(const String& outName, bool animationOnly);
void ExportAnimation(const String& outName, bool animationOnly);
void CollectMeshes(OutModel& model, aiNode* node);
void CollectBones(OutModel& model, bool animationOnly = false);
void CollectBonesFinal(Vector<aiNode*>& dest, const HashSet<aiNode*>& necessary, aiNode* node);
void MoveToBindPose(OutModel& model, aiNode* current);
void DetectBoneScale(OutModel& model);
void BakeSkeletonScale(OutModel& model);
void AutoFaceZPlus(OutModel& model);
void CollectAnimations(OutModel* model = nullptr);
void BuildBoneCollisionInfo(OutModel& model);
void BuildAndSaveModel(OutModel& model);
void BuildAndSaveAnimations(OutModel* model = nullptr);

void ExportScene(const String& outName, bool asPrefab);
void CollectSceneModels(OutScene& scene, aiNode* node);
void CreateHierarchy(Scene* scene, aiNode* srcNode, HashMap<aiNode*, Node*>& nodeMapping);
Node* CreateSceneNode(Scene* scene, aiNode* srcNode, HashMap<aiNode*, Node*>& nodeMapping);
void BuildAndSaveScene(OutScene& scene, bool asPrefab);

void CollectMaterialTextures(HashSet<String>& usedTextures);
void ExportMaterials(HashSet<String>& usedTextures);
void BuildAndSaveMaterial(aiMaterial* material, HashSet<String>& usedTextures);
void CopyTextures(const HashSet<String>& usedTextures, const String& sourcePath);

void CombineLods(const Vector<float>& lodDistances, const Vector<String>& modelNames, const String& outName);

void GetMeshesUnderNode(Vector<Pair<aiNode*, aiMesh*>>& dest, aiNode* node);
unsigned GetMeshIndex(aiMesh* mesh);
unsigned GetBoneIndex(OutModel& model, const String& boneName);
aiBone* GetMeshBone(OutModel& model, const String& boneName);
Matrix3x4 GetOffsetMatrix(OutModel& model, const String& boneName);
void GetBlendData(OutModel& model, aiMesh* mesh, aiNode* meshNode, Vector<i32>& boneMappings, Vector<Vector<unsigned char>>&
    blendIndices, Vector<Vector<float>>& blendWeights);
String GetMeshMaterialName(aiMesh* mesh);
String GetMaterialTextureName(const String& nameIn);
String GenerateMaterialName(aiMaterial* material);
String GenerateTextureName(unsigned texIndex);
unsigned GetNumValidFaces(aiMesh* mesh);

void WriteShortIndices(unsigned short*& dest, aiMesh* mesh, unsigned index, unsigned offset);
void WriteLargeIndices(unsigned*& dest, aiMesh* mesh, unsigned index, unsigned offset);
void WriteVertex(float*& dest, aiMesh* mesh, unsigned index, bool isSkinned, BoundingBox& box,
    const Matrix3x4& vertexTransform, const Matrix3& normalTransform, Vector<Vector<unsigned char>>& blendIndices,
    Vector<Vector<float>>& blendWeights);
Vector<VertexElement> GetVertexElements(aiMesh* mesh, bool isSkinned);

aiNode* GetNode(const String& name, aiNode* rootNode, bool caseSensitive = true);
aiMatrix4x4 GetDerivedTransform(aiNode* node, aiNode* rootNode, bool rootInclusive = true);
aiMatrix4x4 GetDerivedTransform(aiMatrix4x4 transform, aiNode* node, aiNode* rootNode, bool rootInclusive = true);
aiMatrix4x4 GetMeshBakingTransform(aiNode* meshNode, aiNode* modelRootNode);
void GetPosRotScale(const aiMatrix4x4& transform, Vector3& pos, Quaternion& rot, Vector3& scale);

String FromAIString(const aiString& str);
Vector3 ToVector3(const aiVector3D& vec);
Vector2 ToVector2(const aiVector2D& vec);
Quaternion ToQuaternion(const aiQuaternion& quat);
Matrix3x4 ToMatrix3x4(const aiMatrix4x4& mat);
aiMatrix4x4 ToAIMatrix4x4(const Matrix3x4& mat);
String SanitateAssetName(const String& name);

unsigned GetPivotlessBoneIndex(OutModel& model, const String& boneName);
void ExtrapolatePivotlessAnimation(OutModel* model);
void CollectSceneNodesAsBones(OutModel &model, aiNode* rootNode);
void ExportMdlToFBX(const String& inFile, const String& outFile);

int main(int argc, char** argv)
{
    Vector<String> arguments;

    #ifdef WIN32
    arguments = ParseArguments(GetCommandLineW());
    #else
    arguments = ParseArguments(argc, argv);
    #endif

    Run(arguments);
    return 0;
}

void Run(const Vector<String>& arguments)
{
    if (arguments.Size() < 2)
    {
        ErrorExit(
            "Usage: AssetTool <command> <input file> <output file> [options]\n"
            "See http://assimp.sourceforge.net/main_features_formats.html for input formats\n\n"
            "Commands:\n"
            "model       Output a model\n"
            "anim        Output animation(s)\n"
            "scene       Output a scene\n"
            "node        Output a node and its children (prefab)\n"
            "dump        Dump scene node structure. No output file is generated\n"
            "info        Show model/animation info. Supports native .mdl/.ani and all Assimp formats\n"
            "export      Export native .mdl to FBX (with optional animations)\n"
            "            Syntax: export <input.mdl> <output.fbx> [options]\n"
            "lod         Combine several Urho3D models as LOD levels of the output model\n"
            "            Syntax: lod <dist0> <mdl0> <dist1 <mdl1> ... <output file>\n"
            "\n"
            "Options:\n"
            "-b          Save scene in binary format, default format is XML\n"
            "-j          Save scene in JSON format, default format is XML\n"
            "-h          Generate hard instead of smooth normals if input has no normals\n"
            "-i          Use local ID's for scene nodes\n"
            "-l          Output a material list file for models\n"
            "-na         Do not output animations\n"
            "-nm         Do not output materials\n"
            "-nt         Do not output material textures\n"
            "-nc         Do not use material diffuse color value, instead output white\n"
            "-nh         Do not save full node hierarchy (scene mode only)\n"
            "-ns         Do not create subdirectories for resources\n"
            "-nz         Do not create a zone and a directional light (scene mode only)\n"
            "-nf         Do not fix infacing normals\n"
            "-ne         Do not save empty nodes (scene mode only)\n"
            "-mb <x>     Maximum number of bones per submesh. Default 64\n"
            "-p <path>   Set path for scene resources. Default is output file path\n"
            "-r <name>   Use the named scene node as root node\n"
            "-f <freq>   Animation tick frequency to use if unspecified. Default 4800\n"
            "-o          Optimize redundant submeshes. Loses scene hierarchy and animations\n"
            "-s <filter> Include non-skinning bones in the model's skeleton. Can be given a\n"
            "            case-insensitive semicolon separated filter list. Bone is included\n"
            "            if its name contains any of the filters. Prefix filter with minus\n"
            "            sign to use as an exclude. For example -s \"Bip01;-Dummy;-Helper\"\n"
            "-t          Generate tangents\n"
            "-v          Enable verbose Assimp library logging\n"
            "-eao        Interpret material emissive texture as ambient occlusion\n"
            "-cm         Check and do not overwrite if material exists\n"
            "-ct         Check and do not overwrite if texture exists\n"
            "-ctn        Check and do not overwrite if texture has newer timestamp\n"
            "-am         Export all meshes even if identical (scene mode only)\n"
            "-bp         Move bones to bind pose before saving model\n"
            "-split <start> <end> (animation model only)\n"
            "            Split animation, will only import from start frame to end frame\n"
            "-np         Do not suppress $fbx pivot nodes (FBX files only)\n"
            "-scale <x>  Scale geometry and animation translations by the given factor\n"
            "            For standalone anim FBX: use the same scale as the model import\n"
            "-normalize <size> [X|Y|Z]  Auto-scale so the given axis equals <size>\n"
            "            If axis omitted: uses Y when skeleton detected (hip+neck),\n"
            "            otherwise largest axis. Uniform scale preserves proportions.\n"
            "            -normalize 1.8 Y    Scale so Y height = 1.8 (human male)\n"
            "            -normalize 1.5      Deer: auto-detects skeleton, uses Y = 1.5m\n"
            "-bake-scale  Bake non-identity bone scale into geometry and fix skeleton/animations\n"
            "            Removes unit-conversion artifacts (e.g. 2.54 imperial-to-metric,\n"
            "            100.0 Blender default). Adjusts child bone positions and animation\n"
            "            keyframes to compensate. Combine with -normalize for correct sizing.\n"
            "            Example: -bake-scale -normalize 1.8 Y  (imperial biped to 1.8m)\n"
            "-forcezforwards  Auto-rotate model to face Z+ using skeleton analysis\n"
            "            Quadrupeds: projects hip-to-neck onto XZ, snaps to nearest 90-deg.\n"
            "            Bipeds: uses cross(spine, arm) to find forward. If arms are not in\n"
            "            a T-pose (e.g. Mixamo bind pose), synthesizes a temporary T-pose\n"
            "            from arm-chain bones to compute the cross product correctly.\n"
            "-anim <path.ani> Include animation file(s) in FBX export (repeatable)\n"
            "-allanims    Auto-discover and include all matching .ani files (export only)\n"
        );
    }

    context_->RegisterSubsystem(new FileSystem(context_));
    context_->RegisterSubsystem(new ResourceCache(context_));
    context_->RegisterSubsystem(new WorkQueue(context_));
    RegisterSceneLibrary(context_);
    RegisterGraphicsLibrary(context_);
#ifdef URHO3D_PHYSICS
    RegisterPhysicsLibrary(context_);
#endif

    String command = arguments[0].ToLower();
    String rootNodeName;

    unsigned flags =
        aiProcess_ConvertToLeftHanded |
        aiProcess_JoinIdenticalVertices |
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_LimitBoneWeights |
        aiProcess_ImproveCacheLocality |
        aiProcess_RemoveRedundantMaterials |
        aiProcess_FixInfacingNormals |
        aiProcess_FindInvalidData |
        aiProcess_GenUVCoords |
        aiProcess_FindInstances |
        aiProcess_OptimizeMeshes;

    for (unsigned i = 2; i < arguments.Size(); ++i)
    {
        if (arguments[i].Length() > 1 && arguments[i][0] == '-')
        {
            String argument = arguments[i].Substring(1).ToLower();
            String value = i + 1 < arguments.Size() ? arguments[i + 1] : String::EMPTY;

            if (argument == "b")
                saveBinary_ = true;
            else if(argument == "j")
                saveJson_ = true;
            else if (argument == "h")
            {
                flags &= ~aiProcess_GenSmoothNormals;
                flags |= aiProcess_GenNormals;
            }
            else if (argument == "i")
                localIDs_ = true;
            else if (argument == "l")
                saveMaterialList_ = true;
            else if (argument == "t")
                flags |= aiProcess_CalcTangentSpace;
            else if (argument == "o")
                flags |= aiProcess_PreTransformVertices;
            else if (argument.Length() == 2 && argument[0] == 'n')
            {
                switch (tolower(argument[1]))
                {
                case 'a':
                    noAnimations_ = true;
                    break;

                case 'c':
                    noMaterialDiffuseColor_ = true;
                    break;

                case 'm':
                    noMaterials_ = true;
                    break;

                case 'h':
                    noHierarchy_ = true;
                    break;

                case 'e':
                    noEmptyNodes_ = true;
                    break;

                case 's':
                    useSubdirs_ = false;
                    break;

                case 't':
                    noTextures_ = true;
                    break;

                case 'z':
                    createZone_ = false;
                    break;

                case 'f':
                    flags &= ~aiProcess_FixInfacingNormals;
                    break;

                case 'p':
                        suppressFbxPivotNodes_ = false;
                    break;

                }
            }
            else if (argument == "mb" && !value.Empty())
            {
                maxBones_ = ToU32(value);
                if (maxBones_ < 1)
                    maxBones_ = 1;
                ++i;
            }
            else if (argument == "p" && !value.Empty())
            {
                resourcePath_ = AddTrailingSlash(value);
                ++i;
            }
            else if (argument == "r" && !value.Empty())
            {
                rootNodeName = value;
                ++i;
            }
            else if (argument == "f" && !value.Empty())
            {
                defaultTicksPerSecond_ = ToFloat(value);
                ++i;
            }
            else if (argument == "s")
            {
                includeNonSkinningBones_ = true;
                if (value.Length() && (value[0] != '-' || value.Length() > 3))
                {
                    Vector<String> filters = value.Split(';');
                    for (unsigned i = 0; i < filters.Size(); ++i)
                    {
                        if (filters[i][0] == '-')
                            nonSkinningBoneExcludes_.Push(filters[i].Substring(1));
                        else
                            nonSkinningBoneIncludes_.Push(filters[i]);
                    }
                }
            }
            else if (argument == "v")
                verboseLog_ = true;
            else if (argument == "eao")
                emissiveAO_ = true;
            else if (argument == "cm")
                noOverwriteMaterial_ = true;
            else if (argument == "ct")
                noOverwriteTexture_ = true;
            else if (argument == "ctn")
                noOverwriteNewerTexture_ = true;
            else if (argument == "am")
                checkUniqueModel_ = false;
            else if (argument == "bp")
                moveToBindPose_ = true;
            else if (argument == "scale" && !value.Empty())
            {
                importScale_ = ToFloat(value);
                ++i;
            }
            else if (argument == "normalize" && !value.Empty())
            {
                normalizeTarget_ = ToFloat(value);
                ++i;
                // Optional axis parameter
                if (i + 1 < arguments.Size())
                {
                    String axis = arguments[i + 1].ToUpper();
                    if (axis == "X") { normalizeAxis_ = 0; ++i; }
                    else if (axis == "Y") { normalizeAxis_ = 1; ++i; }
                    else if (axis == "Z") { normalizeAxis_ = 2; ++i; }
                }
            }
            else if (argument == "anim" && !value.Empty())
            {
                exportAnimPaths_.Push(value);
                ++i;
            }
            else if (argument == "allanims")
                exportAllAnims_ = true;
            else if (argument == "bake-scale")
                bakeScale_ = true;
            else if (argument == "forcezforwards")
                forceZForwards_ = true;
            else if (argument == "split")
            {
                String value2 = i + 2 < arguments.Size() ? arguments[i + 2] : String::EMPTY;
                if (value.Length() && value2.Length() && (value[0] != '-') && (value2[0] != '-'))
                {
                    importStartTime_ = ToFloat(value);
                    importEndTime_ = ToFloat(value2);
                }
            }
        }
    }

    // Export command — native .mdl → FBX
    if (command == "export")
    {
        if (arguments.Size() < 3)
            ErrorExit("Usage: AssetTool export <input.mdl> <output.fbx> [options]");

        String inFile = arguments[1];
        String outFile = arguments[2];
        inFile.Replace("\r\n", ""); inFile.Replace("\r", ""); inFile.Replace("\n", "");
        outFile.Replace("\r\n", ""); outFile.Replace("\r", ""); outFile.Replace("\n", "");

        ExportMdlToFBX(inFile, outFile);
        return;
    }

    // Rescale native .ani — scale position keyframes without FBX round-trip
    if (command == "rescale")
    {
        if (arguments.Size() < 4)
            ErrorExit("Usage: AssetTool rescale <input.ani> <output.ani> -scale <factor>\n"
                       "Scales position keyframes in a native .ani file.");

        String inFile = arguments[1];
        String outFile = arguments[2];
        float scale = 1.0f;

        for (unsigned i = 3; i < arguments.Size(); ++i)
        {
            String arg = arguments[i].Substring(1).ToLower();
            String val = (i + 1 < arguments.Size()) ? arguments[i + 1] : String::EMPTY;
            if (arg == "scale" && !val.Empty())
            {
                scale = ToFloat(val);
                ++i;
            }
        }

        bool stripRootScale = false;
        for (unsigned i = 3; i < arguments.Size(); ++i)
        {
            String a = arguments[i].ToLower();
            if (a == "-strip-root-scale")
                stripRootScale = true;
        }

        if (scale == 1.0f && !stripRootScale)
            ErrorExit("No -scale specified or scale is 1.0 and no -strip-root-scale — nothing to do");

        SharedPtr<File> file(new File(context_, inFile));
        if (!file->IsOpen())
            ErrorExit("Could not open " + inFile);

        SharedPtr<Animation> anim(new Animation(context_));
        if (!anim->BeginLoad(*file))
            ErrorExit("Failed to load animation " + inFile);

        // Strip root bone scale tracks if requested
        if (stripRootScale)
        {
            for (const auto& trackEntry : anim->GetTracks())
            {
                AnimationTrack* track = const_cast<AnimationTrack*>(&trackEntry.second_);
                if (!!(track->channelMask_ & AnimationChannels::Scale))
                {
                    // Only strip single-key scale tracks (identity poses, not real animation)
                    bool allIdentity = true;
                    for (unsigned k = 0; k < track->keyFrames_.Size(); ++k)
                    {
                        Vector3 s = track->keyFrames_[k].scale_;
                        if ((s - Vector3::ONE).Length() > 0.01f)
                            allIdentity = false;
                    }
                    if (allIdentity || track->keyFrames_.Size() <= 1)
                    {
                        track->channelMask_ &= ~AnimationChannels::Scale;
                        for (unsigned k = 0; k < track->keyFrames_.Size(); ++k)
                            track->keyFrames_[k].scale_ = Vector3::ONE;
                        PrintLine("Stripped scale from track: " + trackEntry.first_.ToString());
                    }
                }
            }
        }

        // Scale position keyframes
        unsigned totalScaled = 0;
        for (const auto& trackEntry : anim->GetTracks())
        {
            AnimationTrack* track = const_cast<AnimationTrack*>(&trackEntry.second_);
            if (!!(track->channelMask_ & AnimationChannels::Position))
            {
                for (unsigned k = 0; k < track->keyFrames_.Size(); ++k)
                {
                    track->keyFrames_[k].position_ *= scale;
                    ++totalScaled;
                }
            }
        }

        File outFileStream(context_);
        if (!outFileStream.Open(outFile, FILE_WRITE))
            ErrorExit("Could not open output file " + outFile);
        anim->Save(outFileStream);

        PrintLine("Rescaled " + String(totalScaled) + " position keyframes by " + String(scale));
        PrintLine("Saved: " + outFile);
        return;
    }

    // Native Urho3D format info — handle .ani and .mdl without Assimp
    if (command == "info")
    {
        String inFile = arguments[1];
        String ext = GetExtension(inFile).ToLower();

        if (ext == ".ani")
        {
            SharedPtr<File> file(new File(context_, inFile));
            if (!file->IsOpen())
                ErrorExit("Could not open " + inFile);

            SharedPtr<Animation> anim(new Animation(context_));
            if (!anim->BeginLoad(*file))
                ErrorExit("Failed to load animation " + inFile);

            PrintLine("");
            PrintLine("Animation Info: " + inFile);
            PrintLine("  Name:     " + anim->GetAnimationName());
            PrintLine("  Length:   " + String(anim->GetLength(), 4) + " s");
            PrintLine("  Tracks:   " + String(anim->GetNumTracks()));

            const Vector<AnimationTriggerPoint>& triggers = anim->GetTriggers();
            if (!triggers.Empty())
                PrintLine("  Triggers: " + String(triggers.Size()));

            PrintLine("");

            const HashMap<StringHash, AnimationTrack>& tracks = anim->GetTracks();
            int totalKeys = 0;
            for (HashMap<StringHash, AnimationTrack>::ConstIterator it = tracks.Begin(); it != tracks.End(); ++it)
            {
                const AnimationTrack& track = it->second_;
                totalKeys += track.GetNumKeyFrames();

                String ch;
                if ((track.channelMask_ & AnimationChannels::Position) != AnimationChannels::None) ch += "P";
                if ((track.channelMask_ & AnimationChannels::Rotation) != AnimationChannels::None) ch += "R";
                if ((track.channelMask_ & AnimationChannels::Scale) != AnimationChannels::None) ch += "S";

                String line = "  " + track.name_ + " [" + ch + "] " + String(track.GetNumKeyFrames()) + " keys";

                if (track.GetNumKeyFrames() > 0)
                {
                    const AnimationKeyFrame& first = track.keyFrames_[0];
                    const AnimationKeyFrame& last = track.keyFrames_[track.keyFrames_.Size() - 1];
                    line += "  t: " + String(first.time_, 3) + " -> " + String(last.time_, 3);

                    if ((track.channelMask_ & AnimationChannels::Position) != AnimationChannels::None)
                    {
                        Vector3 posMin(M_INFINITY, M_INFINITY, M_INFINITY);
                        Vector3 posMax(-M_INFINITY, -M_INFINITY, -M_INFINITY);
                        for (unsigned k = 0; k < track.keyFrames_.Size(); ++k)
                        {
                            const Vector3& p = track.keyFrames_[k].position_;
                            posMin.x_ = Min(posMin.x_, p.x_);
                            posMin.y_ = Min(posMin.y_, p.y_);
                            posMin.z_ = Min(posMin.z_, p.z_);
                            posMax.x_ = Max(posMax.x_, p.x_);
                            posMax.y_ = Max(posMax.y_, p.y_);
                            posMax.z_ = Max(posMax.z_, p.z_);
                        }
                        line += "\n                                    pos: " + posMin.ToString() + " -> " + posMax.ToString();
                    }
                }
                PrintLine(line);
            }

            PrintLine("");
            PrintLine("  Total keyframes: " + String(totalKeys));

            if (!triggers.Empty())
            {
                PrintLine("");
                PrintLine("  Triggers:");
                for (unsigned i = 0; i < triggers.Size(); ++i)
                    PrintLine("    @" + String(triggers[i].time_, 3) + " s: " + triggers[i].data_.ToString());
            }
            PrintLine("");
            return;
        }

        if (ext == ".mdl")
        {
            // Model::BeginLoad creates VertexBuffer/IndexBuffer which need Graphics subsystem.
            // Register a headless Graphics so buffers can be created without a window.
            if (!context_->GetSubsystem<Graphics>())
            {
#ifdef URHO3D_VULKAN
                auto* graphics = new Graphics(context_, GAPI_VULKAN);
#elif defined(URHO3D_D3D11)
                auto* graphics = new Graphics(context_, GAPI_D3D11);
#else
                auto* graphics = new Graphics(context_, GAPI_OPENGL);
#endif
                context_->RegisterSubsystem(graphics);
            }

            SharedPtr<File> file(new File(context_, inFile));
            if (!file->IsOpen())
                ErrorExit("Could not open " + inFile);

            SharedPtr<Model> model(new Model(context_));
            if (!model->BeginLoad(*file))
                ErrorExit("Failed to load model " + inFile);

            BoundingBox bbox = model->GetBoundingBox();
            Vector3 dims = bbox.max_ - bbox.min_;
            float diagonal = dims.Length();
            Skeleton& skel = model->GetSkeleton();

            int totalVerts = 0, totalIndices = 0;
            for (unsigned g = 0; g < model->GetNumGeometries(); ++g)
            {
                Geometry* geom = model->GetGeometry(g, 0);
                if (geom) { totalVerts += geom->GetVertexCount(); totalIndices += geom->GetIndexCount(); }
            }

            PrintLine("");
            PrintLine("Model Info: " + inFile);
            PrintLine("  Geometries:  " + String(model->GetNumGeometries()));
            PrintLine("  Vertices:    " + String(totalVerts));
            PrintLine("  Triangles:   " + String(totalIndices / 3));
            PrintLine("  Bones:       " + String(skel.GetNumBones()));
            PrintLine("  Morphs:      " + String(model->GetNumMorphs()));
            PrintLine("");
            PrintLine("  BBox min:    " + bbox.min_.ToString());
            PrintLine("  BBox max:    " + bbox.max_.ToString());
            PrintLine("  Size:        " + String(dims.x_, 2) + " x " + String(dims.y_, 2) + " x " + String(dims.z_, 2));
            PrintLine("  Diagonal:    " + String(diagonal, 2));

            if (diagonal > 50.0f)
            {
                PrintLine("");
                PrintLine("  WARNING: Model appears oversized (likely centimeter units)");
                PrintLine("    Suggested: -scale " + String(2.0f / diagonal, 6) + "  (normalize to ~2.0 units)");
                PrintLine("    Common:    -scale 0.010000  (cm to meters)");
            }

            for (unsigned g = 0; g < model->GetNumGeometries(); ++g)
            {
                PrintLine("");
                PrintLine("  Geometry " + String(g) + ":");
                int numLods = model->GetNumGeometryLodLevels(g);
                PrintLine("    LOD levels: " + String(numLods));

                for (int l = 0; l < numLods; ++l)
                {
                    Geometry* geom = model->GetGeometry(g, l);
                    if (!geom) continue;
                    PrintLine("    Verts: " + String(geom->GetVertexCount()) +
                        "  Idx: " + String(geom->GetIndexCount()) +
                        "  Tris: " + String(geom->GetIndexCount() / 3));
                }
            }

            if (skel.GetNumBones() > 1)
            {
                PrintLine("");
                PrintLine("  Skeleton (" + String(skel.GetNumBones()) + " bones):");
                const Vector<Bone>& bones = skel.GetBones();
                for (unsigned i = 0; i < bones.Size(); ++i)
                {
                    const Bone& bone = bones[i];
                    int depth = 0;
                    int p = bone.parentIndex_;
                    while (p > 0 && depth < 10) { p = bones[p].parentIndex_; depth++; }

                    String indent;
                    for (int d = 0; d < depth; ++d) indent += "  ";
                    PrintLine("    " + indent + bone.name_);
                }
            }

            PrintLine("");
            return;
        }
    }

    if (command == "model" || command == "scene" || command == "anim" || command == "node" || command == "dump" || command == "info")
    {
        String inFile = arguments[1];
        // Strip stray line endings from input path (can come from shell copy-paste)
        inFile.Replace("\r\n", "");
        inFile.Replace("\r", "");
        inFile.Replace("\n", "");
        String outFile;
        if (arguments.Size() > 2 && arguments[2][0] != '-')
        {
            outFile = GetInternalPath(arguments[2]);
            outFile.Replace("\r\n", "");
            outFile.Replace("\r", "");
            outFile.Replace("\n", "");
        }

        inputName_ = GetFileName(inFile);
        outName_ = outFile;
        outPath_ = GetPath(outFile);

        if (resourcePath_.Empty())
        {
            resourcePath_ = outPath_;
            // If output file is under a Models/ directory, strip back to the resource root.
            // Handles both "Models/Foo.mdl" and "Models/Animals/Foo.mdl" etc.
            if (command == "model")
            {
                i32 modelsPos = resourcePath_.Find("Models/", 0, false);
                if (modelsPos != String::NPOS)
                    resourcePath_ = resourcePath_.Substring(0, modelsPos);
            }
            if (resourcePath_.Empty())
                resourcePath_ = "./";
        }

        resourcePath_ = AddTrailingSlash(resourcePath_);

        if (command != "dump" && command != "info" && outFile.Empty())
            ErrorExit("No output file defined");

        if (verboseLog_)
            Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE, aiDefaultLogStream_STDOUT);

        PrintLine("Reading file " + inFile);

        if (!inFile.EndsWith(".fbx", false))
            suppressFbxPivotNodes_ = false;

        // Suppress $AssimpFbx$ pivot nodes for both model and anim commands.
        // Without this, standalone anim exports get channel names like "Hips_$AssimpFbx$_Rotation"
        // which don't match the model skeleton's clean bone names at runtime.
        if (suppressFbxPivotNodes_ && (command == "model" || command == "anim"))
        {
            PrintLine("Suppressing $fbx nodes");
            aiPropertyStore *aiprops = aiCreatePropertyStore();
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, 1);       //default = true;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, 0);             //default = false;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_READ_MATERIALS, 1);                 //default = true;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_READ_CAMERAS, 1);                   //default = true;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_READ_LIGHTS, 1);                    //default = true;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, 1);                //default = true;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_STRICT_MODE, 0);                    //default = false;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, 0);                //**false, default = true;
            aiSetImportPropertyInteger(aiprops, AI_CONFIG_IMPORT_FBX_OPTIMIZE_EMPTY_ANIMATION_CURVES, 1);//default = true;

            scene_ = aiImportFileExWithProperties(GetNativePath(inFile).CString(), flags, nullptr, aiprops);

            // prevent processing animation suppression, both cannot work simultaneously
            suppressFbxPivotNodes_ = false;
        }
        else
            scene_ = aiImportFile(GetNativePath(inFile).CString(), flags);

        if (!scene_)
            ErrorExit("Could not open or parse input file " + inFile + ": " + String(aiGetErrorString()));

        if (verboseLog_)
            Assimp::DefaultLogger::kill();

        rootNode_ = scene_->mRootNode;
        if (!rootNodeName.Empty())
        {
            rootNode_ = GetNode(rootNodeName, rootNode_, false);
            if (!rootNode_)
                ErrorExit("Could not find scene node " + rootNodeName);
        }

        if (command == "dump")
        {
            DumpNodes(rootNode_, 0);
            return;
        }

        if (command == "info")
        {
            // Collect mesh info and compute bounding box in import units
            unsigned totalMeshes = scene_->mNumMeshes;
            unsigned totalVertices = 0;
            unsigned totalBones = 0;
            BoundingBox box;

            // Track unique bone names across all meshes
            HashSet<String> boneNames;

            for (unsigned i = 0; i < totalMeshes; ++i)
            {
                aiMesh* mesh = scene_->mMeshes[i];
                totalVertices += mesh->mNumVertices;

                for (unsigned j = 0; j < mesh->mNumBones; ++j)
                    boneNames.Insert(FromAIString(mesh->mBones[j]->mName));

                for (unsigned v = 0; v < mesh->mNumVertices; ++v)
                {
                    Vector3 vertex(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
                    box.Merge(vertex);
                }
            }
            totalBones = boneNames.Size();

            Vector3 size = box.Size();
            float maxDim = Max(Max(size.x_, size.y_), size.z_);

            char buf[256];

            PrintLine("");
            PrintLine("Model Info:");
            PrintLine("  Meshes:      " + String(totalMeshes) + " (" + String(totalVertices) + " total vertices)");
            PrintLine("  Bones:       " + String(totalBones));
            PrintLine("  Animations:  " + String(scene_->mNumAnimations));
            PrintLine("  Materials:   " + String(scene_->mNumMaterials));
            PrintLine("");
            sprintf(buf, "  Bounding box min: (%.2f, %.2f, %.2f)", box.min_.x_, box.min_.y_, box.min_.z_);
            PrintLine(buf);
            sprintf(buf, "  Bounding box max: (%.2f, %.2f, %.2f)", box.max_.x_, box.max_.y_, box.max_.z_);
            PrintLine(buf);
            sprintf(buf, "  Size:        %.2f x %.2f x %.2f (import units)", size.x_, size.y_, size.z_);
            PrintLine(buf);
            PrintLine("");

            if (maxDim > 0.0f)
            {
                PrintLine("  Suggested -scale values:");
                sprintf(buf, "    -scale %.6f  (normalize to ~1.0 unit)", 1.0f / maxDim);
                PrintLine(buf);
                sprintf(buf, "    -scale %.6f  (normalize to ~2.0 units, human height)", 2.0f / maxDim);
                PrintLine(buf);
                if (maxDim > 10.0f)
                    PrintLine("    -scale 0.010000  (centimeters to meters, common for Blender/Mixamo)");
            }

            // Animation details
            if (scene_->mNumAnimations > 0)
            {
                PrintLine("");
                PrintLine("  Animations:");
                for (unsigned a = 0; a < scene_->mNumAnimations; ++a)
                {
                    aiAnimation* anim = scene_->mAnimations[a];
                    String name = FromAIString(anim->mName);
                    if (name.Empty()) name = "(unnamed)";
                    double duration = anim->mDuration;
                    double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 24.0;
                    double seconds = duration / tps;
                    sprintf(buf, "    [%u] %-30s  %.2fs  %u channels  %.0f tps",
                        a, name.CString(), (float)seconds, anim->mNumChannels, tps);
                    PrintLine(buf);
                }
            }

            // Material details
            if (scene_->mNumMaterials > 0)
            {
                PrintLine("");
                PrintLine("  Materials:");
                for (unsigned m = 0; m < scene_->mNumMaterials; ++m)
                {
                    aiMaterial* mat = scene_->mMaterials[m];
                    aiString matName;
                    mat->Get(AI_MATKEY_NAME, matName);
                    String name = FromAIString(matName);
                    if (name.Empty()) name = "(unnamed)";

                    // Count textures across common types
                    unsigned texCount = 0;
                    String texTypes;
                    const aiTextureType types[] = {
                        aiTextureType_DIFFUSE, aiTextureType_SPECULAR, aiTextureType_NORMALS,
                        aiTextureType_EMISSIVE, aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS
                    };
                    const char* typeNames[] = {
                        "diffuse", "specular", "normal", "emissive", "metalness", "roughness"
                    };
                    for (unsigned t = 0; t < 6; ++t)
                    {
                        unsigned count = mat->GetTextureCount(types[t]);
                        if (count > 0)
                        {
                            texCount += count;
                            if (!texTypes.Empty()) texTypes += ", ";
                            texTypes += typeNames[t];
                            aiString texPath;
                            mat->GetTexture(types[t], 0, &texPath);
                            texTypes += "=" + String(FromAIString(texPath));
                        }
                    }
                    sprintf(buf, "    [%u] %-25s  %u textures", m, name.CString(), texCount);
                    PrintLine(buf);
                    if (!texTypes.Empty())
                        PrintLine("         " + texTypes);
                }
            }

            // Embedded textures
            if (scene_->mNumTextures > 0)
            {
                PrintLine("");
                PrintLine("  Embedded textures: " + String(scene_->mNumTextures));
                for (unsigned t = 0; t < scene_->mNumTextures; ++t)
                {
                    aiTexture* tex = scene_->mTextures[t];
                    String hint = tex->achFormatHint;
                    if (tex->mHeight == 0)
                        sprintf(buf, "    [%u] compressed (%s) %u bytes", t, hint.CString(), tex->mWidth);
                    else
                        sprintf(buf, "    [%u] raw %ux%u (%s)", t, tex->mWidth, tex->mHeight, hint.CString());
                    PrintLine(buf);
                }
            }

            // Per-mesh details
            PrintLine("");
            PrintLine("  Meshes:");
            for (unsigned i = 0; i < totalMeshes; ++i)
            {
                aiMesh* mesh = scene_->mMeshes[i];
                String meshName = FromAIString(mesh->mName);
                if (meshName.Empty()) meshName = "(unnamed)";
                sprintf(buf, "    [%u] %-25s  %u verts  %u faces  %u bones  mat:%u",
                    i, meshName.CString(), mesh->mNumVertices, mesh->mNumFaces,
                    mesh->mNumBones, mesh->mMaterialIndex);
                PrintLine(buf);
            }

            PrintLine("");
            return;
        }

        // Auto-normalize: compute scale from bounding box to reach target height.
        // Must apply the mesh baking transform (node hierarchy) to vertices, matching
        // what WriteVertex does — otherwise the BBox is in a different coordinate space.
        if (normalizeTarget_ > 0.0f && importScale_ == 1.0f)
        {
            BoundingBox box;
            for (unsigned i = 0; i < scene_->mNumMeshes; ++i)
            {
                aiMesh* mesh = scene_->mMeshes[i];
                aiNode* meshNode = nullptr;
                for (unsigned n = 0; n < scene_->mRootNode->mNumChildren && !meshNode; ++n)
                {
                    std::function<aiNode*(aiNode*)> findMeshNode = [&](aiNode* node) -> aiNode* {
                        for (unsigned m = 0; m < node->mNumMeshes; ++m)
                            if (node->mMeshes[m] == i)
                                return node;
                        for (unsigned c = 0; c < node->mNumChildren; ++c)
                        {
                            aiNode* found = findMeshNode(node->mChildren[c]);
                            if (found) return found;
                        }
                        return nullptr;
                    };
                    meshNode = findMeshNode(scene_->mRootNode->mChildren[n]);
                }
                if (!meshNode) meshNode = scene_->mRootNode;

                aiMatrix4x4 bakingTransform = GetMeshBakingTransform(meshNode, scene_->mRootNode);
                Vector3 bakePos;
                Quaternion bakeRot;
                Vector3 bakeScale;
                GetPosRotScale(bakingTransform, bakePos, bakeRot, bakeScale);
                Matrix3x4 vertexTransform(bakePos, bakeRot, bakeScale);

                for (unsigned v = 0; v < mesh->mNumVertices; ++v)
                {
                    Vector3 vertex = vertexTransform * ToVector3(mesh->mVertices[v]);
                    box.Merge(vertex);
                }
            }
            Vector3 size = box.Size();
            float refDim;
            String axisName;
            if (normalizeAxis_ == 0)      { refDim = size.x_; axisName = "X"; }
            else if (normalizeAxis_ == 1) { refDim = size.y_; axisName = "Y"; }
            else if (normalizeAxis_ == 2) { refDim = size.z_; axisName = "Z"; }
            else
            {
                // Auto-detect: if model has a skeleton (hip+neck), use Y (height) instead of largest.
                // Both bipeds and quadrupeds have meaningful height on Y after import.
                // Search the scene tree directly — model collection hasn't happened yet.
                bool hasSpine = false;
                if (scene_ && scene_->mRootNode)
                {
                    // FindBoneByName is defined later in this file — use a local lambda for the search
                    std::function<aiNode*(aiNode*, const char*)> findBone = [&](aiNode* node, const char* name) -> aiNode* {
                        if (!node) return nullptr;
                        String nodeName(node->mName.C_Str());
                        String baseName = nodeName;
                        if (baseName.StartsWith("mixamorig:"))
                            baseName = baseName.Substring(10);
                        unsigned fbxPos = baseName.Find("_$AssimpFbx$");
                        if (fbxPos != String::NPOS)
                            baseName = baseName.Substring(0, fbxPos);
                        if (baseName.Compare(name, false) == 0)
                            return node;
                        for (unsigned i = 0; i < node->mNumChildren; ++i)
                        {
                            aiNode* found = findBone(node->mChildren[i], name);
                            if (found) return found;
                        }
                        return nullptr;
                    };
                    aiNode* root = scene_->mRootNode;
                    // Biped hip names
                    aiNode* hip = findBone(root, "Hips");
                    if (!hip) hip = findBone(root, "Hip");
                    if (!hip) hip = findBone(root, "Pelvis");
                    if (!hip) hip = findBone(root, "Bip01");
                    // Quadruped hip names (Quaternius/Ultimate Animals packs)
                    if (!hip) hip = findBone(root, "Body");
                    if (!hip) hip = findBone(root, "Back");
                    // Neck — bare "Neck" or numbered "Neck1"
                    aiNode* neck = findBone(root, "Neck");
                    if (!neck) neck = findBone(root, "Neck1");
                    if (!neck) neck = findBone(root, "Head");
                    if (hip && neck)
                        hasSpine = true;
                }
                if (hasSpine)
                {
                    refDim = size.y_;
                    axisName = "Y (auto-detected skeleton)";
                }
                else
                {
                    refDim = Max(Max(size.x_, size.y_), size.z_);
                    axisName = "largest";
                }
            }

            if (refDim > 0.0f)
            {
                importScale_ = normalizeTarget_ / refDim;
                PrintLine("Auto-normalize (" + axisName + "): " + String((double)refDim, 3) +
                    " -> " + String((double)normalizeTarget_, 3) +
                    " (scale " + String((double)importScale_, 6) +
                    ", bbox " + String((double)size.x_, 1) + "x" + String((double)size.y_, 1) + "x" + String((double)size.z_, 1) + ")");
            }
        }
        else if (normalizeTarget_ > 0.0f && importScale_ != 1.0f)
        {
            ErrorExit("Cannot use both -scale and -normalize");
        }

        if (command == "model")
            ExportModel(outFile, scene_->mFlags & AI_SCENE_FLAGS_INCOMPLETE);

        if (command == "anim")
        {
            noMaterials_ = true;
            ExportAnimation(outFile, scene_->mFlags & AI_SCENE_FLAGS_INCOMPLETE);
        }
        if (command == "scene" || command == "node")
        {
            bool asPrefab = command == "node";
            // Saving as prefab requires the hierarchy, especially the root node
            if (asPrefab)
                noHierarchy_ = false;
            ExportScene(outFile, asPrefab);
        }

        if (!noMaterials_)
        {
            // Collect texture names from materials, copy textures to destination,
            // THEN write material XMLs with verification. This order ensures
            // materials never reference textures that failed to copy.
            HashSet<String> usedTextures;
            CollectMaterialTextures(usedTextures);
            if (!noTextures_)
                CopyTextures(usedTextures, GetPath(inFile));
            ExportMaterials(usedTextures);
        }
    }
    else if (command == "lod")
    {
        Vector<float> lodDistances;
        Vector<String> modelNames;
        String outFile;

        unsigned numLodArguments = 0;
        for (unsigned i = 1; i < arguments.Size(); ++i)
        {
            if (arguments[i][0] == '-')
                break;
            ++numLodArguments;
        }
        if (numLodArguments < 4)
            ErrorExit("Must define at least 2 LOD levels");
        if (!(numLodArguments & 1u))
            ErrorExit("No output file defined");

        for (unsigned i = 1; i < numLodArguments + 1; ++i)
        {
            if (i == numLodArguments)
                outFile = GetInternalPath(arguments[i]);
            else
            {
                if (i & 1u)
                    lodDistances.Push(Max(ToFloat(arguments[i]), 0.0f));
                else
                    modelNames.Push(GetInternalPath(arguments[i]));
            }
        }

        if (lodDistances[0] != 0.0f)
        {
            PrintLine("Warning: first LOD distance forced to 0");
            lodDistances[0] = 0.0f;
        }

        CombineLods(lodDistances, modelNames, outFile);
    }
    else
        ErrorExit("Unrecognized command " + command);
}

void DumpNodes(aiNode* rootNode, unsigned level)
{
    if (!rootNode)
        return;

    String indent(' ', level * 2);
    Vector3 pos, scale;
    Quaternion rot;
    aiMatrix4x4 transform = GetDerivedTransform(rootNode, rootNode_);
    GetPosRotScale(transform, pos, rot, scale);

    PrintLine(indent + "Node " + FromAIString(rootNode->mName) + " pos " + String(pos));

    if (rootNode->mNumMeshes == 1)
        PrintLine(indent + "  " + String(rootNode->mNumMeshes) + " geometry");
    if (rootNode->mNumMeshes > 1)
        PrintLine(indent + "  " + String(rootNode->mNumMeshes) + " geometries");

    for (unsigned i = 0; i < rootNode->mNumChildren; ++i)
        DumpNodes(rootNode->mChildren[i], level + 1);
}

void ExportModel(const String& outName, bool animationOnly)
{
    if (outName.Empty())
        ErrorExit("No output file defined");

    OutModel model;
    model.rootNode_ = rootNode_;
    model.outName_ = outName;

    CollectMeshes(model, model.rootNode_);
    CollectBones(model, animationOnly);
    DetectBoneScale(model);
    BakeSkeletonScale(model);
    AutoFaceZPlus(model);
    BuildBoneCollisionInfo(model);
    BuildAndSaveModel(model);
    if (!noAnimations_)
    {
        CollectAnimations(&model);
        BuildAndSaveAnimations(&model);

        // Save scene-global animations
        CollectAnimations();
        BuildAndSaveAnimations();
    }
}

void ExportAnimation(const String& outName, bool animationOnly)
{
    if (outName.Empty())
        ErrorExit("No output file defined");

    OutModel model;
    model.rootNode_ = rootNode_;
    model.outName_ = outName;

    CollectMeshes(model, model.rootNode_);
    CollectBones(model, animationOnly);
    DetectBoneScale(model);
    BakeSkeletonScale(model);
    AutoFaceZPlus(model);
    BuildBoneCollisionInfo(model);
    //    BuildAndSaveModel(model);
    if (!noAnimations_)
    {
        // Most fbx animation files contain only a skeleton and no skinned mesh.
        // Assume the scene node contains the model's bone definition and,
        // transfer the info to the model.
        if (suppressFbxPivotNodes_ && model.bones_.Size() == 0)
            CollectSceneNodesAsBones(model, rootNode_);

        CollectAnimations(&model);
        BuildAndSaveAnimations(&model);

        // Save scene-global animations
        CollectAnimations();
        BuildAndSaveAnimations();
    }
}

void CollectMeshes(OutModel& model, aiNode* node)
{
    for (unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene_->mMeshes[node->mMeshes[i]];
        for (unsigned j = 0; j < model.meshes_.Size(); ++j)
        {
            if (mesh == model.meshes_[j])
            {
                PrintLine("Warning: same mesh found multiple times");
                break;
            }
        }

        model.meshIndices_.Insert(node->mMeshes[i]);
        model.meshes_.Push(mesh);
        model.meshNodes_.Push(node);
        model.totalVertices_ += mesh->mNumVertices;
        model.totalIndices_ += GetNumValidFaces(mesh) * 3;
    }

    for (unsigned i = 0; i < node->mNumChildren; ++i)
        CollectMeshes(model, node->mChildren[i]);
}

void CollectBones(OutModel& model, bool animationOnly)
{
    HashSet<aiNode*> necessary;
    HashSet<aiNode*> rootNodes;

    bool haveSkinnedMeshes = false;
    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        if (model.meshes_[i]->HasBones())
        {
            haveSkinnedMeshes = true;
            break;
        }
    }

    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        aiMesh* mesh = model.meshes_[i];
        aiNode* meshNode = model.meshNodes_[i];
        aiNode* meshParentNode = meshNode->mParent;
        aiNode* rootNode = nullptr;

        for (unsigned j = 0; j < mesh->mNumBones; ++j)
        {
            aiBone* bone = mesh->mBones[j];
            String boneName(FromAIString(bone->mName));
            aiNode* boneNode = GetNode(boneName, scene_->mRootNode, true);
            if (!boneNode)
                ErrorExit("Could not find scene node for bone " + boneName);
            necessary.Insert(boneNode);
            rootNode = boneNode;

            for (;;)
            {
                boneNode = boneNode->mParent;
                if (!boneNode || ((boneNode == meshNode || boneNode == meshParentNode) && !animationOnly))
                    break;
                rootNode = boneNode;
                necessary.Insert(boneNode);
            }

            if (rootNodes.Find(rootNode) == rootNodes.End())
                rootNodes.Insert(rootNode);
        }

        // When model is partially skinned, include the attachment nodes of the rigid meshes in the skeleton
        if (haveSkinnedMeshes && !mesh->mNumBones)
        {
            aiNode* boneNode = meshNode;
            necessary.Insert(boneNode);
            rootNode = boneNode;

            for (;;)
            {
                boneNode = boneNode->mParent;
                if (!boneNode || ((boneNode == meshNode || boneNode == meshParentNode) && !animationOnly))
                    break;
                rootNode = boneNode;
                necessary.Insert(boneNode);
            }

            if (rootNodes.Find(rootNode) == rootNodes.End())
                rootNodes.Insert(rootNode);
        }
    }


    // If we find multiple root nodes, try to remedy by going back in the parent chain and finding a common parent
    if (rootNodes.Size() > 1)
    {
        for (HashSet<aiNode*>::Iterator i = rootNodes.Begin(); i != rootNodes.End(); ++i)
        {
            aiNode* commonParent = (*i);

            while (commonParent)
            {
                unsigned found = 0;
                for (HashSet<aiNode*>::Iterator j = rootNodes.Begin(); j != rootNodes.End(); ++j)
                {
                    if (i == j)
                        continue;
                    aiNode* parent = *j;
                    while (parent)
                    {
                        if (parent == commonParent)
                        {
                            ++found;
                            break;
                        }
                        parent = parent->mParent;
                    }
                }

                if (found >= rootNodes.Size() - 1)
                {
                    PrintLine("Multiple roots initially found, using new root node " + FromAIString(commonParent->mName));
                    rootNodes.Clear();
                    rootNodes.Insert(commonParent);
                    necessary.Insert(commonParent);
                    break;
                }

                commonParent = commonParent->mParent;
            }

            if (rootNodes.Size() == 1)
                break; // Succeeded
        }
        if (rootNodes.Size() > 1)
            ErrorExit("Skeleton with multiple root nodes found, not supported");
    }

    if (rootNodes.Empty())
        return;

    model.rootBone_ = *rootNodes.Begin();

    // Move the model to bind pose now if requested
    if (moveToBindPose_)
    {
        PrintLine("Moving bones to bind pose");
        MoveToBindPose(model, model.rootBone_);
    }

    CollectBonesFinal(model.bones_, necessary, model.rootBone_);
    // Initialize the bone collision info
    model.boneRadii_.Resize(model.bones_.Size());
    model.boneHitboxes_.Resize(model.bones_.Size());
    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        model.boneRadii_[i] = 0.0f;
        model.boneHitboxes_[i] = BoundingBox(0.0f, 0.0f);
    }
}

void MoveToBindPose(OutModel& model, aiNode* current)
{
    String nodeName(FromAIString(current->mName));
    Matrix3x4 bindWorldTransform = GetOffsetMatrix(model, nodeName).Inverse();
    // Skip if we get an identity offset matrix (bone lookup failed)
    if (!bindWorldTransform.Equals(Matrix3x4::IDENTITY))
    {
        if (current->mParent && current != model.rootNode_)
        {
            aiMatrix4x4 parentWorldTransform = GetDerivedTransform(current->mParent, model.rootNode_, true);
            Matrix3x4 parentInverse = ToMatrix3x4(parentWorldTransform).Inverse();
            current->mTransformation = ToAIMatrix4x4(parentInverse * bindWorldTransform);
        }
        else
            current->mTransformation = ToAIMatrix4x4(bindWorldTransform);
    }

    for (unsigned i = 0; i < current->mNumChildren; ++i)
        MoveToBindPose(model, current->mChildren[i]);
}

void CollectBonesFinal(Vector<aiNode*>& dest, const HashSet<aiNode*>& necessary, aiNode* node)
{
    bool includeBone = necessary.Find(node) != necessary.End();
    String boneName = FromAIString(node->mName);

    // Check include/exclude filters for non-skinned bones
    if (!includeBone && includeNonSkinningBones_)
    {
        // If no includes specified, include by default but check for excludes
        if (nonSkinningBoneIncludes_.Empty())
            includeBone = true;

        // Check against includes/excludes
        for (unsigned i = 0; i < nonSkinningBoneIncludes_.Size(); ++i)
        {
            if (boneName.Contains(nonSkinningBoneIncludes_[i], false))
            {
                includeBone = true;
                break;
            }
        }
        for (unsigned i = 0; i < nonSkinningBoneExcludes_.Size(); ++i)
        {
            if (boneName.Contains(nonSkinningBoneExcludes_[i], false))
            {
                includeBone = false;
                break;
            }
        }

        if (includeBone)
            PrintLine("Including non-skinning bone " + boneName);
    }

    if (includeBone)
        dest.Push(node);

    for (unsigned i = 0; i < node->mNumChildren; ++i)
        CollectBonesFinal(dest, necessary, node->mChildren[i]);
}

void DetectBoneScale(OutModel& model)
{
    bool hasNonIdentityScale = false;

    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        aiNode* bone = model.bones_[i];
        String boneName = FromAIString(bone->mName);

        aiVector3D aiScale, aiPos;
        aiQuaternion aiRot;
        bone->mTransformation.Decompose(aiScale, aiRot, aiPos);

        float sx = aiScale.x, sy = aiScale.y, sz = aiScale.z;
        bool nonIdentity = fabsf(sx - 1.0f) > 0.001f || fabsf(sy - 1.0f) > 0.001f || fabsf(sz - 1.0f) > 0.001f;

        if (nonIdentity)
        {
            // Check for non-uniform scale — reject outright
            if (fabsf(sx - sy) > 0.001f || fabsf(sx - sz) > 0.001f)
                ErrorExit("Bone \"" + boneName + "\" has non-uniform scale (" +
                    String(sx) + ", " + String(sy) + ", " + String(sz) +
                    "). Cannot auto-fix — artist must apply scale in Blender.");

            PrintLine("Warning: bone \"" + boneName + "\" has uniform scale " + String(sx));
            hasNonIdentityScale = true;
        }
    }

    if (hasNonIdentityScale && !bakeScale_)
        PrintLine("Hint: use -bake-scale to fix bone scale at import time");
}

void BakeSkeletonScale(OutModel& model)
{
    if (!bakeScale_ || model.bones_.Empty())
        return;

    // Phase 1: Record world-space bone positions with original (bad) scale,
    // and record each bone's original local uniform scale factor
    HashMap<String, Vector3> worldPositions;
    originalBoneScales_.Clear();
    originalBoneTransforms_.Clear();
    rootBoneScale_ = 1.0f;

    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        aiNode* bone = model.bones_[i];
        String boneName = FromAIString(bone->mName);

        // Decompose local transform to get this bone's scale
        aiVector3D aiScale, aiPos;
        aiQuaternion aiRot;
        bone->mTransformation.Decompose(aiScale, aiRot, aiPos);
        float localScale = aiScale.x; // uniform — already validated by DetectBoneScale

        bool nonIdentity = fabsf(localScale - 1.0f) > 0.001f;
        if (nonIdentity)
            originalBoneScales_[boneName] = localScale;

        // Save original transform before bake modifies it (needed for animation fallback)
        originalBoneTransforms_[boneName] = bone->mTransformation;

        if (i == 0)
            rootBoneScale_ = localScale;

        // Compute world position including all ancestor transforms
        aiMatrix4x4 worldTransform = GetDerivedTransform(bone, model.rootNode_, false);
        aiVector3D worldPos, worldScale;
        aiQuaternion worldRot;
        worldTransform.Decompose(worldScale, worldRot, worldPos);
        worldPositions[boneName] = ToVector3(worldPos);
    }

    bool anyBaked = false;

    // Phase 2: Remove scale from bones, fix local positions to preserve world positions
    // Only process bones that have non-identity scale OR are descendants of such bones.
    // Descendants need adjustment because their positions are in the (formerly scaled) parent space.
    // Bones with identity scale and no scaled ancestor are left untouched.
    HashSet<aiNode*> modifiedBones;

    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        aiNode* bone = model.bones_[i];
        String boneName = FromAIString(bone->mName);

        aiVector3D aiScale, aiPos;
        aiQuaternion aiRot;
        bone->mTransformation.Decompose(aiScale, aiRot, aiPos);

        bool nonIdentity = fabsf(aiScale.x - 1.0f) > 0.001f;

        // Check if any ancestor bone was modified (had scale removed)
        bool ancestorModified = false;
        aiNode* p = bone->mParent;
        while (p)
        {
            if (modifiedBones.Contains(p)) { ancestorModified = true; break; }
            p = p->mParent;
        }

        // Skip bones that don't need adjustment
        if (!nonIdentity && !ancestorModified)
            continue;

        if (nonIdentity)
            anyBaked = true;
        modifiedBones.Insert(bone);

        Vector3 targetWorldPos = worldPositions[boneName];

        if (i == 0)
        {
            // Root bone: local position = world position (no parent)
            aiPos = aiVector3D(targetWorldPos.x_, targetWorldPos.y_, targetWorldPos.z_);
        }
        else
        {
            // Non-root: compute new local position from parent's corrected world transform
            // Parent's world transform has already been corrected (processed earlier in depth-first order)
            aiMatrix4x4 parentWorld = GetDerivedTransform(bone->mParent, model.rootNode_, false);
            aiMatrix4x4 parentWorldInverse = parentWorld;
            parentWorldInverse.Inverse();

            // Transform target world position into parent's corrected local space
            aiVector3D targetWorld(targetWorldPos.x_, targetWorldPos.y_, targetWorldPos.z_);
            aiVector3D localPos = parentWorldInverse * targetWorld;
            aiPos = localPos;
        }

        // Set identity scale, keep rotation, set corrected position
        aiScale = aiVector3D(1.0f, 1.0f, 1.0f);

        // Rebuild the transformation matrix: T * R * S (with S = identity)
        aiMatrix4x4 transMat, rotMat, scaleMat;
        aiMatrix4x4::Translation(aiPos, transMat);
        rotMat = aiMatrix4x4(aiRot.GetMatrix());
        aiMatrix4x4::Scaling(aiScale, scaleMat);
        bone->mTransformation = transMat * rotMat * scaleMat;

        if (nonIdentity)
            PrintLine("Baked scale out of bone \"" + boneName + "\"");
        else
            PrintLine("Adjusted position for bone \"" + boneName + "\" (ancestor had scale)");
    }

    if (anyBaked)
        PrintLine("Skeleton scale bake complete — all bones now at identity scale");
}

// Find a bone node by name (case-insensitive), searching scene tree recursively.
// Matches "Name" or "mixamorig:Name" or "Name_$AssimpFbx$_*" variants.
aiNode* FindBoneByName(aiNode* root, const String& boneName)
{
    if (!root) return nullptr;
    String nodeName = FromAIString(root->mName);
    // Exact match, or with mixamorig: prefix, or the base name before $_AssimpFbx$
    String baseName = nodeName;
    if (baseName.StartsWith("mixamorig:"))
        baseName = baseName.Substring(10);
    unsigned fbxPos = baseName.Find("_$AssimpFbx$");
    if (fbxPos != String::NPOS)
        baseName = baseName.Substring(0, fbxPos);

    if (baseName.Compare(boneName, false) == 0)
        return root;

    for (unsigned i = 0; i < root->mNumChildren; ++i)
    {
        aiNode* found = FindBoneByName(root->mChildren[i], boneName);
        if (found) return found;
    }
    return nullptr;
}

// Get world-space position of a node relative to rootNode.
aiVector3D GetBoneWorldPos(aiNode* bone, aiNode* rootNode)
{
    aiMatrix4x4 world = GetDerivedTransform(bone, rootNode, true);
    aiVector3D pos, dummy;
    aiQuaternion dummyQ;
    world.Decompose(dummy, dummyQ, pos);
    return pos;
}

// ─── Structural Bone Identification ──────────────────────────────────────────
// Identifies skeleton landmarks from geometry alone: bind-pose positions,
// parent-child relationships, chain lengths, branch directions.
// No bone name lookups. Works with any exporter, any naming convention.

/// Check if an aiNode is a real bone in this model (not a mesh or dummy node).
static bool IsBone(aiNode* node, const OutModel& model)
{
    for (unsigned i = 0; i < model.bones_.Size(); ++i)
        if (model.bones_[i] == node) return true;
    return false;
}

/// Count the length of the longest ascending-Y chain from a starting node.
static int ChainLengthAscendingY(aiNode* start, aiNode* rootNode, const OutModel& model)
{
    if (!start) return 0;
    aiVector3D pos = GetBoneWorldPos(start, rootNode);
    int best = 0;
    for (unsigned i = 0; i < start->mNumChildren; ++i)
    {
        aiNode* child = start->mChildren[i];
        if (!IsBone(child, model)) continue;
        aiVector3D childPos = GetBoneWorldPos(child, rootNode);
        if (childPos.y > pos.y)
        {
            int sub = 1 + ChainLengthAscendingY(child, rootNode, model);
            if (sub > best) best = sub;
        }
    }
    return best;
}

/// Count leaf bones near ground level. These are limb endpoints (feet/paws/hooves).
/// A leaf bone is a bone with no bone children. Ground level = within groundThreshold
/// of the lowest bone Y in the skeleton.
static int CountGroundContacts(aiNode* rootBone, aiNode* rootNode, const OutModel& model)
{
    // First pass: find lowest and highest bone Y positions
    float lowestY = 1e9f;
    float highestY = -1e9f;
    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        aiVector3D pos = GetBoneWorldPos(model.bones_[i], rootNode);
        if (pos.y < lowestY) lowestY = pos.y;
        if (pos.y > highestY) highestY = pos.y;
    }

    // Ground threshold: bottom 15% of skeleton height
    float skeletonHeight = highestY - lowestY;
    if (skeletonHeight < 0.001f) return 0;
    float groundThreshold = lowestY + skeletonHeight * 0.15f;

    // Second pass: count leaf bones near ground
    int contacts = 0;
    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        aiNode* bone = model.bones_[i];
        // Check if leaf (no bone children)
        bool isLeaf = true;
        for (unsigned c = 0; c < bone->mNumChildren; ++c)
        {
            if (IsBone(bone->mChildren[c], model))
            { isLeaf = false; break; }
        }
        if (!isLeaf) continue;

        aiVector3D pos = GetBoneWorldPos(bone, rootNode);
        if (pos.y < groundThreshold)
            ++contacts;
    }
    return contacts;
}

/// Skeleton classification
enum SkeletonType { SKELETON_UNKNOWN, SKELETON_BIPED, SKELETON_QUADRUPED };

/// Structural skeleton analysis result.
struct SkeletonLandmarks
{
    aiNode* hip{nullptr};       // Root of the skeleton (lowest common ancestor)
    aiNode* spineTop{nullptr};  // Top of the longest ascending chain (neck region)
    aiNode* leftArm{nullptr};   // Lateral branch with negative X relative to spine
    aiNode* rightArm{nullptr};  // Lateral branch with positive X relative to spine
    SkeletonType type{SKELETON_UNKNOWN};
};

/// Walk the skeleton tree from rootBone to identify hip, spine top, and arm branches.
/// Uses only bind-pose world positions and chain topology — zero name lookups.
static SkeletonLandmarks AnalyzeSkeleton(OutModel& model)
{
    SkeletonLandmarks lm;
    if (model.bones_.Empty() || !model.rootBone_)
        return lm;

    // Hip: rootBone or its first bone child
    lm.hip = model.rootBone_;
    if (!IsBone(lm.hip, model))
    {
        for (unsigned i = 0; i < model.rootBone_->mNumChildren; ++i)
        {
            if (IsBone(model.rootBone_->mChildren[i], model))
            {
                lm.hip = model.rootBone_->mChildren[i];
                break;
            }
        }
    }
    if (!lm.hip) return lm;

    // Walk the longest ascending-Y chain from hip to find the spine top.
    // At each node, pick the child whose ascending sub-chain is longest.
    // This follows the spine (longest chain) past arm/leg branches (shorter).
    aiNode* current = lm.hip;
    aiNode* lastBranchPoint = nullptr;
    for (int depth = 0; depth < 50; ++depth)
    {
        aiVector3D pos = GetBoneWorldPos(current, model.rootNode_);

        // Gather bone children that ascend in Y
        struct ChildInfo { aiNode* node; int chainLen; float y; };
        Vector<ChildInfo> ascending;
        for (unsigned i = 0; i < current->mNumChildren; ++i)
        {
            aiNode* child = current->mChildren[i];
            if (!IsBone(child, model)) continue;
            aiVector3D childPos = GetBoneWorldPos(child, model.rootNode_);
            if (childPos.y > pos.y)
            {
                ChildInfo ci;
                ci.node = child;
                ci.chainLen = 1 + ChainLengthAscendingY(child, model.rootNode_, model);
                ci.y = childPos.y;
                ascending.Push(ci);
            }
        }

        if (ascending.Empty())
            break; // current is the top of the ascending chain

        // If there are 2+ ascending branches, this is a branch point (likely shoulders/neck)
        if (ascending.Size() >= 2)
            lastBranchPoint = current;

        // Follow the longest chain (spine)
        int bestIdx = 0;
        for (unsigned i = 1; i < ascending.Size(); ++i)
        {
            if (ascending[i].chainLen > ascending[bestIdx].chainLen)
                bestIdx = (int)i;
        }
        current = ascending[bestIdx].node;
    }
    lm.spineTop = current;

    // Classify skeleton by counting ground contacts (leaf bones near ground level).
    // Quadruped: 4+ ground contacts (front-left, front-right, back-left, back-right feet)
    // Biped: 2 ground contacts (left foot, right foot)
    // Unknown: 0-1 or 3 ground contacts
    int contacts = CountGroundContacts(model.rootBone_, model.rootNode_, model);

    if (contacts >= 4)
        lm.type = SKELETON_QUADRUPED;
    else if (contacts == 2)
        lm.type = SKELETON_BIPED;
    else
        lm.type = SKELETON_UNKNOWN;

    PrintLine("AnalyzeSkeleton: " + String(contacts) + " ground contacts -> " +
        (lm.type == SKELETON_QUADRUPED ? "QUADRUPED" :
         lm.type == SKELETON_BIPED ? "BIPED" : "UNKNOWN"));

    // Find arms: at the branch point (or spine top), the two lateral children
    // that are NOT on the spine. Identify left/right by X position relative to spine.
    if ((lm.type == SKELETON_BIPED) && lastBranchPoint)
    {
        aiVector3D branchPos = GetBoneWorldPos(lastBranchPoint, model.rootNode_);
        float spineX = branchPos.x;

        // The spine continuation is the child we already followed (longest chain).
        // The other ascending children at the branch point are arm candidates.
        // Also check non-ascending children — arms may be at the same Y in some rigs.
        aiNode* bestLeft = nullptr;
        aiNode* bestRight = nullptr;
        float bestLeftDist = 0.0f;
        float bestRightDist = 0.0f;

        for (unsigned i = 0; i < lastBranchPoint->mNumChildren; ++i)
        {
            aiNode* child = lastBranchPoint->mChildren[i];
            if (!IsBone(child, model)) continue;

            aiVector3D childPos = GetBoneWorldPos(child, model.rootNode_);
            float lateralX = childPos.x - spineX;

            // Skip the spine continuation — it's the child closest to directly above
            aiVector3D delta = childPos;
            delta.x -= branchPos.x;
            delta.y -= branchPos.y;
            delta.z -= branchPos.z;
            float horizDist = sqrtf(delta.x * delta.x + delta.z * delta.z);

            // Arms are lateral — significant horizontal displacement from spine
            if (fabsf(lateralX) < 0.01f && horizDist < fabsf(delta.y))
                continue; // Likely spine continuation, skip

            if (lateralX < 0 && fabsf(lateralX) > bestLeftDist)
            {
                bestLeft = child;
                bestLeftDist = fabsf(lateralX);
            }
            else if (lateralX > 0 && fabsf(lateralX) > bestRightDist)
            {
                bestRight = child;
                bestRightDist = fabsf(lateralX);
            }
        }

        lm.leftArm = bestLeft;
        lm.rightArm = bestRight;
    }

    return lm;
}

// ─── End Structural Bone Identification ──────────────────────────────────────

/// Collect a bone and all its descendants into a flat list by walking the tree.
static void CollectArmChain(aiNode* root, const OutModel& model, Vector<aiNode*>& chain)
{
    if (!root) return;
    chain.Push(root);
    for (unsigned i = 0; i < root->mNumChildren; ++i)
    {
        if (IsBone(root->mChildren[i], model))
            CollectArmChain(root->mChildren[i], model, chain);
    }
}

/// Temporarily straighten arm bones to T-pose using structurally identified arm roots.
/// Walks the tree from each arm root, identity-rotates every bone in the chain,
/// computes world positions, restores originals. Zero name lookups.
bool GetSyntheticTPoseArmPositions(OutModel& model, const SkeletonLandmarks& lm,
                                   aiVector3D& outLeftPos, aiVector3D& outRightPos)
{
    if (!lm.leftArm || !lm.rightArm)
        return false;

    // Collect both arm chains
    Vector<aiNode*> allArmBones;
    CollectArmChain(lm.leftArm, model, allArmBones);
    CollectArmChain(lm.rightArm, model, allArmBones);

    if (allArmBones.Empty())
        return false;

    // Save transforms
    Vector<aiMatrix4x4> saved(allArmBones.Size());
    for (unsigned i = 0; i < allArmBones.Size(); ++i)
        saved[i] = allArmBones[i]->mTransformation;

    // Identity-rotate each bone (preserve position and scale)
    for (unsigned i = 0; i < allArmBones.Size(); ++i)
    {
        aiVector3D pos, scale;
        aiQuaternion rot;
        allArmBones[i]->mTransformation.Decompose(scale, rot, pos);
        aiMatrix4x4 scaleMat, transMat;
        aiMatrix4x4::Scaling(scale, scaleMat);
        aiMatrix4x4::Translation(pos, transMat);
        allArmBones[i]->mTransformation = transMat * scaleMat;
    }

    // Compute world positions from the structurally identified arm roots
    outLeftPos = GetBoneWorldPos(lm.leftArm, model.rootNode_);
    outRightPos = GetBoneWorldPos(lm.rightArm, model.rootNode_);

    PrintLine("AutoFace: synthetic T-pose applied to " + String(allArmBones.Size()) +
        " arm bones (structural), L=(" +
        String(outLeftPos.x) + ", " + String(outLeftPos.y) + ", " + String(outLeftPos.z) + ") R=(" +
        String(outRightPos.x) + ", " + String(outRightPos.y) + ", " + String(outRightPos.z) + ")");

    // Restore original transforms
    for (unsigned i = 0; i < allArmBones.Size(); ++i)
        allArmBones[i]->mTransformation = saved[i];

    return true;
}

void AutoFaceZPlus(OutModel& model)
{
    if (!forceZForwards_ || model.bones_.Empty() || !model.rootBone_)
        return;

    // Structural skeleton analysis — identify landmarks from geometry, not names
    SkeletonLandmarks lm = AnalyzeSkeleton(model);

    if (!lm.hip)
    {
        PrintLine("Warning: -forcezforwards: could not identify hip bone structurally, skipping");
        return;
    }

    if (!lm.spineTop)
    {
        PrintLine("Warning: -forcezforwards: could not identify spine top structurally, skipping");
        return;
    }

    aiVector3D hipPos = GetBoneWorldPos(lm.hip, model.rootNode_);
    aiVector3D neckPos = GetBoneWorldPos(lm.spineTop, model.rootNode_);

    float dx = neckPos.x - hipPos.x;
    float dy = neckPos.y - hipPos.y;
    float dz = neckPos.z - hipPos.z;
    float xzMag = sqrtf(dx * dx + dz * dz);
    float absDy = fabsf(dy);

    PrintLine("AutoFace: hip=\"" + FromAIString(lm.hip->mName) + "\" spineTop=\"" +
        FromAIString(lm.spineTop->mName) + "\" delta=(" + String(dx) + ", " + String(dy) + ", " + String(dz) +
        ") xzMag=" + String(xzMag) + " absY=" + String(absDy) +
        (lm.type == SKELETON_BIPED ? " [BIPED]" :
         lm.type == SKELETON_QUADRUPED ? " [QUADRUPED]" : " [UNKNOWN]"));

    // Unknown skeleton type — don't guess, skip
    if (lm.type == SKELETON_UNKNOWN)
    {
        PrintLine("AutoFace: unknown skeleton type — skipping orientation correction");
        return;
    }

    // Biped detected structurally: use cross-product method
    if (lm.type == SKELETON_BIPED)
    {
        PrintLine("AutoFace: biped detected — using cross-product method");

        if (!lm.leftArm || !lm.rightArm)
        {
            PrintLine("Warning: -forcezforwards: biped but could not identify arm branches structurally, skipping");
            return;
        }

        PrintLine("AutoFace: arms found structurally — left=\"" + FromAIString(lm.leftArm->mName) +
            "\" right=\"" + FromAIString(lm.rightArm->mName) + "\"");

        aiVector3D leftPos = GetBoneWorldPos(lm.leftArm, model.rootNode_);
        aiVector3D rightPos = GetBoneWorldPos(lm.rightArm, model.rootNode_);

        // Check if bind pose arms are roughly horizontal (usable T-pose)
        float armDx = rightPos.x - leftPos.x;
        float armDy = rightPos.y - leftPos.y;
        float armDz = rightPos.z - leftPos.z;
        float armXZMag = sqrtf(armDx * armDx + armDz * armDz);

        if (fabsf(armDy) >= armXZMag * 0.5f)
        {
            // Arms are NOT in a usable T-pose — use synthetic T-pose
            PrintLine("AutoFace: arm vector Y=" + String(fabsf(armDy)) + " XZ=" + String(armXZMag) +
                " — bind pose is NOT T-pose, applying synthetic T-pose");
            aiVector3D synthLeft, synthRight;
            if (GetSyntheticTPoseArmPositions(model, lm, synthLeft, synthRight))
            {
                leftPos = synthLeft;
                rightPos = synthRight;
            }
            else
            {
                PrintLine("Warning: -forcezforwards: synthetic T-pose failed, using raw arm positions");
            }
        }
        else
        {
            PrintLine("AutoFace: arm vector Y=" + String(fabsf(armDy)) + " XZ=" + String(armXZMag) +
                " — bind pose is T-pose, using directly");
        }

        // Spine vector (hip → neck)
        float sx = dx, sy = dy, sz = dz;
        // Arm vector (left → right)
        float ax = rightPos.x - leftPos.x;
        float ay = rightPos.y - leftPos.y;
        float az = rightPos.z - leftPos.z;

        PrintLine("AutoFace: spine=(" + String(sx) + ", " + String(sy) + ", " + String(sz) +
            ") arm=(" + String(ax) + ", " + String(ay) + ", " + String(az) + ")");

        // Forward = cross(spine, arm)
        float fx = sy * az - sz * ay;
        float fy = sz * ax - sx * az;
        float fz = sx * ay - sy * ax;

        // Project onto XZ plane
        dx = fx;
        dz = fz;

        PrintLine("AutoFace: cross-product forward=(" + String(fx) + ", " + String(fy) + ", " + String(fz) +
            ") XZ=(" + String(dx) + ", " + String(dz) + ")");
    }

    // Determine major axis on XZ plane
    float absDx = fabsf(dx);
    float absDz = fabsf(dz);

    float correctionAngle = 0.0f;
    String detectedDir;

    if (absDz >= absDx)
    {
        if (dz > 0.0f)
        {
            detectedDir = "Z+";
            correctionAngle = 0.0f;
        }
        else
        {
            detectedDir = "Z-";
            correctionAngle = 180.0f;
        }
    }
    else
    {
        if (dx > 0.0f)
        {
            detectedDir = "X+";
            correctionAngle = -90.0f;
        }
        else
        {
            detectedDir = "X-";
            correctionAngle = 90.0f;
        }
    }

    if (fabsf(correctionAngle) < 0.001f)
    {
        PrintLine("AutoFace: already facing Z+, no correction needed");
        return;
    }

    PrintLine("AutoFace: detected forward " + detectedDir + ", applying " +
        String(correctionAngle) + " deg Y correction");

    // Apply Y rotation to root bone's mTransformation
    float radians = correctionAngle * (float)M_PI / 180.0f;
    aiMatrix4x4 rotMat;
    aiMatrix4x4::RotationY(radians, rotMat);

    model.rootBone_->mTransformation = model.rootBone_->mTransformation * rotMat;
}

void CollectAnimations(OutModel* model)
{
    const aiScene* scene = scene_;
    for (unsigned i = 0; i < scene->mNumAnimations; ++i)
    {
        aiAnimation* anim = scene->mAnimations[i];
        if (allAnimations_.Contains(anim))
            continue;

        if (model)
        {
            bool modelBoneFound = false;
            for (unsigned j = 0; j < anim->mNumChannels; ++j)
            {
                aiNodeAnim* channel = anim->mChannels[j];
                String channelName = FromAIString(channel->mNodeName);
                if (GetBoneIndex(*model, channelName) != M_MAX_UNSIGNED)
                {
                    modelBoneFound = true;
                    break;
                }
            }
            if (modelBoneFound)
            {
                model->animations_.Push(anim);
                allAnimations_.Insert(anim);
            }
        }
        else
        {
            sceneAnimations_.Push(anim);
            allAnimations_.Insert(anim);
        }
    }

    /// \todo Vertex morphs are ignored for now
}

void BuildBoneCollisionInfo(OutModel& model)
{
    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        aiMesh* mesh = model.meshes_[i];
        for (unsigned j = 0; j < mesh->mNumBones; ++j)
        {
            aiBone* bone = mesh->mBones[j];
            String boneName = FromAIString(bone->mName);
            unsigned boneIndex = GetBoneIndex(model, boneName);
            if (boneIndex == M_MAX_UNSIGNED)
                continue;
            for (unsigned k = 0; k < bone->mNumWeights; ++k)
            {
                float weight = bone->mWeights[k].mWeight;
                // Require skinning weight to be sufficiently large before vertex contributes to bone hitbox
                if (weight > 0.33f)
                {
                    aiVector3D vertexBoneSpace = bone->mOffsetMatrix * mesh->mVertices[bone->mWeights[k].mVertexId];
                    Vector3 vertex = ToVector3(vertexBoneSpace);
                    float radius = vertex.Length();
                    if (radius > model.boneRadii_[boneIndex])
                        model.boneRadii_[boneIndex] = radius;
                    model.boneHitboxes_[boneIndex].Merge(vertex);
                }
            }
        }
    }
}

void BuildAndSaveModel(OutModel& model)
{
    if (!model.rootNode_)
    {
        PrintLine("Null root node for model, skipping model save");
        return;
    }

    String rootNodeName = FromAIString(model.rootNode_->mName);
    if (!model.meshes_.Size())
    {
        PrintLine("No geometries found starting from node " + rootNodeName + ", skipping model save");
        return;
    }

    PrintLine("Writing model " + rootNodeName);

    SharedPtr<Model> outModel(new Model(context_));
    Vector<Vector<i32>> allBoneMappings;
    BoundingBox box;

    unsigned numValidGeometries = 0;

    bool combineBuffers = true;
    // Check if buffers can be combined (same vertex elements, under 65535 vertices)
    Vector<VertexElement> elements = GetVertexElements(model.meshes_[0], model.bones_.Size() > 0);
    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        if (GetNumValidFaces(model.meshes_[i]))
        {
            ++numValidGeometries;
            if (i > 0 && GetVertexElements(model.meshes_[i], model.bones_.Size() > 0) != elements)
                combineBuffers = false;
        }
    }

    // Check if keeping separate buffers allows to avoid 32-bit indices
    if (combineBuffers && model.totalVertices_ > 65535)
    {
        bool allUnder65k = true;
        for (unsigned i = 0; i < model.meshes_.Size(); ++i)
        {
            if (GetNumValidFaces(model.meshes_[i]))
            {
                if (model.meshes_[i]->mNumVertices > 65535)
                    allUnder65k = false;
            }
        }
        if (allUnder65k == true)
            combineBuffers = false;
    }

    SharedPtr<IndexBuffer> ib;
    SharedPtr<VertexBuffer> vb;
    Vector<SharedPtr<VertexBuffer>> vbVector;
    Vector<SharedPtr<IndexBuffer>> ibVector;
    unsigned startVertexOffset = 0;
    unsigned startIndexOffset = 0;
    unsigned destGeomIndex = 0;
    bool isSkinned = model.bones_.Size() > 0;

    outModel->SetNumGeometries(numValidGeometries);

    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        aiMesh* mesh = model.meshes_[i];
        Vector<VertexElement> elements = GetVertexElements(mesh, isSkinned);
        unsigned validFaces = GetNumValidFaces(mesh);
        if (!validFaces)
            continue;

        bool largeIndices;
        if (combineBuffers)
            largeIndices = model.totalIndices_ > 65535;
        else
            largeIndices = mesh->mNumVertices > 65535;

        // Create new buffers if necessary
        if (!combineBuffers || vbVector.Empty())
        {
            vb = new VertexBuffer(context_);
            ib = new IndexBuffer(context_);

            if (combineBuffers)
            {
                ib->SetSize(model.totalIndices_, largeIndices);
                vb->SetSize(model.totalVertices_, elements);
            }
            else
            {
                ib->SetSize(validFaces * 3, largeIndices);
                vb->SetSize(mesh->mNumVertices, elements);
            }

            vbVector.Push(vb);
            ibVector.Push(ib);
            startVertexOffset = 0;
            startIndexOffset = 0;
        }

        // Get the world transform of the mesh for baking into the vertices
        Matrix3x4 vertexTransform;
        Matrix3 normalTransform;
        Vector3 pos, scale;
        Quaternion rot;
        GetPosRotScale(GetMeshBakingTransform(model.meshNodes_[i], model.rootNode_), pos, rot, scale);
        vertexTransform = Matrix3x4(pos, rot, scale);
        normalTransform = rot.RotationMatrix();

        SharedPtr<Geometry> geom(new Geometry(context_));

        PrintLine("Writing geometry " + String(i) + " with " + String(mesh->mNumVertices) + " vertices " +
            String(validFaces * 3) + " indices");

        if (model.bones_.Size() > 0 && !mesh->HasBones())
            PrintLine("Warning: model has bones but geometry " + String(i) + " has no skinning information");

        byte* vertexData = vb->GetShadowData();
        byte* indexData = ib->GetShadowData();

        // Build the index data
        if (!largeIndices)
        {
            unsigned short* dest = (unsigned short*)indexData + startIndexOffset;
            for (unsigned j = 0; j < mesh->mNumFaces; ++j)
                WriteShortIndices(dest, mesh, j, startVertexOffset);
        }
        else
        {
            unsigned* dest = (unsigned*)indexData + startIndexOffset;
            for (unsigned j = 0; j < mesh->mNumFaces; ++j)
                WriteLargeIndices(dest, mesh, j, startVertexOffset);
        }

        // Build the vertex data
        // If there are bones, get blend data
        Vector<Vector<unsigned char>> blendIndices;
        Vector<Vector<float>> blendWeights;
        Vector<i32> boneMappings;
        if (model.bones_.Size())
            GetBlendData(model, mesh, model.meshNodes_[i], boneMappings, blendIndices, blendWeights);

        auto* dest = (float*)((unsigned char*)vertexData + startVertexOffset * vb->GetVertexSize());
        for (unsigned j = 0; j < mesh->mNumVertices; ++j)
            WriteVertex(dest, mesh, j, isSkinned, box, vertexTransform, normalTransform, blendIndices, blendWeights);

        // Calculate the geometry center
        Vector3 center = Vector3::ZERO;
        if (validFaces)
        {
            for (unsigned j = 0; j < mesh->mNumFaces; ++j)
            {
                if (mesh->mFaces[j].mNumIndices == 3)
                {
                    center += vertexTransform * ToVector3(mesh->mVertices[mesh->mFaces[j].mIndices[0]]);
                    center += vertexTransform * ToVector3(mesh->mVertices[mesh->mFaces[j].mIndices[1]]);
                    center += vertexTransform * ToVector3(mesh->mVertices[mesh->mFaces[j].mIndices[2]]);
                }
            }

            center /= (float)validFaces * 3;
        }

        // Define the geometry
        geom->SetIndexBuffer(ib);
        geom->SetVertexBuffer(0, vb);
        geom->SetDrawRange(TRIANGLE_LIST, startIndexOffset, validFaces * 3, true);
        outModel->SetNumGeometryLodLevels(destGeomIndex, 1);
        outModel->SetGeometry(destGeomIndex, 0, geom);
        outModel->SetGeometryCenter(destGeomIndex, center);
        if (model.bones_.Size() > maxBones_)
            allBoneMappings.Push(boneMappings);

        startVertexOffset += mesh->mNumVertices;
        startIndexOffset += validFaces * 3;
        ++destGeomIndex;
    }

    // Define the model buffers and bounding box
    Vector<i32> emptyMorphRange;
    outModel->SetVertexBuffers(vbVector, emptyMorphRange, emptyMorphRange);
    outModel->SetIndexBuffers(ibVector);
    outModel->SetBoundingBox(box);
    PrintLine("BBox after vertices: min=" + box.min_.ToString() + " max=" + box.max_.ToString() +
              " importScale=" + String(importScale_));

    // Build skeleton if necessary
    if (model.bones_.Size() && model.rootBone_)
    {
        PrintLine("Writing skeleton with " + String(model.bones_.Size()) + " bones, rootbone " +
            FromAIString(model.rootBone_->mName));

        Skeleton skeleton;
        Vector<Bone>& bones = skeleton.GetModifiableBones();

        for (unsigned i = 0; i < model.bones_.Size(); ++i)
        {
            aiNode* boneNode = model.bones_[i];
            String boneName(FromAIString(boneNode->mName));

            Bone newBone;
            newBone.name_ = boneName;

            aiMatrix4x4 transform = boneNode->mTransformation;
            // Make the root bone transform relative to the model's root node, if it is not already
            // (in case there are nodes between that are not accounted for otherwise)
            if (boneNode == model.rootBone_)
                transform = GetDerivedTransform(boneNode, model.rootNode_, false);

            GetPosRotScale(transform, newBone.initialPosition_, newBone.initialRotation_, newBone.initialScale_);
            newBone.initialPosition_ *= importScale_;

            // Get offset information if exists
            newBone.offsetMatrix_ = GetOffsetMatrix(model, boneName);
            if (bakeScale_ && !originalBoneScales_.Empty())
            {
                // Adjust offset matrix for removed bone scale. With scale removed from
                // the bone hierarchy, the runtime bone world transform no longer includes
                // the ancestor scale. The offset matrix must compensate:
                //   new_offset = accumulatedScale * old_offset
                float accScale = 1.0f;
                aiNode* a = boneNode;
                while (a && a != model.rootNode_)
                {
                    String aName = FromAIString(a->mName);
                    HashMap<String, float>::Iterator it = originalBoneScales_.Find(aName);
                    if (it != originalBoneScales_.End())
                        accScale *= it->second_;
                    a = a->mParent;
                }
                if (fabsf(accScale - 1.0f) > 0.001f)
                {
                    newBone.offsetMatrix_.m00_ *= accScale;
                    newBone.offsetMatrix_.m01_ *= accScale;
                    newBone.offsetMatrix_.m02_ *= accScale;
                    newBone.offsetMatrix_.m03_ *= accScale;
                    newBone.offsetMatrix_.m10_ *= accScale;
                    newBone.offsetMatrix_.m11_ *= accScale;
                    newBone.offsetMatrix_.m12_ *= accScale;
                    newBone.offsetMatrix_.m13_ *= accScale;
                    newBone.offsetMatrix_.m20_ *= accScale;
                    newBone.offsetMatrix_.m21_ *= accScale;
                    newBone.offsetMatrix_.m22_ *= accScale;
                    newBone.offsetMatrix_.m23_ *= accScale;
                }
            }
            if (importScale_ != 1.0f)
            {
                // Scale the translation component of the offset matrix to match scaled vertex space
                newBone.offsetMatrix_.m03_ *= importScale_;
                newBone.offsetMatrix_.m13_ *= importScale_;
                newBone.offsetMatrix_.m23_ *= importScale_;
            }
            newBone.radius_ = model.boneRadii_[i] * importScale_;
            newBone.boundingBox_ = model.boneHitboxes_[i];
            newBone.boundingBox_.min_ *= importScale_;
            newBone.boundingBox_.max_ *= importScale_;
            newBone.collisionMask_ = BONECOLLISION_SPHERE | BONECOLLISION_BOX;
            newBone.parentIndex_ = i;
            bones.Push(newBone);
        }
        // Set the bone hierarchy
        for (unsigned i = 1; i < model.bones_.Size(); ++i)
        {
            String parentName = FromAIString(model.bones_[i]->mParent->mName);
            for (unsigned j = 0; j < bones.Size(); ++j)
            {
                if (bones[j].name_ == parentName)
                {
                    bones[i].parentIndex_ = j;
                    break;
                }
            }
        }

        outModel->SetSkeleton(skeleton);
        if (model.bones_.Size() > maxBones_)
            outModel->SetGeometryBoneMappings(allBoneMappings);
    }

    File outFile(context_);
    if (!outFile.Open(model.outName_, FILE_WRITE))
        ErrorExit("Could not open output file " + model.outName_);
    outModel->Save(outFile);

    // Save material list for use by the editor — always when multiple materials, or when -l flag is set
    if (!noMaterials_ && (saveMaterialList_ || model.meshes_.Size() > 1))
    {
        String materialListName = ReplaceExtension(model.outName_, ".txt");
        File listFile(context_);
        if (listFile.Open(materialListName, FILE_WRITE))
        {
            for (unsigned i = 0; i < model.meshes_.Size(); ++i)
            {
                String matPath = GetMeshMaterialName(model.meshes_[i]);
                listFile.WriteLine(matPath);

                // Validate material file exists relative to resource path
                String fullPath = resourcePath_ + matPath;
                if (!context_->GetSubsystem<FileSystem>()->FileExists(fullPath))
                    PrintLine("Warning: material not found at " + fullPath + " — model may render without textures");
            }
        }
        else
            PrintLine("Warning: could not write material list file " + materialListName);
    }
}

void BuildAndSaveAnimations(OutModel* model)
{
    // extrapolate anim
    ExtrapolatePivotlessAnimation(model);

    // build and save anim
    const Vector<aiAnimation*>& animations = model ? model->animations_ : sceneAnimations_;

    for (unsigned i = 0; i < animations.Size(); ++i)
    {
        aiAnimation* anim = animations[i];

        auto duration = (float)anim->mDuration;
        String animName = FromAIString(anim->mName);
        String animOutName;

        float thisImportEndTime = importEndTime_;
        float thisImportStartTime = importStartTime_;

        // If no animation split specified, set the end time to duration
        if (thisImportEndTime == 0.0f)
            thisImportEndTime = duration;

        if (animName.Empty())
            animName = "Anim" + String(i + 1);
        if (model)
            animOutName = GetPath(model->outName_) + GetFileName(model->outName_) + "_" + SanitateAssetName(animName) + ".ani";
        else
            animOutName = outPath_ + GetFileName(outName_) + "_" + SanitateAssetName(animName) + ".ani";

        auto ticksPerSecond = (float)anim->mTicksPerSecond;
        // If ticks per second not specified, it's probably a .X file. In this case use the default tick rate
        if (ticksPerSecond < M_EPSILON)
            ticksPerSecond = defaultTicksPerSecond_;
        float tickConversion = 1.0f / ticksPerSecond;

        // Find out the start time of animation from each channel's first keyframe for adjusting the keyframe times
        // to start from zero
        float startTime = duration;
        for (unsigned j = 0; j < anim->mNumChannels; ++j)
        {
            aiNodeAnim* channel = anim->mChannels[j];
            if (channel->mNumPositionKeys > 0)
                startTime = Min(startTime, (float)channel->mPositionKeys[0].mTime);
            if (channel->mNumRotationKeys > 0)
                startTime = Min(startTime, (float)channel->mRotationKeys[0].mTime);
            if (channel->mNumScalingKeys > 0)
                startTime = Min(startTime, (float)channel->mScalingKeys[0].mTime);
        }
        if (startTime > thisImportStartTime)
            thisImportStartTime = startTime;
        duration = thisImportEndTime - thisImportStartTime;

        SharedPtr<Animation> outAnim(new Animation(context_));
        outAnim->SetAnimationName(animName);
        outAnim->SetLength(duration * tickConversion);

        PrintLine("Writing animation " + animName + " length " + String(outAnim->GetLength()));
        for (unsigned j = 0; j < anim->mNumChannels; ++j)
        {
            aiNodeAnim* channel = anim->mChannels[j];
            String channelName = FromAIString(channel->mNodeName);
            if (channelName.StartsWith("mixamorig:"))
                channelName = channelName.Substring(10);
            // Strip Assimp FBX pivot suffix (e.g. "LeftArm_$AssimpFbx$_Rotation" → "LeftArm")
            // Some Assimp versions emit these even with PRESERVE_PIVOTS=0.
            {
                i32 fbxPos = channelName.Find("_$AssimpFbx$");
                if (fbxPos != String::NPOS)
                {
                    String cleanName = channelName.Substring(0, fbxPos);
                    // Skip duplicate pivot channels for same bone — keep first only
                    if (outAnim->GetTrack(cleanName) != nullptr)
                        continue;
                    channelName = cleanName;
                }
            }
            aiNode* boneNode = nullptr;

            if (model)
            {
                unsigned boneIndex;
                i32 pos = channelName.Find("_$AssimpFbx$");

                if (!suppressFbxPivotNodes_ || pos == String::NPOS)
                {
                    boneIndex = GetBoneIndex(*model, channelName);
                    if (boneIndex == M_MAX_UNSIGNED)
                    {
                        PrintLine("Warning: skipping animation track " + channelName + " not found in model skeleton");
                        outAnim->RemoveTrack(channelName);
                        continue;
                    }
                    boneNode = model->bones_[boneIndex];
                }
                else
                {
                    channelName = channelName.Substring(0, pos);

                    // every first $fbx animation channel for a bone will consolidate other $fbx animation to a single channel
                    // skip subsequent $fbx animation channel for the same bone
                    if (outAnim->GetTrack(channelName) != nullptr)
                        continue;

                    boneIndex = GetPivotlessBoneIndex(*model, channelName);
                    if (boneIndex == M_MAX_UNSIGNED)
                    {
                        PrintLine("Warning: skipping animation track " + channelName + " not found in model skeleton");
                        outAnim->RemoveTrack(channelName);
                        continue;
                    }

                    boneNode = model->pivotlessBones_[boneIndex];
                }
            }
            else
            {
                // Strip $AssimpFbx$ pivot suffix for standalone animation export
                // (safety net — PRESERVE_PIVOTS=0 should already prevent these)
                i32 fbxPos = channelName.Find("_$AssimpFbx$");
                if (fbxPos != String::NPOS)
                {
                    channelName = channelName.Substring(0, fbxPos);
                    // Skip duplicate pivot channels for same bone — keep first only
                    if (outAnim->GetTrack(channelName) != nullptr)
                        continue;
                }

                boneNode = GetNode(channelName, scene_->mRootNode);
                if (!boneNode)
                    boneNode = GetNode(FromAIString(channel->mNodeName), scene_->mRootNode);
                if (!boneNode)
                {
                    PrintLine("Warning: skipping animation track " + channelName + " whose scene node was not found");
                    outAnim->RemoveTrack(channelName);
                    continue;
                }
            }

            // To export single frame animation, check if first key frame is identical to bone transformation
            aiVector3D bonePos, boneScale;
            aiQuaternion boneRot;
            boneNode->mTransformation.Decompose(boneScale, boneRot, bonePos);

            bool posEqual = true;
            bool scaleEqual = true;
            bool rotEqual = true;

            if (channel->mNumPositionKeys > 0 && !ToVector3(bonePos).Equals(ToVector3(channel->mPositionKeys[0].mValue)))
                posEqual = false;
            if (channel->mNumScalingKeys > 0 && !ToVector3(boneScale).Equals(ToVector3(channel->mScalingKeys[0].mValue)))
                scaleEqual = false;
            if (channel->mNumRotationKeys > 0 && !ToQuaternion(boneRot).Equals(ToQuaternion(channel->mRotationKeys[0].mValue)))
                rotEqual = false;

            AnimationTrack* track = outAnim->CreateTrack(channelName);

            // Check which channels are used
            track->channelMask_ = AnimationChannels::None;
            if (channel->mNumPositionKeys > 1 || !posEqual)
                track->channelMask_ |= AnimationChannels::Position;
            if (channel->mNumRotationKeys > 1 || !rotEqual)
                track->channelMask_ |= AnimationChannels::Rotation;
            if (channel->mNumScalingKeys > 1 || !scaleEqual)
                track->channelMask_ |= AnimationChannels::Scale;
            // Check for redundant identity scale in all keyframes and remove in that case
            if (!!(track->channelMask_ & AnimationChannels::Scale))
            {
                bool redundantScale = true;
                for (unsigned k = 0; k < channel->mNumScalingKeys; ++k)
                {
                    float SCALE_EPSILON = 0.000001f;
                    Vector3 scaleVec = ToVector3(channel->mScalingKeys[k].mValue);
                    if (fabsf(scaleVec.x_ - 1.0f) >= SCALE_EPSILON || fabsf(scaleVec.y_ - 1.0f) >= SCALE_EPSILON ||
                        fabsf(scaleVec.z_ - 1.0f) >= SCALE_EPSILON)
                    {
                        redundantScale = false;
                        break;
                    }
                }
                if (redundantScale)
                    track->channelMask_ &= ~AnimationChannels::Scale;
            }
            // When baking scale, strip all scale channels — skeleton is now identity scale
            if (bakeScale_ && !!(track->channelMask_ & AnimationChannels::Scale))
                track->channelMask_ &= ~AnimationChannels::Scale;

            if (!track->channelMask_)
            {
                PrintLine("Warning: skipping animation track " + channelName + " with no keyframes");
                outAnim->RemoveTrack(channelName);
                continue;
            }

            // Currently only same amount of keyframes is supported
            // Note: should also check the times of individual keyframes for match
            if ((channel->mNumPositionKeys > 1 && channel->mNumRotationKeys > 1 && channel->mNumPositionKeys != channel->mNumRotationKeys) ||
                (channel->mNumPositionKeys > 1 && channel->mNumScalingKeys > 1 && channel->mNumPositionKeys != channel->mNumScalingKeys) ||
                (channel->mNumRotationKeys > 1 && channel->mNumScalingKeys > 1 && channel->mNumRotationKeys != channel->mNumScalingKeys))
            {
                PrintLine("Warning: differing amounts of channel keyframes, skipping animation track " + channelName);
                outAnim->RemoveTrack(channelName);
                continue;
            }

            unsigned keyFrames = channel->mNumPositionKeys;
            if (channel->mNumRotationKeys > keyFrames)
                keyFrames = channel->mNumRotationKeys;
            if (channel->mNumScalingKeys > keyFrames)
                keyFrames = channel->mNumScalingKeys;

            if (keyFrames == 0)
                URHO3D_LOGWARNINGF("Animation track '%s' has 0 keyFrames (pos=%u rot=%u scale=%u)",
                    channelName.CString(), channel->mNumPositionKeys, channel->mNumRotationKeys, channel->mNumScalingKeys);
            for (unsigned k = 0; k < keyFrames; ++k)
            {
                AnimationKeyFrame kf;
                kf.time_ = 0.0f;
                kf.position_ = Vector3::ZERO;
                kf.rotation_ = Quaternion::IDENTITY;
                kf.scale_ = Vector3::ONE;

                // Get time for the keyframe. Adjust with animation's start time
                if (!!(track->channelMask_ & AnimationChannels::Position) && k < channel->mNumPositionKeys)
                    kf.time_ = ((float)channel->mPositionKeys[k].mTime - startTime);
                else if (!!(track->channelMask_ & AnimationChannels::Rotation) && k < channel->mNumRotationKeys)
                    kf.time_ = ((float)channel->mRotationKeys[k].mTime - startTime);
                else if (!!(track->channelMask_ & AnimationChannels::Scale) && k < channel->mNumScalingKeys)
                    kf.time_ = ((float)channel->mScalingKeys[k].mTime - startTime);

                // Make sure time stays positive
                kf.time_ = Max(kf.time_, 0.0f);

                // Start with the bone's base transform. If scale was baked, use the ORIGINAL
                // (pre-bake) transform so fallback positions aren't contaminated by the bake's
                // position adjustments (which are 100x larger for descendants of scaled bones)
                aiMatrix4x4 boneTransform = boneNode->mTransformation;
                if (bakeScale_ && !originalBoneTransforms_.Empty())
                {
                    String bName = FromAIString(boneNode->mName);
                    HashMap<String, aiMatrix4x4>::Iterator btIt = originalBoneTransforms_.Find(bName);
                    if (btIt != originalBoneTransforms_.End())
                        boneTransform = btIt->second_;
                }
                aiVector3D pos, scale;
                aiQuaternion rot;
                boneTransform.Decompose(scale, rot, pos);
                // Then apply the active channels
                if (!!(track->channelMask_ & AnimationChannels::Position) && k < channel->mNumPositionKeys)
                    pos = channel->mPositionKeys[k].mValue;
                if (!!(track->channelMask_ & AnimationChannels::Rotation) && k < channel->mNumRotationKeys)
                    rot = channel->mRotationKeys[k].mValue;
                if (!!(track->channelMask_ & AnimationChannels::Scale) && k < channel->mNumScalingKeys)
                    scale = channel->mScalingKeys[k].mValue;

                // If root bone, transform with nodes in between model root node (if any)
                if (model && boneNode == model->rootBone_)
                {
                    aiMatrix4x4 transMat, scaleMat, rotMat;
                    aiMatrix4x4::Translation(pos, transMat);
                    aiMatrix4x4::Scaling(scale, scaleMat);
                    rotMat = aiMatrix4x4(rot.GetMatrix());
                    aiMatrix4x4 tform = transMat * rotMat * scaleMat;
                    aiMatrix4x4 tformOld = tform;
                    tform = GetDerivedTransform(tform, boneNode, model->rootNode_, false);
                    // Do not decompose if did not actually change
                    if (tform != tformOld)
                        tform.Decompose(scale, rot, pos);
                }

                if (!!(track->channelMask_ & AnimationChannels::Position))
                {
                    kf.position_ = ToVector3(pos);

                    // When baking scale, adjust position keyframes by ACCUMULATED ancestor scale.
                    // Scale cascades: removing scale from the root affects ALL descendants,
                    // not just direct children. Every bone's keyframe positions were expressed
                    // in a coordinate space that included the cascaded ancestor scale.
                    if (bakeScale_ && model && !originalBoneScales_.Empty() && boneNode != model->rootBone_)
                    {
                        float accScale = 1.0f;
                        aiNode* a = boneNode->mParent;
                        while (a && a != model->rootNode_)
                        {
                            String aName = FromAIString(a->mName);
                            HashMap<String, float>::Iterator sit = originalBoneScales_.Find(aName);
                            if (sit != originalBoneScales_.End())
                                accScale *= sit->second_;
                            a = a->mParent;
                        }
                        if (fabsf(accScale - 1.0f) > 0.001f)
                            kf.position_ *= accScale;
                    }

                    kf.position_ *= importScale_;
                }
                if (!!(track->channelMask_ & AnimationChannels::Rotation))
                    kf.rotation_ = ToQuaternion(rot);
                if (!!(track->channelMask_ & AnimationChannels::Scale))
                    kf.scale_ = ToVector3(scale);
                if (kf.time_ >= thisImportStartTime && kf.time_ <= thisImportEndTime)
                {
                    kf.time_ = (kf.time_ - thisImportStartTime) * tickConversion;
                    track->keyFrames_.Push(kf);
                }
                else if (k == 0 && track->keyFrames_.Empty())
                {
                    URHO3D_LOGWARNINGF("Animation keyframe rejected: track '%s' kf[0] time=%.4f "
                        "range=[%.4f, %.4f] startTime=%.4f tickConv=%.6f",
                        channelName.CString(), kf.time_,
                        thisImportStartTime, thisImportEndTime, startTime, tickConversion);
                }
            }
        }

        File outFile(context_);
        if (!outFile.Open(animOutName, FILE_WRITE))
            ErrorExit("Could not open output file " + animOutName);
        outAnim->Save(outFile);
    }
}

void ExportScene(const String& outName, bool asPrefab)
{
    OutScene outScene;
    outScene.outName_ = outName;
    outScene.rootNode_ = rootNode_;

    if (useSubdirs_)
        context_->GetSubsystem<FileSystem>()->CreateDir(resourcePath_ + "Models");

    CollectSceneModels(outScene, rootNode_);

    // Save models, their material lists and animations
    for (unsigned i = 0; i < outScene.models_.Size(); ++i)
        BuildAndSaveModel(outScene.models_[i]);

    // Save scene-global animations
    if (!noAnimations_)
    {
        CollectAnimations();
        BuildAndSaveAnimations();
    }

    // Save scene
    BuildAndSaveScene(outScene, asPrefab);
}

void CollectSceneModels(OutScene& scene, aiNode* node)
{
    Vector<Pair<aiNode*, aiMesh*>> meshes;
    GetMeshesUnderNode(meshes, node);

    if (meshes.Size())
    {
        OutModel model;
        model.rootNode_ = node;
        model.outName_ = resourcePath_ + (useSubdirs_ ? "Models/" : "") + SanitateAssetName(FromAIString(node->mName)) + ".mdl";
        for (unsigned i = 0; i < meshes.Size(); ++i)
        {
            aiMesh* mesh = meshes[i].second_;
            unsigned meshIndex = GetMeshIndex(mesh);
            model.meshIndices_.Insert(meshIndex);
            model.meshes_.Push(mesh);
            model.meshNodes_.Push(meshes[i].first_);
            model.totalVertices_ += mesh->mNumVertices;
            model.totalIndices_ += GetNumValidFaces(mesh) * 3;
        }

        // Check if a model with identical mesh indices already exists. If yes, do not export twice
        bool unique = true;
        if (checkUniqueModel_)
        {
            for (unsigned i = 0; i < scene.models_.Size(); ++i)
            {
                if (scene.models_[i].meshIndices_ == model.meshIndices_)
                {
                    PrintLine("Added node " + FromAIString(node->mName));
                    scene.nodes_.Push(node);
                    scene.nodeModelIndices_.Push(i);
                    unique = false;
                    break;
                }
            }
        }
        if (unique)
        {
            PrintLine("Added model " + model.outName_);
            PrintLine("Added node " + FromAIString(node->mName));
            CollectBones(model);
            DetectBoneScale(model);
            BakeSkeletonScale(model);
            AutoFaceZPlus(model);
            BuildBoneCollisionInfo(model);
            if (!noAnimations_)
            {
                CollectAnimations(&model);
                BuildAndSaveAnimations(&model);
            }

            scene.models_.Push(model);
            scene.nodes_.Push(node);
            scene.nodeModelIndices_.Push(scene.models_.Size() - 1);
        }
    }

    for (unsigned i = 0; i < node->mNumChildren; ++i)
        CollectSceneModels(scene, node->mChildren[i]);
}

void CreateHierarchy(Scene* scene, aiNode* srcNode, HashMap<aiNode*, Node*>& nodeMapping)
{
    CreateSceneNode(scene, srcNode, nodeMapping);
    for (unsigned i = 0; i < srcNode->mNumChildren; ++i)
        CreateHierarchy(scene, srcNode->mChildren[i], nodeMapping);
}

Node* CreateSceneNode(Scene* scene, aiNode* srcNode, HashMap<aiNode*, Node*>& nodeMapping)
{
    if (nodeMapping.Contains(srcNode))
        return nodeMapping[srcNode];
    // Flatten hierarchy if requested
    if (noHierarchy_)
    {
        Node* outNode = scene->CreateChild(FromAIString(srcNode->mName), localIDs_ ? LOCAL : REPLICATED);
        Vector3 pos, scale;
        Quaternion rot;
        GetPosRotScale(GetDerivedTransform(srcNode, rootNode_), pos, rot, scale);
        outNode->SetTransform(pos, rot, scale);
        nodeMapping[srcNode] = outNode;

        return outNode;
    }

    if (srcNode == rootNode_ || !srcNode->mParent)
    {
        Node* outNode = scene->CreateChild(FromAIString(srcNode->mName), localIDs_ ? LOCAL : REPLICATED);
        Vector3 pos, scale;
        Quaternion rot;
        GetPosRotScale(srcNode->mTransformation, pos, rot, scale);
        outNode->SetTransform(pos, rot, scale);
        nodeMapping[srcNode] = outNode;

        return outNode;
    }
    else
    {
        // Ensure the existence of the parent chain as in the original file
        if (!nodeMapping.Contains(srcNode->mParent))
            CreateSceneNode(scene, srcNode->mParent, nodeMapping);

        Node* parent = nodeMapping[srcNode->mParent];
        Node* outNode = parent->CreateChild(FromAIString(srcNode->mName), localIDs_ ? LOCAL : REPLICATED);
        Vector3 pos, scale;
        Quaternion rot;
        GetPosRotScale(srcNode->mTransformation, pos, rot, scale);
        outNode->SetTransform(pos, rot, scale);
        nodeMapping[srcNode] = outNode;

        return outNode;
    }
}

void BuildAndSaveScene(OutScene& scene, bool asPrefab)
{
    if (!asPrefab)
        PrintLine("Writing scene");
    else
        PrintLine("Writing node hierarchy");

    SharedPtr<Scene> outScene(new Scene(context_));

    if (!asPrefab)
    {
        #ifdef URHO3D_PHYSICS
        /// \todo Make the physics properties configurable
        outScene->CreateComponent<PhysicsWorld>();
        #endif

        /// \todo Make the octree properties configurable, or detect from the scene contents
        outScene->CreateComponent<Octree>();

        outScene->CreateComponent<DebugRenderer>();

        if (createZone_)
        {
            Node* zoneNode = outScene->CreateChild("Zone", localIDs_ ? LOCAL : REPLICATED);
            auto* zone = zoneNode->CreateComponent<Zone>();
            zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.f));
            zone->SetAmbientColor(Color(0.25f, 0.25f, 0.25f));

            // Create default light only if scene does not define them
            if (!scene_->HasLights())
            {
                Node* lightNode = outScene->CreateChild("GlobalLight", localIDs_ ? LOCAL : REPLICATED);
                auto* light = lightNode->CreateComponent<Light>();
                light->SetLightType(LIGHT_DIRECTIONAL);
                lightNode->SetRotation(Quaternion(60.0f, 30.0f, 0.0f));
            }
        }
    }

    auto* cache = context_->GetSubsystem<ResourceCache>();

    HashMap<aiNode*, Node*> nodeMapping;

    Node* outRootNode = nullptr;
    if (asPrefab)
        outRootNode = CreateSceneNode(outScene, rootNode_, nodeMapping);
    else
    {
        // If not saving as a prefab, associate the root node with the scene first to prevent unnecessary creation of a root
        // However do not do that if the root node does not have an identity matrix, or itself contains a model
        // (models at the Urho scene root are not preferable)
        if (ToMatrix3x4(rootNode_->mTransformation).Equals(Matrix3x4::IDENTITY) && !scene.nodes_.Contains(rootNode_))
           nodeMapping[rootNode_] = outScene;
    }

    // If is allowed to export empty nodes, export the full Assimp node hierarchy first
    if (!noHierarchy_ && !noEmptyNodes_)
        CreateHierarchy(outScene, rootNode_, nodeMapping);

    // Create geometry nodes
    for (unsigned i = 0; i < scene.nodes_.Size(); ++i)
    {
        const OutModel& model = scene.models_[scene.nodeModelIndices_[i]];
        Node* modelNode = CreateSceneNode(outScene, scene.nodes_[i], nodeMapping);
        auto* staticModel =
            static_cast<StaticModel*>(
                model.bones_.Empty() ? modelNode->CreateComponent<StaticModel>() : modelNode->CreateComponent<AnimatedModel>());

        // Create a dummy model so that the reference can be stored
        String modelName = (useSubdirs_ ? "Models/" : "") + GetFileNameAndExtension(model.outName_);
        if (!cache->Exists(modelName))
        {
            auto* dummyModel = new Model(context_);
            dummyModel->SetName(modelName);
            dummyModel->SetNumGeometries(model.meshes_.Size());
            cache->AddManualResource(dummyModel);
        }
        staticModel->SetModel(cache->GetResource<Model>(modelName));

        // Set materials if they are known
        for (unsigned j = 0; j < model.meshes_.Size(); ++j)
        {
            String matName = GetMeshMaterialName(model.meshes_[j]);
            // Create a dummy material so that the reference can be stored
            if (!cache->Exists(matName))
            {
                auto* dummyMat = new Material(context_);
                dummyMat->SetName(matName);
                cache->AddManualResource(dummyMat);
            }
            staticModel->SetMaterial(j, cache->GetResource<Material>(matName));
        }
    }

    // Create lights
    if (!asPrefab)
    {
        for (unsigned i = 0; i < scene_->mNumLights; ++i)
        {
            aiLight* light = scene_->mLights[i];
            aiNode* lightNode = GetNode(FromAIString(light->mName), rootNode_, true);
            if (!lightNode)
                continue;
            Node* outNode = CreateSceneNode(outScene, lightNode, nodeMapping);

            Vector3 lightAdjustPosition = ToVector3(light->mPosition);
            Vector3 lightAdjustDirection = ToVector3(light->mDirection);
            // If light is not aligned at the scene node, an adjustment node needs to be created
            if (!lightAdjustPosition.Equals(Vector3::ZERO) || (light->mType != aiLightSource_POINT &&
                !lightAdjustDirection.Equals(Vector3::FORWARD)))
            {
                outNode = outNode->CreateChild("LightAdjust");
                outNode->SetPosition(lightAdjustPosition);
                outNode->SetDirection(lightAdjustDirection);
            }

            auto* outLight = outNode->CreateComponent<Light>();
            outLight->SetColor(Color(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b));

            switch (light->mType)
            {
            case aiLightSource_DIRECTIONAL:
                outLight->SetLightType(LIGHT_DIRECTIONAL);
                break;
            case aiLightSource_SPOT:
                outLight->SetLightType(LIGHT_SPOT);
                outLight->SetFov(light->mAngleOuterCone * 0.5f * M_RADTODEG);
                break;
            case aiLightSource_POINT:
                outLight->SetLightType(LIGHT_POINT);
                break;
            default:
                break;
            }

            // Calculate range from attenuation parameters so that light intensity has been reduced to 10% at that distance
            if (light->mType != aiLightSource_DIRECTIONAL)
            {
                float a = light->mAttenuationQuadratic;
                float b = light->mAttenuationLinear;
                float c = -10.0f;
                if (!Equals(a, 0.0f))
                {
                    float root1 = (-b + sqrtf(b * b - 4.0f * a * c)) / (2.0f * a);
                    float root2 = (-b - sqrtf(b * b - 4.0f * a * c)) / (2.0f * a);
                    outLight->SetRange(Max(root1, root2));
                }
                else if (!Equals(b, 0.0f))
                    outLight->SetRange(-c / b);
            }
        }
    }

    File file(context_);
    if (!file.Open(scene.outName_, FILE_WRITE))
        ErrorExit("Could not open output file " + scene.outName_);
    if (!asPrefab)
    {
        if (saveBinary_)
            outScene->Save(file);
        else if (saveJson_)
            outScene->SaveJSON(file);
        else
            outScene->SaveXML(file);
    }
    else
    {
        if (saveBinary_)
            outRootNode->Save(file);
        else if (saveJson_)
            outRootNode->SaveJSON(file);
        else
            outRootNode->SaveXML(file);
    }
}

void CollectMaterialTextures(HashSet<String>& usedTextures)
{
    // Lightweight pass: gather all texture filenames from materials without writing anything.
    // Called before CopyTextures so the copy step knows what to look for.
    for (unsigned i = 0; i < scene_->mNumMaterials; ++i)
    {
        aiMaterial* material = scene_->mMaterials[i];
        aiString stringVal;
        if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), stringVal) == AI_SUCCESS)
            usedTextures.Insert(GetFileNameAndExtension(FromAIString(stringVal)));
        if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0), stringVal) == AI_SUCCESS)
            usedTextures.Insert(GetFileNameAndExtension(FromAIString(stringVal)));
        if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_SPECULAR, 0), stringVal) == AI_SUCCESS)
            usedTextures.Insert(GetFileNameAndExtension(FromAIString(stringVal)));
        if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_LIGHTMAP, 0), stringVal) == AI_SUCCESS)
            usedTextures.Insert(GetFileNameAndExtension(FromAIString(stringVal)));
        if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_EMISSIVE, 0), stringVal) == AI_SUCCESS)
            usedTextures.Insert(GetFileNameAndExtension(FromAIString(stringVal)));
    }
}

void ExportMaterials(HashSet<String>& usedTextures)
{
    if (useSubdirs_)
        context_->GetSubsystem<FileSystem>()->CreateDir(resourcePath_ + "Materials");

    for (unsigned i = 0; i < scene_->mNumMaterials; ++i)
        BuildAndSaveMaterial(scene_->mMaterials[i], usedTextures);
}

void BuildAndSaveMaterial(aiMaterial* material, HashSet<String>& usedTextures)
{
    aiString matNameStr;
    material->Get(AI_MATKEY_NAME, matNameStr);
    String matName = SanitateAssetName(FromAIString(matNameStr));
    if (matName.Trimmed().Empty())
        matName = GenerateMaterialName(material);

    // Do not actually create a material instance, but instead craft an xml file manually
    XMLFile outMaterial(context_);
    XMLElement materialElem = outMaterial.CreateRoot("material");

    String diffuseTexName;
    String normalTexName;
    String specularTexName;
    String lightmapTexName;
    String emissiveTexName;
    Color diffuseColor = Color::WHITE;
    Color specularColor;
    Color emissiveColor = Color::BLACK;
    bool hasAlpha = false;
    bool twoSided = false;
    float specPower = 1.0f;

    aiString stringVal;
    float floatVal;
    int intVal;
    aiColor3D colorVal;

    if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0), stringVal) == AI_SUCCESS)
        diffuseTexName = GetFileNameAndExtension(FromAIString(stringVal));
    if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0), stringVal) == AI_SUCCESS)
        normalTexName = GetFileNameAndExtension(FromAIString(stringVal));
    if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_SPECULAR, 0), stringVal) == AI_SUCCESS)
        specularTexName = GetFileNameAndExtension(FromAIString(stringVal));
    if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_LIGHTMAP, 0), stringVal) == AI_SUCCESS)
        lightmapTexName = GetFileNameAndExtension(FromAIString(stringVal));
    if (material->Get(AI_MATKEY_TEXTURE(aiTextureType_EMISSIVE, 0), stringVal) == AI_SUCCESS)
        emissiveTexName = GetFileNameAndExtension(FromAIString(stringVal));
    if (!noMaterialDiffuseColor_)
    {
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, colorVal) == AI_SUCCESS)
            diffuseColor = Color(colorVal.r, colorVal.g, colorVal.b);
    }
    if (material->Get(AI_MATKEY_COLOR_SPECULAR, colorVal) == AI_SUCCESS)
        specularColor = Color(colorVal.r, colorVal.g, colorVal.b);
    if (!emissiveAO_)
    {
        if (material->Get(AI_MATKEY_COLOR_EMISSIVE, colorVal) == AI_SUCCESS)
            emissiveColor = Color(colorVal.r, colorVal.g, colorVal.b);
    }
    if (material->Get(AI_MATKEY_OPACITY, floatVal) == AI_SUCCESS)
    {
        /// \hack New Assimp behavior - some materials may return 0 opacity, which is invisible.
        /// Revert to full opacity in that case
        if (floatVal < M_EPSILON)
            floatVal = 1.0f;

        if (floatVal < 1.0f)
            hasAlpha = true;
        diffuseColor.a_ = floatVal;
    }
    if (material->Get(AI_MATKEY_SHININESS, floatVal) == AI_SUCCESS)
        specPower = floatVal;
    if (material->Get(AI_MATKEY_TWOSIDED, intVal) == AI_SUCCESS)
        twoSided = (intVal != 0);

    String techniqueName = "Techniques/NoTexture";
    if (!diffuseTexName.Empty())
    {
        techniqueName = "Techniques/Diff";
        if (!normalTexName.Empty())
            techniqueName += "Normal";
        if (!specularTexName.Empty())
            techniqueName += "Spec";
        // For now lightmap does not coexist with normal & specular
        if (normalTexName.Empty() && specularTexName.Empty() && !lightmapTexName.Empty())
            techniqueName += "LightMap";
        if (lightmapTexName.Empty() && !emissiveTexName.Empty())
            techniqueName += emissiveAO_ ? "AO" : "Emissive";
    }
    if (hasAlpha)
        techniqueName += "Alpha";

    XMLElement techniqueElem = materialElem.CreateChild("technique");
    techniqueElem.SetString("name", techniqueName + ".xml");

    if (!diffuseTexName.Empty())
    {
        XMLElement diffuseElem = materialElem.CreateChild("texture");
        diffuseElem.SetString("unit", "diffuse");
        diffuseElem.SetString("name", GetMaterialTextureName(diffuseTexName));
        usedTextures.Insert(diffuseTexName);
    }
    if (!normalTexName.Empty())
    {
        XMLElement normalElem = materialElem.CreateChild("texture");
        normalElem.SetString("unit", "normal");
        normalElem.SetString("name", GetMaterialTextureName(normalTexName));
        usedTextures.Insert(normalTexName);
    }
    if (!specularTexName.Empty())
    {
        XMLElement specularElem = materialElem.CreateChild("texture");
        specularElem.SetString("unit", "specular");
        specularElem.SetString("name", GetMaterialTextureName(specularTexName));
        usedTextures.Insert(specularTexName);
    }
    if (!lightmapTexName.Empty())
    {
        XMLElement lightmapElem = materialElem.CreateChild("texture");
        lightmapElem.SetString("unit", "emissive");
        lightmapElem.SetString("name", GetMaterialTextureName(lightmapTexName));
        usedTextures.Insert(lightmapTexName);
    }
    if (!emissiveTexName.Empty())
    {
        XMLElement emissiveElem = materialElem.CreateChild("texture");
        emissiveElem.SetString("unit", "emissive");
        emissiveElem.SetString("name", GetMaterialTextureName(emissiveTexName));
        usedTextures.Insert(emissiveTexName);
    }

    XMLElement diffuseColorElem = materialElem.CreateChild("parameter");
    diffuseColorElem.SetString("name", "MatDiffColor");
    diffuseColorElem.SetColor("value", diffuseColor);
    XMLElement specularElem = materialElem.CreateChild("parameter");
    specularElem.SetString("name", "MatSpecColor");
    specularElem.SetVector4("value", Vector4(specularColor.r_, specularColor.g_, specularColor.b_, specPower));
    XMLElement emissiveColorElem = materialElem.CreateChild("parameter");
    emissiveColorElem.SetString("name", "MatEmissiveColor");
    emissiveColorElem.SetColor("value", emissiveColor);

    if (twoSided)
    {
        XMLElement cullElem = materialElem.CreateChild("cull");
        XMLElement shadowCullElem = materialElem.CreateChild("shadowcull");
        cullElem.SetString("value", "none");
        shadowCullElem.SetString("value", "none");
    }

    auto* fileSystem = context_->GetSubsystem<FileSystem>();

    String outFileName = resourcePath_ + (useSubdirs_ ? "Materials/" : "" ) + matName + ".xml";
    if (noOverwriteMaterial_ && fileSystem->FileExists(outFileName))
    {
        PrintLine("Skipping save of existing material " + matName);
        return;
    }

    // Verify all referenced textures can be resolved before writing the material.
    // If any texture is missing, refuse to write the material and warn loudly.
    // This prevents orphaned material XMLs pointing at nonexistent textures.
    String missingTextures;
    String texNames[] = { diffuseTexName, normalTexName, specularTexName, lightmapTexName, emissiveTexName };
    for (unsigned t = 0; t < 5; ++t)
    {
        if (texNames[t].Empty())
            continue;
        // Embedded textures (starting with '*') are always available
        if (texNames[t].Length() && texNames[t][0] == '*')
            continue;
        // Check if texture exists at source location OR already in resource path
        String destCheck = resourcePath_ + GetMaterialTextureName(texNames[t]);
        if (!fileSystem->FileExists(destCheck))
        {
            if (!missingTextures.Empty())
                missingTextures += ", ";
            missingTextures += texNames[t];
        }
    }

    if (!missingTextures.Empty())
    {
        PrintLine("WARNING: Material '" + matName + "' references missing textures: " + missingTextures);
        PrintLine("  REFUSING to write orphaned material — fix texture paths or provide the files!");
        PrintLine("  Expected locations: source dir or " + resourcePath_ + (useSubdirs_ ? "Textures/" : ""));
        return;
    }

    PrintLine("Writing material " + matName);

    File outFile(context_);
    if (!outFile.Open(outFileName, FILE_WRITE))
        ErrorExit("Could not open output file " + outFileName);
    outMaterial.Save(outFile);
}

void CopyTextures(const HashSet<String>& usedTextures, const String& sourcePath)
{
    auto* fileSystem = context_->GetSubsystem<FileSystem>();

    if (useSubdirs_)
        fileSystem->CreateDir(resourcePath_ + "Textures");

    for (HashSet<String>::ConstIterator i = usedTextures.Begin(); i != usedTextures.End(); ++i)
    {
        // Handle assimp embedded textures
        if (i->Length() && i->At(0) == '*')
        {
            unsigned texIndex = ToI32(i->Substring(1));
            if (texIndex >= scene_->mNumTextures)
                PrintLine("Skipping out of range texture index " + String(texIndex));
            else
            {
                aiTexture* tex = scene_->mTextures[texIndex];
                String fullDestName = resourcePath_ + GenerateTextureName(texIndex);
                bool destExists = fileSystem->FileExists(fullDestName);
                if (destExists && noOverwriteTexture_)
                {
                    PrintLine("Skipping copy of existing embedded texture " + GetFileNameAndExtension(fullDestName));
                    continue;
                }
                // Encoded texture
                if (!tex->mHeight)
                {
                    PrintLine("Saving embedded texture " + GetFileNameAndExtension(fullDestName));
                    File dest(context_, fullDestName, FILE_WRITE);
                    dest.Write((const void*)tex->pcData, tex->mWidth);
                }
                // RGBA8 texture
                else
                {
                    PrintLine("Saving embedded RGBA texture " + GetFileNameAndExtension(fullDestName));
                    Image image(context_);
                    image.SetSize(tex->mWidth, tex->mHeight, 4);
                    memcpy(image.GetData(), (const void*)tex->pcData, (size_t)tex->mWidth * tex->mHeight * 4);
                    image.SavePNG(fullDestName);
                }
            }
        }
        else
        {
            String fullSourceName = sourcePath + *i;
            String fullDestName = resourcePath_ + (useSubdirs_ ? "Textures/" : "") + *i;

            if (!fileSystem->FileExists(fullSourceName))
            {
                PrintLine("Skipping copy of nonexisting material texture " + *i);
                continue;
            }
            {
                File test(context_, fullSourceName);
                if (!test.GetSize())
                {
                    PrintLine("Skipping copy of zero-size material texture " + *i);
                    continue;
                }
            }

            bool destExists = fileSystem->FileExists(fullDestName);
            if (destExists && noOverwriteTexture_)
            {
                PrintLine("Skipping copy of existing texture " + *i);
                continue;
            }
            if (destExists && noOverwriteNewerTexture_ && fileSystem->GetLastModifiedTime(fullDestName) >
                fileSystem->GetLastModifiedTime(fullSourceName))
            {
                PrintLine("Skipping copying of material texture " + *i + ", destination is newer");
                continue;
            }

            PrintLine("Copying material texture " + *i);
            fileSystem->Copy(fullSourceName, fullDestName);
        }
    }
}

void CombineLods(const Vector<float>& lodDistances, const Vector<String>& modelNames, const String& outName)
{
    // Load models
    Vector<SharedPtr<Model>> srcModels;
    for (unsigned i = 0; i < modelNames.Size(); ++i)
    {
        PrintLine("Reading LOD level " + String(i) + ": model " + modelNames[i] + " distance " + String(lodDistances[i]));
        File srcFile(context_);
        srcFile.Open(modelNames[i]);
        SharedPtr<Model> srcModel(new Model(context_));
        if (!srcModel->Load(srcFile))
            ErrorExit("Could not load input model " + modelNames[i]);
        srcModels.Push(srcModel);
    }

    // Check that none of the models already has LOD levels
    for (unsigned i = 0; i < srcModels.Size(); ++i)
    {
        for (unsigned j = 0; j < srcModels[i]->GetNumGeometries(); ++j)
        {
            if (srcModels[i]->GetNumGeometryLodLevels(j) > 1)
                ErrorExit(modelNames[i] + " already has multiple LOD levels defined");
        }
    }

    // Check for number of geometries (need to have same amount for now)
    for (unsigned i = 1; i < srcModels.Size(); ++i)
    {
        if (srcModels[i]->GetNumGeometries() != srcModels[0]->GetNumGeometries())
            ErrorExit(modelNames[i] + " has different amount of geometries than " + modelNames[0]);
    }

    // If there are bones, check for compatibility (need to have exact match for now)
    for (unsigned i = 1; i < srcModels.Size(); ++i)
    {
        if (srcModels[i]->GetSkeleton().GetNumBones() != srcModels[0]->GetSkeleton().GetNumBones())
            ErrorExit(modelNames[i] + " has different amount of bones than " + modelNames[0]);
        for (unsigned j = 0; j < srcModels[0]->GetSkeleton().GetNumBones(); ++j)
        {
            if (srcModels[i]->GetSkeleton().GetBone(j)->name_ != srcModels[0]->GetSkeleton().GetBone(j)->name_)
                ErrorExit(modelNames[i] + " has different bones than " + modelNames[0]);
        }
        if (srcModels[i]->GetGeometryBoneMappings() != srcModels[0]->GetGeometryBoneMappings())
            ErrorExit(modelNames[i] + " has different per-geometry bone mappings than " + modelNames[0]);
    }

    Vector<SharedPtr<VertexBuffer>> vbVector;
    Vector<SharedPtr<IndexBuffer>> ibVector;
    Vector<i32> emptyMorphRange;

    // Create the final model
    SharedPtr<Model> outModel(new Model(context_));
    outModel->SetNumGeometries(srcModels[0]->GetNumGeometries());
    for (unsigned i = 0; i < srcModels[0]->GetNumGeometries(); ++i)
    {
        outModel->SetNumGeometryLodLevels(i, srcModels.Size());
        for (unsigned j = 0; j < srcModels.Size(); ++j)
        {
            Geometry* geometry = srcModels[j]->GetGeometry(i, 0);
            geometry->SetLodDistance(lodDistances[j]);
            outModel->SetGeometry(i, j, geometry);

            for (unsigned k = 0; k < geometry->GetNumVertexBuffers(); ++k)
            {
                SharedPtr<VertexBuffer> vb(geometry->GetVertexBuffer(k));
                if (!vbVector.Contains(vb))
                    vbVector.Push(vb);
            }

            SharedPtr<IndexBuffer> ib(geometry->GetIndexBuffer());
            if (!ibVector.Contains(ib))
                ibVector.Push(ib);
        }
    }

    outModel->SetVertexBuffers(vbVector, emptyMorphRange, emptyMorphRange);
    outModel->SetIndexBuffers(ibVector);
    outModel->SetSkeleton(srcModels[0]->GetSkeleton());
    outModel->SetGeometryBoneMappings(srcModels[0]->GetGeometryBoneMappings());
    outModel->SetBoundingBox(srcModels[0]->GetBoundingBox());
    /// \todo Vertex morphs are ignored for now

    // Save the final model
    PrintLine("Writing output model");
    File outFile(context_);
    if (!outFile.Open(outName, FILE_WRITE))
        ErrorExit("Could not open output file " + outName);
    outModel->Save(outFile);
}

void GetMeshesUnderNode(Vector<Pair<aiNode*, aiMesh*>>& dest, aiNode* node)
{
    for (unsigned i = 0; i < node->mNumMeshes; ++i)
        dest.Push(MakePair(node, scene_->mMeshes[node->mMeshes[i]]));
}

unsigned GetMeshIndex(aiMesh* mesh)
{
    for (unsigned i = 0; i < scene_->mNumMeshes; ++i)
    {
        if (scene_->mMeshes[i] == mesh)
            return i;
    }
    return M_MAX_UNSIGNED;
}

unsigned GetBoneIndex(OutModel& model, const String& boneName)
{
    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        String bName(model.bones_[i]->mName.data);
        if (bName.StartsWith("mixamorig:"))
            bName = bName.Substring(10);
        if (boneName == bName)
            return i;
    }
    return M_MAX_UNSIGNED;
}

aiBone* GetMeshBone(OutModel& model, const String& boneName)
{
    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        aiMesh* mesh = model.meshes_[i];
        for (unsigned j = 0; j < mesh->mNumBones; ++j)
        {
            aiBone* bone = mesh->mBones[j];
            if (boneName == bone->mName.data)
                return bone;
        }
    }
    return nullptr;
}

Matrix3x4 GetOffsetMatrix(OutModel& model, const String& boneName)
{
    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        aiMesh* mesh = model.meshes_[i];
        aiNode* node = model.meshNodes_[i];
        for (unsigned j = 0; j < mesh->mNumBones; ++j)
        {
            aiBone* bone = mesh->mBones[j];
            if (boneName == bone->mName.data)
            {
                aiMatrix4x4 offset = bone->mOffsetMatrix;
                aiMatrix4x4 nodeDerivedInverse = GetMeshBakingTransform(node, model.rootNode_);
                nodeDerivedInverse.Inverse();
                offset *= nodeDerivedInverse;
                return ToMatrix3x4(offset);
            }
        }
    }

    // Fallback for rigid skinning for which actual offset matrix information doesn't exist
    for (unsigned i = 0; i < model.meshes_.Size(); ++i)
    {
        aiMesh* mesh = model.meshes_[i];
        aiNode* node = model.meshNodes_[i];
        if (!mesh->HasBones() && boneName == node->mName.data)
        {
            aiMatrix4x4 nodeDerivedInverse = GetMeshBakingTransform(node, model.rootNode_);
            nodeDerivedInverse.Inverse();
            return ToMatrix3x4(nodeDerivedInverse);
        }
    }

    return Matrix3x4::IDENTITY;
}

void GetBlendData(OutModel& model, aiMesh* mesh, aiNode* meshNode, Vector<i32>& boneMappings, Vector<Vector<unsigned char>>&
    blendIndices, Vector<Vector<float>>& blendWeights)
{
    blendIndices.Resize(mesh->mNumVertices);
    blendWeights.Resize(mesh->mNumVertices);
    boneMappings.Clear();

    // If model has more bones than can fit vertex shader parameters, write the per-geometry mappings
    if (model.bones_.Size() > maxBones_)
    {
        if (mesh->mNumBones > maxBones_)
        {
            ErrorExit(
                "Geometry (submesh) has over " + String(maxBones_) + " bone influences. Try splitting to more submeshes\n"
                "that each stay at " + String(maxBones_) + " bones or below."
            );
        }
        if (mesh->mNumBones > 0)
        {
            boneMappings.Resize(mesh->mNumBones);
            for (unsigned i = 0; i < mesh->mNumBones; ++i)
            {
                aiBone* bone = mesh->mBones[i];
                String boneName = FromAIString(bone->mName);
                unsigned globalIndex = GetBoneIndex(model, boneName);
                if (globalIndex == M_MAX_UNSIGNED)
                    ErrorExit("Bone " + boneName + " not found");
                boneMappings[i] = globalIndex;
                for (unsigned j = 0; j < bone->mNumWeights; ++j)
                {
                    unsigned vertex = bone->mWeights[j].mVertexId;
                    blendIndices[vertex].Push(i);
                    blendWeights[vertex].Push(bone->mWeights[j].mWeight);
                }
            }
        }
        else
        {
            // If mesh does not have skinning information, implement rigid skinning so that it stays compatible with AnimatedModel
            String boneName = FromAIString(meshNode->mName);
            unsigned globalIndex = GetBoneIndex(model, boneName);
            if (globalIndex == M_MAX_UNSIGNED)
                PrintLine("Warning: bone " + boneName + " not found, skipping rigid skinning");
            else
            {
                boneMappings.Push(globalIndex);
                for (unsigned i = 0; i < mesh->mNumVertices; ++i)
                {
                    blendIndices[i].Push(0);
                    blendWeights[i].Push(1.0f);
                }
            }
        }
    }
    else
    {
        if (mesh->mNumBones > 0)
        {
            for (unsigned i = 0; i < mesh->mNumBones; ++i)
            {
                aiBone* bone = mesh->mBones[i];
                String boneName = FromAIString(bone->mName);
                unsigned globalIndex = GetBoneIndex(model, boneName);
                if (globalIndex == M_MAX_UNSIGNED)
                    ErrorExit("Bone " + boneName + " not found");
                for (unsigned j = 0; j < bone->mNumWeights; ++j)
                {
                    unsigned vertex = bone->mWeights[j].mVertexId;
                    blendIndices[vertex].Push(globalIndex);
                    blendWeights[vertex].Push(bone->mWeights[j].mWeight);
                }
            }
        }
        else
        {
            String boneName = FromAIString(meshNode->mName);
            unsigned globalIndex = GetBoneIndex(model, boneName);
            if (globalIndex == M_MAX_UNSIGNED)
                PrintLine("Warning: bone " + boneName + " not found, skipping rigid skinning");
            else
            {
                for (unsigned i = 0; i < mesh->mNumVertices; ++i)
                {
                    blendIndices[i].Push(globalIndex);
                    blendWeights[i].Push(1.0f);
                }
            }
        }
    }

    // Normalize weights now if necessary, also remove too many influences
    for (unsigned i = 0; i < blendWeights.Size(); ++i)
    {
        if (blendWeights[i].Size() > 4)
        {
            PrintLine("Warning: more than 4 bone influences in vertex " + String(i));

            while (blendWeights[i].Size() > 4)
            {
                unsigned lowestIndex = 0;
                float lowest = M_INFINITY;
                for (unsigned j = 0; j < blendWeights[i].Size(); ++j)
                {
                    if (blendWeights[i][j] < lowest)
                    {
                        lowest = blendWeights[i][j];
                        lowestIndex = j;
                    }
                }
                blendWeights[i].Erase(lowestIndex);
                blendIndices[i].Erase(lowestIndex);
            }
        }

        float sum = 0.0f;
        for (unsigned j = 0; j < blendWeights[i].Size(); ++j)
            sum += blendWeights[i][j];
        if (sum != 1.0f && sum != 0.0f)
        {
            for (unsigned j = 0; j < blendWeights[i].Size(); ++j)
                blendWeights[i][j] /= sum;
        }
    }
}

String GetMeshMaterialName(aiMesh* mesh)
{
    aiMaterial* material = scene_->mMaterials[mesh->mMaterialIndex];
    aiString matNameStr;
    material->Get(AI_MATKEY_NAME, matNameStr);
    String matName = SanitateAssetName(FromAIString(matNameStr));
    if (matName.Trimmed().Empty())
        matName = GenerateMaterialName(material);

    if (!useSubdirs_)
        return matName + ".xml";

    // Compute material path relative to resource root, matching where materials are actually written.
    // outPath_ is the model's output directory (e.g. "bin/Data/Models/Animals/")
    // resourcePath_ is the resource root (e.g. "bin/Data/")
    // The material subdir relative to resource root = outPath_ - resourcePath_ + "Materials/"
    String relDir;
    if (outPath_.StartsWith(resourcePath_, false))
        relDir = outPath_.Substring(resourcePath_.Length());
    return relDir + "Materials/" + matName + ".xml";
}

String GenerateMaterialName(aiMaterial* material)
{
    for (unsigned i = 0; i < scene_->mNumMaterials; ++i)
    {
        if (scene_->mMaterials[i] == material)
            return inputName_ + "_Material" + String(i);
    }

    // Should not go here
    return String::EMPTY;
}

String GetMaterialTextureName(const String& nameIn)
{
    // Detect assimp embedded texture
    if (nameIn.Length() && nameIn[0] == '*')
        return GenerateTextureName(ToI32(nameIn.Substring(1)));
    else
        return (useSubdirs_ ? "Textures/" : "") + nameIn;
}

String GenerateTextureName(unsigned texIndex)
{
    if (texIndex < scene_->mNumTextures)
    {
        // If embedded texture contains encoded data, use the format hint for file extension. Else save RGBA8 data as PNG
        aiTexture* tex = scene_->mTextures[texIndex];
        if (!tex->mHeight)
            return (useSubdirs_ ? "Textures/" : "") + inputName_ + "_Texture" + String(texIndex) + "." + tex->achFormatHint;
        else
            return (useSubdirs_ ? "Textures/" : "") + inputName_ + "_Texture" + String(texIndex) + ".png";
    }

    // Should not go here
    return String::EMPTY;
}

unsigned GetNumValidFaces(aiMesh* mesh)
{
    unsigned ret = 0;

    for (unsigned j = 0; j < mesh->mNumFaces; ++j)
    {
        if (mesh->mFaces[j].mNumIndices == 3)
            ++ret;
    }

    return ret;
}

void WriteShortIndices(unsigned short*& dest, aiMesh* mesh, unsigned index, unsigned offset)
{
    if (mesh->mFaces[index].mNumIndices == 3)
    {
        *dest++ = mesh->mFaces[index].mIndices[0] + offset;
        *dest++ = mesh->mFaces[index].mIndices[1] + offset;
        *dest++ = mesh->mFaces[index].mIndices[2] + offset;
    }
}

void WriteLargeIndices(unsigned*& dest, aiMesh* mesh, unsigned index, unsigned offset)
{
    if (mesh->mFaces[index].mNumIndices == 3)
    {
        *dest++ = mesh->mFaces[index].mIndices[0] + offset;
        *dest++ = mesh->mFaces[index].mIndices[1] + offset;
        *dest++ = mesh->mFaces[index].mIndices[2] + offset;
    }
}

void WriteVertex(float*& dest, aiMesh* mesh, unsigned index, bool isSkinned, BoundingBox& box,
    const Matrix3x4& vertexTransform, const Matrix3& normalTransform, Vector<Vector<unsigned char>>& blendIndices,
    Vector<Vector<float>>& blendWeights)
{
    Vector3 vertex = vertexTransform * ToVector3(mesh->mVertices[index]);
    vertex *= importScale_;
    box.Merge(vertex);
    *dest++ = vertex.x_;
    *dest++ = vertex.y_;
    *dest++ = vertex.z_;

    if (mesh->HasNormals())
    {
        Vector3 normal = normalTransform * ToVector3(mesh->mNormals[index]);
        *dest++ = normal.x_;
        *dest++ = normal.y_;
        *dest++ = normal.z_;
    }

    for (unsigned i = 0; i < mesh->GetNumColorChannels() && i < MAX_CHANNELS; ++i)
    {
        *((unsigned*)dest) = Color(mesh->mColors[i][index].r, mesh->mColors[i][index].g, mesh->mColors[i][index].b,
            mesh->mColors[i][index].a).ToU32();
        ++dest;
    }

    for (unsigned i = 0; i < mesh->GetNumUVChannels() && i < MAX_CHANNELS; ++i)
    {
        Vector3 texCoord = ToVector3(mesh->mTextureCoords[i][index]);
        *dest++ = texCoord.x_;
        *dest++ = texCoord.y_;
    }

    if (mesh->HasTangentsAndBitangents())
    {
        Vector3 tangent = normalTransform * ToVector3(mesh->mTangents[index]);
        Vector3 normal = normalTransform * ToVector3(mesh->mNormals[index]);
        Vector3 bitangent = normalTransform * ToVector3(mesh->mBitangents[index]);
        // Check handedness
        float w = 1.0f;
        if ((tangent.CrossProduct(normal)).DotProduct(bitangent) < 0.5f)
            w = -1.0f;

        *dest++ = tangent.x_;
        *dest++ = tangent.y_;
        *dest++ = tangent.z_;
        *dest++ = w;
    }

    if (isSkinned)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            if (i < blendWeights[index].Size())
                *dest++ = blendWeights[index][i];
            else
                *dest++ = 0.0f;
        }

        auto* destBytes = (unsigned char*)dest;
        ++dest;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (i < blendIndices[index].Size())
                *destBytes++ = blendIndices[index][i];
            else
                *destBytes++ = 0;
        }
    }
}

Vector<VertexElement> GetVertexElements(aiMesh* mesh, bool isSkinned)
{
    Vector<VertexElement> ret;

    // Position must always be first and of type Vector3 for raycasts to work
    ret.Push(VertexElement(TYPE_VECTOR3, SEM_POSITION));

    if (mesh->HasNormals())
        ret.Push(VertexElement(TYPE_VECTOR3, SEM_NORMAL));

    for (unsigned i = 0; i < mesh->GetNumColorChannels() && i < MAX_CHANNELS; ++i)
        ret.Push(VertexElement(TYPE_UBYTE4_NORM, SEM_COLOR, i));

    /// \todo Assimp mesh structure can specify 3D UV-coords. How to determine the difference? For now always treated as 2D.
    for (unsigned i = 0; i < mesh->GetNumUVChannels() && i < MAX_CHANNELS; ++i)
        ret.Push(VertexElement(TYPE_VECTOR2, SEM_TEXCOORD, i));

    if (mesh->HasTangentsAndBitangents())
        ret.Push(VertexElement(TYPE_VECTOR4, SEM_TANGENT));

    if (isSkinned)
    {
        ret.Push(VertexElement(TYPE_VECTOR4, SEM_BLENDWEIGHTS));
        ret.Push(VertexElement(TYPE_UBYTE4, SEM_BLENDINDICES));
    }

    return ret;
}

aiNode* GetNode(const String& name, aiNode* rootNode, bool caseSensitive)
{
    if (!rootNode)
        return nullptr;
    if (!name.Compare(rootNode->mName.data, caseSensitive))
        return rootNode;
    for (unsigned i = 0; i < rootNode->mNumChildren; ++i)
    {
        aiNode* found = GetNode(name, rootNode->mChildren[i], caseSensitive);
        if (found)
            return found;
    }
    return nullptr;
}

aiMatrix4x4 GetDerivedTransform(aiNode* node, aiNode* rootNode, bool rootInclusive)
{
    return GetDerivedTransform(node->mTransformation, node, rootNode, rootInclusive);
}

aiMatrix4x4 GetDerivedTransform(aiMatrix4x4 transform, aiNode* node, aiNode* rootNode, bool rootInclusive)
{
    // If basenode is defined, go only up to it in the parent chain
    while (node && node != rootNode)
    {
        node = node->mParent;
        if (!rootInclusive && node == rootNode)
            break;
        if (node)
            transform = node->mTransformation * transform;
    }
    return transform;
}

aiMatrix4x4 GetMeshBakingTransform(aiNode* meshNode, aiNode* modelRootNode)
{
    if (meshNode == modelRootNode)
        return {};
    else
        return GetDerivedTransform(meshNode, modelRootNode);
}

void GetPosRotScale(const aiMatrix4x4& transform, Vector3& pos, Quaternion& rot, Vector3& scale)
{
    aiVector3D aiPos;
    aiQuaternion aiRot;
    aiVector3D aiScale;
    transform.Decompose(aiScale, aiRot, aiPos);
    pos = ToVector3(aiPos);
    rot = ToQuaternion(aiRot);
    scale = ToVector3(aiScale);
}


String FromAIString(const aiString& str)
{
    return String(str.data);
}

Vector3 ToVector3(const aiVector3D& vec)
{
    return Vector3(vec.x, vec.y, vec.z);
}

Vector2 ToVector2(const aiVector2D& vec)
{
    return Vector2(vec.x, vec.y);
}

Quaternion ToQuaternion(const aiQuaternion& quat)
{
    return Quaternion(quat.w, quat.x, quat.y, quat.z);
}

Matrix3x4 ToMatrix3x4(const aiMatrix4x4& mat)
{
    Matrix3x4 ret;
    memcpy(&ret.m00_, &mat.a1, sizeof(Matrix3x4));
    return ret;
}

aiMatrix4x4 ToAIMatrix4x4(const Matrix3x4& mat)
{
    aiMatrix4x4 ret;
    memcpy(&ret.a1, &mat.m00_, sizeof(Matrix3x4));
    return ret;
}

String SanitateAssetName(const String& name)
{
    String fixedName = name;
    fixedName.Replace("<", "");
    fixedName.Replace(">", "");
    fixedName.Replace("?", "");
    fixedName.Replace("*", "");
    fixedName.Replace(":", "");
    fixedName.Replace("\"", "");
    fixedName.Replace("/", "");
    fixedName.Replace("\\", "");
    fixedName.Replace("|", "");
    // Strip line endings — CR (\r), LF (\n), CRLF (\r\n)
    fixedName.Replace("\r\n", "");
    fixedName.Replace("\r", "");
    fixedName.Replace("\n", "");

    return fixedName;
}

unsigned GetPivotlessBoneIndex(OutModel& model, const String& boneName)
{
    for (unsigned i = 0; i < model.pivotlessBones_.Size(); ++i)
    {
        if (boneName == model.pivotlessBones_[i]->mName.data)
            return i;
    }
    return M_MAX_UNSIGNED;
}

void FillChainTransforms(OutModel &model, aiMatrix4x4 *chain, const String& mainBoneName)
{
    for (unsigned j = 0; j < TransformationComp_MAXIMUM; ++j)
    {
        String transfBoneName = mainBoneName + "_$AssimpFbx$_" + String(transformSuffix[j]);

        for (unsigned k = 0; k < model.bones_.Size(); ++k)
        {
            String boneName = String(model.bones_[k]->mName.data);

            if (boneName == transfBoneName)
            {
                chain[j] = model.bones_[k]->mTransformation;
                break;
            }
        }
    }
}

void ExpandAnimatedChannelKeys(aiAnimation* anim, unsigned mainChannel, const int *channelIndices)
{
    aiNodeAnim* channel = anim->mChannels[mainChannel];
    unsigned int poskeyFrames = channel->mNumPositionKeys;
    unsigned int rotkeyFrames = channel->mNumRotationKeys;
    unsigned int scalekeyFrames = channel->mNumScalingKeys;

    // Get max key frames
    for (unsigned i = 0; i < TransformationComp_MAXIMUM; ++i)
    {
        if (channelIndices[i] != -1 && channelIndices[i] != mainChannel)
        {
            aiNodeAnim* channel2 = anim->mChannels[channelIndices[i]];

            if (channel2->mNumPositionKeys > poskeyFrames)
                poskeyFrames = channel2->mNumPositionKeys;
            if (channel2->mNumRotationKeys > rotkeyFrames)
                rotkeyFrames = channel2->mNumRotationKeys;
            if (channel2->mNumScalingKeys  > scalekeyFrames)
                scalekeyFrames = channel2->mNumScalingKeys;
        }
    }

    // Resize and init vector key array
    if (poskeyFrames > channel->mNumPositionKeys)
    {
        auto* newKeys  = new aiVectorKey[poskeyFrames];
        for (unsigned i = 0; i < poskeyFrames; ++i)
        {
            if (i < channel->mNumPositionKeys )
                newKeys[i] = aiVectorKey(channel->mPositionKeys[i].mTime, channel->mPositionKeys[i].mValue);
            else
                newKeys[i].mValue = aiVector3D(0.0f, 0.0f, 0.0f);
        }
        delete[] channel->mPositionKeys;
        channel->mPositionKeys = newKeys;
        channel->mNumPositionKeys = poskeyFrames;
    }
    if (rotkeyFrames > channel->mNumRotationKeys)
    {
        auto* newKeys  = new aiQuatKey[rotkeyFrames];
        for (unsigned i = 0; i < rotkeyFrames; ++i)
        {
            if (i < channel->mNumRotationKeys)
                newKeys[i] = aiQuatKey(channel->mRotationKeys[i].mTime, channel->mRotationKeys[i].mValue);
            else
                newKeys[i].mValue = aiQuaternion();
        }
        delete[] channel->mRotationKeys;
        channel->mRotationKeys = newKeys;
        channel->mNumRotationKeys = rotkeyFrames;
    }
    if (scalekeyFrames > channel->mNumScalingKeys)
    {
        auto* newKeys  = new aiVectorKey[scalekeyFrames];
        for (unsigned i = 0; i < scalekeyFrames; ++i)
        {
            if ( i < channel->mNumScalingKeys)
                newKeys[i] = aiVectorKey(channel->mScalingKeys[i].mTime, channel->mScalingKeys[i].mValue);
            else
                newKeys[i].mValue = aiVector3D(1.0f, 1.0f, 1.0f);
        }
        delete[] channel->mScalingKeys;
        channel->mScalingKeys = newKeys;
        channel->mNumScalingKeys = scalekeyFrames;
    }
}

void InitAnimatedChainTransformIndices(aiAnimation* anim, unsigned mainChannel, const String& mainBoneName, int *channelIndices)
{
    int numTransforms = 0;

    for (unsigned j = 0; j < TransformationComp_MAXIMUM; ++j)
    {
        String transfBoneName = mainBoneName + "_$AssimpFbx$_" + String(transformSuffix[j]);
        channelIndices[j] = -1;

        for (unsigned k = 0; k < anim->mNumChannels; ++k)
        {
            aiNodeAnim* channel = anim->mChannels[k];
            String channelName = FromAIString(channel->mNodeName);

            if (channelName == transfBoneName)
            {
                ++numTransforms;
                channelIndices[j] = k;
                break;
            }
        }
    }

    // resize animated channel key size
    if (numTransforms > 1)
        ExpandAnimatedChannelKeys(anim, mainChannel, channelIndices);
}

void CreatePivotlessFbxBoneStruct(OutModel &model)
{
    // Init
    model.pivotlessBones_.Clear();
    aiMatrix4x4 chains[TransformationComp_MAXIMUM];

    for (unsigned i = 0; i < model.bones_.Size(); ++i)
    {
        String mainBoneName = String(model.bones_[i]->mName.data);

        // Skip $fbx nodes
        if (mainBoneName.Find("$AssimpFbx$") != String::NPOS)
            continue;

        std::fill_n(chains, static_cast<unsigned int>(TransformationComp_MAXIMUM), aiMatrix4x4());
        FillChainTransforms(model, &chains[0], mainBoneName);

        // Calculate chained transform
        aiMatrix4x4 finalTransform;
        for (const auto& chain : chains)
            finalTransform = finalTransform * chain;

        // New bone node
        auto*pnode = new aiNode;
        pnode->mName = model.bones_[i]->mName;
        pnode->mTransformation = finalTransform * model.bones_[i]->mTransformation;

        model.pivotlessBones_.Push(pnode);
    }
}

void ExtrapolatePivotlessAnimation(OutModel* model)
{
    if (suppressFbxPivotNodes_ && model)
    {
        PrintLine("Suppressing $fbx nodes");

        // Construct new bone structure from suppressed $fbx pivot nodes
        CreatePivotlessFbxBoneStruct(*model);

        // Extrapolate anim
        const Vector<aiAnimation *> &animations = model->animations_;
        for (unsigned i = 0; i < animations.Size(); ++i)
        {
            aiAnimation* anim = animations[i];
            Vector<String> mainBoneCompleteList;
            mainBoneCompleteList.Clear();

            for (unsigned j = 0; j < anim->mNumChannels; ++j)
            {
                aiNodeAnim* channel = anim->mChannels[j];
                String channelName = FromAIString(channel->mNodeName);
                i32 pos = channelName.Find("_$AssimpFbx$");

                if (pos != String::NPOS)
                {
                    // Every first $fbx animation channel for a bone will consolidate other $fbx animation to a single channel
                    // skip subsequent $fbx animation channel for the same bone
                    String mainBoneName = channelName.Substring(0, pos);

                    if (mainBoneCompleteList.Find(mainBoneName) != mainBoneCompleteList.End())
                        continue;

                    mainBoneCompleteList.Push(mainBoneName);
                    unsigned boneIdx = GetBoneIndex(*model, mainBoneName);

                    // This condition exists if a geometry, not a bone, has a key animation
                    if (boneIdx == M_MAX_UNSIGNED)
                        continue;

                    // Init chain indices and fill transforms
                    aiMatrix4x4 mainboneTransform = model->bones_[boneIdx]->mTransformation;
                    aiMatrix4x4 chain[TransformationComp_MAXIMUM];
                    int channelIndices[TransformationComp_MAXIMUM];

                    InitAnimatedChainTransformIndices(anim, j, mainBoneName, &channelIndices[0]);
                    std::fill_n(chain, static_cast<unsigned int>(TransformationComp_MAXIMUM), aiMatrix4x4());
                    FillChainTransforms(*model, &chain[0], mainBoneName);

                    unsigned keyFrames = channel->mNumPositionKeys;
                    if (channel->mNumRotationKeys > keyFrames)
                        keyFrames = channel->mNumRotationKeys;
                    if (channel->mNumScalingKeys  > keyFrames)
                        keyFrames = channel->mNumScalingKeys;

                    for (unsigned k = 0; k < keyFrames; ++k)
                    {
                        double frameTime = 0.0;
                        aiMatrix4x4 finalTransform;

                        // Chain transform animated values
                        for (unsigned l = 0; l < TransformationComp_MAXIMUM; ++l)
                        {
                            // It's either the chain transform or animation channel transform
                            if (channelIndices[l] != -1)
                            {
                                aiMatrix4x4 animtform, tempMat;
                                aiNodeAnim* animchannel = anim->mChannels[channelIndices[l]];

                                if (k < animchannel->mNumPositionKeys)
                                {
                                    aiMatrix4x4::Translation(animchannel->mPositionKeys[k].mValue, tempMat);
                                    animtform = animtform * tempMat;
                                    frameTime = Max(animchannel->mPositionKeys[k].mTime, frameTime);
                                }
                                if (k < animchannel->mNumRotationKeys)
                                {
                                    tempMat = aiMatrix4x4(animchannel->mRotationKeys[k].mValue.GetMatrix());
                                    animtform = animtform * tempMat;
                                    frameTime = Max(animchannel->mRotationKeys[k].mTime, frameTime);
                                }
                                if (k < animchannel->mNumScalingKeys)
                                {
                                    aiMatrix4x4::Scaling(animchannel->mScalingKeys[k].mValue, tempMat);
                                    animtform = animtform * tempMat;
                                    frameTime = Max(animchannel->mScalingKeys[k].mTime, frameTime);
                                }

                                finalTransform = finalTransform * animtform;
                            }
                            else
                                finalTransform = finalTransform * chain[l];
                        }

                        aiVector3D animPos, animScale;
                        aiQuaternion animRot;
                        finalTransform = finalTransform * mainboneTransform;
                        finalTransform.Decompose(animScale, animRot, animPos);

                        // New values
                        if (k < channel->mNumPositionKeys)
                        {
                            channel->mPositionKeys[k].mValue = animPos;
                            channel->mPositionKeys[k].mTime = frameTime;
                        }

                        if (k < channel->mNumRotationKeys)
                        {
                            channel->mRotationKeys[k].mValue = animRot;
                            channel->mRotationKeys[k].mTime = frameTime;
                        }

                        if (k < channel->mNumScalingKeys)
                        {
                            channel->mScalingKeys[k].mValue = animScale;
                            channel->mScalingKeys[k].mTime = frameTime;
                        }
                    }
                }
            }
        }
    }
}

void CollectSceneNodesAsBones(OutModel &model, aiNode* rootNode)
{
    if (!rootNode)
        return;

    model.bones_.Push(rootNode);

    for (unsigned i = 0; i < rootNode->mNumChildren; ++i)
    {
        CollectSceneNodesAsBones(model, rootNode->mChildren[i]);
    }
}

// ============================================================================
// MDL → FBX Export
// Reads native Urho3D .mdl binary directly (no GPU required), builds an
// aiScene, and calls Assimp's FBX exporter.
// ============================================================================

struct ExportBone
{
    String name;
    int parentIndex;
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;
    float offsetMatrix[12]; // Matrix3x4 raw data
};

struct ExportGeomRef
{
    unsigned vbIndex;
    unsigned ibIndex;
    unsigned indexStart;
    unsigned indexCount;
    unsigned primitiveType;
    float lodDistance;
    Vector<unsigned> boneMappings;
};

void ExportMdlToFBX(const String& inFile, const String& outFile)
{
    PrintLine("Exporting " + inFile + " -> " + outFile);

    // --- Read MDL binary ---
    SharedPtr<File> file(new File(context_, inFile));
    if (!file->IsOpen())
        ErrorExit("Could not open " + inFile);

    String fileID = file->ReadFileID();
    if (fileID != "UMDL" && fileID != "UMD2")
        ErrorExit(inFile + " is not a valid Urho3D model file (got " + fileID + ")");

    bool isUMD2 = (fileID == "UMD2");

    // -- Vertex buffers --
    unsigned numVBs = file->ReadU32();
    struct VBData {
        unsigned vertexCount;
        unsigned vertexSize; // bytes per vertex
        Vector<VertexElement> elements;
        SharedArrayPtr<unsigned char> data;
    };
    Vector<VBData> vertexBuffers(numVBs);

    for (unsigned i = 0; i < numVBs; ++i)
    {
        VBData& vb = vertexBuffers[i];
        vb.vertexCount = file->ReadU32();

        if (isUMD2)
        {
            unsigned numElements = file->ReadU32();
            for (unsigned j = 0; j < numElements; ++j)
            {
                unsigned desc = file->ReadU32();
                VertexElement elem;
                elem.type_ = (VertexElementType)(desc & 0xFF);
                elem.semantic_ = (VertexElementSemantic)((desc >> 8) & 0xFF);
                elem.index_ = (i8)((desc >> 16) & 0xFF);
                elem.perInstance_ = false;
                elem.offset_ = 0;
                vb.elements.Push(elem);
            }
            VertexBuffer::UpdateOffsets(vb.elements);
            vb.vertexSize = VertexBuffer::GetVertexSize(vb.elements);
        }
        else
        {
            // UMDL uses element mask
            unsigned elementMask = file->ReadU32();
            vb.elements = VertexBuffer::GetElements(VertexElements(elementMask));
            VertexBuffer::UpdateOffsets(vb.elements);
            vb.vertexSize = VertexBuffer::GetVertexSize(vb.elements);
        }

        // Morph range — always present in both UMDL and UMD2
        file->ReadU32(); // morphRangeStart
        file->ReadU32(); // morphRangeCount

        unsigned dataSize = vb.vertexCount * vb.vertexSize;
        vb.data = new unsigned char[dataSize];
        file->Read(vb.data.Get(), dataSize);
    }

    // -- Index buffers --
    unsigned numIBs = file->ReadU32();
    struct IBData {
        unsigned indexCount;
        unsigned indexSize; // 2 or 4
        SharedArrayPtr<unsigned char> data;
    };
    Vector<IBData> indexBuffers(numIBs);

    for (unsigned i = 0; i < numIBs; ++i)
    {
        IBData& ib = indexBuffers[i];
        ib.indexCount = file->ReadU32();
        ib.indexSize = file->ReadU32();
        unsigned dataSize = ib.indexCount * ib.indexSize;
        ib.data = new unsigned char[dataSize];
        file->Read(ib.data.Get(), dataSize);
    }

    // -- Geometries --
    unsigned numGeometries = file->ReadU32();
    Vector<ExportGeomRef> geometries;

    for (unsigned i = 0; i < numGeometries; ++i)
    {
        // Bone mappings for this geometry
        unsigned numBoneMappings = file->ReadU32();
        Vector<unsigned> boneMappings(numBoneMappings);
        for (unsigned j = 0; j < numBoneMappings; ++j)
            boneMappings[j] = file->ReadU32();

        // LOD levels
        unsigned numLods = file->ReadU32();
        for (unsigned j = 0; j < numLods; ++j)
        {
            ExportGeomRef geom;
            geom.lodDistance = file->ReadFloat();
            geom.primitiveType = file->ReadU32();
            geom.vbIndex = file->ReadU32();
            geom.ibIndex = file->ReadU32();
            geom.indexStart = file->ReadU32();
            geom.indexCount = file->ReadU32();
            geom.boneMappings = boneMappings;

            // Only export LOD 0
            if (j == 0)
                geometries.Push(geom);
        }
    }

    // -- Morphs (skip for now) --
    unsigned numMorphs = file->ReadU32();
    for (unsigned i = 0; i < numMorphs; ++i)
    {
        file->ReadString(); // name
        unsigned numMorphBuffers = file->ReadU32();
        for (unsigned j = 0; j < numMorphBuffers; ++j)
        {
            file->ReadU32(); // buffer index
            unsigned morphElementMask = file->ReadU32();
            unsigned morphVertexCount = file->ReadU32();

            // Calculate morph vertex size
            unsigned morphVertexSize = sizeof(unsigned); // vertex index
            if (morphElementMask & 1) morphVertexSize += sizeof(float) * 3; // position
            if (morphElementMask & 2) morphVertexSize += sizeof(float) * 3; // normal
            if (morphElementMask & 8) morphVertexSize += sizeof(float) * 3; // tangent

            file->Seek(file->GetPosition() + morphVertexCount * morphVertexSize);
        }
    }

    // -- Skeleton --
    Vector<ExportBone> bones;
    unsigned numBones = file->ReadI32();
    bones.Resize(numBones);

    for (unsigned i = 0; i < numBones; ++i)
    {
        ExportBone& bone = bones[i];
        bone.name = file->ReadString();
        bone.parentIndex = file->ReadI32();
        bone.position = file->ReadVector3();
        bone.rotation = file->ReadQuaternion();
        bone.scale = file->ReadVector3();
        file->Read(bone.offsetMatrix, sizeof(float) * 12);

        // Collision info
        unsigned char collisionMask = file->ReadU8();
        if (collisionMask & 1) file->ReadFloat(); // sphere radius
        if (collisionMask & 2)
        {
            file->ReadVector3(); // bbox min
            file->ReadVector3(); // bbox max
        }
    }

    PrintLine("  " + String(numVBs) + " vertex buffer(s), " + String(numIBs) + " index buffer(s)");
    PrintLine("  " + String(geometries.Size()) + " geometries, " + String(numBones) + " bones");

    // --- Build aiScene ---
    aiScene* scene = new aiScene();
    scene->mFlags = 0;
    scene->mNumMeshes = geometries.Size();
    scene->mMeshes = new aiMesh*[scene->mNumMeshes];

    // Build node hierarchy from bones
    // Create a map from bone index to aiNode
    Vector<aiNode*> boneNodes(numBones);

    // Root node
    scene->mRootNode = new aiNode("RootNode");

    if (numBones > 0)
    {
        // Create aiNode for each bone
        for (unsigned i = 0; i < numBones; ++i)
        {
            boneNodes[i] = new aiNode(bones[i].name.CString());

            // Set transform from bone's initial position/rotation/scale
            aiMatrix4x4 mat;
            // Compose TRS
            Vector3& p = bones[i].position;
            Quaternion& r = bones[i].rotation;
            Vector3& s = bones[i].scale;
            Matrix3x4 trs(p, r, s);
            mat = aiMatrix4x4(
                trs.m00_, trs.m01_, trs.m02_, trs.m03_,
                trs.m10_, trs.m11_, trs.m12_, trs.m13_,
                trs.m20_, trs.m21_, trs.m22_, trs.m23_,
                0.0f, 0.0f, 0.0f, 1.0f
            );
            boneNodes[i]->mTransformation = mat;
        }

        // Build parent-child relationships
        for (unsigned i = 0; i < numBones; ++i)
        {
            int parentIdx = bones[i].parentIndex;
            if (parentIdx == (int)i || parentIdx < 0)
            {
                // Root bone — parent to scene root
                boneNodes[i]->mParent = scene->mRootNode;
            }
            else
            {
                boneNodes[i]->mParent = boneNodes[parentIdx];
            }
        }

        // Count children for each node
        Vector<unsigned> childCounts(numBones + 1, 0); // +1 for root
        for (unsigned i = 0; i < numBones; ++i)
        {
            int parentIdx = bones[i].parentIndex;
            if (parentIdx == (int)i || parentIdx < 0)
                childCounts[numBones]++; // root's children
            else
                childCounts[parentIdx]++;
        }

        // Allocate children arrays
        scene->mRootNode->mNumChildren = childCounts[numBones];
        scene->mRootNode->mChildren = new aiNode*[childCounts[numBones]];
        for (unsigned i = 0; i < numBones; ++i)
        {
            boneNodes[i]->mNumChildren = childCounts[i];
            if (childCounts[i] > 0)
                boneNodes[i]->mChildren = new aiNode*[childCounts[i]];
            else
                boneNodes[i]->mChildren = nullptr;
        }

        // Fill children arrays
        Vector<unsigned> childIndices(numBones + 1, 0);
        for (unsigned i = 0; i < numBones; ++i)
        {
            int parentIdx = bones[i].parentIndex;
            if (parentIdx == (int)i || parentIdx < 0)
            {
                scene->mRootNode->mChildren[childIndices[numBones]++] = boneNodes[i];
            }
            else
            {
                boneNodes[parentIdx]->mChildren[childIndices[parentIdx]++] = boneNodes[i];
            }
        }
    }
    else
    {
        scene->mRootNode->mNumChildren = 0;
        scene->mRootNode->mChildren = nullptr;
    }

    // Attach mesh references to root node
    scene->mRootNode->mNumMeshes = scene->mNumMeshes;
    scene->mRootNode->mMeshes = new unsigned[scene->mNumMeshes];
    for (unsigned i = 0; i < scene->mNumMeshes; ++i)
        scene->mRootNode->mMeshes[i] = i;

    // Build meshes
    for (unsigned g = 0; g < geometries.Size(); ++g)
    {
        ExportGeomRef& geomRef = geometries[g];
        VBData& vb = vertexBuffers[geomRef.vbIndex];
        IBData& ib = indexBuffers[geomRef.ibIndex];

        aiMesh* mesh = new aiMesh();
        scene->mMeshes[g] = mesh;
        mesh->mMaterialIndex = g; // one material per geometry
        mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

        // Figure out which vertices are actually used by this geometry's indices
        unsigned indexStart = geomRef.indexStart;
        unsigned indexCount = geomRef.indexCount;

        // Find min/max vertex index to determine vertex range
        unsigned minVertex = UINT_MAX, maxVertex = 0;
        for (unsigned i = 0; i < indexCount; ++i)
        {
            unsigned idx;
            if (ib.indexSize == 2)
                idx = ((unsigned short*)ib.data.Get())[indexStart + i];
            else
                idx = ((unsigned*)ib.data.Get())[indexStart + i];
            if (idx < minVertex) minVertex = idx;
            if (idx > maxVertex) maxVertex = idx;
        }

        unsigned vertexCount = maxVertex - minVertex + 1;
        mesh->mNumVertices = vertexCount;

        // Find element offsets
        int posOffset = -1, normOffset = -1, uvOffset = -1, tangOffset = -1;
        int blendWeightOffset = -1, blendIndexOffset = -1;
        for (unsigned e = 0; e < vb.elements.Size(); ++e)
        {
            const VertexElement& elem = vb.elements[e];
            switch (elem.semantic_)
            {
            case SEM_POSITION: posOffset = elem.offset_; break;
            case SEM_NORMAL: normOffset = elem.offset_; break;
            case SEM_TEXCOORD: if (elem.index_ == 0) uvOffset = elem.offset_; break;
            case SEM_TANGENT: tangOffset = elem.offset_; break;
            case SEM_BLENDWEIGHTS: blendWeightOffset = elem.offset_; break;
            case SEM_BLENDINDICES: blendIndexOffset = elem.offset_; break;
            default: break;
            }
        }

        // Allocate and copy vertex attributes
        if (posOffset >= 0)
        {
            mesh->mVertices = new aiVector3D[vertexCount];
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                float* src = (float*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + posOffset);
                mesh->mVertices[v] = aiVector3D(src[0] * importScale_, src[1] * importScale_, src[2] * importScale_);
            }
        }

        if (normOffset >= 0)
        {
            mesh->mNormals = new aiVector3D[vertexCount];
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                float* src = (float*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + normOffset);
                mesh->mNormals[v] = aiVector3D(src[0], src[1], src[2]);
            }
        }

        if (uvOffset >= 0)
        {
            mesh->mTextureCoords[0] = new aiVector3D[vertexCount];
            mesh->mNumUVComponents[0] = 2;
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                float* src = (float*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + uvOffset);
                mesh->mTextureCoords[0][v] = aiVector3D(src[0], src[1], 0.0f);
            }
        }

        if (tangOffset >= 0)
        {
            mesh->mTangents = new aiVector3D[vertexCount];
            mesh->mBitangents = new aiVector3D[vertexCount];
            for (unsigned v = 0; v < vertexCount; ++v)
            {
                float* src = (float*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + tangOffset);
                mesh->mTangents[v] = aiVector3D(src[0], src[1], src[2]);
                // Compute bitangent from normal cross tangent
                if (normOffset >= 0)
                {
                    float* nsrc = (float*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + normOffset);
                    aiVector3D n(nsrc[0], nsrc[1], nsrc[2]);
                    aiVector3D t(src[0], src[1], src[2]);
                    aiVector3D b = n ^ t; // cross product
                    float w = src[3]; // tangent w component (handedness)
                    mesh->mBitangents[v] = b * w;
                }
            }
        }

        // Copy faces (triangles)
        mesh->mNumFaces = indexCount / 3;
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned f = 0; f < mesh->mNumFaces; ++f)
        {
            mesh->mFaces[f].mNumIndices = 3;
            mesh->mFaces[f].mIndices = new unsigned[3];
            for (unsigned k = 0; k < 3; ++k)
            {
                unsigned idx;
                if (ib.indexSize == 2)
                    idx = ((unsigned short*)ib.data.Get())[indexStart + f * 3 + k];
                else
                    idx = ((unsigned*)ib.data.Get())[indexStart + f * 3 + k];
                mesh->mFaces[f].mIndices[k] = idx - minVertex; // rebase to 0
            }
        }

        // Skinning data — build aiBone array
        if (blendWeightOffset >= 0 && blendIndexOffset >= 0 && numBones > 0)
        {
            // Collect per-bone vertex weights
            // boneMappings maps local bone index → global bone index
            unsigned numLocalBones = geomRef.boneMappings.Size();
            if (numLocalBones == 0)
                numLocalBones = numBones; // no mapping means direct indexing

            // First pass: collect weights per bone
            Vector<Vector<aiVertexWeight>> boneWeights(numBones);

            for (unsigned v = 0; v < vertexCount; ++v)
            {
                unsigned char* indices = (unsigned char*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + blendIndexOffset);
                float* weights = (float*)(vb.data.Get() + (minVertex + v) * vb.vertexSize + blendWeightOffset);

                for (unsigned w = 0; w < 4; ++w)
                {
                    if (weights[w] > 0.0f)
                    {
                        unsigned localBoneIdx = indices[w];
                        unsigned globalBoneIdx;
                        if (geomRef.boneMappings.Size() > 0 && localBoneIdx < geomRef.boneMappings.Size())
                            globalBoneIdx = geomRef.boneMappings[localBoneIdx];
                        else
                            globalBoneIdx = localBoneIdx;

                        if (globalBoneIdx < numBones)
                        {
                            aiVertexWeight vw;
                            vw.mVertexId = v;
                            vw.mWeight = weights[w];
                            boneWeights[globalBoneIdx].Push(vw);
                        }
                    }
                }
            }

            // Count how many bones actually have weights
            unsigned numActiveBones = 0;
            for (unsigned b = 0; b < numBones; ++b)
            {
                if (boneWeights[b].Size() > 0)
                    numActiveBones++;
            }

            mesh->mNumBones = numActiveBones;
            mesh->mBones = new aiBone*[numActiveBones];

            unsigned boneIdx = 0;
            for (unsigned b = 0; b < numBones; ++b)
            {
                if (boneWeights[b].Empty())
                    continue;

                aiBone* aiBonePtr = new aiBone();
                mesh->mBones[boneIdx++] = aiBonePtr;
                aiBonePtr->mName = aiString(bones[b].name.CString());

                // Set offset matrix (inverse bind pose) from stored data
                float* m = bones[b].offsetMatrix;
                aiBonePtr->mOffsetMatrix = aiMatrix4x4(
                    m[0], m[1], m[2], m[3],
                    m[4], m[5], m[6], m[7],
                    m[8], m[9], m[10], m[11],
                    0.0f, 0.0f, 0.0f, 1.0f
                );

                // Apply scale to offset matrix translation
                if (importScale_ != 1.0f)
                {
                    aiBonePtr->mOffsetMatrix.a4 *= importScale_;
                    aiBonePtr->mOffsetMatrix.b4 *= importScale_;
                    aiBonePtr->mOffsetMatrix.c4 *= importScale_;
                }

                aiBonePtr->mNumWeights = boneWeights[b].Size();
                aiBonePtr->mWeights = new aiVertexWeight[aiBonePtr->mNumWeights];
                for (unsigned w = 0; w < aiBonePtr->mNumWeights; ++w)
                    aiBonePtr->mWeights[w] = boneWeights[b][w];
            }
        }
    }

    // Phase 2 fidelity: read the .txt material list sidecar and parse each
    // referenced material XML, so the exported FBX carries actual diffuse
    // textures + colors instead of empty placeholders. Falls back to empty
    // named materials if the sidecar is missing or unreadable, so models
    // with no material list still export.
    scene->mNumMaterials = Max((unsigned)geometries.Size(), 1u);
    scene->mMaterials = new aiMaterial*[scene->mNumMaterials];

    // Resolve Data/ root from the input path so material XMLs (which store
    // paths relative to Data/) can be located on disk.
    String dataRoot;
    {
        unsigned dataIdx = inFile.Find("/Data/");
        if (dataIdx == String::NPOS)
            dataIdx = inFile.Find("\\Data\\");
        if (dataIdx != String::NPOS)
            dataRoot = inFile.Substring(0, dataIdx + 6);  // includes trailing slash
    }

    // Read the .txt sidecar (one material XML path per line, in geometry order)
    Vector<String> materialPaths;
    {
        String txtPath = ReplaceExtension(inFile, ".txt");
        auto* fs = context_->GetSubsystem<FileSystem>();
        if (fs && fs->FileExists(txtPath))
        {
            SharedPtr<File> txt(new File(context_, txtPath, FILE_READ));
            if (txt->IsOpen())
            {
                while (!txt->IsEof())
                {
                    String line = txt->ReadLine().Trimmed();
                    if (!line.Empty())
                        materialPaths.Push(line);
                }
            }
        }
    }

    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
    {
        aiMaterial* aiMat = new aiMaterial();
        scene->mMaterials[i] = aiMat;

        bool loadedReal = false;
        if (i < materialPaths.Size() && !dataRoot.Empty())
        {
            String matFilePath = dataRoot + materialPaths[i];
            auto* fs = context_->GetSubsystem<FileSystem>();
            if (fs && fs->FileExists(matFilePath))
            {
                SharedPtr<XMLFile> xml(new XMLFile(context_));
                SharedPtr<File> matFile(new File(context_, matFilePath, FILE_READ));
                if (matFile->IsOpen() && xml->Load(*matFile))
                {
                    XMLElement matElem = xml->GetRoot("material");
                    if (matElem)
                    {
                        String matName = GetFileName(materialPaths[i]);
                        aiString aiMatName(matName.CString());
                        aiMat->AddProperty(&aiMatName, AI_MATKEY_NAME);

                        // Texture units: diffuse / normal / specular / emissive
                        for (XMLElement tex = matElem.GetChild("texture"); tex;
                             tex = tex.GetNext("texture"))
                        {
                            String unit = tex.GetAttribute("unit").ToLower();
                            String name = tex.GetAttribute("name");
                            if (name.Empty())
                                continue;
                            aiString aiTexPath(name.CString());
                            if (unit == "diffuse" || unit == "0")
                                aiMat->AddProperty(&aiTexPath, AI_MATKEY_TEXTURE_DIFFUSE(0));
                            else if (unit == "normal" || unit == "1")
                                aiMat->AddProperty(&aiTexPath, AI_MATKEY_TEXTURE_NORMALS(0));
                            else if (unit == "specular" || unit == "2")
                                aiMat->AddProperty(&aiTexPath, AI_MATKEY_TEXTURE_SPECULAR(0));
                            else if (unit == "emissive" || unit == "3")
                                aiMat->AddProperty(&aiTexPath, AI_MATKEY_TEXTURE_EMISSIVE(0));
                        }

                        // Diffuse / specular / emissive colors from <parameter> children
                        for (XMLElement param = matElem.GetChild("parameter"); param;
                             param = param.GetNext("parameter"))
                        {
                            String pname = param.GetAttribute("name");
                            String pval = param.GetAttribute("value");
                            if (pval.Empty())
                                continue;
                            Vector4 c = ToVector4(pval);
                            aiColor4D aiCol((float)c.x_, (float)c.y_, (float)c.z_, (float)c.w_);
                            if (pname == "MatDiffColor")
                                aiMat->AddProperty(&aiCol, 1, AI_MATKEY_COLOR_DIFFUSE);
                            else if (pname == "MatSpecColor")
                                aiMat->AddProperty(&aiCol, 1, AI_MATKEY_COLOR_SPECULAR);
                            else if (pname == "MatEmissiveColor")
                                aiMat->AddProperty(&aiCol, 1, AI_MATKEY_COLOR_EMISSIVE);
                        }
                        loadedReal = true;
                    }
                }
            }
        }

        if (!loadedReal)
        {
            aiString matName(("Material_" + String(i)).CString());
            aiMat->AddProperty(&matName, AI_MATKEY_NAME);
        }
    }

    // --- Load animations if requested ---
    Vector<String> animPaths = exportAnimPaths_;

    // Auto-discover animations if -allanims
    if (exportAllAnims_)
    {
        String basePath = GetPath(inFile);
        String baseName = GetFileName(inFile);
        // Remove trailing _mesh, _Mesh, etc. from base name for matching
        if (baseName.EndsWith("_mesh", false) || baseName.EndsWith("_Mesh", false))
            baseName = baseName.Substring(0, baseName.Length() - 5);

        auto* fs = context_->GetSubsystem<FileSystem>();
        Vector<String> files;
        fs->ScanDir(files, basePath, "*.ani", SCAN_FILES, false);
        for (unsigned i = 0; i < files.Size(); ++i)
        {
            animPaths.Push(basePath + files[i]);
        }
        // Also scan the directory where the output model lives
        String modelDir = GetPath(inFile);
        if (modelDir != basePath)
        {
            fs->ScanDir(files, modelDir, "*.ani", SCAN_FILES, false);
            for (unsigned i = 0; i < files.Size(); ++i)
                animPaths.Push(modelDir + files[i]);
        }
    }

    if (animPaths.Size() > 0)
    {
        // Count valid animations
        Vector<SharedPtr<Animation>> animations;
        for (unsigned i = 0; i < animPaths.Size(); ++i)
        {
            SharedPtr<File> aniFile(new File(context_, animPaths[i]));
            if (!aniFile->IsOpen())
            {
                PrintLine("Warning: Could not open animation " + animPaths[i]);
                continue;
            }

            // Check header
            String aniID = aniFile->ReadFileID();
            if (aniID != "UANI")
            {
                PrintLine("Warning: " + animPaths[i] + " is not a valid .ani file");
                continue;
            }

            // Read animation data
            String animName = aniFile->ReadString();
            float animLength = aniFile->ReadFloat();
            unsigned numTracks = aniFile->ReadU32();

            if (numTracks == 0)
                continue;

            SharedPtr<Animation> anim(new Animation(context_));
            anim->SetAnimationName(animName);
            anim->SetLength(animLength);

            for (unsigned t = 0; t < numTracks; ++t)
            {
                String trackName = aniFile->ReadString();
                AnimationChannels channelMask = AnimationChannels(aniFile->ReadU8());
                unsigned numKeyFrames = aniFile->ReadU32();

                AnimationTrack* track = anim->CreateTrack(trackName);
                track->channelMask_ = channelMask;

                for (unsigned k = 0; k < numKeyFrames; ++k)
                {
                    AnimationKeyFrame kf;
                    kf.time_ = aniFile->ReadFloat();
                    if (!!(channelMask & AnimationChannels::Position))
                        kf.position_ = aniFile->ReadVector3();
                    if (!!(channelMask & AnimationChannels::Rotation))
                        kf.rotation_ = aniFile->ReadQuaternion();
                    if (!!(channelMask & AnimationChannels::Scale))
                        kf.scale_ = aniFile->ReadVector3();
                    track->keyFrames_.Push(kf);
                }
            }

            animations.Push(anim);
            PrintLine("  Loaded animation: " + animName + " (" + String(numTracks) + " tracks, " +
                      String(animLength, 2) + "s)");
        }

        // Convert animations to aiAnimation
        if (animations.Size() > 0)
        {
            scene->mNumAnimations = animations.Size();
            scene->mAnimations = new aiAnimation*[animations.Size()];

            for (unsigned a = 0; a < animations.Size(); ++a)
            {
                Animation* anim = animations[a];
                aiAnimation* aiAnim = new aiAnimation();
                scene->mAnimations[a] = aiAnim;

                aiAnim->mName = aiString(anim->GetAnimationName().CString());
                // Phase 3 fidelity fix: encode time directly in seconds and set
                // ticks-per-second = 1 so the 30fps assumption stops drifting
                // animations authored at 24/60/120 fps. Assimp's FBX exporter
                // honours mTicksPerSecond when writing, and Blender reads it.
                aiAnim->mDuration = anim->GetLength();
                aiAnim->mTicksPerSecond = 1.0;

                const HashMap<StringHash, AnimationTrack>& tracks = anim->GetTracks();
                aiAnim->mNumChannels = tracks.Size();
                aiAnim->mChannels = new aiNodeAnim*[tracks.Size()];

                unsigned channelIdx = 0;
                for (HashMap<StringHash, AnimationTrack>::ConstIterator it = tracks.Begin();
                     it != tracks.End(); ++it)
                {
                    const AnimationTrack& track = it->second_;
                    aiNodeAnim* channel = new aiNodeAnim();
                    aiAnim->mChannels[channelIdx++] = channel;

                    channel->mNodeName = aiString(track.name_.CString());

                    // Position keys
                    if (!!(track.channelMask_ & AnimationChannels::Position))
                    {
                        channel->mNumPositionKeys = track.keyFrames_.Size();
                        channel->mPositionKeys = new aiVectorKey[track.keyFrames_.Size()];
                        for (unsigned k = 0; k < track.keyFrames_.Size(); ++k)
                        {
                            const AnimationKeyFrame& kf = track.keyFrames_[k];
                            channel->mPositionKeys[k].mTime = kf.time_;
                            channel->mPositionKeys[k].mValue = aiVector3D(
                                kf.position_.x_ * importScale_,
                                kf.position_.y_ * importScale_,
                                kf.position_.z_ * importScale_);
                        }
                    }
                    else
                    {
                        channel->mNumPositionKeys = 1;
                        channel->mPositionKeys = new aiVectorKey[1];
                        channel->mPositionKeys[0].mTime = 0.0;
                        channel->mPositionKeys[0].mValue = aiVector3D(0, 0, 0);
                    }

                    // Rotation keys
                    if (!!(track.channelMask_ & AnimationChannels::Rotation))
                    {
                        channel->mNumRotationKeys = track.keyFrames_.Size();
                        channel->mRotationKeys = new aiQuatKey[track.keyFrames_.Size()];
                        for (unsigned k = 0; k < track.keyFrames_.Size(); ++k)
                        {
                            const AnimationKeyFrame& kf = track.keyFrames_[k];
                            channel->mRotationKeys[k].mTime = kf.time_;
                            channel->mRotationKeys[k].mValue = aiQuaternion(
                                kf.rotation_.w_, kf.rotation_.x_,
                                kf.rotation_.y_, kf.rotation_.z_);
                        }
                    }
                    else
                    {
                        channel->mNumRotationKeys = 1;
                        channel->mRotationKeys = new aiQuatKey[1];
                        channel->mRotationKeys[0].mTime = 0.0;
                        channel->mRotationKeys[0].mValue = aiQuaternion(1, 0, 0, 0);
                    }

                    // Scale keys
                    if (!!(track.channelMask_ & AnimationChannels::Scale))
                    {
                        channel->mNumScalingKeys = track.keyFrames_.Size();
                        channel->mScalingKeys = new aiVectorKey[track.keyFrames_.Size()];
                        for (unsigned k = 0; k < track.keyFrames_.Size(); ++k)
                        {
                            const AnimationKeyFrame& kf = track.keyFrames_[k];
                            channel->mScalingKeys[k].mTime = kf.time_;
                            channel->mScalingKeys[k].mValue = aiVector3D(
                                kf.scale_.x_, kf.scale_.y_, kf.scale_.z_);
                        }
                    }
                    else
                    {
                        channel->mNumScalingKeys = 1;
                        channel->mScalingKeys = new aiVectorKey[1];
                        channel->mScalingKeys[0].mTime = 0.0;
                        channel->mScalingKeys[0].mValue = aiVector3D(1, 1, 1);
                    }
                }
            }
        }
    }

    // Apply scale to bone node translations too
    if (importScale_ != 1.0f)
    {
        for (unsigned i = 0; i < numBones; ++i)
        {
            aiMatrix4x4& m = boneNodes[i]->mTransformation;
            m.a4 *= importScale_;
            m.b4 *= importScale_;
            m.c4 *= importScale_;
        }
    }

    // --- Export ---
    PrintLine("  Writing FBX: " + outFile);
    aiReturn result = aiExportScene(scene, "fbx", GetNativePath(outFile).CString(), 0);

    if (result != aiReturn_SUCCESS)
    {
        PrintLine("ERROR: FBX export failed: " + String(aiGetErrorString()));
    }
    else
    {
        PrintLine("  Export successful!");
    }

    // Cleanup — aiScene destructor handles recursive deletion
    delete scene;
}

