// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/UI/BorderImage.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UIElement.h>
#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Graphics/Camera.h>

using namespace Urho3D;

/// Single vital bar with auto-show/hide behavior.
struct VitalBar
{
    BorderImage* background{};   // dark background (fixed width)
    BorderImage* fill{};         // colored fill (width = value * maxWidth)
    Text* label{};               // bar name label above bar
    Text* criticalLabel{};       // critical text ("STARVING", etc.)
    float value{1.0f};           // 0.0-1.0 normalized
    float showThreshold{0.75f};  // show when value drops below this
    float hideDelay{5.0f};       // seconds after threshold before hiding
    float currentAlpha{0.0f};    // for smooth fade
    float hideTimer{0.0f};       // counts up when value > showThreshold
    Color barColor;              // fill color at normal levels
    Color criticalColor;         // color when below 25%
    String labelText;            // e.g. "Hunger"
    String criticalText;         // e.g. "STARVING"
};

/// Status icon identifiers (bottom-left icon stack).
enum StatusIcon
{
    ICON_FREEZING = 0,
    ICON_OVERHEATING,
    ICON_STARVING,
    ICON_DEHYDRATED,
    ICON_EXHAUSTED,
    ICON_NEAR_FIRE,
    ICON_IN_SHELTER,
    ICON_ENCUMBERED,
    ICON_MAX
};

/// Single status icon with fade behavior.
struct StatusIconEntry
{
    Text* element{};       // text glyph used as icon (Unicode symbol)
    float currentAlpha{};  // smooth fade
    bool active{};         // whether condition is currently true
    bool wasActive{};      // previous frame state (for edge detection)
    String hintText;       // context hint shown on first activation
};

/// HUD overlay with vital bars (HP, Hunger, Thirst, Stamina),
/// status icons (bottom-left), and context hints (bottom-right).
/// Attach to any scene node as a LogicComponent.
class HUD : public LogicComponent
{
    URHO3D_OBJECT(HUD, LogicComponent);

public:
    explicit HUD(Context* context);
    static void RegisterObject(Context* context);

    void Start() override;
    void Update(float timeStep) override;

    /// Set vital values (0.0-1.0). Call each frame from player state.
    void SetHP(float v);
    void SetHunger(float v);
    void SetThirst(float v);
    void SetStamina(float v);

    /// Set effective temperature in Celsius. HUD shows indicator when uncomfortable.
    void SetTemperature(float celsius);

    /// Set the font used by all HUD text elements.
    void SetFont(Font* font, int size);

    // --- Phase 2: Status Icons ---
    /// Set a status icon active or inactive. HUD handles fade.
    void SetStatusIcon(StatusIcon icon, bool active);

    // --- Phase 2: Context Hints ---
    /// Set the context hint text (e.g. "E: Pick up"). Empty string hides it.
    void SetContextHint(const String& text);

private:
    void CreateBars();
    void CreateBar(VitalBar& bar, const String& name);
    void UpdateBar(VitalBar& bar, float timeStep);
    void CreateStatusIcons();
    void UpdateStatusIcons(float timeStep);
    void CreateContextHint();
    void UpdateContextHint(float timeStep);

    UIElement* barContainer_{};   // bottom-center anchor
    UIElement* topRow_{};         // HP + Hunger + Thirst side by side
    UIElement* bottomRow_{};      // Stamina centered below

    VitalBar hpBar_;
    VitalBar hungerBar_;
    VitalBar thirstBar_;
    VitalBar staminaBar_;

    // Temperature indicator (text-only, no bar)
    Text* tempIndicator_{};
    float temperature_{15.0f};       // current effective temp in Celsius
    float tempIndicatorAlpha_{0.0f}; // smooth fade

    // --- Phase 2: Status Icons (bottom-left) ---
    UIElement* iconContainer_{};
    StatusIconEntry icons_[ICON_MAX];

    // --- Phase 2: Context Hint (bottom-right) ---
    Text* contextHintText_{};
    String contextHintTarget_;       // current desired text
    float contextHintAlpha_{};       // smooth fade
    float statusHintTimer_{};        // countdown for status icon hints (overrides normal hints)

    SharedPtr<Font> font_;
    int fontSize_{9};

    // Bar layout constants
    static constexpr int BAR_WIDTH = 120;
    static constexpr int BAR_HEIGHT = 8;
    static constexpr int BAR_GAP = 6;
    static constexpr int BAR_MARGIN_BOTTOM = 80;
    static constexpr float FADE_SPEED = 3.0f;
    static constexpr float CRITICAL_THRESHOLD = 0.25f;
    static constexpr int ICON_SIZE = 16;
    static constexpr int ICON_GAP = 4;
    static constexpr int ICON_MARGIN = 20;
};
