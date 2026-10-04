#include "ModelTool.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/OctreeQuery.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Skeleton.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/IK/IKSolver.h>
#include <Urho3D/IK/IKEffector.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/GraphicsAPI/IndexBuffer.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/JSONValue.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Graphics/DrawableEvents.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/DropDownList.h>
#include <Urho3D/UI/ScrollView.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/UIEvents.h>

#include <functional>

#include <climits>

#ifdef URHO3D_DATABASE_SQLITE
#include <SQLite/sqlite3.h>
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#endif

#include <Urho3D/Core/IPCBootstrap.h>

URHO3D_DEFINE_APPLICATION_MAIN(ModelTool);

// Forward declaration — defined near DrawBoneGizmo
static float BoneGizmoRadius(const Vector<Bone>& bones, int boneIndex, Node* cameraNode);

ModelTool::ModelTool(Context* context) : Application(context) {}

void ModelTool::Setup()
{
    engineParameters_[EP_WINDOW_TITLE] = "ModelTool";
    engineParameters_[EP_WINDOW_ICON] = "Icons/ModelTool.png";
    engineParameters_[EP_WINDOW_WIDTH] = 1280;
    engineParameters_[EP_WINDOW_HEIGHT] = 720;
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_LOG_NAME] = "ModelTool.log";
    engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data;../../brainfarts;../../Staging";
    engineParameters_[EP_SOUND] = true;
    engineParameters_[EP_WINDOW_RESIZABLE] = true;

    // Command-line: --path <model> or bare path as last arg
    const Vector<String>& args = GetArguments();
    for (unsigned i = 0; i < args.Size(); ++i)
    {
        if (args[i] == "--path" && i + 1 < args.Size())
            startupModelPath_ = args[++i];
        else if (!args[i].StartsWith("-") && (args[i].EndsWith(".mdl") || args[i].EndsWith(".fbx")))
            startupModelPath_ = args[i];
    }
}

void ModelTool::Start()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(style);
    font_ = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    CreateScene();
    SetupViewport();
    CreateUI();

    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(ModelTool, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(ModelTool, HandlePostRenderUpdate));
    SubscribeToEvent(E_MOUSEMOVE, URHO3D_HANDLER(ModelTool, HandleMouseMove));
    SubscribeToEvent(E_MOUSEWHEEL, URHO3D_HANDLER(ModelTool, HandleMouseWheel));
    SubscribeToEvent(E_DROPFILE, URHO3D_HANDLER(ModelTool, HandleDropFile));

    // Command-line arguments (from Setup --path, or legacy -model/-folder)
    URHO3D_LOGINFOF("startupModelPath_: '%s'", startupModelPath_.CString());
    if (!startupModelPath_.Empty())
        LoadModel(startupModelPath_);
    else
    {
        const Vector<String>& startArgs = GetArguments();
        for (unsigned i = 0; i < startArgs.Size(); ++i)
        {
            if ((startArgs[i] == "-model" || startArgs[i] == "model") && i + 1 < startArgs.Size())
            {
                LoadModel(startArgs[i + 1]);
                break;
            }
            else if ((startArgs[i] == "-folder" || startArgs[i] == "folder") && i + 1 < startArgs.Size())
            {
                ScanFolder(startArgs[i + 1]);
                break;
            }
        }
    }

    GetSubsystem<Input>()->SetMouseVisible(true);
    GetSubsystem<Input>()->SetMouseGrabbed(false);

    // Start IPC listener
    StartIPC();

    // Initialize resource tracking DB
    InitResourceDB();
}

void ModelTool::Stop()
{
    CloseResourceDB();
    StopIPC();
}

// ============================================================================
// Scene
// ============================================================================

void ModelTool::CreateScene()
{
    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<DebugRenderer>();

    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.3f, 0.3f, 0.3f));
    zone->SetFogColor(Color(0.2f, 0.2f, 0.25f));
    zone->SetFogStart(500.0f);
    zone->SetFogEnd(750.0f);

    lightNode_ = scene_->CreateChild("Light");
    lightNode_->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    auto* light = lightNode_->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(1.0f, 0.95f, 0.9f));
    light->SetCastShadows(true);

    // Ground plane for visual reference
    auto* cache = GetSubsystem<ResourceCache>();
    Node* planeNode = scene_->CreateChild("Plane");
    planeNode->SetScale(Vector3(20.0f, 1.0f, 20.0f));
    auto* planeObject = planeNode->CreateComponent<StaticModel>();
    planeObject->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    planeObject->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

    modelNode_ = scene_->CreateChild("Model");
    refModelNode_ = scene_->CreateChild("RefModel");
    refModelNode_->SetPosition(Vector3(3.0f, 0.0f, 0.0f));

    cameraNode_ = scene_->CreateChild("Camera");
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
    camera->SetNearClip(0.1f);
    cameraNode_->SetPosition(Vector3(0.0f, 2.0f, -5.0f));
    cameraNode_->LookAt(Vector3::ZERO);
}

void ModelTool::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();
    auto* camera = cameraNode_->GetComponent<Camera>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, camera));
    renderer->SetViewport(0, viewport);
}

// ============================================================================
// Model Loading
// ============================================================================

// All ModelTool persistent state lives in a single JSON file:
//   ~/.local/share/Urho3D/ModelTool/state.json
// Holds: lastModel (absolute path) + recentAnims (per-model animation snapshots).
static String GetStateFilePath(FileSystem* fs)
{
    String prefs = fs->GetAppPreferencesDir("Urho3D", "ModelTool");
    if (!fs->DirExists(prefs))
        fs->CreateDir(prefs);
    return prefs + "state.json";
}

// Load the state JSON file. Returns an empty JSONFile if missing or invalid.
static SharedPtr<JSONFile> LoadStateFile(Context* ctx)
{
    SharedPtr<JSONFile> jf(new JSONFile(ctx));
    auto* fs = ctx->GetSubsystem<FileSystem>();
    String path = GetStateFilePath(fs);
    if (fs->FileExists(path))
    {
        File file(ctx, path, FILE_READ);
        if (file.IsOpen())
            jf->Load(file);
    }
    return jf;
}

static void WriteStateFile(Context* ctx, JSONFile& jf)
{
    auto* fs = ctx->GetSubsystem<FileSystem>();
    String path = GetStateFilePath(fs);
    File file(ctx, path, FILE_WRITE);
    if (file.IsOpen())
        jf.Save(file, "  ");
}

void ModelTool::SaveLastModelDir(const String& fullModelPath)
{
    if (fullModelPath.Empty()) return;
    SharedPtr<JSONFile> jf = LoadStateFile(context_);
    jf->GetRoot().Set("lastModel", JSONValue(fullModelPath));
    WriteStateFile(context_, *jf);
}

String ModelTool::LoadLastModelDir() const
{
    SharedPtr<JSONFile> jf = LoadStateFile(context_);
    return jf->GetRoot().Get("lastModel").GetString();
}

void ModelTool::SaveAnimationManifest(const String& absModelPath) const
{
    if (absModelPath.Empty()) return;

    // Collect non-bind-pose anims as a JSON array
    JSONArray emptyArr;
    JSONValue animArray(emptyArr);
    int written = 0;
    for (unsigned i = 0; i < availableAnims_.Size(); ++i)
    {
        if (availableAnims_[i] == "[Bind Pose]") continue;
        animArray.Push(JSONValue(availableAnims_[i]));
        ++written;
    }

    SharedPtr<JSONFile> jf = LoadStateFile(context_);
    JSONValue& root = jf->GetRoot();
    JSONValue recentObj = root.Get("recentAnims");
    if (!recentObj.IsObject())
        recentObj = JSONValue(JSONObject());

    if (written > 0)
        recentObj.Set(absModelPath, animArray);
    else
        recentObj.Erase(absModelPath);  // empty list — drop the entry

    root.Set("recentAnims", recentObj);
    WriteStateFile(context_, *jf);
    URHO3D_LOGINFOF("Saved %d animation entries for %s", written, absModelPath.CString());
}

void ModelTool::LoadAnimationManifest(const String& absModelPath)
{
    if (absModelPath.Empty()) return;
    SharedPtr<JSONFile> jf = LoadStateFile(context_);
    const JSONValue& recentObj = jf->GetRoot().Get("recentAnims");
    if (!recentObj.IsObject()) return;
    const JSONValue& animArray = recentObj.Get(absModelPath);
    if (!animArray.IsArray()) return;

    int loaded = 0;
    for (unsigned i = 0; i < animArray.Size(); ++i)
    {
        String path = animArray[i].GetString();
        if (path.Empty()) continue;
        if (LoadAnimationFile(path))
            ++loaded;
    }
    URHO3D_LOGINFOF("Loaded %d animations for %s", loaded, absModelPath.CString());
}

void ModelTool::LoadModel(const String& path)
{
    auto* cache = GetSubsystem<ResourceCache>();

    // If path looks absolute, add its parent directory as a resource path
    // so the model AND its materials/textures resolve correctly
    auto* fs = GetSubsystem<FileSystem>();
    if (IsAbsolutePath(path) && fs->FileExists(path))
    {
        String dir = GetPath(path);
        if (!dir.Empty())
        {
            cache->AddResourceDir(dir, 0);
            URHO3D_LOGINFOF("Added resource dir for model: %s", dir.CString());
        }
    }

    String loadPath = path;
    if (IsAbsolutePath(path))
        loadPath = GetFileNameAndExtension(path);

    auto* model = cache->GetResource<Model>(loadPath);
    if (!model)
    {
        URHO3D_LOGERRORF("Failed to load model: %s", path.CString());
        if (statusText_) statusText_->SetText("ERROR: " + path);
        return;
    }

    // Persist the OUTGOING animated model's loaded animation list before we wipe state.
    if (isAnimated_ && !currentModelPath_.Empty() && availableAnims_.Size() > 1)
    {
        auto* fs = GetSubsystem<FileSystem>();
        const Vector<String>& dirs = cache->GetResourceDirs();
        for (unsigned i = 0; i < dirs.Size(); ++i)
        {
            String absOld = dirs[i] + currentModelPath_;
            if (fs->FileExists(absOld))
            {
                SaveAnimationManifest(absOld);
                break;
            }
        }
    }

    RemoveAllProps();
    TeardownIKRig();           // drop solver/effectors and the scene-root target nodes
    ikPoseMode_ = false;
    capturedPoses_.Clear();
    RebuildPoseList();
    modelNode_->RemoveAllComponents();
    modelNode_->RemoveAllChildren();
    modelNode_->SetPosition(Vector3::ZERO);
    modelNode_->SetRotation(Quaternion::IDENTITY);
    modelNode_->SetScale(1.0f);

    URHO3D_LOGINFOF("Loaded model: %s (%d geometries, %d bones)",
        path.CString(), model->GetNumGeometries(), model->GetSkeleton().GetNumBones());

    currentModel_ = model;
    currentModelPath_ = path;

    // Persist the absolute model file path for next session and scan folder for browse
    {
        auto* fs = GetSubsystem<FileSystem>();
        const Vector<String>& dirs = cache->GetResourceDirs();
        for (unsigned i = 0; i < dirs.Size(); ++i)
        {
            String absPath = dirs[i] + path;
            if (fs->FileExists(absPath))
            {
                SaveLastModelDir(absPath);  // save full path including filename
                // Scan folder for browse keys — but only if not already scanning this folder
                String folder = GetPath(absPath);
                if (folder != browseFolderPath_)
                    ScanFolder(folder);
                break;
            }
        }
        // Also handle absolute paths (outside resource dirs)
        if (path.StartsWith("/") && folderModels_.Empty())
        {
            String folder = GetPath(path);
            if (folder != browseFolderPath_)
                ScanFolder(folder);
        }
    }

    isAnimated_ = false;
    staticModelComp_ = nullptr;
    animatedModelComp_ = nullptr;
    animController_ = nullptr;
    currentAnimIndex_ = -1;
    animPlaying_ = false;
    availableAnims_.Clear();

    Skeleton& skeleton = model->GetSkeleton();
    if (skeleton.GetNumBones() > 1)
    {
        isAnimated_ = true;
        animatedModelComp_ = modelNode_->CreateComponent<AnimatedModel>();
        animatedModelComp_->SetModel(model, true, true);
        animatedModelComp_->SetCastShadows(true);
        staticModelComp_ = animatedModelComp_;
        animController_ = modelNode_->CreateComponent<AnimationController>();

        // Phase 0: auto-detect IK chains from skeleton topology (leaf -> nearest branch).
        ikChains_ = DetectIKChains();
        URHO3D_LOGINFOF("IK: detected %u chain(s) from skeleton topology", ikChains_.Size());
        {
            const Vector<Bone>& ikDbgBones = animatedModelComp_->GetSkeleton().GetBones();
            for (unsigned ci = 0; ci < ikChains_.Size(); ++ci)
            {
                const IKChain& c = ikChains_[ci];
                URHO3D_LOGINFOF("  chain %u: effector '%s' [%d] -> root '%s' [%d], length %u",
                    ci,
                    ikDbgBones[c.effectorBoneIndex_].name_.CString(), c.effectorBoneIndex_,
                    ikDbgBones[c.rootBoneIndex_].name_.CString(), c.rootBoneIndex_,
                    c.length_);
            }
        }
        ScanAnimations();

        // After auto-scan, also load any animations from a sibling <model>.anims manifest
        {
            auto* fs = GetSubsystem<FileSystem>();
            const Vector<String>& dirs = cache->GetResourceDirs();
            for (unsigned i = 0; i < dirs.Size(); ++i)
            {
                String abs = dirs[i] + path;
                if (fs->FileExists(abs))
                {
                    LoadAnimationManifest(abs);
                    break;
                }
            }
        }
    }
    else
    {
        staticModelComp_ = modelNode_->CreateComponent<StaticModel>();
        staticModelComp_->SetModel(model, true);
        staticModelComp_->SetCastShadows(true);
    }

    // --- Material resolution ---
    // Try: 1) material list .txt, 2) per-geometry Materials/Name_N.xml,
    // 3) single Materials/Name.xml for all, 4) fallback default lit material
    bool materialsApplied = false;
    String matListPath = ReplaceExtension(path, ".txt");
    if (cache->Exists(matListPath))
    {
        staticModelComp_->ApplyMaterialList(matListPath);
        materialsApplied = true;
    }

    if (!materialsApplied)
    {
        // Try Materials/ directory relative to model path, and also root Materials/
        String modelDir = GetPath(path);       // e.g. "Models/"
        String baseName = GetFileName(path);   // e.g. "Jack"
        unsigned numGeoms = model->GetNumGeometries();

        // Try per-geometry: Materials/Name_0.xml, Materials/Name_1.xml, ...
        bool allFound = true;
        for (unsigned i = 0; i < numGeoms; ++i)
        {
            String matPath = modelDir + "Materials/" + baseName + "_" + String(i) + ".xml";
            if (!cache->Exists(matPath))
                matPath = "Materials/" + baseName + "_" + String(i) + ".xml";
            if (cache->Exists(matPath))
            {
                auto* mat = cache->GetResource<Material>(matPath);
                if (mat) staticModelComp_->SetMaterial(i, mat);
            }
            else
            {
                allFound = false;
                break;
            }
        }

        if (allFound && numGeoms > 0)
            materialsApplied = true;
    }

    if (!materialsApplied)
    {
        // Try single material: Materials/Name.xml
        String modelDir = GetPath(path);
        String baseName = GetFileName(path);
        unsigned numGeoms = model->GetNumGeometries();

        String matPath = modelDir + "Materials/" + baseName + ".xml";
        if (!cache->Exists(matPath))
            matPath = "Materials/" + baseName + ".xml";
        if (cache->Exists(matPath))
        {
            auto* mat = cache->GetResource<Material>(matPath);
            if (mat)
            {
                for (unsigned i = 0; i < numGeoms; ++i)
                    staticModelComp_->SetMaterial(i, mat);
                materialsApplied = true;
            }
        }
    }

    if (!materialsApplied)
    {
        // Scan Materials/ directory for any .xml files and apply them to geometries
        auto* fs = GetSubsystem<FileSystem>();
        String modelDir = GetPath(path);
        unsigned numGeoms = model->GetNumGeometries();
        const Vector<String>& resourceDirs = cache->GetResourceDirs();

        // Check Materials/ subdirs relative to model dir and at root
        Vector<String> matDirs;
        matDirs.Push(modelDir + "Materials/");
        matDirs.Push("Materials/");
        // Also check sibling: if model is in Models/, check ../Materials/
        if (modelDir.EndsWith("Models/") || modelDir.EndsWith("models/"))
        {
            String parent = GetParentPath(modelDir.Substring(0, modelDir.Length() - 1));
            matDirs.Push(parent + "Materials/");
        }

        Vector<String> foundMats;
        for (unsigned d = 0; d < matDirs.Size() && foundMats.Empty(); ++d)
        {
            for (unsigned r = 0; r < resourceDirs.Size() && foundMats.Empty(); ++r)
            {
                String absDir = resourceDirs[r] + matDirs[d];
                if (fs->DirExists(absDir))
                {
                    Vector<String> xmlFiles;
                    fs->ScanDir(xmlFiles, absDir, "*.xml", SCAN_FILES, false);
                    for (unsigned f = 0; f < xmlFiles.Size(); ++f)
                    {
                        // Verify it's actually a material file by trying to load it
                        String matResPath = matDirs[d] + xmlFiles[f];
                        auto* mat = cache->GetResource<Material>(matResPath, false);
                        if (mat)
                            foundMats.Push(matResPath);
                    }
                }
            }
        }

        if (!foundMats.Empty())
        {
            // Apply found materials: one per geometry, cycling if fewer materials than geometries
            for (unsigned i = 0; i < numGeoms; ++i)
            {
                unsigned matIdx = i < foundMats.Size() ? i : foundMats.Size() - 1;
                auto* mat = cache->GetResource<Material>(foundMats[matIdx]);
                if (mat)
                    staticModelComp_->SetMaterial(i, mat);
            }
            materialsApplied = true;
            URHO3D_LOGINFOF("Applied %d scanned materials to %s", foundMats.Size(), path.CString());
        }
    }

    if (!materialsApplied)
    {
        // Fallback: apply a default lit material so the model isn't invisible
        auto* defaultMat = cache->GetResource<Material>("Materials/DefaultGrey.xml");
        if (!defaultMat)
        {
            // Create a basic grey lit material on the fly
            defaultMat = new Material(context_);
            defaultMat->SetTechnique(0, cache->GetResource<Technique>("Techniques/DiffUnlit.xml"));
            defaultMat->SetShaderParameter("MatDiffColor", Color(0.6f, 0.6f, 0.6f, 1.0f));
        }
        unsigned numGeoms = model->GetNumGeometries();
        for (unsigned i = 0; i < numGeoms; ++i)
            staticModelComp_->SetMaterial(i, defaultMat);
        URHO3D_LOGWARNINGF("No materials found for %s — using default grey", path.CString());
    }

    // Auto-scale oversized models (e.g. FBX exported in centimeters)
    BoundingBox bbox = currentModel_->GetBoundingBox();
    float diagonal = (bbox.max_ - bbox.min_).Length();
    if (diagonal > 50.0f)
    {
        float scale = 10.0f / diagonal;
        modelNode_->SetScale(scale);
        URHO3D_LOGINFOF("Model oversized (diag=%.1f), auto-scaled to %.4f", diagonal, scale);
    }

    AutoFrameCamera();
    RebuildInfoText();
    RebuildAnimList();

    if (statusText_) statusText_->SetText(path);
    if (playbackPanel_) playbackPanel_->SetVisible(isAnimated_);
    if (posePanel_ && !isAnimated_) posePanel_->SetVisible(false);
    RefreshViewMenuItems();

    // Status bar
    if (statusBarText_)
    {
        String info = GetFileNameAndExtension(path);
        info += " — " + String(model->GetNumGeometries()) + " geom";
        if (isAnimated_)
            info += ", " + String(model->GetSkeleton().GetNumBones()) + " bones";
        unsigned totalVerts = 0;
        for (unsigned g = 0; g < model->GetNumGeometries(); ++g)
        {
            auto* geom = model->GetGeometry(g, 0);
            if (geom && geom->GetVertexBuffer(0))
                totalVerts += geom->GetVertexBuffer(0)->GetVertexCount();
        }
        info += ", " + String(totalVerts) + " verts";
        statusBarText_->SetText(info);
    }
}

void ModelTool::AutoFrameCamera()
{
    if (!currentModel_ || !modelNode_) return;

    BoundingBox bbox = currentModel_->GetBoundingBox();
    float scale = modelNode_->GetScale().x_;

    // Work in world space
    Vector3 worldMin = bbox.min_ * scale + modelNode_->GetPosition();
    Vector3 worldMax = bbox.max_ * scale + modelNode_->GetPosition();

    // Include ref model in framing if present
    if (refModel_ && refModelNode_)
    {
        BoundingBox refBbox = refModel_->GetBoundingBox();
        float refScale = refModelNode_->GetScale().x_;
        Vector3 rMin = refBbox.min_ * refScale + refModelNode_->GetPosition();
        Vector3 rMax = refBbox.max_ * refScale + refModelNode_->GetPosition();
        worldMin.x_ = Min(worldMin.x_, rMin.x_);
        worldMin.y_ = Min(worldMin.y_, rMin.y_);
        worldMin.z_ = Min(worldMin.z_, rMin.z_);
        worldMax.x_ = Max(worldMax.x_, rMax.x_);
        worldMax.y_ = Max(worldMax.y_, rMax.y_);
        worldMax.z_ = Max(worldMax.z_, rMax.z_);
    }

    modelCenter_ = (worldMin + worldMax) * 0.5f;
    float diagonal = (worldMax - worldMin).Length();

    URHO3D_LOGINFOF("AutoFrame: scale=%.4f worldDiag=%.2f center=(%f,%f,%f)",
        scale, diagonal, modelCenter_.x_, modelCenter_.y_, modelCenter_.z_);

    if (diagonal < 0.001f) diagonal = 2.0f;

    auto* camera = cameraNode_->GetComponent<Camera>();
    float halfAngle = camera->GetFov() * 0.5f * M_DEGTORAD;
    cameraDistance_ = (diagonal * 0.5f) / tanf(halfAngle) * 1.3f;
    cameraYaw_ = 45.0f;
    cameraPitch_ = 20.0f;

    URHO3D_LOGINFOF("AutoFrame: cameraDistance=%.2f", cameraDistance_);
}

void ModelTool::ScanAnimations()
{
    availableAnims_.Clear();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();

    // Read explicit .animations sidecar file (shared anims across skeletons)
    String animListPath = ReplaceExtension(currentModelPath_, ".animations");
    if (cache->Exists(animListPath))
    {
        SharedPtr<File> listFile = cache->GetFile(animListPath);
        if (listFile)
        {
            while (!listFile->IsEof())
            {
                String line = listFile->ReadLine().Trimmed();
                if (line.Empty() || line.StartsWith("#"))
                    continue;
                if (cache->Exists(line) && !availableAnims_.Contains(line))
                    availableAnims_.Push(line);
            }
        }
    }

    // Scan directory for .ani files matching model name prefix
    String modelName = GetFileName(currentModelPath_);
    String modelDir = GetParentPath(currentModelPath_);
    const Vector<String>& resourceDirs = cache->GetResourceDirs();

    for (unsigned d = 0; d < resourceDirs.Size(); ++d)
    {
        String fullDir = resourceDirs[d] + modelDir;
        if (!fs->DirExists(fullDir)) continue;

        Vector<String> files;
        fs->ScanDir(files, fullDir, "*.ani", SCAN_FILES, false);
        for (unsigned f = 0; f < files.Size(); ++f)
        {
            if (GetFileName(files[f]).StartsWith(modelName, false))
            {
                String animPath = modelDir + files[f];
                if (availableAnims_.Find(animPath) == availableAnims_.End())
                    availableAnims_.Push(animPath);
            }
        }
    }

    Sort(availableAnims_.Begin(), availableAnims_.End());

    // Generate a real bind pose animation from skeleton initial transforms
    if (isAnimated_ && animatedModelComp_)
    {
        const Skeleton& skel = animatedModelComp_->GetSkeleton();
        const Vector<Bone>& bones = skel.GetBones();
        if (bones.Size() > 0)
        {
            SharedPtr<Animation> bindPoseAnim(new Animation(context_));
            bindPoseAnim->SetName("[Bind Pose]");
            bindPoseAnim->SetAnimationName("[Bind Pose]");
            bindPoseAnim->SetLength(0.0f);

            for (unsigned i = 0; i < bones.Size(); ++i)
            {
                const Bone& bone = bones[i];
                AnimationTrack* track = bindPoseAnim->CreateTrack(bone.name_);
                track->channelMask_ = AnimationChannels::Position | AnimationChannels::Rotation | AnimationChannels::Scale;

                AnimationKeyFrame kf;
                kf.time_ = 0.0f;
                kf.position_ = bone.initialPosition_;
                kf.rotation_ = bone.initialRotation_;
                kf.scale_ = bone.initialScale_;
                track->AddKeyFrame(kf);
            }

            auto* cache = GetSubsystem<ResourceCache>();
            cache->AddManualResource(bindPoseAnim);
        }
    }
    availableAnims_.Insert(0, "[Bind Pose]");

    if (availableAnims_.Size() > 1)
    {
        // Prefer Idle/Sitting animation over alphabetical first (which is often Die)
        int bestIndex = 1; // skip bind pose
        for (unsigned i = 1; i < availableAnims_.Size(); ++i)
        {
            String name = GetFileName(availableAnims_[i]).ToLower();
            if (name.Contains("idle") || name.Contains("sitting"))
            {
                bestIndex = (int)i;
                break;
            }
        }
        PlayAnimation(bestIndex);
    }
}

bool ModelTool::LoadAnimationFile(const String& fullPath)
{
    if (fullPath.Empty() || !fullPath.EndsWith(".ani", false))
        return false;

    // Convert absolute path to ResourceCache-relative if possible
    auto* cache = GetSubsystem<ResourceCache>();
    String resourcePath = fullPath;
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (fullPath.StartsWith(dirs[i]))
        {
            resourcePath = fullPath.Substring(dirs[i].Length());
            break;
        }
    }

    if (availableAnims_.Find(resourcePath) != availableAnims_.End())
    {
        URHO3D_LOGINFOF("LoadAnimationFile: %s already loaded", resourcePath.CString());
        return false;
    }

    // Try to actually load it via cache
    auto* anim = cache->GetResource<Animation>(resourcePath);
    if (!anim)
    {
        URHO3D_LOGWARNINGF("LoadAnimationFile: failed to load %s", resourcePath.CString());
        return false;
    }

    availableAnims_.Push(resourcePath);
    URHO3D_LOGINFOF("LoadAnimationFile: added %s (%.2fs)", resourcePath.CString(), anim->GetLength());
    RebuildAnimList();
    RebuildInfoText();
    return true;
}

void ModelTool::ShowLoadAnimationsDialog()
{
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();
    auto* fs = GetSubsystem<FileSystem>();

    // Stage 1: pick a folder via FileSelector (any .ani file)
    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Pick a folder containing .ani files");
    fileSelector_->SetButtonTexts("Choose", "Cancel");

    Vector<String> filters;
    filters.Push("*.ani");
    fileSelector_->SetFilters(filters, 0);

    // Default to model's directory if available
    String startDir;
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    if (!currentModelPath_.Empty())
    {
        String modelDir = GetParentPath(currentModelPath_);
        for (unsigned i = 0; i < dirs.Size(); ++i)
        {
            if (fs->DirExists(dirs[i] + modelDir))
            {
                startDir = dirs[i] + modelDir;
                break;
            }
        }
    }
    if (startDir.Empty())
    {
        for (unsigned i = 0; i < dirs.Size(); ++i)
        {
            if (fs->DirExists(dirs[i] + "Models/"))
            {
                startDir = dirs[i] + "Models/";
                break;
            }
        }
    }
    fileSelector_->SetPath(startDir);

    SubscribeToEvent(fileSelector_, E_FILESELECTED, [this](StringHash, VariantMap& ed)
    {
        String path = ed[FileSelected::P_FILENAME].GetString();
        bool ok = ed[FileSelected::P_OK].GetBool();
        fileSelector_.Reset();
        if (!ok || path.Empty()) return;

        // Stage 2: scan that folder for .ani files and show checkbox list
        String folder = GetPath(path);
        auto* fs = GetSubsystem<FileSystem>();
        Vector<String> aniFiles;
        fs->ScanDir(aniFiles, folder, "*.ani", SCAN_FILES, false);
        Sort(aniFiles.Begin(), aniFiles.End());
        if (aniFiles.Empty())
        {
            URHO3D_LOGWARNING("No .ani files found in folder");
            return;
        }

        auto* ui = GetSubsystem<UI>();
        auto* uiRoot = ui->GetRoot();
        auto* style = uiRoot->GetDefaultStyle();

        auto* dialog = uiRoot->CreateChild<Window>("AnimMultiSelect");
        dialog->SetStyleAuto();
        dialog->SetMinSize(420, 500);
        dialog->SetMaxSize(420, 700);
        dialog->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));
        dialog->SetAlignment(HA_CENTER, VA_CENTER);
        dialog->SetMovable(true);
        dialog->SetModal(true);
        dialog->SetOpacity(0.95f);

        auto* title = dialog->CreateChild<Text>();
        title->SetFont(font_, 14);
        title->SetText("Select Animations to Load (" + String(aniFiles.Size()) + " found)");
        title->SetColor(Color(0.9f, 0.9f, 0.7f));

        // Toolbar with All/None
        auto* toolbar = dialog->CreateChild<UIElement>();
        toolbar->SetLayout(LM_HORIZONTAL, 4, IntRect(0, 0, 0, 0));
        toolbar->SetFixedHeight(24);

        auto makeBtn = [&](const String& text) -> Button*
        {
            auto* btn = toolbar->CreateChild<Button>();
            btn->SetStyleAuto();
            btn->SetFixedSize(80, 22);
            auto* t = btn->CreateChild<Text>();
            t->SetFont(font_, 11);
            t->SetText(text);
            t->SetAlignment(HA_CENTER, VA_CENTER);
            return btn;
        };
        auto* btnAll = makeBtn("All");
        auto* btnNone = makeBtn("None");

        // Scroll list with checkboxes
        auto* list = dialog->CreateChild<ListView>();
        list->SetStyleAuto();
        list->SetMinHeight(380);
        list->SetSelectOnClickEnd(true);
        list->SetHighlightMode(HM_ALWAYS);
        list->SetMultiselect(false);

        Vector<CheckBox*> boxes;
        Vector<String> filenames(aniFiles);  // capture for lambda
        for (unsigned i = 0; i < aniFiles.Size(); ++i)
        {
            auto* row = new UIElement(context_);
            row->SetLayout(LM_HORIZONTAL, 6, IntRect(2, 2, 2, 2));
            row->SetFixedHeight(20);

            auto* cb = row->CreateChild<CheckBox>();
            cb->SetStyleAuto();
            cb->SetChecked(false);
            boxes.Push(cb);

            auto* lbl = row->CreateChild<Text>();
            lbl->SetFont(font_, 11);
            lbl->SetText(aniFiles[i]);
            lbl->SetColor(Color(0.9f, 0.9f, 0.9f));

            list->AddItem(row);
        }

        // Bottom row: Load + Cancel
        auto* btnRow = dialog->CreateChild<UIElement>();
        btnRow->SetLayout(LM_HORIZONTAL, 6, IntRect(0, 4, 0, 0));
        btnRow->SetFixedHeight(28);
        auto makeAction = [&](const String& text) -> Button*
        {
            auto* btn = btnRow->CreateChild<Button>();
            btn->SetStyleAuto();
            btn->SetFixedSize(100, 26);
            auto* t = btn->CreateChild<Text>();
            t->SetFont(font_, 12);
            t->SetText(text);
            t->SetAlignment(HA_CENTER, VA_CENTER);
            return btn;
        };
        auto* btnLoad = makeAction("Load");
        auto* btnCancel = makeAction("Cancel");

        // Capture by value for lambda
        auto boxesCapture = boxes;
        SubscribeToEvent(btnAll, E_RELEASED, [boxesCapture](StringHash, VariantMap&)
        {
            for (auto* cb : boxesCapture) cb->SetChecked(true);
        });
        SubscribeToEvent(btnNone, E_RELEASED, [boxesCapture](StringHash, VariantMap&)
        {
            for (auto* cb : boxesCapture) cb->SetChecked(false);
        });

        SubscribeToEvent(btnLoad, E_RELEASED, [this, boxesCapture, filenames, folder, dialog](StringHash, VariantMap&)
        {
            int loaded = 0;
            for (unsigned i = 0; i < boxesCapture.Size() && i < filenames.Size(); ++i)
            {
                if (boxesCapture[i]->IsChecked())
                {
                    if (LoadAnimationFile(folder + filenames[i]))
                        ++loaded;
                }
            }
            URHO3D_LOGINFOF("Loaded %d animations", loaded);
            dialog->Remove();
        });

        SubscribeToEvent(btnCancel, E_RELEASED, [dialog](StringHash, VariantMap&)
        {
            dialog->Remove();
        });
    });
}

void ModelTool::HandleDropFile(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace DropFile;
    String path = eventData[P_FILENAME].GetString();
    if (path.EndsWith(".ani", false))
        LoadAnimationFile(path);
    else if (path.EndsWith(".mdl", false))
        LoadModel(path);
    else if (path.EndsWith(".fbx", false) || path.EndsWith(".obj", false) ||
             path.EndsWith(".gltf", false) || path.EndsWith(".glb", false) ||
             path.EndsWith(".dae", false) || path.EndsWith(".blend", false))
    {
        ImportModel(path);
    }
}

void ModelTool::PlayAnimation(int index)
{
    if (index < 0 || index >= (int)availableAnims_.Size()) return;

    currentAnimIndex_ = index;

    if (animController_)
    {
        animController_->PlayExclusive(availableAnims_[index], 0, animLooped_, 0.2f);
        float spd = animReversed_ ? -animSpeed_ : animSpeed_;
        animController_->SetSpeed(availableAnims_[index], spd);
        animPlaying_ = true;

        // Track as sole active animation on layer 0
        activeAnims_.Clear();
        ActiveAnim aa;
        aa.name = availableAnims_[index];
        aa.layer = 0;
        aa.weight = 1.0f;
        aa.blendMode = ABM_LERP;
        activeAnims_.Push(aa);
        RebuildActiveAnimDisplay();
    }

    if (animNameText_)
        animNameText_->SetText(GetFileName(availableAnims_[index]));

    // Update ListView selection (suppress handler to avoid recursion)
    if (animListView_)
    {
        suppressAnimSelect_ = true;
        animListView_->SetSelection(index);
        suppressAnimSelect_ = false;
    }

    RebuildInfoText();
    LoadLabelsForCurrentAnim();
    RebuildTextKeyList();
    RebuildTrackList();
    ClearPayloadFieldRows();
}

void ModelTool::AddBlendAnimation(int index)
{
    if (index < 0 || index >= (int)availableAnims_.Size() || !animController_)
        return;

    const String& animName = availableAnims_[index];

    // Check if already active
    for (unsigned i = 0; i < activeAnims_.Size(); ++i)
    {
        if (activeAnims_[i].name == animName)
        {
            URHO3D_LOGWARNINGF("Animation '%s' already blended on layer %d", GetFileName(animName).CString(), activeAnims_[i].layer);
            return;
        }
    }

    // Find next free layer
    unsigned char nextLayer = 0;
    for (unsigned i = 0; i < activeAnims_.Size(); ++i)
    {
        if (activeAnims_[i].layer >= nextLayer)
            nextLayer = activeAnims_[i].layer + 1;
    }

    animController_->Play(animName, nextLayer, animLooped_, 0.2f);
    float spd = animReversed_ ? -animSpeed_ : animSpeed_;
    animController_->SetSpeed(animName, spd);

    ActiveAnim aa;
    aa.name = animName;
    aa.layer = nextLayer;
    aa.weight = 1.0f;
    aa.blendMode = ABM_LERP;
    activeAnims_.Push(aa);

    URHO3D_LOGINFOF("Blended '%s' on layer %d", GetFileName(animName).CString(), nextLayer);
    RebuildActiveAnimDisplay();
}

void ModelTool::RemoveBlendAnimation(unsigned index)
{
    if (index >= activeAnims_.Size() || !animController_)
        return;

    animController_->Stop(activeAnims_[index].name, 0.2f);
    activeAnims_.Erase(index);
    RebuildActiveAnimDisplay();
}

void ModelTool::RebuildActiveAnimDisplay()
{
    if (!activeAnimContainer_) return;

    activeAnimContainer_->RemoveAllChildren();

    for (unsigned i = 0; i < activeAnims_.Size(); ++i)
    {
        ActiveAnim& aa = activeAnims_[i];

        auto* row = activeAnimContainer_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        row->SetFixedHeight(22);

        // Layer label
        auto* layerLbl = row->CreateChild<Text>();
        layerLbl->SetFont(font_, 10);
        layerLbl->SetText("L" + String((int)aa.layer));
        layerLbl->SetColor(Color(0.6f, 0.8f, 0.6f));
        layerLbl->SetMinWidth(22);

        // Animation name
        auto* nameLbl = row->CreateChild<Text>();
        nameLbl->SetFont(font_, 10);
        nameLbl->SetText(GetFileName(aa.name));
        nameLbl->SetColor(Color(0.9f, 0.85f, 0.5f));
        nameLbl->SetMinWidth(80);

        // Weight slider
        auto* wSlider = row->CreateChild<Slider>();
        wSlider->SetStyleAuto();
        wSlider->SetFixedSize(80, 14);
        wSlider->SetRange(1.0f);
        wSlider->SetValue(aa.weight);
        wSlider->SetVar("BlendIdx", (int)i);
        SubscribeToEvent(wSlider, E_SLIDERCHANGED, [this](StringHash, VariantMap& eventData)
        {
            auto* sl = static_cast<Slider*>(eventData[SliderChanged::P_ELEMENT].GetPtr());
            int idx = sl->GetVar("BlendIdx").GetI32();
            if (idx >= 0 && idx < (int)activeAnims_.Size() && animController_)
            {
                activeAnims_[idx].weight = eventData[SliderChanged::P_VALUE].GetFloat();
                animController_->SetWeight(activeAnims_[idx].name, activeAnims_[idx].weight);
            }
        });

        // Weight value label
        auto* wLbl = row->CreateChild<Text>();
        wLbl->SetFont(font_, 10);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.1f", aa.weight);
        wLbl->SetText(buf);
        wLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
        wLbl->SetMinWidth(28);

        // Additive toggle button
        auto* modeBtn = row->CreateChild<Button>();
        modeBtn->SetStyleAuto();
        modeBtn->SetFixedSize(32, 18);
        auto* modeLbl = modeBtn->CreateChild<Text>();
        modeLbl->SetFont(font_, 9);
        modeLbl->SetText(aa.blendMode == ABM_ADDITIVE ? "Add" : "Lerp");
        modeLbl->SetAlignment(HA_CENTER, VA_CENTER);
        modeBtn->SetVar("BlendIdx", (int)i);
        SubscribeToEvent(modeBtn, E_RELEASED, [this](StringHash, VariantMap& eventData)
        {
            auto* btn = static_cast<Button*>(eventData[Released::P_ELEMENT].GetPtr());
            int idx = btn->GetVar("BlendIdx").GetI32();
            if (idx >= 0 && idx < (int)activeAnims_.Size() && animController_)
            {
                ActiveAnim& a = activeAnims_[idx];
                a.blendMode = (a.blendMode == ABM_LERP) ? ABM_ADDITIVE : ABM_LERP;
                animController_->SetBlendMode(a.name, a.blendMode);
                RebuildActiveAnimDisplay();
            }
        });

        // Remove button
        auto* rmBtn = row->CreateChild<Button>();
        rmBtn->SetStyleAuto();
        rmBtn->SetFixedSize(20, 18);
        auto* rmLbl = rmBtn->CreateChild<Text>();
        rmLbl->SetFont(font_, 10);
        rmLbl->SetText("X");
        rmLbl->SetAlignment(HA_CENTER, VA_CENTER);
        rmBtn->SetVar("BlendIdx", (int)i);
        SubscribeToEvent(rmBtn, E_RELEASED, [this](StringHash, VariantMap& eventData)
        {
            auto* btn = static_cast<Button*>(eventData[Released::P_ELEMENT].GetPtr());
            int idx = btn->GetVar("BlendIdx").GetI32();
            if (idx >= 0 && idx < (int)activeAnims_.Size())
                RemoveBlendAnimation((unsigned)idx);
        });
    }
}

// ============================================================================
// UI
// ============================================================================

void ModelTool::CreateUI()
{
    CreateMenuBar();
    CreateInfoPanel();
    CreatePlaybackPanel();
    CreatePosePanel();
    CreateTextKeyPanel();
    CreateKeyframePanel();
    CreateAudioPanel();
    if (audioPanel_)
        audioPanel_->SetVisible(false);  // Hidden on startup — toggle from Tools menu

    auto* ui = GetSubsystem<UI>();

    // Status text (left)
    statusText_ = ui->GetRoot()->CreateChild<Text>();
    statusText_->SetFont(font_, 14);
    statusText_->SetColor(Color(0.7f, 0.7f, 0.7f));
    statusText_->SetHorizontalAlignment(HA_LEFT);
    statusText_->SetVerticalAlignment(VA_BOTTOM);
    statusText_->SetPosition(8, -8);
    statusText_->SetText("No model — File > Open or -model <path>");

    // Status bar (right) — context-sensitive info
    statusBarText_ = ui->GetRoot()->CreateChild<Text>();
    statusBarText_->SetFont(font_, 11);
    statusBarText_->SetColor(Color(0.6f, 0.7f, 0.6f));
    statusBarText_->SetHorizontalAlignment(HA_RIGHT);
    statusBarText_->SetVerticalAlignment(VA_BOTTOM);
    statusBarText_->SetPosition(-8, -8);

    RefreshViewMenuItems();
}

void ModelTool::CreateMenuBar()
{
    auto* ui = GetSubsystem<UI>();
    auto* graphics = GetSubsystem<Graphics>();

    auto* bar = ui->GetRoot()->CreateChild<BorderImage>("MenuBar");
    bar->SetLayout(LM_HORIZONTAL, 2, IntRect(2, 2, 2, 2));
    bar->SetFixedHeight(28);
    bar->SetFixedWidth(graphics->GetWidth());
    bar->SetHorizontalAlignment(HA_LEFT);
    bar->SetVerticalAlignment(VA_TOP);
    bar->SetColor(Color(0.12f, 0.12f, 0.15f, 0.9f));

    // Helper: create a styled dropdown menu item
    auto makeItem = [this](const String& label) -> Text*
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 12);
        item->SetText(label);
        item->SetStyleAuto();
        item->SetMinWidth(100);
        item->SetMinHeight(22);
        item->SetColor(Color(0.9f, 0.9f, 0.9f));
        return item;
    };

    // Helper: create a placeholder (the always-visible label in the bar)
    auto makePlaceholder = [this](const String& label) -> Text*
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 12);
        item->SetText(label);
        item->SetStyleAuto();
        item->SetColor(Color(0.8f, 0.8f, 0.85f));
        return item;
    };

    // Helper: create a dropdown with consistent styling
    auto makeDropDown = [&](int width) -> DropDownList*
    {
        auto* dd = bar->CreateChild<DropDownList>();
        dd->SetStyleAuto();
        dd->SetFixedSize(width, 24);
        dd->SetResizePopup(true);
        // Darken the dropdown button to match bar
        dd->SetColor(Color(0.18f, 0.18f, 0.22f));
        // Style the popup background so items have a visible container
        auto* popup = dd->GetPopup();
        if (popup)
            popup->SetColor(Color(0.16f, 0.16f, 0.20f, 0.95f));
        return dd;
    };

    // ---- File menu ----
    auto* fileMenu = makeDropDown(120);
    fileMenu->AddItem(makePlaceholder("File"));       // 0
    fileMenu->AddItem(makeItem("Open Model"));        // 1
    fileMenu->AddItem(makeItem("Open Folder"));       // 2
    fileMenu->AddItem(makeItem("Import FBX..."));     // 3
    fileMenu->AddItem(makeItem("Load Animations"));   // 4
    fileMenu->AddItem(makeItem("Save Model"));        // 5
    fileMenu->AddItem(makeItem("Export FBX"));        // 6
    fileMenu->AddItem(makeItem("Quit"));              // 7
    fileMenu->SetSelection(0);

    SubscribeToEvent(fileMenu, E_ITEMSELECTED, [this, fileMenu](StringHash, VariantMap& eventData)
    {
        int sel = eventData[ItemSelected::P_SELECTION].GetI32();
        if (sel == 1)
        {
            auto* ui = GetSubsystem<UI>();
            auto* style = ui->GetRoot()->GetDefaultStyle();
            auto* fs = GetSubsystem<FileSystem>();

            fileSelector_ = new FileSelector(context_);
            fileSelector_->SetDefaultStyle(style);
            fileSelector_->SetTitle("Open Model");
            fileSelector_->SetButtonTexts("Open", "Cancel");

            Vector<String> filters;
            filters.Push("*.mdl");
            fileSelector_->SetFilters(filters, 0);

            // Prefer last-loaded model file (full path)
            String lastPath = LoadLastModelDir();
            String startDir;
            String startFile;
            if (!lastPath.Empty() && fs->FileExists(lastPath))
            {
                startDir = GetPath(lastPath);
                startFile = GetFileNameAndExtension(lastPath);
            }
            else if (!lastPath.Empty() && fs->DirExists(lastPath))
            {
                // Legacy: file used to store just the dir
                startDir = lastPath;
            }
            if (startDir.Empty())
            {
                auto* cache = GetSubsystem<ResourceCache>();
                const Vector<String>& dirs = cache->GetResourceDirs();
                for (unsigned i = 0; i < dirs.Size(); ++i)
                {
                    if (fs->DirExists(dirs[i] + "Models/"))
                    {
                        startDir = dirs[i] + "Models/";
                        if (dirs[i].Contains("/Data/") || dirs[i].EndsWith("/Data"))
                            break;
                    }
                }
            }
            if (!startDir.Empty())
                fileSelector_->SetPath(startDir);
            if (!startFile.Empty())
                fileSelector_->SetFileName(startFile);
            SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(ModelTool, HandleFileOpen));
        }
        else if (sel == 2)
        {
            // Open Folder — use file selector to pick any .mdl, then scan its parent directory
            auto* ui = GetSubsystem<UI>();
            auto* style = ui->GetRoot()->GetDefaultStyle();
            auto* fs = GetSubsystem<FileSystem>();

            fileSelector_ = new FileSelector(context_);
            fileSelector_->SetDefaultStyle(style);
            fileSelector_->SetTitle("Open Folder (pick any .mdl in target folder)");
            fileSelector_->SetButtonTexts("Open", "Cancel");

            Vector<String> filters;
            filters.Push("*.mdl");
            fileSelector_->SetFilters(filters, 0);

            auto* cache = GetSubsystem<ResourceCache>();
            const Vector<String>& dirs = cache->GetResourceDirs();
            for (unsigned i = 0; i < dirs.Size(); ++i)
            {
                if (fs->DirExists(dirs[i] + "Models/"))
                {
                    fileSelector_->SetPath(dirs[i] + "Models/");
                    break;
                }
            }

            SubscribeToEvent(fileSelector_, E_FILESELECTED, [this](StringHash, VariantMap& ed)
            {
                String path = ed[FileSelected::P_FILENAME].GetString();
                bool ok = ed[FileSelected::P_OK].GetBool();
                fileSelector_.Reset();
                if (!ok || path.Empty()) return;
                ScanFolder(GetPath(path));
            });
        }
        else if (sel == 3)
            ShowImportDialog();
        else if (sel == 4)
            ShowLoadAnimationsDialog();
        else if (sel == 5)
            SaveModel();
        else if (sel == 6)
            ExportFBX();
        else if (sel == 7)
            engine_->Exit();
        fileMenu->SetSelection(0);
    });

    // ---- View menu ----
    auto* viewMenu = makeDropDown(150);
    viewMenu->AddItem(makePlaceholder("View"));     // 0
    viewMenu->AddItem(makeItem("Wireframe"));       // 1
    viewMenu->AddItem(makeItem("Skeleton"));        // 2
    viewMenu->AddItem(makeItem("Bounding Box"));    // 3
    viewMenu->AddItem(makeItem("Reset Camera"));    // 4
    viewMenu->AddItem(makeItem("Toggle Info"));     // 5
    viewMenu->AddItem(makeItem("Vertex Editor"));   // 6 — hidden when no model
    viewMenu->AddItem(makeItem("Pose Studio"));     // 7 — hidden when not animated
    viewMenu->AddItem(makeItem("Text Keys"));       // 8 — hidden when not animated
    viewMenu->SetSelection(0);
    viewMenu_ = viewMenu;

    SubscribeToEvent(viewMenu, E_ITEMSELECTED, [this, viewMenu](StringHash, VariantMap& eventData)
    {
        int sel = eventData[ItemSelected::P_SELECTION].GetI32();
        if (sel == 1)
        {
            showWireframe_ = !showWireframe_;
            auto* camera = cameraNode_->GetComponent<Camera>();
            if (camera) camera->SetFillMode(showWireframe_ ? FILL_WIREFRAME : FILL_SOLID);
        }
        else if (sel == 2)
            showSkeleton_ = !showSkeleton_;
        else if (sel == 3)
            showBoundingBox_ = !showBoundingBox_;
        else if (sel == 4)
            AutoFrameCamera();
        else if (sel == 5)
        {
            if (infoPanel_) infoPanel_->SetVisible(!infoPanel_->IsVisible());
        }
        else if (sel == 6)
        {
            if (currentModel_)
            {
                if (!vertexEditMode_)
                    EnterVertexEditMode();
                else
                    ExitVertexEditMode();
            }
        }
        else if (sel == 7)
        {
            if (posePanel_ && isAnimated_) posePanel_->SetVisible(!posePanel_->IsVisible());
        }
        else if (sel == 8)
        {
            if (isAnimated_) ToggleTextKeyEditorWindow();
        }
        viewMenu->SetSelection(0);
    });

    // ---- Tools menu ----
    auto* toolsMenu = makeDropDown(160);
    toolsMenu->AddItem(makePlaceholder("Tools"));          // 0
    toolsMenu->AddItem(makeItem("Audio Capture"));         // 1
    toolsMenu->AddItem(makeItem("Load Slot B"));           // 2
    toolsMenu->AddItem(makeItem("Remove Slot B"));         // 3
    toolsMenu->AddItem(makeItem("Switch Active (Tab)"));   // 4
    toolsMenu->SetSelection(0);

    SubscribeToEvent(toolsMenu, E_ITEMSELECTED, [this, toolsMenu](StringHash, VariantMap& eventData)
    {
        int sel = eventData[ItemSelected::P_SELECTION].GetI32();
        if (sel == 1)
        {
            if (audioPanel_)
                audioPanel_->SetVisible(!audioPanel_->IsVisible());
        }
        else if (sel == 2)
            LoadReferenceModel();
        else if (sel == 3)
            RemoveReferenceModel();
        else if (sel == 4)
            SwapActiveModel();
        toolsMenu->SetSelection(0);
    });

    // ---- Help menu ----
    auto* helpMenu = makeDropDown(120);
    helpMenu->AddItem(makePlaceholder("Help"));        // 0
    helpMenu->AddItem(makeItem("Instructions"));       // 1
    helpMenu->SetSelection(0);

    SubscribeToEvent(helpMenu, E_ITEMSELECTED, [this, helpMenu](StringHash, VariantMap& eventData)
    {
        int sel = eventData[ItemSelected::P_SELECTION].GetI32();
        if (sel == 1)
            ShowHelpWindow();
        helpMenu->SetSelection(0);
    });

    // ---- Browse status text (right side of menu bar) ----
    browseStatusText_ = bar->CreateChild<Text>("BrowseStatus");
    browseStatusText_->SetFont(font_, 12);
    browseStatusText_->SetColor(Color(0.7f, 0.85f, 0.7f));
    browseStatusText_->SetText("");
    browseStatusText_->SetHorizontalAlignment(HA_RIGHT);

}

void ModelTool::RefreshViewMenuItems()
{
    if (!viewMenu_) return;
    bool hasModel = currentModel_ != nullptr;
    // Item 6: Vertex Editor — needs a model
    if (viewMenu_->GetNumItems() > 6)
    {
        auto* item = viewMenu_->GetItem(6);
        if (item) item->SetColor(hasModel ? Color(0.9f, 0.9f, 0.9f) : Color(0.4f, 0.4f, 0.4f));
    }
    // Item 7: Pose Studio — needs animated model
    if (viewMenu_->GetNumItems() > 7)
    {
        auto* item = viewMenu_->GetItem(7);
        if (item) item->SetColor(isAnimated_ ? Color(0.9f, 0.9f, 0.9f) : Color(0.4f, 0.4f, 0.4f));
    }
    // Item 8: Text Keys — needs animated model
    if (viewMenu_->GetNumItems() > 8)
    {
        auto* item = viewMenu_->GetItem(8);
        if (item) item->SetColor(isAnimated_ ? Color(0.9f, 0.9f, 0.9f) : Color(0.4f, 0.4f, 0.4f));
    }
}

void ModelTool::CreateInfoPanel()
{
    auto* ui = GetSubsystem<UI>();
    auto* graphics = GetSubsystem<Graphics>();

    // Window container
    infoPanel_ = ui->GetRoot()->CreateChild<Window>("InfoPanel");
    infoPanel_->SetStyleAuto();
    infoPanel_->SetPosition(graphics->GetWidth() - 420, 36);
    infoPanel_->SetSize(400, graphics->GetHeight() - 120);
    infoPanel_->SetMovable(true);
    infoPanel_->SetResizable(true);
    infoPanel_->SetLayout(LM_VERTICAL, 0, IntRect(0, 0, 0, 0));
    infoPanel_->SetOpacity(0.92f);
    infoPanel_->SetColor(Color(0.20f, 0.20f, 0.25f));

    // Title bar — distinct from content, acts as drag handle
    auto* titleBar = infoPanel_->CreateChild<BorderImage>("TitleBar");
    titleBar->SetLayout(LM_HORIZONTAL, 4, IntRect(8, 4, 4, 4));
    titleBar->SetFixedHeight(28);
    titleBar->SetColor(Color(0.14f, 0.14f, 0.18f));

    auto* title = titleBar->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Model Info");
    title->SetColor(Color(0.9f, 0.9f, 0.4f));
    title->SetAlignment(HA_LEFT, VA_CENTER);

    // Spacer to push close button right
    auto* spacer = titleBar->CreateChild<UIElement>();
    spacer->SetMinWidth(10);
    spacer->SetLayoutFlexScale(Vector2(1.0f, 0.0f));

    // Close button
    auto* closeBtn = titleBar->CreateChild<Button>();
    closeBtn->SetStyleAuto();
    closeBtn->SetFixedSize(22, 20);
    closeBtn->SetColor(Color(0.4f, 0.15f, 0.15f));
    auto* closeLbl = closeBtn->CreateChild<Text>();
    closeLbl->SetFont(font_, 12);
    closeLbl->SetText("X");
    closeLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (infoPanel_) infoPanel_->SetVisible(false);
    });

    // Separator line
    auto* sep = infoPanel_->CreateChild<BorderImage>();
    sep->SetFixedHeight(1);
    sep->SetColor(Color(0.35f, 0.35f, 0.4f));

    // ScrollView for collapsible sections
    infoScroll_ = infoPanel_->CreateChild<ScrollView>();
    infoScroll_->SetStyleAuto();
    infoScroll_->SetMinSize(380, graphics->GetHeight() - 200);

    infoContent_ = infoScroll_->CreateChild<UIElement>();
    infoContent_->SetLayout(LM_VERTICAL, 2, IntRect(6, 4, 6, 4));
    infoContent_->SetMinWidth(370);
    infoScroll_->SetContentElement(infoContent_);

    auto* placeholder = infoContent_->CreateChild<Text>();
    placeholder->SetFont(font_, 11);
    placeholder->SetText("No model loaded.");
    placeholder->SetColor(Color(0.6f, 0.6f, 0.6f));
}

UIElement* ModelTool::CreateCollapsibleSection(const String& title, bool startExpanded)
{
    if (!infoContent_) return nullptr;

    // Separator between sections
    if (infoContent_->GetNumChildren() > 0)
    {
        auto* sep = infoContent_->CreateChild<BorderImage>();
        sep->SetFixedHeight(1);
        sep->SetColor(Color(0.30f, 0.30f, 0.35f, 0.5f));
    }

    // Header button
    auto* header = infoContent_->CreateChild<Button>();
    header->SetStyleAuto();
    header->SetFixedHeight(24);
    header->SetLayout(LM_HORIZONTAL, 4, IntRect(6, 0, 6, 0));
    header->SetColor(startExpanded ? Color(0.28f, 0.28f, 0.34f) : Color(0.15f, 0.15f, 0.19f));

    auto* headerText = header->CreateChild<Text>();
    headerText->SetFont(font_, 12);
    headerText->SetText(String(startExpanded ? "v " : "> ") + title);
    headerText->SetColor(startExpanded ? Color(0.95f, 0.9f, 0.45f) : Color(0.6f, 0.55f, 0.3f));
    headerText->SetAlignment(HA_LEFT, VA_CENTER);

    // Content container
    auto* content = infoContent_->CreateChild<UIElement>();
    content->SetLayout(LM_VERTICAL, 1, IntRect(8, 2, 4, 4));
    content->SetVisible(startExpanded);

    // Store refs in button vars for toggle
    header->SetVar("SectionContent", content);
    header->SetVar("SectionTitle", title);

    SubscribeToEvent(header, E_RELEASED, [this](StringHash, VariantMap& eventData)
    {
        auto* btn = static_cast<Button*>(eventData[Released::P_ELEMENT].GetPtr());
        auto* content = static_cast<UIElement*>(btn->GetVar("SectionContent").GetPtr());
        if (!content) return;

        bool expanded = !content->IsVisible();
        content->SetVisible(expanded);

        // Depth coloring: expanded = bright (active), collapsed = dark (unreached)
        btn->SetColor(expanded ? Color(0.28f, 0.28f, 0.34f) : Color(0.15f, 0.15f, 0.19f));

        String title = btn->GetVar("SectionTitle").GetString();
        auto* txt = btn->GetChildStaticCast<Text>(0);
        if (txt)
        {
            txt->SetText(String(expanded ? "v " : "> ") + title);
            txt->SetColor(expanded ? Color(0.95f, 0.9f, 0.45f) : Color(0.6f, 0.55f, 0.3f));
        }

        // Force layout refresh
        if (infoContent_)
        {
            infoContent_->SetHeight(0);
            infoContent_->UpdateLayout();
        }
    });

    currentSection_ = content;
    return content;
}

void ModelTool::AddInfoLine(const String& text, const Color& color)
{
    UIElement* target = currentSection_ ? currentSection_ : infoContent_;
    if (!target) return;

    auto* line = target->CreateChild<Text>();
    line->SetFont(font_, 11);
    line->SetText(text);
    line->SetColor(color);
}

void ModelTool::AddInfoSeparator()
{
    UIElement* target = currentSection_ ? currentSection_ : infoContent_;
    if (!target) return;

    auto* sep = target->CreateChild<BorderImage>();
    sep->SetFixedHeight(1);
    sep->SetColor(Color(0.35f, 0.35f, 0.4f, 0.4f));
}

void ModelTool::ShowHelpWindow()
{
    // Toggle - if already visible, close it
    if (helpWindow_ && helpWindow_->IsVisible())
    {
        helpWindow_->SetVisible(false);
        return;
    }

    auto* ui = GetSubsystem<UI>();

    if (!helpWindow_)
    {
        helpWindow_ = ui->GetRoot()->CreateChild<Window>("HelpWindow");
        helpWindow_->SetStyleAuto();
        helpWindow_->SetLayout(LM_VERTICAL, 4, IntRect(12, 8, 12, 8));
        helpWindow_->SetSize(420, 540);
        helpWindow_->SetMovable(true);
        helpWindow_->SetOpacity(0.95f);
        helpWindow_->SetColor(Color(0.30f, 0.30f, 0.36f));
        helpWindow_->SetAlignment(HA_CENTER, VA_CENTER);

        auto* title = helpWindow_->CreateChild<Text>();
        title->SetFont(font_, 15);
        title->SetText("ModelTool Instructions");
        title->SetColor(Color(0.9f, 0.9f, 0.4f));

        auto addLine = [this](UIElement* parent, const String& text, const Color& color = Color(0.85f, 0.85f, 0.85f))
        {
            auto* line = parent->CreateChild<Text>();
            line->SetFont(font_, 12);
            line->SetText(text);
            line->SetColor(color);
        };

        addLine(helpWindow_, "");
        addLine(helpWindow_, "MOUSE", Color(0.7f, 0.9f, 0.4f));
        addLine(helpWindow_, "  Right-click drag    Orbit camera");
        addLine(helpWindow_, "  Scroll wheel        Zoom in/out");
        addLine(helpWindow_, "  Middle-click drag   Rotate light");
        addLine(helpWindow_, "");
        addLine(helpWindow_, "KEYBOARD", Color(0.7f, 0.9f, 0.4f));
        addLine(helpWindow_, "  F                   Toggle wireframe");
        addLine(helpWindow_, "  S                   Toggle skeleton");
        addLine(helpWindow_, "  B                   Toggle bounding box");
        addLine(helpWindow_, "  R                   Reset camera");
        addLine(helpWindow_, "  Tab                 Toggle info panel");
        addLine(helpWindow_, "  Space               Play / Pause animation");
        addLine(helpWindow_, "  [ / ]               Prev / Next animation");
        addLine(helpWindow_, "  Escape              Quit");
        addLine(helpWindow_, "");
        addLine(helpWindow_, "ANIMATION PANEL", Color(0.7f, 0.9f, 0.4f));
        addLine(helpWindow_, "  Click list item     Switch animation");
        addLine(helpWindow_, "  Loop checkbox       Toggle looping");
        addLine(helpWindow_, "  Fwd/Rev button      Toggle reverse playback");
        addLine(helpWindow_, "  + Blend             Add anim on next layer");
        addLine(helpWindow_, "  Weight slider       Per-layer blend weight");
        addLine(helpWindow_, "  Lerp/Add toggle     Blend mode per layer");
        addLine(helpWindow_, "  X button            Remove blended anim");
        addLine(helpWindow_, "");
        addLine(helpWindow_, "VERTEX EDITOR", Color(0.7f, 0.9f, 0.4f));
        addLine(helpWindow_, "  V                   Toggle vertex edit mode");
        addLine(helpWindow_, "  LMB click           Select nearest vertex");
        addLine(helpWindow_, "  LMB drag            Move selected vertex");
        addLine(helpWindow_, "  DEL                 Delete vertex + triangles");
        addLine(helpWindow_, "  Ctrl+S              Save edited model");
        addLine(helpWindow_, "");

        auto* closeBtn = helpWindow_->CreateChild<Button>();
        closeBtn->SetStyleAuto();
        closeBtn->SetFixedSize(80, 26);
        closeBtn->SetHorizontalAlignment(HA_CENTER);
        auto* closeLbl = closeBtn->CreateChild<Text>();
        closeLbl->SetFont(font_, 13);
        closeLbl->SetText("Close");
        closeLbl->SetAlignment(HA_CENTER, VA_CENTER);
        SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&)
        {
            if (helpWindow_) helpWindow_->SetVisible(false);
        });
    }

    helpWindow_->SetVisible(true);
    helpWindow_->BringToFront();
}

void ModelTool::CreatePlaybackPanel()
{
    auto* ui = GetSubsystem<UI>();
    auto* graphics = GetSubsystem<Graphics>();

    playbackPanel_ = ui->GetRoot()->CreateChild<Window>("PlaybackPanel");
    playbackPanel_->SetStyleAuto();
    playbackPanel_->SetFixedWidth(340);
    playbackPanel_->SetLayout(LM_VERTICAL, 4, IntRect(8, 6, 8, 6));
    playbackPanel_->SetPosition(8, 36);
    playbackPanel_->SetMovable(true);
    playbackPanel_->SetOpacity(0.92f);
    playbackPanel_->SetColor(Color(0.20f, 0.20f, 0.25f));
    playbackPanel_->SetVisible(false);

    // Title
    auto* title = playbackPanel_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Animation");
    title->SetColor(Color(0.9f, 0.9f, 0.4f));

    // Animation list
    animListView_ = playbackPanel_->CreateChild<ListView>();
    animListView_->SetStyleAuto();
    animListView_->SetHighlightMode(HM_ALWAYS);
    animListView_->SetMinHeight(120);
    animListView_->SetMaxHeight(200);
    SubscribeToEvent(animListView_, E_ITEMSELECTED, URHO3D_HANDLER(ModelTool, HandleAnimSelected));

    // Current animation name
    animNameText_ = playbackPanel_->CreateChild<Text>();
    animNameText_->SetFont(font_, 12);
    animNameText_->SetColor(Color(0.9f, 0.7f, 0.3f));
    animNameText_->SetText("(none)");

    // Animation navigation row: Prev | Next (Play/Pause moved to scrubber row)
    auto* btnRow = playbackPanel_->CreateChild<UIElement>();
    btnRow->SetLayout(LM_HORIZONTAL, 4);
    btnRow->SetFixedHeight(28);

    auto* prevBtn = btnRow->CreateChild<Button>();
    prevBtn->SetStyleAuto();
    prevBtn->SetFixedSize(70, 26);
    auto* prevLbl = prevBtn->CreateChild<Text>();
    prevLbl->SetFont(font_, 12);
    prevLbl->SetText("<< Prev");
    prevLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(prevBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (availableAnims_.Empty()) return;
        int idx = currentAnimIndex_ - 1;
        if (idx < 0) idx = (int)availableAnims_.Size() - 1;
        PlayAnimation(idx);
    });

    auto* nextBtn = btnRow->CreateChild<Button>();
    nextBtn->SetStyleAuto();
    nextBtn->SetFixedSize(70, 26);
    auto* nextLbl = nextBtn->CreateChild<Text>();
    nextLbl->SetFont(font_, 12);
    nextLbl->SetText("Next >>");
    nextLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(nextBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (availableAnims_.Empty()) return;
        int idx = (currentAnimIndex_ + 1) % (int)availableAnims_.Size();
        PlayAnimation(idx);
    });

    // Spacer pushes Loop+Fwd/Rev to the right edge
    auto* spacer = btnRow->CreateChild<UIElement>();
    spacer->SetMinWidth(0);
    spacer->SetLayoutFlexScale(Vector2(1.0f, 1.0f));

    // Loop checkbox
    loopCheck_ = btnRow->CreateChild<CheckBox>();
    loopCheck_->SetStyleAuto();
    loopCheck_->SetChecked(true);
    SubscribeToEvent(loopCheck_, E_TOGGLED, [this](StringHash, VariantMap&)
    {
        animLooped_ = loopCheck_->IsChecked();
        if (animController_ && currentAnimIndex_ >= 0)
            animController_->SetLooped(availableAnims_[currentAnimIndex_], animLooped_);
    });
    auto* loopLblBtn = btnRow->CreateChild<Text>();
    loopLblBtn->SetFont(font_, 12);
    loopLblBtn->SetText("Loop");
    loopLblBtn->SetColor(Color(0.85f, 0.85f, 0.85f));
    loopLblBtn->SetVerticalAlignment(VA_CENTER);

    // Reverse button
    auto* revBtn = btnRow->CreateChild<Button>();
    revBtn->SetStyleAuto();
    revBtn->SetFixedSize(55, 24);
    reverseLabel_ = revBtn->CreateChild<Text>();
    reverseLabel_->SetFont(font_, 11);
    reverseLabel_->SetText("Fwd");
    reverseLabel_->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(revBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        animReversed_ = !animReversed_;
        if (reverseLabel_) reverseLabel_->SetText(animReversed_ ? "Rev" : "Fwd");
        if (animController_ && currentAnimIndex_ >= 0 && animPlaying_)
        {
            float spd = animReversed_ ? -animSpeed_ : animSpeed_;
            animController_->SetSpeed(availableAnims_[currentAnimIndex_], spd);
        }
    });

    // Animation time
    animTimeText_ = playbackPanel_->CreateChild<Text>();
    animTimeText_->SetFont(font_, 12);
    animTimeText_->SetColor(Color(0.9f, 0.9f, 0.5f));
    animTimeText_->SetText("Time: 0.00 / 0.00");

    // Scrubber row: [<<] [Play/Pause] [>>] [---slider---]
    auto* scrubRow = playbackPanel_->CreateChild<UIElement>();
    scrubRow->SetLayout(LM_HORIZONTAL, 4, IntRect(0, 0, 0, 0));
    scrubRow->SetFixedHeight(22);

    auto makeIconBtn = [&](const String& glyph) -> Button*
    {
        auto* btn = scrubRow->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetFixedSize(28, 22);
        auto* t = btn->CreateChild<Text>();
        t->SetFont(font_, 13);
        t->SetText(glyph);
        t->SetAlignment(HA_CENTER, VA_CENTER);
        return btn;
    };

    auto* prevFrameBtn = makeIconBtn("<");
    playPauseBtn_ = makeIconBtn("||");  // starts playing → show pause
    auto* nextFrameBtn = makeIconBtn(">");

    auto stepKeyframe = [this](int delta)
    {
        if (!animController_ || currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
            return;
        const String& animPath = availableAnims_[currentAnimIndex_];
        auto* cache = GetSubsystem<ResourceCache>();
        auto* anim = cache->GetResource<Animation>(animPath, false);
        if (!anim || anim->GetNumTracks() == 0) return;
        AnimationTrack* track = anim->GetTrack((unsigned)0);
        if (!track || track->GetNumKeyFrames() == 0) return;

        float t = animController_->GetTime(animPath);
        i32 idx = 0;
        track->GetKeyFrameIndex(t, idx);
        int total = (int)track->GetNumKeyFrames();

        int newIdx = (int)idx + delta;
        if (newIdx < 0) newIdx = animLooped_ ? (total - 1) : 0;
        else if (newIdx >= total) newIdx = animLooped_ ? 0 : (total - 1);

        AnimationKeyFrame* kf = track->GetKeyFrame(newIdx);
        if (kf)
            animController_->SetTime(animPath, kf->time_);

        animPlaying_ = false;
        animController_->SetSpeed(animPath, 0.0f);
        if (playPauseBtn_)
        {
            auto* lbl = playPauseBtn_->GetChildStaticCast<Text>(0);
            if (lbl) lbl->SetText(">");
        }
    };

    SubscribeToEvent(prevFrameBtn, E_RELEASED, [stepKeyframe](StringHash, VariantMap&) { stepKeyframe(-1); });
    SubscribeToEvent(nextFrameBtn, E_RELEASED, [stepKeyframe](StringHash, VariantMap&) { stepKeyframe(+1); });

    SubscribeToEvent(playPauseBtn_, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (!animController_ || currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
            return;
        animPlaying_ = !animPlaying_;
        const String& anim = availableAnims_[currentAnimIndex_];
        float spd = animReversed_ ? -animSpeed_ : animSpeed_;
        animController_->SetSpeed(anim, animPlaying_ ? spd : 0.0f);
        if (playPauseBtn_)
        {
            auto* lbl = playPauseBtn_->GetChildStaticCast<Text>(0);
            if (lbl) lbl->SetText(animPlaying_ ? "||" : ">");
        }
    });

    animSlider_ = scrubRow->CreateChild<Slider>();
    animSlider_->SetStyleAuto();
    animSlider_->SetFixedHeight(18);
    animSlider_->SetRange(1.0f);
    SubscribeToEvent(animSlider_, E_SLIDERCHANGED, URHO3D_HANDLER(ModelTool, HandleAnimSlider));

    // Speed
    animSpeedText_ = playbackPanel_->CreateChild<Text>();
    animSpeedText_->SetFont(font_, 12);
    animSpeedText_->SetColor(Color(0.7f, 0.9f, 0.7f));
    animSpeedText_->SetText("Speed: 1.0x");

    speedSlider_ = playbackPanel_->CreateChild<Slider>();
    speedSlider_->SetStyleAuto();
    speedSlider_->SetFixedHeight(18);
    speedSlider_->SetRange(1.0f);
    speedSlider_->SetValue(0.31f);
    SubscribeToEvent(speedSlider_, E_SLIDERCHANGED, URHO3D_HANDLER(ModelTool, HandleSpeedSlider));

    // --- Blend button + active animations ---
    auto* blendRow = playbackPanel_->CreateChild<UIElement>();
    blendRow->SetLayout(LM_HORIZONTAL, 4);
    blendRow->SetFixedHeight(26);

    auto* blendBtn = blendRow->CreateChild<Button>();
    blendBtn->SetStyleAuto();
    blendBtn->SetFixedSize(90, 24);
    auto* blendLbl = blendBtn->CreateChild<Text>();
    blendLbl->SetFont(font_, 12);
    blendLbl->SetText("+ Blend");
    blendLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(blendBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (animListView_)
        {
            int sel = animListView_->GetSelection();
            if (sel >= 0 && sel < (int)availableAnims_.Size())
                AddBlendAnimation(sel);
        }
    });

    auto* clearBtn = blendRow->CreateChild<Button>();
    clearBtn->SetStyleAuto();
    clearBtn->SetFixedSize(90, 24);
    auto* clearLbl = clearBtn->CreateChild<Text>();
    clearLbl->SetFont(font_, 12);
    clearLbl->SetText("Clear All");
    clearLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(clearBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (animController_) animController_->StopAll(0.2f);
        activeAnims_.Clear();
        RebuildActiveAnimDisplay();
    });

    // Active animations container
    activeAnimContainer_ = playbackPanel_->CreateChild<UIElement>();
    activeAnimContainer_->SetLayout(LM_VERTICAL, 2);
}

// ============================================================================
// Text Key Schema Templates
// ============================================================================

static VariantType ParseVariantTypeName(const String& typeName)
{
    if (typeName == "String") return VAR_STRING;
    if (typeName == "Int") return VAR_INT;
    if (typeName == "Float") return VAR_FLOAT;
    if (typeName == "Bool") return VAR_BOOL;
    if (typeName == "Vector3") return VAR_VECTOR3;
    if (typeName == "Map" || typeName == "VariantMap") return VAR_VARIANTMAP;
    if (typeName == "Vector" || typeName == "VariantVector") return VAR_VARIANTVECTOR;
    if (typeName == "ResourceRef" || typeName == "ResRef") return VAR_RESOURCEREF;
    return VAR_STRING;
}

// Resource type names for the ResourceRef sub-dropdown
static const char* RESOURCE_TYPE_NAMES[] = { "Material", "ParticleEffect", "Sound", "Texture2D", "Model", "Animation", "XMLFile" };
static const int RESOURCE_TYPE_COUNT = 7;

static int VariantTypeToPayloadIndex(VariantType vt)
{
    // Must match PAYLOAD_TYPE_NAMES/PAYLOAD_TYPES order defined below CreateTextKeyPanel
    switch (vt)
    {
    case VAR_STRING: return 0;
    case VAR_INT: return 1;
    case VAR_FLOAT: return 2;
    case VAR_BOOL: return 3;
    case VAR_VECTOR3: return 4;
    case VAR_VARIANTMAP: return 5;
    case VAR_VARIANTVECTOR: return 6;
    case VAR_RESOURCEREF: return 7;
    default: return 0;
    }
}

static Variant ParseDefaultValue(const JSONValue& val, VariantType vt)
{
    switch (vt)
    {
    case VAR_STRING:
        return val.IsString() ? Variant(val.GetString()) : Variant(String::EMPTY);
    case VAR_INT:
        return val.IsNumber() ? Variant((int)val.GetI32()) : Variant(0);
    case VAR_FLOAT:
        return val.IsNumber() ? Variant((float)val.GetDouble()) : Variant(0.0f);
    case VAR_BOOL:
        return val.IsBool() ? Variant(val.GetBool()) : Variant(false);
    case VAR_VECTOR3:
        if (val.IsArray() && val.GetArray().Size() >= 3)
        {
            const JSONArray& arr = val.GetArray();
            return Variant(Vector3((float)arr[0].GetDouble(), (float)arr[1].GetDouble(), (float)arr[2].GetDouble()));
        }
        return Variant(Vector3::ZERO);
    case VAR_VARIANTMAP:
        return Variant(VariantMap());
    case VAR_VARIANTVECTOR:
        return Variant(VariantVector());
    case VAR_RESOURCEREF:
    {
        String path = val.IsString() ? val.GetString() : String::EMPTY;
        return Variant(ResourceRef(StringHash::ZERO, path));
    }
    default:
        return Variant::EMPTY;
    }
}

void ModelTool::LoadTextKeyTemplates()
{
    textKeyTemplates_.Clear();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();

    // Scan all resource dirs for TextKeyTemplates/*.json
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (const String& dir : dirs)
    {
        String templateDir = dir + "TextKeyTemplates/";
        if (!fs->DirExists(templateDir))
            continue;

        StringVector files;
        fs->ScanDir(files, templateDir, "*.json", SCAN_FILES, false);

        for (const String& fileName : files)
        {
            String fullPath = templateDir + fileName;
            JSONFile jf(context_);
            File f(context_, fullPath, FILE_READ);
            if (!f.IsOpen() || !jf.Load(f))
            {
                URHO3D_LOGWARNINGF("Failed to load template: %s", fullPath.CString());
                continue;
            }

            const JSONValue& root = jf.GetRoot();

            TextKeyTemplate tmpl;
            tmpl.name = root.Get("name").GetString();
            if (tmpl.name.Empty())
                tmpl.name = GetFileName(fileName);
            tmpl.category = root.Get("category").GetString();
            tmpl.description = root.Get("description").GetString();

            // Parse color from hex "#RRGGBB"
            String colorStr = root.Get("color").GetString();
            if (colorStr.StartsWith("#") && colorStr.Length() >= 7)
            {
                unsigned r = ToU32(colorStr.Substring(1, 2), 16);
                unsigned g = ToU32(colorStr.Substring(3, 2), 16);
                unsigned b = ToU32(colorStr.Substring(5, 2), 16);
                tmpl.color = Color(r / 255.0f, g / 255.0f, b / 255.0f);
            }

            // Parse fields
            const JSONValue& fieldsVal = root.Get("fields");
            if (fieldsVal.IsArray())
            {
                const JSONArray& fieldsArr = fieldsVal.GetArray();
                for (unsigned fi = 0; fi < fieldsArr.Size(); ++fi)
                {
                    const JSONValue& fieldDef = fieldsArr[fi];
                    TextKeyTemplateField field;
                    field.name = fieldDef.Get("name").GetString();
                    field.type = ParseVariantTypeName(fieldDef.Get("type").GetString());
                    field.description = fieldDef.Get("description").GetString();
                    field.resourceType = fieldDef.Get("resourceType").GetString();
                    field.defaultValue = ParseDefaultValue(fieldDef.Get("default"), field.type);
                    // For ResourceRef with a known type, set the type_ on the ResourceRef
                    if (field.type == VAR_RESOURCEREF && !field.resourceType.Empty())
                    {
                        ResourceRef ref = field.defaultValue.GetResourceRef();
                        ref.type_ = StringHash(field.resourceType);
                        field.defaultValue = ref;
                    }
                    tmpl.fields.Push(field);
                }
            }

            textKeyTemplates_.Push(tmpl);
            URHO3D_LOGDEBUGF("Loaded text key template: %s (%d fields)", tmpl.name.CString(), tmpl.fields.Size());
        }
    }

    URHO3D_LOGINFOF("Loaded %d text key templates", textKeyTemplates_.Size());
}

void ModelTool::ApplyTemplate(int templateIndex)
{
    if (templateIndex < 0 || templateIndex >= (int)textKeyTemplates_.Size())
        return;

    const TextKeyTemplate& tmpl = textKeyTemplates_[templateIndex];

    // Set the key name to the template name
    if (textKeyNameEdit_)
        textKeyNameEdit_->SetText(tmpl.name);

    // Clear existing fields and populate from template defaults
    ClearPayloadFieldRows();
    for (const TextKeyTemplateField& field : tmpl.fields)
    {
        int typeIdx = VariantTypeToPayloadIndex(field.type);
        AddPayloadFieldRow(typeIdx, field.name, field.defaultValue);
        // For ResourceRef fields, set the resource type on the newly created row
        if (field.type == VAR_RESOURCEREF && !field.resourceType.Empty() && !payloadRows_.Empty())
        {
            PayloadFieldRow& newRow = payloadRows_.Back();
            newRow.resourceTypeName = field.resourceType;
            // Select the matching resource type in the sub-dropdown
            if (newRow.resourceTypeDD)
            {
                for (int ri = 0; ri < RESOURCE_TYPE_COUNT; ++ri)
                {
                    if (field.resourceType == RESOURCE_TYPE_NAMES[ri])
                    {
                        newRow.resourceTypeDD->SetSelection(ri);
                        break;
                    }
                }
            }
        }
    }

    RefreshJSONPreview();
    URHO3D_LOGINFOF("Applied template: %s (%d fields)", tmpl.name.CString(), tmpl.fields.Size());
}

// ============================================================================
// Text Key Panel
// ============================================================================

void ModelTool::CreateTextKeyPanel()
{
    auto* ui = GetSubsystem<UI>();

    // Collapsible header button
    auto* headerBtn = playbackPanel_->CreateChild<Button>();
    headerBtn->SetStyleAuto();
    headerBtn->SetFixedHeight(20);
    headerBtn->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 0, 4, 0));
    textKeyTitle_ = headerBtn->CreateChild<Text>();
    textKeyTitle_->SetFont(font_, 12);
    textKeyTitle_->SetColor(Color(0.5f, 0.9f, 0.9f));
    textKeyTitle_->SetText("v Text Keys (0)");

    // Content container — everything else goes inside this so it can be hidden as a unit
    textKeySection_ = playbackPanel_->CreateChild<UIElement>();
    textKeySection_->SetLayout(LM_VERTICAL, 2);
    textKeySection_->SetVisible(false);  // collapsed by default

    SubscribeToEvent(headerBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (!textKeySection_) return;
        bool nowVisible = !textKeySection_->IsVisible();
        textKeySection_->SetVisible(nowVisible);
        if (textKeyTitle_)
        {
            String t = textKeyTitle_->GetText();
            if (t.StartsWith("v ")) t = t.Substring(2);
            else if (t.StartsWith("> ")) t = t.Substring(2);
            textKeyTitle_->SetText((nowVisible ? "v " : "> ") + t);
        }
    });

    // Flash text — shows key name when fired during playback
    textKeyFlash_ = textKeySection_->CreateChild<Text>();
    textKeyFlash_->SetFont(font_, 14);
    textKeyFlash_->SetColor(Color(1.0f, 1.0f, 0.3f));
    textKeyFlash_->SetText("");

    // Scrollable container for text key list
    textKeyContainer_ = textKeySection_->CreateChild<UIElement>();
    textKeyContainer_->SetLayout(LM_VERTICAL, 2);
    textKeyContainer_->SetMaxHeight(150);

    // Add key controls: name + data line edits
    auto* addRow = textKeySection_->CreateChild<UIElement>();
    addRow->SetLayout(LM_HORIZONTAL, 4);
    addRow->SetFixedHeight(24);

    auto* nameLbl = addRow->CreateChild<Text>();
    nameLbl->SetFont(font_, 11);
    nameLbl->SetText("Name:");
    nameLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    nameLbl->SetFixedWidth(40);

    textKeyNameEdit_ = addRow->CreateChild<LineEdit>();
    textKeyNameEdit_->SetStyleAuto();
    textKeyNameEdit_->SetFixedHeight(22);
    textKeyNameEdit_->SetMinWidth(120);
    textKeyNameEdit_->SetText("FootDown");

    // ---- Template picker ----
    auto* templateRow = textKeySection_->CreateChild<UIElement>();
    templateRow->SetLayout(LM_HORIZONTAL, 4);
    templateRow->SetFixedHeight(24);

    auto* templateLbl = templateRow->CreateChild<Text>();
    templateLbl->SetFont(font_, 11);
    templateLbl->SetText("Template:");
    templateLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    templateLbl->SetFixedWidth(60);

    templateDropDown_ = templateRow->CreateChild<DropDownList>();
    templateDropDown_->SetStyleAuto();
    templateDropDown_->SetFixedHeight(22);
    templateDropDown_->SetMinWidth(160);
    templateDropDown_->SetResizePopup(true);

    // Load templates and populate the dropdown
    LoadTextKeyTemplates();

    // First item is always "Custom (free-form)"
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);
        item->SetText("Custom (free-form)");
        item->SetStyleAuto();
        templateDropDown_->AddItem(item);
    }
    for (unsigned i = 0; i < textKeyTemplates_.Size(); ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);
        item->SetText(textKeyTemplates_[i].name);
        item->SetStyleAuto();
        item->SetColor(textKeyTemplates_[i].color);
        templateDropDown_->AddItem(item);
    }
    templateDropDown_->SetSelection(0);

    SubscribeToEvent(templateDropDown_, E_ITEMSELECTED, [this](StringHash, VariantMap& eventData)
    {
        using namespace ItemSelected;
        int sel = eventData[P_SELECTION].GetI32();
        if (sel > 0)
            ApplyTemplate(sel - 1);  // offset by 1 for "Custom" entry
    });

    // ---- Structured payload editor ----
    auto* fieldsHeader = textKeySection_->CreateChild<UIElement>();
    fieldsHeader->SetLayout(LM_HORIZONTAL, 4);
    fieldsHeader->SetFixedHeight(22);

    auto* fieldsLbl = fieldsHeader->CreateChild<Text>();
    fieldsLbl->SetFont(font_, 11);
    fieldsLbl->SetText("Fields:");
    fieldsLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    fieldsLbl->SetFixedWidth(50);

    auto* addFieldBtn = fieldsHeader->CreateChild<Button>();
    addFieldBtn->SetStyleAuto();
    addFieldBtn->SetFixedSize(90, 20);
    auto* addFieldLbl = addFieldBtn->CreateChild<Text>();
    addFieldLbl->SetFont(font_, 11);
    addFieldLbl->SetText("+ Add Field");
    addFieldLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(addFieldBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        AddPayloadFieldRow(0, String::EMPTY, Variant::EMPTY);
    });

    payloadFieldsContainer_ = textKeySection_->CreateChild<UIElement>();
    payloadFieldsContainer_->SetLayout(LM_VERTICAL, 2);

    // Button row: Add | Delete | Save
    auto* keyBtnRow = textKeySection_->CreateChild<UIElement>();
    keyBtnRow->SetLayout(LM_HORIZONTAL, 4);
    keyBtnRow->SetFixedHeight(26);

    auto* addBtn = keyBtnRow->CreateChild<Button>();
    addBtn->SetStyleAuto();
    addBtn->SetFixedSize(70, 24);
    auto* addLbl = addBtn->CreateChild<Text>();
    addLbl->SetFont(font_, 11);
    addLbl->SetText("+ Add");
    addLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(addBtn, E_RELEASED, [this](StringHash, VariantMap&) { AddTextKeyAtCurrentTime(); });

    auto* delBtn = keyBtnRow->CreateChild<Button>();
    delBtn->SetStyleAuto();
    delBtn->SetFixedSize(70, 24);
    auto* delLbl = delBtn->CreateChild<Text>();
    delLbl->SetFont(font_, 11);
    delLbl->SetText("Delete");
    delLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(delBtn, E_RELEASED, [this](StringHash, VariantMap&) { DeleteSelectedTextKey(); });

    auto* saveBtn = keyBtnRow->CreateChild<Button>();
    saveBtn->SetStyleAuto();
    saveBtn->SetFixedSize(90, 24);
    auto* saveLbl = saveBtn->CreateChild<Text>();
    saveLbl->SetFont(font_, 11);
    saveLbl->SetText("Save Keys");
    saveLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(saveBtn, E_RELEASED, [this](StringHash, VariantMap&) { SaveTextKeys(); });

    auto* editorBtn = keyBtnRow->CreateChild<Button>();
    editorBtn->SetStyleAuto();
    editorBtn->SetFixedSize(60, 24);
    auto* editorLbl = editorBtn->CreateChild<Text>();
    editorLbl->SetFont(font_, 11);
    editorLbl->SetText("Editor");
    editorLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(editorBtn, E_RELEASED, [this](StringHash, VariantMap&) { ToggleTextKeyEditorWindow(); });

    // ---- Live JSON preview ----
    auto* previewLabel = textKeySection_->CreateChild<Text>();
    previewLabel->SetFont(font_, 11);
    previewLabel->SetText("JSON preview:");
    previewLabel->SetColor(Color(0.6f, 0.7f, 0.8f));

    payloadJsonPreview_ = textKeySection_->CreateChild<Text>();
    payloadJsonPreview_->SetFont(font_, 10);
    payloadJsonPreview_->SetColor(Color(0.7f, 0.85f, 0.6f));
    payloadJsonPreview_->SetText("(no key)");
    payloadJsonPreview_->SetWordwrap(false);

    // ---- Tree view of selected text key (display-only) ----
    auto* treeLabel = textKeySection_->CreateChild<Text>();
    treeLabel->SetFont(font_, 11);
    treeLabel->SetText("Tree:");
    treeLabel->SetColor(Color(0.6f, 0.7f, 0.8f));

    textKeyTreeView_ = textKeySection_->CreateChild<ListView>();
    textKeyTreeView_->SetStyle("HierarchyListView");
    textKeyTreeView_->SetHierarchyMode(true);
    textKeyTreeView_->SetHighlightMode(HM_ALWAYS);
    textKeyTreeView_->SetMinHeight(160);

    // Subscribe to text key events from AnimationState
    SubscribeToEvent(E_ANIMATIONTEXTKEY, URHO3D_HANDLER(ModelTool, HandleTextKeyEvent));
}

void ModelTool::RebuildTextKeyList()
{
    if (!textKeyContainer_) return;

    textKeyContainer_->RemoveAllChildren();
    textKeyButtons_.Clear();
    selectedTextKey_ = -1;

    // Find the current animation resource
    Animation* anim = nullptr;
    if (animController_ && currentAnimIndex_ >= 0 && currentAnimIndex_ < (int)availableAnims_.Size())
    {
        auto* cache = GetSubsystem<ResourceCache>();
        anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
    }

    String arrow = (textKeySection_ && textKeySection_->IsVisible()) ? "v " : "> ";
    unsigned numKeys = anim ? anim->GetNumTextKeys() : 0;
    if (textKeyTitle_)
        textKeyTitle_->SetText(arrow + "Text Keys (" + String(numKeys) + ")");
    if (!anim || numKeys == 0)
        return;

    const Vector<AnimationTextKey>& keys = anim->GetTextKeys();
    for (int i = 0; i < (int)keys.Size(); ++i)
    {
        auto* btn = textKeyContainer_->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetFixedHeight(20);
        btn->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 1, 4, 1));

        auto* timeText = btn->CreateChild<Text>();
        timeText->SetFont(font_, 11);
        timeText->SetText(String((double)keys[i].time_, 3) + "s");
        timeText->SetColor(Color(0.9f, 0.9f, 0.5f));
        timeText->SetFixedWidth(55);

        auto* nameText = btn->CreateChild<Text>();
        nameText->SetFont(font_, 11);
        nameText->SetText(keys[i].name_);
        nameText->SetColor(Color(0.5f, 0.9f, 0.9f));

        if (!keys[i].data_.IsEmpty())
        {
            auto* dataText = btn->CreateChild<Text>();
            dataText->SetFont(font_, 10);
            String summary;
            if (keys[i].data_.GetType() == VAR_VARIANTMAP)
                summary = "{" + String((int)keys[i].data_.GetVariantMap().Size()) + " fields}";
            else
                summary = "(" + keys[i].data_.ToString() + ")";
            dataText->SetText(summary);
            dataText->SetColor(Color(0.7f, 0.7f, 0.7f));
        }

        int keyIndex = i;
        SubscribeToEvent(btn, E_RELEASED, [this, keyIndex](StringHash, VariantMap&)
        {
            selectedTextKey_ = keyIndex;
            // Highlight selected
            for (int j = 0; j < (int)textKeyButtons_.Size(); ++j)
            {
                if (textKeyButtons_[j])
                    textKeyButtons_[j]->SetColor(j == selectedTextKey_ ? Color(0.3f, 0.4f, 0.5f) : Color::WHITE);
            }
            // Scrub to key time
            if (animController_ && currentAnimIndex_ >= 0)
            {
                auto* cache = GetSubsystem<ResourceCache>();
                Animation* a = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
                if (a)
                {
                    AnimationTextKey* k = a->GetTextKey(keyIndex);
                    if (k)
                    {
                        animController_->SetTime(availableAnims_[currentAnimIndex_], k->time_);
                        // Populate edit fields
                        if (textKeyNameEdit_) textKeyNameEdit_->SetText(k->name_);
                        // Populate structured payload rows
                        ClearPayloadFieldRows();
                        if (k->data_.GetType() == VAR_VARIANTMAP)
                            PopulateRowsFromVariantMap(k->data_.GetVariantMap());
                        else if (k->data_.GetType() == VAR_STRING && !k->data_.GetString().Empty())
                            AddPayloadFieldRow(0 /*String*/, "value", k->data_);  // legacy single-string

                        RebuildTextKeyTree();
                    }
                }
            }
        });

        textKeyButtons_.Push(btn);
    }
}

void ModelTool::AddTextKeyAtCurrentTime()
{
    if (!animController_ || currentAnimIndex_ < 0) return;

    // Block bind pose — it's a manual placeholder, not a real animation
    if (availableAnims_[currentAnimIndex_] == "[Bind Pose]")
    {
        URHO3D_LOGWARNING("Cannot add text keys to bind pose — select a real animation");
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    Animation* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
    if (!anim) return;

    float t = animController_->GetTime(availableAnims_[currentAnimIndex_]);
    String name = textKeyNameEdit_ ? textKeyNameEdit_->GetText().Trimmed() : "Key";
    if (name.Empty()) name = "Key";

    // Build a VariantMap from the field rows. If no rows, data is EMPTY.
    Variant data = BuildVariantMapFromRows();

    // Commit row names to the labels map recursively (only at Add time, not on every keystroke)
    std::function<void(const Vector<PayloadFieldRow>&)> commitLabels =
        [&](const Vector<PayloadFieldRow>& rows)
    {
        for (const auto& r : rows)
        {
            if (r.nameEdit)
            {
                String n = r.nameEdit->GetText().Trimmed();
                if (!n.Empty())
                    hashToLabel_[StringHash(n)] = n;
            }
            commitLabels(r.children);
        }
    };
    commitLabels(payloadRows_);

    anim->AddTextKey(t, false, name, data);
    URHO3D_LOGINFOF("Added text key '%s' at %.3fs (%d fields)",
        name.CString(), t,
        data.GetType() == VAR_VARIANTMAP ? (int)data.GetVariantMap().Size() : 0);

    RebuildTextKeyList();
    RebuildTextKeyTree();
    RebuildTextKeyEditorWindow();
}

void ModelTool::DeleteSelectedTextKey()
{
    if (selectedTextKey_ < 0) return;
    if (!animController_ || currentAnimIndex_ < 0) return;

    auto* cache = GetSubsystem<ResourceCache>();
    Animation* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
    if (!anim) return;

    AnimationTextKey* key = anim->GetTextKey(selectedTextKey_);
    if (key)
        URHO3D_LOGINFOF("Deleted text key '%s' at %.3fs", key->name_.CString(), key->time_);

    anim->RemoveTextKey(selectedTextKey_);
    selectedTextKey_ = -1;
    ClearPayloadFieldRows();
    RebuildTextKeyList();
    RebuildTextKeyTree();
    RebuildTextKeyEditorWindow();
    RefreshJSONPreview();
}

void ModelTool::SaveTextKeys()
{
    if (!animController_ || currentAnimIndex_ < 0) return;

    auto* cache = GetSubsystem<ResourceCache>();
    Animation* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
    if (!anim) return;

    const Vector<AnimationTextKey>& keys = anim->GetTextKeys();
    if (keys.Empty())
    {
        URHO3D_LOGINFO("No text keys to save");
        return;
    }

    // Build JSON sidecar path from the animation resource name
    // Find the actual file path on disk
    String animPath = availableAnims_[currentAnimIndex_];
    String jsonPath = ReplaceExtension(animPath, ".json");

    // Try to resolve to an absolute path
    auto* fs = GetSubsystem<FileSystem>();
    String absPath;

    // Check if animPath is already absolute
    if (fs->FileExists(animPath))
        absPath = ReplaceExtension(animPath, ".json");
    else
    {
        // Try resource dirs
        const Vector<String>& resourceDirs = cache->GetResourceDirs();
        for (const String& dir : resourceDirs)
        {
            if (fs->FileExists(dir + animPath))
            {
                absPath = dir + ReplaceExtension(animPath, ".json");
                break;
            }
        }
    }

    if (absPath.Empty())
    {
        URHO3D_LOGERRORF("Cannot resolve path for: %s", animPath.CString());
        return;
    }

    // Build JSON
    JSONFile jsonFile(context_);
    JSONValue& root = jsonFile.GetRoot();
    JSONArray emptyArr;
    JSONValue keyArray(emptyArr);

    for (const AnimationTextKey& key : keys)
    {
        JSONValue entry;
        entry.Set("name", JSONValue(key.name_));
        entry.Set("time", JSONValue((double)key.time_));
        if (!key.data_.IsEmpty())
        {
            // Write as typed Variant ({type, value}) so Animation::BeginLoad's
            // GetVariant() can round-trip it correctly.
            JSONValue dataVal;
            dataVal.SetVariant(key.data_, context_);
            entry.Set("data", dataVal);
        }
        keyArray.Push(entry);
    }

    root.Set("textkeys", keyArray);

    // Collect hashes actually referenced by any textkey's data (recursively)
    HashSet<StringHash> referencedHashes;
    std::function<void(const Variant&)> collectHashes = [&](const Variant& v)
    {
        if (v.GetType() == VAR_VARIANTMAP)
        {
            const VariantMap& m = v.GetVariantMap();
            for (auto it = m.Begin(); it != m.End(); ++it)
            {
                referencedHashes.Insert(it->first_);
                collectHashes(it->second_);
            }
        }
        else if (v.GetType() == VAR_VARIANTVECTOR)
        {
            const VariantVector& vec = v.GetVariantVector();
            for (unsigned i = 0; i < vec.Size(); ++i)
                collectHashes(vec[i]);
        }
    };
    for (const AnimationTextKey& key : keys)
        collectHashes(key.data_);

    // Write only labels that map to a referenced hash — drops orphans from old edits
    JSONValue labelsObj;
    bool anyLabel = false;
    for (auto it = hashToLabel_.Begin(); it != hashToLabel_.End(); ++it)
    {
        if (referencedHashes.Contains(it->first_))
        {
            labelsObj.Set(it->first_.ToString(), JSONValue(it->second_));
            anyLabel = true;
        }
    }
    if (anyLabel)
        root.Set("labels", labelsObj);

    File outFile(context_, absPath, FILE_WRITE);
    if (outFile.IsOpen())
    {
        jsonFile.Save(outFile, "  ");
        URHO3D_LOGINFOF("Saved %d text keys to %s", keys.Size(), absPath.CString());
    }
    else
    {
        URHO3D_LOGERRORF("Failed to write: %s", absPath.CString());
    }
}

// ============================================================================
// Structured Payload Editor — VariantMap of typed fields per text key
// ============================================================================

// Type list — atomic types first, container types last.
// Map and Vector start empty; their contents will eventually be edited via the tree editor.
static const char* PAYLOAD_TYPE_NAMES[] = { "String", "Int", "Float", "Bool", "Vector3", "Map", "Vector", "ResRef" };
static const VariantType PAYLOAD_TYPES[]  = { VAR_STRING, VAR_INT, VAR_FLOAT, VAR_BOOL, VAR_VECTOR3, VAR_VARIANTMAP, VAR_VARIANTVECTOR, VAR_RESOURCEREF };
static const int PAYLOAD_TYPE_COUNT = 8;

void ModelTool::AddPayloadFieldRow(int defaultType, const String& name, const Variant& value)
{
    if (!payloadFieldsContainer_) return;
    AddPayloadFieldRowInternal(payloadFieldsContainer_, payloadRows_, 0, false, defaultType, name, value);
}

void ModelTool::AddPayloadFieldRowInternal(UIElement* container, Vector<PayloadFieldRow>& rows,
    int depth, bool isVectorElem, int defaultType, const String& name, const Variant& value)
{
    if (!container) return;

    PayloadFieldRow row;
    row.depth = depth;
    row.isVectorElement = isVectorElem;

    row.row = container->CreateChild<UIElement>();
    row.row->SetLayout(LM_HORIZONTAL, 3);
    row.row->SetFixedHeight(22);

    // Type dropdown
    row.typeDropDown = row.row->CreateChild<DropDownList>();
    row.typeDropDown->SetStyleAuto();
    row.typeDropDown->SetFixedSize(70, 20);
    row.typeDropDown->SetResizePopup(true);
    for (int i = 0; i < PAYLOAD_TYPE_COUNT; ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);
        item->SetText(PAYLOAD_TYPE_NAMES[i]);
        item->SetStyleAuto();
        row.typeDropDown->AddItem(item);
    }
    row.typeDropDown->SetSelection(Clamp(defaultType, 0, PAYLOAD_TYPE_COUNT - 1));

    // Name field — Maps get an editable name, Vectors get an index label
    if (isVectorElem)
    {
        auto* idxLabel = row.row->CreateChild<Text>();
        idxLabel->SetFont(font_, 11);
        idxLabel->SetColor(Color(0.6f, 0.8f, 0.6f));
        idxLabel->SetText("[" + String(rows.Size()) + "]");
        idxLabel->SetFixedSize(30, 20);
        row.nameEdit = nullptr;
    }
    else
    {
        row.nameEdit = row.row->CreateChild<LineEdit>();
        row.nameEdit->SetStyleAuto();
        row.nameEdit->SetFixedSize(80, 20);
        row.nameEdit->SetText(name);
    }

    // Value editor(s)
    int typeIdx = Clamp(defaultType, 0, PAYLOAD_TYPE_COUNT - 1);
    VariantType vt = PAYLOAD_TYPES[typeIdx];
    if (vt == VAR_BOOL)
    {
        row.boolCheck = row.row->CreateChild<CheckBox>();
        row.boolCheck->SetStyleAuto();
        if (value.GetType() == VAR_BOOL)
            row.boolCheck->SetChecked(value.GetBool());
    }
    else if (vt == VAR_VECTOR3)
    {
        for (int c = 0; c < 3; ++c)
        {
            auto* edit = row.row->CreateChild<LineEdit>();
            edit->SetStyleAuto();
            edit->SetFixedSize(45, 20);
            row.valueEdits.Push(edit);
        }
        if (value.GetType() == VAR_VECTOR3)
        {
            Vector3 v = value.GetVector3();
            row.valueEdits[0]->SetText(String((double)v.x_, 4));
            row.valueEdits[1]->SetText(String((double)v.y_, 4));
            row.valueEdits[2]->SetText(String((double)v.z_, 4));
        }
    }
    else if (vt == VAR_RESOURCEREF)
    {
        // Resource type sub-dropdown
        row.resourceTypeDD = row.row->CreateChild<DropDownList>();
        row.resourceTypeDD->SetStyleAuto();
        row.resourceTypeDD->SetFixedSize(85, 20);
        row.resourceTypeDD->SetResizePopup(true);
        for (int ri = 0; ri < RESOURCE_TYPE_COUNT; ++ri)
        {
            auto* item = new Text(context_);
            item->SetFont(font_, 10);
            item->SetText(RESOURCE_TYPE_NAMES[ri]);
            item->SetStyleAuto();
            row.resourceTypeDD->AddItem(item);
        }
        row.resourceTypeDD->SetSelection(0);

        // Path text display
        row.resourcePathText = row.row->CreateChild<Text>();
        row.resourcePathText->SetFont(font_, 10);
        row.resourcePathText->SetColor(Color(0.7f, 0.9f, 0.7f));
        row.resourcePathText->SetFixedHeight(20);
        row.resourcePathText->SetMinWidth(80);

        // Pre-fill from ResourceRef value
        if (value.GetType() == VAR_RESOURCEREF)
        {
            const ResourceRef& ref = value.GetResourceRef();
            row.resourcePathText->SetText(ref.name_.Empty() ? "(none)" : ref.name_);
            row.resourceTypeName = ref.type_ != StringHash::ZERO
                ? context_->GetTypeName(ref.type_)
                : String::EMPTY;
            // Try to select matching resource type in dropdown
            for (int ri = 0; ri < RESOURCE_TYPE_COUNT; ++ri)
            {
                if (row.resourceTypeName == RESOURCE_TYPE_NAMES[ri])
                {
                    row.resourceTypeDD->SetSelection(ri);
                    break;
                }
            }
        }
        else
            row.resourcePathText->SetText("(none)");

        // Also store path in a hidden LineEdit so BuildVariantFromRow can read it
        auto* pathEdit = row.row->CreateChild<LineEdit>();
        pathEdit->SetStyleAuto();
        pathEdit->SetFixedSize(0, 0);  // hidden
        pathEdit->SetVisible(false);
        if (value.GetType() == VAR_RESOURCEREF)
            pathEdit->SetText(value.GetResourceRef().name_);
        row.valueEdits.Push(pathEdit);

        // Browse button
        auto* browseBtn = row.row->CreateChild<Button>();
        browseBtn->SetStyleAuto();
        browseBtn->SetFixedSize(55, 20);
        auto* browseLbl = browseBtn->CreateChild<Text>();
        browseLbl->SetFont(font_, 10);
        browseLbl->SetText("Browse");
        browseLbl->SetAlignment(HA_CENTER, VA_CENTER);
    }
    else if (vt == VAR_VARIANTMAP || vt == VAR_VARIANTVECTOR)
    {
        // "+ Child" / "+ Element" button instead of placeholder
        auto* addChildBtn = row.row->CreateChild<Button>();
        addChildBtn->SetStyleAuto();
        addChildBtn->SetFixedSize(70, 20);
        auto* addChildLbl = addChildBtn->CreateChild<Text>();
        addChildLbl->SetFont(font_, 10);
        addChildLbl->SetText(vt == VAR_VARIANTMAP ? "+ Child" : "+ Elem");
        addChildLbl->SetAlignment(HA_CENTER, VA_CENTER);

        // Child container — sibling of row.row, indented
        row.childContainer = container->CreateChild<UIElement>();
        row.childContainer->SetLayout(LM_VERTICAL, 1, IntRect(20, 0, 0, 0));
    }
    else
    {
        // String, Int, Float — single LineEdit
        auto* edit = row.row->CreateChild<LineEdit>();
        edit->SetStyleAuto();
        edit->SetFixedSize(140, 20);
        if (!value.IsEmpty())
            edit->SetText(value.ToString());
        row.valueEdits.Push(edit);
    }

    // Delete button
    auto* delBtn = row.row->CreateChild<Button>();
    delBtn->SetStyleAuto();
    delBtn->SetFixedSize(20, 20);
    auto* delLbl = delBtn->CreateChild<Text>();
    delLbl->SetFont(font_, 12);
    delLbl->SetText("x");
    delLbl->SetAlignment(HA_CENTER, VA_CENTER);

    rows.Push(row);

    // Wire up "+ Child/Element" button now that the row is in the vector
    if (vt == VAR_VARIANTMAP || vt == VAR_VARIANTVECTOR)
    {
        UIElement* rowElem = row.row;
        bool isMap = (vt == VAR_VARIANTMAP);
        // The "+ Child" button is child index 2 (after typeDropDown and nameEdit/indexLabel)
        auto* addChildBtn = static_cast<Button*>(row.row->GetChild(2));
        SubscribeToEvent(addChildBtn, E_RELEASED, [this, rowElem, isMap](StringHash, VariantMap&)
        {
            PayloadFieldRow* parent = FindPayloadFieldRow(payloadRows_, rowElem);
            if (parent && parent->childContainer)
            {
                AddPayloadFieldRowInternal(parent->childContainer, parent->children,
                    parent->depth + 1, !isMap, 0, String::EMPTY, Variant::EMPTY);
            }
        });

        // Populate children from existing value
        if (vt == VAR_VARIANTMAP && value.GetType() == VAR_VARIANTMAP && !value.GetVariantMap().Empty())
        {
            PopulateRowsInto(rows.Back().childContainer, rows.Back().children, depth + 1, value.GetVariantMap());
        }
        else if (vt == VAR_VARIANTVECTOR && value.GetType() == VAR_VARIANTVECTOR && !value.GetVariantVector().Empty())
        {
            PopulateVectorInto(rows.Back().childContainer, rows.Back().children, depth + 1, value.GetVariantVector());
        }
    }

    // Wire up ResourceRef Browse button and resource type dropdown
    if (vt == VAR_RESOURCEREF)
    {
        UIElement* rowElem = row.row;
        // Find the Browse button — it's the last child before the delete button
        // Layout: typeDD, nameEdit/idxLabel, resTypeDD, pathText, hiddenEdit, browseBtn, deleteBtn
        // Browse is second-to-last child
        unsigned browseIdx = row.row->GetNumChildren() - 2;  // before delete btn
        auto* browseBtn = static_cast<Button*>(row.row->GetChild(browseIdx));
        SubscribeToEvent(browseBtn, E_RELEASED, [this, rowElem](StringHash, VariantMap&)
        {
            PayloadFieldRow* r = FindPayloadFieldRow(payloadRows_, rowElem);
            if (r)
                OpenResourceRefBrowser(r);
        });

        // Resource type dropdown change updates resourceTypeName
        auto* resDD = row.resourceTypeDD;
        SubscribeToEvent(resDD, E_ITEMSELECTED, [this, rowElem](StringHash, VariantMap&)
        {
            PayloadFieldRow* r = FindPayloadFieldRow(payloadRows_, rowElem);
            if (r && r->resourceTypeDD)
            {
                int sel = r->resourceTypeDD->GetSelection();
                if (sel >= 0 && sel < RESOURCE_TYPE_COUNT)
                    r->resourceTypeName = RESOURCE_TYPE_NAMES[sel];
            }
            RefreshJSONPreview();
        });
    }

    // Delete handler — recursive search from root
    UIElement* rowElem = row.row;
    SubscribeToEvent(delBtn, E_RELEASED, [this, rowElem](StringHash, VariantMap&)
    {
        RemovePayloadFieldRowRecursive(payloadRows_, rowElem);
    });

    // Type change handler
    auto* dd = row.typeDropDown;
    SubscribeToEvent(dd, E_ITEMSELECTED, [this, dd](StringHash, VariantMap&)
    {
        // Find this row anywhere in the tree
        PayloadFieldRow* r = nullptr;
        // Search helper — find by typeDropDown pointer
        std::function<PayloadFieldRow*(Vector<PayloadFieldRow>&)> find =
            [&](Vector<PayloadFieldRow>& rows) -> PayloadFieldRow*
        {
            for (unsigned i = 0; i < rows.Size(); ++i)
            {
                if (rows[i].typeDropDown == dd) return &rows[i];
                auto* found = find(rows[i].children);
                if (found) return found;
            }
            return nullptr;
        };
        r = find(payloadRows_);
        if (r)
        {
            // Find index within siblings for RebuildRowFromType
            // Actually, RebuildRowFromType works on payloadRows_ by index — need to generalize
            // For now, rebuild inline
            UIElement* parentContainer = r->row->GetParent();

            // Clear old value controls (children between typeDropDown/nameLabel and delete button)
            Vector<UIElement*> toRemove;
            unsigned startIdx = r->isVectorElement ? 2 : 2;  // after typeDD + name/index
            for (unsigned i = startIdx; i + 1 < r->row->GetNumChildren(); ++i)
                toRemove.Push(r->row->GetChild(i));
            for (auto* c : toRemove)
                c->Remove();
            r->valueEdits.Clear();
            r->boolCheck = nullptr;
            r->resourcePathText = nullptr;
            r->resourceTypeDD = nullptr;
            r->resourceTypeName.Clear();

            // Clear old children
            if (r->childContainer)
            {
                r->childContainer->Remove();
                r->childContainer = nullptr;
                r->children.Clear();
            }

            // Rebuild value controls
            int newTypeIdx = Clamp((int)dd->GetSelection(), 0, PAYLOAD_TYPE_COUNT - 1);
            VariantType newVt = PAYLOAD_TYPES[newTypeIdx];

            // Hold delete button, remove, add new controls, re-add
            UIElement* delBtnElem = nullptr;
            if (r->row->GetNumChildren() >= 2)
                delBtnElem = r->row->GetChild(r->row->GetNumChildren() - 1);
            SharedPtr<UIElement> heldDel(delBtnElem);
            if (delBtnElem)
                r->row->RemoveChild(delBtnElem);

            if (newVt == VAR_BOOL)
            {
                r->boolCheck = r->row->CreateChild<CheckBox>();
                r->boolCheck->SetStyleAuto();
                SubscribeToEvent(r->boolCheck, E_TOGGLED, [this](StringHash, VariantMap&) { RefreshJSONPreview(); });
            }
            else if (newVt == VAR_VECTOR3)
            {
                for (int c = 0; c < 3; ++c)
                {
                    auto* edit = r->row->CreateChild<LineEdit>();
                    edit->SetStyleAuto();
                    edit->SetFixedSize(45, 20);
                    r->valueEdits.Push(edit);
                    SubscribeToEvent(edit, E_TEXTCHANGED, [this](StringHash, VariantMap&) { RefreshJSONPreview(); });
                }
            }
            else if (newVt == VAR_VARIANTMAP || newVt == VAR_VARIANTVECTOR)
            {
                auto* addBtn = r->row->CreateChild<Button>();
                addBtn->SetStyleAuto();
                addBtn->SetFixedSize(70, 20);
                auto* addLbl = addBtn->CreateChild<Text>();
                addLbl->SetFont(font_, 10);
                addLbl->SetText(newVt == VAR_VARIANTMAP ? "+ Child" : "+ Elem");
                addLbl->SetAlignment(HA_CENTER, VA_CENTER);

                r->childContainer = parentContainer->CreateChild<UIElement>();
                r->childContainer->SetLayout(LM_VERTICAL, 1, IntRect(20, 0, 0, 0));

                UIElement* rowElem = r->row;
                bool isMap = (newVt == VAR_VARIANTMAP);
                SubscribeToEvent(addBtn, E_RELEASED, [this, rowElem, isMap](StringHash, VariantMap&)
                {
                    PayloadFieldRow* parent = FindPayloadFieldRow(payloadRows_, rowElem);
                    if (parent && parent->childContainer)
                    {
                        AddPayloadFieldRowInternal(parent->childContainer, parent->children,
                            parent->depth + 1, !isMap, 0, String::EMPTY, Variant::EMPTY);
                    }
                });
            }
            else if (newVt == VAR_RESOURCEREF)
            {
                // Resource type sub-dropdown
                r->resourceTypeDD = r->row->CreateChild<DropDownList>();
                r->resourceTypeDD->SetStyleAuto();
                r->resourceTypeDD->SetFixedSize(85, 20);
                r->resourceTypeDD->SetResizePopup(true);
                for (int ri = 0; ri < RESOURCE_TYPE_COUNT; ++ri)
                {
                    auto* item = new Text(context_);
                    item->SetFont(font_, 10);
                    item->SetText(RESOURCE_TYPE_NAMES[ri]);
                    item->SetStyleAuto();
                    r->resourceTypeDD->AddItem(item);
                }
                r->resourceTypeDD->SetSelection(0);
                r->resourceTypeName = RESOURCE_TYPE_NAMES[0];

                // Path text
                r->resourcePathText = r->row->CreateChild<Text>();
                r->resourcePathText->SetFont(font_, 10);
                r->resourcePathText->SetColor(Color(0.7f, 0.9f, 0.7f));
                r->resourcePathText->SetFixedHeight(20);
                r->resourcePathText->SetMinWidth(80);
                r->resourcePathText->SetText("(none)");

                // Hidden path edit
                auto* pathEdit = r->row->CreateChild<LineEdit>();
                pathEdit->SetStyleAuto();
                pathEdit->SetFixedSize(0, 0);
                pathEdit->SetVisible(false);
                r->valueEdits.Push(pathEdit);

                // Browse button
                auto* browseBtn = r->row->CreateChild<Button>();
                browseBtn->SetStyleAuto();
                browseBtn->SetFixedSize(55, 20);
                auto* browseLbl = browseBtn->CreateChild<Text>();
                browseLbl->SetFont(font_, 10);
                browseLbl->SetText("Browse");
                browseLbl->SetAlignment(HA_CENTER, VA_CENTER);

                UIElement* rowElem = r->row;
                SubscribeToEvent(browseBtn, E_RELEASED, [this, rowElem](StringHash, VariantMap&)
                {
                    PayloadFieldRow* pr = FindPayloadFieldRow(payloadRows_, rowElem);
                    if (pr)
                        OpenResourceRefBrowser(pr);
                });

                auto* resDD = r->resourceTypeDD;
                SubscribeToEvent(resDD, E_ITEMSELECTED, [this, rowElem](StringHash, VariantMap&)
                {
                    PayloadFieldRow* pr = FindPayloadFieldRow(payloadRows_, rowElem);
                    if (pr && pr->resourceTypeDD)
                    {
                        int sel = pr->resourceTypeDD->GetSelection();
                        if (sel >= 0 && sel < RESOURCE_TYPE_COUNT)
                            pr->resourceTypeName = RESOURCE_TYPE_NAMES[sel];
                    }
                    RefreshJSONPreview();
                });
            }
            else
            {
                auto* edit = r->row->CreateChild<LineEdit>();
                edit->SetStyleAuto();
                edit->SetFixedSize(140, 20);
                r->valueEdits.Push(edit);
                SubscribeToEvent(edit, E_TEXTCHANGED, [this](StringHash, VariantMap&) { RefreshJSONPreview(); });
            }

            if (heldDel)
                r->row->AddChild(heldDel);

            RefreshJSONPreview();
        }
    });

    // Live preview on text/value changes
    if (row.nameEdit)
        SubscribeToEvent(row.nameEdit, E_TEXTCHANGED, [this](StringHash, VariantMap&) { RefreshJSONPreview(); });
    for (auto* edit : row.valueEdits)
        SubscribeToEvent(edit, E_TEXTCHANGED, [this](StringHash, VariantMap&) { RefreshJSONPreview(); });
    if (row.boolCheck)
        SubscribeToEvent(row.boolCheck, E_TOGGLED, [this](StringHash, VariantMap&) { RefreshJSONPreview(); });

    RefreshJSONPreview();
}

void ModelTool::RemovePayloadFieldRow(unsigned index)
{
    if (index >= payloadRows_.Size()) return;
    if (payloadRows_[index].childContainer)
        payloadRows_[index].childContainer->Remove();
    if (payloadRows_[index].row)
        payloadRows_[index].row->Remove();
    payloadRows_.Erase(index);
    RefreshJSONPreview();
}

bool ModelTool::RemovePayloadFieldRowRecursive(Vector<PayloadFieldRow>& rows, UIElement* target)
{
    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        if (rows[i].row == target)
        {
            if (rows[i].childContainer)
                rows[i].childContainer->Remove();
            target->Remove();
            rows.Erase(i);
            RefreshJSONPreview();
            return true;
        }
        if (RemovePayloadFieldRowRecursive(rows[i].children, target))
            return true;
    }
    return false;
}

ModelTool::PayloadFieldRow* ModelTool::FindPayloadFieldRow(Vector<PayloadFieldRow>& rows, UIElement* target)
{
    for (unsigned i = 0; i < rows.Size(); ++i)
    {
        if (rows[i].row == target) return &rows[i];
        PayloadFieldRow* found = FindPayloadFieldRow(rows[i].children, target);
        if (found) return found;
    }
    return nullptr;
}

void ModelTool::ClearPayloadFieldRows()
{
    for (auto& r : payloadRows_)
    {
        if (r.childContainer) r.childContainer->Remove();
        if (r.row) r.row->Remove();
    }
    payloadRows_.Clear();
    RefreshJSONPreview();
}

// ============================================================================
// ResourceRef file browser — Phase 2
// ============================================================================

/// Map resource type name to file filter extensions
static String GetResourceFilter(const String& resourceType)
{
    if (resourceType == "Material") return "*.xml";
    if (resourceType == "ParticleEffect") return "*.xml";
    if (resourceType == "Sound") return "*.ogg *.wav";
    if (resourceType == "Texture2D") return "*.png *.jpg *.dds";
    if (resourceType == "Model") return "*.mdl";
    if (resourceType == "Animation") return "*.ani";
    if (resourceType == "XMLFile") return "*.xml";
    return "*.*";
}

/// Map resource type name to a likely subdirectory
static String GetResourceSubdir(const String& resourceType)
{
    if (resourceType == "Material") return "Materials/";
    if (resourceType == "ParticleEffect") return "Particle/";
    if (resourceType == "Sound") return "Sounds/";
    if (resourceType == "Texture2D") return "Textures/";
    if (resourceType == "Model") return "Models/";
    if (resourceType == "Animation") return "Models/";
    if (resourceType == "XMLFile") return "";
    return "";
}

void ModelTool::OpenResourceRefBrowser(PayloadFieldRow* row)
{
    if (!row) return;

    // Store which row we're browsing for
    pendingBrowseRow_ = row->row;

    // Determine resource type from the row's sub-dropdown
    String resType = row->resourceTypeName;
    if (resType.Empty() && row->resourceTypeDD)
    {
        int sel = row->resourceTypeDD->GetSelection();
        if (sel >= 0 && sel < RESOURCE_TYPE_COUNT)
            resType = RESOURCE_TYPE_NAMES[sel];
    }

    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Select " + resType + " Resource");
    fileSelector_->SetButtonTexts("Select", "Cancel");

    String filterStr = GetResourceFilter(resType);
    Vector<String> filters;
    filters.Push(filterStr);
    filters.Push("*.*");
    fileSelector_->SetFilters(filters, 0);

    // Start in the appropriate resource subdirectory
    const Vector<String>& dirs = cache->GetResourceDirs();
    String startDir;
    String subdir = GetResourceSubdir(resType);
    auto* fs = GetSubsystem<FileSystem>();
    for (const String& dir : dirs)
    {
        String candidate = dir + subdir;
        if (fs->DirExists(candidate))
        {
            startDir = candidate;
            break;
        }
        if (fs->DirExists(dir))
        {
            startDir = dir;
            // Keep searching for a better match
        }
    }
    if (!startDir.Empty())
        fileSelector_->SetPath(startDir);

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(ModelTool, HandleResourceRefSelected));
}

void ModelTool::HandleResourceRefSelected(StringHash, VariantMap& eventData)
{
    using namespace FileSelected;

    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty() || !pendingBrowseRow_)
    {
        pendingBrowseRow_ = nullptr;
        return;
    }

    // Convert absolute path to a resource-relative path
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    String relativePath = path;
    for (const String& dir : dirs)
    {
        if (path.StartsWith(dir))
        {
            relativePath = path.Substring(dir.Length());
            break;
        }
    }

    // Find the row and update it
    PayloadFieldRow* row = FindPayloadFieldRow(payloadRows_, pendingBrowseRow_);
    if (row)
    {
        // Update displayed path
        if (row->resourcePathText)
            row->resourcePathText->SetText(relativePath);
        // Update hidden LineEdit used by BuildVariantFromRow
        if (row->valueEdits.Size() > 0)
            row->valueEdits[0]->SetText(relativePath);

        RefreshJSONPreview();
    }

    pendingBrowseRow_ = nullptr;
}

void ModelTool::RebuildRowFromType(unsigned rowIndex, int newType)
{
    // Legacy path — type changes are now handled inline in the E_ITEMSELECTED handler
    // within AddPayloadFieldRowInternal. This function is kept for backward compatibility
    // but should not be called for new rows.
    (void)rowIndex;
    (void)newType;
}

Variant ModelTool::BuildVariantFromRow(const PayloadFieldRow& r) const
{
    int typeIdx = r.typeDropDown ? r.typeDropDown->GetSelection() : 0;
    typeIdx = Clamp(typeIdx, 0, PAYLOAD_TYPE_COUNT - 1);
    VariantType vt = PAYLOAD_TYPES[typeIdx];

    switch (vt)
    {
    case VAR_STRING:
        return r.valueEdits.Size() > 0 ? Variant(r.valueEdits[0]->GetText()) : Variant(String::EMPTY);
    case VAR_INT:
        return r.valueEdits.Size() > 0 ? Variant((int)ToI32(r.valueEdits[0]->GetText())) : Variant(0);
    case VAR_FLOAT:
        return r.valueEdits.Size() > 0 ? Variant(ToFloat(r.valueEdits[0]->GetText())) : Variant(0.0f);
    case VAR_BOOL:
        return r.boolCheck ? Variant(r.boolCheck->IsChecked()) : Variant(false);
    case VAR_VECTOR3:
        if (r.valueEdits.Size() >= 3)
            return Variant(Vector3(ToFloat(r.valueEdits[0]->GetText()),
                                   ToFloat(r.valueEdits[1]->GetText()),
                                   ToFloat(r.valueEdits[2]->GetText())));
        return Variant(Vector3::ZERO);
    case VAR_VARIANTMAP:
    {
        VariantMap childMap;
        for (const auto& child : r.children)
        {
            if (!child.nameEdit) continue;
            String cname = child.nameEdit->GetText().Trimmed();
            if (cname.Empty()) continue;
            childMap[StringHash(cname)] = BuildVariantFromRow(child);
        }
        return Variant(childMap);
    }
    case VAR_VARIANTVECTOR:
    {
        VariantVector childVec;
        for (const auto& child : r.children)
            childVec.Push(BuildVariantFromRow(child));
        return Variant(childVec);
    }
    case VAR_RESOURCEREF:
    {
        String path = r.valueEdits.Size() > 0 ? r.valueEdits[0]->GetText() : String::EMPTY;
        StringHash resType = r.resourceTypeName.Empty() ? StringHash::ZERO : StringHash(r.resourceTypeName);
        return Variant(ResourceRef(resType, path));
    }
    default:
        return Variant::EMPTY;
    }
}

Variant ModelTool::BuildVariantMapFromRows() const
{
    if (payloadRows_.Empty())
        return Variant::EMPTY;

    VariantMap map;
    for (const auto& r : payloadRows_)
    {
        if (!r.nameEdit) continue;
        String name = r.nameEdit->GetText().Trimmed();
        if (name.Empty()) continue;
        map[StringHash(name)] = BuildVariantFromRow(r);
    }
    return map.Empty() ? Variant::EMPTY : Variant(map);
}

void ModelTool::LoadLabelsForCurrentAnim()
{
    hashToLabel_.Clear();
    if (currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();

    // Resolve absolute path of the .ani then read sibling .json
    const String& animPath = availableAnims_[currentAnimIndex_];
    String absJson;
    if (fs->FileExists(animPath))
        absJson = ReplaceExtension(animPath, ".json");
    else
    {
        const Vector<String>& dirs = cache->GetResourceDirs();
        for (const String& dir : dirs)
        {
            if (fs->FileExists(dir + animPath))
            {
                absJson = dir + ReplaceExtension(animPath, ".json");
                break;
            }
        }
    }

    if (absJson.Empty() || !fs->FileExists(absJson))
        return;

    JSONFile jf(context_);
    File f(context_, absJson, FILE_READ);
    if (!f.IsOpen() || !jf.Load(f))
        return;

    const JSONValue& labels = jf.GetRoot().Get("labels");
    if (!labels.IsObject())
        return;

    for (auto it = labels.Begin(); it != labels.End(); ++it)
    {
        // Key is hex StringHash, value is human-readable name
        StringHash hash(ToU32(it->first_, 16));
        String name = it->second_.GetString();
        hashToLabel_[hash] = name;
    }
}

void ModelTool::RebuildTextKeyTree()
{
    if (!textKeyTreeView_) return;

    textKeyTreeView_->DisableInternalLayoutUpdate();
    textKeyTreeView_->RemoveAllItems();

    if (selectedTextKey_ < 0 || !animController_ || currentAnimIndex_ < 0)
    {
        auto* empty = new Text(context_);
        empty->SetFont(font_, 11);
        empty->SetText("(no key selected)");
        empty->SetColor(Color(0.5f, 0.5f, 0.5f));
        empty->SetFixedHeight(16);
        textKeyTreeView_->AddItem(empty);
        textKeyTreeView_->EnableInternalLayoutUpdate();
        textKeyTreeView_->UpdateInternalLayout();
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    Animation* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
    if (!anim || selectedTextKey_ >= (int)anim->GetNumTextKeys())
    {
        textKeyTreeView_->EnableInternalLayoutUpdate();
        textKeyTreeView_->UpdateInternalLayout();
        return;
    }

    AnimationTextKey* key = anim->GetTextKey(selectedTextKey_);
    if (!key)
    {
        textKeyTreeView_->EnableInternalLayoutUpdate();
        textKeyTreeView_->UpdateInternalLayout();
        return;
    }

    // Root: "FootDown @ 0.392s"
    auto* root = new Text(context_);
    root->SetFont(font_, 12);
    root->SetText(key->name_ + "  @ " + String((double)key->time_, 3) + "s");
    root->SetColor(Color(0.6f, 0.95f, 0.95f));
    root->SetFixedHeight(18);
    textKeyTreeView_->AddItem(root);

    unsigned index = 1;
    if (!key->data_.IsEmpty())
        AppendVariantToTree(root, "data", key->data_, index);

    textKeyTreeView_->EnableInternalLayoutUpdate();
    textKeyTreeView_->UpdateInternalLayout();
    textKeyTreeView_->Expand(0, true);  // expand root
}

void ModelTool::AppendVariantToTree(Text* parentItem, const String& label, const Variant& v, unsigned& index)
{
    VariantType vt = v.GetType();
    String typeName = Variant::GetTypeName(vt);

    auto* item = new Text(context_);
    item->SetFont(font_, 11);
    item->SetFixedHeight(16);

    if (vt == VAR_VARIANTMAP)
    {
        const VariantMap& m = v.GetVariantMap();
        item->SetText(label + " : " + typeName + "  (" + String(m.Size()) + " items)");
        item->SetColor(Color(0.85f, 0.85f, 0.4f));
        textKeyTreeView_->InsertItem(index++, item, parentItem);
        // Children: human label if known, else hex StringHash
        for (auto it = m.Begin(); it != m.End(); ++it)
        {
            String childLabel;
            auto labelIt = hashToLabel_.Find(it->first_);
            if (labelIt != hashToLabel_.End())
                childLabel = labelIt->second_;
            else
                childLabel = it->first_.ToString();
            AppendVariantToTree(item, childLabel, it->second_, index);
        }
    }
    else if (vt == VAR_VARIANTVECTOR)
    {
        const VariantVector& vec = v.GetVariantVector();
        item->SetText(label + " : " + typeName + "  (" + String(vec.Size()) + " items)");
        item->SetColor(Color(0.85f, 0.85f, 0.4f));
        textKeyTreeView_->InsertItem(index++, item, parentItem);
        for (unsigned i = 0; i < vec.Size(); ++i)
            AppendVariantToTree(item, "[" + String(i) + "]", vec[i], index);
    }
    else if (vt == VAR_RESOURCEREF)
    {
        const ResourceRef& ref = v.GetResourceRef();
        String typePart = ref.type_ != StringHash::ZERO ? context_->GetTypeName(ref.type_) : "?";
        item->SetText(label + " : ResourceRef<" + typePart + "> = " + ref.name_);
        item->SetColor(Color(0.7f, 0.9f, 0.7f));
        textKeyTreeView_->InsertItem(index++, item, parentItem);
    }
    else
    {
        // Leaf — show name : type = value
        item->SetText(label + " : " + typeName + " = " + v.ToString());
        item->SetColor(Color(0.9f, 0.9f, 0.9f));
        textKeyTreeView_->InsertItem(index++, item, parentItem);
    }
}

// ============================================================================
// Standalone Text Key Editor Window
// ============================================================================

void ModelTool::CreateTextKeyEditorWindow()
{
    if (textKeyEditorWindow_) return;  // already built

    auto* ui = GetSubsystem<UI>();
    auto* uiRoot = ui->GetRoot();

    textKeyEditorWindow_ = uiRoot->CreateChild<Window>("TextKeyEditorWindow");
    textKeyEditorWindow_->SetStyle("Window");
    textKeyEditorWindow_->SetMovable(true);
    textKeyEditorWindow_->SetResizable(true);
    textKeyEditorWindow_->SetMinSize(420, 360);
    textKeyEditorWindow_->SetSize(520, 480);
    textKeyEditorWindow_->SetPosition(360, 60);
    textKeyEditorWindow_->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));
    textKeyEditorWindow_->SetOpacity(0.95f);
    textKeyEditorWindow_->SetVisible(false);

    // Title bar — "Text Key Editor — <animation>"
    auto* titleRow = textKeyEditorWindow_->CreateChild<UIElement>();
    titleRow->SetLayout(LM_HORIZONTAL, 4);
    titleRow->SetFixedHeight(22);

    editorTitleText_ = titleRow->CreateChild<Text>();
    editorTitleText_->SetFont(font_, 13);
    editorTitleText_->SetColor(Color(0.95f, 0.95f, 0.7f));
    editorTitleText_->SetText("Text Key Editor");

    auto* spacer = titleRow->CreateChild<UIElement>();
    spacer->SetLayoutFlexScale(Vector2(1.0f, 1.0f));

    auto* closeBtn = titleRow->CreateChild<Button>();
    closeBtn->SetStyleAuto();
    closeBtn->SetFixedSize(22, 20);
    auto* closeLbl = closeBtn->CreateChild<Text>();
    closeLbl->SetFont(font_, 13);
    closeLbl->SetText("x");
    closeLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (textKeyEditorWindow_) textKeyEditorWindow_->SetVisible(false);
    });

    // Tree view — fills the rest
    editorTreeView_ = textKeyEditorWindow_->CreateChild<ListView>();
    editorTreeView_->SetStyle("HierarchyListView");
    editorTreeView_->SetHierarchyMode(true);
    editorTreeView_->SetHighlightMode(HM_ALWAYS);
    editorTreeView_->SetMinSize(400, 300);
}

void ModelTool::ToggleTextKeyEditorWindow()
{
    if (!textKeyEditorWindow_)
        CreateTextKeyEditorWindow();
    if (!textKeyEditorWindow_) return;

    bool nowVisible = !textKeyEditorWindow_->IsVisible();
    textKeyEditorWindow_->SetVisible(nowVisible);
    if (nowVisible)
    {
        textKeyEditorWindow_->BringToFront();
        RebuildTextKeyEditorWindow();
    }
}

void ModelTool::RebuildTextKeyEditorWindow()
{
    if (!textKeyEditorWindow_ || !editorTreeView_) return;
    if (!textKeyEditorWindow_->IsVisible()) return;

    editorTreeView_->DisableInternalLayoutUpdate();
    editorTreeView_->RemoveAllItems();

    if (currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
    {
        if (editorTitleText_)
            editorTitleText_->SetText("Text Key Editor — (no animation)");
        editorTreeView_->EnableInternalLayoutUpdate();
        editorTreeView_->UpdateInternalLayout();
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    Animation* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
    if (!anim)
    {
        editorTreeView_->EnableInternalLayoutUpdate();
        editorTreeView_->UpdateInternalLayout();
        return;
    }

    if (editorTitleText_)
        editorTitleText_->SetText("Text Key Editor — " + GetFileName(availableAnims_[currentAnimIndex_]));

    // Walk all text keys and add them as top-level tree nodes with their data hierarchy below
    const Vector<AnimationTextKey>& keys = anim->GetTextKeys();
    unsigned index = 0;

    // Use the existing AppendVariantToTree for the data subtree, but feed editorTreeView_ via a swap.
    // (The helper writes into textKeyTreeView_ — make it work for both by temporarily swapping the pointer.)
    ListView* savedTree = textKeyTreeView_;
    textKeyTreeView_ = editorTreeView_;

    if (keys.Empty())
    {
        auto* placeholder = new Text(context_);
        placeholder->SetFont(font_, 11);
        placeholder->SetText("(this animation has no text keys)");
        placeholder->SetColor(Color(0.5f, 0.5f, 0.5f));
        placeholder->SetFixedHeight(16);
        editorTreeView_->AddItem(placeholder);
    }
    else
    {
        for (unsigned i = 0; i < keys.Size(); ++i)
        {
            const AnimationTextKey& k = keys[i];
            auto* keyItem = new Text(context_);
            keyItem->SetFont(font_, 12);
            keyItem->SetText(k.name_ + "  @ " + String((double)k.time_, 3) + "s");
            keyItem->SetColor(Color(0.6f, 0.95f, 0.95f));
            keyItem->SetFixedHeight(18);
            editorTreeView_->AddItem(keyItem);
            ++index;

            if (!k.data_.IsEmpty())
                AppendVariantToTree(keyItem, "data", k.data_, index);
        }
    }

    textKeyTreeView_ = savedTree;

    editorTreeView_->EnableInternalLayoutUpdate();
    editorTreeView_->UpdateInternalLayout();
    // Expand all top-level entries so the user immediately sees what's inside
    for (unsigned i = 0; i < keys.Size() + 1; ++i)
        editorTreeView_->Expand(i, true);
}

void ModelTool::RefreshJSONPreview()
{
    if (!payloadJsonPreview_) return;
    if (!textKeySection_ || !textKeySection_->IsVisible())
        return;

    if (!animController_ || currentAnimIndex_ < 0)
    {
        payloadJsonPreview_->SetText("(no animation)");
        return;
    }

    // Build a single text key entry as JSON for preview
    JSONFile jf(context_);
    JSONValue& root = jf.GetRoot();

    String name = textKeyNameEdit_ ? textKeyNameEdit_->GetText() : String("Key");
    if (name.Empty()) name = "Key";
    float time = animController_->GetTime(availableAnims_[currentAnimIndex_]);

    root.Set("name", JSONValue(name));
    root.Set("time", JSONValue((double)time));

    Variant data = BuildVariantMapFromRows();
    if (!data.IsEmpty())
    {
        JSONValue dataVal;
        dataVal.SetVariant(data, context_);
        root.Set("data", dataVal);
    }

    String preview = jf.ToString("  ");
    payloadJsonPreview_->SetText(preview);
}

void ModelTool::PopulateRowsFromVariantMap(const VariantMap& map)
{
    if (!payloadFieldsContainer_) return;
    PopulateRowsInto(payloadFieldsContainer_, payloadRows_, 0, map);
}

void ModelTool::PopulateRowsInto(UIElement* container, Vector<PayloadFieldRow>& rows, int depth, const VariantMap& map)
{
    for (auto it = map.Begin(); it != map.End(); ++it)
    {
        String name;
        auto labelIt = hashToLabel_.Find(it->first_);
        if (labelIt != hashToLabel_.End())
            name = labelIt->second_;
        else
            name = it->first_.ToString();

        int typeIdx = 0;
        VariantType vt = it->second_.GetType();
        for (int i = 0; i < PAYLOAD_TYPE_COUNT; ++i)
        {
            if (PAYLOAD_TYPES[i] == vt) { typeIdx = i; break; }
        }
        // AddPayloadFieldRowInternal handles recursive population of children
        AddPayloadFieldRowInternal(container, rows, depth, false, typeIdx, name, it->second_);
    }
}

void ModelTool::PopulateVectorInto(UIElement* container, Vector<PayloadFieldRow>& rows, int depth, const VariantVector& vec)
{
    for (unsigned i = 0; i < vec.Size(); ++i)
    {
        int typeIdx = 0;
        VariantType vt = vec[i].GetType();
        for (int t = 0; t < PAYLOAD_TYPE_COUNT; ++t)
        {
            if (PAYLOAD_TYPES[t] == vt) { typeIdx = t; break; }
        }
        AddPayloadFieldRowInternal(container, rows, depth, true, typeIdx, String::EMPTY, vec[i]);
    }
}

void ModelTool::HandleTextKeyEvent(StringHash, VariantMap& eventData)
{
    using namespace AnimationTextKeyEvent;
    String name = eventData[P_NAME].GetString();
    float time = eventData[P_TIME].GetFloat();

    // Flash the key name on screen
    if (textKeyFlash_)
    {
        textKeyFlash_->SetText(String((double)time, 3) + "s  " + name);
        textKeyFlashTimer_ = 0.8f;
    }
}

// ============================================================================
// Keyframe Editor (Phase 2)
// ============================================================================

void ModelTool::CreateKeyframePanel()
{
    // Collapsible header button
    auto* headerBtn = playbackPanel_->CreateChild<Button>();
    headerBtn->SetStyleAuto();
    headerBtn->SetFixedHeight(20);
    headerBtn->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 0, 4, 0));
    keyframeTitle_ = headerBtn->CreateChild<Text>();
    keyframeTitle_->SetFont(font_, 12);
    keyframeTitle_->SetColor(Color(0.9f, 0.6f, 0.9f));
    keyframeTitle_->SetText("> Keyframes");

    // Content container
    keyframeSection_ = playbackPanel_->CreateChild<UIElement>();
    keyframeSection_->SetLayout(LM_VERTICAL, 2);
    keyframeSection_->SetVisible(false);  // collapsed by default

    SubscribeToEvent(headerBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        if (!keyframeSection_) return;
        bool nowVisible = !keyframeSection_->IsVisible();
        keyframeSection_->SetVisible(nowVisible);
        if (keyframeTitle_)
        {
            String t = keyframeTitle_->GetText();
            if (t.StartsWith("v ")) t = t.Substring(2);
            else if (t.StartsWith("> ")) t = t.Substring(2);
            keyframeTitle_->SetText((nowVisible ? "v " : "> ") + t);
        }
    });

    // Track selector dropdown
    auto* trackRow = keyframeSection_->CreateChild<UIElement>();
    trackRow->SetLayout(LM_HORIZONTAL, 4);
    trackRow->SetFixedHeight(24);

    auto* trackLbl = trackRow->CreateChild<Text>();
    trackLbl->SetFont(font_, 11);
    trackLbl->SetText("Track:");
    trackLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    trackLbl->SetFixedWidth(40);

    trackDropDown_ = trackRow->CreateChild<DropDownList>();
    trackDropDown_->SetStyleAuto();
    trackDropDown_->SetFixedHeight(22);
    trackDropDown_->SetMinWidth(240);
    trackDropDown_->SetResizePopup(true);
    SubscribeToEvent(trackDropDown_, E_ITEMSELECTED, [this](StringHash, VariantMap& eventData)
    {
        using namespace ItemSelected;
        int sel = eventData[P_SELECTION].GetI32();
        SelectTrack(sel);
    });

    // Keyframe list
    keyframeContainer_ = keyframeSection_->CreateChild<UIElement>();
    keyframeContainer_->SetLayout(LM_VERTICAL, 1);
    keyframeContainer_->SetMaxHeight(120);

    // Keyframe time display
    kfTimeText_ = keyframeSection_->CreateChild<Text>();
    kfTimeText_->SetFont(font_, 11);
    kfTimeText_->SetColor(Color(0.9f, 0.9f, 0.5f));
    kfTimeText_->SetText("Time: —");

    // Position row
    auto* posRow = keyframeSection_->CreateChild<UIElement>();
    posRow->SetLayout(LM_HORIZONTAL, 2);
    posRow->SetFixedHeight(22);

    auto* posLbl = posRow->CreateChild<Text>();
    posLbl->SetFont(font_, 10);
    posLbl->SetText("Pos:");
    posLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    posLbl->SetFixedWidth(30);

    auto makeEdit = [&](UIElement* parent, int width) -> LineEdit*
    {
        auto* edit = parent->CreateChild<LineEdit>();
        edit->SetStyleAuto();
        edit->SetFixedHeight(20);
        edit->SetFixedWidth(width);
        edit->SetText("0.000");
        return edit;
    };

    kfPosX_ = makeEdit(posRow, 88);
    kfPosY_ = makeEdit(posRow, 88);
    kfPosZ_ = makeEdit(posRow, 88);

    // Rotation row
    auto* rotRow = keyframeSection_->CreateChild<UIElement>();
    rotRow->SetLayout(LM_HORIZONTAL, 2);
    rotRow->SetFixedHeight(22);

    auto* rotLbl = rotRow->CreateChild<Text>();
    rotLbl->SetFont(font_, 10);
    rotLbl->SetText("Rot:");
    rotLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    rotLbl->SetFixedWidth(30);

    kfRotX_ = makeEdit(rotRow, 65);
    kfRotY_ = makeEdit(rotRow, 65);
    kfRotZ_ = makeEdit(rotRow, 65);
    kfRotW_ = makeEdit(rotRow, 65);

    // Scale row
    auto* scaleRow = keyframeSection_->CreateChild<UIElement>();
    scaleRow->SetLayout(LM_HORIZONTAL, 2);
    scaleRow->SetFixedHeight(22);

    auto* scaleLbl = scaleRow->CreateChild<Text>();
    scaleLbl->SetFont(font_, 10);
    scaleLbl->SetText("Scl:");
    scaleLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    scaleLbl->SetFixedWidth(30);

    kfScaleX_ = makeEdit(scaleRow, 88);
    kfScaleY_ = makeEdit(scaleRow, 88);
    kfScaleZ_ = makeEdit(scaleRow, 88);

    // Button row: Apply | Insert | Delete | Save
    auto* kfBtnRow = keyframeSection_->CreateChild<UIElement>();
    kfBtnRow->SetLayout(LM_HORIZONTAL, 4);
    kfBtnRow->SetFixedHeight(26);

    auto makeBtn = [&](UIElement* parent, const String& label, int w) -> Button*
    {
        auto* btn = parent->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetFixedSize(w, 24);
        auto* lbl = btn->CreateChild<Text>();
        lbl->SetFont(font_, 11);
        lbl->SetText(label);
        lbl->SetAlignment(HA_CENTER, VA_CENTER);
        return btn;
    };

    auto* applyBtn = makeBtn(kfBtnRow, "Apply", 60);
    SubscribeToEvent(applyBtn, E_RELEASED, [this](StringHash, VariantMap&) { ApplyKeyframeEdits(); });

    auto* insertBtn = makeBtn(kfBtnRow, "Insert", 60);
    SubscribeToEvent(insertBtn, E_RELEASED, [this](StringHash, VariantMap&) { InsertKeyframeAtCurrentTime(); });

    auto* deleteBtn = makeBtn(kfBtnRow, "Delete", 60);
    SubscribeToEvent(deleteBtn, E_RELEASED, [this](StringHash, VariantMap&) { DeleteSelectedKeyframe(); });

    auto* saveAnimBtn = makeBtn(kfBtnRow, "Save", 60);
    SubscribeToEvent(saveAnimBtn, E_RELEASED, [this](StringHash, VariantMap&) { SaveAnimation(); });

    // ── Animation Editor Phase 4: Trim & Save As ─────────────────────────────
    // Two time fields plus "Set" buttons that snap from current scrub time, and
    // a "Trim As..." button that opens a FileSelector to write a trimmed copy.
    auto* trimRow = keyframeSection_->CreateChild<UIElement>();
    trimRow->SetLayout(LM_HORIZONTAL, 4);
    trimRow->SetFixedHeight(24);

    auto* trimLbl = trimRow->CreateChild<Text>();
    trimLbl->SetFont(font_, 10);
    trimLbl->SetText("Trim:");
    trimLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    trimLbl->SetFixedWidth(36);

    auto* inLbl = trimRow->CreateChild<Text>();
    inLbl->SetFont(font_, 10);
    inLbl->SetText("in");
    inLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    inLbl->SetFixedWidth(14);

    trimInEdit_ = trimRow->CreateChild<LineEdit>();
    trimInEdit_->SetStyleAuto();
    trimInEdit_->SetFixedHeight(20);
    trimInEdit_->SetFixedWidth(60);
    trimInEdit_->SetText("0.000");

    auto* setInBtn = trimRow->CreateChild<Button>();
    setInBtn->SetStyleAuto();
    setInBtn->SetFixedSize(40, 20);
    {
        auto* lbl = setInBtn->CreateChild<Text>();
        lbl->SetFont(font_, 10);
        lbl->SetText("Set");
        lbl->SetAlignment(HA_CENTER, VA_CENTER);
    }
    SubscribeToEvent(setInBtn, E_RELEASED, [this](StringHash, VariantMap&) { SetTrimInToCurrentTime(); });

    auto* outLbl = trimRow->CreateChild<Text>();
    outLbl->SetFont(font_, 10);
    outLbl->SetText("out");
    outLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    outLbl->SetFixedWidth(20);

    trimOutEdit_ = trimRow->CreateChild<LineEdit>();
    trimOutEdit_->SetStyleAuto();
    trimOutEdit_->SetFixedHeight(20);
    trimOutEdit_->SetFixedWidth(60);
    trimOutEdit_->SetText("0.000");

    auto* setOutBtn = trimRow->CreateChild<Button>();
    setOutBtn->SetStyleAuto();
    setOutBtn->SetFixedSize(40, 20);
    {
        auto* lbl = setOutBtn->CreateChild<Text>();
        lbl->SetFont(font_, 10);
        lbl->SetText("Set");
        lbl->SetAlignment(HA_CENTER, VA_CENTER);
    }
    SubscribeToEvent(setOutBtn, E_RELEASED, [this](StringHash, VariantMap&) { SetTrimOutToCurrentTime(); });

    auto* trimActionRow = keyframeSection_->CreateChild<UIElement>();
    trimActionRow->SetLayout(LM_HORIZONTAL, 4);
    trimActionRow->SetFixedHeight(24);

    auto* trimSaveBtn = trimActionRow->CreateChild<Button>();
    trimSaveBtn->SetStyleAuto();
    trimSaveBtn->SetFixedHeight(24);
    {
        auto* lbl = trimSaveBtn->CreateChild<Text>();
        lbl->SetFont(font_, 11);
        lbl->SetText("Trim As...");
        lbl->SetAlignment(HA_CENTER, VA_CENTER);
        lbl->SetColor(Color(0.9f, 0.85f, 0.5f));
    }
    SubscribeToEvent(trimSaveBtn, E_RELEASED, [this](StringHash, VariantMap&) { TrimAndSaveAs(); });

    auto* splitBtn = trimActionRow->CreateChild<Button>();
    splitBtn->SetStyleAuto();
    splitBtn->SetFixedHeight(24);
    {
        auto* lbl = splitBtn->CreateChild<Text>();
        lbl->SetFont(font_, 11);
        lbl->SetText("Split @ Scrub");
        lbl->SetAlignment(HA_CENTER, VA_CENTER);
        lbl->SetColor(Color(0.5f, 0.85f, 0.9f));
    }
    SubscribeToEvent(splitBtn, E_RELEASED, [this](StringHash, VariantMap&) { SplitAtCurrentTime(); });
}

void ModelTool::RebuildTrackList()
{
    if (!trackDropDown_)
        return;

    trackDropDown_->RemoveAllItems();
    trackNames_.Clear();
    selectedTrackIndex_ = -1;
    selectedKeyframeIndex_ = -1;

    if (!animController_ || currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    const auto& tracks = anim->GetTracks();
    for (auto it = tracks.Begin(); it != tracks.End(); ++it)
    {
        trackNames_.Push(it->second_.name_);
    }

    // Sort alphabetically for consistent UI
    Sort(trackNames_.Begin(), trackNames_.End());

    for (unsigned i = 0; i < trackNames_.Size(); ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);
        item->SetText(trackNames_[i]);
        item->SetColor(Color(0.85f, 0.85f, 0.85f));
        item->SetMinWidth(200);
        trackDropDown_->AddItem(item);
    }

    if (keyframeTitle_)
    {
        String arrow = (keyframeSection_ && keyframeSection_->IsVisible()) ? "v " : "> ";
        keyframeTitle_->SetText(arrow + "Keyframes (" + String(tracks.Size()) + " tracks)");
    }
}

void ModelTool::SelectTrack(int index)
{
    selectedTrackIndex_ = index;
    selectedKeyframeIndex_ = -1;
    RebuildKeyframeList();
}

void ModelTool::RebuildKeyframeList()
{
    if (!keyframeContainer_)
        return;

    keyframeContainer_->RemoveAllChildren();
    keyframeButtons_.Clear();

    if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= (int)trackNames_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    AnimationTrack* track = anim->GetTrack(trackNames_[selectedTrackIndex_]);
    if (!track)
        return;

    for (int i = 0; i < (int)track->GetNumKeyFrames(); ++i)
    {
        const AnimationKeyFrame* kf = const_cast<AnimationTrack*>(track)->GetKeyFrame(i);
        if (!kf)
            continue;

        auto* btn = keyframeContainer_->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetFixedHeight(20);
        btn->SetMinWidth(300);
        btn->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 1, 4, 1));

        auto* timeText = btn->CreateChild<Text>();
        timeText->SetFont(font_, 10);
        timeText->SetText(String((double)kf->time_, 3) + "s");
        timeText->SetColor(Color(0.9f, 0.9f, 0.5f));
        timeText->SetFixedWidth(50);

        auto* posText = btn->CreateChild<Text>();
        posText->SetFont(font_, 10);
        posText->SetText(String((double)kf->position_.x_, 2) + "," +
                         String((double)kf->position_.y_, 2) + "," +
                         String((double)kf->position_.z_, 2));
        posText->SetColor(Color(0.7f, 0.9f, 0.7f));

        int kfIndex = i;
        SubscribeToEvent(btn, E_RELEASED, [this, kfIndex](StringHash, VariantMap&)
        {
            SelectKeyframe(kfIndex);
        });

        // Highlight selected
        if (i == selectedKeyframeIndex_)
            btn->SetColor(Color(0.3f, 0.3f, 0.5f));

        keyframeButtons_.Push(btn);
    }
}

void ModelTool::SelectKeyframe(int index)
{
    selectedKeyframeIndex_ = index;

    if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= (int)trackNames_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    AnimationTrack* track = anim->GetTrack(trackNames_[selectedTrackIndex_]);
    if (!track || index < 0 || index >= (int)track->GetNumKeyFrames())
        return;

    AnimationKeyFrame* kf = track->GetKeyFrame(index);
    if (!kf)
        return;

    // Populate transform editors
    if (kfTimeText_)
        kfTimeText_->SetText("Time: " + String((double)kf->time_, 4) + "s");

    if (kfPosX_) kfPosX_->SetText(String((double)kf->position_.x_, 4));
    if (kfPosY_) kfPosY_->SetText(String((double)kf->position_.y_, 4));
    if (kfPosZ_) kfPosZ_->SetText(String((double)kf->position_.z_, 4));

    if (kfRotX_) kfRotX_->SetText(String((double)kf->rotation_.x_, 4));
    if (kfRotY_) kfRotY_->SetText(String((double)kf->rotation_.y_, 4));
    if (kfRotZ_) kfRotZ_->SetText(String((double)kf->rotation_.z_, 4));
    if (kfRotW_) kfRotW_->SetText(String((double)kf->rotation_.w_, 4));

    if (kfScaleX_) kfScaleX_->SetText(String((double)kf->scale_.x_, 4));
    if (kfScaleY_) kfScaleY_->SetText(String((double)kf->scale_.y_, 4));
    if (kfScaleZ_) kfScaleZ_->SetText(String((double)kf->scale_.z_, 4));

    // Scrub animation to this keyframe time
    if (animController_ && currentAnimIndex_ >= 0)
        animController_->SetTime(availableAnims_[currentAnimIndex_], kf->time_);

    RebuildKeyframeList();  // refresh highlight
}

void ModelTool::ApplyKeyframeEdits()
{
    if (selectedTrackIndex_ < 0 || selectedKeyframeIndex_ < 0)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    AnimationTrack* track = anim->GetTrack(trackNames_[selectedTrackIndex_]);
    if (!track || selectedKeyframeIndex_ >= (int)track->GetNumKeyFrames())
        return;

    AnimationKeyFrame* kf = track->GetKeyFrame(selectedKeyframeIndex_);
    if (!kf)
        return;

    // Read values from line edits
    if (kfPosX_ && kfPosY_ && kfPosZ_)
    {
        kf->position_.x_ = ToFloat(kfPosX_->GetText());
        kf->position_.y_ = ToFloat(kfPosY_->GetText());
        kf->position_.z_ = ToFloat(kfPosZ_->GetText());
    }

    if (kfRotX_ && kfRotY_ && kfRotZ_ && kfRotW_)
    {
        kf->rotation_.x_ = ToFloat(kfRotX_->GetText());
        kf->rotation_.y_ = ToFloat(kfRotY_->GetText());
        kf->rotation_.z_ = ToFloat(kfRotZ_->GetText());
        kf->rotation_.w_ = ToFloat(kfRotW_->GetText());
        kf->rotation_.Normalize();
    }

    if (kfScaleX_ && kfScaleY_ && kfScaleZ_)
    {
        kf->scale_.x_ = ToFloat(kfScaleX_->GetText());
        kf->scale_.y_ = ToFloat(kfScaleY_->GetText());
        kf->scale_.z_ = ToFloat(kfScaleZ_->GetText());
    }

    keyframeDirty_ = true;

    // Re-apply pose at current time to see the change
    if (animController_ && currentAnimIndex_ >= 0)
    {
        float t = animController_->GetTime(availableAnims_[currentAnimIndex_]);
        animController_->SetTime(availableAnims_[currentAnimIndex_], t);
    }

    RebuildKeyframeList();
    URHO3D_LOGINFO("Keyframe updated");
}

void ModelTool::InsertKeyframeAtCurrentTime()
{
    if (selectedTrackIndex_ < 0 || selectedTrackIndex_ >= (int)trackNames_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim || !animController_)
        return;

    AnimationTrack* track = anim->GetTrack(trackNames_[selectedTrackIndex_]);
    if (!track)
        return;

    float t = animController_->GetTime(availableAnims_[currentAnimIndex_]);

    // Get interpolated bone transform at current time from the animation state
    AnimationKeyFrame kf;
    kf.time_ = t;

    // If track has keyframes, interpolate to get current pose
    if (track->GetNumKeyFrames() > 0)
    {
        // Find surrounding keyframes
        i32 idx = 0;
        track->GetKeyFrameIndex(t, idx);
        AnimationKeyFrame* prev = track->GetKeyFrame(idx);
        AnimationKeyFrame* next = (idx + 1 < track->GetNumKeyFrames()) ? track->GetKeyFrame(idx + 1) : nullptr;

        if (prev && next && next->time_ > prev->time_)
        {
            float factor = (t - prev->time_) / (next->time_ - prev->time_);
            kf.position_ = prev->position_.Lerp(next->position_, factor);
            kf.rotation_ = prev->rotation_.Slerp(next->rotation_, factor);
            kf.scale_ = prev->scale_.Lerp(next->scale_, factor);
        }
        else if (prev)
        {
            kf.position_ = prev->position_;
            kf.rotation_ = prev->rotation_;
            kf.scale_ = prev->scale_;
        }
    }
    else
    {
        kf.position_ = Vector3::ZERO;
        kf.rotation_ = Quaternion::IDENTITY;
        kf.scale_ = Vector3::ONE;
    }

    track->AddKeyFrame(kf);
    keyframeDirty_ = true;

    RebuildKeyframeList();
    URHO3D_LOGINFOF("Inserted keyframe at %.3fs on track %s", t, trackNames_[selectedTrackIndex_].CString());
}

void ModelTool::DeleteSelectedKeyframe()
{
    if (selectedTrackIndex_ < 0 || selectedKeyframeIndex_ < 0)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    AnimationTrack* track = anim->GetTrack(trackNames_[selectedTrackIndex_]);
    if (!track || selectedKeyframeIndex_ >= (int)track->GetNumKeyFrames())
        return;

    track->RemoveKeyFrame(selectedKeyframeIndex_);
    selectedKeyframeIndex_ = -1;
    keyframeDirty_ = true;

    RebuildKeyframeList();
    URHO3D_LOGINFO("Keyframe deleted");
}

void ModelTool::SaveAnimation()
{
    if (currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    // Resolve to a writable file path
    String animPath = availableAnims_[currentAnimIndex_];
    String fullPath = cache->GetResourceFileName(animPath);
    if (fullPath.Empty())
    {
        URHO3D_LOGERRORF("SaveAnimation: cannot resolve path for %s", animPath.CString());
        return;
    }

    File file(context_, fullPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERRORF("SaveAnimation: cannot open %s for writing", fullPath.CString());
        return;
    }

    if (anim->Save(file))
    {
        keyframeDirty_ = false;
        URHO3D_LOGINFOF("Animation saved: %s", fullPath.CString());
    }
    else
    {
        URHO3D_LOGERROR("SaveAnimation: save failed");
    }
}

// ============================================================================
// Pose Capture & Animation Creation
// ============================================================================

void ModelTool::CreatePosePanel()
{
    auto* ui = GetSubsystem<UI>();

    posePanel_ = ui->GetRoot()->CreateChild<Window>("PosePanel");
    posePanel_->SetStyleAuto();
    posePanel_->SetFixedWidth(300);
    posePanel_->SetLayout(LM_VERTICAL, 4, IntRect(8, 6, 8, 6));
    posePanel_->SetPosition(360, 36);
    posePanel_->SetMovable(true);
    posePanel_->SetOpacity(0.92f);
    posePanel_->SetColor(Color(0.22f, 0.18f, 0.22f));
    posePanel_->SetVisible(false);

    auto* title = posePanel_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Pose Studio");
    title->SetColor(Color(0.95f, 0.7f, 0.95f));

    // Capture button
    auto* captureBtn = posePanel_->CreateChild<Button>();
    captureBtn->SetStyleAuto();
    captureBtn->SetFixedHeight(28);
    auto* capLbl = captureBtn->CreateChild<Text>();
    capLbl->SetFont(font_, 12);
    capLbl->SetText("Capture Current Pose");
    capLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(captureBtn, E_RELEASED, [this](StringHash, VariantMap&) { CapturePose(); });

    // Pose list
    poseListView_ = posePanel_->CreateChild<ListView>();
    poseListView_->SetStyleAuto();
    poseListView_->SetHighlightMode(HM_ALWAYS);
    poseListView_->SetMinHeight(60);
    poseListView_->SetMaxHeight(120);

    // Insert pose row
    auto* insertRow = posePanel_->CreateChild<UIElement>();
    insertRow->SetLayout(LM_HORIZONTAL, 4);
    insertRow->SetFixedHeight(28);

    auto* insertBtn = insertRow->CreateChild<Button>();
    insertBtn->SetStyleAuto();
    insertBtn->SetFixedSize(140, 26);
    auto* insLbl = insertBtn->CreateChild<Text>();
    insLbl->SetFont(font_, 11);
    insLbl->SetText("Insert at Time");
    insLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(insertBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        int sel = poseListView_ ? poseListView_->GetSelection() : -1;
        if (sel >= 0 && sel < (int)capturedPoses_.Size())
            InsertPoseAtTime(sel);
    });

    auto* deleteBtn = insertRow->CreateChild<Button>();
    deleteBtn->SetStyleAuto();
    deleteBtn->SetFixedSize(70, 26);
    auto* delLbl = deleteBtn->CreateChild<Text>();
    delLbl->SetFont(font_, 11);
    delLbl->SetText("Delete");
    delLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(deleteBtn, E_RELEASED, [this](StringHash, VariantMap&)
    {
        int sel = poseListView_ ? poseListView_->GetSelection() : -1;
        if (sel >= 0 && sel < (int)capturedPoses_.Size())
        {
            capturedPoses_.Erase(sel);
            RebuildPoseList();
        }
    });

    // Separator
    auto* sep = posePanel_->CreateChild<BorderImage>();
    sep->SetFixedHeight(1);
    sep->SetColor(Color(0.4f, 0.3f, 0.4f));

    // New animation section
    auto* newTitle = posePanel_->CreateChild<Text>();
    newTitle->SetFont(font_, 12);
    newTitle->SetText("New Animation");
    newTitle->SetColor(Color(0.7f, 0.9f, 0.7f));

    // Name row
    auto* nameRow = posePanel_->CreateChild<UIElement>();
    nameRow->SetLayout(LM_HORIZONTAL, 4);
    nameRow->SetFixedHeight(24);
    auto* nameLbl = nameRow->CreateChild<Text>();
    nameLbl->SetFont(font_, 11);
    nameLbl->SetText("Name:");
    nameLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    nameLbl->SetFixedWidth(50);
    newAnimNameEdit_ = nameRow->CreateChild<LineEdit>();
    newAnimNameEdit_->SetStyleAuto();
    newAnimNameEdit_->SetFixedHeight(22);
    newAnimNameEdit_->SetText("Sleep");

    // Length row
    auto* lenRow = posePanel_->CreateChild<UIElement>();
    lenRow->SetLayout(LM_HORIZONTAL, 4);
    lenRow->SetFixedHeight(24);
    auto* lenLbl = lenRow->CreateChild<Text>();
    lenLbl->SetFont(font_, 11);
    lenLbl->SetText("Length:");
    lenLbl->SetColor(Color(0.7f, 0.7f, 0.7f));
    lenLbl->SetFixedWidth(50);
    newAnimLengthEdit_ = lenRow->CreateChild<LineEdit>();
    newAnimLengthEdit_->SetStyleAuto();
    newAnimLengthEdit_->SetFixedHeight(22);
    newAnimLengthEdit_->SetText("2.0");

    // Create button
    auto* createBtn = posePanel_->CreateChild<Button>();
    createBtn->SetStyleAuto();
    createBtn->SetFixedHeight(28);
    auto* creLbl = createBtn->CreateChild<Text>();
    creLbl->SetFont(font_, 12);
    creLbl->SetText("Create Animation");
    creLbl->SetAlignment(HA_CENTER, VA_CENTER);
    creLbl->SetColor(Color(0.3f, 1.0f, 0.3f));
    SubscribeToEvent(createBtn, E_RELEASED, [this](StringHash, VariantMap&) { CreateNewAnimation(); });
}

void ModelTool::ToggleBoneFilter(int boneIndex)
{
    if (!isAnimated_ || !animatedModelComp_) return;
    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (boneIndex < 0 || boneIndex >= (int)bones.Size()) return;

    bool adding = !poseBoneFilter_.Contains(boneIndex);

    // Toggle this bone + all descendants
    Vector<bool> inSubtree(bones.Size(), false);
    inSubtree[boneIndex] = true;
    for (unsigned i = (unsigned)boneIndex + 1; i < bones.Size(); ++i)
    {
        int p = bones[i].parentIndex_;
        if (p >= 0 && p < (int)bones.Size() && inSubtree[p])
            inSubtree[i] = true;
    }

    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (!inSubtree[i]) continue;
        if (adding)
            poseBoneFilter_.Insert((int)i);
        else
            poseBoneFilter_.Erase((int)i);
    }

    unsigned count = poseBoneFilter_.Size();
    if (statusText_)
        statusText_->SetText(adding
            ? "Pose filter: added " + bones[boneIndex].name_ + " + children (" + String(count) + " bones)"
            : "Pose filter: removed " + bones[boneIndex].name_ + " + children (" + String(count) + " bones)");

    // Update bone list button colors to show filter state
    for (unsigned i = 0; i < boneButtons_.Size(); ++i)
    {
        int bi = boneButtons_[i]->GetVar("BoneIndex").GetI32();
        bool inFilter = poseBoneFilter_.Contains(bi);
        bool selected = (bi == selectedBone_);
        auto* label = boneButtons_[i]->GetChildStaticCast<Text>(0);
        if (label)
        {
            if (selected)
                label->SetColor(Color(1.0f, 1.0f, 0.4f));
            else if (inFilter)
                label->SetColor(Color(0.4f, 1.0f, 0.4f));  // green = in filter
            else
                label->SetColor(Color(0.85f, 0.85f, 0.85f));
        }
    }
}

void ModelTool::ClearBoneFilter()
{
    poseBoneFilter_.Clear();
    if (statusText_)
        statusText_->SetText("Pose filter cleared — all bones included");
    // Reset button colors
    for (unsigned i = 0; i < boneButtons_.Size(); ++i)
    {
        int bi = boneButtons_[i]->GetVar("BoneIndex").GetI32();
        bool selected = (bi == selectedBone_);
        auto* label = boneButtons_[i]->GetChildStaticCast<Text>(0);
        if (label)
            label->SetColor(selected ? Color(1.0f, 1.0f, 0.4f) : Color(0.85f, 0.85f, 0.85f));
    }
}

void ModelTool::HandleBoneRotationSlider(StringHash, VariantMap& eventData)
{
    if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_) return;

    Skeleton& skeleton = animatedModelComp_->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();
    if (selectedBone_ >= (int)bones.Size()) return;

    Node* boneNode = bones[selectedBone_].node_;
    if (!boneNode) return;

    // Sliders range 0-360, center at 180 = no change
    float rx = boneRotX_ ? (boneRotX_->GetValue() - 180.0f) : 0.0f;
    float ry = boneRotY_ ? (boneRotY_->GetValue() - 180.0f) : 0.0f;
    float rz = boneRotZ_ ? (boneRotZ_->GetValue() - 180.0f) : 0.0f;

    if (boneRotXLabel_) { char b[16]; snprintf(b, 16, "X: %.0f", rx); boneRotXLabel_->SetText(b); }
    if (boneRotYLabel_) { char b[16]; snprintf(b, 16, "Y: %.0f", ry); boneRotYLabel_->SetText(b); }
    if (boneRotZLabel_) { char b[16]; snprintf(b, 16, "Z: %.0f", rz); boneRotZLabel_->SetText(b); }

    // Apply as Euler rotation offset on top of the bone's initial rotation
    Quaternion before = boneNode->GetRotation();
    Quaternion offset;
    offset.FromEulerAngles(rx, ry, rz);
    Quaternion after = bones[selectedBone_].initialRotation_ * offset;
    boneNode->SetRotation(after);
    PushBoneRotEdit(selectedBone_, before, after);
}

void ModelTool::HandlePropOffsetSlider(StringHash, VariantMap&)
{
    if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_)
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (selectedBone_ >= (int)bones.Size() || !bones[selectedBone_].node_)
        return;

    Node* attachNode = bones[selectedBone_].node_->GetChild("_PoseAttach");
    if (!attachNode)
        return;

    // Position sliders: range 0-200, center 100 = 0.0, maps to [-1, +1]
    float px = propPosX_ ? (propPosX_->GetValue() - 100.0f) / 100.0f : 0.0f;
    float py = propPosY_ ? (propPosY_->GetValue() - 100.0f) / 100.0f : 0.0f;
    float pz = propPosZ_ ? (propPosZ_->GetValue() - 100.0f) / 100.0f : 0.0f;
    attachNode->SetPosition(Vector3(px, py, pz));

    // Rotation sliders: range 0-360, value = degrees
    float rx = propRotX_ ? propRotX_->GetValue() : 0.0f;
    float ry = propRotY_ ? propRotY_->GetValue() : 0.0f;
    float rz = propRotZ_ ? propRotZ_->GetValue() : 0.0f;
    Quaternion rot;
    rot.FromEulerAngles(rx, ry, rz);
    attachNode->SetRotation(rot);

    // Scale slider: range 0-300, maps to 0.1-3.0
    float s = propScale_ ? (propScale_->GetValue() / 100.0f) : 1.0f;
    s = Max(s, 0.1f);
    attachNode->SetScale(Vector3(s, s, s));
}

void ModelTool::UpdateBoneEditControls()
{
    if (!boneEditPanel_) return;

    if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_)
    {
        boneEditPanel_->SetVisible(false);
        return;
    }

    boneEditPanel_->SetVisible(true);

    // Reset sliders to center (no offset) when switching bones
    if (boneRotX_) boneRotX_->SetValue(180.0f);
    if (boneRotY_) boneRotY_->SetValue(180.0f);
    if (boneRotZ_) boneRotZ_->SetValue(180.0f);
    if (boneRotXLabel_) boneRotXLabel_->SetText("X: 0");
    if (boneRotYLabel_) boneRotYLabel_->SetText("Y: 0");
    if (boneRotZLabel_) boneRotZLabel_->SetText("Z: 0");
}

void ModelTool::CapturePose()
{
    if (!isAnimated_ || !animatedModelComp_)
        return;

    Skeleton& skeleton = animatedModelComp_->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();
    if (bones.Empty())
        return;

    CapturedPose pose;

    // Name: animation name + time
    String animName = "bind";
    float time = 0.0f;
    if (animController_ && currentAnimIndex_ >= 0 && currentAnimIndex_ < (int)availableAnims_.Size())
    {
        animName = GetFileName(availableAnims_[currentAnimIndex_]);
        time = animController_->GetTime(availableAnims_[currentAnimIndex_]);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s@%.2fs", animName.CString(), (double)time);
    pose.name = buf;

    // Capture each bone's current local transform from the scene node.
    // Priority: manual bone filter > edit history dirty set > all bones
    HashSet<int> dirtyBones;
    if (!poseBoneFilter_.Empty())
        dirtyBones = poseBoneFilter_;  // manual filter takes precedence
    else
        dirtyBones = GetDirtyBones();  // auto-detect from edit history

    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (!dirtyBones.Empty() && !dirtyBones.Contains((int)i))
            continue;

        const Bone& bone = bones[i];
        CapturedPose::BoneTransform bt;
        bt.boneName = bone.name_;

        if (bone.node_)
        {
            bt.position = bone.node_->GetPosition();
            bt.rotation = bone.node_->GetRotation();
            bt.scale = bone.node_->GetScale();
        }
        else
        {
            bt.position = bone.initialPosition_;
            bt.rotation = bone.initialRotation_;
            bt.scale = bone.initialScale_;
        }
        pose.bones.Push(bt);
    }

    capturedPoses_.Push(pose);
    RebuildPoseList();

    // Pause animation so the user can see what they captured
    if (animPlaying_ && animController_ && currentAnimIndex_ >= 0 && currentAnimIndex_ < (int)availableAnims_.Size())
    {
        animPlaying_ = false;
        animController_->SetSpeed(availableAnims_[currentAnimIndex_], 0.0f);
        if (playPauseBtn_)
        {
            auto* lbl = playPauseBtn_->GetChildStaticCast<Text>(0);
            if (lbl) lbl->SetText(">");
        }
    }

    // Flash status
    if (statusText_)
    {
        statusText_->SetText("Captured: " + pose.name);
        statusText_->SetColor(Color(0.5f, 1.0f, 0.5f));
    }

    // Select the newly captured pose in the list
    if (poseListView_)
        poseListView_->SetSelection(capturedPoses_.Size() - 1);

    URHO3D_LOGINFOF("Captured pose '%s' (%u bones)", pose.name.CString(), pose.bones.Size());
}

void ModelTool::InsertPoseAtTime(int poseIndex)
{
    if (poseIndex < 0 || poseIndex >= (int)capturedPoses_.Size())
        return;
    if (currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    float time = 0.0f;
    if (animController_)
        time = animController_->GetTime(availableAnims_[currentAnimIndex_]);

    const CapturedPose& pose = capturedPoses_[poseIndex];

    for (unsigned i = 0; i < pose.bones.Size(); ++i)
    {
        const CapturedPose::BoneTransform& bt = pose.bones[i];
        AnimationTrack* track = anim->GetTrack(bt.boneName);
        if (!track)
        {
            track = anim->CreateTrack(bt.boneName);
            track->channelMask_ = AnimationChannels::Position | AnimationChannels::Rotation | AnimationChannels::Scale;
        }

        AnimationKeyFrame kf;
        kf.time_ = time;
        kf.position_ = bt.position;
        kf.rotation_ = bt.rotation;
        kf.scale_ = bt.scale;
        track->AddKeyFrame(kf);
    }

    keyframeDirty_ = true;
    RebuildInfoText();
    URHO3D_LOGINFOF("Inserted pose '%s' at t=%.3fs into '%s'",
        pose.name.CString(), (double)time, anim->GetAnimationName().CString());
}

void ModelTool::CreateNewAnimation()
{
    if (!isAnimated_ || !animatedModelComp_ || !currentModel_)
        return;

    String animName = newAnimNameEdit_ ? newAnimNameEdit_->GetText().Trimmed() : "NewAnim";
    float length = newAnimLengthEdit_ ? ToFloat(newAnimLengthEdit_->GetText()) : 2.0f;
    if (length < 0.01f) length = 2.0f;
    if (animName.Empty()) animName = "NewAnim";

    // Build output path next to the model
    auto* cache = GetSubsystem<ResourceCache>();
    String modelFullPath = cache->GetResourceFileName(currentModelPath_);
    String dir = GetPath(modelFullPath);
    String modelBase = GetFileName(currentModelPath_);
    String animPath = dir + modelBase + "_" + animName + ".ani";

    // Create the animation
    SharedPtr<Animation> newAnim(new Animation(context_));
    newAnim->SetAnimationName(animName);
    newAnim->SetLength(length);

    // If poses are captured, insert the first at t=0 and (if 2+) the second at t=length
    if (!capturedPoses_.Empty())
    {
        const CapturedPose& pose0 = capturedPoses_[0];
        for (unsigned i = 0; i < pose0.bones.Size(); ++i)
        {
            const CapturedPose::BoneTransform& bt = pose0.bones[i];
            AnimationTrack* track = newAnim->CreateTrack(bt.boneName);
            track->channelMask_ = AnimationChannels::Position | AnimationChannels::Rotation | AnimationChannels::Scale;

            AnimationKeyFrame kf0;
            kf0.time_ = 0.0f;
            kf0.position_ = bt.position;
            kf0.rotation_ = bt.rotation;
            kf0.scale_ = bt.scale;
            track->AddKeyFrame(kf0);

            // Second pose at end of animation (for breathing cycle etc.)
            if (capturedPoses_.Size() >= 2)
            {
                // Look up matching bone by name, not index — poses may
                // have different bone orderings or counts.
                const CapturedPose& pose1 = capturedPoses_[1];
                bool found = false;
                for (unsigned j = 0; j < pose1.bones.Size(); ++j)
                {
                    if (pose1.bones[j].boneName == bt.boneName)
                    {
                        const CapturedPose::BoneTransform& bt1 = pose1.bones[j];
                        AnimationKeyFrame kf1;
                        kf1.time_ = length;
                        kf1.position_ = bt1.position;
                        kf1.rotation_ = bt1.rotation;
                        kf1.scale_ = bt1.scale;
                        track->AddKeyFrame(kf1);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    // Bone missing in pose 1 — hold pose 0
                    AnimationKeyFrame kf1 = kf0;
                    kf1.time_ = length;
                    track->AddKeyFrame(kf1);
                }
            }
            else
            {
                // Duplicate pose at end for a static hold
                AnimationKeyFrame kf1 = kf0;
                kf1.time_ = length;
                track->AddKeyFrame(kf1);
            }
        }
    }

    // Save to disk
    File file(context_, animPath, FILE_WRITE);
    if (file.IsOpen() && newAnim->Save(file))
    {
        file.Close();
        URHO3D_LOGINFOF("Created animation: %s (%.2fs, %u tracks)", animPath.CString(),
            (double)length, newAnim->GetNumTracks());

        // Load it into the viewer
        String resPath = GetFileName(currentModelPath_);
        resPath = GetPath(currentModelPath_) + modelBase + "_" + animName + ".ani";
        if (LoadAnimationFile(animPath))
        {
            RebuildAnimList();
            // Select the new animation
            for (unsigned i = 0; i < availableAnims_.Size(); ++i)
            {
                if (availableAnims_[i].Contains(animName))
                {
                    PlayAnimation(i);
                    break;
                }
            }
        }
    }
    else
    {
        URHO3D_LOGERROR("Failed to save new animation: " + animPath);
    }
}

void ModelTool::RebuildPoseList()
{
    if (!poseListView_) return;
    poseListView_->RemoveAllItems();

    for (unsigned i = 0; i < capturedPoses_.Size(); ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);
        item->SetText(String(i) + ": " + capturedPoses_[i].name +
            " (" + String(capturedPoses_[i].bones.Size()) + " bones)");
        item->SetColor(Color(0.85f, 0.75f, 0.95f));
        poseListView_->AddItem(item);
    }
}

// ============================================================================
// Animation Editor Phase 4 — Trim & Save As
// ============================================================================

void ModelTool::SetTrimInToCurrentTime()
{
    if (!animController_ || currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
        return;
    if (!trimInEdit_)
        return;
    float t = animController_->GetTime(availableAnims_[currentAnimIndex_]);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", (double)t);
    trimInEdit_->SetText(buf);
}

void ModelTool::SetTrimOutToCurrentTime()
{
    if (!animController_ || currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
        return;
    if (!trimOutEdit_)
        return;
    float t = animController_->GetTime(availableAnims_[currentAnimIndex_]);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", (double)t);
    trimOutEdit_->SetText(buf);
}

void ModelTool::TrimAndSaveAs()
{
    if (currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
    {
        URHO3D_LOGWARNING("TrimAndSaveAs: no animation loaded");
        return;
    }
    if (!trimInEdit_ || !trimOutEdit_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
    {
        URHO3D_LOGERROR("TrimAndSaveAs: animation resource missing");
        return;
    }

    float trimIn  = ToFloat(trimInEdit_->GetText());
    float trimOut = ToFloat(trimOutEdit_->GetText());
    float length  = anim->GetLength();

    // Validate the trim window
    if (trimIn < 0.0f) trimIn = 0.0f;
    if (trimOut > length) trimOut = length;
    if (trimOut <= trimIn + 1e-4f)
    {
        URHO3D_LOGWARNINGF("TrimAndSaveAs: invalid range in=%.3f out=%.3f length=%.3f",
            (double)trimIn, (double)trimOut, (double)length);
        return;
    }

    // Save these for the FileSelector callback
    const float fIn  = trimIn;
    const float fOut = trimOut;
    const String sourcePath = availableAnims_[currentAnimIndex_];

    // Open FileSelector for the destination .ani path
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();
    auto* fs = GetSubsystem<FileSystem>();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Save trimmed animation as...");
    fileSelector_->SetButtonTexts("Save", "Cancel");

    Vector<String> filters;
    filters.Push("*.ani");
    fileSelector_->SetFilters(filters, 0);

    // Default to the source animation's directory
    String startDir;
    const Vector<String>& dirs = cache->GetResourceDirs();
    String sourceDir = GetParentPath(sourcePath);
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (fs->DirExists(dirs[i] + sourceDir))
        {
            startDir = dirs[i] + sourceDir;
            break;
        }
    }
    if (startDir.Empty() && !dirs.Empty())
        startDir = dirs[0];
    fileSelector_->SetPath(startDir);

    // Suggest a default filename: <stem>_trim.ani
    String suggestedName = GetFileName(sourcePath) + "_trim.ani";
    fileSelector_->SetFileName(suggestedName);

    SubscribeToEvent(fileSelector_, E_FILESELECTED, [this, fIn, fOut, sourcePath](StringHash, VariantMap& ed)
    {
        String destPath = ed[FileSelected::P_FILENAME].GetString();
        bool ok = ed[FileSelected::P_OK].GetBool();
        fileSelector_.Reset();
        if (!ok || destPath.Empty())
            return;

        // Re-fetch the animation (the lambda doesn't carry it — avoids stale captures)
        auto* cache = GetSubsystem<ResourceCache>();
        auto* anim = cache->GetResource<Animation>(sourcePath);
        if (!anim)
        {
            URHO3D_LOGERROR("TrimAndSaveAs callback: source animation gone");
            return;
        }

        if (!destPath.EndsWith(".ani"))
            destPath += ".ani";

        WriteTrimmedAnimation(anim, fIn, fOut, destPath);
    });
}

bool ModelTool::WriteTrimmedAnimation(Animation* source, float in, float out, const String& destPath)
{
    if (!source)
        return false;

    // Clone preserves tracks, textKeys, triggers, length
    SharedPtr<Animation> trimmed = source->Clone(GetFileName(destPath));
    if (!trimmed)
    {
        URHO3D_LOGERROR("WriteTrimmedAnimation: clone failed");
        return false;
    }

    const float epsilon = 1e-4f;

    // Walk every track and prune+shift its keyframes via public GetTrack(i32).
    i32 trackCount = trimmed->GetNumTracks();
    for (i32 ti = 0; ti < trackCount; ++ti)
    {
        AnimationTrack* track = trimmed->GetTrack(ti);
        if (!track)
            continue;
        Vector<AnimationKeyFrame> kept;
        kept.Reserve(track->keyFrames_.Size());
        for (unsigned k = 0; k < track->keyFrames_.Size(); ++k)
        {
            const AnimationKeyFrame& kf = track->keyFrames_[k];
            if (kf.time_ < in - epsilon || kf.time_ > out + epsilon)
                continue;
            AnimationKeyFrame shifted = kf;
            shifted.time_ = Max(0.0f, kf.time_ - in);
            kept.Push(shifted);
        }
        track->keyFrames_ = kept;
    }

    // Prune and shift text keys via the public Add/Remove API.
    Vector<AnimationTextKey> keptText;
    for (i32 i = 0; i < trimmed->GetNumTextKeys(); ++i)
    {
        const AnimationTextKey* tk = trimmed->GetTextKey(i);
        if (!tk)
            continue;
        if (tk->time_ < in - epsilon || tk->time_ > out + epsilon)
            continue;
        AnimationTextKey shifted = *tk;
        shifted.time_ = Max(0.0f, tk->time_ - in);
        keptText.Push(shifted);
    }
    trimmed->RemoveAllTextKeys();
    for (unsigned i = 0; i < keptText.Size(); ++i)
        trimmed->AddTextKey(keptText[i].time_, false, keptText[i].name_, keptText[i].data_);

    // Prune and shift triggers (no name, just time + data).
    Vector<AnimationTriggerPoint> keptTrig;
    for (i32 i = 0; i < trimmed->GetNumTriggers(); ++i)
    {
        const AnimationTriggerPoint* tp = trimmed->GetTrigger(i);
        if (!tp)
            continue;
        if (tp->time_ < in - epsilon || tp->time_ > out + epsilon)
            continue;
        AnimationTriggerPoint shifted = *tp;
        shifted.time_ = Max(0.0f, tp->time_ - in);
        keptTrig.Push(shifted);
    }
    trimmed->RemoveAllTriggers();
    for (unsigned i = 0; i < keptTrig.Size(); ++i)
        trimmed->AddTrigger(keptTrig[i]);

    trimmed->SetLength(out - in);

    File outFile(context_, destPath, FILE_WRITE);
    if (!outFile.IsOpen())
    {
        URHO3D_LOGERRORF("WriteTrimmedAnimation: cannot open %s for writing", destPath.CString());
        return false;
    }
    if (!trimmed->Save(outFile))
    {
        URHO3D_LOGERROR("WriteTrimmedAnimation: save failed");
        return false;
    }
    URHO3D_LOGINFOF("Animation slice saved: %s [%.3f → %.3f, length %.3f]",
        destPath.CString(), (double)in, (double)out, (double)(out - in));
    return true;
}

void ModelTool::SplitAtCurrentTime()
{
    if (currentAnimIndex_ < 0 || currentAnimIndex_ >= (int)availableAnims_.Size())
    {
        URHO3D_LOGWARNING("SplitAtCurrentTime: no animation loaded");
        return;
    }
    if (!animController_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
    if (!anim)
        return;

    float splitTime = animController_->GetTime(availableAnims_[currentAnimIndex_]);
    float length = anim->GetLength();
    if (splitTime <= 1e-4f || splitTime >= length - 1e-4f)
    {
        URHO3D_LOGWARNINGF("SplitAtCurrentTime: scrub time %.3f is at or outside [0, %.3f] — nothing to split",
            (double)splitTime, (double)length);
        return;
    }

    const float fSplit = splitTime;
    const float fLength = length;
    const String sourcePath = availableAnims_[currentAnimIndex_];

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();
    auto* fs = GetSubsystem<FileSystem>();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Split — pick base name (writes <base>_a.ani + <base>_b.ani)");
    fileSelector_->SetButtonTexts("Split", "Cancel");

    Vector<String> filters;
    filters.Push("*.ani");
    fileSelector_->SetFilters(filters, 0);

    String startDir;
    const Vector<String>& dirs = cache->GetResourceDirs();
    String sourceDir = GetParentPath(sourcePath);
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (fs->DirExists(dirs[i] + sourceDir))
        {
            startDir = dirs[i] + sourceDir;
            break;
        }
    }
    if (startDir.Empty() && !dirs.Empty())
        startDir = dirs[0];
    fileSelector_->SetPath(startDir);
    fileSelector_->SetFileName(GetFileName(sourcePath) + ".ani");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, [this, fSplit, fLength, sourcePath](StringHash, VariantMap& ed)
    {
        String basePath = ed[FileSelected::P_FILENAME].GetString();
        bool ok = ed[FileSelected::P_OK].GetBool();
        fileSelector_.Reset();
        if (!ok || basePath.Empty())
            return;

        auto* cache = GetSubsystem<ResourceCache>();
        auto* anim = cache->GetResource<Animation>(sourcePath);
        if (!anim)
        {
            URHO3D_LOGERROR("SplitAtCurrentTime callback: source animation gone");
            return;
        }

        // Strip a trailing .ani so we can append _a / _b cleanly
        String stem = basePath;
        if (stem.EndsWith(".ani"))
            stem = stem.Substring(0, stem.Length() - 4);

        const String pathA = stem + "_a.ani";
        const String pathB = stem + "_b.ani";

        bool okA = WriteTrimmedAnimation(anim, 0.0f, fSplit, pathA);
        bool okB = WriteTrimmedAnimation(anim, fSplit, fLength, pathB);
        if (okA && okB)
        {
            URHO3D_LOGINFOF("Split complete at %.3f → %s + %s",
                (double)fSplit, pathA.CString(), pathB.CString());
        }
        else
        {
            URHO3D_LOGERRORF("Split partial — A:%s B:%s", okA ? "ok" : "FAILED", okB ? "ok" : "FAILED");
        }
    });
}

// ============================================================================
// Info Text — rebuild as ListView items
// ============================================================================

void ModelTool::RebuildInfoText()
{
    if (!infoContent_ || !currentModel_) return;

    infoContent_->RemoveAllChildren();
    currentSection_ = nullptr;

    auto* cache = GetSubsystem<ResourceCache>();

    // ---- Assets (expanded) ----
    CreateCollapsibleSection("Assets", true);
    AddInfoLine("Model: " + currentModelPath_);

    String matListPath = ReplaceExtension(currentModelPath_, ".txt");
    if (cache->Exists(matListPath))
        AddInfoLine("MatList: " + matListPath);

    if (staticModelComp_ && staticModelComp_->GetNumGeometries() > 0)
    {
        AddInfoSeparator();
        for (unsigned g = 0; g < staticModelComp_->GetNumGeometries(); ++g)
        {
            Material* mat = staticModelComp_->GetMaterial(g);
            if (!mat) continue;
            AddInfoLine("Material[" + String(g) + "]: " + mat->GetName());
            for (int u = 0; u < MAX_TEXTURE_UNITS; ++u)
            {
                Texture* tex = mat->GetTexture((TextureUnit)u);
                if (tex)
                    AddInfoLine("  Texture: " + tex->GetName());
            }
        }
    }

    if (!availableAnims_.Empty())
    {
        AddInfoSeparator();
        for (unsigned i = 0; i < availableAnims_.Size(); ++i)
            AddInfoLine("Anim: " + GetFileName(availableAnims_[i]));
    }

    // ---- Model Summary (expanded) ----
    CreateCollapsibleSection("Model Summary", true);
    AddInfoLine("Path: " + currentModelPath_);
    AddInfoLine("Type: " + String(isAnimated_ ? "AnimatedModel" : "StaticModel"));

    BoundingBox bbox = currentModel_->GetBoundingBox();
    Vector3 dims = bbox.max_ - bbox.min_;
    AddInfoLine("Size: " + String((double)dims.x_, 2) + " x " + String((double)dims.y_, 2) + " x " + String((double)dims.z_, 2));

    int totalVerts = 0, totalIndices = 0;
    for (int g = 0; g < currentModel_->GetNumGeometries(); ++g)
    {
        Geometry* geom = currentModel_->GetGeometry(g, 0);
        if (geom) { totalVerts += geom->GetVertexCount(); totalIndices += geom->GetIndexCount(); }
    }

    AddInfoLine("Geometries: " + String(currentModel_->GetNumGeometries()));
    AddInfoLine("Vertices: " + String(totalVerts) + "  Triangles: " + String(totalIndices / 3));

    Skeleton& skel = currentModel_->GetSkeleton();
    AddInfoLine("Bones: " + String(skel.GetNumBones()) + "  Morphs: " + String(currentModel_->GetNumMorphs()));

    if (modelNode_->GetScale().x_ < 0.99f)
    {
        AddInfoLine("Auto-scale: " + String((double)modelNode_->GetScale().x_, 4) + " (oversized model)", Color(0.9f, 0.6f, 0.3f));
    }

    // ---- Geometry (collapsed) ----
    for (int g = 0; g < currentModel_->GetNumGeometries(); ++g)
    {
        CreateCollapsibleSection("Geometry " + String(g), false);
        int numLods = currentModel_->GetNumGeometryLodLevels(g);
        AddInfoLine("LOD levels: " + String(numLods));

        for (int l = 0; l < numLods; ++l)
        {
            Geometry* geom = currentModel_->GetGeometry(g, l);
            if (!geom) continue;

            if (numLods > 1)
                AddInfoLine("LOD " + String(l) + " (dist=" + String((double)geom->GetLodDistance(), 1) + ")");

            AddInfoLine("Verts: " + String(geom->GetVertexCount()) +
                "  Idx: " + String(geom->GetIndexCount()) +
                "  Tris: " + String(geom->GetIndexCount() / 3));

            for (int vb = 0; vb < geom->GetNumVertexBuffers(); ++vb)
            {
                VertexBuffer* vbuf = geom->GetVertexBuffer(vb);
                if (!vbuf) continue;

                AddInfoLine("VB" + String(vb) + ": " + String(vbuf->GetVertexSize()) + " bytes/vert");

                const Vector<VertexElement>& elems = vbuf->GetElements();
                for (unsigned e = 0; e < elems.Size(); ++e)
                {
                    const VertexElement& el = elems[e];
                    String s = "  " + SemanticName(el.semantic_);
                    if (el.index_ > 0) s += String((int)el.index_);
                    s += ": " + TypeName(el.type_) + " @ " + String(el.offset_);
                    AddInfoLine(s);
                }
            }

            IndexBuffer* ib = geom->GetIndexBuffer();
            if (ib)
                AddInfoLine("IB: " + String(ib->GetIndexCount()) + " idx, " + String(ib->GetIndexSize()) + " bytes/idx");
        }
    }

    // ---- Skeleton (collapsed) — clickable bone buttons ----
    boneButtons_.Clear();
    if (skel.GetNumBones() > 1)
    {
        CreateCollapsibleSection("Skeleton (" + String(skel.GetNumBones()) + " bones)", false);
        const Vector<Bone>& bones = skel.GetBones();
        for (unsigned i = 0; i < bones.Size(); ++i)
        {
            const Bone& bone = bones[i];
            int depth = 0;
            int p = bone.parentIndex_;
            while (p > 0 && depth < 10) { p = bones[p].parentIndex_; depth++; }

            String indent;
            for (int d = 0; d < depth; ++d) indent += "  ";

            UIElement* target = currentSection_ ? currentSection_ : infoContent_;
            if (!target) continue;

            auto* btn = target->CreateChild<Button>();
            btn->SetStyleAuto();
            btn->SetFixedHeight(18);
            btn->SetLayout(LM_HORIZONTAL, 0, IntRect(2, 0, 2, 0));
            btn->SetColor(Color(0.18f, 0.18f, 0.22f, 0.6f));
            btn->SetVar("BoneIndex", (int)i);

            auto* label = btn->CreateChild<Text>();
            label->SetFont(font_, 11);
            label->SetText(indent + bone.name_ + "  r=" + String((double)bone.radius_, 3));
            label->SetColor(Color(0.85f, 0.85f, 0.85f));
            label->SetAlignment(HA_LEFT, VA_CENTER);

            SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(ModelTool, HandleBoneListClick));
            boneButtons_.Push(btn);
        }
    }

    // ---- Animation Inspector (collapsed) ----
    if (currentAnimIndex_ >= 0 && currentAnimIndex_ < (int)availableAnims_.Size())
    {
        auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_]);
        if (anim)
        {
            CreateCollapsibleSection("Animation Inspector", false);
            AddInfoLine("Name: " + anim->GetAnimationName());
            AddInfoLine("Length: " + String(anim->GetLength(), 3) + "s");
            AddInfoLine("Tracks: " + String(anim->GetNumTracks()));

            const Vector<AnimationTriggerPoint>& triggers = anim->GetTriggers();
            if (!triggers.Empty())
            {
                AddInfoLine("Triggers: " + String(triggers.Size()));
                for (unsigned tr = 0; tr < triggers.Size(); ++tr)
                    AddInfoLine("  @" + String((double)triggers[tr].time_, 3) + "s: " + triggers[tr].data_.ToString());
            }

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

                String s = "  " + track.name_ + " [" + ch + "] " + String(track.GetNumKeyFrames()) + " keys";

                if (track.GetNumKeyFrames() > 0)
                {
                    float t0 = track.keyFrames_[0].time_;
                    float t1 = track.keyFrames_[track.keyFrames_.Size() - 1].time_;
                    s += "  t:" + String((double)t0, 2) + "->" + String((double)t1, 2);
                }
                AddInfoLine(s);
            }
            AddInfoLine("Total keyframes: " + String(totalKeys));
        }
    }

    // ---- Animation Catalogue (collapsed) ----
    if (availableAnims_.Size() > 1) // >1 because [Bind Pose] is always there
    {
        CreateCollapsibleSection("Animation Catalogue (" + String(availableAnims_.Size() - 1) + " anims)", false);

        // Check for essential animation types
        const char* essentials[] = {"idle", "walk", "run", "eat", "die", "attack"};
        bool found[6] = {};
        for (unsigned i = 1; i < availableAnims_.Size(); ++i) // skip bind pose
        {
            String lower = GetFileName(availableAnims_[i]).ToLower();
            for (int e = 0; e < 6; ++e)
                if (lower.Contains(essentials[e])) found[e] = true;
        }

        String missing;
        for (int e = 0; e < 6; ++e)
            if (!found[e]) { if (!missing.Empty()) missing += ", "; missing += essentials[e]; }
        if (!missing.Empty())
            AddInfoLine("MISSING: " + missing, Color(1.0f, 0.4f, 0.4f));
        else
            AddInfoLine("All essentials present", Color(0.4f, 1.0f, 0.4f));

        AddInfoSeparator();

        // List each animation with details
        for (unsigned i = 1; i < availableAnims_.Size(); ++i)
        {
            auto* anim = cache->GetResource<Animation>(availableAnims_[i]);
            if (!anim) continue;

            String name = GetFileName(availableAnims_[i]);
            float duration = anim->GetLength();
            unsigned numTracks = anim->GetNumTracks();

            unsigned totalKeys = 0;
            const HashMap<StringHash, AnimationTrack>& tracks = anim->GetTracks();
            for (HashMap<StringHash, AnimationTrack>::ConstIterator it = tracks.Begin(); it != tracks.End(); ++it)
                totalKeys += it->second_.GetNumKeyFrames();

            String line = name + "  " + String((double)duration, 2) + "s  " +
                String(numTracks) + "tr  " + String(totalKeys) + "kf";
            AddInfoLine(line, Color(0.85f, 0.85f, 0.65f));
        }
    }

    // ---- Materials (collapsed) ----
    if (staticModelComp_)
    {
        CreateCollapsibleSection("Materials", false);
        for (unsigned g = 0; g < staticModelComp_->GetNumGeometries(); ++g)
        {
            Material* mat = staticModelComp_->GetMaterial(g);
            if (!mat) { AddInfoLine("Geom " + String(g) + ": <none>"); continue; }

            // Material name + [Edit] button on same row
            {
                UIElement* target = currentSection_ ? currentSection_ : infoContent_;
                if (target)
                {
                    auto* row = target->CreateChild<UIElement>();
                    row->SetLayout(LM_HORIZONTAL, 4, IntRect(0, 0, 0, 0));
                    row->SetMinHeight(20);

                    auto* label = row->CreateChild<Text>();
                    label->SetFont(font_, 11);
                    label->SetText("Geom " + String(g) + ": " + mat->GetName());
                    label->SetColor(Color(0.85f, 0.85f, 0.5f));

                    auto* editBtn = row->CreateChild<Button>();
                    editBtn->SetStyleAuto();
                    editBtn->SetFixedSize(40, 18);
                    editBtn->SetColor(Color(0.25f, 0.35f, 0.25f));
                    editBtn->SetVar("GeomIndex", (int)g);

                    auto* editLbl = editBtn->CreateChild<Text>();
                    editLbl->SetFont(font_, 10);
                    editLbl->SetText("[Edit]");
                    editLbl->SetAlignment(HA_CENTER, VA_CENTER);
                    editLbl->SetColor(Color(0.9f, 0.9f, 0.5f));

                    SubscribeToEvent(editBtn, E_RELEASED, [this](StringHash, VariantMap& ed)
                    {
                        auto* btn = static_cast<Button*>(ed[Released::P_ELEMENT].GetPtr());
                        unsigned geom = (unsigned)btn->GetVar("GeomIndex").GetI32();
                        OpenMaterialEditor(geom);
                    });
                }
            }

            for (unsigned tc = 0; tc < (unsigned)mat->GetNumTechniques(); ++tc)
            {
                Technique* tech = mat->GetTechnique(tc);
                if (tech) AddInfoLine("  Tech: " + tech->GetName());
            }

            const String unitNames[] = {"diffuse","normal","specular","emissive","environment","volume","custom1","custom2"};
            for (int u = 0; u < MAX_TEXTURE_UNITS; ++u)
            {
                Texture* tex = mat->GetTexture((TextureUnit)u);
                if (tex)
                {
                    String uname = (u < 8) ? unitNames[u] : String(u);
                    AddInfoLine("  " + uname + ": " + tex->GetName());
                }
            }

            const HashMap<StringHash, MaterialShaderParameter>& params = mat->GetShaderParameters();
            for (HashMap<StringHash, MaterialShaderParameter>::ConstIterator it = params.Begin(); it != params.End(); ++it)
                AddInfoLine("  " + it->second_.name_ + " = " + it->second_.value_.ToString());

            if (!mat->GetVertexShaderDefines().Empty())
                AddInfoLine("  VS: " + mat->GetVertexShaderDefines());
            if (!mat->GetPixelShaderDefines().Empty())
                AddInfoLine("  PS: " + mat->GetPixelShaderDefines());
        }
    }

    // ---- Morph Targets (collapsed) ----
    if (animatedModelComp_ && animatedModelComp_->GetNumMorphs() > 0)
    {
        CreateCollapsibleSection("Morph Targets", false);
        const Vector<ModelMorph>& morphs = currentModel_->GetMorphs();
        for (unsigned i = 0; i < morphs.Size(); ++i)
            AddInfoLine(morphs[i].name_ + ": " + String((double)animatedModelComp_->GetMorphWeight(i), 2));
    }

    currentSection_ = nullptr;
}

void ModelTool::RebuildAnimList()
{
    if (!animListView_) return;

    animListView_->RemoveAllItems();

    auto* cache = GetSubsystem<ResourceCache>();
    for (unsigned i = 0; i < availableAnims_.Size(); ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);

        String label = GetFileName(availableAnims_[i]);
        auto* anim = cache->GetResource<Animation>(availableAnims_[i]);
        if (anim && anim->GetLength() > 0.0f)
            label += "  (" + String((double)anim->GetLength(), 2) + "s)";

        item->SetText(label);
        item->SetStyleAuto();
        animListView_->AddItem(item);
    }

    if (currentAnimIndex_ >= 0)
        animListView_->SetSelection(currentAnimIndex_);
}

// ============================================================================
// Camera
// ============================================================================

void ModelTool::UpdateCamera(float)
{
    float yawRad = cameraYaw_ * M_DEGTORAD;
    float pitchRad = cameraPitch_ * M_DEGTORAD;

    float x = modelCenter_.x_ + cameraDistance_ * cosf(pitchRad) * sinf(yawRad);
    float y = modelCenter_.y_ + cameraDistance_ * sinf(pitchRad);
    float z = modelCenter_.z_ + cameraDistance_ * cosf(pitchRad) * cosf(yawRad);

    cameraNode_->SetPosition(Vector3(x, y, z));
    cameraNode_->LookAt(modelCenter_);
}

// ============================================================================
// Events
// ============================================================================

void ModelTool::HandleUpdate(StringHash, VariantMap& eventData)
{
    float timeStep = eventData[Update::P_TIMESTEP].GetFloat();
    UpdateCamera(timeStep);
    PollIPC();

    auto* input = GetSubsystem<Input>();

    // Keyboard shortcuts via Input (not E_KEYDOWN) so they work even when UI has focus
    if (input->GetKeyPress(KEY_ESCAPE))
        engine_->Exit();

    // V key — toggle vertex edit mode
    if (input->GetKeyPress(KEY_V) && currentModel_)
    {
        if (!vertexEditMode_)
            EnterVertexEditMode();
        else
            ExitVertexEditMode();
    }

    // Vertex edit mode input
    if (vertexEditMode_)
    {
        auto* ui = GetSubsystem<UI>();

        // LMB click — pick or start drag
        if (input->GetMouseButtonPress(MOUSEB_LEFT))
        {
            if (!ui->GetElementAt(input->GetMousePosition()))
            {
                PickVertex(input->GetMousePosition().x_, input->GetMousePosition().y_);
                if (selectedVertex_ >= 0)
                {
                    vertexDragging_ = true;
                    dragStartPos_ = GetVertexWorldPosition(selectedGeometry_, selectedVertex_);
                    dragPlaneNormal_ = (cameraNode_->GetPosition() - dragStartPos_).Normalized();
                }
            }
        }

        // Mouse drag — move vertex
        if (vertexDragging_ && input->GetMouseButtonDown(MOUSEB_LEFT))
            MoveSelectedVertex(input->GetMousePosition().x_, input->GetMousePosition().y_);

        // LMB release — end drag
        if (vertexDragging_ && !input->GetMouseButtonDown(MOUSEB_LEFT))
        {
            vertexDragging_ = false;
            if (selectedVertex_ >= 0) vertexDirty_ = true;
        }

        // DEL — delete selected vertex
        if (input->GetKeyPress(KEY_DELETE) && selectedVertex_ >= 0)
            DeleteSelectedVertex();

        // Ctrl+S — save
        if (input->GetKeyDown(KEY_CTRL) && input->GetKeyPress(KEY_S) && editModel_)
            SaveModel();
    }

    // I — toggle IK posing mode (needs an animated model with detected chains)
    if (input->GetKeyPress(KEY_I) && currentModel_)
        ToggleIKPoseMode();

    // IK posing input — left-drag a target sphere; the solver pulls the limb to it.
    if (ikPoseMode_)
    {
        auto* ui = GetSubsystem<UI>();

        if (input->GetMouseButtonPress(MOUSEB_LEFT) && !ui->GetElementAt(input->GetMousePosition()))
        {
            int chain = PickIKTarget(input->GetMousePosition().x_, input->GetMousePosition().y_);
            if (chain >= 0 && chain < (int)ikTargets_.Size() && ikTargets_[chain])
            {
                ikSelectedChain_ = chain;
                ikDragging_ = true;
                Vector3 tpos = ikTargets_[chain]->GetWorldPosition();
                ikDragPlaneNormal_ = (cameraNode_->GetWorldPosition() - tpos).Normalized();
            }
        }

        if (ikDragging_ && input->GetMouseButtonDown(MOUSEB_LEFT) &&
            ikSelectedChain_ >= 0 && ikSelectedChain_ < (int)ikTargets_.Size() && ikTargets_[ikSelectedChain_])
        {
            auto* camera = cameraNode_->GetComponent<Camera>();
            auto* graphics = GetSubsystem<Graphics>();
            if (camera && graphics)
            {
                float nx = (float)input->GetMousePosition().x_ / (float)graphics->GetWidth();
                float ny = (float)input->GetMousePosition().y_ / (float)graphics->GetHeight();
                Ray ray = camera->GetScreenRay(nx, ny);
                Plane plane(ikDragPlaneNormal_, ikTargets_[ikSelectedChain_]->GetWorldPosition());
                float hit = ray.HitDistance(plane);
                if (hit < M_INFINITY)
                    ikTargets_[ikSelectedChain_]->SetWorldPosition(ray.origin_ + ray.direction_ * hit);
            }
        }

        if (ikDragging_ && !input->GetMouseButtonDown(MOUSEB_LEFT))
            ikDragging_ = false;

        SolveIK();  // re-solve each frame so the pose holds and tracks the target
    }

    // LMB click — model selection, bone picking, or gizmo drag start
    if (input->GetMouseButtonPress(MOUSEB_LEFT) && !vertexEditMode_ && !ikPoseMode_)
    {
        auto* ui = GetSubsystem<UI>();
        if (!ui->GetElementAt(input->GetMousePosition()))
        {
            // Raycast against scene to detect which model was clicked
            if (refModel_)
            {
                auto* camera = cameraNode_->GetComponent<Camera>();
                auto* graphics = GetSubsystem<Graphics>();
                auto* octree = scene_->GetComponent<Octree>();
                float nx = (float)input->GetMousePosition().x_ / (float)graphics->GetWidth();
                float ny = (float)input->GetMousePosition().y_ / (float)graphics->GetHeight();
                Ray ray = camera->GetScreenRay(nx, ny);

                Vector<RayQueryResult> results;
                RayOctreeQuery query(results, ray, RAY_TRIANGLE, 100.0f);
                octree->Raycast(query);

                for (unsigned r = 0; r < results.Size(); ++r)
                {
                    Node* hitNode = results[r].node_;
                    // Walk up to find if this belongs to refModelNode_ or modelNode_
                    Node* walk = hitNode;
                    while (walk && walk != scene_)
                    {
                        if (walk == refModelNode_ && !editingRef_)
                        {
                            SwapActiveModel();
                            break;
                        }
                        else if (walk == modelNode_ && editingRef_)
                        {
                            SwapActiveModel();
                            break;
                        }
                        walk = walk->GetParent();
                    }
                    break; // only check first hit
                }
            }

            if (!isAnimated_) goto skipBonePick;
            // Check if click is near a gizmo ring — if so, start drag instead of picking
            if (selectedBone_ >= 0 && animatedModelComp_)
            {
                const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
                if (selectedBone_ < (int)bones.Size() && bones[selectedBone_].node_)
                {
                    auto* camera = cameraNode_->GetComponent<Camera>();
                    auto* graphics = GetSubsystem<Graphics>();
                    Node* boneNode = bones[selectedBone_].node_;
                    Vector3 bonePos = boneNode->GetWorldPosition();
                    float gizmoLen = BoneGizmoRadius(bones, selectedBone_, cameraNode_);
                    float w = (float)graphics->GetWidth();
                    float h = (float)graphics->GetHeight();
                    Vector2 screenBone = camera->WorldToScreenPoint(bonePos);
                    float bx = screenBone.x_ * w;
                    float by = screenBone.y_ * h;
                    float mx = (float)input->GetMousePosition().x_;
                    float my = (float)input->GetMousePosition().y_;

                    // Project ring edge points to screen and check proximity to mouse
                    Vector3 axisX, axisY, axisZ;
                    if (boneGizmoLocal_)
                    {
                        Quaternion rot = boneNode->GetWorldRotation();
                        axisX = rot * Vector3::RIGHT; axisY = rot * Vector3::UP; axisZ = rot * Vector3::FORWARD;
                    }
                    else { axisX = Vector3::RIGHT; axisY = Vector3::UP; axisZ = Vector3::FORWARD; }

                    // Check which ring the click is nearest
                    float bestDist = 15.0f; // pixel threshold
                    int bestAxis = -1;
                    Vector3 perpA[] = {axisY, axisX, axisX};
                    Vector3 perpB[] = {axisZ, axisZ, axisY};
                    for (int a = 0; a < 3; ++a)
                    {
                        for (int seg = 0; seg < 16; ++seg)
                        {
                            float angle = (float)seg / 16.0f * 360.0f * M_DEGTORAD;
                            Vector3 ringPt = bonePos + (perpA[a] * cosf(angle) + perpB[a] * sinf(angle)) * gizmoLen;
                            Vector2 sp = camera->WorldToScreenPoint(ringPt);
                            float d = sqrtf((sp.x_ * w - mx) * (sp.x_ * w - mx) + (sp.y_ * h - my) * (sp.y_ * h - my));
                            if (d < bestDist) { bestDist = d; bestAxis = a; }
                        }
                    }

                    if (bestAxis >= 0)
                    {
                        BeginBoneGizmoDrag(bestAxis);
                        goto skipBonePick;
                    }

                    // No ring hit — check inner arcball circle for free rotation
                    float clickDist = sqrtf((bx - mx) * (bx - mx) + (by - my) * (by - my));
                    Vector2 screenEdge = camera->WorldToScreenPoint(bonePos + Vector3::RIGHT * gizmoLen);
                    float sphereScreenRadius = sqrtf((screenEdge.x_ * w - bx) * (screenEdge.x_ * w - bx) +
                                                     (screenEdge.y_ * h - by) * (screenEdge.y_ * h - by));
                    if (clickDist < sphereScreenRadius * 0.9f)
                    {
                        BeginBoneGizmoDrag(-1);
                        goto skipBonePick;
                    }
                }
            }
            PickBone(input->GetMousePosition().x_, input->GetMousePosition().y_);
            skipBonePick:;
        }
    }

    // Gizmo drag update
    if (boneGizmoDragging_ && input->GetMouseButtonDown(MOUSEB_LEFT))
        UpdateBoneGizmoDrag();

    // Gizmo drag end
    if (boneGizmoDragging_ && !input->GetMouseButtonDown(MOUSEB_LEFT))
        EndBoneGizmoDrag();

    // Folder browse: Left/Right to navigate, G = keep, X = reject
    // Import mode: K = promote to bin/Data, G = delete from DMZ
    // Must be checked BEFORE gizmo G key to avoid key press being consumed
    if (!folderModels_.Empty())
    {
        if (input->GetKeyPress(KEY_RIGHT))
            BrowseNext();
        if (input->GetKeyPress(KEY_LEFT))
            BrowsePrev();
        if (input->GetKeyPress(KEY_K))
            PromoteCurrentModel();
        if (input->GetKeyPress(KEY_G))
            DeleteCurrentModel();
    }

    // G key — toggle gizmo local/world (only when not in folder browse mode)
    if (input->GetKeyPress(KEY_G) && selectedBone_ >= 0 && folderModels_.Empty())
        boneGizmoLocal_ = !boneGizmoLocal_;

    // Ctrl+Z — undo bone rotation
    if (input->GetKeyDown(KEY_CTRL) && input->GetKeyPress(KEY_Z))
        UndoBoneRot();

    if (input->GetKeyPress(KEY_F))
    {
        showWireframe_ = !showWireframe_;
        auto* camera = cameraNode_->GetComponent<Camera>();
        if (camera) camera->SetFillMode(showWireframe_ ? FILL_WIREFRAME : FILL_SOLID);
    }

    if (input->GetKeyPress(KEY_S))
        showSkeleton_ = !showSkeleton_;

    if (input->GetKeyPress(KEY_B))
        showBoundingBox_ = !showBoundingBox_;

    if (input->GetKeyPress(KEY_R))
        AutoFrameCamera();

    if (input->GetKeyPress(KEY_TAB))
    {
        if (infoPanel_) infoPanel_->SetVisible(!infoPanel_->IsVisible());
    }

    // Backtick — swap active model between slot A and slot B
    if (input->GetKeyPress(KEY_BACKQUOTE) && refModel_)
        SwapActiveModel();

    if (input->GetKeyPress(KEY_SPACE))
    {
        if (animController_ && currentAnimIndex_ >= 0)
        {
            animPlaying_ = !animPlaying_;
            float spd = animReversed_ ? -animSpeed_ : animSpeed_;
            animController_->SetSpeed(availableAnims_[currentAnimIndex_], animPlaying_ ? spd : 0.0f);
        }
    }

    if (input->GetKeyPress(KEY_RIGHTBRACKET))
    {
        if (!availableAnims_.Empty())
        {
            int idx = (currentAnimIndex_ + 1) % (int)availableAnims_.Size();
            PlayAnimation(idx);
        }
    }

    if (input->GetKeyPress(KEY_LEFTBRACKET))
    {
        if (!availableAnims_.Empty())
        {
            int idx = currentAnimIndex_ - 1;
            if (idx < 0) idx = (int)availableAnims_.Size() - 1;
            PlayAnimation(idx);
        }
    }

    // T key — add text key at current time (only when not editing a LineEdit)
    if (input->GetKeyPress(KEY_T) && !vertexEditMode_)
    {
        auto* ui = GetSubsystem<UI>();
        auto* focused = ui->GetFocusElement();
        bool editingText = focused && (focused->GetType() == LineEdit::GetTypeStatic());
        if (!editingText)
            AddTextKeyAtCurrentTime();
    }

    // Delete key — remove selected text key (only when not in vertex edit and not editing text)
    if (input->GetKeyPress(KEY_DELETE) && !vertexEditMode_ && selectedTextKey_ >= 0)
    {
        auto* ui = GetSubsystem<UI>();
        auto* focused = ui->GetFocusElement();
        bool editingText = focused && (focused->GetType() == LineEdit::GetTypeStatic());
        if (!editingText)
            DeleteSelectedTextKey();
    }

    // Ctrl+S — save text keys (when not in vertex edit mode)
    if (input->GetKeyDown(KEY_CTRL) && input->GetKeyPress(KEY_S) && !vertexEditMode_ && currentAnimIndex_ >= 0)
        SaveTextKeys();

    // Text key flash fade
    if (textKeyFlashTimer_ > 0.0f)
    {
        textKeyFlashTimer_ -= timeStep;
        if (textKeyFlash_)
        {
            float alpha = Clamp(textKeyFlashTimer_ / 0.8f, 0.0f, 1.0f);
            textKeyFlash_->SetColor(Color(1.0f, 1.0f, 0.3f, alpha));
            if (textKeyFlashTimer_ <= 0.0f)
                textKeyFlash_->SetText("");
        }
    }

    // Update audio capture panel
    UpdateAudioPanel(timeStep);

    // Update playback display
    if (animController_ && currentAnimIndex_ >= 0 && currentAnimIndex_ < (int)availableAnims_.Size())
    {
        float t = animController_->GetTime(availableAnims_[currentAnimIndex_]);
        float len = animController_->GetLength(availableAnims_[currentAnimIndex_]);
        String state = animPlaying_ ? " PLAY" : " PAUSED";

        if (animTimeText_)
        {
            // Read actual keyframe index from the first track of the animation
            int currentFrame = 0;
            int totalFrames = 0;
            auto* cache = GetSubsystem<ResourceCache>();
            auto* anim = cache->GetResource<Animation>(availableAnims_[currentAnimIndex_], false);
            if (anim && anim->GetNumTracks() > 0)
            {
                AnimationTrack* track = anim->GetTrack((unsigned)0);
                if (track && track->GetNumKeyFrames() > 0)
                {
                    totalFrames = (int)track->GetNumKeyFrames();
                    i32 idx = 0;
                    if (track->GetKeyFrameIndex(t, idx))
                        currentFrame = (int)idx;
                }
            }

            animTimeText_->SetText(GetFileName(availableAnims_[currentAnimIndex_]) +
                "  " + String((double)t, 2) + " / " + String((double)len, 2) + "s" +
                "  [" + String(currentFrame) + " / " + String(totalFrames > 0 ? totalFrames - 1 : 0) + "]" +
                state);
        }

        if (animSlider_ && len > 0.0f)
            animSlider_->SetValue(t / len);

        // Update status bar with animation context
        if (statusBarText_)
            statusBarText_->SetText(GetFileName(availableAnims_[currentAnimIndex_]) +
                "  " + String((double)t, 2) + "s / " + String((double)len, 2) + "s" +
                (animPlaying_ ? "  PLAYING" : "  PAUSED"));
    }

    // Status bar: bone context when posing
    if (statusBarText_ && selectedBone_ >= 0 && isAnimated_ && animatedModelComp_)
    {
        const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
        if (selectedBone_ < (int)bones.Size())
        {
            Vector3 euler = bones[selectedBone_].node_ ?
                bones[selectedBone_].node_->GetRotation().EulerAngles() : Vector3::ZERO;
            statusBarText_->SetText("Bone: " + bones[selectedBone_].name_ +
                "  R(" + String((double)euler.x_, 1) + ", " + String((double)euler.y_, 1) +
                ", " + String((double)euler.z_, 1) + ")" +
                (boneGizmoLocal_ ? "  LOCAL" : "  WORLD"));
        }
    }
}

void ModelTool::HandlePostRenderUpdate(StringHash, VariantMap&)
{
    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!debug) return;

    if (showBoundingBox_ && currentModel_)
    {
        BoundingBox bbox = currentModel_->GetBoundingBox();
        debug->AddBoundingBox(bbox, modelNode_->GetWorldTransform(), Color::YELLOW, false);
    }

    if ((showSkeleton_ || selectedBone_ >= 0) && isAnimated_ && animatedModelComp_)
    {
        const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();

        // Build selected subtree mask — mark selected, propagate to children
        // Bones are stored parent-before-child, so one forward pass is enough
        Vector<bool> inSubtree(bones.Size(), false);
        if (selectedBone_ >= 0 && selectedBone_ < (int)bones.Size())
        {
            inSubtree[selectedBone_] = true;
            for (unsigned i = (unsigned)selectedBone_ + 1; i < bones.Size(); ++i)
            {
                int p = bones[i].parentIndex_;
                if (p >= 0 && p < (int)bones.Size() && inSubtree[p])
                    inSubtree[i] = true;
            }
        }

        for (unsigned i = 0; i < bones.Size(); ++i)
        {
            Node* boneNode = bones[i].node_;
            if (!boneNode) continue;

            Vector3 pos = boneNode->GetWorldPosition();
            if (bones[i].parentIndex_ >= 0 && bones[i].parentIndex_ < (int)bones.Size())
            {
                Node* parentNode = bones[bones[i].parentIndex_].node_;
                if (parentNode)
                {
                    Color lineColor = Color::CYAN;
                    if (inSubtree[i])
                        lineColor = Color::YELLOW;
                    else if (!poseBoneFilter_.Empty() && poseBoneFilter_.Contains((int)i))
                        lineColor = Color::GREEN;
                    if (showSkeleton_ || inSubtree[i])
                        debug->AddLine(parentNode->GetWorldPosition(), pos, lineColor, false);
                }
            }
        }
    }

    // Draw bone rotation gizmo
    DrawBoneGizmo();

    // Draw vertex overlay
    if (vertexEditMode_ && editModel_)
        DrawVertexOverlay(debug);

    // IK target spheres — selected/dragged one highlighted yellow, others cyan.
    if (ikPoseMode_)
    {
        for (unsigned i = 0; i < ikTargets_.Size(); ++i)
        {
            if (!ikTargets_[i])
                continue;
            Color col = ((int)i == ikSelectedChain_) ? Color::YELLOW : Color::CYAN;
            debug->AddSphere(Sphere(ikTargets_[i]->GetWorldPosition(), 0.03f * cameraDistance_), col, false);
        }
    }
}

void ModelTool::HandleMouseMove(StringHash, VariantMap& eventData)
{
    auto* input = GetSubsystem<Input>();

    if (input->GetMouseButtonDown(MOUSEB_RIGHT))
    {
        int dx = eventData[MouseMove::P_DX].GetI32();
        int dy = eventData[MouseMove::P_DY].GetI32();
        cameraYaw_ += dx * 0.3f;
        cameraPitch_ = Clamp(cameraPitch_ + dy * 0.3f, -89.0f, 89.0f);
    }

    if (input->GetMouseButtonDown(MOUSEB_MIDDLE) && lightNode_)
    {
        int dx = eventData[MouseMove::P_DX].GetI32();
        int dy = eventData[MouseMove::P_DY].GetI32();
        Quaternion rot = lightNode_->GetRotation();
        rot = Quaternion(dy * 0.5f, lightNode_->GetRight()) * rot;
        rot = Quaternion(dx * 0.5f, Vector3::UP) * rot;
        lightNode_->SetRotation(rot);
    }
}

void ModelTool::HandleMouseWheel(StringHash, VariantMap& eventData)
{
    // Don't zoom camera when mouse is over a UI element that handles scroll
    auto* ui = GetSubsystem<UI>();
    UIElement* hover = ui->GetElementAt(ui->GetCursorPosition());
    if (hover && hover != ui->GetRoot())
        return;

    int wheel = eventData[MouseWheel::P_WHEEL].GetI32();
    cameraDistance_ *= (wheel > 0) ? 0.9f : 1.1f;
    cameraDistance_ = Max(cameraDistance_, 0.1f);
}

void ModelTool::HandleFileOpen(StringHash, VariantMap& eventData)
{
    String path = eventData[FileSelected::P_FILENAME].GetString();
    bool ok = eventData[FileSelected::P_OK].GetBool();
    fileSelector_.Reset();
    if (!ok || path.Empty()) return;

    // Resolve symlinks so path matches registered resource dirs
    char resolvedBuf[PATH_MAX];
    String resolvedPath = path;
    if (realpath(path.CString(), resolvedBuf))
        resolvedPath = String(resolvedBuf);

    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        String resolvedDir = dirs[i];
        if (realpath(dirs[i].CString(), resolvedBuf))
            resolvedDir = String(resolvedBuf) + "/";

        if (resolvedPath.StartsWith(resolvedDir))
        {
            path = resolvedPath.Substring(resolvedDir.Length());
            break;
        }
        if (path.StartsWith(dirs[i]))
        {
            path = path.Substring(dirs[i].Length());
            break;
        }
    }

    if (!path.EndsWith(".mdl", false))
    {
        URHO3D_LOGERRORF("FileOpen: not a model file: %s", path.CString());
        if (statusText_)
            statusText_->SetText("Error: only .mdl files supported");
        return;
    }

    URHO3D_LOGINFOF("FileOpen: resolved path = %s", path.CString());
    LoadModel(path);
}

void ModelTool::HandleAnimSlider(StringHash, VariantMap& eventData)
{
    if (!animController_ || currentAnimIndex_ < 0) return;
    float value = eventData[SliderChanged::P_VALUE].GetFloat();
    float length = animController_->GetLength(availableAnims_[currentAnimIndex_]);
    animController_->SetTime(availableAnims_[currentAnimIndex_], value * length);
}

void ModelTool::HandleSpeedSlider(StringHash, VariantMap& eventData)
{
    float value = eventData[SliderChanged::P_VALUE].GetFloat();
    animSpeed_ = 0.1f + value * 2.9f;
    if (animSpeedText_) animSpeedText_->SetText("Speed: " + String((double)animSpeed_, 1) + "x");
    if (animController_ && currentAnimIndex_ >= 0 && animPlaying_)
    {
        float spd = animReversed_ ? -animSpeed_ : animSpeed_;
        animController_->SetSpeed(availableAnims_[currentAnimIndex_], spd);
    }
}

void ModelTool::HandleAnimSelected(StringHash, VariantMap& eventData)
{
    if (suppressAnimSelect_) return;

    using namespace ItemSelected;
    int sel = eventData[P_SELECTION].GetI32();
    if (sel >= 0 && sel < (int)availableAnims_.Size())
        PlayAnimation(sel);
}

// ============================================================================
// Bone picking
// ============================================================================

void ModelTool::PickBone(int screenX, int screenY)
{
    if (!animatedModelComp_ || !cameraNode_) return;

    auto* camera = cameraNode_->GetComponent<Camera>();
    if (!camera) return;

    auto* graphics = GetSubsystem<Graphics>();
    float nx = (float)screenX / (float)graphics->GetWidth();
    float ny = (float)screenY / (float)graphics->GetHeight();
    Ray ray = camera->GetScreenRay(nx, ny);

    Skeleton& skeleton = animatedModelComp_->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();

    float bestDist = M_INFINITY;
    int bestBone = -1;

    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        Node* boneNode = bones[i].node_;
        if (!boneNode) continue;

        Vector3 boneWorldPos = boneNode->GetWorldPosition();

        // Distance from ray to bone position
        Vector3 toPoint = boneWorldPos - ray.origin_;
        float along = toPoint.DotProduct(ray.direction_);
        if (along < 0.0f) continue;  // behind camera

        Vector3 closest = ray.origin_ + ray.direction_ * along;
        float dist = (closest - boneWorldPos).Length();

        // Scale threshold by distance from camera so far bones are still pickable
        float threshold = along * 0.03f;
        if (threshold < 0.1f) threshold = 0.1f;

        if (dist < threshold && dist < bestDist)
        {
            bestDist = dist;
            bestBone = (int)i;
        }
    }

    SelectBone(bestBone);
}

void ModelTool::DrawBoneSubtree(DebugRenderer* debug, const Skeleton& skel, unsigned boneIndex, const Color& color)
{
    const Vector<Bone>& bones = skel.GetBones();
    if (boneIndex >= bones.Size()) return;

    Node* boneNode = bones[boneIndex].node_;
    if (!boneNode) return;

    // Draw sphere at this bone
    Vector3 pos = boneNode->GetWorldPosition();
    debug->AddSphere(Sphere(pos, 0.02f * cameraDistance_), color, false);

    // Draw lines to children and recurse
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (bones[i].parentIndex_ == (int)boneIndex && i != boneIndex)
        {
            Node* childNode = bones[i].node_;
            if (childNode)
            {
                debug->AddLine(pos, childNode->GetWorldPosition(), color, false);
                DrawBoneSubtree(debug, skel, i, Color(color.r_ * 0.8f, color.g_ * 0.8f, color.b_, color.a_));
            }
        }
    }
}

// ── Bone Rotation Edit History ────────────────────────────────────────────────

void ModelTool::PushBoneRotEdit(int boneIndex, const Quaternion& before, const Quaternion& after)
{
    // Collapse consecutive edits on the same bone (slider drags produce many)
    if (!boneRotHistory_.Empty())
    {
        BoneRotEdit& last = boneRotHistory_.Back();
        if (last.boneIndex == boneIndex)
        {
            last.after = after;
            return;
        }
    }
    BoneRotEdit edit;
    edit.boneIndex = boneIndex;
    edit.before = before;
    edit.after = after;
    boneRotHistory_.Push(edit);
}

void ModelTool::UndoBoneRot()
{
    if (boneRotHistory_.Empty() || !isAnimated_ || !animatedModelComp_)
        return;

    const BoneRotEdit& edit = boneRotHistory_.Back();
    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (edit.boneIndex >= 0 && edit.boneIndex < (int)bones.Size())
    {
        Node* boneNode = bones[edit.boneIndex].node_;
        if (boneNode)
            boneNode->SetRotation(edit.before);
    }
    boneRotHistory_.Pop();

    // Update slider display if this was the selected bone
    if (edit.boneIndex == selectedBone_)
        UpdateBoneEditControls();
}

HashSet<int> ModelTool::GetDirtyBones() const
{
    HashSet<int> dirty;
    for (unsigned i = 0; i < boneRotHistory_.Size(); ++i)
        dirty.Insert(boneRotHistory_[i].boneIndex);
    return dirty;
}

// Compute gizmo radius for a bone: distance to nearest child, or parent if leaf.
static float BoneGizmoRadius(const Vector<Bone>& bones, int boneIndex, Node* cameraNode)
{
    if (boneIndex < 0 || boneIndex >= (int)bones.Size() || !bones[boneIndex].node_)
        return 0.5f;

    Vector3 pos = bones[boneIndex].node_->GetWorldPosition();
    float len = 0.0f;
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (bones[i].parentIndex_ == boneIndex && (int)i != boneIndex && bones[i].node_)
        {
            float d = (bones[i].node_->GetWorldPosition() - pos).Length();
            if (len == 0.0f || d < len)
                len = d;
        }
    }
    if (len == 0.0f && bones[boneIndex].parentIndex_ >= 0)
    {
        Node* parentNode = bones[bones[boneIndex].parentIndex_].node_;
        if (parentNode)
            len = (parentNode->GetWorldPosition() - pos).Length();
    }
    if (len < 0.001f && cameraNode)
        len = (pos - cameraNode->GetWorldPosition()).Length() * 0.15f;
    return len;
}

// ── Pose Studio Prop Attachment ───────────────────────────────────────────────

Vector<ModelTool::IKChain> ModelTool::DetectIKChains() const
{
    Vector<IKChain> chains;
    if (!animatedModelComp_)
        return chains;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    const unsigned numBones = bones.Size();
    if (numBones == 0)
        return chains;

    // Count children per bone. A root bone's parentIndex_ equals its own index
    // (Skeleton.cpp), so the self-reference is skipped -- roots aren't their own child.
    Vector<int> childCount(numBones);
    for (unsigned i = 0; i < numBones; ++i)
        childCount[i] = 0;
    for (unsigned i = 0; i < numBones; ++i)
    {
        const int parent = bones[i].parentIndex_;
        if (parent >= 0 && parent < (int)numBones && parent != (int)i)
            ++childCount[parent];
    }

    // Each leaf (no children) is a terminal effector. Walk parent-ward to the nearest
    // branch (>1 child) or to the root (self-parent), accumulating segment length.
    for (unsigned i = 0; i < numBones; ++i)
    {
        if (childCount[i] != 0)
            continue;  // not a terminal

        IKChain chain;
        chain.effectorBoneIndex_ = (int)i;

        int walk = (int)i;
        unsigned length = 0;
        for (;;)
        {
            const int parent = bones[walk].parentIndex_;
            if (parent < 0 || parent >= (int)numBones || parent == walk)
                break;  // reached the root bone (self-parent sentinel)
            ++length;
            walk = parent;
            if (childCount[walk] > 1)
                break;  // reached a branch -- limb meets spine
        }
        chain.rootBoneIndex_ = walk;
        chain.length_ = length;
        chains.Push(chain);
    }

    return chains;
}

// ── IK posing (Phase 1) ──────────────────────────────────────────────────────
// Wire the auto-detected chains (ikChains_) to a live IKSolver so the user can drag
// a per-chain target and the limb follows. The rig is built on ENTERING IK mode (so
// the solver captures whatever pose is on screen as its base) and torn down on exit
// or model reload. Targets are scene-root children, independent of the bone hierarchy
// the solver moves. Bone nodes update in place, so the existing pose-capture/keyframe
// path keeps working with zero glue.

void ModelTool::SetupIKRig()
{
    TeardownIKRig();
    if (!animatedModelComp_ || !modelNode_ || ikChains_.Empty())
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();

    ikSolver_ = modelNode_->CreateComponent<IKSolver>();
    ikSolver_->SetAlgorithm(IKSolver::FABRIK);
    ikSolver_->SetFeature(IKSolver::JOINT_ROTATIONS, true);  // skinned: rotate joints, not just move nodes
    ikSolver_->SetMaximumIterations(20);
    ikSolver_->SetTolerance(0.001f);

    ikTargets_.Clear();
    ikTargets_.Resize(ikChains_.Size());
    for (unsigned i = 0; i < ikChains_.Size(); ++i)
    {
        const IKChain& c = ikChains_[i];
        if (c.effectorBoneIndex_ < 0 || c.effectorBoneIndex_ >= (int)bones.Size())
            continue;
        Node* boneNode = bones[c.effectorBoneIndex_].node_;
        if (!boneNode)
            continue;

        IKEffector* eff = boneNode->CreateComponent<IKEffector>();
        eff->SetChainLength(c.length_);

        // Target starts exactly at the effector's current world pose, so the first
        // Solve() is a no-op and the model holds its pose until the user drags it.
        Node* target = scene_->CreateChild("_IKTarget_" + String(i));
        target->SetTemporary(true);
        target->SetWorldPosition(boneNode->GetWorldPosition());
        target->SetWorldRotation(boneNode->GetWorldRotation());
        eff->SetTargetNode(target);
        ikTargets_[i] = target;
    }

    ikSolver_->RebuildChainTrees();
    URHO3D_LOGINFOF("IK: rig built — %u effector(s) live", ikChains_.Size());
}

void ModelTool::TeardownIKRig()
{
    for (unsigned i = 0; i < ikTargets_.Size(); ++i)
        if (ikTargets_[i])
            ikTargets_[i]->Remove();
    ikTargets_.Clear();

    if (animatedModelComp_)
    {
        const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
        for (unsigned i = 0; i < bones.Size(); ++i)
            if (bones[i].node_)
                if (auto* eff = bones[i].node_->GetComponent<IKEffector>())
                    eff->Remove();
    }
    if (ikSolver_)
    {
        ikSolver_->Remove();
        ikSolver_ = nullptr;
    }
    ikSelectedChain_ = -1;
    ikDragging_ = false;
}

void ModelTool::SolveIK()
{
    if (ikSolver_)
        ikSolver_->Solve();
}

void ModelTool::ToggleIKPoseMode()
{
    if (ikPoseMode_)
    {
        TeardownIKRig();
        ikPoseMode_ = false;
        URHO3D_LOGINFO("IK pose mode OFF");
        return;
    }

    if (!isAnimated_ || !animatedModelComp_ || ikChains_.Empty())
    {
        URHO3D_LOGWARNING("IK pose mode needs an animated model with detected chains");
        return;
    }
    SetupIKRig();
    ikPoseMode_ = true;
    URHO3D_LOGINFO("IK pose mode ON — left-drag a target sphere to pose");
}

int ModelTool::PickIKTarget(int screenX, int screenY) const
{
    if (!cameraNode_)
        return -1;
    auto* camera = cameraNode_->GetComponent<Camera>();
    auto* graphics = GetSubsystem<Graphics>();
    if (!camera || !graphics)
        return -1;

    int best = -1;
    float bestDist = 24.0f;  // screen-pixel pick radius
    for (unsigned i = 0; i < ikTargets_.Size(); ++i)
    {
        if (!ikTargets_[i])
            continue;
        Vector3 viewPos = camera->GetView() * ikTargets_[i]->GetWorldPosition();
        if (viewPos.z_ <= 0.0f)
            continue;  // behind the camera
        Vector2 sp = camera->WorldToScreenPoint(ikTargets_[i]->GetWorldPosition());
        float px = sp.x_ * (float)graphics->GetWidth();
        float py = sp.y_ * (float)graphics->GetHeight();
        float d = Vector2((float)screenX - px, (float)screenY - py).Length();
        if (d < bestDist)
        {
            bestDist = d;
            best = (int)i;
        }
    }
    return best;
}

void ModelTool::AttachPropToBone(int boneIndex, const String& meshName)
{
    if (!isAnimated_ || !animatedModelComp_)
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (boneIndex < 0 || boneIndex >= (int)bones.Size())
        return;

    Node* boneNode = bones[boneIndex].node_;
    if (!boneNode)
        return;

    // Remove existing prop on this bone first
    RemovePropFromBone(boneIndex);

    Node* attachNode = boneNode->CreateChild("_PoseAttach");
    attachNode->SetTemporary(true);

    auto* cache = GetSubsystem<ResourceCache>();
    auto* model = attachNode->CreateComponent<StaticModel>();
    auto* mesh = cache->GetResource<Model>(meshName);
    if (mesh)
        model->SetModel(mesh);

    // Semi-transparent material
    auto* mat = cache->GetResource<Material>("Materials/DefaultGrey.xml");
    if (mat)
    {
        SharedPtr<Material> propMat = mat->Clone();
        propMat->SetShaderParameter("MatDiffColor", Color(0.6f, 0.8f, 1.0f, 0.4f));
        model->SetMaterial(propMat);
    }

    // Scale to bone-appropriate size
    float radius = BoneGizmoRadius(bones, boneIndex, cameraNode_);
    attachNode->SetScale(Vector3(radius * 0.3f, radius * 0.3f, radius * 0.3f));

    URHO3D_LOGINFOF("Attached prop '%s' to bone %d (%s)", meshName.CString(), boneIndex, bones[boneIndex].name_.CString());
}

void ModelTool::RemovePropFromBone(int boneIndex)
{
    if (!isAnimated_ || !animatedModelComp_)
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (boneIndex < 0 || boneIndex >= (int)bones.Size() || !bones[boneIndex].node_)
        return;

    Node* boneNode = bones[boneIndex].node_;
    Node* attach = boneNode->GetChild("_PoseAttach");
    if (attach)
        attach->Remove();
}

void ModelTool::RemoveAllProps()
{
    if (!isAnimated_ || !animatedModelComp_)
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    for (unsigned i = 0; i < bones.Size(); ++i)
    {
        if (bones[i].node_)
        {
            Node* attach = bones[i].node_->GetChild("_PoseAttach");
            if (attach)
                attach->Remove();
        }
    }
}

void ModelTool::HandlePropBrowseSelected(StringHash, VariantMap& eventData)
{
    String filePath = eventData[FileSelected::P_FILENAME].GetString();
    bool ok = eventData[FileSelected::P_OK].GetBool();
    fileSelector_.Reset();
    if (!ok || filePath.Empty())
        return;

    // Convert absolute path to resource-relative path
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    String resourcePath = filePath;
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (filePath.StartsWith(dirs[i]))
        {
            resourcePath = filePath.Substring(dirs[i].Length());
            break;
        }
    }

    AttachPropToBone(pendingPropBone_, resourcePath);
    UpdateBonePopover();
}

// ── Bone Rotation Gizmo ──────────────────────────────────────────────────────

void ModelTool::DrawBoneGizmo()
{
    if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_)
        return;

    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!debug) return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (selectedBone_ >= (int)bones.Size()) return;

    Node* boneNode = bones[selectedBone_].node_;
    if (!boneNode) return;

    Vector3 pos = boneNode->GetWorldPosition();
    float len = BoneGizmoRadius(bones, selectedBone_, cameraNode_);

    Vector3 axisX, axisY, axisZ;
    if (boneGizmoLocal_)
    {
        Quaternion rot = boneNode->GetWorldRotation();
        axisX = rot * Vector3::RIGHT;
        axisY = rot * Vector3::UP;
        axisZ = rot * Vector3::FORWARD;
    }
    else
    {
        axisX = Vector3::RIGHT;
        axisY = Vector3::UP;
        axisZ = Vector3::FORWARD;
    }

    // Axis lines from center
    Color colX = (boneGizmoAxis_ == 0 && boneGizmoDragging_) ? Color::YELLOW : Color::RED;
    Color colY = (boneGizmoAxis_ == 1 && boneGizmoDragging_) ? Color::YELLOW : Color::GREEN;
    Color colZ = (boneGizmoAxis_ == 2 && boneGizmoDragging_) ? Color::YELLOW : Color::BLUE;
    debug->AddLine(pos, pos + axisX * len, colX, false);
    debug->AddLine(pos, pos + axisY * len, colY, false);
    debug->AddLine(pos, pos + axisZ * len, colZ, false);

    // Axis rotation rings — constrained single-axis rotation
    const int segs = 32;
    for (int i = 0; i < segs; ++i)
    {
        float a0 = (float)i / segs * 360.0f * M_DEGTORAD;
        float a1 = (float)(i + 1) / segs * 360.0f * M_DEGTORAD;
        // X ring (YZ plane)
        debug->AddLine(pos + (axisY * cosf(a0) + axisZ * sinf(a0)) * len,
                      pos + (axisY * cosf(a1) + axisZ * sinf(a1)) * len, colX, false);
        // Y ring (XZ plane)
        debug->AddLine(pos + (axisX * cosf(a0) + axisZ * sinf(a0)) * len,
                      pos + (axisX * cosf(a1) + axisZ * sinf(a1)) * len, colY, false);
        // Z ring (XY plane)
        debug->AddLine(pos + (axisX * cosf(a0) + axisY * sinf(a0)) * len,
                      pos + (axisX * cosf(a1) + axisY * sinf(a1)) * len, colZ, false);
    }

    // Arcball circle — camera-facing, for free rotation (inside rings)
    Color circleCol = (boneGizmoAxis_ == -1 && boneGizmoDragging_) ? Color::YELLOW : Color(0.5f, 0.5f, 0.5f, 0.4f);
    Vector3 camDir = (cameraNode_->GetWorldPosition() - pos).Normalized();
    Vector3 circleRight = camDir.CrossProduct(Vector3::UP).Normalized();
    Vector3 circleUp = circleRight.CrossProduct(camDir).Normalized();
    for (int i = 0; i < segs; ++i)
    {
        float a0 = (float)i / segs * 360.0f * M_DEGTORAD;
        float a1 = (float)(i + 1) / segs * 360.0f * M_DEGTORAD;
        debug->AddLine(pos + (circleRight * cosf(a0) + circleUp * sinf(a0)) * len * 0.85f,
                      pos + (circleRight * cosf(a1) + circleUp * sinf(a1)) * len * 0.85f, circleCol, false);
    }

    // Draw rotation arc during drag — slerp from start to current on the virtual sphere
    if (boneGizmoDragging_)
    {
        Quaternion camRot = cameraNode_->GetWorldRotation();
        const int arcSegs = 16;
        for (int i = 0; i < arcSegs; ++i)
        {
            float t0 = (float)i / arcSegs;
            float t1 = (float)(i + 1) / arcSegs;

            // Slerp on the unit sphere between start and current
            float angle = acosf(Clamp(boneGizmoDragStart_.DotProduct(boneGizmoDragCurrent_), -1.0f, 1.0f));
            Vector3 s0, s1;
            if (angle < 0.001f)
                break; // no visible arc
            float sinA = sinf(angle);
            s0 = boneGizmoDragStart_ * (sinf((1.0f - t0) * angle) / sinA) + boneGizmoDragCurrent_ * (sinf(t0 * angle) / sinA);
            s1 = boneGizmoDragStart_ * (sinf((1.0f - t1) * angle) / sinA) + boneGizmoDragCurrent_ * (sinf(t1 * angle) / sinA);

            // View-space sphere points to world: camera rotation transforms the virtual sphere
            Vector3 w0 = pos + camRot * (s0 * len);
            Vector3 w1 = pos + camRot * (s1 * len);
            debug->AddLine(w0, w1, Color::YELLOW, false);
        }

        // Dot at drag start
        Vector3 startWorld = pos + camRot * (boneGizmoDragStart_ * len);
        debug->AddSphere(Sphere(startWorld, len * 0.05f), Color::WHITE, false);
    }
}

void ModelTool::BeginBoneGizmoDrag(int axis)
{
    if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_)
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (selectedBone_ >= (int)bones.Size()) return;

    Node* boneNode = bones[selectedBone_].node_;
    if (!boneNode) return;

    boneGizmoAxis_ = axis;
    boneGizmoDragging_ = true;
    boneGizmoNodeStart_ = boneNode->GetWorldPosition();
    boneGizmoRotStart_ = boneNode->GetRotation();

    auto* input = GetSubsystem<Input>();
    auto* camera = cameraNode_->GetComponent<Camera>();
    auto* graphics = GetSubsystem<Graphics>();
    IntVector2 mousePos = input->GetMousePosition();
    float w = (float)graphics->GetWidth();
    float h = (float)graphics->GetHeight();

    Vector2 screenCenter = camera->WorldToScreenPoint(boneGizmoNodeStart_);
    float cx = screenCenter.x_ * w;
    float cy = screenCenter.y_ * h;

    const Vector<Bone>& bonesRef = animatedModelComp_->GetSkeleton().GetBones();
    float gizmoLen = BoneGizmoRadius(bonesRef, selectedBone_, cameraNode_);
    Vector2 screenEdge = camera->WorldToScreenPoint(boneGizmoNodeStart_ + Vector3::RIGHT * gizmoLen);
    float sphereRadius = sqrtf((screenEdge.x_ * w - cx) * (screenEdge.x_ * w - cx) +
                               (screenEdge.y_ * h - cy) * (screenEdge.y_ * h - cy));
    if (sphereRadius < 10.0f) sphereRadius = 10.0f;

    float sx = ((float)mousePos.x_ - cx) / sphereRadius;
    float sy = ((float)mousePos.y_ - cy) / sphereRadius;
    float sz2 = 1.0f - sx * sx - sy * sy;
    if (sz2 > 0.0f)
        boneGizmoDragStart_ = Vector3(sx, -sy, sqrtf(sz2));
    else
        boneGizmoDragStart_ = Vector3(sx, -sy, 0.0f).Normalized();
}

void ModelTool::UpdateBoneGizmoDrag()
{
    if (!boneGizmoDragging_ || selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_)
        return;

    const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
    if (selectedBone_ >= (int)bones.Size()) return;

    Node* boneNode = bones[selectedBone_].node_;
    if (!boneNode) return;

    auto* input = GetSubsystem<Input>();
    auto* camera = cameraNode_->GetComponent<Camera>();
    auto* graphics = GetSubsystem<Graphics>();
    IntVector2 mousePos = input->GetMousePosition();
    float w = (float)graphics->GetWidth();
    float h = (float)graphics->GetHeight();

    Vector2 screenCenter = camera->WorldToScreenPoint(boneGizmoNodeStart_);
    float cx = screenCenter.x_ * w;
    float cy = screenCenter.y_ * h;

    float gizmoLen = BoneGizmoRadius(bones, selectedBone_, cameraNode_);
    Vector2 screenEdge = camera->WorldToScreenPoint(boneGizmoNodeStart_ + Vector3::RIGHT * gizmoLen);
    float sphereRadius = sqrtf((screenEdge.x_ * w - cx) * (screenEdge.x_ * w - cx) +
                               (screenEdge.y_ * h - cy) * (screenEdge.y_ * h - cy));
    if (sphereRadius < 10.0f) sphereRadius = 10.0f;

    float sx = ((float)mousePos.x_ - cx) / sphereRadius;
    float sy = ((float)mousePos.y_ - cy) / sphereRadius;
    float sz2 = 1.0f - sx * sx - sy * sy;
    Vector3 currentSphere;
    if (sz2 > 0.0f)
        currentSphere = Vector3(sx, -sy, sqrtf(sz2));
    else
        currentSphere = Vector3(sx, -sy, 0.0f).Normalized();

    boneGizmoDragCurrent_ = currentSphere;

    Vector3 cross = boneGizmoDragStart_.CrossProduct(currentSphere);
    float dot = boneGizmoDragStart_.DotProduct(currentSphere);
    Quaternion arcballRot(dot, cross.x_, cross.y_, cross.z_);
    arcballRot.Normalize();

    Quaternion camRot = cameraNode_->GetWorldRotation();
    Quaternion worldRot = camRot * arcballRot * camRot.Inverse();

    if (boneNode->GetParent())
    {
        Quaternion parentRot = boneNode->GetParent()->GetWorldRotation();
        boneNode->SetRotation(parentRot.Inverse() * worldRot * parentRot * boneGizmoRotStart_);
    }
    else
    {
        boneNode->SetRotation(worldRot * boneGizmoRotStart_);
    }

    // Sync sliders to match gizmo rotation
    UpdateBoneEditControls();
}

void ModelTool::EndBoneGizmoDrag()
{
    if (!boneGizmoDragging_)
        return;

    // Push history: before = rotation at drag start, after = current rotation
    if (selectedBone_ >= 0 && isAnimated_ && animatedModelComp_)
    {
        const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
        if (selectedBone_ < (int)bones.Size() && bones[selectedBone_].node_)
            PushBoneRotEdit(selectedBone_, boneGizmoRotStart_, bones[selectedBone_].node_->GetRotation());
    }

    boneGizmoDragging_ = false;
    boneGizmoAxis_ = -1;
}

// ── End Bone Rotation Gizmo ──────────────────────────────────────────────────

void ModelTool::SelectBone(int boneIndex)
{
    selectedBone_ = boneIndex;

    // Highlight the selected button in the bone list, dehighlight others
    for (unsigned i = 0; i < boneButtons_.Size(); ++i)
    {
        Button* btn = boneButtons_[i];
        bool selected = (btn->GetVar("BoneIndex").GetI32() == boneIndex);
        btn->SetColor(selected ? Color(0.35f, 0.35f, 0.15f, 0.9f) : Color(0.18f, 0.18f, 0.22f, 0.6f));
        auto* label = btn->GetChildStaticCast<Text>(0);
        if (label)
            label->SetColor(selected ? Color(1.0f, 1.0f, 0.4f) : Color(0.85f, 0.85f, 0.85f));
    }

    // Update status text
    if (boneIndex >= 0 && isAnimated_ && animatedModelComp_)
    {
        Skeleton& skeleton = animatedModelComp_->GetSkeleton();
        const Vector<Bone>& bones = skeleton.GetBones();
        if (boneIndex < (int)bones.Size() && statusText_)
            statusText_->SetText("Bone: " + bones[boneIndex].name_);
    }
    else if (statusText_ && currentModelPath_.Length())
    {
        statusText_->SetText(currentModelPath_);
    }

    UpdateBonePopover();
    UpdateBoneEditControls();
}

void ModelTool::HandleBoneListClick(StringHash, VariantMap& eventData)
{
    auto* btn = static_cast<Button*>(eventData[Released::P_ELEMENT].GetPtr());
    if (!btn) return;
    int boneIndex = btn->GetVar("BoneIndex").GetI32();

    // Shift+click toggles bone filter (bone + descendants)
    auto* input = GetSubsystem<Input>();
    if (input->GetKeyDown(KEY_LSHIFT) || input->GetKeyDown(KEY_RSHIFT))
    {
        ToggleBoneFilter(boneIndex);
        return;
    }

    SelectBone(boneIndex);
}

void ModelTool::UpdateBonePopover()
{
    if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_)
    {
        if (bonePopover_) bonePopover_->SetVisible(false);
        return;
    }

    Skeleton& skeleton = animatedModelComp_->GetSkeleton();
    const Vector<Bone>& bones = skeleton.GetBones();
    if (selectedBone_ >= (int)bones.Size())
    {
        if (bonePopover_) bonePopover_->SetVisible(false);
        return;
    }

    auto* ui = GetSubsystem<UI>();

    // Create side panel once — anchored to the left edge
    if (!bonePopover_)
    {
        bonePopover_ = ui->GetRoot()->CreateChild<Window>("BonePanel");
        bonePopover_->SetStyleAuto();
        bonePopover_->SetLayout(LM_VERTICAL, 2, IntRect(8, 6, 8, 6));
        bonePopover_->SetFixedWidth(250);
        bonePopover_->SetPosition(10, 36);
        bonePopover_->SetMovable(true);
        bonePopover_->SetOpacity(0.93f);
        bonePopover_->SetColor(Color(0.18f, 0.18f, 0.24f));

        // Title bar
        auto* titleBar = bonePopover_->CreateChild<BorderImage>();
        titleBar->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
        titleBar->SetFixedHeight(22);
        titleBar->SetColor(Color(0.14f, 0.14f, 0.18f));

        auto* title = titleBar->CreateChild<Text>();
        title->SetFont(font_, 12);
        title->SetText("Bone");
        title->SetColor(Color(0.9f, 0.9f, 0.4f));
    }

    // Remove old content lines (keep title bar at index 0)
    while (bonePopover_->GetNumChildren() > 1)
        bonePopover_->RemoveChildAtIndex(1);

    const Bone& bone = bones[selectedBone_];

    auto addField = [this](const String& text, const Color& color = Color(0.85f, 0.85f, 0.85f))
    {
        auto* line = bonePopover_->CreateChild<Text>();
        line->SetFont(font_, 10);
        line->SetText(text);
        line->SetColor(color);
    };

    // Title
    addField(bone.name_ + "  [" + String(selectedBone_) + "]", Color(0.95f, 0.9f, 0.4f));

    // Parent
    String parentName = "ROOT";
    if (bone.parentIndex_ >= 0 && bone.parentIndex_ < (int)bones.Size())
        parentName = bones[bone.parentIndex_].name_ + " [" + String(bone.parentIndex_) + "]";
    addField("Parent: " + parentName);

    // Depth
    int depth = 0;
    int p = bone.parentIndex_;
    while (p > 0 && depth < 20) { p = bones[p].parentIndex_; depth++; }
    addField("Depth: " + String(depth));

    // Initial position
    const Vector3& pos = bone.initialPosition_;
    addField("Position: " + String((double)pos.x_, 3) + ", " + String((double)pos.y_, 3) + ", " + String((double)pos.z_, 3));

    // Initial rotation (as Euler)
    Vector3 euler = bone.initialRotation_.EulerAngles();
    addField("Rotation: " + String((double)euler.x_, 1) + ", " + String((double)euler.y_, 1) + ", " + String((double)euler.z_, 1));

    // Initial scale
    const Vector3& scl = bone.initialScale_;
    addField("Scale: " + String((double)scl.x_, 3) + ", " + String((double)scl.y_, 3) + ", " + String((double)scl.z_, 3));

    // Radius
    addField("Radius: " + String((double)bone.radius_, 4));

    // Bounding box
    const BoundingBox& bb = bone.boundingBox_;
    if (bb.Defined())
    {
        addField("BBox min: " + String((double)bb.min_.x_, 2) + ", " + String((double)bb.min_.y_, 2) + ", " + String((double)bb.min_.z_, 2));
        addField("BBox max: " + String((double)bb.max_.x_, 2) + ", " + String((double)bb.max_.y_, 2) + ", " + String((double)bb.max_.z_, 2));
    }
    else
        addField("BBox: undefined");

    // Collision type
    String collStr = "None";
    if (bone.collisionMask_ & BONECOLLISION_SPHERE) collStr = "Sphere";
    if (bone.collisionMask_ & BONECOLLISION_BOX) collStr = "Box";
    if ((bone.collisionMask_ & BONECOLLISION_SPHERE) && (bone.collisionMask_ & BONECOLLISION_BOX)) collStr = "Sphere+Box";
    addField("Collision: " + collStr);

    // Animated flag
    addField(String("Animated: ") + (bone.animated_ ? "Yes" : "No"));

    // World position (runtime, from node)
    if (bone.node_)
    {
        Vector3 wp = bone.node_->GetWorldPosition();
        addField("World pos: " + String((double)wp.x_, 3) + ", " + String((double)wp.y_, 3) + ", " + String((double)wp.z_, 3),
                 Color(0.6f, 0.85f, 0.6f));
    }

    // --- Bone rotation sliders for pose editing ---
    {
        addField("--- Pose Edit ---", Color(0.4f, 0.9f, 0.4f));

        auto makeRotSlider = [this](const String& axis, Slider*& slider, Text*& label)
        {
            auto* row = bonePopover_->CreateChild<UIElement>();
            row->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
            row->SetMinHeight(18);

            label = row->CreateChild<Text>();
            label->SetFont(font_, 10);
            label->SetText(axis + ": 0");
            label->SetColor(Color(0.9f, 0.9f, 0.9f));
            label->SetMinWidth(40);

            slider = row->CreateChild<Slider>();
            slider->SetStyleAuto();
            slider->SetFixedHeight(12);
            slider->SetMinWidth(150);
            slider->SetRange(360.0f);
            slider->SetValue(180.0f);  // center = no offset
            SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(ModelTool, HandleBoneRotationSlider));
        };

        makeRotSlider("X", boneRotX_, boneRotXLabel_);
        makeRotSlider("Y", boneRotY_, boneRotYLabel_);
        makeRotSlider("Z", boneRotZ_, boneRotZLabel_);

        // Filter status
        bool inFilter = poseBoneFilter_.Contains(selectedBone_);
        addField(inFilter ? "[In pose filter]" : "[Not in pose filter — Shift+click to add]",
                 inFilter ? Color(0.4f, 1.0f, 0.4f) : Color(0.6f, 0.6f, 0.6f));

        // --- Pose toolbar ---
        addField("--- Pose Tools ---", Color(0.4f, 0.9f, 0.7f));

        auto* toolRow = bonePopover_->CreateChild<UIElement>();
        toolRow->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        toolRow->SetMinHeight(22);

        // Local/World toggle
        auto* localBtn = toolRow->CreateChild<Button>();
        localBtn->SetStyleAuto();
        localBtn->SetFixedSize(65, 20);
        auto* localLbl = localBtn->CreateChild<Text>();
        localLbl->SetFont(font_, 10);
        localLbl->SetText(boneGizmoLocal_ ? "Local" : "World");
        localLbl->SetAlignment(HA_CENTER, VA_CENTER);
        SubscribeToEvent(localBtn, E_RELEASED, [this, localLbl](StringHash, VariantMap&) {
            boneGizmoLocal_ = !boneGizmoLocal_;
            localLbl->SetText(boneGizmoLocal_ ? "Local" : "World");
        });

        // Reset Bone
        auto* resetBtn = toolRow->CreateChild<Button>();
        resetBtn->SetStyleAuto();
        resetBtn->SetFixedSize(55, 20);
        auto* resetLbl = resetBtn->CreateChild<Text>();
        resetLbl->SetFont(font_, 10);
        resetLbl->SetText("Reset");
        resetLbl->SetAlignment(HA_CENTER, VA_CENTER);
        resetBtn->SetVar("BoneIndex", selectedBone_);
        SubscribeToEvent(resetBtn, E_RELEASED, [this](StringHash, VariantMap& ed) {
            auto* btn = static_cast<Button*>(ed[Released::P_ELEMENT].GetPtr());
            int bi = btn->GetVar("BoneIndex").GetI32();
            if (bi >= 0 && isAnimated_ && animatedModelComp_)
            {
                const Vector<Bone>& bones = animatedModelComp_->GetSkeleton().GetBones();
                if (bi < (int)bones.Size() && bones[bi].node_)
                {
                    Quaternion before = bones[bi].node_->GetRotation();
                    bones[bi].node_->SetRotation(bones[bi].initialRotation_);
                    PushBoneRotEdit(bi, before, bones[bi].initialRotation_);
                    UpdateBoneEditControls();
                }
            }
        });

        // Undo
        auto* undoBtn = toolRow->CreateChild<Button>();
        undoBtn->SetStyleAuto();
        undoBtn->SetFixedSize(45, 20);
        auto* undoLbl = undoBtn->CreateChild<Text>();
        undoLbl->SetFont(font_, 10);
        undoLbl->SetText("Undo");
        undoLbl->SetAlignment(HA_CENTER, VA_CENTER);
        SubscribeToEvent(undoBtn, E_RELEASED, [this](StringHash, VariantMap&) { UndoBoneRot(); });

        // --- Prop attachment ---
        addField("--- Props ---", Color(0.4f, 0.7f, 0.9f));

        bool hasProp = bone.node_ && bone.node_->GetChild("_PoseAttach");

        // Attach/Remove button row with primitive type dropdown
        auto* propRow = bonePopover_->CreateChild<UIElement>();
        propRow->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        propRow->SetMinHeight(22);

        auto* attachBtn = propRow->CreateChild<Button>();
        attachBtn->SetStyleAuto();
        attachBtn->SetFixedSize(80, 20);
        auto* attachLbl = attachBtn->CreateChild<Text>();
        attachLbl->SetFont(font_, 10);
        attachLbl->SetText(hasProp ? "Remove Prop" : "Attach Prop");
        attachLbl->SetAlignment(HA_CENTER, VA_CENTER);
        attachBtn->SetVar("BoneIndex", selectedBone_);
        attachBtn->SetVar("HasProp", hasProp);

        // Prop type dropdown — primitives, weapons, tools, then Browse
        static const char* propMeshPaths[] = {
            // Primitives
            "Models/Cylinder.mdl", "Models/Box.mdl", "Models/Sphere.mdl",
            "Models/Cone.mdl", "Models/Capsule.mdl",
            // Weapons
            "Models/Weapons/Axe.mdl", "Models/Weapons/Axe_Small.mdl",
            "Models/Weapons/Axe_Double.mdl", "Models/Weapons/Sword.mdl",
            "Models/Weapons/Sword_2.mdl", "Models/Weapons/Sword_Big.mdl",
            "Models/Weapons/Dagger.mdl", "Models/Weapons/Dagger_2.mdl",
            "Models/Weapons/Spear.mdl", "Models/Weapons/Claymore.mdl",
            "Models/Weapons/Scythe.mdl",
            "Models/Weapons/Bow_Wooden.mdl", "Models/Weapons/Bow_Wooden2.mdl",
            "Models/Weapons/Arrow.mdl",
            "Models/Weapons/Shield_Round.mdl", "Models/Weapons/Shield_Round_2.mdl",
            "Models/Weapons/Shield_Heater.mdl", "Models/Weapons/Shield_Heater_2.mdl",
            "Models/Weapons/Shield_Celtic_Golden.mdl",
            "Models/Weapons/Hammer_Small.mdl", "Models/Weapons/Hammer_Double.mdl",
            // Tools & Props
            "Models/Survival/Axe.mdl", "Models/Survival/Axe_Small.mdl",
            "Models/Props/Pickaxe_Bronze.mdl", "Models/Props/Torch_Metal.mdl",
            "Models/Props/Anvil_Log.mdl", "Models/Props/Workbench.mdl",
            "Models/Props/Bucket_Wooden_1.mdl", "Models/Props/Bag.mdl",
            "Models/Props/Barrel.mdl", "Models/Props/Chest_Wood.mdl",
            "Models/Props/Cauldron.mdl", "Models/Props/Rope_1.mdl",
        };
        static const char* propMeshLabels[] = {
            // Primitives
            "Cylinder", "Box", "Sphere", "Cone", "Capsule",
            // Weapons
            "Axe", "Axe (Small)", "Axe (Double)",
            "Sword", "Sword 2", "Sword (Big)",
            "Dagger", "Dagger 2", "Spear", "Claymore", "Scythe",
            "Bow", "Bow 2", "Arrow",
            "Shield (Round)", "Shield (Round 2)",
            "Shield (Heater)", "Shield (Heater 2)", "Shield (Celtic)",
            "Hammer (Small)", "Hammer (Double)",
            // Tools & Props
            "Hatchet", "Hatchet (Small)",
            "Pickaxe", "Torch", "Anvil", "Workbench",
            "Bucket", "Bag", "Barrel", "Chest", "Cauldron", "Rope",
            // Browse
            "Browse...",
        };
        static const int NUM_PROP_ENTRIES = sizeof(propMeshLabels) / sizeof(propMeshLabels[0]);
        static const int BROWSE_INDEX = NUM_PROP_ENTRIES - 1;

        auto* propDD = propRow->CreateChild<DropDownList>();
        propDD->SetStyleAuto();
        propDD->SetFixedSize(100, 20);
        propDD->SetResizePopup(true);
        propDD->SetColor(Color(0.18f, 0.18f, 0.22f));
        for (int pi = 0; pi < NUM_PROP_ENTRIES; ++pi)
        {
            auto* item = new Text(context_);
            item->SetFont(font_, 10);
            item->SetText(propMeshLabels[pi]);
            item->SetColor(Color(0.85f, 0.85f, 0.85f));
            item->SetMinWidth(90);
            propDD->AddItem(item);
        }
        propDD->SetSelection(0);

        SubscribeToEvent(attachBtn, E_RELEASED, [this, propDD](StringHash, VariantMap& ed) {
            auto* btn = static_cast<Button*>(ed[Released::P_ELEMENT].GetPtr());
            if (!btn) return;
            int bi = btn->GetVar("BoneIndex").GetI32();
            if (btn->GetVar("HasProp").GetBool())
            {
                RemovePropFromBone(bi);
                propPosX_ = propPosY_ = propPosZ_ = nullptr;
                propRotX_ = propRotY_ = propRotZ_ = nullptr;
                propScale_ = nullptr;
            }
            else
            {
                unsigned sel = propDD ? propDD->GetSelection() : 0;
                if (sel == (unsigned)BROWSE_INDEX)
                {
                    // Browse... — open file dialog for arbitrary .mdl
                    pendingPropBone_ = bi;
                    auto* ui = GetSubsystem<UI>();
                    auto* style = ui->GetRoot()->GetDefaultStyle();
                    fileSelector_ = new FileSelector(context_);
                    fileSelector_->SetDefaultStyle(style);
                    fileSelector_->SetTitle("Select Prop Model");
                    fileSelector_->SetButtonTexts("Attach", "Cancel");
                    Vector<String> filters;
                    filters.Push("*.mdl");
                    fileSelector_->SetFilters(filters, 0);
                    auto* cache = GetSubsystem<ResourceCache>();
                    const Vector<String>& dirs = cache->GetResourceDirs();
                    if (!dirs.Empty())
                        fileSelector_->SetPath(dirs[0] + "Models/");
                    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(ModelTool, HandlePropBrowseSelected));
                    return;
                }
                if (sel >= (unsigned)BROWSE_INDEX) sel = 0;
                AttachPropToBone(bi, propMeshPaths[sel]);
            }
            UpdateBonePopover();
        });

        // Offset controls (only when prop exists)
        if (hasProp)
        {
            Node* attachNode = bone.node_->GetChild("_PoseAttach");

            auto makePropSlider = [this](const String& label, float range, float value, Slider*& outSlider)
            {
                auto* row = bonePopover_->CreateChild<UIElement>();
                row->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
                row->SetMinHeight(18);

                auto* lbl = row->CreateChild<Text>();
                lbl->SetFont(font_, 9);
                lbl->SetText(label);
                lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
                lbl->SetMinWidth(36);

                outSlider = row->CreateChild<Slider>();
                outSlider->SetStyleAuto();
                outSlider->SetFixedHeight(12);
                outSlider->SetMinWidth(140);
                outSlider->SetRange(range);
                outSlider->SetValue(value);
                SubscribeToEvent(outSlider, E_SLIDERCHANGED, URHO3D_HANDLER(ModelTool, HandlePropOffsetSlider));
            };

            // Position sliders: range 200 (center 100 = 0.0, maps to [-1, +1])
            Vector3 pos = attachNode ? attachNode->GetPosition() : Vector3::ZERO;
            makePropSlider("PosX", 200.0f, 100.0f + pos.x_ * 100.0f, propPosX_);
            makePropSlider("PosY", 200.0f, 100.0f + pos.y_ * 100.0f, propPosY_);
            makePropSlider("PosZ", 200.0f, 100.0f + pos.z_ * 100.0f, propPosZ_);

            // Rotation sliders: range 360
            Vector3 euler = attachNode ? attachNode->GetRotation().EulerAngles() : Vector3::ZERO;
            makePropSlider("RotX", 360.0f, euler.x_ < 0 ? euler.x_ + 360.0f : euler.x_, propRotX_);
            makePropSlider("RotY", 360.0f, euler.y_ < 0 ? euler.y_ + 360.0f : euler.y_, propRotY_);
            makePropSlider("RotZ", 360.0f, euler.z_ < 0 ? euler.z_ + 360.0f : euler.z_, propRotZ_);

            // Scale slider: range 300 (value = percentage, 100 = 1.0x)
            float curScale = attachNode ? attachNode->GetScale().x_ : 1.0f;
            makePropSlider("Scale", 300.0f, curScale * 100.0f, propScale_);

            // Reset button
            auto* resetRow = bonePopover_->CreateChild<UIElement>();
            resetRow->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
            resetRow->SetMinHeight(22);

            auto* resetBtn = resetRow->CreateChild<Button>();
            resetBtn->SetStyleAuto();
            resetBtn->SetFixedSize(55, 20);
            auto* resetLbl = resetBtn->CreateChild<Text>();
            resetLbl->SetFont(font_, 10);
            resetLbl->SetText("Reset");
            resetLbl->SetAlignment(HA_CENTER, VA_CENTER);
            SubscribeToEvent(resetBtn, E_RELEASED, [this](StringHash, VariantMap&) {
                if (selectedBone_ < 0 || !isAnimated_ || !animatedModelComp_) return;
                const Vector<Bone>& b = animatedModelComp_->GetSkeleton().GetBones();
                if (selectedBone_ >= (int)b.Size() || !b[selectedBone_].node_) return;
                Node* an = b[selectedBone_].node_->GetChild("_PoseAttach");
                if (an)
                {
                    an->SetPosition(Vector3::ZERO);
                    an->SetRotation(Quaternion::IDENTITY);
                    float r = BoneGizmoRadius(b, selectedBone_, cameraNode_);
                    an->SetScale(Vector3(r * 0.3f, r * 0.3f, r * 0.3f));
                }
                UpdateBonePopover();
            });
        }
        else
        {
            propPosX_ = propPosY_ = propPosZ_ = nullptr;
            propRotX_ = propRotY_ = propRotZ_ = nullptr;
            propScale_ = nullptr;
        }
    }

    bonePopover_->SetVisible(true);
}

// ============================================================================
// Vertex Editor
// ============================================================================

void ModelTool::EnterVertexEditMode()
{
    if (!currentModel_ || vertexEditMode_) return;

    editModel_ = currentModel_->Clone();
    if (!editModel_) return;

    // Swap cloned model onto the component
    if (animatedModelComp_)
    {
        animatedModelComp_->SetModel(editModel_);
    }
    else if (staticModelComp_)
    {
        staticModelComp_->SetModel(editModel_);
    }

    vertexEditMode_ = true;
    selectedVertex_ = -1;
    selectedGeometry_ = -1;
    vertexDirty_ = false;
    vertexDragging_ = false;

    // Create billboard overlay for vertex dots
    vertexOverlayNode_ = scene_->CreateChild("VertexOverlay");
    vertexBillboards_ = vertexOverlayNode_->CreateComponent<BillboardSet>();
    vertexBillboards_->SetFixedScreenSize(true);
    vertexBillboards_->SetFaceCameraMode(FC_ROTATE_XYZ);
    vertexBillboards_->SetSorted(false);

    // Create a small white circle texture (8x8) for the dot sprite
    SharedPtr<Image> dotImage(new Image(context_));
    dotImage->SetSize(8, 8, 4);
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            float dx = x - 3.5f, dy = y - 3.5f;
            float dist = sqrtf(dx * dx + dy * dy);
            unsigned char a = dist < 3.0f ? 255 : (dist < 3.5f ? (unsigned char)(255.0f * (3.5f - dist) / 0.5f) : 0);
            dotImage->SetPixel(x, y, Color(1.0f, 1.0f, 1.0f, a / 255.0f));
        }
    }
    SharedPtr<Texture2D> dotTex(new Texture2D(context_));
    dotTex->SetData(dotImage);

    auto* cache = GetSubsystem<ResourceCache>();
    SharedPtr<Material> dotMat(new Material(context_));
    dotMat->SetTechnique(0, cache->GetResource<Technique>("Techniques/DiffUnlitParticleAlpha.xml"));
    dotMat->SetTexture(TU_DIFFUSE, dotTex);
    dotMat->SetCullMode(CULL_NONE);
    vertexBillboards_->SetMaterial(dotMat);

    if (statusText_)
        statusText_->SetText("VERTEX EDIT — Click=select, DEL=delete, Ctrl+S=save, V=exit");

    URHO3D_LOGINFO("Entered vertex edit mode");
}

void ModelTool::ExitVertexEditMode()
{
    if (!vertexEditMode_) return;

    if (vertexDirty_)
    {
        if (statusText_)
            statusText_->SetText("UNSAVED CHANGES — Ctrl+S to save, V again to discard");
        vertexEditMode_ = false;  // Will still prompt on next V
        // Set a flag so next V truly exits
        // Actually, just let them exit — they were warned
    }

    // Restore original model
    if (animatedModelComp_)
        animatedModelComp_->SetModel(currentModel_);
    else if (staticModelComp_)
        staticModelComp_->SetModel(currentModel_);

    editModel_.Reset();
    vertexEditMode_ = false;
    selectedVertex_ = -1;
    selectedGeometry_ = -1;
    vertexDirty_ = false;
    vertexDragging_ = false;

    // Remove billboard overlay
    if (vertexOverlayNode_)
    {
        vertexOverlayNode_->Remove();
        vertexOverlayNode_ = nullptr;
        vertexBillboards_ = nullptr;
    }

    if (statusText_)
        statusText_->SetText(currentModelPath_);

    URHO3D_LOGINFO("Exited vertex edit mode");
}

Vector3 ModelTool::SkinVertex(const unsigned char* vertData, unsigned stride, unsigned posOffset,
    unsigned weightOffset, unsigned indexOffset, bool hasSkinning, unsigned vertIndex,
    const Vector<Bone>& bones, const Matrix3x4& worldTransform)
{
    Vector3 localPos = *reinterpret_cast<const Vector3*>(vertData + vertIndex * stride + posOffset);

    if (!hasSkinning)
        return worldTransform * localPos;

    const float* weights = reinterpret_cast<const float*>(vertData + vertIndex * stride + weightOffset);
    const unsigned char* indices = vertData + vertIndex * stride + indexOffset;

    Vector3 skinnedPos = Vector3::ZERO;
    for (int b = 0; b < 4; ++b)
    {
        float w = weights[b];
        if (w < 0.001f) continue;

        unsigned boneIdx = indices[b];
        if (boneIdx >= bones.Size() || !bones[boneIdx].node_) continue;

        Matrix3x4 skinMatrix = bones[boneIdx].node_->GetWorldTransform() * bones[boneIdx].offsetMatrix_;
        skinnedPos += (skinMatrix * localPos) * w;
    }
    return skinnedPos;
}

void ModelTool::DrawVertexOverlay(DebugRenderer* debug)
{
    if (!editModel_ || !modelNode_ || !vertexBillboards_) return;

    Matrix3x4 worldTransform = modelNode_->GetWorldTransform();

    // Get skeleton for CPU skinning
    const Vector<Bone>* bonesPtr = nullptr;
    if (isAnimated_ && animatedModelComp_)
        bonesPtr = &animatedModelComp_->GetSkeleton().GetBones();

    // Count total vertices across all geometries
    unsigned totalVerts = 0;
    for (unsigned g = 0; g < editModel_->GetNumGeometries(); ++g)
    {
        Geometry* geom = editModel_->GetGeometry(g, 0);
        if (!geom) continue;
        VertexBuffer* vb = geom->GetVertexBuffer(0);
        if (vb && vb->GetShadowData())
            totalVerts += vb->GetVertexCount();
    }

    vertexBillboards_->SetNumBillboards(totalVerts);

    unsigned bbIndex = 0;
    for (unsigned g = 0; g < editModel_->GetNumGeometries(); ++g)
    {
        Geometry* geom = editModel_->GetGeometry(g, 0);
        if (!geom) continue;

        VertexBuffer* vb = geom->GetVertexBuffer(0);
        if (!vb) continue;

        const auto* data = vb->GetShadowData();
        if (!data) continue;

        unsigned stride = vb->GetVertexSize();
        unsigned posOffset = vb->GetElementOffset(SEM_POSITION);
        unsigned vertCount = vb->GetVertexCount();

        bool hasSkinning = vb->HasElement(SEM_BLENDWEIGHTS) && vb->HasElement(SEM_BLENDINDICES);
        unsigned weightOffset = hasSkinning ? vb->GetElementOffset(SEM_BLENDWEIGHTS) : 0;
        unsigned indexOffset = hasSkinning ? vb->GetElementOffset(SEM_BLENDINDICES) : 0;

        const Vector<Bone> emptyBones;
        const Vector<Bone>& bones = bonesPtr ? *bonesPtr : emptyBones;

        for (unsigned i = 0; i < vertCount; ++i)
        {
            Vector3 worldPos = SkinVertex(reinterpret_cast<const unsigned char*>(data), stride, posOffset, weightOffset, indexOffset,
                hasSkinning && bonesPtr, i, bones, worldTransform);

            Billboard* bb = vertexBillboards_->GetBillboard(bbIndex);
            bb->position_ = worldPos;
            bb->enabled_ = true;

            if ((int)g == selectedGeometry_ && (int)i == selectedVertex_)
            {
                bb->size_ = Vector2(8.0f, 8.0f);
                bb->color_ = Color::YELLOW;
            }
            else
            {
                bb->size_ = Vector2(4.0f, 4.0f);
                bb->color_ = Color(0.9f, 0.2f, 0.2f);
            }
            ++bbIndex;
        }
    }

    vertexBillboards_->Commit();
}

void ModelTool::PickVertex(int screenX, int screenY)
{
    if (!editModel_ || !cameraNode_) return;

    auto* camera = cameraNode_->GetComponent<Camera>();
    if (!camera) return;

    auto* graphics = GetSubsystem<Graphics>();
    float nx = (float)screenX / (float)graphics->GetWidth();
    float ny = (float)screenY / (float)graphics->GetHeight();
    Ray ray = camera->GetScreenRay(nx, ny);

    Matrix3x4 worldTransform = modelNode_->GetWorldTransform();

    // Get skeleton for CPU skinning (same as DrawVertexOverlay)
    const Vector<Bone>* bonesPtr = nullptr;
    if (isAnimated_ && animatedModelComp_)
        bonesPtr = &animatedModelComp_->GetSkeleton().GetBones();

    float bestDist = M_INFINITY;
    int bestGeom = -1, bestVert = -1;

    for (unsigned g = 0; g < editModel_->GetNumGeometries(); ++g)
    {
        Geometry* geom = editModel_->GetGeometry(g, 0);
        if (!geom) continue;

        VertexBuffer* vb = geom->GetVertexBuffer(0);
        if (!vb) continue;

        const auto* data = vb->GetShadowData();
        if (!data) continue;

        unsigned stride = vb->GetVertexSize();
        unsigned posOffset = vb->GetElementOffset(SEM_POSITION);
        unsigned vertCount = vb->GetVertexCount();

        bool hasSkinning = vb->HasElement(SEM_BLENDWEIGHTS) && vb->HasElement(SEM_BLENDINDICES);
        unsigned weightOffset = hasSkinning ? vb->GetElementOffset(SEM_BLENDWEIGHTS) : 0;
        unsigned indexOffset = hasSkinning ? vb->GetElementOffset(SEM_BLENDINDICES) : 0;

        const Vector<Bone> emptyBones;
        const Vector<Bone>& bones = bonesPtr ? *bonesPtr : emptyBones;

        for (unsigned i = 0; i < vertCount; ++i)
        {
            Vector3 worldPos = SkinVertex(reinterpret_cast<const unsigned char*>(data), stride, posOffset, weightOffset, indexOffset,
                hasSkinning && bonesPtr, i, bones, worldTransform);

            Vector3 toPoint = worldPos - ray.origin_;
            float along = toPoint.DotProduct(ray.direction_);
            if (along < 0.0f) continue;

            Vector3 closest = ray.origin_ + ray.direction_ * along;
            float dist = (closest - worldPos).Length();

            float threshold = along * 0.02f;
            if (threshold < 0.05f) threshold = 0.05f;

            if (dist < threshold && dist < bestDist)
            {
                bestDist = dist;
                bestGeom = (int)g;
                bestVert = (int)i;
            }
        }
    }

    selectedGeometry_ = bestGeom;
    selectedVertex_ = bestVert;
    UpdateVertexStatusText();
}

void ModelTool::DeleteSelectedVertex()
{
    if (!editModel_ || selectedGeometry_ < 0 || selectedVertex_ < 0) return;

    Geometry* geom = editModel_->GetGeometry((unsigned)selectedGeometry_, 0);
    if (!geom) return;

    VertexBuffer* vb = geom->GetVertexBuffer(0);
    IndexBuffer* ib = geom->GetIndexBuffer();
    if (!vb || !ib) return;

    const auto* vertData = vb->GetShadowData();
    const auto* indexData = ib->GetShadowData();
    if (!vertData || !indexData) return;

    unsigned vertSize = vb->GetVertexSize();
    unsigned vertCount = vb->GetVertexCount();
    unsigned indexSize = ib->GetIndexSize();
    unsigned indexCount = ib->GetIndexCount();
    unsigned vertToDelete = (unsigned)selectedVertex_;

    if (vertToDelete >= vertCount) return;

    // --- Rebuild index buffer: skip triangles referencing deleted vert, remap ---
    Vector<unsigned> newIndices;
    for (unsigned i = 0; i + 2 < indexCount; i += 3)
    {
        unsigned i0, i1, i2;
        if (indexSize == 2)
        {
            i0 = ((const unsigned short*)indexData)[i];
            i1 = ((const unsigned short*)indexData)[i + 1];
            i2 = ((const unsigned short*)indexData)[i + 2];
        }
        else
        {
            i0 = ((const unsigned*)indexData)[i];
            i1 = ((const unsigned*)indexData)[i + 1];
            i2 = ((const unsigned*)indexData)[i + 2];
        }

        // Skip triangles that reference the deleted vertex
        if (i0 == vertToDelete || i1 == vertToDelete || i2 == vertToDelete)
            continue;

        // Remap indices above deleted vertex down by 1
        if (i0 > vertToDelete) i0--;
        if (i1 > vertToDelete) i1--;
        if (i2 > vertToDelete) i2--;

        newIndices.Push(i0);
        newIndices.Push(i1);
        newIndices.Push(i2);
    }

    // --- Rebuild vertex buffer: remove the deleted vertex ---
    unsigned newVertCount = vertCount - 1;
    SharedArrayPtr<byte> newVertData(new byte[newVertCount * vertSize]);

    // Copy vertices before the deleted one
    if (vertToDelete > 0)
        memcpy(newVertData.Get(), vertData, vertToDelete * vertSize);

    // Copy vertices after the deleted one
    if (vertToDelete < vertCount - 1)
        memcpy(newVertData.Get() + vertToDelete * vertSize,
               vertData + (vertToDelete + 1) * vertSize,
               (vertCount - vertToDelete - 1) * vertSize);

    // --- Upload to GPU ---
    vb->SetSize(newVertCount, vb->GetElements());
    vb->SetData(newVertData.Get());

    unsigned newIndexCount = newIndices.Size();
    bool largeIndices = newVertCount > 65535;
    ib->SetSize(newIndexCount, largeIndices);

    if (largeIndices)
    {
        ib->SetData(newIndices.Buffer());
    }
    else
    {
        Vector<unsigned short> shortIndices(newIndexCount);
        for (unsigned i = 0; i < newIndexCount; ++i)
            shortIndices[i] = (unsigned short)newIndices[i];
        ib->SetData(shortIndices.Buffer());
    }

    geom->SetDrawRange(TRIANGLE_LIST, 0, newIndexCount);

    // --- Recalculate bounding box ---
    BoundingBox newBBox;
    for (unsigned g = 0; g < editModel_->GetNumGeometries(); ++g)
    {
        Geometry* gm = editModel_->GetGeometry(g, 0);
        if (!gm) continue;
        VertexBuffer* gvb = gm->GetVertexBuffer(0);
        if (!gvb || !gvb->GetShadowData()) continue;

        unsigned gStride = gvb->GetVertexSize();
        unsigned gPosOff = gvb->GetElementOffset(SEM_POSITION);
        const auto* gData = gvb->GetShadowData();
        for (unsigned v = 0; v < gvb->GetVertexCount(); ++v)
        {
            Vector3 pos = *reinterpret_cast<const Vector3*>(gData + v * gStride + gPosOff);
            newBBox.Merge(pos);
        }
    }
    editModel_->SetBoundingBox(newBBox);

    unsigned removedTris = (indexCount - newIndexCount) / 3;
    URHO3D_LOGINFOF("Deleted vertex %d from geometry %d — removed %d triangles", selectedVertex_, selectedGeometry_, removedTris);

    selectedVertex_ = -1;
    selectedGeometry_ = -1;
    vertexDirty_ = true;
    UpdateVertexStatusText();
    RebuildInfoText();
}

void ModelTool::MoveSelectedVertex(int screenX, int screenY)
{
    if (!editModel_ || selectedGeometry_ < 0 || selectedVertex_ < 0 || !cameraNode_) return;

    auto* camera = cameraNode_->GetComponent<Camera>();
    if (!camera) return;

    auto* graphics = GetSubsystem<Graphics>();
    float nx = (float)screenX / (float)graphics->GetWidth();
    float ny = (float)screenY / (float)graphics->GetHeight();
    Ray ray = camera->GetScreenRay(nx, ny);

    Plane dragPlane(dragPlaneNormal_, dragStartPos_);
    float hitDist = ray.HitDistance(dragPlane);
    if (hitDist >= M_INFINITY) return;

    Vector3 newWorldPos = ray.origin_ + ray.direction_ * hitDist;
    Vector3 newLocalPos = modelNode_->GetWorldTransform().Inverse() * newWorldPos;

    Geometry* geom = editModel_->GetGeometry((unsigned)selectedGeometry_, 0);
    if (!geom) return;

    VertexBuffer* vb = geom->GetVertexBuffer(0);
    if (!vb) return;

    auto* data = const_cast<byte*>(vb->GetShadowData());
    if (!data) return;

    unsigned stride = vb->GetVertexSize();
    unsigned posOffset = vb->GetElementOffset(SEM_POSITION);
    *reinterpret_cast<Vector3*>(data + (unsigned)selectedVertex_ * stride + posOffset) = newLocalPos;

    vb->SetData(data);
}

void ModelTool::SaveModel()
{
    if (!editModel_)
    {
        if (statusText_) statusText_->SetText("ERROR: No edited model to save");
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    const Vector<String>& dirs = cache->GetResourceDirs();

    String absolutePath;
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        String candidate = dirs[i] + currentModelPath_;
        if (fs->FileExists(candidate))
        {
            absolutePath = candidate;
            break;
        }
    }

    if (absolutePath.Empty())
    {
        if (statusText_) statusText_->SetText("ERROR: Cannot resolve path for " + currentModelPath_);
        return;
    }

    File file(context_, absolutePath, FILE_WRITE);
    if (file.IsOpen())
    {
        editModel_->Save(file);
        vertexDirty_ = false;
        if (statusText_) statusText_->SetText("SAVED: " + absolutePath);
        URHO3D_LOGINFOF("Saved edited model to %s", absolutePath.CString());
    }
    else
    {
        if (statusText_) statusText_->SetText("ERROR: Cannot write " + absolutePath);
    }
}

void ModelTool::ExportFBX()
{
    if (currentModelPath_.Empty())
    {
        if (statusText_) statusText_->SetText("ERROR: No model loaded to export");
        return;
    }

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();
    auto* fs = GetSubsystem<FileSystem>();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Export FBX Package (.zip)");
    fileSelector_->SetButtonTexts("Export", "Cancel");

    Vector<String> filters;
    filters.Push("*.zip");
    fileSelector_->SetFilters(filters, 0);

    // Default to same directory as the model
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        String candidate = dirs[i] + currentModelPath_;
        if (fs->FileExists(candidate))
        {
            fileSelector_->SetPath(GetPath(candidate));
            String baseName = GetFileName(currentModelPath_);
            fileSelector_->SetFileName(baseName + ".zip");
            break;
        }
    }

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(ModelTool, HandleExportFBXSelected));
}

void ModelTool::HandleExportFBXSelected(StringHash, VariantMap& eventData)
{
    String zipPath = eventData[FileSelected::P_FILENAME].GetString();
    bool ok = eventData[FileSelected::P_OK].GetBool();
    fileSelector_.Reset();
    if (!ok || zipPath.Empty()) return;

    if (!zipPath.EndsWith(".zip", false))
        zipPath += ".zip";

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    const Vector<String>& dirs = cache->GetResourceDirs();

    // Resolve the absolute path to the source .mdl
    String mdlAbsPath;
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        String candidate = dirs[i] + currentModelPath_;
        if (fs->FileExists(candidate))
        {
            mdlAbsPath = candidate;
            break;
        }
    }
    if (mdlAbsPath.Empty())
    {
        if (statusText_) statusText_->SetText("ERROR: Cannot resolve " + currentModelPath_);
        return;
    }

    // Find AssetTool
    String assetImporter = fs->GetProgramDir() + "tool/AssetTool";
    if (!fs->FileExists(assetImporter))
        assetImporter = fs->GetProgramDir() + "AssetTool";
    if (!fs->FileExists(assetImporter))
    {
        if (statusText_) statusText_->SetText("ERROR: AssetTool not found");
        URHO3D_LOGERROR("AssetTool not found in program dir or tool/ subdirectory");
        return;
    }

    if (statusText_) statusText_->SetText("Exporting FBX package...");

    String modelBaseName = GetFileName(currentModelPath_);

    // Create staging directory
    String stagingBase = fs->GetTemporaryDir() + "urho3d_fbx_export/";
    String stagingDir = stagingBase + modelBaseName + "/";
    // Clean any prior export staging
    fs->SystemCommand("rm -rf \"" + stagingBase + "\"");
    fs->CreateDir(stagingBase);
    fs->CreateDir(stagingDir);

    // Export FBX into staging dir
    String fbxPath = stagingDir + modelBaseName + ".fbx";
    String cmd = "\"" + assetImporter + "\" export \"" + mdlAbsPath + "\" \"" + fbxPath + "\"";

    // Include animations loaded in ModelTool
    for (unsigned i = 0; i < availableAnims_.Size(); ++i)
    {
        for (unsigned d = 0; d < dirs.Size(); ++d)
        {
            String animCandidate = dirs[d] + availableAnims_[i];
            if (fs->FileExists(animCandidate))
            {
                cmd += " -anim \"" + animCandidate + "\"";
                break;
            }
        }
    }

    URHO3D_LOGINFOF("Export FBX: %s", cmd.CString());
    int result = system(cmd.CString());
    if (result != 0)
    {
        if (statusText_) statusText_->SetText("ERROR: FBX export failed (code " + String(result) + ")");
        URHO3D_LOGERRORF("FBX export failed with code %d", result);
        return;
    }

    // Gather textures from loaded materials
    StaticModel* drawable = animatedModelComp_ ? (StaticModel*)animatedModelComp_ : staticModelComp_;
    HashSet<String> gatheredTextures;
    unsigned texturesCopied = 0;

    if (drawable)
    {
        for (unsigned i = 0; i < drawable->GetNumGeometries(); ++i)
        {
            Material* mat = drawable->GetMaterial(i);
            if (!mat)
                continue;

            for (int unit = 0; unit < MAX_TEXTURE_UNITS; ++unit)
            {
                Texture* tex = mat->GetTexture((TextureUnit)unit);
                if (!tex)
                    continue;

                String texName = tex->GetName();
                if (texName.Empty() || gatheredTextures.Contains(texName))
                    continue;
                gatheredTextures.Insert(texName);

                // Resolve to absolute path on disk
                String texAbsPath;
                for (unsigned d = 0; d < dirs.Size(); ++d)
                {
                    String candidate = dirs[d] + texName;
                    if (fs->FileExists(candidate))
                    {
                        texAbsPath = candidate;
                        break;
                    }
                }
                if (texAbsPath.Empty())
                {
                    URHO3D_LOGWARNINGF("Texture not found on disk: %s", texName.CString());
                    continue;
                }

                // Copy to staging, preserving relative path structure
                String destPath = stagingDir + texName;
                String destDir = GetPath(destPath);
                fs->CreateDir(destDir);
                fs->Copy(texAbsPath, destPath);
                ++texturesCopied;
                URHO3D_LOGINFOF("  Packed texture: %s", texName.CString());
            }
        }
    }

    // Create zip archive
    // Remove existing zip first
    if (fs->FileExists(zipPath))
        fs->SystemCommand("rm -f \"" + zipPath + "\"");

    String zipCmd = "cd \"" + stagingBase + "\" && zip -r \"" + zipPath + "\" \"" + modelBaseName + "/\"";
    URHO3D_LOGINFOF("Creating zip: %s", zipCmd.CString());
    int zipResult = fs->SystemCommand(zipCmd);

    // Clean up staging
    fs->SystemCommand("rm -rf \"" + stagingBase + "\"");

    if (zipResult == 0 && fs->FileExists(zipPath))
    {
        String msg = "EXPORTED: " + zipPath + " (" + String(texturesCopied) + " textures)";
        if (statusText_) statusText_->SetText(msg);
        URHO3D_LOGINFOF("FBX package exported: %s (%u textures)", zipPath.CString(), texturesCopied);
    }
    else
    {
        if (statusText_) statusText_->SetText("ERROR: Failed to create zip archive");
        URHO3D_LOGERRORF("zip command failed with code %d", zipResult);
    }
}

Vector3 ModelTool::GetVertexWorldPosition(int geomIndex, int vertIndex)
{
    if (!editModel_ || geomIndex < 0 || vertIndex < 0) return Vector3::ZERO;

    Geometry* geom = editModel_->GetGeometry((unsigned)geomIndex, 0);
    if (!geom) return Vector3::ZERO;

    VertexBuffer* vb = geom->GetVertexBuffer(0);
    if (!vb) return Vector3::ZERO;

    const auto* data = vb->GetShadowData();
    if (!data) return Vector3::ZERO;

    unsigned stride = vb->GetVertexSize();
    unsigned posOffset = vb->GetElementOffset(SEM_POSITION);

    bool hasSkinning = vb->HasElement(SEM_BLENDWEIGHTS) && vb->HasElement(SEM_BLENDINDICES);
    unsigned weightOffset = hasSkinning ? vb->GetElementOffset(SEM_BLENDWEIGHTS) : 0;
    unsigned indexOffset = hasSkinning ? vb->GetElementOffset(SEM_BLENDINDICES) : 0;

    const Vector<Bone>* bonesPtr = nullptr;
    if (isAnimated_ && animatedModelComp_)
        bonesPtr = &animatedModelComp_->GetSkeleton().GetBones();

    const Vector<Bone> emptyBones;
    const Vector<Bone>& bones = bonesPtr ? *bonesPtr : emptyBones;

    return SkinVertex(reinterpret_cast<const unsigned char*>(data), stride, posOffset, weightOffset, indexOffset,
        hasSkinning && bonesPtr, (unsigned)vertIndex, bones, modelNode_->GetWorldTransform());
}

void ModelTool::UpdateVertexStatusText()
{
    if (!statusText_) return;

    if (selectedVertex_ >= 0 && selectedGeometry_ >= 0)
    {
        Vector3 pos = GetVertexWorldPosition(selectedGeometry_, selectedVertex_);
        char buf[128];
        snprintf(buf, sizeof(buf), "VERTEX EDIT — Geom %d Vert %d (%.3f, %.3f, %.3f)",
                 selectedGeometry_, selectedVertex_, pos.x_, pos.y_, pos.z_);

        // Count total verts
        unsigned totalVerts = 0;
        if (editModel_)
        {
            for (unsigned g = 0; g < editModel_->GetNumGeometries(); ++g)
            {
                Geometry* gm = editModel_->GetGeometry(g, 0);
                if (gm && gm->GetVertexBuffer(0))
                    totalVerts += gm->GetVertexBuffer(0)->GetVertexCount();
            }
        }

        String s(buf);
        s += " | Total: " + String(totalVerts) + " verts";
        if (vertexDirty_) s += " [MODIFIED]";
        statusText_->SetText(s);
    }
    else
    {
        String s = "VERTEX EDIT — Click to select";
        if (vertexDirty_) s += " [MODIFIED]";
        statusText_->SetText(s);
    }
}

// ============================================================================
// Helpers
// ============================================================================

String ModelTool::SemanticName(VertexElementSemantic sem)
{
    switch (sem)
    {
    case SEM_POSITION: return "POSITION";
    case SEM_NORMAL: return "NORMAL";
    case SEM_BINORMAL: return "BINORMAL";
    case SEM_TANGENT: return "TANGENT";
    case SEM_TEXCOORD: return "TEXCOORD";
    case SEM_COLOR: return "COLOR";
    case SEM_BLENDWEIGHTS: return "BLENDWEIGHTS";
    case SEM_BLENDINDICES: return "BLENDINDICES";
    case SEM_OBJECTINDEX: return "OBJECTINDEX";
    default: return "UNKNOWN";
    }
}

String ModelTool::TypeName(VertexElementType type)
{
    switch (type)
    {
    case TYPE_INT: return "Int";
    case TYPE_FLOAT: return "Float";
    case TYPE_VECTOR2: return "Vec2";
    case TYPE_VECTOR3: return "Vec3";
    case TYPE_VECTOR4: return "Vec4";
    case TYPE_UBYTE4: return "UB4";
    case TYPE_UBYTE4_NORM: return "UB4N";
    default: return "?";
    }
}

// ============================================================================
// Folder Browse
// ============================================================================

void ModelTool::ScanFolder(const String& folderPath)
{
    auto* fs = GetSubsystem<FileSystem>();

    String absFolder = folderPath;
    if (!absFolder.EndsWith("/"))
        absFolder += "/";

    // Resolve to absolute path if relative
    if (!absFolder.StartsWith("/"))
        absFolder = fs->GetCurrentDir() + absFolder;

    if (!fs->DirExists(absFolder))
    {
        URHO3D_LOGERRORF("Folder does not exist: %s", absFolder.CString());
        if (statusText_) statusText_->SetText("ERROR: Folder not found: " + absFolder);
        return;
    }

    browseFolderPath_ = absFolder;
    folderModels_.Clear();
    folderReview_.Clear();

    // Auto-detect import DMZ
    importMode_ = absFolder.Contains("_import_review");
    if (importMode_)
        importDMZPath_ = absFolder;

    Vector<String> files;
    fs->ScanDir(files, absFolder, "*.mdl", SCAN_FILES, false);
    Sort(files.Begin(), files.End());

    // Convert to resource-relative paths
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();

    for (unsigned i = 0; i < files.Size(); ++i)
    {
        String absPath = absFolder + files[i];
        String resourcePath;

        // Try to make resource-relative using unresolved paths
        // (symlinks inside resource dirs must not be resolved away)
        for (unsigned d = 0; d < dirs.Size(); ++d)
        {
            if (absPath.StartsWith(dirs[d]))
            {
                resourcePath = absPath.Substring(dirs[d].Length());
                break;
            }
        }

        if (!resourcePath.Empty())
            folderModels_.Push(resourcePath);
        else
            folderModels_.Push(absPath);  // outside resource dirs — use absolute path
    }

    if (folderModels_.Empty())
    {
        if (statusText_) statusText_->SetText("No .mdl files found in " + absFolder);
        return;
    }

    URHO3D_LOGINFOF("Folder browse: %d models in %s", folderModels_.Size(), absFolder.CString());
    browseIndex_ = 0;
    LoadModel(folderModels_[browseIndex_]);

    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + GetFileName(folderModels_[browseIndex_]));

    if (statusText_)
        statusText_->SetText("Left/Right: browse   K: promote to bin/Data   G: delete");
}

void ModelTool::BrowseNext()
{
    if (folderModels_.Empty()) return;
    browseIndex_ = (browseIndex_ + 1) % folderModels_.Size();
    LoadModel(folderModels_[browseIndex_]);

    String flag;
    String fname = GetFileName(folderModels_[browseIndex_]);
    if (folderReview_.Contains(fname))
        flag = " [" + folderReview_[fname] + "]";

    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + fname + flag);
}

void ModelTool::BrowsePrev()
{
    if (folderModels_.Empty()) return;
    browseIndex_ = (browseIndex_ == 0) ? folderModels_.Size() - 1 : browseIndex_ - 1;
    LoadModel(folderModels_[browseIndex_]);

    String flag;
    String fname = GetFileName(folderModels_[browseIndex_]);
    if (folderReview_.Contains(fname))
        flag = " [" + folderReview_[fname] + "]";

    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + fname + flag);
}

void ModelTool::BrowseFlagKeep()
{
    if (folderModels_.Empty()) return;
    String fname = GetFileName(folderModels_[browseIndex_]);
    folderReview_[fname] = "keep";
    WriteFolderReview();

    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + fname + " [keep]");

    // Notify IPC caller
    if (!ipcReplyTo_.Empty())
        SendIPCResponse(ipcReplyTo_, "KEEP " + folderModels_[browseIndex_]);

    // Auto-advance to next model
    BrowseNext();
}

void ModelTool::BrowseFlagReject()
{
    if (folderModels_.Empty()) return;
    String fname = GetFileName(folderModels_[browseIndex_]);
    folderReview_[fname] = "reject";
    WriteFolderReview();

    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + fname + " [reject]");

    // Notify IPC caller
    if (!ipcReplyTo_.Empty())
        SendIPCResponse(ipcReplyTo_, "REJECT " + folderModels_[browseIndex_]);

    // Auto-advance to next model
    BrowseNext();
}

void ModelTool::WriteFolderReview()
{
    if (browseFolderPath_.Empty()) return;

    String reviewPath = browseFolderPath_ + "folder_review.txt";
    File file(context_, reviewPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERRORF("Cannot write review file: %s", reviewPath.CString());
        return;
    }

    for (HashMap<String, String>::ConstIterator it = folderReview_.Begin(); it != folderReview_.End(); ++it)
    {
        file.WriteString(it->first_ + " | " + it->second_ + "\n");
    }
}

// ============================================================================
// Import — FBX/OBJ/etc → DMZ staging → review → promote or delete
// ============================================================================

void ModelTool::ShowImportDialog()
{
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Import Model (FBX, OBJ, glTF, DAE, Blend)");
    fileSelector_->SetButtonTexts("Import", "Cancel");

    Vector<String> filters;
    filters.Push("*.fbx");
    filters.Push("*.obj");
    filters.Push("*.gltf");
    filters.Push("*.glb");
    filters.Push("*.dae");
    filters.Push("*.blend");
    fileSelector_->SetFilters(filters, 0);

    // Start in brainfarts if it exists
    auto* fs = GetSubsystem<FileSystem>();
    String brainfarts = fs->GetCurrentDir() + "brainfarts/";
    // Walk up from cwd to find brainfarts
    if (!fs->DirExists(brainfarts))
    {
        String dir = fs->GetCurrentDir();
        for (int i = 0; i < 5; ++i)
        {
            dir = GetParentPath(dir);
            if (fs->DirExists(dir + "brainfarts/"))
            {
                brainfarts = dir + "brainfarts/";
                break;
            }
        }
    }
    if (fs->DirExists(brainfarts))
        fileSelector_->SetPath(brainfarts);

    SubscribeToEvent(fileSelector_, E_FILESELECTED, [this](StringHash, VariantMap& ed)
    {
        String path = ed[FileSelected::P_FILENAME].GetString();
        bool ok = ed[FileSelected::P_OK].GetBool();
        fileSelector_.Reset();
        if (!ok || path.Empty()) return;

        // Probe the file first, show selective import dialog
        FBXProbeInfo info;
        if (ProbeFBX(path, info))
            ShowSelectiveImportDialog(path, info);
        else
        {
            // Probe failed — fall back to direct import
            if (statusText_) statusText_->SetText("Probe failed — importing all");
            ImportModel(path);
        }
    });
}

void ModelTool::ImportModel(const String& fbxPath)
{
    auto* fs = GetSubsystem<FileSystem>();

    // Find project root (parent of bin/)
    String projectRoot;
    String dir = fs->GetProgramDir();
    for (int i = 0; i < 5; ++i)
    {
        if (fs->DirExists(dir + "brainfarts/"))
        {
            projectRoot = dir;
            break;
        }
        dir = GetParentPath(dir);
    }
    if (projectRoot.Empty())
    {
        // Fallback: use parent of source file
        projectRoot = GetPath(GetPath(fbxPath));
    }

    // Create DMZ directory
    String dmzPath = projectRoot + "brainfarts/_import_review/";
    if (!fs->DirExists(dmzPath))
        fs->CreateDir(dmzPath);

    // Find AssetTool binary (sibling tool/ directory)
    String assetTool = fs->GetProgramDir() + "tool/AssetTool";
    if (!fs->FileExists(assetTool))
    {
        assetTool = fs->GetProgramDir() + "AssetTool";
        if (!fs->FileExists(assetTool))
        {
            URHO3D_LOGERROR("AssetTool not found — cannot import");
            if (statusText_) statusText_->SetText("ERROR: AssetTool not found");
            return;
        }
    }

    // Copy source file into DMZ
    String srcName = GetFileNameAndExtension(fbxPath);
    String dmzSrc = dmzPath + srcName;
    fs->Copy(fbxPath, dmzSrc);

    // Run AssetTool to convert
    String mdlName = GetFileName(fbxPath) + ".mdl";
    String mdlPath = dmzPath + mdlName;
    String cmd = "\"" + assetTool + "\" model \"" + dmzSrc + "\" \"" + mdlPath + "\" -l -t";

    URHO3D_LOGINFOF("Import: %s", cmd.CString());
    int result = fs->SystemCommand(cmd);
    if (result != 0)
    {
        URHO3D_LOGERRORF("AssetTool failed with exit code %d", result);
        if (statusText_) statusText_->SetText("ERROR: Import failed (exit " + String(result) + ")");
        return;
    }

    if (!fs->FileExists(mdlPath))
    {
        URHO3D_LOGERROR("Import produced no output file");
        if (statusText_) statusText_->SetText("ERROR: No .mdl produced");
        return;
    }

    // Enter import mode
    importMode_ = true;
    importDMZPath_ = dmzPath;

    // Load and scan
    LoadModel(mdlPath);
    ScanFolder(dmzPath);

    if (statusText_)
        statusText_->SetText("IMPORT MODE — K: promote to bin/Data   G: delete   Left/Right: browse");
}

void ModelTool::PromoteCurrentModel()
{
    if (folderModels_.Empty() || !importMode_) return;

    auto* fs = GetSubsystem<FileSystem>();
    String mdlPath = folderModels_[browseIndex_];
    String mdlName = GetFileNameAndExtension(mdlPath);
    String baseName = GetFileName(mdlPath);

    // Find bin/Data/Models/ destination
    String projectRoot;
    String dir = fs->GetProgramDir();
    for (int i = 0; i < 5; ++i)
    {
        if (fs->DirExists(dir + "bin/Data/"))
        {
            projectRoot = dir;
            break;
        }
        dir = GetParentPath(dir);
    }
    if (projectRoot.Empty())
    {
        URHO3D_LOGERROR("Cannot find bin/Data/ for promotion");
        if (statusText_) statusText_->SetText("ERROR: bin/Data/ not found");
        return;
    }

    String destModels = projectRoot + "bin/Data/Models/";
    if (!fs->DirExists(destModels))
        fs->CreateDir(destModels);

    // Move .mdl
    fs->Copy(mdlPath, destModels + mdlName);
    // Move .txt (material list) if exists
    String txtPath = GetPath(mdlPath) + baseName + ".txt";
    if (fs->FileExists(txtPath))
        fs->Copy(txtPath, destModels + baseName + ".txt");

    // Move materials
    String srcMats = GetPath(mdlPath) + "Materials/";
    String destMats = projectRoot + "bin/Data/Materials/";
    if (fs->DirExists(srcMats))
    {
        if (!fs->DirExists(destMats))
            fs->CreateDir(destMats);
        Vector<String> matFiles;
        fs->ScanDir(matFiles, srcMats, "*.xml", SCAN_FILES, false);
        for (unsigned i = 0; i < matFiles.Size(); ++i)
            fs->Copy(srcMats + matFiles[i], destMats + matFiles[i]);
    }

    // Move textures
    String srcTexs = GetPath(mdlPath) + "Textures/";
    String destTexs = projectRoot + "bin/Data/Textures/";
    if (fs->DirExists(srcTexs))
    {
        if (!fs->DirExists(destTexs))
            fs->CreateDir(destTexs);
        Vector<String> texFiles;
        fs->ScanDir(texFiles, srcTexs, "*.*", SCAN_FILES, false);
        for (unsigned i = 0; i < texFiles.Size(); ++i)
            fs->Copy(srcTexs + texFiles[i], destTexs + texFiles[i]);
    }

    URHO3D_LOGINFOF("Promoted: %s → bin/Data/", mdlName.CString());

    // Register in resource tracking DB
    RegisterModelWithDependencies("Models/" + mdlName);

    // Remove from browse list and delete from DMZ
    fs->Delete(mdlPath);
    if (fs->FileExists(txtPath))
        fs->Delete(txtPath);
    // Delete the source FBX/OBJ from DMZ
    String srcName = baseName;
    Vector<String> dmzFiles;
    fs->ScanDir(dmzFiles, importDMZPath_, "*.*", SCAN_FILES, false);
    for (unsigned i = 0; i < dmzFiles.Size(); ++i)
    {
        if (GetFileName(dmzFiles[i]) == baseName && !dmzFiles[i].EndsWith(".mdl") && !dmzFiles[i].EndsWith(".txt"))
            fs->Delete(importDMZPath_ + dmzFiles[i]);
    }

    folderModels_.Erase(browseIndex_);
    if (folderModels_.Empty())
    {
        importMode_ = false;
        if (statusText_) statusText_->SetText("All imports reviewed");
        return;
    }
    if (browseIndex_ >= folderModels_.Size())
        browseIndex_ = 0;
    LoadModel(folderModels_[browseIndex_]);

    String fname = GetFileName(folderModels_[browseIndex_]);
    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + fname + " [promoted prev]");
    if (statusText_)
        statusText_->SetText("PROMOTED " + mdlName + " — K: promote   G: delete   Left/Right: browse");
}

void ModelTool::DeleteCurrentModel()
{
    if (folderModels_.Empty()) return;

    auto* fs = GetSubsystem<FileSystem>();
    String mdlPath = folderModels_[browseIndex_];
    String mdlName = GetFileNameAndExtension(mdlPath);
    String baseName = GetFileName(mdlPath);

    // Determine if this is a DMZ/brainfarts model or a bin/Data model
    bool isDMZ = mdlPath.Contains("brainfarts") || mdlPath.Contains("_import_review");
    bool isBinData = mdlPath.Contains("bin/Data") || mdlPath.StartsWith("Models/");

    if (isBinData)
    {
        // Use dependency tracker — safely deletes orphaned materials/textures
        String resourcePath = mdlPath;
        // Normalize to resource-relative path if absolute
        if (resourcePath.Contains("bin/Data/"))
            resourcePath = resourcePath.Substring(resourcePath.Find("bin/Data/") + 9);
        UnregisterModelWithOrphans(resourcePath);
    }
    else
    {
        // DMZ or brainfarts — nuke everything, no dependency tracking needed
        if (fs->FileExists(mdlPath))
            fs->Delete(mdlPath);
        String txtPath = GetPath(mdlPath) + baseName + ".txt";
        if (fs->FileExists(txtPath))
            fs->Delete(txtPath);
        // Delete source FBX/OBJ if in import DMZ
        if (!importDMZPath_.Empty())
        {
            Vector<String> dmzFiles;
            fs->ScanDir(dmzFiles, importDMZPath_, "*.*", SCAN_FILES, false);
            for (unsigned i = 0; i < dmzFiles.Size(); ++i)
            {
                if (GetFileName(dmzFiles[i]) == baseName && !dmzFiles[i].EndsWith(".mdl") && !dmzFiles[i].EndsWith(".txt"))
                    fs->Delete(importDMZPath_ + dmzFiles[i]);
            }
        }
    }

    URHO3D_LOGINFOF("Deleted: %s%s", mdlName.CString(), isBinData ? " (with orphan cleanup)" : "");

    folderModels_.Erase(browseIndex_);
    if (folderModels_.Empty())
    {
        importMode_ = false;
        if (statusText_) statusText_->SetText("All models reviewed");
        return;
    }
    if (browseIndex_ >= folderModels_.Size())
        browseIndex_ = 0;
    LoadModel(folderModels_[browseIndex_]);

    String fname = GetFileName(folderModels_[browseIndex_]);
    if (browseStatusText_)
        browseStatusText_->SetText(String(browseIndex_ + 1) + "/" + String(folderModels_.Size()) + ": " + fname);
    if (statusText_)
        statusText_->SetText("DELETED " + mdlName + " — K: promote   G: delete   Left/Right: browse");
}

// ============================================================================
// Resource Tracking DB — SQLite dependency graph for models/materials/textures
// ============================================================================

void ModelTool::InitResourceDB()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (resourceDB_) return;

    auto* fs = GetSubsystem<FileSystem>();
    // Find bin/Data/
    String dbPath;
    String dir = fs->GetProgramDir();
    for (int i = 0; i < 5; ++i)
    {
        if (fs->DirExists(dir + "bin/Data/"))
        {
            dbPath = dir + "bin/Data/resources.db";
            break;
        }
        dir = GetParentPath(dir);
    }
    if (dbPath.Empty())
    {
        dbPath = fs->GetProgramDir() + "resources.db";
    }

    int rc = sqlite3_open(dbPath.CString(), &resourceDB_);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERRORF("ResourceDB: failed to open %s: %s", dbPath.CString(), sqlite3_errmsg(resourceDB_));
        resourceDB_ = nullptr;
        return;
    }

    sqlite3_exec(resourceDB_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(resourceDB_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    const char* schema =
        "CREATE TABLE IF NOT EXISTS resources ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT UNIQUE NOT NULL,"
        "  type TEXT NOT NULL"  // model, material, texture, animation, sound
        ");"
        "CREATE TABLE IF NOT EXISTS dependencies ("
        "  parent_id INTEGER NOT NULL,"
        "  child_id INTEGER NOT NULL,"
        "  PRIMARY KEY (parent_id, child_id),"
        "  FOREIGN KEY (parent_id) REFERENCES resources(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (child_id) REFERENCES resources(id)"
        ");";

    char* errMsg = nullptr;
    rc = sqlite3_exec(resourceDB_, schema, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERRORF("ResourceDB: schema error: %s", errMsg);
        sqlite3_free(errMsg);
    }
    else
    {
        URHO3D_LOGINFOF("ResourceDB: opened %s", dbPath.CString());
    }
#endif
}

void ModelTool::CloseResourceDB()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (resourceDB_)
    {
        sqlite3_close(resourceDB_);
        resourceDB_ = nullptr;
    }
#endif
}

int ModelTool::RegisterResource(const String& path, const String& type)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!resourceDB_) return -1;

    // Try insert, ignore if exists
    const char* insertSQL = "INSERT OR IGNORE INTO resources (path, type) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(resourceDB_, insertSQL, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type.CString(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return GetResourceId(path);
#else
    return -1;
#endif
}

void ModelTool::AddDependency(int parentId, int childId)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!resourceDB_ || parentId < 0 || childId < 0) return;

    const char* sql = "INSERT OR IGNORE INTO dependencies (parent_id, child_id) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(resourceDB_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_int(stmt, 2, childId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
#endif
}

int ModelTool::GetResourceId(const String& path)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!resourceDB_) return -1;

    const char* sql = "SELECT id FROM resources WHERE path = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(resourceDB_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.CString(), -1, SQLITE_TRANSIENT);
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return id;
#else
    return -1;
#endif
}

Vector<String> ModelTool::GetDependents(const String& path)
{
    Vector<String> result;
#ifdef URHO3D_DATABASE_SQLITE
    if (!resourceDB_) return result;

    // Find all resources that depend on this one (parents that reference this child)
    const char* sql =
        "SELECT r.path FROM resources r "
        "JOIN dependencies d ON d.parent_id = r.id "
        "WHERE d.child_id = (SELECT id FROM resources WHERE path = ?);";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(resourceDB_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, path.CString(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.Push(String((const char*)sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
#endif
    return result;
}

void ModelTool::RegisterModelWithDependencies(const String& mdlPath)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!resourceDB_) InitResourceDB();
    if (!resourceDB_) return;

    int modelId = RegisterResource(mdlPath, "model");

    // Read .txt material list
    String basePath = mdlPath.Substring(0, mdlPath.Length() - 4);  // strip .mdl
    String txtPath = basePath + ".txt";

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();

    // Find absolute path to the txt file
    String absTxt;
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (fs->FileExists(dirs[i] + txtPath))
        {
            absTxt = dirs[i] + txtPath;
            break;
        }
    }
    // Also try absolute path directly
    if (absTxt.Empty() && fs->FileExists(txtPath))
        absTxt = txtPath;

    if (absTxt.Empty()) return;

    // Read material references from .txt
    File txtFile(context_, absTxt, FILE_READ);
    if (!txtFile.IsOpen()) return;

    while (!txtFile.IsEof())
    {
        String matPath = txtFile.ReadLine().Trimmed();
        if (matPath.Empty()) continue;

        int matId = RegisterResource(matPath, "material");
        AddDependency(modelId, matId);

        // Parse material XML for texture references
        String absMatPath;
        for (unsigned i = 0; i < dirs.Size(); ++i)
        {
            if (fs->FileExists(dirs[i] + matPath))
            {
                absMatPath = dirs[i] + matPath;
                break;
            }
        }
        if (absMatPath.Empty()) continue;

        XMLFile matXml(context_);
        File matFile(context_, absMatPath, FILE_READ);
        if (!matFile.IsOpen() || !matXml.Load(matFile)) continue;

        XMLElement root = matXml.GetRoot();
        XMLElement texElem = root.GetChild("texture");
        while (texElem)
        {
            String texName = texElem.GetAttribute("name");
            if (!texName.Empty())
            {
                int texId = RegisterResource(texName, "texture");
                AddDependency(matId, texId);
            }
            texElem = texElem.GetNext("texture");
        }
    }

    URHO3D_LOGINFOF("ResourceDB: registered %s with dependencies", mdlPath.CString());
#endif
}

void ModelTool::UnregisterModelWithOrphans(const String& mdlPath)
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!resourceDB_) InitResourceDB();
    if (!resourceDB_) return;

    auto* fs = GetSubsystem<FileSystem>();
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();

    int modelId = GetResourceId(mdlPath);
    if (modelId < 0) return;

    // Get materials this model depends on
    const char* childSQL =
        "SELECT r.id, r.path, r.type FROM resources r "
        "JOIN dependencies d ON d.child_id = r.id "
        "WHERE d.parent_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(resourceDB_, childSQL, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, modelId);

    struct Child { int id; String path; String type; };
    Vector<Child> children;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        Child c;
        c.id = sqlite3_column_int(stmt, 0);
        c.path = String((const char*)sqlite3_column_text(stmt, 1));
        c.type = String((const char*)sqlite3_column_text(stmt, 2));
        children.Push(c);
    }
    sqlite3_finalize(stmt);

    // Delete model's dependency edges
    const char* delDepSQL = "DELETE FROM dependencies WHERE parent_id = ?;";
    sqlite3_prepare_v2(resourceDB_, delDepSQL, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, modelId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Delete model resource record
    const char* delResSQL = "DELETE FROM resources WHERE id = ?;";
    sqlite3_prepare_v2(resourceDB_, delResSQL, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, modelId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Delete model file
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        String absPath = dirs[i] + mdlPath;
        if (fs->FileExists(absPath))
        {
            fs->Delete(absPath);
            URHO3D_LOGINFOF("ResourceDB: deleted model %s", absPath.CString());
            break;
        }
    }
    // Delete .txt
    String txtPath = mdlPath.Substring(0, mdlPath.Length() - 4) + ".txt";
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        String absPath = dirs[i] + txtPath;
        if (fs->FileExists(absPath))
        {
            fs->Delete(absPath);
            break;
        }
    }

    // Check each child — if orphaned (no other parents), delete it and its children
    for (unsigned i = 0; i < children.Size(); ++i)
    {
        // Count remaining parents
        const char* countSQL = "SELECT COUNT(*) FROM dependencies WHERE child_id = ?;";
        sqlite3_prepare_v2(resourceDB_, countSQL, -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, children[i].id);
        int parentCount = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            parentCount = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (parentCount == 0)
        {
            // Orphaned — if it's a material, check its texture children too
            if (children[i].type == "material")
            {
                // Get texture children of this material
                sqlite3_prepare_v2(resourceDB_, childSQL, -1, &stmt, nullptr);
                sqlite3_bind_int(stmt, 1, children[i].id);
                Vector<Child> texChildren;
                while (sqlite3_step(stmt) == SQLITE_ROW)
                {
                    Child tc;
                    tc.id = sqlite3_column_int(stmt, 0);
                    tc.path = String((const char*)sqlite3_column_text(stmt, 1));
                    tc.type = String((const char*)sqlite3_column_text(stmt, 2));
                    texChildren.Push(tc);
                }
                sqlite3_finalize(stmt);

                // Delete material's dependency edges
                sqlite3_prepare_v2(resourceDB_, delDepSQL, -1, &stmt, nullptr);
                sqlite3_bind_int(stmt, 1, children[i].id);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);

                // Check each texture
                for (unsigned t = 0; t < texChildren.Size(); ++t)
                {
                    sqlite3_prepare_v2(resourceDB_, countSQL, -1, &stmt, nullptr);
                    sqlite3_bind_int(stmt, 1, texChildren[t].id);
                    int texParents = 0;
                    if (sqlite3_step(stmt) == SQLITE_ROW)
                        texParents = sqlite3_column_int(stmt, 0);
                    sqlite3_finalize(stmt);

                    if (texParents == 0)
                    {
                        // Delete orphaned texture file
                        for (unsigned d = 0; d < dirs.Size(); ++d)
                        {
                            String absPath = dirs[d] + texChildren[t].path;
                            if (fs->FileExists(absPath))
                            {
                                fs->Delete(absPath);
                                URHO3D_LOGINFOF("ResourceDB: deleted orphaned texture %s", absPath.CString());
                                break;
                            }
                        }
                        // Delete texture resource record
                        sqlite3_prepare_v2(resourceDB_, delResSQL, -1, &stmt, nullptr);
                        sqlite3_bind_int(stmt, 1, texChildren[t].id);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
            }

            // Delete orphaned material/resource file
            for (unsigned d = 0; d < dirs.Size(); ++d)
            {
                String absPath = dirs[d] + children[i].path;
                if (fs->FileExists(absPath))
                {
                    fs->Delete(absPath);
                    URHO3D_LOGINFOF("ResourceDB: deleted orphaned %s %s", children[i].type.CString(), absPath.CString());
                    break;
                }
            }
            // Delete resource record
            sqlite3_prepare_v2(resourceDB_, delResSQL, -1, &stmt, nullptr);
            sqlite3_bind_int(stmt, 1, children[i].id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        else
        {
            URHO3D_LOGINFOF("ResourceDB: keeping shared %s %s (%d other dependents)",
                children[i].type.CString(), children[i].path.CString(), parentCount);
        }
    }
#endif
}

// ============================================================================
// Slot B — second model in the scene
// ============================================================================

void ModelTool::LoadReferenceModel()
{
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Load Slot B Model");
    fileSelector_->SetButtonTexts("Open", "Cancel");

    Vector<String> filters;
    filters.Push("*.mdl");
    fileSelector_->SetFilters(filters, 0);

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (fs->DirExists(dirs[i] + "Models/"))
        {
            fileSelector_->SetPath(dirs[i] + "Models/");
            if (dirs[i].Contains("/Data/") || dirs[i].EndsWith("/Data"))
                break;
        }
    }

    SubscribeToEvent(fileSelector_, E_FILESELECTED,
        URHO3D_HANDLER(ModelTool, HandleRefModelSelected));
}

void ModelTool::HandleRefModelSelected(StringHash, VariantMap& eventData)
{
    String path = eventData[FileSelected::P_FILENAME].GetString();
    bool ok = eventData[FileSelected::P_OK].GetBool();
    fileSelector_.Reset();
    if (!ok || path.Empty())
        return;

    // Clean up existing slot B
    RemoveReferenceModel();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();

    if (IsAbsolutePath(path) && fs->FileExists(path))
    {
        String dir = GetPath(path);
        if (!dir.Empty())
            cache->AddResourceDir(dir, 0);
    }

    String loadPath = path;
    if (IsAbsolutePath(path))
        loadPath = GetFileNameAndExtension(path);

    auto* mdl = cache->GetResource<Model>(loadPath);
    if (!mdl)
    {
        URHO3D_LOGERRORF("Slot B: failed to load '%s'", path.CString());
        return;
    }

    refModel_ = mdl;
    refModelPath_ = loadPath;

    // Position to the right of slot A
    float offsetX = 3.0f;
    if (currentModel_ && modelNode_)
    {
        BoundingBox bbox = currentModel_->GetBoundingBox();
        float width = (bbox.max_.x_ - bbox.min_.x_) * modelNode_->GetScale().x_;
        offsetX = width + 1.0f;
    }
    refModelNode_->SetPosition(Vector3(offsetX, 0.0f, 0.0f));

    // Remove old components on refModelNode_
    refModelNode_->RemoveAllComponents();
    refModelNode_->RemoveAllChildren();

    Skeleton& skel = mdl->GetSkeleton();
    if (skel.GetNumBones() > 1)
    {
        refIsAnimated_ = true;
        refAnimatedModelComp_ = refModelNode_->CreateComponent<AnimatedModel>();
        refAnimatedModelComp_->SetModel(mdl, true, true);
        refAnimatedModelComp_->SetCastShadows(true);

        // Materials
        String matList = ReplaceExtension(loadPath, ".txt");
        refAnimatedModelComp_->ApplyMaterialList(matList);

        refAnimController_ = refModelNode_->CreateComponent<AnimationController>();

        // Scan for animations
        refAvailableAnims_.Clear();
        String modelName = GetFileName(loadPath);
        String modelDir = GetParentPath(loadPath);
        const Vector<String>& resDirs = cache->GetResourceDirs();
        for (unsigned d = 0; d < resDirs.Size(); ++d)
        {
            String fullDir = resDirs[d] + modelDir;
            if (!fs->DirExists(fullDir)) continue;
            Vector<String> files;
            fs->ScanDir(files, fullDir, "*.ani", SCAN_FILES, false);
            for (unsigned f = 0; f < files.Size(); ++f)
            {
                if (GetFileName(files[f]).StartsWith(modelName, false))
                {
                    String animPath = modelDir + files[f];
                    if (refAvailableAnims_.Find(animPath) == refAvailableAnims_.End())
                        refAvailableAnims_.Push(animPath);
                }
            }
        }
        Sort(refAvailableAnims_.Begin(), refAvailableAnims_.End());

        // Auto-play idle
        for (unsigned i = 0; i < refAvailableAnims_.Size(); ++i)
        {
            if (GetFileName(refAvailableAnims_[i]).ToLower().Contains("idle"))
            {
                refAnimController_->PlayExclusive(refAvailableAnims_[i], 0, true, 0.2f);
                refCurrentAnimIndex_ = (int)i;
                refAnimPlaying_ = true;
                break;
            }
        }
    }
    else
    {
        refIsAnimated_ = false;
        auto* sm = refModelNode_->CreateComponent<StaticModel>();
        sm->SetModel(mdl);
        sm->SetCastShadows(true);
    }

    URHO3D_LOGINFOF("Slot B: loaded '%s' (%d bones, %u anims) at X=%.1f",
        loadPath.CString(), skel.GetNumBones(), refAvailableAnims_.Size(),
        refModelNode_->GetPosition().x_);

    AutoFrameCamera();
}

void ModelTool::RemoveReferenceModel()
{
    if (refAnimController_)
        refAnimController_->StopAll();

    refModelNode_->RemoveAllComponents();
    refModelNode_->RemoveAllChildren();
    refModel_.Reset();
    refAnimatedModelComp_ = nullptr;
    refAnimController_ = nullptr;
    refIsAnimated_ = false;
    refModelPath_.Clear();
    refAvailableAnims_.Clear();
    refCurrentAnimIndex_ = -1;
    refAnimPlaying_ = false;

    // If we were editing slot B, swap back to A
    if (editingRef_)
        SwapActiveModel();
}

void ModelTool::SwapActiveModel()
{
    if (!refModel_)
    {
        URHO3D_LOGWARNING("SwapActiveModel: no slot B model loaded");
        return;
    }

    // Literal field swap — both survive, they just trade places
    Swap(modelNode_, refModelNode_);
    Swap(currentModel_, refModel_);
    Swap(animatedModelComp_, refAnimatedModelComp_);
    Swap(animController_, refAnimController_);
    Swap(isAnimated_, refIsAnimated_);
    Swap(currentModelPath_, refModelPath_);
    Swap(availableAnims_, refAvailableAnims_);
    Swap(currentAnimIndex_, refCurrentAnimIndex_);
    Swap(animPlaying_, refAnimPlaying_);

    // staticModelComp_ tracks whichever animatedModelComp_ is now active
    staticModelComp_ = animatedModelComp_;

    editingRef_ = !editingRef_;

    // Rebuild UI for the now-active model
    selectedBone_ = -1;
    RebuildAnimList();
    RebuildInfoText();

    if (statusBarText_)
        statusBarText_->SetText(editingRef_ ? "[Slot B] " + currentModelPath_ : "[Slot A] " + currentModelPath_);

    URHO3D_LOGINFOF("Active: %s (%s)", editingRef_ ? "Slot B" : "Slot A", currentModelPath_.CString());
}

// ============================================================================
// IPC — Unix domain socket messaging
// ============================================================================

bool ModelTool::StartIPC()
{
#ifndef _WIN32
    EnsureIPCDirectory();

    // Create listener socket
    ipcSocketPath_ = String(IPC_TTY_DIR) + "modelviewer.sock";

    // Check for stale socket
    {
        int probe = socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe >= 0)
        {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, ipcSocketPath_.CString(), sizeof(addr.sun_path) - 1);
            if (connect(probe, (struct sockaddr*)&addr, sizeof(addr)) == 0)
            {
                close(probe);
                URHO3D_LOGERROR("IPC socket already in use: " + ipcSocketPath_);
                return false;
            }
            close(probe);
        }
    }

    unlink(ipcSocketPath_.CString());
    ipcListenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipcListenFd_ < 0)
    {
        URHO3D_LOGERROR("Failed to create IPC socket");
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipcSocketPath_.CString(), sizeof(addr.sun_path) - 1);

    if (bind(ipcListenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        close(ipcListenFd_);
        ipcListenFd_ = -1;
        URHO3D_LOGERROR("Failed to bind IPC socket");
        return false;
    }

    if (listen(ipcListenFd_, 5) < 0)
    {
        close(ipcListenFd_);
        ipcListenFd_ = -1;
        URHO3D_LOGERROR("Failed to listen on IPC socket");
        return false;
    }

    int flags = fcntl(ipcListenFd_, F_GETFL, 0);
    fcntl(ipcListenFd_, F_SETFL, flags | O_NONBLOCK);
    chmod(ipcSocketPath_.CString(), 0770);

    URHO3D_LOGINFO("IPC listening on " + ipcSocketPath_);
    return true;
#else
    return false;
#endif
}

void ModelTool::StopIPC()
{
#ifndef _WIN32
    for (unsigned i = 0; i < ipcClientFds_.Size(); i++)
        close(ipcClientFds_[i]);
    ipcClientFds_.Clear();

    if (ipcListenFd_ >= 0)
    {
        close(ipcListenFd_);
        ipcListenFd_ = -1;
    }

    if (!ipcSocketPath_.Empty())
        unlink(ipcSocketPath_.CString());
#endif
}

void ModelTool::PollIPC()
{
#ifndef _WIN32
    if (ipcListenFd_ < 0)
        return;

    // Accept new connections
    for (;;)
    {
        int clientFd = accept(ipcListenFd_, nullptr, nullptr);
        if (clientFd < 0) break;
        int flags = fcntl(clientFd, F_GETFL, 0);
        fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);
        ipcClientFds_.Push(clientFd);
    }

    // Read from connected clients
    Vector<int> dead;
    for (unsigned i = 0; i < ipcClientFds_.Size(); i++)
    {
        String accumulated;
        char buf[4096];
        for (;;)
        {
            ssize_t n = read(ipcClientFds_[i], buf, sizeof(buf) - 1);
            if (n > 0) { buf[n] = '\0'; accumulated += String(buf, (unsigned)n); }
            else { if (n == 0) { close(ipcClientFds_[i]); dead.Push(i); } break; }
        }
        if (!accumulated.Empty())
            HandleIPCMessage(accumulated);
    }
    for (int i = dead.Size() - 1; i >= 0; i--)
        ipcClientFds_.Erase(dead[i]);
#endif
}

void ModelTool::HandleIPCMessage(const String& message)
{
    // Strip bracketed paste escapes and control chars
    String clean = message;
    clean.Replace("\x1b[200~", "");
    clean.Replace("\x1b[201~", "");
    clean.Replace("\r", "");
    clean.Replace("\n", " ");
    clean = clean.Trimmed();

    if (clean.Empty())
        return;

    URHO3D_LOGINFOF("IPC received: %s", clean.CString());

    // Commands:
    //   load <resource-path>         — load a single model
    //   folder <path>                — scan folder for browse mode
    //   next                         — browse next
    //   prev                         — browse previous
    //   replyto <role>               — set who gets keep/reject responses
    //   status                       — report current state
    //   ping                         — liveness check

    Vector<String> parts = clean.Split(' ', false);
    if (parts.Empty()) return;

    String cmd = parts[0].ToLower();

    if (cmd == "load" && parts.Size() > 1)
    {
        String path = clean.Substring(clean.Find(' ') + 1).Trimmed();
        LoadModel(path);
        if (!ipcReplyTo_.Empty())
            SendIPCResponse(ipcReplyTo_, "LOADED " + path);
    }
    else if (cmd == "folder" && parts.Size() > 1)
    {
        String path = clean.Substring(clean.Find(' ') + 1).Trimmed();
        ScanFolder(path);
        if (!ipcReplyTo_.Empty())
            SendIPCResponse(ipcReplyTo_, "FOLDER " + String(folderModels_.Size()) + " models");
    }
    else if (cmd == "next")
    {
        BrowseNext();
    }
    else if (cmd == "prev")
    {
        BrowsePrev();
    }
    else if (cmd == "replyto" && parts.Size() > 1)
    {
        ipcReplyTo_ = parts[1];
        URHO3D_LOGINFOF("IPC reply target set to: %s", ipcReplyTo_.CString());
    }
    else if (cmd == "status")
    {
        String status = "ModelTool";
        if (!currentModelPath_.Empty())
            status += " model=" + currentModelPath_;
        if (!folderModels_.Empty())
            status += " browse=" + String(browseIndex_ + 1) + "/" + String(folderModels_.Size());
        if (!ipcReplyTo_.Empty())
            SendIPCResponse(ipcReplyTo_, status);
    }
    else if (cmd == "ping")
    {
        if (!ipcReplyTo_.Empty())
            SendIPCResponse(ipcReplyTo_, "PONG");
    }
    else
    {
        URHO3D_LOGWARNINGF("IPC unknown command: %s", clean.CString());
    }
}

bool ModelTool::SendIPCResponse(const String& targetRole, const String& message)
{
#ifndef _WIN32
    String sockPath = String(IPC_TTY_DIR) + targetRole + ".sock";

    struct stat st;
    if (stat(sockPath.CString(), &st) != 0 || !S_ISSOCK(st.st_mode))
    {
        URHO3D_LOGWARNINGF("IPC target socket not found: %s", sockPath.CString());
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.CString(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        URHO3D_LOGWARNINGF("IPC cannot connect to %s", targetRole.CString());
        return false;
    }

    // Bracketed paste for Claude instances (harmless for non-Claude targets)
    const char* pasteStart = "\x1b[200~";
    const char* pasteEnd = "\x1b[201~";
    String flat = message;
    flat.Replace("\n", " ");
    flat.Replace("\r", "");

    ssize_t w1 = write(fd, pasteStart, 6);
    ssize_t written = write(fd, flat.CString(), flat.Length());
    ssize_t w2 = write(fd, pasteEnd, 6);
    (void)w1; (void)w2;
    if (written > 0)
    {
        usleep(150000);  // 150ms — let Ink process paste before submit
        ssize_t w3 = write(fd, "\r", 1);
        (void)w3;
    }
    close(fd);

    URHO3D_LOGINFOF("IPC sent to %s: %s", targetRole.CString(), message.CString());
    return written > 0;
#else
    return false;
#endif
}

// ============================================================================
// Audio Capture Panel
// ============================================================================

void ModelTool::CreateAudioPanel()
{
    auto* ui = GetSubsystem<UI>();
    auto* graphics = GetSubsystem<Graphics>();

    audioPanel_ = ui->GetRoot()->CreateChild<Window>("AudioPanel");
    audioPanel_->SetStyleAuto();
    audioPanel_->SetFixedWidth(340);
    audioPanel_->SetLayout(LM_VERTICAL, 4, IntRect(8, 6, 8, 6));
    audioPanel_->SetPosition(8, graphics->GetHeight() - 200);
    audioPanel_->SetMovable(true);
    audioPanel_->SetOpacity(0.92f);
    audioPanel_->SetColor(Color(0.20f, 0.25f, 0.20f));

    // Title
    auto* title = audioPanel_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Audio Capture");
    title->SetColor(Color(0.4f, 0.9f, 0.4f));

    // Device selector row
    auto* deviceRow = audioPanel_->CreateChild<UIElement>();
    deviceRow->SetLayout(LM_HORIZONTAL, 4);
    deviceRow->SetFixedHeight(24);

    auto* deviceLabel = deviceRow->CreateChild<Text>();
    deviceLabel->SetFont(font_, 11);
    deviceLabel->SetText("Device: ");
    deviceLabel->SetColor(Color(0.8f, 0.8f, 0.8f));

    captureDeviceDD_ = deviceRow->CreateChild<DropDownList>();
    captureDeviceDD_->SetStyleAuto();
    captureDeviceDD_->SetMinWidth(200);
    captureDeviceDD_->SetFixedHeight(22);

    // Populate device list
    auto* audio = GetSubsystem<Audio>();
    unsigned numDevices = audio->GetNumCaptureDevices();

    auto* defaultItem = new Text(context_);
    defaultItem->SetFont(font_, 11);
    defaultItem->SetText("(Default)");
    defaultItem->SetStyleAuto();
    captureDeviceDD_->AddItem(defaultItem);

    for (unsigned i = 0; i < numDevices; ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 11);
        item->SetText(audio->GetCaptureDeviceName(i));
        item->SetStyleAuto();
        captureDeviceDD_->AddItem(item);
    }

    // Button row
    auto* btnRow = audioPanel_->CreateChild<UIElement>();
    btnRow->SetLayout(LM_HORIZONTAL, 4);
    btnRow->SetFixedHeight(28);

    recordBtn_ = btnRow->CreateChild<Button>();
    recordBtn_->SetStyleAuto();
    recordBtn_->SetFixedSize(80, 26);
    recordBtnText_ = recordBtn_->CreateChild<Text>();
    recordBtnText_->SetFont(font_, 12);
    recordBtnText_->SetText("Record");
    recordBtnText_->SetColor(Color::WHITE);
    recordBtnText_->SetHorizontalAlignment(HA_CENTER);
    recordBtnText_->SetVerticalAlignment(VA_CENTER);
    SubscribeToEvent(recordBtn_, E_RELEASED, URHO3D_HANDLER(ModelTool, HandleRecordToggle));

    playBtn_ = btnRow->CreateChild<Button>();
    playBtn_->SetStyleAuto();
    playBtn_->SetFixedSize(60, 26);
    auto* playText = playBtn_->CreateChild<Text>();
    playText->SetFont(font_, 12);
    playText->SetText("Play");
    playText->SetColor(Color::WHITE);
    playText->SetHorizontalAlignment(HA_CENTER);
    playText->SetVerticalAlignment(VA_CENTER);
    playBtn_->SetEnabled(false);
    SubscribeToEvent(playBtn_, E_RELEASED, URHO3D_HANDLER(ModelTool, HandleAudioPlay));

    saveBtn_ = btnRow->CreateChild<Button>();
    saveBtn_->SetStyleAuto();
    saveBtn_->SetFixedSize(60, 26);
    auto* saveText = saveBtn_->CreateChild<Text>();
    saveText->SetFont(font_, 12);
    saveText->SetText("Save");
    saveText->SetColor(Color::WHITE);
    saveText->SetHorizontalAlignment(HA_CENTER);
    saveText->SetVerticalAlignment(VA_CENTER);
    saveBtn_->SetEnabled(false);
    SubscribeToEvent(saveBtn_, E_RELEASED, URHO3D_HANDLER(ModelTool, HandleAudioSave));

    // Duration text
    captureDurationText_ = audioPanel_->CreateChild<Text>();
    captureDurationText_->SetFont(font_, 11);
    captureDurationText_->SetText("Ready");
    captureDurationText_->SetColor(Color(0.7f, 0.7f, 0.7f));

    // Create preview playback node
    audioPreviewNode_ = scene_->CreateChild("AudioPreview");

    // Pre-open the capture device so hardware warms up — eliminates ~1.5s init latency on Record
    // (reusing `audio` declared earlier in this function)
    audio->PreOpenCapture("", 44100);
}

void ModelTool::HandleRecordToggle(StringHash, VariantMap&)
{
    auto* audio = GetSubsystem<Audio>();

    if (!isRecording_)
    {
        // StartCapture clears warm-up samples and begins fresh recording.
        // Device is already pre-opened from CreateAudioPanel — no hardware init delay.
        String deviceName;
        unsigned sel = captureDeviceDD_->GetSelection();
        if (sel > 0)
            deviceName = audio->GetCaptureDeviceName(sel - 1);

        if (audio->StartCapture(deviceName, 44100))
        {
            isRecording_ = true;
            recordBtnText_->SetText("Stop");
            recordBtnText_->SetColor(Color(1.0f, 0.3f, 0.3f));
            playBtn_->SetEnabled(false);
            saveBtn_->SetEnabled(false);
            captureDurationText_->SetColor(Color(1.0f, 0.4f, 0.4f));
        }
    }
    else
    {
        // Snapshot the captured audio into a Sound before stopping
        unsigned sampleCount = audio->GetCaptureSampleCount();
        i32 sampleRate = audio->GetCaptureSampleRate();

        if (sampleCount > 0)
        {
            // Drain all samples into a temporary buffer
            Vector<i16> raw(sampleCount);
            unsigned drained = audio->DrainCaptureSamples(&raw[0], sampleCount);

            unsigned trimStart = 0;
            unsigned usable = drained - trimStart;
            if (usable > 0)
            {
                capturedSound_ = new Sound(context_);
                capturedSound_->SetSize(usable * sizeof(i16));
                capturedSound_->SetFormat(sampleRate, true, false);  // 16-bit mono
                capturedSound_->SetLooped(false);
                memcpy(capturedSound_->GetStart(), &raw[trimStart], usable * sizeof(i16));
                sampleCount = usable;
            }
            else
            {
                capturedSound_.Reset();
                sampleCount = 0;
            }
        }
        else
            capturedSound_.Reset();

        audio->PauseCapture();  // Keep device warm for next recording
        isRecording_ = false;
        recordBtnText_->SetText("Record");
        recordBtnText_->SetColor(Color::WHITE);
        captureDurationText_->SetColor(Color(0.7f, 0.9f, 0.7f));

        bool hasSamples = capturedSound_.NotNull();
        playBtn_->SetEnabled(hasSamples);
        saveBtn_->SetEnabled(hasSamples);

        if (hasSamples)
            captureDurationText_->SetText(ToString("Captured: %.1fs  (%u samples)",
                (float)sampleCount / sampleRate, sampleCount));
    }
}

void ModelTool::HandleAudioSave(StringHash, VariantMap&)
{
    if (!capturedSound_)
        return;

    // Open file selector for save location
    if (!fileSelector_)
        fileSelector_ = new FileSelector(context_);

    fileSelector_->SetDefaultStyle(GetSubsystem<ResourceCache>()->GetResource<XMLFile>("UI/DefaultStyle.xml"));
    fileSelector_->SetTitle("Save Recorded Audio");
    fileSelector_->SetButtonTexts("Save", "Cancel");
    fileSelector_->SetFilters({"*.wav"}, 0);

    // Default to the project Sounds directory
    auto* fs = GetSubsystem<FileSystem>();
    String soundsDir = fs->GetProgramDir() + "Data/Sounds/";
    if (fs->DirExists(soundsDir))
        fileSelector_->SetPath(soundsDir);

    fileSelector_->SetFileName("recorded.wav");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, [this](StringHash, VariantMap& eventData) {
        using namespace FileSelected;
        String path = eventData[P_FILENAME].GetString();
        bool ok = eventData[P_OK].GetBool();

        if (ok && !path.Empty() && capturedSound_)
        {
            // Write WAV manually from the Sound's PCM data
            File file(context_, path, FILE_WRITE);
            if (!file.IsOpen())
            {
                URHO3D_LOGERRORF("Could not open %s for writing", path.CString());
                fileSelector_.Reset();
                return;
            }

            unsigned dataSize = capturedSound_->GetDataSize();
            u32 fileSize = 36 + dataSize;
            i32 sampleRate = (i32)capturedSound_->GetFrequency();
            i16 channels = 1;
            i16 bitsPerSample = capturedSound_->IsSixteenBit() ? 16 : 8;
            i32 byteRate = sampleRate * channels * bitsPerSample / 8;
            i16 blockAlign = channels * bitsPerSample / 8;

            file.Write("RIFF", 4);
            file.WriteU32(fileSize);
            file.Write("WAVE", 4);
            file.Write("fmt ", 4);
            file.WriteU32(16);
            file.WriteU16(1);  // PCM
            file.WriteU16(channels);
            file.WriteU32(sampleRate);
            file.WriteU32(byteRate);
            file.WriteU16(blockAlign);
            file.WriteU16(bitsPerSample);
            file.Write("data", 4);
            file.WriteU32(dataSize);
            file.Write(capturedSound_->GetStart(), dataSize);

            lastSavedCapturePath_ = path;
            if (captureDurationText_)
                captureDurationText_->SetText("Saved: " + GetFileName(path) + ".wav");
            URHO3D_LOGINFOF("Audio saved to %s (%u bytes)", path.CString(), dataSize);
        }

        fileSelector_.Reset();
    });
}

void ModelTool::HandleAudioPlay(StringHash, VariantMap&)
{
    if (!capturedSound_)
        return;

    PlaySoundPreview(capturedSound_);
}

void ModelTool::HandleCaptureDeviceSelected(StringHash, VariantMap&)
{
    if (isRecording_)
        return;  // Don't switch devices mid-recording

    auto* audio = GetSubsystem<Audio>();

    // Close current warm device
    if (audio->IsCapturing())
        audio->StopCapture();

    // Pre-open the newly selected device
    String deviceName;
    unsigned sel = captureDeviceDD_->GetSelection();
    if (sel > 0)
        deviceName = audio->GetCaptureDeviceName(sel - 1);

    audio->PreOpenCapture(deviceName, 44100);
}

void ModelTool::UpdateAudioPanel(float)
{
    if (!isRecording_ || !captureDurationText_)
        return;

    auto* audio = GetSubsystem<Audio>();
    float duration = audio->GetCaptureDuration();
    captureDurationText_->SetText(ToString("Recording: %.1fs  (%u samples)",
        duration, audio->GetCaptureSampleCount()));
}

void ModelTool::PlaySoundPreview(Sound* sound)
{
    if (!sound || !audioPreviewNode_)
        return;

    auto* audio = GetSubsystem<Audio>();
    URHO3D_LOGINFOF("PlaySoundPreview: dataSize=%u freq=%u 16bit=%d stereo=%d playing=%d",
        sound->GetDataSize(), (unsigned)sound->GetFrequency(),
        (int)sound->IsSixteenBit(), (int)sound->IsStereo(),
        (int)audio->IsPlaying());

    // Remove any existing source
    auto* existing = audioPreviewNode_->GetComponent<SoundSource>();
    if (existing)
        audioPreviewNode_->RemoveComponent(existing);

    auto* source = audioPreviewNode_->CreateComponent<SoundSource>();
    source->SetGain(1.0f);
    source->Play(sound, sound->GetFrequency());

    URHO3D_LOGINFOF("PlaySoundPreview: source gain=%.2f attenuation=%.2f timePos=%.3f",
        source->GetGain(), source->GetAttenuation(), source->GetTimePosition());
}

// ============================================================================
// Material Editor
// ============================================================================

void ModelTool::ScanTechniques()
{
    availableTechniques_.Clear();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    const Vector<String>& dirs = cache->GetResourceDirs();

    // Scan Techniques/ under each resource directory (CoreData and Data)
    HashSet<String> seen;
    for (const String& dir : dirs)
    {
        String techDir = dir + "Techniques/";
        if (!fs->DirExists(techDir))
            continue;

        StringVector files;
        fs->ScanDir(files, techDir, "*.xml", SCAN_FILES, false);

        for (const String& file : files)
        {
            String resPath = "Techniques/" + file;
            if (!seen.Contains(resPath))
            {
                seen.Insert(resPath);
                availableTechniques_.Push(resPath);
            }
        }
    }

    Sort(availableTechniques_.Begin(), availableTechniques_.End());
    URHO3D_LOGINFOF("ScanTechniques: found %u techniques", availableTechniques_.Size());
}

void ModelTool::OpenMaterialEditor(unsigned geomIndex)
{
    if (!staticModelComp_)
        return;

    if (geomIndex >= staticModelComp_->GetNumGeometries())
        return;

    Material* mat = staticModelComp_->GetMaterial(geomIndex);
    if (!mat)
    {
        URHO3D_LOGWARNING("No material on geometry " + String(geomIndex));
        return;
    }

    // Close any existing editor window first
    CloseMaterialEditor();

    CreateMaterialEditorWindow(mat, geomIndex);
}

void ModelTool::CloseMaterialEditor()
{
    if (materialEditorWindow_)
    {
        materialEditorWindow_->Remove();
        materialEditorWindow_ = nullptr;
    }

    matDiffR_ = nullptr;
    matDiffG_ = nullptr;
    matDiffB_ = nullptr;
    matDiffA_ = nullptr;
    matSpecR_ = nullptr;
    matSpecG_ = nullptr;
    matSpecB_ = nullptr;
    matSpecPower_ = nullptr;
    matEmR_ = nullptr;
    matEmG_ = nullptr;
    matEmB_ = nullptr;
    matDiffREdit_ = nullptr;
    matDiffGEdit_ = nullptr;
    matDiffBEdit_ = nullptr;
    matDiffAEdit_ = nullptr;
    matSpecREdit_ = nullptr;
    matSpecGEdit_ = nullptr;
    matSpecBEdit_ = nullptr;
    matSpecPowerEdit_ = nullptr;
    matEmREdit_ = nullptr;
    matEmGEdit_ = nullptr;
    matEmBEdit_ = nullptr;
    matTechDropdown_ = nullptr;
    matDiffuseText_ = nullptr;
}

void ModelTool::CreateMaterialEditorWindow(Material* mat, unsigned geomIndex)
{
    materialEditorGeomIndex_ = geomIndex;

    auto* ui = GetSubsystem<UI>();
    auto* graphics = GetSubsystem<Graphics>();

    // Scan techniques for dropdown population
    if (availableTechniques_.Empty())
        ScanTechniques();

    // Create the window
    materialEditorWindow_ = ui->GetRoot()->CreateChild<Window>("MaterialEditor");
    materialEditorWindow_->SetStyleAuto();
    materialEditorWindow_->SetLayout(LM_VERTICAL, 4, IntRect(8, 6, 8, 6));
    materialEditorWindow_->SetFixedWidth(320);
    materialEditorWindow_->SetPosition(graphics->GetWidth() - 340, 36);
    materialEditorWindow_->SetMovable(true);
    materialEditorWindow_->SetOpacity(0.93f);
    materialEditorWindow_->SetColor(Color(0.18f, 0.18f, 0.24f));

    // Title bar
    auto* titleBar = materialEditorWindow_->CreateChild<BorderImage>();
    titleBar->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
    titleBar->SetFixedHeight(22);
    titleBar->SetColor(Color(0.14f, 0.14f, 0.18f));

    auto* title = titleBar->CreateChild<Text>();
    title->SetFont(font_, 12);
    title->SetText("Material Editor [" + String(geomIndex) + "]");
    title->SetColor(Color(0.9f, 0.9f, 0.4f));

    // Material name
    auto* matNameText = materialEditorWindow_->CreateChild<Text>();
    matNameText->SetFont(font_, 10);
    matNameText->SetText(mat->GetName().Empty() ? "<unnamed>" : mat->GetName());
    matNameText->SetColor(Color(0.7f, 0.85f, 0.7f));

    // ---- Technique dropdown ----
    {
        auto* row = materialEditorWindow_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        row->SetMinHeight(24);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font_, 10);
        lbl->SetText("Technique:");
        lbl->SetColor(Color(0.85f, 0.85f, 0.85f));
        lbl->SetFixedWidth(70);

        matTechDropdown_ = row->CreateChild<DropDownList>();
        matTechDropdown_->SetStyleAuto();
        matTechDropdown_->SetFixedHeight(22);
        matTechDropdown_->SetMinWidth(200);
        matTechDropdown_->SetResizePopup(true);

        // Find the current technique name for preselection
        String currentTechName;
        if (mat->GetNumTechniques() > 0)
        {
            Technique* tech = mat->GetTechnique(0);
            if (tech)
                currentTechName = tech->GetName();
        }

        int selectedIdx = 0;
        for (unsigned i = 0; i < availableTechniques_.Size(); ++i)
        {
            auto* item = new Text(context_);
            item->SetFont(font_, 11);
            item->SetText(availableTechniques_[i]);
            item->SetStyleAuto();
            matTechDropdown_->AddItem(item);

            if (availableTechniques_[i] == currentTechName)
                selectedIdx = (int)i;
        }

        if (!availableTechniques_.Empty())
            matTechDropdown_->SetSelection(selectedIdx);

        SubscribeToEvent(matTechDropdown_, E_ITEMSELECTED, URHO3D_HANDLER(ModelTool, HandleMatTechSelected));
    }

    // ---- Separator ----
    {
        auto* sep = materialEditorWindow_->CreateChild<BorderImage>();
        sep->SetFixedHeight(1);
        sep->SetColor(Color(0.35f, 0.35f, 0.4f, 0.4f));
    }

    // ---- Helper lambda for slider + lineEdit rows ----
    auto makeColorSlider = [this](const String& label, Slider*& slider, LineEdit*& edit,
                                   float range, float initialValue)
    {
        auto* row = materialEditorWindow_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        row->SetMinHeight(20);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font_, 10);
        lbl->SetText(label);
        lbl->SetColor(Color(0.85f, 0.85f, 0.85f));
        lbl->SetFixedWidth(70);

        slider = row->CreateChild<Slider>();
        slider->SetStyleAuto();
        slider->SetFixedHeight(12);
        slider->SetMinWidth(140);
        slider->SetRange(range);
        slider->SetValue(Clamp(initialValue, 0.0f, range));

        edit = row->CreateChild<LineEdit>();
        edit->SetStyleAuto();
        edit->SetFixedSize(60, 18);
        edit->SetCursorPosition(0);
        edit->SetText(String((double)initialValue, 3));

        // Slider changed → update edit + material
        SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(ModelTool, HandleMatSliderChanged));

        // Edit finished → update slider + material
        SubscribeToEvent(edit, E_TEXTFINISHED, [this, slider, range](StringHash, VariantMap& ed)
        {
            if (suppressMatSync_) return;
            String text = static_cast<LineEdit*>(ed[TextFinished::P_ELEMENT].GetPtr())->GetText();
            float val = Clamp(ToFloat(text), 0.0f, range);
            suppressMatSync_ = true;
            slider->SetValue(val);
            suppressMatSync_ = false;
            UpdateMaterialFromSliders();
        });
    };

    // ---- Diffuse Color (RGBA) ----
    {
        auto* sectionLabel = materialEditorWindow_->CreateChild<Text>();
        sectionLabel->SetFont(font_, 10);
        sectionLabel->SetText("Diffuse Color");
        sectionLabel->SetColor(Color(0.9f, 0.8f, 0.4f));
    }

    // Get current diffuse color
    Color diffuse(1.0f, 1.0f, 1.0f, 1.0f);
    {
        const Variant& v = mat->GetShaderParameter("MatDiffColor");
        if (v.GetType() == VAR_VECTOR4)
        {
            Vector4 c = v.GetVector4();
            diffuse = Color(c.x_, c.y_, c.z_, c.w_);
        }
        else if (v.GetType() == VAR_COLOR)
            diffuse = v.GetColor();
    }

    makeColorSlider("R", matDiffR_, matDiffREdit_, 1.0f, diffuse.r_);
    makeColorSlider("G", matDiffG_, matDiffGEdit_, 1.0f, diffuse.g_);
    makeColorSlider("B", matDiffB_, matDiffBEdit_, 1.0f, diffuse.b_);
    makeColorSlider("A", matDiffA_, matDiffAEdit_, 1.0f, diffuse.a_);

    // ---- Specular Color (RGB + Power) ----
    {
        auto* sectionLabel = materialEditorWindow_->CreateChild<Text>();
        sectionLabel->SetFont(font_, 10);
        sectionLabel->SetText("Specular Color");
        sectionLabel->SetColor(Color(0.9f, 0.8f, 0.4f));
    }

    // Get current specular color (stored as Vector4: R,G,B,Power)
    Vector4 specular(0.0f, 0.0f, 0.0f, 1.0f);
    {
        const Variant& v = mat->GetShaderParameter("MatSpecColor");
        if (v.GetType() == VAR_VECTOR4)
            specular = v.GetVector4();
    }

    makeColorSlider("R", matSpecR_, matSpecREdit_, 1.0f, specular.x_);
    makeColorSlider("G", matSpecG_, matSpecGEdit_, 1.0f, specular.y_);
    makeColorSlider("B", matSpecB_, matSpecBEdit_, 1.0f, specular.z_);
    makeColorSlider("Power", matSpecPower_, matSpecPowerEdit_, 128.0f, specular.w_);

    // ---- Emissive Color (RGB) ----
    {
        auto* sectionLabel = materialEditorWindow_->CreateChild<Text>();
        sectionLabel->SetFont(font_, 10);
        sectionLabel->SetText("Emissive Color");
        sectionLabel->SetColor(Color(0.9f, 0.8f, 0.4f));
    }

    // Get current emissive color
    Color emissive(0.0f, 0.0f, 0.0f);
    {
        const Variant& v = mat->GetShaderParameter("MatEmissiveColor");
        if (v.GetType() == VAR_VECTOR3)
        {
            Vector3 c = v.GetVector3();
            emissive = Color(c.x_, c.y_, c.z_);
        }
        else if (v.GetType() == VAR_COLOR)
            emissive = v.GetColor();
    }

    makeColorSlider("R", matEmR_, matEmREdit_, 1.0f, emissive.r_);
    makeColorSlider("G", matEmG_, matEmGEdit_, 1.0f, emissive.g_);
    makeColorSlider("B", matEmB_, matEmBEdit_, 1.0f, emissive.b_);

    // ---- Separator ----
    {
        auto* sep = materialEditorWindow_->CreateChild<BorderImage>();
        sep->SetFixedHeight(1);
        sep->SetColor(Color(0.35f, 0.35f, 0.4f, 0.4f));
    }

    // ---- Diffuse texture row ----
    {
        auto* texRow = materialEditorWindow_->CreateChild<UIElement>();
        texRow->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        texRow->SetMinHeight(22);

        auto* texLabel = texRow->CreateChild<Text>();
        texLabel->SetFont(font_, 10);
        texLabel->SetText("Diffuse:");
        texLabel->SetColor(Color(0.85f, 0.85f, 0.85f));
        texLabel->SetFixedWidth(70);

        matDiffuseText_ = texRow->CreateChild<Text>();
        matDiffuseText_->SetFont(font_, 10);
        matDiffuseText_->SetColor(Color(0.7f, 0.85f, 0.7f));

        Texture* diffTex = mat->GetTexture(TU_DIFFUSE);
        matDiffuseText_->SetText(diffTex ? GetFileName(diffTex->GetName()) : "<none>");

        auto* browseBtn = texRow->CreateChild<Button>();
        browseBtn->SetStyleAuto();
        browseBtn->SetFixedSize(60, 18);
        browseBtn->SetColor(Color(0.25f, 0.3f, 0.35f));

        auto* browseLbl = browseBtn->CreateChild<Text>();
        browseLbl->SetFont(font_, 10);
        browseLbl->SetText("Browse");
        browseLbl->SetAlignment(HA_CENTER, VA_CENTER);

        SubscribeToEvent(browseBtn, E_RELEASED, URHO3D_HANDLER(ModelTool, HandleMatTextureBrowse));
    }

    // ---- Separator ----
    {
        auto* sep = materialEditorWindow_->CreateChild<BorderImage>();
        sep->SetFixedHeight(1);
        sep->SetColor(Color(0.35f, 0.35f, 0.4f, 0.4f));
    }

    // ---- Button row: Apply, Save, Close ----
    {
        auto* btnRow = materialEditorWindow_->CreateChild<UIElement>();
        btnRow->SetLayout(LM_HORIZONTAL, 6, IntRect(2, 2, 2, 2));
        btnRow->SetMinHeight(26);

        // Apply button
        auto* applyBtn = btnRow->CreateChild<Button>();
        applyBtn->SetStyleAuto();
        applyBtn->SetFixedSize(60, 22);
        applyBtn->SetColor(Color(0.25f, 0.35f, 0.25f));

        auto* applyLbl = applyBtn->CreateChild<Text>();
        applyLbl->SetFont(font_, 10);
        applyLbl->SetText("Apply");
        applyLbl->SetAlignment(HA_CENTER, VA_CENTER);

        SubscribeToEvent(applyBtn, E_RELEASED, [this](StringHash, VariantMap&) { UpdateMaterialFromSliders(); });

        // Save button
        auto* saveBtn = btnRow->CreateChild<Button>();
        saveBtn->SetStyleAuto();
        saveBtn->SetFixedSize(60, 22);
        saveBtn->SetColor(Color(0.3f, 0.3f, 0.4f));

        auto* saveLbl = saveBtn->CreateChild<Text>();
        saveLbl->SetFont(font_, 10);
        saveLbl->SetText("Save");
        saveLbl->SetAlignment(HA_CENTER, VA_CENTER);

        SubscribeToEvent(saveBtn, E_RELEASED, [this](StringHash, VariantMap&) { SaveEditedMaterial(); });

        // Close button
        auto* closeBtn = btnRow->CreateChild<Button>();
        closeBtn->SetStyleAuto();
        closeBtn->SetFixedSize(60, 22);
        closeBtn->SetColor(Color(0.35f, 0.2f, 0.2f));

        auto* closeLbl = closeBtn->CreateChild<Text>();
        closeLbl->SetFont(font_, 10);
        closeLbl->SetText("Close");
        closeLbl->SetAlignment(HA_CENTER, VA_CENTER);

        SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&) { CloseMaterialEditor(); });
    }
}

void ModelTool::UpdateMaterialFromSliders()
{
    if (!staticModelComp_)
        return;

    Material* mat = staticModelComp_->GetMaterial(materialEditorGeomIndex_);
    if (!mat)
        return;

    // Diffuse color
    if (matDiffR_ && matDiffG_ && matDiffB_ && matDiffA_)
    {
        Vector4 diffuse(matDiffR_->GetValue(), matDiffG_->GetValue(),
                        matDiffB_->GetValue(), matDiffA_->GetValue());
        mat->SetShaderParameter("MatDiffColor", diffuse);
    }

    // Specular color (RGB + power)
    if (matSpecR_ && matSpecG_ && matSpecB_ && matSpecPower_)
    {
        Vector4 specular(matSpecR_->GetValue(), matSpecG_->GetValue(),
                         matSpecB_->GetValue(), matSpecPower_->GetValue());
        mat->SetShaderParameter("MatSpecColor", specular);
    }

    // Emissive color
    if (matEmR_ && matEmG_ && matEmB_)
    {
        Vector3 emissive(matEmR_->GetValue(), matEmG_->GetValue(), matEmB_->GetValue());
        mat->SetShaderParameter("MatEmissiveColor", emissive);
    }
}

void ModelTool::HandleMatSliderChanged(StringHash eventType, VariantMap& eventData)
{
    if (suppressMatSync_)
        return;

    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    float value = eventData[P_VALUE].GetFloat();

    // Sync the companion LineEdit for whichever slider changed
    auto syncEdit = [&](Slider* s, LineEdit* e)
    {
        if (slider == s && e)
        {
            suppressMatSync_ = true;
            e->SetText(String((double)value, 3));
            suppressMatSync_ = false;
        }
    };

    syncEdit(matDiffR_, matDiffREdit_);
    syncEdit(matDiffG_, matDiffGEdit_);
    syncEdit(matDiffB_, matDiffBEdit_);
    syncEdit(matDiffA_, matDiffAEdit_);
    syncEdit(matSpecR_, matSpecREdit_);
    syncEdit(matSpecG_, matSpecGEdit_);
    syncEdit(matSpecB_, matSpecBEdit_);
    syncEdit(matSpecPower_, matSpecPowerEdit_);
    syncEdit(matEmR_, matEmREdit_);
    syncEdit(matEmG_, matEmGEdit_);
    syncEdit(matEmB_, matEmBEdit_);

    // Live preview
    UpdateMaterialFromSliders();
}

void ModelTool::HandleMatTextureBrowse(StringHash eventType, VariantMap& eventData)
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    auto* style = uiRoot->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Select Diffuse Texture");
    fileSelector_->SetButtonTexts("Select", "Cancel");

    Vector<String> filters;
    filters.Push("*.png");
    filters.Push("*.jpg");
    filters.Push("*.dds");
    filters.Push("*.tga");
    filters.Push("*.bmp");
    filters.Push("*.*");
    fileSelector_->SetFilters(filters, 0);

    // Start in a Textures/ directory if possible
    auto* cache = GetSubsystem<ResourceCache>();
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (const String& dir : dirs)
    {
        auto* fs = GetSubsystem<FileSystem>();
        if (fs->DirExists(dir + "Textures/"))
        {
            fileSelector_->SetPath(dir + "Textures/");
            break;
        }
    }

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(ModelTool, HandleMatTextureSelected));
}

void ModelTool::HandleMatTextureSelected(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty())
        return;

    if (!staticModelComp_)
        return;

    Material* mat = staticModelComp_->GetMaterial(materialEditorGeomIndex_);
    if (!mat)
        return;

    // Convert absolute path to a resource path
    auto* cache = GetSubsystem<ResourceCache>();
    String resPath;
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (const String& dir : dirs)
    {
        if (path.StartsWith(dir))
        {
            resPath = path.Substring(dir.Length());
            break;
        }
    }

    if (resPath.Empty())
    {
        URHO3D_LOGWARNING("Selected texture is not in a resource directory: " + path);
        return;
    }

    auto* tex = cache->GetResource<Texture2D>(resPath);
    if (!tex)
    {
        URHO3D_LOGWARNING("Failed to load texture: " + resPath);
        return;
    }

    mat->SetTexture(TU_DIFFUSE, tex);

    // Update the label
    if (matDiffuseText_)
        matDiffuseText_->SetText(GetFileName(resPath));

    URHO3D_LOGINFOF("Material texture set: %s", resPath.CString());
}

void ModelTool::HandleMatTechSelected(StringHash eventType, VariantMap& eventData)
{
    using namespace ItemSelected;
    int sel = eventData[P_SELECTION].GetI32();

    if (sel < 0 || sel >= (int)availableTechniques_.Size())
        return;

    if (!staticModelComp_)
        return;

    Material* mat = staticModelComp_->GetMaterial(materialEditorGeomIndex_);
    if (!mat)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* tech = cache->GetResource<Technique>(availableTechniques_[sel]);
    if (tech)
    {
        mat->SetTechnique(0, tech);
        URHO3D_LOGINFOF("Material technique set: %s", availableTechniques_[sel].CString());
    }
}

void ModelTool::SaveEditedMaterial()
{
    if (!staticModelComp_)
        return;

    Material* mat = staticModelComp_->GetMaterial(materialEditorGeomIndex_);
    if (!mat)
        return;

    // Determine save path: use material's existing name, or derive from model path
    String matName = mat->GetName();
    if (matName.Empty())
    {
        // Generate a name from the model path
        String baseName = GetFileName(currentModelPath_);
        matName = "Materials/" + baseName + "_" + String(materialEditorGeomIndex_) + ".xml";
    }

    // Resolve to absolute path via resource directories
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    String absPath;

    const Vector<String>& dirs = cache->GetResourceDirs();
    for (const String& dir : dirs)
    {
        String candidate = dir + matName;
        // Use the first resource dir that has a Materials/ directory
        String matDir = GetPath(candidate);
        if (fs->DirExists(matDir))
        {
            absPath = candidate;
            break;
        }
    }

    if (absPath.Empty())
    {
        // Fallback: create in first resource dir
        if (!dirs.Empty())
        {
            absPath = dirs[0] + matName;
            String matDir = GetPath(absPath);
            if (!fs->DirExists(matDir))
                fs->CreateDir(matDir);
        }
    }

    if (absPath.Empty())
    {
        URHO3D_LOGWARNING("Cannot determine save path for material");
        return;
    }

    File file(context_, absPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGWARNING("Failed to open file for writing: " + absPath);
        return;
    }

    if (mat->Save(file))
    {
        URHO3D_LOGINFOF("Material saved: %s", absPath.CString());

        // Update the material's name if it was unnamed
        if (mat->GetName().Empty())
            mat->SetName(matName);

        // Refresh the info panel to reflect changes
        RebuildInfoText();
    }
    else
    {
        URHO3D_LOGWARNING("Failed to save material: " + absPath);
    }
}

// ============================================================================
// Selective FBX Import
// ============================================================================

bool ModelTool::ProbeFBX(const String& fbxPath, FBXProbeInfo& info)
{
    auto* fs = GetSubsystem<FileSystem>();

    String assetTool = fs->GetProgramDir() + "tool/AssetTool";
    if (!fs->FileExists(assetTool))
    {
        assetTool = fs->GetProgramDir() + "AssetTool";
        if (!fs->FileExists(assetTool))
            return false;
    }

    String tmpPath = fs->GetTemporaryDir() + "urho3d_fbx_probe.txt";
    String cmd = "\"" + assetTool + "\" info \"" + fbxPath + "\" /dev/null > \"" + tmpPath + "\" 2>&1";
    int result = fs->SystemCommand(cmd);
    if (result != 0)
        return false;

    File f(context_, tmpPath, FILE_READ);
    if (!f.IsOpen())
        return false;

    // Parse the info output
    enum Section { SEC_NONE, SEC_ANIMS, SEC_MATS, SEC_MESHES };
    Section section = SEC_NONE;

    while (!f.IsEof())
    {
        String line = f.ReadLine();

        // Suggested scale
        if (line.Contains("normalize to ~2.0"))
        {
            // Extract the -scale value
            unsigned scalePos = line.Find("-scale ");
            if (scalePos != String::NPOS)
            {
                String scaleStr = line.Substring(scalePos + 7).Trimmed();
                unsigned paren = scaleStr.Find('(');
                if (paren != String::NPOS)
                    scaleStr = scaleStr.Substring(0, paren).Trimmed();
                info.suggestedScale = ToFloat(scaleStr);
            }
        }

        // Bounding box
        if (line.Contains("Bounding box min:"))
        {
            unsigned p1 = line.Find('(');
            unsigned p2 = line.Find(')');
            if (p1 != String::NPOS && p2 != String::NPOS)
            {
                String coords = line.Substring(p1 + 1, p2 - p1 - 1);
                Vector<String> parts = coords.Split(',');
                if (parts.Size() == 3)
                    info.bboxMin = Vector3(ToFloat(parts[0].Trimmed()), ToFloat(parts[1].Trimmed()), ToFloat(parts[2].Trimmed()));
            }
        }
        if (line.Contains("Bounding box max:"))
        {
            unsigned p1 = line.Find('(');
            unsigned p2 = line.Find(')');
            if (p1 != String::NPOS && p2 != String::NPOS)
            {
                String coords = line.Substring(p1 + 1, p2 - p1 - 1);
                Vector<String> parts = coords.Split(',');
                if (parts.Size() == 3)
                    info.bboxMax = Vector3(ToFloat(parts[0].Trimmed()), ToFloat(parts[1].Trimmed()), ToFloat(parts[2].Trimmed()));
            }
        }

        // Section headers
        if (line.Trimmed().StartsWith("Animations:") && !line.Contains("["))
        { section = SEC_ANIMS; continue; }
        if (line.Trimmed().StartsWith("Materials:") && !line.Contains("["))
        { section = SEC_MATS; continue; }
        if (line.Trimmed().StartsWith("Meshes:") && !line.Contains("["))
        { section = SEC_MESHES; continue; }

        // Empty line ends section
        if (line.Trimmed().Empty() && section != SEC_NONE)
        { section = SEC_NONE; continue; }

        // Parse items
        if (section == SEC_ANIMS && line.Contains("["))
        {
            FBXProbeInfo::AnimInfo ai;
            // Format: "    [0] name                            0.88s  113 channels  24 tps"
            unsigned bracket = line.Find(']');
            if (bracket == String::NPOS) continue;
            String rest = line.Substring(bracket + 1).Trimmed();

            // Name ends before the duration (first number followed by 's')
            // Walk backwards from first digit cluster
            Vector<String> tokens = rest.Split(' ');
            ai.name = "";
            for (unsigned t = 0; t < tokens.Size(); ++t)
            {
                String tok = tokens[t].Trimmed();
                if (tok.Empty()) continue;
                // If token ends with 's' and is a number, it's the duration
                if (tok.EndsWith("s") && tok.Length() > 1)
                {
                    String numPart = tok.Substring(0, tok.Length() - 1);
                    float val = ToFloat(numPart);
                    if (val > 0.0f)
                    {
                        ai.duration = val;
                        // Next tokens: N channels  N tps
                        if (t + 1 < tokens.Size()) ai.channels = ToI32(tokens[t + 1].Trimmed());
                        if (t + 3 < tokens.Size()) ai.tps = ToFloat(tokens[t + 3].Trimmed());
                        break;
                    }
                }
                if (!ai.name.Empty()) ai.name += " ";
                ai.name += tok;
            }
            if (!ai.name.Empty())
                info.anims.Push(ai);
        }

        if (section == SEC_MATS && line.Contains("["))
        {
            FBXProbeInfo::MatInfo mi;
            unsigned bracket = line.Find(']');
            if (bracket == String::NPOS) continue;
            String rest = line.Substring(bracket + 1).Trimmed();
            // Format: "Material #27               1 textures"
            Vector<String> tokens = rest.Split(' ');
            mi.name = "";
            for (unsigned t = 0; t < tokens.Size(); ++t)
            {
                String tok = tokens[t].Trimmed();
                if (tok.Empty()) continue;
                if (tok == "textures" || tok == "texture")
                {
                    if (t > 0) mi.texCount = ToI32(tokens[t - 1].Trimmed());
                    break;
                }
                if (t > 0 && !mi.name.Empty()) mi.name += " ";
                if (t == 0 || !tok.Empty()) mi.name += tok;
            }
            if (!mi.name.Empty())
                info.materials.Push(mi);
        }

        if (section == SEC_MESHES && line.Contains("["))
        {
            FBXProbeInfo::MeshInfo mi;
            unsigned bracket = line.Find(']');
            if (bracket == String::NPOS) continue;
            String rest = line.Substring(bracket + 1).Trimmed();
            // Format: "gaiyi                      6342 verts  11292 faces  52 bones  mat:0"
            Vector<String> tokens = rest.Split(' ');
            mi.name = "";
            bool pastName = false;
            for (unsigned t = 0; t < tokens.Size(); ++t)
            {
                String tok = tokens[t].Trimmed();
                if (tok.Empty()) continue;
                if (tok == "verts") { if (t > 0) mi.verts = ToI32(tokens[t - 1].Trimmed()); pastName = true; continue; }
                if (tok == "faces") { if (t > 0) mi.faces = ToI32(tokens[t - 1].Trimmed()); continue; }
                if (tok == "bones") { if (t > 0) mi.bones = ToI32(tokens[t - 1].Trimmed()); continue; }
                if (tok.StartsWith("mat:")) { mi.matIndex = ToI32(tok.Substring(4)); continue; }
                if (!pastName)
                {
                    if (!mi.name.Empty()) mi.name += " ";
                    mi.name += tok;
                }
            }
            if (!mi.name.Empty())
                info.meshes.Push(mi);
        }
    }

    f.Close();
    fs->Delete(tmpPath);
    return !info.meshes.Empty() || !info.anims.Empty() || !info.materials.Empty();
}

void ModelTool::ShowSelectiveImportDialog(const String& fbxPath, const FBXProbeInfo& info)
{
    selectiveImportPath_ = fbxPath;
    selectiveImportInfo_ = info;
    meshCheckboxes_.Clear();
    animCheckboxes_.Clear();
    matCheckboxes_.Clear();

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    // Remove previous dialog if any
    if (selectiveImportWindow_)
    {
        selectiveImportWindow_->Remove();
        selectiveImportWindow_ = nullptr;
    }

    selectiveImportWindow_ = new Window(context_);
    selectiveImportWindow_->SetStyle("Window", style);
    selectiveImportWindow_->SetMinSize(500, 400);
    selectiveImportWindow_->SetMaxSize(700, 800);
    selectiveImportWindow_->SetLayout(LM_VERTICAL, 4, IntRect(8, 8, 8, 8));
    selectiveImportWindow_->SetMovable(true);
    selectiveImportWindow_->SetResizable(true);
    selectiveImportWindow_->SetPosition(100, 50);
    ui->GetRoot()->AddChild(selectiveImportWindow_);

    // Title
    auto* title = selectiveImportWindow_->CreateChild<Text>();
    title->SetFont(font_, 14);
    title->SetText("Import: " + GetFileNameAndExtension(fbxPath));
    title->SetColor(Color(1.0f, 0.9f, 0.4f));

    // Scrollable content
    auto* scroll = selectiveImportWindow_->CreateChild<ScrollView>();
    scroll->SetStyleAuto();
    scroll->SetMinHeight(250);

    auto* content = new UIElement(context_);
    content->SetLayout(LM_VERTICAL, 3);
    scroll->SetContentElement(content);

    auto makeLabel = [&](UIElement* parent, const String& text, const Color& color = Color(0.85f, 0.85f, 0.85f)) {
        auto* t = parent->CreateChild<Text>();
        t->SetFont(font_, 11);
        t->SetText(text);
        t->SetColor(color);
        return t;
    };

    auto makeCheckRow = [&](UIElement* parent, const String& label, bool checked = true) -> CheckBox* {
        auto* row = parent->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetFixedHeight(20);
        auto* cb = row->CreateChild<CheckBox>();
        cb->SetStyleAuto();
        cb->SetChecked(checked);
        auto* t = row->CreateChild<Text>();
        t->SetFont(font_, 10);
        t->SetText(label);
        t->SetColor(Color(0.8f, 0.8f, 0.8f));
        return cb;
    };

    // ---- Meshes ----
    if (!info.meshes.Empty())
    {
        makeLabel(content, "Meshes (" + String(info.meshes.Size()) + ")", Color(0.3f, 0.9f, 1.0f));
        importMeshesAll_ = makeCheckRow(content, "Import ALL meshes");
        for (unsigned i = 0; i < info.meshes.Size(); ++i)
        {
            const auto& m = info.meshes[i];
            String label = m.name + "  (" + String(m.verts) + " verts, " + String(m.faces) + " faces";
            if (m.bones > 0) label += ", " + String(m.bones) + " bones";
            label += ")";
            meshCheckboxes_.Push(makeCheckRow(content, "  " + label));
        }
    }

    // ---- Animations ----
    if (!info.anims.Empty())
    {
        makeLabel(content, "Animations (" + String(info.anims.Size()) + ")", Color(0.4f, 1.0f, 0.5f));
        importAnimsAll_ = makeCheckRow(content, "Import ALL animations");
        for (unsigned i = 0; i < info.anims.Size(); ++i)
        {
            const auto& a = info.anims[i];
            String label = a.name + "  (" + String(a.duration) + "s, " + String(a.channels) + " ch)";
            animCheckboxes_.Push(makeCheckRow(content, "  " + label));
        }
    }

    // ---- Materials ----
    if (!info.materials.Empty())
    {
        makeLabel(content, "Materials (" + String(info.materials.Size()) + ")", Color(1.0f, 0.7f, 0.3f));
        importMatsAll_ = makeCheckRow(content, "Import ALL materials");
        for (unsigned i = 0; i < info.materials.Size(); ++i)
        {
            const auto& m = info.materials[i];
            String label = m.name;
            if (m.texCount > 0) label += "  (" + String(m.texCount) + " textures)";
            matCheckboxes_.Push(makeCheckRow(content, "  " + label));
        }
    }

    // ---- Options ----
    makeLabel(content, "Options", Color(0.9f, 0.9f, 0.5f));

    // Scale
    {
        auto* row = content->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetFixedHeight(22);
        makeLabel(row, "Scale:");
        importScaleEdit_ = row->CreateChild<LineEdit>();
        importScaleEdit_->SetStyleAuto();
        importScaleEdit_->SetFixedWidth(120);
        importScaleEdit_->SetText(String(info.suggestedScale));
    }

    // Output prefix
    {
        auto* row = content->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetFixedHeight(22);
        makeLabel(row, "Output prefix:");
        importPrefixEdit_ = row->CreateChild<LineEdit>();
        importPrefixEdit_->SetStyleAuto();
        importPrefixEdit_->SetFixedWidth(200);
        importPrefixEdit_->SetText(GetFileName(fbxPath));
    }

    // Tangents
    importTangents_ = makeCheckRow(content, "Generate tangents", true);

    // ---- Buttons ----
    auto* btnRow = selectiveImportWindow_->CreateChild<UIElement>();
    btnRow->SetLayout(LM_HORIZONTAL, 8);
    btnRow->SetFixedHeight(30);

    auto makeBtn = [&](UIElement* parent, const String& text) {
        auto* btn = parent->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetMinSize(100, 26);
        auto* t = btn->CreateChild<Text>();
        t->SetFont(font_, 11);
        t->SetText(text);
        t->SetAlignment(HA_CENTER, VA_CENTER);
        return btn;
    };

    auto* importBtn = makeBtn(btnRow, "Import");
    auto* cancelBtn = makeBtn(btnRow, "Cancel");

    SubscribeToEvent(importBtn, E_RELEASED, [this](StringHash, VariantMap&) {
        ExecuteSelectiveImport();
    });

    SubscribeToEvent(cancelBtn, E_RELEASED, [this](StringHash, VariantMap&) {
        if (selectiveImportWindow_)
        {
            selectiveImportWindow_->Remove();
            selectiveImportWindow_ = nullptr;
        }
    });
}

void ModelTool::ExecuteSelectiveImport()
{
    auto* fs = GetSubsystem<FileSystem>();

    String assetTool = fs->GetProgramDir() + "tool/AssetTool";
    if (!fs->FileExists(assetTool))
    {
        assetTool = fs->GetProgramDir() + "AssetTool";
        if (!fs->FileExists(assetTool))
        {
            if (statusText_) statusText_->SetText("ERROR: AssetTool not found");
            return;
        }
    }

    // Determine what to import
    bool anyMesh = false, anyAnim = false, anyMat = false;

    if (importMeshesAll_ && importMeshesAll_->IsChecked())
        anyMesh = true;
    else
    {
        for (unsigned i = 0; i < meshCheckboxes_.Size(); ++i)
            if (meshCheckboxes_[i]->IsChecked()) { anyMesh = true; break; }
    }

    if (importAnimsAll_ && importAnimsAll_->IsChecked())
        anyAnim = true;
    else
    {
        for (unsigned i = 0; i < animCheckboxes_.Size(); ++i)
            if (animCheckboxes_[i]->IsChecked()) { anyAnim = true; break; }
    }

    if (importMatsAll_ && importMatsAll_->IsChecked())
        anyMat = true;
    else
    {
        for (unsigned i = 0; i < matCheckboxes_.Size(); ++i)
            if (matCheckboxes_[i]->IsChecked()) { anyMat = true; break; }
    }

    if (!anyMesh && !anyAnim && !anyMat)
    {
        if (statusText_) statusText_->SetText("Nothing selected to import");
        return;
    }

    // Get scale and prefix
    float scale = 1.0f;
    if (importScaleEdit_)
        scale = ToFloat(importScaleEdit_->GetText());

    String prefix = importPrefixEdit_ ? importPrefixEdit_->GetText().Trimmed() : GetFileName(selectiveImportPath_);
    if (prefix.Empty())
        prefix = GetFileName(selectiveImportPath_);

    // Create DMZ directory
    String projectRoot;
    String dir = fs->GetProgramDir();
    for (int i = 0; i < 5; ++i)
    {
        if (fs->DirExists(dir + "brainfarts/"))
        { projectRoot = dir; break; }
        dir = GetParentPath(dir);
    }
    if (projectRoot.Empty())
        projectRoot = GetPath(GetPath(selectiveImportPath_));

    String dmzPath = projectRoot + "brainfarts/_import_review/";
    if (!fs->DirExists(dmzPath))
        fs->CreateDir(dmzPath);

    // Build commands
    String scaleArg = (scale != 1.0f) ? " -scale " + String(scale) : "";
    String tangentArg = (importTangents_ && importTangents_->IsChecked()) ? " -t" : "";
    bool success = true;

    // Import model (meshes)
    if (anyMesh)
    {
        String mdlPath = dmzPath + prefix + ".mdl";
        String cmd = "\"" + assetTool + "\" model \"" + selectiveImportPath_ + "\" \"" + mdlPath + "\" -l" + tangentArg + scaleArg;
        if (!anyAnim) cmd += " -na";
        if (!anyMat) cmd += " -nm -nt";
        URHO3D_LOGINFOF("Import model: %s", cmd.CString());
        int result = fs->SystemCommand(cmd);
        if (result != 0)
        {
            URHO3D_LOGERRORF("Model import failed (exit %d)", result);
            if (statusText_) statusText_->SetText("ERROR: Model import failed");
            success = false;
        }
    }

    // Import animations only (if meshes weren't imported, or we want separate anim files)
    if (anyAnim && !anyMesh)
    {
        String animOut = dmzPath + prefix + ".ani";
        String cmd = "\"" + assetTool + "\" anim \"" + selectiveImportPath_ + "\" \"" + animOut + "\"" + scaleArg;
        URHO3D_LOGINFOF("Import anims: %s", cmd.CString());
        int result = fs->SystemCommand(cmd);
        if (result != 0)
        {
            URHO3D_LOGERRORF("Animation import failed (exit %d)", result);
            if (statusText_) statusText_->SetText("ERROR: Animation import failed");
            success = false;
        }
    }

    // Materials only (import model but skip the .mdl, keep materials)
    if (anyMat && !anyMesh)
    {
        String matOut = dmzPath + prefix + "_mat.mdl";
        String cmd = "\"" + assetTool + "\" model \"" + selectiveImportPath_ + "\" \"" + matOut + "\" -l -na" + scaleArg;
        URHO3D_LOGINFOF("Import materials: %s", cmd.CString());
        int result = fs->SystemCommand(cmd);
        if (result != 0)
        {
            URHO3D_LOGERRORF("Material import failed (exit %d)", result);
            success = false;
        }
    }

    // Close dialog
    if (selectiveImportWindow_)
    {
        selectiveImportWindow_->Remove();
        selectiveImportWindow_ = nullptr;
    }

    if (success)
    {
        // Enter import mode and load the result
        importMode_ = true;
        importDMZPath_ = dmzPath;

        String mdlPath = dmzPath + prefix + ".mdl";
        if (fs->FileExists(mdlPath))
        {
            LoadModel(mdlPath);
            ScanFolder(dmzPath);
        }

        if (statusText_)
            statusText_->SetText("IMPORT MODE — K: promote   G: delete   Left/Right: browse");
    }
}
