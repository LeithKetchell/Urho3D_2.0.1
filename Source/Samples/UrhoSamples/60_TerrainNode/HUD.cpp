// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "HUD.h"
#include "Creature.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/Timer.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/UI.h>

HUD::HUD(Context* context)
    : LogicComponent(context)
{
    SetUpdateEventMask(LogicComponentEvents::Update);
}

void HUD::RegisterObject(Context* context)
{
    context->RegisterFactory<HUD>();
}

void HUD::Start()
{
    // Set up bar parameters per the plan
    hpBar_.barColor = Color(0.8f, 0.2f, 0.2f);       // #CC3333 deep red
    hpBar_.criticalColor = Color(1.0f, 0.0f, 0.0f);
    hpBar_.showThreshold = 1.01f;  // visible whenever HP < 1.0 (always show when damaged)
    hpBar_.hideDelay = 5.0f;
    hpBar_.labelText = "HP";
    hpBar_.criticalText = "";

    hungerBar_.barColor = Color(0.8f, 0.53f, 0.2f);   // #CC8833 amber
    hungerBar_.criticalColor = Color(0.9f, 0.3f, 0.1f);
    hungerBar_.showThreshold = 0.75f;
    hungerBar_.hideDelay = 5.0f;
    hungerBar_.labelText = "Hunger";
    hungerBar_.criticalText = "STARVING";

    thirstBar_.barColor = Color(0.2f, 0.4f, 0.67f);   // #3366AA muted blue
    thirstBar_.criticalColor = Color(0.5f, 0.2f, 0.2f);
    thirstBar_.showThreshold = 0.75f;
    thirstBar_.hideDelay = 5.0f;
    thirstBar_.labelText = "Thirst";
    thirstBar_.criticalText = "DEHYDRATED";

    staminaBar_.barColor = Color(0.8f, 0.67f, 0.2f);  // #CCAA33 gold
    staminaBar_.criticalColor = Color(0.4f, 0.4f, 0.4f);
    staminaBar_.showThreshold = 1.01f;  // caller controls visibility via value changes
    staminaBar_.hideDelay = 3.0f;
    staminaBar_.labelText = "Stamina";
    staminaBar_.criticalText = "";

    // Load default font if none set
    if (!font_)
    {
        auto* cache = GetSubsystem<ResourceCache>();
        font_ = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");
        if (!font_)
            font_ = cache->GetResource<Font>("Fonts/BlueHighway.sdf");
    }

    CreateBars();
    CreateStatusIcons();
    CreateContextHint();
}

void HUD::SetFont(Font* font, int size)
{
    font_ = font;
    fontSize_ = size;
}

void HUD::CreateBars()
{
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();

    // Bottom-center container
    barContainer_ = root->CreateChild<UIElement>("HUDVitals");
    barContainer_->SetAlignment(HA_CENTER, VA_BOTTOM);
    barContainer_->SetPosition(0, -BAR_MARGIN_BOTTOM);
    barContainer_->SetLayout(LM_VERTICAL, BAR_GAP, IntRect(0, 0, 0, 0));

    // Top row: HP | Hunger | Thirst side by side
    topRow_ = barContainer_->CreateChild<UIElement>("HUDTopRow");
    topRow_->SetLayout(LM_HORIZONTAL, BAR_GAP, IntRect(0, 0, 0, 0));
    topRow_->SetHorizontalAlignment(HA_CENTER);

    // Bottom row: Stamina centered
    bottomRow_ = barContainer_->CreateChild<UIElement>("HUDBottomRow");
    bottomRow_->SetLayout(LM_HORIZONTAL, 0, IntRect(0, 0, 0, 0));
    bottomRow_->SetHorizontalAlignment(HA_CENTER);

    CreateBar(hpBar_, "HP");
    CreateBar(hungerBar_, "Hunger");
    CreateBar(thirstBar_, "Thirst");
    CreateBar(staminaBar_, "Stamina");

    // Temperature indicator — text below the bars, hidden when comfortable
    tempIndicator_ = barContainer_->CreateChild<Text>("TempIndicator");
    tempIndicator_->SetFont(font_, fontSize_);
    tempIndicator_->SetHorizontalAlignment(HA_CENTER);
    tempIndicator_->SetColor(Color(1.0f, 1.0f, 1.0f, 0.0f));
    tempIndicator_->SetVisible(false);
}

void HUD::CreateBar(VitalBar& bar, const String& name)
{
    // HP/Hunger/Thirst go in top row, Stamina in bottom row
    UIElement* parent = (name == "Stamina") ? bottomRow_ : topRow_;

    auto* wrapper = parent->CreateChild<UIElement>(name + "Wrapper");
    wrapper->SetFixedSize(BAR_WIDTH, BAR_HEIGHT + 14);  // room for label above
    wrapper->SetLayoutMode(LM_FREE);

    // Label above bar
    bar.label = wrapper->CreateChild<Text>(name + "Label");
    bar.label->SetFont(font_, fontSize_);
    bar.label->SetColor(Color(0.87f, 0.8f, 0.73f));  // #DDCCBB warm off-white
    bar.label->SetText(bar.labelText);
    bar.label->SetHorizontalAlignment(HA_CENTER);
    bar.label->SetPosition(0, 0);

    // Background (dark)
    bar.background = wrapper->CreateChild<BorderImage>(name + "Bg");
    bar.background->SetColor(Color(0.0f, 0.0f, 0.0f, 0.5f));
    bar.background->SetFixedSize(BAR_WIDTH, BAR_HEIGHT);
    bar.background->SetPosition(0, 14);

    // Fill (colored)
    bar.fill = bar.background->CreateChild<BorderImage>(name + "Fill");
    bar.fill->SetColor(bar.barColor);
    bar.fill->SetFixedSize(BAR_WIDTH, BAR_HEIGHT);
    bar.fill->SetPosition(0, 0);

    // Critical text (hidden by default)
    bar.criticalLabel = wrapper->CreateChild<Text>(name + "Critical");
    bar.criticalLabel->SetFont(font_, fontSize_);
    bar.criticalLabel->SetColor(Color(1.0f, 0.3f, 0.2f, 0.0f));
    bar.criticalLabel->SetText(bar.criticalText);
    bar.criticalLabel->SetHorizontalAlignment(HA_CENTER);
    bar.criticalLabel->SetPosition(0, BAR_HEIGHT + 16);
    bar.criticalLabel->SetVisible(false);

    // Start fully transparent
    bar.currentAlpha = 0.0f;
    wrapper->SetOpacity(0.0f);
}

void HUD::Update(float timeStep)
{
    UpdateBar(hpBar_, timeStep);
    UpdateBar(hungerBar_, timeStep);
    UpdateBar(thirstBar_, timeStep);
    UpdateBar(staminaBar_, timeStep);
    UpdateStatusIcons(timeStep);
    UpdateContextHint(timeStep);

    // Temperature indicator — only visible when uncomfortable
    if (tempIndicator_)
    {
        // Thresholds from warmth_rules DB
        const WarmthRules& wr = Creature::GetWarmthRules();
        bool freezing = temperature_ < wr.severeCold;
        bool cold = temperature_ < wr.coldThreshold;
        bool shivering = temperature_ < wr.comfortMin;
        bool hot = temperature_ > wr.heatThreshold;
        bool uncomfortable = shivering || hot;

        float targetAlpha = uncomfortable ? 1.0f : 0.0f;
        if (tempIndicatorAlpha_ < targetAlpha)
            tempIndicatorAlpha_ = Min(tempIndicatorAlpha_ + FADE_SPEED * timeStep, targetAlpha);
        else if (tempIndicatorAlpha_ > targetAlpha)
            tempIndicatorAlpha_ = Max(tempIndicatorAlpha_ - FADE_SPEED * timeStep, targetAlpha);

        tempIndicator_->SetVisible(tempIndicatorAlpha_ > 0.01f);

        if (tempIndicatorAlpha_ > 0.01f)
        {
            Color c;
            String text;
            if (freezing)
            {
                c = Color(0.4f, 0.6f, 1.0f, tempIndicatorAlpha_);  // bright blue
                text = "FREEZING";
            }
            else if (cold)
            {
                c = Color(0.5f, 0.7f, 0.95f, tempIndicatorAlpha_); // light blue
                text = "COLD";
            }
            else if (shivering)
            {
                c = Color(0.7f, 0.8f, 0.9f, tempIndicatorAlpha_);  // pale blue
                text = "Chilly";
            }
            else if (hot)
            {
                c = Color(1.0f, 0.4f, 0.15f, tempIndicatorAlpha_); // orange-red
                text = "OVERHEATING";
            }
            tempIndicator_->SetText(text);
            tempIndicator_->SetColor(c);

            // Pulse when freezing or overheating
            if (freezing || hot)
            {
                float pulse = 0.7f + 0.3f * sinf(GetSubsystem<Time>()->GetElapsedTime() * 3.0f);
                c.a_ = tempIndicatorAlpha_ * pulse;
                tempIndicator_->SetColor(c);
            }
        }
    }
}

void HUD::UpdateBar(VitalBar& bar, float timeStep)
{
    // Determine target alpha
    float targetAlpha = 0.0f;
    if (bar.value < bar.showThreshold)
    {
        targetAlpha = 1.0f;
        bar.hideTimer = 0.0f;
    }
    else
    {
        bar.hideTimer += timeStep;
        if (bar.hideTimer < bar.hideDelay)
            targetAlpha = 1.0f;  // still showing during delay
    }

    // Smooth fade
    if (bar.currentAlpha < targetAlpha)
        bar.currentAlpha = Min(bar.currentAlpha + FADE_SPEED * timeStep, targetAlpha);
    else if (bar.currentAlpha > targetAlpha)
        bar.currentAlpha = Max(bar.currentAlpha - FADE_SPEED * timeStep, targetAlpha);

    // Update fill width
    int fillW = (int)(Clamp(bar.value, 0.0f, 1.0f) * BAR_WIDTH);
    if (bar.fill)
        bar.fill->SetFixedWidth(Max(0, fillW));

    // Critical color swap below 25%
    bool critical = bar.value < CRITICAL_THRESHOLD;
    if (bar.fill)
    {
        Color c = critical ? bar.criticalColor : bar.barColor;
        c.a_ = bar.currentAlpha;
        bar.fill->SetColor(c);
    }

    // Background alpha
    if (bar.background)
    {
        Color bg(0.0f, 0.0f, 0.0f, bar.currentAlpha * 0.5f);
        bar.background->SetColor(bg);
    }

    // Label alpha
    if (bar.label)
    {
        Color lc = bar.label->GetColor(C_TOPLEFT);
        lc.a_ = bar.currentAlpha;
        bar.label->SetColor(lc);
    }

    // Critical text — show at 0% with pulse
    if (bar.criticalLabel && bar.criticalText.Length() > 0)
    {
        bool showCrit = critical && bar.value < 0.01f && bar.currentAlpha > 0.1f;
        bar.criticalLabel->SetVisible(showCrit);
        if (showCrit)
        {
            // Subtle pulse: oscillate alpha between 0.5 and 1.0
            float pulse = 0.75f + 0.25f * sinf(GetSubsystem<Time>()->GetElapsedTime() * 4.0f);
            Color cc(1.0f, 0.3f, 0.2f, pulse * bar.currentAlpha);
            bar.criticalLabel->SetColor(cc);
        }
    }

    // Set parent wrapper opacity for the whole bar group
    UIElement* wrapper = bar.background ? bar.background->GetParent() : nullptr;
    if (wrapper)
        wrapper->SetOpacity(bar.currentAlpha);
}

void HUD::SetHP(float v)
{
    hpBar_.value = Clamp(v, 0.0f, 1.0f);
}

void HUD::SetHunger(float v)
{
    hungerBar_.value = Clamp(v, 0.0f, 1.0f);
}

void HUD::SetThirst(float v)
{
    thirstBar_.value = Clamp(v, 0.0f, 1.0f);
}

void HUD::SetStamina(float v)
{
    staminaBar_.value = Clamp(v, 0.0f, 1.0f);
}

void HUD::SetTemperature(float celsius)
{
    temperature_ = celsius;
}

// ============================================================================
// Phase 2: Status Icons
// ============================================================================

void HUD::CreateStatusIcons()
{
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();

    // Bottom-left container — icons stack vertically upward
    iconContainer_ = root->CreateChild<UIElement>("HUDStatusIcons");
    iconContainer_->SetAlignment(HA_LEFT, VA_BOTTOM);
    iconContainer_->SetPosition(ICON_MARGIN, -ICON_MARGIN);
    iconContainer_->SetLayout(LM_VERTICAL, ICON_GAP, IntRect(0, 0, 0, 0));

    // Icon definitions: symbol, color, context hint on activation
    struct IconDef
    {
        const char* symbol;
        Color color;
        const char* hint;
    };

    IconDef defs[ICON_MAX] = {
        {"*FREEZING*",    Color(0.4f, 0.6f, 1.0f),   "Find shelter or a fire to warm up"},
        {"*HOT*",         Color(1.0f, 0.4f, 0.15f),  "Find shade or water to cool down"},
        {"*STARVING*",    Color(0.9f, 0.3f, 0.1f),   "Eat food — hunt, gather, or fish"},
        {"*THIRSTY*",     Color(0.3f, 0.5f, 0.9f),   "Drink water — river or well (E)"},
        {"*TIRED*",       Color(0.6f, 0.4f, 0.8f),   "Rest or sleep in shelter"},
        {"*WARMTH*",      Color(1.0f, 0.6f, 0.2f),   ""},
        {"*SHELTER*",     Color(0.8f, 0.7f, 0.3f),   ""},
        {"*HEAVY*",       Color(0.9f, 0.8f, 0.2f),   "Drop items to move faster"},
    };

    for (int i = 0; i < ICON_MAX; ++i)
    {
        auto* text = iconContainer_->CreateChild<Text>(String("StatusIcon") + String(i));
        text->SetFont(font_, fontSize_);
        text->SetText(defs[i].symbol);
        text->SetColor(Color(defs[i].color.r_, defs[i].color.g_, defs[i].color.b_, 0.0f));
        text->SetVisible(false);
        icons_[i].element = text;
        icons_[i].currentAlpha = 0.0f;
        icons_[i].active = false;
        icons_[i].wasActive = false;
        icons_[i].hintText = defs[i].hint;
    }
}

void HUD::UpdateStatusIcons(float timeStep)
{
    // Auto-derive icon states from DB thresholds
    const WarmthRules& wr = Creature::GetWarmthRules();
    icons_[ICON_FREEZING].active = temperature_ < wr.coldThreshold;
    icons_[ICON_OVERHEATING].active = temperature_ > wr.heatThreshold;
    icons_[ICON_STARVING].active = hungerBar_.value < (float)Creature::GetHungerRules().criticalThreshold / 100.0f;
    icons_[ICON_DEHYDRATED].active = thirstBar_.value < (float)Creature::GetThirstRules().criticalThreshold / 100.0f;
    icons_[ICON_EXHAUSTED].active = staminaBar_.value < (float)Creature::GetStaminaRules().lowThreshold / 100.0f;

    // Count down status hint timer
    if (statusHintTimer_ > 0.0f)
        statusHintTimer_ -= timeStep;

    for (int i = 0; i < ICON_MAX; ++i)
    {
        // Rising edge: icon just activated — show context hint
        if (icons_[i].active && !icons_[i].wasActive && icons_[i].hintText.Length() > 0)
        {
            contextHintTarget_ = icons_[i].hintText;
            statusHintTimer_ = 4.0f;  // hold status hint for 4 seconds
        }
        icons_[i].wasActive = icons_[i].active;

        float target = icons_[i].active ? 1.0f : 0.0f;
        if (icons_[i].currentAlpha < target)
            icons_[i].currentAlpha = Min(icons_[i].currentAlpha + FADE_SPEED * timeStep, target);
        else if (icons_[i].currentAlpha > target)
            icons_[i].currentAlpha = Max(icons_[i].currentAlpha - FADE_SPEED * timeStep, target);

        bool vis = icons_[i].currentAlpha > 0.01f;
        if (icons_[i].element)
        {
            icons_[i].element->SetVisible(vis);
            if (vis)
            {
                Color c = icons_[i].element->GetColor(C_TOPLEFT);
                c.a_ = icons_[i].currentAlpha;
                icons_[i].element->SetColor(c);
            }
        }
    }
}

void HUD::SetStatusIcon(StatusIcon icon, bool active)
{
    if (icon >= 0 && icon < ICON_MAX)
        icons_[icon].active = active;
}

// ============================================================================
// Phase 2: Context Hints
// ============================================================================

void HUD::CreateContextHint()
{
    auto* ui = GetSubsystem<UI>();
    auto* root = ui->GetRoot();

    contextHintText_ = root->CreateChild<Text>("HUDContextHint");
    contextHintText_->SetFont(font_, fontSize_ + 2);  // slightly larger for readability
    contextHintText_->SetAlignment(HA_RIGHT, VA_BOTTOM);
    contextHintText_->SetPosition(-ICON_MARGIN, -ICON_MARGIN);
    contextHintText_->SetColor(Color(0.87f, 0.8f, 0.73f, 0.0f));  // warm off-white
    contextHintText_->SetVisible(false);
}

void HUD::UpdateContextHint(float timeStep)
{
    if (!contextHintText_)
        return;

    float target = contextHintTarget_.Length() > 0 ? 1.0f : 0.0f;
    if (contextHintAlpha_ < target)
        contextHintAlpha_ = Min(contextHintAlpha_ + FADE_SPEED * timeStep, target);
    else if (contextHintAlpha_ > target)
        contextHintAlpha_ = Max(contextHintAlpha_ - FADE_SPEED * timeStep, target);

    bool vis = contextHintAlpha_ > 0.01f;
    contextHintText_->SetVisible(vis);
    if (vis)
    {
        // Update text when fading in (not while fading out — keep old text during fade)
        if (target > 0.5f)
            contextHintText_->SetText(contextHintTarget_);
        Color c(0.87f, 0.8f, 0.73f, contextHintAlpha_);
        contextHintText_->SetColor(c);
    }
}

void HUD::SetContextHint(const String& text)
{
    // Don't let empty/normal hints override an active status icon hint
    if (text.Empty() && statusHintTimer_ > 0.0f)
        return;

    // A non-empty interaction hint (E: Chop, etc.) always takes priority
    if (!text.Empty())
        statusHintTimer_ = 0.0f;

    contextHintTarget_ = text;
}
