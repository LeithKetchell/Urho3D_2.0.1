// Copyright (c) 2008-2025 the Urho3D project
// License: MIT

#include "Precompiled.h"
#include "ProfilerUI.h"
#include "Graphics.h"
#include "../UI/UI.h"
#include "../UI/UIElement.h"
#include "../UI/UIEvents.h"

namespace Urho3D
{

ProfilerUI::ProfilerUI(Context* context) : Object(context), profiler_(nullptr), graphics_(nullptr)
{
}

void ProfilerUI::Initialize(UI* ui, VulkanProfiler* profiler, Graphics* graphics)
{
    profiler_ = profiler;
    graphics_ = graphics ? graphics : GetSubsystem<Graphics>();

    if (!ui)
        return;

    // Create root window with vertical layout
    window_ = ui->GetRoot()->CreateChild<Window>();
    window_->SetStyleAuto();
    window_->SetPosition(10, 10);
    window_->SetOpacity(0.9f);
    window_->SetMovable(true);
    window_->SetResizable(false);
    window_->SetEnabled(true);
    window_->SetLayout(LM_VERTICAL, 6, IntRect(8, 8, 8, 8));  // Vertical layout with spacing and padding

    // Create title
    auto title = window_->CreateChild<Text>();
    title->SetStyleAuto();
    title->SetText("Performance");
    title->SetFontSize(14);

    // FPS text
    fpsText_ = window_->CreateChild<Text>();
    fpsText_->SetStyleAuto();
    fpsText_->SetFontSize(12);
    fpsText_->SetText("FPS: 0");

    // Frame time text
    frameTimeText_ = window_->CreateChild<Text>();
    frameTimeText_->SetStyleAuto();
    frameTimeText_->SetFontSize(12);
    frameTimeText_->SetText("Frame: 0.00ms");

    // Average frame time text
    avgFrameTimeText_ = window_->CreateChild<Text>();
    avgFrameTimeText_->SetStyleAuto();
    avgFrameTimeText_->SetFontSize(12);
    avgFrameTimeText_->SetText("Avg: 0.00ms");

    // Instance stats text
    instanceStatsText_ = window_->CreateChild<Text>();
    instanceStatsText_->SetStyleAuto();
    instanceStatsText_->SetFontSize(12);
    instanceStatsText_->SetText("Instances: 0");

    // Renderer stats (batches, triangles, pipeline changes)
    rendererStatsText_ = window_->CreateChild<Text>();
    rendererStatsText_->SetStyleAuto();
    rendererStatsText_->SetFontSize(12);
    rendererStatsText_->SetText("Tri: 0  Bat: 0  Pip: 0");

    // Camera position text
    cameraPosText_ = window_->CreateChild<Text>();
    cameraPosText_->SetStyleAuto();
    cameraPosText_->SetFontSize(12);
    cameraPosText_->SetText("Cam: 0, 0, 0");

    // Custom stats text
    customStatsText_ = window_->CreateChild<Text>();
    customStatsText_->SetStyleAuto();
    customStatsText_->SetFontSize(12);
    customStatsText_->SetText("");
}

void ProfilerUI::Update(float timeStep)
{
    // Always record frame data even if UI is hidden (maintains accurate rolling average)
    if (profiler_)
        profiler_->RecordFrame(timeStep);

    if (!profiler_ || !window_ || !window_->IsVisible())
        return;

    // Throttle text updates to ~4 Hz (every 250ms) to avoid per-frame SetText / UI layout overhead
    updateAccum_ += timeStep;
    if (updateAccum_ < 0.25f)
        return;
    updateAccum_ = 0.0f;

    float fps = profiler_->GetFPS();
    float frameTime = profiler_->GetFrameTime();
    float avgFrameTime = profiler_->GetAverageFrameTime();

    if (fpsText_)
        fpsText_->SetText(String("FPS: ") + String((int)fps));

    if (frameTimeText_)
    {
        char buf[32];
        sprintf(buf, "Frame: %.2fms", frameTime);
        frameTimeText_->SetText(String(buf));
    }

    if (avgFrameTimeText_)
    {
        char buf[32];
        sprintf(buf, "Avg: %.2fms", avgFrameTime);
        avgFrameTimeText_->SetText(String(buf));
    }

    if (instanceStatsText_ && graphics_)
    {
        char buf[64];
        sprintf(buf, "Inst: %u draws, %u total",
                graphics_->GetNumInstancedDrawCalls(),
                graphics_->GetTotalInstanceCount());
        instanceStatsText_->SetText(String(buf));
    }

    if (rendererStatsText_ && graphics_)
    {
        char buf[96];
        sprintf(buf, "Tri: %u  Bat: %u  Pip: %u  VB: %u",
                graphics_->GetNumPrimitives(),
                graphics_->GetNumBatches(),
                graphics_->GetNumPipelineChanges(),
                graphics_->GetNumVertexBufferBinds());
        rendererStatsText_->SetText(String(buf));
    }
}

void ProfilerUI::SetVisible(bool visible)
{
    if (window_)
        window_->SetVisible(visible);
}

void ProfilerUI::SetCustomStats(const String& stats)
{
    if (customStatsText_)
        customStatsText_->SetText(stats);
}

void ProfilerUI::SetCameraPos(const Vector3& pos)
{
    if (cameraPosText_)
    {
        char buf[64];
        sprintf(buf, "Cam: %.1f, %.1f, %.1f", pos.x_, pos.y_, pos.z_);
        cameraPosText_->SetText(String(buf));
    }
}

} // namespace Urho3D
