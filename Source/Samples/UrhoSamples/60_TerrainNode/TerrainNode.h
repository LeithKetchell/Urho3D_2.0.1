// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Container/HashSet.h>
#include <Urho3D/Math/Plane.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/UI/DropDownList.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Sprite.h>
#include <Urho3D/UI/Menu.h>
#include <Urho3D/UI/Window.h>
#include <Urho3D/UI/DockManager.h>
#include <Urho3D/UI/DockPanel.h>
#include <Urho3D/UI/DockSplit.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/ListView.h>
#include <Urho3D/UI/CheckBox.h>
#include <Urho3D/UI/ScrollView.h>
// HttpRequest removed — Melbourne time computed locally via tzdata
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/UI/FileSelector.h>

#include "Sample.h"
#include "OOFO.h"
#include <Urho3D/Audio/SoundSource3D.h>
#include <Urho3D/Graphics/ParticleEmitter.h>
#include <Urho3D/Graphics/ParticleEffect.h>
#include "Rabbit.h"
#include "Deer.h"
#include "Fox.h"
#include "Wolf.h"
#include "Stag.h"
#include "Bull.h"
#include "Cow.h"
#include "Horse.h"
#include "Donkey.h"
#include "Alpaca.h"
#include "Husky.h"
#include "ShibaInu.h"
#include "CaveMan.h"
#include "CaveWoman.h"
#include "Fish.h"
#include "SchoolFish.h"
#include "FishSpatialHash.h"
#include "LandAnimalSpatialHash.h"
#include "SchoolState.h"
#include "GrassSystem.h"
#include "EcosystemManager.h"
#include "HUD.h"
#include "BuildingSystem.h"
#include "HabitatRules.h"
#include "ResourcePickup.h"
#include "ResourceMap.h"
#include "SoilMap.h"
#include <Urho3D/Graphics/TerrainBrush.h>
#include <Urho3D/Graphics/ProfilerUI.h>
#include <Urho3D/Game/GameDB.h>
#include <Urho3D/Game/CombatResolver.h>
#include <Urho3D/Game/PopulationManager.h>
#include <Urho3D/Scene/DrivenKey.h>

namespace Urho3D
{

class Node;
class Scene;
class ParticleEmitter;
class Slider;

}

/// Weather state for atmospheric effects.
struct WeatherState
{
    float cloudCover{};       // 0.0 clear → 1.0 overcast
    float precipitation{};    // 0.0 none → 1.0 heavy
    float windSpeed{};        // 0.0 calm → 1.0 gale
    float windAngle{};        // radians, world-space direction
    float fogDensity{1.0f};   // multiplier on zone fog distance (lower = thicker)
    float ambientDim{1.0f};   // multiplier on zone ambient color

    WeatherState Lerp(const WeatherState& target, float t) const
    {
        WeatherState result;
        result.cloudCover = cloudCover + (target.cloudCover - cloudCover) * t;
        result.precipitation = precipitation + (target.precipitation - precipitation) * t;
        result.windSpeed = windSpeed + (target.windSpeed - windSpeed) * t;
        result.windAngle = windAngle + (target.windAngle - windAngle) * t;
        result.fogDensity = fogDensity + (target.fogDensity - fogDensity) * t;
        result.ambientDim = ambientDim + (target.ambientDim - ambientDim) * t;
        return result;
    }
};

/// Undo/redo action record.
struct UndoAction
{
    enum Type { NODE_CREATE, NODE_DELETE, TERRAIN_EDIT };
    Type type;
    // Node actions
    unsigned nodeID{};
    String xmlData;
    Vector3 position;
    Quaternion rotation;
    // Terrain actions — full heightmap snapshots
    SharedPtr<Image> beforeHM;
    SharedPtr<Image> afterHM;
};

/// Biome classification for world spawning.
enum BiomeType
{
    BIOME_WATER,        ///< Below water surface
    BIOME_RIVERBANK,    ///< Near water edge, low elevation
    BIOME_GRASSLAND,    ///< Flat terrain, high grass weight
    BIOME_FOREST,       ///< Moderate slope, high vegetation, mid-elevation
    BIOME_MOUNTAIN,     ///< Steep slope or high elevation, rock dominant
    BIOME_DESERT,       ///< Flat, dry terrain with low vegetation
    BIOME_ANY           ///< Fallback — everything can spawn
};

/// Convert BiomeType to gather_sources.terrain string.
String BiomeToString(BiomeType biome);

/// Check if a gather source terrain requirement matches a biome.
bool TerrainMatchesSource(const String& sourceTerrain, BiomeType biome);

/// TerrainNode example with dropdown menus, terrain editing, and celestial day/night cycle.
class TerrainNode : public Sample
{
    URHO3D_OBJECT(TerrainNode, Sample);

public:
    explicit TerrainNode(Context* context);

    void Start() override;
    void Stop() override;
    bool OnEscapePressed() override;

private:
    // --- Login screen ---
    void CreateLoginScene();
    void CreateLoginUI();
    void DestroyLoginUI();
    void EnterWorld();
    /// Called when server's scene finishes async loading — adds local-only visuals.
    void OnGameSceneLoaded(StringHash eventType, VariantMap& eventData);
    /// Create client-only visual entities (lights, skybox, water, celestials, fish, etc.)
    void CreateLocalVisuals();
    void FinishEnterWorld();
    void ReturnToLogin();
    void HandleLoginButton(StringHash eventType, VariantMap& eventData);
    void HandleRegisterButton(StringHash eventType, VariantMap& eventData);
    void HandleOfflineButton(StringHash eventType, VariantMap& eventData);

    Window* loginWindow_{};
    LineEdit* usernameEdit_{};
    LineEdit* passwordEdit_{};
    Button* loginBtn_{};
    Button* registerBtn_{};
    Text* loginStatusText_{};
    String loggedInUsername_;
    String serverSceneName_;  // scene filename received from AuthServer
    bool loggedIn_{false};
    bool asyncSceneLoading_{false};  // true while waiting for async scene load
    SharedPtr<Scene> gameScene_;     // empty scene assigned to connection on connect
    bool gameSceneReady_{false};     // true once gameScene_ is created and assigned
    bool loginTimerActive_{false};   // debug: timing login-to-render
    bool firstFramePending_{false};  // debug: waiting for first rendered frame
    HiresTimer loginTimer_;          // debug: measures login-to-render time
    int adminLevel_{0};       // admin level granted by AuthServer (0 = regular user)
    int ownedPatchX_{};       // first/home patch coordinates (for camera positioning)
    int ownedPatchZ_{};
    Vector<IntVector2> ownedPatches_;  // all patches owned by this user
    bool OwnsThisPatch(int px, int pz) const;
    SharedPtr<Text> loadingText_;    // "Loading..." overlay during async scene load

    void CreateScene();          // calls CreateSceneGraph() + SetupSceneBindings()
    void CreateSceneGraph();     // builds serializable scene objects
    void SetupSceneBindings();   // binds app state — safe after create OR load
    SharedPtr<Scene> pendingScene_;  // deferred scene swap — set by load, applied next frame
    void OnSceneLoaded();
    void CreateInstructions();
    void SetupViewport();
    void SubscribeToEvents();
    void MoveCamera(float timeStep);
    void HandleBeginFrame(StringHash eventType, VariantMap& eventData);
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
    void HandleDrivenKeyOutput(StringHash eventType, VariantMap& eventData);

    // --- Menu bar ---
    void CreateMenuBar();
    DropDownList* CreateMenuDropdown(const String& label, const Vector<String>& items);
    void HandleFileMenu(StringHash eventType, VariantMap& eventData);
    void HandleEditMenu(StringHash eventType, VariantMap& eventData);
    void HandleEnvironmentAction(StringHash eventType, VariantMap& eventData);
    void HandleFishWiggleSlider(StringHash eventType, VariantMap& eventData);
    void HandleFishSpeedSlider(StringHash eventType, VariantMap& eventData);
    void HandleWaterSlider(StringHash eventType, VariantMap& eventData);
    void HandleHabitatSlider(StringHash eventType, VariantMap& eventData);
    void HandleHabitatButton(StringHash eventType, VariantMap& eventData);
    void RespawnSpecies(const String& species);
    void RespawnAllAnimals();
    void HandleMenuButton(StringHash eventType, VariantMap& eventData);
    void HandleCollapsibleToggle(StringHash eventType, VariantMap& eventData);
    void CreateTerrainPanel();
    void ToggleTerrainPanel();
    Menu* CreateMenuItem(UIElement* parent, const String& text, int actionId);
    UIElement* CreateCollapsibleSection(UIElement* parent, Font* font, const String& title, bool expanded);
    SharedPtr<Texture2D> GenerateShapeIcon(int shape);

    UIElement* menuBar_{};
    SharedPtr<DockManager> dockManager_;
    SharedPtr<DropDownList> fileMenu_;
    SharedPtr<DropDownList> editMenu_;
    Menu* viewMenu_{};
    Menu* environmentMenu_{};
    SharedPtr<Window> terrainPanel_;
    Text* fishWiggleLabel_{};
    Text* fishSpeedLabel_{};
    Text* waterHeightLabel_{};
    Text* waterNoiseLabel_{};
    Text* waterFresnelLabel_{};
    Text* waterDepthLabel_{};
    Text* waterRippleLabel_{};
    Text* waterDecayLabel_{};

    // --- AI Tuning panel (admin-only) ---
    SharedPtr<Window> tuningPanel_;
    struct TuningEntry { String key; float value; String label; String category; float minVal; float maxVal; };
    Vector<TuningEntry> tuningEntries_;
    void CreateTuningPanel();
    void PopulateTuningPanel();
    void HandleTuningSliderChanged(StringHash eventType, VariantMap& eventData);
    void HandleTuningResetDefaults(StringHash eventType, VariantMap& eventData);
    void RequestTuningData();

    // --- Patch boundaries ---
    Node* CreatePatchBoundary(Node* oldNode, int patchX, int patchZ, const Color& color);
    void CreateOwnedPatchBoundaries();
    void UpdateCurrentPatchBoundary();
    Vector<WeakPtr<Node>> ownedPatchBoundaries_;
    WeakPtr<Node> currentPatchBoundary_;
    int currentPatchX_{0x7FFFFFFF};  // force first update
    int currentPatchZ_{0x7FFFFFFF};

    // --- Camera transition (edit mode) ---
    void BeginEditTransition();   // chase/FP → god cam for editing
    void EndEditTransition();     // god cam → back to previous mode
    void UpdateCameraTransition(float timeStep);
    bool cameraTransitioning_{false};
    bool transitionToEdit_{true};  // true=entering edit, false=leaving
    float transitionTime_{0.0f};
    float transitionDuration_{0.75f};
    Vector3 transitionStartPos_;
    Vector3 transitionEndPos_;
    Quaternion transitionStartRot_;
    Quaternion transitionEndRot_;
    CameraMode preEditCameraMode_{CAM_GOD};  // mode to restore after editing

    // --- Terrain editing ---
    void ApplyBrush(const Vector3& worldPos, float timeStep);
    void ApplyLowerBrush(const Vector3& worldPos, float timeStep);
    void DrawBrushOutline(const Vector3& worldPos);

    SharedPtr<TerrainBrush> brush_;
    SharedPtr<Image> editableHeightMap_;
    SharedPtr<Image> waterMap_;
    SharedPtr<Texture2D> waterMapTex_;

    // Weather Phase 4: rainfall accumulation (reduced resolution)
    // R = surface water (0-255), G = soil moisture (0-255)
    SharedPtr<Image> rainfallMap_;
    int rainfallFrameCounter_{0};
    static constexpr int RAINFALL_MAP_SIZE = 256;
    static constexpr int RAINFALL_UPDATE_INTERVAL = 30;
    void UpdateRainfallAccumulation();
    int brushMode_{0};         // 0=off, 1=raise, 2=lower, 3=smooth, 4=flatten, 5=erosion, 6=river
    int brushShape_{0};        // 0=circle, 1=square, 2=triangle, 3=star, 4=pentagon, 5=hexagon, 6=octagon
    float brushRotation_{0.0f}; // degrees
    float brushRadius_{5.0f};
    float brushStrength_{0.05f};
    float smoothStrength_{0.3f};
    Vector3 cachedBrushHit_;
    Vector3 cachedBrushNormal_{Vector3::UP};
    bool hasBrushHit_{false};
    Text* brushSizeLabel_{};
    Text* brushStrLabel_{};
    Text* brushRotLabel_{};
    LineEdit* brushRotEdit_{};
    void HandleBrushRotEdit(StringHash eventType, VariantMap& eventData);
    float lockedFlattenHeight_{-1.0f};  // locked on first click, reset on release
    BorderImage* shapeIcons_[7]{};     // shape icon images for rotation updates
    Button* activeModeBtn_{};           // currently selected mode button
    Button* activeShapeBtn_{};          // currently selected shape button
    void HighlightBrushButton(Button*& active, Button* btn);

    // --- Weather system ---
    void UpdateWeather(float timeStep);
    void ApplyWeatherToScene();
    WeatherState CalculateSeasonalWeather();
    void HandleAudioSlider(StringHash eventType, VariantMap& eventData);
    void HandleWeatherSlider(StringHash eventType, VariantMap& eventData);
    void HandleHemisphereToggle(StringHash eventType, VariantMap& eventData);

    // --- Celestial bodies ---
    void CreateCelestialBodies();
    void UpdateCelestialBodies(float timeStep);
    void UpdateAtmosphere(float sunAltitude);
    void UpdateSeasonalEffects();
    float CalculateSunAltitude();
    float CalculateSunAzimuth(float altitude);
    float CalculateMoonAltitude();
    float CalculateMoonAzimuth(float moonAlt);
    Vector3 AltAzToFlatEarth(float altitude, float azimuth, float distance = 500.0f);

    WeakPtr<Node> sunNode_;
    WeakPtr<Node> moonNode_;
    WeakPtr<Node> moonShadowNode_;   ///< Earth shadow disc — child of moonNode_
    SharedPtr<Material> sunMat_;
    SharedPtr<Material> moonMat_;
    SharedPtr<Material> moonShadowMat_;  ///< Dark feathered disc material
    float sunOcclusionFade_{1.0f};
    float moonOcclusionFade_{1.0f};
    bool cachedSunOccluded_{false};
    bool cachedMoonOccluded_{false};
    unsigned occlusionFrameSkip_{0};
    unsigned contextHintFrameSkip_{0};
    unsigned fishHashFrameSkip_{0};
    float cloudAngle_{};
    WeakPtr<Light> sunLight_;
    WeakPtr<Light> moonLight_;
    SharedPtr<Material> skyboxMat_;

    // --- Melbourne time (computed locally via tzdata) ---
    void SyncMelbourneTime();
    void HandleTimeOfDaySlider(StringHash eventType, VariantMap& eventData);
    void HandleDateOffsetSlider(StringHash eventType, VariantMap& eventData);
    void ApplyDateOffsets();
    float timeOfDay_{};
    float timeOfDayOffset_{};  // manual offset from Melbourne time (-12..+12 hours)
    int dayOfYear_{};
    int baseDayOfYear_{};      // from Melbourne time, before offsets
    float baseMoonAge_{};      // from Melbourne time, before offsets
    float moonAge_{};
    bool lunarEclipseActive_{false};  ///< Server says blood moon is happening
    float daySliderOffset_{};    // ±15 days
    float monthSliderOffset_{};  // ±6 months (in days)
    float yearSliderOffset_{};   // ±15 years (in days)
    float timeSyncTimer_{};
    Text* todLabel_{};
    Text* dayOffsetLabel_{};
    Text* monthOffsetLabel_{};
    Text* yearOffsetLabel_{};

    // --- Cached celestial calculations ---
    float cachedSunAlt_{};
    float cachedSunAz_{};
    float cachedMoonAlt_{};
    float cachedMoonAz_{};

    // --- Selection ---
    void SelectNode(Node* node);
    void DeselectNode();
    WeakPtr<Node> selectedNode_;
    Vector<SharedPtr<Material>> originalMaterials_;

    // --- Radial Command UI (replaces possession) ---
    SharedPtr<class RadialCommandUI> radialUI_;

    // --- NPC Ring UI (health/stat overlay above NPC heads) ---
    SharedPtr<class NPCRingUI> npcRingUI_;
    void InitRadialCommands();
    void HandleRadialCommand(StringHash eventType, VariantMap& eventData);

    // --- Undo/Redo ---
    void PushUndo(const UndoAction& action);
    void Undo();
    void Redo();
    void BeginTerrainStroke();
    void EndTerrainStroke();
    String SerializeNode(Node* node);
    SharedPtr<Image> CloneHeightMap();
    void RestoreHeightMap(Image* src);
    Vector<UndoAction> undoStack_;
    int undoCursor_{0};
    bool terrainStrokeActive_{false};
    SharedPtr<Image> terrainStrokeBefore_;

    // --- Transform gizmo ---
    void DrawGizmo();
    void BeginGizmoDrag(int axis);
    void UpdateGizmoDrag();
    void EndGizmoDrag();
    void WakeSelectedNode();
    int gizmoMode_{0};        // 0=none, 1=translate, 2=rotate, 3=scale
    bool gizmoLocal_{false};  // false=world, true=local
    bool gizmoDragging_{false};
    int gizmoAxis_{-1};       // 0=X, 1=Y, 2=Z
    Vector3 gizmoDragStart_;  // mouse ray hit at drag start
    Vector3 gizmoNodeStart_;  // node position at drag start
    Quaternion gizmoRotStart_; // node rotation at drag start
    Vector3 gizmoScaleStart_; // node scale at drag start

    // --- Import / Generate ---
    void ShowImportModelDialog();
    void HandleImportModelChosen(StringHash eventType, VariantMap& eventData);
    void ShowGenerateMeshPanel();
    void HandleGenerateMesh(StringHash eventType, VariantMap& eventData);
    void HandleMeshShapeChanged(StringHash eventType, VariantMap& eventData);
    SharedPtr<Window> generateMeshPanel_;
    DropDownList* meshShapeList_{};
    Slider* meshParam1Slider_{};
    Slider* meshParam2Slider_{};
    Slider* meshParam3Slider_{};
    Text* meshParam1Label_{};
    Text* meshParam2Label_{};
    Text* meshParam3Label_{};
    DropDownList* meshMaterialList_{};
    LineEdit* meshNameEdit_{};

    // --- Hierarchy window ---
    void ToggleHierarchyWindow();
    void BuildHierarchyTree();
    void PopulateHierarchy(Node* node, Text* parentItem, unsigned& index);
    void HandleHierarchySelectionChanged(StringHash eventType, VariantMap& eventData);
    void HandleHierarchyDoubleClick(StringHash eventType, VariantMap& eventData);
    void HighlightInHierarchy(Node* node);
    SharedPtr<Window> hierarchyWindow_;
    ListView* hierarchyList_{};

    // --- Inspector window ---
    void ToggleInspectorWindow();
    void RebuildInspector();
    void CreateNodeSection(Node* node);
    void CreateComponentSection(Component* component, unsigned compIndex);
    LineEdit* CreateVec3Row(UIElement* parent, const String& label, const Vector3& value, const String& tag);
    void HandleInspectorTransformEdit(StringHash eventType, VariantMap& eventData);
    void HandleInspectorAttributeEdit(StringHash eventType, VariantMap& eventData);
    void HandleInspectorCheckToggle(StringHash eventType, VariantMap& eventData);
    void HandleInspectorEnumSelect(StringHash eventType, VariantMap& eventData);
    SharedPtr<Window> inspectorWindow_;
    ScrollView* inspectorScroll_{};
    UIElement* inspectorContent_{};

    // --- Creature Inspect HUD ---
    void ShowInspectPanel(Creature* creature);
    void HideInspectPanel();
    void UpdateInspectPanel();
    SharedPtr<Window> inspectPanel_;
    Text* inspectNameText_{};
    BorderImage* inspectHpBar_{};
    BorderImage* inspectHungerBar_{};
    BorderImage* inspectThirstBar_{};
    BorderImage* inspectWarmthBar_{};
    BorderImage* inspectStaminaBar_{};
    Text* inspectStateText_{};
    WeakPtr<Node> inspectedNode_;

    // --- Debug Log window ---
    void CreateDebugLogWindow();
    void ToggleDebugLogWindow();
    void HandleLogMessage(StringHash eventType, VariantMap& eventData);
    SharedPtr<Window> debugLogWindow_;
    ListView* debugLogList_{};
    static const unsigned MAX_DEBUG_LOG_LINES = 500;

    // --- Font / Theme ---
    void LoadThemePrefs();
    void SaveThemePrefs();
    void ApplyFont(const String& fontName, int fontSize);
    void HandleSettingsMenu(StringHash eventType, VariantMap& eventData);
    SharedPtr<Font> font_;
    String currentFontName_{"Anonymous Pro"};
    int currentFontSize_{11};
    Vector<String> availableFonts_;
    SharedPtr<DropDownList> settingsMenu_;

    // --- State ---
    Text* instructionText_{};
    Text* cameraCoordsText_{};
    Text* clockText_{};
    bool menuOpen_{false};
    int heightFogOverride_{0};  // 0=auto (time-based), 1=forced on, -1=forced off
    bool godRaysEnabled_{true};
    bool shadowsEnabled_{true};
    bool waterReflectionEnabled_{true};
    bool postProcessEnabled_{true};
    bool hemisphereEnabled_{true};

    // --- Weather state ---
    WeatherState weather_;              // current interpolated state
    WeatherState weatherTarget_;        // what we're transitioning toward
    float weatherTransitionTimer_{};    // seconds remaining in current transition
    float weatherTransitionDuration_{60.0f}; // total transition time (game-seconds)
    float weatherCheckTimer_{};         // countdown to next weather re-evaluation
    float weatherCheckInterval_{300.0f}; // re-evaluate every 5 game-minutes
    bool weatherOverride_{false};       // true = manual slider control, false = automatic
    Text* cloudCoverLabel_{};
    Text* precipLabel_{};
    Text* rainLabel_{};
    float rainOverride_{-1.0f};  // -1 = auto (follows precipitation), 0..1 = manual override

    // --- Drought visual system (client-derived from weather) ---
    float droughtSeverity_{0.0f};   // 0.0 = normal, 1.0 = severe drought
    float baseWaterY_{5.0f};        // original water surface height
    bool droughtWarningShown_{false};
    WeakPtr<Node> dustEmitterNode_;
    void UpdateDroughtVisuals(float timeStep);

    // --- Cached seasonal state (only recomputed when dayOfYear_ changes) ---
    int lastSeasonDay_{-1};
    float cachedSeasonFactor_{};
    Color cachedSeasonBias_;
    ClimateInfo cachedClimateDay_{15.0f, 2.0f, 3.0f};    ///< DB climate for current season, daytime
    ClimateInfo cachedClimateNight_{8.0f, 3.0f, 4.0f};   ///< DB climate for current season, nighttime
    void UpdateCachedClimate();
    float cachedFogStart_{500.0f};
    float cachedFogEnd_{750.0f};
    Color cachedTerrainTint_{Color::WHITE};
    Color cachedShallowColor_;
    Color cachedDeepColor_;
    // Seasonal skybox cubemaps: spring=0, summer=1, autumn=2, winter=3
    SharedPtr<TextureCube> seasonSkyboxes_[4];
    int lastSeasonIndex_{-1};
    // Weather skybox cubemaps: clear, overcast, storm
    SharedPtr<TextureCube> weatherSkyboxes_[3]; // 0=clear, 1=overcast, 2=storm

    // --- Weather system (Phase 1: cloud density sampling) ---
    float SampleCloudDensity(const Vector3& worldPos);
    void UpdateLocalWeather(float timeStep);
    SharedPtr<Image> cloudFaces_[6];   // BrightDay1 cubemap faces for CPU sampling
    float localCloudDensity_{};        // 0.0 = clear, 1.0 = thick cloud

    // --- Lightning ---
    void UpdateLightning(float timeStep);
    float lightningTimer_{};           // countdown to next strike chance
    float lightningIntensity_{};       // current flash brightness (0=off, 1=peak)
    int lightningFlickerCount_{};      // remaining flickers in current strike
    float lightningFlickerPhase_{};    // time within current flicker on/off cycle
    bool lightningFlickerOn_{};        // current flicker state
    float lightningFadeTimer_{};       // post-flash fade duration
    float lightningAfterDark_{};       // pupil contraction darkening (0=none, peaks ~0.15)

    // --- Heightmap I/O ---
    void ShowExportPrefabDialog();
    void HandlePrefabExportChosen(StringHash eventType, VariantMap& eventData);
    void ShowLoadPrefabDialog();
    void HandlePrefabLoadChosen(StringHash eventType, VariantMap& eventData);
    WeakPtr<Node> prefabBrush_;   // loaded prefab template, cloned on each instance
    void HandleRigidBodySleep(StringHash eventType, VariantMap& eventData);
    Text* prefabBrushLabel_{};      // "Object Brush: X" label in File menu
    void UpdatePrefabBrushLabel();
    void ShowSaveSceneDialog();
    void ShowLoadSceneDialog();
    void HandleSceneSaveChosen(StringHash eventType, VariantMap& eventData);
    void HandleSceneLoadChosen(StringHash eventType, VariantMap& eventData);
    void ShowSaveHeightmapDialog();
    void ShowLoadHeightmapDialog();
    void HandleHeightmapSaveChosen(StringHash eventType, VariantMap& eventData);
    void HandleHeightmapLoadChosen(StringHash eventType, VariantMap& eventData);
    void HandleFileSelectorCancel(StringHash eventType, VariantMap& eventData);
    void SaveHeightmapToFile(const String& path);
    void LoadHeightmapFromFile(const String& path);
    SharedPtr<FileSelector> fileSelector_;

    // --- Minimap ---
    void CreateMinimap();
    void UpdateMinimapCamera();
    void UpdateMinimapBlips();
    void RefreshMinimap();
    Sprite* minimap_{};
    Sprite* minimapCameraDot_{};

    // Floating vitals panel — tracks selected creature in screen space
    void CreateSelectedVitalsPanel();
    void UpdateSelectedVitalsPanel();
    UIElement* vitalsPanel_{};
    BorderImage* vitalHungerBar_{};
    BorderImage* vitalThirstBar_{};
    BorderImage* vitalStaminaBar_{};
    BorderImage* vitalWarmthBar_{};
    Vector<Sprite*> minimapBlips_;
    unsigned minimapBlipUsed_{0};
    SharedPtr<Texture2D> minimapTex_;
    WeakPtr<Node> minimapCameraNode_;

    // --- Erosion ---
    void RunErosion(int iterations);
    void WakeSleepingBodiesOnTerrain();
    void HandleErosionSlider(StringHash eventType, VariantMap& eventData);
    int erosionIterations_{50};
    float erosionRainfall_{0.012f};
    float erosionStrength_{0.3f};  // Ks (erosion rate)
    float erosionMaxDepth_{0.5f};  // max carve depth (fraction of original height)
    float erosionMinHeight_{0.0f}; // absolute minimum height floor [0..1]
    float erosionRidgeProtect_{0.0f}; // ridge protection strength [0..1]
    int erosionBorderPad_{4};      // no-erode border in cells
    Text* erosionIterLabel_{};
    Text* erosionRainLabel_{};
    Text* erosionStrLabel_{};
    Text* erosionDepthLabel_{};
    Text* erosionFloorLabel_{};
    Text* erosionRidgeLabel_{};
    Text* erosionBorderLabel_{};

    // --- Animals ---
    void CreateAnimals();
    Vector<WeakPtr<Node>> animalNodes_;
    /// Replicated creature components — driven manually since LogicComponent
    /// event subscriptions don't work for components on replicated nodes.
    Vector<WeakPtr<Creature>> replicatedCreatures_;

    // --- Loose Resources ---
    void CreateResourceMap();
    void UpdateResourceStreaming();
    void TryPickupAtCursor(const Ray& pickRay);
    void HandleResourceDepleted(MemoryBuffer& msg);

    // --- Rain particles ---
    void CreateRain();
    void UpdateRain(float timeStep);
    WeakPtr<Node> rainNode_;
    WeakPtr<ParticleEmitter> rainEmitter_;
    SharedPtr<ParticleEffect> rainEffect_;

    // --- Snow particles ---
    void CreateSnow();
    void UpdateSnow(float timeStep);
    float CalculateTemperature() const;
    float CalculateEffectiveTemperature() const;
    WeakPtr<Node> snowNode_;
    WeakPtr<ParticleEmitter> snowEmitter_;
    SharedPtr<ParticleEffect> snowEffect_;

    // --- Ecosystem ---
    void CreateEcosystem();
    // CreateTrees removed — trees are server-authoritative (see InitTreeModels + HandleSpawnTree)
    SharedPtr<EcosystemManager> ecosystem_;
    SharedPtr<ResourceMap> resourceMap_;

    // --- Trees (server-authoritative, client caches models) ---
    void InitTreeModels();
    void UpdateTreeSeason(float seasonAngle);
    void UpdateDecorSeason(float seasonAngle);
    void UpdateTreeLOD();
    static constexpr int NUM_TREE_SPECIES = 6;  // 0=oak,1=pine,2=eucalyptus,3=acacia,4=willow,5=sheoak
    SharedPtr<Model> treeModel_[NUM_TREE_SPECIES];
    SharedPtr<Model> treeModelLOD1_[NUM_TREE_SPECIES];
    SharedPtr<Material> treeBarkMat_[NUM_TREE_SPECIES];
    SharedPtr<Material> treeLeafMat_[NUM_TREE_SPECIES];
    SharedPtr<Material> treeImposterMat_[NUM_TREE_SPECIES];
    SharedPtr<Model> treeImposterQuad_;
    Color treeBaseLeafColor_[NUM_TREE_SPECIES];
    bool treeEvergreen_[NUM_TREE_SPECIES]{};
    bool treeModelsReady_{false};
    float treeLodTimer_{0.0f};
    /// Active resource pickup nodes spawned by streaming LOD.
    HashMap<unsigned, WeakPtr<Node>> activePickupNodes_;
    float resourceStreamTimer_{};

    // --- Decoration Scatter (static Nature/ models placed by biome) ---
    enum DecorCategory
    {
        DECOR_BUSH = 0,
        DECOR_FLOWER,
        DECOR_MUSHROOM,
        DECOR_STUMP,
        DECOR_ROCK,
        DECOR_FERN,
        DECOR_GRASS,         // tall grass patches
        DECOR_GROUNDPLANT,   // small ground-hugging plants
        DECOR_SAPLING,       // small decorative trees (KayKit Pine/Willow)
        DECOR_CACTUS,        // desert cacti and flowering cacti
        DECOR_PALM,          // palm trees for arid/desert edges
        DECOR_DEADTREE,      // dead/dried trees for desert and barren terrain
        NUM_DECOR_CATEGORIES
    };
    void InitDecorModels();
    void UpdateDecorScatter();
    struct DecorEntry
    {
        SharedPtr<Model> model;
        SharedPtr<Material> material;
        String matListPath;  // if non-empty, use ApplyMaterialList instead of single material
        float baseScale{1.0f};
    };
    Vector<DecorEntry> decorModels_[NUM_DECOR_CATEGORIES];
    HashMap<unsigned, WeakPtr<Node>> activeDecorNodes_;
    bool decorModelsReady_{false};
    float decorScatterTimer_{0.0f};

    // --- Water Edge Scatter (Pond/ models at waterline, streamed by camera) ---
    enum WaterEdgeCategory
    {
        WEDGE_CATTAIL = 0,   // at waterline — tall reeds
        WEDGE_LILYPAD,       // on water surface
        WEDGE_BANKPLANT,     // small plants on bank (mint, calla, grass)
        WEDGE_FROG,          // bank wildlife
        WEDGE_ROCK,          // bank pebbles and rocks
        WEDGE_BIRD,          // perching birds on bank
        WEDGE_DRAGONFLY,     // hovering dragonflies near waterline
        WEDGE_BRANCH,        // driftwood branches on bank
        NUM_WEDGE_CATEGORIES
    };
    void InitWaterEdgeModels();
    void UpdateWaterEdgeScatter();
    Vector<DecorEntry> waterEdgeModels_[NUM_WEDGE_CATEGORIES];
    HashMap<unsigned, WeakPtr<Node>> activeWaterEdgeNodes_;
    bool waterEdgeModelsReady_{false};
    float waterEdgeScatterTimer_{0.0f};

    // --- Grass (GPU-driven) ---
    void CreateGrass();
    SharedPtr<GrassSystem> grassSystem_;

    // --- Water ripples ---
    SharedPtr<class WaterRippleSystem> rippleSystem_;

    // --- Soundscape ---
    WeakPtr<class Soundscape> soundscape_;

    // --- Metal deposits ---
    WeakPtr<class MetalDeposits> metalDeposits_;

    // --- Soil map (Plan 9) ---
    WeakPtr<SoilMap> soilMap_;
    /// Query soil type at world position. Returns SOIL_ROCK if no map loaded.
    SoilType GetSoilType(const Vector3& worldPos) const;
    /// Query fertility (0-255) at world position.
    unsigned char GetFertility(const Vector3& worldPos) const;
    /// Query mineral density (0-255) at world position.
    unsigned char GetMineralDensity(const Vector3& worldPos) const;

    // --- Fish ---
    void CreateFish();
    void CreateSchoolFish();
    void RebuildFishSpatialHash();
    FishSpatialHash fishSpatialHash_;

    void RebuildLandAnimalSpatialHash();
    LandAnimalSpatialHash landAnimalHash_;
    Vector<WeakPtr<Node>> fishNodes_;
    SchoolStateCache schoolStateCache_;
    /// Per-school population state (indexed by school ID).
    Vector<FishPopulationState> schoolPopStates_;

    /// Server-provided fish spawn points (from water body analysis).
    struct FishSpawnInfo { float x, z, depth; };
    Vector<FishSpawnInfo> serverFishSpawns_;
    int frameNumber_{0};

    // --- Settlement Patch Ownership ---
    HashMap<unsigned long long, unsigned> patchClaims_;  // (sx<<16|sz) → settlement ID
    Vector<WeakPtr<Node>> patchOverlayNodes_;
    bool showTerritoryOverlay_{false};
    void HandleSettlementClaims(MemoryBuffer& msg);
    void HandlePatchResult(MemoryBuffer& msg);
    void SendPatchClaim(int patchX, int patchZ);
    void UpdateTerritoryOverlay();
    /// Get a unique color for a settlement ID.
    Color SettlementColor(unsigned settlementId);

    // --- Campfire ---
    void CreateCampfire();
    WeakPtr<Node> campfireNode_;
    WeakPtr<Light> campfireLight_;
    float fireBrightnessTarget_{1.5f};
    float fireBrightnessCurrent_{1.5f};
    Color fireColorTarget_{1.0f, 0.6f, 0.2f};
    Color fireColorCurrent_{1.0f, 0.6f, 0.2f};
    float fireFadeTime_{0.3f};
    float fireFadeTimer_{0.0f};
    bool campfireRayVisible_{false};
    WeakPtr<ParticleEmitter> campfireFireEmitter_;
    WeakPtr<ParticleEmitter> campfireSmokeEmitter_;
    WeakPtr<ParticleEmitter> campfireEmbersEmitter_;
    float embersPhase_{0.0f};  // pulsation phase [0..2π]
    float embersTimeRemaining_{0.0f};  // offline embers countdown before COLD (seconds)
    static constexpr float EMBERS_DURATION = 120.0f;  // how long embers last before going cold
    // Fuel system — real wallclock seconds, immune to day-cycle scrub
    float fuelSeconds_{270.0f};      // current remaining fuel (≈3 sticks at startup)
    float maxFuelSeconds_{1800.0f};  // capacity cap (~30 min of stored burn)
    float fireIntensity_{1.0f};      // derived [0..1], drives visuals
    // Cached "max" baseline values, captured at CreateCampfire() and modulated by intensity each frame
    float cfBaseFireRateMin_{0.0f};
    float cfBaseFireRateMax_{0.0f};
    float cfBaseSmokeRateMin_{0.0f};
    float cfBaseSmokeRateMax_{0.0f};
    float cfBaseLightBrightness_{1.5f};
    float cfBaseLightRange_{5.0f};
    bool fireOut_{false};            // latched true when fuel runs out, false on relight
    // Fire System Phase 3 — server-authoritative pit state. When set, the
    // local Campfire mirrors server burnUnits + burnRate (extrapolating between
    // E_PIT_STATE_CHANGED broadcasts). Local UpdateCampfireFuel still runs but
    // serves as the extrapolation step; broadcasts snap us back to truth.
    unsigned activeFirePitId_{0};
    float activeFirePitBurnRate_{1.0f};
    float activeFirePitDist_{999.0f};       // XZ distance to adopted pit (for nearest-pit switching)
    unsigned char activeFirePitState_{0};   // mirrors server FirePitState (0=UNLIT,1=LIT,2=EMBERS,3=COLD)
    unsigned char activeSignalType_{0};    // Phase 24: 0=none, 1=danger, 2=trade
    WeakPtr<Node> signalFireNode_;         // Phase 24: particle effect node on watchtower
    DrivenKey burnCurveKey_;  // Non-linear burn rate — matches server's campfire_burn.json
    float activeFirePitWetness_{0.0f};
    void HandlePitStateChanged(StringHash eventType, VariantMap& eventData);
    void UpdateCampfireFuel(float realTimeStep);
    void TryCampfireInteract();
    void TryPlantCrop();
    void TryHarvestCrop();
    // Phase 4a: friction ignition client-side tracking
    bool ignitionActive_{false};     // server confirmed ignition is running
    float ignitionProgress_{0.0f};   // 0.0-1.0 progress (from server status events)
    void HandlePitIgnitionStatus(StringHash eventType, VariantMap& eventData);
    // Woodpile server sync — receive authoritative pile state from server
    void HandleWoodpileState(StringHash eventType, VariantMap& eventData);
    // Fuel burn durations (real seconds) — loaded from GameDB fuel_types table.
    // fuel_value is a multiplier: burn_seconds = fuel_value * 3600 / fire_rules.fuel_per_hour.
    float stickBurnSeconds_{90.0f};       // item 2 (Stick) — fallback
    float charcoalBurnSeconds_{600.0f};   // item 43 (Charcoal) — fallback
    FireRules fireRules_;                 // cached from DB
    void HandleCampfireSlider(StringHash eventType, VariantMap& eventData);
    void HandleCampfireSettingsChanged(StringHash eventType, VariantMap& eventData);
    void HandleAnimationTextKey(StringHash eventType, VariantMap& eventData);
    Text* cfFireRateLabel_{};
    Text* cfFireSizeLabel_{};
    Text* cfSmokeRateLabel_{};
    Text* cfSmokeSizeLabel_{};
    Text* cfLightRangeLabel_{};
    Text* cfBrightnessLabel_{};
    Text* cfVelocityLabel_{};
    Text* cfUpdraftLabel_{};
    Text* cfLifetimeLabel_{};
    Text* cfSmokeEmitSizeLabel_{};
    Text* cfSmokeGrowLabel_{};
    Text* cfSmokeLifeLabel_{};
    Text* cfSmokeRiseLabel_{};
    Text* cfSmokeDampLabel_{};
    // Slider pointers for syncing UI from loaded scene state
    HashMap<int, Slider*> campfireSliders_;
    void SyncCampfireUI();

    // --- OOFO fleet ---
    void CreateOOFOs();
    void UpdateOOFOs(float timeStep);
    static const int NUM_OOFOS = 3;
    Vector<SharedPtr<OOFO>> oofos_;
    Vector<Vector3> oofoCloudPositions_;
    float oofoSpawnTimers_[3]{};  // countdown per OOFO before spawning
    int oofosSpawned_{0};
    bool oofoRayVisible_{false};
    bool landAnimalRayVisible_{false};
    bool waterAnimalRayVisible_{false};
    // Per-species ray toggles
    bool rabbitRayVisible_{false};
    bool deerRayVisible_{false};
    bool foxRayVisible_{false};
    bool wolfRayVisible_{false};
    bool stagRayVisible_{false};
    bool bullRayVisible_{false};
    bool cowRayVisible_{false};
    bool horseRayVisible_{false};
    bool donkeyRayVisible_{false};
    bool alpacaRayVisible_{false};
    bool huskyRayVisible_{false};
    bool shibaInuRayVisible_{false};
    bool caveManRayVisible_{false};
    bool caveWomanRayVisible_{false};
    bool fishRayVisible_{false};
    bool schoolFishRayVisible_{false};
    bool fireRayVisible_{false};
    bool grassRayVisible_{false};
    // Per-species visibility toggles (default all visible)
    bool rabbitVisible_{true};
    bool deerVisible_{true};
    bool foxVisible_{true};
    bool wolfVisible_{true};
    bool stagVisible_{true};
    bool bullVisible_{true};
    bool cowVisible_{true};
    bool horseVisible_{true};
    bool donkeyVisible_{true};
    bool alpacaVisible_{true};
    bool huskyVisible_{true};
    bool shibaInuVisible_{true};
    bool caveManVisible_{true};
    bool caveWomanVisible_{true};

    // --- AuthServer discovery & connection ---
    void DiscoverAuthServer();
    void HandleHostDiscovered(StringHash eventType, VariantMap& eventData);
    void HandleServerConnected(StringHash eventType, VariantMap& eventData);
    void HandleServerDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleConnectFailed(StringHash eventType, VariantMap& eventData);
    void HandleAuthMessage(StringHash eventType, VariantMap& eventData);
    void HandleAsyncLoadFinished(StringHash eventType, VariantMap& eventData);
    void HandleAuthConnectButton(StringHash eventType, VariantMap& eventData);
    void UpdateAuthButtonState();
    String authServerAddress_{"127.0.0.1"};
    unsigned short authServerPort_{63697};  // Game server (cosmic prime)
    float discoveryTimer_{0.0f};
    float discoveryInterval_{3.0f};  // retry every 3 seconds
    bool authConnected_{false};
    bool authDiscovering_{true};
    Window* networkPanel_{};
    Button* authConnectBtn_{};
    Text* authBtnLabel_{};
    Text* networkStatusText_{};
    /// Pending registration credentials (sent after plain DH connect completes).
    String pendingRegisterUsername_;
    String pendingRegisterPassword_;

    // --- Peer offload (NAT punchthrough) ---
    void HandlePeerConnected(StringHash eventType, VariantMap& eventData);
    void HandleNATPunchFailed(StringHash eventType, VariantMap& eventData);
    void HandlePeerDisconnected(StringHash eventType, VariantMap& eventData);
    void RegisterGuidWithAuthServer();
    void RelayToAuth(int innerMsgID, const VectorBuffer& innerPayload);

    bool isPeered_{false};
    bool isSubServer_{false};
    String peerGuid_;
    Vector<unsigned char> peerToken_;
    int peerPatchX_{0};
    int peerPatchZ_{0};
    SharedPtr<Connection> peerConnection_;

    // --- Server-authoritative edits ---
    void SendTerrainSync();
    unsigned terrainVersion_{0};
    void SendWaterEdit(const Vector3& worldPos);
    void SendTerrainEdit(const Vector3& worldPos, float timeStep);
    void SendObjectCreate(Node* node, const Vector3& position, const Vector3& surfaceNormal);
    void SendObjectDelete(unsigned nodeID);
    void SendObjectTransform(unsigned nodeID, const Vector3& pos, const Quaternion& rot, const Vector3& scale);
    void SendEditMessage(int msgID, const VectorBuffer& payload);
    void HandleEditReject(MemoryBuffer& msg);
    void HandleEditBroadcast(MemoryBuffer& msg);
    void HandleResourcePatch(MemoryBuffer& msg);
    void HandleNewTerrain(MemoryBuffer& msg);
    void SendPatchPosition(int patchX, int patchZ);
    int lastReportedPatchX_{0x7FFFFFFF};
    int lastReportedPatchZ_{0x7FFFFFFF};

    /// Terrain edit snapshot for rollback — stores small heightmap region keyed by editID
    struct TerrainEditSnapshot
    {
        IntVector2 regionMin;   // top-left in heightmap coords
        IntVector2 regionSize;  // width x height of snapshotted region
        Vector<unsigned char> heightData;  // raw pixel data of the region before edit
        Vector<unsigned char> waterData;   // water map region snapshot (mode 6 only)
    };
    unsigned nextEditID_{1};
    HashMap<unsigned, TerrainEditSnapshot> terrainEditSnapshots_;
    /// Object edit snapshot for rollback
    struct ObjectEditSnapshot
    {
        unsigned char subtype;  // 0=create, 1=delete, 2=transform
        unsigned nodeID;
        String xmlData;         // serialized node (for create rollback = delete, delete rollback = recreate)
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
    };
    HashMap<unsigned, ObjectEditSnapshot> objectEditSnapshots_;
    float editSendAccumulator_{0.0f};
    static constexpr float EDIT_SEND_INTERVAL = 0.1f;  // 100ms rate limit
    bool replayingBroadcast_{false};  // true while replaying a remote edit — suppresses re-send

    // --- Camera modes & possession ---
    void UpdateCharacterCamera();
    void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
    void HandleClientObjectID(StringHash eventType, VariantMap& eventData);
    /// Attach creature logic component to a replicated node by creatureId.
    void AttachCreatureComponent(Node* creatureNode, int creatureId);
    /// Detect server-replicated creature nodes and attach client-side logic components.
    void HandleNodeAdded(StringHash eventType, VariantMap& eventData);

    // cameraMode_ is inherited from Sample (CAM_GOD, CAM_CHASE, CAM_FIRSTPERSON)
    unsigned clientObjectID_{};
    WeakPtr<Node> characterNode_;
    HashMap<Connection*, WeakPtr<Node>> serverObjects_;

    // --- God mode camera + possession ---
    void TogglePossession();             // Legacy toggle (P key) — retained for backward compat
    void PossessNPC(Node* npcNode);      // Possess a specific HumanNPC
    void UnpossessNPC();                 // Return to god cam
    void UpdatePossessionLerp(float timeStep);
    WeakPtr<Node> possessedNPC_;         // Currently possessed HumanNPC node (null = god cam)
    bool possessing_{false};             // true = player controls character, false = god mode
    int possessedNPCPlayerId_{-1};       // Server-assigned playerId for possessed NPC's inventory (-1 = none)
    bool possessionLerping_{false};      // true during camera transition
    float possessionLerpTime_{0.0f};
    static constexpr float POSSESSION_LERP_DURATION = 0.3f;
    Vector3 possessionLerpStartPos_;
    Quaternion possessionLerpStartRot_;
    Vector3 possessionLerpEndPos_;
    Quaternion possessionLerpEndRot_;

    // --- 3D Audio listener ---
    Node* listenerNode_{};               // Owns the SoundListener component
    void UpdateListenerPosition();       // Called after possession lerp completes

    // --- Water / rendering ---
    SharedPtr<Node> reflectionCameraNode_;
    WeakPtr<Node> waterNode_;
    Plane waterPlane_;
    Plane waterClipPlane_;
    SharedPtr<ProfilerUI> profilerUI_;
    RenderPath* renderPath_{};

    /// Performance Phase 4 — shader parameter deduplication.
    /// Wraps `renderPath_->SetShaderParameter` and skips uploads when the
    /// value is identical to the last one. Per-frame god-ray spam (8 calls
    /// in the sun update path, most of which are static literals like 0.5,
    /// 0.97, 0.4) becomes 0 uploads after the first frame.
    void SetShaderParamCached(const String& name, const Variant& value);
    /// Cache invalidation hook for the cached shader params.
    /// Call after a render path swap (e.g. when post-process effects change).
    void InvalidateShaderParamCache() { shaderParamCache_.Clear(); }
    HashMap<String, Variant> shaderParamCache_;
    RenderPath* reflectionRenderPath_{};
    WeakPtr<Zone> zone_;
    WeakPtr<Terrain> terrain_;
    Color origFogColor_;
    float origFogStart_{};
    float origFogEnd_{};

    // --- Water droplets post-process ---
    bool wasUnderwater_{false};
    float breachTime_{-100.0f};
    float splashTimer_{0.0f};       // countdown to next splash burst
    float splashInterval_{1.2f};    // seconds between splash bursts

    // --- Survival HUD (driven by MSG_VITAL_UPDATE from AuthServer) ---
    void HandleVitalUpdate(MemoryBuffer& msg);
    void UpdateVitalBars();

    int vitalHp_{20};
    int vitalMaxHp_{20};
    float vitalHunger_{100.0f};
    float vitalThirst_{100.0f};
    float vitalStamina_{100.0f};
    float vitalWarmth_{15.0f};
    bool vitalAlive_{true};
    float vitalSpeedMult_{1.0f};

    /// HUD component (vital bars with fade, critical states, status icons, context hints)
    WeakPtr<HUD> hud_;

    // --- Context hint raycast (Phase 2 HUD) ---
    void UpdateContextHintRaycast();
    static constexpr float INTERACT_DISTANCE = 5.0f;

    // --- Inventory UI (driven by MSG_INVENTORY_UPDATE/DELTA from AuthServer) ---
    void HandleInventoryUpdate(MemoryBuffer& msg);
    void HandleInventoryDelta(MemoryBuffer& msg);
    void CreateInventoryUI();
    void RefreshInventoryGrid();
    void HandleInventoryDragDrop(StringHash eventType, VariantMap& eventData);
    void ToggleInventory();
    void SendPickup(unsigned nodeId);
    void SendResourceHarvest(const Vector3& worldPos, int itemId);
    void SendDrop(int itemId, int qty);

    // Combat
    void TryMeleeAttack();
    void HandleCombatResult(int msgID, MemoryBuffer& msg);
    /// Combat Phase 2: server-authoritative creature death message handler.
    /// Triggers the death animation and fires E_CREATUREDIED on the local node.
    void HandleCreatureDeath(MemoryBuffer& msg);

    struct ClientInventorySlot
    {
        int itemId{};
        int quantity{};
        int durability{-1};
        int tier{};
        String slotType;       // "bag", "hand", "offhand", "body", "head", "feet", "back"
        String itemName;
        String itemCategory;   // "weapon", "tool", "armor", "clothing", "food", etc.
        float itemWeight{};
        float weight{};        // total weight (itemWeight * quantity)
    };
    Vector<ClientInventorySlot> inventory_;
    float inventoryWeight_{};
    float inventoryMaxWeight_{30.0f};
    float inventoryAbsWeight_{60.0f};
    int inventoryMaxSlots_{10};
    bool inventoryOpen_{false};

    SharedPtr<Window> inventoryWindow_;
    Vector<Button*> inventorySlotButtons_;
    Text* inventoryWeightText_{};
    BorderImage* inventoryWeightBar_{};
    int selectedSlotIndex_{-1};

    // --- Equipment Slots (Phase 2) ---
    void CreateEquipmentUI(UIElement* parent);
    void RefreshEquipmentSlots();
    void HandleEquipSlotClick(StringHash eventType, VariantMap& eventData);
    void HandleBagSlotDoubleClick(StringHash eventType, VariantMap& eventData);
    void HandleBagSlotClick(StringHash eventType, VariantMap& eventData);
    void EquipItem(int bagIndex);
    void UnequipItem(const String& slotType);
    bool CanEquipToSlot(const String& itemCategory, const String& slotType) const;
    String FindBestEquipSlot(const String& itemCategory) const;
    void SendEquip(int itemId, const String& targetSlot);
    void SendUnequip(const String& slot);

    /// Equipment slot name → Button mapping (6 slots)
    struct EquipSlotUI
    {
        String slotType;       // "hand", "offhand", "body", "head", "feet", "back"
        String label;          // "Main Hand", "Off Hand", etc.
        Button* button{};
        Text* text{};
    };
    static const int NUM_EQUIP_SLOTS = 6;
    EquipSlotUI equipSlots_[6];
    float lastBagClickTime_{};     // for double-click detection
    int lastBagClickIndex_{-1};    // which slot was last clicked
    static constexpr float DOUBLE_CLICK_TIME = 0.4f;

    // --- Consumables & Context Menu (Phase 3) ---
    void ShowItemContextMenu(int bagIndex, const IntVector2& screenPos);
    void DismissItemContextMenu();
    void HandleContextMenuAction(StringHash eventType, VariantMap& eventData);
    void UseItem(int bagIndex);
    void DropItem(int bagIndex);
    void SendUseItem(int itemId);
    void SendDrink(const String& sourceType);
    bool TryDrink();
    bool IsConsumable(const String& category) const;
    SharedPtr<Window> itemContextMenu_;
    int contextMenuBagIndex_{-1};

    // --- Crafting UI (Phase 4) ---
    void InitGameDB();
    void BuildSpawnTable();
    void HandleAnimalDied(StringHash eventType, VariantMap& eventData);
    void HandleFishBorn(StringHash eventType, VariantMap& eventData);

    /// Spawn one creature of the given species at the given position.
    /// Used by both the initial CreateAnimals loop and the Phase 1
    /// MSG_SPAWN_CREATURE handler. Returns the created LandAnimal* or
    /// nullptr if the species is unknown / scene is missing.
    /// pos.y is used as-is — caller is responsible for snapping to terrain
    /// (the spawn-loop has already done so; the message handler does it
    /// just before calling).
    class LandAnimal* SpawnCreatureAt(int creatureId, const Vector3& pos);

    /// Server→client MSG_SPAWN_CREATURE handler. Reads regionId, creatureId,
    /// and (x, 0, z) from the message; snaps Y to terrain; calls SpawnCreatureAt.
    void HandleSpawnCreatureMsg(StringHash eventType, VariantMap& eventData);

    /// Spawn a timed visual effect at a phenomenon location (client-side only).
    void SpawnPhenomenonEffect(const Vector3& pos, const String& visualHint);

    // ── Server-Authoritative Creature AI (NPC AI Phase 1) ──────────────
    /// Handle MSG_CREATURE_AI_STATE — server pushes authoritative creature state.
    void HandleCreatureAIState(MemoryBuffer& msg);
    void RequestDeathLog();
    void HandleDeathLogResult(MemoryBuffer& msg);
    void RequestDeathAnalytics();
    void HandleDeathAnalyticsResult(MemoryBuffer& msg);
    /// Map server-assigned spawnId → local scene node for AI state correlation.
    HashMap<unsigned, WeakPtr<Node>> spawnIdToNode_;

    // ── Server-Authoritative Trees ──────────────────────────────────────
    void HandleSpawnTree(MemoryBuffer& msg);
    /// Map server tree ID → local scene node for removal on harvest.
    HashMap<unsigned, WeakPtr<Node>> treeIdToNode_;
    unsigned focusedTreeId_{0};
    void SendChopTree(unsigned treeId);
    void SendTapTree(unsigned treeId);
    // ── Mining ──────────────────────────────────────────────────────────
    WeakPtr<Node> focusedOutcropNode_;
    void SendMineRequest(Node* outcropNode);
    // Arrow HUD — shows ammo count when bow equipped
    void UpdateArrowHUD();
    WeakPtr<Text> arrowCountText_;

    // ── Torch Visual System (Fire Carrying Phase 2) ─────────────────────
    void UpdateTorchVisual();
    void CreateTorchFlame(int fireItemId);
    void RemoveTorchFlame(bool showExtinguish);
    WeakPtr<Node> torchFlameNode_;
    int equippedTorchItem_{0};  // 0=none, 109=basic, 871=resin

    // ── Bark Vessel Visual States (Water Phase 1) ���───────────────────
    /// Update NPC's bark vessel visual: fire glow, water shimmer, or nothing.
    void UpdateNPCVesselVisual(Node* npcNode, unsigned char contents);
    void CreateCraftingUI();
    void RefreshRecipeList();
    void SelectRecipe(int index);
    void RefreshRecipeDetail();
    void HandleCraftButton(StringHash eventType, VariantMap& eventData);
    void HandleRecipeSelect(StringHash eventType, VariantMap& eventData);
    void ToggleCrafting();
    void UpdateCraftTimer(float timeStep);
    HashMap<int, int> BuildInventoryMap() const;
    void SendCraft(int recipeId);

    SharedPtr<class GameDB> gameDB_;
    SharedPtr<class PopulationManager> popManager_;
    Vector<struct RecipeInfo> recipes_;

    /// Cached gather source lookup: itemId → first matching GatherSourceInfo.
    HashMap<int, struct GatherSourceInfo> gatherSourceByItem_;
    /// Cached water sources from DB.
    Vector<struct WaterSourceInfo> waterSources_;
    int selectedRecipeIndex_{-1};
    bool craftingOpen_{false};
    float craftTimer_{};          // countdown during crafting
    float craftDuration_{};       // total craft time for current recipe
    int craftingRecipeId_{-1};    // recipe being crafted (-1 = idle)

    SharedPtr<Window> craftingWindow_;
    ListView* recipeList_{};
    UIElement* recipeDetail_{};
    Text* recipeTitle_{};
    Text* recipeDesc_{};
    UIElement* recipeInputs_{};   // container for ingredient rows
    Text* recipeOutput_{};
    Button* craftBtn_{};
    Text* craftBtnText_{};
    BorderImage* craftProgressBar_{};
    BorderImage* craftProgressFill_{};

    // --- Storage UI (Phase 5) ---
    void CreateStorageUI();
    void OpenStorage(int buildingId);
    void CloseStorage();
    void ToggleStorage();
    void RefreshStorageGrid();
    void HandleStorageSlotClick(StringHash eventType, VariantMap& eventData);
    void HandleStorageContents(MemoryBuffer& msg);
    void TransferToStorage(int bagIndex);
    void TransferFromStorage(int storageIndex);
    void SendOpenStorage(int buildingId);
    void SendCloseStorage(int buildingId);
    void SendTransfer(int itemId, int qty, bool toStorage);

    struct StorageSlot
    {
        int itemId{};
        int quantity{};
        String itemName;
    };
    Vector<StorageSlot> storageContents_;
    SharedPtr<Window> storageWindow_;
    Vector<Button*> storageSlotButtons_;
    Text* storageTitle_{};
    int openBuildingId_{-1};
    bool storageOpen_{false};
    static const int STORAGE_MAX_SLOTS = 20;

    // --- Inventory Polish (Phase 6) ---
    void SplitStack(int bagIndex);
    void SortInventory();
    void HandleSortButton(StringHash eventType, VariantMap& eventData);
    void ShowItemTooltip(int bagIndex, const IntVector2& screenPos);
    void HideItemTooltip();
    void HandleSlotHoverBegin(StringHash eventType, VariantMap& eventData);
    void HandleSlotHoverEnd(StringHash eventType, VariantMap& eventData);
    void UpdateWeightBarColor();

    SharedPtr<Window> itemTooltip_;
    Button* sortButton_{};

    // --- Inventory Phase 6: Sorting and Filters ---
    enum InventorySortMode { SORT_CATEGORY = 0, SORT_NAME, SORT_WEIGHT, SORT_TIER, SORT_COUNT };
    InventorySortMode inventorySortMode_{SORT_CATEGORY};
    String inventoryFilter_;  // empty = show all, else category name
    Vector<Button*> filterButtons_;
    void UpdateFilterButtonColors();
    void HandleFilterButton(StringHash eventType, VariantMap& eventData);

    // --- Biome Classification ---
    BiomeType ClassifyTerrain(const Vector3& worldPos) const;
    void SampleWeightMap(const Vector2& uv, float& outR, float& outG, float& outB) const;
    void CacheWeightMapImage();
    SharedPtr<Image> weightMapImage_;   ///< CPU-side weight map for biome sampling
    bool biomeDebugOverlay_{false};     ///< F7 toggles biome name at crosshair

    // --- Water Distance Map (for habitat spawning) ---
    void BuildWaterDistanceMap();
    float SampleWaterDistance(float worldX, float worldZ) const;
    Vector<float> waterDistMap_;        ///< Low-res grid of distances to nearest water
    int waterDistMapW_{0};              ///< Grid width (cells)
    int waterDistMapH_{0};              ///< Grid height (cells)
    float waterDistCellSize_{0.0f};     ///< World units per cell
    Vector3 waterDistOrigin_;           ///< World-space origin of the grid

    // --- Tapestry Inscription ---
    String tapestryInscription_;  // last received inscription text

    // --- Canoe Transport ---
    bool inCanoe_{false};
    int boardedCanoeId_{-1};
    float canoeSpeedBoost_{2.0f};  // multiplier when in canoe on water
    void TryBoardCanoe();

    // --- Building System ---
    void InitBuildingSystem();
    void LoadBuildingTypes();
    void LoadSnapRules();
    void CreateBuildMenuUI();
    void RefreshBuildMenu();
    void ToggleBuildMode();
    void HandleBuildMenuSelect(StringHash eventType, VariantMap& eventData);
    void HandleBuildMessage(int msgID, MemoryBuffer& msg);
    void TryGateInteract();
    void TryBuildingInteract();
    /// Fire system Phase 2b: deposit available softwood/hardwood from inventory
    /// into the named woodpile, respecting per-type capacity. Client-only
    /// state until Phase 3 wires server authority.
    void DepositToWoodpile(int placedId);

    SharedPtr<BuildingSystem> buildingSystem_;
    HabitatRules habitatRules_;
    SharedPtr<Window> buildMenuWindow_;
    ListView* buildMenuList_{};
    Text* buildStatusText_{};
    bool buildMenuOpen_{false};
    int focusedGateId_{-1};
    // Fire system Phase 2b — focused woodpile (set by hint scanner, consumed by E-key)
    int focusedWoodpileId_{-1};

    // Resource Chain Phase 2 — focused harvest target (set by hint scanner, consumed by E-key)
    unsigned focusedHarvestNodeId_{0};
    int      focusedHarvestCreatureId_{0};

    // Resource Chain Phase 2 — trap state lookup. Server trap node id → local PlacedTrap node.
    // Populated in MSG_TRAP_SPAWNED, looked up in MSG_TRAP_REMOVED + the trap-check scanner.
    // Replaces a per-removal scene walk (fix from TASK_RESOURCE_CHAIN_PHASE_2.md addendum).
    HashMap<unsigned, WeakPtr<Node>> trapNodes_;
    /// Placed crops: cropId → scene node. Populated by MSG_CROP_SPAWNED, removed by MSG_CROP_REMOVED.
    HashMap<int, WeakPtr<Node>> cropNodes_;
    /// Cached seed item IDs from GameDB crop_types — used by TryPlantCrop().
    HashSet<int> seedItemIds_;
    // Trap-check scanner: per (trap, creature) dedupe so a creature standing in range only
    // generates one MSG_TRAP_CHECK round-trip. Cleared per-trap when the creature leaves range
    // or the trap is removed.
    HashMap<unsigned, HashSet<unsigned>> trapCheckSent_;
    float    trapCheckTimer_{0.0f};
    static constexpr float TRAP_CHECK_INTERVAL = 1.0f;  // scan at 1 Hz
    // Conservative outer bound — covers the widest seed_data.sql attract_range (40m for Meat Trap
    // Wolf and Berry Trap Deer). Server enforces the real per-(trap,creature) range from GameDB,
    // so this only needs to be ≥ the max seed value. Future Phase 3 polish: cache the actual
    // range per placed trap from GameDB at spawn time so the scanner can be tighter per trap.
    static constexpr float TRAP_CHECK_RADIUS   = 40.0f;
    /// Scan placed traps against local animals; send MSG_TRAP_CHECK for any new pair in range.
    void ScanTrapsForCatches();

    // Respawn
    int respawnBuildingId_{0};
    Vector3 respawnPosition_;

    // Combat fumble effect — floating text + camera shake on nat 1
    WeakPtr<Text> fumbleText_;
    float fumbleTextTimer_{0.0f};
    float fumbleTextStartY_{0.0f};
    float cameraShakeTimer_{0.0f};
    void ShowFumbleEffect();

    // Offline mode — auto-launch local AuthServer + connect with reserved login.
    // State machine driven by HandleOfflineButton, HandleConnectFailed,
    // HandleServerConnected, and TickOfflineConnect (called from HandleUpdate).
    enum OfflineMode
    {
        OFFLINE_NONE = 0,         ///< Not in an offline-mode flow.
        OFFLINE_TRY_CONNECT,      ///< First connect attempt; if it fails we'll spawn.
        OFFLINE_SPAWN_PENDING,    ///< AuthServer just spawned; waiting for retry timer.
        OFFLINE_RETRY_CONNECT,    ///< Post-spawn retry attempt.
    };
    OfflineMode offlineMode_{OFFLINE_NONE};
    float offlineRetryTimer_{0.0f};
    int   offlineRetriesLeft_{0};
    static constexpr int   OFFLINE_MAX_RETRIES   = 90;   // 90 × 1s = 90s window for AuthServer startup
    static constexpr float OFFLINE_RETRY_INTERVAL = 1.0f;  // seconds between retries
    void TickOfflineConnect(float timeStep);
    void OfflineSpawnAuthServer();
    void OfflineDial();

    // --- Trade System (Client UI) ---
    void SendTradeRequest(unsigned targetNodeId);
    void SendTradeOffer(int itemId, int qty, bool adding);
    void SendTradeLock();
    void SendTradeCancel();
    void SendTradeAccept();
    void SendTradeReject();
    void HandleTradeIncoming(MemoryBuffer& msg);
    void HandleTradeAccepted(MemoryBuffer& msg);
    void HandleTradeUpdate(MemoryBuffer& msg);
    void HandleTradeLock(MemoryBuffer& msg);
    void HandleTradeComplete(MemoryBuffer& msg);
    void HandleTradeCancel(MemoryBuffer& msg);
    void CreateTradeUI();
    void RefreshTradeOffers();
    void CloseTradeWindow();
    void HandleTradeOfferSlotClick(StringHash eventType, VariantMap& eventData);
    void HandleTradeBagSlotClick(StringHash eventType, VariantMap& eventData);

    struct TradeOfferItem
    {
        int itemId{};
        int quantity{};
        String itemName;
    };
    bool tradeOpen_{false};
    bool tradePending_{false};
    bool tradeLocked_{false};
    bool tradePartnerLocked_{false};
    SharedPtr<Window> tradeWindow_;
    Vector<Button*> tradeMyOfferSlots_;
    Vector<Button*> tradeTheirOfferSlots_;
    Vector<TradeOfferItem> myTradeOffer_;
    Vector<TradeOfferItem> theirTradeOffer_;
    Text* tradeStatusText_{};
    Button* tradeLockBtn_{};
    Button* tradeCancelBtn_{};
    // Incoming trade request prompt
    bool tradeIncomingPending_{false};
    int tradeIncomingPlayerId_{-1};
    String tradeIncomingName_;
    SharedPtr<Window> tradePromptWindow_;
    // Phase 2: proximity warning
    unsigned tradePartnerNodeId_{0};
    float tradeProximityCheckTimer_{0.0f};

};
