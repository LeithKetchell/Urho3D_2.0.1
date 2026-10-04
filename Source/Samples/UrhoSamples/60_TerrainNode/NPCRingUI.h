// NPCRingUI — unified stat & command ring system for human NPCs.
// Inner ring: 5 colored arcs (HP, hunger, thirst, warmth, stamina).
// Outer ring: command segments on selected NPC (clickable).
// Screen-space, projected from NPC world position each frame.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Math/Color.h>
#include <Urho3D/Math/Vector2.h>

namespace Urho3D
{
class Camera;
class DebugRenderer;
class Node;
class Scene;
class UIElement;
}

using namespace Urho3D;

/// A command segment in the outer ring.
struct RingCommand
{
    int taskId{-1};
    Color color{Color::WHITE};
};

/// Per-NPC ring state (reused each frame).
struct NPCRingState
{
    WeakPtr<Node> node;
    unsigned spawnId{};
    float hp{}, hunger{}, thirst{}, warmth{}, stamina{};
    float maxHp{10.0f};
    Vector2 screenPos;
    bool visible{};
};

class NPCRingUI : public Object
{
    URHO3D_OBJECT(NPCRingUI, Object);

public:
    explicit NPCRingUI(Context* context);

    /// Set command list for the outer ring.
    void SetCommands(const Vector<RingCommand>& commands) { commands_ = commands; }

    /// Select an NPC — shows command ring.
    void Select(Node* npcNode, unsigned spawnId);
    /// Deselect — hides command ring.
    void Deselect();
    /// Return selected spawnId (0 = none).
    unsigned GetSelectedSpawnId() const { return selectedSpawnId_; }

    /// Update all rings. Call each frame from HandlePostRenderUpdate.
    /// Iterates visible human NPCs, projects positions, updates ring states.
    void Update(Scene* scene, Camera* camera);

    /// Draw all rings. Call from HandlePostRenderUpdate after Update.
    void Render();

    /// Check if a click at screen position hits a command segment.
    /// Returns taskId or -1 if no hit.
    int HitTest(int screenX, int screenY) const;

    /// Ring radii in pixels.
    void SetStatRadius(float r) { statRadius_ = r; }
    void SetCommandInnerRadius(float r) { cmdInnerRadius_ = r; }
    void SetCommandOuterRadius(float r) { cmdOuterRadius_ = r; }

    /// Signal: a command was clicked.
    static const StringHash E_RING_COMMAND;
    static const StringHash P_TASKID;
    static const StringHash P_SPAWNID;

private:
    void DrawArc(float cx, float cy, float innerR, float outerR,
                 float startAngle, float sweepAngle, const Color& color, float fillRatio);
    void DrawRing(const NPCRingState& state, bool selected);
    Vector3 ScreenToWorld(float sx, float sy) const;

    Vector<NPCRingState> ringStates_;
    Vector<RingCommand> commands_;

    unsigned selectedSpawnId_{};
    Vector2 selectedScreenPos_;

    float statRadius_{20.0f};
    float cmdInnerRadius_{28.0f};
    float cmdOuterRadius_{52.0f};

    DebugRenderer* debugRenderer_{};
    Camera* camera_{};
};
