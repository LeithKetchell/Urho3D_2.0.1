// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "../Math/Vector3.h"
#include "../UI/Window.h"
#include "../UI/Text.h"
#include "../Graphics/VulkanProfiler.h"

namespace Urho3D
{

class UI;

/// Profiler UI overlay for displaying FPS and performance metrics
class ProfilerUI : public Object
{
    URHO3D_OBJECT(ProfilerUI, Object);

public:
    /// Constructor
    ProfilerUI(Context* context);

    /// Initialize profiler UI
    void Initialize(UI* ui, VulkanProfiler* profiler, Graphics* graphics = nullptr);

    /// Update profiler display (throttled to ~4Hz to avoid per-frame SetText overhead)
    void Update(float timeStep = 0.0f);

    /// Show/hide profiler UI
    void SetVisible(bool visible);

    /// Check if profiler UI is visible
    bool IsVisible() const { return window_ ? window_->IsVisible() : false; }

    /// Set custom stats (mushroom count, batches, etc)
    void SetCustomStats(const String& stats);

    /// Set camera position for display
    void SetCameraPos(const Vector3& pos);

private:
    /// Root window for profiler display
    SharedPtr<Window> window_;

    /// Text element for FPS
    SharedPtr<Text> fpsText_;

    /// Text element for frame time
    SharedPtr<Text> frameTimeText_;

    /// Text element for average frame time
    SharedPtr<Text> avgFrameTimeText_;

    /// Text element for custom stats
    SharedPtr<Text> customStatsText_;

    /// Text element for instance stats
    SharedPtr<Text> instanceStatsText_;

    /// Text element for renderer stats (batches, triangles, pipeline changes)
    SharedPtr<Text> rendererStatsText_;

    /// Text element for camera position
    SharedPtr<Text> cameraPosText_;

    /// Profiler reference
    VulkanProfiler* profiler_;

    /// Graphics reference for instance stats
    Graphics* graphics_;

    /// Accumulator for throttling text updates (seconds)
    float updateAccum_ = 0.0f;
};

} // namespace Urho3D
