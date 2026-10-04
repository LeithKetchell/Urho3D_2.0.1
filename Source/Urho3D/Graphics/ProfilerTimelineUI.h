// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "ProfilerTimeline.h"

namespace Urho3D
{

class BorderImage;
class Graphics;
class Text;
class UI;
class UIElement;
class Window;

/// Timeline profiler UI overlay (Phase 3 of PLAN_PROFILER_TIMELINE).
///
/// Shows a bottom-of-screen panel with:
///   - Horizontal frame history as vertical bars (height = frame time)
///   - CPU and GPU lanes with colored segments per event
///   - Click-to-select frame detail view
///   - Pause/resume toggle (P key when panel focused)
///
/// Uses the same Urho3D UI system as ProfilerUI — no custom rendering needed.
class ProfilerTimelineUI : public Object
{
    URHO3D_OBJECT(ProfilerTimelineUI, Object);

public:
    ProfilerTimelineUI(Context* context);

    /// Initialize the timeline panel. Call after UI default style is loaded.
    void Initialize(UI* ui, Graphics* graphics);

    /// Update the visualization from ProfilerTimeline data. Call once per frame.
    void Update();

    /// Show/hide the timeline panel.
    void SetVisible(bool visible);
    bool IsVisible() const;

    /// Toggle pause on the underlying ProfilerTimeline.
    void TogglePause();

    /// Target frame time (ms) for bar height scaling. Default 16.67ms (60 fps).
    void SetTargetFrameTime(float ms) { targetFrameTimeMs_ = ms; }

private:
    /// Rebuild the frame bar elements from current ProfilerTimeline data.
    void RebuildFrameBars();

    /// Rebuild the detail view for the selected frame.
    void RebuildDetailView();

    /// Get color for a category string.
    Color CategoryColor(const String& category) const;

    Graphics* graphics_{};
    UI* ui_{};

    /// Root panel anchored to bottom of screen.
    SharedPtr<Window> panel_;

    /// Container for frame bar elements (horizontal row).
    SharedPtr<UIElement> barContainer_;

    /// Container for the detail lane view (selected frame).
    SharedPtr<UIElement> detailContainer_;

    /// Status/info text (FPS, selected frame info).
    SharedPtr<Text> statusText_;

    /// Currently selected frame index in ProfilerTimeline::GetFrames().
    int selectedFrame_{-1};

    /// Target frame time for scaling bar heights.
    float targetFrameTimeMs_{16.67f};

    /// Panel height in pixels.
    static const int PANEL_HEIGHT = 120;

    /// Width of each frame bar in pixels.
    static const int BAR_WIDTH = 3;

    /// Maximum number of frame bars to display.
    static const int MAX_BARS = 200;

    /// Height of the bar area.
    static const int BAR_AREA_HEIGHT = 60;

    /// Update throttle accumulator.
    float updateAccum_{0.0f};
};

} // namespace Urho3D
