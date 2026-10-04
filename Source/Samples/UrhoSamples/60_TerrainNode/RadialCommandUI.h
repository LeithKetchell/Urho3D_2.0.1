// RadialCommandUI — screen-space radial command menu around a selected NPC.
// Two concentric rings of clickable command icons, centered on the NPC's
// projected screen position. Replaces the old possession system.

#pragma once

#include <Urho3D/Core/Object.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Math/Vector2.h>
#include <Urho3D/Math/Color.h>

namespace Urho3D
{
class Button;
class Camera;
class Font;
class Graphics;
class Node;
class Text;
class UIElement;
}

using namespace Urho3D;

/// A single command in the radial menu.
struct RadialCommand
{
    String iconPath;    ///< Path to icon texture (insignia, no text)
    int taskId{-1};     ///< Server task ID (STASK_*) or directive type
    int ring{0};        ///< 0 = inner ring, 1 = outer ring
    Color tint{Color::WHITE};  ///< Icon tint color
};

/// Screen-space radial command UI for NPC interaction.
class RadialCommandUI : public Object
{
    URHO3D_OBJECT(RadialCommandUI, Object);

public:
    explicit RadialCommandUI(Context* context);
    ~RadialCommandUI();

    /// Show the radial menu for a selected NPC node.
    void Show(Node* npcNode, unsigned spawnId);
    /// Hide and clean up.
    void Hide();
    /// Return true if visible.
    bool IsVisible() const { return visible_; }
    /// Update positions each frame (tracks NPC screen position).
    void Update(Camera* camera);
    /// Return the selected NPC's spawn ID.
    unsigned GetSelectedSpawnId() const { return selectedSpawnId_; }
    /// Return the selected NPC node.
    Node* GetSelectedNode() const { return selectedNode_; }

    /// Set the font used for labels.
    void SetFont(Font* font) { font_ = font; }
    /// Set ring radii in pixels.
    void SetRingRadii(float inner, float outer) { innerRadius_ = inner; outerRadius_ = outer; }

    /// Add a command to the menu. Call before Show(), or rebuild with RebuildButtons().
    void AddCommand(const RadialCommand& cmd);
    /// Clear all commands.
    void ClearCommands();
    /// Rebuild button layout after adding/removing commands.
    void RebuildButtons();

    /// Signal: a command was clicked. Caller connects to this.
    /// eventData["TaskId"] = int, eventData["SpawnId"] = unsigned
    static const StringHash E_RADIAL_COMMAND;
    static const StringHash P_TASKID;
    static const StringHash P_SPAWNID;

private:
    void HandleButtonClick(StringHash eventType, VariantMap& eventData);
    void PositionButtons(const Vector2& center);

    WeakPtr<Node> selectedNode_;
    unsigned selectedSpawnId_{};
    bool visible_{};

    SharedPtr<UIElement> container_;    ///< Root UI element (holds everything)
    Vector<RadialCommand> commands_;
    Vector<SharedPtr<Button>> buttons_;
    Vector<SharedPtr<Text>> labels_;

    SharedPtr<Font> font_;
    float innerRadius_{60.0f};
    float outerRadius_{110.0f};
};
