#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/AnimationState.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/FileSelector.h>
#include <Urho3D/UI/DropDownList.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/ListView.h>
#include <Urho3D/UI/ScrollView.h>
#include <Urho3D/UI/Slider.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/CheckBox.h>
#include <Urho3D/UI/Window.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/Audio/Audio.h>
#include <Urho3D/Audio/Sound.h>
#include <Urho3D/Audio/SoundSource.h>

struct sqlite3;

namespace Urho3D { class IKSolver; class IKEffector; }

using namespace Urho3D;

/// A single field definition inside a text key template
struct TextKeyTemplateField
{
    String name;
    VariantType type{VAR_STRING};
    Variant defaultValue;
    String description;
    String resourceType;    ///< for ResourceRef fields: "Material", "Sound", "ParticleEffect", etc.
};

/// A loaded text key schema template
struct TextKeyTemplate
{
    String name;
    String category;
    Color color{Color::WHITE};
    String description;
    Vector<TextKeyTemplateField> fields;
};

/// IPC socket directory
static const char* IPC_TTY_DIR = "/tmp/urho_claude/tty/";
static const char* IPC_INST_DIR = "/tmp/urho_claude/instances/";

class ModelTool : public Application
{
    URHO3D_OBJECT(ModelTool, Application);

public:
    explicit ModelTool(Context* context);

    void Setup() override;
    void Start() override;
    void Stop() override;

private:
    // Scene
    void CreateScene();
    void SetupViewport();

    // Model
    void LoadModel(const String& path);
    void SaveLastModelDir(const String& dirPath);
    String LoadLastModelDir() const;
    void AutoFrameCamera();
    void ScanAnimations();
    void ScanFolder(const String& folderPath);
    void BrowseNext();
    void BrowsePrev();
    void BrowseFlagKeep();
    void BrowseFlagReject();
    void WriteFolderReview();
    void ImportModel(const String& fbxPath);
    void ShowImportDialog();
    void PromoteCurrentModel();

    // Selective import — probe FBX, let user pick what to import
    struct FBXProbeInfo {
        struct MeshInfo { String name; int verts; int faces; int bones; int matIndex; };
        struct AnimInfo { String name; float duration; int channels; float tps; };
        struct MatInfo { String name; int texCount; String diffusePath; };
        Vector<MeshInfo> meshes;
        Vector<AnimInfo> anims;
        Vector<MatInfo> materials;
        float suggestedScale{1.0f};
        Vector3 bboxMin;
        Vector3 bboxMax;
    };
    bool ProbeFBX(const String& fbxPath, FBXProbeInfo& info);
    void ShowSelectiveImportDialog(const String& fbxPath, const FBXProbeInfo& info);
    void ExecuteSelectiveImport();

    // Selective import UI state
    Window* selectiveImportWindow_{};
    String selectiveImportPath_;
    FBXProbeInfo selectiveImportInfo_;
    Vector<CheckBox*> meshCheckboxes_;
    Vector<CheckBox*> animCheckboxes_;
    Vector<CheckBox*> matCheckboxes_;
    CheckBox* importMeshesAll_{};
    CheckBox* importAnimsAll_{};
    CheckBox* importMatsAll_{};
    LineEdit* importScaleEdit_{};
    LineEdit* importPrefixEdit_{};
    CheckBox* importTangents_{};
    void DeleteCurrentModel();
    void PlayAnimation(int index);
    bool LoadAnimationFile(const String& fullPath);  // returns true if added (not duplicate)
    void ShowLoadAnimationsDialog();
    void HandleDropFile(StringHash eventType, VariantMap& eventData);
    /// Write the currently-loaded animation list (excluding [Bind Pose]) to <model>.anims next to the model.
    void SaveAnimationManifest(const String& absModelPath) const;
    /// Read sibling <model>.anims and load each listed animation. Called after ScanAnimations.
    void LoadAnimationManifest(const String& absModelPath);
    void AddBlendAnimation(int index);
    void RemoveBlendAnimation(unsigned index);
    void RebuildActiveAnimDisplay();

    // Pose capture & animation creation
    struct CapturedPose
    {
        String name;
        struct BoneTransform
        {
            String boneName;
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
        };
        Vector<BoneTransform> bones;
    };
    void CapturePose();
    void CreateNewAnimation();
    void InsertPoseAtTime(int poseIndex);
    void RebuildPoseList();
    Vector<CapturedPose> capturedPoses_;
    ListView* poseListView_{};
    Window* posePanel_{};
    void CreatePosePanel();

    // IK chain auto-detection (Phase 0): topology walk, leaf terminal -> nearest branch.
    // A terminal is a bone with no children (hand/foot/head/fingertip); the chain root is
    // the nearest ancestor that branches (>1 child = limb meets spine), or the root bone.
    struct IKChain
    {
        int effectorBoneIndex_{-1};  ///< leaf bone the effector sits on
        int rootBoneIndex_{-1};      ///< nearest branching ancestor (chain root)
        unsigned length_{0};         ///< segment count from effector up to root
    };
    /// Build IK chains from the active model's skeleton topology. Pure read, no scene change.
    Vector<IKChain> DetectIKChains() const;
    Vector<IKChain> ikChains_;

    // IK posing (Phase 1): wire detected chains to a live IKSolver for drag-to-pose.
    IKSolver* ikSolver_{};                  ///< solver on modelNode_ (owned by the node)
    bool ikPoseMode_{false};                ///< IK posing mode active (toggle: KEY_I)
    int ikSelectedChain_{-1};               ///< chain whose target is being dragged (-1 = none)
    bool ikDragging_{false};                ///< left-drag of the selected target in progress
    Vector3 ikDragPlaneNormal_;             ///< camera-facing plane normal captured at drag start
    Vector<WeakPtr<Node>> ikTargets_;       ///< target node per chain (parallel to ikChains_)
    void SetupIKRig();                      ///< build solver + effectors + targets from ikChains_
    void TeardownIKRig();                   ///< remove solver/effectors/targets, reset IK state
    void SolveIK();                         ///< run the solver (updates bone nodes)
    void ToggleIKPoseMode();                ///< enter/leave IK posing (rebuilds rig on entry)
    int PickIKTarget(int screenX, int screenY) const;  ///< nearest target's chain to a screen point, or -1

    // Bone filter for pose export — only bones in this set get tracks.
    // Empty = all bones (no filter). Shift+click toggles bone + descendants.
    HashSet<int> poseBoneFilter_;
    void ToggleBoneFilter(int boneIndex);
    void ClearBoneFilter();

    // Bone pose editing — rotate selected bone live, capture adjusted pose
    void CreateBoneEditControls();
    void UpdateBoneEditControls();
    void HandleBoneRotationSlider(StringHash eventType, VariantMap& eventData);
    Slider* boneRotX_{};
    Slider* boneRotY_{};
    Slider* boneRotZ_{};
    Text* boneRotXLabel_{};
    Text* boneRotYLabel_{};
    Text* boneRotZLabel_{};
    UIElement* boneEditPanel_{};
    LineEdit* newAnimLengthEdit_{};
    LineEdit* newAnimNameEdit_{};

    // UI
    void CreateUI();
    void CreateMenuBar();
    void CreateInfoPanel();
    void CreatePlaybackPanel();
    void RebuildInfoText();
    void RebuildAnimList();
    UIElement* CreateCollapsibleSection(const String& title, bool startExpanded);
    void AddInfoLine(const String& text, const Color& color = Color(0.85f, 0.85f, 0.85f));
    void AddInfoSeparator();
    void ShowHelpWindow();

    // Camera
    void UpdateCamera(float timeStep);

    // Events
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
    void HandleMouseMove(StringHash eventType, VariantMap& eventData);
    void HandleMouseWheel(StringHash eventType, VariantMap& eventData);
    void HandleFileOpen(StringHash eventType, VariantMap& eventData);
    void HandleAnimSlider(StringHash eventType, VariantMap& eventData);
    void HandleSpeedSlider(StringHash eventType, VariantMap& eventData);
    void HandleAnimSelected(StringHash eventType, VariantMap& eventData);

    // Helpers
    String SemanticName(VertexElementSemantic sem);
    String TypeName(VertexElementType type);

    // Scene
    SharedPtr<Scene> scene_;
    Node* modelNode_{};
    Node* cameraNode_{};
    Node* lightNode_{};

    // Model state
    SharedPtr<Model> currentModel_;
    StaticModel* staticModelComp_{};
    AnimatedModel* animatedModelComp_{};
    AnimationController* animController_{};
    bool isAnimated_{};
    String currentModelPath_;
    String startupModelPath_;  // --path or bare arg: load on startup
    Vector<String> availableAnims_;
    int currentAnimIndex_{-1};
    bool animPlaying_{};

    // Camera
    float cameraDistance_{5.0f};
    float cameraYaw_{45.0f};
    float cameraPitch_{30.0f};
    Vector3 modelCenter_;

    // Debug overlays
    bool showWireframe_{};
    bool showSkeleton_{};
    bool showBoundingBox_{};

    // Bone rotation edit history (undo + dirty detection for partial pose export)
    struct BoneRotEdit
    {
        int boneIndex;
        Quaternion before;
        Quaternion after;
    };
    Vector<BoneRotEdit> boneRotHistory_;
    void PushBoneRotEdit(int boneIndex, const Quaternion& before, const Quaternion& after);
    void UndoBoneRot();
    HashSet<int> GetDirtyBones() const;

    // Pose Studio prop attachment
    void AttachPropToBone(int boneIndex, const String& meshName = "Models/Cylinder.mdl");
    void RemovePropFromBone(int boneIndex);
    void RemoveAllProps();
    void HandlePropOffsetSlider(StringHash eventType, VariantMap& eventData);
    void HandlePropBrowseSelected(StringHash eventType, VariantMap& eventData);
    Slider* propPosX_{};
    Slider* propPosY_{};
    Slider* propPosZ_{};
    Slider* propRotX_{};
    Slider* propRotY_{};
    Slider* propRotZ_{};
    Slider* propScale_{};
    int pendingPropBone_{-1};  // bone index waiting for file selector result

    // Bone rotation gizmo (Pose Studio)
    void DrawBoneGizmo();
    void BeginBoneGizmoDrag(int axis);
    void UpdateBoneGizmoDrag();
    void EndBoneGizmoDrag();
    bool boneGizmoLocal_{true};        // local (bone-relative) vs world axes
    int boneGizmoAxis_{-1};            // 0=X, 1=Y, 2=Z, -1=none
    bool boneGizmoDragging_{false};
    Quaternion boneGizmoRotStart_;      // rotation at drag start
    Vector3 boneGizmoDragStart_;        // arcball start point (on virtual sphere)
    Vector3 boneGizmoDragCurrent_;     // current point on virtual sphere (for arc drawing)
    Vector3 boneGizmoNodeStart_;        // bone world pos at drag start

    // Actual vertex bounding box (computed from vertex data, not stored BB)
    BoundingBox vertexBBox_;

    // REBAKE undo — backup of the model file before rebake
    String rebakeBackupPath_;   // absolute path to the .bak copy
    String rebakeOrigPath_;     // absolute path of the model that was rebaked
    String pendingReloadPath_;  // deferred reload — set by REBAKE, executed next frame

    // Reference model — second animated model in the same scene
    void LoadReferenceModel();
    void HandleRefModelSelected(StringHash eventType, VariantMap& eventData);
    void RemoveReferenceModel();
    void SwapActiveModel();
    Node* refModelNode_{};
    SharedPtr<Model> refModel_;
    AnimatedModel* refAnimatedModelComp_{};
    AnimationController* refAnimController_{};
    bool refIsAnimated_{};
    String refModelPath_;
    Vector<String> refAvailableAnims_;
    int refCurrentAnimIndex_{-1};
    bool refAnimPlaying_{};
    bool editingRef_{};  // true = UI operates on ref model

    // View menu context sensitivity
    DropDownList* viewMenu_{};
    void RefreshViewMenuItems();

    // Status bar
    Text* statusBarText_{};

    // Bone picking & inspector
    int selectedBone_{-1};
    void PickBone(int screenX, int screenY);
    void DrawBoneSubtree(DebugRenderer* debug, const Skeleton& skel, unsigned boneIndex, const Color& color);
    void SelectBone(int boneIndex);
    void UpdateBonePopover();
    void HandleBoneListClick(StringHash eventType, VariantMap& eventData);
    Window* bonePopover_{};
    Vector<Button*> boneButtons_;

    // Vertex editor
    void EnterVertexEditMode();
    void ExitVertexEditMode();
    void DrawVertexOverlay(DebugRenderer* debug);
    void PickVertex(int screenX, int screenY);
    void DeleteSelectedVertex();
    void MoveSelectedVertex(int screenX, int screenY);
    void SaveModel();
    void ExportFBX();
    void SeparateMeshes();
    void HandleExportFBXSelected(StringHash eventType, VariantMap& eventData);
    Vector3 GetVertexWorldPosition(int geomIndex, int vertIndex);
    Vector3 SkinVertex(const unsigned char* vertData, unsigned stride, unsigned posOffset,
        unsigned weightOffset, unsigned indexOffset, bool hasSkinning, unsigned vertIndex,
        const Vector<Bone>& bones, const Matrix3x4& worldTransform);
    void UpdateVertexStatusText();

    bool vertexEditMode_{};
    SharedPtr<Model> editModel_;
    int selectedGeometry_{-1};
    int selectedVertex_{-1};
    bool vertexDirty_{};
    bool vertexDragging_{};
    Vector3 dragStartPos_;
    Vector3 dragPlaneNormal_;
    Node* vertexOverlayNode_{};
    BillboardSet* vertexBillboards_{};

    // Folder browse
    Vector<String> folderModels_;       // absolute paths of .mdl files in scanned folder
    unsigned browseIndex_{};
    String browseFolderPath_;           // absolute path to scanned folder
    HashMap<String, String> folderReview_; // filename -> "keep" or "reject"
    Text* browseStatusText_{};
    bool importMode_{};                 // true when browsing an import DMZ
    String importDMZPath_;              // absolute path to import staging directory

    // UI
    SharedPtr<Font> font_;
    Window* infoPanel_{};
    ScrollView* infoScroll_{};
    UIElement* infoContent_{};
    UIElement* currentSection_{};
    Window* playbackPanel_{};
    ListView* animListView_{};
    Slider* animSlider_{};
    Button* playPauseBtn_{};
    Slider* speedSlider_{};
    Text* animTimeText_{};
    Text* animSpeedText_{};
    Text* statusText_{};
    Text* animNameText_{};
    SharedPtr<FileSelector> fileSelector_;
    Window* helpWindow_{};
    float animSpeed_{1.0f};
    bool suppressAnimSelect_{};
    bool suppressAnimSlider_{};
    bool animLooped_{true};
    bool animReversed_{false};
    CheckBox* loopCheck_{};
    Text* reverseLabel_{};

    // Multi-animation blending
    struct ActiveAnim {
        String name;
        unsigned char layer;
        float weight{1.0f};
        AnimationBlendMode blendMode{ABM_LERP};
    };
    Vector<ActiveAnim> activeAnims_;
    UIElement* activeAnimContainer_{};

    // IPC
    bool StartIPC();
    void StopIPC();
    void PollIPC();
    void HandleIPCMessage(const String& message);
    bool SendIPCResponse(const String& targetRole, const String& message);

    // Text keys
    void CreateTextKeyPanel();
    void RebuildTextKeyList();
    void AddTextKeyAtCurrentTime();
    void DeleteSelectedTextKey();
    void SaveTextKeys();
    void HandleTextKeyEvent(StringHash eventType, VariantMap& eventData);

    // Structured payload editor — each text key carries a VariantMap of typed fields
    struct PayloadFieldRow
    {
        UIElement* row{};                ///< the row UIElement (for removal)
        DropDownList* typeDropDown{};    ///< type selector
        LineEdit* nameEdit{};            ///< human-readable field name (hashed on save); null for Vector elements
        Vector<LineEdit*> valueEdits;    ///< 1-N edits depending on type
        CheckBox* boolCheck{};           ///< for VAR_BOOL
        // Hierarchy support for nested Map/Vector editing
        UIElement* childContainer{};     ///< holds child rows (sibling of row in parent container)
        Vector<PayloadFieldRow> children; ///< child rows for Map/Vector types
        int depth{};                     ///< nesting level (0 = top)
        bool isVectorElement{};          ///< true if this row is a positional element in a parent Vector
        // ResourceRef support — Phase 2
        Text* resourcePathText{};        ///< display of current resource path
        DropDownList* resourceTypeDD{};  ///< resource type selector (Material, Sound, etc.)
        String resourceTypeName;         ///< current resource type name for file filtering
    };

    void AddPayloadFieldRow(int defaultType = 0, const String& name = String::EMPTY, const Variant& value = Variant::EMPTY);
    void AddPayloadFieldRowInternal(UIElement* container, Vector<PayloadFieldRow>& rows, int depth, bool isVectorElem, int defaultType, const String& name, const Variant& value);
    void RemovePayloadFieldRow(unsigned index);
    bool RemovePayloadFieldRowRecursive(Vector<PayloadFieldRow>& rows, UIElement* target);
    PayloadFieldRow* FindPayloadFieldRow(Vector<PayloadFieldRow>& rows, UIElement* target);
    void ClearPayloadFieldRows();
    Variant BuildVariantMapFromRows() const;
    Variant BuildVariantFromRow(const PayloadFieldRow& row) const;
    void PopulateRowsFromVariantMap(const VariantMap& map);
    void PopulateRowsInto(UIElement* container, Vector<PayloadFieldRow>& rows, int depth, const VariantMap& map);
    void PopulateVectorInto(UIElement* container, Vector<PayloadFieldRow>& rows, int depth, const VariantVector& vec);
    void RebuildRowFromType(unsigned rowIndex, int newType);
    void OpenResourceRefBrowser(PayloadFieldRow* row);
    void HandleResourceRefSelected(StringHash eventType, VariantMap& eventData);
    void LoadLabelsForCurrentAnim();  // reads "labels" object from sidecar JSON

    UIElement* textKeyContainer_{};
    Text* textKeyTitle_{};
    UIElement* textKeySection_{};
    Text* textKeyFlash_{};       // flashes key name during playback
    float textKeyFlashTimer_{};
    int selectedTextKey_{-1};
    Vector<Button*> textKeyButtons_;
    LineEdit* textKeyNameEdit_{};
    UIElement* payloadFieldsContainer_{};   ///< holds all PayloadFieldRow rows
    Vector<PayloadFieldRow> payloadRows_;
    /// Maps human-readable field names → hex StringHash strings, restored from sidecar "labels"
    HashMap<StringHash, String> hashToLabel_;
    Text* payloadJsonPreview_{};            ///< live JSON serialization of the current key
    void RefreshJSONPreview();
    UIElement* pendingBrowseRow_{};         ///< row element of the ResourceRef field being browsed

    // Tree view of the selected text key (display-only for now)
    ListView* textKeyTreeView_{};
    void RebuildTextKeyTree();
    void AppendVariantToTree(Text* parentItem, const String& label, const Variant& v, unsigned& index);

    // Schema templates — loaded from bin/Data/TextKeyTemplates/*.json
    void LoadTextKeyTemplates();
    void ApplyTemplate(int templateIndex);
    Vector<TextKeyTemplate> textKeyTemplates_;
    DropDownList* templateDropDown_{};

    // Standalone Text Key Editor window
    void CreateTextKeyEditorWindow();
    void ToggleTextKeyEditorWindow();
    void RebuildTextKeyEditorWindow();
    Window* textKeyEditorWindow_{};
    ListView* editorTreeView_{};   ///< tree view inside the standalone window
    Text* editorTitleText_{};      ///< title text showing current animation name

    // Keyframe editor (Phase 2)
    void CreateKeyframePanel();
    void RebuildTrackList();
    void RebuildKeyframeList();
    void SelectTrack(int index);
    void SelectKeyframe(int index);
    void InsertKeyframeAtCurrentTime();
    void DeleteSelectedKeyframe();
    void ApplyKeyframeEdits();
    void SaveAnimation();

    // Animation Editor — Dopesheet (Phase 5)
    void CreateDopesheetPanel();
    void RebuildDopesheet();
    void SetKeyAtScrubTime();               ///< Set keyframe for selected bone at current scrub time
    void CreateBlankAnimation();             ///< Create empty animation with just a duration
    void HandleDopesheetKeyClick(int boneIdx, int keyIdx);
    void AdjustSelectedKeyTime(float newTime);
    UIElement* dopesheetSection_{};
    ScrollView* dopesheetScroll_{};
    UIElement* dopesheetContent_{};          ///< Inner container with bone rows
    Text* dopesheetTitle_{};
    LineEdit* kfTimeEdit_{};                 ///< Fine time adjustment for selected key
    int dopesheetSelectedBone_{-1};
    int dopesheetSelectedKey_{-1};
    float dopesheetTimeScale_{1.0f};         ///< Pixels per second on dopesheet
    /// Animation Editor Phase 4 — trim the loaded animation to [trimIn, trimOut]
    /// and write a new .ani file via FileSelector. Drops keyframes/text-keys/
    /// triggers outside the range, shifts survivors by -trimIn, sets the new
    /// length. Original animation in the cache is not modified.
    void TrimAndSaveAs();
    void SetTrimInToCurrentTime();
    void SetTrimOutToCurrentTime();
    /// Split the loaded animation at the current scrub time and write two
    /// new .ani files: <name>_a.ani contains [0, splitTime], <name>_b.ani
    /// contains [splitTime, length]. Both ranges include keyframes/text-keys/
    /// triggers; the second file's events shift by -splitTime. FileSelector
    /// picks the destination directory + base name for both files.
    void SplitAtCurrentTime();
    /// Shared backend for Trim and Split: clone `source`, prune everything
    /// outside [in, out], shift survivors by -in, set new length, save to
    /// `destPath`. Returns true on success.
    bool WriteTrimmedAnimation(class Animation* source, float in, float out, const String& destPath);
    LineEdit* trimInEdit_{};
    LineEdit* trimOutEdit_{};

    DropDownList* trackDropDown_{};
    UIElement* keyframeContainer_{};
    Text* keyframeTitle_{};
    UIElement* keyframeSection_{};
    int selectedTrackIndex_{-1};
    int selectedKeyframeIndex_{-1};
    Vector<String> trackNames_;                ///< Bone names with animation tracks
    Vector<Button*> keyframeButtons_;

    // Transform editors for selected keyframe
    LineEdit* kfPosX_{};
    LineEdit* kfPosY_{};
    LineEdit* kfPosZ_{};
    LineEdit* kfRotX_{};
    LineEdit* kfRotY_{};
    LineEdit* kfRotZ_{};
    LineEdit* kfRotW_{};
    LineEdit* kfScaleX_{};
    LineEdit* kfScaleY_{};
    LineEdit* kfScaleZ_{};
    Text* kfTimeText_{};
    bool keyframeDirty_{};

    // Audio capture
    void CreateAudioPanel();
    void HandleRecordToggle(StringHash eventType, VariantMap& eventData);
    void HandleAudioSave(StringHash eventType, VariantMap& eventData);
    void HandleAudioPlay(StringHash eventType, VariantMap& eventData);
    void HandleCaptureDeviceSelected(StringHash eventType, VariantMap& eventData);
    void UpdateAudioPanel(float timeStep);
    /// Play a Sound resource (used for preview and text key playback).
    void PlaySoundPreview(Sound* sound);

    Window* audioPanel_{};
    DropDownList* captureDeviceDD_{};
    Button* recordBtn_{};
    Text* recordBtnText_{};
    Text* captureDurationText_{};
    Button* saveBtn_{};
    Button* playBtn_{};
    bool isRecording_{};
    String lastSavedCapturePath_;
    /// Captured audio stored as a Sound after recording stops.
    SharedPtr<Sound> capturedSound_;
    /// Node with SoundSource for preview playback.
    Node* audioPreviewNode_{};

    // Resource tracking DB
    void InitResourceDB();
    void CloseResourceDB();
    int RegisterResource(const String& path, const String& type);
    void AddDependency(int parentId, int childId);
    int GetResourceId(const String& path);
    Vector<String> GetDependents(const String& path);
    void RegisterModelWithDependencies(const String& mdlPath);
    void UnregisterModelWithOrphans(const String& mdlPath);
    sqlite3* resourceDB_{nullptr};

    // Material Editor
    void OpenMaterialEditor(unsigned geomIndex);
    void CloseMaterialEditor();
    void CreateMaterialEditorWindow(Material* mat, unsigned geomIndex);
    void UpdateMaterialFromSliders();
    void HandleMatSliderChanged(StringHash eventType, VariantMap& eventData);
    void HandleMatTextureBrowse(StringHash eventType, VariantMap& eventData);
    void HandleMatTextureSelected(StringHash eventType, VariantMap& eventData);
    void HandleMatTechSelected(StringHash eventType, VariantMap& eventData);
    void SaveEditedMaterial();
    void ScanTechniques();

    Window* materialEditorWindow_{};
    unsigned materialEditorGeomIndex_{};
    Slider* matDiffR_{};
    Slider* matDiffG_{};
    Slider* matDiffB_{};
    Slider* matDiffA_{};
    Slider* matSpecR_{};
    Slider* matSpecG_{};
    Slider* matSpecB_{};
    Slider* matSpecPower_{};
    Slider* matEmR_{};
    Slider* matEmG_{};
    Slider* matEmB_{};
    LineEdit* matDiffREdit_{};
    LineEdit* matDiffGEdit_{};
    LineEdit* matDiffBEdit_{};
    LineEdit* matDiffAEdit_{};
    LineEdit* matSpecREdit_{};
    LineEdit* matSpecGEdit_{};
    LineEdit* matSpecBEdit_{};
    LineEdit* matSpecPowerEdit_{};
    LineEdit* matEmREdit_{};
    LineEdit* matEmGEdit_{};
    LineEdit* matEmBEdit_{};
    DropDownList* matTechDropdown_{};
    Text* matDiffuseText_{};
    Vector<String> availableTechniques_;
    bool suppressMatSync_{};

    // IPC state
    int ipcListenFd_{-1};
    String ipcSocketPath_;
    String ipcReplyTo_;               // role to send keep/reject responses to
    Vector<int> ipcClientFds_;
};
