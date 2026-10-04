// RadialCommandUI — screen-space radial command menu around a selected NPC.

#include "RadialCommandUI.h"

#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/Math/MathDefs.h>

const StringHash RadialCommandUI::E_RADIAL_COMMAND("RadialCommand");
const StringHash RadialCommandUI::P_TASKID("TaskId");
const StringHash RadialCommandUI::P_SPAWNID("SpawnId");

RadialCommandUI::RadialCommandUI(Context* context) :
    Object(context)
{
}

RadialCommandUI::~RadialCommandUI()
{
    Hide();
}

void RadialCommandUI::AddCommand(const RadialCommand& cmd)
{
    commands_.Push(cmd);
}

void RadialCommandUI::ClearCommands()
{
    commands_.Clear();
}

void RadialCommandUI::Show(Node* npcNode, unsigned spawnId)
{
    if (!npcNode)
        return;

    Hide();

    selectedNode_ = npcNode;
    selectedSpawnId_ = spawnId;
    visible_ = true;

    auto* ui = GetSubsystem<UI>();
    container_ = ui->GetRoot()->CreateChild<UIElement>("RadialCommandUI");
    container_->SetPosition(0, 0);
    container_->SetSize(ui->GetRoot()->GetSize());
    container_->SetPriority(200);  // Above most UI

    RebuildButtons();
}

void RadialCommandUI::Hide()
{
    if (container_)
    {
        container_->Remove();
        container_.Reset();
    }
    buttons_.Clear();
    labels_.Clear();
    selectedNode_.Reset();
    selectedSpawnId_ = 0;
    visible_ = false;
}

void RadialCommandUI::RebuildButtons()
{
    if (!container_)
        return;

    // Clear old buttons
    container_->RemoveAllChildren();
    buttons_.Clear();
    labels_.Clear();

    auto* cache = GetSubsystem<ResourceCache>();
    int iconSize = 32;

    for (unsigned i = 0; i < commands_.Size(); ++i)
    {
        const RadialCommand& cmd = commands_[i];

        auto* btn = container_->CreateChild<Button>();
        btn->SetFixedSize(iconSize, iconSize);
        btn->SetVar("TaskId", cmd.taskId);
        btn->SetVar("Index", (int)i);

        // Try to load icon texture
        if (!cmd.iconPath.Empty())
        {
            auto* tex = cache->GetResource<Texture2D>(cmd.iconPath, false);
            if (tex)
            {
                auto* img = btn->CreateChild<BorderImage>();
                img->SetTexture(tex);
                img->SetFixedSize(iconSize, iconSize);
                img->SetColor(cmd.tint);
            }
            else
            {
                // Fallback: colored square
                btn->SetColor(cmd.tint);
                btn->SetStyleAuto();
            }
        }
        else
        {
            // No icon — solid colored square
            btn->SetColor(cmd.tint);
            btn->SetStyleAuto();
        }

        SubscribeToEvent(btn, E_RELEASED,
            URHO3D_HANDLER(RadialCommandUI, HandleButtonClick));

        buttons_.Push(SharedPtr<Button>(btn));
    }
}

void RadialCommandUI::Update(Camera* camera)
{
    if (!visible_ || !selectedNode_ || !camera || !container_)
    {
        if (visible_ && !selectedNode_)
            Hide();  // NPC died or was removed
        return;
    }

    auto* graphics = GetSubsystem<Graphics>();
    float w = (float)graphics->GetWidth();
    float h = (float)graphics->GetHeight();

    // Project NPC world position to screen
    Vector3 npcWorldPos = selectedNode_->GetWorldPosition();
    // Offset to chest height (roughly 60% of model height)
    npcWorldPos.y_ += 1.0f;

    Vector2 screenPos = camera->WorldToScreenPoint(npcWorldPos);
    Vector2 center(screenPos.x_ * w, screenPos.y_ * h);

    // Check if NPC is behind camera
    Vector3 toNpc = npcWorldPos - camera->GetNode()->GetWorldPosition();
    if (toNpc.DotProduct(camera->GetNode()->GetWorldDirection()) < 0.0f)
    {
        container_->SetVisible(false);
        return;
    }
    container_->SetVisible(true);

    PositionButtons(center);
}

void RadialCommandUI::PositionButtons(const Vector2& center)
{
    // Count commands per ring
    unsigned innerCount = 0, outerCount = 0;
    for (unsigned i = 0; i < commands_.Size(); ++i)
    {
        if (commands_[i].ring == 0) ++innerCount;
        else ++outerCount;
    }

    unsigned innerIdx = 0, outerIdx = 0;

    for (unsigned i = 0; i < buttons_.Size() && i < commands_.Size(); ++i)
    {
        float radius;
        float angle;
        unsigned count;
        unsigned idx;

        if (commands_[i].ring == 0)
        {
            radius = innerRadius_;
            count = innerCount;
            idx = innerIdx++;
        }
        else
        {
            radius = outerRadius_;
            count = outerCount;
            idx = outerIdx++;
        }

        // Distribute evenly, starting from top (-90 degrees)
        if (count < 1) count = 1;
        angle = -90.0f + (360.0f / (float)count) * (float)idx;
        float rad = angle * M_DEGTORAD;

        float bx = center.x_ + radius * cosf(rad) - buttons_[i]->GetWidth() * 0.5f;
        float by = center.y_ + radius * sinf(rad) - buttons_[i]->GetHeight() * 0.5f;

        buttons_[i]->SetPosition((int)bx, (int)by);
    }
}

void RadialCommandUI::HandleButtonClick(StringHash, VariantMap& eventData)
{
    auto* btn = static_cast<Button*>(eventData[Released::P_ELEMENT].GetPtr());
    if (!btn)
        return;

    int taskId = btn->GetVar("TaskId").GetI32();

    VariantMap& data = GetEventDataMap();
    data[P_TASKID] = taskId;
    data[P_SPAWNID] = selectedSpawnId_;
    SendEvent(E_RADIAL_COMMAND, data);
}
