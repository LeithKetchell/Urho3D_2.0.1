// NPCRingUI — unified stat & command ring system for human NPCs.

#include "NPCRingUI.h"
#include "Creature.h"
#include "HumanNPC.h"

#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Math/MathDefs.h>

const StringHash NPCRingUI::E_RING_COMMAND("RingCommand");
const StringHash NPCRingUI::P_TASKID("TaskId");
const StringHash NPCRingUI::P_SPAWNID("SpawnId");

NPCRingUI::NPCRingUI(Context* context) :
    Object(context)
{
}

void NPCRingUI::Select(Node* npcNode, unsigned spawnId)
{
    selectedSpawnId_ = spawnId;
}

void NPCRingUI::Deselect()
{
    selectedSpawnId_ = 0;
}

Vector3 NPCRingUI::ScreenToWorld(float sx, float sy) const
{
    if (!camera_) return Vector3::ZERO;
    auto* graphics = GetSubsystem<Graphics>();
    float nx = sx / (float)graphics->GetWidth();
    float ny = sy / (float)graphics->GetHeight();
    // Unproject at a fixed depth (2m in front of camera)
    Ray ray = camera_->GetScreenRay(nx, ny);
    return ray.origin_ + ray.direction_ * 2.0f;
}

void NPCRingUI::Update(Scene* scene, Camera* camera)
{
    ringStates_.Clear();
    camera_ = camera;
    debugRenderer_ = scene ? scene->GetComponent<DebugRenderer>() : nullptr;
    if (!scene || !camera)
        return;

    auto* graphics = GetSubsystem<Graphics>();
    float w = (float)graphics->GetWidth();
    float h = (float)graphics->GetHeight();
    Vector3 camPos = camera->GetNode()->GetWorldPosition();
    Vector3 camDir = camera->GetNode()->GetWorldDirection();

    const auto& children = scene->GetChildren();
    for (unsigned i = 0; i < children.Size(); ++i)
    {
        Node* child = children[i];
        if (!child) continue;

        auto* creature = child->GetDerivedComponent<HumanNPC>(true);
        if (!creature) continue;

        // Distance cull
        Vector3 npcPos = child->GetWorldPosition();
        float dist = (npcPos - camPos).Length();
        if (dist > 30.0f) continue;

        // Behind camera check
        Vector3 toNpc = npcPos - camPos;
        if (toNpc.DotProduct(camDir) < 0.0f) continue;

        // Project to screen
        Vector3 headPos = npcPos + Vector3(0.0f, 1.8f, 0.0f);
        Vector2 screenPos = camera->WorldToScreenPoint(headPos);
        screenPos.x_ *= w;
        screenPos.y_ *= h;

        // Off-screen check
        if (screenPos.x_ < -50.0f || screenPos.x_ > w + 50.0f ||
            screenPos.y_ < -50.0f || screenPos.y_ > h + 50.0f)
            continue;

        NPCRingState state;
        state.node = child;
        state.spawnId = creature->GetSpawnId();
        state.hp = (float)creature->GetHp();
        state.maxHp = (float)creature->GetMaxHp();
        state.hunger = creature->GetHunger();
        state.thirst = creature->GetThirst();
        state.warmth = creature->GetWarmth();
        state.stamina = creature->GetStamina();
        state.screenPos = screenPos;
        state.visible = true;

        if (state.spawnId == selectedSpawnId_)
            selectedScreenPos_ = screenPos;

        ringStates_.Push(state);
    }
}

void NPCRingUI::Render()
{
    for (unsigned i = 0; i < ringStates_.Size(); ++i)
    {
        bool selected = (ringStates_[i].spawnId == selectedSpawnId_);
        DrawRing(ringStates_[i], selected);
    }
}

void NPCRingUI::DrawRing(const NPCRingState& state, bool selected)
{
    float cx = state.screenPos.x_;
    float cy = state.screenPos.y_;

    // Stat ring — 5 arcs, each 72°, starting from top (-90°)
    struct StatArc { float value; float maxVal; Color color; };
    StatArc stats[] = {
        { state.hp,      state.maxHp, Color(0.8f, 0.2f, 0.2f) },   // HP red
        { state.hunger,  100.0f,      Color(0.8f, 0.53f, 0.2f) },   // Hunger orange
        { state.thirst,  100.0f,      Color(0.2f, 0.4f, 0.67f) },   // Thirst blue
        { state.warmth,  100.0f,      Color(0.8f, 0.67f, 0.2f) },   // Warmth gold
        { state.stamina, 100.0f,      Color(0.2f, 0.67f, 0.2f) },   // Stamina green
    };

    float arcSweep = 360.0f / 5.0f;  // 72°
    float gap = 2.0f;  // degrees gap between arcs
    float startAngle = -90.0f;

    for (int s = 0; s < 5; ++s)
    {
        float fill = (stats[s].maxVal > 0.0f) ? stats[s].value / stats[s].maxVal : 0.0f;
        fill = Clamp(fill, 0.0f, 1.0f);
        DrawArc(cx, cy, statRadius_ * 0.6f, statRadius_,
                startAngle + gap * 0.5f, arcSweep - gap, stats[s].color, fill);
        startAngle += arcSweep;
    }

    // Command ring — only on selected NPC
    if (selected && !commands_.Empty())
    {
        float cmdSweep = 360.0f / (float)commands_.Size();
        float cmdStart = -90.0f;

        for (unsigned c = 0; c < commands_.Size(); ++c)
        {
            DrawArc(cx, cy, cmdInnerRadius_, cmdOuterRadius_,
                    cmdStart + gap * 0.5f, cmdSweep - gap, commands_[c].color, 1.0f);
            cmdStart += cmdSweep;
        }
    }
}

void NPCRingUI::DrawArc(float cx, float cy, float innerR, float outerR,
                         float startAngle, float sweepAngle, const Color& color, float fillRatio)
{
    if (fillRatio <= 0.0f || !debugRenderer_)
        return;

    float actualSweep = sweepAngle * fillRatio;
    int segments = Max(4, (int)(actualSweep / 10.0f));  // ~1 segment per 10°

    float startRad = startAngle * M_DEGTORAD;
    float stepRad = (actualSweep * M_DEGTORAD) / (float)segments;

    // Draw filled arc as triangle strip in world space on a plane facing the camera.
    // Convert screen (cx,cy) + radius offsets to world positions via camera unproject.
    for (int i = 0; i < segments; ++i)
    {
        float a0 = startRad + stepRad * (float)i;
        float a1 = startRad + stepRad * (float)(i + 1);

        // Screen-space positions of the 4 corners of this segment
        float ix0 = cx + innerR * cosf(a0), iy0 = cy + innerR * sinf(a0);
        float ox0 = cx + outerR * cosf(a0), oy0 = cy + outerR * sinf(a0);
        float ix1 = cx + innerR * cosf(a1), iy1 = cy + innerR * sinf(a1);
        float ox1 = cx + outerR * cosf(a1), oy1 = cy + outerR * sinf(a1);

        // Unproject to world at fixed depth
        Vector3 wi0 = ScreenToWorld(ix0, iy0);
        Vector3 wo0 = ScreenToWorld(ox0, oy0);
        Vector3 wi1 = ScreenToWorld(ix1, iy1);
        Vector3 wo1 = ScreenToWorld(ox1, oy1);

        debugRenderer_->AddTriangle(wi0, wo0, wi1, color, false);
        debugRenderer_->AddTriangle(wi1, wo0, wo1, color, false);
    }
}

int NPCRingUI::HitTest(int screenX, int screenY) const
{
    if (selectedSpawnId_ == 0 || commands_.Empty())
        return -1;

    float dx = (float)screenX - selectedScreenPos_.x_;
    float dy = (float)screenY - selectedScreenPos_.y_;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < cmdInnerRadius_ || dist > cmdOuterRadius_)
        return -1;

    // Angle from top (-90°), clockwise
    float angle = atan2f(dy, dx) * M_RADTODEG;  // -180 to 180
    angle += 90.0f;  // rotate so top = 0°
    if (angle < 0.0f) angle += 360.0f;

    float segSweep = 360.0f / (float)commands_.Size();
    int segIndex = ((int)(angle / segSweep)) % (int)commands_.Size();
    if (segIndex >= 0 && segIndex < (int)commands_.Size())
        return commands_[segIndex].taskId;

    return -1;
}
