// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include <cstring>
#include "RadialCommandUI.h"
#include "NPCRingUI.h"
#include "HumanNPC.h"
#include "Creature.h"
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/CustomGeometry.h>
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
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/DrawableEvents.h>
#include <Urho3D/Graphics/ParticleEffect.h>
#include <Urho3D/Graphics/ParticleEmitter.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Graphics/TerrainPatch.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/GraphicsAPI/RenderSurface.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/GraphicsAPI/TextureCube.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/IOEvents.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Scene/SmoothedTransform.h>
#include <Urho3D/Scene/DrivenKeySystem.h>
#include <Urho3D/Scene/DrivenKeyEvents.h>
#include <Urho3D/Audio/Audio.h>
#include <Urho3D/Audio/AudioDefs.h>
#include <Urho3D/Audio/SoundListener.h>
#include "MetalDeposits.h"
#include "SoilMap.h"
#include "Soundscape.h"
#include "WaterRippleSystem.h"
#include "CampfireEvents.h"
#include "ScentRegistry.h"
#include "Creature.h"
#include "LTreeGenerator.h"
// Game settings stored as Vars on scene root node — auto-serialize with scene
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Slider.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/Text3D.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/GraphicsAPI/Shader.h>
#include <Urho3D/GraphicsAPI/ShaderVariation.h>
#include <Urho3D/GraphicsAPI/VertexBuffer.h>
#include <Urho3D/GraphicsAPI/IndexBuffer.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Database/Database.h>
#include <Urho3D/Database/DbConnection.h>
#include <Urho3D/Database/DbResult.h>
#ifdef URHO3D_VULKAN
#include <Urho3D/GraphicsAPI/Vulkan/VulkanGraphicsImpl.h>
#endif

#include "TerrainNode.h"

#include <Urho3D/DebugNew.h>
#include <Urho3D/Graphics/ProfilerUI.h>

#include <Urho3D/Math/Random.h>
#include <ctime>
#include <cmath>

URHO3D_DEFINE_APPLICATION_MAIN(TerrainNode)

static const float MELBOURNE_LAT = -37.8136f;
static const float MELBOURNE_LON = 144.96f;
static const float SOLAR_NOON = 12.0f + (11.0f * 15.0f - MELBOURNE_LON) / 15.0f;
static const float CELESTIAL_TIME_SCALE = 1.0f;
static const float DEG_TO_RAD = M_PI / 180.0f;
static const float RAD_TO_DEG = 180.0f / M_PI;

TerrainNode::TerrainNode(Context* context) :
    Sample(context)
{
}

void TerrainNode::Stop()
{
    // Clean up possession state before shutdown
    if (possessedNPC_)
    {
        auto* npc = possessedNPC_->GetDerivedComponent<HumanNPC>(false);
        if (npc)
            npc->SetPossessed(false);
        possessedNPC_ = nullptr;
        possessing_ = false;
        characterNode_ = nullptr;
    }

    skyboxMat_.Reset();
    for (int i = 0; i < 4; ++i)
        seasonSkyboxes_[i].Reset();
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

bool TerrainNode::OnEscapePressed()
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

void TerrainNode::SelectNode(Node* node)
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

    // Show inspect HUD if this is a creature
    auto* creature = node->GetDerivedComponent<Creature>(true);
    if (creature)
    {
        ShowInspectPanel(creature);
        // Show radial command UI for human NPCs
        auto* humanNPC = node->GetDerivedComponent<HumanNPC>(true);
        if (humanNPC && radialUI_)
        {
            unsigned spawnId = creature->GetSpawnId();
            radialUI_->Show(node, spawnId);
        }
    }
    else
    {
        HideInspectPanel();
        if (radialUI_)
            radialUI_->Hide();
    }
}

void TerrainNode::DeselectNode()
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

    // Hide radial command UI
    if (radialUI_)
        radialUI_->Hide();

    // Clear inspector
    if (inspectorWindow_ && inspectorWindow_->IsVisible())
        RebuildInspector();

    HideInspectPanel();
}

// ── Radial Command UI ───────────────────────────────────────────────────────

void TerrainNode::InitRadialCommands()
{
    radialUI_ = new RadialCommandUI(context_);

    // Inner ring: survival commands
    radialUI_->AddCommand({"Textures/Icons/gather.png", 11, 0, Color(0.4f, 0.8f, 0.2f)});    // STASK_GATHER
    radialUI_->AddCommand({"Textures/Icons/hunt.png", 6, 0, Color(0.9f, 0.3f, 0.2f)});        // STASK_HUNT
    radialUI_->AddCommand({"Textures/Icons/chop.png", 21, 0, Color(0.6f, 0.4f, 0.2f)});       // STASK_CHOP
    radialUI_->AddCommand({"Textures/Icons/fish.png", 28, 0, Color(0.2f, 0.5f, 0.9f)});       // STASK_FISH
    radialUI_->AddCommand({"Textures/Icons/mine.png", 29, 0, Color(0.5f, 0.5f, 0.5f)});       // STASK_MINE
    radialUI_->AddCommand({"Textures/Icons/craft.png", 18, 0, Color(0.8f, 0.6f, 0.2f)});      // STASK_CRAFT

    // Outer ring: social/advanced commands
    radialUI_->AddCommand({"Textures/Icons/build.png", 24, 1, Color(0.7f, 0.5f, 0.3f)});      // STASK_BUILD
    radialUI_->AddCommand({"Textures/Icons/trade.png", 32, 1, Color(0.9f, 0.8f, 0.2f)});      // STASK_TRADE
    radialUI_->AddCommand({"Textures/Icons/guard.png", 44, 1, Color(0.8f, 0.2f, 0.2f)});      // STASK_GUARD
    radialUI_->AddCommand({"Textures/Icons/heal.png", 41, 1, Color(0.2f, 0.9f, 0.5f)});       // STASK_HEAL
    radialUI_->AddCommand({"Textures/Icons/teach.png", 37, 1, Color(0.6f, 0.6f, 0.9f)});      // STASK_TEACH
    radialUI_->AddCommand({"Textures/Icons/stop.png", -1, 1, Color(0.9f, 0.1f, 0.1f)});       // DIRECTIVE_NONE (cancel)

    SubscribeToEvent(radialUI_, RadialCommandUI::E_RADIAL_COMMAND,
        URHO3D_HANDLER(TerrainNode, HandleRadialCommand));
}

void TerrainNode::HandleRadialCommand(StringHash, VariantMap& eventData)
{
    int taskId = eventData[RadialCommandUI::P_TASKID].GetI32();
    unsigned spawnId = eventData[RadialCommandUI::P_SPAWNID].GetU32();

    // Find a valid connection — prefer peer relay, fall back to direct server
    auto* network = GetSubsystem<Network>();
    Connection* conn = peerConnection_;
    if (!conn)
        conn = network ? network->GetServerConnection() : nullptr;
    if (!conn)
    {
        URHO3D_LOGWARNING("GOD_DIRECTIVE failed — no server connection");
        return;
    }

    // Send MSG_GOD_DIRECTIVE to server
    VectorBuffer buf;
    buf.WriteU32(spawnId);

    // Shift+click = FORBID (prevent NPC from ever picking this task)
    auto* input = GetSubsystem<Input>();
    bool shiftHeld = input && (input->GetQualifiers() & QUAL_SHIFT);

    const char* dirName = "NONE";
    if (taskId < 0)
    {
        // Cancel directive
        buf.WriteI32(0);  // DIRECTIVE_NONE
        buf.WriteI32(0);
    }
    else if (shiftHeld)
    {
        // Forbid this task
        buf.WriteI32(3);  // DIRECTIVE_FORBID_TASK
        buf.WriteI32(taskId);
        dirName = "FORBID";
    }
    else
    {
        // Focus on this task
        buf.WriteI32(1);  // DIRECTIVE_FOCUS_TASK
        buf.WriteI32(taskId);
        dirName = "FOCUS";
    }

    // Position (current NPC position for area directives)
    Node* npcNode = radialUI_->GetSelectedNode();
    Vector3 pos = npcNode ? npcNode->GetWorldPosition() : Vector3::ZERO;
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(pos.y_);
    buf.WriteFloat(pos.z_);

    if (peerConnection_)
    {
        // Peered: relay through subserver
        VectorBuffer relay;
        relay.WriteI32(MSG_GOD_DIRECTIVE);
        relay.Write(buf.GetData(), buf.GetSize());
        peerConnection_->SendMessage(MSG_RELAY_TO_AUTH, true, true, relay);
    }
    else
    {
        // Direct: send to auth server
        conn->SendMessage(MSG_GOD_DIRECTIVE, true, true, buf);
    }

    URHO3D_LOGINFOF("Sent GOD_DIRECTIVE: spawnId=%u task=%d (%s)", spawnId, taskId, dirName);

    if (hud_)
    {
        if (taskId < 0)
            hud_->SetContextHint("Directive cleared");
        else if (shiftHeld)
            hud_->SetContextHint("Task forbidden");
        else
            hud_->SetContextHint("Task assigned");
    }
}

// ── Creature Inspect HUD ─────────────────────────────────────────────────────

static const char* CreatureStateName(CreatureState s)
{
    switch (s)
    {
    case CREATURE_IDLE:     return "IDLE";
    case CREATURE_WANDER:   return "WANDER";
    case CREATURE_EAT:      return "EAT";
    case CREATURE_FLEE:     return "FLEE";
    case CREATURE_FIGHT:    return "FIGHT";
    case CREATURE_DIE:      return "DIE";
    case CREATURE_SIT:      return "SIT";
    case CREATURE_SLEEP:    return "SLEEP";
    case CREATURE_HUNT:     return "HUNT";
    case CREATURE_ALERT:    return "ALERT";
    case CREATURE_SCAVENGE: return "SCAVENGE";
    case CREATURE_CORPSE:   return "CORPSE";
    case CREATURE_TRAPPED:  return "TRAPPED";
    case CREATURE_VICTORY:  return "VICTORY";
    default:                return "???";
    }
}

static BorderImage* CreateVitalBar(Context* ctx, UIElement* parent, Font* font, const String& label, const Color& color, int yPos)
{
    auto* row = parent->CreateChild<UIElement>();
    row->SetPosition(8, yPos);
    row->SetFixedSize(180, 16);

    auto* lbl = row->CreateChild<Text>();
    lbl->SetFont(font, 10);
    lbl->SetText(label);
    lbl->SetColor(Color(0.87f, 0.8f, 0.73f));
    lbl->SetPosition(0, 0);

    auto* bg = row->CreateChild<BorderImage>();
    bg->SetPosition(65, 2);
    bg->SetFixedSize(110, 12);
    bg->SetColor(Color(0.15f, 0.15f, 0.15f));

    auto* fill = bg->CreateChild<BorderImage>();
    fill->SetPosition(0, 0);
    fill->SetFixedSize(110, 12);
    fill->SetColor(color);

    return fill;
}

void TerrainNode::ShowInspectPanel(Creature* creature)
{
    if (!creature || !creature->GetNode())
        return;

    inspectedNode_ = creature->GetNode();

    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    if (!inspectPanel_)
    {
        inspectPanel_ = new Window(context_);
        ui->GetRoot()->AddChild(inspectPanel_);
        inspectPanel_->SetStyleAuto();
        inspectPanel_->SetFixedSize(196, 140);
        inspectPanel_->SetPosition(10, 80);
        inspectPanel_->SetOpacity(0.85f);
        inspectPanel_->SetMovable(true);

        inspectNameText_ = inspectPanel_->CreateChild<Text>();
        inspectNameText_->SetFont(font, 11);
        inspectNameText_->SetColor(Color(0.95f, 0.9f, 0.8f));
        inspectNameText_->SetPosition(8, 4);

        inspectHpBar_ = CreateVitalBar(context_, inspectPanel_, font, "HP", Color(0.8f, 0.2f, 0.2f), 22);
        inspectHungerBar_ = CreateVitalBar(context_, inspectPanel_, font, "Hunger", Color(0.7f, 0.5f, 0.2f), 40);
        inspectThirstBar_ = CreateVitalBar(context_, inspectPanel_, font, "Thirst", Color(0.2f, 0.5f, 0.8f), 58);
        inspectWarmthBar_ = CreateVitalBar(context_, inspectPanel_, font, "Warmth", Color(0.8f, 0.6f, 0.1f), 76);
        inspectStaminaBar_ = CreateVitalBar(context_, inspectPanel_, font, "Stamina", Color(0.2f, 0.7f, 0.3f), 94);

        inspectStateText_ = inspectPanel_->CreateChild<Text>();
        inspectStateText_->SetFont(font, 10);
        inspectStateText_->SetColor(Color(0.7f, 0.7f, 0.7f));
        inspectStateText_->SetPosition(8, 116);
    }

    inspectPanel_->SetVisible(true);
    inspectNameText_->SetText(creature->GetNode()->GetName());
    UpdateInspectPanel();
}

void TerrainNode::HideInspectPanel()
{
    if (inspectPanel_)
        inspectPanel_->SetVisible(false);
    inspectedNode_.Reset();
}

void TerrainNode::UpdateInspectPanel()
{
    if (!inspectPanel_ || !inspectPanel_->IsVisible())
        return;
    if (inspectedNode_.Expired())
    {
        HideInspectPanel();
        return;
    }

    auto* creature = inspectedNode_->GetDerivedComponent<Creature>(true);
    if (!creature)
    {
        HideInspectPanel();
        return;
    }

    float hunger = creature->GetHunger();
    float thirst = creature->GetThirst();
    float warmth = creature->GetWarmth();
    float stamina = creature->GetStamina();
    float hp = (float)creature->GetHp();
    float maxHp = (float)Max(creature->GetMaxHp(), 1);

    inspectHpBar_->SetFixedSize((int)(110.0f * Clamp(hp / maxHp, 0.0f, 1.0f)), 12);
    inspectHungerBar_->SetFixedSize((int)(110.0f * Clamp(hunger / 100.0f, 0.0f, 1.0f)), 12);
    inspectThirstBar_->SetFixedSize((int)(110.0f * Clamp(thirst / 100.0f, 0.0f, 1.0f)), 12);
    inspectWarmthBar_->SetFixedSize((int)(110.0f * Clamp(warmth / 100.0f, 0.0f, 1.0f)), 12);
    inspectStaminaBar_->SetFixedSize((int)(110.0f * Clamp(stamina / 100.0f, 0.0f, 1.0f)), 12);

    inspectStateText_->SetText(String("State: ") + CreatureStateName(creature->GetState()));
}

// ── End Creature Inspect HUD ─────────────────────────────────────────────────

// Custom message IDs (must match AuthServer)
static const int MSG_AUTH_LOGIN      = 100;
static const int MSG_AUTH_REGISTER   = 101;
static const int MSG_AUTH_RESULT     = 102;
static const int MSG_LOAD_SCENE      = 103;
static const int MSG_REGISTER_GUID   = 104;  // Client → AuthServer: register NAT GUID after auth
// MSG_WEATHER_UPDATE (0x78) and MSG_CELESTIAL_STATE (0x79) now in Protocol.h

// Remote event for telling clients which avatar node they control
static const StringHash E_CLIENTOBJECTID("ClientObjectID");
static const StringHash P_ID("ID");
// Edit messages: MSG_EDIT_TERRAIN (0xA2), MSG_EDIT_OBJECT (0xA3),
// MSG_EDIT_REJECT (0xA4), MSG_EDIT_BROADCAST (0xA5) — defined in Protocol.h

// Fire System Phase 3 — server pit state replication via remote event.
// Strings must match AuthServer.cpp definitions.
static const StringHash E_PIT_STATE_CHANGED("PitStateChanged");
static const StringHash P_PIT_ID("PitId");
static const StringHash P_PIT_STATE("State");
static const StringHash P_PIT_BURN_UNITS("BurnUnits");
static const StringHash P_PIT_BURN_RATE("BurnRate");
static const StringHash P_PIT_WETNESS("Wetness");
static const StringHash P_PIT_POS_X("PosX");
static const StringHash P_PIT_POS_Z("PosZ");
static const StringHash P_PIT_UTC_MS("UtcMs");
static const StringHash P_PIT_MAX_FUEL("MaxFuel");
static const StringHash P_PIT_SIGNAL("Signal");

// Fire System Phase 4b — client→server tend request (embers revival).
// Strings must match AuthServer.cpp.
static const StringHash E_PIT_TEND_REQUEST("PitTendRequest");
static const StringHash P_PIT_TEND_ITEM("ItemId");
static const StringHash P_PIT_TEND_QTY("Quantity");

// Fire System Phase 4a — friction ignition events. Strings match AuthServer.cpp.
static const StringHash E_PIT_IGNITE_REQUEST("PitIgniteRequest");
static const StringHash E_PIT_IGNITION_STATUS("PitIgnitionStatus");
static const StringHash P_PIT_IGNITION_ACTIVE("Active");
static const StringHash P_PIT_IGNITION_PROGRESS("Progress");

// Fire System Phase 4c — torch events. Strings match AuthServer.cpp.
static const StringHash E_TORCH_LIGHT_REQUEST("TorchLightRequest");
static const StringHash E_TORCH_IGNITE_REQUEST("TorchIgniteRequest");

// Woodpile server sync — strings match AuthServer.cpp.
static const StringHash E_WOODPILE_DEPOSIT("WoodpileDeposit");
static const StringHash E_WOODPILE_STATE("WoodpileState");
static const StringHash P_PILE_BUILDING_ID("BuildingId");
static const StringHash P_PILE_SOFTWOOD("Softwood");
static const StringHash P_PILE_HARDWOOD("Hardwood");
static const StringHash P_PILE_CAPACITY("Capacity");

void TerrainNode::Start()
{
    Sample::Start();

    // Replace the Sample base's generic window icon + title with our custom
    // stone-age icon for this app. Sample::Start calls SetWindowTitleAndIcon
    // which hardcodes Textures/UrhoIcon.png — override here.
    {
        auto* graphics = GetSubsystem<Graphics>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (auto* icon = cache->GetResource<Image>("Icons/60_TerrainNode.png"))
            graphics->SetWindowIcon(icon);
        graphics->SetWindowTitle("Urho3D — TerrainNode");
    }

    // Register animal types
    Rabbit::RegisterObject(context_);
    Deer::RegisterObject(context_);
    Fox::RegisterObject(context_);
    Wolf::RegisterObject(context_);
    Stag::RegisterObject(context_);
    Bull::RegisterObject(context_);
    Cow::RegisterObject(context_);
    Horse::RegisterObject(context_);
    Donkey::RegisterObject(context_);
    Alpaca::RegisterObject(context_);
    Husky::RegisterObject(context_);
    ShibaInu::RegisterObject(context_);
    CaveMan::RegisterObject(context_);
    CaveWoman::RegisterObject(context_);
    Fish::RegisterObject(context_);
    SchoolFish::RegisterObject(context_);
    GrassSystem::RegisterObject(context_);
    HUD::RegisterObject(context_);
    ResourcePickup::RegisterObject(context_);
    BuildingSystem::RegisterObject(context_);  // Re-enabled: needed for network replication
    Soundscape::RegisterObject(context_);
    MetalDeposits::RegisterObject(context_);
    SoilMap::RegisterObject(context_);
    // Driven key / response curve system
    {
        context_->RegisterSubsystem(new DrivenKeySystem(context_));
        auto* dks = GetSubsystem<DrivenKeySystem>();
        if (dks->LoadSet("DrivenKeys/test_curves.json"))
        {
            URHO3D_LOGINFO("DrivenKeySystem: test_curves.json loaded OK");
        }
        else
            URHO3D_LOGWARNING("DrivenKeySystem: test_curves.json load FAILED");
    }

    // Fallback RNG seed — local time + favourite prime.
    SetRandomSeed((unsigned)time(nullptr) + 25773u);

    auto* cache = GetSubsystem<ResourceCache>();
    auto* uiStyle = cache->GetResource<XMLFile>("UI/WarmStyle.xml");
    GetSubsystem<UI>()->GetRoot()->SetDefaultStyle(uiStyle);

    // Load font from shared theme prefs
    LoadThemePrefs();
    font_ = cache->GetResource<Font>("Fonts/" + currentFontName_ + ".ttf");
    if (!font_)
        font_ = font_;

    // Login scene — animated sky backdrop
    CreateLoginScene();
    SetupViewport();
    SubscribeToEvents();
    CreateLoginUI();

    Sample::InitMouseMode(MM_FREE);

    // Radial command UI (replaces possession)
    InitRadialCommands();

    // NPC ring UI — health/stat overlay above NPC heads
    npcRingUI_ = new NPCRingUI(context_);

    // Prevent 10fps throttle when REQUIRE_CLICK_TO_FOCUS blocks re-focus in MM_RELATIVE
    GetSubsystem<Engine>()->SetMaxInactiveFps(60);

    auto* graphics = GetSubsystem<Graphics>();
    auto* ui = GetSubsystem<UI>();
    profilerUI_ = new ProfilerUI(context_);
    profilerUI_->Initialize(ui, graphics->GetVulkanProfiler(), graphics);
    profilerUI_->SetVisible(false);
}

// ============================================================================
// Login Scene — animated sky backdrop
// ============================================================================

void TerrainNode::CreateLoginScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();

    // Zone — dark ambient, atmospheric
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.1f, 0.1f, 0.15f));
    zone->SetFogStart(500.0f);
    zone->SetFogEnd(750.0f);
    zone_ = zone;
    origFogColor_ = zone->GetFogColor();
    origFogStart_ = zone->GetFogStart();
    origFogEnd_ = zone->GetFogEnd();

    // Directional light (sun)
    Node* lightNode = scene_->CreateChild("DirectionalLight", LOCAL);
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    sunLight_ = lightNode->CreateComponent<Light>();
    sunLight_->SetLightType(LIGHT_DIRECTIONAL);
    sunLight_->SetColor(Color(1.2f, 1.2f, 1.2f));

    // Skybox
    Node* skyNode = scene_->CreateChild("Sky");
    skyNode->SetScale(500.0f);
    auto* skybox = skyNode->CreateComponent<Skybox>();
    skybox->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    skyboxMat_ = cache->GetResource<Material>("Materials/Skybox.xml");
    skybox->SetMaterial(skyboxMat_);

    // Preload seasonal skybox cubemaps
    seasonSkyboxes_[0] = cache->GetResource<TextureCube>("Textures/SkyboxSpring.xml");
    seasonSkyboxes_[1] = cache->GetResource<TextureCube>("Textures/SkyboxSummer.xml");
    seasonSkyboxes_[2] = cache->GetResource<TextureCube>("Textures/SkyboxAutumn.xml");
    seasonSkyboxes_[3] = cache->GetResource<TextureCube>("Textures/SkyboxWinter.xml");
    weatherSkyboxes_[0] = cache->GetResource<TextureCube>("Textures/SkyboxClear.xml");
    weatherSkyboxes_[1] = cache->GetResource<TextureCube>("Textures/SkyboxOvercast.xml");
    weatherSkyboxes_[2] = cache->GetResource<TextureCube>("Textures/SkyboxStorm.xml");

    // Load cloud map faces for CPU-side weather sampling (BrightDay1 has alpha channel)
    {
        const char* cloudFaceNames[] = {
            "Textures/BrightDay1_PosX.png", "Textures/BrightDay1_NegX.png",
            "Textures/BrightDay1_PosY.png", "Textures/BrightDay1_NegY.png",
            "Textures/BrightDay1_PosZ.png", "Textures/BrightDay1_NegZ.png"
        };
        for (int i = 0; i < 6; ++i)
            cloudFaces_[i] = cache->GetResource<Image>(cloudFaceNames[i]);
    }

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
        SyncMelbourneTime();
    timeSyncTimer_ = timeOverride ? 9999.0f : 300.0f;

    CreateCelestialBodies();

    // Camera — angled up at the sky
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(750.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
    cameraNode_->SetRotation(Quaternion(30.0f, 0.0f, 0.0f)); // look upward
}

// ============================================================================
// Login UI
// ============================================================================

void TerrainNode::CreateLoginUI()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();
    Font* font = font_;

    // Centered login window
    loginWindow_ = root->CreateChild<Window>();
    loginWindow_->SetStyle("Window");
    loginWindow_->SetLayout(LM_VERTICAL, 10, IntRect(20, 20, 20, 20));
    loginWindow_->SetAlignment(HA_CENTER, VA_CENTER);
    loginWindow_->SetMinSize(340, 300);
    loginWindow_->SetColor(Color(0.12f, 0.12f, 0.18f, 0.92f));

    // Title
    auto* title = loginWindow_->CreateChild<Text>();
    title->SetFont(font, 22);
    title->SetText("TerrainNode");
    title->SetColor(Color(0.8f, 0.85f, 1.0f));
    title->SetHorizontalAlignment(HA_CENTER);

    // Subtitle / server status
    loginStatusText_ = loginWindow_->CreateChild<Text>();
    loginStatusText_->SetFont(font, 12);
    loginStatusText_->SetText("Scanning for server...");
    loginStatusText_->SetColor(Color(0.6f, 0.6f, 0.6f));
    loginStatusText_->SetHorizontalAlignment(HA_CENTER);

    // Spacer
    auto* spacer = loginWindow_->CreateChild<UIElement>();
    spacer->SetFixedHeight(10);

    // Username label + field
    auto* userLabel = loginWindow_->CreateChild<Text>();
    userLabel->SetFont(font, 13);
    userLabel->SetText("Username");
    userLabel->SetColor(Color(0.7f, 0.7f, 0.8f));

    usernameEdit_ = loginWindow_->CreateChild<LineEdit>();
    usernameEdit_->SetStyle("LineEdit");
    usernameEdit_->SetFixedHeight(28);
    usernameEdit_->SetMinWidth(300);

    // Password label + field
    auto* passLabel = loginWindow_->CreateChild<Text>();
    passLabel->SetFont(font, 13);
    passLabel->SetText("Password");
    passLabel->SetColor(Color(0.7f, 0.7f, 0.8f));

    passwordEdit_ = loginWindow_->CreateChild<LineEdit>();
    passwordEdit_->SetStyle("LineEdit");
    passwordEdit_->SetFixedHeight(28);
    passwordEdit_->SetMinWidth(300);
    passwordEdit_->SetEchoCharacter('*');

    // Spacer
    auto* spacer2 = loginWindow_->CreateChild<UIElement>();
    spacer2->SetFixedHeight(8);

    // Button row
    auto* btnRow = loginWindow_->CreateChild<UIElement>();
    btnRow->SetLayout(LM_HORIZONTAL, 10);
    btnRow->SetFixedHeight(36);
    btnRow->SetHorizontalAlignment(HA_CENTER);

    // Login button
    loginBtn_ = btnRow->CreateChild<Button>();
    loginBtn_->SetStyle("Button");
    loginBtn_->SetFixedSize(130, 34);
    auto* loginLabel = loginBtn_->CreateChild<Text>();
    loginLabel->SetFont(font, 15);
    loginLabel->SetText("Login");
    loginLabel->SetHorizontalAlignment(HA_CENTER);
    loginLabel->SetVerticalAlignment(VA_CENTER);
    loginLabel->SetColor(Color(0.9f, 0.9f, 1.0f));

    // Register button
    registerBtn_ = btnRow->CreateChild<Button>();
    registerBtn_->SetStyle("Button");
    registerBtn_->SetFixedSize(130, 34);
    auto* regLabel = registerBtn_->CreateChild<Text>();
    regLabel->SetFont(font, 15);
    regLabel->SetText("Register");
    regLabel->SetHorizontalAlignment(HA_CENTER);
    regLabel->SetVerticalAlignment(VA_CENTER);
    regLabel->SetColor(Color(0.9f, 0.9f, 1.0f));

    // Play Offline button (debug — bypass auth)
    auto* offlineBtn = btnRow->CreateChild<Button>();
    offlineBtn->SetStyle("Button");
    offlineBtn->SetFixedSize(130, 34);
    auto* offLabel = offlineBtn->CreateChild<Text>();
    offLabel->SetFont(font, 15);
    offLabel->SetText("Offline");
    offLabel->SetHorizontalAlignment(HA_CENTER);
    offLabel->SetVerticalAlignment(VA_CENTER);
    offLabel->SetColor(Color(1.0f, 0.8f, 0.4f));

    // Wire up buttons
    SubscribeToEvent(loginBtn_, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleLoginButton));
    SubscribeToEvent(registerBtn_, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleRegisterButton));
    SubscribeToEvent(offlineBtn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleOfflineButton));
}

void TerrainNode::DestroyLoginUI()
{
    if (loginWindow_)
    {
        loginWindow_->Remove();
        loginWindow_ = nullptr;
    }
    usernameEdit_ = nullptr;
    passwordEdit_ = nullptr;
    loginBtn_ = nullptr;
    registerBtn_ = nullptr;
    loginStatusText_ = nullptr;
}

void TerrainNode::HandleLoginButton(StringHash eventType, VariantMap& eventData)
{
    String username = usernameEdit_ ? usernameEdit_->GetText().Trimmed() : String::EMPTY;
    String password = passwordEdit_ ? passwordEdit_->GetText().Trimmed() : String::EMPTY;

    if (username.Empty() || password.Empty())
    {
        if (loginStatusText_)
        {
            loginStatusText_->SetText("Enter username and password");
            loginStatusText_->SetColor(Color(1.0f, 0.6f, 0.2f));
        }
        return;
    }

    if (loginStatusText_)
    {
        loginStatusText_->SetText("Authenticating...");
        loginStatusText_->SetColor(Color(0.7f, 0.7f, 0.7f));
    }

    // PAKE: set credentials, then connect (or reconnect if already connected)
    auto* network = GetSubsystem<Network>();
    network->SetCredentials(username, password);

    // If already connected, disconnect first — need fresh key exchange with PAKE
    if (authConnected_)
    {
        network->Disconnect(100);
        authConnected_ = false;
    }

    // Create empty game scene for the connection — server will load TestScene.xml
    // into it via MSG_LOADSCENE. Login scene stays visible until EnterWorld.
    if (!gameSceneReady_)
    {
        gameScene_ = new Scene(context_);
        SubscribeToEvent(gameScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(TerrainNode, OnGameSceneLoaded));
        // Creature attachment handled by per-frame scan in HandleUpdate
        gameSceneReady_ = true;
    }
    // Connect with the game scene — MSG_LOADSCENE will target this scene
    network->Connect(authServerAddress_, authServerPort_, gameScene_);
}

void TerrainNode::HandleRegisterButton(StringHash eventType, VariantMap& eventData)
{
    String username = usernameEdit_ ? usernameEdit_->GetText().Trimmed() : String::EMPTY;
    String password = passwordEdit_ ? passwordEdit_->GetText().Trimmed() : String::EMPTY;

    if (username.Empty() || password.Empty())
    {
        if (loginStatusText_)
        {
            loginStatusText_->SetText("Enter username and password");
            loginStatusText_->SetColor(Color(1.0f, 0.6f, 0.2f));
        }
        return;
    }

    if (loginStatusText_)
    {
        loginStatusText_->SetText("Registering...");
        loginStatusText_->SetColor(Color(0.7f, 0.7f, 0.7f));
    }

    auto* network = GetSubsystem<Network>();

    // Registration uses plain DH (no PAKE) — no password exists yet
    network->ClearCredentials();

    // If not connected, connect first (plain DH), then send register on connect
    if (!authConnected_)
    {
        // Store credentials for sending after connect
        pendingRegisterUsername_ = username;
        pendingRegisterPassword_ = password;
        if (!gameSceneReady_)
        {
            gameScene_ = new Scene(context_);
            SubscribeToEvent(gameScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(TerrainNode, OnGameSceneLoaded));
            gameSceneReady_ = true;
        }
        network->Connect(authServerAddress_, authServerPort_, gameScene_);
        return;
    }

    // Already connected — send register directly
    Connection* serverConnection = network->GetServerConnection();
    if (serverConnection)
    {
        VectorBuffer msg;
        msg.WriteString(username);
        msg.WriteString(password);
        serverConnection->SendMessage(MSG_AUTH_REGISTER, true, true, msg);
    }
}

void TerrainNode::HandleOfflineButton(StringHash eventType, VariantMap& eventData)
{
    // Offline mode is no longer a hard bypass of AuthServer. The game's
    // server-authoritative systems (combat HP, harvest, inventory, replacement
    // spawns, trap-catch, ...) all require an AuthServer connection — there is
    // no parallel offline code path. Instead, "offline" means: dial the local
    // AuthServer, and if no process is bound to the port, spawn one ourselves
    // and retry. The reserved login bypasses the password prompt.

    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // CRITICAL: turn off LAN discovery before doing anything else. The login
    // screen's per-frame update loop forcibly overwrites loginStatusText_ to
    // "Scanning for server..." every frame as long as authDiscovering_ is true,
    // which would make our offline status text invisible. Also stops the periodic
    // UDP discovery ping from interfering with our explicit localhost dial.
    authDiscovering_ = false;
    discoveryTimer_  = 999999.0f;  // belt + braces — make sure it never re-fires

    // Offline mode: skip PAKE encryption for localhost. The PAKE key
    // derivation has a server→client key mismatch bug that causes the client
    // to decrypt server messages as garbage → instant disconnect. Loopback
    // doesn't need encryption anyway. The auth still uses MSG_AUTH_LOGIN
    // with the reserved credentials; it's just not wrapped in PAKE.
    // PAKE disabled for localhost — loopback doesn't need encryption.
    // network->SetCredentials(String(OFFLINE_RESERVED_USERNAME),
    //                         String(OFFLINE_RESERVED_PASSWORD));
    URHO3D_LOGINFO("[NetDebug] Offline mode: PAKE disabled for localhost (crypto bypass)");

    authServerAddress_ = "127.0.0.1";
    // authServerPort_ keeps its existing value (set during DiscoverAuthServer or
    // from defaults). The local AuthServer binds to the same port.

    if (loginStatusText_)
    {
        loginStatusText_->SetText("Offline mode — connecting to local AuthServer...");
        loginStatusText_->SetColor(Color(0.7f, 0.7f, 0.9f));
    }
    URHO3D_LOGINFOF("Offline mode: button pressed — dialing 127.0.0.1:%u (max %d retries × %.1fs)",
                     (unsigned)authServerPort_, OFFLINE_MAX_RETRIES, OFFLINE_RETRY_INTERVAL);

    // Spawn a local AuthServer FIRST if none is running. Don't try to
    // connect before the server exists — that wastes the first attempt,
    // occupies SLikeNet's connection state, and causes "already in progress"
    // races on the retry loop.
    auto* fs = GetSubsystem<FileSystem>();
    bool serverAlreadyRunning = false;
    if (fs)
    {
        // Quick probe: can we connect to the port? If yes, server is already up.
        // SLikeNet doesn't have a non-blocking probe, so we check if the binary
        // is already in the process table. This isn't perfect but avoids spawning
        // a duplicate.
        String binaryName = "AuthServer";
        unsigned probe = fs->SystemRun(
            "/bin/sh", {"-c", "pgrep -x " + binaryName + " >/dev/null 2>&1"});
        serverAlreadyRunning = (probe == 0);
    }

    if (!serverAlreadyRunning)
    {
        URHO3D_LOGINFO("[NetDebug] No local AuthServer detected — spawning before connect");
        OfflineSpawnAuthServer();
    }
    else
        URHO3D_LOGINFO("[NetDebug] Local AuthServer already running — skipping spawn");

    offlineMode_ = OFFLINE_SPAWN_PENDING;
    offlineRetriesLeft_ = OFFLINE_MAX_RETRIES;
    offlineRetryTimer_  = 2.0f;  // give the server 2s head start before first dial
}

void TerrainNode::OfflineDial()
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // If a stale connection is open, drop it before dialing again.
    if (authConnected_)
    {
        network->Disconnect(100);
        authConnected_ = false;
    }

    URHO3D_LOGINFOF("Offline mode: Connect(%s, %u) [state=%d retriesLeft=%d]",
                     authServerAddress_.CString(), (unsigned)authServerPort_,
                     (int)offlineMode_, offlineRetriesLeft_);
    if (!gameSceneReady_)
    {
        gameScene_ = new Scene(context_);
        SubscribeToEvent(gameScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(TerrainNode, OnGameSceneLoaded));
        // Creature attachment handled by per-frame scan in HandleUpdate
        gameSceneReady_ = true;
    }
    network->Connect(authServerAddress_, authServerPort_, gameScene_);
}

void TerrainNode::OfflineSpawnAuthServer()
{
    auto* fs = GetSubsystem<FileSystem>();
    if (!fs)
    {
        URHO3D_LOGERROR("Offline mode: no FileSystem subsystem; cannot spawn AuthServer");
        return;
    }

    // AuthServer binary lives next to the running 60_TerrainNode binary.
    String binaryPath = fs->GetProgramDir() + "AuthServer";
#ifdef _WIN32
    binaryPath += ".exe";
#endif

    if (!fs->FileExists(binaryPath))
    {
        URHO3D_LOGERRORF("Offline mode: AuthServer binary not found at %s", binaryPath.CString());
        if (loginStatusText_)
        {
            loginStatusText_->SetText("AuthServer binary not found next to client");
            loginStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
        }
        offlineMode_ = OFFLINE_NONE;
        return;
    }

    Vector<String> args;
    // Pass the same log level to the child AuthServer so debug diagnostics
    // are visible in its log file (AuthServer.log in the binary directory).
    args.Push("-LogLevel");
    args.Push("1");
    unsigned pid = fs->SystemRunAsync(binaryPath, args);
    if (pid == 0)
    {
        URHO3D_LOGERROR("Offline mode: SystemRunAsync(AuthServer) returned 0");
        if (loginStatusText_)
        {
            loginStatusText_->SetText("Failed to launch AuthServer process");
            loginStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
        }
        offlineMode_ = OFFLINE_NONE;
        return;
    }

    URHO3D_LOGINFOF("Offline mode: spawned local AuthServer (pid %u, %s)", pid, binaryPath.CString());
    if (loginStatusText_)
    {
        loginStatusText_->SetText("Spawned local AuthServer — retrying...");
        loginStatusText_->SetColor(Color(0.7f, 0.7f, 0.9f));
    }
}

void TerrainNode::TickOfflineConnect(float timeStep)
{
    if (offlineMode_ == OFFLINE_NONE)
        return;
    if (offlineRetryTimer_ > 0.0f)
    {
        offlineRetryTimer_ -= timeStep;
        if (offlineRetryTimer_ > 0.0f)
            return;
    }

    if (offlineMode_ == OFFLINE_SPAWN_PENDING || offlineMode_ == OFFLINE_RETRY_CONNECT)
    {
        if (offlineRetriesLeft_ <= 0)
        {
            URHO3D_LOGERROR("Offline mode: gave up after retries — local AuthServer never came up");
            if (loginStatusText_)
            {
                loginStatusText_->SetText("Local AuthServer did not come up");
                loginStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
            }
            offlineMode_ = OFFLINE_NONE;
            return;
        }
        --offlineRetriesLeft_;
        offlineMode_ = OFFLINE_RETRY_CONNECT;
        OfflineDial();
        // Set the timer so the next CONNECTFAILED-or-quiet window has a budget.
        // The actual retry decrement happens on the next CONNECTFAILED event;
        // this timer prevents us from spinning if the failure is silent.
        offlineRetryTimer_ = OFFLINE_RETRY_INTERVAL;
    }
}

// ============================================================================
// Enter World — called after successful authentication
// ============================================================================

void TerrainNode::EnterWorld()
{
    URHO3D_LOGINFO("[NetDebug] EnterWorld() — setting loggedIn=true, destroying login UI");
    loggedIn_ = true;
    DestroyLoginUI();

    // Show loading overlay while world builds
    {
        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        loadingText_ = ui->GetRoot()->CreateChild<Text>();
        loadingText_->SetFont(font_, 18);
        loadingText_->SetText("Loading world...");
        loadingText_->SetColor(Color(0.9f, 0.85f, 0.7f));
        loadingText_->SetHorizontalAlignment(HA_CENTER);
        loadingText_->SetVerticalAlignment(VA_CENTER);
    }

    // Wait for GPU to finish all commands referencing login scene resources
#ifdef URHO3D_VULKAN
    auto* graphics = GetSubsystem<Graphics>();
    if (graphics)
    {
        auto* impl = graphics->GetImpl_Vulkan();
        if (impl && impl->GetDevice())
            vkDeviceWaitIdle(impl->GetDevice());
    }
#endif

    // The game scene was created and assigned to the connection in
    // HandleServerConnected (before auth). Switch scene_ to it.
    // MSG_LOADSCENE may have already loaded TestScene.xml into gameScene_.
    scene_ = gameScene_;

    if (!scene_)
    {
        URHO3D_LOGERROR("[NetDebug] EnterWorld: no game scene — connection setup failed");
        return;
    }

    // Check if server scene has already loaded (MSG_LOADSCENE arrived during auth)
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (serverConn && serverConn->IsSceneLoaded())
    {
        URHO3D_LOGINFO("[NetDebug] Scene already loaded — setting up world");
        VariantMap dummy;
        OnGameSceneLoaded(StringHash::ZERO, dummy);
    }
    else
    {
        URHO3D_LOGINFO("[NetDebug] Waiting for MSG_LOADSCENE from server...");
    }
}

void TerrainNode::OnGameSceneLoaded(StringHash, VariantMap&)
{
    URHO3D_LOGINFO("[NetDebug] OnGameSceneLoaded — server scene loaded, adding local visuals");

    // Switch to the game scene — this is now the active rendered scene
    if (gameScene_)
        scene_ = gameScene_;

    if (!scene_)
    {
        URHO3D_LOGERROR("[NetDebug] OnGameSceneLoaded: no scene!");
        return;
    }

    // Verify terrain loaded
    terrain_ = scene_->GetComponent<Terrain>(true);
    URHO3D_LOGINFOF("[NetDebug] Scene has %u children, terrain=%s",
        scene_->GetNumChildren(), terrain_ ? "YES" : "NO");

    // Connection::HandleAsyncLoadFinished sends MSG_SCENELOADED to the server.
    // No explicit send needed — verified working.

    // Create client-only visual entities (LOCAL nodes — not replicated)
    // Must happen BEFORE SetupSceneBindings so water/sky/camera nodes exist when bindings resolve
    URHO3D_LOGINFO("[NetDebug] Calling CreateLocalVisuals...");
    CreateLocalVisuals();
    URHO3D_LOGINFO("[NetDebug] CreateLocalVisuals done");

    // Bind cached pointers from the loaded scene (terrain_, zone_, etc.)
    URHO3D_LOGINFO("[NetDebug] Calling SetupSceneBindings...");
    SetupSceneBindings();
    URHO3D_LOGINFO("[NetDebug] SetupSceneBindings done");

    // Enter the world — UI, camera, HUD
    URHO3D_LOGINFO("[NetDebug] Calling FinishEnterWorld...");
    FinishEnterWorld();
    URHO3D_LOGINFO("[NetDebug] FinishEnterWorld done — world is live");
}

void TerrainNode::CreateLocalVisuals()
{
    if (!scene_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    // Directional light — client derives from UTC (server sends time)
    Node* lightNode = scene_->CreateChild("DirectionalLight", LOCAL);
    lightNode->SetRotation(Quaternion(60.0f, 30.0f, 0.0f));
    sunLight_ = lightNode->CreateComponent<Light>();
    sunLight_->SetLightType(LIGHT_DIRECTIONAL);
    sunLight_->SetColor(Color(1.0f, 0.95f, 0.8f));
    sunLight_->SetCastShadows(true);
    sunLight_->SetShadowCascade(CascadeParameters(20.0f, 60.0f, 180.0f, 560.0f, 0.1f));
    sunLight_->SetShadowBias(BiasParameters(0.00025f, 0.0f));

    // Moon light
    Node* moonNode = scene_->CreateChild("MoonLight", LOCAL);
    moonNode->SetRotation(Quaternion(120.0f, -30.0f, 0.0f));
    auto* moonLight = moonNode->CreateComponent<Light>();
    moonLight->SetLightType(LIGHT_DIRECTIONAL);
    moonLight->SetColor(Color(0.3f, 0.35f, 0.5f));
    moonLight->SetBrightness(0.0f);

    // Skybox
    Node* skyNode = scene_->CreateChild("Sky", LOCAL);
    auto* skybox = skyNode->CreateComponent<Skybox>();
    skybox->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    skyboxMat_ = cache->GetResource<Material>("Materials/Skybox.xml");
    if (skyboxMat_)
        skybox->SetMaterial(skyboxMat_);

    // Water plane
    Node* waterNode = scene_->CreateChild("Water", LOCAL);
    waterNode->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
    waterNode->SetScale(Vector3(2048.0f, 1.0f, 2048.0f));
    auto* waterModel = waterNode->CreateComponent<StaticModel>();
    waterModel->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
    waterModel->SetMaterial(cache->GetResource<Material>("Materials/Water.xml"));

    // Camera node (LOCAL — each client has their own)
    cameraNode_ = scene_->CreateChild("Camera", LOCAL);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(750.0f);
    cameraNode_->SetPosition(Vector3(0.0f, 50.0f, 0.0f));

    // Client-only entities — things the server doesn't replicate
    CreateCelestialBodies();
    CreateOOFOs();
    CreateFish();
    CreateSchoolFish();
    CreateEcosystem();
    CreateGrass();
    InitTreeModels();
    CreateRain();
    CreateSnow();

    // Weight map + biome cache
    CacheWeightMapImage();

    // GameDB + building system
    InitGameDB();
    InitBuildingSystem();

    // Resource map (client receives from server via protocol, but needs local init)
    CreateResourceMap();

    // Ambient soundscape (client-only — server has no audio)
    Node* soundNode = scene_->CreateChild("Soundscape", LOCAL);
    soundscape_ = soundNode->CreateComponent<Soundscape>();

    // 3D audio listener on camera node
    listenerNode_ = cameraNode_->CreateChild("Listener");
    listenerNode_->CreateComponent<SoundListener>();
    GetSubsystem<Audio>()->SetListener(listenerNode_->GetComponent<SoundListener>());

    // HUD — vital bars
    Node* hudNode = scene_->CreateChild("HUD", LOCAL);
    hud_ = hudNode->CreateComponent<HUD>();
    hud_->SetFont(font_, 9);

    SyncCampfireUI();
}

void TerrainNode::FinishEnterWorld()
{
    // Remove loading overlay
    if (loadingText_)
    {
        loadingText_->Remove();
        loadingText_.Reset();
    }

    CreateInstructions();
    SetupViewport();

    // Phase 5a: start in god cam with free cursor — player clicks NPCs to possess.
    Sample::InitMouseMode(MM_FREE);
    cameraMode_ = CAM_GOD;
    menuOpen_ = true;

    CreateMenuBar();
    CreateMinimap();
    CreateSelectedVitalsPanel();

    // AuthServer network panel (top-right) — shows connected status
    {
        auto* cache = GetSubsystem<ResourceCache>();
        auto* ui = GetSubsystem<UI>();
        Font* font = font_;

        networkPanel_ = ui->GetRoot()->CreateChild<Window>();
        auto* panel = networkPanel_;
        panel->SetStyle("Window");
        panel->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));
        panel->SetHorizontalAlignment(HA_RIGHT);
        panel->SetVerticalAlignment(VA_TOP);
        panel->SetPosition(-8, 30);
        panel->SetMinWidth(240);
        panel->SetColor(Color(0.15f, 0.15f, 0.2f, 0.9f));

        // Hide network panel in offline mode — it's just clutter
        if (loggedInUsername_ == "Offline")
            panel->SetVisible(false);

        auto* title = panel->CreateChild<Text>();
        title->SetFont(font, 16);
        title->SetText("Network");
        title->SetColor(Color(0.8f, 0.8f, 1.0f));

        // Logged-in user label
        auto* userLabel = panel->CreateChild<Text>();
        userLabel->SetFont(font, 13);
        userLabel->SetText("User: " + loggedInUsername_);
        userLabel->SetColor(Color(0.3f, 1.0f, 0.3f));

        authConnectBtn_ = panel->CreateChild<Button>();
        authConnectBtn_->SetStyle("Button");
        authConnectBtn_->SetFixedHeight(32);
        authConnectBtn_->SetMinWidth(220);

        authBtnLabel_ = authConnectBtn_->CreateChild<Text>();
        authBtnLabel_->SetFont(font, 15);
        authBtnLabel_->SetHorizontalAlignment(HA_CENTER);
        authBtnLabel_->SetVerticalAlignment(VA_CENTER);

        networkStatusText_ = panel->CreateChild<Text>();
        networkStatusText_->SetFont(font, 14);
        if (authConnected_)
        {
            networkStatusText_->SetColor(Color(0.3f, 1.0f, 0.3f));
            networkStatusText_->SetText("Connected to " + authServerAddress_ + ":" + String(authServerPort_));
        }
        else
        {
            networkStatusText_->SetColor(Color(0.6f, 0.6f, 0.6f));
            networkStatusText_->SetText("Offline");
        }

        UpdateAuthButtonState();
        SubscribeToEvent(authConnectBtn_, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleAuthConnectButton));
    }

    // Position camera at owned patch center before spawning avatar
    // If initial position is underwater, search nearby for dry land
    if (cameraNode_)
    {
        const float patchWorldSize = 128.0f;
        float camX = (ownedPatchX_ + 0.5f) * patchWorldSize;
        float camZ = (ownedPatchZ_ + 0.5f) * patchWorldSize;
        float camY = 20.0f;
        if (terrain_)
        {
            float terrH = terrain_->GetHeight(Vector3(camX, 0.0f, camZ));
            if (terrH < 7.0f)  // Below or near water level
            {
                float bestH = terrH;
                for (float r = 16.0f; r <= 128.0f; r += 16.0f)
                {
                    for (int angle = 0; angle < 8; ++angle)
                    {
                        float a = angle * (M_PI / 4.0f);
                        float px = camX + r * cosf(a);
                        float pz = camZ + r * sinf(a);
                        float h = terrain_->GetHeight(Vector3(px, 0.0f, pz));
                        if (h > bestH)
                        {
                            bestH = h;
                            camX = px;
                            camZ = pz;
                        }
                    }
                    if (bestH > 12.0f)
                        break;
                }
                terrH = bestH;
            }
            camY = Max(terrH, 5.0f) + 10.0f;
        }
        cameraNode_->SetPosition(Vector3(camX, camY, camZ));
    }

    // Show owned patch boundaries (green)
    CreateOwnedPatchBoundaries();

    // Start in god cam — no player avatar spawned.
    // Player possesses HumanNPCs by clicking on them.
    cameraMode_ = CAM_GOD;
    possessing_ = false;

}

// ============================================================================
// Return to Login — called when server connection is lost while in-world
// ============================================================================

void TerrainNode::ReturnToLogin()
{
    URHO3D_LOGINFO("Server connection lost — returning to login screen");

    // Wait for GPU to finish all in-flight work before destroying scene resources.
    // Without this, Vulkan images/buffers/descriptors are freed while still referenced
    // by in-flight command buffers, causing SEGV in descriptor pool cleanup.
#ifdef URHO3D_VULKAN
    {
        auto* graphics = GetSubsystem<Graphics>();
        if (graphics)
        {
            auto* impl = graphics->GetImpl_Vulkan();
            if (impl && impl->GetDevice())
                vkDeviceWaitIdle(impl->GetDevice());
        }
    }
#endif

    // --- Tear down world UI ---
    // Remove all UI children except the profiler (which persists across scenes)
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();

    // Remove menu bar
    if (menuBar_)
    {
        menuBar_->Remove();
        menuBar_ = nullptr;
    }
    fileMenu_.Reset();
    editMenu_.Reset();
    viewMenu_ = nullptr;
    environmentMenu_ = nullptr;

    // Remove terrain panel
    terrainPanel_.Reset();

    // Remove hierarchy and inspector windows
    hierarchyWindow_.Reset();
    hierarchyList_ = nullptr;
    inspectorWindow_.Reset();
    inspectorScroll_ = nullptr;
    inspectorContent_ = nullptr;

    // Remove minimap
    if (minimap_)
    {
        minimap_->Remove();
        minimap_ = nullptr;
    }
    minimapCameraDot_ = nullptr;
    minimapTex_.Reset();
    minimapCameraNode_.Reset();

    // Remove instruction text
    if (instructionText_)
    {
        instructionText_->Remove();
        instructionText_ = nullptr;
    }

    // Remove file selector if open
    fileSelector_.Reset();

    // Remove network panel — authConnectBtn_ is a child of the panel Window
    if (authConnectBtn_)
    {
        // Walk up to the panel Window and remove it
        auto* panel = authConnectBtn_->GetParent();
        if (panel)
            panel->Remove();
        authConnectBtn_ = nullptr;
        authBtnLabel_ = nullptr;
        networkStatusText_ = nullptr;
    }

    // --- Reset world state ---
    loggedIn_ = false;
    loggedInUsername_.Clear();
    serverSceneName_.Clear();
    adminLevel_ = 0;

    // Hide admin-only UI on logout
    if (menuBar_)
    {
        auto* btn = menuBar_->GetChild("AITuningBtn", false);
        if (btn) btn->SetVisible(false);
    }
    if (tuningPanel_)
        tuningPanel_->SetVisible(false);

    // Reset cached pointers that refer to world scene nodes/components
    selectedNode_.Reset();
    originalMaterials_.Clear();
    sunNode_ = nullptr;
    moonNode_ = nullptr;
    sunLight_ = nullptr;
    moonLight_ = nullptr;
    sunMat_.Reset();
    moonMat_.Reset();
    skyboxMat_.Reset();
    lastSeasonIndex_ = -1;
    waterNode_.Reset();
    reflectionCameraNode_.Reset();
    zone_.Reset();
    terrain_.Reset();
    editableHeightMap_.Reset();
    renderPath_ = nullptr;
    reflectionRenderPath_ = nullptr;
    prefabBrush_.Reset();
    oofos_.Clear();
    oofosSpawned_ = 0;
    cameraNode_.Reset();

    // Reset undo/redo
    undoStack_.Clear();
    undoCursor_ = 0;
    terrainStrokeActive_ = false;
    terrainStrokeBefore_.Reset();

    // Reset brush state
    brushMode_ = 0;
    hasBrushHit_ = false;
    activeModeBtn_ = nullptr;
    activeShapeBtn_ = nullptr;

    // --- Rebuild login scene ---
    CreateLoginScene();
    SetupViewport();
    CreateLoginUI();

    Sample::InitMouseMode(MM_FREE);

    // Resume LAN discovery so we can reconnect
    authDiscovering_ = true;
    discoveryTimer_ = 0.0f;
}

// ============================================================================
// Scene
// ============================================================================

void TerrainNode::CreateScene()
{
    // Phase 1: Build the scene graph (all serializable nodes/components)
    CreateSceneGraph();
    // Phase 2: Bind app state (caches, pointers, non-serializable systems)
    SetupSceneBindings();
}

void TerrainNode::SetupSceneBindings()
{
    // Re-bind cached pointers from the scene graph. Safe to call after
    // CreateSceneGraph() or Scene::LoadXML() — finds everything by name.
    auto* cache = GetSubsystem<ResourceCache>();

    zone_ = scene_->GetComponent<Zone>(true);
    if (zone_)
    {
        origFogColor_ = zone_->GetFogColor();
        origFogStart_ = zone_->GetFogStart();
        origFogEnd_ = zone_->GetFogEnd();
    }

    Node* dlNode = scene_->GetChild("DirectionalLight", true);
    sunLight_ = dlNode ? dlNode->GetComponent<Light>() : nullptr;

    skyboxMat_ = cache->GetResource<Material>("Materials/Skybox.xml");

    // Preload seasonal skybox cubemaps
    seasonSkyboxes_[0] = cache->GetResource<TextureCube>("Textures/SkyboxSpring.xml");
    seasonSkyboxes_[1] = cache->GetResource<TextureCube>("Textures/SkyboxSummer.xml");
    seasonSkyboxes_[2] = cache->GetResource<TextureCube>("Textures/SkyboxAutumn.xml");
    seasonSkyboxes_[3] = cache->GetResource<TextureCube>("Textures/SkyboxWinter.xml");
    weatherSkyboxes_[0] = cache->GetResource<TextureCube>("Textures/SkyboxClear.xml");
    weatherSkyboxes_[1] = cache->GetResource<TextureCube>("Textures/SkyboxOvercast.xml");
    weatherSkyboxes_[2] = cache->GetResource<TextureCube>("Textures/SkyboxStorm.xml");

    // Load cloud map faces for CPU-side weather sampling
    {
        const char* cloudFaceNames[] = {
            "Textures/BrightDay1_PosX.png", "Textures/BrightDay1_NegX.png",
            "Textures/BrightDay1_PosY.png", "Textures/BrightDay1_NegY.png",
            "Textures/BrightDay1_PosZ.png", "Textures/BrightDay1_NegZ.png"
        };
        for (int i = 0; i < 6; ++i)
            cloudFaces_[i] = cache->GetResource<Image>(cloudFaceNames[i]);
    }

    terrain_ = scene_->GetComponent<Terrain>(true);
    if (terrain_)
    {
        // Editable heightmap (RGBA)
        Image* origHM = terrain_->GetHeightMap();
        if (origHM)
        {
            int w = origHM->GetWidth(), h = origHM->GetHeight();
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

            // Water map
            waterMap_ = cache->GetResource<Image>("Textures/WaterHeightMap.png");
            if (!waterMap_ || waterMap_->GetWidth() != w || waterMap_->GetHeight() != h)
            {
                waterMap_ = new Image(context_);
                waterMap_->SetSize(w, h, 1);
                memset(waterMap_->GetData(), 0, w * h);
            }
        }

        // Water map GPU texture
        waterMapTex_ = new Texture2D(context_);
        waterMapTex_->SetFilterMode(FILTER_BILINEAR);
        waterMapTex_->SetAddressMode(COORD_U, ADDRESS_CLAMP);
        waterMapTex_->SetAddressMode(COORD_V, ADDRESS_CLAMP);
        waterMapTex_->SetData(waterMap_);
        auto* terrainMat = terrain_->GetMaterial();
        if (terrainMat)
        {
            terrainMat->SetTexture(TU_ENVIRONMENT, waterMapTex_);
            terrainMat->SetShaderParameter("WaterLevel", 5.0f);
            terrainMat->SetShaderParameter("TerrainSpacingY", terrain_->GetSpacing().y_);
        }

        // Rainfall accumulation map (Weather Phase 4)
        rainfallMap_ = new Image(context_);
        rainfallMap_->SetSize(RAINFALL_MAP_SIZE, RAINFALL_MAP_SIZE, 4);  // RGBA
        memset(rainfallMap_->GetData(), 0, RAINFALL_MAP_SIZE * RAINFALL_MAP_SIZE * 4);

        // Brush
        brush_ = new TerrainBrush(context_);
        brush_->SetTerrain(terrain_, editableHeightMap_);
        brush_->SetWaterMap(waterMap_);

        // Habitat rules + water distance map
        habitatRules_.Load(context_, "GameDB/habitat_rules.json");
        BuildWaterDistanceMap();
    }

    // Camera — create if it doesn't exist (startup from saved scene)
    if (!cameraNode_)
    {
        cameraNode_ = new Node(context_);
        auto* camera = cameraNode_->CreateComponent<Camera>();
        camera->SetFarClip(750.0f);
        // Place above terrain center
        float startY = 60.0f;
        if (terrain_)
            startY = Max(startY, terrain_->GetHeight(Vector3(64.0f, 0.0f, 64.0f)) + 10.0f);
        cameraNode_->SetPosition(Vector3(64.0f, startY, 64.0f));
    }

    waterNode_ = scene_->GetChild("Water", true);
    sunNode_ = scene_->GetChild("Sun", true);
    moonNode_ = scene_->GetChild("Moon", true);

    // Water reflection plane
    if (waterNode_)
    {
        waterPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());
        waterClipPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());
    }

    // Water ripple system
    rippleSystem_ = new WaterRippleSystem(context_);
    rippleSystem_->Initialize();
    {
        auto* waterMat = cache->GetResource<Material>("Materials/Water.xml");
        if (waterMat)
            waterMat->SetTexture(TU_EMISSIVE, rippleSystem_->GetTexture());
    }

    // Campfire pointers
    campfireNode_ = scene_->GetChild("Campfire", true);
    campfireLight_ = nullptr;
    campfireFireEmitter_ = nullptr;
    campfireSmokeEmitter_ = nullptr;
    if (campfireNode_)
    {
        Node* fl = campfireNode_->GetChild("FireLight");
        if (fl) campfireLight_ = fl->GetComponent<Light>();
        Node* fn = campfireNode_->GetChild("Fire");
        if (fn) campfireFireEmitter_ = fn->GetComponent<ParticleEmitter>();
        Node* sn = campfireNode_->GetChild("Smoke");
        if (sn) campfireSmokeEmitter_ = sn->GetComponent<ParticleEmitter>();
        // Capture intensity baselines from the loaded scene's effect/light values
        if (campfireLight_)
        {
            cfBaseLightBrightness_ = campfireLight_->GetBrightness();
            cfBaseLightRange_ = campfireLight_->GetRange();
        }
        if (campfireFireEmitter_ && campfireFireEmitter_->GetEffect())
        {
            cfBaseFireRateMin_ = campfireFireEmitter_->GetEffect()->GetMinEmissionRate();
            cfBaseFireRateMax_ = campfireFireEmitter_->GetEffect()->GetMaxEmissionRate();
        }
        if (campfireSmokeEmitter_ && campfireSmokeEmitter_->GetEffect())
        {
            cfBaseSmokeRateMin_ = campfireSmokeEmitter_->GetEffect()->GetMinEmissionRate();
            cfBaseSmokeRateMax_ = campfireSmokeEmitter_->GetEffect()->GetMaxEmissionRate();
        }
        fireBrightnessTarget_ = cfBaseLightBrightness_;
        fireBrightnessCurrent_ = fireBrightnessTarget_;
        fireColorTarget_ = campfireLight_ ? campfireLight_->GetColor() : Color(1.0f, 0.6f, 0.2f);
        fireColorCurrent_ = fireColorTarget_;
        fireOut_ = false;
    }
    SyncCampfireUI();

    // Soundscape
    soundscape_ = scene_->GetDerivedComponent<Soundscape>(true);

    // HUD
    hud_ = scene_->GetDerivedComponent<HUD>(true);

    // Metal deposits — must be cached so terrain brush can expose buried deposits
    metalDeposits_ = scene_->GetDerivedComponent<MetalDeposits>(true);

    // Soil map — cached for O(1) soil queries
    soilMap_ = scene_->GetDerivedComponent<SoilMap>(true);

    // Restore settings from scene Vars (auto-serialized with scene)
    {
        const Variant& gr = scene_->GetVar("GodRays");
        godRaysEnabled_ = gr.IsEmpty() ? true : gr.GetBool();
    }
    {
        const Variant& sh = scene_->GetVar("Shadows");
        shadowsEnabled_ = sh.IsEmpty() ? true : sh.GetBool();
    }
    {
        const Variant& wr = scene_->GetVar("WaterReflection");
        waterReflectionEnabled_ = wr.IsEmpty() ? true : wr.GetBool();
    }
    {
        const Variant& pp = scene_->GetVar("PostProcess");
        postProcessEnabled_ = pp.IsEmpty() ? true : pp.GetBool();
    }
    {
        const Variant& hl = scene_->GetVar("HemisphereLighting");
        hemisphereEnabled_ = hl.IsEmpty() ? true : hl.GetBool();
    }

    // Deselect stale selection
    selectedNode_.Reset();
    originalMaterials_.Clear();

    // DebugRenderer (LOCAL — survives network Clear)
    if (!scene_->GetComponent<DebugRenderer>())
        scene_->CreateComponent<DebugRenderer>(LOCAL);

    // Weight map cache for biome classification
    CacheWeightMapImage();

    // GameDB + building system
    InitGameDB();
    InitBuildingSystem();

    // Entity creation moved to CreateLocalVisuals() — called from OnGameSceneLoaded
    // after SetupSceneBindings. This method only binds pointers now.
}

void TerrainNode::CreateSceneGraph()
{
    // Full procedural scene creation — matching proven Sample 23 (Water) pattern.
    // Everything built synchronously, no async XML loading.

    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    // LOCAL components survive Connection::HandleAsyncLoadFinished Clear(true, false)
    if (!scene_->GetComponent<Octree>())
        scene_->CreateComponent<Octree>(LOCAL);
    if (!scene_->GetComponent<PhysicsWorld>())
        scene_->CreateComponent<PhysicsWorld>(LOCAL);
    if (!scene_->GetComponent<DebugRenderer>())
        scene_->CreateComponent<DebugRenderer>(LOCAL);

    // Zone
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.15f, 0.15f, 0.15f));
    zone->SetFogColor(Color(0.7f, 0.7f, 0.75f));
    zone->SetFogStart(500.0f);
    zone->SetFogEnd(750.0f);
    float fogMinHeight = 5.0f;
    float fogMaxHeight = 18.0f;
    zone->SetFogHeight(fogMinHeight);
    zone->SetFogHeightScale(1.0f / Max(fogMaxHeight - fogMinHeight, M_EPSILON));

    // Directional light (sun)
    Node* lightNode = scene_->CreateChild("DirectionalLight", LOCAL);
    lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f));
    sunLight_ = lightNode->CreateComponent<Light>();
    sunLight_->SetLightType(LIGHT_DIRECTIONAL);
    sunLight_->SetCastShadows(false);  // Default off — user enables via perf toggles
    sunLight_->SetShadowBias(BiasParameters(0.00025f, 0.5f));
    sunLight_->SetShadowCascade(CascadeParameters(10.0f, 40.0f, 100.0f, 0.0f, 0.8f));
    sunLight_->SetSpecularIntensity(0.5f);
    sunLight_->SetColor(Color(1.2f, 1.2f, 1.2f));

    // Skybox
    Node* skyNode = scene_->CreateChild("Sky");
    skyNode->SetScale(500.0f);
    auto* skybox = skyNode->CreateComponent<Skybox>();
    skybox->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
    skybox->SetMaterial(cache->GetResource<Material>("Materials/Skybox.xml"));

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

    auto* terrainBody = terrainNode->CreateComponent<RigidBody>();
    terrainBody->SetCollisionLayer(2);
    terrainBody->SetFriction(0.75f);
    auto* terrainShape = terrainNode->CreateComponent<CollisionShape>();
    terrainShape->SetTerrain();

    // Metal deposits (scene component — serializable)
    {
        auto* md = terrainNode->CreateComponent<MetalDeposits>();
        int hmSize = terrain->GetHeightMap()->GetWidth();
        if (md->LoadMap("Textures/MetalDeposits.png", hmSize))
            md->SpawnOutcrops(scene_, terrain, 5.0f);
    }

    // Soil map (Plan 9 — soil type, fertility, mineral density, moisture)
    {
        auto* sm = terrainNode->CreateComponent<SoilMap>();
        int hmSize = terrain->GetHeightMap()->GetWidth();
        if (!sm->LoadMap("Textures/SoilMap.png", hmSize))
        {
            sm->GenerateDefault(terrain, 5.0f);
            sm->SaveMap("Data/Textures/SoilMap.png");
        }
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

    // Water — per-patch quads, only where terrain dips below water level.
    // Each patch is individually frustum-cullable, unlike a single giant quad.
    {
        const float waterY = 5.0f;
        waterNode_ = scene_->CreateChild("Water");
        waterNode_->SetPosition(Vector3(0.0f, waterY, 0.0f));
        baseWaterY_ = waterY;  // cache for drought recession

        auto* planeModel = cache->GetResource<Model>("Models/Plane.mdl");
        auto* waterMat = cache->GetResource<Material>("Materials/Water.xml");
        IntVector2 numPatches = terrain->GetNumPatches();
        Vector2 patchWorld = Vector2(terrain->GetSpacing().x_ * terrain->GetPatchSize(),
                                     terrain->GetSpacing().z_ * terrain->GetPatchSize());
        Vector3 terrainPos = terrainNode->GetWorldPosition();
        float halfTerrainX = numPatches.x_ * patchWorld.x_ * 0.5f;
        float halfTerrainZ = numPatches.y_ * patchWorld.y_ * 0.5f;

        int samplesPerEdge = 5;  // 5×5 sample grid per patch
        for (int pz = 0; pz < numPatches.y_; ++pz)
        {
            for (int px = 0; px < numPatches.x_; ++px)
            {
                float patchMinX = terrainPos.x_ - halfTerrainX + px * patchWorld.x_;
                float patchMinZ = terrainPos.z_ - halfTerrainZ + pz * patchWorld.y_;

                // Sample terrain heights across this patch
                bool hasWater = false;
                for (int sz = 0; sz <= samplesPerEdge && !hasWater; ++sz)
                {
                    for (int sx = 0; sx <= samplesPerEdge && !hasWater; ++sx)
                    {
                        float wx = patchMinX + (sx / (float)samplesPerEdge) * patchWorld.x_;
                        float wz = patchMinZ + (sz / (float)samplesPerEdge) * patchWorld.y_;
                        float h = terrain->GetHeight(Vector3(wx, 0.0f, wz));
                        if (h < waterY)
                            hasWater = true;
                    }
                }

                if (hasWater)
                {
                    float cx = patchMinX + patchWorld.x_ * 0.5f;
                    float cz = patchMinZ + patchWorld.y_ * 0.5f;
                    Node* tile = waterNode_->CreateChild("WaterTile");
                    tile->SetPosition(Vector3(cx, 0.0f, cz));
                    tile->SetScale(Vector3(patchWorld.x_, 1.0f, patchWorld.y_));
                    auto* sm = tile->CreateComponent<StaticModel>();
                    sm->SetModel(planeModel);
                    sm->SetMaterial(waterMat);
                    sm->SetViewMask(0x80000000);
                }
            }
        }
        URHO3D_LOGINFOF("Water: %d/%d patches have water tiles", waterNode_->GetNumChildren(), numPatches.x_ * numPatches.y_);
    }

    // Ambient soundscape
    Node* soundNode = scene_->CreateChild("Soundscape");
    soundscape_ = soundNode->CreateComponent<Soundscape>();

    // 3D audio listener — starts on camera (god mode), moves to NPC on possession
    listenerNode_ = cameraNode_->CreateChild("Listener");
    listenerNode_->CreateComponent<SoundListener>();
    GetSubsystem<Audio>()->SetListener(listenerNode_->GetComponent<SoundListener>());

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
        SyncMelbourneTime();
    timeSyncTimer_ = timeOverride ? 9999.0f : 300.0f;

    // Camera — must be created before entities so they can spawn near it
    cameraNode_ = new Node(context_);
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(750.0f);
    cameraNode_->SetPosition(Vector3(64.0f, 60.0f, 64.0f));

    // HUD — vital bars
    Node* hudNode = scene_->CreateChild("HUD");
    auto* hud = hudNode->CreateComponent<HUD>();
    hud->SetFont(font_, 9);

    // Entities are created in SetupSceneBindings() after terrain_/habitat rules are bound
}

// OnSceneLoaded() — DEAD CODE, preserved for future network scene loading reintegration.
// When network play is re-enabled, this will be called after LoadAsyncXML finishes
// to rebind cached pointers and create dynamic entities.
void TerrainNode::OnSceneLoaded()
{
    URHO3D_LOGWARNING("OnSceneLoaded() called — this path is currently unused (procedural scene creation active)");
}

void TerrainNode::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    instructionText_ = ui->GetRoot()->CreateChild<Text>();
    instructionText_->SetText(
        "WASD move, Mouse look, Tab cursor, P possess\n"
        "LMB raise, RMB lower, Scroll brush size\n"
        "F5 debug, F fill, H fog, F11 fullscreen\n"
        "NumPad Enter hide UI, 1 sun, 2 moon");
    instructionText_->SetFont(font_, 15);
    instructionText_->SetTextAlignment(HA_CENTER);
    instructionText_->SetHorizontalAlignment(HA_CENTER);
    instructionText_->SetVerticalAlignment(VA_CENTER);
    instructionText_->SetPosition(0, ui->GetRoot()->GetHeight() / 4);

    // Camera coordinates — lower left
    cameraCoordsText_ = ui->GetRoot()->CreateChild<Text>();
    cameraCoordsText_->SetFont(font_, 13);
    cameraCoordsText_->SetColor(Color(0.8f, 0.8f, 0.6f));
    cameraCoordsText_->SetHorizontalAlignment(HA_LEFT);
    cameraCoordsText_->SetVerticalAlignment(VA_BOTTOM);
    cameraCoordsText_->SetPosition(8, -8);

    // Melbourne clock — lower right
    clockText_ = ui->GetRoot()->CreateChild<Text>();
    clockText_->SetFont(font_, 13);
    clockText_->SetColor(Color(0.9f, 0.9f, 0.8f));
    clockText_->SetHorizontalAlignment(HA_RIGHT);
    clockText_->SetVerticalAlignment(VA_BOTTOM);
    clockText_->SetPosition(-8, -8);

    // Survival HUD bars now handled by HUD component (created in CreateScene)
}

// ============================================================================
// Viewport + Water Reflection
// ============================================================================

void TerrainNode::SetupViewport()
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

    // Water-dependent setup — only when the full world is loaded
    URHO3D_LOGINFOF("[NetDebug] SetupViewport: waterNode_=%p", (void*)waterNode_.Get());
    if (waterNode_)
    {
        rp->Append(cache->GetResource<XMLFile>("PostProcess/Underwater.xml"));
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

        rp->Append(cache->GetResource<XMLFile>("PostProcess/WaterDroplets.xml"));
        rp->SetEnabled("WaterDroplets", false);

        waterPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());
        waterClipPlane_ = Plane(waterNode_->GetWorldRotation() * Vector3(0.0f, 1.0f, 0.0f), waterNode_->GetWorldPosition());

        reflectionCameraNode_ = scene_->CreateChild("ReflectionCamera", LOCAL);
        auto* reflectionCamera = reflectionCameraNode_->CreateComponent<Camera>();
        reflectionCamera->SetFarClip(750.0f);  // Must reach sun/moon billboards for water reflection
        reflectionCamera->SetViewMask(0x7fffffff);
        reflectionCamera->SetAutoAspectRatio(false);
        reflectionCamera->SetUseReflection(true);
        reflectionCamera->SetReflectionPlane(waterPlane_);
        reflectionCamera->SetUseClipping(true);
        reflectionCamera->SetClipPlane(waterClipPlane_);
        reflectionCamera->SetAspectRatio((float)graphics->GetWidth() / (float)graphics->GetHeight());

        // RTT at half resolution — full-res reflection doubles GPU cost for marginal quality
        int rttW = graphics->GetWidth() / 2;
        int rttH = graphics->GetHeight() / 2;
        SharedPtr<Texture2D> renderTexture(new Texture2D(context_));
        renderTexture->SetSize(rttW, rttH, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET);
        renderTexture->SetFilterMode(FILTER_BILINEAR);
        RenderSurface* surface = renderTexture->GetRenderSurface();
        SharedPtr<Viewport> rttViewport(new Viewport(context_, scene_, reflectionCamera));
        rttViewport->SetDrawDebug(false);
        surface->SetViewport(0, rttViewport);
        surface->SetUpdateMode(SURFACE_UPDATEALWAYS);
        auto* waterMat = cache->GetResource<Material>("Materials/Water.xml");
        waterMat->SetTexture(TU_DIFFUSE, renderTexture);
    }

    rp->Append(cache->GetResource<XMLFile>("PostProcess/GodRays.xml"));
    rp->Append(cache->GetResource<XMLFile>("PostProcess/MoonRays.xml"));
    rp->SetEnabled("MoonRays", false);
    renderPath_ = rp;
}

void TerrainNode::SubscribeToEvents()
{
    SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(TerrainNode, HandleBeginFrame));
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(TerrainNode, HandleUpdate));
    SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(TerrainNode, HandlePostRenderUpdate));
    SubscribeToEvent(E_CAMPFIRE_SETTINGS_CHANGED, URHO3D_HANDLER(TerrainNode, HandleCampfireSettingsChanged));
    SubscribeToEvent(E_ANIMATIONTEXTKEY, URHO3D_HANDLER(TerrainNode, HandleAnimationTextKey));
    SubscribeToEvent(E_DRIVENKEY_OUTPUT, URHO3D_HANDLER(TerrainNode, HandleDrivenKeyOutput));

    // AuthServer discovery & connection
    SubscribeToEvent(E_NETWORKHOSTDISCOVERED, URHO3D_HANDLER(TerrainNode, HandleHostDiscovered));
    SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(TerrainNode, HandleServerConnected));
    SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(TerrainNode, HandleServerDisconnected));
    SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(TerrainNode, HandleConnectFailed));
    SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(TerrainNode, HandleAuthMessage));

    // Peer offload (NAT punchthrough)
    SubscribeToEvent(E_PEERCONNECTED, URHO3D_HANDLER(TerrainNode, HandlePeerConnected));
    SubscribeToEvent(E_PEERDISCONNECTED, URHO3D_HANDLER(TerrainNode, HandlePeerDisconnected));
    SubscribeToEvent(E_NETWORKNATPUNCHTROUGHFAILED, URHO3D_HANDLER(TerrainNode, HandleNATPunchFailed));

    // Detect server-replicated creature nodes and attach client-side logic
    // Creature attachment handled by per-frame scan in HandleUpdate

    // Player avatar network events
    SubscribeToEvent(E_PHYSICSPRESTEP, URHO3D_HANDLER(TerrainNode, HandlePhysicsPreStep));
    SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(TerrainNode, HandleClientConnected));
    SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(TerrainNode, HandleClientDisconnected));
    SubscribeToEvent(E_CLIENTOBJECTID, URHO3D_HANDLER(TerrainNode, HandleClientObjectID));
    GetSubsystem<Network>()->RegisterRemoteEvent(E_CLIENTOBJECTID);

    // Fire System Phase 3 — server pit state broadcasts
    SubscribeToEvent(E_PIT_STATE_CHANGED, URHO3D_HANDLER(TerrainNode, HandlePitStateChanged));
    GetSubsystem<Network>()->RegisterRemoteEvent(E_PIT_STATE_CHANGED);

    // Load campfire burn curve (matches server's campfire_burn.json)
    {
        auto* rc = GetSubsystem<ResourceCache>();
        auto* burnJson = rc->GetResource<JSONFile>("DrivenKeys/campfire_burn.json");
        if (burnJson)
        {
            DrivenKeySet burnSet;
            if (burnSet.LoadJSON(burnJson->GetRoot()) && !burnSet.keys.Empty())
                burnCurveKey_ = burnSet.keys[0];
        }
    }

    // Fire System Phase 4a — friction ignition status
    SubscribeToEvent(E_PIT_IGNITION_STATUS, URHO3D_HANDLER(TerrainNode, HandlePitIgnitionStatus));
    GetSubsystem<Network>()->RegisterRemoteEvent(E_PIT_IGNITION_STATUS);
    GetSubsystem<Network>()->RegisterRemoteEvent(E_PIT_IGNITE_REQUEST);

    // Fire System Phase 4c — torch events
    GetSubsystem<Network>()->RegisterRemoteEvent(E_TORCH_LIGHT_REQUEST);
    GetSubsystem<Network>()->RegisterRemoteEvent(E_TORCH_IGNITE_REQUEST);

    // Woodpile server sync
    SubscribeToEvent(E_WOODPILE_STATE, URHO3D_HANDLER(TerrainNode, HandleWoodpileState));
    GetSubsystem<Network>()->RegisterRemoteEvent(E_WOODPILE_STATE);
    GetSubsystem<Network>()->RegisterRemoteEvent(E_WOODPILE_DEPOSIT);

    // Debug log capture
    SubscribeToEvent(E_LOGMESSAGE, URHO3D_HANDLER(TerrainNode, HandleLogMessage));

    // Start LAN discovery immediately
    DiscoverAuthServer();
}

// ============================================================================
// Menu Bar
// ============================================================================

DropDownList* TerrainNode::CreateMenuDropdown(const String& label, const Vector<String>& items)
{
    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

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

void TerrainNode::CreateMenuBar()
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

    // File (merged with former Create menu)
    {
        Vector<String> items;
        items.Push("Save Scene");           // 0
        items.Push("Load Scene");           // 1
        items.Push("Import Model...");      // 2
        items.Push("Export Prefab");        // 3
        items.Push("From Prefab...");       // 4
        items.Push("Clear Object Brush");   // 5
        items.Push("Screenshot");           // 6
        items.Push("Exit");                 // 7
        fileMenu_ = CreateMenuDropdown("File", items);
        SubscribeToEvent(fileMenu_, E_ITEMSELECTED, URHO3D_HANDLER(TerrainNode, HandleFileMenu));
    }

    // Edit (unchanged)
    {
        Vector<String> items;
        items.Push("Undo (Ctrl+Z)");
        items.Push("Redo (Ctrl+Y)");
        items.Push("Translate (T)");
        items.Push("Rotate (R)");
        items.Push("Scale (S)");
        items.Push("World (G)");
        editMenu_ = CreateMenuDropdown("Edit", items);
        SubscribeToEvent(editMenu_, E_ITEMSELECTED, URHO3D_HANDLER(TerrainNode, HandleEditMenu));
    }

    // View (Menu + popup Window with collapsible sections, same pattern as Environment)
    {
        Font* font = font_;
        viewMenu_ = menuBar_->CreateChild<Menu>();
        viewMenu_->SetStyle("DropDownList");
        viewMenu_->SetFixedHeight(24);
        viewMenu_->SetMinWidth(50);

        auto* viewLabel = viewMenu_->CreateChild<Text>();
        viewLabel->SetFont(font, 12);
        viewLabel->SetText("View");
        viewLabel->SetColor(Color::WHITE);

        auto* viewPopup = new Window(context_);
        viewPopup->SetStyleAuto();
        viewPopup->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
        viewPopup->SetMinWidth(280);
        viewPopup->SetDefaultStyle(GetSubsystem<UI>()->GetRoot()->GetDefaultStyle());
        viewPopup->SetOpacity(0.85f);
        viewMenu_->SetPopup(viewPopup);
        viewMenu_->SetPopupOffset(0, viewMenu_->GetHeight());

        CreateMenuItem(viewPopup, "Hierarchy", 200);
        CreateMenuItem(viewPopup, "Inspector", 201);
        CreateMenuItem(viewPopup, "Debug Log", 202);
        CreateMenuItem(viewPopup, "Toggle Fullscreen  (F11)", 203);
        CreateMenuItem(viewPopup, "Toggle Wireframe  (F)", 204);
        CreateMenuItem(viewPopup, "Toggle Debug Geometry  (F5)", 205);
        CreateMenuItem(viewPopup, "Toggle Height Fog  (H)", 206);
        CreateMenuItem(viewPopup, "Toggle Profiler", 207);
        CreateMenuItem(viewPopup, "Toggle OOFO Detector", 208);
        CreateMenuItem(viewPopup, "Toggle God Rays", 209);
        // CreateMenuItem(viewPopup, "Toggle Grass Rays", 210);  // QUARANTINED
        CreateMenuItem(viewPopup, "Toggle Campfire Ray", 211);

        // --- Animal Rays collapsible section ---
        auto* animalRaySection = CreateCollapsibleSection(viewPopup, font, "Animal Rays", false);
        {
            auto makeRayCb = [&](UIElement* parent, const String& label, bool* target, const Color& swatch)
            {
                auto* row = parent->CreateChild<UIElement>();
                row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 1, 4, 1));
                row->SetMinHeight(20);

                auto* cb = row->CreateChild<CheckBox>();
                cb->SetStyleAuto();
                cb->SetChecked(*target);
                cb->SetVar("BoolPtr", (void*)target);
                SubscribeToEvent(cb, E_TOGGLED, [](StringHash, VariantMap& ed) {
                    auto* box = static_cast<CheckBox*>(ed[Toggled::P_ELEMENT].GetPtr());
                    if (box) {
                        bool* ptr = static_cast<bool*>(box->GetVar("BoolPtr").GetVoidPtr());
                        if (ptr) *ptr = box->IsChecked();
                    }
                });

                auto* sw = row->CreateChild<BorderImage>();
                sw->SetFixedSize(12, 12);
                sw->SetColor(swatch);

                auto* lbl = row->CreateChild<Text>();
                lbl->SetFont(font, 11);
                lbl->SetText(label);
                lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
            };

            // Land Animals — ray + visibility toggle per species
            auto* landRays = CreateCollapsibleSection(animalRaySection, font, "Land Animals", false);
            {
                auto makeRayVisCb = [&](UIElement* parent, const String& label, bool* rayTarget, bool* visTarget, const Color& swatch)
                {
                    makeRayCb(parent, label, rayTarget, swatch);
                    // Add visibility checkbox on the same section
                    auto* row = parent->CreateChild<UIElement>();
                    row->SetLayout(LM_HORIZONTAL, 4, IntRect(20, 1, 4, 1));
                    row->SetMinHeight(20);
                    auto* cb = row->CreateChild<CheckBox>();
                    cb->SetStyleAuto();
                    cb->SetChecked(*visTarget);
                    cb->SetVar("BoolPtr", (void*)visTarget);
                    SubscribeToEvent(cb, E_TOGGLED, [](StringHash, VariantMap& ed) {
                        auto* box = static_cast<CheckBox*>(ed[Toggled::P_ELEMENT].GetPtr());
                        if (box) {
                            bool* ptr = static_cast<bool*>(box->GetVar("BoolPtr").GetVoidPtr());
                            if (ptr) *ptr = box->IsChecked();
                        }
                    });
                    auto* lbl = row->CreateChild<Text>();
                    lbl->SetFont(font, 10);
                    lbl->SetText("Visible");
                    lbl->SetColor(Color(0.7f, 0.7f, 0.7f));
                };

                makeRayVisCb(landRays, "Rabbit",     &rabbitRayVisible_,     &rabbitVisible_,     Color(0.2f, 1.0f, 0.2f));
                makeRayVisCb(landRays, "Deer",       &deerRayVisible_,       &deerVisible_,       Color(0.8f, 0.4f, 1.0f));
                makeRayVisCb(landRays, "Fox",        &foxRayVisible_,        &foxVisible_,        Color(1.0f, 0.4f, 0.1f));
                makeRayVisCb(landRays, "Wolf",       &wolfRayVisible_,       &wolfVisible_,       Color(0.6f, 0.0f, 0.0f));
                makeRayVisCb(landRays, "Stag",       &stagRayVisible_,       &stagVisible_,       Color(0.6f, 0.3f, 1.0f));
                makeRayVisCb(landRays, "Bull",       &bullRayVisible_,       &bullVisible_,       Color(0.5f, 0.25f, 0.0f));
                makeRayVisCb(landRays, "Cow",        &cowRayVisible_,        &cowVisible_,        Color(0.9f, 0.9f, 0.7f));
                makeRayVisCb(landRays, "Horse",      &horseRayVisible_,      &horseVisible_,      Color(0.4f, 0.3f, 0.2f));
                makeRayVisCb(landRays, "Donkey",     &donkeyRayVisible_,     &donkeyVisible_,     Color(0.5f, 0.5f, 0.5f));
                makeRayVisCb(landRays, "Alpaca",     &alpacaRayVisible_,     &alpacaVisible_,     Color(1.0f, 0.9f, 0.8f));
                makeRayVisCb(landRays, "Husky",      &huskyRayVisible_,      &huskyVisible_,      Color(0.3f, 0.5f, 0.8f));
                makeRayVisCb(landRays, "Shiba Inu",  &shibaInuRayVisible_,   &shibaInuVisible_,   Color(1.0f, 0.6f, 0.2f));
                makeRayVisCb(landRays, "CaveMan",    &caveManRayVisible_,    &caveManVisible_,    Color(0.0f, 1.0f, 1.0f));
                makeRayVisCb(landRays, "CaveWoman",  &caveWomanRayVisible_,  &caveWomanVisible_,  Color(1.0f, 0.0f, 1.0f));
            }

            // Water Animals
            auto* waterRays = CreateCollapsibleSection(animalRaySection, font, "Water Animals", false);
            {
                makeRayCb(waterRays, "All Water",    &waterAnimalRayVisible_, Color(0.7f, 0.7f, 0.7f));
                makeRayCb(waterRays, "Fish",         &fishRayVisible_,        Color(0.2f, 0.7f, 1.0f));
                makeRayCb(waterRays, "School Fish",  &schoolFishRayVisible_,  Color(0.1f, 1.0f, 0.8f));
            }
        }

        // --- Perf Toggles collapsible section ---
        auto* perfSection = CreateCollapsibleSection(viewPopup, font, "Perf Toggles", false);
        {
            auto makePerfCb = [&](UIElement* parent, const String& label, bool* target)
            {
                auto* row = parent->CreateChild<UIElement>();
                row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 1, 4, 1));
                row->SetMinHeight(20);

                auto* cb = row->CreateChild<CheckBox>();
                cb->SetStyleAuto();
                cb->SetChecked(*target);
                cb->SetVar("BoolPtr", (void*)target);
                SubscribeToEvent(cb, E_TOGGLED, [](StringHash, VariantMap& ed) {
                    auto* box = static_cast<CheckBox*>(ed[Toggled::P_ELEMENT].GetPtr());
                    if (box) {
                        bool* ptr = static_cast<bool*>(box->GetVar("BoolPtr").GetVoidPtr());
                        if (ptr) *ptr = box->IsChecked();
                    }
                });

                auto* lbl = row->CreateChild<Text>();
                lbl->SetFont(font, 11);
                lbl->SetText(label);
                lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
            };

            makePerfCb(perfSection, "Shadows",          &shadowsEnabled_);
            makePerfCb(perfSection, "God Rays",          &godRaysEnabled_);
            makePerfCb(perfSection, "Water Reflection",  &waterReflectionEnabled_);
            makePerfCb(perfSection, "Post-Processing",   &postProcessEnabled_);
        }

        // --- Audio volume sliders ---
        auto* audioSection = CreateCollapsibleSection(viewPopup, font, "Audio", false);
        {
            auto* audio = GetSubsystem<Audio>();
            auto makeVolSlider = [&](UIElement* parent, const String& label, const String& soundType)
            {
                auto* row = parent->CreateChild<UIElement>();
                row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 1, 4, 1));
                row->SetMinHeight(20);

                auto* lbl = row->CreateChild<Text>();
                lbl->SetFont(font, 10);
                lbl->SetText(label);
                lbl->SetColor(Color(0.8f, 0.8f, 0.8f));
                lbl->SetMinWidth(60);

                auto* slider = row->CreateChild<Slider>();
                slider->SetStyleAuto();
                slider->SetRange(1.0f);
                slider->SetValue(audio ? audio->GetMasterGain(soundType) : 1.0f);
                slider->SetMinSize(120, 16);
                slider->SetVar("SoundType", soundType);
                SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleAudioSlider));

                auto* val = row->CreateChild<Text>();
                val->SetFont(font, 10);
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", (int)(slider->GetValue() * 100));
                val->SetText(String(buf));
                val->SetColor(Color(0.6f, 0.8f, 0.6f));
                val->SetName("AudioValueLabel");
                slider->SetVar("ValueLabel", val);
            };

            makeVolSlider(audioSection, "Master",  SOUND_MASTER);
            makeVolSlider(audioSection, "Effects", SOUND_EFFECT);
            makeVolSlider(audioSection, "Ambient", SOUND_AMBIENT);
            makeVolSlider(audioSection, "Music",   SOUND_MUSIC);
        }
    }

    // Settings (font + size selection)
    {
        Vector<String> items;
        // Scan available fonts
        auto* fs = GetSubsystem<FileSystem>();
        String fontDir = fs->GetProgramDir() + "Data/Fonts/";
        Vector<String> fontFiles;
        fs->ScanDir(fontFiles, fontDir, "*.ttf", SCAN_FILES, false);
        availableFonts_.Clear();
        for (unsigned i = 0; i < fontFiles.Size(); ++i)
        {
            String name = fontFiles[i].Substring(0, fontFiles[i].FindLast('.'));
            availableFonts_.Push(name);
            items.Push("Font: " + name);
        }
        int sizes[] = {9, 10, 11, 12, 13, 14, 16, 18};
        for (int i = 0; i < 8; ++i)
            items.Push("Size: " + String(sizes[i]));

        settingsMenu_ = CreateMenuDropdown("Settings", items);
        SubscribeToEvent(settingsMenu_, E_ITEMSELECTED, URHO3D_HANDLER(TerrainNode, HandleSettingsMenu));
    }

    // Environment (custom popup with collapsible sections)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        Font* font = font_;

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
        envPopup->SetLayoutFlexScale(Vector2(1.0f, 0.0f));  // don't stretch height
        environmentMenu_->SetPopup(envPopup);
        environmentMenu_->SetPopupOffset(0, environmentMenu_->GetHeight());

        if (terrain_)
            CreateMenuItem(envPopup, "Terrain Tools", 104);

        // --- Time Scrub section (expanded by default) ---
        auto* timeScrubSection = CreateCollapsibleSection(envPopup, font, "Time Scrub", false);
        {
            // Time of day slider
            auto* todRow = timeScrubSection->CreateChild<UIElement>();
            todRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
            todRow->SetMinHeight(22);

            auto* todText = todRow->CreateChild<Text>();
            todText->SetFont(font, 12);
            todText->SetText("Time:");
            todText->SetColor(Color(0.9f, 0.9f, 0.9f));
            todText->SetMinWidth(55);

            auto* todSlider = todRow->CreateChild<Slider>();
            todSlider->SetStyleAuto();
            todSlider->SetFixedHeight(16);
            todSlider->SetMinWidth(200);
            todSlider->SetRange(24.0f);
            todSlider->SetValue(12.0f);
            SubscribeToEvent(todSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleTimeOfDaySlider));

            todLabel_ = todRow->CreateChild<Text>();
            todLabel_->SetFont(font, 12);
            todLabel_->SetText("+0:00");
            todLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
            todLabel_->SetMinWidth(55);

            // Date offset sliders
            auto createDateSlider = [&](const String& label, float range, Text*& outLabel, int sliderId) {
                auto* row = timeScrubSection->CreateChild<UIElement>();
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
                slider->SetValue(range * 0.5f);
                slider->SetVar("SliderID", sliderId);
                SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleDateOffsetSlider));

                outLabel = row->CreateChild<Text>();
                outLabel->SetFont(font, 12);
                outLabel->SetText("0");
                outLabel->SetColor(Color(0.9f, 0.9f, 0.9f));
                outLabel->SetMinWidth(55);
            };

            createDateSlider("Days:", 30.0f, dayOffsetLabel_, 20);
            createDateSlider("Months:", 12.0f, monthOffsetLabel_, 21);
            createDateSlider("Years:", 30.0f, yearOffsetLabel_, 22);
        }

        // --- Weather section (collapsed by default) ---
        auto* weatherSection = CreateCollapsibleSection(envPopup, font, "Weather", false);
        {
            // Cloud cover slider
            auto* cloudRow = weatherSection->CreateChild<UIElement>();
            cloudRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
            cloudRow->SetMinHeight(22);

            auto* cloudText = cloudRow->CreateChild<Text>();
            cloudText->SetFont(font, 12);
            cloudText->SetText("Clouds:");
            cloudText->SetColor(Color(0.9f, 0.9f, 0.9f));
            cloudText->SetMinWidth(55);

            auto* cloudSlider = cloudRow->CreateChild<Slider>();
            cloudSlider->SetStyleAuto();
            cloudSlider->SetFixedHeight(16);
            cloudSlider->SetMinWidth(200);
            cloudSlider->SetRange(1.0f);
            cloudSlider->SetValue(0.0f);
            cloudSlider->SetVar("SliderID", 30);
            SubscribeToEvent(cloudSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleWeatherSlider));

            cloudCoverLabel_ = cloudRow->CreateChild<Text>();
            cloudCoverLabel_->SetFont(font, 12);
            cloudCoverLabel_->SetText("Auto");
            cloudCoverLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
            cloudCoverLabel_->SetMinWidth(50);

            // Rain slider (drives precipitation + rain particles together)
            auto* rainRow = weatherSection->CreateChild<UIElement>();
            rainRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
            rainRow->SetMinHeight(22);

            auto* rainText = rainRow->CreateChild<Text>();
            rainText->SetFont(font, 12);
            rainText->SetText("Rain:");
            rainText->SetColor(Color(0.9f, 0.9f, 0.9f));
            rainText->SetMinWidth(55);

            auto* rainSlider = rainRow->CreateChild<Slider>();
            rainSlider->SetStyleAuto();
            rainSlider->SetFixedHeight(16);
            rainSlider->SetMinWidth(200);
            rainSlider->SetRange(1.0f);
            rainSlider->SetValue(0.0f);
            rainSlider->SetVar("SliderID", 31);
            SubscribeToEvent(rainSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleWeatherSlider));

            rainLabel_ = rainRow->CreateChild<Text>();
            rainLabel_->SetFont(font, 12);
            rainLabel_->SetText("Auto");
            rainLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
            rainLabel_->SetMinWidth(50);

            // Hemisphere lighting toggle
            auto* hemiRow = weatherSection->CreateChild<UIElement>();
            hemiRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
            hemiRow->SetMinHeight(22);

            auto* hemiCb = hemiRow->CreateChild<CheckBox>();
            hemiCb->SetStyleAuto();
            hemiCb->SetChecked(hemisphereEnabled_);
            SubscribeToEvent(hemiCb, E_TOGGLED, URHO3D_HANDLER(TerrainNode, HandleHemisphereToggle));

            auto* hemiLabel = hemiRow->CreateChild<Text>();
            hemiLabel->SetFont(font, 12);
            hemiLabel->SetText("Hemisphere Lighting");
            hemiLabel->SetColor(Color(0.9f, 0.9f, 0.9f));
        }

        // --- Animals section (collapsed by default) ---
        auto* animalsSection = CreateCollapsibleSection(envPopup, font, "Animals", false);
        {
            // --- Fish sub-section ---
            auto* fishSection = CreateCollapsibleSection(animalsSection, font, "Fish", false);
            {
                // Fish type breakdown
                struct FishRow { const char* name; int count; };
                FishRow fishTypes[] = {
                    {"Fish", 50},
                    {"School Fish", 45}   // 3 schools x 15
                };
                for (const auto& f : fishTypes)
                {
                    auto* row = fishSection->CreateChild<UIElement>();
                    row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                    row->SetMinHeight(20);

                    auto* nameText = row->CreateChild<Text>();
                    nameText->SetFont(font, 11);
                    nameText->SetText(f.name);
                    nameText->SetColor(Color(0.9f, 0.9f, 0.9f));
                    nameText->SetMinWidth(80);

                    auto* countText = row->CreateChild<Text>();
                    countText->SetFont(font, 11);
                    countText->SetText(String("x") + String(f.count));
                    countText->SetColor(Color(0.7f, 0.9f, 0.7f));
                }

                // Wiggle amplitude
                auto* fishAmpRow = fishSection->CreateChild<UIElement>();
                fishAmpRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                fishAmpRow->SetMinHeight(22);

                auto* fishAmpText = fishAmpRow->CreateChild<Text>();
                fishAmpText->SetFont(font, 12);
                fishAmpText->SetText("Wiggle:");
                fishAmpText->SetColor(Color(0.9f, 0.9f, 0.9f));
                fishAmpText->SetMinWidth(55);

                auto* fishAmpSlider = fishAmpRow->CreateChild<Slider>();
                fishAmpSlider->SetStyleAuto();
                fishAmpSlider->SetFixedHeight(16);
                fishAmpSlider->SetMinWidth(200);
                fishAmpSlider->SetRange(1.0f);
                fishAmpSlider->SetValue(0.3f);
                SubscribeToEvent(fishAmpSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleFishWiggleSlider));

                fishWiggleLabel_ = fishAmpRow->CreateChild<Text>();
                fishWiggleLabel_->SetFont(font, 12);
                fishWiggleLabel_->SetText("0.030");
                fishWiggleLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
                fishWiggleLabel_->SetMinWidth(50);

                // Speed
                auto* fishSpdRow = fishSection->CreateChild<UIElement>();
                fishSpdRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                fishSpdRow->SetMinHeight(22);

                auto* fishSpdText = fishSpdRow->CreateChild<Text>();
                fishSpdText->SetFont(font, 12);
                fishSpdText->SetText("Speed:");
                fishSpdText->SetColor(Color(0.9f, 0.9f, 0.9f));
                fishSpdText->SetMinWidth(55);

                auto* fishSpdSlider = fishSpdRow->CreateChild<Slider>();
                fishSpdSlider->SetStyleAuto();
                fishSpdSlider->SetFixedHeight(16);
                fishSpdSlider->SetMinWidth(200);
                fishSpdSlider->SetRange(1.0f);
                fishSpdSlider->SetValue(0.25f);
                SubscribeToEvent(fishSpdSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleFishSpeedSlider));

                fishSpeedLabel_ = fishSpdRow->CreateChild<Text>();
                fishSpeedLabel_->SetFont(font, 12);
                fishSpeedLabel_->SetText("2.0 Hz");
                fishSpeedLabel_->SetColor(Color(0.9f, 0.9f, 0.9f));
                fishSpeedLabel_->SetMinWidth(50);
            }

            // --- Land sub-section (data-driven from habitat_rules.json) ---
            auto* landSection = CreateCollapsibleSection(animalsSection, font, "Land", false);
            {
                // Land species names (Fish/SchoolFish excluded)
                const char* landSpecies[] = {
                    "Rabbit", "Deer", "Fox", "Wolf", "Stag", "Bull",
                    "Cow", "Horse", "Donkey", "Alpaca", "Husky", "ShibaInu"
                };

                for (const char* speciesName : landSpecies)
                {
                    const HabitatRule* rule = habitatRules_.GetRule(speciesName);
                    int density = rule ? rule->density : 0;
                    float altMin = rule ? rule->altitudeMin : 5.5f;
                    float altMax = rule ? rule->altitudeMax : 100.0f;

                    // Collapsible per-species section
                    String title = String(speciesName) + " (x" + String(density) + ")";
                    auto* speciesSection = CreateCollapsibleSection(landSection, font, title, false);

                    // Density slider
                    {
                        auto* row = speciesSection->CreateChild<UIElement>();
                        row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                        row->SetMinHeight(22);

                        auto* lbl = row->CreateChild<Text>();
                        lbl->SetFont(font, 11);
                        lbl->SetText("Density:");
                        lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
                        lbl->SetMinWidth(65);

                        auto* slider = row->CreateChild<Slider>();
                        slider->SetStyleAuto();
                        slider->SetFixedHeight(16);
                        slider->SetMinWidth(140);
                        slider->SetRange(20.0f);
                        slider->SetValue((float)density);
                        slider->SetVar("HabitatSpecies", String(speciesName));
                        slider->SetVar("HabitatParam", String("density"));
                        SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleHabitatSlider));

                        auto* valText = row->CreateChild<Text>();
                        valText->SetFont(font, 11);
                        valText->SetText(String(density));
                        valText->SetColor(Color(0.7f, 0.9f, 0.7f));
                        valText->SetMinWidth(30);
                        valText->SetName(String(speciesName) + "_density_val");
                    }

                    // Altitude min/max row
                    {
                        auto* row = speciesSection->CreateChild<UIElement>();
                        row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                        row->SetMinHeight(22);

                        auto* lbl = row->CreateChild<Text>();
                        lbl->SetFont(font, 11);
                        lbl->SetText("Alt:");
                        lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
                        lbl->SetMinWidth(35);

                        auto* minSlider = row->CreateChild<Slider>();
                        minSlider->SetStyleAuto();
                        minSlider->SetFixedHeight(16);
                        minSlider->SetMinWidth(70);
                        minSlider->SetRange(100.0f);
                        minSlider->SetValue(altMin);
                        minSlider->SetVar("HabitatSpecies", String(speciesName));
                        minSlider->SetVar("HabitatParam", String("altMin"));
                        SubscribeToEvent(minSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleHabitatSlider));

                        auto* dash = row->CreateChild<Text>();
                        dash->SetFont(font, 11);
                        dash->SetText("-");
                        dash->SetColor(Color(0.7f, 0.7f, 0.7f));

                        auto* maxSlider = row->CreateChild<Slider>();
                        maxSlider->SetStyleAuto();
                        maxSlider->SetFixedHeight(16);
                        maxSlider->SetMinWidth(70);
                        maxSlider->SetRange(100.0f);
                        maxSlider->SetValue(Min(altMax, 100.0f));
                        maxSlider->SetVar("HabitatSpecies", String(speciesName));
                        maxSlider->SetVar("HabitatParam", String("altMax"));
                        SubscribeToEvent(maxSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleHabitatSlider));

                        auto* valText = row->CreateChild<Text>();
                        valText->SetFont(font, 11);
                        valText->SetText(String((int)altMin) + "-" + String((int)altMax));
                        valText->SetColor(Color(0.7f, 0.9f, 0.7f));
                        valText->SetMinWidth(50);
                        valText->SetName(String(speciesName) + "_alt_val");
                    }

                    // Respawn button for this species
                    {
                        auto* btn = speciesSection->CreateChild<Button>();
                        btn->SetStyleAuto();
                        btn->SetFixedHeight(20);
                        btn->SetMinWidth(80);
                        btn->SetVar("HabitatAction", String("respawn"));
                        btn->SetVar("HabitatSpecies", String(speciesName));
                        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleHabitatButton));

                        auto* btnText = btn->CreateChild<Text>();
                        btnText->SetFont(font, 11);
                        btnText->SetText("Respawn");
                        btnText->SetAlignment(HA_CENTER, VA_CENTER);
                    }
                }

                // --- Global buttons at bottom of Land section ---
                auto* btnRow = landSection->CreateChild<UIElement>();
                btnRow->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 4, 4, 4));
                btnRow->SetMinHeight(26);

                // Save Rules
                {
                    auto* btn = btnRow->CreateChild<Button>();
                    btn->SetStyleAuto();
                    btn->SetFixedHeight(22);
                    btn->SetMinWidth(60);
                    btn->SetVar("HabitatAction", String("save"));
                    SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleHabitatButton));
                    auto* t = btn->CreateChild<Text>();
                    t->SetFont(font, 11);
                    t->SetText("Save");
                    t->SetAlignment(HA_CENTER, VA_CENTER);
                }
                // Reset Rules
                {
                    auto* btn = btnRow->CreateChild<Button>();
                    btn->SetStyleAuto();
                    btn->SetFixedHeight(22);
                    btn->SetMinWidth(60);
                    btn->SetVar("HabitatAction", String("reset"));
                    SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleHabitatButton));
                    auto* t = btn->CreateChild<Text>();
                    t->SetFont(font, 11);
                    t->SetText("Reset");
                    t->SetAlignment(HA_CENTER, VA_CENTER);
                }
                // Respawn All
                {
                    auto* btn = btnRow->CreateChild<Button>();
                    btn->SetStyleAuto();
                    btn->SetFixedHeight(22);
                    btn->SetMinWidth(80);
                    btn->SetVar("HabitatAction", String("respawnAll"));
                    SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleHabitatButton));
                    auto* t = btn->CreateChild<Text>();
                    t->SetFont(font, 11);
                    t->SetText("Respawn All");
                    t->SetAlignment(HA_CENTER, VA_CENTER);
                }
            }

        }

        // --- Water section (collapsed by default) ---
        auto* waterSection = CreateCollapsibleSection(envPopup, font, "Water", false);
        {
            // Helper lambda: one slider row
            auto makeRow = [&](UIElement* parent, const String& label, float range, float defaultVal,
                               int sliderId, Text*& outLabel) -> Slider*
            {
                auto* row = parent->CreateChild<UIElement>();
                row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                row->SetMinHeight(22);

                auto* lbl = row->CreateChild<Text>();
                lbl->SetFont(font, 12);
                lbl->SetText(label);
                lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
                lbl->SetMinWidth(80);

                auto* slider = row->CreateChild<Slider>();
                slider->SetStyleAuto();
                slider->SetFixedHeight(16);
                slider->SetMinWidth(180);
                slider->SetRange(range);
                slider->SetValue(defaultVal);
                slider->SetVar("SliderID", sliderId);
                SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleWaterSlider));

                outLabel = row->CreateChild<Text>();
                outLabel->SetFont(font, 12);
                outLabel->SetColor(Color(0.9f, 0.9f, 0.9f));
                outLabel->SetMinWidth(55);

                return slider;
            };

            // Water Height    (ID 45)  0 → 50, default 5 (Y position of the water plane)
            makeRow(waterSection, "Height:", 50.0f, 5.0f, 45, waterHeightLabel_)->SetValue(5.0f);
            waterHeightLabel_->SetText("5.0");

            // Noise Strength  (ID 40)  0 → 0.08, default 0.02
            makeRow(waterSection, "Noise:", 0.08f, 0.02f, 40, waterNoiseLabel_)->SetValue(0.02f);
            waterNoiseLabel_->SetText("0.020");

            // Fresnel Power   (ID 41)  1 → 8, default 3
            makeRow(waterSection, "Fresnel:", 7.0f, 2.0f, 41, waterFresnelLabel_)->SetValue(2.0f);
            waterFresnelLabel_->SetText("3.0");

            // Depth Scale     (ID 42)  0 → 5, default 1.5
            makeRow(waterSection, "Depth:", 5.0f, 1.5f, 42, waterDepthLabel_)->SetValue(1.5f);
            waterDepthLabel_->SetText("1.50");

            // Ripple Strength (ID 43)  0 → 1.5, default 0.4
            makeRow(waterSection, "Ripple:", 1.5f, 0.4f, 43, waterRippleLabel_)->SetValue(0.4f);
            waterRippleLabel_->SetText("0.40");

            // Ripple Decay    (ID 44)  0 (slow) → 1 (fast), maps to baseDamping 0.995 → 0.900
            makeRow(waterSection, "Decay:", 1.0f, 0.43f, 44, waterDecayLabel_)->SetValue(0.43f);
            waterDecayLabel_->SetText("0.97");
        }

        // --- Campfire Settings section ---
        auto* campfireSection = CreateCollapsibleSection(envPopup, font, "Campfire", false);
        {
            auto makeRow = [&](UIElement* parent, const String& label, float range, float defaultVal,
                               int sliderId, Text*& outLabel) -> Slider*
            {
                auto* row = parent->CreateChild<UIElement>();
                row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
                row->SetMinHeight(22);

                auto* lbl = row->CreateChild<Text>();
                lbl->SetFont(font, 12);
                lbl->SetText(label);
                lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
                lbl->SetMinWidth(80);

                auto* slider = row->CreateChild<Slider>();
                slider->SetStyleAuto();
                slider->SetFixedHeight(16);
                slider->SetMinWidth(180);
                slider->SetRange(range);
                slider->SetValue(defaultVal);
                slider->SetVar("SliderID", sliderId);
                SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleCampfireSlider));
                campfireSliders_[sliderId] = slider;

                outLabel = row->CreateChild<Text>();
                outLabel->SetFont(font, 12);
                outLabel->SetColor(Color(0.9f, 0.9f, 0.9f));
                outLabel->SetMinWidth(55);

                return slider;
            };

            // Fire emission rate  (ID 50)  0 → 100, default 45
            makeRow(campfireSection, "Fire Rate:", 100.0f, 45.0f, 50, cfFireRateLabel_);
            cfFireRateLabel_->SetText("45");

            // Fire particle size  (ID 51)  0.01 → 1.0, default 0.25
            makeRow(campfireSection, "Fire Size:", 1.0f, 0.25f, 51, cfFireSizeLabel_);
            cfFireSizeLabel_->SetText("0.25");

            // Smoke emission rate (ID 52)  0 → 200, default 150
            makeRow(campfireSection, "Smoke Rate:", 200.0f, 150.0f, 52, cfSmokeRateLabel_);
            cfSmokeRateLabel_->SetText("150");

            // Smoke particle size (ID 53)  0.01 → 1.5, default 0.4
            makeRow(campfireSection, "Smoke Size:", 1.5f, 0.4f, 53, cfSmokeSizeLabel_);
            cfSmokeSizeLabel_->SetText("0.40");

            // Smoke emitter size  (ID 75)  0 → 2, default 1.0 (box radius)
            makeRow(campfireSection, "Smoke Emit R:", 2.0f, 1.0f, 75, cfSmokeEmitSizeLabel_);
            cfSmokeEmitSizeLabel_->SetText("1.00");

            // Smoke size multiplier (ID 76) 0.1 → 3, default 1.3 (grows over lifetime)
            makeRow(campfireSection, "Smoke Grow:", 3.0f, 1.3f, 76, cfSmokeGrowLabel_);
            cfSmokeGrowLabel_->SetText("1.30");

            // Smoke lifetime      (ID 77)  0.5 → 8, default 4
            makeRow(campfireSection, "Smoke Life:", 8.0f, 4.0f, 77, cfSmokeLifeLabel_);
            cfSmokeLifeLabel_->SetText("4.0");

            // Smoke updraft       (ID 78)  0 → 10, default 2
            makeRow(campfireSection, "Smoke Rise:", 10.0f, 2.0f, 78, cfSmokeRiseLabel_);
            cfSmokeRiseLabel_->SetText("2.0");

            // Smoke damping       (ID 79)  0 → 5, default 2
            makeRow(campfireSection, "Smoke Damp:", 5.0f, 2.0f, 79, cfSmokeDampLabel_);
            cfSmokeDampLabel_->SetText("2.0");

            // Light range         (ID 54)  0.5 → 20, default 5
            makeRow(campfireSection, "Light Range:", 20.0f, 5.0f, 54, cfLightRangeLabel_);
            cfLightRangeLabel_->SetText("5.0");

            // Light brightness    (ID 55)  0.1 → 5, default 1.5
            makeRow(campfireSection, "Brightness:", 5.0f, 1.5f, 55, cfBrightnessLabel_);
            cfBrightnessLabel_->SetText("1.50");

            // Fire velocity       (ID 56)  0 → 5, default 1.5
            makeRow(campfireSection, "Velocity:", 5.0f, 1.5f, 56, cfVelocityLabel_);
            cfVelocityLabel_->SetText("1.5");

            // Fire upward force   (ID 57)  0 → 10, default 2
            makeRow(campfireSection, "Updraft:", 10.0f, 2.0f, 57, cfUpdraftLabel_);
            cfUpdraftLabel_->SetText("2.0");

            // Fire time to live   (ID 58)  0.1 → 4, default 1
            makeRow(campfireSection, "Lifetime:", 4.0f, 1.0f, 58, cfLifetimeLabel_);
            cfLifetimeLabel_->SetText("1.0");

            // --- Fire color frames: Birth / Mid / Death ---
            // Each frame has R, G, B, A sliders (IDs 60-71)
            // Birth color: default (0.1, 0.5, 1.0, 1.0) — the blue you see
            auto makeColorRow = [&](UIElement* parent, const String& label, int baseId,
                                    float defR, float defG, float defB, float defA)
            {
                auto* header = parent->CreateChild<Text>();
                header->SetFont(font, 11);
                header->SetText(label);
                header->SetColor(Color(0.8f, 0.8f, 0.5f));
                header->SetFixedHeight(18);

                Text* dummy;
                makeRow(parent, "  R:", 1.0f, defR, baseId,     dummy);
                makeRow(parent, "  G:", 1.0f, defG, baseId + 1, dummy);
                makeRow(parent, "  B:", 1.0f, defB, baseId + 2, dummy);
                makeRow(parent, "  A:", 1.0f, defA, baseId + 3, dummy);
            };

            // Birth (t=0)    IDs 60-63
            makeColorRow(campfireSection, "Birth Color:", 60, 0.1f, 0.5f, 1.0f, 1.0f);
            // Mid   (t=0.5)  IDs 64-67
            makeColorRow(campfireSection, "Mid Color:", 64, 1.0f, 0.63f, 0.45f, 1.0f);
            // Death (t=1.0)  IDs 68-71
            makeColorRow(campfireSection, "Death Color:", 68, 0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    // AI Tuning button (admin-only, hidden until login)
    {
        auto* btn = menuBar_->CreateChild<Button>("AITuningBtn");
        btn->SetStyleAuto();
        btn->SetFixedHeight(24);
        btn->SetMinWidth(80);
        btn->SetVisible(false); // shown after admin login
        auto* lbl = btn->CreateChild<Text>();
        lbl->SetFont(font_, 12);
        lbl->SetText("AI Tuning");
        lbl->SetColor(Color::WHITE);
        lbl->SetAlignment(HA_CENTER, VA_CENTER);
        SubscribeToEvent(btn, E_RELEASED, [this](StringHash, VariantMap&) {
            if (!tuningPanel_)
                CreateTuningPanel();
            tuningPanel_->SetVisible(!tuningPanel_->IsVisible());
            if (tuningPanel_->IsVisible())
                RequestTuningData();
        });
    }
}

void TerrainNode::RequestTuningData()
{
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network->GetServerConnection();
    if (!serverConn) return;
    VectorBuffer buf;
    serverConn->SendMessage(MSG_TUNING_REQUEST, true, true, buf);
}

void TerrainNode::CreateTuningPanel()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    tuningPanel_ = new Window(context_);
    uiRoot->AddChild(tuningPanel_);
    tuningPanel_->SetStyleAuto();
    tuningPanel_->SetLayout(LM_VERTICAL, 4, IntRect(6, 6, 6, 6));
    tuningPanel_->SetMinSize(360, 200);
    tuningPanel_->SetMaxSize(400, 600);
    tuningPanel_->SetPosition(100, 60);
    tuningPanel_->SetMovable(true);
    tuningPanel_->SetResizable(true);
    tuningPanel_->SetOpacity(0.9f);

    auto* title = tuningPanel_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("AI Tuning");
    title->SetColor(Color(1.0f, 0.9f, 0.6f));

    // Content area — populated when data arrives
    auto* content = tuningPanel_->CreateChild<UIElement>("TuningContent");
    content->SetLayout(LM_VERTICAL, 2, IntRect(0, 0, 0, 0));

    // Reset Defaults button
    auto* resetBtn = tuningPanel_->CreateChild<Button>();
    resetBtn->SetStyleAuto();
    resetBtn->SetFixedHeight(24);
    resetBtn->SetMinWidth(120);
    auto* resetLbl = resetBtn->CreateChild<Text>();
    resetLbl->SetFont(font_, 11);
    resetLbl->SetText("Reset Defaults");
    resetLbl->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(resetBtn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleTuningResetDefaults));
}

void TerrainNode::PopulateTuningPanel()
{
    if (!tuningPanel_) return;
    auto* content = tuningPanel_->GetChild("TuningContent", false);
    if (!content) return;
    content->RemoveAllChildren();

    // Group by category
    Vector<String> categories;
    for (const auto& e : tuningEntries_)
    {
        if (!categories.Contains(e.category))
            categories.Push(e.category);
    }

    for (const auto& cat : categories)
    {
        // Category header
        auto* catLabel = content->CreateChild<Text>();
        catLabel->SetFont(font_, 12);
        catLabel->SetText(cat.ToUpper());
        catLabel->SetColor(Color(0.6f, 0.8f, 1.0f));

        for (unsigned i = 0; i < tuningEntries_.Size(); ++i)
        {
            const auto& e = tuningEntries_[i];
            if (e.category != cat) continue;

            auto* row = content->CreateChild<UIElement>();
            row->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 1, 4, 1));
            row->SetMinHeight(20);

            auto* lbl = row->CreateChild<Text>();
            lbl->SetFont(font_, 10);
            lbl->SetText(e.label);
            lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
            lbl->SetMinWidth(160);

            auto* slider = row->CreateChild<Slider>();
            slider->SetStyleAuto();
            slider->SetFixedHeight(14);
            slider->SetMinWidth(120);
            slider->SetRange(e.maxVal - e.minVal);
            slider->SetValue(e.value - e.minVal);
            slider->SetVar("TuningIndex", i);
            SubscribeToEvent(slider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleTuningSliderChanged));

            auto* valText = row->CreateChild<Text>();
            valText->SetFont(font_, 10);
            valText->SetColor(Color(0.9f, 0.9f, 0.9f));
            valText->SetMinWidth(50);
            char buf[16]; snprintf(buf, sizeof(buf), "%.2f", e.value);
            valText->SetText(buf);
        }
    }
}

void TerrainNode::HandleTuningSliderChanged(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    if (!slider) return;

    int idx = slider->GetVar("TuningIndex").GetI32();
    if (idx < 0 || idx >= (int)tuningEntries_.Size()) return;

    auto& e = tuningEntries_[idx];
    float newValue = slider->GetValue() + e.minVal;
    e.value = newValue;

    // Update value text (next sibling)
    auto* parent = slider->GetParent();
    if (parent && parent->GetNumChildren() >= 3)
    {
        auto* valText = parent->GetChildStaticCast<Text>(2);
        if (valText)
        {
            char buf[16]; snprintf(buf, sizeof(buf), "%.2f", newValue);
            valText->SetText(buf);
        }
    }

    // Send update to server
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network->GetServerConnection();
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteString(e.key);
        buf.WriteFloat(newValue);
        serverConn->SendMessage(MSG_TUNING_UPDATE, true, true, buf);
    }
}

void TerrainNode::HandleTuningResetDefaults(StringHash eventType, VariantMap& eventData)
{
    // Re-request fresh data from server (server will have original DB values
    // after next restart; for live reset, Phase 5 could add a reset command)
    RequestTuningData();
}

static const char* DEFAULT_INSTRUCTIONS =
    "WASD move, Mouse look, Tab cursor, P possess\n"
    "LMB raise, RMB lower, Scroll brush size\n"
    "F5 debug, F fill, H fog, F11 fullscreen\n"
    "NumPad Enter hide UI, 1 sun, 2 moon";

void TerrainNode::HandleFileMenu(StringHash eventType, VariantMap& eventData)
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
    case 3: ShowExportPrefabDialog(); break;
    case 4: ShowLoadPrefabDialog(); break;
    case 5:
        if (prefabBrush_)
        {
            prefabBrush_->Remove();
            prefabBrush_.Reset();
        }
        UpdatePrefabBrushLabel();
        break;
    case 6:
    {
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
    case 7: engine_->Exit(); break;
    }

    fileMenu_->SetSelection(M_MAX_UNSIGNED);
}

void TerrainNode::HandleEditMenu(StringHash eventType, VariantMap& eventData)
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


Menu* TerrainNode::CreateMenuItem(UIElement* parent, const String& text, int actionId)
{
    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

    auto* item = parent->CreateChild<Menu>();
    item->SetStyleAuto();
    item->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));

    auto* label = item->CreateChild<Text>();
    label->SetFont(font, 12);
    label->SetText(text);
    label->SetColor(Color(0.9f, 0.9f, 0.9f));

    item->SetMinHeight(22);

    item->SetVar("MenuAction", actionId);
    SubscribeToEvent(item, E_MENUSELECTED, URHO3D_HANDLER(TerrainNode, HandleEnvironmentAction));
    return item;
}

UIElement* TerrainNode::CreateCollapsibleSection(UIElement* parent, Font* font, const String& title, bool expanded)
{
    // Header button — toggle arrow prefix
    auto* header = parent->CreateChild<Button>();
    header->SetStyle("Button");
    header->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 2, 4, 2));
    header->SetMinHeight(22);
    header->SetMinWidth(300);

    auto* headerText = header->CreateChild<Text>();
    headerText->SetFont(font, 12);
    headerText->SetText(String(expanded ? "v " : "> ") + title);
    headerText->SetColor(Color(0.95f, 0.95f, 0.8f));

    // Content container
    auto* content = parent->CreateChild<UIElement>();
    content->SetLayout(LM_VERTICAL, 2, IntRect(8, 2, 4, 2));
    content->SetVisible(expanded);

    // Store content element pointer as a var on the header button
    header->SetVar("SectionContent", content);
    header->SetVar("SectionTitle", title);
    SubscribeToEvent(header, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleCollapsibleToggle));

    return content;
}

void TerrainNode::HandleCollapsibleToggle(StringHash eventType, VariantMap& eventData)
{
    using namespace Released;
    auto* button = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!button) return;

    auto* content = static_cast<UIElement*>(button->GetVar("SectionContent").GetPtr());
    if (!content) return;

    bool nowVisible = !content->IsVisible();
    content->SetVisible(nowVisible);

    // Update header text arrow
    String title = button->GetVar("SectionTitle").GetString();
    auto* headerText = button->GetChildStaticCast<Text>(0);
    if (headerText)
        headerText->SetText(String(nowVisible ? "v " : "> ") + title);

    // Shrink-to-fit: reset parent window height so layout recalculates
    auto* popup = button->GetParent();
    if (popup)
    {
        popup->SetHeight(0);
        popup->UpdateLayout();
    }
}

void TerrainNode::HandleEnvironmentAction(StringHash eventType, VariantMap& eventData)
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

    // View menu actions (200-series)
    case 200: ToggleHierarchyWindow(); break;
    case 201: ToggleInspectorWindow(); break;
    case 202: ToggleDebugLogWindow(); break;
    case 203: GetSubsystem<Graphics>()->ToggleFullscreen(); break;
    case 204:
    {
        auto* camera = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
        if (camera)
            camera->SetFillMode(camera->GetFillMode() == FILL_SOLID ? FILL_WIREFRAME : FILL_SOLID);
        break;
    }
    case 205: drawDebug_ = !drawDebug_; break;
    case 206:
        if (zone_)
        {
            bool on = !zone_->GetHeightFog();
            zone_->SetHeightFog(on);
            heightFogOverride_ = on ? 1 : -1;
        }
        break;
    case 207:
        if (profilerUI_)
            profilerUI_->SetVisible(!profilerUI_->IsVisible());
        break;
    case 208: oofoRayVisible_ = !oofoRayVisible_; break;
    case 209:
    {
        godRaysEnabled_ = !godRaysEnabled_;
        auto* gfx = GetSubsystem<Graphics>();
        ProfilerTimeline* tl = gfx ? gfx->GetProfilerTimeline() : nullptr;
        if (tl) tl->AddMarker(godRaysEnabled_ ? "GodRays ON" : "GodRays OFF");
        break;
    }
    // case 210:  // QUARANTINED — grass not rendering
    case 211:
    {
        campfireRayVisible_ = !campfireRayVisible_;
        URHO3D_LOGINFOF("Campfire ray: %s", campfireRayVisible_ ? "ON" : "OFF");
        auto* gfx = GetSubsystem<Graphics>();
        ProfilerTimeline* tl = gfx ? gfx->GetProfilerTimeline() : nullptr;
        if (tl) tl->AddMarker(campfireRayVisible_ ? "CampfireRay ON" : "CampfireRay OFF");
        break;
    }
    //     grassRayVisible_ = !grassRayVisible_;
    //     URHO3D_LOGINFOF("Grass rays: %s", grassRayVisible_ ? "ON" : "OFF");
    //     break;
    }

    // Close whichever popup is showing
    if (environmentMenu_ && environmentMenu_->GetShowPopup())
        environmentMenu_->ShowPopup(false);
    if (viewMenu_ && viewMenu_->GetShowPopup())
        viewMenu_->ShowPopup(false);
}

void TerrainNode::HandleTimeOfDaySlider(StringHash eventType, VariantMap& eventData)
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

    // Send adjusted UTC epoch to server — privileged clients only
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (serverConn && loggedIn_)
    {
        // Compute total scrub delta in seconds from all sliders
        float hourDelta = timeOfDayOffset_ * 3600.0f;
        float dayDelta = (daySliderOffset_ + monthSliderOffset_ + yearSliderOffset_) * 86400.0f;
        long long scrubDeltaSeconds = (long long)(hourDelta + dayDelta);

        // Client's real UTC + scrub = the new epoch basis for the server
        long long adjustedEpoch = (long long)time(nullptr) + scrubDeltaSeconds;

        VectorBuffer buf;
        buf.WriteFloat(0.0f);   // legacy hour (ignored when epoch present)
        buf.WriteI32(0);        // legacy doy (ignored when epoch present)
        buf.WriteI64((long long)adjustedEpoch);
        serverConn->SendMessage(MSG_ADMIN_TIME_OVERRIDE, true, true, buf);
    }
}

void TerrainNode::HandleDateOffsetSlider(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::ApplyDateOffsets()
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

    // Send adjusted UTC epoch to server so NPC behavior syncs to god's chosen time
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (serverConn && loggedIn_)
    {
        float hourDelta = timeOfDayOffset_ * 3600.0f;
        float dayDelta = (daySliderOffset_ + monthSliderOffset_ + yearSliderOffset_) * 86400.0f;
        long long scrubDeltaSeconds = (long long)(hourDelta + dayDelta);
        long long adjustedEpoch = (long long)time(nullptr) + scrubDeltaSeconds;

        VectorBuffer buf;
        buf.WriteFloat(0.0f);
        buf.WriteI32(0);
        buf.WriteI64((long long)adjustedEpoch);
        serverConn->SendMessage(MSG_ADMIN_TIME_OVERRIDE, true, true, buf);
    }
}

void TerrainNode::HandleFishWiggleSlider(StringHash eventType, VariantMap& eventData)
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
        fishWiggleLabel_->SetText(String((double)amplitude, 3));
}

void TerrainNode::HandleFishSpeedSlider(StringHash eventType, VariantMap& eventData)
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
        fishSpeedLabel_->SetText(String((double)freq, 1) + " Hz");
}

void TerrainNode::HandleWaterSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    if (!slider) return;

    float val = eventData[P_VALUE].GetFloat();
    int id = slider->GetVar("SliderID").GetI32();

    // Get water material via ResourceCache (shared across all water tiles)
    Material* mat = nullptr;
    if (waterNode_)
        mat = GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/Water.xml");

    switch (id)
    {
    case 45: // Water Height  0 → 50
    {
        if (waterNode_)
        {
            Vector3 pos = waterNode_->GetPosition();
            pos.y_ = val;
            waterNode_->SetPosition(pos);
            // Update reflection + clip planes
            waterPlane_ = Plane(Vector3::UP, pos);
            waterClipPlane_ = Plane(Vector3::UP, pos);
            if (reflectionCameraNode_)
            {
                auto* reflCam = reflectionCameraNode_->GetComponent<Camera>();
                if (reflCam)
                {
                    reflCam->SetReflectionPlane(waterPlane_);
                    reflCam->SetClipPlane(waterClipPlane_);
                }
            }
            // Update render path WaterLevel
            if (renderPath_)
                renderPath_->SetShaderParameter("WaterLevel", val);
        }
        if (waterHeightLabel_) waterHeightLabel_->SetText(String((double)val, 1));
        break;
    }

    case 40: // Noise Strength  0 → 0.08
        if (mat) mat->SetShaderParameter("NoiseStrength", val);
        if (waterNoiseLabel_) waterNoiseLabel_->SetText(String((double)val, 3));
        break;

    case 41: // Fresnel Power  slider 0→7 → value 1→8
    {
        float fp = val + 1.0f;
        if (mat) mat->SetShaderParameter("FresnelPower", fp);
        if (waterFresnelLabel_) waterFresnelLabel_->SetText(String((double)fp, 1));
        break;
    }

    case 42: // Depth Scale  0 → 5
        if (mat) mat->SetShaderParameter("DepthScale", val);
        if (waterDepthLabel_) waterDepthLabel_->SetText(String((double)val, 2));
        break;

    case 43: // Ripple Strength  0 → 1.5
        if (mat) mat->SetShaderParameter("RippleStrength", val);
        if (waterRippleLabel_) waterRippleLabel_->SetText(String((double)val, 2));
        break;

    case 44: // Ripple Decay  slider 0→1 maps to baseDamping 0.995→0.900
    {
        float damping = 0.995f - val * 0.095f;
        if (rippleSystem_) rippleSystem_->SetBaseDamping(damping);
        if (waterDecayLabel_) waterDecayLabel_->SetText(String((double)damping, 3));
        break;
    }

    default: break;
    }
}

void TerrainNode::HandleCampfireSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    if (!slider) return;

    float val = eventData[P_VALUE].GetFloat();
    int id = slider->GetVar("SliderID").GetI32();

    // Update label text
    switch (id)
    {
    case 50: if (cfFireRateLabel_) cfFireRateLabel_->SetText(String((int)val)); break;
    case 51: if (cfFireSizeLabel_) cfFireSizeLabel_->SetText(String((double)val, 2)); break;
    case 52: if (cfSmokeRateLabel_) cfSmokeRateLabel_->SetText(String((int)val)); break;
    case 53: if (cfSmokeSizeLabel_) cfSmokeSizeLabel_->SetText(String((double)val, 2)); break;
    case 54: if (cfLightRangeLabel_) cfLightRangeLabel_->SetText(String((double)val, 1)); break;
    case 55: if (cfBrightnessLabel_) cfBrightnessLabel_->SetText(String((double)val, 2)); break;
    case 56: if (cfVelocityLabel_) cfVelocityLabel_->SetText(String((double)val, 1)); break;
    case 57: if (cfUpdraftLabel_) cfUpdraftLabel_->SetText(String((double)val, 1)); break;
    case 58: if (cfLifetimeLabel_) cfLifetimeLabel_->SetText(String((double)Max(val, 0.1f), 1)); break;
    case 75: if (cfSmokeEmitSizeLabel_) cfSmokeEmitSizeLabel_->SetText(String((double)val, 2)); break;
    case 76: if (cfSmokeGrowLabel_) cfSmokeGrowLabel_->SetText(String((double)val, 2)); break;
    case 77: if (cfSmokeLifeLabel_) cfSmokeLifeLabel_->SetText(String((double)Max(val, 0.5f), 1)); break;
    case 78: if (cfSmokeRiseLabel_) cfSmokeRiseLabel_->SetText(String((double)val, 1)); break;
    case 79: if (cfSmokeDampLabel_) cfSmokeDampLabel_->SetText(String((double)val, 1)); break;
    default: break;
    }

    // Broadcast to all campfires — fire once, forget
    VariantMap& data = GetEventDataMap();
    data[CampfireSettingsChanged::P_SLIDERID] = id;
    data[CampfireSettingsChanged::P_VALUE] = val;
    SendEvent(E_CAMPFIRE_SETTINGS_CHANGED, data);
}

void TerrainNode::HandleAnimationTextKey(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace AnimationTextKeyEvent;
    Node* node = static_cast<Node*>(eventData[P_NODE].GetPtr());
    Animation* anim = static_cast<Animation*>(eventData[P_ANIMATION].GetPtr());
    String name = eventData[P_NAME].GetString();
    float time = eventData[P_TIME].GetFloat();
    String data = eventData[P_DATA].ToString();

    // The model node fires the event; walk up to the parent (creature node) for naming
    Node* parent = node ? node->GetParent() : nullptr;
    String parentName = parent ? parent->GetName() : String("?");
    String animName = anim ? GetFileName(anim->GetName()) : String("?");

    URHO3D_LOGDEBUGF("[TextKey] %s: %s @ %.2fs in %s  data=%s",
        parentName.CString(), name.CString(), time, animName.CString(), data.CString());

    // Text key → driven key bridge: key name "drive", data = "paramName=value"
    // Feeds the named parameter into DrivenKeySystem as a driver input.
    // Example text key: name="drive", data="flame_intensity=0.8"
    if (name == "drive")
    {
        auto* dks = GetSubsystem<DrivenKeySystem>();
        if (dks && !data.Empty())
        {
            unsigned eq = data.Find('=');
            if (eq != String::NPOS)
            {
                String paramName = data.Substring(0, eq).Trimmed();
                float value = ToFloat(data.Substring(eq + 1).Trimmed());
                dks->SetDriver(StringHash(paramName), value);
            }
        }
        return;
    }

    // Text key driven sound: key name "sound", data = resource path
    // Also accept "sfx", "footstep", "vocal", "death" as aliases — all play 3D sound on the creature node
    if (name == "sound" || name == "sfx" || name == "footstep" || name == "vocal" || name == "death")
    {
        if (data.Empty() || !parent)
            return;

        auto* source = parent->GetComponent<SoundSource3D>();
        if (!source)
            source = (node ? node->GetComponent<SoundSource3D>() : nullptr);
        if (!source)
            return;

        auto* cache = GetSubsystem<ResourceCache>();
        auto* sound = cache->GetResource<Sound>(data);
        if (sound)
            source->Play(sound);
    }

    // Equipment attach: text key name="equip", data="bone:model_path[:material_path]"
    // Optional offset/rotation after material: "bone:model:mat:ox,oy,oz:rx,ry,rz:sx,sy,sz"
    if (name == "equip" && parent)
    {
        // Parse: bone:model[:material[:offset[:rotation[:scale]]]]
        Vector<String> parts = data.Split(':');
        if (parts.Size() < 2)
            return;

        String boneName = parts[0].Trimmed();
        String modelPath = parts[1].Trimmed();

        // Find the bone node on the model node (child of creature parent)
        Node* modelNode = parent->GetChildren().Size() > 0 ? parent->GetChildren()[0].Get() : nullptr;
        if (!modelNode)
            return;
        Node* boneNode = modelNode->GetChild(boneName, true);
        if (!boneNode)
        {
            URHO3D_LOGWARNINGF("[Equip] Bone '%s' not found on %s", boneName.CString(), parentName.CString());
            return;
        }

        // Remove existing attachment on this bone (re-equip)
        Node* existing = boneNode->GetChild("Equip_" + boneName);
        if (existing)
            existing->Remove();

        auto* cache = GetSubsystem<ResourceCache>();
        auto* mdl = cache->GetResource<Model>(modelPath);
        if (!mdl)
        {
            URHO3D_LOGWARNINGF("[Equip] Model '%s' not found", modelPath.CString());
            return;
        }

        Node* equipNode = boneNode->CreateChild("Equip_" + boneName);
        auto* sm = equipNode->CreateComponent<StaticModel>();
        sm->SetModel(mdl);
        sm->SetCastShadows(false);

        // Optional material
        if (parts.Size() >= 3 && !parts[2].Trimmed().Empty())
        {
            auto* mat = cache->GetResource<Material>(parts[2].Trimmed());
            if (mat)
                sm->SetMaterial(mat);
        }

        // Optional offset: ox,oy,oz
        if (parts.Size() >= 4 && !parts[3].Trimmed().Empty())
        {
            Vector<String> ov = parts[3].Split(',');
            if (ov.Size() == 3)
                equipNode->SetPosition(Vector3(ToFloat(ov[0]), ToFloat(ov[1]), ToFloat(ov[2])));
        }

        // Optional rotation: rx,ry,rz (euler degrees)
        if (parts.Size() >= 5 && !parts[4].Trimmed().Empty())
        {
            Vector<String> rv = parts[4].Split(',');
            if (rv.Size() == 3)
                equipNode->SetRotation(Quaternion(ToFloat(rv[0]), ToFloat(rv[1]), ToFloat(rv[2])));
        }

        // Optional scale: sx,sy,sz or uniform s
        if (parts.Size() >= 6 && !parts[5].Trimmed().Empty())
        {
            Vector<String> sv = parts[5].Split(',');
            if (sv.Size() == 3)
                equipNode->SetScale(Vector3(ToFloat(sv[0]), ToFloat(sv[1]), ToFloat(sv[2])));
            else if (sv.Size() == 1)
                equipNode->SetScale(ToFloat(sv[0]));
        }

        URHO3D_LOGINFOF("[Equip] Attached '%s' to bone '%s' on %s",
            modelPath.CString(), boneName.CString(), parentName.CString());
        return;
    }

    // Equipment detach: text key name="unequip", data="bone"
    if (name == "unequip" && parent)
    {
        String boneName = data.Trimmed();
        Node* modelNode = parent->GetChildren().Size() > 0 ? parent->GetChildren()[0].Get() : nullptr;
        if (!modelNode)
            return;
        Node* boneNode = modelNode->GetChild(boneName, true);
        if (boneNode)
        {
            Node* equipNode = boneNode->GetChild("Equip_" + boneName);
            if (equipNode)
            {
                equipNode->Remove();
                URHO3D_LOGINFOF("[Equip] Detached from bone '%s' on %s",
                    boneName.CString(), parentName.CString());
            }
        }
        return;
    }
}

void TerrainNode::HandleCampfireSettingsChanged(StringHash eventType, VariantMap& eventData)
{
    using namespace CampfireSettingsChanged;
    int id = eventData[P_SLIDERID].GetI32();
    float val = eventData[P_VALUE].GetFloat();

    // Find all campfire nodes and apply the setting
    Vector<Node*> campfires;
    {
        const Vector<SharedPtr<Node>>& children = scene_->GetChildren();
        for (unsigned i = 0; i < children.Size(); ++i)
            if (children[i]->GetName() == "Campfire")
                campfires.Push(children[i].Get());
    }

    for (Node* cf : campfires)
    {
        Node* fireNode = cf->GetChild("Fire");
        Node* smokeNode = cf->GetChild("Smoke");
        Node* lightNode = cf->GetChild("FireLight");
        ParticleEmitter* fireEmit = fireNode ? fireNode->GetComponent<ParticleEmitter>() : nullptr;
        ParticleEmitter* smokeEmit = smokeNode ? smokeNode->GetComponent<ParticleEmitter>() : nullptr;
        Light* light = lightNode ? lightNode->GetComponent<Light>() : nullptr;
        ParticleEffect* fe = fireEmit ? fireEmit->GetEffect() : nullptr;
        ParticleEffect* se = smokeEmit ? smokeEmit->GetEffect() : nullptr;

        switch (id)
        {
        // Fire settings
        case 50:
            // Fire rate: update both the live effect (so the slider feels responsive)
            // AND the cached baseline so UpdateCampfireFuel modulates the new max.
            cfBaseFireRateMin_ = val * 0.9f;
            cfBaseFireRateMax_ = val * 1.1f;
            if (fe) { fe->SetMinEmissionRate(val * 0.9f); fe->SetMaxEmissionRate(val * 1.1f); }
            break;
        case 51:
        {
            float lo = Max(val * 0.4f, 0.01f), hi = Max(val, 0.02f);
            if (fe) { fe->SetMinParticleSize(Vector2(lo, lo)); fe->SetMaxParticleSize(Vector2(hi, hi)); }
            break;
        }
        case 56: if (fe) { fe->SetMinVelocity(val * 0.5f); fe->SetMaxVelocity(val * 1.5f); } break;
        case 57: if (fe) fe->SetConstantForce(Vector3(0.0f, val, 0.0f)); break;
        case 58: { float t = Max(val, 0.1f); if (fe) { fe->SetMinTimeToLive(t); fe->SetMaxTimeToLive(t); } break; }

        // Fire color frames
        case 60: case 61: case 62: case 63:
        case 64: case 65: case 66: case 67:
        case 68: case 69: case 70: case 71:
        {
            if (!fe || fe->GetNumColorFrames() < 3) break;
            int frameIdx = (id - 60) / 4;
            int channel  = (id - 60) % 4;
            const ColorFrame* existing = fe->GetColorFrame(frameIdx);
            if (!existing) break;
            Color c = existing->color_;
            if (channel == 0) c.r_ = val;
            else if (channel == 1) c.g_ = val;
            else if (channel == 2) c.b_ = val;
            else c.a_ = val;
            float times[] = {0.0f, 0.5f, 1.0f};
            fe->SetColorFrame(frameIdx, ColorFrame(c, times[frameIdx]));
            break;
        }

        // Smoke settings
        case 52:
            cfBaseSmokeRateMin_ = val * 0.5f;
            cfBaseSmokeRateMax_ = val;
            if (se) { se->SetMinEmissionRate(val * 0.5f); se->SetMaxEmissionRate(val); }
            break;
        case 53:
        {
            float lo = Max(val * 0.25f, 0.01f), hi = Max(val, 0.02f);
            if (se) { se->SetMinParticleSize(Vector2(lo, lo * 1.5f)); se->SetMaxParticleSize(Vector2(hi, hi * 1.2f)); }
            break;
        }
        case 75: if (se) se->SetEmitterSize(Vector3(val, val, val)); break;
        case 76: if (se) se->SetSizeMul(Max(val, 0.1f)); break;
        case 77: { float t = Max(val, 0.5f); if (se) { se->SetMinTimeToLive(t); se->SetMaxTimeToLive(t); } break; }
        case 78: if (se) se->SetConstantForce(Vector3(0.0f, val, 0.0f)); break;
        case 79: if (se) se->SetDampingForce(val); break;

        // Light settings
        case 54:
            cfBaseLightRange_ = Max(val, 0.5f);
            if (light) light->SetRange(Max(val, 0.5f));
            break;
        case 55:
            cfBaseLightBrightness_ = val;
            if (light) light->SetBrightness(val);
            break;

        default: break;
        }
    }

    // Keep flicker state in sync for brightness changes
    if (id == 55) { fireBrightnessCurrent_ = val; fireBrightnessTarget_ = val; }
}

// ============================================================================
// Habitat Rules UI handlers
// ============================================================================

void TerrainNode::HandleHabitatSlider(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    if (!slider) return;

    String species = slider->GetVar("HabitatSpecies").GetString();
    String param = slider->GetVar("HabitatParam").GetString();
    float val = eventData[P_VALUE].GetFloat();

    // Find the mutable rule
    auto& allRules = const_cast<HashMap<String, HabitatRule>&>(habitatRules_.GetAllRules());
    auto it = allRules.Find(species);
    if (it == allRules.End())
        return;

    HabitatRule& rule = it->second_;

    if (param == "density")
    {
        rule.density = (int)val;
        // Update value label
        auto* ui = GetSubsystem<UI>();
        auto* valText = ui->GetRoot()->GetChildStaticCast<Text>(species + "_density_val", true);
        if (valText)
            valText->SetText(String(rule.density));
    }
    else if (param == "altMin")
    {
        rule.altitudeMin = val;
        auto* ui = GetSubsystem<UI>();
        auto* valText = ui->GetRoot()->GetChildStaticCast<Text>(species + "_alt_val", true);
        if (valText)
            valText->SetText(String((int)rule.altitudeMin) + "-" + String((int)rule.altitudeMax));
    }
    else if (param == "altMax")
    {
        rule.altitudeMax = val;
        auto* ui = GetSubsystem<UI>();
        auto* valText = ui->GetRoot()->GetChildStaticCast<Text>(species + "_alt_val", true);
        if (valText)
            valText->SetText(String((int)rule.altitudeMin) + "-" + String((int)rule.altitudeMax));
    }
}

void TerrainNode::HandleHabitatButton(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace Released;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn) return;

    String action = btn->GetVar("HabitatAction").GetString();
    String species = btn->GetVar("HabitatSpecies").GetString();

    if (action == "respawn" && !species.Empty())
    {
        RespawnSpecies(species);
    }
    else if (action == "respawnAll")
    {
        RespawnAllAnimals();
    }
    else if (action == "save")
    {
        if (habitatRules_.Save(context_, "GameDB/habitat_rules.json"))
            URHO3D_LOGINFO("Habitat rules saved");
    }
    else if (action == "reset")
    {
        habitatRules_.Load(context_, "GameDB/habitat_rules.json");
        URHO3D_LOGINFO("Habitat rules reloaded from disk");
        // Re-spawn all with fresh rules
        RespawnAllAnimals();
    }
}

void TerrainNode::RespawnSpecies(const String& species)
{
    // Per-species respawn: remove all, rebuild all (CreateAnimals spawns everything from rules)
    URHO3D_LOGINFOF("Respawning species: %s (full rebuild)", species.CString());
    RespawnAllAnimals();
}

void TerrainNode::RespawnAllAnimals()
{
    // Remove all land animals
    for (unsigned i = 0; i < animalNodes_.Size(); ++i)
    {
        Node* node = animalNodes_[i];
        if (node)
            node->Remove();
    }
    animalNodes_.Clear();

    // Animals respawn via server-side PopulationManager + BroadcastSpawnCreature.
    URHO3D_LOGINFO("Cleared local animal nodes — server handles respawning");
}

void TerrainNode::HandleAudioSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    if (!slider) return;

    float val = eventData[P_VALUE].GetFloat();
    String soundType = slider->GetVar("SoundType").GetString();

    auto* audio = GetSubsystem<Audio>();
    if (audio)
        audio->SetMasterGain(soundType, val);

    // Update percentage label
    auto* label = static_cast<Text*>(slider->GetVar("ValueLabel").GetPtr());
    if (label)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)(val * 100));
        label->SetText(String(buf));
    }

    // Persist audio gains across sessions
    SaveThemePrefs();
}

void TerrainNode::HandleWeatherSlider(StringHash eventType, VariantMap& eventData)
{
    using namespace SliderChanged;
    auto* slider = static_cast<Slider*>(eventData[P_ELEMENT].GetPtr());
    if (!slider) return;

    float val = eventData[P_VALUE].GetFloat();
    int id = slider->GetVar("SliderID").GetI32();

    // Any slider movement enables override mode
    weatherOverride_ = true;

    char buf[16];
    if (id == 30) // cloud cover
    {
        weather_.cloudCover = val;
        weatherTarget_.cloudCover = val;
        // Derive fog/ambient from cloud cover
        weather_.fogDensity = 1.0f - val * 0.3f - weather_.precipitation * 0.3f;
        weather_.ambientDim = 1.0f - val * 0.4f;
        snprintf(buf, sizeof(buf), "%d%%", (int)(val * 100.0f));
        if (cloudCoverLabel_) cloudCoverLabel_->SetText(buf);
    }
    else if (id == 31) // rain (drives precipitation, clouds, rain particles)
    {
        weather_.precipitation = val;
        weatherTarget_.precipitation = val;
        rainOverride_ = val;
        // Rain forces cloud cover up — can't rain from clear sky
        // Clouds lead rain: light rain = moderate clouds, heavy rain = overcast
        float minCloud = val * 0.85f + 0.15f * val * val; // ramps faster than rain
        if (weather_.cloudCover < minCloud)
        {
            weather_.cloudCover = minCloud;
            weatherTarget_.cloudCover = minCloud;
        }
        weather_.fogDensity = 1.0f - weather_.cloudCover * 0.3f - val * 0.3f;
        weather_.ambientDim = 1.0f - weather_.cloudCover * 0.4f;
        weather_.windSpeed = Clamp(0.1f + val * 0.4f, 0.0f, 1.0f);
        snprintf(buf, sizeof(buf), "%d%%", (int)(val * 100.0f));
        if (rainLabel_) rainLabel_->SetText(buf);
    }

    ApplyWeatherToScene();
}

void TerrainNode::HandleHemisphereToggle(StringHash eventType, VariantMap& eventData)
{
    using namespace Toggled;
    auto* cb = static_cast<CheckBox*>(eventData[P_ELEMENT].GetPtr());
    if (cb)
    {
        hemisphereEnabled_ = cb->IsChecked();
        URHO3D_LOGINFOF("Hemisphere lighting: %s", hemisphereEnabled_ ? "ON" : "OFF");
    }
}

void TerrainNode::HandleMenuButton(StringHash eventType, VariantMap& eventData)
{
    using namespace Released;
    auto* element = static_cast<UIElement*>(eventData[P_ELEMENT].GetPtr());
    if (!element) return;

    int action = element->GetVar("MenuAction").GetI32();

    switch (action)
    {
    // Brush modes — stay in menu mode so user can also pick shape, then Tab to paint
    case 200:
    {
        int wasBrush = brushMode_;
        brushMode_ = 0;
        if (terrainPanel_) terrainPanel_->SetVisible(false);
        if (wasBrush != 0 && cameraMode_ == CAM_GOD && preEditCameraMode_ != CAM_GOD)
            EndEditTransition();
        break;
    }
    case 201: case 202: case 203: case 204: case 205:
    {
        int prev = brushMode_;
        if (action == 201) brushMode_ = 1;
        else if (action == 202) brushMode_ = 3;
        else if (action == 203) brushMode_ = 4;
        else if (action == 204) brushMode_ = 5;
        else brushMode_ = 6;  // river
        // Auto-switch to god cam if not already there
        if (prev == 0 && cameraMode_ != CAM_GOD)
            BeginEditTransition();
        break;
    }

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
    if (action >= 200 && action <= 205)
        HighlightBrushButton(activeModeBtn_, static_cast<Button*>(element));
    else if (action >= 210 && action <= 216)
        HighlightBrushButton(activeShapeBtn_, static_cast<Button*>(element));

    // Auto-enter camera mode only when brush is active (mode != none)
    if ((action >= 201 && action <= 205) || (action >= 210 && action <= 216))
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

// ---------------------------------------------------------------------------
// Collapsible UI section helper
// ---------------------------------------------------------------------------

static UIElement* CreateCollapsibleSection(UIElement* parent, Font* font, const String& title, bool startExpanded = true)
{
    auto* context = parent->GetContext();

    // Header button
    auto* header = parent->CreateChild<Button>();
    header->SetStyleAuto();
    header->SetFixedHeight(22);
    header->SetLayout(LM_HORIZONTAL, 4, IntRect(4, 0, 4, 0));

    auto* headerText = header->CreateChild<Text>();
    headerText->SetFont(font, 11);
    headerText->SetText(String(startExpanded ? "v " : "> ") + title);
    headerText->SetColor(Color(0.9f, 0.9f, 0.9f));
    headerText->SetAlignment(HA_LEFT, VA_CENTER);

    // Content container
    auto* content = parent->CreateChild<UIElement>();
    content->SetLayout(LM_VERTICAL, 2, IntRect(2, 2, 2, 2));
    content->SetVisible(startExpanded);

    // Store content and title for toggle
    header->SetVar("SectionContent", content);
    header->SetVar("SectionTitle", title);

    // Toggle handler — subscribe inline
    header->SubscribeToEvent(header, E_RELEASED, [](StringHash, VariantMap& eventData)
    {
        auto* btn = static_cast<Button*>(eventData[Released::P_ELEMENT].GetPtr());
        auto* content = static_cast<UIElement*>(btn->GetVar("SectionContent").GetPtr());
        if (!content) return;
        bool expanded = !content->IsVisible();
        content->SetVisible(expanded);
        // Update header text
        String title = btn->GetVar("SectionTitle").GetString();
        auto* txt = btn->GetChildStaticCast<Text>(0);
        if (txt)
            txt->SetText(String(expanded ? "v " : "> ") + title);

        // Force parent window to shrink-to-fit: walk up to the Window and
        // reset its height so the layout doesn't use the old size as target
        auto* panel = content->GetParent();
        if (panel)
        {
            panel->SetHeight(0);
            panel->UpdateLayout();
        }
    });

    return content;
}

void TerrainNode::CreateTerrainPanel()
{
    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;
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

    // ===== Brush Mode section (expanded) =====
    auto* modeContent = CreateCollapsibleSection(terrainPanel_, font, "Brush Mode", true);

    auto* modeRow = modeContent->CreateChild<UIElement>();
    modeRow->SetLayout(LM_HORIZONTAL, 2);
    modeRow->SetFixedHeight(24);
    const char* modeNames[] = {"Off", "Height", "Smooth", "Flatten", "Erode", "River"};
    const int modeActions[] = {200, 201, 202, 203, 204, 205};
    for (int i = 0; i < 6; ++i)
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
        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleMenuButton));
    }

    // ===== Brush Shape section (expanded) =====
    auto* shapeContent = CreateCollapsibleSection(terrainPanel_, font, "Brush Shape", true);

    auto* shapeRow = shapeContent->CreateChild<UIElement>();
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
        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleMenuButton));
    }

    // ===== Brush Settings section (expanded) =====
    auto* settingsContent = CreateCollapsibleSection(terrainPanel_, font, "Brush Settings", true);

    // Brush size slider (0.25-50)
    auto* sizeRow = settingsContent->CreateChild<UIElement>();
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
    SubscribeToEvent(sizeSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Brush strength slider (0.01-0.2)
    auto* bstrRow = settingsContent->CreateChild<UIElement>();
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
    SubscribeToEvent(bstrSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Rotation slider (-180..+180) + text edit
    auto* rotRow = settingsContent->CreateChild<UIElement>();
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
    SubscribeToEvent(rotSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    brushRotEdit_ = rotRow->CreateChild<LineEdit>();
    brushRotEdit_->SetStyleAuto();
    brushRotEdit_->SetFixedWidth(50);
    brushRotEdit_->SetFixedHeight(18);
    { char buf[16]; sprintf(buf, "%.1f", brushRotation_); brushRotEdit_->SetText(buf); }
    brushRotEdit_->SetCursorPosition(0);
    SubscribeToEvent(brushRotEdit_, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleBrushRotEdit));

    // ===== Heightmap I/O section (collapsed) =====
    auto* ioContent = CreateCollapsibleSection(terrainPanel_, font, "Heightmap I/O", false);

    auto* ioRow = ioContent->CreateChild<UIElement>();
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
    SubscribeToEvent(saveBtn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleMenuButton));

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
    SubscribeToEvent(loadBtn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleMenuButton));

    // ===== Hydraulic Erosion section (collapsed) =====
    auto* erosionContent = CreateCollapsibleSection(terrainPanel_, font, "Hydraulic Erosion", false);

    // Iterations slider (10-500)
    auto* iterRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(iterSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Rainfall slider (0.001-0.05)
    auto* rainRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(rainSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Erosion strength slider (0.01-1.0)
    auto* strRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(strSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Max Depth slider (0.0-1.0, fraction of original height)
    auto* depthRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(depthSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Min Height Floor slider (0.0-0.5, fraction of height range)
    auto* floorRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(floorSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Ridge Protection slider (0.0-1.0)
    auto* ridgeRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(ridgeSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Border Padding slider (0-16 cells)
    auto* borderRow = erosionContent->CreateChild<UIElement>();
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
    SubscribeToEvent(borderSlider, E_SLIDERCHANGED, URHO3D_HANDLER(TerrainNode, HandleErosionSlider));

    // Erode button
    auto* erodeBtn = erosionContent->CreateChild<Button>();
    erodeBtn->SetStyleAuto();
    erodeBtn->SetMinWidth(120);
    erodeBtn->SetFixedHeight(22);
    erodeBtn->SetVar("MenuAction", 240);
    auto* erodeTxt = erodeBtn->CreateChild<Text>();
    erodeTxt->SetFont(font, 11);
    erodeTxt->SetText("Erode World");
    erodeTxt->SetColor(Color(0.5f, 1.0f, 0.5f));
    erodeTxt->SetAlignment(HA_CENTER, VA_CENTER);
    SubscribeToEvent(erodeBtn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleMenuButton));
}

void TerrainNode::HandleErosionSlider(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HandleBrushRotEdit(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HighlightBrushButton(Button*& active, Button* btn)
{
    // Reset previous
    if (active)
        active->SetColor(Color::WHITE);
    active = btn;
    if (active)
        active->SetColor(Color(0.4f, 0.8f, 1.0f));  // light blue highlight
}

void TerrainNode::ToggleTerrainPanel()
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

void TerrainNode::ShowSaveHeightmapDialog()
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

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandleHeightmapSaveChosen));
}

void TerrainNode::ShowLoadHeightmapDialog()
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

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandleHeightmapLoadChosen));
}

void TerrainNode::HandleHeightmapSaveChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;
    SaveHeightmapToFile(path);
}

void TerrainNode::HandleHeightmapLoadChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;
    LoadHeightmapFromFile(path);
}

void TerrainNode::SaveHeightmapToFile(const String& path)
{
    if (!editableHeightMap_) return;

    if (editableHeightMap_->SavePNG(path))
        URHO3D_LOGINFOF("Heightmap saved to %s", path.CString());
    else
        URHO3D_LOGERRORF("Failed to save heightmap to %s", path.CString());
}

void TerrainNode::LoadHeightmapFromFile(const String& path)
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

void TerrainNode::ShowExportPrefabDialog()
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

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandlePrefabExportChosen));
}

void TerrainNode::HandlePrefabExportChosen(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::ShowLoadPrefabDialog()
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

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandlePrefabLoadChosen));
}

void TerrainNode::HandlePrefabLoadChosen(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::UpdatePrefabBrushLabel()
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

void TerrainNode::HandleRigidBodySleep(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::ShowSaveSceneDialog()
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
    fileSelector_->SetFileName("TestScene.xml");

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandleSceneSaveChosen));
}

void TerrainNode::ShowLoadSceneDialog()
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

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandleSceneLoadChosen));
}

void TerrainNode::HandleSceneSaveChosen(StringHash eventType, VariantMap& eventData)
{
    using namespace FileSelected;
    String path = eventData[P_FILENAME].GetString();
    bool ok = eventData[P_OK].GetBool();
    fileSelector_.Reset();

    if (!ok || path.Empty()) return;

    // Flush current state to scene Vars before save
    if (scene_)
    {
        scene_->SetVar("GodRays", godRaysEnabled_);
        scene_->SetVar("Shadows", shadowsEnabled_);
        scene_->SetVar("WaterReflection", waterReflectionEnabled_);
        scene_->SetVar("PostProcess", postProcessEnabled_);
        scene_->SetVar("HemisphereLighting", hemisphereEnabled_);
        if (cameraNode_)
        {
            Vector3 pos = cameraNode_->GetWorldPosition();
            scene_->SetVar("CameraX", pos.x_);
            scene_->SetVar("CameraY", pos.y_);
            scene_->SetVar("CameraZ", pos.z_);
            scene_->SetVar("CameraYaw", yaw_);
            scene_->SetVar("CameraPitch", pitch_);
            scene_->SetVar("CameraMode", (int)cameraMode_);
        }
    }

    File file(context_, path, FILE_WRITE);
    if (scene_->SaveXML(file))
        URHO3D_LOGINFOF("Scene saved to %s", path.CString());
    else
        URHO3D_LOGERRORF("Failed to save scene to %s", path.CString());
}

void TerrainNode::HandleSceneLoadChosen(StringHash eventType, VariantMap& eventData)
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

    // Load into a NEW scene object — defer swap to next frame start so the
    // renderer finishes drawing the current frame with the old scene's resources.
    SharedPtr<Scene> newScene(new Scene(context_));
    if (newScene->LoadXML(file))
    {
        pendingScene_ = newScene;
        URHO3D_LOGINFO("Scene loaded — swap deferred to next frame");
    }
    else
        URHO3D_LOGERRORF("Failed to load scene from %s", path.CString());
}

void TerrainNode::WakeSleepingBodiesOnTerrain()
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

void TerrainNode::DrawGizmo()
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

void TerrainNode::BeginGizmoDrag(int axis)
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

void TerrainNode::UpdateGizmoDrag()
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

void TerrainNode::EndGizmoDrag()
{
    if (!gizmoDragging_)
        return;

    gizmoDragging_ = false;
    gizmoAxis_ = -1;
}

void TerrainNode::WakeSelectedNode()
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
            SubscribeToEvent(selectedNode_, E_RIGIDBODYSLEEP, URHO3D_HANDLER(TerrainNode, HandleRigidBodySleep));
        }
        rb->Activate();
    }
}

// ============================================================================
// Import Model
// ============================================================================

void TerrainNode::ShowImportModelDialog()
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

    SubscribeToEvent(fileSelector_, E_FILESELECTED, URHO3D_HANDLER(TerrainNode, HandleImportModelChosen));
}

void TerrainNode::HandleImportModelChosen(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::ShowGenerateMeshPanel()
{
    if (generateMeshPanel_)
    {
        generateMeshPanel_->SetVisible(!generateMeshPanel_->IsVisible());
        return;
    }

    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = ui->GetRoot()->GetDefaultStyle();
    Font* font = font_;

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
    SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleGenerateMesh));
}

void TerrainNode::HandleGenerateMesh(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HandleMeshShapeChanged(StringHash eventType, VariantMap& eventData)
{
    // Future: update parameter labels based on selected shape
}

// ============================================================================
// Undo / Redo
// ============================================================================

void TerrainNode::PushUndo(const UndoAction& action)
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

void TerrainNode::Undo()
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

void TerrainNode::Redo()
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

String TerrainNode::SerializeNode(Node* node)
{
    XMLFile xmlFile(context_);
    XMLElement rootElem = xmlFile.CreateRoot("node");
    node->SaveXML(rootElem);
    return xmlFile.ToString();
}

SharedPtr<Image> TerrainNode::CloneHeightMap()
{
    if (brush_)
        return brush_->CloneHeightMap();

    return SharedPtr<Image>();
}

void TerrainNode::RestoreHeightMap(Image* src)
{
    if (brush_)
    {
        brush_->RestoreHeightMap(src);
        WakeSleepingBodiesOnTerrain();
    }
}

void TerrainNode::BeginTerrainStroke()
{
    if (!terrainStrokeActive_)
    {
        terrainStrokeBefore_ = CloneHeightMap();
        terrainStrokeActive_ = true;
    }
}

void TerrainNode::EndTerrainStroke()
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
        RefreshMinimap();
    }
}

// ============================================================================
// Minimap
// ============================================================================

void TerrainNode::CreateSelectedVitalsPanel()
{
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    Font* font = font_;

    vitalsPanel_ = uiRoot->CreateChild<BorderImage>("VitalsPanel");
    auto* panel = static_cast<BorderImage*>(vitalsPanel_);
    panel->SetStyle("Window");
    panel->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
    panel->SetFixedSize(140, 64);
    panel->SetOpacity(0.85f);
    panel->SetVisible(false);
    panel->SetPriority(100);  // above other UI

    auto makeBar = [&](const String& label, const Color& color) -> BorderImage*
    {
        auto* row = panel->CreateChild<UIElement>();
        row->SetLayout(LM_HORIZONTAL, 2, IntRect(0, 0, 0, 0));
        row->SetFixedHeight(11);

        auto* lbl = row->CreateChild<Text>();
        lbl->SetFont(font, 9);
        lbl->SetText(label);
        lbl->SetColor(Color(0.9f, 0.9f, 0.9f));
        lbl->SetMinWidth(36);

        auto* track = row->CreateChild<BorderImage>();
        track->SetStyle("Window");
        track->SetFixedSize(90, 9);
        track->SetColor(Color(0.05f, 0.05f, 0.05f, 0.8f));

        auto* fill = track->CreateChild<BorderImage>();
        fill->SetTexture(GetSubsystem<ResourceCache>()->GetResource<Texture2D>("Textures/UI.png"));
        fill->SetImageRect(IntRect(48, 0, 64, 16));
        fill->SetBorder(IntRect(0, 0, 0, 0));
        fill->SetColor(color);
        fill->SetSize(90, 9);
        fill->SetPosition(0, 0);
        return fill;
    };

    vitalHungerBar_  = makeBar("Hunger",  Color(0.9f, 0.7f, 0.2f));
    vitalThirstBar_  = makeBar("Thirst",  Color(0.3f, 0.6f, 0.9f));
    vitalStaminaBar_ = makeBar("Stamina", Color(0.4f, 0.9f, 0.4f));
    vitalWarmthBar_  = makeBar("Warmth",  Color(0.9f, 0.4f, 0.2f));
}

void TerrainNode::UpdateSelectedVitalsPanel()
{
    if (!vitalsPanel_)
        return;

    // Hide if nothing selected or selected node has no vitals
    if (!selectedNode_ || selectedNode_.Expired())
    {
        vitalsPanel_->SetVisible(false);
        return;
    }

    auto* creature = selectedNode_->GetDerivedComponent<Creature>();
    if (!creature || !creature->HasVitals())
    {
        vitalsPanel_->SetVisible(false);
        return;
    }

    // Project the creature's head position to screen coords
    auto* camera = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
    auto* graphics = GetSubsystem<Graphics>();
    if (!camera || !graphics)
    {
        vitalsPanel_->SetVisible(false);
        return;
    }

    Vector3 worldPos = selectedNode_->GetWorldPosition() + Vector3(0.0f, 2.5f, 0.0f);

    // Behind-camera test using camera-space Z
    Vector3 viewSpace = camera->GetView() * worldPos;
    if (viewSpace.z_ <= 0.0f)
    {
        vitalsPanel_->SetVisible(false);
        return;
    }

    Vector2 screenPos = camera->WorldToScreenPoint(worldPos);
    int sw = graphics->GetWidth();
    int sh = graphics->GetHeight();
    int px = (int)(screenPos.x_ * sw) - vitalsPanel_->GetWidth() / 2;
    int py = (int)(screenPos.y_ * sh) - vitalsPanel_->GetHeight();

    // Clamp to screen
    px = Clamp(px, 0, sw - vitalsPanel_->GetWidth());
    py = Clamp(py, 0, sh - vitalsPanel_->GetHeight());

    vitalsPanel_->SetPosition(px, py);
    vitalsPanel_->SetVisible(true);

    // Update fill widths
    auto setBar = [](BorderImage* bar, float value)
    {
        if (!bar) return;
        float frac = Clamp(value / 100.0f, 0.0f, 1.0f);
        bar->SetSize((int)(90 * frac), 9);
    };
    setBar(vitalHungerBar_,  creature->GetHunger());
    setBar(vitalThirstBar_,  creature->GetThirst());
    setBar(vitalStaminaBar_, creature->GetStamina());
    setBar(vitalWarmthBar_,  creature->GetWarmth());
}

void TerrainNode::CreateMinimap()
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

    // Fixed top-down orthographic camera covering the full terrain — rendered once
    IntVector2 numPatches = terrain_->GetNumPatches();
    float terrainWorldSize = numPatches.x_ * terrain_->GetSpacing().x_ * terrain_->GetPatchSize();
    minimapCameraNode_ = scene_->CreateTemporaryChild("MinimapCamera", LOCAL);
    auto* minimapCam = minimapCameraNode_->CreateComponent<Camera>();
    minimapCam->SetOrthographic(true);
    minimapCam->SetOrthoSize(terrainWorldSize);
    minimapCam->SetFarClip(500.0f);
    minimapCam->SetNearClip(1.0f);
    minimapCam->SetFlipVertical(true);  // Vulkan RTT Y-flip compensation
    // Look straight down, centered on terrain
    Vector3 terrainCenter = terrain_->GetNode()->GetWorldPosition();
    minimapCameraNode_->SetPosition(Vector3(terrainCenter.x_, 200.0f, terrainCenter.z_));
    minimapCameraNode_->SetRotation(Quaternion(90.0f, 0.0f, 0.0f));

    // Render once via manual update mode — re-render only on heightmap edits
    RenderSurface* surface = minimapTex_->GetRenderSurface();
    SharedPtr<Viewport> minimapViewport(new Viewport(context_, scene_, minimapCam));
    minimapViewport->SetDrawDebug(false);
    surface->SetViewport(0, minimapViewport);
    surface->SetUpdateMode(SURFACE_MANUALUPDATE);
    surface->QueueUpdate();

    // Sprite in bottom-right corner — rotated by yaw each frame
    minimap_ = uiRoot->CreateChild<Sprite>("Minimap");
    minimap_->SetTexture(minimapTex_);
    minimap_->SetImageRect(IntRect(0, 0, texSize, texSize));
    minimap_->SetFixedSize(displaySize, displaySize);
    minimap_->SetAlignment(HA_RIGHT, VA_BOTTOM);
    minimap_->SetHotSpot(displaySize / 2, displaySize / 2);  // rotate around center
    minimap_->SetPosition(Vector2(-(displaySize / 2.0f + 8.0f), -(displaySize / 2.0f + 8.0f)));
    minimap_->SetOpacity(0.85f);

    // Camera dot — child Sprite, repositioned each frame based on player world XZ
    minimapCameraDot_ = minimap_->CreateChild<Sprite>("CameraDot");
    minimapCameraDot_->SetFixedSize(6, 6);
    minimapCameraDot_->SetColor(Color::RED);
    minimapCameraDot_->SetPosition(Vector2(displaySize / 2.0f - 3.0f, displaySize / 2.0f - 3.0f));
}

void TerrainNode::UpdateMinimapCamera()
{
    if (!minimap_ || !cameraNode_ || !terrain_)
        return;

    const int displaySize = 192;

    // Rotate the minimap image so camera forward = "up"
    minimap_->SetRotation(-yaw_);

    // Map player world XZ to minimap pixel coords
    Vector3 camPos = cameraNode_->GetWorldPosition();
    Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();
    IntVector2 numPatches = terrain_->GetNumPatches();
    float terrainWorldSize = numPatches.x_ * terrain_->GetSpacing().x_ * terrain_->GetPatchSize();
    float halfSize = terrainWorldSize * 0.5f;

    // Normalise to 0..1 across terrain
    float u = (camPos.x_ - terrainPos.x_ + halfSize) / terrainWorldSize;
    float v = 1.0f - (camPos.z_ - terrainPos.z_ + halfSize) / terrainWorldSize;  // Z flipped for screen coords

    float dotX = u * displaySize - 3.0f;
    float dotY = v * displaySize - 3.0f;
    if (minimapCameraDot_)
        minimapCameraDot_->SetPosition(Vector2(dotX, dotY));
}

void TerrainNode::RefreshMinimap()
{
    if (minimapTex_)
    {
        RenderSurface* surface = minimapTex_->GetRenderSurface();
        if (surface)
            surface->QueueUpdate();
    }
}

void TerrainNode::UpdateMinimapBlips()
{
    if (!minimap_ || !cameraNode_ || !terrain_)
        return;

    const int displaySize = 192;
    const int dotSize = 4;

    // Terrain mapping constants
    Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();
    IntVector2 numPatches = terrain_->GetNumPatches();
    float terrainWorldSize = numPatches.x_ * terrain_->GetSpacing().x_ * terrain_->GetPatchSize();
    float halfSize = terrainWorldSize * 0.5f;

    auto* camera = cameraNode_->GetComponent<Camera>();
    if (!camera)
        return;
    const Frustum& frustum = camera->GetFrustum();

    // Reset pool index — reuse existing sprites
    minimapBlipUsed_ = 0;

    // Helper: place a blip if the object is in the frustum
    auto placeBlip = [&](const Vector3& worldPos, const Color& color)
    {
        if (frustum.IsInside(worldPos) == OUTSIDE)
            return;

        float u = (worldPos.x_ - terrainPos.x_ + halfSize) / terrainWorldSize;
        float v = 1.0f - (worldPos.z_ - terrainPos.z_ + halfSize) / terrainWorldSize;

        // Get or create a sprite from the pool
        Sprite* blip;
        if (minimapBlipUsed_ < minimapBlips_.Size())
        {
            blip = minimapBlips_[minimapBlipUsed_];
        }
        else
        {
            blip = minimap_->CreateChild<Sprite>("Blip");
            blip->SetFixedSize(dotSize, dotSize);
            minimapBlips_.Push(blip);
        }
        blip->SetColor(color);
        blip->SetPosition(Vector2(u * displaySize - dotSize * 0.5f, v * displaySize - dotSize * 0.5f));
        blip->SetVisible(true);
        ++minimapBlipUsed_;
    };

    // Animals — green tones by species
    for (unsigned i = 0; i < animalNodes_.Size(); ++i)
    {
        Node* n = animalNodes_[i].Get();
        if (!n || !n->IsEnabled()) continue;
        const String& name = n->GetName();
        Color c(0.2f, 0.8f, 0.2f);  // default green
        if (name == "Fox" || name == "Wolf")
            c = Color(1.0f, 0.4f, 0.1f);  // predators orange
        else if (name == "Deer" || name == "Stag")
            c = Color(0.6f, 0.3f, 1.0f);  // purple
        placeBlip(n->GetWorldPosition(), c);
    }

    // Fish — blue
    for (unsigned i = 0; i < fishNodes_.Size(); ++i)
    {
        Node* n = fishNodes_[i].Get();
        if (!n || !n->IsEnabled()) continue;
        placeBlip(n->GetWorldPosition(), Color(0.3f, 0.6f, 1.0f));
    }

    // Campfire — color matches its flickering light
    if (campfireNode_ && campfireLight_)
        placeBlip(campfireNode_->GetWorldPosition(), campfireLight_->GetColor());

    // Hide unused pool sprites
    for (unsigned i = minimapBlipUsed_; i < minimapBlips_.Size(); ++i)
        minimapBlips_[i]->SetVisible(false);
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

SharedPtr<Texture2D> TerrainNode::GenerateShapeIcon(int shape)
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

void TerrainNode::MoveCamera(float timeStep)
{
    auto* input = GetSubsystem<Input>();

    // Numpad Enter = toggle all UI visibility (does NOT affect debug draw)
    if (input->GetKeyPress(KEY_KP_ENTER))
    {
        auto* uiRoot = GetSubsystem<UI>()->GetRoot();
        uiRoot->SetVisible(!uiRoot->IsVisible());
    }

    // F1 = cycle camera mode: editor → third person → first person
    if (input->GetKeyPress(KEY_F1))
    {
        static const char* modeNames[] = {"Free-fly Editor", "Third Person", "First Person", "Free Flight"};
        // Cycle: GOD → CHASE → FIRSTPERSON → GOD (skip FREEFLIGHT)
        if (cameraMode_ == CAM_GOD)
            cameraMode_ = CAM_CHASE;
        else if (cameraMode_ == CAM_CHASE)
            cameraMode_ = CAM_FIRSTPERSON;
        else
            cameraMode_ = CAM_GOD;
        URHO3D_LOGINFOF("Camera mode: %s", modeNames[cameraMode_]);
    }

    // Tab = unpossess if possessing, otherwise toggle cursor/menu mode
    if (input->GetKeyPress(KEY_TAB))
    {
        if (possessing_)
        {
            UnpossessNPC();
        }
        else
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
                // Clear gizmo when leaving edit mode
                if (gizmoMode_ != 0)
                {
                    WakeSelectedNode();
                    gizmoMode_ = 0;
                }
            }
        }
    }

    // P = possess selected HumanNPC / unpossess current
    if (input->GetKeyPress(KEY_P))
    {
        if (possessing_)
        {
            UnpossessNPC();
        }
        else if (selectedNode_ && !selectedNode_.Expired())
        {
            auto* npc = selectedNode_->GetDerivedComponent<HumanNPC>(false);
            if (npc)
                PossessNPC(npc->GetNode());
        }
    }

    // Tilde/Backtick = also toggle cursor mode (convenience alias)
    if (input->GetKeyPress(KEY_BACKQUOTE))
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

    // I = toggle inventory panel
    if (input->GetKeyPress(KEY_I))
        ToggleInventory();

    // C = toggle crafting panel
    if (input->GetKeyPress(KEY_C))
        ToggleCrafting();

    // B = toggle build mode
    if (input->GetKeyPress(KEY_B))
        ToggleBuildMode();

    // T = trade (if looking at another player's HumanNPC) or place trap (on terrain).
    if (input->GetKeyPress(KEY_T))
    {
        // Accept/reject incoming trade prompt
        if (tradeIncomingPending_)
        {
            SendTradeAccept();
            tradeIncomingPending_ = false;
        }
        else if (!tradeOpen_ && !tradePending_)
        {
            auto* camera = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
            auto* octree = scene_ ? scene_->GetComponent<Octree>() : nullptr;
            if (camera && octree)
            {
                Ray cameraRay = camera->GetScreenRay(0.5f, 0.5f);
                Vector<RayQueryResult> results;
                RayOctreeQuery query(results, cameraRay, RAY_TRIANGLE, 100.0f);
                octree->Raycast(query);

                bool sentTrade = false;
                for (unsigned i = 0; i < results.Size(); ++i)
                {
                    if (dynamic_cast<TerrainPatch*>(results[i].drawable_))
                        continue;

                    Node* hitNode = results[i].node_;
                    // Walk up to scene-level parent
                    while (hitNode && hitNode->GetParent() && hitNode->GetParent() != scene_)
                        hitNode = hitNode->GetParent();

                    if (!hitNode || hitNode == scene_)
                        continue;

                    // Check if it's a HumanNPC (not our own possessed one)
                    auto* npc = hitNode->GetDerivedComponent<HumanNPC>(false);
                    if (npc && hitNode != possessedNPC_.Get())
                    {
                        // Check proximity (3m)
                        if (possessedNPC_)
                        {
                            float dist = (hitNode->GetWorldPosition() - possessedNPC_->GetWorldPosition()).Length();
                            if (dist > 3.0f)
                            {
                                URHO3D_LOGINFO("Too far to trade (must be within 3m)");
                                break;
                            }
                        }
                        tradePartnerNodeId_ = hitNode->GetID();
                        SendTradeRequest(hitNode->GetID());
                        sentTrade = true;
                        break;
                    }
                }

                // Fall through to trap placement if no NPC was targeted
                if (!sentTrade)
                {
                    for (unsigned i = 0; i < results.Size(); ++i)
                    {
                        if (!dynamic_cast<TerrainPatch*>(results[i].drawable_))
                            continue;

                        Vector3 hitPos = results[i].position_;
                        auto* network = GetSubsystem<Network>();
                        auto* serverConn = network ? network->GetServerConnection() : nullptr;
                        if (serverConn)
                        {
                            VectorBuffer buf;
                            buf.WriteI32(400);
                            buf.WriteFloat(hitPos.x_);
                            buf.WriteFloat(hitPos.y_);
                            buf.WriteFloat(hitPos.z_);
                            buf.WriteFloat(0.0f);
                            serverConn->SendMessage(MSG_PLACE_TRAP, true, true, buf);
                            URHO3D_LOGINFOF("Sent MSG_PLACE_TRAP at (%.1f,%.1f,%.1f)",
                                             hitPos.x_, hitPos.y_, hitPos.z_);
                        }
                        break;
                    }
                }
            }
        }
    }

    // Y/N for incoming trade prompts
    if (tradeIncomingPending_)
    {
        if (input->GetKeyPress(KEY_Y))
        {
            SendTradeAccept();
            tradeIncomingPending_ = false;
            if (tradePromptWindow_)
            {
                tradePromptWindow_->Remove();
                tradePromptWindow_ = nullptr;
            }
        }
        if (input->GetKeyPress(KEY_N))
        {
            SendTradeReject();
            tradeIncomingPending_ = false;
            if (tradePromptWindow_)
            {
                tradePromptWindow_->Remove();
                tradePromptWindow_ = nullptr;
            }
        }
    }

    // Escape closes trade window
    if (tradeOpen_ && input->GetKeyPress(KEY_ESCAPE))
    {
        SendTradeCancel();
        CloseTradeWindow();
    }

    // Phase 4: proximity warning while trade is open
    if (tradeOpen_ && possessedNPC_ && tradePartnerNodeId_ != 0)
    {
        tradeProximityCheckTimer_ += timeStep;
        if (tradeProximityCheckTimer_ >= 0.5f)
        {
            tradeProximityCheckTimer_ = 0.0f;
            Node* partner = scene_ ? scene_->GetNode(tradePartnerNodeId_) : nullptr;
            if (partner)
            {
                float dist = (partner->GetWorldPosition() - possessedNPC_->GetWorldPosition()).Length();
                if (dist > 4.0f && tradeStatusText_)
                    tradeStatusText_->SetText("Moving too far — trade will cancel!");
                else if (tradeStatusText_ && tradeStatusText_->GetText() == "Moving too far — trade will cancel!")
                    tradeStatusText_->SetText("");
            }
        }
    }

    // E = interact (harvest dead/trapped creature, campfire feed, gate toggle, shelter sleep/respawn, repair)
    if (input->GetKeyPress(KEY_E))
    {
        // Harvest is highest priority — if a dead/trapped creature is focused, take it.
        // Resource Chain Phase 2: server validates and rolls loot from GameDB.
        if (focusedHarvestNodeId_ != 0 && focusedHarvestCreatureId_ != 0)
        {
            auto* network = GetSubsystem<Network>();
            auto* serverConn = network ? network->GetServerConnection() : nullptr;
            if (serverConn)
            {
                VectorBuffer buf;
                buf.WriteU32(focusedHarvestNodeId_);
                buf.WriteI32(focusedHarvestCreatureId_);
                serverConn->SendMessage(MSG_HARVEST, true, true, buf);
                URHO3D_LOGINFOF("Sent MSG_HARVEST: nodeId=%u creatureId=%d",
                                 focusedHarvestNodeId_, focusedHarvestCreatureId_);
            }
        }
        // Campfire reach is 3m and only fires when fuel is needed — try it first,
        // fall through to building interact if player isn't near the fire.
        TryCampfireInteract();
        TryBoardCanoe();
        TryBuildingInteract();
        TryHarvestCrop();
        TryDrink();

        // Tree chopping
        if (focusedTreeId_ != 0)
            SendChopTree(focusedTreeId_);

        // Mining — E on an ore outcrop
        if (focusedOutcropNode_)
            SendMineRequest(focusedOutcropNode_);
    }

    // Q = tap tree for resin/bark/medicine (requires knife equipped, server validates)
    if (input->GetKeyPress(KEY_Q) && focusedTreeId_ != 0)
        SendTapTree(focusedTreeId_);

    // P = plant crop (if player has seeds + digging stick)
    if (input->GetKeyPress(KEY_P))
    {
        TryPlantCrop();
    }

    // R = rotate ghost in build mode
    if (input->GetKeyPress(KEY_R) && buildingSystem_ && buildingSystem_->IsBuildMode())
        buildingSystem_->RotateGhost();

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

    // Build mode: left-click to place, Escape to cancel
    if (buildingSystem_ && buildingSystem_->IsBuildMode())
    {
        if (input->GetMouseButtonPress(MOUSEB_LEFT) && buildingSystem_->IsGhostValid())
        {
            auto* network = GetSubsystem<Network>();
            auto* serverConn = network ? network->GetServerConnection() : nullptr;
            buildingSystem_->RequestBuild(serverConn);
        }
        if (input->GetKeyPress(KEY_ESCAPE))
        {
            buildingSystem_->SetBuildMode(false);
            if (buildMenuWindow_)
                buildMenuWindow_->SetVisible(false);
            buildMenuOpen_ = false;
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

    // Hotkeys (Space = debug toggle only in editor mode; in character modes it's jump)
    if (input->GetKeyPress(KEY_SPACE) && cameraMode_ == CAM_GOD)
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

    if (input->GetKeyPress(KEY_F7))
        biomeDebugOverlay_ = !biomeDebugOverlay_;

    if (input->GetKeyPress(KEY_F10))
        RequestDeathLog();

    if (input->GetKeyPress(KEY_HOME))
        RequestDeathAnalytics();

    if (input->GetKeyPress(KEY_F11))
        GetSubsystem<Graphics>()->ToggleFullscreen();

    // M = toggle territory overlay
    if (input->GetKeyPress(KEY_M))
    {
        showTerritoryOverlay_ = !showTerritoryOverlay_;
        if (showTerritoryOverlay_)
            UpdateTerritoryOverlay();
        else
        {
            for (unsigned i = 0; i < patchOverlayNodes_.Size(); ++i)
                if (patchOverlayNodes_[i]) patchOverlayNodes_[i]->Remove();
            patchOverlayNodes_.Clear();
        }
    }

    // Undo/Redo
    if ((input->GetQualifiers() & QUAL_CTRL) && input->GetKeyPress(KEY_Z))
        Undo();
    if ((input->GetQualifiers() & QUAL_CTRL) && input->GetKeyPress(KEY_Y))
        Redo();

    // Gizmo mode shortcuts — only in editor/cursor mode (menuOpen_), not during normal play
    if (cameraMode_ == CAM_GOD && menuOpen_ && input->GetKeyPress(KEY_T))
    {
        int prev = gizmoMode_;
        gizmoMode_ = (prev == 1) ? 0 : 1;
        if (prev != 0 && gizmoMode_ == 0)
            WakeSelectedNode();
    }
    if (cameraMode_ == CAM_GOD && menuOpen_ && input->GetKeyPress(KEY_R))
    {
        int prev = gizmoMode_;
        gizmoMode_ = (prev == 2) ? 0 : 2;
        if (prev != 0 && gizmoMode_ == 0)
            WakeSelectedNode();
    }
    if (cameraMode_ == CAM_GOD && menuOpen_ && input->GetKeyPress(KEY_S))
    {
        int prev = gizmoMode_;
        gizmoMode_ = (prev == 3) ? 0 : 3;
        if (prev != 0 && gizmoMode_ == 0)
            WakeSelectedNode();
    }
    if (cameraMode_ == CAM_GOD && input->GetKeyPress(KEY_G))
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

    if (cameraMode_ == CAM_GOD && (input->GetKeyPress(KEY_BACKSPACE) || input->GetKeyPress(KEY_DELETE)) && selectedNode_ && !selectedNode_.Expired())
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

        // Send object delete to server before removing locally
        SendObjectDelete(node->GetID());

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

    // Scroll wheel: tool active = adjust brush radius, editor mode = inch camera forward/back
    int wheel = input->GetMouseMoveWheel();
    if (wheel != 0)
    {
        if (brushMode_ != 0 && cameraMode_ == CAM_GOD)
            brushRadius_ = Clamp(brushRadius_ + wheel * 0.25f, 0.25f, 50.0f);
        else if (cameraMode_ == CAM_GOD)
            cameraNode_->Translate(Vector3::FORWARD * (float)wheel * 0.5f);
    }

    // Mouse look (when menu closed) — all camera modes use yaw/pitch
    if (!menuOpen_)
    {
        const float MOUSE_SENSITIVITY = 0.1f;
        IntVector2 mouseMove = input->GetMouseMove();
        yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
        pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
        pitch_ = Clamp(pitch_, -90.0f, 90.0f);
    }

    // Camera positioning depends on mode
    if (cameraMode_ == CAM_GOD)
    {
        // Free-fly camera with WASD + sphere cast collision
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

        float MOVE_SPEED = input->GetKeyDown(KEY_SHIFT) ? 100.0f : 20.0f;
        if (inCanoe_) MOVE_SPEED *= canoeSpeedBoost_;
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
    }
    else
    {
        // First person / third person: camera follows character
        UpdateCharacterCamera();
    }

    // Sync reflection camera to main camera each frame
    if (reflectionCameraNode_ && cameraNode_)
    {
        reflectionCameraNode_->SetWorldPosition(cameraNode_->GetWorldPosition());
        reflectionCameraNode_->SetWorldRotation(cameraNode_->GetWorldRotation());
    }

    // Terrain brush raycast — only in editor camera mode
    hasBrushHit_ = false;
    bool showBrush = cameraMode_ == CAM_GOD && ((brushMode_ != 0) || (terrainPanel_ && terrainPanel_->IsVisible()) || prefabBrush_);
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
                    SubscribeToEvent(instance, E_RIGIDBODYSLEEP, URHO3D_HANDLER(TerrainNode, HandleRigidBodySleep));
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

            // Send object create to server
            SendObjectCreate(instance, cachedBrushHit_, cachedBrushNormal_);
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

    // Melee attack — LMB while possessing a character
    if (possessing_ && input->GetMouseButtonPress(MOUSEB_LEFT))
        TryMeleeAttack();

    // Object selection raycast (when no brush mode active, and not gizmo dragging)
    if (!possessing_ && brushMode_ == 0 && !gizmoDragging_ && input->GetMouseButtonPress(MOUSEB_LEFT))
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

                    // Check for resource pickups first
                    bool pickedUp = false;
                    Node* hitNode = nullptr;
                    for (unsigned i = 0; i < results.Size(); ++i)
                    {
                        Node* node = results[i].body_->GetNode();
                        if (!node || node->HasComponent<Terrain>())
                            continue;
                        if (node->GetComponent<ResourcePickup>())
                        {
                            TryPickupAtCursor(pickRay);
                            pickedUp = true;
                            break;
                        }
                        if (!hitNode)
                            hitNode = node;
                    }

                    // Octree fallback — select nodes without physics bodies (campfires, etc.)
                    if (!pickedUp && !hitNode)
                    {
                        auto* octree = scene_->GetComponent<Octree>();
                        if (octree)
                        {
                            Vector<RayQueryResult> octreeResults;
                            RayOctreeQuery oq(octreeResults, pickRay, RAY_OBB, 300.0f);
                            octree->Raycast(oq);
                            for (unsigned j = 0; j < octreeResults.Size(); ++j)
                            {
                                Node* node = octreeResults[j].node_;
                                if (!node || dynamic_cast<TerrainPatch*>(octreeResults[j].drawable_))
                                    continue;
                                // Walk up to scene's direct child
                                Node* candidate = node;
                                while (candidate && candidate->GetParent() && candidate->GetParent() != scene_)
                                    candidate = candidate->GetParent();
                                if (!candidate || candidate == scene_)
                                    continue;
                                // Only accept nodes with game components we can interact with
                                if (candidate->GetDerivedComponent<Creature>(false) ||
                                    candidate->GetComponent<StaticModel>())
                                {
                                    hitNode = candidate;
                                    break;
                                }
                            }
                        }
                    }

                    if (!pickedUp)
                    {
                        if (hitNode)
                        {
                            // In god cam: click on a HumanNPC → possess it directly
                            if (cameraMode_ == CAM_GOD)
                            {
                                auto* npc = hitNode->GetDerivedComponent<HumanNPC>(false);
                                if (npc)
                                {
                                    PossessNPC(hitNode);
                                    hitNode = nullptr;  // Don't also select
                                }
                            }
                            if (hitNode)
                                SelectNode(hitNode);
                        }
                        else
                            DeselectNode();
                    }
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

    // Update camera coordinates display
    if (cameraCoordsText_ && cameraNode_)
    {
        Vector3 p = cameraNode_->GetWorldPosition();
        char buf[64];
        snprintf(buf, sizeof(buf), "X:%.1f  Y:%.1f  Z:%.1f", p.x_, p.y_, p.z_);
        cameraCoordsText_->SetText(buf);
    }

    // Update Melbourne clock display
    if (clockText_)
    {
        static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        static const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int hour = (int)timeOfDay_;
        int minute = (int)((timeOfDay_ - hour) * 60.0f);
        if (hour < 0) hour = 0;
        if (hour > 23) hour = 23;
        if (minute < 0) minute = 0;
        if (minute > 59) minute = 59;

        // Convert dayOfYear_ (1-365) to day + month
        int remaining = Clamp(dayOfYear_, 1, 365) - 1;
        int month = 0;
        for (int m = 0; m < 12; ++m)
        {
            if (remaining < daysInMonth[m])
            {
                month = m;
                break;
            }
            remaining -= daysInMonth[m];
        }
        int day = remaining + 1;

        char clockBuf[32];
        snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d  %d %s", hour, minute, day, monthNames[month]);
        clockText_->SetText(clockBuf);
    }
}

// ============================================================================
// Terrain Brush
// ============================================================================

// BrushShapeFalloff logic moved to TerrainBrush::ShapeFalloff().
// DrawBrushOutline builds its own outline vertices independently.

void TerrainNode::ApplyBrush(const Vector3& worldPos, float timeStep)
{
    if (!terrain_ || !editableHeightMap_ || brushMode_ == 0 || !brush_)
        return;

    // Client-side patch ownership check (admins exempt, broadcasts exempt)
    if (adminLevel_ == 0 && !replayingBroadcast_)
    {
        const float patchWorldSize = 128.0f;
        int bpx = (int)floorf(worldPos.x_ / patchWorldSize);
        int bpz = (int)floorf(worldPos.z_ / patchWorldSize);
        if (!OwnsThisPatch(bpx, bpz))
        {
            URHO3D_LOGINFOF("ApplyBrush REJECTED: worldPos=(%.1f,%.1f,%.1f) patch=(%d,%d) ownedPatches=%u adminLevel=%d",
                worldPos.x_, worldPos.y_, worldPos.z_, bpx, bpz, ownedPatches_.Size(), adminLevel_);
            return;
        }
    }

    // Sync brush parameters from UI state
    brush_->SetMode(brushMode_);
    brush_->SetShape(brushShape_);
    brush_->SetRadius(brushRadius_);
    brush_->SetStrength(brushStrength_);
    brush_->SetSmoothStrength(smoothStrength_);
    brush_->SetRotation(brushRotation_);
    brush_->SetFlattenHeight(lockedFlattenHeight_);

    // Delegate to shared brush
    bool modified = brush_->Apply(worldPos, timeStep);

    // Read back flatten height (may have been locked by brush)
    if (brushMode_ == 4)
        lockedFlattenHeight_ = brush_->GetFlattenHeight();

    if (!modified)
        return;

    // Check for newly exposed metal deposits after terrain lowering
    if (metalDeposits_ && terrain_ && (brushMode_ == 1 || brushMode_ == 5))
    {
        Vector3 spacing = terrain_->GetSpacing();
        int brushPixels = (int)(brushRadius_ / spacing.x_) + 1;
        IntVector2 center = terrain_->WorldToHeightMap(worldPos);

        for (int dz = -brushPixels; dz <= brushPixels; ++dz)
        {
            for (int dx = -brushPixels; dx <= brushPixels; ++dx)
            {
                int gx = center.x_ + dx;
                int gz = center.y_ + dz;
                unsigned char type, purity, qty, depth;
                if (!metalDeposits_->Sample(gx, gz, type, purity, qty, depth))
                    continue;

                // Deposit depth: A channel, 1 unit ≈ 0.1m world height
                float depositWorldDepth = depth * 0.1f;
                // Original terrain height at this grid point (from heightmap)
                IntVector2 hmPos(gx, gz);
                Vector3 worldPt = terrain_->HeightMapToWorld(hmPos);
                float surfaceHeight = worldPt.y_;
                // Original surface was higher — deposit is at (originalSurface - depositWorldDepth)
                // But we only have current height. Approximate: if current terrain height
                // is low enough that we've dug past the deposit depth, expose it.
                float waterLevel = 5.0f;
                float depthBelowWater = waterLevel + depositWorldDepth;
                if (surfaceHeight <= depthBelowWater && depth > 5)
                {
                    // Newly exposed — spawn an outcrop if one doesn't exist
                    metalDeposits_->SpawnOutcrops(scene_, terrain_, waterLevel, (int)depth);
                    break;  // one spawn pass per brush stroke is enough
                }
            }
            if (dz > brushPixels) break;  // already spawned
        }
    }

    // Water reveal: when terrain is lowered below water level, spawn water tile for that patch
    if ((brushMode_ == 2 || brushMode_ == 5) && waterNode_ && terrain_)
    {
        Vector3 spacing = terrain_->GetSpacing();
        IntVector2 numPatches = terrain_->GetNumPatches();
        float patchWorldX = spacing.x_ * terrain_->GetPatchSize();
        float patchWorldZ = spacing.z_ * terrain_->GetPatchSize();
        Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();
        float halfTerrainX = numPatches.x_ * patchWorldX * 0.5f;
        float halfTerrainZ = numPatches.y_ * patchWorldZ * 0.5f;

        // Determine which patch the brush center is in
        int px = (int)((worldPos.x_ - terrainPos.x_ + halfTerrainX) / patchWorldX);
        int pz = (int)((worldPos.z_ - terrainPos.z_ + halfTerrainZ) / patchWorldZ);
        px = Clamp(px, 0, numPatches.x_ - 1);
        pz = Clamp(pz, 0, numPatches.y_ - 1);

        // Check if any point in the brush area is now below water
        float h = terrain_->GetHeight(worldPos);
        if (h < baseWaterY_)
        {
            // Check if this patch already has a water tile (by position proximity)
            float cx = terrainPos.x_ - halfTerrainX + px * patchWorldX + patchWorldX * 0.5f;
            float cz = terrainPos.z_ - halfTerrainZ + pz * patchWorldZ + patchWorldZ * 0.5f;
            bool hasTile = false;
            for (unsigned i = 0; i < waterNode_->GetNumChildren(); ++i)
            {
                Node* tile = waterNode_->GetChild(i);
                Vector3 tp = tile->GetPosition();
                if (Abs(tp.x_ - cx) < 1.0f && Abs(tp.z_ - cz) < 1.0f)
                { hasTile = true; break; }
            }
            if (!hasTile)
            {
                auto* cache = GetSubsystem<ResourceCache>();
                Node* tile = waterNode_->CreateChild("WaterTile");
                tile->SetPosition(Vector3(cx, 0.0f, cz));
                tile->SetScale(Vector3(patchWorldX, 1.0f, patchWorldZ));
                auto* sm = tile->CreateComponent<StaticModel>();
                sm->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
                sm->SetMaterial(cache->GetResource<Material>("Materials/Water.xml"));
                sm->SetViewMask(0x80000000);
            }
        }
    }

    // Re-upload water map to GPU after river brush edits
    if (brushMode_ == 6 && waterMapTex_ && waterMap_)
        waterMapTex_->SetData(waterMap_);

    // Send water edit to server for river brush (water height map sync)
    if (brushMode_ == 6)
        SendWaterEdit(worldPos);

    // Send terrain edit to server (rate-limited)
    SendTerrainEdit(worldPos, timeStep);

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

void TerrainNode::ApplyLowerBrush(const Vector3& worldPos, float timeStep)
{
    int saved = brushMode_;
    brushMode_ = 2;
    ApplyBrush(worldPos, timeStep);
    brushMode_ = saved;
}

// ---------------------------------------------------------------------------
// Camera transition (edit mode ↔ play mode)
// ---------------------------------------------------------------------------

void TerrainNode::BeginEditTransition()
{
    if (!cameraNode_ || cameraTransitioning_)
        return;

    preEditCameraMode_ = cameraMode_;
    transitionToEdit_ = true;
    cameraTransitioning_ = true;
    transitionTime_ = 0.0f;

    // Start from current camera state
    transitionStartPos_ = cameraNode_->GetPosition();
    transitionStartRot_ = cameraNode_->GetRotation();

    // Target: 66% up, 33% back from current position
    // "Back" = opposite of camera's forward direction projected onto XZ plane
    Vector3 forward = cameraNode_->GetDirection();
    Vector3 backXZ(-forward.x_, 0.0f, -forward.z_);
    if (backXZ.Length() < 0.001f)
        backXZ = Vector3::BACK;  // looking straight down, default to -Z
    backXZ.Normalize();

    float upDist = 20.0f;    // 66% of the offset goes up
    float backDist = 10.0f;  // 33% goes backward

    transitionEndPos_ = transitionStartPos_ + Vector3::UP * upDist + backXZ * backDist;

    // Orient camera to look at the original look-at target
    // The target is where the camera was pointing at the terrain
    Vector3 lookTarget = transitionStartPos_ + forward * 30.0f;  // approximate
    if (terrain_)
    {
        // Raycast to find the actual terrain point we were looking at
        auto* camera = cameraNode_->GetComponent<Camera>();
        if (camera)
        {
            Ray ray = camera->GetScreenRay(0.5f, 0.5f);
            auto* octree = scene_ ? scene_->GetComponent<Octree>() : nullptr;
            if (octree)
            {
                Vector<RayQueryResult> results;
                RayOctreeQuery query(results, ray, RAY_TRIANGLE, 500.0f);
                octree->Raycast(query);
                for (unsigned i = 0; i < results.Size(); ++i)
                {
                    if (dynamic_cast<TerrainPatch*>(results[i].drawable_))
                    {
                        lookTarget = results[i].position_;
                        break;
                    }
                }
            }
        }
    }

    // Calculate rotation that looks from end position toward the target
    Vector3 dir = (lookTarget - transitionEndPos_).Normalized();
    transitionEndRot_.FromLookRotation(dir);
}

void TerrainNode::EndEditTransition()
{
    if (!cameraNode_ || cameraTransitioning_)
        return;

    transitionToEdit_ = false;
    cameraTransitioning_ = true;
    transitionTime_ = 0.0f;

    // Start from current god-cam position
    transitionStartPos_ = cameraNode_->GetPosition();
    transitionStartRot_ = cameraNode_->GetRotation();

    // Target: back to where the avatar is (chase position)
    if (characterNode_ && !characterNode_.Expired())
    {
        Node* charNode = characterNode_;
        Vector3 charPos = charNode->GetPosition();

        if (preEditCameraMode_ == CAM_FIRSTPERSON)
        {
            transitionEndPos_ = charPos + Vector3::UP * 1.7f;  // eye height
            transitionEndRot_.FromLookRotation(charNode->GetDirection());
        }
        else
        {
            // Chase cam: behind and above
            Vector3 charDir = charNode->GetDirection();
            transitionEndPos_ = charPos - charDir * 5.0f + Vector3::UP * 3.0f;
            Vector3 lookDir = (charPos + Vector3::UP * 1.5f - transitionEndPos_).Normalized();
            transitionEndRot_.FromLookRotation(lookDir);
        }
    }
    else
    {
        // No avatar — just stay put and restore mode
        transitionEndPos_ = transitionStartPos_;
        transitionEndRot_ = transitionStartRot_;
    }
}

void TerrainNode::UpdateCameraTransition(float timeStep)
{
    if (!cameraTransitioning_ || !cameraNode_)
        return;

    transitionTime_ += timeStep;
    float t = Clamp(transitionTime_ / transitionDuration_, 0.0f, 1.0f);

    // Smooth ease-in-out
    float smooth = t * t * (3.0f - 2.0f * t);

    cameraNode_->SetPosition(transitionStartPos_.Lerp(transitionEndPos_, smooth));
    cameraNode_->SetRotation(transitionStartRot_.Slerp(transitionEndRot_, smooth));

    if (t >= 1.0f)
    {
        cameraTransitioning_ = false;
        if (transitionToEdit_)
        {
            cameraMode_ = CAM_GOD;
            // Set yaw/pitch from final rotation so god-cam continues smoothly
            Vector3 euler = transitionEndRot_.EulerAngles();
            pitch_ = euler.x_;
            yaw_ = euler.y_;
        }
        else
        {
            cameraMode_ = preEditCameraMode_;
        }
    }
}

// ---------------------------------------------------------------------------
// Patch boundary visualization
// ---------------------------------------------------------------------------

Node* TerrainNode::CreatePatchBoundary(Node* oldNode, int patchX, int patchZ, const Color& color)
{
    if (!scene_ || !terrain_)
        return nullptr;

    // Remove old boundary node if it exists
    if (oldNode)
        oldNode->Remove();

    const float patchWorldSize = 128.0f;
    float x0 = patchX * patchWorldSize;
    float z0 = patchZ * patchWorldSize;
    float x1 = x0 + patchWorldSize;
    float z1 = z0 + patchWorldSize;
    const float yOffset = 0.3f;
    const float ribbonWidth = 1.5f;  // world units wide
    const int segments = 32;

    // Build a loop of points around the patch boundary
    Vector<Vector3> loop;
    // Edge 1: x0,z0 → x1,z0
    for (int i = 0; i <= segments; ++i)
    {
        float x = x0 + (x1 - x0) * i / (float)segments;
        float y = terrain_->GetHeight(Vector3(x, 0, z0)) + yOffset;
        loop.Push(Vector3(x, y, z0));
    }
    // Edge 2: x1,z0 → x1,z1
    for (int i = 1; i <= segments; ++i)
    {
        float z = z0 + (z1 - z0) * i / (float)segments;
        float y = terrain_->GetHeight(Vector3(x1, 0, z)) + yOffset;
        loop.Push(Vector3(x1, y, z));
    }
    // Edge 3: x1,z1 → x0,z1
    for (int i = 1; i <= segments; ++i)
    {
        float x = x1 + (x0 - x1) * i / (float)segments;
        float y = terrain_->GetHeight(Vector3(x, 0, z1)) + yOffset;
        loop.Push(Vector3(x, y, z1));
    }
    // Edge 4: x0,z1 → x0,z0
    for (int i = 1; i < segments; ++i)
    {
        float z = z1 + (z0 - z1) * i / (float)segments;
        float y = terrain_->GetHeight(Vector3(x0, 0, z)) + yOffset;
        loop.Push(Vector3(x0, y, z));
    }

    // Build triangle strip ribbon — inner and outer edges offset perpendicular to boundary
    Node* node = scene_->CreateChild("PatchBoundary");
    auto* cg = node->CreateComponent<CustomGeometry>();
    cg->SetNumGeometries(1);
    cg->BeginGeometry(0, TRIANGLE_STRIP);

    unsigned n = loop.Size();
    float halfW = ribbonWidth * 0.5f;
    for (unsigned i = 0; i <= n; ++i)
    {
        unsigned idx = i % n;
        unsigned next = (i + 1) % n;
        // Tangent along the boundary
        Vector3 tangent = (loop[next] - loop[idx]).Normalized();
        // Outward normal = tangent cross UP (points inward toward patch center)
        Vector3 outward = tangent.CrossProduct(Vector3::UP).Normalized();

        Vector3 p = loop[idx];
        // Outer vertex
        cg->DefineVertex(p + outward * halfW);
        cg->DefineColor(color);
        cg->DefineNormal(Vector3::UP);
        // Inner vertex
        cg->DefineVertex(p - outward * halfW);
        cg->DefineColor(color);
        cg->DefineNormal(Vector3::UP);
    }

    cg->SetMaterial(GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/VColUnlit.xml"));
    cg->Commit();
    cg->SetOccludee(false);
    cg->SetCastShadows(false);
    return node;
}

bool TerrainNode::OwnsThisPatch(int px, int pz) const
{
    for (unsigned i = 0; i < ownedPatches_.Size(); ++i)
    {
        if (ownedPatches_[i].x_ == px && ownedPatches_[i].y_ == pz)
            return true;
    }
    return false;
}

void TerrainNode::CreateOwnedPatchBoundaries()
{
    // Remove old boundaries
    for (unsigned i = 0; i < ownedPatchBoundaries_.Size(); ++i)
    {
        if (ownedPatchBoundaries_[i])
            ownedPatchBoundaries_[i]->Remove();
    }
    ownedPatchBoundaries_.Clear();

    URHO3D_LOGINFOF("CreateOwnedPatchBoundaries: %u patches, terrain_=%s, scene_=%s",
        ownedPatches_.Size(), terrain_ ? "yes" : "NULL", scene_ ? "yes" : "NULL");

    // Create gold boundary for each owned patch
    for (unsigned i = 0; i < ownedPatches_.Size(); ++i)
    {
        Node* node = CreatePatchBoundary(nullptr, ownedPatches_[i].x_, ownedPatches_[i].y_, Color(1.0f, 0.84f, 0.0f, 1.0f));
        URHO3D_LOGINFOF("  patch (%d,%d) -> node=%s", ownedPatches_[i].x_, ownedPatches_[i].y_, node ? "created" : "NULL");
        ownedPatchBoundaries_.Push(WeakPtr<Node>(node));
    }
}

void TerrainNode::UpdateCurrentPatchBoundary()
{
    if (!cameraNode_ || !terrain_)
        return;

    const float patchWorldSize = 128.0f;
    Vector3 camPos = cameraNode_->GetPosition();
    int px = (int)floorf(camPos.x_ / patchWorldSize);
    int pz = (int)floorf(camPos.z_ / patchWorldSize);

    if (px == currentPatchX_ && pz == currentPatchZ_)
        return;  // still on the same patch

    currentPatchX_ = px;
    currentPatchZ_ = pz;

    // Notify server of patch change for resource streaming
    if (loggedIn_ && (px != lastReportedPatchX_ || pz != lastReportedPatchZ_))
        SendPatchPosition(px, pz);

    // Don't draw current-patch boundary if it's an owned patch (already shown in green)
    if (OwnsThisPatch(px, pz))
    {
        if (currentPatchBoundary_)
        {
            currentPatchBoundary_->Remove();
            currentPatchBoundary_ = nullptr;
        }
        return;
    }

    currentPatchBoundary_ = CreatePatchBoundary(currentPatchBoundary_, px, pz, Color(1.0f, 1.0f, 1.0f, 1.0f));
}

void TerrainNode::DrawBrushOutline(const Vector3& worldPos)
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
    else if (brushMode_ == 6) brushColor = Color(0.2f, 0.4f, 1.0f);  // blue for river

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

void TerrainNode::CreateCelestialBodies()
{
    auto* cache = GetSubsystem<ResourceCache>();
    sunNode_ = scene_->CreateTemporaryChild("Sun", LOCAL);
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

    moonNode_ = scene_->CreateTemporaryChild("Moon", LOCAL);
    moonMat_ = new Material(context_);
    auto* moonTech = cache->GetResource<Technique>("Techniques/MoonPhaseDiffMap.xml");
    moonMat_->SetTechnique(0, moonTech ? moonTech : sunTech);
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

    // Earth shadow disc — sits in front of the moon, slides by phase angle
    moonShadowNode_ = moonNode_->CreateChild("EarthShadow", LOCAL);
    moonShadowMat_ = new Material(context_);
    auto* shadowTech = cache->GetResource<Technique>("Techniques/EarthShadow.xml");
    moonShadowMat_->SetTechnique(0, shadowTech ? shadowTech : sunTech);
    moonShadowMat_->SetShaderParameter("MatDiffColor", Color(0.0f, 0.0f, 0.0f, 1.0f));
    auto* shadowBB = moonShadowNode_->CreateComponent<BillboardSet>();
    shadowBB->SetNumBillboards(1);
    shadowBB->SetMaterial(moonShadowMat_);
    shadowBB->SetFaceCameraMode(FC_ROTATE_XYZ);
    shadowBB->SetSorted(false);
    Billboard* shadowQuad = shadowBB->GetBillboard(0);
    shadowQuad->position_ = Vector3::ZERO;
    shadowQuad->size_ = Vector2(22.0f, 22.0f);  // Slightly larger than moon for full coverage
    shadowQuad->enabled_ = true;
    shadowBB->Commit();

    // Reuse MoonLight from loaded scene XML if present, else create
    Node* moonLightNode = scene_->GetChild("MoonLight", true);
    if (!moonLightNode)
        moonLightNode = scene_->CreateTemporaryChild("MoonLight", LOCAL);
    moonLight_ = moonLightNode->GetOrCreateComponent<Light>();
    moonLight_->SetLightType(LIGHT_DIRECTIONAL);
    moonLight_->SetColor(Color(0.6f, 0.6f, 1.0f));
    moonLight_->SetCastShadows(false);  // One shadow-casting light is enough — two doubles shadow pass cost
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

float TerrainNode::CalculateSunAltitude()
{
    float decl = 23.44f * sinf((360.0f / 365.0f) * (dayOfYear_ - 81) * DEG_TO_RAD);
    float hourAngle = 15.0f * (timeOfDay_ + timeOfDayOffset_ - SOLAR_NOON);
    float latRad = MELBOURNE_LAT * DEG_TO_RAD;
    float declRad = decl * DEG_TO_RAD;
    float haRad = hourAngle * DEG_TO_RAD;
    float sinAlt = sinf(latRad) * sinf(declRad) + cosf(latRad) * cosf(declRad) * cosf(haRad);
    return asinf(Clamp(sinAlt, -1.0f, 1.0f)) * RAD_TO_DEG;
}

float TerrainNode::CalculateSunAzimuth(float altitude)
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

float TerrainNode::CalculateMoonAltitude()
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

float TerrainNode::CalculateMoonAzimuth(float moonAlt)
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

Vector3 TerrainNode::AltAzToFlatEarth(float altitude, float azimuth, float distance)
{
    float altRad = altitude * DEG_TO_RAD;
    float azRad = azimuth * DEG_TO_RAD;
    float y = distance * sinf(altRad);
    float horizDist = distance * cosf(altRad);
    float x = horizDist * sinf(azRad);
    float z = horizDist * cosf(azRad);
    return Vector3(x, y, z);
}

void TerrainNode::UpdateCelestialBodies(float timeStep)
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

    // Earth shadow disc position — offset by phase angle
    if (moonShadowNode_)
    {
        float phase = moonAge_ / 29.53f;  // 0..1 (0 = new moon, 0.5 = full)
        float phaseAngle = phase * 2.0f * M_PI;
        // cos(0) = 1 → new moon, shadow centered on moon (fully dark)
        // cos(PI) = -1 → full moon, shadow way off to the side (fully lit)
        // Moon billboard radius is 10 units (20x20 quad / 2)
        float moonRadius = 10.0f;
        float shadowOffset = (1.0f - cosf(phaseAngle)) * moonRadius;
        // Waxing (phase 0→0.5): shadow retreats left-to-right (0 → +20)
        // Waning (phase 0.5→1): shadow returns right-to-left (+20 → 0)
        moonShadowNode_->SetPosition(Vector3(shadowOffset, 0.0f, -0.2f));  // z=-0.2 = in front, no z-fight
    }

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
    {
        // Quantize light direction to discrete angular steps to prevent shadow
        // map shimmer. The real-time Melbourne sun moves continuously, and even
        // with texel snapping the shadow frustum rotation causes per-frame
        // texel swimming. Snapping to ~0.2 degree steps makes the direction
        // perceptually smooth while keeping the shadow map stable.
        Vector3 rawDir = (camPos - (sunNode_ ? sunNode_->GetPosition() : Vector3::ZERO)).Normalized();
        static constexpr float SHADOW_DIR_STEP = 0.0035f;  // ~0.2 degrees in radians
        float invStep = 1.0f / SHADOW_DIR_STEP;
        rawDir.x_ = roundf(rawDir.x_ * invStep) * SHADOW_DIR_STEP;
        rawDir.y_ = roundf(rawDir.y_ * invStep) * SHADOW_DIR_STEP;
        rawDir.z_ = roundf(rawDir.z_ * invStep) * SHADOW_DIR_STEP;
        float len = rawDir.Length();
        if (len > 0.001f)
            sunLight_->GetNode()->SetDirection(rawDir / len);
    }

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

    // --- Occlusion test: fade sun/moon when behind terrain ---
    // Rate-limited to every 6 frames — raycasts are expensive, fade is smooth enough
    if (occlusionFrameSkip_++ >= 6)
    {
        occlusionFrameSkip_ = 0;
        auto* octree = scene_->GetComponent<Octree>();
        if (octree)
        {
            // Sun occlusion
            if (sunNode_ && cachedSunAlt_ > 0.0f)
            {
                Vector3 toSun = (sunNode_->GetWorldPosition() - camPos);
                float sunDist = toSun.Length();
                Ray sunRay(camPos, toSun / sunDist);
                Vector<RayQueryResult> results;
                RayOctreeQuery query(results, sunRay, RAY_TRIANGLE, sunDist);
                octree->Raycast(query);
                cachedSunOccluded_ = false;
                for (unsigned i = 0; i < results.Size(); ++i)
                {
                    auto* drawable = results[i].drawable_;
                    if (!dynamic_cast<TerrainPatch*>(drawable) && !drawable->IsOccluder())
                        continue;
                    if (results[i].distance_ < sunDist)
                    {
                        cachedSunOccluded_ = true;
                        break;
                    }
                }
            }

            // Moon occlusion
            if (moonNode_ && cachedMoonAlt_ > 0.0f)
            {
                Vector3 toMoon = (moonNode_->GetWorldPosition() - camPos);
                float moonDist = toMoon.Length();
                Ray moonRay(camPos, toMoon / moonDist);
                Vector<RayQueryResult> results;
                RayOctreeQuery query(results, moonRay, RAY_TRIANGLE, moonDist);
                octree->Raycast(query);
                cachedMoonOccluded_ = false;
                for (unsigned i = 0; i < results.Size(); ++i)
                {
                    auto* drawable = results[i].drawable_;
                    if (!dynamic_cast<TerrainPatch*>(drawable) && !drawable->IsOccluder())
                        continue;
                    if (results[i].distance_ < moonDist)
                    {
                        cachedMoonOccluded_ = true;
                        break;
                    }
                }
            }
        }
    }
    // Smooth fade applied every frame from cached results
    {
        float sunTarget = cachedSunOccluded_ ? 0.0f : 1.0f;
        sunOcclusionFade_ = Lerp(sunOcclusionFade_, sunTarget, Min(1.0f, timeStep * 2.0f));
        if (sunMat_)
            sunMat_->SetShaderParameter("MatDiffColor", Color(1.0f, 1.0f, 1.0f, sunOcclusionFade_));

        float moonTarget = cachedMoonOccluded_ ? 0.0f : 1.0f;
        moonOcclusionFade_ = Lerp(moonOcclusionFade_, moonTarget, Min(1.0f, timeStep * 2.0f));
        if (moonMat_)
            moonMat_->SetShaderParameter("MatDiffColor", Color(1.0f, 1.0f, 1.0f, moonOcclusionFade_));
    }

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

        // Sun god rays — uses "GodRays" pass
        bool sunRayActive = false;
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

                // Performance Phase 4 — dedup. Density/Decay/Weight are
                // literals that never change, so they upload once and then
                // skip every subsequent frame. The dynamic values still
                // upload only when they actually change.
                SetShaderParamCached("LightScreenPos", screenPos);
                SetShaderParamCached("LightRadius", screenRadius);
                SetShaderParamCached("GodRayColor", sunColor);
                SetShaderParamCached("GodRayDensity", 0.5f);
                SetShaderParamCached("GodRayDecay", 0.97f);
                SetShaderParamCached("GodRayWeight", 0.4f);
                SetShaderParamCached("GodRayExposure", exposure * sunOcclusionFade_);
                SetShaderParamCached("GodRayIntensity", intensity * sunOcclusionFade_);
                sunRayActive = sunOcclusionFade_ > 0.01f;
            }
        }
        renderPath_->SetEnabled("GodRays", sunRayActive);

        // Moon god rays — uses separate "MoonRays" pass (can be active alongside sun)
        bool moonRayActive = false;
        if (godRaysEnabled_ && moonNode_ && cachedMoonAlt_ > -5.0f)
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

                // Set parameters directly on the MoonRays command (not shared with GodRays)
                for (int ci = 0; ci < renderPath_->GetNumCommands(); ++ci)
                {
                    RenderPathCommand* cmd = renderPath_->GetCommand(ci);
                    if (cmd && cmd->tag_ == "MoonRays")
                    {
                        cmd->SetShaderParameter("LightScreenPos", screenPos);
                        cmd->SetShaderParameter("LightRadius", screenRadius);
                        cmd->SetShaderParameter("GodRayColor", Vector3(0.6f, 0.7f, 0.9f));
                        cmd->SetShaderParameter("GodRayDensity", 0.5f);
                        cmd->SetShaderParameter("GodRayDecay", 0.97f);
                        cmd->SetShaderParameter("GodRayWeight", 0.4f);
                        // Phase 3 lunar cycle: god rays scale with phase illumination
                        float phaseIntensity = (1.0f - cosf(moonAge_ / 29.53f * 2.0f * M_PI)) * 0.5f;
                        cmd->SetShaderParameter("GodRayExposure", 0.4f * moonOcclusionFade_ * phaseIntensity);
                        cmd->SetShaderParameter("GodRayIntensity", 0.8f * moonFade * moonOcclusionFade_ * phaseIntensity);
                        break;
                    }
                }
                moonRayActive = moonOcclusionFade_ > 0.01f;
            }
        }
        renderPath_->SetEnabled("MoonRays", moonRayActive);

    }
}

// ============================================================================
// Rain Particles
// ============================================================================

void TerrainNode::CreateRain()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Create rain emitter node (will be repositioned above camera each frame)
    rainNode_ = scene_->CreateTemporaryChild("Rain");

    // Create particle effect programmatically so we can update wind force dynamically
    rainEffect_ = new ParticleEffect(context_);

    // Material — unlit alpha-blended particle
    auto* rainMat = cache->GetResource<Material>("Materials/RainDrop.xml");
    if (!rainMat)
    {
        // Fallback to smoke texture if custom material missing
        rainMat = cache->GetResource<Material>("Materials/Particle.xml");
    }
    rainEffect_->SetMaterial(rainMat);

    // Emitter shape — wide box above camera
    rainEffect_->SetEmitterType(EMITTER_BOX);
    rainEffect_->SetEmitterSize(Vector3(40.0f, 1.0f, 40.0f));

    // Initial direction — downward with slight randomness
    rainEffect_->SetMinDirection(Vector3(-0.1f, -1.0f, -0.1f));
    rainEffect_->SetMaxDirection(Vector3(0.1f, -1.0f, 0.1f));

    // Fall speed
    rainEffect_->SetMinVelocity(12.0f);
    rainEffect_->SetMaxVelocity(16.0f);

    // Gravity — pulls drops down, wind added per-frame via SetConstantForce
    rainEffect_->SetConstantForce(Vector3(0.0f, -9.8f, 0.0f));
    rainEffect_->SetDampingForce(0.3f);

    // Particle appearance — thin vertical streaks
    rainEffect_->SetMinParticleSize(Vector2(0.015f, 0.15f));
    rainEffect_->SetMaxParticleSize(Vector2(0.025f, 0.25f));

    // Lifetime — enough to fall from emitter to ground
    rainEffect_->SetMinTimeToLive(1.2f);
    rainEffect_->SetMaxTimeToLive(1.8f);

    // World-space particles — don't move with emitter node
    rainEffect_->SetRelative(false);
    rainEffect_->SetUpdateInvisible(true);

    // Direction-aligned stretching — streaks align to velocity
    rainEffect_->SetFaceCameraMode(FC_DIRECTION);

    // Start with zero emission — turned on by precipitation
    rainEffect_->SetMinEmissionRate(0.0f);
    rainEffect_->SetMaxEmissionRate(0.0f);

    // Particle pool
    rainEffect_->SetNumParticles(800);

    // Color — blue-white translucent, fading out over lifetime
    rainEffect_->SetNumColorFrames(2);
    rainEffect_->SetColorFrame(0, ColorFrame(Color(0.7f, 0.75f, 0.85f, 0.4f), 0.0f));
    rainEffect_->SetColorFrame(1, ColorFrame(Color(0.7f, 0.75f, 0.85f, 0.0f), 1.0f));

    // Create emitter component
    rainEmitter_ = rainNode_->CreateComponent<ParticleEmitter>();
    rainEmitter_->SetEffect(rainEffect_);
    rainEmitter_->SetEmitting(false);  // starts off until precipitation > 0.1

    URHO3D_LOGINFO("Rain particle system created");
}

void TerrainNode::UpdateRain(float timeStep)
{
    if (!rainNode_ || !rainEffect_ || !cameraNode_)
        return;

    // Move emitter box above camera
    Vector3 camPos = cameraNode_->GetWorldPosition();
    rainNode_->SetWorldPosition(camPos + Vector3(0.0f, 15.0f, 0.0f));

    // Rain intensity: use override slider if set, otherwise follow precipitation
    float rainIntensity = (rainOverride_ >= 0.0f) ? rainOverride_ : weather_.precipitation;

    // Rain only above freezing — snow takes over below 0C
    float temp = CalculateTemperature();

    // Emission rate driven by rain intensity
    if (rainIntensity < 0.1f || temp < 0.0f)
    {
        if (rainEmitter_->IsEmitting())
            rainEmitter_->SetEmitting(false);
        return;
    }

    if (!rainEmitter_->IsEmitting())
        rainEmitter_->SetEmitting(true);

    // Quadratic ramp: intensity 0.1..1.0 -> 0..800 particles/sec (heavier at top end)
    float t = (rainIntensity - 0.1f) / 0.9f;
    float rate = t * t * 800.0f;
    rainEffect_->SetMinEmissionRate(rate * 0.8f);
    rainEffect_->SetMaxEmissionRate(rate);

    // Wind force — lateral acceleration based on wind speed and angle
    float windMag = weather_.windSpeed * 20.0f;  // 0..20 m/s^2 lateral
    Vector3 force;
    force.x_ = cosf(weather_.windAngle) * windMag;
    force.y_ = -9.8f;  // gravity constant
    force.z_ = sinf(weather_.windAngle) * windMag;
    rainEffect_->SetConstantForce(force);

    // Kill particles that hit terrain or water
    const float waterY = 5.0f;
    Vector<Billboard>& bbs = rainEmitter_->GetBillboards();
    auto* terrain = scene_ ? scene_->GetComponent<Terrain>(true) : nullptr;
    for (int i = 0; i < bbs.Size(); ++i)
    {
        if (!bbs[i].enabled_) continue;

        // Kill at water surface
        if (bbs[i].position_.y_ < waterY)
        { bbs[i].enabled_ = false; continue; }

        // Kill at terrain surface — creates implied ground splash
        if (terrain)
        {
            float groundY = terrain->GetHeight(bbs[i].position_);
            if (bbs[i].position_.y_ <= groundY + 0.1f)
            {
                bbs[i].enabled_ = false;

                // Feed ripple system at impact point when near water
                if (rippleSystem_ && groundY < waterY + 2.0f && rainIntensity > 0.3f)
                    rippleSystem_->SplashAt(bbs[i].position_.x_, bbs[i].position_.z_, 0.02f);
            }
        }
    }
    rainEmitter_->Commit();
}

// ============================================================================
// Snow Particles
// ============================================================================

void TerrainNode::CreateSnow()
{
    auto* cache = GetSubsystem<ResourceCache>();

    snowNode_ = scene_->CreateTemporaryChild("Snow");

    snowEffect_ = new ParticleEffect(context_);

    // Material — reuse particle material (white flake)
    auto* snowMat = cache->GetResource<Material>("Materials/Particle.xml");
    snowEffect_->SetMaterial(snowMat);

    // Emitter shape — wide box above camera
    snowEffect_->SetEmitterType(EMITTER_BOX);
    snowEffect_->SetEmitterSize(Vector3(40.0f, 2.0f, 40.0f));

    // Slow downward drift with lateral wander
    snowEffect_->SetMinDirection(Vector3(-0.3f, -1.0f, -0.3f));
    snowEffect_->SetMaxDirection(Vector3(0.3f, -1.0f, 0.3f));

    // Slow fall speed
    snowEffect_->SetMinVelocity(1.5f);
    snowEffect_->SetMaxVelocity(3.0f);

    // Light gravity, some damping for floaty feel
    snowEffect_->SetConstantForce(Vector3(0.0f, -1.5f, 0.0f));
    snowEffect_->SetDampingForce(0.5f);

    // Flake appearance — small, roughly square
    snowEffect_->SetMinParticleSize(Vector2(0.04f, 0.04f));
    snowEffect_->SetMaxParticleSize(Vector2(0.07f, 0.07f));

    // Long lifetime — slow fall
    snowEffect_->SetMinTimeToLive(3.0f);
    snowEffect_->SetMaxTimeToLive(5.0f);

    // World-space particles
    snowEffect_->SetRelative(false);
    snowEffect_->SetUpdateInvisible(true);

    // Tumbling rotation
    snowEffect_->SetFaceCameraMode(FC_ROTATE_XYZ);
    snowEffect_->SetMinRotationSpeed(-90.0f);
    snowEffect_->SetMaxRotationSpeed(90.0f);

    // Start with zero emission
    snowEffect_->SetMinEmissionRate(0.0f);
    snowEffect_->SetMaxEmissionRate(0.0f);

    // Particle pool
    snowEffect_->SetNumParticles(400);

    // Color — white, slight blue tint, fading out
    snowEffect_->SetNumColorFrames(2);
    snowEffect_->SetColorFrame(0, ColorFrame(Color(0.9f, 0.92f, 1.0f, 0.6f), 0.0f));
    snowEffect_->SetColorFrame(1, ColorFrame(Color(0.9f, 0.92f, 1.0f, 0.0f), 1.0f));

    snowEmitter_ = snowNode_->CreateComponent<ParticleEmitter>();
    snowEmitter_->SetEffect(snowEffect_);
    snowEmitter_->SetEmitting(false);

    URHO3D_LOGINFO("Snow particle system created");
}

void TerrainNode::UpdateCachedClimate()
{
    if (!gameDB_)
        return;

    // Determine current season string from dayOfYear_
    float seasonAngle = fmodf((dayOfYear_ - 81) / 365.0f + 1.0f, 1.0f);
    String season;
    if (seasonAngle < 0.25f)
        season = "spring";
    else if (seasonAngle < 0.5f)
        season = "summer";
    else if (seasonAngle < 0.75f)
        season = "autumn";
    else
        season = "winter";

    gameDB_->GetClimateRules(season, "day", cachedClimateDay_);
    gameDB_->GetClimateRules(season, "night", cachedClimateNight_);
}

float TerrainNode::CalculateTemperature() const
{
    // Interpolate between cached day and night base temps from DB
    float effectiveTime = fmodf(timeOfDay_ + timeOfDayOffset_ + 24.0f, 24.0f);

    // Day/night blend: 0.0 = full night, 1.0 = full day
    float dayFactor;
    if (effectiveTime >= 7.0f && effectiveTime <= 17.0f)
        dayFactor = 1.0f;   // full day
    else if (effectiveTime >= 20.0f || effectiveTime <= 4.0f)
        dayFactor = 0.0f;   // full night
    else if (effectiveTime < 7.0f)
        dayFactor = (effectiveTime - 4.0f) / 3.0f;   // dawn transition 4-7
    else
        dayFactor = 1.0f - (effectiveTime - 17.0f) / 3.0f;  // dusk transition 17-20

    float baseTemp = Lerp(cachedClimateNight_.baseTemp, cachedClimateDay_.baseTemp, dayFactor);

    // Altitude effect: -6C per 100 units above water level (lapse rate)
    float cameraY = cameraNode_ ? cameraNode_->GetWorldPosition().y_ : 5.0f;
    float altAboveWater = Max(cameraY - 5.0f, 0.0f);
    float altEffect = altAboveWater * 0.06f;  // 6C per 100 units

    return baseTemp - altEffect;
}

float TerrainNode::CalculateEffectiveTemperature() const
{
    float temp = CalculateTemperature();

    // Interpolate wind/rain chill between day and night DB values
    float effectiveTime = fmodf(timeOfDay_ + timeOfDayOffset_ + 24.0f, 24.0f);
    float dayFactor;
    if (effectiveTime >= 7.0f && effectiveTime <= 17.0f)
        dayFactor = 1.0f;
    else if (effectiveTime >= 20.0f || effectiveTime <= 4.0f)
        dayFactor = 0.0f;
    else if (effectiveTime < 7.0f)
        dayFactor = (effectiveTime - 4.0f) / 3.0f;
    else
        dayFactor = 1.0f - (effectiveTime - 17.0f) / 3.0f;

    float windChillMax = Lerp(cachedClimateNight_.windChill, cachedClimateDay_.windChill, dayFactor);
    float rainChillMax = Lerp(cachedClimateNight_.rainChill, cachedClimateDay_.rainChill, dayFactor);

    // Wind chill: wind speed reduces perceived temperature
    temp -= weather_.windSpeed * windChillMax;

    // Rain chill: being in rain is cold
    if (weather_.precipitation > 0.1f)
    {
        float rainChill = (weather_.precipitation - 0.1f) / 0.9f * rainChillMax;
        temp -= rainChill;
    }

    // Cloud cover reduces solar warming during day (proportional to day wind chill as proxy)
    temp -= weather_.cloudCover * cachedClimateDay_.windChill;

    return temp;
}

void TerrainNode::UpdateSnow(float timeStep)
{
    if (!snowNode_ || !snowEffect_ || !cameraNode_)
        return;

    // Move emitter above camera
    Vector3 camPos = cameraNode_->GetWorldPosition();
    snowNode_->SetWorldPosition(camPos + Vector3(0.0f, 12.0f, 0.0f));

    // Snow intensity: use precipitation but only when cold
    float temp = CalculateTemperature();
    float snowIntensity = (rainOverride_ >= 0.0f) ? rainOverride_ : weather_.precipitation;

    // Snow only below freezing
    if (temp >= 0.0f || snowIntensity < 0.1f)
    {
        if (snowEmitter_->IsEmitting())
            snowEmitter_->SetEmitting(false);
        return;
    }

    if (!snowEmitter_->IsEmitting())
        snowEmitter_->SetEmitting(true);

    // Quadratic ramp: intensity 0.1..1.0 -> 0..400 particles/sec
    float t = (snowIntensity - 0.1f) / 0.9f;
    float rate = t * t * 400.0f;
    snowEffect_->SetMinEmissionRate(rate * 0.8f);
    snowEffect_->SetMaxEmissionRate(rate);

    // Wind force — gentler than rain (flakes have more air resistance)
    float windMag = weather_.windSpeed * 8.0f;
    Vector3 force;
    force.x_ = cosf(weather_.windAngle) * windMag;
    force.y_ = -1.5f;
    force.z_ = sinf(weather_.windAngle) * windMag;
    snowEffect_->SetConstantForce(force);
}

// ============================================================================
// Campfire
// ============================================================================

void TerrainNode::CreateCampfire()
{
    if (!terrain_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    const float waterY = 5.0f;

    // Scan terrain for a shoreline position: find a spot just above water.
    // Pass 1: strict band (0.5–2.0 above water) for ideal beach placement.
    // Pass 2: wider band (0.5–5.0) if no strict match — catches steeper terrain.
    // Pass 3: any dry land — pick the lowest point above water as fallback.
    Vector3 bestPos;
    bool found = false;

    // Pass 1: ideal shoreline
    for (float x = -200.0f; x < 200.0f && !found; x += 10.0f)
    {
        for (float z = -200.0f; z < 200.0f && !found; z += 10.0f)
        {
            float h = terrain_->GetHeight(Vector3(x, 0.0f, z));
            if (h > waterY + 0.5f && h < waterY + 2.0f)
            {
                bestPos = Vector3(x, h, z);
                found = true;
            }
        }
    }

    // Pass 2: wider band
    if (!found)
    {
        for (float x = -200.0f; x < 200.0f && !found; x += 10.0f)
        {
            for (float z = -200.0f; z < 200.0f && !found; z += 10.0f)
            {
                float h = terrain_->GetHeight(Vector3(x, 0.0f, z));
                if (h > waterY + 0.5f && h < waterY + 5.0f)
                {
                    bestPos = Vector3(x, h, z);
                    found = true;
                }
            }
        }
    }

    // Pass 3: any dry land — pick the lowest point above water
    if (!found)
    {
        float lowestAboveWater = 9999.0f;
        for (float x = -200.0f; x < 200.0f; x += 10.0f)
        {
            for (float z = -200.0f; z < 200.0f; z += 10.0f)
            {
                float h = terrain_->GetHeight(Vector3(x, 0.0f, z));
                if (h > waterY + 0.3f && h < lowestAboveWater)
                {
                    lowestAboveWater = h;
                    bestPos = Vector3(x, h, z);
                    found = true;
                }
            }
        }
        if (found)
            URHO3D_LOGINFOF("CreateCampfire: no shoreline — using lowest dry land at height %.1f", lowestAboveWater);
    }

    if (!found)
    {
        URHO3D_LOGWARNING("CreateCampfire: no dry land found — placing at origin");
        bestPos = Vector3(0.0f, terrain_->GetHeight(Vector3::ZERO), 0.0f);
    }

    // Create campfire node
    campfireNode_ = scene_->CreateChild("Campfire");
    campfireNode_->SetPosition(bestPos);

    // Fire pit base — Bonfire model with wood materials
    Node* pitNode = campfireNode_->CreateChild("Pit");
    auto* pitModel = pitNode->CreateComponent<StaticModel>();
    Model* pitMdl = cache->GetResource<Model>("Models/Buildings/Bonfire.mdl");
    if (!pitMdl) pitMdl = cache->GetResource<Model>("Models/Cylinder.mdl");  // fallback
    pitModel->SetModel(pitMdl);
    pitModel->SetMaterial(0, cache->GetResource<Material>("Materials/Wood.xml"));
    pitModel->SetMaterial(1, cache->GetResource<Material>("Materials/LightWood.xml"));

    // Fire emitter — load existing Fire.xml particle effect
    auto* fireEffect = cache->GetResource<ParticleEffect>("Particle/Fire.xml");
    if (fireEffect)
    {
        Node* fireNode = campfireNode_->CreateChild("Fire");
        campfireFireEmitter_ = fireNode->CreateComponent<ParticleEmitter>();
        campfireFireEmitter_->SetEffect(fireEffect);
        campfireFireEmitter_->SetEmitting(false);  // off until server says fire is lit
        // Capture baseline rates so intensity modulation has a "max" to scale against
        cfBaseFireRateMin_ = fireEffect->GetMinEmissionRate();
        cfBaseFireRateMax_ = fireEffect->GetMaxEmissionRate();
    }

    // Smoke emitter — SmokeStack is continuous (activetime=0), Smoke.xml bursts then stops
    auto* smokeEffect = cache->GetResource<ParticleEffect>("Particle/SmokeStack.xml");
    if (smokeEffect)
    {
        Node* smokeNode = campfireNode_->CreateChild("Smoke");
        smokeNode->SetPosition(Vector3(0.0f, 0.3f, 0.0f));  // slightly above fire
        campfireSmokeEmitter_ = smokeNode->CreateComponent<ParticleEmitter>();
        campfireSmokeEmitter_->SetEffect(smokeEffect);
        campfireSmokeEmitter_->SetEmitting(false);  // off until server says fire is lit
        cfBaseSmokeRateMin_ = smokeEffect->GetMinEmissionRate();
        cfBaseSmokeRateMax_ = smokeEffect->GetMaxEmissionRate();
    }

    // Embers emitter — small glowing particles at ground level, only active in EMBERS state.
    // Pulsates red/yellow to give a distinct visual for the EMBERS window.
    {
        Node* embersNode = campfireNode_->CreateChild("Embers");
        embersNode->SetPosition(Vector3(0.0f, 0.05f, 0.0f));
        campfireEmbersEmitter_ = embersNode->CreateComponent<ParticleEmitter>();
        // Clone the fire effect and reconfigure for embers
        SharedPtr<ParticleEffect> embersEffect = fireEffect ? fireEffect->Clone() : cache->GetResource<ParticleEffect>("Particle/Fire.xml")->Clone();
        if (embersEffect)
        {
            embersEffect->SetMinEmissionRate(3.0f);
            embersEffect->SetMaxEmissionRate(6.0f);
            embersEffect->SetMinParticleSize(Vector2(0.08f, 0.08f));
            embersEffect->SetMaxParticleSize(Vector2(0.15f, 0.15f));
            embersEffect->SetMinTimeToLive(0.5f);
            embersEffect->SetMaxTimeToLive(1.2f);
            embersEffect->SetMinVelocity(0.1f);
            embersEffect->SetMaxVelocity(0.3f);
            embersEffect->SetMinDirection(Vector3(-0.3f, 0.5f, -0.3f));
            embersEffect->SetMaxDirection(Vector3(0.3f, 1.0f, 0.3f));
            embersEffect->SetConstantForce(Vector3(0.0f, 0.05f, 0.0f));
            // Start color: bright orange-red → End color: dim red, fades out
            embersEffect->SetNumColorFrames(2);
            embersEffect->SetColorFrame(0, ColorFrame(Color(1.0f, 0.3f, 0.05f, 0.9f), 0.0f));
            embersEffect->SetColorFrame(1, ColorFrame(Color(0.6f, 0.1f, 0.0f, 0.0f), 1.0f));
            campfireEmbersEmitter_->SetEffect(embersEffect);
        }
        campfireEmbersEmitter_->SetEmitting(false);  // off until EMBERS state
    }

    // Point light for fire glow — pulsates and shifts color
    Node* lightNode = campfireNode_->CreateChild("FireLight");
    lightNode->SetPosition(Vector3(0.0f, 1.0f, 0.0f));
    campfireLight_ = lightNode->CreateComponent<Light>();
    campfireLight_->SetLightType(LIGHT_POINT);
    campfireLight_->SetRange(5.0f);
    campfireLight_->SetColor(Color(1.0f, 0.6f, 0.2f));
    campfireLight_->SetBrightness(1.5f);
    campfireLight_->SetCastShadows(true);
    cfBaseLightBrightness_ = 1.5f;
    cfBaseLightRange_ = 5.0f;
    fireOut_ = false;
    // Seed the flicker state
    fireBrightnessTarget_ = cfBaseLightBrightness_ * Random(0.55f, 1.20f);
    fireBrightnessCurrent_ = fireBrightnessTarget_;
    fireColorTarget_ = Color(1.0f, Random(0.3f, 0.6f), Random(0.0f, 0.2f));
    fireFadeTime_ = Random(0.2f, 0.5f);
    fireFadeTimer_ = 0.0f;

    URHO3D_LOGINFOF("Campfire placed at (%.1f, %.1f, %.1f) near shoreline",
        bestPos.x_, bestPos.y_, bestPos.z_);
}

void TerrainNode::SyncCampfireUI()
{
    // Helper: set a slider value by ID (fires the change event to update label)
    auto setSlider = [&](int id, float val)
    {
        auto it = campfireSliders_.Find(id);
        if (it != campfireSliders_.End() && it->second_)
            it->second_->SetValue(val);
    };

    // Fire emitter
    if (campfireFireEmitter_)
    {
        auto* fe = campfireFireEmitter_->GetEffect();
        if (fe)
        {
            setSlider(50, (fe->GetMinEmissionRate() + fe->GetMaxEmissionRate()) * 0.5f);  // fire rate
            setSlider(51, fe->GetMaxParticleSize().x_);  // fire size
            setSlider(56, (fe->GetMinVelocity() + fe->GetMaxVelocity()) * 0.5f);  // velocity
            setSlider(57, fe->GetConstantForce().y_);  // updraft
            setSlider(58, fe->GetMinTimeToLive());  // lifetime

            // Color frames
            if (fe->GetNumColorFrames() >= 3)
            {
                for (int f = 0; f < 3; ++f)
                {
                    const ColorFrame* cf = fe->GetColorFrame(f);
                    if (cf)
                    {
                        int base = 60 + f * 4;
                        setSlider(base, cf->color_.r_);
                        setSlider(base + 1, cf->color_.g_);
                        setSlider(base + 2, cf->color_.b_);
                        setSlider(base + 3, cf->color_.a_);
                    }
                }
            }
        }
    }

    // Smoke emitter
    if (campfireSmokeEmitter_)
    {
        auto* se = campfireSmokeEmitter_->GetEffect();
        if (se)
        {
            setSlider(52, (se->GetMinEmissionRate() + se->GetMaxEmissionRate()) * 0.5f);  // smoke rate
            setSlider(53, se->GetMaxParticleSize().x_);  // smoke size
            setSlider(75, se->GetEmitterSize().x_);  // emitter radius
            setSlider(76, se->GetSizeMul());  // grow multiplier
            setSlider(77, se->GetMinTimeToLive());  // lifetime
            setSlider(78, se->GetConstantForce().y_);  // rise
            setSlider(79, se->GetDampingForce());  // damping
        }
    }

    // Light
    if (campfireLight_)
    {
        setSlider(54, campfireLight_->GetRange());
        setSlider(55, campfireLight_->GetBrightness());
    }
}

void TerrainNode::HandlePitStateChanged(StringHash, VariantMap& eventData)
{
    // Phase 3: server pit state arrived. Adopt the pit nearest our local Campfire
    // node (Sample 60 only has one campfire — multi-pit comes with Phase 4 ignition).
    if (!campfireNode_)
        return;

    unsigned pitId    = eventData[P_PIT_ID].GetU32();
    unsigned state    = eventData[P_PIT_STATE].GetU32();
    float burnUnits   = eventData[P_PIT_BURN_UNITS].GetFloat();
    float burnRate    = eventData[P_PIT_BURN_RATE].GetFloat();
    float wetness     = eventData[P_PIT_WETNESS].GetFloat();
    float posX        = eventData[P_PIT_POS_X].GetFloat();
    float posZ        = eventData[P_PIT_POS_Z].GetFloat();
    long long utcMs   = (long long)eventData[P_PIT_UTC_MS].GetI64();

    // Adopt the nearest pit within range. If a closer pit broadcasts, switch to it.
    // This handles multi-pit scenarios where the player builds a second Stone Ring.
    Node* charNode = characterNode_ ? characterNode_ : campfireNode_;
    Vector3 ref = charNode->GetWorldPosition();
    float dist = Sqrt((posX - ref.x_) * (posX - ref.x_) + (posZ - ref.z_) * (posZ - ref.z_));
    if (dist < 25.0f && (activeFirePitId_ == 0 || pitId == activeFirePitId_ || dist < activeFirePitDist_))
    {
        activeFirePitId_ = pitId;
        activeFirePitDist_ = dist;
    }
    if (pitId != activeFirePitId_)
        return; // Not the nearest pit

    // Extrapolate forward from the broadcast's UTC timestamp.
    long long nowMs = (long long)time(nullptr) * 1000LL;
    float deltaSec = (float)(nowMs - utcMs) / 1000.0f;
    if (deltaSec < 0.0f) deltaSec = 0.0f;
    if (state == 1 /* PIT_LIT */ || state == 2 /* PIT_EMBERS */)
    {
        burnUnits -= burnRate * deltaSec;
        if (burnUnits < 0.0f) burnUnits = 0.0f;
    }

    // Sync max fuel from server so burn curve fuelRatio matches
    float serverMax = eventData[P_PIT_MAX_FUEL].GetFloat();
    if (serverMax > 0.0f)
        maxFuelSeconds_ = serverMax;

    fuelSeconds_ = Min(burnUnits, maxFuelSeconds_);
    activeFirePitBurnRate_ = burnRate;
    activeFirePitWetness_ = wetness;
    activeFirePitState_ = (unsigned char)state;
    // PIT_COLD or PIT_UNLIT — fire is out. Release adoption so player can auto-switch
    // to a nearby active pit on next broadcast.
    if (state == 0 /* PIT_UNLIT */ || state == 3 /* PIT_COLD */)
    {
        fuelSeconds_ = 0.0f;
        activeFirePitId_ = 0;
        activeFirePitDist_ = 999.0f;
    }

    // Phase 24: Signal fire state
    unsigned signalType = eventData.Contains(P_PIT_SIGNAL) ? eventData[P_PIT_SIGNAL].GetU32() : 0;
    if (signalType > 0 && activeSignalType_ == 0)
    {
        // Signal just activated — show HUD alert
        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (ui && cache)
        {
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                auto* text = ui->GetRoot()->CreateChild<Text>();
                text->SetFont(font, 16);
                text->SetText(signalType == 1 ? "DANGER SIGNAL LIT!" : "TRADE SIGNAL LIT!");
                text->SetColor(signalType == 1 ? Color(1.0f, 0.3f, 0.2f) : Color(0.9f, 0.8f, 0.2f));
                text->SetAlignment(HA_CENTER, VA_CENTER);
                text->SetPosition(0, -140);
                text->SetVar("fadeTimer", 4.0f);
            }
        }
        // Spawn fire particle on nearest watchtower
        if (buildingSystem_ && cache)
        {
            Vector3 pitPos(posX, 0.0f, posZ);
            const auto& placed = buildingSystem_->GetPlacedBuildings();
            float bestDist = 50.0f;
            Node* bestTower = nullptr;
            for (unsigned i = 0; i < placed.Size(); ++i)
            {
                if (placed[i].buildingTypeId != 32 || placed[i].node.Expired())
                    continue;
                float d = (placed[i].position - pitPos).Length();
                if (d < bestDist)
                {
                    bestDist = d;
                    bestTower = placed[i].node;
                }
            }
            if (bestTower)
            {
                Node* fireNode = bestTower->CreateChild("SignalFire");
                fireNode->SetPosition(Vector3(0.0f, 5.0f, 0.0f));  // top of watchtower
                auto* fireEffect = cache->GetResource<ParticleEffect>("Particle/Fire.xml");
                if (fireEffect)
                {
                    auto* emitter = fireNode->CreateComponent<ParticleEmitter>();
                    emitter->SetEffect(fireEffect);
                    emitter->SetEmitting(true);
                }
                auto* smokeEffect = cache->GetResource<ParticleEffect>("Particle/Smoke.xml");
                if (smokeEffect)
                {
                    Node* smokeNode = fireNode->CreateChild("SignalSmoke");
                    auto* emitter = smokeNode->CreateComponent<ParticleEmitter>();
                    emitter->SetEffect(smokeEffect);
                    emitter->SetEmitting(true);
                }
                signalFireNode_ = fireNode;
            }
        }
        URHO3D_LOGINFOF("[Signal] Signal fire activated: type=%u at pit %u", signalType, pitId);
    }
    else if (signalType == 0 && activeSignalType_ != 0)
    {
        // Signal expired — remove fire particle
        if (signalFireNode_ && !signalFireNode_.Expired())
        {
            signalFireNode_->Remove();
            signalFireNode_ = nullptr;
        }
        URHO3D_LOGINFOF("[Signal] Signal fire expired at pit %u", pitId);
    }
    activeSignalType_ = (unsigned char)signalType;

    URHO3D_LOGINFOF("PitState pitId=%u state=%u burnUnits=%.0f rate=%.2f signal=%u (extrapolated %.1fs)",
        pitId, state, fuelSeconds_, burnRate, signalType, deltaSec);
}

void TerrainNode::HandlePitIgnitionStatus(StringHash, VariantMap& eventData)
{
    unsigned pitId = eventData[P_PIT_ID].GetU32();
    if (pitId != activeFirePitId_)
        return;

    bool active = eventData[P_PIT_IGNITION_ACTIVE].GetBool();
    float progress = eventData[P_PIT_IGNITION_PROGRESS].GetFloat();

    ignitionActive_ = active;
    ignitionProgress_ = progress;

    if (active)
    {
        URHO3D_LOGINFOF("Ignition started on pit %u", pitId);
        if (hud_)
            hud_->SetContextHint("Fire-making started — stay close!");
    }
    else if (progress >= 1.0f)
    {
        URHO3D_LOGINFOF("Ignition COMPLETE on pit %u", pitId);
        if (hud_)
            hud_->SetContextHint("Fire lit!");
    }
    else
    {
        URHO3D_LOGINFOF("Ignition RUINED on pit %u", pitId);
        if (hud_)
            hud_->SetContextHint("Fire-making failed — wood lost!");
    }
}

// Real-wallclock fuel decay. Drives fireIntensity_, modulates emitters and light.
// Consumables are immune to time scrub — uses raw engine timeStep, never the scaled clock.
// Phase 3: when activeFirePitId_ is set, server is authoritative for the BURN
// RATE — we use it instead of the raw 1.0/sec local rate, but the local decay
// still runs to extrapolate between server broadcasts.
void TerrainNode::UpdateCampfireFuel(float realTimeStep)
{
    if (!campfireNode_)
        return;

    // Decay fuel using the same non-linear burn curve as the server.
    // Server formula: burnRate * curve(fuelRatio) * (1 - 0.7*wetness), burnRate=1.0.
    // Client evaluates the curve per-frame so rate tracks changing fuelRatio.
    float rate;
    if (!burnCurveKey_.points.Empty() && maxFuelSeconds_ > 0.0f && activeFirePitId_ != 0)
    {
        float fuelRatio = fuelSeconds_ / maxFuelSeconds_;
        float curveMultiplier = burnCurveKey_.Evaluate(fuelRatio);
        rate = curveMultiplier * (1.0f - 0.7f * activeFirePitWetness_);
        if (rate < 0.05f) rate = 0.05f;
    }
    else
        rate = (activeFirePitId_ != 0) ? activeFirePitBurnRate_ : 1.0f;

    if (fuelSeconds_ > 0.0f)
    {
        fuelSeconds_ -= rate * realTimeStep;
        if (fuelSeconds_ < 0.0f)
            fuelSeconds_ = 0.0f;
    }

    // Compute intensity from burn curve: roaring fire at full fuel, dim embers when low.
    // The curve gives a smooth visual arc that matches the non-linear burn rate.
    // Below TAPER_WINDOW, apply an additional linear fade so the fire visibly dies.
    const float TAPER_WINDOW = 30.0f;
    float prevIntensity = fireIntensity_;
    if (fuelSeconds_ <= 0.0f)
        fireIntensity_ = 0.0f;
    else if (!burnCurveKey_.points.Empty() && maxFuelSeconds_ > 0.0f)
    {
        float fuelRatio = fuelSeconds_ / maxFuelSeconds_;
        fireIntensity_ = burnCurveKey_.Evaluate(fuelRatio);
        // Final taper in last 30s so the fire visibly gutters out
        if (fuelSeconds_ < TAPER_WINDOW)
            fireIntensity_ *= fuelSeconds_ / TAPER_WINDOW;
    }
    else
    {
        // Fallback: legacy behavior when no curve loaded
        if (fuelSeconds_ >= TAPER_WINDOW)
            fireIntensity_ = 1.0f;
        else
            fireIntensity_ = fuelSeconds_ / TAPER_WINDOW;
    }

    // Apply intensity to fire emitter rates (smoke/fire scale together)
    if (campfireFireEmitter_)
    {
        if (auto* fe = campfireFireEmitter_->GetEffect())
        {
            fe->SetMinEmissionRate(cfBaseFireRateMin_ * fireIntensity_);
            fe->SetMaxEmissionRate(cfBaseFireRateMax_ * fireIntensity_);
        }
        // Hard-stop emitter when fully out, restart on relight
        bool shouldEmit = fireIntensity_ > 0.0f;
        if (campfireFireEmitter_->IsEmitting() != shouldEmit)
            campfireFireEmitter_->SetEmitting(shouldEmit);
    }

    if (campfireSmokeEmitter_)
    {
        if (auto* se = campfireSmokeEmitter_->GetEffect())
        {
            // Smoke lingers slightly past fire-out — use sqrt curve so it tapers slower
            float smokeMul = Sqrt(fireIntensity_);
            se->SetMinEmissionRate(cfBaseSmokeRateMin_ * smokeMul);
            se->SetMaxEmissionRate(cfBaseSmokeRateMax_ * smokeMul);
        }
    }

    // Offline fire state machine — drive activeFirePitState_ locally when no server
    if (activeFirePitId_ == 0)
    {
        if (fuelSeconds_ > 0.0f)
        {
            if (activeFirePitState_ != 1 /* PIT_LIT */)
            {
                activeFirePitState_ = 1;
                fireOut_ = false;
                embersTimeRemaining_ = 0.0f;
                URHO3D_LOGINFO("Campfire (local): fire revived — LIT");
            }
        }
        else if (activeFirePitState_ == 1 /* PIT_LIT */)
        {
            activeFirePitState_ = 2;
            embersTimeRemaining_ = EMBERS_DURATION;
            URHO3D_LOGINFO("Campfire (local): fuel exhausted — EMBERS");
        }
        else if (activeFirePitState_ == 2 /* PIT_EMBERS */)
        {
            embersTimeRemaining_ -= realTimeStep;
            if (embersTimeRemaining_ <= 0.0f)
            {
                embersTimeRemaining_ = 0.0f;
                activeFirePitState_ = 3;
                URHO3D_LOGINFO("Campfire (local): embers died — COLD");
            }
        }
    }

    // Embers emitter — pulsating red/yellow glow only during EMBERS state
    if (campfireEmbersEmitter_)
    {
        bool isEmbers = (activeFirePitState_ == 2 /* PIT_EMBERS */);
        if (campfireEmbersEmitter_->IsEmitting() != isEmbers)
            campfireEmbersEmitter_->SetEmitting(isEmbers);

        if (isEmbers)
        {
            // Pulsate emission rate with a slow sine wave (period ~2s)
            embersPhase_ += realTimeStep * 3.14159f;
            if (embersPhase_ > 6.28318f) embersPhase_ -= 6.28318f;
            float pulse = 0.5f + 0.5f * Sin(embersPhase_ * 57.2958f);  // Sin expects degrees
            if (auto* ee = campfireEmbersEmitter_->GetEffect())
            {
                ee->SetMinEmissionRate(2.0f + pulse * 4.0f);
                ee->SetMaxEmissionRate(4.0f + pulse * 6.0f);
                // Shift color between red and yellow-orange
                Color startCol(1.0f, 0.2f + pulse * 0.4f, 0.05f * pulse, 0.8f + 0.2f * pulse);
                ee->SetColorFrame(0, ColorFrame(startCol, 0.0f));
            }
            // Dim light to embers glow — warm red, low brightness
            if (campfireLight_)
            {
                campfireLight_->SetColor(Color(1.0f, 0.25f + pulse * 0.15f, 0.05f));
                campfireLight_->SetBrightness(0.3f + pulse * 0.2f);
                campfireLight_->SetRange(2.0f + pulse * 0.5f);
            }
        }
    }

    // COLD state — kill the light entirely
    if (activeFirePitState_ == 3 /* PIT_COLD */ && campfireLight_)
    {
        campfireLight_->SetBrightness(0.0f);
        campfireLight_->SetRange(0.0f);
    }

    // Track latched fire-out state for log spam control + future events
    if (!fireOut_ && fireIntensity_ <= 0.0f && activeFirePitState_ != 2 /* not embers */)
    {
        fireOut_ = true;
        URHO3D_LOGINFO("Campfire: fuel exhausted — fire is out");
    }
    else if (fireOut_ && fireIntensity_ > 0.0f)
    {
        fireOut_ = false;
        URHO3D_LOGINFO("Campfire: relit");
    }
}

// P-key: attempt to plant a crop at the player's position.
// Finds the first seed item in inventory, sends MSG_PLANT_CROP to server.
// Server does full validation (terrain, season, water, tool).
void TerrainNode::TryPlantCrop()
{
    if (!characterNode_)
        return;

    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    // Find first seed in inventory (any item registered in crop_types)
    int seedItemId = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (seedItemIds_.Contains(inventory_[i].itemId))
        {
            seedItemId = inventory_[i].itemId;
            break;
        }
    }

    if (seedItemId == 0)
    {
        URHO3D_LOGINFO("[Farming] No seeds in inventory");
        return;
    }

    Vector3 pos = characterNode_->GetWorldPosition();

    VectorBuffer buf;
    buf.WriteI32(seedItemId);
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(pos.y_);
    buf.WriteFloat(pos.z_);
    serverConn->SendMessage(MSG_PLANT_CROP, true, true, buf);

    URHO3D_LOGINFOF("[Farming] Sent MSG_PLANT_CROP: seed=%d at (%.1f,%.1f,%.1f)",
        seedItemId, pos.x_, pos.y_, pos.z_);
}

// E-key: harvest nearest mature crop within 3m.
void TerrainNode::TryHarvestCrop()
{
    if (!characterNode_)
        return;

    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    Vector3 playerPos = characterNode_->GetWorldPosition();
    int nearestCropId = -1;
    float nearestDist = 9.0f;  // 3m squared

    for (auto it = cropNodes_.Begin(); it != cropNodes_.End(); ++it)
    {
        if (!it->second_)
            continue;

        Node* cropNode = it->second_;
        int stage = cropNode->GetVar("GrowthStage").GetI32();
        if (stage < 3)
            continue;  // Not mature

        float distSq = (cropNode->GetWorldPosition() - playerPos).LengthSquared();
        if (distSq < nearestDist)
        {
            nearestDist = distSq;
            nearestCropId = it->first_;
        }
    }

    if (nearestCropId < 0)
        return;  // No mature crops nearby

    VectorBuffer buf;
    buf.WriteI32(nearestCropId);
    serverConn->SendMessage(MSG_HARVEST_CROP, true, true, buf);

    URHO3D_LOGINFOF("[Farming] Sent MSG_HARVEST_CROP: cropId=%d", nearestCropId);
}

// E-key handler when crosshair/proximity targets the campfire pit.
// Phase 4b: send E_PIT_TEND_REQUEST when adopted pit is in EMBERS state and
// player has Softwood. Server validates inventory + proximity + state, applies
// tend, broadcasts new state. Falls back to legacy free-fuel for offline mode
// (no server pit adopted) so single-player testing still works.
void TerrainNode::TryCampfireInteract()
{
    if (!campfireNode_ || !characterNode_)
        return;

    Vector3 playerPos = characterNode_->GetWorldPosition();
    Vector3 firePos = campfireNode_->GetWorldPosition();
    float dist = (playerPos - firePos).Length();
    const float REACH = 3.0f;
    if (dist > REACH)
        return;

    // Phase 4a/4b: server-authoritative path when a pit has been adopted.
    if (activeFirePitId_ != 0)
    {
        // Phase 4a/4c: COLD or UNLIT pit — try torch first (instant), fall back to friction (2hr)
        if (activeFirePitState_ == 0 /* PIT_UNLIT */ || activeFirePitState_ == 3 /* PIT_COLD */)
        {
            // Phase 4c: Burning Torch (109) = instant ignition — always preferred
            bool haveBurningTorch = false;
            for (unsigned i = 0; i < inventory_.Size(); ++i)
            {
                if (inventory_[i].itemId == 109 && inventory_[i].quantity > 0)
                {
                    haveBurningTorch = true;
                    break;
                }
            }
            if (haveBurningTorch)
            {
                auto* network = GetSubsystem<Network>();
                Connection* serverConn = network ? network->GetServerConnection() : nullptr;
                if (!serverConn)
                    return;
                VariantMap data;
                data[P_PIT_ID] = (unsigned)activeFirePitId_;
                serverConn->SendRemoteEvent(E_TORCH_IGNITE_REQUEST, true, data);
                URHO3D_LOGINFOF("Sent torch-ignite request for pit %u", activeFirePitId_);
                return;
            }

            // Phase 4a: friction ignition fallback
            if (ignitionActive_)
            {
                if (hud_)
                    hud_->SetContextHint("Fire-making in progress...");
                return;
            }
            // Need both Softwood (15) and Hardwood (16)
            bool haveSoftwood = false, haveHardwood = false;
            for (unsigned i = 0; i < inventory_.Size(); ++i)
            {
                if (inventory_[i].itemId == 15 && inventory_[i].quantity > 0)
                    haveSoftwood = true;
                if (inventory_[i].itemId == 16 && inventory_[i].quantity > 0)
                    haveHardwood = true;
            }
            if (!haveSoftwood || !haveHardwood)
            {
                if (hud_)
                    hud_->SetContextHint("Need Burning Torch, or Softwood + Hardwood");
                return;
            }
            auto* network = GetSubsystem<Network>();
            Connection* serverConn = network ? network->GetServerConnection() : nullptr;
            if (!serverConn)
                return;
            VariantMap data;
            data[P_PIT_ID] = (unsigned)activeFirePitId_;
            serverConn->SendRemoteEvent(E_PIT_IGNITE_REQUEST, true, data);
            URHO3D_LOGINFOF("Sent friction-ignite request for pit %u", activeFirePitId_);
            return;
        }

        // Phase 4b: EMBERS → cheap revival (Softwood only)
        if (activeFirePitState_ == 2 /* PIT_EMBERS */)
        {
            bool haveSoftwood = false;
            for (unsigned i = 0; i < inventory_.Size(); ++i)
            {
                if (inventory_[i].itemId == 15 && inventory_[i].quantity > 0)
                {
                    haveSoftwood = true;
                    break;
                }
            }
            if (!haveSoftwood)
            {
                if (hud_)
                    hud_->SetContextHint("Need Softwood to revive embers");
                return;
            }
            auto* network = GetSubsystem<Network>();
            Connection* serverConn = network ? network->GetServerConnection() : nullptr;
            if (!serverConn)
                return;
            VariantMap data;
            data[P_PIT_ID] = (unsigned)activeFirePitId_;
            data[P_PIT_TEND_ITEM] = (int)15;
            data[P_PIT_TEND_QTY] = (int)1;
            serverConn->SendRemoteEvent(E_PIT_TEND_REQUEST, true, data);
            URHO3D_LOGINFOF("Sent embers-revival request for pit %u", activeFirePitId_);
            return;
        }

        // PIT_LIT — Phase 4c: if player has unlit Torch (108), light it
        {
            bool haveUnlitTorch = false;
            for (unsigned i = 0; i < inventory_.Size(); ++i)
            {
                if (inventory_[i].itemId == 108 && inventory_[i].quantity > 0)
                {
                    haveUnlitTorch = true;
                    break;
                }
            }
            if (haveUnlitTorch)
            {
                auto* network = GetSubsystem<Network>();
                Connection* serverConn = network ? network->GetServerConnection() : nullptr;
                if (!serverConn)
                    return;
                serverConn->SendRemoteEvent(E_TORCH_LIGHT_REQUEST, true, VariantMap());
                URHO3D_LOGINFO("Sent torch-light request");
                if (hud_)
                    hud_->SetContextHint("Lighting torch...");
                return;
            }
            if (hud_)
                hud_->SetContextHint("Fire is burning");
        }
        return;
    }

    // Offline fallback: no server pit adopted. Local state machine drives transitions.
    if (activeFirePitState_ == 3 /* PIT_COLD */)
    {
        if (hud_)
            hud_->SetContextHint("Fire is cold — need Softwood + Hardwood to relight");
        return;
    }

    if (activeFirePitState_ == 2 /* PIT_EMBERS */)
    {
        // Embers revival — cheap, just softwood
        fuelSeconds_ = Min(fuelSeconds_ + stickBurnSeconds_, maxFuelSeconds_);
        URHO3D_LOGINFOF("Campfire (local): embers revived! +%.0fs fuel (now %.0f / %.0f)",
            stickBurnSeconds_, fuelSeconds_, maxFuelSeconds_);
        return;
    }

    // LIT or UNLIT — add fuel normally
    fuelSeconds_ = Min(fuelSeconds_ + stickBurnSeconds_, maxFuelSeconds_);
    URHO3D_LOGINFOF("Campfire (local): +%.0fs fuel (now %.0f / %.0f)",
        stickBurnSeconds_, fuelSeconds_, maxFuelSeconds_);
}

// ============================================================================
// Fish
// ============================================================================

void TerrainNode::CreateFish()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Query DB creature id=8 for fish stats
    CreatureInfo fishCI;
    bool hasFishDB = false;
    if (gameDB_)
        hasFishDB = gameDB_->GetCreature(8, fishCI);

    String fishModelPath = hasFishDB && !fishCI.model.Empty() ? fishCI.model : "Models/UrhoFish.mdl";
    auto* fishModel = cache->GetResource<Model>(fishModelPath);
    auto* fishMat = cache->GetResource<Material>("Materials/UrhoFish.xml");

    if (!fishModel || !fishMat)
    {
        URHO3D_LOGERROR("Failed to load fish model or material");
        return;
    }
    SharedPtr<Material> baseMat(fishMat);

    SharedPtr<Material> orbitMat(fishMat->Clone());
    orbitMat->SetShaderParameter("WiggleAmplitude", 0.06f);
    orbitMat->SetShaderParameter("WiggleFrequency", 5.0f);

    SharedPtr<Material> stareMat(fishMat->Clone());
    stareMat->SetShaderParameter("WiggleAmplitude", 0.01f);
    stareMat->SetShaderParameter("WiggleFrequency", 0.8f);

    const int NUM_FISH = 50;
    const float waterY = 5.0f;
    const float MIN_DEPTH = 1.0f;
    Terrain* t = terrain_;
    Node* sunLightNode = scene_->GetChild("DirectionalLight", true);

    // Use server-provided spawn points if available (from water body flood-fill).
    // Otherwise fall back to local grid scan (offline mode).
    struct WaterPoint { float x, z, terrainH; };
    Vector<WaterPoint> waterPoints;

    if (!serverFishSpawns_.Empty())
    {
        // Server already did the flood-fill — use its spawn points
        for (unsigned i = 0; i < serverFishSpawns_.Size(); ++i)
        {
            float h = t ? t->GetHeight(Vector3(serverFishSpawns_[i].x, 0.0f, serverFishSpawns_[i].z)) : 0.0f;
            waterPoints.Push({serverFishSpawns_[i].x, serverFishSpawns_[i].z, h});
        }
        URHO3D_LOGINFOF("CreateFish: using %u server-provided spawn points", waterPoints.Size());
    }
    else
    {
        // Fallback: local grid scan
        Vector3 terrainOrigin = t ? t->GetNode()->GetWorldPosition() : Vector3::ZERO;
        Vector3 spacing = t ? t->GetSpacing() : Vector3::ONE;
        IntVector2 numVerts = t ? t->GetNumVertices() : IntVector2::ZERO;
        float terrainW = (float)(numVerts.x_ - 1) * spacing.x_;
        float terrainH = (float)(numVerts.y_ - 1) * spacing.z_;
        float halfW = terrainW * 0.5f;
        float halfH = terrainH * 0.5f;
        const float GRID_STEP = 16.0f;

        for (float gx = terrainOrigin.x_ - halfW; gx < terrainOrigin.x_ + halfW; gx += GRID_STEP)
        {
            for (float gz = terrainOrigin.z_ - halfH; gz < terrainOrigin.z_ + halfH; gz += GRID_STEP)
            {
                float h = t ? t->GetHeight(Vector3(gx, 0.0f, gz)) : 0.0f;
                if (waterY - h >= MIN_DEPTH + 0.5f)
                    waterPoints.Push({gx, gz, h});
            }
        }
        URHO3D_LOGINFOF("CreateFish: %u local grid points found", waterPoints.Size());
    }

    if (waterPoints.Empty())
    {
        URHO3D_LOGWARNING("CreateFish: no underwater points found");
        return;
    }

    // Distribute fish proportionally across all water — stride ensures coverage
    const float stride = (float)waterPoints.Size() / (float)NUM_FISH;

    for (int i = 0; i < NUM_FISH; ++i)
    {
        Node* fishNode = scene_->CreateTemporaryChild("Fish", LOCAL);

        // Strided pick with jitter — ensures coverage of all water areas
        unsigned baseIdx = (unsigned)(i * stride) % waterPoints.Size();
        unsigned jitterRange = Max(1u, (unsigned)(stride * 0.5f));
        unsigned idx = (baseIdx + (unsigned)Random(0, (int)jitterRange)) % waterPoints.Size();
        const WaterPoint& wp = waterPoints[idx];
        float x = wp.x + Random(-8.0f, 8.0f);
        float z = wp.z + Random(-8.0f, 8.0f);
        float groundH = t ? t->GetHeight(Vector3(x, 0.0f, z)) : wp.terrainH;
        float minY = Max(groundH + 0.5f, waterY - 4.0f);
        float maxY = waterY - MIN_DEPTH;
        if (minY > maxY) minY = maxY;
        Vector3 pos(x, Random(minY, maxY), z);
        fishNode->SetPosition(pos);
        fishNode->SetRotation(Quaternion(0.0f, Random(0.0f, 360.0f), 0.0f));
        auto* sm = fishNode->CreateComponent<StaticModel>();
        sm->SetModel(fishModel, true);
        sm->SetMaterial(fishMat);
        sm->SetCastShadows(false);

        // Attach Fish component — handles its own swim AI
        auto* fish = fishNode->CreateComponent<Fish>();
        if (hasFishDB)
        {
            float speed = (float)fishCI.speed * 0.0625f;  // DB speed 8 → 0.5 swim units
            float vr = fishCI.visionRange > 0.0f ? fishCI.visionRange : fishCI.detectionRange * 4.0f;
            fish->InitFromDB(fishCI.id, fishCI.model, speed, fishCI.wanderRadius,
                             vr, fishCI.visionAngle, true);  // big fish = predator
        }
        fish->SetMaterials(baseMat, orbitMat, stareMat);
        fish->SetCameraNode(cameraNode_);
        fish->SetSpatialHash(&fishSpatialHash_);
        if (sunLightNode)
            fish->SetSunLightNode(sunLightNode);
        fishNodes_.Push(WeakPtr<Node>(fishNode));
    }

    URHO3D_LOGINFOF("Spawned %d fish", NUM_FISH);
}

void TerrainNode::CreateSchoolFish()
{
    auto* cache = GetSubsystem<ResourceCache>();

    // Query DB creature id=8 for model path
    CreatureInfo fishCI;
    bool hasFishDB = false;
    if (gameDB_)
        hasFishDB = gameDB_->GetCreature(8, fishCI);

    String fishModelPath = hasFishDB && !fishCI.model.Empty() ? fishCI.model : "Models/UrhoFish.mdl";
    auto* fishModel = cache->GetResource<Model>(fishModelPath);
    auto* fishMat = cache->GetResource<Material>("Materials/UrhoFish.xml");

    if (!fishModel || !fishMat)
        return;

    // Tiny fish material — faster wiggle for smaller bodies
    SharedPtr<Material> schoolMat(fishMat->Clone());
    schoolMat->SetShaderParameter("WiggleAmplitude", 0.03f);
    schoolMat->SetShaderParameter("WiggleFrequency", 8.0f);

    const float waterY = 5.0f;
    const float TINY_SCALE = hasFishDB ? fishCI.desiredSize * 1.5f : 0.3f;
    const int NUM_SCHOOLS = 3;
    const int FISH_PER_SCHOOL = hasFishDB ? Max(fishCI.packSize * 5, 15) : 15;
    Terrain* t = terrain_;
    Node* sunLightNode = scene_->GetChild("DirectionalLight", true);

    // Build deep-water candidate list across the terrain (reuse terrain bounds)
    Vector3 tOrigin = t ? t->GetNode()->GetWorldPosition() : Vector3::ZERO;
    Vector3 tSpacing = t ? t->GetSpacing() : Vector3::ONE;
    IntVector2 tVerts = t ? t->GetNumVertices() : IntVector2::ZERO;
    float tW = (float)(tVerts.x_ - 1) * tSpacing.x_ * 0.5f;
    float tH = (float)(tVerts.y_ - 1) * tSpacing.z_ * 0.5f;

    struct DeepPoint { float x, z, groundH; };
    Vector<DeepPoint> deepPoints;
    for (float gx = tOrigin.x_ - tW; gx < tOrigin.x_ + tW; gx += 16.0f)
    {
        for (float gz = tOrigin.z_ - tH; gz < tOrigin.z_ + tH; gz += 16.0f)
        {
            float h = t ? t->GetHeight(Vector3(gx, 0.0f, gz)) : 0.0f;
            if (waterY - h >= 2.0f)
                deepPoints.Push({gx, gz, h});
        }
    }

    // Distribute schools evenly across all deep water, not random clustering
    const float schoolStride = deepPoints.Empty() ? 1.0f : (float)deepPoints.Size() / (float)NUM_SCHOOLS;

    // Initialize per-school population tracking
    schoolPopStates_.Resize(NUM_SCHOOLS);
    for (int s = 0; s < NUM_SCHOOLS; ++s)
    {
        schoolPopStates_[s].schoolID = (unsigned)s;
        schoolPopStates_[s].currentCount = FISH_PER_SCHOOL;
        schoolPopStates_[s].populationCap = FISH_PER_SCHOOL;  // habitat-driven cap = initial size
        schoolPopStates_[s].totalCaught = 0;
    }

    for (int school = 0; school < NUM_SCHOOLS; ++school)
    {
        // Strided pick — each school gets a different region of deep water
        Vector3 schoolCenter;
        if (!deepPoints.Empty())
        {
            unsigned idx = (unsigned)(school * schoolStride) % deepPoints.Size();
            const DeepPoint& dp = deepPoints[idx];
            schoolCenter = Vector3(dp.x, Lerp(dp.groundH + 0.5f, waterY - 0.5f, 0.5f), dp.z);
        }
        else
            schoolCenter = Vector3(0.0f, waterY - 2.0f, 0.0f);

        // Spawn fish in a tight cluster around school center
        for (int i = 0; i < FISH_PER_SCHOOL; ++i)
        {
            Node* fishNode = scene_->CreateTemporaryChild("SchoolFish", LOCAL);
            Vector3 offset(Random(-2.0f, 2.0f), Random(-0.3f, 0.3f), Random(-2.0f, 2.0f));
            fishNode->SetPosition(schoolCenter + offset);
            fishNode->SetRotation(Quaternion(0.0f, Random(0.0f, 360.0f), 0.0f));
            fishNode->SetScale(TINY_SCALE);

            auto* sm = fishNode->CreateComponent<StaticModel>();
            sm->SetModel(fishModel, true);
            sm->SetMaterial(schoolMat);
            sm->SetCastShadows(false);

            auto* fish = fishNode->CreateComponent<SchoolFish>();
            if (hasFishDB)
            {
                float speed = (float)fishCI.speed * 0.1f;  // DB speed 8 → 0.8 school swim speed
                float vr = fishCI.detectionRange * 3.0f;   // school fish: wider awareness
                fish->InitFromDB(fishCI.id, fishCI.model, speed, fishCI.wanderRadius * 4.0f,
                                 vr, 0.3f, false);  // school fish = prey
            }
            fish->SetSchoolID(school);
            fish->SetCameraNode(cameraNode_);
            fish->SetSpatialHash(&fishSpatialHash_);
            fish->SetSchoolCache(&schoolStateCache_);
            fish->SetPopulationState(&schoolPopStates_[school]);
            if (sunLightNode)
                fish->SetSunLightNode(sunLightNode);
            fishNodes_.Push(WeakPtr<Node>(fishNode));
        }
    }

    URHO3D_LOGINFOF("Spawned %d schools of %d tiny fish each", NUM_SCHOOLS, FISH_PER_SCHOOL);
}

void TerrainNode::RebuildFishSpatialHash()
{
    if (!scene_ || !loggedIn_)
        return;

    fishSpatialHash_.Clear();
    schoolStateCache_.InvalidateAll(++frameNumber_);

    // Use tracked fish node list — avoids iterating all scene children
    for (unsigned i = 0; i < fishNodes_.Size(); ++i)
    {
        Node* n = fishNodes_[i].Get();
        if (!n) continue;
        auto* fish = n->GetComponent<Fish>();
        if (fish)
            fishSpatialHash_.Insert(fish, n->GetWorldPosition());
    }
}

void TerrainNode::RebuildLandAnimalSpatialHash()
{
    if (!scene_ || !loggedIn_)
        return;

    landAnimalHash_.Clear();

    for (unsigned i = 0; i < animalNodes_.Size(); ++i)
    {
        Node* n = animalNodes_[i].Get();
        if (!n) continue;
        auto* animal = n->GetComponent<LandAnimal>();
        if (animal)
            landAnimalHash_.Insert(animal, n->GetWorldPosition());
    }
}

// ============================================================================
// Animals
// ============================================================================

// Spawn table — built dynamically from GameDB::GetCreaturesByHabitat()
// at InitGameDB time. Maps species name → model path, material list,
// creature DB ID, and a fallback default count for habitat-rule fallback.
namespace
{
    struct SpawnEntry {
        String name;
        String modelPath;
        String matList;
        int creatureId;
        int defaultCount;
    };

    static Vector<SpawnEntry> gSpawnTable;

    static const SpawnEntry* FindSpawnEntryByCreatureId(int creatureId)
    {
        for (const SpawnEntry& e : gSpawnTable)
            if (e.creatureId == creatureId)
                return &e;
        return nullptr;
    }

    static const SpawnEntry* FindSpawnEntryByName(const String& name)
    {
        for (const SpawnEntry& e : gSpawnTable)
            if (e.name == name)
                return &e;
        return nullptr;
    }
}

void TerrainNode::CreateAnimals()
{
    if (!terrain_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    const float waterLevel = 5.5f;

    // --- Phase 2: Terrain-wide grid sampling ---
    // Build a grid of candidate spawn points classified by biome.
    Vector3 spacing = terrain_->GetSpacing();
    IntVector2 numVerts = terrain_->GetNumVertices();
    float terrainW = (float)(numVerts.x_ - 1) * spacing.x_;
    float terrainH = (float)(numVerts.y_ - 1) * spacing.z_;
    Vector3 terrainOrigin = terrain_->GetNode()->GetWorldPosition();
    float halfW = terrainW * 0.5f;
    float halfH = terrainH * 0.5f;

    // Sample every 16 world units — ~160 points for a 2K terrain
    const float GRID_STEP = 16.0f;

    struct GridPoint {
        Vector3 pos;
        HabitatBiome biome;
        float waterDist;
    };
    Vector<GridPoint> gridPoints;

    for (float gx = terrainOrigin.x_ - halfW; gx < terrainOrigin.x_ + halfW; gx += GRID_STEP)
    {
        for (float gz = terrainOrigin.z_ - halfH; gz < terrainOrigin.z_ + halfH; gz += GRID_STEP)
        {
            Vector3 worldPos(gx, 0.0f, gz);
            float h = terrain_->GetHeight(worldPos);
            worldPos.y_ = h;

            BiomeType bt = ClassifyTerrain(worldPos);
            HabitatBiome hb = HabitatRules::FromBiomeType((int)bt);

            // Look up precomputed water distance (Phase 4)
            float waterDist = SampleWaterDistance(gx, gz);
            if (waterDist < 0.0f)
                waterDist = (h > waterLevel) ? 999.0f : 0.0f;

            GridPoint gp;
            gp.pos = worldPos;
            gp.biome = hb;
            gp.waterDist = waterDist;
            gridPoints.Push(gp);
        }
    }

    URHO3D_LOGINFOF("CreateAnimals: sampled %u grid points across %.0fx%.0f terrain",
        gridPoints.Size(), terrainW, terrainH);

    unsigned totalSpawned = 0;

    for (const auto& entry : gSpawnTable)
    {
        // Determine target count: habitat rule density > PopulationManager > hardcoded default
        int count = entry.defaultCount;
        const HabitatRule* rule = habitatRules_.GetRule(entry.name);
        if (rule)
            count = rule->density;

        // PopulationManager override if available
        Vector3 camPos = cameraNode_ ? cameraNode_->GetPosition() : Vector3::ZERO;
        int regionId = (popManager_ && popManager_->IsReady()) ? popManager_->FindRegion(camPos.x_, camPos.z_) : -1;
        if (regionId >= 0)
        {
            int pop = popManager_->GetPopulation(regionId, entry.creatureId);
            if (pop > 0)
                count = pop;
        }

        // Collect valid spawn candidates for this species
        Vector<unsigned> validIndices;
        if (rule)
        {
            for (unsigned i = 0; i < gridPoints.Size(); ++i)
            {
                const GridPoint& gp = gridPoints[i];
                if (HabitatRules::IsValidSpawnPoint(*rule, gp.biome, gp.pos.y_, gp.waterDist))
                    validIndices.Push(i);
            }
        }
        else
        {
            // No rule — accept any point above water (legacy behavior)
            for (unsigned i = 0; i < gridPoints.Size(); ++i)
            {
                if (gridPoints[i].pos.y_ > waterLevel)
                    validIndices.Push(i);
            }
        }

        if (validIndices.Empty())
        {
            URHO3D_LOGWARNINGF("CreateAnimals: no valid habitat for %s, skipping", entry.name.CString());
            continue;
        }

        // Determine group size
        int gsMin = rule ? rule->groupSizeMin : 1;
        int gsMax = rule ? rule->groupSizeMax : 1;

        // Place groups until we reach target count
        int spawned = 0;
        while (spawned < count)
        {
            // Pick a random valid grid point as group center
            unsigned idx = validIndices[Random((int)validIndices.Size())];
            const GridPoint& center = gridPoints[idx];

            int groupSize = (gsMin == gsMax) ? gsMin : (gsMin + Random(gsMax - gsMin + 1));
            groupSize = Min(groupSize, count - spawned);

            for (int g = 0; g < groupSize; ++g)
            {
                // Jitter within 8m of the group center, but re-validate the
                // habitat rule at the jittered point — the grid validation only
                // confirms the centre, and ±8m can cross a riverbank straight
                // into a shallow spit. Resample up to 8 times before falling
                // back to the (already-validated) centre.
                float jx = center.pos.x_;
                float jz = center.pos.z_;
                float jy = center.pos.y_;
                bool jitterOk = false;
                for (int attempt = 0; attempt < 8; ++attempt)
                {
                    float tx = center.pos.x_ + Random(-8.0f, 8.0f);
                    float tz = center.pos.z_ + Random(-8.0f, 8.0f);
                    float ty = terrain_->GetHeight(Vector3(tx, 0.0f, tz));
                    if (ty < waterLevel)
                        continue;

                    if (rule)
                    {
                        float twd = SampleWaterDistance(tx, tz);
                        if (twd < 0.0f)
                            twd = (ty > waterLevel) ? 999.0f : 0.0f;
                        // Reuse the centre's biome — biome doesn't change much
                        // over an 8 m radius and reclassifying every attempt is
                        // expensive in this hot path.
                        if (!HabitatRules::IsValidSpawnPoint(*rule, center.biome, ty, twd))
                            continue;
                    }

                    jx = tx;
                    jz = tz;
                    jy = ty;
                    jitterOk = true;
                    break;
                }
                (void)jitterOk; // fallback to centre is fine if all attempts failed

                Vector3 pos(jx, jy, jz);

                // SpawnCreatureAt handles all node/model/component creation,
                // applies habitat-rule + GameDB combat stats, pushes to
                // animalNodes_. Returns nullptr only on unknown creatureId
                // or missing scene — neither possible inside this loop.
                LandAnimal* spawned_a = SpawnCreatureAt(entry.creatureId, pos);
                if (spawned_a)
                    URHO3D_LOGINFOF("  spawn %s at (%.1f, %.1f, %.1f)", entry.name.CString(), pos.x_, pos.y_, pos.z_);
                ++spawned;
            }
        }

        URHO3D_LOGINFOF("CreateAnimals: %s — %d spawned (%u valid grid points)",
            entry.name.CString(), spawned, validIndices.Size());
        totalSpawned += spawned;
    }

    URHO3D_LOGINFOF("CreateAnimals: %u total animals across %u species",
        totalSpawned, gSpawnTable.Size());

    // Place ALL cavepeople near the campfire — supports multiple NPCs per camp
    Node* campfire = scene_->GetChild("Campfire", true);
    if (campfire && terrain_)
    {
        Vector3 firePos = campfire->GetPosition();
        // Find the inland direction (highest neighbouring terrain)
        Vector3 bestDir;
        float bestH = -999.0f;
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dz = -1; dz <= 1; ++dz)
            {
                if (dx == 0 && dz == 0) continue;
                Vector3 probe = firePos + Vector3((float)dx * 6.0f, 0.0f, (float)dz * 6.0f);
                float h = terrain_->GetHeight(probe);
                if (h > bestH) { bestH = h; bestDir = Vector3((float)dx, 0.0f, (float)dz).Normalized(); }
            }
        }

        // Gather all caveman/cavewoman nodes and place them in a semicircle around the fire
        Vector<Node*> caveNodes;
        const Vector<SharedPtr<Node>>& children = scene_->GetChildren();
        for (unsigned i = 0; i < children.Size(); i++)
        {
            const String& name = children[i]->GetName();
            if (name == "CaveMan" || name == "CaveWoman")
                caveNodes.Push(children[i]);
        }

        float baseAngle = Atan2(bestDir.z_, bestDir.x_);
        for (unsigned i = 0; i < caveNodes.Size(); i++)
        {
            // Spread NPCs in a semicircle around the campfire, inland side
            float angle = baseAngle + (float)i * 40.0f - (float)(caveNodes.Size() - 1) * 20.0f;
            float dist = 3.5f + Random(0.0f, 2.0f);
            Vector3 npcPos = firePos + Vector3(Cos(angle) * dist, 0.0f, Sin(angle) * dist);
            npcPos.y_ = terrain_->GetHeight(npcPos);

            caveNodes[i]->SetPosition(npcPos);
            auto* creature = caveNodes[i]->GetComponent<Creature>();
            if (creature) creature->SetHomePosition(npcPos);
            auto* npc = caveNodes[i]->GetComponent<HumanNPC>();
            if (npc)
            {
                npc->SetCampfireNode(campfire);
                if (cameraNode_)
                    npc->SetCameraNode(cameraNode_);
                if (resourceMap_)
                    npc->SetResourceMap(resourceMap_);
            }
        }

        URHO3D_LOGINFOF("Placed %u cavepeople near campfire at (%.1f, %.1f, %.1f)",
            caveNodes.Size(), firePos.x_, firePos.y_, firePos.z_);
    }
}

// ============================================================================
// Loose Resources — stones, sticks, fiber, berries, flint
// ============================================================================

void TerrainNode::CreateResourceMap()
{
    if (!terrain_ || !scene_)
        return;

    resourceMap_ = new ResourceMap(context_);
    ResourceMap::RegisterObject(context_);

    const float waterLevel = 5.0f;
    resourceMap_->Generate(terrain_, ecosystem_, waterLevel, gameDB_);

    URHO3D_LOGINFOF("ResourceMap: %u resource pixels generated", resourceMap_->GetResourceCount());
}

void TerrainNode::UpdateResourceStreaming()
{
    if (!resourceMap_ || !terrain_ || !scene_ || !cameraNode_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    Vector3 camPos = cameraNode_->GetWorldPosition();
    const float SPAWN_RADIUS = 150.0f;
    const float DESPAWN_RADIUS = 180.0f;
    const float SPAWN_RADIUS_SQ = SPAWN_RADIUS * SPAWN_RADIUS;
    const float DESPAWN_RADIUS_SQ = DESPAWN_RADIUS * DESPAWN_RADIUS;

    // Despawn nodes beyond radius
    Vector<unsigned> toRemove;
    for (auto it = activePickupNodes_.Begin(); it != activePickupNodes_.End(); ++it)
    {
        Node* node = it->second_;
        if (!node)
        {
            toRemove.Push(it->first_);
            continue;
        }
        Vector3 diff = node->GetWorldPosition() - camPos;
        diff.y_ = 0.f;
        if (diff.LengthSquared() > DESPAWN_RADIUS_SQ)
        {
            node->Remove();
            toRemove.Push(it->first_);
        }
    }
    for (unsigned key : toRemove)
        activePickupNodes_.Erase(key);

    // Spawn new nodes within radius — scan resource map pixels in camera area
    // Convert camera world pos to pixel bounds
    const int MAP_SIZE = ResourceMap::MAP_SIZE;
    float pixelsPerMeterX = (float)MAP_SIZE / (resourceMap_->GetImage() ? 1.f : 1.f);

    // Use terrain bounds from the resourceMap's image
    Image* img = resourceMap_->GetImage();
    if (!img)
        return;

    // Get terrain extents from the resource map's coordinate system
    Vector3 spacing = terrain_->GetSpacing();
    IntVector2 numVerts = terrain_->GetNumVertices();
    float terrainSizeX = (float)(numVerts.x_ - 1) * spacing.x_;
    float terrainSizeZ = (float)(numVerts.y_ - 1) * spacing.z_;
    float originX = -terrainSizeX * 0.5f;
    float originZ = -terrainSizeZ * 0.5f;

    // Pixel bounds for the spawn region
    float minWX = camPos.x_ - SPAWN_RADIUS;
    float maxWX = camPos.x_ + SPAWN_RADIUS;
    float minWZ = camPos.z_ - SPAWN_RADIUS;
    float maxWZ = camPos.z_ + SPAWN_RADIUS;

    int minPx = Clamp((int)(((minWX - originX) / terrainSizeX) * MAP_SIZE), 0, MAP_SIZE - 1);
    int maxPx = Clamp((int)(((maxWX - originX) / terrainSizeX) * MAP_SIZE), 0, MAP_SIZE - 1);
    int minPz = Clamp((int)(((minWZ - originZ) / terrainSizeZ) * MAP_SIZE), 0, MAP_SIZE - 1);
    int maxPz = Clamp((int)(((maxWZ - originZ) / terrainSizeZ) * MAP_SIZE), 0, MAP_SIZE - 1);

    // Fallback material/scale per resource type (DB doesn't carry these)
    struct VisualFallback
    {
        const char* modelPath;
        const char* matPath;
        Vector3 scale;
        const char* name;
    };
    static const VisualFallback fallbackVisuals[] = {
        { nullptr, nullptr, Vector3::ZERO, nullptr },                                                                          // 0 = NONE
        { "Models/Pickups/Pebble_Round_1.mdl",  "Materials/_defaultMat.xml",                      Vector3(0.3f, 0.3f, 0.3f),  "Loose Stone" },   // 1 = STONE
        { "Models/Pickups/WoodLog.mdl",         "Materials/Wood.xml",                             Vector3(0.3f, 0.3f, 0.3f),  "Fallen Stick" },  // 2 = STICK
        { "Models/Pickups/Fern_1.mdl",          "Materials/leafsGreen.xml",                       Vector3(0.3f, 0.4f, 0.3f),  "Plant Fiber" },   // 3 = FIBER
        { "Models/Pickups/Pebble_Round_2.mdl",  "Materials/_defaultMat.xml",                      Vector3(0.5f, 0.4f, 0.5f),  "Clay" },          // 4 = CLAY
        { "Models/Pickups/Pebble_Round_3.mdl",  "Materials/_defaultMat.xml",                      Vector3(0.2f, 0.2f, 0.2f),  "Flint" },         // 5 = FLINT
        { "Models/Pickups/BushBerries_1.mdl",   "Materials/Berry.xml",                            Vector3(0.2f, 0.2f, 0.2f),  "Berries" },       // 6 = BERRIES
        { "Models/Pickups/WoodLog.mdl",         "Materials/Wood.xml",                             Vector3(0.4f, 0.4f, 0.4f),  "Log" },           // 7 = LOG
        { "Models/Pickups/Mushroom_Common.mdl", "Materials/Wood.xml",                             Vector3(0.3f, 0.3f, 0.3f),  "Mushroom" },      // 8 = MUSHROOM
        { "Models/Pickups/Fern_1.mdl",          "Materials/leafsGreen.xml",                       Vector3(0.2f, 0.3f, 0.2f),  "Herb" },          // 9 = HERB
        { nullptr, nullptr, Vector3::ZERO, nullptr },                                                                          // 10 = SHELL (placeholder)
        { "Models/Pickups/Grass_Common_Tall.mdl", "Materials/Green.xml",                          Vector3(0.3f, 0.4f, 0.3f),  "Reeds" },         // 11 = REEDS
        { "Models/Pickups/WoodLog.mdl",         "Materials/Wood.xml",                             Vector3(0.35f, 0.35f, 0.35f), "Softwood" },    // 12 = SOFTWOOD
        { "Models/Pickups/WoodLog_Moss.mdl",    "Materials/Wood.xml",                             Vector3(0.45f, 0.45f, 0.45f), "Hardwood" },    // 13 = HARDWOOD
    };
    static const int NUM_VISUALS = sizeof(fallbackVisuals) / sizeof(fallbackVisuals[0]);

    unsigned spawned = 0;
    const unsigned MAX_SPAWN_PER_FRAME = 50;

    for (int pz = minPz; pz <= maxPz && spawned < MAX_SPAWN_PER_FRAME; ++pz)
    {
        for (int px = minPx; px <= maxPx && spawned < MAX_SPAWN_PER_FRAME; ++px)
        {
            // Unique key from pixel coords
            unsigned key = (unsigned)pz * MAP_SIZE + (unsigned)px;
            if (activePickupNodes_.Contains(key))
                continue;

            unsigned pixel = img->GetPixelInt(px, pz);
            unsigned char type = (unsigned char)(pixel & 0xFF);
            if (type == RES_NONE || type >= NUM_VISUALS)
                continue;
            // Skip placeholder slots
            if (!fallbackVisuals[type].modelPath)
                continue;

            unsigned char qty = (unsigned char)((pixel >> 8) & 0xFF);
            unsigned char flags = (unsigned char)((pixel >> 24) & 0xFF);
            if (qty == 0 || (flags & RFLAG_DEPLETED))
                continue;

            // Convert pixel to world position
            float wx = originX + ((float)px + 0.5f) / (float)MAP_SIZE * terrainSizeX;
            float wz = originZ + ((float)pz + 0.5f) / (float)MAP_SIZE * terrainSizeZ;

            // Distance check
            float dx = wx - camPos.x_;
            float dz = wz - camPos.z_;
            if (dx * dx + dz * dz > SPAWN_RADIUS_SQ)
                continue;

            float wy = terrain_->GetHeight(Vector3(wx, 0.f, wz));

            // Resolve model/name from DB gather sources, fall back to static table
            int itemId = ResourceTypeToItemId((ResourceType)type);
            const VisualFallback& fb = fallbackVisuals[type];
            const char* modelPath = fb.modelPath;
            const char* matPath   = fb.matPath;
            Vector3 scale         = fb.scale;
            String  displayName   = fb.name;

            auto it = gatherSourceByItem_.Find(itemId);
            if (it != gatherSourceByItem_.End())
            {
                const GatherSourceInfo& gs = it->second_;

                // Terrain biome filter — skip resources that don't belong in this biome
                BiomeType biome = ClassifyTerrain(Vector3(wx, wy, wz));
                if (!TerrainMatchesSource(gs.terrain, biome))
                    continue;

                displayName = gs.name;
                if (!gs.model.Empty())
                    modelPath = gs.model.CString();
            }

            Model* mdl = cache->GetResource<Model>(modelPath);
            if (!mdl)
                mdl = cache->GetResource<Model>(fb.modelPath);  // fallback to hardcoded
            if (!mdl)
                mdl = cache->GetResource<Model>("Models/Box.mdl");

            Node* node = scene_->CreateTemporaryChild(displayName, LOCAL);
            node->SetPosition(Vector3(wx, wy, wz));

            // Variant-based scale variation
            unsigned char variant = (unsigned char)((pixel >> 16) & 0xFF);
            float sv = 0.8f + (float)variant / 255.f * 0.4f;
            node->SetScale(scale * sv);

            auto* sm = node->CreateComponent<StaticModel>();
            sm->SetModel(mdl);
            Material* mat = cache->GetResource<Material>(matPath);
            if (mat)
                sm->SetMaterial(mat);
            sm->SetCastShadows(false);

            auto* body = node->CreateComponent<RigidBody>();
            body->SetCollisionLayer(2);
            body->SetKinematic(true);
            body->SetMass(0.f);
            auto* shape = node->CreateComponent<CollisionShape>();
            shape->SetSphere(scale.x_ * sv * 2.5f);

            node->SetVar("ItemID", itemId);
            node->SetVar("ItemQty", (int)qty);

            auto* pickup = node->CreateComponent<ResourcePickup>();
            pickup->sourceName_ = displayName;
            pickup->itemId_ = itemId;
            pickup->quantity_ = (int)qty;

            activePickupNodes_[key] = node;
            ++spawned;
        }
    }
}

// ============================================================================
// Pickup interaction — called from left-click raycast
// ============================================================================

void TerrainNode::TryPickupAtCursor(const Ray& pickRay)
{
    if (!scene_)
        return;

    auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
    if (!physicsWorld)
        return;

    Vector<PhysicsRaycastResult> results;
    physicsWorld->Raycast(results, pickRay, 300.f);

    for (unsigned i = 0; i < results.Size(); ++i)
    {
        Node* node = results[i].body_->GetNode();
        if (!node)
            continue;

        auto* pickup = node->GetComponent<ResourcePickup>();
        if (!pickup)
            continue;

        // Range check — character in possession mode, else camera
        Node* refNode = characterNode_ ? characterNode_ : cameraNode_;
        if (refNode && !pickup->IsInRange(refNode))
        {
            URHO3D_LOGINFOF("Too far to pick up %s", pickup->sourceName_.CString());
            return;
        }

        // Send server-authoritative harvest request with world position + resource type
        SendResourceHarvest(node->GetWorldPosition(), pickup->itemId_);
        return;
    }
}

// ============================================================================
// Resource Map — server authority message handlers
// ============================================================================

void TerrainNode::HandleResourceDepleted(MemoryBuffer& msg)
{
    float worldX = msg.ReadFloat();
    float worldZ = msg.ReadFloat();
    unsigned char newQty = msg.ReadU8();
    unsigned char resourceType = msg.ReadU8();

    if (!resourceMap_ || !resourceMap_->GetImage())
        return;

    // Update the local resource map image to match the server's state
    Image* img = resourceMap_->GetImage();
    Vector3 spacing = terrain_->GetSpacing();
    IntVector2 numVerts = terrain_->GetNumVertices();
    float terrainSizeX = (float)(numVerts.x_ - 1) * spacing.x_;
    float terrainSizeZ = (float)(numVerts.y_ - 1) * spacing.z_;
    float originX = -terrainSizeX * 0.5f;
    float originZ = -terrainSizeZ * 0.5f;

    int px = Clamp((int)(((worldX - originX) / terrainSizeX) * (float)ResourceMap::MAP_SIZE), 0, ResourceMap::MAP_SIZE - 1);
    int pz = Clamp((int)(((worldZ - originZ) / terrainSizeZ) * (float)ResourceMap::MAP_SIZE), 0, ResourceMap::MAP_SIZE - 1);

    unsigned pixel = img->GetPixelInt(px, pz);
    unsigned char flags = (unsigned char)((pixel >> 24) & 0xFF);
    unsigned char variant = (unsigned char)((pixel >> 16) & 0xFF);

    if (newQty == 0 && !(flags & RFLAG_RESPAWNABLE))
    {
        // Permanent depletion — clear the pixel entirely
        img->SetPixelInt(px, pz, 0);
    }
    else
    {
        if (newQty == 0)
            flags |= RFLAG_DEPLETED;
        else
            flags &= ~RFLAG_DEPLETED;  // respawn clears depleted

        img->SetPixelInt(px, pz,
            (unsigned)resourceType | ((unsigned)newQty << 8) |
            ((unsigned)variant << 16) | ((unsigned)flags << 24));
    }

    // Remove the active pickup node at this pixel (it'll respawn from streaming if qty > 0)
    unsigned key = (unsigned)pz * ResourceMap::MAP_SIZE + (unsigned)px;
    auto it = activePickupNodes_.Find(key);
    if (it != activePickupNodes_.End())
    {
        if (it->second_)
            it->second_->Remove();
        activePickupNodes_.Erase(it);
    }
}

// ============================================================================
// OOFO fleet
// ============================================================================

void TerrainNode::CreateOOFOs()
{
    auto* cache = GetSubsystem<ResourceCache>();
    oofoCloudPositions_ = OOFO::BuildCloudPositions(cache);

    // Stagger spawns with random delays so OOFOs don't all appear at once
    oofosSpawned_ = 0;
    for (int i = 0; i < NUM_OOFOS; ++i)
        oofoSpawnTimers_[i] = Random(1.0f, 8.0f) + i * Random(2.0f, 5.0f);
}

void TerrainNode::UpdateOOFOs(float timeStep)
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

void TerrainNode::UpdateSeasonalEffects()
{
    if (dayOfYear_ == lastSeasonDay_)
        return;
    lastSeasonDay_ = dayOfYear_;

    // Refresh climate rules from DB for the new season
    UpdateCachedClimate();

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

        if (grassSystem_)
        {
            Color grassSpring(0.5f, 0.85f, 0.35f, 1.0f);
            Color grassSummer(0.75f, 0.8f, 0.3f, 1.0f);
            Color grassAutumn(0.7f, 0.55f, 0.2f, 1.0f);
            Color grassWinter(0.5f, 0.45f, 0.3f, 1.0f);
            Color grassTint;
            if (seasonAngle < 0.25f)
                grassTint = grassSpring.Lerp(grassSummer, seasonAngle / 0.25f);
            else if (seasonAngle < 0.5f)
                grassTint = grassSummer.Lerp(grassAutumn, (seasonAngle - 0.25f) / 0.25f);
            else if (seasonAngle < 0.75f)
                grassTint = grassAutumn.Lerp(grassWinter, (seasonAngle - 0.5f) / 0.25f);
            else
                grassTint = grassWinter.Lerp(grassSpring, (seasonAngle - 0.75f) / 0.25f);
            grassSystem_->SetSeasonalTint(grassTint);
        }

        // Ecosystem seasonal growth multiplier: spring 1.5, summer 1.0, autumn 0.5, winter 0.0
        if (ecosystem_)
        {
            float ecoMult;
            if (seasonAngle < 0.25f)
                ecoMult = Lerp(1.5f, 1.0f, seasonAngle / 0.25f);       // spring → summer
            else if (seasonAngle < 0.5f)
                ecoMult = Lerp(1.0f, 0.5f, (seasonAngle - 0.25f) / 0.25f);  // summer → autumn
            else if (seasonAngle < 0.75f)
                ecoMult = Lerp(0.5f, 0.0f, (seasonAngle - 0.5f) / 0.25f);   // autumn → winter
            else
                ecoMult = Lerp(0.0f, 1.5f, (seasonAngle - 0.75f) / 0.25f);  // winter → spring
            ecosystem_->SetSeasonMultiplier(ecoMult);
        }

        // Forest seasonal color — deciduous trees change, evergreens stay green
        UpdateTreeSeason(seasonAngle);

        // Decor scatter seasonal tint — bushes, flowers, ferns shift with seasons
        UpdateDecorSeason(seasonAngle);
    }

    // Water color (shared material across all water tiles)
    if (waterNode_)
    {
        auto* waterMat = GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/Water.xml");
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

            // Moon specular glistening — direction and intensity
            if (moonNode_ && cachedMoonAlt_ > 0.0f)
            {
                Vector3 moonDir = (moonNode_->GetWorldPosition() - waterNode_->GetWorldPosition()).Normalized();
                waterMat->SetShaderParameter("MoonDir", moonDir);
                waterMat->SetShaderParameter("MoonColor", Vector3(0.5f, 0.55f, 0.8f));
                waterMat->SetShaderParameter("MoonSpecular", moonOcclusionFade_);
            }
            else
            {
                waterMat->SetShaderParameter("MoonSpecular", 0.0f);
            }

            // Sun specular glistening — direction and intensity
            if (sunNode_ && cachedSunAlt_ > 0.0f)
            {
                Vector3 sunDir = (sunNode_->GetWorldPosition() - waterNode_->GetWorldPosition()).Normalized();
                float sunSpec = Clamp((cachedSunAlt_ - 2.0f) / 10.0f, 0.0f, 1.0f);
                waterMat->SetShaderParameter("SunDir", sunDir);
                waterMat->SetShaderParameter("SunColor", Vector3(1.0f, 0.92f, 0.8f));
                waterMat->SetShaderParameter("SunSpecular", sunSpec);
            }
            else
            {
                waterMat->SetShaderParameter("SunSpecular", 0.0f);
            }
        }
    }

    // Seasonal skybox blending
    // seasonAngle: 0=spring equinox, cycles through spring→summer→autumn→winter
    if (skyboxMat_ && seasonSkyboxes_[0])
    {
        float seasonAngle = fmodf((dayOfYear_ - 81) / 365.0f + 1.0f, 1.0f);
        // Map to season indices: 0=spring, 1=summer, 2=autumn, 3=winter
        float seasonPos = seasonAngle * 4.0f;   // 0..4
        int currentSeason = ((int)seasonPos) % 4;
        int nextSeason = (currentSeason + 1) % 4;
        float blend = seasonPos - (int)seasonPos; // 0..1 within current season

        // Only swap textures when the season index changes
        if (currentSeason != lastSeasonIndex_)
        {
            lastSeasonIndex_ = currentSeason;
            skyboxMat_->SetTexture(TU_DIFFUSE, seasonSkyboxes_[currentSeason]);
            skyboxMat_->SetTexture(TU_SPECULAR, seasonSkyboxes_[nextSeason]);
        }
        skyboxMat_->SetShaderParameter("SeasonBlend", blend);

        // Weather skybox override — cloudCover drives which skybox shows
        // 0.0-0.3 = clear, 0.3-0.7 = seasonal (no override), 0.7-0.85 = overcast, 0.85+ = storm
        float cc = weather_.cloudCover;
        if (cc > 0.7f && weatherSkyboxes_[1])
        {
            if (cc > 0.85f && weatherSkyboxes_[2])
            {
                // Storm: blend overcast→storm between 0.85 and 1.0
                float stormBlend = Clamp((cc - 0.85f) / 0.15f, 0.0f, 1.0f);
                skyboxMat_->SetTexture(TU_DIFFUSE, weatherSkyboxes_[1]);
                skyboxMat_->SetTexture(TU_SPECULAR, weatherSkyboxes_[2]);
                skyboxMat_->SetShaderParameter("SeasonBlend", stormBlend);
            }
            else
            {
                // Overcast: blend seasonal→overcast between 0.7 and 0.85
                float overcastBlend = Clamp((cc - 0.7f) / 0.15f, 0.0f, 1.0f);
                skyboxMat_->SetTexture(TU_SPECULAR, weatherSkyboxes_[1]);
                skyboxMat_->SetShaderParameter("SeasonBlend", overcastBlend);
            }
        }
        else if (cc < 0.3f && weatherSkyboxes_[0])
        {
            // Clear: blend seasonal→clear between 0.3 and 0.0
            float clearBlend = Clamp((0.3f - cc) / 0.3f, 0.0f, 1.0f);
            skyboxMat_->SetTexture(TU_SPECULAR, weatherSkyboxes_[0]);
            skyboxMat_->SetShaderParameter("SeasonBlend", clearBlend);
        }
    }
}

// ---------------------------------------------------------------------------
// Drought Client Visuals — derived from weather state, no server message
// ---------------------------------------------------------------------------

void TerrainNode::UpdateDroughtVisuals(float timeStep)
{
    // Drought builds when precipitation is low during warm seasons, decays when rain falls
    float seasonAngle = fmodf((dayOfYear_ - 81) / 365.0f + 1.0f, 1.0f);
    bool warmSeason = (seasonAngle > 0.1f && seasonAngle < 0.6f);  // spring through mid-autumn
    float precip = weather_.precipitation;

    if (warmSeason && precip < 0.05f)
        droughtSeverity_ = Min(1.0f, droughtSeverity_ + 0.002f * timeStep);  // slow buildup
    else if (precip > 0.3f)
        droughtSeverity_ = Max(0.0f, droughtSeverity_ - 0.02f * timeStep);   // rain breaks drought fast
    else
        droughtSeverity_ = Max(0.0f, droughtSeverity_ - 0.005f * timeStep);  // gradual recovery

    if (droughtSeverity_ < 0.01f)
    {
        // No drought — clean up dust if present
        if (dustEmitterNode_)
        {
            dustEmitterNode_->Remove();
            dustEmitterNode_ = nullptr;
        }
        return;
    }

    // --- Terrain tint: shift toward brown/ochre ---
    if (terrain_)
    {
        auto* terrainMat = terrain_->GetMaterial();
        if (terrainMat)
        {
            Color droughtTint(0.85f + 0.15f * droughtSeverity_,
                              0.75f - 0.1f * droughtSeverity_,
                              0.55f - 0.15f * droughtSeverity_);
            Color blended = cachedTerrainTint_.Lerp(droughtTint, droughtSeverity_);
            terrainMat->SetShaderParameter("MatDiffColor", blended);
        }
    }

    // --- Water level recession: up to 1.5m drop at max severity ---
    if (waterNode_)
    {
        float drop = droughtSeverity_ * 1.5f;
        waterNode_->SetPosition(Vector3(0.0f, baseWaterY_ - drop, 0.0f));
    }

    // --- Dust particles at severity > 0.5 ---
    if (droughtSeverity_ > 0.5f && cameraNode_)
    {
        if (!dustEmitterNode_)
        {
            dustEmitterNode_ = scene_->CreateChild("DroughtDust");
            auto* cache = GetSubsystem<ResourceCache>();
            auto* dustEffect = cache->GetResource<ParticleEffect>("Particle/Dust.xml");
            if (!dustEffect)
                dustEffect = cache->GetResource<ParticleEffect>("Particle/Smoke.xml");
            if (dustEffect)
            {
                auto* emitter = dustEmitterNode_->CreateComponent<ParticleEmitter>();
                SharedPtr<ParticleEffect> local = dustEffect->Clone();
                local->SetMinParticleSize(Vector2(0.5f, 0.5f));
                local->SetMaxParticleSize(Vector2(1.5f, 1.5f));
                emitter->SetEffect(local);
            }
        }

        // Follow camera, emit more at higher severity
        dustEmitterNode_->SetWorldPosition(cameraNode_->GetWorldPosition() + Vector3(0, -2, 0));
        auto* emitter = dustEmitterNode_->GetComponent<ParticleEmitter>();
        if (emitter && emitter->GetEffect())
        {
            float intensity = (droughtSeverity_ - 0.5f) * 2.0f;  // 0..1 over severity 0.5..1.0
            emitter->GetEffect()->SetMinEmissionRate(1.0f + intensity * 4.0f);
            emitter->GetEffect()->SetMaxEmissionRate(3.0f + intensity * 8.0f);
        }
    }
    else if (droughtSeverity_ <= 0.5f && dustEmitterNode_)
    {
        dustEmitterNode_->Remove();
        dustEmitterNode_ = nullptr;
    }

    // Drought HUD warning at moderate+ severity
    if (hud_ && droughtSeverity_ > 0.3f && !droughtWarningShown_)
    {
        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (ui && cache)
        {
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                auto* text = ui->GetRoot()->CreateChild<Text>();
                text->SetFont(font, 16);
                text->SetText("Drought setting in — conserve water!");
                text->SetColor(Color(0.9f, 0.6f, 0.2f));
                text->SetAlignment(HA_CENTER, VA_CENTER);
                text->SetPosition(0, -140);
                text->SetVar("fadeTimer", 5.0f);
            }
        }
        droughtWarningShown_ = true;
    }
    if (droughtSeverity_ < 0.1f)
        droughtWarningShown_ = false;
}

void TerrainNode::UpdateAtmosphere(float sunAltitude)
{
    if (!zone_)
        return;

    // Recompute seasonal cache only when dayOfYear_ changes
    UpdateSeasonalEffects();

    Color ambient, fogColor, sunColor, moonColor;
    bool sunEnabled = true;
    bool moonEnabled = false;

    // Multi-stop crepuscular color ramp keyed to sun altitude (degrees)
    // Daylight (>20) → Golden (10-20) → Deep Orange (5-10) → Rose/Magenta (0-5)
    // → Purple (-5 to 0) → Deep Blue (<-5)
    if (sunAltitude > 20.0f)
    {
        // Full daylight
        ambient = Color(0.15f, 0.15f, 0.15f);
        fogColor = Color(0.7f, 0.7f, 0.75f);
        sunColor = Color(1.2f, 1.2f, 1.2f);
    }
    else if (sunAltitude > 10.0f)
    {
        // Daylight → Golden hour
        float t = (sunAltitude - 10.0f) / 10.0f;
        ambient = Color(0.16f, 0.13f, 0.08f).Lerp(Color(0.15f, 0.15f, 0.15f), t);
        fogColor = Color(0.85f, 0.7f, 0.45f).Lerp(Color(0.7f, 0.7f, 0.75f), t);
        sunColor = Color(1.4f, 1.1f, 0.7f).Lerp(Color(1.2f, 1.2f, 1.2f), t);
    }
    else if (sunAltitude > 5.0f)
    {
        // Golden → Deep orange
        float t = (sunAltitude - 5.0f) / 5.0f;
        ambient = Color(0.14f, 0.09f, 0.05f).Lerp(Color(0.16f, 0.13f, 0.08f), t);
        fogColor = Color(0.95f, 0.5f, 0.2f).Lerp(Color(0.85f, 0.7f, 0.45f), t);
        sunColor = Color(1.5f, 0.7f, 0.25f).Lerp(Color(1.4f, 1.1f, 0.7f), t);
    }
    else if (sunAltitude > 0.0f)
    {
        // Deep orange → Rose/magenta
        float t = sunAltitude / 5.0f;
        ambient = Color(0.12f, 0.06f, 0.07f).Lerp(Color(0.14f, 0.09f, 0.05f), t);
        fogColor = Color(0.8f, 0.3f, 0.35f).Lerp(Color(0.95f, 0.5f, 0.2f), t);
        sunColor = Color(1.3f, 0.4f, 0.3f).Lerp(Color(1.5f, 0.7f, 0.25f), t);
    }
    else if (sunAltitude > -5.0f)
    {
        // Rose/magenta → Purple twilight
        float t = sunAltitude / -5.0f;
        ambient = Color(0.12f, 0.06f, 0.07f).Lerp(Color(0.06f, 0.04f, 0.09f), t);
        fogColor = Color(0.8f, 0.3f, 0.35f).Lerp(Color(0.3f, 0.15f, 0.35f), t);
        sunColor = Color(1.3f, 0.4f, 0.3f).Lerp(Color(0.3f, 0.1f, 0.15f), t);
        moonColor = Color(0.0f, 0.0f, 0.0f).Lerp(Color(0.3f, 0.3f, 0.5f), t);
        moonEnabled = true;
    }
    else
    {
        // Deep blue night (below -5)
        float t = Clamp((sunAltitude + 5.0f) / -3.0f, 0.0f, 1.0f);
        ambient = Color(0.06f, 0.04f, 0.09f).Lerp(Color(0.02f, 0.02f, 0.04f), t);
        fogColor = Color(0.3f, 0.15f, 0.35f).Lerp(Color(0.05f, 0.05f, 0.15f), t);
        sunColor = Color(0.3f, 0.1f, 0.15f).Lerp(Color(0.0f, 0.0f, 0.0f), t);
        sunEnabled = (sunAltitude > -8.0f);
        moonColor = Color(0.3f, 0.3f, 0.5f).Lerp(Color(0.6f, 0.6f, 1.0f), t);
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

    // Hemisphere lighting: sky=bright blue bounce, ground=dark warm bounce
    if (hemisphereEnabled_)
    {
        float intensity = Max(Max(ambient.r_, ambient.g_), ambient.b_);
        zone_->SetSkyAmbientColor(Color(intensity * 0.6f, intensity * 0.7f, intensity * 1.0f));
        zone_->SetGroundAmbientColor(Color(intensity * 0.15f, intensity * 0.1f, intensity * 0.05f));
    }
    else
    {
        zone_->SetSkyAmbientColor(Color::BLACK);
        zone_->SetGroundAmbientColor(Color::BLACK);
    }

    if (sunLight_)
    {
        sunLight_->SetColor(sunColor);
        sunLight_->SetEnabled(sunEnabled);
    }
    if (moonLight_)
    {
        // Phase 2 lunar cycle: brightness scales with phase (full=1.0, new=0.0)
        float moonPhase = moonAge_ / 29.53f;
        float moonIllumination = (1.0f - cosf(moonPhase * 2.0f * M_PI)) * 0.5f;
        Color phasedColor = moonColor * moonIllumination;
        moonLight_->SetColor(phasedColor);
        moonLight_->SetEnabled(moonEnabled && moonIllumination > 0.05f);
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
        if (sunAltitude > 20.0f)
            skyTint = Color(0.85f, 0.85f, 0.9f);
        else if (sunAltitude > 10.0f)
        {
            // Daylight → Golden sky
            float t = (sunAltitude - 10.0f) / 10.0f;
            skyTint = Color(0.9f, 0.75f, 0.5f).Lerp(Color(0.85f, 0.85f, 0.9f), t);
        }
        else if (sunAltitude > 5.0f)
        {
            // Golden → Deep orange sky
            float t = (sunAltitude - 5.0f) / 5.0f;
            skyTint = Color(0.95f, 0.5f, 0.25f).Lerp(Color(0.9f, 0.75f, 0.5f), t);
        }
        else if (sunAltitude > 0.0f)
        {
            // Deep orange → Rose/magenta sky
            float t = sunAltitude / 5.0f;
            skyTint = Color(0.85f, 0.35f, 0.4f).Lerp(Color(0.95f, 0.5f, 0.25f), t);
        }
        else if (sunAltitude > -5.0f)
        {
            // Rose → Purple twilight sky
            float t = sunAltitude / -5.0f;
            skyTint = Color(0.85f, 0.35f, 0.4f).Lerp(Color(0.25f, 0.12f, 0.35f), t);
        }
        else
        {
            // Purple → Deep blue night sky
            float t = Clamp((sunAltitude + 5.0f) / -3.0f, 0.0f, 1.0f);
            skyTint = Color(0.25f, 0.12f, 0.35f).Lerp(Color(0.03f, 0.03f, 0.08f), t);
        }

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
// Weather System
// ============================================================================

WeatherState TerrainNode::CalculateSeasonalWeather()
{
    WeatherState target;

    // Season factor: 0=winter solstice, 1=summer solstice
    float sf = cachedSeasonFactor_;

    // Seasonal baselines for cloud cover
    // Spring/autumn = more overcast, summer = clearer, winter = medium-high
    float seasonAngle = fmodf((dayOfYear_ - 81) / 365.0f + 1.0f, 1.0f);
    float baseCloud;
    if (seasonAngle < 0.25f) // spring
        baseCloud = 0.45f;
    else if (seasonAngle < 0.5f) // summer
        baseCloud = 0.25f;
    else if (seasonAngle < 0.75f) // autumn
        baseCloud = 0.55f;
    else // winter
        baseCloud = 0.50f;

    // Lunar influence: new moon (+30% storm/cloud), full moon (-30%)
    // moonAge_ cycles 0..29.53, full moon at ~14.7
    float lunarPhase = cosf(moonAge_ * 6.2831853f / 29.53f); // +1=new, -1=full
    float lunarCloudMod = lunarPhase * 0.15f; // ±15%

    // Diurnal pattern: afternoon buildup (peak at 15:00)
    float effectiveTime = fmodf(timeOfDay_ + timeOfDayOffset_ + 24.0f, 24.0f);
    float diurnalMod = 0.0f;
    if (effectiveTime > 12.0f && effectiveTime < 18.0f)
        diurnalMod = 0.15f * sinf((effectiveTime - 12.0f) * 3.14159f / 6.0f);
    // Morning fog tendency
    if (effectiveTime > 5.0f && effectiveTime < 9.0f)
        diurnalMod += 0.1f * sinf((effectiveTime - 5.0f) * 3.14159f / 4.0f);

    float computedCloud = Clamp(baseCloud + lunarCloudMod + diurnalMod, 0.0f, 1.0f);

    // Modulate with actual skybox cloud alpha overhead — the skybox IS the weather
    Vector3 camPos = cameraNode_ ? cameraNode_->GetWorldPosition() : Vector3::ZERO;
    localCloudDensity_ = SampleCloudDensity(camPos);
    // Blend: 50% seasonal computation + 50% skybox alpha (spatial variation)
    target.cloudCover = Clamp(computedCloud * 0.5f + localCloudDensity_ * 0.5f, 0.0f, 1.0f);

    // Precipitation: only when cloud cover > 0.5
    if (target.cloudCover > 0.5f)
    {
        float precipChance = (target.cloudCover - 0.5f) * 2.0f; // 0..1
        // Thick skybox cloud overhead boosts rainfall directly
        if (localCloudDensity_ > 0.6f)
            precipChance += (localCloudDensity_ - 0.6f) * 0.5f;
        // Summer thunderstorms are intense but brief
        if (seasonAngle >= 0.25f && seasonAngle < 0.5f)
            precipChance *= 1.3f;
        // Winter precipitation more likely as snow (handled later)
        target.precipitation = Clamp(precipChance * 0.6f, 0.0f, 1.0f);
    }

    // Wind: base from season, boosted during storms
    target.windSpeed = Lerp(0.1f, 0.3f, 1.0f - sf) + target.precipitation * 0.4f;
    target.windSpeed = Clamp(target.windSpeed, 0.0f, 1.0f);
    target.windAngle = fmodf(dayOfYear_ * 0.1f + moonAge_ * 0.3f, 6.2831853f);

    // Fog density: cloud cover thickens fog, precipitation thickens more
    target.fogDensity = 1.0f - target.cloudCover * 0.3f - target.precipitation * 0.3f;
    target.fogDensity = Clamp(target.fogDensity, 0.3f, 1.0f);

    // Ambient dimming: overcast dims light
    target.ambientDim = 1.0f - target.cloudCover * 0.4f;
    target.ambientDim = Clamp(target.ambientDim, 0.4f, 1.0f);

    return target;
}

void TerrainNode::UpdateWeather(float timeStep)
{
    if (weatherOverride_)
        return; // manual slider control, no automatic updates

    float gameTimeStep = timeStep * CELESTIAL_TIME_SCALE;

    // Periodically pick a new weather target
    weatherCheckTimer_ -= gameTimeStep;
    if (weatherCheckTimer_ <= 0.0f)
    {
        weatherCheckTimer_ = weatherCheckInterval_;
        weatherTarget_ = CalculateSeasonalWeather();
        weatherTransitionTimer_ = weatherTransitionDuration_;
    }

    // Lerp toward target
    if (weatherTransitionTimer_ > 0.0f)
    {
        weatherTransitionTimer_ -= gameTimeStep;
        float t = 1.0f - Clamp(weatherTransitionTimer_ / weatherTransitionDuration_, 0.0f, 1.0f);
        // Smooth step for natural transitions
        t = t * t * (3.0f - 2.0f * t);
        weather_ = weather_.Lerp(weatherTarget_, t);
    }

    ApplyWeatherToScene();
    UpdateDroughtVisuals(timeStep);
}

// ============================================================================
// Weather System Phase 1: Cloud Density from Skybox Alpha
// ============================================================================

float TerrainNode::SampleCloudDensity(const Vector3& worldPos)
{
    // No cloud faces loaded — return zero (clear sky)
    if (!cloudFaces_[0])
        return 0.0f;

    // The "up" direction from this position (straight up for flat terrain)
    Vector3 dir(0.0f, 1.0f, 0.0f);

    // Apply inverse cloud rotation to get the unrotated skybox lookup
    // Cloud rotation axis matches SkyboxBlend.glsl: normalize(0.12, 1.0, 0.07)
    const Vector3 axis = Vector3(0.12f, 1.0f, 0.07f).Normalized();
    float cloudTotal = cloudAngle_ + timeOfDayOffset_ * 6.2831853f / 18.0f;
    float angle = -cloudTotal;  // inverse rotation

    // Rodrigues rotation: dir * cos(a) + cross(axis, dir) * sin(a) + axis * dot(axis, dir) * (1 - cos(a))
    float c = cosf(angle);
    float s = sinf(angle);
    Vector3 rotated = dir * c + axis.CrossProduct(dir) * s + axis * axis.DotProduct(dir) * (1.0f - c);

    // Determine which cubemap face and UV coords
    float absX = fabsf(rotated.x_);
    float absY = fabsf(rotated.y_);
    float absZ = fabsf(rotated.z_);

    int face;
    float u, v;

    if (absX >= absY && absX >= absZ)
    {
        if (rotated.x_ > 0) { face = 0; u = -rotated.z_; v = -rotated.y_; } // +X
        else                 { face = 1; u =  rotated.z_; v = -rotated.y_; } // -X
        float ma = absX;
        u = (u / ma + 1.0f) * 0.5f;
        v = (v / ma + 1.0f) * 0.5f;
    }
    else if (absY >= absX && absY >= absZ)
    {
        if (rotated.y_ > 0) { face = 2; u =  rotated.x_; v =  rotated.z_; } // +Y
        else                 { face = 3; u =  rotated.x_; v = -rotated.z_; } // -Y
        float ma = absY;
        u = (u / ma + 1.0f) * 0.5f;
        v = (v / ma + 1.0f) * 0.5f;
    }
    else
    {
        if (rotated.z_ > 0) { face = 4; u =  rotated.x_; v = -rotated.y_; } // +Z
        else                 { face = 5; u = -rotated.x_; v = -rotated.y_; } // -Z
        float ma = absZ;
        u = (u / ma + 1.0f) * 0.5f;
        v = (v / ma + 1.0f) * 0.5f;
    }

    if (!cloudFaces_[face])
        return 0.0f;

    int w = cloudFaces_[face]->GetWidth();
    int h = cloudFaces_[face]->GetHeight();
    int px = Clamp((int)(u * w), 0, w - 1);
    int py = Clamp((int)(v * h), 0, h - 1);

    Color pixel = cloudFaces_[face]->GetPixel(px, py);
    return pixel.a_;  // alpha = cloud density
}

void TerrainNode::ApplyWeatherToScene()
{
    if (!zone_)
        return;

    // Modulate fog distances — weather.fogDensity < 1 = thicker fog
    float weatherFogStart = cachedFogStart_ * weather_.fogDensity;
    float weatherFogEnd = cachedFogEnd_ * weather_.fogDensity;
    zone_->SetFogStart(weatherFogStart);
    zone_->SetFogEnd(weatherFogEnd);

    // Shift fog color toward gray when overcast/raining
    Color stormGray(0.45f, 0.48f, 0.52f);
    Color currentFog = zone_->GetFogColor();
    Color weatherFog = currentFog.Lerp(stormGray, weather_.cloudCover * 0.5f);
    zone_->SetFogColor(weatherFog);

    // Dim ambient by cloud cover
    Color currentAmbient = zone_->GetAmbientColor();
    Color dimmedAmbient(
        currentAmbient.r_ * weather_.ambientDim,
        currentAmbient.g_ * weather_.ambientDim,
        currentAmbient.b_ * weather_.ambientDim
    );
    zone_->SetAmbientColor(dimmedAmbient);

    // Dim sun during overcast
    if (sunLight_ && sunLight_->IsEnabled())
    {
        Color sunColor = sunLight_->GetColor();
        float sunDim = 1.0f - weather_.cloudCover * 0.5f;
        sunLight_->SetColor(Color(sunColor.r_ * sunDim, sunColor.g_ * sunDim, sunColor.b_ * sunDim));
    }

    // Dim skybox tint during overcast
    if (skyboxMat_)
    {
        float skyDim = 1.0f - weather_.cloudCover * 0.3f;
        Variant currentTint = skyboxMat_->GetShaderParameter("MatDiffColor");
        if (currentTint.GetType() == VAR_COLOR)
        {
            Color tint = currentTint.GetColor();
            skyboxMat_->SetShaderParameter("MatDiffColor",
                Color(tint.r_ * skyDim, tint.g_ * skyDim, tint.b_ * skyDim));
        }
    }

    // Weather-modulate hemisphere lighting (Phase 3)
    if (hemisphereEnabled_)
    {
        Color sky = zone_->GetSkyAmbientColor();
        Color gnd = zone_->GetGroundAmbientColor();

        // Overcast desaturates sky toward flat gray
        if (weather_.cloudCover > 0.0f)
        {
            float cc = weather_.cloudCover;
            float skyGray = sky.r_ * 0.299f + sky.g_ * 0.587f + sky.b_ * 0.114f;
            sky = Color(
                Lerp(sky.r_, skyGray * 0.7f, cc * 0.6f),
                Lerp(sky.g_, skyGray * 0.7f, cc * 0.6f),
                Lerp(sky.b_, skyGray * 0.7f, cc * 0.6f)
            );
            // Rain darkens ground bounce
            float rainDarken = 1.0f - weather_.precipitation * 0.5f;
            gnd = Color(gnd.r_ * rainDarken, gnd.g_ * rainDarken, gnd.b_ * rainDarken);
        }

        // Snow whitens ground bounce (winter: cachedSeasonFactor_ < 0.3)
        if (cachedSeasonFactor_ < 0.3f)
        {
            float snowFactor = (0.3f - cachedSeasonFactor_) / 0.3f;  // 0 at equinox, 1 at solstice
            snowFactor *= (1.0f - weather_.cloudCover * 0.3f);  // less snow bounce when overcast
            Color snowBounce(sky.r_ * 0.8f, sky.g_ * 0.8f, sky.b_ * 0.85f);
            gnd = gnd.Lerp(snowBounce, snowFactor * 0.6f);
        }

        zone_->SetSkyAmbientColor(sky);
        zone_->SetGroundAmbientColor(gnd);
    }

    // God ray intensity reduced by cloud cover
    if (godRaysEnabled_ && renderPath_)
    {
        float baseIntensity = 0.6f;  // default god ray intensity
        float weatherIntensity = baseIntensity * (1.0f - weather_.cloudCover * 0.7f);
        SetShaderParamCached("GodRayIntensity", weatherIntensity);
    }
}

// ============================================================================
// Lightning
// ============================================================================

void TerrainNode::UpdateLightning(float timeStep)
{
    // Only strike during heavy precipitation with high cloud cover
    float stormIntensity = Min(weather_.precipitation, weather_.cloudCover);

    // Decay after-dark (pupil contraction) smoothly back to zero
    if (lightningAfterDark_ > 0.0f)
    {
        lightningAfterDark_ -= timeStep * 0.3f; // ~0.5s recovery
        if (lightningAfterDark_ < 0.0f) lightningAfterDark_ = 0.0f;
    }

    if (stormIntensity < 0.6f)
    {
        lightningIntensity_ = 0.0f;
        lightningTimer_ = 0.0f;
        lightningFlickerCount_ = 0;
        lightningFadeTimer_ = 0.0f;
        // Still apply after-dark if fading out
        if (lightningAfterDark_ > 0.0f)
            goto apply;
        return;
    }

    // Active flicker sequence
    if (lightningFlickerCount_ > 0)
    {
        lightningFlickerPhase_ -= timeStep;
        if (lightningFlickerPhase_ <= 0.0f)
        {
            lightningFlickerOn_ = !lightningFlickerOn_;
            if (lightningFlickerOn_)
            {
                lightningIntensity_ = 0.7f + Random(0.3f);
                lightningFlickerPhase_ = 0.03f + Random(0.05f);
            }
            else
            {
                lightningIntensity_ = 0.0f;
                lightningFlickerCount_--;
                if (lightningFlickerCount_ > 0)
                    lightningFlickerPhase_ = 0.05f + Random(0.1f);
                else
                {
                    // Flickers done — start fade and after-dark
                    lightningFadeTimer_ = 0.4f;
                    lightningAfterDark_ = 0.15f;
                }
            }
        }
        goto apply;
    }

    // Fade-out after flicker sequence
    if (lightningFadeTimer_ > 0.0f)
    {
        lightningFadeTimer_ -= timeStep;
        lightningIntensity_ = Clamp(lightningFadeTimer_ / 0.4f, 0.0f, 1.0f) * 0.35f;
        if (lightningFadeTimer_ <= 0.0f)
        {
            lightningIntensity_ = 0.0f;
            lightningFadeTimer_ = 0.0f;
        }
        goto apply;
    }

    // Roll for a new strike
    lightningTimer_ -= timeStep;
    if (lightningTimer_ <= 0.0f)
    {
        float interval = 8.0f + (1.0f - stormIntensity) * 25.0f;
        lightningTimer_ = interval;

        float chance = (stormIntensity - 0.5f) * 0.4f;
        if (Random(1.0f) < chance)
        {
            lightningFlickerCount_ = 2 + Random(3);
            lightningFlickerOn_ = true;
            lightningFlickerPhase_ = 0.04f + Random(0.04f);
            lightningIntensity_ = 0.8f + Random(0.2f);
        }
    }

apply:
    // Apply lightning to scene lighting — boost on top of what ApplyWeatherToScene set
    float li = lightningIntensity_;
    float dark = lightningAfterDark_;

    if (li <= 0.0f && dark <= 0.0f)
        return;

    if (zone_)
    {
        // Ambient: spike toward near-daylight white-blue, then dip below normal
        Color ambient = zone_->GetAmbientColor();
        Color flashAmbient(0.8f, 0.82f, 0.9f); // cold white-blue
        Color boosted = ambient.Lerp(flashAmbient, li);
        // After-dark: dim below current ambient
        boosted = Color(boosted.r_ * (1.0f - dark), boosted.g_ * (1.0f - dark), boosted.b_ * (1.0f - dark));
        zone_->SetAmbientColor(boosted);

        // Fog color: flash brightens fog so distant terrain lights up
        Color fog = zone_->GetFogColor();
        Color brightFog(0.75f, 0.78f, 0.85f);
        Color flashFog = fog.Lerp(brightFog, li * 0.6f);
        flashFog = Color(flashFog.r_ * (1.0f - dark * 0.5f), flashFog.g_ * (1.0f - dark * 0.5f), flashFog.b_ * (1.0f - dark * 0.5f));
        zone_->SetFogColor(flashFog);
    }

    // Sun/directional light pulse
    if (sunLight_)
    {
        Color sunColor = sunLight_->GetColor();
        Color flashSun(1.0f, 0.98f, 0.95f);
        sunLight_->SetColor(sunColor.Lerp(flashSun, li * 0.7f));
    }

    // Skybox brightens during flash
    if (skyboxMat_)
    {
        Variant currentTint = skyboxMat_->GetShaderParameter("MatDiffColor");
        if (currentTint.GetType() == VAR_COLOR)
        {
            Color tint = currentTint.GetColor();
            Color flashTint(1.0f, 1.0f, 1.0f);
            skyboxMat_->SetShaderParameter("MatDiffColor", tint.Lerp(flashTint, li * 0.5f));
        }
    }
}

// ============================================================================
// Weather Phase 4: Rainfall Accumulation
// ============================================================================

void TerrainNode::UpdateRainfallAccumulation()
{
    if (!rainfallMap_ || !terrain_)
        return;

    // Terrain world bounds for coordinate mapping
    const Vector3& spacing = terrain_->GetSpacing();
    int hmSize = terrain_->GetHeightMap() ? terrain_->GetHeightMap()->GetWidth() : 1025;
    float terrainWorldSize = (float)(hmSize - 1) * spacing.x_;
    Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();
    float originX = terrainPos.x_ - terrainWorldSize * 0.5f;
    float originZ = terrainPos.z_ - terrainWorldSize * 0.5f;

    unsigned char* data = rainfallMap_->GetData();
    int stride = RAINFALL_MAP_SIZE * 4;  // RGBA

    // Rainfall deposit rate per update tick (tunable)
    float depositRate = 2.0f;   // units per tick when raining
    float drainRate = 0.5f;     // soil moisture drain per tick (evaporation)
    float surfaceDrainRate = 1.0f;  // surface water evaporation per tick

    for (int z = 0; z < RAINFALL_MAP_SIZE; ++z)
    {
        for (int x = 0; x < RAINFALL_MAP_SIZE; ++x)
        {
            // Map pixel to world position
            float worldX = originX + ((float)x / (float)(RAINFALL_MAP_SIZE - 1)) * terrainWorldSize;
            float worldZ = originZ + ((float)z / (float)(RAINFALL_MAP_SIZE - 1)) * terrainWorldSize;

            // Sample cloud density at this position
            float density = SampleCloudDensity(Vector3(worldX, 0.0f, worldZ));

            int idx = (z * RAINFALL_MAP_SIZE + x) * 4;
            float surfaceWater = (float)data[idx];      // R
            float soilMoisture = (float)data[idx + 1];  // G

            // Rainfall: deposit when cloud density > 0.6
            if (density > 0.6f)
            {
                float rainIntensity = (density - 0.6f) / 0.4f;  // 0-1 within rain range
                float deposit = depositRate * rainIntensity;
                surfaceWater = Min(surfaceWater + deposit, 255.0f);
                soilMoisture = Min(soilMoisture + deposit * 0.5f, 255.0f);
            }

            // Drain: surface water evaporates, soil moisture drains
            surfaceWater = Max(surfaceWater - surfaceDrainRate, 0.0f);
            soilMoisture = Max(soilMoisture - drainRate, 0.0f);

            data[idx]     = (unsigned char)surfaceWater;
            data[idx + 1] = (unsigned char)soilMoisture;
        }
    }
}

// ============================================================================
// Melbourne Time (via OS timezone database)
// ============================================================================

void TerrainNode::SyncMelbourneTime()
{
    // Temporarily switch to Melbourne timezone, get local time, restore
    const char* oldTZ = getenv("TZ");
    setenv("TZ", "Australia/Melbourne", 1);
    tzset();

    time_t now = time(nullptr);
    struct tm* melb = localtime(&now);

    int hour = melb->tm_hour;
    int minute = melb->tm_min;
    int second = melb->tm_sec;
    int yday = melb->tm_yday + 1;  // tm_yday is 0-based, dayOfYear_ is 1-based

    // Restore original timezone
    if (oldTZ)
        setenv("TZ", oldTZ, 1);
    else
        unsetenv("TZ");
    tzset();

    timeOfDay_ = (float)hour + minute / 60.0f + second / 3600.0f;
    dayOfYear_ = yday;

    // Seed RNG from time — gives different seed each second
    unsigned seed = 5381;
    seed = seed * 33 + (unsigned)(now & 0xFFFFFFFF);
    seed = seed * 33 + 25773;
    SetRandomSeed(seed);
    URHO3D_LOGINFOF("Melbourne time: %02d:%02d:%02d (day %d), RNG seed: %u", hour, minute, second, yday, seed);
}

// ============================================================================
// Update
// ============================================================================

void TerrainNode::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
    // Deferred scene swap — runs BEFORE Scene::Update so old scene's components
    // never tick with stale pointers
    if (!pendingScene_)
        return;

    // Swap scene AND viewport atomically
    scene_ = pendingScene_;
    pendingScene_.Reset();
    {
        auto* renderer = GetSubsystem<Renderer>();
        if (renderer && renderer->GetNumViewports() > 0)
        {
            auto* viewport = renderer->GetViewport(0);
            if (viewport)
                viewport->SetScene(scene_);
        }
    }

    SetupSceneBindings();

    // Update reflection camera with loaded water plane
    if (waterNode_ && reflectionCameraNode_)
    {
        auto* reflCam = reflectionCameraNode_->GetComponent<Camera>();
        if (reflCam)
        {
            reflCam->SetReflectionPlane(waterPlane_);
            reflCam->SetClipPlane(waterClipPlane_);
        }
    }
    if (waterNode_ && renderPath_)
        renderPath_->SetShaderParameter("WaterLevel", waterNode_->GetWorldPosition().y_);

    // SetupSceneBindings() already recreated all LOCAL entities, campfire UI, etc.

    // Recreate minimap camera (UI-bound, not in SetupSceneBindings)
    if (minimap_)
    {
        minimapCameraNode_ = scene_->CreateTemporaryChild("MinimapCamera", LOCAL);
        auto* minimapCam = minimapCameraNode_->CreateComponent<Camera>();
        minimapCam->SetOrthographic(true);
        IntVector2 numPatches = terrain_ ? terrain_->GetNumPatches() : IntVector2(16, 16);
        float terrainWorldSize = terrain_ ? numPatches.x_ * terrain_->GetSpacing().x_ * terrain_->GetPatchSize() : 2048.0f;
        minimapCam->SetOrthoSize(terrainWorldSize);
        minimapCam->SetFarClip(500.0f);
        minimapCam->SetNearClip(1.0f);
        minimapCam->SetFlipVertical(true);
        Vector3 terrainCenter = terrain_ ? terrain_->GetNode()->GetWorldPosition() : Vector3::ZERO;
        minimapCameraNode_->SetPosition(Vector3(terrainCenter.x_, 200.0f, terrainCenter.z_));
        minimapCameraNode_->SetRotation(Quaternion(90.0f, 0.0f, 0.0f));
        RefreshMinimap();
    }

    // Restore camera from scene Vars
    possessing_ = false;
    characterNode_ = nullptr;
    clientObjectID_ = 0;
    if (cameraNode_ && scene_)
    {
        const Variant& cx = scene_->GetVar("CameraX");
        const Variant& cy = scene_->GetVar("CameraY");
        const Variant& cz = scene_->GetVar("CameraZ");
        Vector3 camPos(cx.IsEmpty() ? 64.0f : cx.GetFloat(),
                       cy.IsEmpty() ? 60.0f : cy.GetFloat(),
                       cz.IsEmpty() ? 64.0f : cz.GetFloat());
        float minY = 20.0f;
        if (terrain_)
            minY = Max(minY, terrain_->GetHeight(camPos) + 5.0f);
        if (waterNode_)
            minY = Max(minY, waterNode_->GetWorldPosition().y_ + 5.0f);
        camPos.y_ = Max(camPos.y_, minY);
        cameraNode_->SetWorldPosition(camPos);
        yaw_ = scene_->GetVar("CameraYaw").GetFloat();
        pitch_ = scene_->GetVar("CameraPitch").GetFloat();
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
        const Variant& cm = scene_->GetVar("CameraMode");
        cameraMode_ = cm.IsEmpty() ? CAM_GOD : (CameraMode)cm.GetI32();
    }
    else
        cameraMode_ = CAM_GOD;

    // Re-init mouse mode
    menuOpen_ = false;
    auto* input = GetSubsystem<Input>();
    if (input)
    {
        GetSubsystem<UI>()->SetFocusElement(nullptr);
        input->SetMouseMode(MM_RELATIVE);
        input->SetMouseVisible(false);
        useMouseMode_ = MM_RELATIVE;
    }

    URHO3D_LOGINFO("Scene swap complete");
}

void TerrainNode::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Track radial command UI to selected NPC's screen position
    if (radialUI_ && radialUI_->IsVisible() && cameraNode_)
        radialUI_->Update(cameraNode_->GetComponent<Camera>());

    // Scan scene for replicated creature nodes that need components attached.
    // E_NODEADDED fires for the first batch (which gets Clear'd), so we also
    // scan periodically to catch post-Clear nodes.
    if (loggedIn_ && scene_)
    {
        const auto& children = scene_->GetChildren();
        for (unsigned i = 0; i < children.Size(); ++i)
        {
            Node* child = children[i];
            if (!child || child->IsTemporary())
                continue;
            // Only process replicated nodes (low IDs)
            if (child->GetID() >= 0x1000000)
                continue;
            // Already has a creature component?
            if (child->GetDerivedComponent<LandAnimal>(false))
                continue;
            // Check if it's a creature by name
            const Variant& creatureVar = child->GetVar("CreatureId");
            if (creatureVar.IsEmpty())
                continue;
            int creatureId = creatureVar.GetI32();
            if (creatureId <= 0)
                continue;
            // Attach creature component (same logic as HandleNodeAdded)
            AttachCreatureComponent(child, creatureId);
        }

        // Hide orphaned replicated nodes that have an AnimatedModel but no
        // creature component after a grace period — these render with broken
        // animations and are invisible to visibility toggles and raycasts.
        // Grace period avoids hiding nodes that just haven't been attached yet.
        for (unsigned i = 0; i < children.Size(); ++i)
        {
            Node* child = children[i];
            if (!child || child->IsTemporary())
                continue;
            if (child->GetID() >= 0x1000000)
                continue;
            if (child->GetDerivedComponent<LandAnimal>(false))
                continue;
            // Skip nodes with CreatureId — attachment scan above will handle them
            const Variant& creatureVar = child->GetVar("CreatureId");
            if (!creatureVar.IsEmpty() && creatureVar.GetI32() > 0)
                continue;
            auto* mdl = child->GetComponent<AnimatedModel>();
            if (mdl && mdl->IsEnabled())
            {
                URHO3D_LOGWARNINGF("Hiding orphaned replicated AnimatedModel node '%s' (id=%u) — no creature component, no CreatureId",
                    child->GetName().CString(), child->GetID());
                mdl->SetEnabled(false);
            }
        }
    }

    // Drive replicated creature logic + apply visibility toggles.
    for (unsigned i = 0; i < replicatedCreatures_.Size(); ++i)
    {
        Creature* c = replicatedCreatures_[i];
        if (!c || !c->GetNode())
            continue;

        // Apply per-species visibility
        int cid = c->GetCreatureId();
        bool vis = true;
        switch (cid)
        {
        case  1: vis = rabbitVisible_;    break;
        case  2: vis = deerVisible_;      break;
        case  3: vis = foxVisible_;       break;
        case  4: vis = stagVisible_;      break;
        case  5: vis = wolfVisible_;      break;
        case  6: vis = bullVisible_;      break;
        case  7: vis = cowVisible_;       break;
        case  9: vis = donkeyVisible_;    break;
        case 10: vis = horseVisible_;     break;
        case 11: vis = alpacaVisible_;    break;
        case 12: vis = huskyVisible_;     break;
        case 13: vis = shibaInuVisible_;  break;
        case 20: vis = caveManVisible_;   break;
        case 21: vis = caveWomanVisible_; break;
        }
        // Use view mask instead of SetEnabled — replication overrides enabled state.
        auto* mdl = c->GetNode()->GetComponent<AnimatedModel>(true);
        if (mdl)
            mdl->SetViewMask(vis ? 0x7FFFFFFF : 0);

        if (vis)
            c->Update(timeStep);
    }

    // ProfilerTimeline frame — first real call site of the Phase 1 infrastructure.
    // RAII scope opens a frame on construction, closes on every exit path.
    // Inner sections use URHO3D_TIMELINE_EVENT_CAT scopes so the existing
    // HiresTimer-based stat logging stays untouched (different consumers).
    auto* graphicsSubsystem_pt = GetSubsystem<Graphics>();
    ProfilerTimeline* timeline = graphicsSubsystem_pt ? graphicsSubsystem_pt->GetProfilerTimeline() : nullptr;
    ProfilerTimelineFrameScope _ptl_frame(timeline);

    // --- Real wall-clock frame profiling (prints every 120 frames) ---
    static HiresTimer frameTimer;
    static long long accumFrame = 0, accumCelestial = 0, accumFish = 0, accumContext = 0;
    static long long accumRipple = 0, accumOofo = 0, accumMinimap = 0, accumLogic = 0;
    static int profileFrameCount = 0;
    long long frameStart = 0;
    HiresTimer sectionTimer;
    if (loggedIn_)
    {
        frameStart = frameTimer.GetUSec(true);
    }

    // Rebuild fish spatial hash every 3 frames — fish don't move fast enough to need 60Hz updates
    if (fishHashFrameSkip_++ >= 3)
    {
        fishHashFrameSkip_ = 0;
        URHO3D_TIMELINE_EVENT_CAT(timeline, "FishSpatialHash Rebuild", "ai");
        RebuildFishSpatialHash();
    }

    // Rebuild land animal spatial hash every 3 frames — same cadence as fish
    // Land animals are sparser (~20-50) so this is cheaper than the fish hash
    if ((fishHashFrameSkip_ + 1) % 3 == 0)  // offset by 1 frame from fish hash
    {
        URHO3D_TIMELINE_EVENT_CAT(timeline, "LandAnimalHash Rebuild", "ai");
        RebuildLandAnimalSpatialHash();
    }

    // Death System Phase 3: age and expire death scent markers.
    // Cheap (max 16 markers, cap enforced in ScentRegistry::Register), every frame.
    {
        URHO3D_TIMELINE_EVENT_CAT(timeline, "ScentRegistry Tick", "ai");
        ScentRegistry::Tick(timeStep);
    }

    // Resource map streaming — spawn/despawn pickup nodes within camera radius.
    // Throttled to 0.5s — pickup visuals don't need 60 Hz updates.
    resourceStreamTimer_ += timeStep;
    if (resourceStreamTimer_ >= 0.5f)
    {
        resourceStreamTimer_ = 0.f;
        UpdateResourceStreaming();
    }

    // Decoration scatter — static Nature/ models placed by biome, streamed by camera distance.
    decorScatterTimer_ += timeStep;
    if (decorScatterTimer_ >= 0.5f)
    {
        decorScatterTimer_ = 0.f;
        UpdateDecorScatter();
    }

    // Water edge scatter — Pond/ models at waterline, streamed by camera distance.
    waterEdgeScatterTimer_ += timeStep;
    if (waterEdgeScatterTimer_ >= 0.5f)
    {
        waterEdgeScatterTimer_ = 0.f;
        UpdateWaterEdgeScatter();
    }

    // Offline-mode connect retry — drives the spawn-and-retry state machine.
    // No-op when offlineMode_ == OFFLINE_NONE.
    TickOfflineConnect(timeStep);

    // Resource Chain Phase 2 — trap-check scanner.
    // Client-driven detection: any of our local creatures within TRAP_CHECK_RADIUS of
    // a placed trap generates one MSG_TRAP_CHECK round-trip per (trap, creature) pair.
    // Server (local or remote) does the real attract_range gating + d20 vs holdStrength roll.
    // Throttled to TRAP_CHECK_INTERVAL because the proximity test is O(traps × animals).
    trapCheckTimer_ -= timeStep;
    if (loggedIn_ && trapCheckTimer_ <= 0.0f && !trapNodes_.Empty())
    {
        URHO3D_TIMELINE_EVENT_CAT(timeline, "TrapCheck Scan", "ai");
        trapCheckTimer_ = TRAP_CHECK_INTERVAL;
        ScanTrapsForCatches();
    }

    // AuthServer LAN discovery (skipped if user chose localhost or already connected)
    discoveryTimer_ -= timeStep;
    if (discoveryTimer_ <= 0.0f && !authConnected_ && authDiscovering_)
    {
        DiscoverAuthServer();
        discoveryTimer_ = discoveryInterval_;
    }

    // Send pending registration once encryption is established (plain DH)
    if (!pendingRegisterUsername_.Empty() && authConnected_)
    {
        auto* network = GetSubsystem<Network>();
        Connection* serverConn = network ? network->GetServerConnection() : nullptr;
        if (serverConn && serverConn->IsEncryptionReady())
        {
            VectorBuffer msg;
            msg.WriteString(pendingRegisterUsername_);
            msg.WriteString(pendingRegisterPassword_);
            serverConn->SendMessage(MSG_AUTH_REGISTER, true, true, msg);
            pendingRegisterUsername_.Clear();
            pendingRegisterPassword_.Clear();
        }
    }

    // Phase 5a: no deferred avatar lookup. characterNode_ is set by PossessNPC,
    // cleared by UnpossessNPC. The clientObjectID_ node is a position tracker only.
    if (false)
    {
    }

    // Melbourne time sync (every 5 minutes)
    timeSyncTimer_ -= timeStep;
    if (timeSyncTimer_ <= 0.0f)
    {
        SyncMelbourneTime();
        timeSyncTimer_ = 300.0f;
    }

    // First-frame-rendered timing
    if (firstFramePending_ && loggedIn_ && !asyncSceneLoading_)
    {
        firstFramePending_ = false;
        loginTimerActive_ = false;
    }

    if (!loggedIn_ || asyncSceneLoading_)
    {
        // Login screen — slowly rotate camera for a cinematic sky view
        Quaternion rot = cameraNode_->GetRotation();
        cameraNode_->SetRotation(rot * Quaternion(timeStep * 1.5f, Vector3::UP));

        // Update login status with server connection state
        if (loginStatusText_ && !authConnected_ && authDiscovering_)
            loginStatusText_->SetText("Scanning for server...");
        else if (loginStatusText_ && authConnected_)
        {
            if (loginStatusText_->GetText() == "Scanning for server..." ||
                loginStatusText_->GetText().Contains("connecting"))
                loginStatusText_->SetText("Connected — enter credentials");
            loginStatusText_->SetColor(Color(0.3f, 1.0f, 0.3f));
        }
    }
    else
    {
        // Full world mode
        if (possessionLerping_)
            UpdatePossessionLerp(timeStep);
        else if (cameraTransitioning_)
            UpdateCameraTransition(timeStep);
        else
            MoveCamera(timeStep);
        UpdateCurrentPatchBoundary();
        sectionTimer.GetUSec(true);
        // UpdateOOFOs spawns its own fleet on staggered timers, so it must run
        // before oofos_ is populated — the prior !oofos_.Empty() guard deadlocked spawn.
        if (!oofos_.Empty() || oofosSpawned_ < NUM_OOFOS)
        {
            URHO3D_TIMELINE_EVENT_CAT(timeline, "OOFO Update", "ai");
            UpdateOOFOs(timeStep);
        }
        accumOofo += sectionTimer.GetUSec(true);
        if (minimap_)
        {
            URHO3D_TIMELINE_EVENT_CAT(timeline, "Minimap", "ui");
            UpdateMinimapCamera();
            UpdateMinimapBlips();
        }
        {
            URHO3D_TIMELINE_EVENT_CAT(timeline, "Vitals Panel", "ui");
            UpdateSelectedVitalsPanel();
        }
        accumMinimap += sectionTimer.GetUSec(true);
        if (grassSystem_ && characterNode_)
        {
            Vector3 charPos = characterNode_->GetWorldPosition();
            grassSystem_->SetPhysicsShape(0, Vector4(charPos.x_, charPos.y_, charPos.z_, 1.5f));
        }

        // Water ripples — propagate and feed character splashes
        sectionTimer.GetUSec(true);
        if (rippleSystem_)
        {
            URHO3D_TIMELINE_EVENT_CAT(timeline, "Water Ripples", "fx");
            rippleSystem_->Update(timeStep);
            float waterY = waterNode_ ? waterNode_->GetWorldPosition().y_ : 5.0f;
            if (characterNode_)
            {
                Node* charNode = characterNode_;
                Vector3 charPos = charNode->GetWorldPosition();
                if (charPos.y_ < waterY + 0.5f) // at or just above water surface
                {
                    auto* body = charNode->GetComponent<RigidBody>();
                    float speed = body ? body->GetLinearVelocity().Length() : 0.0f;
                    if (speed > 0.5f)
                        rippleSystem_->StampImpact(charPos.x_, charPos.z_, 1.5f + speed * 0.3f, speed * 0.15f);
                }
            }

            // NPC swim ripples — any creature in CREATURE_SWIM state creates wake
            for (auto it = spawnIdToNode_.Begin(); it != spawnIdToNode_.End(); ++it)
            {
                Node* npcNode = it->second_;
                if (!npcNode)
                    continue;
                auto* creature = npcNode->GetComponent<Creature>(true);
                if (!creature || creature->GetState() != CREATURE_SWIM)
                    continue;
                Vector3 npcPos = npcNode->GetWorldPosition();
                if (npcPos.y_ < waterY + 0.5f)
                    rippleSystem_->StampImpact(npcPos.x_, npcPos.z_, 1.0f, 0.15f);
            }
        }
        accumRipple += sectionTimer.GetUSec(true);

        // Tree LOD — throttled to twice per second
        treeLodTimer_ += timeStep;
        if (treeLodTimer_ > 0.5f)
        {
            treeLodTimer_ = 0.0f;
            UpdateTreeLOD();
        }

        if (renderPath_)
        {
            // Performance Phase 4 — these two ride the per-frame update.
            // MainCameraY changes only when the camera moves vertically;
            // UnderwaterColor changes only when the fog color shifts (slow,
            // weather-driven). Cached uploads skip both when static.
            float camY = cameraNode_->GetWorldPosition().y_;
            SetShaderParamCached("MainCameraY", camY);
            if (zone_)
            {
                Color fog = zone_->GetFogColor();
                SetShaderParamCached("UnderwaterColor", Vector3(fog.r_ * 0.3f, fog.g_ * 0.3f, fog.b_ * 0.3f));
            }

            // Water droplets — two triggers:
            // 1) Camera emerges from water → full drench
            // 2) Character splashing in water (chase/FP cam) → periodic light bursts
            float waterY = waterNode_ ? waterNode_->GetWorldPosition().y_ : 5.0f;
            bool isUnderwater = (camY < waterY);
            float now = GetSubsystem<Time>()->GetElapsedTime();
            bool triggerDroplets = false;

            // Trigger 1: camera emerges from water
            if (wasUnderwater_ && !isUnderwater)
                triggerDroplets = true;

            // Trigger 2: character is in water, camera is above water in chase/FP
            if (!isUnderwater && characterNode_ && (cameraMode_ == CAM_CHASE || cameraMode_ == CAM_FIRSTPERSON))
            {
                Node* charNode = characterNode_;
                if (charNode && charNode->GetWorldPosition().y_ < waterY)
                {
                    splashTimer_ -= timeStep;
                    if (splashTimer_ <= 0.0f)
                    {
                        triggerDroplets = true;
                        // Vary interval — faster when character is moving
                        auto* body = charNode->GetComponent<RigidBody>();
                        float speed = body ? body->GetLinearVelocity().Length() : 0.0f;
                        splashInterval_ = Lerp(2.0f, 0.6f, Clamp(speed / 5.0f, 0.0f, 1.0f));
                        splashTimer_ = splashInterval_;
                    }
                }
                else
                    splashTimer_ = 0.0f;  // reset when character leaves water
            }

            if (triggerDroplets)
            {
                breachTime_ = now;
                renderPath_->SetShaderParameter("BreachTime", breachTime_);
                renderPath_->SetEnabled("WaterDroplets", true);
            }
            if (!isUnderwater && (now - breachTime_) > 6.5f)
                renderPath_->SetEnabled("WaterDroplets", false);
            wasUnderwater_ = isUnderwater;
        }
    }

    // Craft timer update
    if (craftingRecipeId_ >= 0)
        UpdateCraftTimer(timeStep);

    // Building system ghost preview update
    if (buildingSystem_ && buildingSystem_->IsBuildMode())
    {
        auto* cam = cameraNode_ ? cameraNode_->GetComponent<Camera>() : nullptr;
        buildingSystem_->UpdateGhostPreview(cam, terrain_, 5.0f);
    }

    // Context hint — crosshair raycast for interactable detection (every 3 frames)
    if (hud_ && !asyncSceneLoading_ && (cameraMode_ == CAM_CHASE || cameraMode_ == CAM_FIRSTPERSON))
    {
        if (contextHintFrameSkip_++ >= 3)
        {
            contextHintFrameSkip_ = 0;
            UpdateContextHintRaycast();
        }
    }

    // Arrow count HUD — update when bow equipped
    UpdateArrowHUD();

    if (hud_)
        hud_->SetContextHint("");

    // Status icons driven by proximity (near-fire, shelter)
    if (hud_ && campfireNode_ && characterNode_)
    {
        float distToFire = (characterNode_->GetWorldPosition() - campfireNode_->GetWorldPosition()).Length();
        hud_->SetStatusIcon(ICON_NEAR_FIRE, distToFire < 8.0f);
    }
    if (hud_ && buildingSystem_ && characterNode_)
    {
        bool inShelter = false;
        int nearId = buildingSystem_->FindNearestBuilding(characterNode_->GetWorldPosition(), 4.0f);
        if (nearId >= 0)
        {
            PlacedBuilding* pb = buildingSystem_->FindPlacedMutable(nearId);
            if (pb)
            {
                const BuildingTypeInfo* info = buildingSystem_->FindTypeInfo(pb->buildingTypeId);
                if (info && info->sleepCapacity > 0)
                    inShelter = true;
            }
        }
        hud_->SetStatusIcon(ICON_IN_SHELTER, inShelter);
    }

    // Campfire fuel decay (real wallclock seconds, immune to time scrub)
    UpdateCampfireFuel(timeStep);

    // Campfire light flicker — lerp brightness and color toward random targets,
    // then multiply by fireIntensity_ so a dying fire dims to nothing.
    // Skip during EMBERS state — UpdateCampfireFuel handles pulsation directly.
    if (campfireLight_ && activeFirePitState_ != 2 /* PIT_EMBERS */)
    {
        fireFadeTimer_ += timeStep;
        float t = Clamp(fireFadeTimer_ / fireFadeTime_, 0.0f, 1.0f);
        float flickerBrightness = Lerp(fireBrightnessCurrent_, fireBrightnessTarget_, t);
        campfireLight_->SetBrightness(flickerBrightness * fireIntensity_);
        campfireLight_->SetRange(cfBaseLightRange_ * fireIntensity_);
        campfireLight_->SetColor(fireColorCurrent_.Lerp(fireColorTarget_, t));

        if (t >= 1.0f)
        {
            // Reached target — pick new random target relative to slider-set max brightness
            fireBrightnessCurrent_ = fireBrightnessTarget_;
            fireColorCurrent_ = fireColorTarget_;
            fireBrightnessTarget_ = cfBaseLightBrightness_ * Random(0.55f, 1.20f);
            // Random fire color: yellow, orange, or red
            float r = 1.0f;
            float g = Random(0.15f, 0.65f);
            float b = Random(0.0f, g * 0.3f);
            fireColorTarget_ = Color(r, g, b);
            fireFadeTime_ = Random(0.2f, 0.5f);
            fireFadeTimer_ = 0.0f;
        }
    }

    // Celestial update AFTER camera movement — god ray screen position must match
    // the camera state that will be used for rendering this frame
    if (!asyncSceneLoading_ && sunNode_ && loggedIn_)
    {
        HiresTimer celestialTimer;
        UpdateCelestialBodies(timeStep);
        UpdateWeather(timeStep);
        if (rainEmitter_) UpdateRain(timeStep);
        if (snowEmitter_) UpdateSnow(timeStep);
        UpdateLightning(timeStep);
        accumCelestial += celestialTimer.GetUSec(true);

        // Weather Phase 4: rainfall accumulation (every N frames)
        if (rainfallMap_ && terrain_)
        {
            if (++rainfallFrameCounter_ >= RAINFALL_UPDATE_INTERVAL)
            {
                rainfallFrameCounter_ = 0;
                UpdateRainfallAccumulation();
                // Phase 2-3: soil dynamics then vegetation growth
                if (ecosystem_)
                {
                    ecosystem_->UpdateSoil();
                    ecosystem_->UpdateGrowth();
                    ecosystem_->UploadSoilToGPU();  // Phase 19: sync worn paths to shader
                }
            }
        }

        // Feed game state to soundscape — crossfades all ambient layers
        if (soundscape_)
        {
            if (sunLight_)
                soundscape_->SetSunAltitude(-sunLight_->GetNode()->GetDirection().y_);
            soundscape_->SetPrecipitation(weather_.precipitation);
            soundscape_->SetWindSpeed(weather_.windSpeed);
            soundscape_->SetCloudCover(weather_.cloudCover);
        }
    }

    // Driven key system — push drivers, then evaluate all response curves
    if (auto* dks = GetSubsystem<DrivenKeySystem>())
    {
        dks->SetDriver(StringHash("TestDriver"), Clamp(timeOfDay_ / 24.0f, 0.0f, 1.0f));
        dks->Update();
    }

    // --- Perf toggles — apply each frame ---
    if (sunLight_)
        sunLight_->SetCastShadows(shadowsEnabled_);
    if (renderPath_)
    {
        if (!godRaysEnabled_)
        {
            renderPath_->SetEnabled("GodRays", false);
            renderPath_->SetEnabled("MoonRays", false);
        }
        if (!postProcessEnabled_)
        {
            renderPath_->SetEnabled("Underwater", false);
            renderPath_->SetEnabled("WaterDroplets", false);
        }
    }
    if (reflectionCameraNode_)
    {
        auto* reflCam = reflectionCameraNode_->GetComponent<Camera>();
        if (reflCam)
            reflCam->SetViewMask(waterReflectionEnabled_ ? 0x7fffffff : 0x00000000);
    }

    // Biome debug overlay (F7)
    if (biomeDebugOverlay_ && cameraNode_ && terrain_ && hud_)
    {
        Vector3 camPos = cameraNode_->GetWorldPosition();
        BiomeType biome = ClassifyTerrain(camPos);
        float h = terrain_->GetHeight(camPos);
        Vector3 n = terrain_->GetNormal(camPos);
        hud_->SetContextHint("Biome: " + BiomeToString(biome) +
            " | H:" + String((int)h) + " S:" + String(n.y_, 2));
    }

    // --- Wall-clock profiling (every 120 frames, real elapsed time) ---
    if (loggedIn_)
    {
        static HiresTimer wallTimer;
        static long long wallAccum = 0;
        static int wallCount = 0;

        wallAccum += wallTimer.GetUSec(true);  // real microseconds since last frame's measurement
        wallCount++;

        if (wallCount >= 120)
        {
            float avgFrameMs = (float)wallAccum / wallCount / 1000.0f;
            float realFps = (avgFrameMs > 0.0f) ? 1000.0f / avgFrameMs : 0.0f;
            float avgCelestialMs = (float)accumCelestial / wallCount / 1000.0f;
            float avgRippleMs = (float)accumRipple / wallCount / 1000.0f;
            float avgOofoMs = (float)accumOofo / wallCount / 1000.0f;
            float avgMinimapMs = (float)accumMinimap / wallCount / 1000.0f;
            URHO3D_LOGINFOF("PERF[%d frames]: frame=%.1fms (%.1f FPS) | celestial=%.2fms ripple=%.2fms oofo=%.2fms minimap=%.2fms",
                wallCount, avgFrameMs, realFps, avgCelestialMs, avgRippleMs, avgOofoMs, avgMinimapMs);
            wallAccum = 0; wallCount = 0;
            accumFrame = 0; accumCelestial = 0; accumFish = 0; accumContext = 0;
            accumRipple = 0; accumOofo = 0; accumMinimap = 0; accumLogic = 0;
            profileFrameCount = 0;
        }
    }

    // Fumble text animation — float up and fade out (1.5s lifetime)
    if (!fumbleText_.Expired())
    {
        fumbleTextTimer_ += timeStep;
        float t = fumbleTextTimer_;

        IntVector2 pos = fumbleText_->GetPosition();
        pos.y_ = (int)(fumbleTextStartY_ - t * 50.0f);
        fumbleText_->SetPosition(pos);

        float alpha = 1.0f - (t / 1.5f);
        if (alpha <= 0.0f)
        {
            fumbleText_->Remove();
            fumbleText_.Reset();
        }
        else
        {
            fumbleText_->SetOpacity(alpha);
        }
    }

    // Camera shake countdown
    if (cameraShakeTimer_ > 0.0f)
        cameraShakeTimer_ = Max(0.0f, cameraShakeTimer_ - timeStep);

    if (profilerUI_)
    {
        // Use wall-clock elapsed time, not clamped game timeStep (Engine::minFps_ clamps to 0.1s)
        static float lastElapsed = 0.0f;
        float elapsed = GetSubsystem<Time>()->GetElapsedTime();
        float realDt = elapsed - lastElapsed;
        lastElapsed = elapsed;
        float profilerDt = (realDt > 0.0f && realDt < 1.0f) ? realDt : timeStep;
        if (cameraNode_)
            profilerUI_->SetCameraPos(cameraNode_->GetWorldPosition());
        profilerUI_->Update(profilerDt);
    }

    UpdateInspectPanel();
}

void TerrainNode::HandleDrivenKeyOutput(StringHash eventType, VariantMap& eventData)
{
    using namespace DrivenKeyOutput;
    StringHash param = eventData[P_PARAM].GetStringHash();
    float value = eventData[P_VALUE].GetFloat();
    StringHash driver = eventData[P_DRIVER].GetStringHash();
    float driverValue = eventData[P_DRIVERVALUE].GetFloat();
    URHO3D_LOGDEBUGF("DrivenKey: driver %s=%.3f -> driven %s=%.3f",
        driver.ToString().CString(), driverValue,
        param.ToString().CString(), value);
}

void TerrainNode::SetShaderParamCached(const String& name, const Variant& value)
{
    // Performance Phase 4 — skip uploads of values that already match the
    // last-sent value for this name. Cuts ~10 redundant SetShaderParameter
    // calls per frame from the god rays + camera + fog hot path. Cache is
    // invalidated explicitly via InvalidateShaderParamCache() on render path
    // swaps so a new pipeline never inherits stale "we already sent this" state.
    if (!renderPath_)
        return;
    auto it = shaderParamCache_.Find(name);
    if (it != shaderParamCache_.End() && it->second_ == value)
        return;
    shaderParamCache_[name] = value;
    renderPath_->SetShaderParameter(name, value);
}

/// Draw a detection ray with flat colour based on world distance from camera.
/// Close creatures = bright, far creatures = dim. One colour for the whole line.
static void DrawFadedRay(DebugRenderer* debug, const Vector3& from, const Vector3& to, const Color& color)
{
    // 'to' is the cursor (camera near plane). Distance = how far the creature is.
    float dist = (from - to).Length();
    // Fade: full brightness within 50m, dim to 15% at 500m+
    float brightness = Clamp(1.0f - (dist - 50.0f) / 450.0f, 0.15f, 1.0f);
    Color flatColor(color.r_ * brightness, color.g_ * brightness, color.b_ * brightness, color.a_);
    debug->AddLine(from, to, flatColor, true);
}

void TerrainNode::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (asyncSceneLoading_)
        return;

    if (!scene_)
        return;

    auto* debug = scene_->GetComponent<DebugRenderer>();
    if (!debug && loggedIn_)
    {
        URHO3D_LOGWARNING("[DebugRay] No DebugRenderer on scene!");
        return;
    }

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
                        DrawFadedRay(debug, n->GetWorldPosition(), cursorNear, Color::GREEN);
                }
            }

            // Sun & Moon locator rays — visible whenever god rays are enabled
            if (godRaysEnabled_ && sunNode_)
                DrawFadedRay(debug, sunNode_->GetWorldPosition(), cursorNear, Color(1.0f, 0.6f, 0.0f));
            if (godRaysEnabled_ && moonNode_)
                DrawFadedRay(debug, moonNode_->GetWorldPosition(), cursorNear, Color(0.0f, 0.8f, 1.0f));

            // Campfire ray
            if (campfireRayVisible_ && campfireNode_)
                DrawFadedRay(debug, campfireNode_->GetWorldPosition(), cursorNear, campfireLight_ ? campfireLight_->GetColor() : Color(1.0f, 0.5f, 0.0f));

            // Animal rays — lines from cursor to each animal, color coded by species
            // Category toggle + per-species toggles filter
            {
                // Land animals
                if (animalNodes_.Size() > 0)
                {
                    // Per-frame ray count logged at DEBUG level only
                    URHO3D_LOGDEBUGF("[DebugRay] Drawing land animal rays: %u nodes", animalNodes_.Size());
                    for (unsigned i = 0; i < animalNodes_.Size(); ++i)
                    {
                        Node* n = animalNodes_[i].Get();
                        if (!n) continue;
                        const String& name = n->GetName();
                        Color color;
                        bool show = false;
                        if (name == "Rabbit"  && rabbitRayVisible_)    { color = Color(0.2f, 1.0f, 0.2f); show = true; }
                        else if (name == "Deer"    && deerRayVisible_)    { color = Color(0.8f, 0.4f, 1.0f); show = true; }
                        else if (name == "Fox"     && foxRayVisible_)     { color = Color(1.0f, 0.4f, 0.1f); show = true; }
                        else if (name == "Wolf"    && wolfRayVisible_)    { color = Color(0.6f, 0.0f, 0.0f); show = true; }
                        else if (name == "Stag"    && stagRayVisible_)    { color = Color(0.6f, 0.3f, 1.0f); show = true; }
                        else if (name == "Bull"    && bullRayVisible_)    { color = Color(0.5f, 0.25f, 0.0f); show = true; }
                        else if (name == "Cow"     && cowRayVisible_)     { color = Color(0.9f, 0.9f, 0.7f); show = true; }
                        else if (name == "Horse"   && horseRayVisible_)   { color = Color(0.4f, 0.3f, 0.2f); show = true; }
                        else if (name == "Donkey"  && donkeyRayVisible_)  { color = Color(0.5f, 0.5f, 0.5f); show = true; }
                        else if (name == "Alpaca"  && alpacaRayVisible_)  { color = Color(1.0f, 0.9f, 0.8f); show = true; }
                        else if (name == "Husky"   && huskyRayVisible_)   { color = Color(0.3f, 0.5f, 0.8f); show = true; }
                        else if (name == "ShibaInu" && shibaInuRayVisible_) { color = Color(1.0f, 0.6f, 0.2f); show = true; }
                        else if (name == "CaveMan"  && caveManRayVisible_)  { color = Color(0.0f, 1.0f, 1.0f); show = true; }
                        else if (name == "CaveWoman" && caveWomanRayVisible_) { color = Color(1.0f, 0.0f, 1.0f); show = true; }
                        if (show)
                            DrawFadedRay(debug, n->GetWorldPosition(), cursorNear, color);
                    }
                }

                // Water animals — Performance Phase 3: iterate the cached
                // fishNodes_ (populated at spawn time, holds both Fish and
                // SchoolFish via WeakPtr) instead of walking the entire scene
                // graph and string-comparing every child every frame.
                if (waterAnimalRayVisible_)
                {
                    for (unsigned i = 0; i < fishNodes_.Size(); ++i)
                    {
                        Node* n = fishNodes_[i].Get();
                        if (!n) continue;
                        const String& name = n->GetName();
                        if (name == "Fish" && fishRayVisible_)
                            DrawFadedRay(debug, n->GetWorldPosition(), cursorNear, Color(0.2f, 0.7f, 1.0f));
                        else if (name == "SchoolFish" && schoolFishRayVisible_)
                            DrawFadedRay(debug, n->GetWorldPosition(), cursorNear, Color(0.1f, 1.0f, 0.8f));
                    }
                }
            }
        }
    }

    if (grassRayVisible_ && grassSystem_ && grassSystem_->GetGrassNode() && debug)
    {
        Vector3 origin = grassSystem_->GetGrassNode()->GetWorldPosition();
        debug->AddLine(origin, origin + Vector3::UP * 5.0f, Color::YELLOW, false);
    }

    // NPC ring UI — health/stat arcs above NPC heads (always visible)
    if (npcRingUI_ && cameraNode_ && loggedIn_)
    {
        auto* cam = cameraNode_->GetComponent<Camera>();
        if (cam)
        {
            npcRingUI_->Update(scene_, cam);
            npcRingUI_->Render();
        }
    }

    // Gizmo — always draw when a tool is active and a node is selected (not gated by drawDebug_)
    DrawGizmo();

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

    auto* physics = scene_->GetComponent<PhysicsWorld>();
    if (physics)
        physics->DrawDebugGeometry(true);

    // Draw bounding box for selected node (works for nodes without physics)
    if (selectedNode_ && !selectedNode_.Expired() && debug)
    {
        Vector<Drawable*> drawables;
        selectedNode_->GetDerivedComponents<Drawable>(drawables, true);
        for (unsigned i = 0; i < drawables.Size(); ++i)
        {
            BoundingBox bb = drawables[i]->GetWorldBoundingBox();
            debug->AddBoundingBox(bb, Color::YELLOW, false);
        }
    }

    // AnimatedModel bounding boxes — shows all skinned meshes even when underground
    if (debug)
    {
        Vector<AnimatedModel*> models;
        scene_->GetComponents<AnimatedModel>(models, true);
        for (unsigned i = 0; i < models.Size(); ++i)
        {
            if (models[i]->IsEnabledEffective())
            {
                BoundingBox bb = models[i]->GetWorldBoundingBox();
                debug->AddBoundingBox(bb, Color::MAGENTA, true);
            }
        }
    }

    // Vision cones — draw head-forward detection cone for each creature
    if (debug)
    {
        Vector<Creature*> creatures;
        scene_->GetDerivedComponents<Creature>(creatures, true);
        for (unsigned i = 0; i < creatures.Size(); ++i)
        {
            Creature* c = creatures[i];
            if (!c->IsEnabledEffective() || !c->GetNode())
                continue;

            float range = c->GetVisionRange();
            float cosAngle = c->GetVisionCosAngle();
            if (range <= 0.0f)
                continue;

            float halfAngle = acosf(Clamp(cosAngle, -1.0f, 1.0f));
            Vector3 pos = c->GetNode()->GetWorldPosition();
            pos.y_ += 1.0f;  // raise to approximate head height
            Vector3 forward = c->GetNode()->GetWorldDirection();
            forward.y_ = 0.0f;  // flatten to XZ (matches CanSee)
            float fwdLen = forward.Length();
            if (fwdLen < 0.001f)
                continue;
            forward /= fwdLen;

            Color coneColor = c->IsPredator() ? Color(1.0f, 0.3f, 0.2f, 0.8f) : Color(0.3f, 0.8f, 1.0f, 0.6f);
            debug->AddCone(pos, forward, range, halfAngle, coneColor, 12, false);
        }
    }
}

void TerrainNode::RunErosion(int iterations)
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

void TerrainNode::ToggleHierarchyWindow()
{
    if (hierarchyWindow_)
    {
        hierarchyWindow_->SetVisible(!hierarchyWindow_->IsVisible());
        if (hierarchyWindow_->IsVisible())
            BuildHierarchyTree();
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;
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

    SubscribeToEvent(hierarchyList_, E_SELECTIONCHANGED, URHO3D_HANDLER(TerrainNode, HandleHierarchySelectionChanged));
    SubscribeToEvent(hierarchyList_, E_ITEMDOUBLECLICKED, URHO3D_HANDLER(TerrainNode, HandleHierarchyDoubleClick));

    BuildHierarchyTree();
}

void TerrainNode::BuildHierarchyTree()
{
    if (!hierarchyList_ || !scene_)
        return;

    hierarchyList_->DisableInternalLayoutUpdate();
    hierarchyList_->RemoveAllItems();

    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

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

void TerrainNode::PopulateHierarchy(Node* node, Text* parentItem, unsigned& index)
{
    if (!node)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

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

void TerrainNode::HandleHierarchySelectionChanged(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HandleHierarchyDoubleClick(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HighlightInHierarchy(Node* node)
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

void TerrainNode::ToggleInspectorWindow()
{
    if (inspectorWindow_)
    {
        inspectorWindow_->SetVisible(!inspectorWindow_->IsVisible());
        if (inspectorWindow_->IsVisible())
            RebuildInspector();
        return;
    }

    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;
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

void TerrainNode::RebuildInspector()
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

void TerrainNode::CreateNodeSection(Node* node)
{
    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

    String nodeTitle = "Node: " + (node->GetName().Empty() ? String("ID ") + String(node->GetID()) : node->GetName());
    auto* section = CreateCollapsibleSection(inspectorContent_, font, nodeTitle, true);

    // Name
    auto* nameRow = section->CreateChild<UIElement>();
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
    SubscribeToEvent(nameEdit, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleInspectorTransformEdit));

    // Position
    CreateVec3Row(section, "Pos", node->GetPosition(), "Position");

    // Rotation (as Euler)
    Vector3 euler = node->GetRotation().EulerAngles();
    CreateVec3Row(section, "Rot", euler, "Rotation");

    // Scale
    CreateVec3Row(section, "Scale", node->GetScale(), "Scale");
}

LineEdit* TerrainNode::CreateVec3Row(UIElement* parent, const String& label, const Vector3& value, const String& tag)
{
    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

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
        SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleInspectorTransformEdit));
    }

    return nullptr;
}

void TerrainNode::HandleInspectorTransformEdit(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::CreateComponentSection(Component* component, unsigned compIndex)
{
    if (!component)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

    auto* section = CreateCollapsibleSection(inspectorContent_, font, component->GetTypeName(), true);

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

        auto* row = section->CreateChild<UIElement>();
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
            SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleInspectorAttributeEdit));
            break;
        }
        case VAR_BOOL:
        {
            auto* cb = row->CreateChild<CheckBox>();
            cb->SetStyleAuto();
            cb->SetChecked(value.GetBool());
            cb->SetVar(StringHash("CompIndex"), compIndex);
            cb->SetVar(StringHash("AttrIndex"), i);
            SubscribeToEvent(cb, E_TOGGLED, URHO3D_HANDLER(TerrainNode, HandleInspectorCheckToggle));
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
                SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleInspectorAttributeEdit));
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
                SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleInspectorAttributeEdit));
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
                SubscribeToEvent(edit, E_TEXTFINISHED, URHO3D_HANDLER(TerrainNode, HandleInspectorAttributeEdit));
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
                SubscribeToEvent(dd, E_ITEMSELECTED, URHO3D_HANDLER(TerrainNode, HandleInspectorEnumSelect));
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

void TerrainNode::HandleInspectorAttributeEdit(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HandleInspectorCheckToggle(StringHash eventType, VariantMap& eventData)
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

void TerrainNode::HandleInspectorEnumSelect(StringHash eventType, VariantMap& eventData)
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

// ============================================================================
// Debug Log Window
// ============================================================================

void TerrainNode::CreateDebugLogWindow()
{
    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;
    auto* uiRoot = GetSubsystem<UI>()->GetRoot();
    auto* graphics = GetSubsystem<Graphics>();

    debugLogWindow_ = new Window(context_);
    uiRoot->AddChild(debugLogWindow_);
    debugLogWindow_->SetStyleAuto();
    debugLogWindow_->SetPosition(10, graphics->GetHeight() - 310);
    debugLogWindow_->SetSize(graphics->GetWidth() - 20, 280);
    debugLogWindow_->SetResizable(true);
    debugLogWindow_->SetMovable(true);
    debugLogWindow_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
    debugLogWindow_->SetOpacity(0.85f);

    auto* title = debugLogWindow_->CreateChild<Text>();
    title->SetFont(font, 12);
    title->SetText("Debug Log");
    title->SetColor(Color(0.5f, 0.7f, 1.0f));

    debugLogList_ = debugLogWindow_->CreateChild<ListView>();
    debugLogList_->SetStyleAuto();
    debugLogList_->SetMinHeight(240);
}

void TerrainNode::ToggleDebugLogWindow()
{
    if (debugLogWindow_)
    {
        debugLogWindow_->SetVisible(!debugLogWindow_->IsVisible());
        return;
    }
    CreateDebugLogWindow();
}

void TerrainNode::HandleLogMessage(StringHash eventType, VariantMap& eventData)
{
    using namespace LogMessage;

    if (!debugLogList_ || !debugLogWindow_ || !debugLogWindow_->IsVisible())
        return;

    int level = eventData[P_LEVEL].GetI32();
    String message = eventData[P_MESSAGE].GetString();

    auto* cache = GetSubsystem<ResourceCache>();
    Font* font = font_;

    auto* line = new Text(context_);
    line->SetFont(font, 11);
    line->SetText(message);

    // Color by log level
    switch (level)
    {
    case LOG_ERROR:
        line->SetColor(Color(1.0f, 0.3f, 0.3f));
        break;
    case LOG_WARNING:
        line->SetColor(Color(1.0f, 0.8f, 0.2f));
        break;
    case LOG_INFO:
        line->SetColor(Color(0.75f, 0.75f, 0.75f));
        break;
    default:  // LOG_DEBUG, LOG_TRACE
        line->SetColor(Color(0.5f, 0.5f, 0.5f));
        break;
    }

    debugLogList_->AddItem(line);

    // Cap log length
    while (debugLogList_->GetNumItems() > MAX_DEBUG_LOG_LINES)
        debugLogList_->RemoveItem((i32)0);

    // Auto-scroll to bottom
    debugLogList_->EnsureItemVisibility(debugLogList_->GetNumItems() - 1);
}

// ============================================================================
// AuthServer Discovery & Connection
// ============================================================================

void TerrainNode::DiscoverAuthServer()
{
    auto* network = GetSubsystem<Network>();
    URHO3D_LOGINFOF("LAN discovery ping on port %d...", (int)authServerPort_);
    network->DiscoverHosts(authServerPort_);
}

void TerrainNode::HandleHostDiscovered(StringHash eventType, VariantMap& eventData)
{
    using namespace NetworkHostDiscovered;

    String address = eventData[P_ADDRESS].GetString();
    int port = eventData[P_PORT].GetI32();
    VariantMap beacon = eventData[P_BEACON].GetVariantMap();

    String serverName = beacon["ServerName"].GetString();
    if (serverName != "AuthServer")
        return;  // Not our server

    URHO3D_LOGINFOF("Discovered AuthServer at %s:%d", address.CString(), port);

    authServerAddress_ = address;
    authServerPort_ = (unsigned short)port;
    authDiscovering_ = false;

    // Update whichever status text is active
    String statusMsg = "Found " + address + ":" + String(port) + " — connecting...";
    if (loginStatusText_)
        loginStatusText_->SetText(statusMsg);
    if (networkStatusText_)
        networkStatusText_->SetText(statusMsg);

    // Auto-connect
    auto* network = GetSubsystem<Network>();
    if (!gameSceneReady_)
    {
        gameScene_ = new Scene(context_);
        SubscribeToEvent(gameScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(TerrainNode, OnGameSceneLoaded));
        // Creature attachment handled by per-frame scan in HandleUpdate
        gameSceneReady_ = true;
    }
    network->Connect(address, (unsigned short)port, gameScene_);
    UpdateAuthButtonState();
}

void TerrainNode::HandleServerConnected(StringHash eventType, VariantMap& eventData)
{
    authConnected_ = true;
    authDiscovering_ = false;

    bool wasOffline = (offlineMode_ != OFFLINE_NONE);
    // Offline state machine: success — leave the state machine.
    if (wasOffline)
    {
        URHO3D_LOGINFO("Offline mode: local AuthServer connection established");
        offlineMode_ = OFFLINE_NONE;
        offlineRetryTimer_ = 0.0f;
        offlineRetriesLeft_ = 0;
    }
    URHO3D_LOGINFO("Connected to AuthServer at " + authServerAddress_ + ":" + String(authServerPort_));

    auto* network = GetSubsystem<Network>();

    // Game scene was created before Connect() and passed as the connection's scene.
    // MSG_LOADSCENE from the server will load TestScene.xml into gameScene_.

    // Offline without PAKE: send MSG_AUTH_LOGIN immediately with the reserved
    // credentials. No encryption, no key exchange — plaintext over loopback.
    if (wasOffline && !network->HasCredentials())
    {
        URHO3D_LOGINFO("[NetDebug] Offline non-PAKE: sending MSG_AUTH_LOGIN directly");
        Connection* serverConn = network->GetServerConnection();
        if (serverConn)
        {
            VectorBuffer msg;
            msg.WriteString(String(OFFLINE_RESERVED_USERNAME));
            msg.WriteString(String(OFFLINE_RESERVED_PASSWORD));
            serverConn->SendMessage(MSG_AUTH_LOGIN, true, true, msg);
        }
    }

    String statusMsg = "Connected to " + authServerAddress_ + ":" + String(authServerPort_);
    if (loginStatusText_)
    {
        loginStatusText_->SetText(wasOffline ? "Authenticating (offline)..." :
            (network->HasCredentials() ? "Authenticating..." : "Connected — enter credentials"));
        loginStatusText_->SetColor(Color(0.7f, 0.7f, 0.7f));
    }
    if (networkStatusText_)
    {
        networkStatusText_->SetText(statusMsg);
        networkStatusText_->SetColor(Color(0.3f, 1.0f, 0.3f));
    }
    UpdateAuthButtonState();
}

void TerrainNode::HandleServerDisconnected(StringHash eventType, VariantMap& eventData)
{
    bool wasPake = false;
    auto* network = GetSubsystem<Network>();
    if (network && network->HasCredentials())
    {
        wasPake = true;
        network->ClearCredentials();
    }

    authConnected_ = false;
    URHO3D_LOGINFOF("[NetDebug] SERVER DISCONNECTED — loggedIn=%d wasPake=%d uptime=%.1fs",
        (int)loggedIn_, (int)wasPake, GetSubsystem<Time>() ? GetSubsystem<Time>()->GetElapsedTime() : -1.0f);

    // If we were in-world, tear down and return to login screen
    if (loggedIn_)
    {
        ReturnToLogin();
        // loginStatusText_ is now valid (recreated by ReturnToLogin)
        if (loginStatusText_)
        {
            loginStatusText_->SetText("Lost connection to server");
            loginStatusText_->SetColor(Color(1.0f, 0.6f, 0.2f));
        }
        return;
    }

    // Still on login screen — update status text
    if (loginStatusText_)
    {
        // If PAKE was active and we got disconnected before auth completed = wrong password
        loginStatusText_->SetText(wasPake ? "Authentication failed — check password" : "Disconnected from server");
        loginStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
    }
    // networkStatusText_ only exists in-world, handled above
    // if (networkStatusText_)
    // {
    //     networkStatusText_->SetText("Disconnected");
    //     networkStatusText_->SetColor(Color(1.0f, 0.6f, 0.2f));
    // }
    UpdateAuthButtonState();
}

void TerrainNode::HandleConnectFailed(StringHash eventType, VariantMap& eventData)
{
    authConnected_ = false;
    URHO3D_LOGWARNINGF("Failed to connect to AuthServer (offlineMode=%d)", (int)offlineMode_);

    // Offline-mode state machine: first attempt failed → spawn local AuthServer
    // and retry. Subsequent retries use the bounded retry counter.
    if (offlineMode_ == OFFLINE_TRY_CONNECT)
    {
        offlineMode_ = OFFLINE_SPAWN_PENDING;
        OfflineSpawnAuthServer();
        if (offlineMode_ == OFFLINE_NONE)
            return;  // spawn failed, status text already set
        offlineRetryTimer_ = OFFLINE_RETRY_INTERVAL;
        return;
    }
    if (offlineMode_ == OFFLINE_RETRY_CONNECT)
    {
        // Stay in RETRY state — TickOfflineConnect will dial again until
        // OFFLINE_MAX_RETRIES is exhausted.
        offlineRetryTimer_ = OFFLINE_RETRY_INTERVAL;
        if (loginStatusText_)
        {
            loginStatusText_->SetText("Waiting for AuthServer to bind port (" +
                                      String(offlineRetriesLeft_) + " retries left)...");
            loginStatusText_->SetColor(Color(0.7f, 0.7f, 0.9f));
        }
        return;
    }

    // Normal (non-offline) connect failure path.
    String statusMsg = "Connect failed — is AuthServer running?";
    if (loginStatusText_)
    {
        loginStatusText_->SetText(statusMsg);
        loginStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
    }
    if (networkStatusText_)
    {
        networkStatusText_->SetText(statusMsg);
        networkStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
    }
    UpdateAuthButtonState();
}

void TerrainNode::HandleAuthMessage(StringHash eventType, VariantMap& eventData)
{
    using namespace NetworkMessage;
    int msgID = eventData[P_MESSAGEID].GetI32();
    const auto& data = eventData[P_DATA].GetBuffer();

    // MSG_CREATURE_AI_STATE is handled at line ~11820 — no logging needed here

    if (msgID == MSG_PEER_INTRODUCE)
    {
        MemoryBuffer msg(data);
        String peerGuid = msg.ReadString();
        Vector<unsigned char> token(32);
        msg.Read(token.Buffer(), 32);
        int patchX = msg.ReadI32();
        int patchZ = msg.ReadI32();

        // Store peer introduction state
        peerGuid_ = peerGuid;
        peerToken_ = token;
        peerPatchX_ = patchX;
        peerPatchZ_ = patchZ;

        auto* network = GetSubsystem<Network>();
        network->SetExpectedPeerToken(token);
        network->SetPendingPeerGuid(peerGuid);
        network->SetPendingPeerPatch(patchX, patchZ);

        URHO3D_LOGINFOF("Peer introduced: GUID=%s, patch=(%d,%d) — starting NAT punchthrough",
            peerGuid.CString(), patchX, patchZ);

        // Attempt NAT punchthrough to the peer
        network->AttemptNATPunchtrough(peerGuid, scene_);
        return;
    }

    if (msgID == MSG_RELAY_FROM_AUTH)
    {
        // Subserver relaying an AuthServer response to us (subclient)
        MemoryBuffer msg(data);
        String targetUsername = msg.ReadString();
        int innerMsgID = msg.ReadI32();
        unsigned innerSize = msg.ReadU32();
        Vector<unsigned char> innerData(innerSize);
        if (innerSize > 0)
            msg.Read(innerData.Buffer(), innerSize);

        URHO3D_LOGINFOF("Relay from AuthServer via subserver: innerMsgID=%d, size=%u",
            innerMsgID, innerSize);

        // Re-dispatch the inner message through HandleAuthMessage
        VariantMap relayEvent;
        relayEvent[NetworkMessage::P_MESSAGEID] = innerMsgID;
        relayEvent[NetworkMessage::P_DATA].SetBuffer(reinterpret_cast<const void*>(innerData.Buffer()), innerSize);
        HandleAuthMessage(eventType, relayEvent);
        return;
    }

    if (msgID == MSG_EDIT_REJECT)
    {
        MemoryBuffer rejectMsg(data);
        HandleEditReject(rejectMsg);
        return;
    }

    if (msgID == MSG_EDIT_BROADCAST)
    {
        MemoryBuffer broadcastMsg(data);
        HandleEditBroadcast(broadcastMsg);
        return;
    }

    if (msgID == MSG_WEATHER_UPDATE)
    {
        MemoryBuffer msg(data);
        WeatherState forecast;
        forecast.cloudCover = msg.ReadFloat();
        forecast.precipitation = msg.ReadFloat();
        forecast.windSpeed = msg.ReadFloat();
        forecast.windAngle = msg.ReadFloat();
        float temperature = msg.ReadFloat();
        float humidity = msg.ReadFloat();
        String condition = msg.ReadString();

        // Derive fog/ambient multipliers from cloud cover and precipitation
        forecast.fogDensity = 1.0f - forecast.cloudCover * 0.3f - forecast.precipitation * 0.3f;
        forecast.fogDensity = Clamp(forecast.fogDensity, 0.3f, 1.0f);
        forecast.ambientDim = 1.0f - forecast.cloudCover * 0.4f;
        forecast.ambientDim = Clamp(forecast.ambientDim, 0.4f, 1.0f);

        // Set as target — weather system will lerp toward it
        weatherTarget_ = forecast;
        weatherOverride_ = false;  // server authority overrides manual sliders
        weatherTransitionTimer_ = weatherTransitionDuration_;

        URHO3D_LOGINFOF("Weather from server: %s, cloud=%d%%, precip=%d%%, wind=%.0f, temp=%.1fC",
            condition.CString(), (int)(forecast.cloudCover * 100), (int)(forecast.precipitation * 100),
            forecast.windSpeed, temperature);
        return;
    }

    if (msgID == MSG_CELESTIAL_STATE)
    {
        MemoryBuffer msg(data);
        float serverMoonAge = msg.ReadFloat();
        bool eclipse = msg.ReadBool();

        // Server-authoritative moon age — override local calculation
        moonAge_ = serverMoonAge;
        lunarEclipseActive_ = eclipse;

        // Update shadow disc tint for eclipse (blood moon)
        if (moonShadowMat_)
        {
            if (eclipse)
                moonShadowMat_->SetShaderParameter("MatDiffColor", Color(0.3f, 0.05f, 0.02f, 1.0f));
            else
                moonShadowMat_->SetShaderParameter("MatDiffColor", Color(0.0f, 0.0f, 0.0f, 1.0f));
        }
        return;
    }

    if (msgID == MSG_VITAL_UPDATE)
    {
        MemoryBuffer msg(data);
        HandleVitalUpdate(msg);
        return;
    }

    if (msgID == MSG_INVENTORY_UPDATE)
    {
        MemoryBuffer msg(data);
        HandleInventoryUpdate(msg);
        return;
    }

    if (msgID == MSG_INVENTORY_DELTA)
    {
        MemoryBuffer msg(data);
        HandleInventoryDelta(msg);
        return;
    }

    if (msgID == MSG_STORAGE_CONTENTS)
    {
        MemoryBuffer msg(data);
        HandleStorageContents(msg);
        return;
    }

    // Combat result
    if (msgID == MSG_COMBAT_RESULT)
    {
        MemoryBuffer msg(data);
        HandleCombatResult(msgID, msg);
        return;
    }

    // Combat Phase 2 — server-driven creature death (HP=0 on the server).
    if (msgID == MSG_CREATURE_DEATH)
    {
        MemoryBuffer msg(data);
        HandleCreatureDeath(msg);
        return;
    }

    // Death System Phase 1 — server-driven replacement spawn after a kill.
    if (msgID == MSG_SPAWN_CREATURE)
    {
        HandleSpawnCreatureMsg(eventType, eventData);
        return;
    }

    // Server-authoritative creature AI state update (NPC AI Phase 1).
    if (msgID == MSG_CREATURE_AI_STATE)
    {
        MemoryBuffer msg(data);
        HandleCreatureAIState(msg);
        return;
    }

    // NPC Observation System — phenomenon witnessed near an NPC
    if (msgID == MSG_PHENOMENON)
    {
        MemoryBuffer msg(data);
        unsigned spawnId = msg.ReadU32();
        int phenomenonType = msg.ReadI32();
        Vector3 pos = msg.ReadVector3();
        String visualHint = msg.ReadString();

        // Route observation to the witnessing NPC
        auto it = spawnIdToNode_.Find(spawnId);
        if (it != spawnIdToNode_.End() && it->second_)
        {
            auto* npc = it->second_->GetDerivedComponent<HumanNPC>(false);
            if (npc)
                npc->ObservePhenomenon(phenomenonType, pos);
        }

        // Technique discovery — floating text above NPC
        if (phenomenonType == 100)
        {
            URHO3D_LOGINFOF("[Technique] NPC %u discovered %s", spawnId, visualHint.CString());

            // Show discovery text on HUD at NPC's screen position
            auto* cache = GetSubsystem<ResourceCache>();
            auto* ui = GetSubsystem<UI>();
            auto* graphics = GetSubsystem<Graphics>();
            if (ui && graphics && cameraNode_)
            {
                auto* cam = cameraNode_->GetComponent<Camera>();
                if (cam)
                {
                    Vector2 screen = cam->WorldToScreenPoint(pos + Vector3(0, 2.5f, 0));
                    if (screen.x_ >= 0 && screen.x_ <= 1 && screen.y_ >= 0 && screen.y_ <= 1)
                    {
                        auto* font = cache ? cache->GetResource<Font>("Fonts/Anonymous Pro.ttf") : nullptr;
                        if (font)
                        {
                            auto* text = ui->GetRoot()->CreateChild<Text>();
                            text->SetFont(font, 14);
                            text->SetText("Discovered: " + visualHint);
                            text->SetColor(Color(0.2f, 1.0f, 0.6f));
                            text->SetPosition((int)(screen.x_ * graphics->GetWidth()) - 60,
                                              (int)(screen.y_ * graphics->GetHeight()) - 20);
                            text->SetVar("fadeTimer", 3.0f);
                        }
                    }
                }
            }
            return;
        }

        // Spawn client-side visual hint effect at the phenomenon location
        SpawnPhenomenonEffect(pos, visualHint);
        return;
    }

    // Settlement epoch advancement notification
    if (msgID == MSG_EPOCH_CHANGED)
    {
        MemoryBuffer msg(data);
        unsigned campfireId = msg.ReadU32();
        int newEpoch = msg.ReadI32();
        String epochName = msg.ReadString();

        URHO3D_LOGINFOF("[Epoch] Settlement %u advanced to %s (tier %d)",
            campfireId, epochName.CString(), newEpoch);

        // Show epoch advancement on HUD
        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (ui && cache)
        {
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                auto* text = ui->GetRoot()->CreateChild<Text>();
                text->SetFont(font, 20);
                text->SetText("Settlement enters the " + epochName + "!");
                text->SetColor(Color(1.0f, 0.85f, 0.2f));
                text->SetAlignment(HA_CENTER, VA_CENTER);
                text->SetPosition(0, -80);
                text->SetVar("fadeTimer", 5.0f);
            }
        }
        return;
    }

    // Phase 30: NPC received a name at maturity — create floating label
    if (msgID == MSG_NPC_NAMED)
    {
        MemoryBuffer msg(data);
        unsigned spawnId = msg.ReadU32();
        String npcName = msg.ReadString();

        auto it = spawnIdToNode_.Find(spawnId);
        if (it != spawnIdToNode_.End() && !it->second_.Expired())
        {
            Node* node = it->second_;
            node->SetVar("NPCName", npcName);

            // Create floating name label if not already present
            Node* existing = node->GetChild("NameLabel");
            if (!existing)
            {
                auto* cache = GetSubsystem<ResourceCache>();
                auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
                if (font)
                {
                    bool isMale = (node->GetName() == "CaveMan");
                    float headHeight = isMale ? 1.9f : 1.75f;
                    Node* labelNode = node->CreateChild("NameLabel");
                    labelNode->SetPosition(Vector3(0.0f, headHeight, 0.0f));

                    auto* text3D = labelNode->CreateComponent<Text3D>();
                    text3D->SetFont(font, 14);
                    text3D->SetText(npcName);
                    text3D->SetColor(Color(1.0f, 0.95f, 0.8f));
                    text3D->SetAlignment(HA_CENTER, VA_BOTTOM);
                    text3D->SetFaceCameraMode(FC_ROTATE_Y);
                    text3D->SetTextEffect(TE_SHADOW);
                    text3D->SetEffectColor(Color(0.0f, 0.0f, 0.0f, 0.6f));
                }
            }

            URHO3D_LOGINFOF("[Naming] NPC %u named '%s' at maturity", spawnId, npcName.CString());
        }
        return;
    }

    // Phase 21: Chieftain succession notification
    if (msgID == MSG_CHIEFTAIN_CHANGED)
    {
        MemoryBuffer msg(data);
        unsigned campfireId = msg.ReadU32();
        unsigned chiefSpawnId = msg.ReadU32();
        String chiefName = msg.ReadString();
        String skillName = msg.ReadString();
        unsigned char reason = msg.ReadU8();

        static const char* reasonText[] = {"elected as first", "succeeded by death of predecessor to become", "surpassed the old leader to become"};
        const char* rText = (reason <= 2) ? reasonText[reason] : "became";

        String announcement = chiefName + " " + rText + " chieftain (Master " + skillName + ")";
        URHO3D_LOGINFOF("[Chieftain] Settlement %u: %s", campfireId, announcement.CString());

        // HUD announcement
        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (ui && cache)
        {
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                auto* text = ui->GetRoot()->CreateChild<Text>();
                text->SetFont(font, 18);
                text->SetText(announcement);
                text->SetColor(Color(0.9f, 0.75f, 0.2f));
                text->SetAlignment(HA_CENTER, VA_CENTER);
                text->SetPosition(0, -50);
                text->SetVar("fadeTimer", 6.0f);
            }
        }
        return;
    }

    // Phase 34: Innovation discovered — show golden HUD announcement
    if (msgID == MSG_INNOVATION_DISCOVERED)
    {
        MemoryBuffer msg(data);
        unsigned campfireId = msg.ReadU32();
        int innovationType = msg.ReadI32();
        String discovererName = msg.ReadString();
        String innovationName = msg.ReadString();

        URHO3D_LOGINFOF("[Innovation] %s discovered: %s", discovererName.CString(), innovationName.CString());

        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (ui && cache)
        {
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                auto* text = ui->GetRoot()->CreateChild<Text>();
                text->SetFont(font, 18);
                text->SetText(discovererName + " discovered: " + innovationName);
                text->SetColor(Color(0.9f, 0.8f, 0.2f));
                text->SetAlignment(HA_CENTER, VA_CENTER);
                text->SetPosition(0, -110);
                text->SetVar("fadeTimer", 5.0f);
            }
        }
        return;
    }

    // Phase 27: NPC healed another NPC — show floating heal text
    if (msgID == MSG_NPC_HEAL)
    {
        MemoryBuffer msg(data);
        unsigned healerSpawnId = msg.ReadU32();
        unsigned patientSpawnId = msg.ReadU32();
        int healAmount = msg.ReadI32();
        bool curedPoison = msg.ReadBool();

        // Find patient node for screen-space text
        auto patIt = spawnIdToNode_.Find(patientSpawnId);
        if (patIt != spawnIdToNode_.End() && !patIt->second_.Expired())
        {
            Node* patientNode = patIt->second_;
            Vector3 pos = patientNode->GetWorldPosition();

            auto* cache = GetSubsystem<ResourceCache>();
            auto* ui = GetSubsystem<UI>();
            auto* graphics = GetSubsystem<Graphics>();
            if (ui && graphics && cameraNode_)
            {
                auto* cam = cameraNode_->GetComponent<Camera>();
                if (cam)
                {
                    Vector2 screen = cam->WorldToScreenPoint(pos + Vector3(0, 2.5f, 0));
                    if (screen.x_ >= 0 && screen.x_ <= 1 && screen.y_ >= 0 && screen.y_ <= 1)
                    {
                        auto* font = cache ? cache->GetResource<Font>("Fonts/Anonymous Pro.ttf") : nullptr;
                        if (font)
                        {
                            String label = "+" + String(healAmount) + " HP";
                            if (curedPoison)
                                label += " (cured)";

                            auto* text = ui->GetRoot()->CreateChild<Text>();
                            text->SetFont(font, 14);
                            text->SetText(label);
                            text->SetColor(Color(0.2f, 0.9f, 0.4f));
                            text->SetPosition((int)(screen.x_ * graphics->GetWidth()) - 30,
                                              (int)(screen.y_ * graphics->GetHeight()) - 20);
                            text->SetVar("fadeTimer", 2.5f);
                        }
                    }
                }
            }
        }

        URHO3D_LOGINFOF("[Herbalism] NPC %u healed NPC %u (+%d HP%s)",
            healerSpawnId, patientSpawnId, healAmount,
            curedPoison ? ", cured poison" : "");
        return;
    }

    // Fish spawn points from water body analysis
    if (msgID == MSG_FISH_SPAWNS)
    {
        MemoryBuffer msg(data);
        unsigned short count = msg.ReadU16();
        serverFishSpawns_.Clear();
        serverFishSpawns_.Reserve(count);
        for (unsigned i = 0; i < count; ++i)
        {
            FishSpawnInfo sp;
            sp.x = msg.ReadFloat();
            sp.z = msg.ReadFloat();
            sp.depth = msg.ReadFloat();
            serverFishSpawns_.Push(sp);
        }
        URHO3D_LOGINFOF("[Fish] Received %u spawn points from server", count);

        // Recreate fish at server-provided locations
        if (!serverFishSpawns_.Empty())
        {
            // Remove existing fish
            for (unsigned i = 0; i < fishNodes_.Size(); ++i)
            {
                Node* n = fishNodes_[i].Get();
                if (n) n->Remove();
            }
            fishNodes_.Clear();
            CreateFish();
            CreateSchoolFish();
        }
        return;
    }

    // Tapestry inscription — show settlement history text
    if (msgID == MSG_TAPESTRY_INSCRIPTION)
    {
        MemoryBuffer msg(data);
        String inscription = msg.ReadString();

        URHO3D_LOGINFOF("[Tapestry] Inscription: %s", inscription.CString());

        auto* ui = GetSubsystem<UI>();
        auto* cache = GetSubsystem<ResourceCache>();
        if (ui && cache)
        {
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                auto* text = ui->GetRoot()->CreateChild<Text>();
                text->SetFont(font, 16);
                text->SetText("Tapestry woven: " + inscription);
                text->SetColor(Color(0.85f, 0.7f, 0.3f));
                text->SetAlignment(HA_CENTER, VA_CENTER);
                text->SetPosition(0, -110);
                text->SetVar("fadeTimer", 6.0f);
            }
        }

        // Store for tooltip display
        tapestryInscription_ = inscription;
        return;
    }

    // Death log response
    if (msgID == MSG_DEATH_LOG_RESULT)
    {
        MemoryBuffer msg(data);
        HandleDeathLogResult(msg);
        return;
    }

    // Death analytics response
    if (msgID == MSG_DEATH_ANALYTICS_RESULT)
    {
        MemoryBuffer msg(data);
        HandleDeathAnalyticsResult(msg);
        return;
    }

    // Settlement patch ownership
    if (msgID == MSG_SETTLEMENT_CLAIMS)
    {
        MemoryBuffer msg(data);
        HandleSettlementClaims(msg);
        return;
    }

    // Patch claim result
    if (msgID == MSG_PATCH_RESULT)
    {
        MemoryBuffer msg(data);
        HandlePatchResult(msg);
        return;
    }

    // Server-authoritative tree spawn
    if (msgID == MSG_SPAWN_TREE)
    {
        MemoryBuffer msg(data);
        HandleSpawnTree(msg);
        return;
    }
    if (msgID == MSG_REMOVE_TREE)
    {
        MemoryBuffer msg(data);
        unsigned treeId = msg.ReadU32();
        auto it = treeIdToNode_.Find(treeId);
        if (it != treeIdToNode_.End())
        {
            // Spawn a stump at the tree's position before removing
            if (it->second_ && scene_)
            {
                auto* cache = GetSubsystem<ResourceCache>();
                Vector3 pos = it->second_->GetWorldPosition();
                Node* stumpNode = scene_->CreateChild("TreeStump", LOCAL);
                stumpNode->SetPosition(pos);
                auto* stumpModel = stumpNode->CreateComponent<StaticModel>();
                stumpModel->SetModel(cache->GetResource<Model>("Models/Nature/TreeStump.mdl"));
                stumpModel->ApplyMaterialList("Models/Nature/TreeStump.txt");
                stumpModel->SetCastShadows(false);
            }
            if (it->second_)
                it->second_->Remove();
            treeIdToNode_.Erase(it);
        }
        if (focusedTreeId_ == treeId)
            focusedTreeId_ = 0;
        URHO3D_LOGINFOF("[Trees] Tree %u removed (stump placed)", treeId);
        return;
    }

    // Mining result — server tells us what we mined
    if (msgID == MSG_MINE_RESULT)
    {
        MemoryBuffer msg(data);
        int oreType = msg.ReadI32();
        int amount = msg.ReadI32();
        int remaining = msg.ReadI32();

        static const char* oreNames[] = {
            "nothing", "copper", "tin", "iron", "gold", "silver", "coal", "obsidian", "flint"
        };
        const char* oreName = (oreType >= 0 && oreType <= 8) ? oreNames[oreType] : "unknown";

        if (hud_)
        {
            String hint = String("Mined ") + String(amount) + " " + oreName;
            if (remaining <= 0)
                hint += " (deposit exhausted)";
            hud_->SetContextHint(hint);
        }
        URHO3D_LOGINFOF("[Mine] Got %d %s ore (%d remaining)", amount, oreName, remaining);
        return;
    }

    // Deposit exhausted — remove outcrop node at world position
    if (msgID == MSG_DEPOSIT_EXHAUSTED)
    {
        MemoryBuffer msg(data);
        float wx = msg.ReadFloat();
        float wz = msg.ReadFloat();

        if (metalDeposits_ && terrain_)
        {
            IntVector2 pixel = terrain_->WorldToHeightMap(Vector3(wx, 0, wz));
            metalDeposits_->RemoveOutcrop(pixel.x_, pixel.y_);
        }

        // Clear focused outcrop if it matches this position
        if (focusedOutcropNode_)
        {
            Vector3 fp = focusedOutcropNode_->GetWorldPosition();
            if (Abs(fp.x_ - wx) < 1.0f && Abs(fp.z_ - wz) < 1.0f)
                focusedOutcropNode_ = nullptr;
        }

        URHO3D_LOGINFOF("[Mine] Deposit exhausted at (%.1f, %.1f)", wx, wz);
        return;
    }

    // Terrain sync — full heightmap from server (hash mismatch or first connect)
    if (msgID == MSG_TERRAIN_FULLSYNC)
    {
        MemoryBuffer msg(data);
        /*int gridX =*/ msg.ReadI32();
        /*int gridZ =*/ msg.ReadI32();
        unsigned version = msg.ReadU32();
        /*unsigned hash =*/ msg.ReadU32();
        unsigned pngSize = msg.ReadU32();

        if (pngSize > 0 && terrain_ && editableHeightMap_)
        {
            MemoryBuffer pngBuf(msg.GetData() + msg.GetPosition(), pngSize);
            if (editableHeightMap_->Load(pngBuf))
            {
                terrain_->ApplyHeightMap();
                terrainVersion_ = version;
                URHO3D_LOGINFOF("[TerrainSync] Full sync applied — %u bytes, version=%u", pngSize, version);
            }
        }
        return;
    }

    // Terrain sync — incremental journal replay
    if (msgID == MSG_TERRAIN_JOURNAL)
    {
        MemoryBuffer msg(data);
        /*int gridX =*/ msg.ReadI32();
        /*int gridZ =*/ msg.ReadI32();
        unsigned editCount = msg.ReadU32();
        URHO3D_LOGINFOF("[TerrainSync] Journal received — %u edits to replay", editCount);

        // Read trailing version/hash after skipping edit data
        if (msg.GetSize() > msg.GetPosition() + 8)
        {
            msg.Seek(msg.GetSize() - 8);
            unsigned version = msg.ReadU32();
            /*unsigned hash =*/ msg.ReadU32();
            terrainVersion_ = version;
        }
        return;
    }

    // Building system messages
    if (msgID == MSG_BUILD_RESULT || msgID == MSG_BUILDING_SPAWN || msgID == MSG_BUILDING_REMOVE ||
        msgID == MSG_GATE_STATE || msgID == MSG_BUILDING_HP || msgID == MSG_RESPAWN_SET)
    {
        MemoryBuffer msg(data);
        HandleBuildMessage(msgID, msg);
        return;
    }

    // Resource map server authority messages
    if (msgID == MSG_RESOURCE_DEPLETED)
    {
        MemoryBuffer msg(data);
        HandleResourceDepleted(msg);
        return;
    }

    // Trap system messages (Resource Chain Phase 2)
    if (msgID == MSG_TRAP_SPAWNED)
    {
        MemoryBuffer msg(data);
        unsigned nodeId = msg.ReadU32();
        int      itemId = msg.ReadI32();
        float    px     = msg.ReadFloat();
        float    py     = msg.ReadFloat();
        float    pz     = msg.ReadFloat();
        float    rot    = msg.ReadFloat();

        // Create the trap node. Use a simple StaticModel with the item's model
        // looked up from GameDB. Store the server's node id in a Var for later
        // removal lookup.
        Node* trapNode = scene_->CreateChild("PlacedTrap");
        trapNode->SetPosition(Vector3(px, py, pz));
        trapNode->SetRotation(Quaternion(rot, Vector3::UP));
        trapNode->SetVar("ServerTrapId", (int)nodeId);
        trapNode->SetVar("TrapItemId", itemId);

        if (gameDB_)
        {
            ItemInfo info;
            if (gameDB_->GetItem(itemId, info) && !info.model.Empty())
            {
                auto* cache = GetSubsystem<ResourceCache>();
                auto* model = cache->GetResource<Model>(info.model);
                if (model)
                {
                    auto* sm = trapNode->CreateComponent<StaticModel>();
                    sm->SetModel(model);
                }
            }
        }
        // Direct lookup table for MSG_TRAP_REMOVED + the trap-check scanner.
        trapNodes_[nodeId] = WeakPtr<Node>(trapNode);
        URHO3D_LOGINFOF("Trap spawned: nodeId=%u itemId=%d at (%.1f,%.1f,%.1f)",
                         nodeId, itemId, px, py, pz);
        return;
    }

    if (msgID == MSG_TRAP_REMOVED)
    {
        MemoryBuffer msg(data);
        unsigned nodeId = msg.ReadU32();

        // Direct HashMap lookup — replaces a per-removal scene walk.
        auto it = trapNodes_.Find(nodeId);
        if (it != trapNodes_.End() && it->second_)
            it->second_->Remove();
        trapNodes_.Erase(nodeId);
        trapCheckSent_.Erase(nodeId);
        return;
    }

    if (msgID == MSG_TRAP_TRIGGERED)
    {
        // Server has decided this trap caught this creature. Apply the visual
        // result locally: snap the creature to the trap position and put it in
        // CREATURE_TRAPPED state. The creature is already registered in the
        // server's creatureStates_ from this same path, so a follow-up harvest
        // (E key) will work.
        MemoryBuffer msg(data);
        unsigned trapNodeId     = msg.ReadU32();
        unsigned creatureNodeId = msg.ReadU32();
        float    cx             = msg.ReadFloat();
        float    cy             = msg.ReadFloat();
        float    cz             = msg.ReadFloat();
        (void)cx; (void)cy; (void)cz;  // server reflects the position back; we trust the local node

        Node* creatureNode = scene_->GetNode(creatureNodeId);
        if (!creatureNode)
        {
            URHO3D_LOGWARNINGF("MSG_TRAP_TRIGGERED: unknown creature node %u (trap=%u)",
                                creatureNodeId, trapNodeId);
            return;
        }
        Creature* creature = creatureNode->GetDerivedComponent<Creature>();
        if (!creature)
        {
            URHO3D_LOGWARNINGF("MSG_TRAP_TRIGGERED: node %u has no Creature component", creatureNodeId);
            return;
        }

        // Snap to trap position so the catch is visually obvious.
        auto trapIt = trapNodes_.Find(trapNodeId);
        if (trapIt != trapNodes_.End() && trapIt->second_)
            creatureNode->SetWorldPosition(trapIt->second_->GetWorldPosition());

        creature->SetState(CREATURE_TRAPPED);
        URHO3D_LOGINFOF("Trap caught: trap=%u creature=%u (%s)",
                         trapNodeId, creatureNodeId, creatureNode->GetName().CString());
        return;
    }

    // --- Farming messages ---
    if (msgID == MSG_CROP_SPAWNED)
    {
        MemoryBuffer msg(data);
        int cropId     = msg.ReadI32();
        int seedItemId = msg.ReadI32();
        float px       = msg.ReadFloat();
        float py       = msg.ReadFloat();
        float pz       = msg.ReadFloat();
        unsigned char stage = (unsigned char)msg.ReadU8();

        // Check if this crop already exists (growth update)
        auto existIt = cropNodes_.Find(cropId);
        if (existIt != cropNodes_.End() && existIt->second_)
        {
            // Update visual scale for growth stage
            Node* node = existIt->second_;
            float scale = 0.2f + stage * 0.267f;  // 0=0.2, 1=0.47, 2=0.73, 3=1.0
            node->SetScale(scale);
            URHO3D_LOGINFOF("[Farming] Crop %d advanced to stage %d", cropId, stage);
            return;
        }

        // Create new crop node
        Node* cropNode = scene_->CreateChild("PlacedCrop");
        cropNode->SetPosition(Vector3(px, py, pz));
        float scale = 0.2f + stage * 0.267f;
        cropNode->SetScale(scale);

        // Load model — try crop-specific model, fall back to generic
        auto* cache = GetSubsystem<ResourceCache>();
        String modelPath;

        // Look up model from GameDB crop_types
        if (gameDB_)
        {
            CropTypeInfo cropType;
            if (gameDB_->GetCropType(seedItemId, cropType) && !cropType.model.Empty())
                modelPath = cropType.model;
        }

        auto* staticModel = cropNode->CreateComponent<StaticModel>();
        Model* model = nullptr;
        if (!modelPath.Empty())
            model = cache->GetResource<Model>(modelPath);
        if (!model)
            model = cache->GetResource<Model>("Models/Box.mdl");  // Placeholder
        if (model)
            staticModel->SetModel(model);

        // Tilled soil patch under the crop — flat brown disk
        auto* soilMdl = cache->GetResource<Model>("Models/Plane.mdl");
        auto* soilMat = cache->GetResource<Material>("Materials/Stone.xml");
        if (soilMdl && soilMat)
        {
            Node* soilNode = cropNode->CreateChild("SoilPatch", LOCAL);
            soilNode->SetPosition(Vector3(0.0f, 0.01f, 0.0f));  // just above ground
            soilNode->SetScale(Vector3(1.5f, 1.0f, 1.5f));
            auto* soilSm = soilNode->CreateComponent<StaticModel>();
            soilSm->SetModel(soilMdl);
            SharedPtr<Material> dirtMat = soilMat->Clone();
            dirtMat->SetShaderParameter("MatDiffColor", Color(0.35f, 0.25f, 0.15f));
            soilSm->SetMaterial(dirtMat);
            soilSm->SetCastShadows(false);
        }

        // Store for tracking
        cropNode->SetVar("CropId", cropId);
        cropNode->SetVar("SeedItemId", seedItemId);
        cropNode->SetVar("GrowthStage", (int)stage);
        cropNodes_[cropId] = WeakPtr<Node>(cropNode);

        URHO3D_LOGINFOF("[Farming] Crop spawned: id=%d seed=%d at (%.1f,%.1f,%.1f) stage=%d",
            cropId, seedItemId, px, py, pz, stage);
        return;
    }

    if (msgID == MSG_CROP_REMOVED)
    {
        MemoryBuffer msg(data);
        int cropId = msg.ReadI32();

        auto it = cropNodes_.Find(cropId);
        if (it != cropNodes_.End())
        {
            if (it->second_)
                it->second_->Remove();
            cropNodes_.Erase(it);
        }
        URHO3D_LOGINFOF("[Farming] Crop removed: id=%d", cropId);
        return;
    }

    if (msgID == MSG_HARVEST_RESULT)
    {
        MemoryBuffer msg(data);
        unsigned targetNodeId = msg.ReadU32();
        int      count        = msg.ReadI32();
        String summary;
        for (int i = 0; i < count; ++i)
        {
            int itemId = msg.ReadI32();
            int qty    = msg.ReadI32();
            ItemInfo info;
            if (gameDB_ && gameDB_->GetItem(itemId, info))
                summary += String(qty) + "x " + info.name + " ";
            else
                summary += String(qty) + "x item" + String(itemId) + " ";
        }
        URHO3D_LOGINFOF("Harvested target=%u: %s", targetNodeId, summary.CString());
        return;
    }

    if (msgID == MSG_RESOURCE_PATCH)
    {
        MemoryBuffer msg(data);
        HandleResourcePatch(msg);
        return;
    }

    if (msgID == MSG_NEW_TERRAIN)
    {
        MemoryBuffer msg(data);
        HandleNewTerrain(msg);
        return;
    }

    if (msgID == MSG_AUTH_RESULT)
    {
        MemoryBuffer msg(data);
        int originalMsgID = msg.ReadI32();  // echoed msg ID (LOGIN or REGISTER)
        bool success = msg.ReadBool();
        String message = msg.ReadString();

        URHO3D_LOGINFOF("Auth result: %s — %s", success ? "OK" : "FAIL", message.CString());

        if (originalMsgID == MSG_AUTH_LOGIN)
        {
            adminLevel_ = msg.ReadI32();
            serverSceneName_ = msg.ReadString();  // scene name packed into auth result
            int patchCount = msg.ReadI32();
            ownedPatches_.Clear();
            for (int i = 0; i < patchCount; ++i)
            {
                int px = msg.ReadI32();
                int pz = msg.ReadI32();
                ownedPatches_.Push(IntVector2(px, pz));
            }
            // First patch is the home patch (for camera positioning)
            if (!ownedPatches_.Empty())
            {
                ownedPatchX_ = ownedPatches_[0].x_;
                ownedPatchZ_ = ownedPatches_[0].y_;
            }
            URHO3D_LOGINFOF("Received %u owned patches, adminLevel=%d:", ownedPatches_.Size(), adminLevel_);
            for (unsigned pi = 0; pi < ownedPatches_.Size(); ++pi)
                URHO3D_LOGINFOF("  patch[%u] = (%d, %d)", pi, ownedPatches_[pi].x_, ownedPatches_[pi].y_);

            if (success)
            {
                loggedInUsername_ = usernameEdit_ ? usernameEdit_->GetText().Trimmed() : "Unknown";
                URHO3D_LOGINFOF("[NetDebug] Login success — user=%s admin=%d scene=%s patch=(%d,%d)",
                    loggedInUsername_.CString(), adminLevel_, serverSceneName_.CString(),
                    ownedPatchX_, ownedPatchZ_);

                // Register our NAT GUID with AuthServer for peer introductions
                RegisterGuidWithAuthServer();

                loginTimer_.Reset();
                loginTimerActive_ = true;
                firstFramePending_ = true;

                // Show AI Tuning button for admins (Phase 4)
                if (adminLevel_ > 0 && menuBar_)
                {
                    auto* btn = menuBar_->GetChild("AITuningBtn", false);
                    if (btn)
                        btn->SetVisible(true);
                }

                URHO3D_LOGINFO("[NetDebug] Calling EnterWorld...");
                EnterWorld();
                URHO3D_LOGINFO("[NetDebug] EnterWorld returned");

                // Request terrain sync — server replays journal or sends full heightmap
                SendTerrainSync();
            }
            else if (loginStatusText_)
            {
                loginStatusText_->SetText(message);
                loginStatusText_->SetColor(Color(1.0f, 0.3f, 0.3f));
            }
        }
        else if (originalMsgID == MSG_AUTH_REGISTER)
        {
            if (loginStatusText_)
            {
                loginStatusText_->SetText(message);
                loginStatusText_->SetColor(success ? Color(0.3f, 1.0f, 0.3f) : Color(1.0f, 0.3f, 0.3f));
            }
        }
    }

    // Possession system — server responses
    if (msgID == MSG_POSSESS)
    {
        MemoryBuffer msg(data);
        unsigned npcNodeId = msg.ReadU32();
        int npcPlayerId = msg.ReadI32();
        bool success = msg.ReadBool();

        if (success)
        {
            possessedNPCPlayerId_ = npcPlayerId;
            URHO3D_LOGINFOF("Possession confirmed: NPC %u, playerId %d", npcNodeId, npcPlayerId);
        }
        else
        {
            // Server rejected — undo local possession
            URHO3D_LOGWARNINGF("Possession rejected: NPC %u already possessed by another player", npcNodeId);
            if (possessedNPC_ && possessedNPC_->GetID() == npcNodeId)
                UnpossessNPC();
        }
        return;
    }

    if (msgID == MSG_UNPOSSESS)
    {
        MemoryBuffer msg(data);
        unsigned npcNodeId = msg.ReadU32();
        possessedNPCPlayerId_ = -1;
        URHO3D_LOGINFOF("Unpossession confirmed: NPC %u", npcNodeId);
        return;
    }

    // --- Trade System messages ---
    if (msgID == MSG_TRADE_INCOMING)
    {
        MemoryBuffer msg(data);
        HandleTradeIncoming(msg);
        return;
    }
    if (msgID == MSG_TRADE_ACCEPT)
    {
        MemoryBuffer msg(data);
        HandleTradeAccepted(msg);
        return;
    }
    if (msgID == MSG_TRADE_UPDATE)
    {
        MemoryBuffer msg(data);
        HandleTradeUpdate(msg);
        return;
    }
    if (msgID == MSG_TRADE_LOCK)
    {
        MemoryBuffer msg(data);
        HandleTradeLock(msg);
        return;
    }
    if (msgID == MSG_TRADE_COMPLETE)
    {
        MemoryBuffer msg(data);
        HandleTradeComplete(msg);
        return;
    }
    if (msgID == MSG_TRADE_CANCEL)
    {
        MemoryBuffer msg(data);
        HandleTradeCancel(msg);
        return;
    }

    if (msgID == MSG_TUNING_DATA)
    {
        MemoryBuffer msg(data);
        unsigned short count = msg.ReadU16();
        tuningEntries_.Clear();
        for (unsigned short i = 0; i < count; ++i)
        {
            TuningEntry e;
            e.key = msg.ReadString();
            e.value = msg.ReadFloat();
            e.label = msg.ReadString();
            e.category = msg.ReadString();
            e.minVal = msg.ReadFloat();
            e.maxVal = msg.ReadFloat();
            tuningEntries_.Push(e);
        }
        URHO3D_LOGINFOF("[AI Tuning] Received %u tuning parameters", count);
        if (tuningPanel_)
            PopulateTuningPanel();
        return;
    }
}

void TerrainNode::HandleAsyncLoadFinished(StringHash /*eventType*/, VariantMap& eventData)
{
    asyncSceneLoading_ = false;
    // Single entry point for all post-scene-load work
    OnSceneLoaded();
}

void TerrainNode::HandleAuthConnectButton(StringHash eventType, VariantMap& eventData)
{
    auto* network = GetSubsystem<Network>();

    if (authConnected_)
    {
        // Disconnect
        network->Disconnect(100);
        authConnected_ = false;
        URHO3D_LOGINFO("Disconnected from AuthServer (manual)");
    }
    else
    {
        // Connect to localhost
        URHO3D_LOGINFOF("Connecting to AuthServer at %s:%d...", authServerAddress_.CString(), (int)authServerPort_);
        if (!gameSceneReady_)
        {
            gameScene_ = new Scene(context_);
            SubscribeToEvent(gameScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(TerrainNode, OnGameSceneLoaded));
            gameSceneReady_ = true;
        }
        network->Connect(authServerAddress_, authServerPort_, gameScene_);
    }
    UpdateAuthButtonState();
}

void TerrainNode::UpdateAuthButtonState()
{
    if (!authBtnLabel_)
        return;

    if (authConnected_)
    {
        authBtnLabel_->SetText("AuthServer: ON");
        authBtnLabel_->SetColor(Color(0.2f, 1.0f, 0.2f));
    }
    else
    {
        authBtnLabel_->SetText("AuthServer: OFF");
        authBtnLabel_->SetColor(Color(1.0f, 0.3f, 0.3f));
    }
}

// ============================================================================
// Peer Offload (NAT Punchthrough)
// ============================================================================

void TerrainNode::RegisterGuidWithAuthServer()
{
    auto* network = GetSubsystem<Network>();
    const String& guid = network->GetGUID();

    if (guid.Empty())
    {
        URHO3D_LOGWARNING("No NAT GUID available — NAT client may not be started");
        return;
    }

    auto* serverConn = network->GetServerConnection();
    if (!serverConn)
        return;

    VectorBuffer msg;
    msg.WriteString(guid);
    serverConn->SendMessage(MSG_REGISTER_GUID, true, true, msg);
    URHO3D_LOGINFOF("Registered GUID '%s' with AuthServer", guid.CString());
}

void TerrainNode::HandlePeerConnected(StringHash eventType, VariantMap& eventData)
{
    using namespace PeerConnected;
    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    bool weAreSubServer = eventData[P_ISSUBSERVER].GetBool();

    isPeered_ = true;
    isSubServer_ = weAreSubServer;
    peerConnection_ = connection;

    if (weAreSubServer)
    {
        // We are the subserver — replicate our scene to the subclient
        URHO3D_LOGINFO("Peered as SUBSERVER — starting replication to subclient");
        connection->SetScene(scene_);
    }
    else
    {
        // We are the subclient — disconnect from AuthServer, receive replication from subserver
        URHO3D_LOGINFO("Peered as SUBCLIENT — migrating from AuthServer to subserver");

        auto* network = GetSubsystem<Network>();
        network->Disconnect(100);  // disconnect from AuthServer
        authConnected_ = false;
        UpdateAuthButtonState();
    }
}

void TerrainNode::HandleNATPunchFailed(StringHash eventType, VariantMap& eventData)
{
    auto* network = GetSubsystem<Network>();

    // Only relevant if we have a pending peer introduction
    if (!network->IsPeerPending())
        return;

    URHO3D_LOGERROR("NAT punchthrough failed for peer at patch (" +
        String(peerPatchX_) + "," + String(peerPatchZ_) + ")");

    // Clear pending state — we stay connected to AuthServer as normal
    network->ClearPendingPeer();
    peerGuid_.Clear();
    peerToken_.Clear();
}

void TerrainNode::HandlePeerDisconnected(StringHash eventType, VariantMap& eventData)
{
    if (!isPeered_)
        return;

    URHO3D_LOGINFO("Peer connection lost");

    if (!isSubServer_)
    {
        // We're a subclient — lost our subserver. Reconnect to AuthServer.
        URHO3D_LOGINFO("Subclient: subserver lost — reconnecting to AuthServer");
        isPeered_ = false;
        peerConnection_.Reset();
        peerGuid_.Clear();

        // Reconnect to AuthServer
        auto* network = GetSubsystem<Network>();
        if (!gameSceneReady_)
        {
            gameScene_ = new Scene(context_);
            SubscribeToEvent(gameScene_, E_ASYNCLOADFINISHED, URHO3D_HANDLER(TerrainNode, OnGameSceneLoaded));
            gameSceneReady_ = true;
        }
        network->Connect(authServerAddress_, authServerPort_, gameScene_);
    }
    else
    {
        // We're a subserver — lost our subclient. Just clean up.
        URHO3D_LOGINFO("Subserver: subclient disconnected");
        isPeered_ = false;
        isSubServer_ = false;
        peerConnection_.Reset();
        peerGuid_.Clear();
    }
}

void TerrainNode::RelayToAuth(int innerMsgID, const VectorBuffer& innerPayload)
{
    if (!isPeered_ || isSubServer_ || !peerConnection_)
    {
        URHO3D_LOGWARNING("RelayToAuth: not a peered subclient — cannot relay");
        return;
    }

    VectorBuffer relay;
    relay.WriteString(loggedInUsername_);
    relay.WriteI32(innerMsgID);
    relay.WriteU32(innerPayload.GetSize());
    relay.Write(innerPayload.GetData(), innerPayload.GetSize());
    peerConnection_->SendMessage(MSG_RELAY_TO_AUTH, true, true, relay);
}

// ---------------------------------------------------------------------------
// Terrain sync — request server to bring our heightmap up to date
// ---------------------------------------------------------------------------

void TerrainNode::SendTerrainSync()
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    // Compute a simple hash of local heightmap for server comparison
    unsigned hash = 0;
    if (editableHeightMap_)
    {
        unsigned char* data = editableHeightMap_->GetData();
        int size = editableHeightMap_->GetWidth() * editableHeightMap_->GetHeight() * editableHeightMap_->GetComponents();
        // FNV-1a hash — fast, good distribution
        hash = 2166136261u;
        for (int i = 0; i < size; i += 4)  // sample every 4th byte for speed
        {
            hash ^= data[i];
            hash *= 16777619u;
        }
    }

    // Send for grid cell (0,0) — single-terrain world
    VectorBuffer buf;
    buf.WriteI32(0);               // gridX
    buf.WriteI32(0);               // gridZ
    buf.WriteU32(terrainVersion_); // client version (0 on first connect)
    buf.WriteU32(hash);            // client heightmap hash
    serverConn->SendMessage(MSG_TERRAIN_SYNC, true, true, buf);
    URHO3D_LOGINFOF("Sent MSG_TERRAIN_SYNC: grid(0,0) version=%u hash=0x%08X", terrainVersion_, hash);
}

// ---------------------------------------------------------------------------
// Server-authoritative edits
// ---------------------------------------------------------------------------

void TerrainNode::SendEditMessage(int msgID, const VectorBuffer& payload)
{
    auto* network = GetSubsystem<Network>();
    if (!network)
        return;

    // Direct connection to AuthServer
    auto* serverConn = network->GetServerConnection();
    if (serverConn && authConnected_)
    {
        serverConn->SendMessage(msgID, true, true, payload);
        return;
    }

    // Relay via peer subserver
    if (isPeered_ && !isSubServer_ && peerConnection_)
    {
        RelayToAuth(msgID, payload);
        return;
    }
}

void TerrainNode::SendWaterEdit(const Vector3& worldPos)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    // Server format: worldX float, worldZ float, radius float, strength float, raise bool
    VectorBuffer buf;
    buf.WriteFloat(worldPos.x_);
    buf.WriteFloat(worldPos.z_);
    buf.WriteFloat(brushRadius_ * (terrain_ ? terrain_->GetSpacing().x_ : 1.0f));
    buf.WriteFloat(brushStrength_);
    buf.WriteBool(true);  // river brush always raises water
    serverConn->SendMessage(MSG_WATER_EDIT, true, true, buf);
}

void TerrainNode::SendTerrainEdit(const Vector3& worldPos, float timeStep)
{
    if (replayingBroadcast_)
        return;
    if (!authConnected_ && !isPeered_)
        return;
    if (!terrain_ || !editableHeightMap_)
        return;

    // Rate limit: only send every EDIT_SEND_INTERVAL seconds
    editSendAccumulator_ += timeStep;
    if (editSendAccumulator_ < EDIT_SEND_INTERVAL)
        return;
    editSendAccumulator_ = 0.0f;

    // Snapshot the brush region for potential rollback
    unsigned editID = nextEditID_++;
    int hmW = editableHeightMap_->GetWidth();
    int hmH = editableHeightMap_->GetHeight();
    IntVector2 center = terrain_->WorldToHeightMap(worldPos);
    int radius = (int)brushRadius_;
    int x0 = Max(center.x_ - radius, 0);
    int y0 = Max(center.y_ - radius, 0);
    int x1 = Min(center.x_ + radius, hmW - 1);
    int y1 = Min(center.y_ + radius, hmH - 1);
    int rw = x1 - x0 + 1;
    int rh = y1 - y0 + 1;

    if (rw > 0 && rh > 0)
    {
        TerrainEditSnapshot snap;
        snap.regionMin = IntVector2(x0, y0);
        snap.regionSize = IntVector2(rw, rh);
        int comps = editableHeightMap_->GetComponents();
        snap.heightData.Resize(rw * rh * comps);
        unsigned char* src = editableHeightMap_->GetData();
        for (int row = 0; row < rh; ++row)
        {
            int srcIdx = ((y0 + row) * hmW + x0) * comps;
            int dstIdx = row * rw * comps;
            memcpy(&snap.heightData[dstIdx], &src[srcIdx], rw * comps);
        }
        // Mode 6 (river) also modifies waterMap_ — snapshot for rollback
        if (brushMode_ == 6 && waterMap_)
        {
            int wComps = waterMap_->GetComponents();
            snap.waterData.Resize(rw * rh * wComps);
            unsigned char* wSrc = waterMap_->GetData();
            for (int row = 0; row < rh; ++row)
            {
                int srcIdx = ((y0 + row) * hmW + x0) * wComps;
                int dstIdx = row * rw * wComps;
                memcpy(&snap.waterData[dstIdx], &wSrc[srcIdx], rw * wComps);
            }
        }
        terrainEditSnapshots_[editID] = snap;

        // Limit snapshot history to prevent unbounded growth
        if (terrainEditSnapshots_.Size() > 100)
        {
            unsigned oldest = nextEditID_ - 100;
            for (auto it = terrainEditSnapshots_.Begin(); it != terrainEditSnapshots_.End();)
            {
                if (it->first_ < oldest)
                    it = terrainEditSnapshots_.Erase(it);
                else
                    ++it;
            }
        }
    }

    VectorBuffer payload;
    payload.WriteU32(editID);
    payload.WriteVector3(worldPos);
    payload.WriteI32(brushMode_);
    payload.WriteI32(brushShape_);
    payload.WriteFloat(brushRadius_);
    payload.WriteFloat(brushStrength_);
    payload.WriteFloat(smoothStrength_);
    payload.WriteFloat(brushRotation_);
    payload.WriteFloat(timeStep);
    payload.WriteFloat(lockedFlattenHeight_);
    SendEditMessage(MSG_EDIT_TERRAIN, payload);
}

void TerrainNode::SendObjectCreate(Node* node, const Vector3& position, const Vector3& surfaceNormal)
{
    if (!authConnected_ && !isPeered_)
        return;
    if (!node)
        return;

    unsigned editID = nextEditID_++;
    String xmlData = SerializeNode(node);

    // Snapshot for rollback: if rejected, delete the node
    ObjectEditSnapshot snap;
    snap.subtype = 0;  // create
    snap.nodeID = node->GetID();
    snap.xmlData = xmlData;
    snap.position = position;
    objectEditSnapshots_[editID] = snap;

    VectorBuffer payload;
    payload.WriteU32(editID);
    payload.WriteU8(0);  // subtype: create
    payload.WriteString(xmlData);
    payload.WriteVector3(position);
    payload.WriteVector3(surfaceNormal);
    SendEditMessage(MSG_EDIT_OBJECT, payload);
}

void TerrainNode::SendObjectDelete(unsigned nodeID)
{
    if (!authConnected_ && !isPeered_)
        return;

    // Snapshot for rollback: if rejected, recreate the node
    Node* node = scene_->GetNode(nodeID);
    unsigned editID = nextEditID_++;

    ObjectEditSnapshot snap;
    snap.subtype = 1;  // delete
    snap.nodeID = nodeID;
    if (node)
    {
        snap.xmlData = SerializeNode(node);
        snap.position = node->GetPosition();
        snap.rotation = node->GetRotation();
        snap.scale = node->GetScale();
    }
    objectEditSnapshots_[editID] = snap;

    VectorBuffer payload;
    payload.WriteU32(editID);
    payload.WriteU8(1);  // subtype: delete
    payload.WriteU32(nodeID);
    SendEditMessage(MSG_EDIT_OBJECT, payload);
}

void TerrainNode::SendObjectTransform(unsigned nodeID, const Vector3& pos, const Quaternion& rot, const Vector3& scale)
{
    if (!authConnected_ && !isPeered_)
        return;

    Node* node = scene_->GetNode(nodeID);
    unsigned editID = nextEditID_++;

    ObjectEditSnapshot snap;
    snap.subtype = 2;  // transform
    snap.nodeID = nodeID;
    if (node)
    {
        snap.position = node->GetPosition();
        snap.rotation = node->GetRotation();
        snap.scale = node->GetScale();
    }
    objectEditSnapshots_[editID] = snap;

    VectorBuffer payload;
    payload.WriteU32(editID);
    payload.WriteU8(2);  // subtype: transform
    payload.WriteU32(nodeID);
    payload.WriteVector3(pos);
    payload.WriteQuaternion(rot);
    payload.WriteVector3(scale);
    SendEditMessage(MSG_EDIT_OBJECT, payload);
}

void TerrainNode::HandleEditReject(MemoryBuffer& msg)
{
    unsigned editID = msg.ReadU32();
    String reason = msg.ReadString();

    URHO3D_LOGWARNINGF("Edit %u rejected: %s", editID, reason.CString());

    // Rollback terrain edit
    auto terrainIt = terrainEditSnapshots_.Find(editID);
    if (terrainIt != terrainEditSnapshots_.End())
    {
        const TerrainEditSnapshot& snap = terrainIt->second_;
        if (editableHeightMap_ && terrain_)
        {
            unsigned char* dst = editableHeightMap_->GetData();
            int hmW = editableHeightMap_->GetWidth();
            int comps = editableHeightMap_->GetComponents();
            int rw = snap.regionSize.x_;
            int rh = snap.regionSize.y_;
            for (int row = 0; row < rh; ++row)
            {
                int dstIdx = ((snap.regionMin.y_ + row) * hmW + snap.regionMin.x_) * comps;
                int srcIdx = row * rw * comps;
                memcpy(&dst[dstIdx], &snap.heightData[srcIdx], rw * comps);
            }
            terrain_->ApplyHeightMap();
            // Rollback water map if snapshot includes water data (mode 6)
            if (!snap.waterData.Empty() && waterMap_)
            {
                int wComps = waterMap_->GetComponents();
                unsigned char* wDst = waterMap_->GetData();
                for (int row = 0; row < rh; ++row)
                {
                    int dstIdx = ((snap.regionMin.y_ + row) * hmW + snap.regionMin.x_) * wComps;
                    int srcIdx = row * rw * wComps;
                    memcpy(&wDst[dstIdx], &snap.waterData[srcIdx], rw * wComps);
                }
                if (waterMapTex_)
                    waterMapTex_->SetData(waterMap_);
            }
        }
        terrainEditSnapshots_.Erase(terrainIt);
        return;
    }

    // Rollback object edit
    auto objIt = objectEditSnapshots_.Find(editID);
    if (objIt != objectEditSnapshots_.End())
    {
        const ObjectEditSnapshot& snap = objIt->second_;
        switch (snap.subtype)
        {
        case 0:  // create was rejected — remove the node
        {
            Node* node = scene_->GetNode(snap.nodeID);
            if (node)
                node->Remove();
            break;
        }
        case 1:  // delete was rejected — recreate the node
        {
            if (!snap.xmlData.Empty())
            {
                XMLFile xmlFile(context_);
                xmlFile.FromString(snap.xmlData);
                Node* restored = scene_->InstantiateXML(xmlFile.GetRoot(), snap.position, snap.rotation);
                if (restored)
                    URHO3D_LOGINFOF("Rollback: restored deleted node at (%.1f, %.1f, %.1f)",
                        snap.position.x_, snap.position.y_, snap.position.z_);
            }
            break;
        }
        case 2:  // transform was rejected — restore original transform
        {
            Node* node = scene_->GetNode(snap.nodeID);
            if (node)
            {
                node->SetPosition(snap.position);
                node->SetRotation(snap.rotation);
                node->SetScale(snap.scale);
            }
            break;
        }
        }
        objectEditSnapshots_.Erase(objIt);
        return;
    }
}

void TerrainNode::HandleEditBroadcast(MemoryBuffer& msg)
{
    String username = msg.ReadString();
    int editType = msg.ReadI32();  // MSG_EDIT_TERRAIN or MSG_EDIT_OBJECT

    if (editType == MSG_EDIT_TERRAIN)
    {
        unsigned editID = msg.ReadU32();
        Vector3 worldPos = msg.ReadVector3();
        int mode = msg.ReadI32();
        int shape = msg.ReadI32();
        float radius = msg.ReadFloat();
        float strength = msg.ReadFloat();
        float smooth = msg.ReadFloat();
        float rotation = msg.ReadFloat();
        float ts = msg.ReadFloat();
        float flattenH = msg.ReadFloat();

        // Temporarily swap brush settings, apply, restore
        int savedMode = brushMode_;
        int savedShape = brushShape_;
        float savedRadius = brushRadius_;
        float savedStrength = brushStrength_;
        float savedSmooth = smoothStrength_;
        float savedRotation = brushRotation_;
        float savedFlatten = lockedFlattenHeight_;

        brushMode_ = mode;
        brushShape_ = shape;
        brushRadius_ = radius;
        brushStrength_ = strength;
        smoothStrength_ = smooth;
        brushRotation_ = rotation;
        lockedFlattenHeight_ = flattenH;

        // Apply without re-sending to server
        replayingBroadcast_ = true;
        ApplyBrush(worldPos, ts);
        replayingBroadcast_ = false;

        brushMode_ = savedMode;
        brushShape_ = savedShape;
        brushRadius_ = savedRadius;
        brushStrength_ = savedStrength;
        smoothStrength_ = savedSmooth;
        brushRotation_ = savedRotation;
        lockedFlattenHeight_ = savedFlatten;

        (void)editID;
    }
    else if (editType == MSG_EDIT_OBJECT)
    {
        unsigned editID = msg.ReadU32();
        unsigned char subtype = msg.ReadU8();

        switch (subtype)
        {
        case 0:  // create
        {
            String xmlData = msg.ReadString();
            Vector3 position = msg.ReadVector3();
            Vector3 surfaceNormal = msg.ReadVector3();

            XMLFile xmlFile(context_);
            if (xmlFile.FromString(xmlData))
            {
                Quaternion surfaceRot;
                surfaceRot.FromRotationTo(Vector3::UP, surfaceNormal);
                Node* instance = scene_->InstantiateXML(xmlFile.GetRoot(), position, surfaceRot);
                if (instance)
                    URHO3D_LOGINFOF("Broadcast: %s created object at (%.1f, %.1f, %.1f)",
                        username.CString(), position.x_, position.y_, position.z_);
            }
            break;
        }
        case 1:  // delete
        {
            unsigned nodeID = msg.ReadU32();
            Node* node = scene_->GetNode(nodeID);
            if (node)
            {
                node->Remove();
                URHO3D_LOGINFOF("Broadcast: %s deleted node %u", username.CString(), nodeID);
            }
            break;
        }
        case 2:  // transform
        {
            unsigned nodeID = msg.ReadU32();
            Vector3 pos = msg.ReadVector3();
            Quaternion rot = msg.ReadQuaternion();
            Vector3 scale = msg.ReadVector3();
            Node* node = scene_->GetNode(nodeID);
            if (node)
            {
                node->SetPosition(pos);
                node->SetRotation(rot);
                node->SetScale(scale);
            }
            break;
        }
        }
        (void)editID;
    }
}

void TerrainNode::HandleResourcePatch(MemoryBuffer& msg)
{
    String resourceID = msg.ReadString();
    int patchX = msg.ReadI32();
    int patchZ = msg.ReadI32();
    int pixelX = msg.ReadI32();
    int pixelZ = msg.ReadI32();
    int pixelW = msg.ReadI32();
    int pixelH = msg.ReadI32();
    int components = msg.ReadI32();
    unsigned dataSize = msg.ReadU32();

    if (resourceID == "water_heightmap" && waterMap_)
    {
        // Blit raw pixels into waterMap_ at (pixelX, pixelZ)
        int mapComponents = waterMap_->GetComponents();
        unsigned char* dest = waterMap_->GetData();
        int destStride = waterMap_->GetWidth() * mapComponents;
        int srcStride = pixelW * components;

        for (int row = 0; row < pixelH; ++row)
        {
            int destRow = pixelZ + row;
            if (destRow < 0 || destRow >= waterMap_->GetHeight())
            {
                msg.Seek(msg.GetPosition() + srcStride);  // skip row
                continue;
            }
            int destCol = pixelX;
            if (destCol < 0 || destCol + pixelW > waterMap_->GetWidth())
            {
                msg.Seek(msg.GetPosition() + srcStride);  // skip row
                continue;
            }
            const unsigned char* srcRow = reinterpret_cast<const unsigned char*>(msg.GetData()) + msg.GetPosition();
            memcpy(dest + destRow * destStride + destCol * mapComponents, srcRow, pixelW * Min(components, mapComponents));
            msg.Seek(msg.GetPosition() + srcStride);
        }

        // Re-upload water map to GPU
        if (waterMapTex_)
            waterMapTex_->SetData(waterMap_);

        URHO3D_LOGINFOF("Resource patch '%s' (%d,%d) → pixels (%d,%d) %dx%d",
            resourceID.CString(), patchX, patchZ, pixelX, pixelZ, pixelW, pixelH);
    }
    else if (resourceID == "resource_map" && resourceMap_ && resourceMap_->GetImage())
    {
        Image* localImg = resourceMap_->GetImage();
        bool ok = false;

        if (components == 0)
        {
            // PNG-compressed data — decode into image
            unsigned startPos = msg.GetPosition();
            SharedPtr<Image> decoded(new Image(context_));
            MemoryBuffer pngStream(reinterpret_cast<const unsigned char*>(msg.GetData()) + startPos, dataSize);
            if (decoded->Load(pngStream) &&
                decoded->GetWidth() == ResourceMap::MAP_SIZE &&
                decoded->GetHeight() == ResourceMap::MAP_SIZE)
            {
                // Clear existing pickup nodes — they'll be respawned by streaming
                for (auto it = activePickupNodes_.Begin(); it != activePickupNodes_.End(); ++it)
                {
                    if (it->second_)
                        it->second_->Remove();
                }
                activePickupNodes_.Clear();

                memcpy(localImg->GetData(), decoded->GetData(),
                       (size_t)decoded->GetWidth() * decoded->GetHeight() * decoded->GetComponents());
                ok = true;
                URHO3D_LOGINFOF("Received server resource map (PNG %u bytes → %dx%d)",
                    dataSize, decoded->GetWidth(), decoded->GetHeight());
            }
            msg.Seek(startPos + dataSize);
        }
        else if (components == 4 && pixelW == ResourceMap::MAP_SIZE && pixelH == ResourceMap::MAP_SIZE &&
                 dataSize == (unsigned)(pixelW * pixelH * components))
        {
            // Raw RGBA fallback
            for (auto it = activePickupNodes_.Begin(); it != activePickupNodes_.End(); ++it)
            {
                if (it->second_)
                    it->second_->Remove();
            }
            activePickupNodes_.Clear();

            const unsigned char* srcData = reinterpret_cast<const unsigned char*>(msg.GetData()) + msg.GetPosition();
            memcpy(localImg->GetData(), srcData, dataSize);
            msg.Seek(msg.GetPosition() + dataSize);
            ok = true;
            URHO3D_LOGINFOF("Received server resource map (raw %u bytes)", dataSize);
        }
        else
        {
            msg.Seek(msg.GetPosition() + dataSize);
            URHO3D_LOGWARNING("Resource map size/format mismatch from server");
        }
    }
    else
    {
        // Skip unknown resource data
        msg.Seek(msg.GetPosition() + dataSize);
        URHO3D_LOGWARNINGF("Unknown resource patch: %s", resourceID.CString());
    }
}

void TerrainNode::HandleNewTerrain(MemoryBuffer& msg)
{
    int gridX = msg.ReadI32();
    int gridZ = msg.ReadI32();
    Vector<byte> pngData = msg.ReadBuffer();

    // Load heightmap from PNG data
    auto* cache = GetSubsystem<ResourceCache>();
    MemoryBuffer pngStream(pngData.Buffer(), pngData.Size());
    SharedPtr<Image> heightmap(new Image(context_));
    if (!heightmap->Load(pngStream))
    {
        URHO3D_LOGERRORF("MSG_NEW_TERRAIN: failed to load heightmap for grid (%d, %d)", gridX, gridZ);
        return;
    }

    // Create terrain node at correct world position
    const float terrainWorldSize = 2048.0f;  // 1025 verts * 2.0 spacing
    float posX = gridX * terrainWorldSize - (terrainWorldSize * 0.5f);
    float posZ = gridZ * terrainWorldSize - (terrainWorldSize * 0.5f);

    String nodeName = "Terrain_" + String(gridX) + "_" + String(gridZ);

    // Don't duplicate if we already have this terrain
    if (scene_->GetChild(nodeName))
    {
        URHO3D_LOGWARNINGF("MSG_NEW_TERRAIN: terrain node '%s' already exists, skipping", nodeName.CString());
        return;
    }

    Node* newTerrainNode = scene_->CreateChild(nodeName);
    newTerrainNode->SetPosition(Vector3(posX, 0.0f, posZ));

    auto* terrain = newTerrainNode->CreateComponent<Terrain>();
    terrain->SetPatchSize(64);
    terrain->SetSpacing(Vector3(2.0f, 0.5f, 2.0f));
    terrain->SetSmoothing(false);
    terrain->SetHeightMap(heightmap);

    // Use same material as the primary terrain
    if (terrain_ && terrain_->GetMaterial())
        terrain->SetMaterial(terrain_->GetMaterial());

    terrain->SetOccluder(true);

    auto* body = newTerrainNode->CreateComponent<RigidBody>();
    body->SetCollisionLayer(2);
    body->SetFriction(0.75f);
    auto* shape = newTerrainNode->CreateComponent<CollisionShape>();
    shape->SetTerrain();

    // Register in ResourceCache for serialization
    String resName = "Terrains/terrain_" + String(gridX) + "_" + String(gridZ) + ".png";
    heightmap->SetName(resName);
    cache->AddManualResource(heightmap);

    URHO3D_LOGINFOF("MSG_NEW_TERRAIN: created terrain at grid (%d, %d), world (%.0f, 0, %.0f)",
        gridX, gridZ, posX, posZ);
}

void TerrainNode::SendPatchPosition(int patchX, int patchZ)
{
    auto* network = GetSubsystem<Network>();
    Connection* conn = network->GetServerConnection();
    if (!conn)
        return;

    VectorBuffer msg;
    msg.WriteI32(patchX);
    msg.WriteI32(patchZ);
    conn->SendMessage(MSG_PATCH_POSITION, true, true, msg);
    lastReportedPatchX_ = patchX;
    lastReportedPatchZ_ = patchZ;
}

// ============================================================================
// Player Avatar & Camera Modes
// ============================================================================

// CreatePlayerAvatar removed — Phase 5a: possession replaces the capsule avatar

void TerrainNode::UpdateCharacterCamera()
{
    // Follow possessed NPC if available, fall back to legacy characterNode_
    Node* followNode = possessedNPC_ ? possessedNPC_.Get() : characterNode_.Get();
    if (!followNode || !cameraNode_)
        return;

    // Hide PlayerModel child in first person (only relevant for legacy capsule)
    Node* modelNode = followNode->GetChild("PlayerModel");
    if (modelNode)
        modelNode->SetEnabled(cameraMode_ != CAM_FIRSTPERSON);

    // Camera shake offset — small random jitter during fumble
    Vector3 shakeOffset = Vector3::ZERO;
    if (cameraShakeTimer_ > 0.0f)
    {
        float intensity = cameraShakeTimer_ / 0.3f;  // decays from 1→0
        shakeOffset = Vector3(
            (Random(2.0f) - 1.0f) * 0.08f * intensity,
            (Random(2.0f) - 1.0f) * 0.06f * intensity,
            0.0f);
    }

    if (cameraMode_ == CAM_FIRSTPERSON)
    {
        // Eye-level inside the character
        Vector3 eyePos = followNode->GetWorldPosition() + Vector3(0.0f, 1.6f, 0.0f) + shakeOffset;
        cameraNode_->SetPosition(eyePos);
        cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));
    }
    else if (cameraMode_ == CAM_CHASE)
    {
        const float CAMERA_DISTANCE = 5.0f;
        const float CAMERA_HEIGHT = 2.0f;

        // Camera position: behind and above
        Quaternion camRot(pitch_, yaw_, 0.0f);
        Vector3 targetPos = followNode->GetWorldPosition() + Vector3(0.0f, CAMERA_HEIGHT, 0.0f);
        Vector3 desiredCamPos = targetPos + camRot * Vector3::BACK * CAMERA_DISTANCE + shakeOffset;

        // Raycast to avoid clipping through walls/terrain
        auto* physicsWorld = scene_ ? scene_->GetComponent<PhysicsWorld>() : nullptr;
        if (physicsWorld)
        {
            Vector3 dir = desiredCamPos - targetPos;
            float dist = dir.Length();
            if (dist > 0.001f)
            {
                PhysicsRaycastResult result;
                physicsWorld->RaycastSingle(result, Ray(targetPos, dir / dist), dist);
                if (result.body_)
                    desiredCamPos = targetPos + (dir / dist) * Max(result.distance_ - 0.1f, 0.5f);
            }
        }

        cameraNode_->SetPosition(desiredCamPos);
        cameraNode_->SetRotation(camRot);
    }
}

void TerrainNode::PossessNPC(Node* npcNode)
{
    if (!npcNode || !cameraNode_)
        return;

    auto* npc = npcNode->GetDerivedComponent<HumanNPC>(false);
    if (!npc)
        return;

    // Unpossess previous NPC if any
    if (possessedNPC_ && possessedNPC_ != npcNode)
    {
        auto* prevNPC = possessedNPC_->GetComponent<HumanNPC>();
        if (prevNPC)
            prevNPC->SetPossessed(false);
    }

    possessedNPC_ = npcNode;
    possessing_ = true;
    npc->SetPossessed(true);

    // Add SmoothedTransform for interpolated movement on the focused character
    if (!npcNode->GetComponent<SmoothedTransform>())
        npcNode->CreateComponent<SmoothedTransform>();

    // Phase 5a: alias characterNode_ to the possessed NPC so all existing
    // code that reads characterNode_ (campfire distance, building placement,
    // underwater checks, etc.) works without modification.
    characterNode_ = npcNode;

    // Set up camera lerp to chase position
    possessionLerping_ = true;
    possessionLerpTime_ = 0.0f;
    possessionLerpStartPos_ = cameraNode_->GetWorldPosition();
    possessionLerpStartRot_ = cameraNode_->GetWorldRotation();

    cameraMode_ = CAM_CHASE;

    const float CAMERA_DISTANCE = 5.0f;
    const float CAMERA_HEIGHT = 2.0f;
    Quaternion camRot(pitch_, yaw_, 0.0f);
    Vector3 targetPos = npcNode->GetWorldPosition() + Vector3(0.0f, CAMERA_HEIGHT, 0.0f);
    possessionLerpEndPos_ = targetPos + camRot * Vector3::BACK * CAMERA_DISTANCE;
    possessionLerpEndRot_ = camRot;

    // Enable mouse look
    auto* input = GetSubsystem<Input>();
    input->SetMouseMode(MM_RELATIVE);
    input->SetMouseVisible(false);
    useMouseMode_ = MM_RELATIVE;
    menuOpen_ = false;

    // Notify server (server validates and sends inventory)
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteU32(npc->GetSpawnId());  // server-assigned spawnId (0 if client-spawned)
        buf.WriteI32(npc->GetCreatureId()); // species ID for lazy-register
        Vector3 npcPos = npcNode->GetWorldPosition();
        buf.WriteFloat(npcPos.x_);
        buf.WriteFloat(npcPos.y_);
        buf.WriteFloat(npcPos.z_);
        serverConn->SendMessage(MSG_POSSESS, true, true, buf);
    }

    URHO3D_LOGINFOF("Possessed NPC: %s", npcNode->GetName().CString());
}

void TerrainNode::UnpossessNPC()
{
    if (!possessedNPC_ || !cameraNode_)
        return;

    auto* npc = possessedNPC_->GetDerivedComponent<HumanNPC>(false);
    unsigned unpossessSpawnId = npc ? npc->GetSpawnId() : 0;
    if (npc)
        npc->SetPossessed(false);

    // Remove SmoothedTransform — no longer the focused character
    auto* smoothed = possessedNPC_->GetComponent<SmoothedTransform>();
    if (smoothed)
        possessedNPC_->RemoveComponent(smoothed);

    Vector3 npcPos = possessedNPC_->GetWorldPosition();

    possessedNPC_ = nullptr;
    possessing_ = false;
    characterNode_ = nullptr;  // Phase 5a: god has no body

    // Pull camera back 2 units along current look direction, keep rotation
    possessionLerping_ = true;
    possessionLerpTime_ = 0.0f;
    possessionLerpStartPos_ = cameraNode_->GetWorldPosition();
    possessionLerpStartRot_ = cameraNode_->GetWorldRotation();

    cameraMode_ = CAM_GOD;
    Vector3 lookDir = cameraNode_->GetWorldDirection();
    possessionLerpEndPos_ = possessionLerpStartPos_ - lookDir * 2.0f;
    possessionLerpEndRot_ = possessionLerpStartRot_;

    // Free cursor for god mode
    auto* input = GetSubsystem<Input>();
    input->SetMouseMode(MM_FREE);
    input->SetMouseVisible(true);
    useMouseMode_ = MM_FREE;
    menuOpen_ = true;

    // Notify server
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteU32(unpossessSpawnId);
        serverConn->SendMessage(MSG_UNPOSSESS, true, true, buf);
    }
    possessedNPCPlayerId_ = -1;

    URHO3D_LOGINFO("Unpossessed NPC — god mode");
}

void TerrainNode::TogglePossession()
{
    // Legacy toggle — if possessing, unpossess; if not, no-op (use click to possess)
    if (possessing_)
        UnpossessNPC();
}

void TerrainNode::UpdatePossessionLerp(float timeStep)
{
    if (!possessionLerping_)
        return;

    possessionLerpTime_ += timeStep;
    float t = possessionLerpTime_ / POSSESSION_LERP_DURATION;

    bool finished = false;
    if (t >= 1.0f)
    {
        t = 1.0f;
        finished = true;
        possessionLerping_ = false;
    }

    // Smooth step for pleasant ease-in/out
    t = t * t * (3.0f - 2.0f * t);

    Vector3 pos = possessionLerpStartPos_.Lerp(possessionLerpEndPos_, t);
    Quaternion rot = possessionLerpStartRot_.Slerp(possessionLerpEndRot_, t);
    cameraNode_->SetWorldPosition(pos);
    cameraNode_->SetWorldRotation(rot);

    // Lerp listener between camera and NPC during transition
    if (listenerNode_)
    {
        if (possessing_ && possessedNPC_)
        {
            // Transitioning to possessed — lerp from camera toward NPC
            Vector3 npcPos = possessedNPC_->GetWorldPosition();
            listenerNode_->SetWorldPosition(possessionLerpStartPos_.Lerp(npcPos, t));
        }
        else
        {
            // Transitioning to god mode — listener follows camera
            listenerNode_->SetWorldPosition(pos);
        }
    }

    if (finished)
        UpdateListenerPosition();
}

void TerrainNode::UpdateListenerPosition()
{
    if (!listenerNode_)
        return;

    if (possessing_ && possessedNPC_)
    {
        // Reparent listener to the possessed NPC
        listenerNode_->SetParent(possessedNPC_);
        listenerNode_->SetPosition(Vector3::ZERO);
    }
    else
    {
        // Reparent listener back to camera
        listenerNode_->SetParent(cameraNode_);
        listenerNode_->SetPosition(Vector3::ZERO);
    }
}

void TerrainNode::HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData)
{
    auto* network = GetSubsystem<Network>();
    Connection* serverConnection = network->GetServerConnection();

    // Client: collect controls and send to server
    if (serverConnection)
    {
        auto* ui = GetSubsystem<UI>();
        auto* input = GetSubsystem<Input>();
        Controls controls;

        controls.yaw_ = yaw_;
        controls.pitch_ = pitch_;

        if (!ui->GetFocusElement() && cameraMode_ != CAM_GOD)
        {
            controls.Set(CTRL_FORWARD, input->GetKeyDown(KEY_W));
            controls.Set(CTRL_BACK, input->GetKeyDown(KEY_S));
            controls.Set(CTRL_LEFT, input->GetKeyDown(KEY_A));
            controls.Set(CTRL_RIGHT, input->GetKeyDown(KEY_D));
            controls.Set(CTRL_JUMP, input->GetKeyDown(KEY_SPACE));
            controls.Set(CTRL_SPRINT, input->GetKeyDown(KEY_SHIFT));
        }

        serverConnection->SetControls(controls);
        serverConnection->SetPosition(cameraNode_ ? cameraNode_->GetPosition() : Vector3::ZERO);
    }
    // Server/subserver: apply controls to each client's avatar
    else if (network->IsServerRunning())
    {
        const Vector<SharedPtr<Connection>>& connections = network->GetClientConnections();

        // Phase 5a: server-side control routing is handled via MSG_POSSESS —
        // the possessed NPC receives controls directly through HumanNPC.
        (void)connections;
    }

    // Controls always route through the server connection (even offline mode
    // connects to a local AuthServer). Server's HandlePhysicsPreStep applies
    // them to the possessed NPC's ServerCreatureAI.
}

void TerrainNode::HandleClientConnected(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientConnected;

    auto* newConnection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    newConnection->SetScene(scene_);

    // Phase 5a: no capsule avatar. Create a lightweight node for server-side
    // position tracking (spectator broadcasts, combat range checks).
    // Client starts in god cam and possesses NPCs to interact.
    Node* tracker = scene_->CreateChild("ClientTracker");
    serverObjects_[newConnection] = tracker;

    // Tell the client which node ID is theirs (for position sync)
    VariantMap remoteEventData;
    remoteEventData[P_ID] = tracker->GetID();
    newConnection->SendRemoteEvent(E_CLIENTOBJECTID, true, remoteEventData);

    URHO3D_LOGINFOF("Client connected — created tracker node %u (god cam, no avatar)", tracker->GetID());
}

void TerrainNode::HandleClientDisconnected(StringHash eventType, VariantMap& eventData)
{
    using namespace ClientDisconnected;

    auto* connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
    Node* avatar = serverObjects_[connection];
    if (avatar)
    {
        URHO3D_LOGINFOF("Client disconnected — removing avatar node %u", avatar->GetID());
        avatar->Remove();
    }
    serverObjects_.Erase(connection);
}

void TerrainNode::HandleClientObjectID(StringHash eventType, VariantMap& eventData)
{
    clientObjectID_ = eventData[P_ID].GetU32();
    URHO3D_LOGINFOF("Received client object ID: %u", clientObjectID_);

    // Phase 5a: player starts in god cam — no avatar, no chase cam.
    // characterNode_ stays null until the player possesses an NPC.
}

void TerrainNode::HandleNodeAdded(StringHash, VariantMap& eventData)
{
    using namespace NodeAdded;
    auto* node = static_cast<Node*>(eventData[P_NODE].GetPtr());
    if (!node)
        return;

    // Replicated creature nodes arrive as parent+child: the parent (e.g. "Rabbit")
    // may arrive nameless initially, but the child model node (e.g. "RabbitModel")
    // arrives after the parent's name is set via delta update. We detect EITHER:
    // - A node whose name matches a creature species
    // - A node whose PARENT name matches a creature species
    // In both cases we attach the component to the parent (creature root) node.

    // Determine which node is the creature root and what species it is
    Node* creatureNode = node;
    String name = node->GetName();

    // If this node's name doesn't match, check if the parent is a creature
    // (this handles the child model node arriving, e.g. "RabbitModel" under "Rabbit")
    int creatureId = 0;
    auto lookupId = [](const String& n) -> int {
        if (n == "Rabbit")    return 1;
        if (n == "Deer")      return 2;
        if (n == "Fox")       return 3;
        if (n == "Stag")      return 4;
        if (n == "Wolf")      return 5;
        if (n == "Bull")      return 6;
        if (n == "Cow")       return 7;
        if (n == "Donkey")    return 9;
        if (n == "Horse")     return 10;
        if (n == "Alpaca")    return 11;
        if (n == "Husky")     return 12;
        if (n == "ShibaInu")  return 13;
        if (n == "CaveMan")   return 20;
        if (n == "CaveWoman") return 21;
        return 0;
    };

    creatureId = lookupId(name);
    if (creatureId == 0 && node->GetParent())
    {
        // Child node — check parent
        creatureId = lookupId(node->GetParent()->GetName());
        creatureNode = node->GetParent();
    }
    if (creatureId == 0)
        return; // Not a creature node

    // Already has a creature component? Skip (avoid double-attach)
    if (creatureNode->GetDerivedComponent<LandAnimal>(false))
        return;

    URHO3D_LOGINFOF("[Replication] Creature node arrived: %s (id=%d)",
        creatureNode->GetName().CString(), creatureId);

    // Attach species-specific LogicComponent to the creature root node
    LandAnimal* animal = nullptr;
    switch (creatureId)
    {
    case  1: animal = creatureNode->CreateComponent<Rabbit>();    break;
    case  2: animal = creatureNode->CreateComponent<Deer>();      break;
    case  3: animal = creatureNode->CreateComponent<Fox>();       break;
    case  4: animal = creatureNode->CreateComponent<Stag>();      break;
    case  5: animal = creatureNode->CreateComponent<Wolf>();      break;
    case  6: animal = creatureNode->CreateComponent<Bull>();      break;
    case  7: animal = creatureNode->CreateComponent<Cow>();       break;
    case  9: animal = creatureNode->CreateComponent<Donkey>();    break;
    case 10: animal = creatureNode->CreateComponent<Horse>();     break;
    case 11: animal = creatureNode->CreateComponent<Alpaca>();    break;
    case 12: animal = creatureNode->CreateComponent<Husky>();     break;
    case 13: animal = creatureNode->CreateComponent<ShibaInu>(); break;
    case 20: animal = creatureNode->CreateComponent<CaveMan>();   break;
    case 21: animal = creatureNode->CreateComponent<CaveWoman>(); break;
    default:
        URHO3D_LOGWARNINGF("HandleNodeAdded: unknown creatureId %d on node %s",
            creatureId, node->GetName().CString());
        return;
    }

    if (animal)
    {
        // Read server-assigned spawnId from replicated node Var.
        const Variant& spawnVar = creatureNode->GetVar("SpawnId");
        unsigned spawnId = spawnVar.IsEmpty() ? 0 : spawnVar.GetU32();
        if (spawnId > 0)
        {
            animal->SetSpawnId(spawnId);
            spawnIdToNode_[spawnId] = creatureNode;
        }

        // Track creature for per-frame updates — LogicComponent event
        // subscriptions don't work for components on replicated nodes.
        replicatedCreatures_.Push(WeakPtr<Creature>(animal));
        URHO3D_LOGINFOF("[Tracked] %s — replicatedCreatures_ now %u",
            creatureNode->GetName().CString(), replicatedCreatures_.Size());

        // Set up NPC references if human
        if (creatureId == 20 || creatureId == 21)
        {
            auto* npc = dynamic_cast<HumanNPC*>(animal);
            if (npc)
            {
                npc->SetCameraNode(cameraNode_);
                Node* campfire = scene_ ? scene_->GetChild("Campfire", true) : nullptr;
                if (campfire)
                    npc->SetCampfireNode(campfire);
            }
        }

        // Roll ability scores — CON modifier adjusts maxHp inside RollStats
        animal->RollStats(1);
        {
            const auto& s = animal->GetStats();
            URHO3D_LOGINFOF("[Stats] %s (spawnId %u): STR %d DEX %d CON %d INT %d WIS %d CHA %d",
                creatureNode->GetName().CString(), spawnId,
                s.strength, s.dexterity, s.constitution,
                s.intelligence, s.wisdom, s.charisma);
        }

        animalNodes_.Push(WeakPtr<Node>(creatureNode));
        URHO3D_LOGINFOF("HandleNodeAdded: attached %s (creatureId=%d spawnId=%u)",
            creatureNode->GetName().CString(), creatureId, spawnId);
    }
}

void TerrainNode::AttachCreatureComponent(Node* creatureNode, int creatureId)
{
    if (!creatureNode || creatureNode->GetDerivedComponent<LandAnimal>(false))
        return;

    LandAnimal* animal = nullptr;
    switch (creatureId)
    {
    case  1: animal = creatureNode->CreateComponent<Rabbit>();    break;
    case  2: animal = creatureNode->CreateComponent<Deer>();      break;
    case  3: animal = creatureNode->CreateComponent<Fox>();       break;
    case  4: animal = creatureNode->CreateComponent<Stag>();      break;
    case  5: animal = creatureNode->CreateComponent<Wolf>();      break;
    case  6: animal = creatureNode->CreateComponent<Bull>();      break;
    case  7: animal = creatureNode->CreateComponent<Cow>();       break;
    case  9: animal = creatureNode->CreateComponent<Donkey>();    break;
    case 10: animal = creatureNode->CreateComponent<Horse>();     break;
    case 11: animal = creatureNode->CreateComponent<Alpaca>();    break;
    case 12: animal = creatureNode->CreateComponent<Husky>();     break;
    case 13: animal = creatureNode->CreateComponent<ShibaInu>(); break;
    case 20: animal = creatureNode->CreateComponent<CaveMan>();   break;
    case 21: animal = creatureNode->CreateComponent<CaveWoman>(); break;
    default: return;
    }

    if (animal)
    {
        const Variant& spawnVar = creatureNode->GetVar("SpawnId");
        unsigned spawnId = spawnVar.IsEmpty() ? 0 : spawnVar.GetU32();
        if (spawnId > 0)
        {
            animal->SetSpawnId(spawnId);
            spawnIdToNode_[spawnId] = creatureNode;
        }
        replicatedCreatures_.Push(WeakPtr<Creature>(animal));
        animalNodes_.Push(WeakPtr<Node>(creatureNode));

        if (creatureId == 20 || creatureId == 21)
        {
            auto* npc = dynamic_cast<HumanNPC*>(animal);
            if (npc)
            {
                npc->SetCameraNode(cameraNode_);
                Node* campfire = scene_ ? scene_->GetChild("Campfire", true) : nullptr;
                if (campfire)
                    npc->SetCampfireNode(campfire);
            }
        }

        // Roll ability scores — CON modifier adjusts maxHp inside RollStats
        animal->RollStats(1);
        {
            const auto& s = animal->GetStats();
            URHO3D_LOGINFOF("[Stats] %s (spawnId %u): STR %d DEX %d CON %d INT %d WIS %d CHA %d",
                creatureNode->GetName().CString(), spawnId,
                s.strength, s.dexterity, s.constitution,
                s.intelligence, s.wisdom, s.charisma);
        }

        URHO3D_LOGINFOF("[AttachCreature] %s creatureId=%d spawnId=%u",
            creatureNode->GetName().CString(), creatureId, spawnId);
    }
}

// ============================================================================
// Ecosystem — texture-based vegetation map
// ============================================================================

void TerrainNode::CreateEcosystem()
{
    if (!terrain_)
        return;

    ecosystem_ = new EcosystemManager(context_);
    ecosystem_->Initialize(terrain_, 5.0f);  // waterLevel = 5.0

    // Phase 2: connect rainfall map as moisture input for vegetation growth
    if (rainfallMap_)
        ecosystem_->SetRainfallMap(rainfallMap_, RAINFALL_MAP_SIZE);

    // Phase 19: bind soil trampling texture to terrain material for worn path visuals
    auto* soilTex = ecosystem_->GetOrCreateSoilTexture();
    if (soilTex && terrain_)
    {
        auto* terrainMat = terrain_->GetMaterial();
        if (terrainMat)
        {
            terrainMat->SetTexture(static_cast<TextureUnit>(5), soilTex);
            // Enable TRAMPLING define so the shader samples the trampling map
            auto* tech = terrainMat->GetTechnique(0);
            if (tech)
            {
                auto* pass = tech->GetPass("base");
                if (pass)
                    pass->SetPixelShaderDefines(pass->GetPixelShaderDefines() + " TRAMPLING");
                pass = tech->GetPass("litbase");
                if (pass)
                    pass->SetPixelShaderDefines(pass->GetPixelShaderDefines() + " TRAMPLING");
                pass = tech->GetPass("light");
                if (pass)
                    pass->SetPixelShaderDefines(pass->GetPixelShaderDefines() + " TRAMPLING");
                pass = tech->GetPass("deferred");
                if (pass)
                    pass->SetPixelShaderDefines(pass->GetPixelShaderDefines() + " TRAMPLING");
            }
        }
    }
}

// ============================================================================
// Trees — server-authoritative placement, client caches L-system models
// ============================================================================

void TerrainNode::InitTreeModels()
{
    if (treeModelsReady_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* diffTech = cache->GetResource<Technique>("Techniques/Diff.xml");

    // Bark textures per species — all use cylindrical UVs from LTreeGenerator
    auto* barkNormal = cache->GetResource<Texture2D>("Textures/Bark_NormalTree.png");
    auto* barkTwisted = cache->GetResource<Texture2D>("Textures/Bark_TwistedTree.png");
    auto* barkDead = cache->GetResource<Texture2D>("Textures/Bark_DeadTree.png");
    Texture2D* barkTextures[NUM_TREE_SPECIES] = {
        barkNormal,   // 0 = Oak
        barkNormal,   // 1 = Pine
        barkTwisted,  // 2 = Eucalyptus (twisted bark)
        barkDead,     // 3 = Acacia (dry bark)
        barkNormal,   // 4 = Willow
        barkTwisted,  // 5 = SheOak (papery bark)
    };

    // Leaf textures — broadleaf vs pine needle cluster
    auto* leafBroad = cache->GetResource<Texture2D>("Textures/Leaves_NormalTree_C.png");
    auto* leafPine = cache->GetResource<Texture2D>("Textures/Leaf_Pine_C.png");
    Texture2D* leafTextures[NUM_TREE_SPECIES] = {
        leafBroad,    // 0 = Oak
        leafPine,     // 1 = Pine
        leafBroad,    // 2 = Eucalyptus
        leafBroad,    // 3 = Acacia
        leafBroad,    // 4 = Willow
        leafPine,     // 5 = SheOak (needle-like)
    };

    TreePreset presets[NUM_TREE_SPECIES] = { LTreeGenerator::Oak(), LTreeGenerator::Pine(), LTreeGenerator::Eucalyptus(), LTreeGenerator::Acacia(), LTreeGenerator::Willow(), LTreeGenerator::SheOak() };
    for (int i = 0; i < NUM_TREE_SPECIES; ++i)
    {
        treeModel_[i] = LTreeGenerator::Generate(context_, presets[i]);
        treeModelLOD1_[i] = LTreeGenerator::GenerateLOD(context_, presets[i]);

        auto* barkMat = new Material(context_);
        barkMat->SetTechnique(0, diffTech);
        if (barkTextures[i])
            barkMat->SetTexture(TU_DIFFUSE, barkTextures[i]);
        barkMat->SetShaderParameter("MatDiffColor", Color::WHITE);
        barkMat->SetShaderParameter("MatSpecColor", Vector4(0.15f, 0.15f, 0.15f, 8.0f));
        treeBarkMat_[i] = barkMat;

        auto* leafMat = new Material(context_);
        leafMat->SetTechnique(0, diffTech);
        if (leafTextures[i])
            leafMat->SetTexture(TU_DIFFUSE, leafTextures[i]);
        leafMat->SetShaderParameter("MatDiffColor", Color::WHITE);
        leafMat->SetShaderParameter("MatSpecColor", Vector4(0.1f, 0.1f, 0.1f, 4.0f));
        treeLeafMat_[i] = leafMat;

        treeBaseLeafColor_[i] = presets[i].leafColor;
        treeEvergreen_[i] = presets[i].evergreen;
    }

    // Imposter billboard materials — baked textures for LOD2 distance
    auto* diffAlphaTech = cache->GetResource<Technique>("Techniques/DiffUnlitAlpha.xml");
    const char* imposterNames[NUM_TREE_SPECIES] = { "Oak", "Pine", "Eucalyptus", "Acacia", "Willow", "SheOak" };
    for (int i = 0; i < NUM_TREE_SPECIES; ++i)
    {
        String texPath = String("Textures/Trees/") + imposterNames[i] + "_Imposter0.png";
        auto* tex = cache->GetResource<Texture2D>(texPath);
        if (!tex)
            continue;

        auto* mat = new Material(context_);
        mat->SetTechnique(0, diffAlphaTech);
        mat->SetTexture(TU_DIFFUSE, tex);
        mat->SetShaderParameter("MatDiffColor", Color::WHITE);
        treeImposterMat_[i] = mat;
    }

    treeModelsReady_ = true;
}

void TerrainNode::UpdateTreeSeason(float seasonAngle)
{
    for (int i = 0; i < NUM_TREE_SPECIES; ++i)
    {
        if (!treeLeafMat_[i])
            continue;

        Color base = treeBaseLeafColor_[i];
        Color leafColor;

        if (treeEvergreen_[i])
        {
            // Evergreen: subtle brightness pulse through the year
            float brightness = 0.85f + 0.15f * sinf(seasonAngle * 6.2831853f);
            leafColor = Color(brightness, brightness, brightness);
        }
        else
        {
            // Seasonal multipliers — texture provides base green, these shift it
            Color spring(0.9f, 1.1f, 0.8f);    // slightly more green
            Color summer(1.0f, 1.0f, 1.0f);    // texture as-is
            Color autumn(1.8f, 0.8f, 0.3f);    // shift toward orange/red
            Color winter(0.5f, 0.45f, 0.35f);  // desaturate and darken

            if (seasonAngle < 0.25f)
                leafColor = spring.Lerp(summer, seasonAngle / 0.25f);
            else if (seasonAngle < 0.5f)
                leafColor = summer.Lerp(autumn, (seasonAngle - 0.25f) / 0.25f);
            else if (seasonAngle < 0.75f)
                leafColor = autumn.Lerp(winter, (seasonAngle - 0.5f) / 0.25f);
            else
                leafColor = winter.Lerp(spring, (seasonAngle - 0.75f) / 0.25f);
        }

        treeLeafMat_[i]->SetShaderParameter("MatDiffColor", leafColor);
    }
}

void TerrainNode::UpdateDecorSeason(float seasonAngle)
{
    // Seasonal tint for vegetation decor categories (bush, flower, fern, grass, ground plant)
    // Rocks and stumps stay neutral
    Color spring(0.9f, 1.1f, 0.85f);
    Color summer(1.0f, 1.0f, 1.0f);
    Color autumn(1.3f, 0.85f, 0.5f);
    Color winter(0.6f, 0.55f, 0.5f);

    Color tint;
    if (seasonAngle < 0.25f)
        tint = spring.Lerp(summer, seasonAngle / 0.25f);
    else if (seasonAngle < 0.5f)
        tint = summer.Lerp(autumn, (seasonAngle - 0.25f) / 0.25f);
    else if (seasonAngle < 0.75f)
        tint = autumn.Lerp(winter, (seasonAngle - 0.5f) / 0.25f);
    else
        tint = winter.Lerp(spring, (seasonAngle - 0.75f) / 0.25f);

    // Apply tint to vegetation decor materials (shared — affects all instances)
    int vegCategories[] = { DECOR_BUSH, DECOR_FLOWER, DECOR_FERN, DECOR_GRASS, DECOR_GROUNDPLANT };
    for (int cat : vegCategories)
    {
        for (unsigned i = 0; i < decorModels_[cat].Size(); ++i)
        {
            if (decorModels_[cat][i].material)
                decorModels_[cat][i].material->SetShaderParameter("MatDiffColor", tint);
        }
    }

    // Stumps and logs — subtle seasonal tint (moss in spring/summer, frost in winter)
    Color stumpSpring(0.85f, 0.95f, 0.8f);   // slight green moss
    Color stumpSummer(0.9f, 0.95f, 0.85f);   // light moss
    Color stumpAutumn(0.95f, 0.9f, 0.8f);    // drying out
    Color stumpWinter(1.1f, 1.1f, 1.15f);    // frost/snow highlight

    Color stumpTint;
    if (seasonAngle < 0.25f)
        stumpTint = stumpSpring.Lerp(stumpSummer, seasonAngle / 0.25f);
    else if (seasonAngle < 0.5f)
        stumpTint = stumpSummer.Lerp(stumpAutumn, (seasonAngle - 0.25f) / 0.25f);
    else if (seasonAngle < 0.75f)
        stumpTint = stumpAutumn.Lerp(stumpWinter, (seasonAngle - 0.5f) / 0.25f);
    else
        stumpTint = stumpWinter.Lerp(stumpSpring, (seasonAngle - 0.75f) / 0.25f);

    for (unsigned i = 0; i < decorModels_[DECOR_STUMP].Size(); ++i)
    {
        if (decorModels_[DECOR_STUMP][i].material)
            decorModels_[DECOR_STUMP][i].material->SetShaderParameter("MatDiffColor", stumpTint);
    }
}

void TerrainNode::UpdateTreeLOD()
{
    if (!cameraNode_ || treeIdToNode_.Empty())
        return;

    Vector3 camPos = cameraNode_->GetWorldPosition();
    const float lod1DistSq = 80.0f * 80.0f;
    const float lod2DistSq = 160.0f * 160.0f;

    for (auto it = treeIdToNode_.Begin(); it != treeIdToNode_.End(); ++it)
    {
        Node* node = it->second_;
        if (!node)
            continue;

        int species = node->GetVar("TreeSpecies").GetI32();
        if (species < 0 || species >= NUM_TREE_SPECIES)
            continue;

        Vector3 pos = node->GetWorldPosition();
        float dx = pos.x_ - camPos.x_;
        float dz = pos.z_ - camPos.z_;
        float distSq = dx * dx + dz * dz;

        auto* sm = node->GetComponent<StaticModel>();
        auto* bb = node->GetComponent<BillboardSet>();

        if (distSq > lod2DistSq && treeImposterMat_[species])
        {
            // LOD2: billboard imposter
            if (sm)
                sm->SetEnabled(false);

            if (!bb)
            {
                // Lazy-create BillboardSet on first LOD2 transition
                bb = node->CreateComponent<BillboardSet>();
                bb->SetNumBillboards(1);
                bb->SetMaterial(treeImposterMat_[species]);
                bb->SetFaceCameraMode(FC_ROTATE_Y);  // Y-axis only — trees don't tilt
                bb->SetSorted(false);

                // Size from model bounding box
                BoundingBox bbox = treeModel_[species] ? treeModel_[species]->GetBoundingBox() : BoundingBox(0.0f, 10.0f);
                float width = bbox.Size().x_ * 1.1f;
                float height = bbox.Size().y_ * 1.1f;

                Billboard* quad = bb->GetBillboard(0);
                quad->position_ = Vector3(0.0f, height * 0.5f, 0.0f);  // center vertically
                quad->size_ = Vector2(width, height);
                quad->enabled_ = true;
                bb->Commit();
            }
            else
                bb->SetEnabled(true);
        }
        else
        {
            // LOD0 or LOD1: 3D geometry
            if (bb)
                bb->SetEnabled(false);

            if (!sm)
                continue;

            sm->SetEnabled(true);
            Model* want = (distSq > lod1DistSq) ? treeModelLOD1_[species] : treeModel_[species];
            if (sm->GetModel() != want && want)
            {
                sm->SetModel(want, true);
                if (treeBarkMat_[species]) sm->SetMaterial(0, treeBarkMat_[species]);
                if (treeLeafMat_[species]) sm->SetMaterial(1, treeLeafMat_[species]);
                sm->SetCastShadows(distSq <= lod1DistSq);
            }
        }
    }
}

// ============================================================================
// Death Log Query — F10 admin tool
// ============================================================================

void TerrainNode::RequestDeathLog()
{
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    buf.WriteU16(20);  // request last 20 entries
    serverConn->SendMessage(MSG_QUERY_DEATH_LOG, true, true, buf);
}

void TerrainNode::HandleDeathLogResult(MemoryBuffer& msg)
{
    static const char* causeNames[] = {
        "combat", "drown", "starve", "age", "scavenge", "fall", "fire", "dehydrate", "freeze"
    };

    unsigned short count = msg.ReadU16();
    URHO3D_LOGINFOF("[DeathLog] Received %u entries", count);

    // Build text for the console/log display
    String header = "\n=== DEATH LOG (last " + String(count) + ") ===\n";
    String log = header;

    for (unsigned i = 0; i < count; ++i)
    {
        String name = msg.ReadString();
        String species = msg.ReadString();
        float px = msg.ReadFloat();
        float pz = msg.ReadFloat();
        unsigned char cause = msg.ReadU8();
        String killer = msg.ReadString();
        int gameDay = msg.ReadI32();

        const char* causeName = (cause < 9) ? causeNames[cause] : "unknown";

        String entry = "  Day " + String(gameDay) + ": ";
        if (!name.Empty())
            entry += name + " (" + species + ")";
        else
            entry += species;
        entry += " — " + String(causeName);
        if (!killer.Empty())
            entry += " by " + killer;
        entry += " at (" + String((int)px) + ", " + String((int)pz) + ")\n";

        log += entry;
    }
    log += "=================================\n";

    URHO3D_LOGRAW(log);

    // Also display in the UI chat area if it exists
    auto* ui = GetSubsystem<UI>();
    if (ui)
    {
        auto* chatLog = ui->GetRoot()->GetChildDynamicCast<Text>("ChatLog", true);
        if (chatLog)
            chatLog->SetText(chatLog->GetText() + log);
    }
}

// ============================================================================
// Death Analytics — Home key admin query
// ============================================================================

void TerrainNode::RequestDeathAnalytics()
{
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    serverConn->SendMessage(MSG_QUERY_DEATH_ANALYTICS, true, true, buf);
}

void TerrainNode::HandleDeathAnalyticsResult(MemoryBuffer& msg)
{
    static const char* causeNames[] = {
        "combat", "drown", "starve", "age", "scavenge", "fall", "fire", "dehydrate", "freeze"
    };

    String log = "\n=== DEATH ANALYTICS ===\n\n";

    // Section 1: deaths by cause
    unsigned char byCauseCount = msg.ReadU8();
    log += "--- Deaths by Cause ---\n";
    for (unsigned i = 0; i < byCauseCount; ++i)
    {
        unsigned char cause = msg.ReadU8();
        unsigned short total = msg.ReadU16();
        const char* name = (cause < 9) ? causeNames[cause] : "unknown";
        log += "  " + String(name) + ": " + String(total) + "\n";
    }

    // Section 2: deaths by species
    unsigned char bySpeciesCount = msg.ReadU8();
    log += "\n--- Deaths by Species ---\n";
    for (unsigned i = 0; i < bySpeciesCount; ++i)
    {
        String species = msg.ReadString();
        unsigned short total = msg.ReadU16();
        log += "  " + species + ": " + String(total) + "\n";
    }

    // Section 3: deaths per game day
    unsigned char byDayCount = msg.ReadU8();
    log += "\n--- Deaths per Day (last 30) ---\n";
    for (unsigned i = 0; i < byDayCount; ++i)
    {
        int day = msg.ReadI32();
        unsigned short deaths = msg.ReadU16();
        log += "  Day " + String(day) + ": " + String(deaths) + "\n";
    }
    log += "===========================\n";

    URHO3D_LOGRAW(log);

    // Display in chat area
    auto* ui = GetSubsystem<UI>();
    if (ui)
    {
        auto* chatLog = ui->GetRoot()->GetChildDynamicCast<Text>("ChatLog", true);
        if (chatLog)
            chatLog->SetText(chatLog->GetText() + log);
    }
}

// ============================================================================
// Grass — cross-billboard clumps written as UMDL .mdl, loaded via cache
// ============================================================================

void TerrainNode::CreateGrass()
{
    if (!terrain_)
        return;
    Node* grassHost = scene_->CreateChild("GrassHost", LOCAL);
    grassSystem_ = grassHost->CreateComponent<GrassSystem>();
    grassSystem_->Initialize(terrain_, 200.0f, 0.5f, 100.0f);
    grassSystem_->SetWind(0.5f, Vector2(1.0f, 0.3f));
    grassSystem_->SetSeasonalTint(Color(1.0f, 1.0f, 1.0f, 1.0f));
    if (ecosystem_ && ecosystem_->GetVegetationTexture())
        grassSystem_->SetDensityMap(ecosystem_->GetVegetationTexture());
}

// --- Font / Theme ---

void TerrainNode::LoadThemePrefs()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    String path = fs->GetProgramDir() + "Data/UI/theme.json";
    if (!fs->FileExists(path))
        return;

    File file(context_, path, FILE_READ);
    if (!file.IsOpen())
        return;

    unsigned size = file.GetSize();
    String content;
    content.Resize(size);
    file.Read(&content[0], size);
    file.Close();

    Vector<String> lines = content.Split('\n');
    for (unsigned i = 0; i < lines.Size(); ++i)
    {
        String line = lines[i].Trimmed();
        if (line.StartsWith("\"font\""))
        {
            unsigned colon = line.Find(':');
            if (colon != String::NPOS)
            {
                String val = line.Substring(colon + 1).Trimmed();
                val.Replace("\"", "");
                val.Replace(",", "");
                val = val.Trimmed();
                if (!val.Empty())
                    currentFontName_ = val;
            }
        }
        else if (line.StartsWith("\"fontSize\""))
        {
            unsigned colon = line.Find(':');
            if (colon != String::NPOS)
            {
                String val = line.Substring(colon + 1).Trimmed();
                val.Replace(",", "");
                int sz = atoi(val.Trimmed().CString());
                if (sz >= 8 && sz <= 24)
                    currentFontSize_ = sz;
            }
        }
        else if (line.StartsWith("\"audio"))
        {
            unsigned colon = line.Find(':');
            if (colon != String::NPOS)
            {
                String key = line.Substring(0, colon).Trimmed();
                key.Replace("\"", "");
                String val = line.Substring(colon + 1).Trimmed();
                val.Replace(",", "");
                float gain = Clamp((float)atof(val.CString()), 0.0f, 1.0f);
                auto* audio = GetSubsystem<Audio>();
                if (audio)
                {
                    if (key == "audioMaster")  audio->SetMasterGain(SOUND_MASTER, gain);
                    else if (key == "audioEffects") audio->SetMasterGain(SOUND_EFFECT, gain);
                    else if (key == "audioAmbient") audio->SetMasterGain(SOUND_AMBIENT, gain);
                    else if (key == "audioMusic")   audio->SetMasterGain(SOUND_MUSIC, gain);
                }
            }
        }
    }
}

void TerrainNode::SaveThemePrefs()
{
    auto* fs = GetSubsystem<FileSystem>();
    String path = fs->GetProgramDir() + "Data/UI/theme.json";

    String dir = path.Substring(0, path.FindLast('/'));
    if (!fs->DirExists(dir))
        fs->CreateDir(dir);

    File file(context_, path, FILE_WRITE);
    if (!file.IsOpen())
        return;

    auto* audio = GetSubsystem<Audio>();
    String json = "{\n";
    json += "  \"font\": \"" + currentFontName_ + "\",\n";
    json += "  \"fontSize\": " + String(currentFontSize_) + ",\n";
    if (audio)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "  \"audioMaster\": %.2f,\n  \"audioEffects\": %.2f,\n  \"audioAmbient\": %.2f,\n  \"audioMusic\": %.2f\n",
            audio->GetMasterGain(SOUND_MASTER), audio->GetMasterGain(SOUND_EFFECT),
            audio->GetMasterGain(SOUND_AMBIENT), audio->GetMasterGain(SOUND_MUSIC));
        json += String(buf);
    }
    else
        json += "  \"audioMaster\": 1.0\n";
    json += "}\n";

    file.Write(json.CString(), json.Length());
    file.Close();
}

static void UpdateFontsRecursive(UIElement* element, Font* font, int baseSize)
{
    auto* text = dynamic_cast<Text*>(element);
    if (text && text->GetFont())
    {
        int oldSize = text->GetFontSize();
        int offset = oldSize - 11;
        text->SetFont(font, baseSize + offset);
    }
    for (unsigned i = 0; i < element->GetNumChildren(); ++i)
        UpdateFontsRecursive(element->GetChild(i), font, baseSize);
}

void TerrainNode::ApplyFont(const String& fontName, int fontSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* newFont = cache->GetResource<Font>("Fonts/" + fontName + ".ttf");
    if (!newFont)
        return;
    font_ = newFont;
    currentFontName_ = fontName;
    currentFontSize_ = fontSize;

    auto* ui = GetSubsystem<UI>();
    UpdateFontsRecursive(ui->GetRoot(), font_, currentFontSize_);
}

void TerrainNode::HandleSettingsMenu(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace ItemSelected;
    auto* list = static_cast<DropDownList*>(eventData[P_ELEMENT].GetPtr());
    unsigned sel = eventData[P_SELECTION].GetU32();
    if (sel == M_MAX_UNSIGNED || !list)
        return;

    auto* item = list->GetItem(sel);
    if (!item)
        return;

    String text = static_cast<Text*>(item)->GetText();

    // Font items are prefixed "Font: "
    if (text.StartsWith("Font: "))
    {
        String fontName = text.Substring(6);
        ApplyFont(fontName, currentFontSize_);
        SaveThemePrefs();
    }
    // Size items are prefixed "Size: "
    else if (text.StartsWith("Size: "))
    {
        int sz = atoi(text.Substring(6).CString());
        if (sz >= 8 && sz <= 24)
        {
            ApplyFont(currentFontName_, sz);
            SaveThemePrefs();
        }
    }

    list->SetSelection(M_MAX_UNSIGNED);
}

// ============================================================================
// Survival HUD
// ============================================================================

void TerrainNode::HandleVitalUpdate(MemoryBuffer& msg)
{
    vitalHp_ = msg.ReadI32();
    vitalMaxHp_ = msg.ReadI32();
    vitalHunger_ = (float)msg.ReadI32();    // server sends as I32
    vitalThirst_ = (float)msg.ReadI32();    // server sends as I32
    vitalStamina_ = (float)msg.ReadI32();   // server sends as I32
    vitalWarmth_ = msg.ReadFloat();
    vitalAlive_ = msg.ReadBool();
    vitalSpeedMult_ = msg.ReadFloat();

    UpdateVitalBars();
}

void TerrainNode::UpdateVitalBars()
{
    if (!hud_)
        return;

    // Feed normalized 0.0-1.0 values to HUD component
    float hp = (vitalMaxHp_ > 0) ? Clamp((float)vitalHp_ / (float)vitalMaxHp_, 0.0f, 1.0f) : 1.0f;
    hud_->SetHP(hp);
    hud_->SetHunger(Clamp(vitalHunger_ / 100.0f, 0.0f, 1.0f));
    hud_->SetThirst(Clamp(vitalThirst_ / 100.0f, 0.0f, 1.0f));
    hud_->SetStamina(Clamp(vitalStamina_ / 100.0f, 0.0f, 1.0f));

    float effectiveTemp = CalculateEffectiveTemperature();
    hud_->SetTemperature(effectiveTemp);

    // Status icons driven by survival stats (DB thresholds)
    hud_->SetStatusIcon(ICON_STARVING,    vitalHunger_  < (float)Creature::GetHungerRules().criticalThreshold);
    hud_->SetStatusIcon(ICON_DEHYDRATED,  vitalThirst_  < (float)Creature::GetThirstRules().criticalThreshold);
    hud_->SetStatusIcon(ICON_EXHAUSTED,   vitalStamina_ < (float)Creature::GetStaminaRules().lowThreshold);
    hud_->SetStatusIcon(ICON_FREEZING,    effectiveTemp < Creature::GetWarmthRules().coldThreshold);
    hud_->SetStatusIcon(ICON_OVERHEATING, effectiveTemp > Creature::GetWarmthRules().heatThreshold);
}

void TerrainNode::UpdateContextHintRaycast()
{
    if (!hud_ || !cameraNode_ || !scene_)
    {
        hud_->SetContextHint("");
        return;
    }

    auto* camera = cameraNode_->GetComponent<Camera>();
    if (!camera)
    {
        hud_->SetContextHint("");
        return;
    }

    // Center-screen ray (crosshair)
    Ray ray = camera->GetScreenRay(0.5f, 0.5f);

    // Use octree raycast for interactable detection (works with all drawables)
    auto* octree = scene_->GetComponent<Octree>();
    if (!octree)
    {
        hud_->SetContextHint("");
        return;
    }

    Vector<RayQueryResult> results;
    RayOctreeQuery query(results, ray, RAY_TRIANGLE, INTERACT_DISTANCE);
    octree->Raycast(query);

    // Walk results, skip terrain patches, find first interactable
    for (unsigned i = 0; i < results.Size(); ++i)
    {
        Node* node = results[i].drawable_->GetNode();
        if (!node)
            continue;

        // Skip terrain
        if (dynamic_cast<TerrainPatch*>(results[i].drawable_))
            continue;

        // Walk up to find a meaningful parent (animals, campfire, etc. are top-level scene children)
        Node* candidate = node;
        while (candidate && candidate->GetParent() && candidate->GetParent() != scene_)
            candidate = candidate->GetParent();

        if (!candidate || candidate == scene_)
            continue;

        const String& name = candidate->GetName();

        // Determine context hint based on node name/type
        if (name == "Campfire")
        {
            hud_->SetContextHint("E: Add Fuel");
            return;
        }
        // Check if this is any animal (has a Creature component)
        if (auto* animal = candidate->GetDerivedComponent<Creature>())
        {
            // CORPSE included so the harvest hint stays alive for the full
            // post-death window (5s DIE + 120s CORPSE), not just the 5s
            // death animation. Server-side MSG_HARVEST is state-agnostic
            // (see AuthServer::HandleHarvest), so this is purely a UX fix.
            const CreatureState st = animal->GetState();
            if (st == CREATURE_DIE || st == CREATURE_CORPSE || st == CREATURE_TRAPPED)
            {
                hud_->SetContextHint("E: Harvest");
                focusedHarvestNodeId_ = candidate->GetID();
                focusedHarvestCreatureId_ = animal->GetCreatureId();
                return;
            }
            // Living animal — check if it's a HumanNPC for trade hint
            auto* npc = candidate->GetDerivedComponent<HumanNPC>(false);
            if (npc && candidate != possessedNPC_.Get())
            {
                float dist = possessedNPC_ ?
                    (candidate->GetWorldPosition() - possessedNPC_->GetWorldPosition()).Length() : 999.0f;
                if (dist <= 3.0f)
                    hud_->SetContextHint("T: Trade");
            }
            continue;
        }
        // Check for placed buildings (gates)
        if (buildingSystem_)
        {
            Variant placedVar = candidate->GetVar("PlacedBuildingId");
            if (!placedVar.IsEmpty())
            {
                int placedId = placedVar.GetI32();
                if (buildingSystem_->IsGate(placedId))
                {
                    bool open = buildingSystem_->IsGateOpen(placedId);
                    hud_->SetContextHint(open ? "E: Close Gate" : "E: Open Gate");
                    focusedGateId_ = placedId;
                    return;
                }
                // Phase 2b: Woodpile shows wood counts on hover. Detect by
                // typeId rather than name so a renamed building still works.
                Variant typeVar = candidate->GetVar("BuildingTypeId");
                if (!typeVar.IsEmpty() && typeVar.GetI32() == 56)
                {
                    if (auto* pb = buildingSystem_->FindPlacedMutable(placedId))
                    {
                        char buf[96];
                        snprintf(buf, sizeof(buf),
                            "Woodpile  SW %d/%d  HW %d/%d   E: Deposit",
                            pb->softwoodBu, pb->woodCapacity,
                            pb->hardwoodBu, pb->woodCapacity);
                        hud_->SetContextHint(String(buf));
                        focusedWoodpileId_ = placedId;
                        return;
                    }
                }

                // Storage buildings (Chest, Barrel, Cabinet, etc.) — show open hint
                if (!typeVar.IsEmpty())
                {
                    int tid = typeVar.GetI32();
                    const BuildingTypeInfo* binfo = buildingSystem_->FindTypeInfo(tid);
                    if (binfo)
                    {
                        if (binfo->storageSlots > 0)
                        {
                            hud_->SetContextHint("E: Open " + binfo->name);
                            return;
                        }
                        if (binfo->sleepCapacity > 0)
                        {
                            hud_->SetContextHint("E: Sleep (" + binfo->name + ")");
                            return;
                        }
                        // Grave marker — show deceased info
                        if (tid == 60)
                        {
                            hud_->SetContextHint("Grave");
                            return;
                        }
                        // Canoe — board/disembark
                        if (tid == 70)
                        {
                            hud_->SetContextHint(inCanoe_ ? "E: Disembark" : "E: Board Canoe");
                            return;
                        }
                    }
                }
            }
        }

        // Check for server-authoritative trees (choppable)
        for (auto tIt = treeIdToNode_.Begin(); tIt != treeIdToNode_.End(); ++tIt)
        {
            if (tIt->second_ && tIt->second_ == candidate)
            {
                hud_->SetContextHint("E: Chop  Q: Tap");
                focusedTreeId_ = tIt->first_;
                return;
            }
        }

        // Ore outcrops (mineable deposits)
        if (name == "Outcrop")
        {
            // Show ore type from deposit map if available
            String hint = "E: Mine";
            if (terrain_ && metalDeposits_)
            {
                Vector3 pos = candidate->GetWorldPosition();
                IntVector2 px = terrain_->WorldToHeightMap(pos);
                int oreType = metalDeposits_->GetOreType(px.x_, px.y_);
                int qty = metalDeposits_->GetOreQty(px.x_, px.y_);
                static const char* oreNames[] = {
                    "", "Copper", "Tin", "Iron", "Gold", "Silver", "Coal", "Obsidian", "Flint",
                    "Tungsten", "Nickel", "Rich Carbon", "", "Chromium"
                };
                if (oreType > 0 && oreType < 14)
                    hint = String(oreNames[oreType]) + " (" + String(qty) + ")  E: Mine";
            }
            hud_->SetContextHint(hint);
            focusedOutcropNode_ = candidate;
            return;
        }

        // Crop plants — show growth stage and harvest hint
        if (name == "PlacedCrop")
        {
            int stage = candidate->GetVar("GrowthStage").GetI32();
            if (stage >= 3)
                hud_->SetContextHint("E: Harvest Crop");
            else
            {
                static const char* stageNames[] = {"Seedling", "Sprout", "Growing", "Mature"};
                hud_->SetContextHint(String(stageNames[Clamp(stage, 0, 3)]));
            }
            return;
        }

        // Generic interactable objects (gather sources, etc.)
    }

    focusedGateId_ = -1;
    focusedWoodpileId_ = -1;
    focusedHarvestNodeId_ = 0;
    focusedHarvestCreatureId_ = 0;
    focusedTreeId_ = 0;
    focusedOutcropNode_ = nullptr;

    // Water proximity hint — show when player is near water level or a well
    if (characterNode_)
    {
        Vector3 playerPos = characterNode_->GetWorldPosition();
        const float waterLevel = 5.5f;
        if (playerPos.y_ < waterLevel + 1.5f)
        {
            hud_->SetContextHint("E: Drink");
            return;
        }
        // Check for nearby Well (type 57)
        if (buildingSystem_)
        {
            int nearId = buildingSystem_->FindNearestBuilding(playerPos, 5.0f);
            if (nearId >= 0)
            {
                const auto& buildings = buildingSystem_->GetPlacedBuildings();
                for (unsigned i = 0; i < buildings.Size(); ++i)
                {
                    if (buildings[i].placedId == nearId && buildings[i].buildingTypeId == 57)
                    {
                        hud_->SetContextHint("E: Drink from Well");
                        return;
                    }
                }
            }
        }
    }

    hud_->SetContextHint("");
}

// ============================================================================
// Resource Chain Phase 2 — Trap-check scanner (client-driven detection)
// ============================================================================

void TerrainNode::ScanTrapsForCatches()
{
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn || !serverConn->IsConnected())
        return;

    const float radiusSq = TRAP_CHECK_RADIUS * TRAP_CHECK_RADIUS;

    // Walk every armed-locally trap. The server is the authority on armed/disarmed
    // state — once disarmed, our checks return silently. We don't track that
    // locally; the dedupe set absorbs the noise.
    for (auto trapIt = trapNodes_.Begin(); trapIt != trapNodes_.End(); )
    {
        const unsigned trapNodeId = trapIt->first_;
        Node* trapNode = trapIt->second_;
        if (!trapNode)
        {
            // Stale weak pointer — node was removed without us seeing MSG_TRAP_REMOVED.
            trapCheckSent_.Erase(trapNodeId);
            trapIt = trapNodes_.Erase(trapIt);
            continue;
        }
        ++trapIt;

        const Vector3 trapPos = trapNode->GetWorldPosition();
        HashSet<unsigned>& sentSet = trapCheckSent_[trapNodeId];

        // Snapshot the current set, so we can prune entries whose creature has
        // left range without invalidating iterators while we send.
        HashSet<unsigned> stillInRange;

        for (unsigned i = 0; i < animalNodes_.Size(); ++i)
        {
            Node* animalNode = animalNodes_[i].Get();
            if (!animalNode || !animalNode->IsEnabled())
                continue;
            Creature* creature = animalNode->GetDerivedComponent<Creature>();
            if (!creature)
                continue;
            // Only living creatures can be caught — skip dead, corpse, already-trapped.
            const CreatureState st = creature->GetState();
            if (st == CREATURE_DIE || st == CREATURE_CORPSE || st == CREATURE_TRAPPED)
                continue;

            const Vector3 cp = animalNode->GetWorldPosition();
            const Vector3 d  = cp - trapPos;
            if (d.LengthSquared() > radiusSq)
                continue;

            const unsigned creatureNodeId = animalNode->GetID();
            stillInRange.Insert(creatureNodeId);

            // Dedupe: only send the first time this (trap, creature) pair enters range.
            if (sentSet.Contains(creatureNodeId))
                continue;
            sentSet.Insert(creatureNodeId);

            VectorBuffer buf;
            buf.WriteU32(trapNodeId);
            buf.WriteU32(creatureNodeId);
            buf.WriteI32(creature->GetCreatureId());
            buf.WriteFloat(cp.x_);
            buf.WriteFloat(cp.y_);
            buf.WriteFloat(cp.z_);
            serverConn->SendMessage(MSG_TRAP_CHECK, true, true, buf);
        }

        // Prune dedupe set: any creatureId no longer in range can re-fire later.
        for (auto sIt = sentSet.Begin(); sIt != sentSet.End(); )
        {
            if (!stillInRange.Contains(*sIt))
                sIt = sentSet.Erase(sIt);
            else
                ++sIt;
        }
    }
}

// ============================================================================
// Inventory UI
// ============================================================================

void TerrainNode::HandleInventoryUpdate(MemoryBuffer& msg)
{
    int count = msg.ReadI32();
    inventoryWeight_ = msg.ReadFloat();
    inventoryMaxWeight_ = msg.ReadFloat();
    inventoryAbsWeight_ = msg.ReadFloat();
    inventoryMaxSlots_ = msg.ReadI32();

    inventory_.Clear();
    for (int i = 0; i < count; ++i)
    {
        ClientInventorySlot slot;
        slot.itemId = msg.ReadI32();
        slot.quantity = msg.ReadI32();
        slot.durability = msg.ReadI32();
        slot.slotType = msg.ReadString();
        inventory_.Push(slot);
    }

    // Resolve item names, categories, tiers from local GameDB
    if (gameDB_)
    {
        for (unsigned i = 0; i < inventory_.Size(); ++i)
        {
            ItemInfo info;
            if (gameDB_->GetItem(inventory_[i].itemId, info))
            {
                inventory_[i].itemName = info.name;
                inventory_[i].itemCategory = info.category;
                inventory_[i].itemWeight = info.weight;
                inventory_[i].weight = info.weight * inventory_[i].quantity;
                inventory_[i].tier = info.tier;
            }
            else
            {
                inventory_[i].itemName = "Item #" + String(inventory_[i].itemId);
            }
        }
    }
    URHO3D_LOGINFOF("Inventory received: %d items, weight %.1f/%.1f kg", count, inventoryWeight_, inventoryMaxWeight_);

    if (!inventoryWindow_)
        CreateInventoryUI();
    RefreshInventoryGrid();
}

void TerrainNode::HandleInventoryDelta(MemoryBuffer& msg)
{
    int itemId = msg.ReadI32();
    int quantity = msg.ReadI32();
    bool added = msg.ReadBool();
    inventoryWeight_ = msg.ReadFloat();

    if (added)
    {
        // Try to stack
        bool stacked = false;
        for (unsigned i = 0; i < inventory_.Size(); ++i)
        {
            if (inventory_[i].itemId == itemId && inventory_[i].slotType == "bag")
            {
                inventory_[i].quantity += quantity;
                stacked = true;
                break;
            }
        }
        if (!stacked)
        {
            ClientInventorySlot slot;
            slot.itemId = itemId;
            slot.quantity = quantity;
            slot.slotType = "bag";
            if (gameDB_)
            {
                ItemInfo info;
                if (gameDB_->GetItem(itemId, info))
                {
                    slot.itemName = info.name;
                    slot.itemCategory = info.category;
                    slot.itemWeight = info.weight;
                    slot.weight = info.weight * quantity;
                    slot.tier = info.tier;
                }
            }
            inventory_.Push(slot);
        }
        URHO3D_LOGINFOF("Picked up item %d x%d", itemId, quantity);
    }
    else
    {
        // Remove quantity
        for (unsigned i = 0; i < inventory_.Size(); ++i)
        {
            if (inventory_[i].itemId == itemId && inventory_[i].slotType == "bag")
            {
                inventory_[i].quantity -= quantity;
                if (inventory_[i].quantity <= 0)
                    inventory_.Erase(i);
                break;
            }
        }
        URHO3D_LOGINFOF("Removed item %d x%d", itemId, quantity);
    }

    RefreshInventoryGrid();
}

void TerrainNode::CreateInventoryUI()
{
    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    inventoryWindow_ = new Window(context_);
    ui->GetRoot()->AddChild(inventoryWindow_);
    inventoryWindow_->SetStyleAuto(style);
    inventoryWindow_->SetSize(290, 480);
    inventoryWindow_->SetHorizontalAlignment(HA_CENTER);
    inventoryWindow_->SetVerticalAlignment(VA_CENTER);
    inventoryWindow_->SetMovable(true);
    inventoryWindow_->SetOpacity(0.9f);
    inventoryWindow_->SetVisible(false);
    inventoryWindow_->SetLayout(LM_VERTICAL, 4, IntRect(6, 6, 6, 6));

    // Title
    auto* title = inventoryWindow_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Inventory");
    title->SetColor(Color(0.9f, 0.85f, 0.7f));
    title->SetHorizontalAlignment(HA_CENTER);

    // --- Equipment section (Phase 2) ---
    CreateEquipmentUI(inventoryWindow_);

    // Separator
    auto* separator = inventoryWindow_->CreateChild<BorderImage>();
    separator->SetColor(Color(0.4f, 0.4f, 0.4f, 0.5f));
    separator->SetMinHeight(2);
    separator->SetMaxHeight(2);

    // Bag label
    auto* bagLabel = inventoryWindow_->CreateChild<Text>();
    bagLabel->SetFont(font_, 11);
    bagLabel->SetText("Bag");
    bagLabel->SetColor(Color(0.7f, 0.65f, 0.55f));

    // Filter buttons row
    {
        auto* filterRow = inventoryWindow_->CreateChild<UIElement>();
        filterRow->SetLayout(LM_HORIZONTAL, 2);
        filterRow->SetMinHeight(20);

        static const char* filterNames[] = {"All", "Wpn", "Tool", "Armr", "Food", "Mat"};
        static const char* filterCats[] = {"", "weapon", "tool", "armor", "food", "material"};
        filterButtons_.Clear();
        for (int f = 0; f < 6; ++f)
        {
            auto* fbtn = filterRow->CreateChild<Button>();
            fbtn->SetStyleAuto(style);
            fbtn->SetSize(42, 18);
            fbtn->SetVar("FilterCat", String(filterCats[f]));
            auto* ftxt = fbtn->CreateChild<Text>();
            ftxt->SetFont(font_, 8);
            ftxt->SetText(filterNames[f]);
            ftxt->SetHorizontalAlignment(HA_CENTER);
            ftxt->SetVerticalAlignment(VA_CENTER);
            SubscribeToEvent(fbtn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleFilterButton));
            filterButtons_.Push(fbtn);
        }
        UpdateFilterButtonColors();
    }

    // Grid area — 3 rows of 5 slots (15 max with bag bonus)
    auto* gridContainer = inventoryWindow_->CreateChild<UIElement>();
    gridContainer->SetLayout(LM_VERTICAL, 2);
    gridContainer->SetMinHeight(160);

    const int slotsPerRow = 5;
    const int slotSize = 48;

    inventorySlotButtons_.Clear();
    for (int row = 0; row < 3; ++row)
    {
        auto* rowElem = gridContainer->CreateChild<UIElement>();
        rowElem->SetLayout(LM_HORIZONTAL, 2);
        rowElem->SetMinHeight(slotSize + 4);

        for (int col = 0; col < slotsPerRow; ++col)
        {
            int idx = row * slotsPerRow + col;
            auto* btn = rowElem->CreateChild<Button>();
            btn->SetStyleAuto(style);
            btn->SetSize(slotSize, slotSize);
            btn->SetColor(Color(0.2f, 0.2f, 0.25f, 0.8f));
            btn->SetVar("BagIndex", idx);

            auto* label = btn->CreateChild<Text>();
            label->SetFont(font_, 9);
            label->SetColor(Color::WHITE);
            label->SetHorizontalAlignment(HA_CENTER);
            label->SetVerticalAlignment(VA_CENTER);
            label->SetText("");

            btn->SetDragDropMode(DD_SOURCE_AND_TARGET);

            // Left-click to select/equip, right-click for context menu
            SubscribeToEvent(btn, E_CLICK, URHO3D_HANDLER(TerrainNode, HandleBagSlotClick));
            // Hover for tooltip
            SubscribeToEvent(btn, E_HOVERBEGIN, URHO3D_HANDLER(TerrainNode, HandleSlotHoverBegin));
            SubscribeToEvent(btn, E_HOVEREND, URHO3D_HANDLER(TerrainNode, HandleSlotHoverEnd));

            inventorySlotButtons_.Push(btn);
        }
    }

    // Weight display
    auto* weightRow = inventoryWindow_->CreateChild<UIElement>();
    weightRow->SetLayout(LM_HORIZONTAL, 4);
    weightRow->SetMinHeight(20);

    inventoryWeightText_ = weightRow->CreateChild<Text>();
    inventoryWeightText_->SetFont(font_, 11);
    inventoryWeightText_->SetColor(Color(0.7f, 0.7f, 0.7f));
    inventoryWeightText_->SetText("0.0 / 30.0 kg");

    // Sort button
    sortButton_ = weightRow->CreateChild<Button>();
    sortButton_->SetStyleAuto(style);
    sortButton_->SetSize(40, 18);
    auto* sortLabel = sortButton_->CreateChild<Text>();
    sortLabel->SetFont(font_, 9);
    sortLabel->SetText("Sort");
    sortLabel->SetHorizontalAlignment(HA_CENTER);
    sortLabel->SetVerticalAlignment(VA_CENTER);
    SubscribeToEvent(sortButton_, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleSortButton));

    // Weight bar
    auto* weightBarBg = inventoryWindow_->CreateChild<BorderImage>();
    weightBarBg->SetColor(Color(0.15f, 0.15f, 0.15f, 0.6f));
    weightBarBg->SetMinHeight(8);
    weightBarBg->SetMaxHeight(8);

    inventoryWeightBar_ = weightBarBg->CreateChild<BorderImage>();
    inventoryWeightBar_->SetColor(Color(0.3f, 0.7f, 0.3f));
    inventoryWeightBar_->SetMinHeight(8);
    inventoryWeightBar_->SetMaxHeight(8);

    // Close button
    auto* closeBtn = inventoryWindow_->CreateChild<Button>();
    closeBtn->SetStyleAuto(style);
    closeBtn->SetMinHeight(24);
    auto* closeTxt = closeBtn->CreateChild<Text>();
    closeTxt->SetFont(font_, 11);
    closeTxt->SetText("Close");
    closeTxt->SetHorizontalAlignment(HA_CENTER);
    SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&) { ToggleInventory(); });

    // Drag-and-drop reorder
    SubscribeToEvent(E_DRAGDROPFINISH, URHO3D_HANDLER(TerrainNode, HandleInventoryDragDrop));
}

void TerrainNode::RefreshInventoryGrid()
{
    if (!inventoryWindow_ || inventorySlotButtons_.Empty())
        return;

    // Clear all slots
    for (unsigned i = 0; i < inventorySlotButtons_.Size(); ++i)
    {
        auto* label = inventorySlotButtons_[i]->GetChildStaticCast<Text>(0);
        if (label)
            label->SetText("");
        inventorySlotButtons_[i]->SetColor(Color(0.2f, 0.2f, 0.25f, 0.8f));
        inventorySlotButtons_[i]->SetVar("InvIndex", -1);  // no item mapped

        // Gray out slots beyond max
        bool available = (int)i < inventoryMaxSlots_;
        inventorySlotButtons_[i]->SetVisible(available);
    }

    // Fill occupied slots (bag items only, filtered by category)
    unsigned slotIdx = 0;
    for (unsigned i = 0; i < inventory_.Size() && slotIdx < inventorySlotButtons_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;

        // Apply category filter
        if (!inventoryFilter_.Empty())
        {
            const String& cat = inventory_[i].itemCategory;
            if (inventoryFilter_ == "armor")
            {
                if (cat != "armor" && cat != "clothing")
                    continue;
            }
            else if (inventoryFilter_ == "food")
            {
                if (cat != "food" && cat != "consumable" && cat != "medical")
                    continue;
            }
            else if (cat != inventoryFilter_)
                continue;
        }

        auto* btn = inventorySlotButtons_[slotIdx];
        btn->SetVar("InvIndex", (int)i);  // map grid slot to inventory_ index
        auto* label = btn->GetChildStaticCast<Text>(0);
        if (label)
        {
            // Show item name if available, else ID
            String name = inventory_[i].itemName.Length() > 0
                ? inventory_[i].itemName
                : String(inventory_[i].itemId);
            String text = name;
            if (inventory_[i].quantity > 1)
                text += " x" + String(inventory_[i].quantity);
            label->SetText(text);

            // Color consumable items differently
            if (IsConsumable(inventory_[i].itemCategory))
                label->SetColor(Color(0.7f, 0.9f, 0.7f));  // greenish for food/drink
            else
                label->SetColor(Color::WHITE);
        }

        // Durability indicator — tint the slot button border when item has durability
        if (inventory_[i].durability >= 0)
        {
            // durability 0 = broken (red), higher = more green
            // We don't know max durability here, so show red at 0, yellow at low, normal otherwise
            if (inventory_[i].durability == 0)
                btn->SetColor(Color(0.5f, 0.2f, 0.2f, 0.9f));  // broken — red tint
            else if (inventory_[i].durability < 5)
                btn->SetColor(Color(0.5f, 0.45f, 0.2f, 0.9f)); // low durability — yellow tint
            else
                btn->SetColor(Color(0.3f, 0.35f, 0.4f, 0.9f)); // normal
        }
        else
        {
            btn->SetColor(Color(0.3f, 0.35f, 0.4f, 0.9f));
        }
        ++slotIdx;
    }

    // Update weight display
    if (inventoryWeightText_)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f / %.1f kg", inventoryWeight_, inventoryMaxWeight_);
        inventoryWeightText_->SetText(buf);

        // Color based on weight
        if (inventoryWeight_ > inventoryMaxWeight_)
            inventoryWeightText_->SetColor(Color(0.9f, 0.2f, 0.2f));  // red — heavy
        else if (inventoryWeight_ > inventoryMaxWeight_ * 0.7f)
            inventoryWeightText_->SetColor(Color(0.9f, 0.7f, 0.2f));  // yellow — getting heavy
        else
            inventoryWeightText_->SetColor(Color(0.7f, 0.7f, 0.7f));  // normal
    }

    // Weight bar
    if (inventoryWeightBar_)
    {
        float frac = Clamp(inventoryWeight_ / inventoryAbsWeight_, 0.0f, 1.0f);
        int parentW = inventoryWeightBar_->GetParent() ? inventoryWeightBar_->GetParent()->GetWidth() : 260;
        inventoryWeightBar_->SetWidth((int)(parentW * frac));

        if (inventoryWeight_ > inventoryMaxWeight_)
            inventoryWeightBar_->SetColor(Color(0.9f, 0.2f, 0.2f));
        else if (inventoryWeight_ > inventoryMaxWeight_ * 0.7f)
            inventoryWeightBar_->SetColor(Color(0.9f, 0.7f, 0.2f));
        else
            inventoryWeightBar_->SetColor(Color(0.3f, 0.7f, 0.3f));
    }

    // Also refresh equipment slots
    RefreshEquipmentSlots();

    // Wire encumbered status icon
    if (hud_)
        hud_->SetStatusIcon(ICON_ENCUMBERED, inventoryWeight_ > inventoryMaxWeight_);
}

void TerrainNode::HandleInventoryDragDrop(StringHash, VariantMap& eventData)
{
    using namespace DragDropFinish;

    auto* source = static_cast<UIElement*>(eventData[P_SOURCE].GetPtr());
    auto* target = static_cast<UIElement*>(eventData[P_TARGET].GetPtr());
    if (!source || !target || source == target)
        return;

    // Only handle drops between inventory slot buttons
    int srcIdx = source->GetVar("InvIndex").GetI32();
    int dstIdx = target->GetVar("InvIndex").GetI32();
    if (srcIdx < 0 || srcIdx >= (int)inventory_.Size())
        return;

    if (dstIdx >= 0 && dstIdx < (int)inventory_.Size())
    {
        // Swap two occupied slots
        Swap(inventory_[srcIdx], inventory_[dstIdx]);
    }
    else
    {
        // Dragged to empty slot — move item to that position
        // Insert at destination, remove from source
        ClientInventorySlot moved = inventory_[srcIdx];
        inventory_.Erase(srcIdx);

        // Find the grid position of the target to determine insert point
        int targetBagIdx = target->GetVar("BagIndex").GetI32();
        // Count bag items up to this grid position to find insert index
        int bagCount = 0;
        unsigned insertAt = inventory_.Size();
        for (unsigned i = 0; i < inventory_.Size(); ++i)
        {
            if (inventory_[i].slotType == "bag")
            {
                if (bagCount >= targetBagIdx)
                {
                    insertAt = i;
                    break;
                }
                ++bagCount;
            }
        }
        inventory_.Insert(insertAt, moved);
    }

    RefreshInventoryGrid();
}

void TerrainNode::ToggleInventory()
{
    inventoryOpen_ = !inventoryOpen_;

    if (!inventoryWindow_)
        CreateInventoryUI();

    inventoryWindow_->SetVisible(inventoryOpen_);

    if (inventoryOpen_)
    {
        RefreshInventoryGrid();
        // Switch to free cursor mode if not already
        auto* input = GetSubsystem<Input>();
        if (!menuOpen_)
        {
            input->SetMouseMode(MM_FREE);
            input->SetMouseVisible(true);
        }
    }
    else
    {
        // Return to relative mode if menu is also closed and window has focus
        if (!menuOpen_)
        {
            auto* input = GetSubsystem<Input>();
            if (input->HasFocus())
            {
                GetSubsystem<UI>()->SetFocusElement(nullptr);
                input->SetMouseMode(MM_RELATIVE);
                input->SetMouseVisible(false);
            }
        }
    }
}

void TerrainNode::SendPickup(unsigned nodeId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network->GetServerConnection();
    if (!serverConn)
        return;

    VectorBuffer buf;
    buf.WriteU32(nodeId);
    serverConn->SendMessage(MSG_PICKUP, true, true, buf);
}

void TerrainNode::SendResourceHarvest(const Vector3& worldPos, int itemId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network->GetServerConnection();
    if (!serverConn)
        return;

    // Reverse map item ID to ResourceType
    unsigned char resType = RES_NONE;
    switch (itemId)
    {
    case 1:  resType = RES_STONE;    break;
    case 2:  resType = RES_STICK;    break;
    case 3:  resType = RES_FIBER;    break;
    case 4:  resType = RES_CLAY;     break;
    case 5:  resType = RES_FLINT;    break;
    case 6:  resType = RES_BERRIES;  break;
    case 11: resType = RES_LOG;      break;
    case 15: resType = RES_SOFTWOOD; break;  // Phase 1b
    case 16: resType = RES_HARDWOOD; break;  // Phase 1b
    default: return;
    }

    VectorBuffer buf;
    buf.WriteFloat(worldPos.x_);
    buf.WriteFloat(worldPos.z_);
    buf.WriteU8(resType);
    serverConn->SendMessage(MSG_RESOURCE_HARVEST, true, true, buf);
}

void TerrainNode::SendDrop(int itemId, int qty)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network->GetServerConnection();
    if (!serverConn)
        return;

    VectorBuffer buf;
    buf.WriteI32(itemId);
    buf.WriteI32(qty);
    serverConn->SendMessage(MSG_DROP, true, true, buf);
}

// ============================================================================
// Combat (Phase 1) — melee attack, server validation, floating damage numbers
// ============================================================================

void TerrainNode::TryMeleeAttack()
{
    if (!scene_ || !cameraNode_)
        return;

    // Swing sound — plays regardless of hit/miss
    if (characterNode_)
    {
        auto* source = characterNode_->GetComponent<SoundSource3D>();
        if (source)
        {
            auto* cache = GetSubsystem<ResourceCache>();
            auto* swingSound = cache ? cache->GetResource<Sound>("Sounds/PlayerFist.wav") : nullptr;
            if (swingSound)
                source->Play(swingSound);
        }
    }

    auto* physicsWorld = scene_->GetComponent<PhysicsWorld>();
    if (!physicsWorld)
        return;

    // Raycast from camera forward (possession = camera behind character)
    auto* camera = cameraNode_->GetComponent<Camera>();
    if (!camera)
        return;
    Ray ray = camera->GetScreenRay(0.5f, 0.5f);

    // Check equipped weapon range — extend raycast for ranged weapons
    float attackRange = 5.0f;  // melee default
    for (unsigned s = 0; s < inventory_.Size(); ++s)
    {
        if (inventory_[s].slotType == "weapon" && gameDB_)
        {
            CombatInfo ci;
            if (gameDB_->GetCombatStats(inventory_[s].itemId, ci) && ci.range > 3.0f)
                attackRange = ci.range;
            break;
        }
    }

    Vector<PhysicsRaycastResult> results;
    physicsWorld->Raycast(results, ray, attackRange);

    for (unsigned i = 0; i < results.Size(); ++i)
    {
        Node* node = results[i].body_->GetNode();
        if (!node || node->HasComponent<Terrain>())
            continue;

        Creature* animal = node->GetDerivedComponent<Creature>(false);
        if (!animal)
            animal = node->GetParent() ? node->GetParent()->GetDerivedComponent<Creature>(false) : nullptr;
        if (!animal)
            continue;

        // Get equipped weapon item ID (0 = bare hands)
        int weaponId = 0;
        for (unsigned s = 0; s < inventory_.Size(); ++s)
        {
            if (inventory_[s].slotType == "weapon")
            {
                weaponId = inventory_[s].itemId;
                break;
            }
        }

        // Ranged weapon: spawn arrow/javelin visual stuck at hit point
        if (attackRange > 5.0f && scene_)
        {
            auto* cache = GetSubsystem<ResourceCache>();
            Node* arrowNode = scene_->CreateChild("StuckProjectile", LOCAL);
            arrowNode->SetPosition(results[i].position_);
            // Orient along hit normal, tilted toward shooter
            Vector3 dir = (results[i].position_ - cameraNode_->GetPosition()).Normalized();
            arrowNode->SetRotation(Quaternion(dir, results[i].normal_));
            // Parent to hit node so it moves with the target
            Node* hitNode = results[i].body_->GetNode();
            if (hitNode)
            {
                arrowNode->SetParent(hitNode);
                // Convert to local space
                arrowNode->SetPosition(hitNode->WorldToLocal(results[i].position_));
            }
            auto* arrowModel = arrowNode->CreateComponent<StaticModel>();
            arrowModel->SetModel(cache->GetResource<Model>("Models/Weapons/Arrow.mdl"));
            arrowModel->SetCastShadows(false);
            arrowNode->SetScale(0.5f);
        }

        // Send to server for authoritative dice roll.
        // Combat Phase 2: include creatureId so the server can lazy-register
        // the target's stats from GameDB on its first hit. The server keys
        // Send spawnId (server-assigned) so server can look up creatureStates_ directly.
        auto* network = GetSubsystem<Network>();
        auto* serverConn = network->GetServerConnection();
        if (serverConn)
        {
            VectorBuffer buf;
            buf.WriteU32(animal->GetSpawnId());  // server-side key (was local nodeId)
            buf.WriteI32(weaponId);
            buf.WriteI32(animal->GetCreatureId());
            serverConn->SendMessage(MSG_ATTACK, true, false, buf);
        }
        else
        {
            // Offline / single-player: resolve locally
            int attackMod  = 0;
            int baseDamage = 1;
            int damageVar  = 2;
            if (gameDB_ && weaponId > 0)
            {
                CombatInfo ci;
                if (gameDB_->GetCombatStats(weaponId, ci))
                {
                    attackMod  = ci.attackMod;
                    baseDamage = ci.damage;
                    damageVar  = ci.damageVar;
                }
            }
            int roll   = (rand() % 20) + 1;
            bool crit  = (roll == 20);
            bool fumble = (roll == 1);
            bool hit   = !fumble && (crit || (roll + attackMod) >= animal->GetDefense());
            int damage = 0;
            if (hit)
            {
                damage = baseDamage + (damageVar > 0 ? (rand() % damageVar) + 1 : 0);
                if (crit) damage *= 2;
            }
            {
                using namespace CombatResult;
                VariantMap& ed = GetEventDataMap();
                ed[P_DAMAGE] = damage;
                ed[P_CRIT]   = crit;
                ed[P_MISS]   = !hit;
                node->SendEvent(E_COMBAT_RESULT, ed);
            }
            if (hit)
                animal->TakeDamage(damage);
            if (fumble)
                ShowFumbleEffect();
        }
        return;
    }
}

void TerrainNode::HandleCombatResult(int /*msgID*/, MemoryBuffer& msg)
{
    unsigned targetSpawnId = msg.ReadU32();  // server-assigned spawnId (was local nodeId)
    bool hit    = msg.ReadBool();
    bool crit   = msg.ReadBool();
    bool fumble = msg.ReadBool();
    int damage  = msg.ReadI32();
    /*int roll  =*/ msg.ReadI32();  // not used client-side for display
    // Combat Phase 2: trailing hpRemaining field. Old servers don't include
    // it; MemoryBuffer reads past EOF return 0, which is benign — we ignore
    // sync if we read 0 from EOF rather than from the server.
    bool hasHpRemaining = !msg.IsEof();
    int hpRemaining = hasHpRemaining ? msg.ReadI32() : -1;

    // Resolve spawnId → local scene node via the spawn tracking map.
    auto sIt = spawnIdToNode_.Find(targetSpawnId);
    Node* target = (sIt != spawnIdToNode_.End() && sIt->second_) ? sIt->second_ : nullptr;

    if (target)
    {
        using namespace CombatResult;
        VariantMap& ed = GetEventDataMap();
        ed[P_DAMAGE] = damage;
        ed[P_CRIT]   = crit;
        ed[P_MISS]   = !hit;
        target->SendEvent(E_COMBAT_RESULT, ed);
    }

    if (target && hit)
    {
        // Hit sound — play on the target's position
        auto* hitSource = target->GetComponent<SoundSource3D>();
        if (hitSource)
        {
            auto* cache = GetSubsystem<ResourceCache>();
            auto* hitSound = cache ? cache->GetResource<Sound>("Sounds/PlayerFistHit.wav") : nullptr;
            if (hitSound)
                hitSource->Play(hitSound);
        }

        Creature* animal = target->GetDerivedComponent<Creature>(false);
        if (!animal && target->GetParent())
            animal = target->GetParent()->GetDerivedComponent<Creature>(false);
        if (animal)
        {
            // Server is authoritative on HP. Mirror its value rather than
            // reapplying TakeDamage locally (which would double-flee/fight
            // and risk diverging from the server's HP).
            if (hasHpRemaining)
                animal->SetHp(hpRemaining);
            else
                animal->TakeDamage(damage);  // legacy fallback
        }
    }

    // Fumble (nat 1): server unequips weapon, client shows feedback.
    // The inventory update from the server handles the actual slot change.
    if (fumble)
        ShowFumbleEffect();
}

void TerrainNode::HandleCreatureDeath(MemoryBuffer& msg)
{
    // Wire format from AuthServer::BroadcastCreatureDeath:
    //   spawnId u32, species string, position Vector3, creatureId i32, cause u8
    unsigned targetSpawnId = msg.ReadU32();
    String   species       = msg.ReadString();
    Vector3  pos           = msg.ReadVector3();
    int      creatureId    = msg.ReadI32();
    // Phase 5c: death cause byte (trailing — old servers don't send it)
    unsigned char deathCause = msg.IsEof() ? 0 : (unsigned char)msg.ReadByte();
    (void)pos; (void)creatureId;

    // Resolve spawnId → local scene node
    auto sIt = spawnIdToNode_.Find(targetSpawnId);
    Node* target = (sIt != spawnIdToNode_.End() && sIt->second_) ? sIt->second_ : nullptr;
    if (!target)
        return;  // already gone client-side

    Creature* creature = target->GetDerivedComponent<Creature>(false);
    if (!creature && target->GetParent())
        creature = target->GetParent()->GetDerivedComponent<Creature>(false);
    if (!creature)
        return;

    if (creature->GetState() == CREATURE_DIE)
        return;  // already dying — server confirmation matches client

    static const char* causeNames[] = {"none", "combat", "drown", "starve", "age", "scavenge", "fall", "fire", "dehydrate", "freeze"};
    const char* causeName = (deathCause < 10) ? causeNames[deathCause] : "unknown";
    URHO3D_LOGINFOF("MSG_CREATURE_DEATH: %s (spawnId %u) died — cause: %s",
        species.CString(), targetSpawnId, causeName);

    // Phase 5d: this IS the server's authoritative death — drive state directly.
    // SetHp(0) syncs HP, SetState triggers death anim + E_CREATUREDIED for
    // scavenger/scent/loot listeners.
    creature->SetHp(0);
    creature->SetState(CREATURE_DIE);
}

// ============================================================================
// Combat Fumble Effect — floating "FUMBLE!" text + camera shake + sound
// ============================================================================

void TerrainNode::ShowFumbleEffect()
{
    // Sound — clumsy weapon slip
    auto* cache = GetSubsystem<ResourceCache>();
    if (cameraNode_)
    {
        auto* src = cameraNode_->GetOrCreateComponent<SoundSource>();
        auto* snd = cache ? cache->GetResource<Sound>("Sounds/NutThrow.wav") : nullptr;
        if (src && snd)
            src->Play(snd);
    }

    // Floating "FUMBLE!" text — centered on screen, orange, floats up and fades
    auto* ui = GetSubsystem<UI>();
    auto* graphics = GetSubsystem<Graphics>();
    if (!ui || !graphics)
        return;

    // Remove previous fumble text if still visible
    if (!fumbleText_.Expired())
        fumbleText_->Remove();

    auto* font = cache ? cache->GetResource<Font>("Fonts/Anonymous Pro.ttf") : nullptr;
    if (!font)
        font = cache ? cache->GetResource<Font>("Fonts/BlueHighway.ttf") : nullptr;
    if (!font)
        return;

    auto* text = ui->GetRoot()->CreateChild<Text>();
    text->SetText("FUMBLE!");
    text->SetFont(font, 22);
    text->SetColor(Color(1.0f, 0.5f, 0.1f));  // orange
    text->SetTextAlignment(HA_CENTER);

    // Position center-screen, slightly above middle
    int sx = graphics->GetWidth() / 2 - 40;
    int sy = graphics->GetHeight() / 2 - 60;
    text->SetPosition(IntVector2(sx, sy));

    fumbleText_ = text;
    fumbleTextTimer_ = 0.0f;
    fumbleTextStartY_ = (float)sy;

    // Start camera shake (0.3s duration)
    cameraShakeTimer_ = 0.3f;
}

// ============================================================================
// Equipment Slots (Inventory Phase 2)
// ============================================================================

void TerrainNode::CreateEquipmentUI(UIElement* parent)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    auto* equipLabel = parent->CreateChild<Text>();
    equipLabel->SetFont(font_, 11);
    equipLabel->SetText("Equipment");
    equipLabel->SetColor(Color(0.7f, 0.65f, 0.55f));

    // Equipment slot definitions (matching equipment_slots table)
    const char* slotTypes[] = {"head", "body", "back", "hand", "offhand", "feet"};
    const char* slotLabels[] = {"Head", "Body", "Back", "Main Hand", "Off Hand", "Feet"};

    const int eqSlotSize = 44;

    // Top row: Head | Body | Back
    auto* topRow = parent->CreateChild<UIElement>();
    topRow->SetLayout(LM_HORIZONTAL, 4);
    topRow->SetMinHeight(eqSlotSize + 16);
    topRow->SetHorizontalAlignment(HA_CENTER);

    // Bottom row: Main Hand | Off Hand | Feet
    auto* botRow = parent->CreateChild<UIElement>();
    botRow->SetLayout(LM_HORIZONTAL, 4);
    botRow->SetMinHeight(eqSlotSize + 16);
    botRow->SetHorizontalAlignment(HA_CENTER);

    for (int i = 0; i < NUM_EQUIP_SLOTS; ++i)
    {
        UIElement* row = (i < 3) ? topRow : botRow;

        // Wrapper with label above slot
        auto* wrapper = row->CreateChild<UIElement>();
        wrapper->SetLayout(LM_VERTICAL, 1);
        wrapper->SetMinSize(eqSlotSize + 8, eqSlotSize + 16);

        auto* slotLabel = wrapper->CreateChild<Text>();
        slotLabel->SetFont(font_, 8);
        slotLabel->SetText(slotLabels[i]);
        slotLabel->SetColor(Color(0.6f, 0.55f, 0.45f));
        slotLabel->SetHorizontalAlignment(HA_CENTER);

        auto* btn = wrapper->CreateChild<Button>();
        btn->SetStyleAuto(style);
        btn->SetSize(eqSlotSize, eqSlotSize);
        btn->SetColor(Color(0.25f, 0.2f, 0.18f, 0.8f));  // earthy dark
        btn->SetVar("EquipSlot", String(slotTypes[i]));

        auto* text = btn->CreateChild<Text>();
        text->SetFont(font_, 8);
        text->SetColor(Color(0.87f, 0.8f, 0.73f));
        text->SetHorizontalAlignment(HA_CENTER);
        text->SetVerticalAlignment(VA_CENTER);
        text->SetText("--");

        // Click to unequip
        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleEquipSlotClick));

        equipSlots_[i].slotType = slotTypes[i];
        equipSlots_[i].label = slotLabels[i];
        equipSlots_[i].button = btn;
        equipSlots_[i].text = text;
    }
}

void TerrainNode::RefreshEquipmentSlots()
{
    for (int i = 0; i < NUM_EQUIP_SLOTS; ++i)
    {
        if (!equipSlots_[i].text)
            continue;

        // Find item equipped in this slot
        bool found = false;
        for (unsigned j = 0; j < inventory_.Size(); ++j)
        {
            if (inventory_[j].slotType == equipSlots_[i].slotType)
            {
                String display = inventory_[j].itemName.Length() > 0
                    ? inventory_[j].itemName
                    : String(inventory_[j].itemId);
                equipSlots_[i].text->SetText(display);
                equipSlots_[i].button->SetColor(Color(0.35f, 0.3f, 0.25f, 0.9f));  // occupied
                found = true;
                break;
            }
        }
        if (!found)
        {
            equipSlots_[i].text->SetText("--");
            equipSlots_[i].button->SetColor(Color(0.25f, 0.2f, 0.18f, 0.8f));  // empty
        }
    }

    // Update torch visual whenever equipment changes
    UpdateTorchVisual();
}

// ---------------------------------------------------------------------------
// Fire Carrying Phase 2: Torch Visual System
// ---------------------------------------------------------------------------

void TerrainNode::UpdateTorchVisual()
{
    // Check for fire items: hand slot (torches) + bag (bundles/vessels)
    int torchItem = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        // Fire bundle (872) always glows; bark vessel (873) depends on contents
        if (inventory_[i].slotType == "bag" && inventory_[i].itemId == 872)
            torchItem = 872;
        if (inventory_[i].slotType == "bag" && inventory_[i].itemId == 873)
        {
            // Check vessel contents from creature state
            unsigned char vc = 0;
            if (characterNode_)
            {
                auto* creature = characterNode_->GetDerivedComponent<Creature>(true);
                if (creature) vc = creature->GetVesselContents();
            }
            if (vc == 1) torchItem = 873;       // fire — show ember glow
            else if (vc == 2) torchItem = 8732;  // water — distinct visual (encoded as 8732)
            // vc == 0: empty — no visual
        }
        if (inventory_[i].slotType == "hand")
        {
            if (inventory_[i].itemId == 109 || inventory_[i].itemId == 871)
                torchItem = inventory_[i].itemId;
            break;
        }
    }

    if (torchItem == equippedTorchItem_)
        return;  // no change

    int prevTorch = equippedTorchItem_;
    equippedTorchItem_ = torchItem;

    if (torchItem > 0)
    {
        // Equip or switch torch — create/update flame
        if (torchFlameNode_)
            torchFlameNode_->Remove();
        CreateTorchFlame(torchItem);
    }
    else if (prevTorch > 0)
    {
        // Torch removed — rain extinguish or burned out
        RemoveTorchFlame(true);
    }
}

void TerrainNode::CreateTorchFlame(int fireItemId)
{
    Node* parent = characterNode_;
    if (!parent)
        parent = cameraNode_;
    if (!parent)
        return;

    // Try to attach to RightHand bone if available (possessed NPC)
    Node* attachPoint = parent;
    auto* animModel = parent->GetComponent<AnimatedModel>();
    if (!animModel)
    {
        for (unsigned i = 0; i < parent->GetNumChildren(); ++i)
        {
            animModel = parent->GetChild(i)->GetComponent<AnimatedModel>();
            if (animModel)
            {
                Node* handBone = parent->GetChild(i)->GetChild("RightHand", true);
                if (handBone)
                    attachPoint = handBone;
                break;
            }
        }
    }
    else
    {
        Node* handBone = parent->GetChild("RightHand", true);
        if (handBone)
            attachPoint = handBone;
    }

    torchFlameNode_ = attachPoint->CreateChild("TorchFlame");
    torchFlameNode_->SetPosition(Vector3(0.0f, 0.3f, 0.0f));

    // Light parameters per fire item tier
    auto* light = torchFlameNode_->CreateComponent<Light>();
    light->SetLightType(LIGHT_POINT);

    float emitMin = 4.0f, emitMax = 8.0f;
    Vector2 sizeMin(0.08f, 0.12f), sizeMax(0.12f, 0.2f);

    switch (fireItemId)
    {
    case 871:  // Resin Torch — bright amber
        light->SetColor(Color(1.0f, 0.65f, 0.2f));
        light->SetRange(8.0f);
        light->SetBrightness(0.9f);
        emitMin = 8.0f; emitMax = 14.0f;
        sizeMin = Vector2(0.15f, 0.25f); sizeMax = Vector2(0.25f, 0.4f);
        break;
    case 872:  // Fire Bundle — dim ember glow, no real flame
        light->SetColor(Color(0.9f, 0.4f, 0.1f));
        light->SetRange(3.0f);
        light->SetBrightness(0.25f);
        emitMin = 1.0f; emitMax = 3.0f;
        sizeMin = Vector2(0.04f, 0.06f); sizeMax = Vector2(0.06f, 0.1f);
        break;
    case 873:  // Bark Vessel (fire) — faint warm glow
        light->SetColor(Color(1.0f, 0.5f, 0.15f));
        light->SetRange(2.5f);
        light->SetBrightness(0.2f);
        emitMin = 0.5f; emitMax = 2.0f;
        sizeMin = Vector2(0.03f, 0.05f); sizeMax = Vector2(0.05f, 0.08f);
        break;
    case 8732:  // Bark Vessel (water) — cool blue shimmer
        light->SetColor(Color(0.3f, 0.5f, 0.9f));
        light->SetRange(1.5f);
        light->SetBrightness(0.15f);
        emitMin = 0.3f; emitMax = 1.0f;
        sizeMin = Vector2(0.02f, 0.02f); sizeMax = Vector2(0.04f, 0.04f);
        break;
    default:   // Basic Torch (109) — warm yellow
        light->SetColor(Color(1.0f, 0.75f, 0.3f));
        light->SetRange(5.0f);
        light->SetBrightness(0.5f);
        break;
    }

    // Particle emitter — fire or water mist depending on item
    auto* cache = GetSubsystem<ResourceCache>();
    const char* particlePath = (fireItemId == 8732) ? "Particle/Smoke.xml" : "Particle/Fire.xml";
    auto* effect = cache->GetResource<ParticleEffect>(particlePath);
    if (effect)
    {
        auto* emitter = torchFlameNode_->CreateComponent<ParticleEmitter>();
        SharedPtr<ParticleEffect> localEffect = effect->Clone();
        localEffect->SetMinEmissionRate(emitMin);
        localEffect->SetMaxEmissionRate(emitMax);
        localEffect->SetMinParticleSize(sizeMin);
        localEffect->SetMaxParticleSize(sizeMax);
        emitter->SetEffect(localEffect);
    }
}

void TerrainNode::RemoveTorchFlame(bool showExtinguish)
{
    if (torchFlameNode_)
    {
        if (showExtinguish)
        {
            // Brief steam/sizzle visual at the torch position before removing
            Vector3 pos = torchFlameNode_->GetWorldPosition();
            torchFlameNode_->Remove();
            torchFlameNode_ = nullptr;

            // Create a short-lived smoke puff
            Node* parent = characterNode_ ? characterNode_.Get() : cameraNode_;
            if (parent)
            {
                Node* steamNode = parent->CreateChild("TorchSteam");
                steamNode->SetWorldPosition(pos);
                auto* cache = GetSubsystem<ResourceCache>();
                auto* smokeEffect = cache->GetResource<ParticleEffect>("Particle/Smoke.xml");
                if (smokeEffect)
                {
                    auto* emitter = steamNode->CreateComponent<ParticleEmitter>();
                    SharedPtr<ParticleEffect> localSmoke = smokeEffect->Clone();
                    localSmoke->SetMinEmissionRate(5.0f);
                    localSmoke->SetMaxEmissionRate(10.0f);
                    localSmoke->SetActiveTime(0.5f);  // brief puff
                    emitter->SetEffect(localSmoke);
                    emitter->SetAutoRemoveMode(REMOVE_NODE);
                }
            }

            // Show HUD feedback
            if (hud_)
                hud_->SetContextHint("Torch extinguished!");
        }
        else
        {
            torchFlameNode_->Remove();
            torchFlameNode_ = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// Water Phase 1: Bark Vessel Visual States per NPC
// ---------------------------------------------------------------------------

void TerrainNode::UpdateNPCVesselVisual(Node* npcNode, unsigned char contents)
{
    if (!npcNode)
        return;

    // Remove previous vessel visual if any
    Node* existing = npcNode->GetChild("VesselFX", true);
    if (existing)
        existing->Remove();

    if (contents == 0)
        return;  // empty — no visual

    // Find a suitable attach point (hand bone or NPC root)
    Node* attachPoint = npcNode;
    for (unsigned i = 0; i < npcNode->GetNumChildren(); ++i)
    {
        auto* anim = npcNode->GetChild(i)->GetComponent<AnimatedModel>();
        if (anim)
        {
            Node* hand = npcNode->GetChild(i)->GetChild("RightHand", true);
            if (hand) attachPoint = hand;
            break;
        }
    }

    Node* fxNode = attachPoint->CreateChild("VesselFX");
    fxNode->SetPosition(Vector3(0.0f, 0.2f, 0.0f));

    auto* light = fxNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_POINT);

    auto* cache = GetSubsystem<ResourceCache>();

    if (contents == 1)
    {
        // Fire — warm ember glow (same as bark vessel in torch system)
        light->SetColor(Color(1.0f, 0.5f, 0.15f));
        light->SetRange(2.5f);
        light->SetBrightness(0.2f);

        auto* effect = cache->GetResource<ParticleEffect>("Particle/Fire.xml");
        if (effect)
        {
            auto* emitter = fxNode->CreateComponent<ParticleEmitter>();
            SharedPtr<ParticleEffect> local = effect->Clone();
            local->SetMinEmissionRate(0.5f);
            local->SetMaxEmissionRate(2.0f);
            local->SetMinParticleSize(Vector2(0.03f, 0.05f));
            local->SetMaxParticleSize(Vector2(0.05f, 0.08f));
            emitter->SetEffect(local);
        }
    }
    else if (contents == 2)
    {
        // Water — cool blue shimmer, no particles (just light ripple)
        light->SetColor(Color(0.3f, 0.5f, 0.9f));
        light->SetRange(1.5f);
        light->SetBrightness(0.15f);

        // Subtle smoke-like mist for water surface shimmer
        auto* effect = cache->GetResource<ParticleEffect>("Particle/Smoke.xml");
        if (effect)
        {
            auto* emitter = fxNode->CreateComponent<ParticleEmitter>();
            SharedPtr<ParticleEffect> local = effect->Clone();
            local->SetMinEmissionRate(0.3f);
            local->SetMaxEmissionRate(1.0f);
            local->SetMinParticleSize(Vector2(0.02f, 0.02f));
            local->SetMaxParticleSize(Vector2(0.04f, 0.04f));
            emitter->SetEffect(local);
        }
    }
}

void TerrainNode::HandleEquipSlotClick(StringHash eventType, VariantMap& eventData)
{
    using namespace Released;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn)
        return;

    String slot = btn->GetVar("EquipSlot").GetString();
    if (slot.Empty())
        return;

    // If a bag slot is selected, try to equip the selected item to this slot
    if (selectedSlotIndex_ >= 0)
    {
        // Find the bag item at selectedSlotIndex_
        unsigned bagIdx = 0;
        for (unsigned i = 0; i < inventory_.Size(); ++i)
        {
            if (inventory_[i].slotType != "bag")
                continue;
            if ((int)bagIdx == selectedSlotIndex_)
            {
                if (CanEquipToSlot(inventory_[i].itemCategory, slot))
                {
                    // Move item: bag → equipment slot
                    inventory_[i].slotType = slot;
                    SendEquip(inventory_[i].itemId, slot);
                }
                break;
            }
            ++bagIdx;
        }
        selectedSlotIndex_ = -1;
        RefreshInventoryGrid();
        RefreshEquipmentSlots();
        return;
    }

    // Otherwise, unequip: move equipped item back to bag
    UnequipItem(slot);
}

void TerrainNode::HandleBagSlotDoubleClick(StringHash eventType, VariantMap& eventData)
{
    // Not used — double-click handled via timestamp in HandleBagSlotClick
}

void TerrainNode::HandleBagSlotClick(StringHash eventType, VariantMap& eventData)
{
    using namespace Click;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn)
        return;

    int mouseButton = eventData[P_BUTTON].GetI32();
    int idx = btn->GetVar("BagIndex").GetI32();

    // Right-click → context menu
    if (mouseButton == MOUSEB_RIGHT)
    {
        DismissItemContextMenu();
        IntVector2 pos(eventData[P_X].GetI32(), eventData[P_Y].GetI32());
        ShowItemContextMenu(idx, pos);
        return;
    }

    // Left-click handling
    float now = GetSubsystem<Time>()->GetElapsedTime();
    auto* input = GetSubsystem<Input>();

    // Shift+left-click → split stack
    if (input->GetQualifierDown(QUAL_SHIFT))
    {
        SplitStack(idx);
        return;
    }

    // If storage is open, left-click transfers to storage
    if (storageOpen_)
    {
        TransferToStorage(idx);
        return;
    }

    // Dismiss context menu if open
    DismissItemContextMenu();

    // Double-click detection: same slot clicked twice within threshold
    if (idx == lastBagClickIndex_ && (now - lastBagClickTime_) < DOUBLE_CLICK_TIME)
    {
        // Double-click → auto-equip
        EquipItem(idx);
        selectedSlotIndex_ = -1;
        lastBagClickIndex_ = -1;
        RefreshInventoryGrid();
        RefreshEquipmentSlots();
        return;
    }

    lastBagClickTime_ = now;
    lastBagClickIndex_ = idx;

    // Single click → select/deselect
    if (selectedSlotIndex_ == idx)
        selectedSlotIndex_ = -1;  // deselect
    else
        selectedSlotIndex_ = idx;

    // Visual highlight
    for (unsigned i = 0; i < inventorySlotButtons_.Size(); ++i)
    {
        if ((int)i == selectedSlotIndex_)
            inventorySlotButtons_[i]->SetColor(Color(0.5f, 0.45f, 0.3f, 0.95f));  // selected highlight
        else if (inventorySlotButtons_[i]->IsVisible())
        {
            // Check if slot has content
            bool hasItem = false;
            unsigned bagIdx = 0;
            for (unsigned j = 0; j < inventory_.Size(); ++j)
            {
                if (inventory_[j].slotType != "bag")
                    continue;
                if (bagIdx == i)
                {
                    hasItem = true;
                    break;
                }
                ++bagIdx;
            }
            inventorySlotButtons_[i]->SetColor(hasItem
                ? Color(0.3f, 0.35f, 0.4f, 0.9f)
                : Color(0.2f, 0.2f, 0.25f, 0.8f));
        }
    }
}

void TerrainNode::EquipItem(int bagIndex)
{
    // Find the bag item at the given visual index
    unsigned bagIdx = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if ((int)bagIdx == bagIndex)
        {
            String bestSlot = FindBestEquipSlot(inventory_[i].itemCategory);
            if (bestSlot.Empty())
                return;  // not equippable

            // If something is already in that slot, swap it to bag
            for (unsigned k = 0; k < inventory_.Size(); ++k)
            {
                if (inventory_[k].slotType == bestSlot)
                {
                    inventory_[k].slotType = "bag";
                    break;
                }
            }

            inventory_[i].slotType = bestSlot;
            SendEquip(inventory_[i].itemId, bestSlot);
            return;
        }
        ++bagIdx;
    }
}

void TerrainNode::UnequipItem(const String& slotType)
{
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType == slotType)
        {
            inventory_[i].slotType = "bag";
            SendUnequip(slotType);
            RefreshInventoryGrid();
            RefreshEquipmentSlots();
            return;
        }
    }
}

bool TerrainNode::CanEquipToSlot(const String& itemCategory, const String& slotType) const
{
    // Equipment slot acceptance rules (mirrors equipment_slots table)
    if (slotType == "hand")
        return itemCategory == "weapon" || itemCategory == "tool";
    if (slotType == "offhand")
        return itemCategory == "weapon" || itemCategory == "armor";
    if (slotType == "body")
        return itemCategory == "armor" || itemCategory == "clothing";
    if (slotType == "head")
        return itemCategory == "armor" || itemCategory == "clothing";
    if (slotType == "feet")
        return itemCategory == "clothing";
    if (slotType == "back")
        return itemCategory == "clothing" || itemCategory == "container";
    return false;
}

String TerrainNode::FindBestEquipSlot(const String& itemCategory) const
{
    // Try each equipment slot in priority order
    const char* slotOrder[] = {"hand", "offhand", "body", "head", "feet", "back"};
    for (int i = 0; i < NUM_EQUIP_SLOTS; ++i)
    {
        if (CanEquipToSlot(itemCategory, slotOrder[i]))
        {
            // Prefer empty slots
            bool occupied = false;
            for (unsigned j = 0; j < inventory_.Size(); ++j)
            {
                if (inventory_[j].slotType == slotOrder[i])
                {
                    occupied = true;
                    break;
                }
            }
            if (!occupied)
                return slotOrder[i];
        }
    }
    // All compatible slots occupied — return first compatible (will swap)
    for (int i = 0; i < NUM_EQUIP_SLOTS; ++i)
    {
        if (CanEquipToSlot(itemCategory, slotOrder[i]))
            return slotOrder[i];
    }
    return "";
}

void TerrainNode::SendEquip(int itemId, const String& targetSlot)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Equip item %d -> slot %s (offline — no server)", itemId, targetSlot.CString());
        return;
    }

    VectorBuffer buf;
    buf.WriteI32(itemId);
    buf.WriteString(targetSlot);
    serverConn->SendMessage(MSG_EQUIP, true, true, buf);
    URHO3D_LOGINFOF("Sent equip: item %d -> slot %s", itemId, targetSlot.CString());
}

void TerrainNode::SendUnequip(const String& slot)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Unequip slot %s (offline — no server)", slot.CString());
        return;
    }

    VectorBuffer buf;
    buf.WriteString(slot);
    serverConn->SendMessage(MSG_UNEQUIP, true, true, buf);
    URHO3D_LOGINFOF("Sent unequip: slot %s", slot.CString());
}

// ============================================================================
// Consumables & Context Menu (Inventory Phase 3)
// ============================================================================

bool TerrainNode::IsConsumable(const String& category) const
{
    // Food, medical items, and consumables. Fuel (softwood/hardwood/charcoal) is
    // consumed by burning, not eating — it gets its own deposit/burn path via
    // woodpile + fire pit.
    return category == "food" || category == "medical" || category == "consumable";
}

void TerrainNode::ShowItemContextMenu(int bagIndex, const IntVector2& screenPos)
{
    // Find the inventory item at this bag index
    unsigned bagIdx = 0;
    int invIdx = -1;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if ((int)bagIdx == bagIndex)
        {
            invIdx = i;
            break;
        }
        ++bagIdx;
    }

    if (invIdx < 0)
        return;  // empty slot — no menu

    contextMenuBagIndex_ = bagIndex;

    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    itemContextMenu_ = new Window(context_);
    ui->GetRoot()->AddChild(itemContextMenu_);
    itemContextMenu_->SetStyleAuto(style);
    itemContextMenu_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));
    itemContextMenu_->SetPosition(screenPos);
    itemContextMenu_->SetOpacity(0.92f);
    itemContextMenu_->SetBringToFront(true);
    itemContextMenu_->SetBringToBack(false);

    const auto& item = inventory_[invIdx];

    // Item name header
    auto* header = itemContextMenu_->CreateChild<Text>();
    header->SetFont(font_, 10);
    String headerText = item.itemName.Length() > 0 ? item.itemName : String(item.itemId);
    if (item.durability >= 0)
    {
        char durBuf[32];
        snprintf(durBuf, sizeof(durBuf), " [%d]", item.durability);
        headerText += String(durBuf);
    }
    header->SetText(headerText);
    header->SetColor(Color(0.9f, 0.85f, 0.7f));

    // Separator
    auto* sep = itemContextMenu_->CreateChild<BorderImage>();
    sep->SetColor(Color(0.4f, 0.4f, 0.4f, 0.5f));
    sep->SetMinHeight(1);
    sep->SetMaxHeight(1);

    // Context actions based on item category
    auto addAction = [&](const String& label, int actionId)
    {
        auto* btn = itemContextMenu_->CreateChild<Button>();
        btn->SetStyleAuto(style);
        btn->SetMinHeight(22);
        btn->SetMinWidth(100);
        btn->SetVar("ActionId", actionId);
        btn->SetVar("BagIndex", bagIndex);

        auto* txt = btn->CreateChild<Text>();
        txt->SetFont(font_, 10);
        txt->SetText(label);
        txt->SetColor(Color(0.87f, 0.8f, 0.73f));
        txt->SetHorizontalAlignment(HA_CENTER);

        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleContextMenuAction));
    };

    // Eat/Use (for food items)
    if (IsConsumable(item.itemCategory))
        addAction("Eat", 1);

    // Equip (for equippable items)
    if (!FindBestEquipSlot(item.itemCategory).Empty())
        addAction("Equip", 2);

    // Drop (always available)
    addAction("Drop", 3);

    // Cancel
    addAction("Cancel", 0);
}

void TerrainNode::DismissItemContextMenu()
{
    if (itemContextMenu_)
    {
        itemContextMenu_->Remove();
        itemContextMenu_.Reset();
    }
    contextMenuBagIndex_ = -1;
}

void TerrainNode::HandleContextMenuAction(StringHash eventType, VariantMap& eventData)
{
    using namespace Released;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn)
        return;

    int action = btn->GetVar("ActionId").GetI32();
    int bagIndex = btn->GetVar("BagIndex").GetI32();

    DismissItemContextMenu();

    switch (action)
    {
    case 1:  // Eat/Use
        UseItem(bagIndex);
        break;
    case 2:  // Equip
        EquipItem(bagIndex);
        RefreshInventoryGrid();
        RefreshEquipmentSlots();
        break;
    case 3:  // Drop
        DropItem(bagIndex);
        break;
    default: // Cancel
        break;
    }
}

void TerrainNode::UseItem(int bagIndex)
{
    // Find the inventory item at this bag index
    unsigned bagIdx = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if ((int)bagIdx == bagIndex)
        {
            if (!IsConsumable(inventory_[i].itemCategory))
            {
                URHO3D_LOGWARNING("Item is not consumable");
                return;
            }

            // Send use/eat message to server
            SendUseItem(inventory_[i].itemId);

            // Optimistic local update: decrement quantity
            inventory_[i].quantity--;
            if (inventory_[i].quantity <= 0)
                inventory_.Erase(i);

            RefreshInventoryGrid();

            // Brief feedback via context hint
            if (hud_)
                hud_->SetContextHint("Consumed");

            return;
        }
        ++bagIdx;
    }
}

void TerrainNode::DropItem(int bagIndex)
{
    // Find the inventory item at this bag index
    unsigned bagIdx = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if ((int)bagIdx == bagIndex)
        {
            SendDrop(inventory_[i].itemId, 1);

            // Optimistic local update
            inventory_[i].quantity--;
            if (inventory_[i].quantity <= 0)
                inventory_.Erase(i);

            RefreshInventoryGrid();
            return;
        }
        ++bagIdx;
    }
}

void TerrainNode::SendUseItem(int itemId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Use item %d (offline — no server)", itemId);
        return;
    }

    // Use existing MSG_EAT for food consumption
    VectorBuffer buf;
    buf.WriteI32(itemId);
    serverConn->SendMessage(MSG_EAT, true, true, buf);
    URHO3D_LOGINFOF("Sent MSG_EAT for item %d", itemId);
}

void TerrainNode::SendDrink(const String& sourceType)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Drink from %s (offline — no server)", sourceType.CString());
        return;
    }

    VectorBuffer buf;
    buf.WriteString(sourceType);
    serverConn->SendMessage(MSG_DRINK, true, true, buf);
    URHO3D_LOGINFOF("Sent MSG_DRINK: source=%s", sourceType.CString());
}

bool TerrainNode::TryDrink()
{
    if (!characterNode_)
        return false;

    Vector3 playerPos = characterNode_->GetWorldPosition();

    // Check for Well building nearby (building type 57)
    if (buildingSystem_)
    {
        int nearId = buildingSystem_->FindNearestBuilding(playerPos, 5.0f);
        if (nearId >= 0)
        {
            const auto& buildings = buildingSystem_->GetPlacedBuildings();
            for (unsigned i = 0; i < buildings.Size(); ++i)
            {
                if (buildings[i].placedId == nearId && buildings[i].buildingTypeId == 57)
                {
                    SendDrink("well");
                    if (hud_)
                        hud_->SetContextHint("Drinking from well...");
                    return true;
                }
            }
        }
    }

    // Check if near natural water (river/lake) — player y near water level
    const float waterLevel = 5.5f;
    if (playerPos.y_ < waterLevel + 1.5f)
    {
        SendDrink("river");
        if (hud_)
            hud_->SetContextHint("Drinking from water...");
        return true;
    }

    return false;
}

// ============================================================================
// Crafting UI (Inventory Phase 4)
// ============================================================================

void TerrainNode::InitGameDB()
{
#ifdef URHO3D_DATABASE_SQLITE
    gameDB_ = new GameDB(context_);
    String dbPath = GetSubsystem<ResourceCache>()->GetResourceDirs()[1] + "GameDB/game_rules.db";
    if (!gameDB_->Open(dbPath))
    {
        // Try alternate path
        dbPath = "Data/GameDB/game_rules.db";
        if (!gameDB_->Open(dbPath))
        {
            URHO3D_LOGWARNING("Could not open game_rules.db for crafting lookups");
            gameDB_.Reset();
            return;
        }
    }
    // Load building schema + seed data (idempotent — IF NOT EXISTS / OR IGNORE)
    String dataDir = GetSubsystem<ResourceCache>()->GetResourceDirs()[1];
    gameDB_->ExecuteFile(dataDir + "GameDB/buildings_schema.sql");
    gameDB_->ExecuteFile(dataDir + "GameDB/buildings_seed.sql");

    // Population dynamics schema (idempotent)
    gameDB_->ExecuteFile(dataDir + "GameDB/population_schema.sql");

    // Survival rules schema + seed (idempotent)
    gameDB_->ExecuteFile(dataDir + "GameDB/survival_schema.sql");
    gameDB_->ExecuteFile(dataDir + "GameDB/survival_seed.sql");
    Creature::LoadSurvivalRules(gameDB_);

    // Fire rules and fuel values from DB
    if (gameDB_->GetFireRules(fireRules_))
    {
        cfBaseLightRange_ = fireRules_.lightRadius;
        URHO3D_LOGINFOF("Fire rules loaded: light_radius=%.1f warmth_radius=%.1f warmth=%.1f",
            fireRules_.lightRadius, fireRules_.warmthRadius, fireRules_.warmthValue);
    }
    FuelInfo fuelInfo;
    if (gameDB_->GetFuelInfo(2, fuelInfo))   // Stick
        stickBurnSeconds_ = fuelInfo.fuelValue * 3600.0f / fireRules_.fuelPerHour;
    if (gameDB_->GetFuelInfo(43, fuelInfo))  // Charcoal
        charcoalBurnSeconds_ = fuelInfo.fuelValue * 3600.0f / fireRules_.fuelPerHour;
    URHO3D_LOGINFOF("Fuel: stick=%.0fs charcoal=%.0fs", stickBurnSeconds_, charcoalBurnSeconds_);

    // Cache seed item IDs from crop_types for client-side planting
    Vector<CropTypeInfo> cropTypes = gameDB_->GetAllCropTypes();
    for (unsigned i = 0; i < cropTypes.Size(); ++i)
        seedItemIds_.Insert(cropTypes[i].seedItemId);
    URHO3D_LOGINFOF("Cached %u seed item IDs from crop_types", seedItemIds_.Size());

    // Initialise population manager and subscribe to animal death events
    popManager_ = new PopulationManager(context_);
    popManager_->Initialize(gameDB_);
    SubscribeToEvent(E_CREATUREDIED, URHO3D_HANDLER(TerrainNode, HandleAnimalDied));
    SubscribeToEvent(E_FISHBORN, URHO3D_HANDLER(TerrainNode, HandleFishBorn));

    // Load all tier 0-3 recipes
    recipes_ = gameDB_->GetRecipesForTier(3);
    URHO3D_LOGINFOF("GameDB loaded: %d recipes", recipes_.Size());

    // Build creature spawn table from DB habitats
    BuildSpawnTable();

    // Cache gather sources by item ID for resource streaming — load per terrain type
    {
        unsigned totalLoaded = 0;
        const char* terrainTypes[] = {"grassland", "forest", "mountain", "water", "riverbank", "any"};
        for (const char* terrain : terrainTypes)
        {
            Vector<GatherSourceInfo> sources = gameDB_->GetGatherSourcesByTerrain(terrain);
            for (const GatherSourceInfo& gs : sources)
            {
                if (!gatherSourceByItem_.Contains(gs.itemId))
                    gatherSourceByItem_[gs.itemId] = gs;
            }
            totalLoaded += sources.Size();
        }
        URHO3D_LOGINFOF("GameDB: cached %u gather sources (%u unique items) via terrain-filtered queries",
            totalLoaded, gatherSourceByItem_.Size());
    }

    // Cache water sources
    waterSources_ = gameDB_->GetAllWaterSources();
    URHO3D_LOGINFOF("GameDB: cached %u water sources", waterSources_.Size());
#endif
}

void TerrainNode::BuildSpawnTable()
{
#ifdef URHO3D_DATABASE_SQLITE
    gSpawnTable.Clear();
    if (!gameDB_)
        return;

    // Query each land habitat — GetCreaturesByHabitat also returns habitat='any' creatures
    const char* habitats[] = {"grassland", "forest", "mountain", "inland"};
    HashSet<int> seen;

    for (const char* habitat : habitats)
    {
        Vector<CreatureInfo> creatures = gameDB_->GetCreaturesByHabitat(habitat);
        for (const CreatureInfo& ci : creatures)
        {
            if (seen.Contains(ci.id))
                continue;
            seen.Insert(ci.id);

            SpawnEntry entry;
            entry.name = ci.name;
            entry.modelPath = ci.model;
            // Material list convention: .mdl → .txt
            entry.matList = ci.model;
            entry.matList.Replace(".mdl", ".txt");
            entry.creatureId = ci.id;
            entry.defaultCount = Max(ci.packSize, 2);
            gSpawnTable.Push(entry);
        }
    }

    URHO3D_LOGINFOF("BuildSpawnTable: %u species from GameDB", gSpawnTable.Size());
#endif
}

void TerrainNode::SpawnPhenomenonEffect(const Vector3& pos, const String& visualHint)
{
    if (!scene_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    // Map visual hint names to particle effect resources
    String effectPath;
    if (visualHint == "ember_glow")
        effectPath = "Particle/Fire.xml";
    else if (visualHint == "sprout_particles")
        effectPath = "Particle/Burst.xml";
    else if (visualHint == "steam_wisps")
        effectPath = "Particle/Smoke.xml";
    else if (visualHint == "flash_burn")
        effectPath = "Particle/Burst.xml";
    else
        effectPath = "Particle/Burst.xml";  // fallback

    auto* effect = cache->GetResource<ParticleEffect>(effectPath);
    if (!effect)
        return;

    Node* effectNode = scene_->CreateTemporaryChild("PhenomenonFX", LOCAL);
    effectNode->SetPosition(pos + Vector3(0.0f, 1.0f, 0.0f));

    auto* emitter = effectNode->CreateComponent<ParticleEmitter>();
    emitter->SetEffect(effect);
    emitter->SetEmitting(true);
    emitter->SetAutoRemoveMode(REMOVE_NODE);

    URHO3D_LOGINFOF("[Phenomena] Visual hint '%s' at (%.1f, %.1f, %.1f)",
        visualHint.CString(), pos.x_, pos.y_, pos.z_);
}

void TerrainNode::HandleAnimalDied(StringHash /*eventType*/, VariantMap& eventData)
{
    // The client used to call popManager_->RecordKill here on the local
    // PopulationManager. That was wrong: in multiplayer N clients each
    // catch E_CREATUREDIED locally and would each increment their own
    // local birth accumulator, causing per-client population divergence
    // and (in Death System Phase 1) N replacement spawns per real death.
    //
    // RecordKill now lives server-side in AuthServer::HandleHarvest,
    // called once per successful harvest (see AuthServer.cpp around the
    // "Population accounting" comment). The client PopulationManager is
    // for read-only display — it must NOT mutate population state.
    //
    // What DOES belong here: client-side reactions that don't mutate
    // authoritative state. Death System Phase 3 lands the first one —
    // a scent marker registered for scavenger AI to investigate.
    using namespace CreatureDied;
    int     creatureId = eventData[P_CREATUREID].GetI32();
    Vector3 pos        = eventData[P_POSITION].GetVector3();
    ScentRegistry::Register(pos, creatureId);

    // Auto-unpossess if the possessed NPC just died
    if (possessedNPC_)
    {
        auto* sender = dynamic_cast<Component*>(GetEventSender());
        if (sender && sender->GetNode() == possessedNPC_.Get())
            UnpossessNPC();
    }
}

void TerrainNode::HandleFishBorn(StringHash /*eventType*/, VariantMap& eventData)
{
    using namespace FishBorn;
    Node* fishNode = static_cast<Node*>(eventData[P_NODE].GetPtr());
    if (fishNode)
        fishNodes_.Push(WeakPtr<Node>(fishNode));
}

LandAnimal* TerrainNode::SpawnCreatureAt(int creatureId, const Vector3& pos)
{
    if (!scene_)
        return nullptr;

    const SpawnEntry* entry = FindSpawnEntryByCreatureId(creatureId);
    if (!entry)
    {
        URHO3D_LOGWARNINGF("SpawnCreatureAt: unknown creatureId %d", creatureId);
        return nullptr;
    }

    auto* cache = GetSubsystem<ResourceCache>();

    Node* node = scene_->CreateTemporaryChild(entry->name, LOCAL);
    node->SetPosition(pos);

    // Model on child node with 180° Y flip — meshes face -Z, Urho3D forward is +Z
    Node* modelNode = node->CreateChild(entry->name + "Model");
    modelNode->SetRotation(Quaternion(180.0f, Vector3::UP));

    auto* model = modelNode->CreateComponent<AnimatedModel>();
    model->SetModel(cache->GetResource<Model>(entry->modelPath), true, true);
    model->ApplyMaterialList(entry->matList);
    model->SetCastShadows(false);  // Animals skip shadow pass — saves ~40 draws per cascade

    modelNode->CreateComponent<AnimationController>();

    // Species-specific component. Kept as an explicit if-chain (rather than
    // type-name reflection) so the linker requires every referenced class —
    // a missing #include shows up as a build error rather than a runtime
    // "unknown component type" log spam.
    LandAnimal* animal = nullptr;
    if      (entry->name == "Rabbit")    animal = node->CreateComponent<Rabbit>();
    else if (entry->name == "Deer")      animal = node->CreateComponent<Deer>();
    else if (entry->name == "Fox")       animal = node->CreateComponent<Fox>();
    else if (entry->name == "Wolf")      animal = node->CreateComponent<Wolf>();
    else if (entry->name == "Stag")      animal = node->CreateComponent<Stag>();
    else if (entry->name == "Bull")      animal = node->CreateComponent<Bull>();
    else if (entry->name == "Cow")       animal = node->CreateComponent<Cow>();
    else if (entry->name == "Horse")     animal = node->CreateComponent<Horse>();
    else if (entry->name == "Donkey")    animal = node->CreateComponent<Donkey>();
    else if (entry->name == "Alpaca")    animal = node->CreateComponent<Alpaca>();
    else if (entry->name == "Husky")     animal = node->CreateComponent<Husky>();
    else if (entry->name == "ShibaInu")  animal = node->CreateComponent<ShibaInu>();
    else if (entry->name == "CaveMan")   animal = node->CreateComponent<CaveMan>();
    else if (entry->name == "CaveWoman") animal = node->CreateComponent<CaveWoman>();

    // Phase 5: Cavemen always spawn near the campfire, not at random terrain positions
    if (animal && (entry->creatureId == 20 || entry->creatureId == 21))
    {
        Node* campfire = scene_ ? scene_->GetChild("Campfire", true) : nullptr;
        if (campfire && terrain_)
        {
            Vector3 firePos = campfire->GetWorldPosition();
            float angle = Random(360.0f);
            float dist = 3.5f + Random(0.0f, 2.0f);
            Vector3 campPos = firePos + Vector3(Cos(angle) * dist, 0.0f, Sin(angle) * dist);
            campPos.y_ = terrain_->GetHeight(campPos);
            node->SetPosition(campPos);
            animal->SetHomePosition(campPos);

            auto* npc = dynamic_cast<HumanNPC*>(animal);
            if (npc)
            {
                npc->SetCampfireNode(campfire);
                if (cameraNode_)
                    npc->SetCameraNode(cameraNode_);
                if (resourceMap_)
                    npc->SetResourceMap(resourceMap_);
            }
        }
    }

    if (animal)
    {
        if (buildingSystem_)
            animal->SetBuildingSystem(buildingSystem_);
        if (gameDB_)
            animal->SetGameDB(gameDB_.Get());
        animal->SetSpatialHash(&landAnimalHash_);
        if (ecosystem_)
            animal->SetEcosystem(ecosystem_);
        if (gameDB_)
        {
            CreatureInfo ci;
            if (gameDB_->GetCreature(entry->creatureId, ci))
            {
                float fleeFrac = (ci.aggression == "aggressive") ? 0.25f : 0.75f;
                animal->InitCombatStats(ci.hp, ci.attack, ci.defense, ci.damage, ci.damageVar,
                    ci.speed, fleeFrac, ci.aggression);
                animal->InitBehaviorStats(ci.fleeSpeed, ci.fleeDistance, ci.visionRange,
                    ci.visionAngle, ci.isPredator != 0, ci.isScavenger != 0, ci.foodGrassWt);
            }
        }

        // Roll ability scores (4d6-drop-lowest) — CON modifier adjusts maxHp inside RollStats
        animal->RollStats(1);

        const auto& s = animal->GetStats();
        URHO3D_LOGINFOF("[Stats] %s (spawnId %u): STR %d DEX %d CON %d INT %d WIS %d CHA %d",
            entry->name.CString(), animal->GetSpawnId(),
            s.strength, s.dexterity, s.constitution,
            s.intelligence, s.wisdom, s.charisma);
    }
    else
    {
        URHO3D_LOGWARNINGF("SpawnCreatureAt: no component constructor for species '%s'", entry->name.CString());
    }

    animalNodes_.Push(WeakPtr<Node>(node));
    return animal;
}

void TerrainNode::HandleSpawnCreatureMsg(StringHash /*eventType*/, VariantMap& eventData)
{
    // Server→client replacement spawn — Death System Phase 1.
    // Wire format from AuthServer::BroadcastSpawnCreature:
    //   region_id i32, creature_id i32, x f32, y f32, z f32, spawn_id u32, npcName string
    using namespace NetworkMessage;
    const auto& data = eventData[P_DATA].GetBuffer();
    MemoryBuffer msg(data);

    int  regionId   = msg.ReadI32();
    int  creatureId = msg.ReadI32();
    float x = msg.ReadFloat();
    /*float y = */ msg.ReadFloat();   // ignored — server sends 0, we snap below
    float z = msg.ReadFloat();
    unsigned spawnId = msg.IsEof() ? 0 : msg.ReadU32();  // server-assigned AI tracking ID
    String npcName = msg.IsEof() ? String::EMPTY : msg.ReadString();
    // Phase 25: selective breeding traits (default 1.0 for fresh spawns)
    float traitSize  = msg.IsEof() ? 1.0f : msg.ReadFloat();
    float traitSpeed = msg.IsEof() ? 1.0f : msg.ReadFloat();
    float traitYield = msg.IsEof() ? 1.0f : msg.ReadFloat();
    (void)regionId;  // logged only; spawn position came from server

    if (!scene_ || !terrain_)
        return;

    // Snap Y to terrain. Then validate the snap landed on dry land — the
    // server has no terrain access, so its random region pick can land in
    // a lake. If invalid, walk outward in 1m steps along a random axis up
    // to 8 attempts; drop the spawn if all attempts are wet.
    const float waterLevel = 5.5f;
    float bestY = terrain_->GetHeight(Vector3(x, 0.0f, z));
    if (bestY <= waterLevel)
    {
        bool found = false;
        for (int attempt = 1; attempt <= 8 && !found; ++attempt)
        {
            float angle = Random(360.0f);
            float r     = (float)attempt * 2.0f;  // 2,4,...,16m outward sweep
            float tx = x + r * Cos(angle);
            float tz = z + r * Sin(angle);
            float ty = terrain_->GetHeight(Vector3(tx, 0.0f, tz));
            if (ty > waterLevel)
            {
                x = tx;
                z = tz;
                bestY = ty;
                found = true;
            }
        }
        if (!found)
        {
            URHO3D_LOGWARNINGF("MSG_SPAWN_CREATURE: no dry land near (%.1f, %.1f) for creatureId %d — dropping",
                x, z, creatureId);
            return;
        }
    }

    Vector3 pos(x, bestY, z);
    LandAnimal* animal = SpawnCreatureAt(creatureId, pos);
    if (animal)
    {
        // Store server-assigned AI tracking ID for MSG_CREATURE_AI_STATE correlation
        if (spawnId > 0)
        {
            animal->SetSpawnId(spawnId);
            spawnIdToNode_[spawnId] = animal->GetNode();
        }

        // Phase 25: Apply selective breeding traits (scale + speed)
        if (traitSize != 1.0f || traitSpeed != 1.0f || traitYield != 1.0f)
            animal->SetTraits(traitSize, traitSpeed, traitYield);

        // Store NPC name from server (humans get syllable names, animals get empty)
        if (!npcName.Empty())
        {
            animal->GetNode()->SetVar("NPCName", npcName);

            // Phase 30: floating name label above NPC head
            auto* cache = GetSubsystem<ResourceCache>();
            auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
            if (font)
            {
                // Position label above head — use model's desired size as height estimate
                float headHeight = (creatureId == 20) ? 1.9f : 1.75f;
                Node* labelNode = animal->GetNode()->CreateChild("NameLabel");
                labelNode->SetPosition(Vector3(0.0f, headHeight, 0.0f));

                auto* text3D = labelNode->CreateComponent<Text3D>();
                text3D->SetFont(font, 14);
                text3D->SetText(npcName);
                text3D->SetColor(Color(1.0f, 0.95f, 0.8f));  // warm parchment
                text3D->SetAlignment(HA_CENTER, VA_BOTTOM);
                text3D->SetFaceCameraMode(FC_ROTATE_Y);
                text3D->SetTextEffect(TE_SHADOW);
                text3D->SetEffectColor(Color(0.0f, 0.0f, 0.0f, 0.6f));
            }
        }

        URHO3D_LOGWARNINGF("MSG_SPAWN_CREATURE: spawned %s creatureId %d (spawnId %u) at (%.1f, %.1f, %.1f)",
            npcName.Empty() ? "animal" : npcName.CString(),
            creatureId, spawnId, pos.x_, pos.y_, pos.z_);
    }
}

void TerrainNode::HandleCreatureAIState(MemoryBuffer& msg)
{
    // Wire format: spawnId u32, state u8, position Vec3, targetId u32, moveSpeed f32,
    //              hp f32, hunger f32, thirst f32, warmth f32, stamina f32,
    //              vesselContents u8, growthProgress f32
    unsigned spawnId = msg.ReadU32();
    unsigned char state = static_cast<unsigned char>(msg.ReadByte());
    float px = msg.ReadFloat();
    float py = msg.ReadFloat();
    float pz = msg.ReadFloat();
    unsigned targetId = msg.ReadU32();
    float moveSpeed = msg.ReadFloat();
    float aiHp = msg.ReadFloat();
    float aiHunger = msg.ReadFloat();
    float aiThirst = msg.ReadFloat();
    float aiWarmth = msg.ReadFloat();
    float aiStamina = msg.ReadFloat();
    (void)targetId;   // Phase 3: used for hunt/defend target

    auto it = spawnIdToNode_.Find(spawnId);
    if (it == spawnIdToNode_.End() || it->second_.Expired())
    {
        // Log first few misses with actual map contents for diagnosis
        static int totalMiss = 0;
        if (++totalMiss == 10)
        {
            String keys;
            for (auto m = spawnIdToNode_.Begin(); m != spawnIdToNode_.End(); ++m)
                keys += String(m->first_) + " ";
            URHO3D_LOGWARNINGF("[AIState] spawnId %u miss — map has %u entries: [%s]",
                spawnId, spawnIdToNode_.Size(), keys.CString());
        }
        return;
    }

    Node* node = it->second_;
    auto* creature = node->GetDerivedComponent<Creature>(true);
    if (!creature)
        return;

    // Snap server Y to local terrain height (server has approximate height only)
    float localY = terrain_ ? terrain_->GetHeight(Vector3(px, 0.0f, pz)) : py;

    // Apply server-authoritative state — creature lerps toward server position
    // and plays the server-chosen animation. Local AI decisions are suppressed.
    auto serverState = static_cast<CreatureState>(state);
    creature->ApplyServerState(serverState, Vector3(px, localY, pz), moveSpeed);
    creature->SetServerVitals(aiHp, aiHunger, aiThirst, aiWarmth, aiStamina);

    // Bark vessel contents — trailing u8 (0=empty, 1=fire, 2=water)
    unsigned char vesselContents = 0;
    if (msg.GetSize() > msg.GetPosition())
        vesselContents = msg.ReadU8();
    unsigned char prevVessel = creature->GetVesselContents();
    creature->SetVesselContents(vesselContents);
    if (vesselContents != prevVessel)
        UpdateNPCVesselVisual(node, vesselContents);

    // Child growth scale — children render at 0.4 to 1.0 based on growthProgress
    if (msg.GetSize() >= msg.GetPosition() + sizeof(float))
    {
        float growthProgress = msg.ReadFloat();
        if (growthProgress < 1.0f)
            node->SetScale(0.4f + 0.6f * growthProgress);
    }

    // Server task (STASK) — drives prop attachment on the client
    if (msg.GetSize() >= msg.GetPosition() + sizeof(int))
    {
        int serverTask = msg.ReadI32();
        creature->SetServerTask(serverTask);
    }

    // Mount state — parent NPC model to horse when mounted
    if (msg.GetSize() >= msg.GetPosition() + sizeof(unsigned))
    {
        unsigned mountedHorse = msg.ReadU32();
        Node* modelNode = node->GetChild(node->GetName() + "Model");

        if (mountedHorse != 0 && modelNode)
        {
            auto horseIt = spawnIdToNode_.Find(mountedHorse);
            if (horseIt != spawnIdToNode_.End() && !horseIt->second_.Expired())
            {
                Node* horseNode = horseIt->second_;
                Node* horseModel = horseNode->GetChild(horseNode->GetName() + "Model");
                if (horseModel)
                {
                    modelNode->SetParent(horseModel);
                    modelNode->SetPosition(Vector3(0.0f, 1.2f, -0.2f));
                    modelNode->SetRotation(Quaternion::IDENTITY);
                }
            }
        }
        else if (mountedHorse == 0 && modelNode && modelNode->GetParent() != node)
        {
            modelNode->SetParent(node);
            modelNode->SetPosition(Vector3::ZERO);
        }
    }

    // Phase 19: record footstep on client ecosystem for visual path wear
    if (ecosystem_ && moveSpeed > 0.5f && serverState != CREATURE_IDLE &&
        serverState != CREATURE_SLEEP && serverState != CREATURE_SIT &&
        serverState != CREATURE_CORPSE && serverState != CREATURE_DIE)
        ecosystem_->RecordFootstep(px, pz);
}

void TerrainNode::HandleSpawnTree(MemoryBuffer& msg)
{
    // Wire format: species u8, pos_x f32, pos_z f32, scale f32, treeId u32, growth_stage u8
    int species = Clamp(static_cast<int>(static_cast<unsigned char>(msg.ReadByte())), 0, NUM_TREE_SPECIES - 1);
    float px = msg.ReadFloat();
    float pz = msg.ReadFloat();
    float scale = msg.ReadFloat();
    unsigned treeId = msg.ReadU32();
    int growthStage = msg.GetSize() > msg.GetPosition() ? (int)msg.ReadU8() : 3;

    float stageScale = (growthStage == 2) ? 0.5f : 1.0f;
    float py = terrain_ ? terrain_->GetHeight(Vector3(px, 0.0f, pz)) : 0.0f;

    InitTreeModels();

    if (!treeModel_[species])
        return;

    Node* treeNode = scene_->CreateChild("Tree", LOCAL);
    treeNode->SetWorldPosition(Vector3(px, py, pz));
    treeNode->SetRotation(Quaternion(0.0f, Random(360.0f), 0.0f));
    treeNode->SetScale(scale * stageScale);
    treeNode->SetVar("TreeId", treeId);
    treeNode->SetVar("TreeSpecies", species);

    auto* sm = treeNode->CreateComponent<StaticModel>();
    sm->SetModel(treeModel_[species], true);
    if (treeBarkMat_[species]) sm->SetMaterial(0, treeBarkMat_[species]);
    if (treeLeafMat_[species]) sm->SetMaterial(1, treeLeafMat_[species]);
    sm->SetCastShadows(true);
    sm->SetViewMask(0x01);

    treeIdToNode_[treeId] = treeNode;
}

void TerrainNode::CreateCraftingUI()
{
    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    craftingWindow_ = new Window(context_);
    ui->GetRoot()->AddChild(craftingWindow_);
    craftingWindow_->SetStyleAuto(style);
    craftingWindow_->SetSize(420, 380);
    craftingWindow_->SetHorizontalAlignment(HA_CENTER);
    craftingWindow_->SetVerticalAlignment(VA_CENTER);
    craftingWindow_->SetMovable(true);
    craftingWindow_->SetOpacity(0.92f);
    craftingWindow_->SetVisible(false);
    craftingWindow_->SetLayout(LM_VERTICAL, 4, IntRect(6, 6, 6, 6));

    // Title
    auto* title = craftingWindow_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Crafting");
    title->SetColor(Color(0.9f, 0.85f, 0.7f));
    title->SetHorizontalAlignment(HA_CENTER);

    // Main content: recipe list (left) + detail (right)
    auto* content = craftingWindow_->CreateChild<UIElement>();
    content->SetLayout(LM_HORIZONTAL, 6);
    content->SetMinHeight(280);

    // --- Left: Recipe list ---
    auto* leftPanel = content->CreateChild<UIElement>();
    leftPanel->SetLayout(LM_VERTICAL, 2);
    leftPanel->SetMinWidth(160);

    auto* listLabel = leftPanel->CreateChild<Text>();
    listLabel->SetFont(font_, 10);
    listLabel->SetText("Recipes");
    listLabel->SetColor(Color(0.7f, 0.65f, 0.55f));

    recipeList_ = leftPanel->CreateChild<ListView>();
    recipeList_->SetStyleAuto(style);
    recipeList_->SetMinHeight(250);
    recipeList_->SetHighlightMode(HM_ALWAYS);
    SubscribeToEvent(recipeList_, E_ITEMSELECTED, URHO3D_HANDLER(TerrainNode, HandleRecipeSelect));

    // --- Right: Recipe detail ---
    auto* rightPanel = content->CreateChild<UIElement>();
    rightPanel->SetLayout(LM_VERTICAL, 4);
    rightPanel->SetMinWidth(230);

    recipeTitle_ = rightPanel->CreateChild<Text>();
    recipeTitle_->SetFont(font_, 12);
    recipeTitle_->SetColor(Color(0.9f, 0.85f, 0.7f));
    recipeTitle_->SetText("Select a recipe");

    recipeDesc_ = rightPanel->CreateChild<Text>();
    recipeDesc_->SetFont(font_, 9);
    recipeDesc_->SetColor(Color(0.6f, 0.6f, 0.6f));
    recipeDesc_->SetWordwrap(true);
    recipeDesc_->SetMaxWidth(220);

    // Separator
    auto* sep = rightPanel->CreateChild<BorderImage>();
    sep->SetColor(Color(0.4f, 0.4f, 0.4f, 0.3f));
    sep->SetMinHeight(1);
    sep->SetMaxHeight(1);

    // Inputs label
    auto* inputsLabel = rightPanel->CreateChild<Text>();
    inputsLabel->SetFont(font_, 10);
    inputsLabel->SetText("Materials:");
    inputsLabel->SetColor(Color(0.7f, 0.65f, 0.55f));

    recipeInputs_ = rightPanel->CreateChild<UIElement>();
    recipeInputs_->SetLayout(LM_VERTICAL, 2);
    recipeInputs_->SetMinHeight(80);

    // Output
    recipeOutput_ = rightPanel->CreateChild<Text>();
    recipeOutput_->SetFont(font_, 10);
    recipeOutput_->SetColor(Color(0.7f, 0.9f, 0.7f));

    // Craft button
    craftBtn_ = rightPanel->CreateChild<Button>();
    craftBtn_->SetStyleAuto(style);
    craftBtn_->SetMinHeight(28);
    craftBtnText_ = craftBtn_->CreateChild<Text>();
    craftBtnText_->SetFont(font_, 11);
    craftBtnText_->SetText("Craft");
    craftBtnText_->SetHorizontalAlignment(HA_CENTER);
    craftBtnText_->SetColor(Color(0.87f, 0.8f, 0.73f));
    SubscribeToEvent(craftBtn_, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleCraftButton));

    // Progress bar
    auto* progressBg = rightPanel->CreateChild<BorderImage>();
    progressBg->SetColor(Color(0.15f, 0.15f, 0.15f, 0.6f));
    progressBg->SetMinHeight(8);
    progressBg->SetMaxHeight(8);
    craftProgressBar_ = progressBg;

    craftProgressFill_ = progressBg->CreateChild<BorderImage>();
    craftProgressFill_->SetColor(Color(0.3f, 0.7f, 0.3f));
    craftProgressFill_->SetMinHeight(8);
    craftProgressFill_->SetMaxHeight(8);
    craftProgressFill_->SetWidth(0);

    // Close button
    auto* closeBtn = craftingWindow_->CreateChild<Button>();
    closeBtn->SetStyleAuto(style);
    closeBtn->SetMinHeight(24);
    auto* closeTxt = closeBtn->CreateChild<Text>();
    closeTxt->SetFont(font_, 11);
    closeTxt->SetText("Close");
    closeTxt->SetHorizontalAlignment(HA_CENTER);
    SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&) { ToggleCrafting(); });

    // Populate recipe list
    RefreshRecipeList();
}

void TerrainNode::RefreshRecipeList()
{
    if (!recipeList_)
        return;

    recipeList_->RemoveAllItems();

    HashMap<int, int> inv = BuildInventoryMap();

    for (unsigned i = 0; i < recipes_.Size(); ++i)
    {
        auto* item = new Text(context_);
        item->SetFont(font_, 10);
        item->SetMinWidth(150);
        item->SetMinHeight(18);

        // Check if we can craft this recipe
        bool canCraft = true;
        for (unsigned j = 0; j < recipes_[i].inputs.Size(); ++j)
        {
            auto it = inv.Find(recipes_[i].inputs[j].itemId);
            int have = (it != inv.End()) ? it->second_ : 0;
            if (have < recipes_[i].inputs[j].quantity)
            {
                canCraft = false;
                break;
            }
        }

        // Also check tool requirement
        if (recipes_[i].toolReq > 0)
        {
            auto it = inv.Find(recipes_[i].toolReq);
            if (it == inv.End() || it->second_ < 1)
                canCraft = false;
        }

        item->SetText(recipes_[i].name);
        item->SetColor(canCraft
            ? Color(0.87f, 0.8f, 0.73f)      // available — warm white
            : Color(0.45f, 0.4f, 0.35f));     // unavailable — dimmed

        recipeList_->AddItem(item);
    }
}

void TerrainNode::HandleRecipeSelect(StringHash eventType, VariantMap& eventData)
{
    using namespace ItemSelected;
    int sel = eventData[P_SELECTION].GetI32();
    SelectRecipe(sel);
}

void TerrainNode::SelectRecipe(int index)
{
    if (index < 0 || index >= (int)recipes_.Size())
    {
        selectedRecipeIndex_ = -1;
        if (recipeTitle_) recipeTitle_->SetText("Select a recipe");
        if (recipeDesc_) recipeDesc_->SetText("");
        if (recipeOutput_) recipeOutput_->SetText("");
        if (recipeInputs_) recipeInputs_->RemoveAllChildren();
        return;
    }

    selectedRecipeIndex_ = index;
    RefreshRecipeDetail();
}

void TerrainNode::RefreshRecipeDetail()
{
    if (selectedRecipeIndex_ < 0 || selectedRecipeIndex_ >= (int)recipes_.Size())
        return;

    const auto& recipe = recipes_[selectedRecipeIndex_];
    HashMap<int, int> inv = BuildInventoryMap();

    // Title
    if (recipeTitle_)
        recipeTitle_->SetText(recipe.name);

    // Description — look up from DB or use recipe name
    if (recipeDesc_)
    {
        String desc = recipe.description;
        if (recipe.toolReq > 0)
        {
#ifdef URHO3D_DATABASE_SQLITE
            ItemInfo toolInfo;
            if (gameDB_ && gameDB_->GetItem(recipe.toolReq, toolInfo))
                desc += (desc.Length() > 0 ? "\n" : "") + String("Requires: ") + toolInfo.name;
            else
#endif
                desc += (desc.Length() > 0 ? "\n" : "") + String("Requires tool ID ") + String(recipe.toolReq);
        }
        if (recipe.stationReq > 0)
        {
#ifdef URHO3D_DATABASE_SQLITE
            ItemInfo stationInfo;
            if (gameDB_ && gameDB_->GetItem(recipe.stationReq, stationInfo))
                desc += (desc.Length() > 0 ? "\n" : "") + String("Station: ") + stationInfo.name;
            else
#endif
                desc += (desc.Length() > 0 ? "\n" : "") + String("Station ID ") + String(recipe.stationReq);
        }
        recipeDesc_->SetText(desc);
    }

    // Inputs
    if (recipeInputs_)
    {
        recipeInputs_->RemoveAllChildren();
        for (unsigned i = 0; i < recipe.inputs.Size(); ++i)
        {
            auto* row = recipeInputs_->CreateChild<Text>();
            row->SetFont(font_, 9);

            int need = recipe.inputs[i].quantity;
            auto it = inv.Find(recipe.inputs[i].itemId);
            int have = (it != inv.End()) ? it->second_ : 0;

            // Resolve item name
            String itemName = String(recipe.inputs[i].itemId);
#ifdef URHO3D_DATABASE_SQLITE
            ItemInfo itemInfo;
            if (gameDB_ && gameDB_->GetItem(recipe.inputs[i].itemId, itemInfo))
                itemName = itemInfo.name;
#endif

            char buf[64];
            snprintf(buf, sizeof(buf), "  %s: %d/%d", itemName.CString(), have, need);
            row->SetText(String(buf));
            row->SetColor(have >= need
                ? Color(0.5f, 0.8f, 0.5f)   // have enough — green
                : Color(0.9f, 0.3f, 0.3f));  // not enough — red
        }
    }

    // Output
    if (recipeOutput_)
    {
        String outputName = String(recipe.outputId);
#ifdef URHO3D_DATABASE_SQLITE
        ItemInfo outputInfo;
        if (gameDB_ && gameDB_->GetItem(recipe.outputId, outputInfo))
            outputName = outputInfo.name;
#endif
        char buf[64];
        snprintf(buf, sizeof(buf), "Makes: %s x%d", outputName.CString(), recipe.outputQty);
        recipeOutput_->SetText(String(buf));
    }

    // Craft button state
    if (craftBtn_ && craftBtnText_)
    {
        bool canCraft = true;
        for (unsigned j = 0; j < recipe.inputs.Size(); ++j)
        {
            auto it = inv.Find(recipe.inputs[j].itemId);
            int have = (it != inv.End()) ? it->second_ : 0;
            if (have < recipe.inputs[j].quantity)
            {
                canCraft = false;
                break;
            }
        }
        if (recipe.toolReq > 0)
        {
            auto it = inv.Find(recipe.toolReq);
            if (it == inv.End() || it->second_ < 1)
                canCraft = false;
        }

        craftBtnText_->SetText(canCraft ? "Craft" : "Missing materials");
        craftBtnText_->SetColor(canCraft
            ? Color(0.87f, 0.8f, 0.73f)
            : Color(0.5f, 0.4f, 0.35f));
    }
}

void TerrainNode::HandleCraftButton(StringHash eventType, VariantMap& eventData)
{
    if (selectedRecipeIndex_ < 0 || selectedRecipeIndex_ >= (int)recipes_.Size())
        return;
    if (craftingRecipeId_ >= 0)
        return;  // already crafting

    const auto& recipe = recipes_[selectedRecipeIndex_];
    HashMap<int, int> inv = BuildInventoryMap();

    // Validate locally
    for (unsigned j = 0; j < recipe.inputs.Size(); ++j)
    {
        auto it = inv.Find(recipe.inputs[j].itemId);
        int have = (it != inv.End()) ? it->second_ : 0;
        if (have < recipe.inputs[j].quantity)
            return;  // can't craft
    }
    if (recipe.toolReq > 0)
    {
        auto it = inv.Find(recipe.toolReq);
        if (it == inv.End() || it->second_ < 1)
            return;
    }

    // Start craft timer
    craftingRecipeId_ = recipe.id;
    craftDuration_ = recipe.craftTime > 0.0f ? recipe.craftTime : 2.0f;
    craftTimer_ = craftDuration_;

    if (craftBtnText_)
    {
        craftBtnText_->SetText("Crafting...");
        craftBtnText_->SetColor(Color(0.9f, 0.8f, 0.3f));
    }
}

void TerrainNode::UpdateCraftTimer(float timeStep)
{
    if (craftingRecipeId_ < 0)
        return;

    craftTimer_ -= timeStep;

    // Update progress bar
    if (craftProgressFill_ && craftProgressBar_ && craftDuration_ > 0.0f)
    {
        float frac = 1.0f - Clamp(craftTimer_ / craftDuration_, 0.0f, 1.0f);
        int parentW = craftProgressBar_->GetWidth();
        if (parentW < 10) parentW = 200;
        craftProgressFill_->SetWidth((int)(parentW * frac));
    }

    if (craftTimer_ <= 0.0f)
    {
        // Craft complete — send to server
        SendCraft(craftingRecipeId_);

        // Optimistic local: deduct inputs, add output
        int recipeIdx = -1;
        for (unsigned i = 0; i < recipes_.Size(); ++i)
        {
            if (recipes_[i].id == craftingRecipeId_)
            {
                recipeIdx = i;
                break;
            }
        }

        if (recipeIdx >= 0)
        {
            const auto& recipe = recipes_[recipeIdx];
            // Deduct inputs
            for (unsigned j = 0; j < recipe.inputs.Size(); ++j)
            {
                if (!recipe.inputs[j].consumed)
                    continue;
                int toRemove = recipe.inputs[j].quantity;
                for (unsigned k = 0; k < inventory_.Size() && toRemove > 0; ++k)
                {
                    if (inventory_[k].itemId == recipe.inputs[j].itemId && inventory_[k].slotType == "bag")
                    {
                        int remove = Min(toRemove, inventory_[k].quantity);
                        inventory_[k].quantity -= remove;
                        toRemove -= remove;
                        if (inventory_[k].quantity <= 0)
                        {
                            inventory_.Erase(k);
                            --k;
                        }
                    }
                }
            }
            // Add output
            bool stacked = false;
            for (unsigned k = 0; k < inventory_.Size(); ++k)
            {
                if (inventory_[k].itemId == recipe.outputId && inventory_[k].slotType == "bag")
                {
                    inventory_[k].quantity += recipe.outputQty;
                    stacked = true;
                    break;
                }
            }
            if (!stacked)
            {
                ClientInventorySlot slot;
                slot.itemId = recipe.outputId;
                slot.quantity = recipe.outputQty;
                slot.slotType = "bag";
#ifdef URHO3D_DATABASE_SQLITE
                ItemInfo outputInfo;
                if (gameDB_ && gameDB_->GetItem(recipe.outputId, outputInfo))
                {
                    slot.itemName = outputInfo.name;
                    slot.itemCategory = outputInfo.category;
                }
#endif
                inventory_.Push(slot);
            }
        }

        // Reset
        craftingRecipeId_ = -1;
        craftTimer_ = 0.0f;
        if (craftProgressFill_)
            craftProgressFill_->SetWidth(0);

        // Refresh UI
        RefreshRecipeList();
        RefreshRecipeDetail();
        RefreshInventoryGrid();

        if (hud_)
            hud_->SetContextHint("Crafted!");
    }
}

void TerrainNode::ToggleCrafting()
{
    craftingOpen_ = !craftingOpen_;

    if (!craftingWindow_)
        CreateCraftingUI();

    craftingWindow_->SetVisible(craftingOpen_);

    if (craftingOpen_)
    {
        RefreshRecipeList();
        if (selectedRecipeIndex_ >= 0)
            RefreshRecipeDetail();

        auto* input = GetSubsystem<Input>();
        if (!menuOpen_)
        {
            input->SetMouseMode(MM_FREE);
            input->SetMouseVisible(true);
        }
    }
    else
    {
        // Cancel active crafting if closing
        if (craftingRecipeId_ >= 0)
        {
            craftingRecipeId_ = -1;
            craftTimer_ = 0.0f;
            if (craftProgressFill_)
                craftProgressFill_->SetWidth(0);
            if (craftBtnText_)
            {
                craftBtnText_->SetText("Craft");
                craftBtnText_->SetColor(Color(0.87f, 0.8f, 0.73f));
            }
        }

        if (!menuOpen_ && !inventoryOpen_)
        {
            auto* input = GetSubsystem<Input>();
            if (input->HasFocus())
            {
                GetSubsystem<UI>()->SetFocusElement(nullptr);
                input->SetMouseMode(MM_RELATIVE);
                input->SetMouseVisible(false);
            }
        }
    }
}

HashMap<int, int> TerrainNode::BuildInventoryMap() const
{
    HashMap<int, int> result;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
        result[inventory_[i].itemId] += inventory_[i].quantity;
    return result;
}

void TerrainNode::SendCraft(int recipeId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Craft recipe %d (offline — no server)", recipeId);
        return;
    }

    VectorBuffer buf;
    buf.WriteI32(recipeId);
    serverConn->SendMessage(MSG_CRAFT, true, true, buf);
    URHO3D_LOGINFOF("Sent craft request for recipe %d", recipeId);
}

// ============================================================================
// Inventory Phase 5: Storage
// ============================================================================

void TerrainNode::CreateStorageUI()
{
    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    storageWindow_ = new Window(context_);
    ui->GetRoot()->AddChild(storageWindow_);
    storageWindow_->SetStyleAuto(style);
    storageWindow_->SetSize(260, 400);
    storageWindow_->SetAlignment(HA_LEFT, VA_CENTER);
    storageWindow_->SetPosition(20, 0);
    storageWindow_->SetMovable(true);
    storageWindow_->SetOpacity(0.9f);
    storageWindow_->SetVisible(false);
    storageWindow_->SetLayout(LM_VERTICAL, 4, IntRect(6, 6, 6, 6));

    // Title
    storageTitle_ = storageWindow_->CreateChild<Text>();
    storageTitle_->SetFont(font_, 13);
    storageTitle_->SetText("Storage");
    storageTitle_->SetColor(Color(0.9f, 0.85f, 0.7f));
    storageTitle_->SetHorizontalAlignment(HA_CENTER);

    // Grid — 4 rows of 5 slots (20 max)
    auto* gridContainer = storageWindow_->CreateChild<UIElement>();
    gridContainer->SetLayout(LM_VERTICAL, 2);

    const int slotsPerRow = 5;
    const int slotSize = 48;

    storageSlotButtons_.Clear();
    for (int row = 0; row < 4; ++row)
    {
        auto* rowElem = gridContainer->CreateChild<UIElement>();
        rowElem->SetLayout(LM_HORIZONTAL, 2);
        rowElem->SetMinHeight(slotSize + 4);

        for (int col = 0; col < slotsPerRow; ++col)
        {
            int idx = row * slotsPerRow + col;
            auto* btn = rowElem->CreateChild<Button>();
            btn->SetStyleAuto(style);
            btn->SetSize(slotSize, slotSize);
            btn->SetColor(Color(0.25f, 0.2f, 0.2f, 0.8f));
            btn->SetVar("StorageIndex", idx);

            auto* label = btn->CreateChild<Text>();
            label->SetFont(font_, 9);
            label->SetColor(Color::WHITE);
            label->SetHorizontalAlignment(HA_CENTER);
            label->SetVerticalAlignment(VA_CENTER);
            label->SetText("");

            SubscribeToEvent(btn, E_CLICK, URHO3D_HANDLER(TerrainNode, HandleStorageSlotClick));
            storageSlotButtons_.Push(btn);
        }
    }

    // Close button
    auto* closeBtn = storageWindow_->CreateChild<Button>();
    closeBtn->SetStyleAuto(style);
    closeBtn->SetMinHeight(24);
    auto* closeTxt = closeBtn->CreateChild<Text>();
    closeTxt->SetFont(font_, 11);
    closeTxt->SetText("Close");
    closeTxt->SetHorizontalAlignment(HA_CENTER);
    SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&) { CloseStorage(); });
}

void TerrainNode::OpenStorage(int buildingId)
{
    openBuildingId_ = buildingId;
    storageOpen_ = true;

    if (!storageWindow_)
        CreateStorageUI();

    storageWindow_->SetVisible(true);

    // Also open inventory side-by-side
    if (!inventoryOpen_)
        ToggleInventory();

    // Animate chest open
    if (buildingSystem_ && buildingSystem_->IsChest(buildingId))
        buildingSystem_->PlayChestOpen(buildingId);

    // Request contents from server
    SendOpenStorage(buildingId);
}

void TerrainNode::CloseStorage()
{
    if (openBuildingId_ >= 0)
    {
        // Animate chest close
        if (buildingSystem_ && buildingSystem_->IsChest(openBuildingId_))
            buildingSystem_->PlayChestClose(openBuildingId_);

        SendCloseStorage(openBuildingId_);
    }

    storageOpen_ = false;
    openBuildingId_ = -1;
    storageContents_.Clear();

    if (storageWindow_)
        storageWindow_->SetVisible(false);
}

void TerrainNode::ToggleStorage()
{
    if (storageOpen_)
        CloseStorage();
    // Opening requires a building ID — done via interaction, not key toggle
}

void TerrainNode::RefreshStorageGrid()
{
    if (!storageWindow_ || storageSlotButtons_.Empty())
        return;

    // Clear all slots
    for (unsigned i = 0; i < storageSlotButtons_.Size(); ++i)
    {
        auto* label = storageSlotButtons_[i]->GetChildStaticCast<Text>(0);
        if (label)
            label->SetText("");
        storageSlotButtons_[i]->SetColor(Color(0.25f, 0.2f, 0.2f, 0.8f));
    }

    // Fill from storageContents_
    for (unsigned i = 0; i < storageContents_.Size() && i < storageSlotButtons_.Size(); ++i)
    {
        auto* btn = storageSlotButtons_[i];
        auto* label = btn->GetChildStaticCast<Text>(0);
        if (label)
        {
            String name = storageContents_[i].itemName.Length() > 0
                ? storageContents_[i].itemName
                : String(storageContents_[i].itemId);
            String text = name;
            if (storageContents_[i].quantity > 1)
                text += " x" + String(storageContents_[i].quantity);
            label->SetText(text);
        }
        btn->SetColor(Color(0.35f, 0.3f, 0.3f, 0.9f));
    }
}

void TerrainNode::HandleStorageSlotClick(StringHash eventType, VariantMap& eventData)
{
    using namespace Click;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn) return;

    int storageIdx = btn->GetVar("StorageIndex").GetI32();

    // Left-click: transfer from storage to bag
    if (storageIdx >= 0 && storageIdx < (int)storageContents_.Size())
        TransferFromStorage(storageIdx);
}

void TerrainNode::HandleStorageContents(MemoryBuffer& msg)
{
    int buildingId = msg.ReadI32();
    int count = msg.ReadI32();

    storageContents_.Clear();
    for (int i = 0; i < count; ++i)
    {
        StorageSlot slot;
        slot.itemId = msg.ReadI32();
        slot.quantity = msg.ReadI32();
        slot.itemName = msg.ReadString();
        storageContents_.Push(slot);
    }

    if (storageTitle_)
        storageTitle_->SetText("Storage (" + String(count) + "/" + String(STORAGE_MAX_SLOTS) + ")");

    RefreshStorageGrid();
}

void TerrainNode::TransferToStorage(int bagIndex)
{
    // Find the bag item at this index
    int bagCount = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if (bagCount == bagIndex)
        {
            if ((int)storageContents_.Size() >= STORAGE_MAX_SLOTS)
            {
                URHO3D_LOGINFO("Storage is full");
                return;
            }
            SendTransfer(inventory_[i].itemId, inventory_[i].quantity, true);

            // Optimistic local update
            StorageSlot ss;
            ss.itemId = inventory_[i].itemId;
            ss.quantity = inventory_[i].quantity;
            ss.itemName = inventory_[i].itemName;
            storageContents_.Push(ss);
            inventory_.Erase(i);

            RefreshInventoryGrid();
            RefreshStorageGrid();
            return;
        }
        ++bagCount;
    }
}

void TerrainNode::TransferFromStorage(int storageIndex)
{
    if (storageIndex < 0 || storageIndex >= (int)storageContents_.Size())
        return;

    // Check weight
    // We don't have weight per storage item stored locally, so just check slot count
    int bagCount = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
        if (inventory_[i].slotType == "bag") ++bagCount;

    if (bagCount >= inventoryMaxSlots_)
    {
        URHO3D_LOGINFO("Inventory is full");
        return;
    }

    const auto& ss = storageContents_[storageIndex];
    SendTransfer(ss.itemId, ss.quantity, false);

    // Optimistic local update
    ClientInventorySlot slot;
    slot.itemId = ss.itemId;
    slot.quantity = ss.quantity;
    slot.itemName = ss.itemName;
    slot.slotType = "bag";
    inventory_.Push(slot);
    storageContents_.Erase(storageIndex);

    RefreshInventoryGrid();
    RefreshStorageGrid();
}

void TerrainNode::SendOpenStorage(int buildingId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Open storage %d (offline — no server)", buildingId);
        return;
    }

    VectorBuffer buf;
    buf.WriteI32(buildingId);
    serverConn->SendMessage(MSG_OPEN_STORAGE, true, true, buf);
}

void TerrainNode::SendCloseStorage(int buildingId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn) return;

    VectorBuffer buf;
    buf.WriteI32(buildingId);
    serverConn->SendMessage(MSG_CLOSE_STORAGE, true, true, buf);
}

void TerrainNode::SendTransfer(int itemId, int qty, bool toStorage)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Transfer item %d x%d %s (offline)", itemId, qty, toStorage ? "to storage" : "from storage");
        return;
    }

    VectorBuffer buf;
    buf.WriteI32(itemId);
    buf.WriteI32(qty);
    buf.WriteBool(toStorage);
    serverConn->SendMessage(MSG_TRANSFER, true, true, buf);
}

// ============================================================================
// Inventory Phase 6: Polish
// ============================================================================

void TerrainNode::SplitStack(int bagIndex)
{
    // Find the bag item at this index
    int bagCount = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if (bagCount == bagIndex)
        {
            if (inventory_[i].quantity <= 1)
                return;  // can't split a single item

            int half = inventory_[i].quantity / 2;
            int remainder = inventory_[i].quantity - half;

            // Check if there's room for a new slot
            int totalBag = 0;
            for (unsigned j = 0; j < inventory_.Size(); ++j)
                if (inventory_[j].slotType == "bag") ++totalBag;

            if (totalBag >= inventoryMaxSlots_)
            {
                URHO3D_LOGINFO("No room to split stack");
                return;
            }

            // Split: reduce original, create new slot
            inventory_[i].quantity = half;

            ClientInventorySlot newSlot;
            newSlot.itemId = inventory_[i].itemId;
            newSlot.quantity = remainder;
            newSlot.itemName = inventory_[i].itemName;
            newSlot.itemCategory = inventory_[i].itemCategory;
            newSlot.itemWeight = inventory_[i].itemWeight;
            newSlot.slotType = "bag";
            newSlot.durability = inventory_[i].durability;
            inventory_.Push(newSlot);

            RefreshInventoryGrid();
            return;
        }
        ++bagCount;
    }
}

void TerrainNode::SortInventory()
{
    Vector<ClientInventorySlot> bagItems;
    Vector<ClientInventorySlot> nonBagItems;

    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType == "bag")
            bagItems.Push(inventory_[i]);
        else
            nonBagItems.Push(inventory_[i]);
    }

    auto categoryOrder = [](const String& cat) -> int {
        if (cat == "weapon") return 0;
        if (cat == "tool") return 1;
        if (cat == "armor" || cat == "clothing") return 2;
        if (cat == "food" || cat == "drink") return 3;
        if (cat == "material") return 4;
        return 5;
    };

    InventorySortMode mode = inventorySortMode_;
    Sort(bagItems.Begin(), bagItems.End(),
        [&categoryOrder, mode](const ClientInventorySlot& a, const ClientInventorySlot& b) -> bool {
            switch (mode)
            {
            case SORT_NAME:
                return a.itemName < b.itemName;
            case SORT_WEIGHT:
                return a.weight > b.weight;  // heaviest first
            case SORT_TIER:
                if (a.tier != b.tier) return a.tier > b.tier;  // highest tier first
                return a.itemName < b.itemName;
            default:  // SORT_CATEGORY
            {
                int ca = categoryOrder(a.itemCategory);
                int cb = categoryOrder(b.itemCategory);
                if (ca != cb) return ca < cb;
                return a.itemName < b.itemName;
            }
            }
        });

    inventory_.Clear();
    for (unsigned i = 0; i < nonBagItems.Size(); ++i)
        inventory_.Push(nonBagItems[i]);
    for (unsigned i = 0; i < bagItems.Size(); ++i)
        inventory_.Push(bagItems[i]);

    RefreshInventoryGrid();
}

void TerrainNode::HandleSortButton(StringHash eventType, VariantMap& eventData)
{
    inventorySortMode_ = static_cast<InventorySortMode>((inventorySortMode_ + 1) % SORT_COUNT);

    static const char* modeNames[] = {"Cat", "Name", "Wt", "Tier"};
    if (sortButton_)
    {
        auto* label = sortButton_->GetChildStaticCast<Text>(0);
        if (label)
            label->SetText(modeNames[inventorySortMode_]);
    }

    SortInventory();
}

void TerrainNode::HandleFilterButton(StringHash eventType, VariantMap& eventData)
{
    auto* element = static_cast<UIElement*>(eventData[Released::P_ELEMENT].GetPtr());
    if (!element) return;

    inventoryFilter_ = element->GetVar("FilterCat").GetString();
    UpdateFilterButtonColors();
    RefreshInventoryGrid();
}

void TerrainNode::UpdateFilterButtonColors()
{
    for (unsigned i = 0; i < filterButtons_.Size(); ++i)
    {
        String cat = filterButtons_[i]->GetVar("FilterCat").GetString();
        bool active = (cat == inventoryFilter_);
        filterButtons_[i]->SetColor(active
            ? Color(0.3f, 0.5f, 0.7f, 0.9f)
            : Color(0.25f, 0.25f, 0.3f, 0.8f));
    }
}

void TerrainNode::ShowItemTooltip(int bagIndex, const IntVector2& screenPos)
{
    // Find the actual item
    int bagCount = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if (bagCount == bagIndex)
        {
            const auto& item = inventory_[i];
            auto* ui = GetSubsystem<UI>();
            auto* cache = GetSubsystem<ResourceCache>();
            auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

            if (!itemTooltip_)
            {
                itemTooltip_ = new Window(context_);
                ui->GetRoot()->AddChild(itemTooltip_);
                itemTooltip_->SetStyleAuto(style);
                itemTooltip_->SetLayout(LM_VERTICAL, 3, IntRect(6, 4, 6, 4));
                itemTooltip_->SetOpacity(0.95f);
                itemTooltip_->SetPriority(200);  // above everything
            }
            else
            {
                itemTooltip_->RemoveAllChildren();
            }

            // Name
            auto* nameText = itemTooltip_->CreateChild<Text>();
            nameText->SetFont(font_, 12);
            nameText->SetText(item.itemName.Length() > 0 ? item.itemName : String(item.itemId));
            nameText->SetColor(Color(0.95f, 0.9f, 0.75f));

            // Category
            if (item.itemCategory.Length() > 0)
            {
                auto* catText = itemTooltip_->CreateChild<Text>();
                catText->SetFont(font_, 9);
                catText->SetText(item.itemCategory);
                catText->SetColor(Color(0.6f, 0.6f, 0.6f));
            }

            // Quantity
            if (item.quantity > 1)
            {
                auto* qtyText = itemTooltip_->CreateChild<Text>();
                qtyText->SetFont(font_, 10);
                qtyText->SetText("Qty: " + String(item.quantity));
                qtyText->SetColor(Color(0.7f, 0.7f, 0.7f));
            }

            // Weight
            if (item.itemWeight > 0.0f)
            {
                char wBuf[32];
                snprintf(wBuf, sizeof(wBuf), "Weight: %.1f kg", item.itemWeight * item.quantity);
                auto* wText = itemTooltip_->CreateChild<Text>();
                wText->SetFont(font_, 10);
                wText->SetText(String(wBuf));
                wText->SetColor(Color(0.7f, 0.7f, 0.7f));
            }

            // Durability
            if (item.durability >= 0)
            {
                auto* durText = itemTooltip_->CreateChild<Text>();
                durText->SetFont(font_, 10);
                durText->SetText("Durability: " + String(item.durability));
                if (item.durability == 0)
                    durText->SetColor(Color(0.9f, 0.3f, 0.3f));
                else if (item.durability < 5)
                    durText->SetColor(Color(0.9f, 0.7f, 0.2f));
                else
                    durText->SetColor(Color(0.5f, 0.8f, 0.5f));
            }

            // Tapestry inscription
            if (item.itemId == 896 && !tapestryInscription_.Empty())
            {
                auto* insText = itemTooltip_->CreateChild<Text>();
                insText->SetFont(font_, 9);
                insText->SetText("\"" + tapestryInscription_ + "\"");
                insText->SetColor(Color(0.85f, 0.7f, 0.3f));
            }

            // Position near cursor, keep on screen
            auto* graphics = GetSubsystem<Graphics>();
            int tipW = 160;
            int tipH = itemTooltip_->GetChildren().Size() * 18 + 12;
            itemTooltip_->SetSize(tipW, tipH);

            int x = screenPos.x_ + 16;
            int y = screenPos.y_ - 8;
            if (x + tipW > graphics->GetWidth())
                x = screenPos.x_ - tipW - 8;
            if (y + tipH > graphics->GetHeight())
                y = graphics->GetHeight() - tipH;

            itemTooltip_->SetPosition(x, y);
            itemTooltip_->SetVisible(true);
            return;
        }
        ++bagCount;
    }
}

void TerrainNode::HideItemTooltip()
{
    if (itemTooltip_)
        itemTooltip_->SetVisible(false);
}

void TerrainNode::HandleSlotHoverBegin(StringHash eventType, VariantMap& eventData)
{
    using namespace HoverBegin;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn) return;

    int bagIdx = btn->GetVar("BagIndex").GetI32();
    IntVector2 screenPos = btn->GetScreenPosition();
    screenPos.x_ += btn->GetWidth();
    ShowItemTooltip(bagIdx, screenPos);
}

void TerrainNode::HandleSlotHoverEnd(StringHash eventType, VariantMap& eventData)
{
    HideItemTooltip();
}

void TerrainNode::UpdateWeightBarColor()
{
    // Already handled in RefreshInventoryGrid — this is a no-op hook for future extensions
}

// ============================================================================
// Building System Phase 1
// ============================================================================

void TerrainNode::InitBuildingSystem()
{
    if (!scene_)
        return;

    // Create building system component on the scene
    auto* existing = scene_->GetComponent<BuildingSystem>();
    if (!existing)
    {
        buildingSystem_ = scene_->CreateComponent<BuildingSystem>(LOCAL);
    }
    else
    {
        buildingSystem_ = existing;
    }

    // Load building types and snap rules from GameDB
    LoadBuildingTypes();
    LoadSnapRules();
}

void TerrainNode::LoadBuildingTypes()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!gameDB_ || !buildingSystem_)
        return;

    Vector<BuildingTypeDBInfo> dbTypes = gameDB_->GetAllBuildingTypes();

    Vector<BuildingTypeInfo> types;
    for (unsigned i = 0; i < dbTypes.Size(); ++i)
    {
        const BuildingTypeDBInfo& dt = dbTypes[i];
        BuildingTypeInfo info;
        info.id = dt.id;
        info.name = dt.name;
        info.category = dt.category;
        info.tier = dt.tier;
        info.footprintX = dt.footprintX;
        info.footprintZ = dt.footprintZ;
        info.height = dt.height;
        info.maxHp = dt.maxHp;
        info.decayRate = dt.decayRate;
        info.warmth = dt.warmth;
        info.storageSlots = dt.storageSlots;
        info.sleepCapacity = dt.sleepCapacity;
        info.respawn = dt.respawn;
        info.snapType = dt.snapType;
        info.modelPath = dt.model;
        info.ghostModelPath = dt.ghostModel;
        info.description = dt.description;
        types.Push(info);
    }

    buildingSystem_->SetBuildingTypes(types);
    URHO3D_LOGINFOF("Loaded %u building types from GameDB", types.Size());
#endif
}

void TerrainNode::LoadSnapRules()
{
#ifdef URHO3D_DATABASE_SQLITE
    if (!buildingSystem_)
        return;

    // Pass GameDB to BuildingSystem for live GetSnapAlign() lookups in FindSnapPoint
    if (gameDB_)
    {
        buildingSystem_->SetGameDB(gameDB_.Get());
        URHO3D_LOGINFO("Snap rules: BuildingSystem wired to GameDB::GetSnapAlign()");
    }

    // Wire terrain for auto-flatten on building placement
    if (terrain_ && editableHeightMap_)
        buildingSystem_->SetTerrain(terrain_, editableHeightMap_);
#endif
}

void TerrainNode::CreateBuildMenuUI()
{
    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    buildMenuWindow_ = new Window(context_);
    ui->GetRoot()->AddChild(buildMenuWindow_);
    buildMenuWindow_->SetStyleAuto(style);
    buildMenuWindow_->SetSize(240, 380);
    buildMenuWindow_->SetAlignment(HA_RIGHT, VA_CENTER);
    buildMenuWindow_->SetPosition(-20, 0);
    buildMenuWindow_->SetMovable(true);
    buildMenuWindow_->SetOpacity(0.9f);
    buildMenuWindow_->SetVisible(false);
    buildMenuWindow_->SetLayout(LM_VERTICAL, 4, IntRect(6, 6, 6, 6));

    // Title
    auto* title = buildMenuWindow_->CreateChild<Text>();
    title->SetFont(font_, 13);
    title->SetText("Build");
    title->SetColor(Color(0.9f, 0.85f, 0.7f));
    title->SetHorizontalAlignment(HA_CENTER);

    // Status text
    buildStatusText_ = buildMenuWindow_->CreateChild<Text>();
    buildStatusText_->SetFont(font_, 10);
    buildStatusText_->SetColor(Color(0.7f, 0.7f, 0.6f));
    buildStatusText_->SetText("Select a building to place");

    // Building list
    buildMenuList_ = buildMenuWindow_->CreateChild<ListView>();
    buildMenuList_->SetStyleAuto(style);
    buildMenuList_->SetMinHeight(280);
    buildMenuList_->SetHighlightMode(HM_ALWAYS);
    SubscribeToEvent(buildMenuList_, E_ITEMSELECTED, URHO3D_HANDLER(TerrainNode, HandleBuildMenuSelect));

    // Close button
    auto* closeBtn = buildMenuWindow_->CreateChild<Button>();
    closeBtn->SetStyleAuto(style);
    closeBtn->SetMinHeight(24);
    auto* closeTxt = closeBtn->CreateChild<Text>();
    closeTxt->SetFont(font_, 11);
    closeTxt->SetText("Close [B]");
    closeTxt->SetHorizontalAlignment(HA_CENTER);
    SubscribeToEvent(closeBtn, E_RELEASED, [this](StringHash, VariantMap&) { ToggleBuildMode(); });
}

void TerrainNode::RefreshBuildMenu()
{
    if (!buildMenuList_ || !buildingSystem_)
        return;

    buildMenuList_->RemoveAllItems();
    const auto& types = buildingSystem_->GetBuildingTypes();
    HashMap<int, int> inv = BuildInventoryMap();

    // Group by category with headers
    const char* categoryOrder[] = {
        "shelter", "wall", "gate", "defense", "utility", "workshop",
        "industry", "storage", "furniture", "decoration", "transport",
        "memorial", "military"
    };
    const unsigned numCategories = sizeof(categoryOrder) / sizeof(categoryOrder[0]);

    for (unsigned c = 0; c < numCategories; ++c)
    {
        String cat(categoryOrder[c]);
        // Collect items in this category
        bool hasItems = false;
        for (unsigned i = 0; i < types.Size(); ++i)
        {
            if (types[i].category == cat)
            {
                hasItems = true;
                break;
            }
        }
        if (!hasItems)
            continue;

        // Category header
        auto* header = new Text(context_);
        header->SetFont(font_, 12);
        String headerText = cat;
        headerText[0] = (char)toupper(headerText[0]);
        header->SetText("-- " + headerText + " --");
        header->SetColor(Color(0.7f, 0.8f, 0.5f));
        header->SetVar("BuildingTypeId", -1);  // not selectable
        buildMenuList_->AddItem(header);

        // Items in this category
        for (unsigned i = 0; i < types.Size(); ++i)
        {
            if (types[i].category != cat)
                continue;

            auto* row = new Text(context_);
            row->SetFont(font_, 11);
            row->SetVar("BuildingTypeId", types[i].id);

            String label = "  " + types[i].name;
            label += " (T" + String(types[i].tier) + ")";

            row->SetText(label);
            row->SetColor(Color(0.85f, 0.85f, 0.85f));

            buildMenuList_->AddItem(row);
        }
    }
}

void TerrainNode::HandleBuildMenuSelect(StringHash eventType, VariantMap& eventData)
{
    using namespace ItemSelected;
    int sel = eventData[P_SELECTION].GetI32();
    if (sel < 0 || !buildMenuList_)
        return;

    auto* item = buildMenuList_->GetItem(sel);
    if (!item)
        return;

    int typeId = item->GetVar("BuildingTypeId").GetI32();
    if (typeId < 0)
        return;  // category header — not selectable
    if (buildingSystem_)
    {
        buildingSystem_->SetBuildMode(true, typeId);

        // Find the building name for status text
        const auto& types = buildingSystem_->GetBuildingTypes();
        for (unsigned i = 0; i < types.Size(); ++i)
        {
            if (types[i].id == typeId)
            {
                if (buildStatusText_)
                    buildStatusText_->SetText("Placing: " + types[i].name + "\nR=Rotate, Click=Place, Esc=Cancel");
                break;
            }
        }
    }
}

void TerrainNode::ToggleBuildMode()
{
    buildMenuOpen_ = !buildMenuOpen_;

    if (!buildMenuWindow_)
        CreateBuildMenuUI();

    buildMenuWindow_->SetVisible(buildMenuOpen_);

    if (buildMenuOpen_)
    {
        RefreshBuildMenu();
        auto* input = GetSubsystem<Input>();
        input->SetMouseMode(MM_FREE);
        input->SetMouseVisible(true);
    }
    else
    {
        // Exit build mode
        if (buildingSystem_)
            buildingSystem_->SetBuildMode(false);

        if (!menuOpen_ && !inventoryOpen_ && !craftingOpen_)
        {
            auto* input = GetSubsystem<Input>();
            if (input->HasFocus())
            {
                GetSubsystem<UI>()->SetFocusElement(nullptr);
                input->SetMouseMode(MM_RELATIVE);
                input->SetMouseVisible(false);
            }
        }
    }
}

void TerrainNode::TryGateInteract()
{
    if (!buildingSystem_ || focusedGateId_ < 0)
        return;

    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    buildingSystem_->RequestGateToggle(serverConn, focusedGateId_);
}

void TerrainNode::TryBoardCanoe()
{
    if (!buildingSystem_ || !characterNode_)
        return;

    Vector3 playerPos = characterNode_->GetWorldPosition();

    if (inCanoe_)
    {
        // Disembark
        inCanoe_ = false;
        boardedCanoeId_ = -1;
        if (hud_)
            hud_->SetContextHint("Disembarked");
        URHO3D_LOGINFO("Disembarked from canoe");
        return;
    }

    // Find nearest canoe within 5m
    int nearId = buildingSystem_->FindNearestBuilding(playerPos, 5.0f);
    if (nearId < 0)
        return;

    const auto& buildings = buildingSystem_->GetPlacedBuildings();
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].placedId == nearId && buildings[i].buildingTypeId == 70)
        {
            inCanoe_ = true;
            boardedCanoeId_ = nearId;
            if (hud_)
                hud_->SetContextHint("Boarded canoe — move on water!");
            URHO3D_LOGINFOF("Boarded canoe %d", nearId);
            return;
        }
    }
}

void TerrainNode::TryBuildingInteract()
{
    if (!buildingSystem_ || !characterNode_)
        return;

    Vector3 playerPos = characterNode_->GetWorldPosition();
    int nearId = buildingSystem_->FindNearestBuilding(playerPos, 5.0f);
    if (nearId < 0)
    {
        // Fall back to gate interact if focused
        TryGateInteract();
        return;
    }

    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;

    // Check if it's a gate — toggle it
    if (buildingSystem_->IsGate(nearId))
    {
        buildingSystem_->RequestGateToggle(serverConn, nearId);
        return;
    }

    // Find the building type
    const auto& buildings = buildingSystem_->GetPlacedBuildings();
    int typeId = -1;
    int hp = 0, maxHp = 0;
    Vector3 buildingPos;
    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        if (buildings[i].placedId == nearId)
        {
            typeId = buildings[i].buildingTypeId;
            hp = buildings[i].hp;
            maxHp = buildings[i].maxHp;
            buildingPos = buildings[i].position;
            break;
        }
    }

    const auto& types = buildingSystem_->GetBuildingTypes();
    const BuildingTypeInfo* info = nullptr;
    for (unsigned i = 0; i < types.Size(); ++i)
    {
        if (types[i].id == typeId)
        {
            info = &types[i];
            break;
        }
    }

    if (!info)
        return;

    // Fire system Phase 2b: Woodpile deposit. Take all available softwood/hardwood
    // from local inventory and add to the pile (capped at woodCapacity per type).
    // Currently client-local only — no inventory consume on server side. Phase 3
    // will replace this with a proper MSG_WOODPILE_TRANSFER round-trip.
    if (info->id == 56)
    {
        DepositToWoodpile(nearId);
        return;
    }

    // Shelter: sleep + set respawn
    if (info->sleepCapacity > 0)
    {
        buildingSystem_->RequestSleep(serverConn, nearId);
        if (!serverConn)
        {
            // Offline sleep — replicate server vitals boost locally
            vitalHp_ = Min(vitalMaxHp_, vitalHp_ + 5);
            vitalStamina_ = Min(100.0f, vitalStamina_ + 30.0f);
            if (info->warmth > 0.0f)
                vitalWarmth_ = Max(vitalWarmth_, info->warmth);
            UpdateVitalBars();
        }
        if (info->respawn)
        {
            buildingSystem_->RequestSetRespawn(serverConn, nearId);
            if (!serverConn)
            {
                respawnBuildingId_ = nearId;
                respawnPosition_ = buildingPos;
            }
        }
        return;
    }

    // Damaged building: repair
    if (hp < maxHp)
    {
        buildingSystem_->RequestRepair(serverConn, nearId);
        return;
    }

    // Storage building: Shift+E = lock/unlock, E = open storage
    if (info->storageSlots > 0 && serverConn)
    {
        auto* input = GetSubsystem<Input>();
        if (input && input->GetKeyDown(KEY_SHIFT))
        {
            // Check if player has a lock item (932=Bronze Lock, 933=Iron Lock)
            int lockItemId = 0;
            for (unsigned i = 0; i < inventory_.Size(); ++i)
            {
                if (inventory_[i].itemId == 932 || inventory_[i].itemId == 933)
                { lockItemId = inventory_[i].itemId; break; }
            }
            if (lockItemId > 0)
            {
                VectorBuffer buf;
                buf.WriteI32(nearId);
                buf.WriteI32(lockItemId);
                serverConn->SendMessage(MSG_INSTALL_LOCK, true, true, buf);
                if (hud_) hud_->SetContextHint("Installing lock...");
            }
            else
            {
                // No lock in inventory — try to remove existing lock (owner only)
                VectorBuffer buf;
                buf.WriteI32(nearId);
                serverConn->SendMessage(MSG_REMOVE_LOCK, true, true, buf);
                if (hud_) hud_->SetContextHint("Removing lock...");
            }
            return;
        }

        VectorBuffer buf;
        buf.WriteI32(nearId);
        serverConn->SendMessage(MSG_OPEN_STORAGE, true, true, buf);
        return;
    }
}

void TerrainNode::DepositToWoodpile(int placedId)
{
    // Server-authoritative: send deposit request, server validates inventory
    // and broadcasts updated woodpile state back to all clients.
    auto* network = GetSubsystem<Network>();
    Connection* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VariantMap data;
    data[P_PILE_BUILDING_ID] = placedId;
    serverConn->SendRemoteEvent(E_WOODPILE_DEPOSIT, true, data);
}

void TerrainNode::HandleWoodpileState(StringHash, VariantMap& eventData)
{
    if (!buildingSystem_)
        return;

    int buildingId = eventData[P_PILE_BUILDING_ID].GetI32();
    int softwood   = eventData[P_PILE_SOFTWOOD].GetI32();
    int hardwood   = eventData[P_PILE_HARDWOOD].GetI32();
    int capacity   = eventData[P_PILE_CAPACITY].GetI32();

    PlacedBuilding* pb = buildingSystem_->FindPlacedMutable(buildingId);
    if (!pb)
        return;

    pb->softwoodBu  = softwood;
    pb->hardwoodBu  = hardwood;
    pb->woodCapacity = capacity;

    URHO3D_LOGINFOF("WoodpileState building=%d SW %d/%d HW %d/%d",
        buildingId, softwood, capacity, hardwood, capacity);
}

void TerrainNode::HandleBuildMessage(int msgID, MemoryBuffer& msg)
{
    if (!buildingSystem_)
        return;

    if (msgID == MSG_BUILD_RESULT)
    {
        bool success = msg.ReadBool();
        if (success)
        {
            int placedId = msg.ReadI32();
            URHO3D_LOGINFOF("Build successful: placed_id=%d", placedId);

            // Auto-claim the patch this building is on
            if (characterNode_)
            {
                Vector3 pos = characterNode_->GetWorldPosition();
                int patchX = (int)(pos.x_ / 64.0f);
                int patchZ = (int)(pos.z_ / 64.0f);
                SendPatchClaim(patchX, patchZ);
            }
        }
        else
        {
            String reason = msg.ReadString();
            URHO3D_LOGINFOF("Build failed: %s", reason.CString());
            if (buildStatusText_)
                buildStatusText_->SetText("Build failed: " + reason);
        }
    }
    else if (msgID == MSG_BUILDING_SPAWN)
    {
        int placedId = msg.ReadI32();
        int typeId = msg.ReadI32();
        float px = msg.ReadFloat();
        float py = msg.ReadFloat();
        float pz = msg.ReadFloat();
        float rot = msg.ReadFloat();
        int hp = msg.ReadI32();
        String storageJson = msg.ReadString();  // metadata (graves: deceased name)
        buildingSystem_->HandleBuildingSpawn(placedId, typeId, Vector3(px, py, pz), rot, hp);

        // Water Phase 4: wells get a water shimmer effect
        if (typeId == 57)  // BUILDING_WELL
        {
            PlacedBuilding* pb = buildingSystem_->FindPlacedMutable(placedId);
            Node* wellNode = (pb && pb->node) ? pb->node.Get() : nullptr;
            if (wellNode)
            {
                Node* fxNode = wellNode->CreateChild("WellWater");
                fxNode->SetPosition(Vector3(0.0f, 0.8f, 0.0f));  // water surface inside well

                auto* light = fxNode->CreateComponent<Light>();
                light->SetLightType(LIGHT_POINT);
                light->SetColor(Color(0.25f, 0.4f, 0.7f));
                light->SetRange(2.0f);
                light->SetBrightness(0.1f);

                auto* cache = GetSubsystem<ResourceCache>();
                auto* effect = cache->GetResource<ParticleEffect>("Particle/Smoke.xml");
                if (effect)
                {
                    auto* emitter = fxNode->CreateComponent<ParticleEmitter>();
                    SharedPtr<ParticleEffect> local = effect->Clone();
                    local->SetMinEmissionRate(0.2f);
                    local->SetMaxEmissionRate(0.5f);
                    local->SetMinParticleSize(Vector2(0.03f, 0.03f));
                    local->SetMaxParticleSize(Vector2(0.06f, 0.06f));
                    emitter->SetEffect(local);
                }
            }
        }

        // Phase 18: Grave marker nameplate — "RIP [name]" above the grave
        if (typeId == 60)  // Grave
        {
            PlacedBuilding* pb = buildingSystem_->FindPlacedMutable(placedId);
            Node* graveNode = (pb && pb->node) ? pb->node.Get() : nullptr;
            if (graveNode)
            {
                auto* cache = GetSubsystem<ResourceCache>();
                auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
                if (font)
                {
                    // Parse deceased name from storage JSON: {"deceased":"Name","day":N}
                    String label = "RIP";
                    if (!storageJson.Empty())
                    {
                        unsigned dPos = storageJson.Find("\"deceased\":\"");
                        if (dPos != String::NPOS)
                        {
                            dPos += 12;  // skip past "deceased":"
                            unsigned dEnd = storageJson.Find("\"", dPos);
                            if (dEnd != String::NPOS)
                                label = "RIP " + storageJson.Substring(dPos, dEnd - dPos);
                        }
                    }

                    Node* labelNode = graveNode->CreateChild("GraveLabel");
                    labelNode->SetPosition(Vector3(0.0f, 0.8f, 0.0f));

                    auto* text3D = labelNode->CreateComponent<Text3D>();
                    text3D->SetFont(font, 10);
                    text3D->SetText(label);
                    text3D->SetColor(Color(0.7f, 0.65f, 0.6f, 0.8f));
                    text3D->SetAlignment(HA_CENTER, VA_BOTTOM);
                    text3D->SetFaceCameraMode(FC_ROTATE_Y);
                    text3D->SetTextEffect(TE_SHADOW);
                    text3D->SetEffectColor(Color(0.0f, 0.0f, 0.0f, 0.4f));
                }
            }
        }
    }
    else if (msgID == MSG_BUILDING_REMOVE)
    {
        int placedId = msg.ReadI32();
        buildingSystem_->HandleBuildingRemove(placedId);
    }
    else if (msgID == MSG_GATE_STATE)
    {
        int placedId = msg.ReadI32();
        bool open = msg.ReadBool();
        buildingSystem_->HandleGateState(placedId, open);
    }
    else if (msgID == MSG_BUILDING_HP)
    {
        int placedId = msg.ReadI32();
        int newHp = msg.ReadI32();
        buildingSystem_->HandleBuildingHpUpdate(placedId, newHp);
    }
    else if (msgID == MSG_RESPAWN_SET)
    {
        int placedId = msg.ReadI32();
        float rx = msg.ReadFloat();
        float ry = msg.ReadFloat();
        float rz = msg.ReadFloat();
        respawnBuildingId_ = placedId;
        respawnPosition_ = Vector3(rx, ry, rz);
        URHO3D_LOGINFOF("Respawn set at building %d (%.1f,%.1f,%.1f)", placedId, rx, ry, rz);
    }
}

// =============================================================================
// Biome Classification
// =============================================================================

String BiomeToString(BiomeType biome)
{
    switch (biome)
    {
    case BIOME_WATER:     return "water";
    case BIOME_RIVERBANK: return "riverbank";
    case BIOME_GRASSLAND: return "grassland";
    case BIOME_FOREST:    return "forest";
    case BIOME_MOUNTAIN:  return "mountain";
    case BIOME_DESERT:    return "desert";
    default:              return "any";
    }
}

bool TerrainMatchesSource(const String& sourceTerrain, BiomeType biome)
{
    if (sourceTerrain == "any")
        return true;
    return sourceTerrain == BiomeToString(biome);
}

void TerrainNode::CacheWeightMapImage()
{
    if (weightMapImage_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    // Prefer PNG (lossless, reliable CPU read) over DDS
    weightMapImage_ = cache->GetResource<Image>("Textures/TerrainWeights.png");
    if (!weightMapImage_)
        weightMapImage_ = cache->GetResource<Image>("Textures/TerrainWeights.dds");

    if (weightMapImage_)
        URHO3D_LOGINFO("Biome classifier: cached weight map image (" +
                       String(weightMapImage_->GetWidth()) + "x" +
                       String(weightMapImage_->GetHeight()) + ")");
    else
        URHO3D_LOGWARNING("Biome classifier: weight map image not found — defaulting to grassland");
}

void TerrainNode::SampleWeightMap(const Vector2& uv, float& outR, float& outG, float& outB) const
{
    if (!weightMapImage_)
    {
        outR = 1.0f; outG = 0.0f; outB = 0.0f;
        return;
    }

    int px = (int)(Clamp(uv.x_, 0.0f, 1.0f) * (float)(weightMapImage_->GetWidth() - 1));
    int py = (int)(Clamp(uv.y_, 0.0f, 1.0f) * (float)(weightMapImage_->GetHeight() - 1));
    Color c = weightMapImage_->GetPixel(px, py);
    outR = c.r_;
    outG = c.g_;
    outB = c.b_;
}

// --- Soil query API (Plan 9) ---

SoilType TerrainNode::GetSoilType(const Vector3& worldPos) const
{
    if (!soilMap_ || !terrain_)
        return SOIL_ROCK;
    IntVector2 hm = terrain_->WorldToHeightMap(worldPos);
    return soilMap_->SampleSoilType(hm.x_, hm.y_);
}

unsigned char TerrainNode::GetFertility(const Vector3& worldPos) const
{
    if (!soilMap_ || !terrain_)
        return 0;
    IntVector2 hm = terrain_->WorldToHeightMap(worldPos);
    return soilMap_->SampleFertility(hm.x_, hm.y_);
}

unsigned char TerrainNode::GetMineralDensity(const Vector3& worldPos) const
{
    if (!soilMap_ || !terrain_)
        return 0;
    IntVector2 hm = terrain_->WorldToHeightMap(worldPos);
    return soilMap_->SampleMineralDensity(hm.x_, hm.y_);
}

BiomeType TerrainNode::ClassifyTerrain(const Vector3& worldPos) const
{
    if (!terrain_)
        return BIOME_ANY;

    const float waterLevel = 5.0f;
    float height = terrain_->GetHeight(worldPos);

    // 1. Underwater
    if (height < waterLevel - 0.5f)
        return BIOME_WATER;

    // 2. Riverbank — near water level, low and flat
    if (height < waterLevel + 3.0f && height >= waterLevel - 0.5f)
        return BIOME_RIVERBANK;

    // 3. Slope check
    Vector3 normal = terrain_->GetNormal(worldPos);
    float flatness = normal.y_;  // 1.0 = flat, 0.0 = cliff

    // 4. Mountain — steep slope OR very high elevation
    if (flatness < 0.7f || height > 80.0f)
        return BIOME_MOUNTAIN;

    // 5. Sample weight map for vegetation vs rock classification
    Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();
    Vector3 spacing = terrain_->GetSpacing();
    IntVector2 numVerts = terrain_->GetNumVertices();
    float terrainSizeX = (float)(numVerts.x_ - 1) * spacing.x_;
    float terrainSizeZ = (float)(numVerts.y_ - 1) * spacing.z_;

    Vector2 uv(
        (worldPos.x_ - terrainPos.x_ + terrainSizeX * 0.5f) / terrainSizeX,
        (worldPos.z_ - terrainPos.z_ + terrainSizeZ * 0.5f) / terrainSizeZ
    );

    float weightR, weightG, weightB;
    SampleWeightMap(uv, weightR, weightG, weightB);

    // 6. Forest — high vegetation, moderate terrain, mid-elevation
    if (weightR > 0.5f && height > 15.0f && height < 70.0f && flatness > 0.8f)
        return BIOME_FOREST;

    // 7. Grassland — high vegetation, flat, lower areas
    if (weightR > 0.4f && flatness > 0.85f)
        return BIOME_GRASSLAND;

    // 8. Rock dominant at moderate height → mountain
    if (weightG > 0.5f)
        return BIOME_MOUNTAIN;

    // 9. Desert — flat, dry terrain with low vegetation (matches ECO_BARREN)
    if (weightR < 0.1f && flatness > 0.8f && height > waterLevel + 1.0f)
        return BIOME_DESERT;

    return BIOME_GRASSLAND;  // default fallback
}

// ============================================================================
// Water Distance Map — precomputed low-res grid for habitat spawning
// ============================================================================

void TerrainNode::BuildWaterDistanceMap()
{
    if (!terrain_)
        return;

    const float waterLevel = 5.5f;
    const float cellSize = 8.0f;  // 8m per cell

    Vector3 spacing = terrain_->GetSpacing();
    IntVector2 numVerts = terrain_->GetNumVertices();
    float terrainW = (float)(numVerts.x_ - 1) * spacing.x_;
    float terrainH = (float)(numVerts.y_ - 1) * spacing.z_;
    Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();

    waterDistOrigin_ = Vector3(terrainPos.x_ - terrainW * 0.5f, 0.0f, terrainPos.z_ - terrainH * 0.5f);
    waterDistCellSize_ = cellSize;
    waterDistMapW_ = (int)(terrainW / cellSize) + 1;
    waterDistMapH_ = (int)(terrainH / cellSize) + 1;
    waterDistMap_.Resize(waterDistMapW_ * waterDistMapH_);

    // Pass 1: mark water vs land
    for (int gz = 0; gz < waterDistMapH_; ++gz)
    {
        for (int gx = 0; gx < waterDistMapW_; ++gx)
        {
            float wx = waterDistOrigin_.x_ + gx * cellSize;
            float wz = waterDistOrigin_.z_ + gz * cellSize;
            float h = terrain_->GetHeight(Vector3(wx, 0.0f, wz));
            waterDistMap_[gz * waterDistMapW_ + gx] = (h < waterLevel) ? 0.0f : 9999.0f;
        }
    }

    // Pass 2: forward distance propagation (top-left to bottom-right)
    for (int gz = 0; gz < waterDistMapH_; ++gz)
    {
        for (int gx = 0; gx < waterDistMapW_; ++gx)
        {
            int idx = gz * waterDistMapW_ + gx;
            if (waterDistMap_[idx] == 0.0f)
                continue;
            float best = waterDistMap_[idx];
            if (gx > 0)
                best = Min(best, waterDistMap_[idx - 1] + cellSize);
            if (gz > 0)
                best = Min(best, waterDistMap_[(gz - 1) * waterDistMapW_ + gx] + cellSize);
            if (gx > 0 && gz > 0)
                best = Min(best, waterDistMap_[(gz - 1) * waterDistMapW_ + gx - 1] + cellSize * 1.414f);
            if (gx < waterDistMapW_ - 1 && gz > 0)
                best = Min(best, waterDistMap_[(gz - 1) * waterDistMapW_ + gx + 1] + cellSize * 1.414f);
            waterDistMap_[idx] = best;
        }
    }

    // Pass 3: backward propagation (bottom-right to top-left)
    for (int gz = waterDistMapH_ - 1; gz >= 0; --gz)
    {
        for (int gx = waterDistMapW_ - 1; gx >= 0; --gx)
        {
            int idx = gz * waterDistMapW_ + gx;
            if (waterDistMap_[idx] == 0.0f)
                continue;
            float best = waterDistMap_[idx];
            if (gx < waterDistMapW_ - 1)
                best = Min(best, waterDistMap_[idx + 1] + cellSize);
            if (gz < waterDistMapH_ - 1)
                best = Min(best, waterDistMap_[(gz + 1) * waterDistMapW_ + gx] + cellSize);
            if (gx < waterDistMapW_ - 1 && gz < waterDistMapH_ - 1)
                best = Min(best, waterDistMap_[(gz + 1) * waterDistMapW_ + gx + 1] + cellSize * 1.414f);
            if (gx > 0 && gz < waterDistMapH_ - 1)
                best = Min(best, waterDistMap_[(gz + 1) * waterDistMapW_ + gx - 1] + cellSize * 1.414f);
            waterDistMap_[idx] = best;
        }
    }

    URHO3D_LOGINFOF("BuildWaterDistanceMap: %dx%d grid (%.0fm cells), terrain %.0fx%.0f",
        waterDistMapW_, waterDistMapH_, cellSize, terrainW, terrainH);
}

float TerrainNode::SampleWaterDistance(float worldX, float worldZ) const
{
    if (waterDistMap_.Empty() || waterDistCellSize_ <= 0.0f)
        return -1.0f;

    float lx = (worldX - waterDistOrigin_.x_) / waterDistCellSize_;
    float lz = (worldZ - waterDistOrigin_.z_) / waterDistCellSize_;

    int gx = Clamp((int)lx, 0, waterDistMapW_ - 1);
    int gz = Clamp((int)lz, 0, waterDistMapH_ - 1);

    return waterDistMap_[gz * waterDistMapW_ + gx];
}

// ============================================================================
// Trade System — Client UI
// ============================================================================

void TerrainNode::SendTradeRequest(unsigned targetNodeId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    buf.WriteU32(targetNodeId);
    serverConn->SendMessage(MSG_TRADE_REQUEST, true, true, buf);
    tradePending_ = true;
    URHO3D_LOGINFOF("Sent trade request to NPC node %u", targetNodeId);
}

void TerrainNode::SendTradeOffer(int itemId, int qty, bool adding)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    buf.WriteI32(itemId);
    buf.WriteI32(qty);
    buf.WriteBool(adding);
    serverConn->SendMessage(MSG_TRADE_OFFER, true, true, buf);
}

void TerrainNode::SendTradeLock()
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    serverConn->SendMessage(MSG_TRADE_LOCK, true, true, buf);
}

void TerrainNode::SendTradeCancel()
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    serverConn->SendMessage(MSG_TRADE_CANCEL, true, true, buf);
}

void TerrainNode::SendTradeAccept()
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    serverConn->SendMessage(MSG_TRADE_ACCEPT, true, true, buf);
}

void TerrainNode::SendTradeReject()
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    serverConn->SendMessage(MSG_TRADE_REJECT, true, true, buf);
}

void TerrainNode::HandleTradeIncoming(MemoryBuffer& msg)
{
    tradeIncomingPlayerId_ = msg.ReadI32();
    tradeIncomingName_ = msg.ReadString();
    tradeIncomingPending_ = true;

    // Show accept/reject prompt
    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

    if (tradePromptWindow_)
        tradePromptWindow_->Remove();

    tradePromptWindow_ = new Window(context_);
    ui->GetRoot()->AddChild(tradePromptWindow_);
    tradePromptWindow_->SetStyleAuto(style);
    tradePromptWindow_->SetSize(320, 80);
    tradePromptWindow_->SetAlignment(HA_CENTER, VA_CENTER);
    tradePromptWindow_->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));

    auto* promptText = tradePromptWindow_->CreateChild<Text>();
    promptText->SetText(tradeIncomingName_ + " wants to trade.");
    promptText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 14);
    promptText->SetColor(Color::WHITE);
    promptText->SetTextAlignment(HA_CENTER);

    auto* hintText = tradePromptWindow_->CreateChild<Text>();
    hintText->SetText("Y = Accept    N = Reject");
    hintText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);
    hintText->SetColor(Color(0.7f, 0.9f, 0.7f));
    hintText->SetTextAlignment(HA_CENTER);

    URHO3D_LOGINFOF("Trade request from %s (playerId %d)", tradeIncomingName_.CString(), tradeIncomingPlayerId_);
}

void TerrainNode::HandleTradeAccepted(MemoryBuffer& msg)
{
    bool accepted = msg.ReadBool();
    (void)accepted;

    tradePending_ = false;
    tradeOpen_ = true;
    tradeLocked_ = false;
    tradePartnerLocked_ = false;
    myTradeOffer_.Clear();
    theirTradeOffer_.Clear();

    // Close the incoming prompt if it was still showing
    if (tradePromptWindow_)
    {
        tradePromptWindow_->Remove();
        tradePromptWindow_ = nullptr;
    }
    tradeIncomingPending_ = false;

    if (!tradeWindow_)
        CreateTradeUI();

    tradeWindow_->SetVisible(true);
    RefreshTradeOffers();

    // Free cursor for UI interaction
    auto* input = GetSubsystem<Input>();
    input->SetMouseMode(MM_FREE);
    input->SetMouseVisible(true);

    URHO3D_LOGINFO("Trade session opened");
}

void TerrainNode::HandleTradeUpdate(MemoryBuffer& msg)
{
    int count = msg.ReadI32();
    theirTradeOffer_.Clear();
    for (int i = 0; i < count; ++i)
    {
        TradeOfferItem item;
        item.itemId = msg.ReadI32();
        item.quantity = msg.ReadI32();
        // Look up name from GameDB
        if (gameDB_)
        {
            ItemInfo info;
            if (gameDB_->GetItem(item.itemId, info))
                item.itemName = info.name;
            else
                item.itemName = "Item #" + String(item.itemId);
        }
        else
            item.itemName = "Item #" + String(item.itemId);
        theirTradeOffer_.Push(item);
    }
    RefreshTradeOffers();
}

void TerrainNode::HandleTradeLock(MemoryBuffer& msg)
{
    bool locked = msg.ReadBool();
    if (!locked)
    {
        // Partner's offer changed after we locked — unlock us too
        tradeLocked_ = false;
        tradePartnerLocked_ = false;
        if (tradeStatusText_)
            tradeStatusText_->SetText("Partner modified offer — unlocked");
    }
    else
    {
        tradePartnerLocked_ = true;
        if (tradeStatusText_)
            tradeStatusText_->SetText("Partner locked in");
    }
    RefreshTradeOffers();
}

void TerrainNode::HandleTradeComplete(MemoryBuffer& msg)
{
    bool success = msg.ReadBool();
    (void)success;

    URHO3D_LOGINFO("Trade completed successfully!");
    if (tradeStatusText_)
        tradeStatusText_->SetText("Trade complete!");

    CloseTradeWindow();
}

void TerrainNode::HandleTradeCancel(MemoryBuffer& msg)
{
    String reason = msg.ReadString();
    URHO3D_LOGINFOF("Trade cancelled: %s", reason.CString());

    CloseTradeWindow();

    // Also dismiss the incoming prompt if pending
    if (tradePromptWindow_)
    {
        tradePromptWindow_->Remove();
        tradePromptWindow_ = nullptr;
    }
    tradeIncomingPending_ = false;
    tradePending_ = false;
}

void TerrainNode::CreateTradeUI()
{
    auto* ui = GetSubsystem<UI>();
    auto* cache = GetSubsystem<ResourceCache>();
    auto* style = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    tradeWindow_ = new Window(context_);
    ui->GetRoot()->AddChild(tradeWindow_);
    tradeWindow_->SetStyleAuto(style);
    tradeWindow_->SetSize(520, 440);
    tradeWindow_->SetAlignment(HA_CENTER, VA_CENTER);
    tradeWindow_->SetLayout(LM_VERTICAL, 4, IntRect(8, 8, 8, 8));
    tradeWindow_->SetVisible(false);

    // Title
    auto* title = tradeWindow_->CreateChild<Text>();
    title->SetText("Trade");
    title->SetFont(font, 16);
    title->SetColor(Color(1.0f, 0.9f, 0.6f));
    title->SetTextAlignment(HA_CENTER);

    // Split layout: left (my offer) | right (their offer)
    auto* splitRow = tradeWindow_->CreateChild<UIElement>();
    splitRow->SetLayout(LM_HORIZONTAL, 8);
    splitRow->SetMinHeight(200);

    // --- My offer panel ---
    auto* myPanel = splitRow->CreateChild<UIElement>();
    myPanel->SetLayout(LM_VERTICAL, 2);
    myPanel->SetMinWidth(240);

    auto* myLabel = myPanel->CreateChild<Text>();
    myLabel->SetText("Your Offer");
    myLabel->SetFont(font, 13);
    myLabel->SetColor(Color(0.7f, 0.9f, 0.7f));

    auto* myGrid = myPanel->CreateChild<UIElement>();
    myGrid->SetLayout(LM_VERTICAL, 2);
    tradeMyOfferSlots_.Clear();
    for (int i = 0; i < 6; ++i)
    {
        auto* btn = myGrid->CreateChild<Button>();
        btn->SetStyleAuto(style);
        btn->SetMinSize(220, 28);
        btn->SetVar("TradeSlotIndex", i);
        btn->SetVar("TradeSlotSide", 0);  // 0 = my side

        auto* label = btn->CreateChild<Text>();
        label->SetFont(font, 11);
        label->SetText("(empty)");
        label->SetColor(Color(0.5f, 0.5f, 0.5f));

        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleTradeOfferSlotClick));
        tradeMyOfferSlots_.Push(btn);
    }

    // --- Their offer panel ---
    auto* theirPanel = splitRow->CreateChild<UIElement>();
    theirPanel->SetLayout(LM_VERTICAL, 2);
    theirPanel->SetMinWidth(240);

    auto* theirLabel = theirPanel->CreateChild<Text>();
    theirLabel->SetText("Their Offer");
    theirLabel->SetFont(font, 13);
    theirLabel->SetColor(Color(0.9f, 0.7f, 0.7f));

    auto* theirGrid = theirPanel->CreateChild<UIElement>();
    theirGrid->SetLayout(LM_VERTICAL, 2);
    tradeTheirOfferSlots_.Clear();
    for (int i = 0; i < 6; ++i)
    {
        auto* btn = theirGrid->CreateChild<Button>();
        btn->SetStyleAuto(style);
        btn->SetMinSize(220, 28);

        auto* label = btn->CreateChild<Text>();
        label->SetFont(font, 11);
        label->SetText("(empty)");
        label->SetColor(Color(0.5f, 0.5f, 0.5f));

        tradeTheirOfferSlots_.Push(btn);
    }

    // --- Inventory bag for drag-to-offer ---
    auto* bagLabel = tradeWindow_->CreateChild<Text>();
    bagLabel->SetText("Your Bag (click to offer)");
    bagLabel->SetFont(font, 12);
    bagLabel->SetColor(Color(0.8f, 0.8f, 0.8f));

    auto* bagRow = tradeWindow_->CreateChild<UIElement>("TradeBagRow");
    bagRow->SetLayout(LM_HORIZONTAL, 2);
    bagRow->SetMinHeight(32);

    // Show up to 10 bag items as clickable buttons
    for (int i = 0; i < 10; ++i)
    {
        auto* btn = bagRow->CreateChild<Button>();
        btn->SetStyleAuto(style);
        btn->SetMinSize(48, 28);
        btn->SetVar("TradeBagIndex", i);

        auto* label = btn->CreateChild<Text>();
        label->SetFont(font, 9);
        label->SetText("");

        SubscribeToEvent(btn, E_RELEASED, URHO3D_HANDLER(TerrainNode, HandleTradeBagSlotClick));
    }

    // --- Buttons row ---
    auto* btnRow = tradeWindow_->CreateChild<UIElement>();
    btnRow->SetLayout(LM_HORIZONTAL, 8);
    btnRow->SetMinHeight(30);

    tradeLockBtn_ = btnRow->CreateChild<Button>();
    tradeLockBtn_->SetStyleAuto(style);
    tradeLockBtn_->SetMinSize(100, 28);
    auto* lockLabel = tradeLockBtn_->CreateChild<Text>();
    lockLabel->SetFont(font, 12);
    lockLabel->SetText("Lock In");
    lockLabel->SetColor(Color::WHITE);
    SubscribeToEvent(tradeLockBtn_, E_RELEASED, [this](StringHash, VariantMap&) {
        if (!tradeLocked_)
        {
            tradeLocked_ = true;
            SendTradeLock();
            if (tradeStatusText_)
                tradeStatusText_->SetText("Locked — waiting for partner...");
            RefreshTradeOffers();
        }
    });

    tradeCancelBtn_ = btnRow->CreateChild<Button>();
    tradeCancelBtn_->SetStyleAuto(style);
    tradeCancelBtn_->SetMinSize(100, 28);
    auto* cancelLabel = tradeCancelBtn_->CreateChild<Text>();
    cancelLabel->SetFont(font, 12);
    cancelLabel->SetText("Cancel");
    cancelLabel->SetColor(Color(1.0f, 0.4f, 0.4f));
    SubscribeToEvent(tradeCancelBtn_, E_RELEASED, [this](StringHash, VariantMap&) {
        SendTradeCancel();
        CloseTradeWindow();
    });

    // Status text
    tradeStatusText_ = tradeWindow_->CreateChild<Text>();
    tradeStatusText_->SetFont(font, 11);
    tradeStatusText_->SetColor(Color(0.8f, 0.8f, 0.6f));
    tradeStatusText_->SetText("");
}

void TerrainNode::RefreshTradeOffers()
{
    if (!tradeWindow_ || !tradeWindow_->IsVisible())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

    // Refresh my offer slots
    for (int i = 0; i < 6; ++i)
    {
        auto* label = tradeMyOfferSlots_[i]->GetChildStaticCast<Text>(0);
        if (!label) continue;

        if (i < (int)myTradeOffer_.Size())
        {
            String text = myTradeOffer_[i].itemName;
            if (myTradeOffer_[i].quantity > 1)
                text += " x" + String(myTradeOffer_[i].quantity);
            label->SetText(text);
            label->SetColor(tradeLocked_ ? Color(0.6f, 0.6f, 0.6f) : Color::WHITE);
        }
        else
        {
            label->SetText("(empty)");
            label->SetColor(Color(0.5f, 0.5f, 0.5f));
        }
    }

    // Refresh their offer slots
    for (int i = 0; i < 6; ++i)
    {
        auto* label = tradeTheirOfferSlots_[i]->GetChildStaticCast<Text>(0);
        if (!label) continue;

        if (i < (int)theirTradeOffer_.Size())
        {
            String text = theirTradeOffer_[i].itemName;
            if (theirTradeOffer_[i].quantity > 1)
                text += " x" + String(theirTradeOffer_[i].quantity);
            label->SetText(text);
            label->SetColor(tradePartnerLocked_ ? Color(0.6f, 0.8f, 0.6f) : Color::WHITE);
        }
        else
        {
            label->SetText("(empty)");
            label->SetColor(Color(0.5f, 0.5f, 0.5f));
        }
    }

    // Refresh bag slots in trade window (named child for reliable lookup)
    auto* bagRow = tradeWindow_->GetChild("TradeBagRow", false);
    if (bagRow)
    {
        unsigned bagIdx = 0;
        for (unsigned i = 0; i < inventory_.Size() && bagIdx < 10; ++i)
        {
            if (inventory_[i].slotType != "bag")
                continue;

            auto* btn = bagRow->GetChild(bagIdx);
            if (btn)
            {
                auto* label = static_cast<Text*>(btn->GetChild(0u));
                if (label)
                {
                    String text = inventory_[i].itemName;
                    if (inventory_[i].quantity > 1)
                        text += " x" + String(inventory_[i].quantity);
                    label->SetText(text);
                    label->SetColor(Color(0.8f, 0.9f, 0.8f));
                }
            }
            ++bagIdx;
        }
        // Clear remaining slots
        for (; bagIdx < 10; ++bagIdx)
        {
            auto* btn = bagRow->GetChild(bagIdx);
            if (btn)
            {
                auto* label = static_cast<Text*>(btn->GetChild(0u));
                if (label)
                    label->SetText("");
            }
        }
    }

    // Update lock button state
    if (tradeLockBtn_)
    {
        auto* lockLabel = tradeLockBtn_->GetChildStaticCast<Text>(0);
        if (lockLabel)
        {
            if (tradeLocked_)
            {
                lockLabel->SetText("Locked");
                lockLabel->SetColor(Color(0.5f, 0.5f, 0.5f));
            }
            else
            {
                lockLabel->SetText("Lock In");
                lockLabel->SetColor(Color::WHITE);
            }
        }
    }
}

void TerrainNode::CloseTradeWindow()
{
    tradeOpen_ = false;
    tradePending_ = false;
    tradeLocked_ = false;
    tradePartnerLocked_ = false;
    tradePartnerNodeId_ = 0;
    tradeProximityCheckTimer_ = 0.0f;
    myTradeOffer_.Clear();
    theirTradeOffer_.Clear();

    if (tradeWindow_)
        tradeWindow_->SetVisible(false);

    if (tradeStatusText_)
        tradeStatusText_->SetText("");
}

void TerrainNode::HandleTradeOfferSlotClick(StringHash /*eventType*/, VariantMap& eventData)
{
    // Click on my offer slot → remove item from offer
    if (tradeLocked_)
        return;

    using namespace Released;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn)
        return;

    int slotIndex = btn->GetVar("TradeSlotIndex").GetI32();
    int side = btn->GetVar("TradeSlotSide").GetI32();

    if (side != 0)
        return;  // can only modify our own side

    if (slotIndex < 0 || slotIndex >= (int)myTradeOffer_.Size())
        return;

    // Remove this item from offer
    TradeOfferItem& item = myTradeOffer_[slotIndex];
    SendTradeOffer(item.itemId, item.quantity, false);
    myTradeOffer_.Erase(slotIndex);
    RefreshTradeOffers();
}

void TerrainNode::HandleTradeBagSlotClick(StringHash /*eventType*/, VariantMap& eventData)
{
    // Click on bag slot → add 1 of that item to our offer
    if (tradeLocked_)
        return;
    if (myTradeOffer_.Size() >= 6)
        return;  // max 6 distinct items

    using namespace Released;
    auto* btn = static_cast<Button*>(eventData[P_ELEMENT].GetPtr());
    if (!btn)
        return;

    int bagIdx = btn->GetVar("TradeBagIndex").GetI32();

    // Map to actual inventory bag items
    unsigned realIdx = 0;
    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType != "bag")
            continue;
        if ((int)realIdx == bagIdx)
        {
            int itemId = inventory_[i].itemId;
            int qty = 1;

            // Check if already in offer — increment quantity
            bool found = false;
            for (unsigned j = 0; j < myTradeOffer_.Size(); ++j)
            {
                if (myTradeOffer_[j].itemId == itemId)
                {
                    myTradeOffer_[j].quantity++;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                TradeOfferItem offer;
                offer.itemId = itemId;
                offer.quantity = qty;
                offer.itemName = inventory_[i].itemName;
                myTradeOffer_.Push(offer);
            }

            SendTradeOffer(itemId, qty, true);
            RefreshTradeOffers();
            return;
        }
        ++realIdx;
    }
}

void TerrainNode::SendChopTree(unsigned treeId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
        return;

    VectorBuffer buf;
    buf.WriteU32(treeId);
    serverConn->SendMessage(MSG_CHOP_TREE, true, true, buf);
}

void TerrainNode::SendTapTree(unsigned treeId)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Tap tree %u (offline — no server)", treeId);
        return;
    }

    VectorBuffer buf;
    buf.WriteU32(treeId);
    serverConn->SendMessage(MSG_TAP_TREE, true, true, buf);
    URHO3D_LOGINFOF("Sent MSG_TAP_TREE: treeId=%u", treeId);
}

void TerrainNode::SendMineRequest(Node* outcropNode)
{
    if (!outcropNode)
        return;

    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Mine outcrop (offline — no server)");
        return;
    }

    // Server expects world position (float x, float z)
    Vector3 pos = outcropNode->GetWorldPosition();
    VectorBuffer buf;
    buf.WriteFloat(pos.x_);
    buf.WriteFloat(pos.z_);
    serverConn->SendMessage(MSG_MINE_REQUEST, true, true, buf);
    URHO3D_LOGINFOF("Sent MSG_MINE_REQUEST at (%.1f, %.1f)", pos.x_, pos.z_);
}

void TerrainNode::UpdateArrowHUD()
{
    // Check if bow is equipped in hand slot
    bool bowEquipped = false;
    int arrowCount = 0;

    for (unsigned i = 0; i < inventory_.Size(); ++i)
    {
        if (inventory_[i].slotType == "hand" && inventory_[i].itemId == 201)
            bowEquipped = true;
        if (inventory_[i].itemId == 202)
            arrowCount += inventory_[i].quantity;
    }

    if (!bowEquipped)
    {
        if (arrowCountText_ && arrowCountText_->IsVisible())
            arrowCountText_->SetVisible(false);
        return;
    }

    // Create text element if needed
    if (!arrowCountText_)
    {
        auto* ui = GetSubsystem<UI>();

        auto* text = ui->GetRoot()->CreateChild<Text>("ArrowCount");
        text->SetFont(font_, currentFontSize_);
        text->SetTextAlignment(HA_CENTER);
        text->SetAlignment(HA_CENTER, VA_CENTER);
        text->SetPosition(0, 30);  // just below center
        arrowCountText_ = text;
    }

    arrowCountText_->SetVisible(true);
    arrowCountText_->SetText("Arrows: " + String(arrowCount));
    arrowCountText_->SetColor(arrowCount < 5 ? Color(1.0f, 0.3f, 0.3f) : Color(0.9f, 0.9f, 0.9f));
}

// =============================================================================
// Settlement Patch Ownership
// =============================================================================

void TerrainNode::HandleSettlementClaims(MemoryBuffer& msg)
{
    unsigned short count = msg.ReadU16();
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned char sx = msg.ReadU8();
        unsigned char sz = msg.ReadU8();
        unsigned short sid = msg.ReadU16();
        unsigned long long key = ((unsigned long long)sx << 16) | sz;
        patchClaims_[key] = sid;
    }
    URHO3D_LOGINFOF("[Settlement] Received %u patch claims", count);

    if (showTerritoryOverlay_)
        UpdateTerritoryOverlay();
}

void TerrainNode::SendPatchClaim(int patchX, int patchZ)
{
    auto* network = GetSubsystem<Network>();
    auto* serverConn = network ? network->GetServerConnection() : nullptr;
    if (!serverConn)
    {
        URHO3D_LOGINFOF("Patch claim (%d,%d) — offline, no server", patchX, patchZ);
        return;
    }

    VectorBuffer buf;
    buf.WriteI32(patchX);
    buf.WriteI32(patchZ);
    serverConn->SendMessage(MSG_PATCH_CLAIM, true, true, buf);
    URHO3D_LOGINFOF("Sent MSG_PATCH_CLAIM: patch (%d,%d)", patchX, patchZ);
}

void TerrainNode::HandlePatchResult(MemoryBuffer& msg)
{
    bool success = msg.ReadBool();
    String message = msg.ReadString();

    URHO3D_LOGINFOF("[PatchClaim] %s: %s", success ? "OK" : "DENIED", message.CString());

    if (hud_)
        hud_->SetContextHint(success ? "Land claimed!" : message);
}

Color TerrainNode::SettlementColor(unsigned settlementId)
{
    // Hash settlement ID to a hue — deterministic, distinct colors per settlement
    float hue = fmod((float)(settlementId * 137) / 256.0f, 1.0f);
    float s = 0.6f, v = 0.8f;
    // HSV to RGB
    int hi = (int)(hue * 6.0f);
    float f = hue * 6.0f - hi;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float r, g, b;
    switch (hi % 6)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return Color(r, g, b, 0.15f);  // 15% alpha
}

void TerrainNode::UpdateTerritoryOverlay()
{
    // Remove old overlays
    for (unsigned i = 0; i < patchOverlayNodes_.Size(); ++i)
    {
        Node* n = patchOverlayNodes_[i].Get();
        if (n) n->Remove();
    }
    patchOverlayNodes_.Clear();

    if (!showTerritoryOverlay_ || patchClaims_.Empty())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* planeMdl = cache->GetResource<Model>("Models/Plane.mdl");
    if (!planeMdl)
        return;

    // Settlement patch: 128×128 world units, terrain centered at origin
    // Terrain half size: (1025-1) * 2.0 / 2 = 1024
    static const float TERRAIN_HALF = 1024.0f;
    static const float SPATCH_SIZE = 128.0f;

    for (auto it = patchClaims_.Begin(); it != patchClaims_.End(); ++it)
    {
        int sx = (int)(it->first_ >> 16);
        int sz = (int)(it->first_ & 0xFFFF);
        unsigned sid = it->second_;

        // Center of this settlement patch in world coords
        float cx = -TERRAIN_HALF + (sx + 0.5f) * SPATCH_SIZE;
        float cz = -TERRAIN_HALF + (sz + 0.5f) * SPATCH_SIZE;
        float cy = terrain_ ? terrain_->GetHeight(Vector3(cx, 0.0f, cz)) : 0.0f;

        Node* overlayNode = scene_->CreateTemporaryChild("PatchOverlay", LOCAL);
        overlayNode->SetPosition(Vector3(cx, cy + 0.2f, cz));
        overlayNode->SetScale(Vector3(SPATCH_SIZE, 1.0f, SPATCH_SIZE));

        auto* sm = overlayNode->CreateComponent<StaticModel>();
        sm->SetModel(planeMdl);
        sm->SetCastShadows(false);

        // Per-settlement color material
        auto* baseMat = cache->GetResource<Material>("Materials/VColUnlit.xml");
        if (baseMat)
        {
            SharedPtr<Material> mat(baseMat->Clone());
            mat->SetShaderParameter("MatDiffColor", SettlementColor(sid));
            sm->SetMaterial(mat);
        }

        patchOverlayNodes_.Push(WeakPtr<Node>(overlayNode));
    }

    URHO3D_LOGINFOF("[Settlement] Created %u territory overlay quads", patchOverlayNodes_.Size());
}

// ============================================================================
// Decoration Scatter — static Nature/ models placed by biome, streamed by camera
// ============================================================================

void TerrainNode::InitDecorModels()
{
    if (decorModelsReady_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    struct DecorDef
    {
        const char* modelPath;
        const char* matPath;
        float scale;
    };

    // Bushes — grassland and forest
    static const DecorDef bushDefs[] = {
        { "Models/Nature/Bush_1.mdl",              "Models/Nature/Materials/DarkGreen.xml",  0.6f },
        { "Models/Nature/Bush_2.mdl",              "Models/Nature/Materials/DarkGreen.xml",  0.6f },
        { "Models/Nature/Bush_Common.mdl",         "Models/Nature/Materials/Green.xml",      0.5f },
        { "Models/Nature/Bush_Common_Flowers.mdl", "Models/Nature/Materials/Green.xml",      0.5f },
        { "Models/Nature/BushBerries_1.mdl",       "Models/Nature/Materials/Berry.xml",      0.4f },
        { "Models/Nature/BushBerries_2.mdl",       "Models/Nature/Materials/Berry.xml",      0.4f },
        { "Models/Nature/plant_bush.mdl",          "Models/Nature/Materials/leafsGreen.xml", 0.5f },
        { "Models/Nature/plant_bushLarge.mdl",     "Models/Nature/Materials/leafsGreen.xml", 0.4f },
    };

    // Flowers — grassland
    static const DecorDef flowerDefs[] = {
        { "Models/Nature/Flower_3_Single.mdl",  "Models/Nature/Materials/Flowers.xml",   0.3f },
        { "Models/Nature/Flower_3_Group.mdl",   "Models/Nature/Materials/Flowers.xml",   0.3f },
        { "Models/Nature/Flower_4_Single.mdl",  "Models/Nature/Materials/Flowers.xml",   0.3f },
        { "Models/Nature/Flower_4_Group.mdl",   "Models/Nature/Materials/Flowers.xml",   0.3f },
        { "Models/Nature/flower_redA.mdl",      "Models/Nature/Materials/Pink.xml",      0.3f },
        { "Models/Nature/flower_redB.mdl",      "Models/Nature/Materials/Pink.xml",      0.3f },
        { "Models/Nature/flower_purpleA.mdl",   "Models/Nature/Materials/colorPurple.xml", 0.3f },
        { "Models/Nature/flower_purpleB.mdl",   "Models/Nature/Materials/colorPurple.xml", 0.3f },
        { "Models/Nature/flower_yellowA.mdl",   "Models/Nature/Materials/Yellow.xml",    0.3f },
        { "Models/Nature/flower_yellowB.mdl",   "Models/Nature/Materials/Yellow.xml",    0.3f },
        { "Models/Nature/Flowers.mdl",          "Models/Nature/Materials/Flowers.xml",   0.3f },
        { "Models/Nature/Clover_1.mdl",         "Models/Nature/Materials/leafsGreen.xml",0.25f },
        { "Models/Nature/Clover_2.mdl",         "Models/Nature/Materials/leafsGreen.xml",0.25f },
    };

    // Mushrooms — forest
    static const DecorDef mushroomDefs[] = {
        { "Models/Nature/Mushroom_Common.mdl",     "Models/Nature/Materials/Mushrooms.xml",    0.3f },
        { "Models/Nature/Mushroom_Laetiporus.mdl", "Models/Nature/Materials/Orange.xml",       0.3f },
        { "Models/Nature/mushroom_red.mdl",        "Models/Nature/Materials/Mushroom_Top.xml", 0.25f },
        { "Models/Nature/mushroom_redTall.mdl",    "Models/Nature/Materials/Mushroom_Top.xml", 0.25f },
        { "Models/Nature/mushroom_tan.mdl",        "Models/Nature/Materials/Mushroom_Top.xml", 0.25f },
        { "Models/Nature/mushroom_tanTall.mdl",    "Models/Nature/Materials/Mushroom_Top.xml", 0.25f },
        { "Models/Nature/mushroom_redGroup.mdl",   "Models/Nature/Materials/Mushroom_Top.xml", 0.25f },
        { "Models/Nature/mushroom_tanGroup.mdl",   "Models/Nature/Materials/Mushroom_Top.xml", 0.25f },
    };

    // Stumps and logs — any biome
    static const DecorDef stumpDefs[] = {
        { "Models/Nature/stump_round.mdl",         "Models/Nature/Materials/woodBark.xml", 0.4f },
        { "Models/Nature/stump_roundDetailed.mdl", "Models/Nature/Materials/woodBark.xml", 0.4f },
        { "Models/Nature/stump_old.mdl",           "Models/Nature/Materials/woodBark.xml", 0.35f },
        { "Models/Nature/stump_oldTall.mdl",       "Models/Nature/Materials/woodBark.xml", 0.35f },
        { "Models/Nature/TreeStump.mdl",           "Models/Nature/Materials/Wood.xml",     0.4f },
        { "Models/Nature/TreeStump_Moss.mdl",      "Models/Nature/Materials/Wood.xml",     0.4f },
        { "Models/Nature/TreeStump_Snow.mdl",      "Models/Nature/Materials/Wood.xml",     0.4f },
        { "Models/Nature/log.mdl",                 "Models/Nature/Materials/woodBark.xml", 0.5f },
        { "Models/Nature/log_large.mdl",           "Models/Nature/Materials/woodBark.xml", 0.4f },
        { "Models/Nature/WoodLog.mdl",             "Models/Nature/Materials/Wood.xml",     0.4f },
        { "Models/Nature/WoodLog_Moss.mdl",        "Models/Nature/Materials/Wood.xml",     0.4f },
        { "Models/Nature/WoodLog_Snow.mdl",        "Models/Nature/Materials/Wood.xml",     0.4f },
    };

    // Rocks — mountain and scattered
    static const DecorDef rockDefs[] = {
        { "Models/Nature/Rock_1.mdl",        "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_2.mdl",        "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_3.mdl",        "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_4.mdl",        "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_5.mdl",        "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_6.mdl",        "Models/Nature/Materials/Rock.xml",  0.45f },
        { "Models/Nature/Rock_7.mdl",        "Models/Nature/Materials/Rock.xml",  0.45f },
        { "Models/Nature/Rock_Moss_1.mdl",   "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_Moss_2.mdl",   "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_Moss_3.mdl",   "Models/Nature/Materials/Rock.xml",  0.5f },
        { "Models/Nature/Rock_Moss_4.mdl",   "Models/Nature/Materials/Rock.xml",  0.45f },
        { "Models/Nature/Rock_Moss_5.mdl",   "Models/Nature/Materials/Rock.xml",  0.45f },
        { "Models/Nature/Rock_Medium_1.mdl", "Models/Nature/Materials/Rock.xml",  0.4f },
        { "Models/Nature/Pebble_Round_1.mdl","Models/Nature/Materials/Rock.xml",  0.3f },
        { "Models/Nature/Pebble_Round_4.mdl","Models/Nature/Materials/Rock.xml",  0.3f },
    };

    // Ferns — forest and wetland
    static const DecorDef fernDefs[] = {
        { "Models/Nature/Fern_1.mdl",           "Models/Nature/Materials/leafsGreen.xml", 0.4f },
        { "Models/Nature/fern_02.mdl",          "Models/Nature/Materials/fern_02.xml",    0.4f },
        { "Models/Nature/hanging_moss.mdl",     "Models/Nature/Materials/leafsDark.xml",  0.35f },
    };

    // Tall grass patches — grassland and wetland
    static const DecorDef grassDefs[] = {
        { "Models/Nature/Grass.mdl",             "Models/Nature/Materials/Grass.xml",       0.4f },
        { "Models/Nature/Grass_2.mdl",           "Models/Nature/Materials/Grass.xml",       0.4f },
        { "Models/Nature/Grass_Short.mdl",       "Models/Nature/Materials/Grass.xml",       0.35f },
        { "Models/Nature/Grass_Common_Tall.mdl", "Models/Nature/Materials/Grass.xml",       0.45f },
        { "Models/Nature/grass_large.mdl",       "Models/Nature/Materials/grass.xml",       0.4f },
        { "Models/Nature/grass_leafsLarge.mdl",  "Models/Nature/Materials/leafsGreen.xml",  0.35f },
        { "Models/Nature/grass_leafs.mdl",       "Models/Nature/Materials/leafsGreen.xml",  0.3f },
    };

    // Ground plants — small vegetation scattered everywhere
    static const DecorDef groundPlantDefs[] = {
        { "Models/Nature/Plant_1.mdl",           "Models/Nature/Materials/Green.xml",       0.3f },
        { "Models/Nature/Plant_2.mdl",           "Models/Nature/Materials/Green.xml",       0.3f },
        { "Models/Nature/Plant_3.mdl",           "Models/Nature/Materials/Green.xml",       0.3f },
        { "Models/Nature/Plant_4.mdl",           "Models/Nature/Materials/Green.xml",       0.3f },
        { "Models/Nature/Plant_5.mdl",           "Models/Nature/Materials/Green.xml",       0.3f },
        { "Models/Nature/plant_flatShort.mdl",   "Models/Nature/Materials/leafsGreen.xml",  0.3f },
        { "Models/Nature/plant_flatTall.mdl",    "Models/Nature/Materials/leafsGreen.xml",  0.3f },
        { "Models/Nature/plant_bushSmall.mdl",   "Models/Nature/Materials/leafsGreen.xml",  0.25f },
        { "Models/Nature/plant_bushTriangle.mdl","Models/Nature/Materials/leafsGreen.xml",  0.3f },
        { "Models/Nature/plant_bushDetailed.mdl","Models/Nature/Materials/leafsGreen.xml",  0.3f },
    };

    struct CategoryDefs
    {
        const DecorDef* defs;
        unsigned count;
    };
    CategoryDefs categories[NUM_DECOR_CATEGORIES] = {
        { bushDefs,        sizeof(bushDefs) / sizeof(bushDefs[0]) },
        { flowerDefs,      sizeof(flowerDefs) / sizeof(flowerDefs[0]) },
        { mushroomDefs,    sizeof(mushroomDefs) / sizeof(mushroomDefs[0]) },
        { stumpDefs,       sizeof(stumpDefs) / sizeof(stumpDefs[0]) },
        { rockDefs,        sizeof(rockDefs) / sizeof(rockDefs[0]) },
        { fernDefs,        sizeof(fernDefs) / sizeof(fernDefs[0]) },
        { grassDefs,       sizeof(grassDefs) / sizeof(grassDefs[0]) },
        { groundPlantDefs, sizeof(groundPlantDefs) / sizeof(groundPlantDefs[0]) },
        { nullptr, 0 },  // DECOR_SAPLING — loaded separately with material lists
        { nullptr, 0 },  // DECOR_CACTUS — loaded separately with material lists
        { nullptr, 0 },  // DECOR_PALM — loaded separately with material lists
    };

    unsigned totalLoaded = 0;
    for (int cat = 0; cat < NUM_DECOR_CATEGORIES; ++cat)
    {
        const CategoryDefs& cd = categories[cat];
        for (unsigned i = 0; i < cd.count; ++i)
        {
            auto* mdl = cache->GetResource<Model>(cd.defs[i].modelPath, false);
            if (!mdl)
                continue;
            DecorEntry entry;
            entry.model = mdl;
            entry.material = cache->GetResource<Material>(cd.defs[i].matPath, false);
            entry.baseScale = cd.defs[i].scale;
            decorModels_[cat].Push(entry);
            ++totalLoaded;
        }
    }

    // Saplings — KayKit small trees with multi-material (use .txt material lists)
    struct SaplingDef { const char* modelPath; const char* matListPath; float scale; };
    static const SaplingDef saplingDefs[] = {
        { "Models/Nature/Pine_1.mdl",   "Models/Nature/Pine_1.txt",   0.5f },
        { "Models/Nature/Pine_2.mdl",   "Models/Nature/Pine_2.txt",   0.5f },
        { "Models/Nature/Pine_3.mdl",   "Models/Nature/Pine_3.txt",   0.45f },
        { "Models/Nature/Pine_4.mdl",   "Models/Nature/Pine_4.txt",   0.45f },
        { "Models/Nature/Pine_5.mdl",   "Models/Nature/Pine_5.txt",   0.5f },
        { "Models/Nature/Willow_1.mdl", "Models/Nature/Willow_1.txt", 0.4f },
        { "Models/Nature/Willow_2.mdl", "Models/Nature/Willow_2.txt", 0.4f },
        { "Models/Nature/Willow_3.mdl", "Models/Nature/Willow_3.txt", 0.4f },
    };
    for (unsigned i = 0; i < sizeof(saplingDefs) / sizeof(saplingDefs[0]); ++i)
    {
        auto* mdl = cache->GetResource<Model>(saplingDefs[i].modelPath, false);
        if (!mdl)
            continue;
        DecorEntry entry;
        entry.model = mdl;
        entry.matListPath = saplingDefs[i].matListPath;
        entry.baseScale = saplingDefs[i].scale;
        decorModels_[DECOR_SAPLING].Push(entry);
        ++totalLoaded;
    }

    // Cacti — desert/barren biome (multi-material via .txt)
    static const SaplingDef cactusDefs[] = {
        { "Models/Nature/Cactus_1.mdl",        "Models/Nature/Cactus_1.txt",        0.5f },
        { "Models/Nature/Cactus_2.mdl",        "Models/Nature/Cactus_2.txt",        0.5f },
        { "Models/Nature/Cactus_3.mdl",        "Models/Nature/Cactus_3.txt",        0.45f },
        { "Models/Nature/Cactus_4.mdl",        "Models/Nature/Cactus_4.txt",        0.45f },
        { "Models/Nature/Cactus_5.mdl",        "Models/Nature/Cactus_5.txt",        0.5f },
        { "Models/Nature/CactusFlower_1.mdl",  "Models/Nature/CactusFlower_1.txt",  0.45f },
        { "Models/Nature/CactusFlowers_2.mdl", "Models/Nature/CactusFlowers_2.txt", 0.45f },
        { "Models/Nature/CactusFlowers_3.mdl", "Models/Nature/CactusFlowers_3.txt", 0.4f },
        { "Models/Nature/CactusFlowers_4.mdl", "Models/Nature/CactusFlowers_4.txt", 0.4f },
        { "Models/Nature/CactusFlowers_5.mdl", "Models/Nature/CactusFlowers_5.txt", 0.45f },
    };
    for (unsigned i = 0; i < sizeof(cactusDefs) / sizeof(cactusDefs[0]); ++i)
    {
        auto* mdl = cache->GetResource<Model>(cactusDefs[i].modelPath, false);
        if (!mdl) continue;
        DecorEntry entry;
        entry.model = mdl;
        entry.matListPath = cactusDefs[i].matListPath;
        entry.baseScale = cactusDefs[i].scale;
        decorModels_[DECOR_CACTUS].Push(entry);
        ++totalLoaded;
    }

    // Palm trees — desert/arid edges (multi-material via .txt)
    static const SaplingDef palmDefs[] = {
        { "Models/Nature/PalmTree_1.mdl", "Models/Nature/PalmTree_1.txt", 0.6f },
        { "Models/Nature/PalmTree_2.mdl", "Models/Nature/PalmTree_2.txt", 0.6f },
        { "Models/Nature/PalmTree_3.mdl", "Models/Nature/PalmTree_3.txt", 0.55f },
        { "Models/Nature/PalmTree_4.mdl", "Models/Nature/PalmTree_4.txt", 0.55f },
    };
    for (unsigned i = 0; i < sizeof(palmDefs) / sizeof(palmDefs[0]); ++i)
    {
        auto* mdl = cache->GetResource<Model>(palmDefs[i].modelPath, false);
        if (!mdl) continue;
        DecorEntry entry;
        entry.model = mdl;
        entry.matListPath = palmDefs[i].matListPath;
        entry.baseScale = palmDefs[i].scale;
        decorModels_[DECOR_PALM].Push(entry);
        ++totalLoaded;
    }

    // Dead trees — desert/barren biome (multi-material via .txt)
    static const SaplingDef deadTreeDefs[] = {
        { "Models/Nature/BirchTree_Dead_1.mdl",    "Models/Nature/BirchTree_Dead_1.txt",    0.35f },
        { "Models/Nature/BirchTree_Dead_2.mdl",    "Models/Nature/BirchTree_Dead_2.txt",    0.35f },
        { "Models/Nature/BirchTree_Dead_3.mdl",    "Models/Nature/BirchTree_Dead_3.txt",    0.3f },
        { "Models/Nature/CommonTree_Dead_1.mdl",   "Models/Nature/CommonTree_Dead_1.txt",   0.35f },
        { "Models/Nature/CommonTree_Dead_2.mdl",   "Models/Nature/CommonTree_Dead_2.txt",   0.35f },
        { "Models/Nature/CommonTree_Dead_3.mdl",   "Models/Nature/CommonTree_Dead_3.txt",   0.3f },
        { "Models/Nature/Willow_Dead_1.mdl",       "Models/Nature/Willow_Dead_1.txt",       0.35f },
        { "Models/Nature/Willow_Dead_2.mdl",       "Models/Nature/Willow_Dead_2.txt",       0.35f },
        { "Models/Nature/Willow_Dead_3.mdl",       "Models/Nature/Willow_Dead_3.txt",       0.3f },
    };
    for (unsigned i = 0; i < sizeof(deadTreeDefs) / sizeof(deadTreeDefs[0]); ++i)
    {
        auto* mdl = cache->GetResource<Model>(deadTreeDefs[i].modelPath, false);
        if (!mdl) continue;
        DecorEntry entry;
        entry.model = mdl;
        entry.matListPath = deadTreeDefs[i].matListPath;
        entry.baseScale = deadTreeDefs[i].scale;
        decorModels_[DECOR_DEADTREE].Push(entry);
        ++totalLoaded;
    }

    decorModelsReady_ = true;
    URHO3D_LOGINFOF("[DecorScatter] Loaded %u decoration models across %d categories", totalLoaded, NUM_DECOR_CATEGORIES);
}

void TerrainNode::UpdateDecorScatter()
{
    if (!terrain_ || !scene_ || !cameraNode_ || !ecosystem_)
        return;

    InitDecorModels();
    if (!decorModelsReady_)
        return;

    Vector3 camPos = cameraNode_->GetWorldPosition();
    const float SPAWN_RADIUS = 120.0f;
    const float DESPAWN_RADIUS = 150.0f;
    const float SPAWN_RADIUS_SQ = SPAWN_RADIUS * SPAWN_RADIUS;
    const float DESPAWN_RADIUS_SQ = DESPAWN_RADIUS * DESPAWN_RADIUS;
    const float CELL_SIZE = 4.0f;  // one potential decor per 4m cell

    // Despawn nodes beyond radius
    Vector<unsigned> toRemove;
    for (auto it = activeDecorNodes_.Begin(); it != activeDecorNodes_.End(); ++it)
    {
        Node* node = it->second_;
        if (!node)
        {
            toRemove.Push(it->first_);
            continue;
        }
        Vector3 diff = node->GetWorldPosition() - camPos;
        diff.y_ = 0.0f;
        if (diff.LengthSquared() > DESPAWN_RADIUS_SQ)
        {
            node->Remove();
            toRemove.Push(it->first_);
        }
    }
    for (unsigned key : toRemove)
        activeDecorNodes_.Erase(key);

    // Grid bounds around camera
    int minCellX = (int)Floor((camPos.x_ - SPAWN_RADIUS) / CELL_SIZE);
    int maxCellX = (int)Floor((camPos.x_ + SPAWN_RADIUS) / CELL_SIZE);
    int minCellZ = (int)Floor((camPos.z_ - SPAWN_RADIUS) / CELL_SIZE);
    int maxCellZ = (int)Floor((camPos.z_ + SPAWN_RADIUS) / CELL_SIZE);

    unsigned spawned = 0;
    const unsigned MAX_SPAWN_PER_FRAME = 40;

    for (int cz = minCellZ; cz <= maxCellZ && spawned < MAX_SPAWN_PER_FRAME; ++cz)
    {
        for (int cx = minCellX; cx <= maxCellX && spawned < MAX_SPAWN_PER_FRAME; ++cx)
        {
            // Deterministic hash from cell coords — same cell always produces same result
            unsigned cellKey = (unsigned)(cz + 32768) * 65536u + (unsigned)(cx + 32768);
            if (activeDecorNodes_.Contains(cellKey))
                continue;

            // Deterministic pseudo-random from cell key
            unsigned h = cellKey;
            h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;

            // Density gate — only ~15% of cells get decoration
            if ((h & 0xFF) > 38)
                continue;

            // World position with jitter within cell
            float wx = ((float)cx + (float)((h >> 8) & 0xFF) / 255.0f) * CELL_SIZE;
            float wz = ((float)cz + (float)((h >> 16) & 0xFF) / 255.0f) * CELL_SIZE;

            // Distance check
            float dx = wx - camPos.x_;
            float dz = wz - camPos.z_;
            if (dx * dx + dz * dz > SPAWN_RADIUS_SQ)
                continue;

            float wy = terrain_->GetHeight(Vector3(wx, 0.0f, wz));
            const float waterLevel = 5.0f;
            if (wy < waterLevel + 0.5f)
                continue;

            // Biome classification determines which category to use
            EcoBiome biome = ecosystem_->SampleBiome(wx, wz);
            float shrubDensity = ecosystem_->SampleShrubDensity(wx, wz);

            // Skip alpine; barren gets desert treatment
            if (biome == ECO_ALPINE)
                continue;
            if (shrubDensity < 0.1f && biome != ECO_FOREST && biome != ECO_BARREN)
                continue;

            // Pick category based on biome + hash variety
            int category = -1;
            unsigned variety = (h >> 24) & 0xFF;

            if (biome == ECO_GRASSLAND)
            {
                if (variety < 55)
                    category = DECOR_BUSH;
                else if (variety < 100)
                    category = DECOR_FLOWER;
                else if (variety < 140)
                    category = DECOR_GRASS;
                else if (variety < 175)
                    category = DECOR_GROUNDPLANT;
                else if (variety < 200)
                    category = DECOR_SAPLING;
                else if (variety < 230)
                    category = DECOR_STUMP;
                else
                    category = DECOR_ROCK;
            }
            else if (biome == ECO_FOREST)
            {
                if (variety < 45)
                    category = DECOR_BUSH;
                else if (variety < 85)
                    category = DECOR_MUSHROOM;
                else if (variety < 125)
                    category = DECOR_FERN;
                else if (variety < 155)
                    category = DECOR_GROUNDPLANT;
                else if (variety < 185)
                    category = DECOR_SAPLING;
                else if (variety < 215)
                    category = DECOR_STUMP;
                else
                    category = DECOR_ROCK;
            }
            else if (biome == ECO_WETLAND)
            {
                if (variety < 80)
                    category = DECOR_FERN;
                else if (variety < 140)
                    category = DECOR_GRASS;
                else if (variety < 200)
                    category = DECOR_BUSH;
                else if (variety < 230)
                    category = DECOR_GROUNDPLANT;
                else
                    category = DECOR_STUMP;
            }
            else if (biome == ECO_BARREN)
            {
                // Desert — cacti, palms, dead trees, rocks, sparse grass
                if (variety < 65)
                    category = DECOR_CACTUS;
                else if (variety < 105)
                    category = DECOR_PALM;
                else if (variety < 145)
                    category = DECOR_DEADTREE;
                else if (variety < 215)
                    category = DECOR_ROCK;
                else
                    category = DECOR_GRASS;
            }
            else
            {
                // Fallback — stumps, rocks, sparse grass
                if (variety < 90)
                    category = DECOR_STUMP;
                else if (variety < 200)
                    category = DECOR_ROCK;
                else
                    category = DECOR_GRASS;
            }

            if (category < 0 || category >= NUM_DECOR_CATEGORIES)
                continue;
            if (decorModels_[category].Empty())
                continue;

            // Pick model from category using hash
            unsigned modelIdx = h % decorModels_[category].Size();
            const DecorEntry& entry = decorModels_[category][modelIdx];

            // Scale variation: 80%-120% of base
            float scaleVar = 0.8f + (float)((h >> 4) & 0xFF) / 255.0f * 0.4f;
            float finalScale = entry.baseScale * scaleVar;

            // Y rotation from hash
            float yRot = (float)(h & 0xFFF) / 4096.0f * 360.0f;

            Node* node = scene_->CreateTemporaryChild("Decor", LOCAL);
            node->SetWorldPosition(Vector3(wx, wy, wz));
            node->SetRotation(Quaternion(0.0f, yRot, 0.0f));
            node->SetScale(finalScale);

            auto* sm = node->CreateComponent<StaticModel>();
            sm->SetModel(entry.model);
            if (!entry.matListPath.Empty())
                sm->ApplyMaterialList(entry.matListPath);
            else if (entry.material)
                sm->SetMaterial(entry.material);
            sm->SetCastShadows(false);
            sm->SetViewMask(0x01);

            activeDecorNodes_[cellKey] = node;
            ++spawned;
        }
    }
}

// ============================================================================
// Water Edge Scatter — Pond/ models at waterline, streamed by camera
// ============================================================================

void TerrainNode::InitWaterEdgeModels()
{
    if (waterEdgeModelsReady_)
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    // Plants material — atlas covers cattails, lily pads, mint, calla, grass
    auto* plantsMat = cache->GetResource<Material>("Materials/Pond/Pond_Plants_Atlas_MAT.xml", false);
    // Props material — rocks, frogs, branches
    auto* propsMat = cache->GetResource<Material>("Materials/Pond/Pond_Pack_MAT.xml", false);

    struct WEdgeDef
    {
        const char* modelPath;
        Material* mat;
        float scale;
    };

    // Cattails — tall reeds at the waterline
    WEdgeDef cattailDefs[] = {
        { "Models/Pond/CatTail_1.mdl",          plantsMat, 0.5f },
        { "Models/Pond/CatTail_2.mdl",          plantsMat, 0.5f },
        { "Models/Pond/CatTail_3.mdl",          plantsMat, 0.5f },
        { "Models/Pond/Long_Grass_Patch_1.mdl",  plantsMat, 0.4f },
        { "Models/Pond/Long_Grass_Patch_2.mdl",  plantsMat, 0.4f },
    };

    // Lily pads — flat on water surface
    WEdgeDef lilyDefs[] = {
        { "Models/Pond/Water_Lily_Leaf_1.mdl",    plantsMat, 0.4f },
        { "Models/Pond/Water_Lily_Leaf_2.mdl",    plantsMat, 0.4f },
        { "Models/Pond/Water_Lily_Leaf_3.mdl",    plantsMat, 0.4f },
        { "Models/Pond/Water_Lily_Leaf_4.mdl",    plantsMat, 0.4f },
        { "Models/Pond/Water_Lily_Blossom_1.mdl", plantsMat, 0.3f },
        { "Models/Pond/Water_Lily_Blossom_2.mdl", plantsMat, 0.3f },
        { "Models/Pond/Water_Lily_Blossom_3.mdl", plantsMat, 0.3f },
        { "Models/Pond/Water_Lettuce_1.mdl",      plantsMat, 0.3f },
        { "Models/Pond/Water_Lettuce_2.mdl",      plantsMat, 0.3f },
        { "Models/Pond/Water_Lettuce_3.mdl",      plantsMat, 0.3f },
        { "Models/Pond/Water_Hyacinth_1.mdl",     plantsMat, 0.3f },
        { "Models/Pond/Water_Hyacinth_2.mdl",     plantsMat, 0.3f },
        { "Models/Pond/Water_Hyacinth_3.mdl",     plantsMat, 0.3f },
    };

    // Bank plants — small waterside vegetation
    WEdgeDef bankPlantDefs[] = {
        { "Models/Pond/Water_Mint_1.mdl",        plantsMat, 0.35f },
        { "Models/Pond/Water_Mint_2.mdl",        plantsMat, 0.35f },
        { "Models/Pond/Water_Mint_3.mdl",        plantsMat, 0.35f },
        { "Models/Pond/Swamp_Calla_1.mdl",       plantsMat, 0.35f },
        { "Models/Pond/Swamp_Calla_2.mdl",       plantsMat, 0.35f },
        { "Models/Pond/Swamp_Calla_3.mdl",       plantsMat, 0.35f },
        { "Models/Pond/Short_Grass_Patch_1.mdl",  plantsMat, 0.35f },
        { "Models/Pond/Short_Grass_Patch_2.mdl",  plantsMat, 0.35f },
        { "Models/Pond/Mini_Plant_1.mdl",         plantsMat, 0.3f },
        { "Models/Pond/Mini_Plant_2.mdl",         plantsMat, 0.3f },
        { "Models/Pond/Mini_Plant_3.mdl",         plantsMat, 0.3f },
    };

    // Frogs — bank wildlife
    WEdgeDef frogDefs[] = {
        { "Models/Pond/Frog_1.mdl", propsMat, 0.25f },
        { "Models/Pond/Frog_2.mdl", propsMat, 0.25f },
        { "Models/Pond/Frog_3.mdl", propsMat, 0.25f },
    };

    // Rocks — bank pebbles
    WEdgeDef rockDefs[] = {
        { "Models/Pond/Pebble_1a.mdl",      propsMat, 0.3f },
        { "Models/Pond/Pebble_1b.mdl",      propsMat, 0.3f },
        { "Models/Pond/Pebble_1a_Moss.mdl", propsMat, 0.3f },
        { "Models/Pond/Pebble_2a.mdl",      propsMat, 0.25f },
        { "Models/Pond/Pebble_2b.mdl",      propsMat, 0.25f },
        { "Models/Pond/Pebble_2a_Moss.mdl", propsMat, 0.25f },
        { "Models/Pond/Rock_1a.mdl",         propsMat, 0.35f },
        { "Models/Pond/Rock_1b.mdl",         propsMat, 0.35f },
        { "Models/Pond/Rock_2a.mdl",         propsMat, 0.35f },
    };

    // Birds — perching on bank near water
    WEdgeDef birdDefs[] = {
        { "Models/Pond/Bird_1.mdl", propsMat, 0.2f },
        { "Models/Pond/Bird_2.mdl", propsMat, 0.2f },
        { "Models/Pond/Bird_3.mdl", propsMat, 0.2f },
    };

    // Dragonflies — hovering near waterline
    WEdgeDef dragonflyDefs[] = {
        { "Models/Pond/Dragonfly_1.mdl", propsMat, 0.15f },
        { "Models/Pond/Dragonfly_2.mdl", propsMat, 0.15f },
        { "Models/Pond/Dragonfly_3.mdl", propsMat, 0.15f },
    };

    // Branches — driftwood on bank
    WEdgeDef branchDefs[] = {
        { "Models/Pond/Branch_1a.mdl", propsMat, 0.35f },
        { "Models/Pond/Branch_1b.mdl", propsMat, 0.35f },
        { "Models/Pond/Branch_2a.mdl", propsMat, 0.3f },
        { "Models/Pond/Branch_2b.mdl", propsMat, 0.3f },
        { "Models/Pond/Branch_3a.mdl", propsMat, 0.3f },
        { "Models/Pond/Branch_3b.mdl", propsMat, 0.3f },
    };

    struct CatDefs { WEdgeDef* defs; unsigned count; };
    CatDefs allCats[NUM_WEDGE_CATEGORIES] = {
        { cattailDefs,     sizeof(cattailDefs) / sizeof(cattailDefs[0]) },
        { lilyDefs,        sizeof(lilyDefs) / sizeof(lilyDefs[0]) },
        { bankPlantDefs,   sizeof(bankPlantDefs) / sizeof(bankPlantDefs[0]) },
        { frogDefs,        sizeof(frogDefs) / sizeof(frogDefs[0]) },
        { rockDefs,        sizeof(rockDefs) / sizeof(rockDefs[0]) },
        { birdDefs,        sizeof(birdDefs) / sizeof(birdDefs[0]) },
        { dragonflyDefs,   sizeof(dragonflyDefs) / sizeof(dragonflyDefs[0]) },
        { branchDefs,      sizeof(branchDefs) / sizeof(branchDefs[0]) },
    };

    unsigned totalLoaded = 0;
    for (int cat = 0; cat < NUM_WEDGE_CATEGORIES; ++cat)
    {
        CatDefs& cd = allCats[cat];
        for (unsigned i = 0; i < cd.count; ++i)
        {
            auto* mdl = cache->GetResource<Model>(cd.defs[i].modelPath, false);
            if (!mdl)
                continue;
            DecorEntry entry;
            entry.model = mdl;
            entry.material = cd.defs[i].mat;
            entry.baseScale = cd.defs[i].scale;
            waterEdgeModels_[cat].Push(entry);
            ++totalLoaded;
        }
    }

    waterEdgeModelsReady_ = true;
    URHO3D_LOGINFOF("[WaterEdgeScatter] Loaded %u models across %d categories", totalLoaded, NUM_WEDGE_CATEGORIES);
}

void TerrainNode::UpdateWaterEdgeScatter()
{
    if (!terrain_ || !scene_ || !cameraNode_)
        return;

    InitWaterEdgeModels();
    if (!waterEdgeModelsReady_)
        return;

    Vector3 camPos = cameraNode_->GetWorldPosition();
    const float SPAWN_RADIUS = 100.0f;
    const float DESPAWN_RADIUS = 130.0f;
    const float SPAWN_RADIUS_SQ = SPAWN_RADIUS * SPAWN_RADIUS;
    const float DESPAWN_RADIUS_SQ = DESPAWN_RADIUS * DESPAWN_RADIUS;
    const float CELL_SIZE = 3.0f;  // denser than decor — water edges are narrow
    const float waterLevel = 5.0f;

    // Despawn beyond radius
    Vector<unsigned> toRemove;
    for (auto it = activeWaterEdgeNodes_.Begin(); it != activeWaterEdgeNodes_.End(); ++it)
    {
        Node* node = it->second_;
        if (!node)
        {
            toRemove.Push(it->first_);
            continue;
        }
        Vector3 diff = node->GetWorldPosition() - camPos;
        diff.y_ = 0.0f;
        if (diff.LengthSquared() > DESPAWN_RADIUS_SQ)
        {
            node->Remove();
            toRemove.Push(it->first_);
        }
    }
    for (unsigned key : toRemove)
        activeWaterEdgeNodes_.Erase(key);

    // Grid bounds around camera
    int minCellX = (int)Floor((camPos.x_ - SPAWN_RADIUS) / CELL_SIZE);
    int maxCellX = (int)Floor((camPos.x_ + SPAWN_RADIUS) / CELL_SIZE);
    int minCellZ = (int)Floor((camPos.z_ - SPAWN_RADIUS) / CELL_SIZE);
    int maxCellZ = (int)Floor((camPos.z_ + SPAWN_RADIUS) / CELL_SIZE);

    unsigned spawned = 0;
    const unsigned MAX_SPAWN_PER_FRAME = 30;

    for (int cz = minCellZ; cz <= maxCellZ && spawned < MAX_SPAWN_PER_FRAME; ++cz)
    {
        for (int cx = minCellX; cx <= maxCellX && spawned < MAX_SPAWN_PER_FRAME; ++cx)
        {
            // Use offset hash space to avoid collisions with DecorScatter
            unsigned cellKey = (unsigned)(cz + 32768) * 65536u + (unsigned)(cx + 32768);
            cellKey ^= 0xA5A5A5A5u;  // separate hash space from decor
            if (activeWaterEdgeNodes_.Contains(cellKey))
                continue;

            // Deterministic pseudo-random
            unsigned h = cellKey;
            h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;

            // Density gate — ~20% of cells near water get vegetation
            if ((h & 0xFF) > 51)
                continue;

            // World position with jitter
            float wx = ((float)cx + (float)((h >> 8) & 0xFF) / 255.0f) * CELL_SIZE;
            float wz = ((float)cz + (float)((h >> 16) & 0xFF) / 255.0f) * CELL_SIZE;

            float dx = wx - camPos.x_;
            float dz = wz - camPos.z_;
            if (dx * dx + dz * dz > SPAWN_RADIUS_SQ)
                continue;

            float wy = terrain_->GetHeight(Vector3(wx, 0.0f, wz));

            // Zone classification:
            // UNDERWATER: wy < waterLevel - 0.3  (lily pads float on surface)
            // WATERLINE:  wy in [waterLevel - 0.3, waterLevel + 0.5] (cattails, reeds)
            // BANK:       wy in [waterLevel + 0.5, waterLevel + 3.0] (plants, frogs, rocks)
            // ABOVE:      wy > waterLevel + 3.0 → skip (too far from water)
            float relHeight = wy - waterLevel;

            int category = -1;
            float placeY = wy;
            unsigned variety = (h >> 24) & 0xFF;

            if (relHeight < -0.3f)
            {
                // Underwater — lily pads float on water surface
                if (relHeight < -3.0f)
                    continue;  // too deep
                category = WEDGE_LILYPAD;
                placeY = waterLevel + 0.01f;  // float on surface
            }
            else if (relHeight < 0.5f)
            {
                // Waterline band — cattails, reeds, dragonflies
                if (variety < 180)
                    category = WEDGE_CATTAIL;
                else if (variety < 220)
                    category = WEDGE_DRAGONFLY;
                else
                    category = WEDGE_BANKPLANT;
            }
            else if (relHeight < 3.0f)
            {
                // Bank — plants, frogs, rocks, birds, branches
                if (variety < 60)
                    category = WEDGE_BANKPLANT;
                else if (variety < 85)
                    category = WEDGE_FROG;
                else if (variety < 105)
                    category = WEDGE_BIRD;
                else if (variety < 145)
                    category = WEDGE_ROCK;
                else if (variety < 180)
                    category = WEDGE_BRANCH;
                else
                    continue;  // sparse on upper bank
            }
            else
            {
                continue;  // too far from water
            }

            if (category < 0 || category >= NUM_WEDGE_CATEGORIES)
                continue;
            if (waterEdgeModels_[category].Empty())
                continue;

            // Dragonflies hover above the waterline
            if (category == WEDGE_DRAGONFLY)
                placeY = Max(placeY, waterLevel) + 0.3f + (float)((h >> 12) & 0x3F) / 63.0f * 0.4f;

            unsigned modelIdx = h % waterEdgeModels_[category].Size();
            const DecorEntry& entry = waterEdgeModels_[category][modelIdx];

            float scaleVar = 0.8f + (float)((h >> 4) & 0xFF) / 255.0f * 0.4f;
            float finalScale = entry.baseScale * scaleVar;
            float yRot = (float)(h & 0xFFF) / 4096.0f * 360.0f;

            Node* node = scene_->CreateTemporaryChild("WaterEdge", LOCAL);
            node->SetWorldPosition(Vector3(wx, placeY, wz));
            node->SetRotation(Quaternion(0.0f, yRot, 0.0f));
            node->SetScale(finalScale);

            auto* sm = node->CreateComponent<StaticModel>();
            sm->SetModel(entry.model);
            if (entry.material)
                sm->SetMaterial(entry.material);
            sm->SetCastShadows(false);
            sm->SetViewMask(0x01);

            activeWaterEdgeNodes_[cellKey] = node;
            ++spawned;
        }
    }
}
