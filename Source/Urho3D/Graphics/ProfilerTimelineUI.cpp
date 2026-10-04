// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#include "Precompiled.h"
#include "ProfilerTimelineUI.h"
#include "Graphics.h"
#include "../UI/UI.h"
#include "../UI/UIElement.h"
#include "../UI/Window.h"
#include "../UI/Text.h"
#include "../UI/BorderImage.h"
#include "../IO/Log.h"

namespace Urho3D
{

// Category color map — deterministic colors for common categories
static const struct { const char* name; Color color; } kCategoryColors[] = {
    {"frame",   Color(0.3f, 0.3f, 0.3f)},     // dark gray
    {"render",  Color(0.2f, 0.5f, 0.9f)},     // blue
    {"shadow",  Color(0.9f, 0.2f, 0.2f)},     // red
    {"post",    Color(0.9f, 0.8f, 0.2f)},     // yellow
    {"physics", Color(0.2f, 0.8f, 0.3f)},     // green
    {"anim",    Color(0.7f, 0.3f, 0.8f)},     // purple
    {"ui",      Color(0.5f, 0.5f, 0.5f)},     // gray
    {"water",   Color(0.2f, 0.7f, 0.9f)},     // cyan
    {"fish",    Color(0.1f, 0.6f, 0.6f)},     // teal
    {"weather", Color(0.6f, 0.6f, 0.8f)},     // light purple
    {nullptr,   Color(0.6f, 0.6f, 0.6f)}      // default
};

ProfilerTimelineUI::ProfilerTimelineUI(Context* context) : Object(context)
{
}

void ProfilerTimelineUI::Initialize(UI* ui, Graphics* graphics)
{
    ui_ = ui;
    graphics_ = graphics;

    if (!ui_ || !graphics_)
        return;

    auto* root = ui_->GetRoot();
    int rootWidth = root->GetWidth();
    int rootHeight = root->GetHeight();
    if (rootWidth <= 0) rootWidth = 1280;
    if (rootHeight <= 0) rootHeight = 720;

    // Create root panel — bottom-of-screen docked window
    panel_ = root->CreateChild<Window>();
    panel_->SetStyleAuto();
    panel_->SetPosition(0, rootHeight - PANEL_HEIGHT);
    panel_->SetSize(rootWidth, PANEL_HEIGHT);
    panel_->SetOpacity(0.85f);
    panel_->SetMovable(false);
    panel_->SetResizable(false);
    panel_->SetLayout(LM_VERTICAL, 2, IntRect(4, 4, 4, 4));

    // Status bar at top of panel
    statusText_ = panel_->CreateChild<Text>();
    statusText_->SetStyleAuto();
    statusText_->SetFontSize(11);
    statusText_->SetText("Timeline Profiler  [P] Pause");

    // Frame bar area — horizontal container for bar elements
    barContainer_ = panel_->CreateChild<UIElement>();
    barContainer_->SetFixedHeight(BAR_AREA_HEIGHT);
    barContainer_->SetLayout(LM_HORIZONTAL, 1, IntRect(0, 0, 0, 0));

    // Detail area — shows CPU/GPU lanes for selected frame
    detailContainer_ = panel_->CreateChild<UIElement>();
    detailContainer_->SetLayout(LM_VERTICAL, 1, IntRect(0, 0, 0, 0));

    panel_->SetVisible(false);

    URHO3D_LOGINFO("ProfilerTimelineUI initialized");
}

void ProfilerTimelineUI::Update()
{
    if (!panel_ || !panel_->IsVisible() || !graphics_)
        return;

    ProfilerTimeline* timeline = graphics_->GetProfilerTimeline();
    if (!timeline)
        return;

    // Throttle UI rebuild to ~10 Hz to avoid per-frame UI element churn
    updateAccum_ += 0.016f;  // approximate — caller should pass real dt
    if (updateAccum_ < 0.1f)
        return;
    updateAccum_ = 0.0f;

    RebuildFrameBars();

    if (selectedFrame_ >= 0)
        RebuildDetailView();
}

void ProfilerTimelineUI::SetVisible(bool visible)
{
    if (panel_)
        panel_->SetVisible(visible);
}

bool ProfilerTimelineUI::IsVisible() const
{
    return panel_ ? panel_->IsVisible() : false;
}

void ProfilerTimelineUI::TogglePause()
{
    if (!graphics_)
        return;
    ProfilerTimeline* timeline = graphics_->GetProfilerTimeline();
    if (timeline)
    {
        timeline->SetPaused(!timeline->IsPaused());
        if (statusText_)
        {
            String status = timeline->IsPaused() ? "PAUSED" : "Timeline Profiler";
            statusText_->SetText(status + "  [P] Pause");
        }
    }
}

void ProfilerTimelineUI::RebuildFrameBars()
{
    ProfilerTimeline* timeline = graphics_->GetProfilerTimeline();
    if (!timeline || !barContainer_)
        return;

    const auto& frames = timeline->GetFrames();
    unsigned frameCount = frames.Size();
    if (frameCount == 0)
        return;

    // Remove old bar elements
    barContainer_->RemoveAllChildren();

    // Show last MAX_BARS frames (newest on right)
    unsigned startIdx = frameCount > (unsigned)MAX_BARS ? frameCount - MAX_BARS : 0;
    float maxMs = targetFrameTimeMs_ * 2.0f;  // bars up to 2x target = full height

    for (unsigned i = startIdx; i < frameCount; ++i)
    {
        const auto& frame = frames[i];
        float frameMs = (float)frame.DurationUs() / 1000.0f;
        float ratio = frameMs / maxMs;
        if (ratio > 1.0f) ratio = 1.0f;
        if (ratio < 0.02f) ratio = 0.02f;

        int barHeight = (int)(ratio * BAR_AREA_HEIGHT);

        auto* bar = barContainer_->CreateChild<BorderImage>();
        bar->SetFixedSize(BAR_WIDTH, BAR_AREA_HEIGHT);
        bar->SetVerticalAlignment(VA_BOTTOM);

        // Color code: green if under target, yellow if near, red if over
        Color barColor;
        if (frameMs <= targetFrameTimeMs_)
            barColor = Color(0.2f, 0.8f, 0.3f);
        else if (frameMs <= targetFrameTimeMs_ * 1.5f)
            barColor = Color(0.9f, 0.8f, 0.2f);
        else
            barColor = Color(0.9f, 0.2f, 0.2f);

        // Draw the bar as a colored rect within the fixed-size element
        // We create a child element for the actual colored portion
        auto* colorBar = bar->CreateChild<BorderImage>();
        colorBar->SetColor(barColor);
        colorBar->SetFixedSize(BAR_WIDTH, barHeight);
        colorBar->SetVerticalAlignment(VA_BOTTOM);

        // If this is the selected frame, draw a highlight border
        if ((int)i == selectedFrame_)
            colorBar->SetColor(Color::WHITE);
    }

    // Update status text with latest frame info
    if (statusText_ && frameCount > 0)
    {
        const auto& latest = frames.Back();
        float latestMs = (float)latest.DurationUs() / 1000.0f;
        unsigned cpuEvents = 0;
        unsigned gpuEvents = 0;
        for (const auto& e : latest.events)
        {
            if (e.lane == ProfilerTimeline::LANE_GPU)
                ++gpuEvents;
            else
                ++cpuEvents;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "%s  %.1fms  CPU:%u  GPU:%u  [P] Pause  [Click bar for detail]",
                 timeline->IsPaused() ? "PAUSED" : "Timeline",
                 latestMs, cpuEvents, gpuEvents);
        statusText_->SetText(String(buf));
    }
}

void ProfilerTimelineUI::RebuildDetailView()
{
    ProfilerTimeline* timeline = graphics_->GetProfilerTimeline();
    if (!timeline || !detailContainer_ || selectedFrame_ < 0)
        return;

    detailContainer_->RemoveAllChildren();

    const auto& frames = timeline->GetFrames();
    if (selectedFrame_ >= (int)frames.Size())
    {
        selectedFrame_ = -1;
        return;
    }

    const auto& frame = frames[selectedFrame_];
    if (frame.events.Empty())
        return;

    float frameMs = (float)frame.DurationUs() / 1000.0f;
    int detailWidth = detailContainer_->GetWidth();
    if (detailWidth <= 0) detailWidth = 800;

    // CPU lane
    auto* cpuLabel = detailContainer_->CreateChild<Text>();
    cpuLabel->SetStyleAuto();
    cpuLabel->SetFontSize(10);
    cpuLabel->SetText("CPU");

    auto* cpuLane = detailContainer_->CreateChild<UIElement>();
    cpuLane->SetFixedHeight(16);
    cpuLane->SetFixedWidth(detailWidth);

    // GPU lane
    auto* gpuLabel = detailContainer_->CreateChild<Text>();
    gpuLabel->SetStyleAuto();
    gpuLabel->SetFontSize(10);
    gpuLabel->SetText("GPU");

    auto* gpuLane = detailContainer_->CreateChild<UIElement>();
    gpuLane->SetFixedHeight(16);
    gpuLane->SetFixedWidth(detailWidth);

    // Plot events as colored segments in the appropriate lane
    unsigned long long frameStart = frame.frameStartUs;
    float usPerPixel = frameMs > 0.0f ? (frame.DurationUs() / (float)detailWidth) : 1.0f;

    for (const auto& e : frame.events)
    {
        float durationUs = (float)e.DurationUs();
        if (durationUs < 1.0f)
            continue;

        UIElement* lane = (e.lane == ProfilerTimeline::LANE_GPU) ? gpuLane : cpuLane;

        // For GPU events, times are absolute GPU nanosecond-derived values.
        // Normalize them relative to the first GPU event in this frame.
        int xPos, width;
        if (e.lane == ProfilerTimeline::LANE_GPU)
        {
            // GPU timestamps are in their own time domain. Find the range of GPU events
            // and map proportionally to the lane width.
            unsigned long long gpuMin = ~0ULL, gpuMax = 0;
            for (const auto& ge : frame.events)
            {
                if (ge.lane == ProfilerTimeline::LANE_GPU)
                {
                    if (ge.startUs < gpuMin) gpuMin = ge.startUs;
                    if (ge.endUs > gpuMax) gpuMax = ge.endUs;
                }
            }
            unsigned long long gpuRange = gpuMax > gpuMin ? gpuMax - gpuMin : 1;
            xPos = (int)((e.startUs - gpuMin) * (unsigned long long)detailWidth / gpuRange);
            width = (int)(e.DurationUs() * (unsigned long long)detailWidth / gpuRange);
        }
        else
        {
            xPos = (int)((e.startUs - frameStart) / usPerPixel);
            width = (int)(durationUs / usPerPixel);
        }

        if (width < 1) width = 1;
        if (xPos < 0) xPos = 0;
        if (xPos + width > detailWidth) width = detailWidth - xPos;

        auto* seg = lane->CreateChild<BorderImage>();
        seg->SetColor(CategoryColor(e.category));
        seg->SetPosition(xPos, 0);
        seg->SetFixedSize(width, 14);
    }
}

Color ProfilerTimelineUI::CategoryColor(const String& category) const
{
    for (int i = 0; kCategoryColors[i].name != nullptr; ++i)
    {
        if (category == kCategoryColors[i].name)
            return kCategoryColors[i].color;
    }
    return kCategoryColors[sizeof(kCategoryColors) / sizeof(kCategoryColors[0]) - 1].color;
}

} // namespace Urho3D
