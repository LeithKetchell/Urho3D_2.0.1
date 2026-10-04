// WorkboardBase — shared base class for WorkboardManager and WorkboardClient.
// Contains workboard parsing, UI panel creation, and section rendering.

#pragma once

#include <Urho3D/Engine/Application.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/ListView.h>
#include <Urho3D/UI/ScrollView.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/Window.h>

#include "WorkboardProtocol.h"

using namespace Urho3D;

class WorkboardBase : public Application
{
    URHO3D_OBJECT(WorkboardBase, Application);

public:
    explicit WorkboardBase(Context* context);

protected:
    // ── Shared UI creation (UV-anchored: minX, minY, maxX, maxY in 0–1 range) ──
    void CreateWorkboardPanel(UIElement* parent, float minX, float minY, float maxX, float maxY);
    void CreatePlanPanel(UIElement* parent, float minX, float minY, float maxX, float maxY);

    // ── Workboard parsing & rendering ──
    void ParseWorkboard(const String& content);
    void RenderWorkboardUI();
    void AddSectionToUI(const WorkboardSection& section);
    void HandleSectionToggle(StringHash eventType, VariantMap& eventData);

    // ── Plan selection (delegates to derived) ──
    void HandlePlanSelected(StringHash eventType, VariantMap& eventData);

    // ── Helpers ──
    String GetProjectRoot();
    String GetClaudeDir();

    // ── Virtual methods (derived classes override) ──
    virtual void OnPlanSelected(const String& filename) = 0;

    // ── Shared members ──
    SharedPtr<Font> font_;
    int fontSize_{11};
    Vector<WorkboardSection> sections_;

    // Workboard UI
    Window* workboardPanel_{};
    ScrollView* workboardScroll_{};
    UIElement* workboardContent_{};

    // Plan UI
    Window* planPanel_{};
    ListView* planListView_{};
    ScrollView* planContentScroll_{};
    Text* planContentText_{};
    Vector<String> planFiles_;
    String currentPlanFile_;

    // Project path
    String projectRoot_;
};
