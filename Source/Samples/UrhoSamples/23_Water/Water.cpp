// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/RenderPath.h>
#include <Urho3D/Graphics/Skybox.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Graphics/TerrainPatch.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/GraphicsAPI/RenderSurface.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Slider.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/GraphicsAPI/Shader.h>
#include <Urho3D/GraphicsAPI/ShaderVariation.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>

#include "Water.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

#include <Urho3D/Math/Random.h>
#include <ctime>
#include <cmath>

URHO3D_DEFINE_APPLICATION_MAIN(Water)

static const float MELBOURNE_LAT = -37.8136f;
static const float MELBOURNE_LON = 144.96f;
static const float SOLAR_NOON = 12.0f + (11.0f * 15.0f - MELBOURNE_LON) / 15.0f;
static const float CELESTIAL_TIME_SCALE = 1.0f;
static const float DEG_TO_RAD = M_PI / 180.0f;
static const float RAD_TO_DEG = 180.0f / M_PI;

Water::Water(Context* context) :
    Sample(context)
{
}

void Water::Stop()
{
    if (timeRequest_)
    {
        HttpRequestState state = timeRequest_->GetState();
        if (state == HTTP_CLOSED || state == HTTP_ERROR)
            timeRequest_.Reset();
        else
            timeRequest_.Detach();
    }

    skyboxMat_.Reset();
    sunMat_.Reset();
    moonMat_.Reset();
    profilerUI_.Reset();
    terrainPanel_.Reset();
    hierarchyWindow_.Reset();
    inspectorWindow_.Reset();
    editableHeightMap_.Reset();
    if (prefabBrush_)
    {
        prefabBrush_->Remove();
        prefabBrush_.Reset();
    }
}

bool Water::OnEscapePressed()
{
    // Peel layers from outside in — last opened, first closed
    if (fileSelector_)
    {
        fileSelector_.Reset();
        return true;
    }
    if (generateMeshPanel_ && generateMeshPanel_->IsVisible())
    {
        generateMeshPanel_->SetVisible(false);
        return true;
    }
    if (inspectorWindow_ && inspectorWindow_->IsVisible())
    {
        inspectorWindow_->SetVisible(false);
        return true;
    }
    if (hierarchyWindow_ && hierarchyWindow_->IsVisible())
    {
        hierarchyWindow_->SetVisible(false);
        return true;
    }
    if (terrainPanel_ && terrainPanel_->IsVisible())
    {
        terrainPanel_->SetVisible(false);
        return true;
    }
    if (brushMode_ != 0)
    {
        brushMode_ = 0;
        return true;
    }
    if (selectedNode_)
    {
        DeselectNode();
        return true;
    }
    if (!menuOpen_)
    {
        // Switch from camera mode to cursor mode
        menuOpen_ = true;
        GetSubsystem<Input>()->SetMouseVisible(true);
        GetSubsystem<Input>()->SetMouseGrabbed(false);
        return true;
    }
    return false;  // nothing to close — let base class exit
}

void Water::SelectNode(Node* node)
{
    if (!node)
        return;

    DeselectNode();
    selectedNode_ = node;

    auto* model = node->GetComponent<StaticModel>();
    if (model)
    {
        for (unsigned i = 0; i < model->GetNumGeometries(); ++i)
        {
            Material* orig = model->GetMaterial(i);
            originalMaterials_.Push(SharedPtr<Material>(orig));

            if (!orig)
                continue;

            SharedPtr<Material> maskMat(orig->Clone());
            Technique* origTech = maskMat->GetTechnique(0);
            if (origTech)
            {
                SharedPtr<Technique> techClone = origTech->Clone();
                Pass* maskPass = techClone->CreatePass("mask");
                maskPass->SetVertexShader("Silhouette");
                maskPass->SetPixelShader("Silhouette");
                maskPass->SetVertexShaderDefines("MASK");
                maskPass->SetPixelShaderDefines("MASK");
                maskMat->SetTechnique(0, techClone);
            }
            maskMat->SetShaderParameter("MatDiffColor", Color(1.0f, 1.0f, 1.0f, 1.0f));
            model->SetMaterial(i, maskMat);
        }
    }

    // Sync hierarchy tree & inspector
    HighlightInHierarchy(node);
    if (inspectorWindow_ && inspectorWindow_->IsVisible())
        RebuildInspector();
}

void Water::DeselectNode()
{
    if (selectedNode_ && !selectedNode_.Expired())
    {
        auto* model = selectedNode_->GetComponent<StaticModel>();
        if (model)
        {
            for (unsigned i = 0; i < originalMaterials_.Size() && i < model->GetNumGeometries(); ++i)
                model->SetMaterial(i, originalMaterials_[i]);
        }

        // Wake rigid bodies so physics settles the object after transforms
        if (gizmoMode_ != 0)
            WakeSelectedNode();
    }

    originalMaterials_.Clear();
    selectedNode_.Reset();

    // Clear inspector
    if (inspectorWindow_ && inspectorWindow_->IsVisible())
        RebuildInspector();
}

void Water::Start()
{
    Sample::Start();

    // Fallback RNG seed — local time + favourite prime.
    // Will be re-seeded from network noise once time sync completes.
    SetRandomSeed((unsigned)time(nullptr) + 25773u);

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    CreateScene();
    CreateInstructions();
    SetupViewport();
    SubscribeToEvents();

    Sample::InitMouseMode(MM_RELATIVE);

    CreateMenuBar();
    CreateMinimap();

    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler(), graphics);
    profilerUI_->SetVisible(false);
}

// ============================================================================
// Scene
// ============================================================================

void Water::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    scene_->CreateComponent<PhysicsWorld>();
    scene_->CreateComponent<DebugRenderer>();

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.7f, 0.7f, 0.75f));
    zone->SetFogStart(500.0f);
    zone->SetFogEnd(750.0f);
    float fogMinHeight = 5.0f;   // fog base (thickest here)
    float fogMaxHeight = 18.0f;  // fog ceiling (fades to clear)
    zone->SetFogHeight(fogMinHeight);
    zone->SetFogHeightScale(1.0f / Max(fogMaxHeight - fogMinHeight, M_EPSILON));

    zone_ = zone;
    origFogColor_ = zone->GetFogColor();
    origFogStart_ = zone->GetFogStart();
    origFogEnd_ = zone->GetFogEnd();

    // Directional light (sun)
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    sunLight_ = lightNode->CreateComponent<Light>();
    sunLight_->SetLightType(LIGHT_DIRECTIONAL);
    sunLight_->SetCastShadows(true);
    sunLight_->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    sunLight_->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));
    sunLight_->SetSpecularIntensity(0.5f);
    sunLight_->SetColor(Color(1.2f, 1.2f, 1.2f));

    // Skybox
    Node* skyNode = scene_->CreateChild("Sky");
    skyNode->SetScale(500.0f);
    auto* skybox = skyNode->CreateComponent<Skybox>();
    skybox->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    skyboxMat_ = cache->GetResource<Material>("Materials/Skybox.xml");
    skybox->SetMaterial(skyboxMat_);

    // Terrain
    Node* terrainNode = scene_->CreateChild("Terrain");
    terrainNode->SetPosition(Vector3(0.0f, -20.0f, 0.0f));
    auto* terrain = terrainNode->CreateComponent<Terrain>();
    terrain->SetPatchSize(64);
    terrain->SetSpacing(Vector3(2.0f, 0.5f, 2.0f));
    terrain->SetSmoothing(false);
    terrain->SetHeightMap(cache->GetResource<Image>("Textures/HeightMap.png"));
    terrain->SetMaterial(cache->GetResource<Material>("Materials/Terrain.xml"));
    terrain->SetOccluder(true);
    terrain_ = terrain;

    auto* terrainBody = terrainNode->CreateComponent<RigidBody>();
    terrainBody->SetCollisionLayer(2);
    terrainBody->SetFriction(0.75f);
    auto* terrainShape = terrainNode->CreateComponent<CollisionShape>();
    terrainShape->SetTerrain();

    // 32-bit editable heightmap (RGBA — R + G/256 + B/65536 + A/16M)
    {
        Image* origHM = terrain->GetHeightMap();
        int w = origHM->GetWidth();
        int h = origHM->GetHeight();
        int origComps = origHM->GetComponents();
        editableHeightMap_ = new Image(context_);
        editableHeightMap_->SetSize(w, h, 4);
        editableHeightMap_->SetName(origHM->GetName());
        unsigned char* src = origHM->GetData();
        unsigned char* dst = editableHeightMap_->GetData();
        for (int i = 0; i < w * h; ++i)
        {
            dst[i * 4] = src[i * origComps];
            dst[i * 4 + 1] = (origComps >= 2) ? src[i * origComps + 1] : 0;
            dst[i * 4 + 2] = (origComps >= 3) ? src[i * origComps + 2] : 0;
            dst[i * 4 + 3] = (origComps >= 4) ? src[i * origComps + 3] : 0;
        }
        terrain->SetHeightMap(editableHeightMap_);
    }

    // 1000 boxes
    for (unsigned i = 0; i < 1000; ++i)
    {
        Node* objectNode = scene_->CreateChild("Box");
        Vector3 position(Random(2000.0f) - 1000.0f, 0.0f, Random(2000.0f) - 1000.0f);
        position.y_ = terrain->GetHeight(position) + 2.25f;
        objectNode->SetPosition(position);
        objectNode->SetRotation(Quaternion(Vector3(0.0f, 1.0f, 0.0f), terrain->GetNormal(position)));
        objectNode->SetScale(5.0f);
        auto* object = objectNode->CreateComponent<StaticModel>();
        object->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
        object->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));
        object->SetCastShadows(true);

        auto* body = objectNode->CreateComponent<RigidBody>();
        body->SetMass(0.0f);
        auto* shape = objectNode->CreateComponent<CollisionShape>();
        shape->SetBox(Vector3::ONE);
    }

    // Water plane
    waterNode_ = scene_->CreateChild("Water");
    waterNode_->SetScale(Vector3(2048.0f, 1.0f, 2048.0f));
    waterNode_->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    auto* water = waterNode_->CreateComponent<StaticModel>();
    water->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    water->SetMaterial(cache->GetResource<Material>("Materials/Water.xml"));
    water->SetViewMask(0x80000000);

    // Initialize time from system clock (Melbourne AEDT = UTC+11)
    {
        time_t now = time(nullptr);
        struct tm* utc = gmtime(&now);
        int hour = utc->tm_hour + 11;
        int yday = utc->tm_yday + 1;
        if (hour >= 24) { hour -= 24; yday++; }
        dayOfYear_ = yday;
        baseDayOfYear_ = yday;
        timeOfDay_ = (float)hour + utc->tm_min / 60.0f + utc->tm_sec / 3600.0f;

        struct tm refMoon = {};
        refMoon.tm_year = 125;
        refMoon.tm_mon = 0;
        refMoon.tm_mday = 29;
        refMoon.tm_hour = 12;
        time_t refTime = mktime(&refMoon);
        double daysSinceNewMoon = difftime(now, refTime) / 86400.0;
        moonAge_ = (float)fmod(daysSinceNewMoon, 29.53);
        if (moonAge_ < 0.0f) moonAge_ += 29.53f;
        baseMoonAge_ = moonAge_;
    }

    // Command-line override: -time 10.5
    bool timeOverride = false;
    const Vector<String>& args = GetArguments();
    for (unsigned i = 0; i < args.Size(); ++i)
    {
        if ((args[i] == "-time" || args[i] == "time") && i + 1 < args.Size())
        {
            timeOfDay_ = ToFloat(args[i + 1]);
            if (timeOfDay_ < 0.0f) timeOfDay_ += 24.0f;
            if (timeOfDay_ >= 24.0f) timeOfDay_ -= 24.0f;
            timeOverride = true;
            break;
        }
    }

    if (!timeOverride)
        FetchNetworkTime();
    timeSyncTimer_ = timeOverride ? 9999.0f : 300.0f;

    CreateCelestialBodies();
    CreateOOFOs();
    CreateFish();

    // Camera
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(750.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 7.0f, -20.0f));
}

void Water::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    instructionText_ = ui->GetRoot()->CreateChild<Text>();
    instructionText_->SetText(
        "WASD move, Mouse look, Tab cursor mode\n"
        "LMB raise, RMB lower, Scroll brush size\n"
        "F5 debug, F fill, H fog, F11 fullscreen\n"
        "NumPad Enter hide UI, 1 sun, 2 moon");
    instructionText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    instructionText_->SetTextAlignment(HA_CENTER);
    instructionText_->SetHorizontalAlignment(HA_CENTER);
    instructionText_->SetVerticalAlignment(VA_CENTER);
    instructionText_->SetPosition(0, ui->GetRoot()->GetHeight() / 4);
}

// ============================================================================
// Viewport + Water Reflection
// ============================================================================

void Water::SetupViewport()
{
    auto* graphics = GetSubsystem<Graphics>();
    auto* renderer = GetSubsystem<Renderer>();
    auto* cache = GetSubsystem<ResourceCache>();

    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    viewport->SetDrawDebug(true);
    renderer->SetViewport(0, viewport);

    SharedPtr<RenderPath> renderPath(new RenderPath());
    renderPath->Load(cache->GetResource<XMLFile>("RenderPaths/ForwardHWDepthOutline.xml"));
    viewport->SetRenderPath(renderPath);

    RenderPath* rp = viewport->GetRenderPath();
    rp->Append(cache->GetResource<XMLFile>("PostProcess/Underwater.xml"));
    rp->Append(cache->GetResource<XMLFile>("PostProcess/GodRays.xml"));
    rp->SetShaderParameter("WaterLevel", waterNode_->GetWorldPosition().y_);
    rp->SetShaderParameter("NoiseStrength", 0.015f);
    rp->SetShaderParameter("NoiseTiling", 1.0f);
    rp->SetShaderParameter("NoiseSpeed", Vector2(0.05f, 0.05f));
    rp->SetShaderParameter("UnderwaterColor", Vector3(0.0f, 0.2f, 0.3f));
    rp->SetShaderParameter("TintIntensity", 0.3f);
    rp->SetShaderParameter("DepthFalloff", 0.02f);
    rp->SetShaderParameter("BreachDepth", 2.0f);
    rp->SetShaderParameter("MainCameraY", cameraNode_->GetWorldPosition().y_);
    rp->SetEnabled("Underwater", true);
    renderPath_ = rp;

    waterPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());
    waterClipPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());

    reflectionCameraNode_ = cameraNode_->CreateChild();
    auto* reflectionCamera = reflectionCameraNode_->CreateComponent<Camera>();
    reflectionCamera->SetFarClip(750.0);
    reflectionCamera->SetViewMask(0x7fffffff);
    reflectionCamera->SetAutoAspectRatio(false);
    reflectionCamera->SetUseReflection(true);
    reflectionCamera->SetReflectionPlane(waterPlane_);
    reflectionCamera->SetUseClipping(true);
    reflectionCamera->SetClipPlane(waterClipPlane_);
    reflectionCamera->SetAspectRatio((float)graphics->GetWidth() / (float)graphics->GetHeight());

    int texSize = 1024;
    SharedPtr<Texture2D> renderTexture(new Texture2D(context_));
    renderTexture->SetSize(texSize, texSize, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET);
    renderTexture->SetFilterMode(FILTER_BILINEAR);
    RenderSurface* surface = renderTexture->GetRenderSurface();
    SharedPtr<Viewport> rttViewport(new Viewport(context_, scene_, reflectionCamera));
    rttViewport->SetDrawDebug(false);
    surface->SetViewport(0, rttViewport);
    auto* waterMat = cache->GetResource<Material>("Materials/Water.xml");
    waterMat->SetTexture(TU_DIFFUSE, renderTexture);
}

void Water::SubscribeToEvents()
{
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(Water, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(Water, HandlePostRenderUpdate));
}

// ============================================================================
// Menu Bar
// ============================================================================

DropDownList* Water::CreateMenuDropdown(const String& label, const Vector<String>& items)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    auto* dd = menuBar_->CreateChild<DropDownList>();
    dd->SetStyleAuto();
    dd->SetResizePopup(true);
    dd->SetFixedHeight(24);
    dd->SetMinWidth(70);

    auto* placeholder = dd->GetPlaceholder();
    if (placeholder)
    {
        auto* phText = placeholder->CreateChild<Text>();
        phText->SetFont(font, 12);
        phText->SetText(label);
        phText->SetColor(Color::WHITE);
    }

    for (unsigned i = 0; i < items.Size(); ++i)
    {
        auto* text = new Text(context_);
        text->SetText(items[i]);
        text->SetFont(font, 12);
        text->SetMinWidth(140);
        text->SetMinHeight(22);
        text->SetStyleAuto();
        text->SetColor(Color(0.9f, 0.9f, 0.9f));
        text->SetHoverColor(Color(1.0f, 1.0f, 0.5f));
        dd->AddItem(text);
    }

    dd->SetSelection(M_MAX_UNSIGNED);
    return dd;
}

void Water::CreateMenuBar()
{
    auto* ui = GetSubsystem<UI>();
    auto* uiRoot = ui->GetRoot();

    menuBar_ = uiRoot->CreateChild<BorderImage>("MenuBar");
    menuBar_->SetStyle("Window");
    menuBar_->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
    menuBar_->SetFixedHeight(28);
    menuBar_->SetMinWidth(uiRoot->GetWidth());
    menuBar_->SetHorizontalAlignment(HA_LEFT);
    menuBar_->SetVerticalAlignment(VA_TOP);
    menuBar_->SetOpacity(0.9f);

    // File
    {
        Vector<String> items;
        items.Push("Save Scene");
        items.Push("Load Scene");
        items.Push("Import Model...");
        items.Push("Generate Primitive...");
        items.Push("Export Prefab");
        items.Push("Screenshot");
        items.Push("Exit");
        fileMenu_ = CreateMenuDropdown("File", items);
        // Grey out Generate Primitive — MeshGenerator only supports hardcoded shapes
        if (auto* genItem = fileMenu_->GetItem(3))
            genItem->SetColor(Color(0.4f, 0.4f, 0.4f));
        SubscribeToEvent(fileMenu_, E_ITEMSELECTED, URHO3D_HANDLER(Water, HandleFileMenu));
    }

    // Create
    {
        Vector<String> items;
        items.Push("From Prefab...");
        items.Push("Clear Object Brush");
        items.Push("Object Brush: None");
        createMenu_ = CreateMenuDropdown("Create", items);
        // Store reference to the label item (index 2)
        prefabBrushLabel_ = static_cast<Text*>(createMenu_->GetItem(2));
        if (prefabBrushLabel_)
            prefabBrushLabel_->SetColor(Color(0.6f, 0.6f, 0.6f));
        SubscribeToEvent(createMenu_, E_ITEMSELECTED, URHO3D_HANDLER(Water, HandleCreateMenu));
    }

    // Edit
    {
        Vector<String> items;
        items.Push("Undo (Ctrl+Z)");
        items.Push("Redo (Ctrl+Y)");
        items.Push("Translate (T)");
        items.Push("Rotate (R)");
        items.Push("Scale (S)");
        items.Push("World (G)");
        editMenu_ = CreateMenuDropdown("Edit", items);
        SubscribeToEvent(editMenu_, E_ITEMSELECTED, URHO3D_HANDLER(Water, HandleEditMenu));
    }

    // View
    {
        Vector<String> items;
        items.Push("Hierarchy");
        items.Push("Inspector");
        viewMenu_ = CreateMenuDropdown("View", items);
        SubscribeToEvent(viewMenu_, E_ITEMSELECTED, URHO3D_HANDLER(Water, HandleViewMenu));
    }

    // Environment (Menu-based with Terrain submenu)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

        environmentMenu_ = menuBar_->CreateChild<Menu>();
        environmentMenu_->SetStyle("DropDownList");
        environmentMenu_->SetFixedHeight(24);
        environmentMenu_->SetMinWidth(90);

        auto* envLabel = environmentMenu_->CreateChild<Text>();
        envLabel->SetFont(font, 12);
        envLabel->SetText("Environment");
        envLabel->SetColor(Color::WHITE);

        auto* envPopup = new Window(context_);
        envPopup->SetStyleAuto();
        envPopup->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
        envPopup->SetMinWidth(320);
        envPopup->SetDefaultStyle(GetSubsystem<UI>()->GetRoot()->GetDefaultStyle());
        envPopup->SetOpacity(0.85f);
        environmentMenu_->SetPopup(envPopup);
        environmentMenu_->SetPopupOffset(0, environmentMenu_->GetHeight());

        if (terrain_)
            CreateMenuItem(envPopup, "Terrain Tools", 104);

        // Time of Day slider: -12h to +12h offset
        auto* todRow = envPopup->CreateChild<UIElement>();
        todRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
        todRow->SetMinHeight(22);

        auto* todText = todRow->CreateChild<Text>();
        todText->SetFont(font, 12);
        todText->SetText("Time:");
        todText->SetColor(Color(0.9f, 0.9f, 0.9f));
        todText->SetMinWidth(40);

        auto* todSlider = todRow->CreateChild<Slider>();
        todSlider->SetStyleAuto();
        todSlider->SetFixedHeight(16);
        todSlider->SetMinWidth(220);
        todSlider->SetRange(24.0f);  // 0..24 maps to -12..+12
        todSlider->SetValue(12.0f);  // center = 0 offset
        SubscribeToEvent(todSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleTimeOfDaySlider));

        todLabel_ = todRow->CreateChild<Text>();
        todLabel_->SetFont(font, 12);
        todLabel_->SetText("+0:00");
        todLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
        todLabel_->SetMinWidth(50);

        // Date offset sliders: ±15 days, ±6 months, ±15 years
        auto createDateSlider = [&](const String& label, float range, Text*& outLabel, int sliderId) {
            auto* row = envPopup->CreateChild<UIElement>();
            row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
            row->SetMinHeight(22);

            auto* lbl = row->CreateChild<Text>();
            lbl->SetFont(font, 12);
            lbl->SetText(label);
            lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
            lbl->SetMinWidth(55);

            auto* slider = row->CreateChild<Slider>();
            slider->SetStyleAuto();
            slider->SetFixedHeight(16);
            slider->SetMinWidth(200);
            slider->SetRange(range);
            slider->SetValue(range * 0.5f);  // center = 0 offset
            slider->SetVar("SliderID", sliderId);
            SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleDateOffsetSlider));

            outLabel = row->CreateChild<Text>();
            outLabel->SetFont(font, 12);
            outLabel->SetText("0");
            outLabel->SetColor(Color(0.9f, 0.9f, 0.9f));
            outLabel->SetMinWidth(55);
        };

        createDateSlider("Days:", 30.0f, dayOffsetLabel_, 20);      // ±15 days
        createDateSlider("Months:", 12.0f, monthOffsetLabel_, 21);   // ±6 months
        createDateSlider("Years:", 30.0f, yearOffsetLabel_, 22);     // ±15 years

        // Fish wiggle amplitude slider
        auto* fishAmpRow = envPopup->CreateChild<UIElement>();
        fishAmpRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
        fishAmpRow->SetMinHeight(22);

        auto* fishAmpText = fishAmpRow->CreateChild<Text>();
        fishAmpText->SetFont(font, 12);
        fishAmpText->SetText("Fish Wiggle:");
        fishAmpText->SetColor(Color(0.9f, 0.9f, 0.9f));
        fishAmpText->SetMinWidth(80);

        auto* fishAmpSlider = fishAmpRow->CreateChild<Slider>();
        fishAmpSlider->SetStyleAuto();
        fishAmpSlider->SetFixedHeight(16);
        fishAmpSlider->SetMinWidth(180);
        fishAmpSlider->SetRange(1.0f);  // 0..1 maps to 0..0.1 amplitude
        fishAmpSlider->SetValue(0.3f);  // default 0.03
        SubscribeToEvent(fishAmpSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleFishWiggleSlider));

        fishWiggleLabel_ = fishAmpRow->CreateChild<Text>();
        fishWiggleLabel_->SetFont(font, 12);
        fishWiggleLabel_->SetText("0.030");
        fishWiggleLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
        fishWiggleLabel_->SetMinWidth(50);

        // Fish wiggle speed slider
        auto* fishSpdRow = envPopup->CreateChild<UIElement>();
        fishSpdRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
        fishSpdRow->SetMinHeight(22);

        auto* fishSpdText = fishSpdRow->CreateChild<Text>();
        fishSpdText->SetFont(font, 12);
        fishSpdText->SetText("Fish Speed:");
        fishSpdText->SetColor(Color(0.9f, 0.9f, 0.9f));
        fishSpdText->SetMinWidth(80);

        auto* fishSpdSlider = fishSpdRow->CreateChild<Slider>();
        fishSpdSlider->SetStyleAuto();
        fishSpdSlider->SetFixedHeight(16);
        fishSpdSlider->SetMinWidth(180);
        fishSpdSlider->SetRange(1.0f);  // 0..1 maps to 0..8 Hz
        fishSpdSlider->SetValue(0.25f);  // default 2 Hz
        SubscribeToEvent(fishSpdSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleFishSpeedSlider));

        fishSpeedLabel_ = fishSpdRow->CreateChild<Text>();
        fishSpeedLabel_->SetFont(font, 12);
        fishSpeedLabel_->SetText("2.0 Hz");
        fishSpeedLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
        fishSpeedLabel_->SetMinWidth(50);
    }

    // Overlay Options (last menu — debug draw and UI overlays)
    {
        Vector<String> items;
        items.Push("Toggle Fullscreen  (F11)");
        items.Push("Toggle Wireframe  (F)");
        items.Push("Toggle Debug Geometry  (F5)");
        items.Push("Toggle Height Fog  (H)");
        items.Push("Toggle Profiler");
        items.Push("Toggle OOFO Detector");
        // Count fish in scene
        int fishCount = 0;
        const Vector<SharedPtr<Node>>& sceneChildren = scene_->GetChildren();
        for (unsigned ci = 0; ci < sceneChildren.Size(); ++ci)
            if (sceneChildren[ci]->GetName() == "Fish") ++fishCount;
        items.Push("Toggle Fish Detector (" + String(fishCount) + " fish)");
        items.Push("Toggle God Rays");
        overlayMenu_ = CreateMenuDropdown("Overlay", items);
        SubscribeToEvent(overlayMenu_, E_ITEMSELECTED, URHO3D_HANDLER(Water, HandleOverlayMenu));
    }
}

static const char* DEFAULT_INSTRUCTIONS =
    "WASD move, Mouse look, Tab cursor mode\n"
    "LMB raise, RMB lower, Scroll brush size\n"
    "F5 debug, F fill, H fog, F11 fullscreen\n"
    "NumPad Enter hide UI, 1 sun, 2 moon";

void Water::HandleFileMenu(StringHash eventType, VariantMap& eventData)
{
    using namespace ItemSelected;
    int sel = eventData[P_SELECTION].GetI32();

    if (sel < 0 || sel >= (int)fileMenu_->GetNumItems())
        return;

    if (instructionText_)
        instructionText_->SetText(
            "Save/Load Scene, Import Model, Export Prefab\n"
            "Screenshot saves to ~/Documents/Urho3D/Screenshots/");

    switch (sel)
    {
    case 0: ShowSaveSceneDialog(); break;
    case 1: ShowLoadSceneDialog(); break;
    case 2: ShowImportModelDialog(); break;
    case 3: break; // Generate Primitive — disabled (MeshGenerator only supports hardcoded shapes)
    case 4: ShowExportPrefabDialog(); break;
    case 5:
    {
        // Screenshot — save to timestamped PNG
        auto* graphics = GetSubsystem<Graphics>();
        Image screenshot(context_);
        if (graphics->TakeScreenShot(screenshot))
        {
            auto* fs = GetSubsystem<FileSystem>();
            String dir = fs->GetUserDocumentsDir() + "Urho3D/Screenshots/";
            fs->CreateDir(dir);
            String filename = dir + "screenshot_" + String(Time::GetTimeSinceEpoch()) + ".png";
            screenshot.SavePNG(filename);
            URHO3D_LOGINFO("Screenshot saved: " + filename);
        }
        break;
    }
    case 6: engine_->Exit(); break;
    }

    fileMenu_->SetSelection(M_MAX_UNSIGNED);
}

void Water::HandleCreateMenu(StringHash eventType, VariantMap& eventData)
{
    unsigned sel = eventData[ItemSelected::P_SELECTION].GetU32();
    createMenu_->SetSelection(M_MAX_UNSIGNED);

    if (sel >= createMenu_->GetNumItems())
        return;
    if (instructionText_)
        instructionText_->SetText(
            "Load a prefab (.xml) as an object brush\n"
            "LMB places instances on terrain");

    switch (sel)
    {
    case 0: ShowLoadPrefabDialog(); break;
    case 1:
        if (prefabBrush_)
        {
            prefabBrush_->Remove();
            prefabBrush_.Reset();
        }
        UpdatePrefabBrushLabel();
        break;
    case 2: break;  // label item, no action
    }
}

void Water::HandleEditMenu(StringHash eventType, VariantMap& eventData)
{
    unsigned sel = eventData[ItemSelected::P_SELECTION].GetU32();
    editMenu_->SetSelection(M_MAX_UNSIGNED);

    if (sel >= editMenu_->GetNumItems())
        return;
    if (instructionText_)
        instructionText_->SetText(
            "T translate, R rotate, S scale, G toggle local/world\n"
            "Ctrl+Z undo, Ctrl+Y redo, Backspace delete");

    switch (sel)
    {
    case 0: Undo(); break;
    case 1: Redo(); break;
    case 2: gizmoMode_ = 1; break;  // Translate
    case 3: gizmoMode_ = 2; break;  // Rotate
    case 4: gizmoMode_ = 3; break;  // Scale
    case 5:
        gizmoLocal_ = !gizmoLocal_;
        // Update menu item text to reflect current state
        if (editMenu_->GetNumItems() > 5)
        {
            auto* item = editMenu_->GetItem(5);
            auto* label = item ? item->GetChildStaticCast<Text>(0) : nullptr;
            if (label)
                label->SetText(gizmoLocal_ ? "Local (G)" : "World (G)");
        }
        break;
    }
}

void Water::HandleViewMenu(StringHash eventType, VariantMap& eventData)
{
    unsigned sel = eventData[ItemSelected::P_SELECTION].GetU32();
    viewMenu_->SetSelection(M_MAX_UNSIGNED);

    if (sel >= viewMenu_->GetNumItems())
        return;
    if (instructionText_)
        instructionText_->SetText(
            "Hierarchy: scene node tree, double-click to focus\n"
            "Inspector: edit selected node properties");

    switch (sel)
    {
    case 0: ToggleHierarchyWindow(); break;
    case 1: ToggleInspectorWindow(); break;
    }
}

void Water::HandleOverlayMenu(StringHash eventType, VariantMap& eventData)
{
    unsigned sel = eventData[ItemSelected::P_SELECTION].GetU32();
    overlayMenu_->SetSelection(M_MAX_UNSIGNED);

    if (sel >= overlayMenu_->GetNumItems())
        return;
    if (instructionText_)
        instructionText_->SetText(
            "F5 debug, F fill mode, H height fog\n"
            "F11 fullscreen, NumPad Enter hide all");

    switch (sel)
    {
    case 0: // Toggle Fullscreen
        GetSubsystem<Graphics>()->ToggleFullscreen();
        break;
    case 1: // Toggle Wireframe
    {
        auto* camera = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
        if (camera)
            camera->SetFillMode(camera->GetFillMode() == FILL_SOLID ? FILL_WIREFRAME : FILL_SOLID);
        break;
    }
    case 2: // Toggle Debug Geometry
        drawDebug_ = !drawDebug_;
        break;
    case 3: // Toggle Height Fog
        if (zone_)
        {
            bool on = !zone_->GetHeightFog();
            zone_->SetHeightFog(on);
            heightFogOverride_ = on ? 1 : -1;
        }
        break;
    case 4: // Toggle Profiler
        if (profilerUI_)
            profilerUI_->SetVisible(!profilerUI_->IsVisible());
        break;
    case 5: // Toggle OOFO Detector
        oofoRayVisible_ = !oofoRayVisible_;
        break;
    case 6: // Toggle Fish Detector
        fishRayVisible_ = !fishRayVisible_;
        break;
    case 7: // Toggle God Rays
        godRaysEnabled_ = !godRaysEnabled_;
        break;
    }
}

Menu* Water::CreateMenuItem(UIElement* parent, const String& text, int actionId)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    auto* item = parent->CreateChild<Menu>();
    item->SetStyleAuto();
    item->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));

    auto* label = item->CreateChild<Text>();
    label->SetFont(font, 12);
    label->SetText(text);
    label->SetColor(Color(0.9f, 0.9f, 0.9f));

    item->SetMinHeight(22);

    item->SetVar("MenuAction", actionId);
    SubscribeToEvent(item, E_MENUSELECTED, URHO3D_HANDLER(Water, HandleEnvironmentAction));
    return item;
}

void Water::HandleEnvironmentAction(StringHash eventType, VariantMap& eventData)
{
    using namespace MenuSelected;
    auto* element = static_cast<UIElement*>(eventData[P_ELEMENT].GetPtr());
    if (!element) return;

    int action = element->GetVar("MenuAction").GetI32();

    switch (action)
    {
    case 104:
        ToggleTerrainPanel();
        break;
    }

    // Close the top-level popup
    if (environmentMenu_ && environmentMenu_->GetShowPopup())
        environmentMenu_->ShowPopup(false);
}

void Water::HandleTimeOfDaySlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    float val = eventData[P_VALUE].GetFloat();  // 0..24
    timeOfDayOffset_ = val - 12.0f;  // -12..+12

    // Update label
    if (todLabel_)
    {
        int hours = (int)timeOfDayOffset_;
        int mins = (int)(Abs(timeOfDayOffset_ - hours) * 60.0f);
        char buf[16];
        snprintf(buf, sizeof(buf), "%+d:%02d", hours, mins);
        todLabel_->SetText(buf);
    }
}

void Water::HandleDateOffsetSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    float val = eventData[P_VALUE].GetFloat();
    int id = slider->GetVar("SliderID").GetI32();

    if (id == 20)  // ±15 days
    {
        daySliderOffset_ = val - 15.0f;
        if (dayOffsetLabel_)
        {
            char buf[16]; snprintf(buf, sizeof(buf), "%+.0f d", daySliderOffset_);
            dayOffsetLabel_->SetText(buf);
        }
    }
    else if (id == 21)  // ±6 months (in days)
    {
        monthSliderOffset_ = (val - 6.0f) * 30.44f;  // ~30.44 days/month
        if (monthOffsetLabel_)
        {
            float months = val - 6.0f;
            char buf[16]; snprintf(buf, sizeof(buf), "%+.0f mo", months);
            monthOffsetLabel_->SetText(buf);
        }
    }
    else if (id == 22)  // ±15 years (in days)
    {
        yearSliderOffset_ = (val - 15.0f) * 365.25f;
        if (yearOffsetLabel_)
        {
            float years = val - 15.0f;
            char buf[16]; snprintf(buf, sizeof(buf), "%+.0f yr", years);
            yearOffsetLabel_->SetText(buf);
        }
    }

    ApplyDateOffsets();
}

void Water::ApplyDateOffsets()
{
    float totalDayOffset = daySliderOffset_ + monthSliderOffset_ + yearSliderOffset_;
    int newDay = baseDayOfYear_ + (int)totalDayOffset;
    // Wrap to 1-365
    newDay = ((newDay - 1) % 365 + 365) % 365 + 1;
    dayOfYear_ = newDay;

    // Moon age cycles every 29.53 days
    moonAge_ = baseMoonAge_ + totalDayOffset;
    moonAge_ = fmodf(fmodf(moonAge_, 29.53f) + 29.53f, 29.53f);

    // Force atmosphere update
    UpdateAtmosphere(CalculateSunAltitude());
}

void Water::HandleFishWiggleSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    float val = eventData[P_VALUE].GetFloat();  // 0..1
    float amplitude = val * 0.1f;  // 0..0.1 world units

    // Update all fish materials
    const Vector<SharedPtr<Node>>& children = scene_->GetChildren();
    for (unsigned i = 0; i < children.Size(); ++i)
    {
        if (children[i]->GetName() == "Fish")
        {
            auto* sm = children[i]->GetComponent<StaticModel>();
            if (sm && sm->GetMaterial())
            {
                sm->GetMaterial()->SetShaderParameter("WiggleAmplitude", amplitude);
            }
        }
    }

    if (fishWiggleLabel_)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f", amplitude);
        fishWiggleLabel_->SetText(buf);
    }
}

void Water::HandleFishSpeedSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    float val = eventData[P_VALUE].GetFloat();  // 0..1
    float freq = val * 8.0f;  // 0..8 Hz

    // Update all fish materials
    const Vector<SharedPtr<Node>>& children = scene_->GetChildren();
    for (unsigned i = 0; i < children.Size(); ++i)
    {
        if (children[i]->GetName() == "Fish")
        {
            auto* sm = children[i]->GetComponent<StaticModel>();
            if (sm && sm->GetMaterial())
            {
                sm->GetMaterial()->SetShaderParameter("WiggleFrequency", freq);
            }
        }
    }

    if (fishSpeedLabel_)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f Hz", freq);
        fishSpeedLabel_->SetText(buf);
    }
}

void Water::HandleMenuButton(StringHash eventType, VariantMap& eventData)
{
    using namespace Released;
    auto* element = static_cast<UIElement*>(eventData[P_ELEMENT].GetPtr());
    if (!element) return;

    int action = element->GetVar("MenuAction").GetI32();

    switch (action)
    {
    // Brush modes — stay in menu mode so user can also pick shape, then Tab to paint
    case 200:
        brushMode_ = 0;
        if (terrainPanel_) terrainPanel_->SetVisible(false);
        break;
    case 201: brushMode_ = 1; break;
    case 202: brushMode_ = 3; break;
    case 203: brushMode_ = 4; break;
    case 204: brushMode_ = 5; break;

    // Brush shapes
    case 210: brushShape_ = 0; break;
    case 211: brushShape_ = 1; break;
    case 212: brushShape_ = 2; break;
    case 213: brushShape_ = 3; break;
    case 214: brushShape_ = 4; break;
    case 215: brushShape_ = 5; break;
    case 216: brushShape_ = 6; break;

    // Save/Load heightmap via file dialog
    case 230: ShowSaveHeightmapDialog(); break;
    case 231: ShowLoadHeightmapDialog(); break;

    // Compute shaders
    case 240: RunErosion(erosionIterations_); break;
    }

    // Highlight selected mode/shape button
    if (action >= 200 && action <= 204)
        HighlightBrushButton(activeModeBtn_, static_cast<Button*>(element));
    else if (action >= 210 && action <= 216)
        HighlightBrushButton(activeShapeBtn_, static_cast<Button*>(element));

    // Auto-enter camera mode only when brush is active (mode != none)
    if ((action >= 201 && action <= 204) || (action >= 210 && action <= 216))
    {
        if (brushMode_ != 0)
        {
            menuOpen_ = false;
            auto* input = GetSubsystem<Input>();
            GetSubsystem<UI>()->SetFocusElement(nullptr);
            input->SetMouseMode(MM_RELATIVE);
            input->SetMouseVisible(false);
            useMouseMode_ = MM_RELATIVE;
        }
    }
}

void Water::CreateTerrainPanel()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    auto* ui = GetSubsystem<UI>();
    auto* uiRoot = ui->GetRoot();

    terrainPanel_ = new Window(context_);
    terrainPanel_->SetStyle("Window");
    terrainPanel_->SetLayout(LM_VERTICAL, 4, IntRect(6, 6, 6, 6));
    terrainPanel_->SetOpacity(0.85f);
    terrainPanel_->SetVisible(false);
    terrainPanel_->SetPosition(10, 34);  // Below menu bar
    terrainPanel_->SetMovable(true);
    uiRoot->AddChild(terrainPanel_);

    // Title
    auto* title = terrainPanel_->CreateChild<Text>();
    title->SetFont(font, 12);
    title->SetText("Terrain Tools");
    title->SetColor(Color::WHITE);

    // Brush mode label
    auto* modeLabel = terrainPanel_->CreateChild<Text>();
    modeLabel->SetFont(font, 11);
    modeLabel->SetText("Brush Mode:");
    modeLabel->SetColor(Color(0.7f, 0.7f, 0.7f));

    // Brush mode buttons
    auto* modeRow = terrainPanel_->CreateChild<UIElement>();
    modeRow->SetLayout(LM_HORIZONTAL, 2);
    modeRow->SetFixedHeight(24);
    const char* modeNames[] = {"Off", "Height", "Smooth", "Flatten", "Erode"};
    const int modeActions[] = {200, 201, 202, 203, 204};
    for (int i = 0; i < 5; ++i)
    {
        auto* btn = modeRow->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetMinWidth(50);
        btn->SetFixedHeight(22);
        btn->SetVar("MenuAction", modeActions[i]);
        auto* txt = btn->CreateChild<Text>();
        txt->SetFont(font, 11);
        txt->SetText(modeNames[i]);
        txt->SetColor(Color::WHITE);
        txt->SetAlignment(HA_CENTER, VA_CENTER);
        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(Water, HandleMenuButton));
    }

    // Shape label
    auto* shapeLabel = terrainPanel_->CreateChild<Text>();
    shapeLabel->SetFont(font, 11);
    shapeLabel->SetText("Brush Shape:");
    shapeLabel->SetColor(Color(0.7f, 0.7f, 0.7f));

    // Shape buttons with programmatic icons
    auto* shapeRow = terrainPanel_->CreateChild<UIElement>();
    shapeRow->SetLayout(LM_HORIZONTAL, 2);
    shapeRow->SetFixedHeight(28);
    for (int i = 0; i < 7; ++i)
    {
        auto* btn = shapeRow->CreateChild<Button>();
        btn->SetStyleAuto();
        btn->SetFixedSize(28, 26);
        btn->SetVar("MenuAction", 210 + i);
        auto* icon = btn->CreateChild<BorderImage>();
        icon->SetTexture(GenerateShapeIcon(i));
        icon->SetImageRect(IntRect(0, 0, 24, 24));
        icon->SetBlendMode(BLEND_ALPHA);
        icon->SetFixedSize(24, 24);
        icon->SetAlignment(HA_CENTER, VA_CENTER);
        shapeIcons_[i] = icon;
        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(Water, HandleMenuButton));
    }

    // Brush size slider (0.25-50)
    auto* sizeRow = terrainPanel_->CreateChild<UIElement>();
    sizeRow->SetLayout(LM_HORIZONTAL, 4);
    sizeRow->SetFixedHeight(20);
    brushSizeLabel_ = sizeRow->CreateChild<Text>();
    brushSizeLabel_->SetFont(font, 10);
    brushSizeLabel_->SetText("Size: " + String(brushRadius_, 1));
    brushSizeLabel_->SetMinWidth(100);
    auto* sizeSlider = sizeRow->CreateChild<Slider>();
    sizeSlider->SetStyleAuto();
    sizeSlider->SetFixedHeight(16);
    sizeSlider->SetMinWidth(100);
    sizeSlider->SetRange(49.75f);  // 0.25..50
    sizeSlider->SetValue(brushRadius_ - 0.25f);
    sizeSlider->SetVar("SliderID", 10);
    SubscribeToEvent(sizeSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Brush strength slider (0.01-0.2)
    auto* bstrRow = terrainPanel_->CreateChild<UIElement>();
    bstrRow->SetLayout(LM_HORIZONTAL, 4);
    bstrRow->SetFixedHeight(20);
    brushStrLabel_ = bstrRow->CreateChild<Text>();
    brushStrLabel_->SetFont(font, 10);
    brushStrLabel_->SetText("Strength: " + String(brushStrength_, 2));
    brushStrLabel_->SetMinWidth(100);
    auto* bstrSlider = bstrRow->CreateChild<Slider>();
    bstrSlider->SetStyleAuto();
    bstrSlider->SetFixedHeight(16);
    bstrSlider->SetMinWidth(100);
    bstrSlider->SetRange(1.0f);  // normalized 0..1 -> 0.01..5.0
    bstrSlider->SetValue((brushStrength_ - 0.01f) / 4.99f);
    bstrSlider->SetVar("SliderID", 11);
    SubscribeToEvent(bstrSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Rotation slider (-180..+180) + text edit
    auto* rotRow = terrainPanel_->CreateChild<UIElement>();
    rotRow->SetLayout(LM_HORIZONTAL, 4);
    rotRow->SetFixedHeight(20);
    brushRotLabel_ = rotRow->CreateChild<Text>();
    brushRotLabel_->SetFont(font, 10);
    { char buf[16]; sprintf(buf, "Rot: %.1f", brushRotation_); brushRotLabel_->SetText(buf); }
    brushRotLabel_->SetMinWidth(70);
    auto* rotSlider = rotRow->CreateChild<Slider>();
    rotSlider->SetStyleAuto();
    rotSlider->SetFixedHeight(16);
    rotSlider->SetMinWidth(100);
    rotSlider->SetRange(360.0f);  // 0..360 maps to -180..+180
    rotSlider->SetValue(brushRotation_ + 180.0f);
    rotSlider->SetVar("SliderID", 12);
    SubscribeToEvent(rotSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    brushRotEdit_ = rotRow->CreateChild<LineEdit>();
    brushRotEdit_->SetStyleAuto();
    brushRotEdit_->SetFixedWidth(50);
    brushRotEdit_->SetFixedHeight(18);
    { char buf[16]; sprintf(buf, "%.1f", brushRotation_); brushRotEdit_->SetText(buf); }
    brushRotEdit_->SetCursorPosition(0);
    SubscribeToEvent(brushRotEdit_, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleBrushRotEdit));

    // Save/Load heightmap
    auto* ioRow = terrainPanel_->CreateChild<UIElement>();
    ioRow->SetLayout(LM_HORIZONTAL, 4);
    ioRow->SetFixedHeight(26);

    auto* saveBtn = ioRow->CreateChild<Button>();
    saveBtn->SetStyleAuto();
    saveBtn->SetMinWidth(80);
    saveBtn->SetFixedHeight(22);
    saveBtn->SetVar("MenuAction", 230);
    auto* saveTxt = saveBtn->CreateChild<Text>();
    saveTxt->SetFont(font, 11);
    saveTxt->SetText("Save Heightmap");
    saveTxt->SetColor(Color::WHITE);
    saveTxt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(saveBtn, E_RELEASED, URHO3D_HANDLER(Water, HandleMenuButton));

    auto* loadBtn = ioRow->CreateChild<Button>();
    loadBtn->SetStyleAuto();
    loadBtn->SetMinWidth(80);
    loadBtn->SetFixedHeight(22);
    loadBtn->SetVar("MenuAction", 231);
    auto* loadTxt = loadBtn->CreateChild<Text>();
    loadTxt->SetFont(font, 11);
    loadTxt->SetText("Load Heightmap");
    loadTxt->SetColor(Color::WHITE);
    loadTxt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(loadBtn, E_RELEASED, URHO3D_HANDLER(Water, HandleMenuButton));

    // --- Erosion section ---
    auto* divider = terrainPanel_->CreateChild<BorderImage>();
    divider->SetStyle("EditorDivider");

    auto* erosionTitle = terrainPanel_->CreateChild<Text>();
    erosionTitle->SetFont(font, 11);
    erosionTitle->SetText("Hydraulic Erosion");
    erosionTitle->SetColor(Color(0.5f, 1.0f, 0.5f));

    // Iterations slider (10-500)
    auto* iterRow = terrainPanel_->CreateChild<UIElement>();
    iterRow->SetLayout(LM_HORIZONTAL, 4);
    iterRow->SetFixedHeight(20);
    erosionIterLabel_ = iterRow->CreateChild<Text>();
    erosionIterLabel_->SetFont(font, 10);
    erosionIterLabel_->SetText("Iterations: " + String(erosionIterations_));
    erosionIterLabel_->SetMinWidth(100);
    auto* iterSlider = iterRow->CreateChild<Slider>();
    iterSlider->SetStyleAuto();
    iterSlider->SetFixedHeight(16);
    iterSlider->SetMinWidth(100);
    iterSlider->SetRange(99.0f);  // 1..100
    iterSlider->SetValue((float)(erosionIterations_ - 1));
    iterSlider->SetVar("SliderID", 1);
    SubscribeToEvent(iterSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Rainfall slider (0.001-0.05)
    auto* rainRow = terrainPanel_->CreateChild<UIElement>();
    rainRow->SetLayout(LM_HORIZONTAL, 4);
    rainRow->SetFixedHeight(20);
    erosionRainLabel_ = rainRow->CreateChild<Text>();
    erosionRainLabel_->SetFont(font, 10);
    erosionRainLabel_->SetText("Rainfall: " + String(erosionRainfall_, 3));
    erosionRainLabel_->SetMinWidth(100);
    auto* rainSlider = rainRow->CreateChild<Slider>();
    rainSlider->SetStyleAuto();
    rainSlider->SetFixedHeight(16);
    rainSlider->SetMinWidth(100);
    rainSlider->SetRange(1.0f);  // normalized 0..1 -> 0.001..0.05
    rainSlider->SetValue((erosionRainfall_ - 0.001f) / 0.049f);
    rainSlider->SetVar("SliderID", 2);
    SubscribeToEvent(rainSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Erosion strength slider (0.01-1.0)
    auto* strRow = terrainPanel_->CreateChild<UIElement>();
    strRow->SetLayout(LM_HORIZONTAL, 4);
    strRow->SetFixedHeight(20);
    erosionStrLabel_ = strRow->CreateChild<Text>();
    erosionStrLabel_->SetFont(font, 10);
    erosionStrLabel_->SetText("Strength: " + String(erosionStrength_, 2));
    erosionStrLabel_->SetMinWidth(100);
    auto* strSlider = strRow->CreateChild<Slider>();
    strSlider->SetStyleAuto();
    strSlider->SetFixedHeight(16);
    strSlider->SetMinWidth(100);
    strSlider->SetRange(1.0f);  // normalized 0..1 -> 0.01..1.0
    strSlider->SetValue((erosionStrength_ - 0.01f) / 0.99f);
    strSlider->SetVar("SliderID", 3);
    SubscribeToEvent(strSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Max Depth slider (0.0-1.0, fraction of original height)
    auto* depthRow = terrainPanel_->CreateChild<UIElement>();
    depthRow->SetLayout(LM_HORIZONTAL, 4);
    depthRow->SetFixedHeight(20);
    erosionDepthLabel_ = depthRow->CreateChild<Text>();
    erosionDepthLabel_->SetFont(font, 10);
    erosionDepthLabel_->SetText("Max Depth: " + String(erosionMaxDepth_, 2));
    erosionDepthLabel_->SetMinWidth(100);
    auto* depthSlider = depthRow->CreateChild<Slider>();
    depthSlider->SetStyleAuto();
    depthSlider->SetFixedHeight(16);
    depthSlider->SetMinWidth(100);
    depthSlider->SetRange(1.0f);  // 0..1
    depthSlider->SetValue(erosionMaxDepth_);
    depthSlider->SetVar("SliderID", 4);
    SubscribeToEvent(depthSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Min Height Floor slider (0.0-0.5, fraction of height range)
    auto* floorRow = terrainPanel_->CreateChild<UIElement>();
    floorRow->SetLayout(LM_HORIZONTAL, 4);
    floorRow->SetFixedHeight(20);
    erosionFloorLabel_ = floorRow->CreateChild<Text>();
    erosionFloorLabel_->SetFont(font, 10);
    erosionFloorLabel_->SetText("Min Floor: " + String(erosionMinHeight_, 2));
    erosionFloorLabel_->SetMinWidth(100);
    auto* floorSlider = floorRow->CreateChild<Slider>();
    floorSlider->SetStyleAuto();
    floorSlider->SetFixedHeight(16);
    floorSlider->SetMinWidth(100);
    floorSlider->SetRange(1.0f);  // normalized 0..1 -> 0.0..0.5
    floorSlider->SetValue(erosionMinHeight_ / 0.5f);
    floorSlider->SetVar("SliderID", 5);
    SubscribeToEvent(floorSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Ridge Protection slider (0.0-1.0)
    auto* ridgeRow = terrainPanel_->CreateChild<UIElement>();
    ridgeRow->SetLayout(LM_HORIZONTAL, 4);
    ridgeRow->SetFixedHeight(20);
    erosionRidgeLabel_ = ridgeRow->CreateChild<Text>();
    erosionRidgeLabel_->SetFont(font, 10);
    erosionRidgeLabel_->SetText("Ridge Prot: " + String(erosionRidgeProtect_, 2));
    erosionRidgeLabel_->SetMinWidth(100);
    auto* ridgeSlider = ridgeRow->CreateChild<Slider>();
    ridgeSlider->SetStyleAuto();
    ridgeSlider->SetFixedHeight(16);
    ridgeSlider->SetMinWidth(100);
    ridgeSlider->SetRange(1.0f);  // 0..1
    ridgeSlider->SetValue(erosionRidgeProtect_);
    ridgeSlider->SetVar("SliderID", 6);
    SubscribeToEvent(ridgeSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Border Padding slider (0-16 cells)
    auto* borderRow = terrainPanel_->CreateChild<UIElement>();
    borderRow->SetLayout(LM_HORIZONTAL, 4);
    borderRow->SetFixedHeight(20);
    erosionBorderLabel_ = borderRow->CreateChild<Text>();
    erosionBorderLabel_->SetFont(font, 10);
    erosionBorderLabel_->SetText("Border Pad: " + String(erosionBorderPad_));
    erosionBorderLabel_->SetMinWidth(100);
    auto* borderSlider = borderRow->CreateChild<Slider>();
    borderSlider->SetStyleAuto();
    borderSlider->SetFixedHeight(16);
    borderSlider->SetMinWidth(100);
    borderSlider->SetRange(16.0f);  // 0..16
    borderSlider->SetValue((float)erosionBorderPad_);
    borderSlider->SetVar("SliderID", 7);
    SubscribeToEvent(borderSlider, E_SLIDERCHANGED, URHO3D_HANDLER(Water, HandleErosionSlider));

    // Erode button
    auto* erodeBtn = terrainPanel_->CreateChild<Button>();
    erodeBtn->SetStyleAuto();
    erodeBtn->SetMinWidth(120);
    erodeBtn->SetFixedHeight(22);
    erodeBtn->SetVar("MenuAction", 240);
    auto* erodeTxt = erodeBtn->CreateChild<Text>();
    erodeTxt->SetFont(font, 11);
    erodeTxt->SetText("Erode World");
    erodeTxt->SetColor(Color(0.5f, 1.0f, 0.5f));
    erodeTxt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(erodeBtn, E_RELEASED, URHO3D_HANDLER(Water, HandleMenuButton));
}

void Water::HandleErosionSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    float value = eventData[P_VALUE].GetFloat();
    int id = slider->GetVar("SliderID").GetI32();

    switch (id)
    {
    case 1:  // Iterations: 1..100
        erosionIterations_ = 1 + (int)value;
        if (erosionIterLabel_)
            erosionIterLabel_->SetText("Iterations: " + String(erosionIterations_));
        break;
    case 2:  // Rainfall: 0.001..0.05
        erosionRainfall_ = 0.001f + value * 0.049f;
        if (erosionRainLabel_)
            erosionRainLabel_->SetText("Rainfall: " + String(erosionRainfall_, 3));
        break;
    case 3:  // Strength: 0.01..1.0
        erosionStrength_ = 0.01f + value * 0.99f;
        if (erosionStrLabel_)
            erosionStrLabel_->SetText("Strength: " + String(erosionStrength_, 2));
        break;
    case 4:  // Max Depth: 0.0..1.0
        erosionMaxDepth_ = value;
        if (erosionDepthLabel_)
            erosionDepthLabel_->SetText("Max Depth: " + String(erosionMaxDepth_, 2));
        break;
    case 5:  // Min Floor: 0.0..0.5
        erosionMinHeight_ = value * 0.5f;
        if (erosionFloorLabel_)
            erosionFloorLabel_->SetText("Min Floor: " + String(erosionMinHeight_, 2));
        break;
    case 6:  // Ridge Protection: 0.0..1.0
        erosionRidgeProtect_ = value;
        if (erosionRidgeLabel_)
            erosionRidgeLabel_->SetText("Ridge Prot: " + String(erosionRidgeProtect_, 2));
        break;
    case 7:  // Border Padding: 0..16
        erosionBorderPad_ = (int)value;
        if (erosionBorderLabel_)
            erosionBorderLabel_->SetText("Border Pad: " + String(erosionBorderPad_));
        break;
    case 10:  // Brush Size: 0.25..50
        brushRadius_ = 0.25f + value;
        if (brushSizeLabel_)
            brushSizeLabel_->SetText("Size: " + String(brushRadius_, 1));
        break;
    case 11:  // Brush Strength: 0.01..5.0
        brushStrength_ = 0.01f + value * 4.99f;
        if (brushStrLabel_)
            brushStrLabel_->SetText("Strength: " + String(brushStrength_, 2));
        break;
    case 12:  // Brush Rotation: -180..+180
    {
        brushRotation_ = value - 180.0f;
        if (brushRotLabel_)
            { char buf[16]; sprintf(buf, "Rot: %.1f", brushRotation_); brushRotLabel_->SetText(buf); }
        if (brushRotEdit_)
            { char buf[16]; sprintf(buf, "%.1f", brushRotation_); brushRotEdit_->SetText(buf); }
        if (activeShapeBtn_ && shapeIcons_[brushShape_])
            shapeIcons_[brushShape_]->SetTexture(GenerateShapeIcon(brushShape_));
        break;
    }
    }
}

void Water::HandleBrushRotEdit(StringHash eventType, VariantMap& eventData)
{
    float val = (float)strtod(brushRotEdit_->GetText().CString(), nullptr);
    if (val < -180.0f) val = -180.0f;
    if (val > 180.0f) val = 180.0f;
    brushRotation_ = val;
    if (brushRotLabel_)
        { char buf[16]; sprintf(buf, "Rot: %.1f", brushRotation_); brushRotLabel_->SetText(buf); }
    { char buf[16]; sprintf(buf, "%.1f", brushRotation_); brushRotEdit_->SetText(buf); }
    if (shapeIcons_[brushShape_])
        shapeIcons_[brushShape_]->SetTexture(GenerateShapeIcon(brushShape_));
}

void Water::HighlightBrushButton(Button*& active, Button* btn)
{
    // Reset previous
    if (active)
        active->SetColor(Color::WHITE);
    active = btn;
    if (active)
        active->SetColor(Color(0.4f, 0.8f, 1.0f));  // light blue highlight
}

void Water::ToggleTerrainPanel()
{
    if (!terrainPanel_)
        CreateTerrainPanel();

    bool show = !terrainPanel_->IsVisible();
    terrainPanel_->SetVisible(show);

    // Close the environment menu popup but stay in menu mode
    // so user can click the panel buttons
    if (environmentMenu_ && environmentMenu_->GetShowPopup())
        environmentMenu_->ShowPopup(false);
}

// ============================================================================
// Heightmap Save/Load
// ============================================================================

void Water::ShowSaveHeightmapDialog()
{
    if (!editableHeightMap_) return;

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Save Heightmap");
    fileSelector_->SetButtonTexts("Save", "Cancel");

    Vector<String> filters;
    filters.Push("*.png");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetProgramDir() + "Data/Textures/");
    fileSelector_->SetFileName("HeightMap_edited.png");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandleHeightmapSaveChosen));
}

void Water::ShowLoadHeightmapDialog()
{
    if (!terrain_ || !editableHeightMap_) return;

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Load Heightmap");
    fileSelector_->SetButtonTexts("Load", "Cancel");

    Vector<String> filters;
    filters.Push("*.png");
    filters.Push("*.*");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetProgramDir() + "Data/Textures/");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandleHeightmapLoadChosen));
}

void Water::HandleHeightmapSaveChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;
    SaveHeightmapToFile(path);
}

void Water::HandleHeightmapLoadChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;
    LoadHeightmapFromFile(path);
}

void Water::SaveHeightmapToFile(const String& path)
{
    if (!editableHeightMap_) return;

    if (editableHeightMap_->SavePNG(path))
        URHO3D_LOGINFOF("Heightmap saved to %s", path.CString());
    else
        URHO3D_LOGERRORF("Failed to save heightmap to %s", path.CString());
}

void Water::LoadHeightmapFromFile(const String& path)
{
    if (!terrain_ || !editableHeightMap_) return;

    SharedPtr<Image> loaded(new Image(context_));
    File file(context_, path);
    if (!loaded->Load(file))
    {
        URHO3D_LOGERRORF("Failed to load heightmap from %s", path.CString());
        return;
    }

    int hmW = editableHeightMap_->GetWidth();
    int hmH = editableHeightMap_->GetHeight();
    int loadW = loaded->GetWidth();
    int loadH = loaded->GetHeight();
    int loadComps = loaded->GetComponents();

    if (loadW != hmW || loadH != hmH)
    {
        URHO3D_LOGERRORF("Heightmap size mismatch: expected %dx%d, got %dx%d", hmW, hmH, loadW, loadH);
        return;
    }

    unsigned char* dst = editableHeightMap_->GetData();
    unsigned char* src = loaded->GetData();
    int dstComps = editableHeightMap_->GetComponents();
    for (int i = 0; i < hmW * hmH; ++i)
    {
        dst[i * dstComps] = src[i * loadComps];
        dst[i * dstComps + 1] = (loadComps >= 2) ? src[i * loadComps + 1] : 0;
        dst[i * dstComps + 2] = (loadComps >= 3) ? src[i * loadComps + 2] : 0;
        dst[i * dstComps + 3] = (loadComps >= 4) ? src[i * loadComps + 3] : 0;
    }
    terrain_->ApplyHeightMap();
    WakeSleepingBodiesOnTerrain();
    URHO3D_LOGINFOF("Heightmap loaded from %s", path.CString());
}

void Water::ShowExportPrefabDialog()
{
    if (!selectedNode_)
    {
        URHO3D_LOGWARNING("Export Prefab: no node selected");
        return;
    }

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Export Prefab");
    fileSelector_->SetButtonTexts("Save", "Cancel");

    Vector<String> filters;
    filters.Push("*.xml");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetProgramDir() + "Data/Prefabs/");

    String name = selectedNode_->GetName();
    fileSelector_->SetFileName(name.Empty() ? "Prefab.xml" : name + ".xml");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandlePrefabExportChosen));
}

void Water::HandlePrefabExportChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty() || !selectedNode_) return;

    // Temporarily restore original materials (selection outline replaces them)
    auto* model = selectedNode_->GetComponent<StaticModel>();
    Vector<SharedPtr<Material>> maskMats;
    if (model && originalMaterials_.Size())
    {
        for (unsigned i = 0; i < model->GetNumGeometries(); ++i)
            maskMats.Push(SharedPtr<Material>(model->GetMaterial(i)));
        for (unsigned i = 0; i < originalMaterials_.Size() && i < model->GetNumGeometries(); ++i)
            model->SetMaterial(i, originalMaterials_[i]);
    }

    // Zero out position and rotation so prefab is origin-relative, but keep scale
    Vector3 savedPos = selectedNode_->GetPosition();
    Quaternion savedRot = selectedNode_->GetRotation();
    selectedNode_->SetPosition(Vector3::ZERO);
    selectedNode_->SetRotation(Quaternion::IDENTITY);

    File file(context_, path, FILE_WRITE);
    bool ok2 = selectedNode_->SaveXML(file);

    // Restore original transform
    selectedNode_->SetPosition(savedPos);
    selectedNode_->SetRotation(savedRot);

    // Re-apply mask materials for outline
    if (model && maskMats.Size())
    {
        for (unsigned i = 0; i < maskMats.Size() && i < model->GetNumGeometries(); ++i)
            model->SetMaterial(i, maskMats[i]);
    }

    if (ok2)
        URHO3D_LOGINFOF("Prefab exported to %s", path.CString());
    else
        URHO3D_LOGERRORF("Failed to export prefab to %s", path.CString());
}

void Water::ShowLoadPrefabDialog()
{
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Load Prefab");
    fileSelector_->SetButtonTexts("Load", "Cancel");

    Vector<String> filters;
    filters.Push("*.xml");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetProgramDir() + "Data/Prefabs/");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandlePrefabLoadChosen));
}

void Water::HandlePrefabLoadChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;

    // Clean up previous prefab brush
    if (prefabBrush_)
    {
        prefabBrush_->Remove();
        prefabBrush_.Reset();
    }

    // Load XML into an orphan node (not in scene graph)
    XMLFile xmlFile(context_);
    File file(context_, path, FILE_READ);
    if (!xmlFile.Load(file))
    {
        URHO3D_LOGERRORF("Failed to load prefab XML: %s", path.CString());
        return;
    }

    prefabBrush_ = scene_->CreateChild("_PrefabBrush", LOCAL);
    if (!prefabBrush_->LoadXML(xmlFile.GetRoot()))
    {
        URHO3D_LOGERRORF("Failed to parse prefab node: %s", path.CString());
        prefabBrush_->Remove();
        prefabBrush_.Reset();
        return;
    }

    // Hide the template — it's only used for cloning
    prefabBrush_->SetEnabledRecursive(false);

    URHO3D_LOGINFOF("Prefab brush loaded: %s (%d children)", path.CString(), prefabBrush_->GetNumChildren());
    UpdatePrefabBrushLabel();
}

void Water::UpdatePrefabBrushLabel()
{
    if (!prefabBrushLabel_) return;
    if (prefabBrush_)
    {
        String name = prefabBrush_->GetName();
        if (name.Empty() || name == "_PrefabBrush")
            name = "Unnamed";
        prefabBrushLabel_->SetText("Object Brush: " + name);
    }
    else
        prefabBrushLabel_->SetText("Object Brush: None");
}

void Water::HandleRigidBodySleep(StringHash eventType, VariantMap& eventData)
{
    using namespace RigidBodySleep;
    auto* body = static_cast<RigidBody*>(eventData[P_BODY].GetPtr());
    if (!body)
        return;
    body->SetMass(0.0f);
    body->SetLinearDamping(0.0f);
    body->SetAngularDamping(0.0f);
    // Unsubscribe — one-shot event
    if (body->GetNode())
        UnsubscribeFromEvent(body->GetNode(), E_RIGIDBODYSLEEP);
}

void Water::ShowSaveSceneDialog()
{
    if (!scene_) return;

    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Save Scene");
    fileSelector_->SetButtonTexts("Save", "Cancel");

    Vector<String> filters;
    filters.Push("*.xml");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetProgramDir() + "Data/Scenes/");
    fileSelector_->SetFileName("Scene.xml");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandleSceneSaveChosen));
}

void Water::ShowLoadSceneDialog()
{
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Load Scene");
    fileSelector_->SetButtonTexts("Load", "Cancel");

    Vector<String> filters;
    filters.Push("*.xml");
    filters.Push("*.*");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetProgramDir() + "Data/Scenes/");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandleSceneLoadChosen));
}

void Water::HandleSceneSaveChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;

    File file(context_, path, FILE_WRITE);
    if (scene_->SaveXML(file))
        URHO3D_LOGINFOF("Scene saved to %s", path.CString());
    else
        URHO3D_LOGERRORF("Failed to save scene to %s", path.CString());
}

void Water::HandleSceneLoadChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;

    File file(context_, path, FILE_READ);
    if (!file.IsOpen())
    {
        URHO3D_LOGERRORF("Failed to open scene file %s", path.CString());
        return;
    }

    if (scene_->LoadXML(file))
    {
        // Re-bind cached pointers after scene load
        zone_ = scene_->GetComponent<Zone>(true);
        terrain_ = scene_->GetComponent<Terrain>(true);
        if (terrain_)
        {
            Image* origHM = terrain_->GetHeightMap();
            if (origHM)
            {
                int w = origHM->GetWidth();
                int h = origHM->GetHeight();
                int origComps = origHM->GetComponents();
                editableHeightMap_ = new Image(context_);
                editableHeightMap_->SetSize(w, h, 4);
                editableHeightMap_->SetName(origHM->GetName());
                unsigned char* src = origHM->GetData();
                unsigned char* dst = editableHeightMap_->GetData();
                for (int i = 0; i < w * h; ++i)
                {
                    dst[i * 4] = src[i * origComps];
                    dst[i * 4 + 1] = (origComps >= 2) ? src[i * origComps + 1] : 0;
                    dst[i * 4 + 2] = (origComps >= 3) ? src[i * origComps + 2] : 0;
                    dst[i * 4 + 3] = (origComps >= 4) ? src[i * origComps + 3] : 0;
                }
                terrain_->SetHeightMap(editableHeightMap_);
            }
        }
        sunNode_ = scene_->GetChild("Sun", true);
        moonNode_ = scene_->GetChild("Moon", true);
        // sunLight_ is on "DirectionalLight" node, NOT the "Sun" billboard node
        Node* dlNode = scene_->GetChild("DirectionalLight", true);
        sunLight_ = dlNode ? dlNode->GetComponent<Light>() : nullptr;
        Node* mlNode = scene_->GetChild("MoonLight", true);
        moonLight_ = mlNode ? mlNode->GetComponent<Light>() : nullptr;

        // Re-bind water node (it's a scene child, destroyed by LoadXML)
        waterNode_ = scene_->GetChild("Water", true);
        if (waterNode_)
        {
            // Re-derive water planes from loaded water node transform
            waterPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());
            waterClipPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());

            // Update reflection camera planes
            if (reflectionCameraNode_)
            {
                auto* reflectionCamera = reflectionCameraNode_->GetComponent<Camera>();
                if (reflectionCamera)
                {
                    reflectionCamera->SetReflectionPlane(waterPlane_);
                    reflectionCamera->SetClipPlane(waterClipPlane_);
                }
            }

            // Update render path WaterLevel parameter
            if (renderPath_)
                renderPath_->SetShaderParameter("WaterLevel", waterNode_->GetWorldPosition().y_);
        }

        // Re-bind fog parameters from loaded zone
        if (zone_)
        {
            origFogColor_ = zone_->GetFogColor();
            origFogStart_ = zone_->GetFogStart();
            origFogEnd_ = zone_->GetFogEnd();
        }

        // Deselect any previously selected node (it's gone now)
        selectedNode_.Reset();
        originalMaterials_.Clear();

        URHO3D_LOGINFOF("Scene loaded from %s", path.CString());
    }
    else
        URHO3D_LOGERRORF("Failed to load scene from %s", path.CString());
}

void Water::WakeSleepingBodiesOnTerrain()
{
    auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
    if (!physicsWorld || !terrain_)
        return;

    // Build bounding box from terrain dimensions
    Vector3 spacing = terrain_->GetSpacing();
    int hmW = terrain_->GetHeightMap() ? terrain_->GetHeightMap()->GetWidth() : 1025;
    int hmH = terrain_->GetHeightMap() ? terrain_->GetHeightMap()->GetHeight() : 1025;
    float halfW = hmW * spacing.x_ * 0.5f;
    float halfH = hmH * spacing.z_ * 0.5f;
    float maxY = 255.0f * spacing.y_;
    Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();
    BoundingBox terrainBB(
        terrainPos + Vector3(-halfW, 0.0f, -halfH),
        terrainPos + Vector3(halfW, maxY, halfH));

    Vector<RigidBody*> bodies;
    physicsWorld->GetRigidBodies(bodies, terrainBB);
    for (unsigned i = 0; i < bodies.Size(); ++i)
    {
        if (!bodies[i]->IsActive())
            bodies[i]->Activate();
    }
}

// ============================================================================
// Transform Gizmo
// ============================================================================

void Water::DrawGizmo()
{
    if (gizmoMode_ == 0 || !selectedNode_ || selectedNode_.Expired())
        return;

    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!debug)
        return;

    Node* node = selectedNode_;
    Vector3 pos = node->GetWorldPosition();

    // Axis length proportional to distance from camera
    float dist = (pos - cameraNode_->GetWorldPosition()).Length();
    float len = dist * 0.15f;
    float thickness = gizmoMode_ == 3 ? 2.0f : 1.0f;

    // Determine axis directions
    Vector3 axisX, axisY, axisZ;
    if (gizmoLocal_)
    {
        Quaternion rot = node->GetWorldRotation();
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

    Color colX = (gizmoAxis_ == 0 && gizmoDragging_) ? Color::YELLOW : Color::RED;
    Color colY = (gizmoAxis_ == 1 && gizmoDragging_) ? Color::YELLOW : Color::GREEN;
    Color colZ = (gizmoAxis_ == 2 && gizmoDragging_) ? Color::YELLOW : Color::BLUE;

    if (gizmoMode_ == 1) // Translate — lines with arrow tips
    {
        debug->AddLine(pos, pos + axisX * len, colX, false);
        debug->AddLine(pos, pos + axisY * len, colY, false);
        debug->AddLine(pos, pos + axisZ * len, colZ, false);
        // Arrow tips
        float tipLen = len * 0.15f;
        debug->AddLine(pos + axisX * len, pos + axisX * (len - tipLen) + axisY * tipLen * 0.3f, colX, false);
        debug->AddLine(pos + axisX * len, pos + axisX * (len - tipLen) - axisY * tipLen * 0.3f, colX, false);
        debug->AddLine(pos + axisY * len, pos + axisY * (len - tipLen) + axisX * tipLen * 0.3f, colY, false);
        debug->AddLine(pos + axisY * len, pos + axisY * (len - tipLen) - axisX * tipLen * 0.3f, colY, false);
        debug->AddLine(pos + axisZ * len, pos + axisZ * (len - tipLen) + axisY * tipLen * 0.3f, colZ, false);
        debug->AddLine(pos + axisZ * len, pos + axisZ * (len - tipLen) - axisY * tipLen * 0.3f, colZ, false);
    }
    else if (gizmoMode_ == 2) // Rotate — circles
    {
        const int segs = 32;
        for (int i = 0; i < segs; ++i)
        {
            float a0 = (float)i / segs * 360.0f * M_DEGTORAD;
            float a1 = (float)(i + 1) / segs * 360.0f * M_DEGTORAD;
            // X axis ring (in YZ plane)
            debug->AddLine(pos + (axisY * cosf(a0) + axisZ * sinf(a0)) * len,
                          pos + (axisY * cosf(a1) + axisZ * sinf(a1)) * len, colX, false);
            // Y axis ring (in XZ plane)
            debug->AddLine(pos + (axisX * cosf(a0) + axisZ * sinf(a0)) * len,
                          pos + (axisX * cosf(a1) + axisZ * sinf(a1)) * len, colY, false);
            // Z axis ring (in XY plane)
            debug->AddLine(pos + (axisX * cosf(a0) + axisY * sinf(a0)) * len,
                          pos + (axisX * cosf(a1) + axisY * sinf(a1)) * len, colZ, false);
        }
    }
    else if (gizmoMode_ == 3) // Scale — lines with box tips
    {
        debug->AddLine(pos, pos + axisX * len, colX, false);
        debug->AddLine(pos, pos + axisY * len, colY, false);
        debug->AddLine(pos, pos + axisZ * len, colZ, false);
        float boxSz = len * 0.06f;
        debug->AddBoundingBox(BoundingBox(pos + axisX * len - Vector3::ONE * boxSz, pos + axisX * len + Vector3::ONE * boxSz), colX, false);
        debug->AddBoundingBox(BoundingBox(pos + axisY * len - Vector3::ONE * boxSz, pos + axisY * len + Vector3::ONE * boxSz), colY, false);
        debug->AddBoundingBox(BoundingBox(pos + axisZ * len - Vector3::ONE * boxSz, pos + axisZ * len + Vector3::ONE * boxSz), colZ, false);
    }

    // Show mode label
    const char* modeNames[] = {"", "Translate", "Rotate", "Scale"};
    const char* spaceNames[] = {"World", "Local"};
    (void)modeNames; (void)spaceNames;
}

void Water::BeginGizmoDrag(int axis)
{
    if (!selectedNode_ || selectedNode_.Expired() || gizmoMode_ == 0)
        return;

    gizmoAxis_ = axis;
    gizmoDragging_ = true;
    gizmoNodeStart_ = selectedNode_->GetWorldPosition();
    gizmoRotStart_ = selectedNode_->GetRotation();
    gizmoScaleStart_ = selectedNode_->GetScale();

    auto* input = GetSubsystem<Input>();
    IntVector2 mousePos = input->GetMousePosition();

    if (gizmoMode_ == 2) // Rotate — arcball, map mouse to virtual sphere
    {
        auto* camera = cameraNode_->GetComponent<Camera>();
        auto* graphics = GetSubsystem<Graphics>();
        float w = (float)graphics->GetWidth();
        float h = (float)graphics->GetHeight();

        // Project object center to screen
        Vector2 screenCenter = camera->WorldToScreenPoint(gizmoNodeStart_);
        float cx = screenCenter.x_ * w;
        float cy = screenCenter.y_ * h;

        // Virtual sphere radius on screen (based on gizmo visual size)
        float dist = (gizmoNodeStart_ - cameraNode_->GetWorldPosition()).Length();
        float len = dist * 0.15f;
        Vector2 screenEdge = camera->WorldToScreenPoint(gizmoNodeStart_ + Vector3::RIGHT * len);
        float sphereRadius = sqrtf((screenEdge.x_ * w - cx) * (screenEdge.x_ * w - cx) +
                                   (screenEdge.y_ * h - cy) * (screenEdge.y_ * h - cy));
        if (sphereRadius < 10.0f) sphereRadius = 10.0f;

        // Map mouse to sphere point
        float sx = ((float)mousePos.x_ - cx) / sphereRadius;
        float sy = ((float)mousePos.y_ - cy) / sphereRadius;
        float sz2 = 1.0f - sx * sx - sy * sy;
        if (sz2 > 0.0f)
            gizmoDragStart_ = Vector3(sx, -sy, sqrtf(sz2));  // on sphere
        else
            gizmoDragStart_ = Vector3(sx, -sy, 0.0f).Normalized();  // outside sphere, clamp to edge

    }
    else // Translate/Scale — store screen pixel position
    {
        gizmoDragStart_ = Vector3((float)mousePos.x_, (float)mousePos.y_, 0.0f);
    }
}

void Water::UpdateGizmoDrag()
{
    if (!gizmoDragging_ || !selectedNode_ || selectedNode_.Expired())
        return;

    auto* input = GetSubsystem<Input>();
    auto* camera = cameraNode_->GetComponent<Camera>();
    auto* graphics = GetSubsystem<Graphics>();

    // Current mouse position in screen pixels
    IntVector2 mousePos = input->GetMousePosition();
    float mx = (float)mousePos.x_;
    float my = (float)mousePos.y_;

    // Mouse delta from drag start in pixels
    float dx = mx - gizmoDragStart_.x_;
    float dy = my - gizmoDragStart_.y_;

    // Get world-space axis direction
    Vector3 axisDir;
    if (gizmoLocal_)
    {
        Quaternion rot = selectedNode_->GetWorldRotation();
        Vector3 axes[] = {rot * Vector3::RIGHT, rot * Vector3::UP, rot * Vector3::FORWARD};
        axisDir = axes[gizmoAxis_];
    }
    else
    {
        Vector3 axes[] = {Vector3::RIGHT, Vector3::UP, Vector3::FORWARD};
        axisDir = axes[gizmoAxis_];
    }

    // Project axis direction to screen space to get a 2D direction
    float w = (float)graphics->GetWidth();
    float h = (float)graphics->GetHeight();
    Vector2 screenOrigin = camera->WorldToScreenPoint(gizmoNodeStart_);
    Vector2 screenAxisEnd = camera->WorldToScreenPoint(gizmoNodeStart_ + axisDir);
    Vector2 screenDir(screenAxisEnd.x_ - screenOrigin.x_, screenAxisEnd.y_ - screenOrigin.y_);

    // Convert from normalized [0..1] to pixels
    screenDir.x_ *= w;
    screenDir.y_ *= h;

    float screenDirLen = screenDir.Length();
    if (screenDirLen < 1.0f)
        return;  // axis is edge-on to camera

    screenDir /= screenDirLen;  // normalize

    // Project mouse pixel delta onto screen-space axis direction
    float screenDelta = dx * screenDir.x_ + dy * screenDir.y_;

    // Convert screen delta to world units: scale by distance from camera
    float camDist = (gizmoNodeStart_ - cameraNode_->GetWorldPosition()).Length();
    float worldDelta = screenDelta * camDist * 0.002f;  // sensitivity factor

    if (gizmoMode_ == 1) // Translate
    {
        Vector3 newWorldPos = gizmoNodeStart_ + axisDir * worldDelta;
        if (selectedNode_->GetParent())
            selectedNode_->SetPosition(selectedNode_->GetParent()->WorldToLocal(newWorldPos));
        else
            selectedNode_->SetPosition(newWorldPos);
    }
    else if (gizmoMode_ == 2) // Rotate — arcball
    {
        // Recalculate screen center and sphere radius
        float w2 = (float)graphics->GetWidth();
        float h2 = (float)graphics->GetHeight();
        Vector2 screenCenter = camera->WorldToScreenPoint(gizmoNodeStart_);
        float cx = screenCenter.x_ * w2;
        float cy = screenCenter.y_ * h2;

        float camDist2 = (gizmoNodeStart_ - cameraNode_->GetWorldPosition()).Length();
        float gizmoLen = camDist2 * 0.15f;
        Vector2 screenEdge = camera->WorldToScreenPoint(gizmoNodeStart_ + Vector3::RIGHT * gizmoLen);
        float sphereRadius = sqrtf((screenEdge.x_ * w2 - cx) * (screenEdge.x_ * w2 - cx) +
                                   (screenEdge.y_ * h2 - cy) * (screenEdge.y_ * h2 - cy));
        if (sphereRadius < 10.0f) sphereRadius = 10.0f;

        // Map current mouse to sphere point
        float sx = (mx - cx) / sphereRadius;
        float sy = (my - cy) / sphereRadius;
        float sz2 = 1.0f - sx * sx - sy * sy;
        Vector3 currentSphere;
        if (sz2 > 0.0f)
            currentSphere = Vector3(sx, -sy, sqrtf(sz2));
        else
            currentSphere = Vector3(sx, -sy, 0.0f).Normalized();

        // Quaternion from start sphere point to current sphere point
        Vector3 cross = gizmoDragStart_.CrossProduct(currentSphere);
        float dot = gizmoDragStart_.DotProduct(currentSphere);

        // Build rotation quaternion: (cross.x, cross.y, cross.z, dot) then normalize
        Quaternion arcballRot(dot, cross.x_, cross.y_, cross.z_);
        arcballRot.Normalize();

        // arcballRot is in view space — convert to world space
        // View-space axes: X=right, Y=up, Z=into screen (camera facing -Z)
        Quaternion camRot = cameraNode_->GetWorldRotation();
        Quaternion worldRot = camRot * arcballRot * camRot.Inverse();

        // Apply to original rotation
        if (selectedNode_->GetParent())
        {
            Quaternion parentRot = selectedNode_->GetParent()->GetWorldRotation();
            selectedNode_->SetRotation(parentRot.Inverse() * worldRot * parentRot * gizmoRotStart_);
        }
        else
        {
            selectedNode_->SetRotation(worldRot * gizmoRotStart_);
        }
    }
    else if (gizmoMode_ == 3) // Scale
    {
        float scaleFactor = 1.0f + screenDelta * 0.005f;
        if (scaleFactor < 0.01f) scaleFactor = 0.01f;
        Vector3 newScale = gizmoScaleStart_;
        if (gizmoAxis_ == 0) newScale.x_ *= scaleFactor;
        else if (gizmoAxis_ == 1) newScale.y_ *= scaleFactor;
        else newScale.z_ *= scaleFactor;
        selectedNode_->SetScale(newScale);
    }
}

void Water::EndGizmoDrag()
{
    if (!gizmoDragging_)
        return;

    gizmoDragging_ = false;
    gizmoAxis_ = -1;
}

void Water::WakeSelectedNode()
{
    if (!selectedNode_ || selectedNode_.Expired())
        return;

    Vector<RigidBody*> bodies;
    selectedNode_->GetDerivedComponents<RigidBody>(bodies, true);
    for (unsigned b = 0; b < bodies.Size(); ++b)
    {
        RigidBody* rb = bodies[b];
        if (rb->GetMass() == 0.0f)
        {
            rb->SetMass(100.0f);
            rb->SetFriction(0.75f);
            rb->SetLinearDamping(0.9f);
            rb->SetAngularDamping(0.9f);
            SubscribeToEvent(selectedNode_, E_RIGIDBODYSLEEP, URHO3D_HANDLER(Water, HandleRigidBodySleep));
        }
        rb->Activate();
    }
}

// ============================================================================
// Import Model
// ============================================================================

void Water::ShowImportModelDialog()
{
    auto* ui = GetSubsystem<UI>();
    auto* style = ui->GetRoot()->GetDefaultStyle();

    fileSelector_ = new FileSelector(context_);
    fileSelector_->SetDefaultStyle(style);
    fileSelector_->SetTitle("Import Model");
    fileSelector_->SetButtonTexts("Import", "Cancel");

    Vector<String> filters;
    filters.Push("*.fbx");
    filters.Push("*.obj");
    filters.Push("*.dae");
    filters.Push("*.blend");
    filters.Push("*.gltf");
    filters.Push("*.glb");
    filters.Push("*.*");
    fileSelector_->SetFilters(filters, 0);

    auto* fs = GetSubsystem<FileSystem>();
    fileSelector_->SetPath(fs->GetUserDocumentsDir());

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(Water, HandleImportModelChosen));
}

void Water::HandleImportModelChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty())
        return;

    auto* fs = GetSubsystem<FileSystem>();
    auto* cache = GetSubsystem<ResourceCache>();

    // Determine output path
    String baseName = GetFileName(path);
    String outputDir = fs->GetProgramDir() + "Data/Models/";
    String outputMdl = outputDir + baseName + ".mdl";

    // Run AssetTool
    String toolPath = fs->GetProgramDir() + "tool/AssetTool";
    String cmd = "\"" + toolPath + "\" model \"" + path + "\" \"" + outputMdl + "\" -t";
    URHO3D_LOGINFOF("Running: %s", cmd.CString());

    int result = fs->SystemCommand(cmd);
    if (result != 0)
    {
        URHO3D_LOGERRORF("AssetTool failed (exit code %d)", result);
        return;
    }

    // Load the imported model and place it in the scene
    auto* model = cache->GetResource<Model>("Models/" + baseName + ".mdl");
    if (!model)
    {
        URHO3D_LOGERROR("Failed to load imported model");
        return;
    }

    // Place at camera position + forward offset
    auto* camera = cameraNode_->GetComponent<Camera>();
    Vector3 spawnPos = cameraNode_->GetPosition() + cameraNode_->GetDirection() * 10.0f;

    Node* parentNode = (selectedNode_ && !selectedNode_.Expired()) ? selectedNode_.Get() : scene_;
    Node* node = parentNode->CreateChild(baseName);
    auto* sm = node->CreateComponent<StaticModel>();
    sm->SetModel(model);

    // Try to find a matching material
    String matPath = "Materials/" + baseName + ".xml";
    auto* mat = cache->GetResource<Material>(matPath, false);
    if (!mat)
        mat = cache->GetResource<Material>("Materials/Stone.xml", false);
    if (mat)
        sm->SetMaterial(mat);

    sm->SetCastShadows(true);
    node->SetPosition(spawnPos);

    // Add physics
    auto* body = node->CreateComponent<RigidBody>();
    body->SetMass(0.0f);
    auto* shape = node->CreateComponent<CollisionShape>();
    BoundingBox bb = model->GetBoundingBox();
    shape->SetBox(bb.Size(), bb.Center());

    // Record undo
    UndoAction action;
    action.type = UndoAction::NODE_CREATE;
    action.nodeID = node->GetID();
    action.xmlData = SerializeNode(node);
    action.position = node->GetPosition();
    action.rotation = node->GetRotation();
    PushUndo(action);

    SelectNode(node);
    if (hierarchyWindow_ && hierarchyWindow_->IsVisible())
        BuildHierarchyTree();

    URHO3D_LOGINFOF("Imported and placed: %s", baseName.CString());
}

// ============================================================================
// Generate Mesh
// ============================================================================

void Water::ShowGenerateMeshPanel()
{
    if (generateMeshPanel_)
    {
        generateMeshPanel_->SetVisible(!generateMeshPanel_->IsVisible());
        return;
    }

    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = ui->GetRoot()->GetDefaultStyle();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    generateMeshPanel_ = new Window(context_);
    generateMeshPanel_->SetStyleAuto();
    generateMeshPanel_->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));
    generateMeshPanel_->SetAlignment(HA_CENTER, VA_CENTER);
    generateMeshPanel_->SetSize(320, 340);
    generateMeshPanel_->SetMovable(true);
    ui->GetRoot()->AddChild(generateMeshPanel_);

    // Title
    auto* title = generateMeshPanel_->CreateChild<Text>();
    title->SetFont(font, 14);
    title->SetText("Generate Mesh");
    title->SetColor(Color::WHITE);

    // Shape dropdown
    auto* shapeRow = generateMeshPanel_->CreateChild<UIElement>();
    shapeRow->SetLayout(LM_HORIZONTAL, 4);
    shapeRow->SetMinHeight(24);
    auto* shapeLbl = shapeRow->CreateChild<Text>();
    shapeLbl->SetFont(font, 12);
    shapeLbl->SetText("Shape:");
    shapeLbl->SetColor(Color(0.8f, 0.8f, 0.8f));

    meshShapeList_ = shapeRow->CreateChild<DropDownList>();
    meshShapeList_->SetStyleAuto();
    meshShapeList_->SetMinSize(150, 22);
    const char* shapes[] = {"Box", "Sphere", "Cylinder", "Cone", "Capsule"};
    for (int i = 0; i < 5; ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font, 12);
        item->SetText(shapes[i]);
        item->SetColor(Color(0.9f, 0.9f, 0.9f));
        item->SetMinSize(0, 20);
        meshShapeList_->AddItem(item);
    }
    meshShapeList_->SetSelection(0);

    // Helper lambda-style: create param row
    auto CreateParamRow = [&](const String& label, float defVal, float maxVal, Text*& outLabel, Slider*& outSlider)
    {
        auto* row = generateMeshPanel_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4);
        row->SetMinHeight(24);
        outLabel = row->CreateChild<Text>();
        outLabel->SetFont(font, 12);
        outLabel->SetText(label + ": " + String(defVal, 1));
        outLabel->SetColor(Color(0.8f, 0.8f, 0.8f));
        outLabel->SetMinWidth(140);
        outSlider = row->CreateChild<Slider>();
        outSlider->SetStyleAuto();
        outSlider->SetRange(maxVal);
        outSlider->SetValue(defVal);
        outSlider->SetMinSize(120, 18);
    };

    CreateParamRow("Size X / Radius", 1.0f, 5.0f, meshParam1Label_, meshParam1Slider_);
    CreateParamRow("Size Y / Height", 1.0f, 5.0f, meshParam2Label_, meshParam2Slider_);
    CreateParamRow("Segments", 16.0f, 64.0f, meshParam3Label_, meshParam3Slider_);

    // Name edit
    auto* nameRow = generateMeshPanel_->CreateChild<UIElement>();
    nameRow->SetLayout(LM_HORIZONTAL, 4);
    nameRow->SetMinHeight(24);
    auto* nameLbl = nameRow->CreateChild<Text>();
    nameLbl->SetFont(font, 12);
    nameLbl->SetText("Name:");
    nameLbl->SetColor(Color(0.8f, 0.8f, 0.8f));
    meshNameEdit_ = nameRow->CreateChild<LineEdit>();
    meshNameEdit_->SetStyleAuto();
    meshNameEdit_->SetMinSize(180, 22);
    meshNameEdit_->SetText("NewMesh");

    // Material dropdown
    auto* matRow = generateMeshPanel_->CreateChild<UIElement>();
    matRow->SetLayout(LM_HORIZONTAL, 4);
    matRow->SetMinHeight(24);
    auto* matLbl = matRow->CreateChild<Text>();
    matLbl->SetFont(font, 12);
    matLbl->SetText("Material:");
    matLbl->SetColor(Color(0.8f, 0.8f, 0.8f));
    meshMaterialList_ = matRow->CreateChild<DropDownList>();
    meshMaterialList_->SetStyleAuto();
    meshMaterialList_->SetMinSize(150, 22);
    const char* mats[] = {"Stone", "Mushroom", "Jack", "Terrain"};
    for (int i = 0; i < 4; ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font, 12);
        item->SetText(mats[i]);
        item->SetColor(Color(0.9f, 0.9f, 0.9f));
        item->SetMinSize(0, 20);
        meshMaterialList_->AddItem(item);
    }
    meshMaterialList_->SetSelection(0);

    // Generate button
    auto* btn = generateMeshPanel_->CreateChild<Button>();
    btn->SetStyleAuto();
    btn->SetMinHeight(28);
    btn->SetLayout(LM_HORIZONTAL, 0, IntRect(8, 4, 8, 4));
    auto* btnText = btn->CreateChild<Text>();
    btnText->SetFont(font, 13);
    btnText->SetText("Generate");
    btnText->SetColor(Color::WHITE);
    btnText->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(Water, HandleGenerateMesh));
}

void Water::HandleGenerateMesh(StringHash eventType, VariantMap& eventData)
{
    if (!meshShapeList_ || !meshNameEdit_)
        return;

    auto* fs = GetSubsystem<FileSystem>();
    auto* cache = GetSubsystem<ResourceCache>();

    String name = meshNameEdit_->GetText().Trimmed();
    if (name.Empty()) name = "NewMesh";

    unsigned shapeIdx = meshShapeList_->GetSelection();
    const char* shapeNames[] = {"box", "sphere", "cylinder", "cone", "capsule"};
    String shapeType = shapeNames[shapeIdx];

    float p1 = meshParam1Slider_ ? meshParam1Slider_->GetValue() : 1.0f;
    float p2 = meshParam2Slider_ ? meshParam2Slider_->GetValue() : 1.0f;
    int p3 = meshParam3Slider_ ? (int)meshParam3Slider_->GetValue() : 16;
    if (p1 < 0.1f) p1 = 0.1f;
    if (p2 < 0.1f) p2 = 0.1f;
    if (p3 < 3) p3 = 3;

    // Write temp XML for MeshGenerator
    String tempXml = fs->GetTemporaryDir() + name + ".xml";
    String outputMdl = fs->GetProgramDir() + "Data/Models/" + name + ".mdl";

    {
        File f(context_, tempXml, FILE_WRITE);
        if (shapeType == "box")
            f.WriteString("<mesh type=\"box\" sizeX=\"" + String(p1) + "\" sizeY=\"" + String(p2) + "\" sizeZ=\"" + String(p1) + "\" />\n");
        else if (shapeType == "sphere")
            f.WriteString("<mesh type=\"sphere\" radius=\"" + String(p1 * 0.5f) + "\" segments=\"" + String(p3) + "\" rings=\"" + String(p3 / 2) + "\" />\n");
        else if (shapeType == "cylinder")
            f.WriteString("<mesh type=\"cylinder\" radius=\"" + String(p1 * 0.5f) + "\" height=\"" + String(p2) + "\" segments=\"" + String(p3) + "\" />\n");
        else if (shapeType == "cone")
            f.WriteString("<mesh type=\"cone\" radius=\"" + String(p1 * 0.5f) + "\" height=\"" + String(p2) + "\" segments=\"" + String(p3) + "\" />\n");
        else if (shapeType == "capsule")
            f.WriteString("<mesh type=\"capsule\" radius=\"" + String(p1 * 0.5f) + "\" height=\"" + String(p2) + "\" segments=\"" + String(p3) + "\" rings=\"" + String(p3 / 2) + "\" />\n");
    }

    // Run MeshGenerator
    String toolPath = fs->GetProgramDir() + "tool/MeshGenerator";
    String cmd = "\"" + toolPath + "\" \"" + tempXml + "\" \"" + outputMdl + "\"";
    URHO3D_LOGINFOF("Running: %s", cmd.CString());

    int result = fs->SystemCommand(cmd);
    if (result != 0)
    {
        URHO3D_LOGERRORF("MeshGenerator failed (exit code %d)", result);
        return;
    }

    // Load and place
    auto* model = cache->GetResource<Model>("Models/" + name + ".mdl");
    if (!model)
    {
        URHO3D_LOGERROR("Failed to load generated model");
        return;
    }

    Vector3 spawnPos = cameraNode_->GetPosition() + cameraNode_->GetDirection() * 10.0f;

    Node* parentNode = (selectedNode_ && !selectedNode_.Expired()) ? selectedNode_.Get() : scene_;
    Node* node = parentNode->CreateChild(name);
    auto* sm = node->CreateComponent<StaticModel>();
    sm->SetModel(model);
    sm->SetCastShadows(true);

    // Apply selected material
    unsigned matIdx = meshMaterialList_ ? meshMaterialList_->GetSelection() : 0;
    const char* matFiles[] = {"Materials/Stone.xml", "Materials/Mushroom.xml", "Materials/Jack.xml", "Materials/Terrain.xml"};
    auto* mat = cache->GetResource<Material>(matFiles[matIdx], false);
    if (mat)
        sm->SetMaterial(mat);

    node->SetPosition(spawnPos);

    // Add physics
    auto* body = node->CreateComponent<RigidBody>();
    body->SetMass(0.0f);
    auto* shape = node->CreateComponent<CollisionShape>();
    BoundingBox bb = model->GetBoundingBox();
    shape->SetBox(bb.Size(), bb.Center());

    // Record undo
    UndoAction action;
    action.type = UndoAction::NODE_CREATE;
    action.nodeID = node->GetID();
    action.xmlData = SerializeNode(node);
    action.position = node->GetPosition();
    action.rotation = node->GetRotation();
    PushUndo(action);

    SelectNode(node);
    if (hierarchyWindow_ && hierarchyWindow_->IsVisible())
        BuildHierarchyTree();

    URHO3D_LOGINFOF("Generated and placed: %s (%s)", name.CString(), shapeType.CString());

    // Hide panel
    if (generateMeshPanel_)
        generateMeshPanel_->SetVisible(false);
}

void Water::HandleMeshShapeChanged(StringHash eventType, VariantMap& eventData)
{
    // Future: update parameter labels based on selected shape
}

// ============================================================================
// Undo / Redo
// ============================================================================

void Water::PushUndo(const UndoAction& action)
{
    // Truncate any redo history beyond cursor
    while ((int)undoStack_.Size() > undoCursor_)
        undoStack_.Pop();

    undoStack_.Push(action);
    ++undoCursor_;

    // Cap at 50 entries
    if (undoStack_.Size() > 50)
    {
        undoStack_.Erase(0);
        --undoCursor_;
    }
}

void Water::Undo()
{
    if (undoCursor_ <= 0)
        return;

    --undoCursor_;
    const UndoAction& action = undoStack_[undoCursor_];

    switch (action.type)
    {
    case UndoAction::NODE_DELETE:
        {
            // Re-create the deleted node from saved XML
            XMLFile xmlFile(context_);
            MemoryBuffer buf(action.xmlData.CString(), action.xmlData.Length());
            if (xmlFile.Load(buf))
            {
                XMLElement rootElem = xmlFile.GetRoot();
                Node* restored = scene_->CreateChild(String::EMPTY, LOCAL);
                restored->LoadXML(rootElem);
                restored->SetPosition(action.position);
                restored->SetRotation(action.rotation);
            }
        }
        break;

    case UndoAction::NODE_CREATE:
        {
            Node* node = scene_->GetNode(action.nodeID);
            if (node)
                node->Remove();
        }
        break;

    case UndoAction::TERRAIN_EDIT:
        if (action.beforeHM)
            RestoreHeightMap(action.beforeHM);
        break;
    }
}

void Water::Redo()
{
    if (undoCursor_ >= (int)undoStack_.Size())
        return;

    const UndoAction& action = undoStack_[undoCursor_];
    ++undoCursor_;

    switch (action.type)
    {
    case UndoAction::NODE_DELETE:
        {
            // Re-delete the node
            Node* node = scene_->GetNode(action.nodeID);
            if (node)
            {
                DeselectNode();
                node->Remove();
            }
        }
        break;

    case UndoAction::NODE_CREATE:
        {
            // Re-create the node
            XMLFile xmlFile(context_);
            MemoryBuffer buf(action.xmlData.CString(), action.xmlData.Length());
            if (xmlFile.Load(buf))
            {
                XMLElement rootElem = xmlFile.GetRoot();
                Node* restored = scene_->CreateChild(String::EMPTY, LOCAL);
                restored->LoadXML(rootElem);
                restored->SetPosition(action.position);
                restored->SetRotation(action.rotation);
            }
        }
        break;

    case UndoAction::TERRAIN_EDIT:
        if (action.afterHM)
            RestoreHeightMap(action.afterHM);
        break;
    }
}

String Water::SerializeNode(Node* node)
{
    XMLFile xmlFile(context_);
    XMLElement rootElem = xmlFile.CreateRoot("node");
    node->SaveXML(rootElem);
    return xmlFile.ToString();
}

SharedPtr<Image> Water::CloneHeightMap()
{
    if (!editableHeightMap_)
        return SharedPtr<Image>();

    SharedPtr<Image> clone(new Image(context_));
    clone->SetSize(editableHeightMap_->GetWidth(), editableHeightMap_->GetHeight(), editableHeightMap_->GetComponents());
    memcpy(clone->GetData(), editableHeightMap_->GetData(),
           editableHeightMap_->GetWidth() * editableHeightMap_->GetHeight() * editableHeightMap_->GetComponents());
    return clone;
}

void Water::RestoreHeightMap(Image* src)
{
    if (!src || !editableHeightMap_ || !terrain_)
        return;

    memcpy(editableHeightMap_->GetData(), src->GetData(),
           src->GetWidth() * src->GetHeight() * src->GetComponents());
    terrain_->ApplyHeightMap();
    WakeSleepingBodiesOnTerrain();
}

void Water::BeginTerrainStroke()
{
    if (!terrainStrokeActive_)
    {
        terrainStrokeBefore_ = CloneHeightMap();
        terrainStrokeActive_ = true;
    }
}

void Water::EndTerrainStroke()
{
    if (terrainStrokeActive_)
    {
        terrainStrokeActive_ = false;
        SharedPtr<Image> after = CloneHeightMap();

        if (terrainStrokeBefore_ && after)
        {
            UndoAction action;
            action.type = UndoAction::TERRAIN_EDIT;
            action.beforeHM = terrainStrokeBefore_;
            action.afterHM = after;
            PushUndo(action);
        }
        terrainStrokeBefore_.Reset();
    }
}

// ============================================================================
// Minimap
// ============================================================================

void Water::CreateMinimap()
{
    if (!terrain_ || !scene_)
        return;

    auto* ui = GetSubsystem<UI>();
    auto* uiRoot = ui->GetRoot();
    const int texSize = 256;
    const int displaySize = 192;

    // Create RTT texture
    minimapTex_ = new Texture2D(context_);
    minimapTex_->SetSize(texSize, texSize, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET);
    minimapTex_->SetFilterMode(FILTER_BILINEAR);

    // Create top-down orthographic camera
    minimapCameraNode_ = scene_->CreateChild("MinimapCamera", LOCAL);
    auto* minimapCam = minimapCameraNode_->CreateComponent<Camera>();
    minimapCam->SetOrthographic(true);
    minimapCam->SetOrthoSize(320.0f);  // world units visible (160 in each direction)
    minimapCam->SetFarClip(500.0f);
    minimapCam->SetNearClip(1.0f);
    minimapCam->SetFlipVertical(true);  // Vulkan RTT Y-flip compensation
    // Look straight down
    minimapCameraNode_->SetRotation(Quaternion(90.0f, 0.0f, 0.0f));

    // Set up RTT viewport
    RenderSurface* surface = minimapTex_->GetRenderSurface();
    SharedPtr<Viewport> minimapViewport(new Viewport(context_, scene_, minimapCam));
    minimapViewport->SetDrawDebug(false);
    surface->SetViewport(0, minimapViewport);
    surface->SetUpdateMode(SURFACE_UPDATEALWAYS);

    // BorderImage in bottom-right corner
    minimap_ = uiRoot->CreateChild<BorderImage>("Minimap");
    minimap_->SetTexture(minimapTex_);
    minimap_->SetImageRect(IntRect(0, 0, texSize, texSize));
    minimap_->SetFixedSize(displaySize, displaySize);
    minimap_->SetAlignment(HA_RIGHT, VA_BOTTOM);
    minimap_->SetPosition(-8, -8);
    minimap_->SetOpacity(0.85f);

    // Camera dot (always centered)
    minimapCameraDot_ = minimap_->CreateChild<BorderImage>("CameraDot");
    minimapCameraDot_->SetFixedSize(6, 6);
    minimapCameraDot_->SetColor(Color::RED);
    minimapCameraDot_->SetEnabled(false);
    minimapCameraDot_->SetPosition(displaySize / 2 - 3, displaySize / 2 - 3);
}

void Water::UpdateMinimapCamera()
{
    if (!minimapCameraNode_ || !cameraNode_)
        return;

    // Position minimap camera above the main camera, looking straight down
    Vector3 camPos = cameraNode_->GetWorldPosition();
    minimapCameraNode_->SetPosition(Vector3(camPos.x_, 200.0f, camPos.z_));
    // Rotate to match camera yaw so forward is always "up" on the minimap
    // +180 because at pitch=90 (looking down), screen "up" is -Z, but camera forward at yaw=0 is +Z
    minimapCameraNode_->SetRotation(Quaternion(90.0f, yaw_, 0.0f));
}

static void DrawLineOnImage(Image* img, int x0, int y0, int x1, int y1, const Color& color)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int w = img->GetWidth(), h = img->GetHeight();
    for (;;)
    {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
            img->SetPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

SharedPtr<Texture2D> Water::GenerateShapeIcon(int shape)
{
    const int sz = 24;
    const int cx = sz / 2, cy = sz / 2;
    const float r = 10.0f;
    Color white(1.0f, 1.0f, 1.0f, 1.0f);

    SharedPtr<Image> img(new Image(context_));
    img->SetSize(sz, sz, 4);
    // Clear to transparent
    for (int y = 0; y < sz; ++y)
        for (int x = 0; x < sz; ++x)
            img->SetPixel(x, y, Color(0, 0, 0, 0));

    // Compute vertices
    Vector<IntVector2> verts;

    switch (shape)
    {
    case 0: // Circle — draw using distance field for clean result
    {
        for (int y = 0; y < sz; ++y)
        {
            for (int x = 0; x < sz; ++x)
            {
                float dx = (float)(x - cx), dy = (float)(y - cy);
                float dist = sqrtf(dx * dx + dy * dy);
                // Anti-aliased ring: 1px wide at radius r
                float alpha = 1.0f - fabsf(dist - r);
                if (alpha > 0.0f)
                    img->SetPixel(x, y, Color(1.0f, 1.0f, 1.0f, Clamp(alpha, 0.0f, 1.0f)));
            }
        }
        // No polygon verts needed, skip line drawing
        SharedPtr<Texture2D> tex(new Texture2D(context_));
        tex->SetSize(sz, sz, Graphics::GetRGBAFormat());
        tex->SetData(img);
        tex->SetFilterMode(FILTER_BILINEAR);
        return tex;
    }
    case 1: // Square
        verts.Push(IntVector2(cx - (int)r, cy - (int)r));
        verts.Push(IntVector2(cx + (int)r, cy - (int)r));
        verts.Push(IntVector2(cx + (int)r, cy + (int)r));
        verts.Push(IntVector2(cx - (int)r, cy + (int)r));
        break;
    case 2: // Triangle (point-up)
    {
        float triAngles[3] = {-M_PI / 2.0f, M_PI * 5.0f / 6.0f, M_PI / 6.0f};
        for (int i = 0; i < 3; ++i)
            verts.Push(IntVector2(cx + (int)(cosf(triAngles[i]) * r), cy + (int)(sinf(triAngles[i]) * r)));
        break;
    }
    case 3: // Star (5-pointed)
    {
        float innerR = r * 0.38f;
        for (int i = 0; i < 10; ++i)
        {
            float a = -M_PI / 2.0f + (float)i / 10.0f * 2.0f * M_PI;
            float rad = (i % 2 == 0) ? r : innerR;
            verts.Push(IntVector2(cx + (int)(cosf(a) * rad), cy + (int)(sinf(a) * rad)));
        }
        break;
    }
    default: // 4=pentagon(5), 5=hexagon(6), 6=octagon(8)
    {
        int sides;
        switch (shape) { case 4: sides = 5; break; case 5: sides = 6; break; default: sides = 8; break; }
        for (int i = 0; i < sides; ++i)
        {
            float a = -M_PI / 2.0f + (float)i / (float)sides * 2.0f * M_PI;
            verts.Push(IntVector2(cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r)));
        }
        break;
    }
    }

    // Apply brush rotation to vertices
    if (brushRotation_ != 0.0f)
    {
        float ra = brushRotation_ * M_DEGTORAD;
        float cs = cosf(ra), sn = sinf(ra);
        for (unsigned i = 0; i < verts.Size(); ++i)
        {
            float lx = (float)(verts[i].x_ - cx);
            float ly = (float)(verts[i].y_ - cy);
            verts[i].x_ = cx + (int)(lx * cs - ly * sn);
            verts[i].y_ = cy + (int)(lx * sn + ly * cs);
        }
    }

    // Draw lines between vertices
    for (unsigned i = 0; i < verts.Size(); ++i)
    {
        unsigned j = (i + 1) % verts.Size();
        DrawLineOnImage(img, verts[i].x_, verts[i].y_, verts[j].x_, verts[j].y_, white);
    }

    SharedPtr<Texture2D> tex(new Texture2D(context_));
    tex->SetSize(sz, sz, Graphics::GetRGBAFormat());
    tex->SetData(img);
    tex->SetFilterMode(FILTER_NEAREST);
    return tex;
}

// ============================================================================
// Camera & Input
// ============================================================================

void Water::MoveCamera(float timeStep)
{
    auto* input = GetSubsystem<Input>();

    // Numpad Enter = toggle all UI and debug draw visibility
    if (input->GetKeyPress(KEY_KP_ENTER))
    {
        auto* uiRoot = GetSubsystem<UI>()->GetRoot();
        uiRoot->SetVisible(!uiRoot->IsVisible());
        drawDebug_ = uiRoot->IsVisible();
    }

    // Tab = toggle cursor/camera mode (checked before UI focus guard)
    if (input->GetKeyPress(KEY_TAB))
    {
        menuOpen_ = !menuOpen_;
        if (menuOpen_)
        {
            input->SetMouseMode(MM_FREE);
            input->SetMouseVisible(true);
            useMouseMode_ = MM_FREE;
        }
        else
        {
            GetSubsystem<UI>()->SetFocusElement(nullptr);
            input->SetMouseMode(MM_RELATIVE);
            input->SetMouseVisible(false);
            useMouseMode_ = MM_RELATIVE;
        }
    }

    // [ and ] = cycle brush shape (before focus guard so it works with terrain panel open)
    {
        static const char* shapeNames[] = {"Circle", "Square", "Triangle", "Star", "Pentagon", "Hexagon", "Octagon"};
        if (input->GetKeyPress(KEY_LEFTBRACKET))
        {
            brushShape_ = (brushShape_ + 6) % 7;
            URHO3D_LOGINFOF("Brush shape: %s (%d)", shapeNames[brushShape_], brushShape_);
        }
        if (input->GetKeyPress(KEY_RIGHTBRACKET))
        {
            brushShape_ = (brushShape_ + 1) % 7;
            URHO3D_LOGINFOF("Brush shape: %s (%d)", shapeNames[brushShape_], brushShape_);
        }
    }

    // Click outside dismisses floating panels (terrain tools, generate mesh)
    if (input->GetMouseButtonPress(MOUSEB_LEFT) || input->GetMouseButtonPress(MOUSEB_RIGHT))
    {
        auto* uiElem = GetSubsystem<UI>()->GetElementAt(input->GetMousePosition());
        if (!uiElem)
        {
            if (terrainPanel_ && terrainPanel_->IsVisible())
                terrainPanel_->SetVisible(false);
            if (generateMeshPanel_ && generateMeshPanel_->IsVisible())
                generateMeshPanel_->SetVisible(false);
        }
    }

    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    // Hotkeys
    if (input->GetKeyPress(KEY_SPACE))
        drawDebug_ = !drawDebug_;

    if (input->GetKeyPress(KEY_Z))
    {
        auto* camera = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
        if (camera)
            camera->SetFillMode(camera->GetFillMode() == FILL_SOLID ? FILL_WIREFRAME : FILL_SOLID);
    }

    if (input->GetKeyPress(KEY_F))
    {
        auto* camera = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
        if (camera)
            camera->SetFillMode(camera->GetFillMode() == FILL_SOLID ? FILL_WIREFRAME : FILL_SOLID);
    }

    if (input->GetKeyPress(KEY_H) && zone_)
    {
        bool on = !zone_->GetHeightFog();
        zone_->SetHeightFog(on);
        heightFogOverride_ = on ? 1 : -1;
    }

    if (input->GetKeyPress(KEY_F5))
        drawDebug_ = !drawDebug_;

    if (input->GetKeyPress(KEY_F11))
        GetSubsystem<Graphics>()->ToggleFullscreen();

    // Undo/Redo
    if ((input->GetQualifiers() & QUAL_CTRL) && input->GetKeyPress(KEY_Z))
        Undo();
    if ((input->GetQualifiers() & QUAL_CTRL) && input->GetKeyPress(KEY_Y))
        Redo();

    // Gizmo mode shortcuts — wake physics when exiting the tool entirely
    if (input->GetKeyPress(KEY_T))
    {
        int prev = gizmoMode_;
        gizmoMode_ = (prev == 1) ? 0 : 1;
        if (prev != 0 && gizmoMode_ == 0)
            WakeSelectedNode();
    }
    if (input->GetKeyPress(KEY_R))
    {
        int prev = gizmoMode_;
        gizmoMode_ = (prev == 2) ? 0 : 2;
        if (prev != 0 && gizmoMode_ == 0)
            WakeSelectedNode();
    }
    if (input->GetKeyPress(KEY_S))
    {
        int prev = gizmoMode_;
        gizmoMode_ = (prev == 3) ? 0 : 3;
        if (prev != 0 && gizmoMode_ == 0)
            WakeSelectedNode();
    }
    if (input->GetKeyPress(KEY_G))
    {
        gizmoLocal_ = !gizmoLocal_;
        // Update edit menu label
        if (editMenu_ && editMenu_->GetNumItems() > 5)
        {
            auto* item = editMenu_->GetItem(5);
            auto* label = item ? item->GetChildStaticCast<Text>(0) : nullptr;
            if (label)
                label->SetText(gizmoLocal_ ? "Local (G)" : "World (G)");
        }
    }

    if ((input->GetKeyPress(KEY_BACKSPACE) || input->GetKeyPress(KEY_DELETE)) && selectedNode_ && !selectedNode_.Expired())
    {
        Node* node = selectedNode_;
        // Record undo before deleting
        UndoAction action;
        action.type = UndoAction::NODE_DELETE;
        action.nodeID = node->GetID();
        action.xmlData = SerializeNode(node);
        action.position = node->GetPosition();
        action.rotation = node->GetRotation();
        PushUndo(action);

        DeselectNode();
        node->Remove();
    }

    // 1/2 = track sun/moon
    if (input->GetKeyPress(KEY_1))
    {
        if (sunNode_)
        {
            Vector3 dir = (sunNode_->GetPosition() - cameraNode_->GetPosition()).Normalized();
            yaw_ = atan2f(dir.x_, dir.z_) * M_RADTODEG;
            pitch_ = asinf(Clamp(dir.y_, -1.0f, 1.0f)) * M_RADTODEG;
            cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
        }
    }
    if (input->GetKeyPress(KEY_2))
    {
        if (moonNode_)
        {
            Vector3 dir = (moonNode_->GetPosition() - cameraNode_->GetPosition()).Normalized();
            yaw_ = atan2f(dir.x_, dir.z_) * M_RADTODEG;
            pitch_ = asinf(Clamp(dir.y_, -1.0f, 1.0f)) * M_RADTODEG;
            cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
        }
    }

    // Scroll wheel: tool active = adjust brush radius, no tool = inch camera forward/back
    int wheel = input->GetMouseMoveWheel();
    if (wheel != 0)
    {
        if (brushMode_ != 0)
            brushRadius_ = Clamp(brushRadius_ + wheel * 0.25f, 0.25f, 50.0f);
        else
            cameraNode_->Translate(Vector3::FORWARD * (float)wheel * 0.5f);
    }

    // Mouse look (when menu closed)
    if (!menuOpen_)
    {
        const float MOUSE_SENSITIVITY = 0.1f;
        IntVector2 mouseMove = input->GetMouseMove();
        yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
        pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
        pitch_ = Clamp(pitch_, -90.0f, 90.0f);
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
    }

    // WASD movement
    {
        const float MOVE_SPEED = 20.0f;
        Vector3 oldPos = cameraNode_->GetPosition();

        if (input->GetKeyDown(KEY_W))
            cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_S))
            cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_A))
            cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
        if (input->GetKeyDown(KEY_D))
            cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);

        // Sphere cast collision
        Vector3 newPos = cameraNode_->GetPosition();
        Vector3 moveVec = newPos - oldPos;
        float moveDist = moveVec.Length();
        if (moveDist > 0.001f)
        {
            auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
            if (physicsWorld)
            {
                const float CAMERA_RADIUS = 0.5f;
                Vector3 moveNorm = moveVec / moveDist;
                PhysicsRaycastResult result;
                physicsWorld->SphereCast(result, Ray(oldPos, moveNorm), CAMERA_RADIUS, moveDist);
                if (result.body_)
                {
                    Vector3 hitPos = oldPos + moveNorm * Max(result.distance_ - 0.01f, 0.0f);
                    float remaining = moveDist - result.distance_;
                    if (remaining > 0.001f)
                    {
                        Vector3 slideVec = moveVec - result.normal_ * moveVec.DotProduct(result.normal_);
                        float slideLen = slideVec.Length();
                        if (slideLen > 0.001f)
                        {
                            Vector3 slideNorm = slideVec / slideLen;
                            float slideDist = Min(slideLen, remaining);
                            PhysicsRaycastResult slideResult;
                            physicsWorld->SphereCast(slideResult, Ray(hitPos, slideNorm), CAMERA_RADIUS, slideDist);
                            if (slideResult.body_)
                                hitPos += slideNorm * Max(slideResult.distance_ - 0.01f, 0.0f);
                            else
                                hitPos += slideNorm * slideDist;
                        }
                    }
                    cameraNode_->SetPosition(hitPos);
                }
            }
        }
    }

    // Clamp camera above terrain
    if (terrain_)
    {
        Vector3 camPos = cameraNode_->GetPosition();
        float terrainHeight = terrain_->GetHeight(camPos) + 1.0f;
        if (camPos.y_ < terrainHeight)
        {
            camPos.y_ = terrainHeight;
            cameraNode_->SetPosition(camPos);
        }
    }

    // Terrain brush raycast
    hasBrushHit_ = false;
    bool showBrush = (brushMode_ != 0) || (terrainPanel_ && terrainPanel_->IsVisible()) || prefabBrush_;
    if (terrain_ && showBrush)
    {
        auto* camera = cameraNode_->GetComponent<Camera>();
        auto* graphics = GetSubsystem<Graphics>();
        if (camera && graphics)
        {
            Ray cameraRay;
            if (menuOpen_)
            {
                IntVector2 pos = input->GetMousePosition();
                cameraRay = camera->GetScreenRay(
                    (float)pos.x_ / (float)graphics->GetWidth(),
                    (float)pos.y_ / (float)graphics->GetHeight());
            }
            else
            {
                cameraRay = camera->GetScreenRay(0.5f, 0.5f);
            }

            auto* octree = scene_->GetComponent<Octree>();
            if (octree)
            {
                Vector<RayQueryResult> results;
                RayOctreeQuery query(results, cameraRay, RAY_TRIANGLE, 750.0f);
                octree->Raycast(query);
                for (unsigned i = 0; i < results.Size(); ++i)
                {
                    if (!dynamic_cast<TerrainPatch*>(results[i].drawable_))
                        continue;

                    cachedBrushHit_ = results[i].position_;
                    cachedBrushNormal_ = results[i].normal_;
                    hasBrushHit_ = true;

                    // Don't paint if mouse is over a UI element (sliders, panels, menus)
                    {
                        auto* uiElem = GetSubsystem<UI>()->GetElementAt(input->GetMousePosition());
                        if (uiElem)
                            break;
                    }

                    if (brushMode_ == 1)
                    {
                        if (input->GetMouseButtonDown(MOUSEB_LEFT))
                        {
                            BeginTerrainStroke();
                            ApplyBrush(cachedBrushHit_, timeStep);
                        }
                        if (input->GetMouseButtonDown(MOUSEB_RIGHT))
                        {
                            BeginTerrainStroke();
                            ApplyLowerBrush(cachedBrushHit_, timeStep);
                        }
                    }
                    else
                    {
                        if (input->GetMouseButtonDown(MOUSEB_LEFT) || input->GetMouseButtonDown(MOUSEB_RIGHT))
                        {
                            BeginTerrainStroke();
                            ApplyBrush(cachedBrushHit_, timeStep);
                        }
                    }
                    break;
                }
            }
        }
    }

    // Prefab brush instancing — clone on left click
    if (prefabBrush_ && hasBrushHit_ && input->GetMouseButtonPress(MOUSEB_LEFT))
    {
        // Don't instance if mouse is over UI
        auto* uiElem = GetSubsystem<UI>()->GetElementAt(input->GetMousePosition());
        if (!uiElem)
        {
            Node* instance = prefabBrush_->Clone();
            instance->SetEnabledRecursive(true);

            // Force collision shapes to rebuild (may not have been created while disabled)
            Vector<CollisionShape*> shapes;
            instance->GetDerivedComponents<CollisionShape>(shapes, true);
            for (unsigned s = 0; s < shapes.Size(); ++s)
                shapes[s]->ApplyAttributes();

            // Set up mass before positioning so body is in correct state when placed
            Vector<RigidBody*> bodies;
            instance->GetDerivedComponents<RigidBody>(bodies, true);
            for (unsigned b = 0; b < bodies.Size(); ++b)
            {
                RigidBody* rb = bodies[b];
                if (rb->GetMass() == 0.0f)
                {
                    rb->SetMass(100.0f);
                    rb->SetFriction(0.75f);
                    rb->SetLinearDamping(0.9f);
                    rb->SetAngularDamping(0.9f);
                    SubscribeToEvent(instance, E_RIGIDBODYSLEEP, URHO3D_HANDLER(Water, HandleRigidBodySleep));
                }
            }

            // Orient clone to terrain surface normal
            Quaternion surfaceRot;
            surfaceRot.FromRotationTo(Vector3::UP, cachedBrushNormal_);
            instance->SetRotation(surfaceRot);

            // Offset along surface normal so the object sits on the terrain
            float yOffset = 0.0f;
            {
                Vector<Drawable*> drawables;
                instance->GetDerivedComponents<Drawable>(drawables, true);
                BoundingBox combined;
                for (unsigned d = 0; d < drawables.Size(); ++d)
                    combined.Merge(drawables[d]->GetBoundingBox());
                if (combined.Defined())
                    yOffset = -combined.min_.y_ * instance->GetScale().y_;
            }
            instance->SetPosition(cachedBrushHit_ + cachedBrushNormal_ * (yOffset + 0.2f));

            // Activate after positioning so Bullet sees the correct initial transform
            for (unsigned b = 0; b < bodies.Size(); ++b)
                bodies[b]->Activate();

            URHO3D_LOGINFOF("Prefab instanced at (%.1f, %.1f, %.1f)", cachedBrushHit_.x_, cachedBrushHit_.y_, cachedBrushHit_.z_);
        }
    }

    // End terrain stroke on mouse release
    if (terrainStrokeActive_ && !input->GetMouseButtonDown(MOUSEB_LEFT) && !input->GetMouseButtonDown(MOUSEB_RIGHT))
        EndTerrainStroke();

    // Gizmo drag handling
    if (gizmoDragging_ && input->GetMouseButtonDown(MOUSEB_LEFT))
    {
        UpdateGizmoDrag();
    }
    else if (gizmoDragging_ && !input->GetMouseButtonDown(MOUSEB_LEFT))
    {
        EndGizmoDrag();
    }

    // Gizmo picking — screen-space, only in cursor mode (TAB)
    if (menuOpen_ && gizmoMode_ != 0 && selectedNode_ && !selectedNode_.Expired() &&
        brushMode_ == 0 && input->GetMouseButtonPress(MOUSEB_LEFT) && !gizmoDragging_)
    {
        auto* uiElem = GetSubsystem<UI>()->GetElementAt(input->GetMousePosition());
        if (!uiElem)
        {
            auto* camera = cameraNode_->GetComponent<Camera>();
            auto* graphics = GetSubsystem<Graphics>();
            IntVector2 mousePos = input->GetMousePosition();
            float w = (float)graphics->GetWidth();
            float h = (float)graphics->GetHeight();
            float mx = (float)mousePos.x_;
            float my = (float)mousePos.y_;

            Vector3 nodePos = selectedNode_->GetWorldPosition();
            float dist = (nodePos - cameraNode_->GetWorldPosition()).Length();
            float len = dist * 0.15f;

            // Project node origin to screen
            Vector2 screenOrigin = camera->WorldToScreenPoint(nodePos);
            float ox = screenOrigin.x_ * w;
            float oy = screenOrigin.y_ * h;

            if (gizmoMode_ == 2) // Rotate — arcball, click anywhere near object
            {
                float screenDist = sqrtf((mx - ox) * (mx - ox) + (my - oy) * (my - oy));
                // Accept click within ~2x the gizmo ring radius on screen
                Vector2 screenRingEdge = camera->WorldToScreenPoint(nodePos + Vector3::RIGHT * len);
                float ringScreenRadius = sqrtf((screenRingEdge.x_ * w - ox) * (screenRingEdge.x_ * w - ox) +
                                               (screenRingEdge.y_ * h - oy) * (screenRingEdge.y_ * h - oy));
                if (screenDist < ringScreenRadius * 2.0f)
                {
                    BeginGizmoDrag(0);  // axis ignored for arcball
                }
            }
            else // Translate/Scale — axis picking
            {
                Vector3 axes[3];
                if (gizmoLocal_)
                {
                    Quaternion rot = selectedNode_->GetWorldRotation();
                    axes[0] = rot * Vector3::RIGHT;
                    axes[1] = rot * Vector3::UP;
                    axes[2] = rot * Vector3::FORWARD;
                }
                else
                {
                    axes[0] = Vector3::RIGHT;
                    axes[1] = Vector3::UP;
                    axes[2] = Vector3::FORWARD;
                }

                int bestAxis = -1;
                float bestDist = 15.0f;  // 15 pixel threshold

                for (int a = 0; a < 3; ++a)
                {
                    Vector3 axisEnd = nodePos + axes[a] * len;
                    Vector2 screenEnd = camera->WorldToScreenPoint(axisEnd);
                    float ex = screenEnd.x_ * w;
                    float ey = screenEnd.y_ * h;

                    float segDx = ex - ox;
                    float segDy = ey - oy;
                    float segLen2 = segDx * segDx + segDy * segDy;
                    if (segLen2 < 4.0f) continue;

                    float t = ((mx - ox) * segDx + (my - oy) * segDy) / segLen2;
                    t = Clamp(t, 0.0f, 1.0f);
                    float closestX = ox + segDx * t;
                    float closestY = oy + segDy * t;
                    float d = sqrtf((mx - closestX) * (mx - closestX) + (my - closestY) * (my - closestY));

                    if (d < bestDist && t > 0.1f)
                    {
                        bestDist = d;
                        bestAxis = a;
                    }
                }

                if (bestAxis >= 0)
                {
                    BeginGizmoDrag(bestAxis);
                }
            }
        }
    }

    // Object selection raycast (when no brush mode active, and not gizmo dragging)
    if (brushMode_ == 0 && !gizmoDragging_ && input->GetMouseButtonPress(MOUSEB_LEFT))
    {
        auto* uiElem = GetSubsystem<UI>()->GetElementAt(input->GetMousePosition());
        if (!uiElem)
        {
            auto* camera = cameraNode_->GetComponent<Camera>();
            auto* graphics = GetSubsystem<Graphics>();
            if (camera && graphics)
            {
                Ray pickRay;
                if (menuOpen_)
                {
                    IntVector2 pos = input->GetMousePosition();
                    pickRay = camera->GetScreenRay(
                        (float)pos.x_ / (float)graphics->GetWidth(),
                        (float)pos.y_ / (float)graphics->GetHeight());
                }
                else
                {
                    pickRay = camera->GetScreenRay(0.5f, 0.5f);
                }

                auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
                if (physicsWorld)
                {
                    Vector<PhysicsRaycastResult> results;
                    physicsWorld->Raycast(results, pickRay, 300.0f);

                    Node* hitNode = nullptr;
                    for (unsigned i = 0; i < results.Size(); ++i)
                    {
                        Node* node = results[i].body_->GetNode();
                        if (node && !node->HasComponent<Terrain>())
                        {
                            hitNode = node;
                            break;
                        }
                    }

                    if (hitNode)
                        SelectNode(hitNode);
                    else
                        DeselectNode();
                }
            }
        }
    }

    // Reset flatten lock when mouse released
    if (brushMode_ == 4 && !input->GetMouseButtonDown(MOUSEB_LEFT) && !input->GetMouseButtonDown(MOUSEB_RIGHT))
        lockedFlattenHeight_ = -1.0f;

    // Reflection camera aspect ratio
    if (reflectionCameraNode_)
    {
        auto* graphics = GetSubsystem<Graphics>();
        auto* reflectionCamera = reflectionCameraNode_->GetComponent<Camera>();
        if (reflectionCamera)
            reflectionCamera->SetAspectRatio((float)graphics->GetWidth() / (float)graphics->GetHeight());
    }
}

// ============================================================================
// Terrain Brush
// ============================================================================

// Unified shape falloff: build the same vertices as DrawBrushOutline,
// then test point-in-polygon + distance-to-nearest-edge for falloff.
float Water::BrushShapeFalloff(float dx, float dz, float radius) const
{
    if (radius < 0.001f)
        return -1.0f;

    // Apply inverse rotation to test point (rotates shape visually)
    if (brushRotation_ != 0.0f)
    {
        float ra = -brushRotation_ * DEG_TO_RAD;
        float cs = cosf(ra), sn = sinf(ra);
        float rdx = dx * cs - dz * sn;
        float rdz = dx * sn + dz * cs;
        dx = rdx;
        dz = rdz;
    }

    // Circle uses simple radial distance
    if (brushShape_ == 0)
    {
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > radius) return -1.0f;
        return 1.0f - dist / radius;
    }

    // Square uses max-axis distance
    if (brushShape_ == 1)
    {
        float adx = fabsf(dx), adz = fabsf(dz);
        if (adx > radius || adz > radius) return -1.0f;
        float edgeDist = Max(adx, adz);
        return 1.0f - edgeDist / radius;
    }

    // All other shapes: build vertex list, test point-in-polygon
    float vx[20], vz[20];
    int nv = 0;

    switch (brushShape_)
    {
    case 2: // Triangle (point-up) — must match DrawBrushOutline angles
    {
        float angles[3] = {90.0f * DEG_TO_RAD, 210.0f * DEG_TO_RAD, 330.0f * DEG_TO_RAD};
        for (int i = 0; i < 3; ++i)
        {
            vx[i] = radius * cosf(angles[i]);
            vz[i] = radius * sinf(angles[i]);
        }
        nv = 3;
        break;
    }
    case 3: // Star (5-pointed, 10 vertices) — start at top (+π/2)
    {
        float innerR = radius * 0.38f;
        for (int i = 0; i < 10; ++i)
        {
            float a = (float)(M_PI / 2.0) + (float)i / 10.0f * 2.0f * (float)M_PI;
            float r = (i % 2 == 0) ? radius : innerR;
            vx[i] = r * cosf(a);
            vz[i] = r * sinf(a);
        }
        nv = 10;
        break;
    }
    default: // 4=pentagon(5), 5=hexagon(6), 6=octagon(8) — start at top (+π/2)
    {
        int sides;
        switch (brushShape_) { case 4: sides = 5; break; case 5: sides = 6; break; default: sides = 8; break; }
        for (int i = 0; i < sides; ++i)
        {
            float a = (float)(M_PI / 2.0) + (float)i / (float)sides * 2.0f * (float)M_PI;
            vx[i] = radius * cosf(a);
            vz[i] = radius * sinf(a);
        }
        nv = sides;
        break;
    }
    }

    // Point-in-polygon (ray casting / crossing number)
    int crossings = 0;
    for (int i = 0; i < nv; ++i)
    {
        int j = (i + 1) % nv;
        if ((vz[i] <= dz && vz[j] > dz) || (vz[j] <= dz && vz[i] > dz))
        {
            float t = (dz - vz[i]) / (vz[j] - vz[i]);
            if (dx < vx[i] + t * (vx[j] - vx[i]))
                crossings++;
        }
    }
    if ((crossings & 1) == 0)
        return -1.0f; // outside

    // Falloff: distance to nearest edge, normalized
    float minDist = 1e9f;
    for (int i = 0; i < nv; ++i)
    {
        int j = (i + 1) % nv;
        float ex = vx[j] - vx[i], ez = vz[j] - vz[i];
        float len = sqrtf(ex * ex + ez * ez);
        if (len < 0.001f) continue;
        // Perpendicular distance from point to edge line
        float d = fabsf((dx - vx[i]) * (-ez / len) + (dz - vz[i]) * (ex / len));
        if (d < minDist) minDist = d;
    }
    // Normalize by inradius approximation (half of radius)
    float inradius = radius * 0.4f;
    return Clamp(minDist / inradius, 0.0f, 1.0f);
}

void Water::ApplyBrush(const Vector3& worldPos, float timeStep)
{
    if (!terrain_ || !editableHeightMap_ || brushMode_ == 0)
        return;

    // Diagnostic: log shape on first frame of each stroke
    static bool wasActive = false;
    bool active = true;
    if (!wasActive)
        URHO3D_LOGINFOF("ApplyBrush: shape=%d mode=%d radius=%.1f", brushShape_, brushMode_, brushRadius_);
    wasActive = active;

    IntVector2 center = terrain_->WorldToHeightMap(worldPos);
    int hmW = editableHeightMap_->GetWidth();
    int hmH = editableHeightMap_->GetHeight();
    int radius = (int)brushRadius_;
    float baseStrength = (brushMode_ == 3) ? smoothStrength_ : brushStrength_;
    float strength = baseStrength * timeStep * 2.5f;

    unsigned char* data = editableHeightMap_->GetData();
    int comps = editableHeightMap_->GetComponents();

    auto readH = [&](int px, int py) -> float {
        int idx = (py * hmW + px) * comps;
        return ((float)data[idx] + (float)data[idx + 1] / 256.0f
                + (float)data[idx + 2] / 65536.0f + (float)data[idx + 3] / 16777216.0f) / 255.0f;
    };
    auto writeH = [&](int px, int py, float h) {
        if (h < 0.0f) h = 0.0f;
        if (h > 1.0f) h = 1.0f;
        float scaled = h * 255.0f;
        int idx = (py * hmW + px) * comps;
        data[idx] = (unsigned char)scaled;
        float rem = (scaled - (float)data[idx]) * 256.0f;
        data[idx + 1] = (unsigned char)rem;
        float rem2 = (rem - (float)data[idx + 1]) * 256.0f;
        data[idx + 2] = (unsigned char)rem2;
        data[idx + 3] = (unsigned char)((rem2 - (float)data[idx + 2]) * 256.0f);
    };

    float flattenHeight = 0.0f;
    if (brushMode_ == 4)
    {
        // Lock height on first click, keep it until mouse released
        if (lockedFlattenHeight_ < 0.0f)
        {
            int cx = Clamp(center.x_, 0, hmW - 1);
            int cy = Clamp(center.y_, 0, hmH - 1);
            lockedFlattenHeight_ = readH(cx, cy);
        }
        flattenHeight = lockedFlattenHeight_;
    }

    for (int dz = -radius; dz <= radius; ++dz)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            float falloff = BrushShapeFalloff((float)dx, (float)dz, brushRadius_);
            if (falloff < 0.0f)
                continue;

            int px = center.x_ + dx;
            int py = center.y_ + dz;
            if (px < 0 || px >= hmW || py < 0 || py >= hmH)
                continue;

            float h = readH(px, py);

            switch (brushMode_)
            {
            case 1: h += strength * falloff; break;
            case 2: h -= strength * falloff; break;
            case 3: // Smooth — average with neighbours
            {
                float sum = 0.0f;
                int count = 0;
                for (int ny = -1; ny <= 1; ++ny)
                {
                    for (int nx = -1; nx <= 1; ++nx)
                    {
                        int npx = px + nx, npy = py + ny;
                        if (npx >= 0 && npx < hmW && npy >= 0 && npy < hmH)
                        {
                            sum += readH(npx, npy);
                            ++count;
                        }
                    }
                }
                float avg = sum / (float)count;
                h = h + (avg - h) * falloff * baseStrength;
                break;
            }
            case 4:
                // Gradually pull toward target height, modulated by falloff and strength
                h = h + (flattenHeight - h) * falloff * baseStrength;
                break;
            case 5:
            {
                // Erosion brush: move material downhill to lowest neighbor
                float lowest = h;
                int lx = px, ly = py;
                for (int ny = -1; ny <= 1; ++ny)
                {
                    for (int nx = -1; nx <= 1; ++nx)
                    {
                        if (nx == 0 && ny == 0) continue;
                        int npx = px + nx, npy = py + ny;
                        if (npx < 0 || npx >= hmW || npy < 0 || npy >= hmH) continue;
                        float nh = readH(npx, npy);
                        if (nh < lowest) { lowest = nh; lx = npx; ly = npy; }
                    }
                }
                float diff = h - lowest;
                if (diff > 0.0001f)
                {
                    float transfer = diff * falloff * baseStrength;
                    h -= transfer;
                    writeH(lx, ly, lowest + transfer);
                }
                break;
            }
            }

            writeH(px, py, h);
        }
    }

    terrain_->ApplyHeightMap();


    Vector3 spacing = terrain_->GetSpacing();
    float worldRadius = brushRadius_ * spacing.x_ + 2.0f;
    auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
    if (physicsWorld)
    {
        Vector<RigidBody*> bodies;
        physicsWorld->GetRigidBodies(bodies, Sphere(worldPos, worldRadius));
        for (unsigned i = 0; i < bodies.Size(); ++i)
            bodies[i]->Activate();
    }
}

void Water::ApplyLowerBrush(const Vector3& worldPos, float timeStep)
{
    int saved = brushMode_;
    brushMode_ = 2;
    ApplyBrush(worldPos, timeStep);
    brushMode_ = saved;
}

void Water::DrawBrushOutline(const Vector3& worldPos)
{
    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!debug || !terrain_)
        return;

    Vector3 spacing = terrain_->GetSpacing();
    float worldRadius = brushRadius_ * spacing.x_;

    Color brushColor = Color::GREEN;
    if (brushMode_ == 2) brushColor = Color::RED;
    else if (brushMode_ == 3) brushColor = Color::CYAN;
    else if (brushMode_ == 4) brushColor = Color(1.0f, 0.5f, 0.0f);
    else if (brushMode_ == 5) brushColor = Color(0.5f, 0.3f, 0.1f);  // brown for erosion

    // Build outline vertices based on shape
    Vector<Vector3> verts;

    switch (brushShape_)
    {
    case 0: // Circle — 32 segments
    {
        const int segs = 32;
        for (int i = 0; i <= segs; ++i)
        {
            float a = (float)i / (float)segs * 2.0f * M_PI;
            verts.Push(worldPos + Vector3(cosf(a) * worldRadius, 0.0f, sinf(a) * worldRadius));
        }
        break;
    }
    case 1: // Square
    {
        float r = worldRadius;
        verts.Push(worldPos + Vector3(-r, 0, -r));
        verts.Push(worldPos + Vector3( r, 0, -r));
        verts.Push(worldPos + Vector3( r, 0,  r));
        verts.Push(worldPos + Vector3(-r, 0,  r));
        verts.Push(worldPos + Vector3(-r, 0, -r)); // close
        break;
    }
    case 2: // Triangle (equilateral, point-up)
    {
        for (int i = 0; i <= 3; ++i)
        {
            float a;
            switch (i % 3)
            {
            case 0: a = 90.0f * DEG_TO_RAD; break;
            case 1: a = 210.0f * DEG_TO_RAD; break;
            case 2: a = 330.0f * DEG_TO_RAD; break;
            default: a = 90.0f * DEG_TO_RAD; break;
            }
            verts.Push(worldPos + Vector3(cosf(a) * worldRadius, 0.0f, sinf(a) * worldRadius));
        }
        break;
    }
    case 3: // Star (5-pointed, 10 vertices alternating outer/inner)
    {
        float innerR = worldRadius * 0.38f;
        for (int i = 0; i <= 10; ++i)
        {
            float a = (float)(i % 10) / 10.0f * 2.0f * M_PI + M_PI / 2.0f; // start at top
            float r = (i % 2 == 0) ? worldRadius : innerR;
            verts.Push(worldPos + Vector3(cosf(a) * r, 0.0f, sinf(a) * r));
        }
        break;
    }
    default: // Regular polygon: 4=pentagon(5), 5=hexagon(6), 6=octagon(8)
    {
        int sides;
        switch (brushShape_)
        {
        case 4: sides = 5; break;
        case 5: sides = 6; break;
        case 6: sides = 8; break;
        default: sides = 6; break;
        }
        for (int i = 0; i <= sides; ++i)
        {
            float a = (float)(i % sides) / (float)sides * 2.0f * M_PI + M_PI / 2.0f; // start at top
            verts.Push(worldPos + Vector3(cosf(a) * worldRadius, 0.0f, sinf(a) * worldRadius));
        }
        break;
    }
    }

    // Apply rotation to outline vertices
    if (brushRotation_ != 0.0f)
    {
        float ra = brushRotation_ * DEG_TO_RAD;
        float cs = cosf(ra), sn = sinf(ra);
        for (unsigned i = 0; i < verts.Size(); ++i)
        {
            float lx = verts[i].x_ - worldPos.x_;
            float lz = verts[i].z_ - worldPos.z_;
            verts[i].x_ = worldPos.x_ + lx * cs - lz * sn;
            verts[i].z_ = worldPos.z_ + lx * sn + lz * cs;
        }
    }

    // Snap each vertex to terrain height
    for (unsigned i = 0; i < verts.Size(); ++i)
        verts[i].y_ = terrain_->GetHeight(verts[i]) + 0.2f;

    for (unsigned i = 1; i < verts.Size(); ++i)
        debug->AddLine(verts[i - 1], verts[i], brushColor);

    // Center crosshair
    float cross = worldRadius * 0.15f;
    float centerY = terrain_->GetHeight(worldPos) + 0.3f;
    Vector3 center(worldPos.x_, centerY, worldPos.z_);
    debug->AddLine(center + Vector3(-cross, 0, 0), center + Vector3(cross, 0, 0), brushColor);
    debug->AddLine(center + Vector3(0, 0, -cross), center + Vector3(0, 0, cross), brushColor);
}

// ============================================================================
// Celestial Bodies
// ============================================================================

void Water::CreateCelestialBodies()
{
    auto* cache = GetSubsystem<ResourceCache>();
    sunNode_ = scene_->CreateChild("Sun");
    auto* sunTech = cache->GetResource<Technique>("Techniques/CelestialBodyDiffMap.xml");
    sunMat_ = new Material(context_);
    sunMat_->SetTechnique(0, sunTech);
    sunMat_->SetShaderParameter("MatDiffColor", Color(1.0f, 1.0f, 1.0f, 1.0f));
    sunMat_->SetTexture(TU_DIFFUSE, cache->GetResource<Texture2D>("Textures/Sun.png"));
    auto* sunBB = sunNode_->CreateComponent<BillboardSet>();
    sunBB->SetNumBillboards(1);
    sunBB->SetMaterial(sunMat_);
    sunBB->SetFaceCameraMode(FC_ROTATE_XYZ);
    sunBB->SetSorted(false);
    Billboard* sunQuad = sunBB->GetBillboard(0);
    sunQuad->position_ = Vector3::ZERO;
    sunQuad->size_ = Vector2(30.0f, 30.0f);
    sunQuad->enabled_ = true;
    sunBB->Commit();

    moonNode_ = scene_->CreateChild("Moon");
    moonMat_ = new Material(context_);
    moonMat_->SetTechnique(0, sunTech);
    moonMat_->SetShaderParameter("MatDiffColor", Color(1.0f, 1.0f, 1.0f, 1.0f));
    moonMat_->SetTexture(TU_DIFFUSE, cache->GetResource<Texture2D>("Textures/Moon.png"));
    auto* moonBB = moonNode_->CreateComponent<BillboardSet>();
    moonBB->SetNumBillboards(1);
    moonBB->SetMaterial(moonMat_);
    moonBB->SetFaceCameraMode(FC_ROTATE_XYZ);
    moonBB->SetSorted(false);
    Billboard* moonQuad = moonBB->GetBillboard(0);
    moonQuad->position_ = Vector3::ZERO;
    moonQuad->size_ = Vector2(20.0f, 20.0f);
    moonQuad->enabled_ = true;
    moonBB->Commit();

    Node* moonLightNode = scene_->CreateChild("MoonLight");
    moonLight_ = moonLightNode->CreateComponent<Light>();
    moonLight_->SetLightType(LIGHT_DIRECTIONAL);
    moonLight_->SetColor(Color(0.6f, 0.6f, 1.0f));
    moonLight_->SetCastShadows(true);
    moonLight_->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    moonLight_->SetShadowCascade(CascadeParameters(10.0f, 50.0f, 200.0f, 0.0f, 0.8f));
    moonLight_->SetEnabled(false);

    float sunAlt = CalculateSunAltitude();
    float sunAz = CalculateSunAzimuth(sunAlt);
    sunNode_->SetPosition(AltAzToFlatEarth(sunAlt, sunAz));

    float moonAlt = CalculateMoonAltitude();
    float moonAz = CalculateMoonAzimuth(moonAlt);
    Vector3 initCamPos = cameraNode_ ? cameraNode_->GetWorldPosition() : Vector3::ZERO;
    moonNode_->SetPosition(initCamPos + AltAzToFlatEarth(moonAlt, moonAz));
    moonNode_->LookAt(initCamPos);

    sunLight_->GetNode()->SetDirection(-AltAzToFlatEarth(sunAlt, sunAz).Normalized());

    UpdateAtmosphere(sunAlt);
}

float Water::CalculateSunAltitude()
{
    float decl = 23.44f * sinf((360.0f / 365.0f) * (dayOfYear_ - 81) * DEG_TO_RAD);
    float hourAngle = 15.0f * (timeOfDay_ + timeOfDayOffset_ - SOLAR_NOON);
    float latRad = MELBOURNE_LAT * DEG_TO_RAD;
    float declRad = decl * DEG_TO_RAD;
    float haRad = hourAngle * DEG_TO_RAD;
    float sinAlt = sinf(latRad) * sinf(declRad) + cosf(latRad) * cosf(declRad) * cosf(haRad);
    return asinf(Clamp(sinAlt, -1.0f, 1.0f)) * RAD_TO_DEG;
}

float Water::CalculateSunAzimuth(float altitude)
{
    float decl = 23.44f * sinf((360.0f / 365.0f) * (dayOfYear_ - 81) * DEG_TO_RAD);
    float hourAngle = 15.0f * (timeOfDay_ + timeOfDayOffset_ - SOLAR_NOON);
    float latRad = MELBOURNE_LAT * DEG_TO_RAD;
    float declRad = decl * DEG_TO_RAD;
    float altRad = altitude * DEG_TO_RAD;
    float cosAlt = cosf(altRad);
    if (fabsf(cosAlt) < 0.001f) return 180.0f;
    float cosAz = (sinf(declRad) - sinf(altRad) * sinf(latRad)) / (cosAlt * cosf(latRad));
    cosAz = Clamp(cosAz, -1.0f, 1.0f);
    float az = acosf(cosAz) * RAD_TO_DEG;
    if (hourAngle > 0.0f) az = 360.0f - az;
    return az;
}

float Water::CalculateMoonAltitude()
{
    float moonHourOffset = moonAge_ * (360.0f / 29.53f);
    float moonHourAngle = 15.0f * (timeOfDay_ + timeOfDayOffset_ - SOLAR_NOON) - moonHourOffset;
    float moonEclLon = (360.0f / 29.53f) * moonAge_;
    float sunEclLon = (360.0f / 365.0f) * (dayOfYear_ - 81);
    float totalEclLon = sunEclLon + moonEclLon;
    float moonDecl = 23.44f * sinf(totalEclLon * DEG_TO_RAD);
    float latRad = MELBOURNE_LAT * DEG_TO_RAD;
    float declRad = moonDecl * DEG_TO_RAD;
    float haRad = moonHourAngle * DEG_TO_RAD;
    float sinAlt = sinf(latRad) * sinf(declRad) + cosf(latRad) * cosf(declRad) * cosf(haRad);
    return asinf(Clamp(sinAlt, -1.0f, 1.0f)) * RAD_TO_DEG;
}

float Water::CalculateMoonAzimuth(float moonAlt)
{
    float moonHourOffset = moonAge_ * (360.0f / 29.53f);
    float moonHourAngle = 15.0f * (timeOfDay_ + timeOfDayOffset_ - SOLAR_NOON) - moonHourOffset;
    float moonEclLon = (360.0f / 29.53f) * moonAge_;
    float sunEclLon = (360.0f / 365.0f) * (dayOfYear_ - 81);
    float totalEclLon = sunEclLon + moonEclLon;
    float moonDecl = 23.44f * sinf(totalEclLon * DEG_TO_RAD);
    float latRad = MELBOURNE_LAT * DEG_TO_RAD;
    float declRad = moonDecl * DEG_TO_RAD;
    float altRad = moonAlt * DEG_TO_RAD;
    float cosAlt = cosf(altRad);
    if (fabsf(cosAlt) < 0.001f) return 180.0f;
    float cosAz = (sinf(declRad) - sinf(altRad) * sinf(latRad)) / (cosAlt * cosf(latRad));
    cosAz = Clamp(cosAz, -1.0f, 1.0f);
    float az = acosf(cosAz) * RAD_TO_DEG;
    if (moonHourAngle > 0.0f) az = 360.0f - az;
    return az;
}

Vector3 Water::AltAzToFlatEarth(float altitude, float azimuth, float distance)
{
    float altRad = altitude * DEG_TO_RAD;
    float azRad = azimuth * DEG_TO_RAD;
    float y = distance * sinf(altRad);
    float horizDist = distance * cosf(altRad);
    float x = horizDist * sinf(azRad);
    float z = horizDist * cosf(azRad);
    return Vector3(x, y, z);
}

void Water::UpdateCelestialBodies(float timeStep)
{
    timeOfDay_ += timeStep * CELESTIAL_TIME_SCALE / 3600.0f;
    if (timeOfDay_ >= 24.0f)
    {
        timeOfDay_ -= 24.0f;
        dayOfYear_++;
        if (dayOfYear_ > 365) dayOfYear_ = 1;
    }
    moonAge_ += timeStep * CELESTIAL_TIME_SCALE / 86400.0f;
    if (moonAge_ >= 29.53f) moonAge_ -= 29.53f;

    cloudAngle_ += timeStep * CELESTIAL_TIME_SCALE * 6.2831853f / (18.0f * 3600.0f);
    if (cloudAngle_ > 6.2831853f) cloudAngle_ -= 6.2831853f;
    // Cloud angle includes time offset so scrubbing visibly rotates clouds
    float cloudTotal = cloudAngle_ + timeOfDayOffset_ * 6.2831853f / 18.0f;

    float starAngle = 6.2831853f * ((timeOfDay_ + timeOfDayOffset_) / 24.0f + (float)dayOfYear_ / 365.25f);

    if (skyboxMat_)
    {
        skyboxMat_->SetShaderParameter("CloudAngle", cloudTotal);
        skyboxMat_->SetShaderParameter("StarAngle", starAngle);
    }

    cachedSunAlt_ = CalculateSunAltitude();
    cachedSunAz_ = CalculateSunAzimuth(cachedSunAlt_);
    Vector3 sunOffset = AltAzToFlatEarth(cachedSunAlt_, cachedSunAz_);
    Vector3 camPos = cameraNode_ ? cameraNode_->GetWorldPosition() : Vector3::ZERO;
    if (sunNode_)
    {
        sunNode_->SetPosition(camPos + sunOffset);
        sunNode_->LookAt(camPos);
        auto* bb = sunNode_->GetComponent<BillboardSet>();
        if (bb) bb->SetEnabled(cachedSunAlt_ > 0.0f);  // hide at/below horizon
    }
    if (sunLight_)
        sunLight_->GetNode()->SetDirection((camPos - (sunNode_ ? sunNode_->GetPosition() : Vector3::ZERO)).Normalized());

    cachedMoonAlt_ = CalculateMoonAltitude();
    cachedMoonAz_ = CalculateMoonAzimuth(cachedMoonAlt_);
    Vector3 moonOffset = AltAzToFlatEarth(cachedMoonAlt_, cachedMoonAz_);
    if (moonNode_)
    {
        moonNode_->SetPosition(camPos + moonOffset);
        moonNode_->LookAt(camPos);
        auto* bb = moonNode_->GetComponent<BillboardSet>();
        if (bb) bb->SetEnabled(cachedMoonAlt_ > 0.0f);  // hide at/below horizon
    }
    if (moonLight_)
        moonLight_->GetNode()->SetDirection(-moonOffset.Normalized());

    UpdateAtmosphere(cachedSunAlt_);

    // --- God rays tracking ---
    if (renderPath_ && cameraNode_)
    {
        auto* cam = cameraNode_->GetComponent<Camera>();
        bool rayActive = false;

        // Try sun first, then moon
        // Screen radius: billboard half-size / distance / tan(FOV/2) * 0.5
        float halfFov = cam->GetFov() * 0.5f * M_DEGTORAD;
        float tanHalf = tanf(halfFov);

        if (godRaysEnabled_ && sunNode_ && cachedSunAlt_ > -5.0f)
        {
            Vector3 toSun = (sunNode_->GetWorldPosition() - camPos);
            float sunDist = toSun.Length();
            toSun /= sunDist;
            if (cameraNode_->GetWorldDirection().DotProduct(toSun) > 0.0f)
            {
                Vector2 screenPos = cam->WorldToScreenPoint(sunNode_->GetWorldPosition());

                // Sun halo varies with altitude in three phases:
                //   High (20+):  pale yellow, tight, bright
                //   Sunset (0-20): deepening red, wider, dimmer
                //   Below horizon (-5 to 0): red fading to white, dimming out
                Vector3 sunColor;
                float intensity, exposure, radiusMult;

                if (cachedSunAlt_ >= 20.0f)
                {
                    // High sky — pale warm white
                    float t = Clamp((cachedSunAlt_ - 20.0f) / 30.0f, 0.0f, 1.0f);
                    sunColor = Vector3(1.0f, Lerp(0.85f, 0.95f, t), Lerp(0.65f, 0.85f, t));
                    intensity = Lerp(0.7f, 0.8f, t);
                    exposure = 0.4f;
                    radiusMult = 1.0f;
                }
                else if (cachedSunAlt_ >= 0.0f)
                {
                    // Sunset — deepening red as altitude drops toward 0
                    float t = cachedSunAlt_ / 20.0f;  // 1=20deg, 0=horizon
                    sunColor = Vector3(1.0f, Lerp(0.25f, 0.85f, t), Lerp(0.1f, 0.65f, t));
                    intensity = Lerp(0.5f, 0.7f, t);
                    exposure = Lerp(0.3f, 0.4f, t);
                    radiusMult = Lerp(2.0f, 1.0f, t);
                }
                else
                {
                    // Below horizon — fade from red toward white, dimming out
                    float t = Clamp((cachedSunAlt_ + 5.0f) / 5.0f, 0.0f, 1.0f);  // 1=horizon, 0=-5deg
                    sunColor = Vector3(1.0f, Lerp(0.8f, 0.25f, t), Lerp(0.7f, 0.1f, t));
                    intensity = Lerp(0.0f, 0.5f, t);
                    exposure = Lerp(0.1f, 0.3f, t);
                    radiusMult = Lerp(1.5f, 2.0f, t);
                }

                float screenRadius = (30.0f / sunDist) / tanHalf * 0.5f * radiusMult;

                renderPath_->SetShaderParameter("LightScreenPos", screenPos);
                renderPath_->SetShaderParameter("LightRadius", screenRadius);
                renderPath_->SetShaderParameter("GodRayColor", sunColor);
                renderPath_->SetShaderParameter("GodRayDensity", 0.5f);
                renderPath_->SetShaderParameter("GodRayDecay", 0.97f);
                renderPath_->SetShaderParameter("GodRayWeight", 0.4f);
                renderPath_->SetShaderParameter("GodRayExposure", exposure);
                renderPath_->SetShaderParameter("GodRayIntensity", intensity);
                rayActive = true;
            }
        }
        if (!rayActive && godRaysEnabled_ && moonNode_ && cachedMoonAlt_ > -5.0f)
        {
            Vector3 toMoon = (moonNode_->GetWorldPosition() - camPos);
            float moonDist = toMoon.Length();
            toMoon /= moonDist;
            if (cameraNode_->GetWorldDirection().DotProduct(toMoon) > 0.0f)
            {
                Vector2 screenPos = cam->WorldToScreenPoint(moonNode_->GetWorldPosition());
                float screenRadius = (20.0f / moonDist) / tanHalf * 0.5f;

                // Moon: simple fade in at moonrise, fade out at moonset
                // Full intensity above 5 deg, fades to zero at -5 deg
                float moonFade = Clamp((cachedMoonAlt_ + 5.0f) / 10.0f, 0.0f, 1.0f);

                renderPath_->SetShaderParameter("LightScreenPos", screenPos);
                renderPath_->SetShaderParameter("LightRadius", screenRadius);
                renderPath_->SetShaderParameter("GodRayColor", Vector3(0.6f, 0.7f, 0.9f));
                renderPath_->SetShaderParameter("GodRayDensity", 0.5f);
                renderPath_->SetShaderParameter("GodRayDecay", 0.97f);
                renderPath_->SetShaderParameter("GodRayWeight", 0.4f);
                renderPath_->SetShaderParameter("GodRayExposure", 0.4f);
                renderPath_->SetShaderParameter("GodRayIntensity", 0.8f * moonFade);
                rayActive = true;
            }
        }
        renderPath_->SetEnabled("GodRays", rayActive);

    }
}

// ============================================================================
// Fish
// ============================================================================

void Water::CreateFish()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fishModel = cache->GetResource<Model>("Models/UrhoFish.mdl");
    auto* fishMat = cache->GetResource<Material>("Materials/UrhoFish.xml");

    if (!fishModel || !fishMat)
    {
        URHO3D_LOGERROR("Failed to load fish model or material");
        return;
    }
    fishBaseMat_ = fishMat;

    fishOrbitMat_ = fishMat->Clone();
    fishOrbitMat_->SetShaderParameter("WiggleAmplitude", 0.06f);
    fishOrbitMat_->SetShaderParameter("WiggleFrequency", 5.0f);

    fishStareMat_ = fishMat->Clone();
    fishStareMat_->SetShaderParameter("WiggleAmplitude", 0.01f);
    fishStareMat_->SetShaderParameter("WiggleFrequency", 0.8f);

    const int NUM_FISH = 50;
    const float waterY = 5.0f;
    const float spawnRadius = 80.0f;
    const float MIN_DEPTH = 1.0f;   // fish must be at least this far below surface
    Terrain* t = terrain_;

    for (int i = 0; i < NUM_FISH; ++i)
    {
        Node* fishNode = scene_->CreateChild("Fish");

        // Pick a random position that's actually underwater with enough depth
        Vector3 pos;
        int tries = 0;
        for (;;)
        {
            float x = Random(-spawnRadius, spawnRadius);
            float z = Random(-spawnRadius, spawnRadius);
            float terrainH = t ? t->GetHeight(Vector3(x, 0.0f, z)) : 0.0f;
            float availableDepth = waterY - terrainH;

            if (availableDepth >= MIN_DEPTH + 0.5f || ++tries > 50)
            {
                // Place between terrain + 0.5 and water - 1.0
                float minY = Max(terrainH + 0.5f, waterY - 4.0f);
                float maxY = waterY - MIN_DEPTH;
                if (minY > maxY) minY = maxY;
                pos = Vector3(x, Random(minY, maxY), z);
                break;
            }
        }
        fishNode->SetPosition(pos);

        // Random facing
        fishNode->SetRotation(Quaternion(0.0f, Random(0.0f, 360.0f), 0.0f));

        auto* sm = fishNode->CreateComponent<StaticModel>();
        sm->SetModel(fishModel, true);  // allowOversized=true
        sm->SetMaterial(fishMat);
        sm->SetCastShadows(false);

        fishNodes_.Push(WeakPtr<Node>(fishNode));
    }

    URHO3D_LOGINFOF("Spawned %d fish", NUM_FISH);
}

void Water::UpdateFish(float timeStep)
{
    const float WATER_Y = 5.0f;
    const float SWIM_SPEED = 0.5f;         // metres per second
    const float TURN_SPEED = 2.0f;        // radians per second
    const float COMFORT_DIST = 12.0f;     // start turning away at this distance
    const float BOUNDARY = 75.0f;         // stay within this radius of origin
    const float MIN_DEPTH = 1.0f;         // min distance below water surface
    const float MAX_DEPTH = 4.0f;         // max distance below water surface

    for (unsigned i = 0; i < fishNodes_.Size(); ++i)
    {
        Node* fish = fishNodes_[i];
        if (!fish) continue;

        Vector3 pos = fish->GetPosition();
        Quaternion rot = fish->GetRotation();
        // Fish model faces -Z, so its "forward" is BACK in Urho's coordinate system
        Vector3 forward = rot * Vector3::BACK;

        // Find nearest neighbour
        float nearestDist = M_INFINITY;
        Vector3 nearestDir;
        for (unsigned j = 0; j < fishNodes_.Size(); ++j)
        {
            if (i == j || !fishNodes_[j]) continue;
            Vector3 diff = fishNodes_[j]->GetPosition() - pos;
            float dist = diff.Length();
            if (dist < nearestDist)
            {
                nearestDist = dist;
                nearestDir = diff;
            }
        }

        // Desired heading: away from nearest neighbour
        Vector3 desiredDir = forward;

        if (nearestDist < COMFORT_DIST && nearestDist > 0.001f)
        {
            // Steer away — the closer they are, the harder the turn
            float urgency = 1.0f - (nearestDist / COMFORT_DIST);
            Vector3 awayDir = -nearestDir.Normalized();
            // Keep it horizontal
            awayDir.y_ = 0.0f;
            if (awayDir.LengthSquared() > 0.001f)
                awayDir.Normalize();
            else
                awayDir = forward;

            desiredDir = forward.Lerp(awayDir, urgency);
        }

        // Camera interaction — three zones based on distance
        Vector3 camPos = cameraNode_->GetPosition();
        Vector3 toCam3D = camPos - pos;          // full 3D vector for pitch
        Vector3 toCamFlat = toCam3D;
        toCamFlat.y_ = 0.0f;
        float camDist = toCamFlat.Length();
        const float STARE_DIST = 3.0f;   // very close — face camera, wiggle slowly
        const float ORBIT_NEAR = 5.0f;   // close — circle camera, agitated wiggle
        const float ORBIT_FAR = 15.0f;   // transition zone
        int fishZone = 0;  // 0=normal, 1=orbit, 2=stare

        if (camDist < 1.0f && camDist > 0.01f)
        {
            // Too close — slide off in whichever tangent matches current heading
            Vector3 radial = toCamFlat.LengthSquared() > 0.001f ? toCamFlat.Normalized() : forward;
            Vector3 tangentCW(radial.z_, 0.0f, -radial.x_);
            Vector3 tangentCCW(-radial.z_, 0.0f, radial.x_);
            // Pick the tangent closest to current forward direction
            desiredDir = (forward.DotProduct(tangentCW) >= forward.DotProduct(tangentCCW)) ? tangentCW : tangentCCW;
            fishZone = 1;  // use orbit wiggle for the dodge
        }
        else if (camDist < STARE_DIST && camDist > 0.5f)
        {
            // Face the camera in full 3D — pitch nose toward it
            Vector3 faceCam = toCam3D;
            if (faceCam.LengthSquared() > 0.001f)
                faceCam.Normalize();
            desiredDir = faceCam;
            fishZone = 2;
        }
        else if (camDist < ORBIT_FAR && camDist > 1.0f)
        {
            // Tangent direction — circle around camera, pick side matching current heading
            Vector3 radial = toCamFlat / camDist;
            Vector3 tangentCW(radial.z_, 0.0f, -radial.x_);
            Vector3 tangentCCW(-radial.z_, 0.0f, radial.x_);
            Vector3 tangent = (forward.DotProduct(tangentCW) >= forward.DotProduct(tangentCCW)) ? tangentCW : tangentCCW;
            float orbitStrength = Clamp(1.0f - (camDist - ORBIT_NEAR) / (ORBIT_FAR - ORBIT_NEAR), 0.0f, 0.8f);
            desiredDir = desiredDir.Lerp(tangent, orbitStrength);
            fishZone = 1;
        }

        // Random wander — small occasional impulse so fish don't swim in straight lines
        if (Random(1.0f) < 0.02f)  // ~2% chance per frame ≈ every couple of seconds
        {
            float wanderAngle = Random(-90.0f, 90.0f);
            Quaternion wanderRot(wanderAngle, Vector3::UP);
            desiredDir = wanderRot * desiredDir;
            // Vertical pitch change — nose up or down slightly
            desiredDir.y_ += Random(-0.15f, 0.15f);
        }

        // Boundary avoidance — steer back toward center if too far out
        float distFromCenter = Vector2(pos.x_, pos.z_).Length();
        if (distFromCenter > BOUNDARY)
        {
            Vector3 toCenter = -pos;
            toCenter.y_ = 0.0f;
            toCenter.Normalize();
            float boundaryUrgency = Clamp((distFromCenter - BOUNDARY) / 10.0f, 0.0f, 1.0f);
            desiredDir = desiredDir.Lerp(toCenter, boundaryUrgency);
        }

        // Shallow water avoidance — probe ahead, turn back toward deeper water
        if (terrain_)
        {
            Vector3 probe = pos + forward * 3.0f;  // look 3m ahead
            float probeH = terrain_->GetHeight(probe);
            float probeDepth = WATER_Y - probeH;
            if (probeDepth < 1.5f)
            {
                // Shallows ahead — steer toward deeper water (away from shore)
                Vector3 toDeep = pos - probe;
                toDeep.y_ = 0.0f;
                if (toDeep.LengthSquared() > 0.001f)
                    toDeep.Normalize();
                float shallowUrgency = Clamp(1.0f - (probeDepth / 1.5f), 0.0f, 1.0f);
                desiredDir = desiredDir.Lerp(toDeep, shallowUrgency);
            }
        }

        // Gently decay vertical component — fish naturally level out but can pitch up/down
        desiredDir.y_ *= 0.8f;
        if (desiredDir.LengthSquared() > 0.001f)
            desiredDir.Normalize();
        else
            desiredDir = forward;

        // Slerp rotation toward desired heading
        // FromLookRotation points +Z along desiredDir, but model faces -Z,
        // so rotate 180° around Y to align the model's nose with desiredDir
        Quaternion targetRot;
        targetRot.FromLookRotation(-desiredDir);
        rot = rot.Slerp(targetRot, TURN_SPEED * timeStep);
        fish->SetRotation(rot);

        // Move forward (model faces -Z) — smooth speed falloff near camera
        float speedFactor = (camDist < ORBIT_FAR) ?
            Lerp(0.15f, 1.0f, Clamp(camDist / ORBIT_FAR, 0.0f, 1.0f)) : 1.0f;
        float speed = SWIM_SPEED * speedFactor;
        forward = rot * Vector3::BACK;
        pos += forward * speed * timeStep;

        // Clamp to water column — stay between terrain floor + 0.3m and water surface - 0.3m
        float terrainH = terrain_ ? terrain_->GetHeight(pos) : 0.0f;
        float floorY = terrainH + 0.3f;
        float ceilY = WATER_Y - 0.3f;
        if (floorY > ceilY)
            floorY = ceilY;  // water too shallow — hug the surface
        pos.y_ = Clamp(pos.y_, floorY, ceilY);

        fish->SetPosition(pos);

        // Per-fish wiggle — swap between 3 pre-built materials (zero allocation)
        auto* sm = fish->GetComponent<StaticModel>();
        if (sm)
        {
            Material* want = (fishZone == 2) ? fishStareMat_.Get() :
                             (fishZone == 1) ? fishOrbitMat_.Get() :
                                               fishBaseMat_.Get();
            if (sm->GetMaterial() != want)
                sm->SetMaterial(want);
        }
    }
}

// ============================================================================
// OOFO fleet
// ============================================================================

void Water::CreateOOFOs()
{
    auto* cache = GetSubsystem<ResourceCache>();
    oofoCloudPositions_ = OOFO::BuildCloudPositions(cache);

    // Stagger spawns with random delays so OOFOs don't all appear at once
    oofosSpawned_ = 0;
    for (int i = 0; i < NUM_OOFOS; ++i)
        oofoSpawnTimers_[i] = Random(1.0f, 8.0f) + i * Random(2.0f, 5.0f);
}

void Water::UpdateOOFOs(float timeStep)
{
    // Spawn OOFOs on their individual delayed timers
    if (oofosSpawned_ < NUM_OOFOS)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        for (int i = 0; i < NUM_OOFOS; ++i)
        {
            if (oofoSpawnTimers_[i] > -1.0f)  // not yet spawned (-1 = done)
            {
                oofoSpawnTimers_[i] -= timeStep;
                if (oofoSpawnTimers_[i] <= 0.0f)
                {
                    SharedPtr<OOFO> o(new OOFO());
                    o->Init(scene_, cache, &oofoCloudPositions_);
                    oofos_.Push(o);
                    ++oofosSpawned_;
                    oofoSpawnTimers_[i] = -1.0f;  // mark spawned
                }
            }
        }
    }

    if (oofos_.Empty()) return;

    float nightFactor = 0.0f;
    if (skyboxMat_)
    {
        Variant nf = skyboxMat_->GetShaderParameter("NightFactor");
        if (nf.GetType() == VAR_FLOAT)
            nightFactor = nf.GetFloat();
    }

    float cloudTotal = cloudAngle_ + timeOfDayOffset_ * 6.2831853f / 18.0f;
    Vector3 camPos = cameraNode_ ? cameraNode_->GetWorldPosition() : Vector3::ZERO;

    for (unsigned i = 0; i < oofos_.Size(); ++i)
        oofos_[i]->Update(timeStep, camPos, cloudTotal, nightFactor);
}

void Water::UpdateSeasonalEffects()
{
    if (dayOfYear_ == lastSeasonDay_)
        return;
    lastSeasonDay_ = dayOfYear_;

    // Season factor: 0=winter solstice, 1=summer solstice
    cachedSeasonFactor_ = 0.5f + 0.5f * sinf((dayOfYear_ - 81) * 6.2831853f / 365.0f);

    // Sky/fog seasonal bias (applied per-frame only during daytime)
    Color summerBias(1.02f, 0.98f, 0.92f);
    Color winterBias(0.92f, 0.95f, 1.05f);
    cachedSeasonBias_ = winterBias.Lerp(summerBias, cachedSeasonFactor_);

    // Fog distance
    cachedFogStart_ = Lerp(350.0f, 600.0f, cachedSeasonFactor_);
    cachedFogEnd_ = Lerp(600.0f, 800.0f, cachedSeasonFactor_);

    // Terrain tint — 4-point seasonal lerp
    if (terrain_)
    {
        float seasonAngle = fmodf((dayOfYear_ - 81) / 365.0f + 1.0f, 1.0f);
        Color spring(0.85f, 1.0f, 0.8f);
        Color summer(0.9f, 1.0f, 0.85f);
        Color autumn(1.0f, 0.85f, 0.7f);
        Color winter(0.85f, 0.85f, 0.9f);

        if (seasonAngle < 0.25f)
            cachedTerrainTint_ = spring.Lerp(summer, seasonAngle / 0.25f);
        else if (seasonAngle < 0.5f)
            cachedTerrainTint_ = summer.Lerp(autumn, (seasonAngle - 0.25f) / 0.25f);
        else if (seasonAngle < 0.75f)
            cachedTerrainTint_ = autumn.Lerp(winter, (seasonAngle - 0.5f) / 0.25f);
        else
            cachedTerrainTint_ = winter.Lerp(spring, (seasonAngle - 0.75f) / 0.25f);

        auto* terrainMat = terrain_->GetMaterial();
        if (terrainMat)
            terrainMat->SetShaderParameter("MatDiffColor", cachedTerrainTint_);
    }

    // Water color
    if (waterNode_)
    {
        auto* sm = waterNode_->GetComponent<StaticModel>();
        auto* waterMat = sm ? sm->GetMaterial() : nullptr;
        if (waterMat)
        {
            Color summerShallow(0.2f, 0.6f, 0.5f);
            Color winterShallow(0.15f, 0.35f, 0.35f);
            Color summerDeep(0.02f, 0.1f, 0.2f);
            Color winterDeep(0.02f, 0.06f, 0.12f);

            cachedShallowColor_ = winterShallow.Lerp(summerShallow, cachedSeasonFactor_);
            cachedDeepColor_ = winterDeep.Lerp(summerDeep, cachedSeasonFactor_);

            waterMat->SetShaderParameter("ShallowColor", cachedShallowColor_);
            waterMat->SetShaderParameter("DeepColor", cachedDeepColor_);
        }
    }
}

void Water::UpdateAtmosphere(float sunAltitude)
{
    if (!zone_)
        return;

    // Recompute seasonal cache only when dayOfYear_ changes
    UpdateSeasonalEffects();

    Color ambient, fogColor, sunColor, moonColor;
    bool sunEnabled = true;
    bool moonEnabled = false;

    if (sunAltitude > 10.0f)
    {
        ambient = Color(0.15f, 0.15f, 0.15f);
        fogColor = Color(0.7f, 0.7f, 0.75f);
        sunColor = Color(1.2f, 1.2f, 1.2f);
    }
    else if (sunAltitude > 0.0f)
    {
        float t = sunAltitude / 10.0f;
        ambient = Color(0.15f, 0.15f, 0.15f).Lerp(Color(0.12f, 0.08f, 0.05f), 1.0f - t);
        fogColor = Color(0.7f, 0.7f, 0.75f).Lerp(Color(1.0f, 0.6f, 0.3f), 1.0f - t);
        sunColor = Color(1.2f, 1.2f, 1.2f).Lerp(Color(1.5f, 0.8f, 0.3f), 1.0f - t);
    }
    else if (sunAltitude > -6.0f)
    {
        float t = sunAltitude / -6.0f;
        ambient = Color(0.12f, 0.08f, 0.05f).Lerp(Color(0.03f, 0.03f, 0.06f), t);
        fogColor = Color(1.0f, 0.6f, 0.3f).Lerp(Color(0.1f, 0.08f, 0.2f), t);
        sunColor = Color(1.5f, 0.8f, 0.3f).Lerp(Color(0.0f, 0.0f, 0.0f), t);
        moonColor = Color(0.0f, 0.0f, 0.0f).Lerp(Color(0.6f, 0.6f, 1.0f), t);
        moonEnabled = true;
    }
    else
    {
        ambient = Color(0.02f, 0.02f, 0.04f);
        fogColor = Color(0.05f, 0.05f, 0.15f);
        sunColor = Color(0.0f, 0.0f, 0.0f);
        sunEnabled = false;
        moonColor = Color(0.6f, 0.6f, 1.0f);
        moonEnabled = true;
    }

    // Apply cached seasonal bias during daytime
    if (sunAltitude > 0.0f)
    {
        fogColor = Color(fogColor.r_ * cachedSeasonBias_.r_, fogColor.g_ * cachedSeasonBias_.g_, fogColor.b_ * cachedSeasonBias_.b_);
        ambient = Color(ambient.r_ * cachedSeasonBias_.r_, ambient.g_ * cachedSeasonBias_.g_, ambient.b_ * cachedSeasonBias_.b_);
    }

    zone_->SetFogStart(cachedFogStart_);
    zone_->SetFogEnd(cachedFogEnd_);
    zone_->SetAmbientColor(ambient);
    zone_->SetFogColor(fogColor);

    if (sunLight_)
    {
        sunLight_->SetColor(sunColor);
        sunLight_->SetEnabled(sunEnabled);
    }
    if (moonLight_)
    {
        moonLight_->SetColor(moonColor);
        moonLight_->SetEnabled(moonEnabled);
    }

    // Height fog: auto (time-based) unless user overrode with H key
    if (heightFogOverride_ == 0)
    {
        float normalScale = 1.0f / 13.0f;
        if (sunAltitude > 20.0f)
        {
            zone_->SetHeightFog(false);
        }
        else if (sunAltitude > 5.0f)
        {
            float t = (sunAltitude - 5.0f) / 15.0f;
            zone_->SetHeightFog(true);
            zone_->SetFogHeightScale(Lerp(normalScale, 50.0f, t));
        }
        else
        {
            zone_->SetHeightFog(true);
            zone_->SetFogHeightScale(normalScale);
        }
    }

    if (skyboxMat_)
    {
        Color skyTint;
        if (sunAltitude > 15.0f)
            skyTint = Color(0.85f, 0.85f, 0.9f);
        else if (sunAltitude > 2.5f)
        {
            float t = (sunAltitude - 2.5f) / 12.5f;
            skyTint = Color(0.9f, 0.5f, 0.3f).Lerp(Color(0.85f, 0.85f, 0.9f), t);
        }
        else if (sunAltitude > 0.0f)
        {
            float t = sunAltitude / 2.5f;
            skyTint = Color(0.8f, 0.4f, 0.25f).Lerp(Color(0.9f, 0.5f, 0.3f), t);
        }
        else if (sunAltitude > -6.0f)
        {
            float t = sunAltitude / -6.0f;
            skyTint = Color(1.0f, 0.5f, 0.3f).Lerp(Color(0.05f, 0.05f, 0.15f), t);
        }
        else
            skyTint = Color(0.03f, 0.03f, 0.08f);

        skyboxMat_->SetShaderParameter("MatDiffColor", skyTint);

        float nightFactor;
        if (sunAltitude > 5.0f)
            nightFactor = 0.0f;
        else if (sunAltitude > -6.0f)
            nightFactor = 1.0f - (sunAltitude + 6.0f) / 11.0f;
        else
            nightFactor = 1.0f;
        skyboxMat_->SetShaderParameter("NightFactor", nightFactor);
    }
}

// ============================================================================
// Network Time
// ============================================================================

void Water::FetchNetworkTime()
{
    auto* network = GetSubsystem<Network>();
    if (!network) return;
    timeRequest_ = network->MakeHttpRequest("http://worldclockapi.com/api/json/AUS%20Eastern%20Standard%20Time/now");
}

void Water::ProcessTimeResponse()
{
    if (!timeRequest_ || timeRequest_->GetState() == HTTP_INITIALIZING)
        return;

    if (timeRequest_->GetState() == HTTP_ERROR)
    {
        timeRequest_.Reset();
        return;
    }

    if (timeRequest_->GetState() != HTTP_OPEN && timeRequest_->GetState() != HTTP_CLOSED)
    {
        timeRequest_.Reset();
        return;
    }

    if (timeRequest_->GetAvailableSize() <= 0 && timeRequest_->GetState() != HTTP_CLOSED)
        return;

    String response;
    while (timeRequest_->GetAvailableSize() > 0)
    {
        char buf[256];
        int read = timeRequest_->Read(buf, Min((int)sizeof(buf) - 1, timeRequest_->GetAvailableSize()));
        if (read > 0)
        {
            buf[read] = 0;
            response += String(buf);
        }
    }

    if (timeRequest_->GetState() != HTTP_CLOSED)
        return;

    timeRequest_.Reset();

    if (response.Empty())
        return;

    unsigned dtPos = response.Find("\"currentDateTime\":\"");
    unsigned doyPos = response.Find("\"ordinalDate\":\"");

    if (dtPos == String::NPOS || doyPos == String::NPOS)
        return;

    dtPos += 19;
    unsigned dtEnd = response.Find("\"", dtPos);
    if (dtEnd == String::NPOS) return;
    String datetime = response.Substring(dtPos, dtEnd - dtPos);

    doyPos += 15;
    unsigned doyEnd = response.Find("\"", doyPos);
    if (doyEnd == String::NPOS) return;
    String ordinalStr = response.Substring(doyPos, doyEnd - doyPos);
    unsigned dashPos = ordinalStr.Find("-");
    if (dashPos == String::NPOS) return;
    String doyStr = ordinalStr.Substring(dashPos + 1);

    unsigned tPos = datetime.Find("T");
    if (tPos == String::NPOS) return;
    String timeStr = datetime.Substring(tPos + 1);

    unsigned plusPos = timeStr.Find("+");
    unsigned minusPos = timeStr.Find("-", 1);
    unsigned tzPos = String::NPOS;
    if (plusPos != String::NPOS) tzPos = plusPos;
    if (minusPos != String::NPOS && (tzPos == String::NPOS || minusPos < tzPos)) tzPos = minusPos;
    if (tzPos != String::NPOS) timeStr = timeStr.Substring(0, tzPos);

    Vector<String> timeParts = timeStr.Split(':');
    if (timeParts.Size() < 2) return;

    int hour = atoi(timeParts[0].CString());
    int minute = atoi(timeParts[1].CString());
    float seconds = 0.0f;
    if (timeParts.Size() >= 3)
        seconds = (float)atof(timeParts[2].CString());

    int newDayOfYear = atoi(doyStr.CString());
    float newTimeOfDay = (float)hour + minute / 60.0f + seconds / 3600.0f;

    timeOfDay_ = newTimeOfDay;
    dayOfYear_ = newDayOfYear;

    // Seed RNG from network response noise — byte-level hash of the raw
    // response gives entropy from server timing, content jitter, etc.
    unsigned seed = 5381;
    for (unsigned i = 0; i < response.Length(); ++i)
        seed = seed * 33 + (unsigned char)response.CString()[i];
    SetRandomSeed(seed);
    URHO3D_LOGINFOF("RNG seeded from network noise: %u", seed);
}

// ============================================================================
// Update
// ============================================================================

void Water::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    MoveCamera(timeStep);

    // Network time sync
    ProcessTimeResponse();
    timeSyncTimer_ -= timeStep;
    if (timeSyncTimer_ <= 0.0f)
    {
        FetchNetworkTime();
        timeSyncTimer_ = 300.0f;
    }

    // Update celestial bodies
    UpdateCelestialBodies(timeStep);
    UpdateOOFOs(timeStep);
    UpdateFish(timeStep);

    // Update minimap camera position
    UpdateMinimapCamera();

    if (renderPath_)
        renderPath_->SetShaderParameter("MainCameraY", cameraNode_->GetWorldPosition().y_);

    if (profilerUI_)
    {
        profilerUI_->Update(timeStep);
        profilerUI_->SetCameraPos(cameraNode_->GetWorldPosition());
    }
}

void Water::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    auto* debug = scene_->GetComponent<DebugRenderer>();

    // Overlay rays — independent of drawDebug_, toggled from Overlay menu
    if (debug && cameraNode_)
    {
        auto* camera = cameraNode_->GetComponent<Camera>();
        if (camera)
        {
            Ray cursorRay = camera->GetScreenRay(0.5f, 0.5f);
            Vector3 cursorNear = cursorRay.origin_ + cursorRay.direction_ * camera->GetNearClip();

            // OOFO detector rays
            if (oofoRayVisible_)
            {
                for (unsigned i = 0; i < oofos_.Size(); ++i)
                {
                    Node* n = oofos_[i]->GetNode();
                    if (n && n->IsEnabled())
                        debug->AddLine(n->GetWorldPosition(), cursorNear, Color::GREEN, true);
                }
            }

            // Sun & Moon locator rays — always draw, even when billboard is hidden below horizon
            if (sunNode_)
                debug->AddLine(sunNode_->GetWorldPosition(), cursorNear, Color(1.0f, 0.6f, 0.0f), true);
            if (moonNode_)
                debug->AddLine(moonNode_->GetWorldPosition(), cursorNear, Color(0.0f, 0.8f, 1.0f), true);
        }

        // Fish markers
        if (fishRayVisible_)
        {
            const Vector<SharedPtr<Node>>& children = scene_->GetChildren();
            for (unsigned i = 0; i < children.Size(); ++i)
            {
                if (children[i]->GetName() == "Fish")
                    debug->AddSphere(Sphere(children[i]->GetWorldPosition(), 3.0f), Color::RED, false);
            }
        }
    }

    if (!drawDebug_)
        return;

    // Editor overlays — gated by drawDebug_ (F5 / NumPad Enter)
    if (hasBrushHit_ && brushMode_ != 0)
        DrawBrushOutline(cachedBrushHit_);

    if (prefabBrush_ && hasBrushHit_ && debug)
    {
        BoundingBox combined;
        Vector<Drawable*> drawables;
        prefabBrush_->GetDerivedComponents<Drawable>(drawables, true);
        for (unsigned i = 0; i < drawables.Size(); ++i)
            combined.Merge(drawables[i]->GetBoundingBox());

        if (combined.Defined())
        {
            Vector3 pos(cachedBrushHit_.x_, cachedBrushHit_.y_, cachedBrushHit_.z_);
            Matrix3x4 transform(pos, cachedBrushNormal_ != Vector3::UP ?
                Quaternion(Vector3::UP, cachedBrushNormal_) : Quaternion::IDENTITY,
                prefabBrush_->GetScale());
            debug->AddBoundingBox(combined, transform, Color::CYAN, false);
        }
    }

    DrawGizmo();

    auto* physics = scene_->GetComponent<PhysicsWorld>();
    if (physics)
        physics->DrawDebugGeometry(true);
}

void Water::RunErosion(int iterations)
{
    if (!terrain_ || !editableHeightMap_)
    {
        URHO3D_LOGERROR("RunErosion: No terrain or heightmap");
        return;
    }

    // Snapshot for undo before erosion
    SharedPtr<Image> beforeErosion = CloneHeightMap();

    auto* graphics = GetSubsystem<Graphics>();
    auto* cache = GetSubsystem<ResourceCache>();

    // Load erosion shader with two pass variations
    auto* shader = cache->GetResource<Shader>("Shaders/GLSL/Erosion.glsl");
    if (!shader)
    {
        URHO3D_LOGERROR("RunErosion: Failed to load Erosion.glsl");
        return;
    }

    ShaderVariation* passFlux = shader->GetVariation(CS, "PASS_FLUX");
    ShaderVariation* passTransport = shader->GetVariation(CS, "PASS_TRANSPORT");
    if (!passFlux || !passTransport)
    {
        URHO3D_LOGERROR("RunErosion: Failed to get shader variations");
        return;
    }

    int hmW = editableHeightMap_->GetWidth();
    int hmH = editableHeightMap_->GetHeight();
    int numCells = hmW * hmH;
    int comps = editableHeightMap_->GetComponents();
    unsigned char* hmData = editableHeightMap_->GetData();

    URHO3D_LOGINFO("RunErosion: " + String(iterations) + " iterations on " +
                   String(hmW) + "x" + String(hmH) + " heightmap");

    // Scale heights to world-space so the erosion algorithm has real gradients
    // Normalized [0,1] over 1025 cells gives ~0.001 gradient — too flat for meaningful flow
    Vector3 spacing = terrain_->GetSpacing();
    float heightScale = 255.0f * spacing.y_;  // world height range

    // Extract heights into float array (world-space)
    // Buffer layout: [0..N-1] = current heights, [N..2N-1] = original heights (for depth constraint)
    float* heightData = new float[numCells * 2];
    float hMin = 1e9f, hMax = -1e9f;
    for (int i = 0; i < numCells; ++i)
    {
        float h;
        if (comps >= 2)
        {
            int idx = i * comps;
            h = ((float)hmData[idx] + (float)hmData[idx + 1] / 256.0f) / 255.0f;
        }
        else
        {
            h = (float)hmData[i * comps] / 255.0f;
        }
        float worldH = h * heightScale;
        heightData[i] = worldH;              // current heights
        heightData[numCells + i] = worldH;   // original heights (copy)
        if (worldH < hMin) hMin = worldH;
        if (worldH > hMax) hMax = worldH;
    }
    URHO3D_LOGINFO("RunErosion: height range [" + String(hMin) + ", " + String(hMax) +
                   "], scale=" + String(heightScale));

    // Initialize water+sediment buffer (interleaved, all zeros)
    float* waterSedData = new float[numCells * 2];
    memset(waterSedData, 0, numCells * 2 * sizeof(float));

    // Initialize flux buffer (LRTB per cell, all zeros)
    float* fluxData = new float[numCells * 4];
    memset(fluxData, 0, numCells * 4 * sizeof(float));

    // Parameters tuned for world-space heights (~0-128 range)
    float paramData[16];
    memset(paramData, 0, sizeof(paramData));
    paramData[0] = 0.1f;               // dt (larger step = faster simulation)
    paramData[1] = erosionRainfall_ * heightScale;  // rainfall scaled to world-space
    paramData[2] = 9.81f;              // gravity
    paramData[3] = 1.0f;               // pipe area
    paramData[4] = 5.0f;               // Kc (sediment capacity — higher = more erosion before deposition)
    paramData[5] = erosionStrength_;   // Ks (erosion rate)
    paramData[6] = erosionStrength_ * 0.5f;  // Kd (deposition rate — lower than Ks so material moves further)
    paramData[7] = 0.01f;              // Ke (evaporation rate — slower so water persists longer)
    paramData[8] = (float)hmW;         // width
    paramData[9] = (float)hmH;         // height
    paramData[10] = erosionMaxDepth_ * heightScale;  // max carve depth in world units
    paramData[11] = erosionMinHeight_ * heightScale;  // min height floor in world units
    paramData[12] = erosionRidgeProtect_;              // ridge protection [0..1]
    paramData[13] = (float)erosionBorderPad_;          // border padding in cells

    // Create VertexBuffers as SSBOs
    Vector<VertexElement> floatElem;
    floatElem.Push(VertexElement(TYPE_FLOAT, SEM_POSITION));

    SharedPtr<VertexBuffer> heightBuf(new VertexBuffer(context_));
    heightBuf->SetShadowed(false);
    heightBuf->SetSize(numCells * 2, floatElem, false);  // 2x: current + original heights
    heightBuf->SetData(heightData);

    Vector<VertexElement> float2Elem;
    float2Elem.Push(VertexElement(TYPE_VECTOR2, SEM_POSITION));

    SharedPtr<VertexBuffer> waterSedBuf(new VertexBuffer(context_));
    waterSedBuf->SetShadowed(false);
    waterSedBuf->SetSize(numCells, float2Elem, false);
    waterSedBuf->SetData(waterSedData);

    Vector<VertexElement> float4Elem;
    float4Elem.Push(VertexElement(TYPE_VECTOR4, SEM_POSITION));

    SharedPtr<VertexBuffer> fluxBuf(new VertexBuffer(context_));
    fluxBuf->SetShadowed(false);
    fluxBuf->SetSize(numCells, float4Elem, false);
    fluxBuf->SetData(fluxData);

    SharedPtr<VertexBuffer> paramBuf(new VertexBuffer(context_));
    paramBuf->SetShadowed(false);
    paramBuf->SetSize(16, floatElem, false);
    paramBuf->SetData(paramData);

    // Bind all 4 SSBOs
    graphics->SetStorageBuffer(0, heightBuf);
    graphics->SetStorageBuffer(1, waterSedBuf);
    graphics->SetStorageBuffer(2, fluxBuf);
    graphics->SetStorageBuffer(3, paramBuf);

    // Dispatch groups: ceil(width/16) x ceil(height/16)
    unsigned groupsX = (hmW + 15) / 16;
    unsigned groupsY = (hmH + 15) / 16;

    // Batch all dispatches into a single command buffer for performance
    graphics->BeginComputeBatch();

    // Run erosion iterations
    for (int iter = 0; iter < iterations; ++iter)
    {
        // Pass A: Rain + Flux
        graphics->SetComputeShader(passFlux);
        graphics->DispatchCompute(groupsX, groupsY);

        // Pass B: Water transport + Erosion + Evaporation
        graphics->SetComputeShader(passTransport);
        graphics->DispatchCompute(groupsX, groupsY);
    }

    // Submit all dispatches and wait for GPU completion
    graphics->EndComputeBatch();

    URHO3D_LOGINFO("RunErosion: " + String(iterations) + " iterations complete, reading back...");

    // Read back the height buffer
    if (heightBuf->GetData(heightData))
    {
        // Convert from world-space back to normalized [0,1]
        float hMinOut = 1e9f, hMaxOut = -1e9f;
        for (int i = 0; i < numCells; ++i)
        {
            if (heightData[i] < hMinOut) hMinOut = heightData[i];
            if (heightData[i] > hMaxOut) hMaxOut = heightData[i];
        }
        // Compute change stats
        float totalDelta = 0.0f;
        float maxDelta = 0.0f;
        int changedCells = 0;
        for (int i = 0; i < numCells; ++i)
        {
            float origH = heightData[numCells + i];
            float delta = Abs(heightData[i] - origH);
            totalDelta += delta;
            if (delta > maxDelta) maxDelta = delta;
            if (delta > 0.001f) ++changedCells;
        }
        URHO3D_LOGINFOF("RunErosion: post range [%.2f, %.2f] changed=%d/%d maxDelta=%.4f totalDelta=%.2f",
                        hMinOut, hMaxOut, changedCells, numCells, maxDelta, totalDelta);

        for (int i = 0; i < numCells; ++i)
        {
            float h = heightData[i] / heightScale;  // back to [0,1]
            if (h < 0.0f) h = 0.0f;
            if (h > 1.0f) h = 1.0f;

            if (comps >= 4)
            {
                float scaled = h * 255.0f;
                int idx = i * comps;
                hmData[idx] = (unsigned char)scaled;
                float rem = (scaled - (float)hmData[idx]) * 256.0f;
                hmData[idx + 1] = (unsigned char)rem;
                float rem2 = (rem - (float)hmData[idx + 1]) * 256.0f;
                hmData[idx + 2] = (unsigned char)rem2;
                hmData[idx + 3] = (unsigned char)((rem2 - (float)hmData[idx + 2]) * 256.0f);
            }
            else if (comps >= 2)
            {
                float scaled = h * 255.0f;
                int idx = i * comps;
                hmData[idx] = (unsigned char)scaled;
                hmData[idx + 1] = (unsigned char)((scaled - (float)hmData[idx]) * 256.0f);
            }
            else
            {
                hmData[i * comps] = (unsigned char)(h * 255.0f);
            }
        }

        // Apply to terrain
        terrain_->ApplyHeightMap();
        WakeSleepingBodiesOnTerrain();
    

        // Push undo for the erosion operation
        UndoAction action;
        action.type = UndoAction::TERRAIN_EDIT;
        action.beforeHM = beforeErosion;
        action.afterHM = CloneHeightMap();
        PushUndo(action);

        URHO3D_LOGINFO("RunErosion: Complete — terrain updated");
    }
    else
    {
        URHO3D_LOGERROR("RunErosion: Failed to read back height data from GPU");
    }

    // Clean up
    graphics->SetComputeShader(nullptr);
    graphics->SetStorageBuffer(0, nullptr);
    graphics->SetStorageBuffer(1, nullptr);
    graphics->SetStorageBuffer(2, nullptr);
    graphics->SetStorageBuffer(3, nullptr);

    delete[] heightData;
    delete[] waterSedData;
    delete[] fluxData;
}

// ============================================================================
// Hierarchy Window
// ============================================================================

void Water::ToggleHierarchyWindow()
{
    if (hierarchyWindow_)
    {
        hierarchyWindow_->SetVisible(!hierarchyWindow_->IsVisible());
        if (hierarchyWindow_->IsVisible())
            BuildHierarchyTree();
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();

    hierarchyWindow_ = new Window(context_);
    uiRoot->AddChild(hierarchyWindow_);
    hierarchyWindow_->SetStyleAuto();
    hierarchyWindow_->SetPosition(8, 36);
    hierarchyWindow_->SetSize(280, 500);
    hierarchyWindow_->SetResizable(true);
    hierarchyWindow_->SetMovable(true);
    hierarchyWindow_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
    hierarchyWindow_->SetOpacity(0.85f);

    auto* title = hierarchyWindow_->CreateChild<Text>();
    title->SetFont(font, 12);
    title->SetText("Scene");
    title->SetColor(Color(0.9f, 0.9f, 0.3f));

    hierarchyList_ = hierarchyWindow_->CreateChild<ListView>();
    hierarchyList_->SetStyle("HierarchyListView");
    hierarchyList_->SetHighlightMode(HM_ALWAYS);
    hierarchyList_->SetMinSize(260, 440);

    SubscribeToEvent(hierarchyList_, E_SELECTIONCHANGED, URHO3D_HANDLER(Water, HandleHierarchySelectionChanged));
    SubscribeToEvent(hierarchyList_, E_ITEMDOUBLECLICKED, URHO3D_HANDLER(Water, HandleHierarchyDoubleClick));

    BuildHierarchyTree();
}

void Water::BuildHierarchyTree()
{
    if (!hierarchyList_ || !scene_)
        return;

    hierarchyList_->DisableInternalLayoutUpdate();
    hierarchyList_->RemoveAllItems();

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    // Root scene item
    auto* rootItem = new Text(context_);
    rootItem->SetFont(font, 11);
    rootItem->SetText("Scene");
    rootItem->SetColor(Color(1.0f, 1.0f, 0.7f));
    rootItem->SetFixedHeight(16);
    rootItem->SetVar(StringHash("NodeID"), scene_->GetID());
    hierarchyList_->AddItem(rootItem);

    unsigned index = 1;
    const auto& children = scene_->GetChildren();
    for (unsigned i = 0; i < children.Size(); ++i)
        PopulateHierarchy(children[i], rootItem, index);

    hierarchyList_->EnableInternalLayoutUpdate();
    hierarchyList_->UpdateInternalLayout();

    // Expand root
    hierarchyList_->Expand(0, true);
}

void Water::PopulateHierarchy(Node* node, Text* parentItem, unsigned& index)
{
    if (!node)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    String name = node->GetName().Empty() ? String("Node ") + String(node->GetID()) : node->GetName();

    auto* item = new Text(context_);
    item->SetFont(font, 11);
    item->SetText(name);
    item->SetColor(Color::WHITE);
    item->SetFixedHeight(16);
    item->SetVar(StringHash("NodeID"), node->GetID());
    hierarchyList_->InsertItem(index, item, parentItem);
    index++;

    // Components (green text)
    const auto& components = node->GetComponents();
    for (unsigned c = 0; c < components.Size(); ++c)
    {
        Component* comp = components[c];
        auto* compItem = new Text(context_);
        compItem->SetFont(font, 10);
        compItem->SetText("  [" + comp->GetTypeName() + "]");
        compItem->SetColor(Color(0.4f, 0.9f, 0.4f));
        compItem->SetFixedHeight(16);
        compItem->SetVar(StringHash("NodeID"), node->GetID());
        compItem->SetVar(StringHash("CompIndex"), c);
        hierarchyList_->InsertItem(index, compItem, item);
        index++;
    }

    // Recurse children
    const auto& children = node->GetChildren();
    for (unsigned i = 0; i < children.Size(); ++i)
        PopulateHierarchy(children[i], item, index);
}

void Water::HandleHierarchySelectionChanged(StringHash eventType, VariantMap& eventData)
{
    auto* selected = hierarchyList_->GetSelectedItem();
    if (!selected)
        return;

    const Variant& nodeIDVar = selected->GetVar(StringHash("NodeID"));
    if (nodeIDVar.IsEmpty())
        return;

    unsigned nodeID = nodeIDVar.GetU32();
    Node* node = scene_->GetNode(nodeID);
    if (node && node != scene_ && node != selectedNode_)
        SelectNode(node);
}

void Water::HandleHierarchyDoubleClick(StringHash eventType, VariantMap& eventData)
{
    using namespace ItemDoubleClicked;
    auto* item = static_cast<UIElement*>(eventData[P_ITEM].GetPtr());
    if (!item)
        return;

    const Variant& nodeIDVar = item->GetVar(StringHash("NodeID"));
    if (nodeIDVar.IsEmpty())
        return;

    unsigned nodeID = nodeIDVar.GetU32();
    Node* node = scene_->GetNode(nodeID);
    if (node && node != scene_ && cameraNode_)
    {
        // Focus camera on node
        Vector3 target = node->GetWorldPosition();
        Vector3 dir = (target - cameraNode_->GetPosition());
        float dist = dir.Length();
        if (dist > 1.0f)
        {
            dir.Normalize();
            cameraNode_->SetPosition(target - dir * Min(dist, 20.0f));
            yaw_ = atan2f(dir.x_, dir.z_) * M_RADTODEG;
            pitch_ = asinf(Clamp(dir.y_, -1.0f, 1.0f)) * M_RADTODEG;
            cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
        }
    }
}

void Water::HighlightInHierarchy(Node* node)
{
    if (!hierarchyList_ || !hierarchyWindow_ || !hierarchyWindow_->IsVisible())
        return;

    if (!node)
    {
        hierarchyList_->ClearSelection();
        return;
    }

    unsigned targetID = node->GetID();
    for (int i = 0; i < hierarchyList_->GetNumItems(); ++i)
    {
        auto* item = hierarchyList_->GetItem(i);
        if (!item)
            continue;
        const Variant& v = item->GetVar(StringHash("NodeID"));
        if (!v.IsEmpty() && v.GetU32() == targetID)
        {
            // Only select if it's a node item (not a component item)
            const Variant& compVar = item->GetVar(StringHash("CompIndex"));
            if (compVar.IsEmpty())
            {
                hierarchyList_->SetSelection(i);
                hierarchyList_->EnsureItemVisibility(i);
                break;
            }
        }
    }
}

// ============================================================================
// Inspector Window
// ============================================================================

void Water::ToggleInspectorWindow()
{
    if (inspectorWindow_)
    {
        inspectorWindow_->SetVisible(!inspectorWindow_->IsVisible());
        if (inspectorWindow_->IsVisible())
            RebuildInspector();
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    auto* graphics = GetSubsystem<Graphics>();

    inspectorWindow_ = new Window(context_);
    uiRoot->AddChild(inspectorWindow_);
    inspectorWindow_->SetStyleAuto();
    inspectorWindow_->SetPosition(graphics->GetWidth() - 308, 36);
    inspectorWindow_->SetSize(300, 500);
    inspectorWindow_->SetResizable(true);
    inspectorWindow_->SetMovable(true);
    inspectorWindow_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
    inspectorWindow_->SetOpacity(0.85f);

    auto* title = inspectorWindow_->CreateChild<Text>();
    title->SetFont(font, 12);
    title->SetText("Inspector");
    title->SetColor(Color(0.9f, 0.9f, 0.3f));

    inspectorScroll_ = inspectorWindow_->CreateChild<ScrollView>();
    inspectorScroll_->SetStyleAuto();
    inspectorScroll_->SetMinSize(280, 440);

    inspectorContent_ = new UIElement(context_);
    inspectorContent_->SetLayout(LM_VERTICAL, 2, IntRect(2, 2, 2, 2));
    inspectorContent_->SetMinWidth(270);
    inspectorScroll_->SetContentElement(inspectorContent_);

    RebuildInspector();
}

void Water::RebuildInspector()
{
    if (!inspectorContent_)
        return;

    inspectorContent_->RemoveAllChildren();

    if (!selectedNode_ || selectedNode_.Expired())
        return;

    Node* node = selectedNode_;
    CreateNodeSection(node);

    const auto& components = node->GetComponents();
    for (unsigned i = 0; i < components.Size(); ++i)
        CreateComponentSection(components[i], i);

    inspectorContent_->SetMinHeight(inspectorContent_->GetNumChildren() * 22 + 40);
}

void Water::CreateNodeSection(Node* node)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    // Header
    auto* header = inspectorContent_->CreateChild<Text>();
    header->SetFont(font, 12);
    header->SetText("Node: " + (node->GetName().Empty() ? String("ID ") + String(node->GetID()) : node->GetName()));
    header->SetColor(Color(0.9f, 0.9f, 0.3f));

    // Name
    auto* nameRow = inspectorContent_->CreateChild<UIElement>();
    nameRow->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
    nameRow->SetFixedHeight(20);

    auto* nameLabel = nameRow->CreateChild<Text>();
    nameLabel->SetFont(font, 11);
    nameLabel->SetText("Name");
    nameLabel->SetColor(Color(0.8f, 0.8f, 0.8f));
    nameLabel->SetMinWidth(70);

    auto* nameEdit = nameRow->CreateChild<LineEdit>();
    nameEdit->SetStyleAuto();
    nameEdit->SetText(node->GetName());
    nameEdit->SetMinWidth(180);
    nameEdit->SetFixedHeight(18);
    nameEdit->SetVar(StringHash("Field"), String("Name"));
    SubscribeToEvent(nameEdit, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleInspectorTransformEdit));

    // Position
    CreateVec3Row(inspectorContent_, "Pos", node->GetPosition(), "Position");

    // Rotation (as Euler)
    Vector3 euler = node->GetRotation().EulerAngles();
    CreateVec3Row(inspectorContent_, "Rot", euler, "Rotation");

    // Scale
    CreateVec3Row(inspectorContent_, "Scale", node->GetScale(), "Scale");
}

LineEdit* Water::CreateVec3Row(UIElement* parent, const String& label, const Vector3& value, const String& tag)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    auto* row = parent->CreateChild<UIElement>();
    row->SetLayout(LM_HORIZONTAL, 2, IntRect(2, 1, 2, 1));
    row->SetFixedHeight(20);

    auto* lbl = row->CreateChild<Text>();
    lbl->SetFont(font, 11);
    lbl->SetText(label);
    lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
    lbl->SetMinWidth(42);

    const char* axes[] = {"X", "Y", "Z"};
    const float vals[] = {value.x_, value.y_, value.z_};

    for (int i = 0; i < 3; ++i)
    {
        auto* axLbl = row->CreateChild<Text>();
        axLbl->SetFont(font, 10);
        axLbl->SetText(axes[i]);
        axLbl->SetColor(i == 0 ? Color(1.0f, 0.3f, 0.3f) : (i == 1 ? Color(0.3f, 1.0f, 0.3f) : Color(0.3f, 0.3f, 1.0f)));
        axLbl->SetMinWidth(10);

        auto* edit = row->CreateChild<LineEdit>();
        edit->SetStyleAuto();
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", vals[i]);
        edit->SetText(buf);
        edit->SetMinWidth(54);
        edit->SetFixedHeight(18);
        edit->SetVar(StringHash("Field"), tag);
        edit->SetVar(StringHash("Axis"), i);
        SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleInspectorTransformEdit));
    }

    return nullptr;
}

void Water::HandleInspectorTransformEdit(StringHash eventType, VariantMap& eventData)
{
    using namespace TextFinished;
    auto* edit = static_cast<LineEdit*>(eventData[P_ELEMENT].GetPtr());
    if (!edit || !selectedNode_ || selectedNode_.Expired())
        return;

    Node* node = selectedNode_;
    String field = edit->GetVar(StringHash("Field")).GetString();

    if (field == "Name")
    {
        node->SetName(edit->GetText());
        if (hierarchyWindow_ && hierarchyWindow_->IsVisible())
            BuildHierarchyTree();
        return;
    }

    int axis = edit->GetVar(StringHash("Axis")).GetI32();
    float val = ToFloat(edit->GetText());

    if (field == "Position")
    {
        Vector3 pos = node->GetPosition();
        if (axis == 0) pos.x_ = val;
        else if (axis == 1) pos.y_ = val;
        else pos.z_ = val;
        node->SetPosition(pos);
    }
    else if (field == "Rotation")
    {
        Vector3 euler = node->GetRotation().EulerAngles();
        if (axis == 0) euler.x_ = val;
        else if (axis == 1) euler.y_ = val;
        else euler.z_ = val;
        node->SetRotation(Quaternion(euler.x_, euler.y_, euler.z_));
    }
    else if (field == "Scale")
    {
        Vector3 scale = node->GetScale();
        if (axis == 0) scale.x_ = val;
        else if (axis == 1) scale.y_ = val;
        else scale.z_ = val;
        node->SetScale(scale);
    }
}

void Water::CreateComponentSection(Component* component, unsigned compIndex)
{
    if (!component)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    // Component header
    auto* header = inspectorContent_->CreateChild<Text>();
    header->SetFont(font, 12);
    header->SetText(component->GetTypeName());
    header->SetColor(Color(0.4f, 0.9f, 0.4f));

    const Vector<AttributeInfo>* attrs = component->GetAttributes();
    if (!attrs)
        return;

    for (unsigned i = 0; i < attrs->Size(); ++i)
    {
        const AttributeInfo& attr = (*attrs)[i];

        // Skip non-editable attributes
        if (attr.mode_ & AM_NOEDIT)
            continue;

        Variant value = component->GetAttribute(i);

        auto* row = inspectorContent_->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 4, IntRect(2, 1, 2, 1));
        row->SetFixedHeight(20);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font, 10);
        lbl->SetText(attr.name_);
        lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
        lbl->SetMinWidth(90);

        switch (attr.type_)
        {
        case VAR_FLOAT:
        case VAR_INT:
        case VAR_STRING:
        {
            auto* edit = row->CreateChild<LineEdit>();
            edit->SetStyleAuto();
            edit->SetText(value.ToString());
            edit->SetMinWidth(140);
            edit->SetFixedHeight(18);
            edit->SetVar(StringHash("CompIndex"), compIndex);
            edit->SetVar(StringHash("AttrIndex"), i);
            SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleInspectorAttributeEdit));
            break;
        }
        case VAR_BOOL:
        {
            auto* cb = row->CreateChild<CheckBox>();
            cb->SetStyleAuto();
            cb->SetChecked(value.GetBool());
            cb->SetVar(StringHash("CompIndex"), compIndex);
            cb->SetVar(StringHash("AttrIndex"), i);
            SubscribeToEvent(cb, E_TOGGLED, URHO3D_HANDLER(Water, HandleInspectorCheckToggle));
            break;
        }
        case VAR_VECTOR3:
        {
            Vector3 v = value.GetVector3();
            const float vals[] = {v.x_, v.y_, v.z_};
            const char* axes[] = {"X", "Y", "Z"};
            for (int a = 0; a < 3; ++a)
            {
                auto* edit = row->CreateChild<LineEdit>();
                edit->SetStyleAuto();
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f", vals[a]);
                edit->SetText(buf);
                edit->SetMinWidth(44);
                edit->SetFixedHeight(18);
                edit->SetVar(StringHash("CompIndex"), compIndex);
                edit->SetVar(StringHash("AttrIndex"), i);
                edit->SetVar(StringHash("Axis"), a);
                edit->SetVar(StringHash("AttrType"), (int)VAR_VECTOR3);
                SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleInspectorAttributeEdit));
            }
            break;
        }
        case VAR_QUATERNION:
        {
            Vector3 euler = value.GetQuaternion().EulerAngles();
            const float vals[] = {euler.x_, euler.y_, euler.z_};
            for (int a = 0; a < 3; ++a)
            {
                auto* edit = row->CreateChild<LineEdit>();
                edit->SetStyleAuto();
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f", vals[a]);
                edit->SetText(buf);
                edit->SetMinWidth(44);
                edit->SetFixedHeight(18);
                edit->SetVar(StringHash("CompIndex"), compIndex);
                edit->SetVar(StringHash("AttrIndex"), i);
                edit->SetVar(StringHash("Axis"), a);
                edit->SetVar(StringHash("AttrType"), (int)VAR_QUATERNION);
                SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleInspectorAttributeEdit));
            }
            break;
        }
        case VAR_COLOR:
        {
            Color c = value.GetColor();
            const float vals[] = {c.r_, c.g_, c.b_, c.a_};
            const char* chans[] = {"R", "G", "B", "A"};
            for (int a = 0; a < 4; ++a)
            {
                auto* edit = row->CreateChild<LineEdit>();
                edit->SetStyleAuto();
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f", vals[a]);
                edit->SetText(buf);
                edit->SetMinWidth(34);
                edit->SetFixedHeight(18);
                edit->SetVar(StringHash("CompIndex"), compIndex);
                edit->SetVar(StringHash("AttrIndex"), i);
                edit->SetVar(StringHash("Axis"), a);
                edit->SetVar(StringHash("AttrType"), (int)VAR_COLOR);
                SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(Water, HandleInspectorAttributeEdit));
            }
            break;
        }
        default:
        {
            // Enum type
            if (attr.enumNames_)
            {
                auto* dd = row->CreateChild<DropDownList>();
                dd->SetStyleAuto();
                dd->SetMinWidth(140);
                dd->SetFixedHeight(18);
                dd->SetVar(StringHash("CompIndex"), compIndex);
                dd->SetVar(StringHash("AttrIndex"), i);

                int enumIdx = 0;
                for (const char** en = attr.enumNames_; *en; ++en, ++enumIdx)
                {
                    auto* t = new Text(context_);
                    t->SetFont(font, 10);
                    t->SetText(*en);
                    dd->AddItem(t);
                }
                dd->SetSelection(value.GetI32());
                SubscribeToEvent(dd, E_ITEMSELECTED, URHO3D_HANDLER(Water, HandleInspectorEnumSelect));
            }
            else
            {
                // Read-only display
                auto* valText = row->CreateChild<Text>();
                valText->SetFont(font, 10);
                valText->SetText(value.ToString());
                valText->SetColor(Color(0.6f, 0.6f, 0.6f));
            }
            break;
        }
        }
    }
}

void Water::HandleInspectorAttributeEdit(StringHash eventType, VariantMap& eventData)
{
    using namespace TextFinished;
    auto* edit = static_cast<LineEdit*>(eventData[P_ELEMENT].GetPtr());
    if (!edit || !selectedNode_ || selectedNode_.Expired())
        return;

    unsigned compIdx = edit->GetVar(StringHash("CompIndex")).GetU32();
    unsigned attrIdx = edit->GetVar(StringHash("AttrIndex")).GetU32();

    Node* node = selectedNode_;
    const auto& components = node->GetComponents();
    if (compIdx >= components.Size())
        return;

    Component* comp = components[compIdx];
    const Vector<AttributeInfo>* attrs = comp->GetAttributes();
    if (!attrs || attrIdx >= attrs->Size())
        return;

    const AttributeInfo& attr = (*attrs)[attrIdx];
    int attrType = edit->GetVar(StringHash("AttrType")).GetI32();

    if (attrType == (int)VAR_VECTOR3)
    {
        int axis = edit->GetVar(StringHash("Axis")).GetI32();
        Vector3 v = comp->GetAttribute(attrIdx).GetVector3();
        float val = ToFloat(edit->GetText());
        if (axis == 0) v.x_ = val;
        else if (axis == 1) v.y_ = val;
        else v.z_ = val;
        comp->SetAttribute(attrIdx, v);
    }
    else if (attrType == (int)VAR_QUATERNION)
    {
        int axis = edit->GetVar(StringHash("Axis")).GetI32();
        Vector3 euler = comp->GetAttribute(attrIdx).GetQuaternion().EulerAngles();
        float val = ToFloat(edit->GetText());
        if (axis == 0) euler.x_ = val;
        else if (axis == 1) euler.y_ = val;
        else euler.z_ = val;
        comp->SetAttribute(attrIdx, Quaternion(euler.x_, euler.y_, euler.z_));
    }
    else if (attrType == (int)VAR_COLOR)
    {
        int axis = edit->GetVar(StringHash("Axis")).GetI32();
        Color c = comp->GetAttribute(attrIdx).GetColor();
        float val = ToFloat(edit->GetText());
        if (axis == 0) c.r_ = val;
        else if (axis == 1) c.g_ = val;
        else if (axis == 2) c.b_ = val;
        else c.a_ = val;
        comp->SetAttribute(attrIdx, c);
    }
    else
    {
        // Simple types
        switch (attr.type_)
        {
        case VAR_FLOAT:
            comp->SetAttribute(attrIdx, ToFloat(edit->GetText()));
            break;
        case VAR_INT:
            comp->SetAttribute(attrIdx, (int)strtol(edit->GetText().CString(), nullptr, 10));
            break;
        case VAR_STRING:
            comp->SetAttribute(attrIdx, edit->GetText());
            break;
        default:
            break;
        }
    }

    comp->ApplyAttributes();
}

void Water::HandleInspectorCheckToggle(StringHash eventType, VariantMap& eventData)
{
    using namespace Toggled;
    auto* cb = static_cast<CheckBox*>(eventData[P_ELEMENT].GetPtr());
    if (!cb || !selectedNode_ || selectedNode_.Expired())
        return;

    unsigned compIdx = cb->GetVar(StringHash("CompIndex")).GetU32();
    unsigned attrIdx = cb->GetVar(StringHash("AttrIndex")).GetU32();

    Node* node = selectedNode_;
    const auto& components = node->GetComponents();
    if (compIdx >= components.Size())
        return;

    Component* comp = components[compIdx];
    comp->SetAttribute(attrIdx, eventData[P_STATE].GetBool());
    comp->ApplyAttributes();
}

void Water::HandleInspectorEnumSelect(StringHash eventType, VariantMap& eventData)
{
    using namespace ItemSelected;
    auto* dd = static_cast<DropDownList*>(eventData[P_ELEMENT].GetPtr());
    if (!dd || !selectedNode_ || selectedNode_.Expired())
        return;

    unsigned compIdx = dd->GetVar(StringHash("CompIndex")).GetU32();
    unsigned attrIdx = dd->GetVar(StringHash("AttrIndex")).GetU32();
    int sel = eventData[P_SELECTION].GetI32();

    Node* node = selectedNode_;
    const auto& components = node->GetComponents();
    if (compIdx >= components.Size())
        return;

    Component* comp = components[compIdx];
    comp->SetAttribute(attrIdx, sel);
    comp->ApplyAttributes();
}
