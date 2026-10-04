// Copyright (c) 2008-2024 the Urho3D project
// License: MIT

#pragma once

#include "../Container/Vector.h"
#include "../Container/Str.h"
#include "../Container/RefCounted.h"

#include <chrono>

namespace Urho3D
{

/// Phase 1 of the In-Engine Profiler Timeline plan.
///
/// Records per-frame events as flat lists of {name, category, startUs, endUs, depth}.
/// Pure CPU wall-clock timing. GPU lane (Phase 2) will add Vulkan timestamp queries
/// alongside the same ring buffer. UI rendering (Phase 3) reads from `GetFrames()`.
///
/// This is a recording infrastructure ONLY. It does not instrument any engine paths
/// by itself — call sites add `URHO3D_TIMELINE_EVENT(name)` (or `BeginEvent` /
/// `EndEvent` directly) where they want measurement. Phase 2+ adds call sites to
/// `View::Render`, `Renderer::Render`, physics step, animation update, etc.
///
/// Design notes:
///  - Header-only so it can be used from any module without library boundary friction
///  - Ring buffer of N frames (default 300 = 5 seconds at 60fps)
///  - Each frame stores a contiguous Vector<Event> — depth field encodes nesting
///  - Open events live on a stack until EndEvent pops them
///  - All times in microseconds since the timeline was created
///  - Singleton-friendly: caller owns the instance, register with Context if desired
class ProfilerTimeline : public RefCounted
{
public:
    /// Lane identifier for CPU vs GPU events.
    enum Lane
    {
        LANE_CPU = 0,
        LANE_GPU = 1
    };

    /// One recorded scope. Times are microseconds since the ProfilerTimeline epoch.
    struct Event
    {
        String   name;
        String   category;
        unsigned long long startUs{0};
        unsigned long long endUs{0};
        int      depth{0};
        Lane     lane{LANE_CPU};

        unsigned long long DurationUs() const { return endUs >= startUs ? endUs - startUs : 0; }
    };

    /// Instant marker (zero-duration annotation, e.g. toggle change).
    struct Marker
    {
        String   name;
        unsigned long long timeUs{0};
    };

    /// One captured frame: a flat list of events plus the frame's own start/end stamps.
    struct Frame
    {
        Vector<Event>      events;
        Vector<Marker>     markers;  ///< Instant annotations within this frame
        unsigned long long frameStartUs{0};
        unsigned long long frameEndUs{0};

        unsigned long long DurationUs() const { return frameEndUs >= frameStartUs ? frameEndUs - frameStartUs : 0; }
    };

    ProfilerTimeline()
        : epoch_(std::chrono::high_resolution_clock::now())
    {
    }

    /// Maximum number of completed frames to retain. Default 300 = ~5s at 60fps.
    /// Calling this clears existing frames if shrinking.
    void SetMaxFrames(unsigned n)
    {
        maxFrames_ = n > 0 ? n : 1;
        while (frames_.Size() > maxFrames_)
            frames_.Erase((unsigned)0);
    }
    unsigned GetMaxFrames() const { return maxFrames_; }

    /// Pause/unpause capture. Begin/End calls are silently ignored while paused.
    /// Useful for freezing the buffer to inspect a specific frame in the UI.
    void SetPaused(bool paused) { paused_ = paused; }
    bool IsPaused() const { return paused_; }

    /// Begin a new frame. Closes any in-flight frame first (defensive).
    void BeginFrame()
    {
        if (paused_)
            return;

        if (currentFrameOpen_)
            EndFrame();  // recover from a previous frame that forgot to close

        // Drop oldest frame if at capacity
        if (frames_.Size() >= maxFrames_)
            frames_.Erase((unsigned)0);

        Frame f;
        f.frameStartUs = NowUs();
        frames_.Push(f);
        currentFrameOpen_ = true;
        openEventStack_.Clear();
    }

    /// Close the current frame. Auto-closes any unmatched BeginEvent calls.
    void EndFrame()
    {
        if (paused_ || !currentFrameOpen_ || frames_.Empty())
            return;

        // Force-close any leaked open events
        while (!openEventStack_.Empty())
            EndEvent();

        Frame& f = frames_.Back();
        f.frameEndUs = NowUs();
        currentFrameOpen_ = false;
    }

    /// Begin a scoped event. Pairs with EndEvent. Can be nested.
    void BeginEvent(const String& name, const String& category = String::EMPTY)
    {
        if (paused_ || !currentFrameOpen_ || frames_.Empty())
            return;

        Frame& f = frames_.Back();
        Event e;
        e.name     = name;
        e.category = category;
        e.startUs  = NowUs();
        e.depth    = (int)openEventStack_.Size();
        f.events.Push(e);

        // Stack stores the index of the event in f.events so EndEvent can patch it
        openEventStack_.Push(f.events.Size() - 1);
    }

    /// End the most recently opened event in the current frame.
    void EndEvent()
    {
        if (paused_ || !currentFrameOpen_ || frames_.Empty() || openEventStack_.Empty())
            return;

        unsigned idx = openEventStack_.Back();
        openEventStack_.Pop();

        Frame& f = frames_.Back();
        if (idx < f.events.Size())
            f.events[idx].endUs = NowUs();
    }

    /// Read access to the captured frames (oldest first, newest last).
    const Vector<Frame>& GetFrames() const { return frames_; }

    /// Number of frames currently retained.
    unsigned GetFrameCount() const { return frames_.Size(); }

    /// Add an instant marker to the current frame (zero-duration annotation).
    /// Use for toggle state changes, one-shot events, etc.
    void AddMarker(const String& name)
    {
        if (paused_ || !currentFrameOpen_ || frames_.Empty())
            return;

        Frame& f = frames_.Back();
        Marker m;
        m.name = name;
        m.timeUs = NowUs();
        f.markers.Push(m);
    }

    /// Inject a pre-computed GPU event into the most recently completed frame.
    /// Called from the GPU timestamp readback path — results arrive 1-2 frames late,
    /// so they're stitched into the last closed frame rather than the current one.
    void InjectGpuEvent(const String& name, const String& category,
                        unsigned long long startUs, unsigned long long endUs, int depth = 0)
    {
        if (paused_ || frames_.Empty())
            return;

        // Find the last completed frame (not the currently open one)
        unsigned targetIdx = frames_.Size() - 1;
        if (currentFrameOpen_ && targetIdx > 0)
            --targetIdx;

        Frame& f = frames_[targetIdx];
        Event e;
        e.name     = name;
        e.category = category;
        e.startUs  = startUs;
        e.endUs    = endUs;
        e.depth    = depth;
        e.lane     = LANE_GPU;
        f.events.Push(e);
    }

    /// Clear all captured data.
    void Clear()
    {
        frames_.Clear();
        openEventStack_.Clear();
        currentFrameOpen_ = false;
    }

    /// Microseconds since this ProfilerTimeline's epoch.
    unsigned long long NowUs() const
    {
        auto now = std::chrono::high_resolution_clock::now();
        return (unsigned long long)
            std::chrono::duration_cast<std::chrono::microseconds>(now - epoch_).count();
    }

    /// Export all captured frames to Chrome trace JSON format (chrome://tracing).
    /// Returns the JSON string. Caller writes it to a file.
    String ExportChromeTrace() const
    {
        String json;
        json += "{\"traceEvents\":[\n";
        bool first = true;

        for (unsigned fi = 0; fi < frames_.Size(); ++fi)
        {
            const Frame& f = frames_[fi];
            for (unsigned ei = 0; ei < f.events.Size(); ++ei)
            {
                const Event& e = f.events[ei];
                if (!first) json += ",\n";
                first = false;

                // Chrome trace format: {"name","cat","ph","ts","dur","pid","tid"}
                // ph: "X" = complete event
                // ts: start time in microseconds
                // dur: duration in microseconds
                // tid: 0 for CPU, 1 for GPU
                json += "{\"name\":\"";
                json += e.name;
                json += "\",\"cat\":\"";
                json += e.category.Empty() ? "default" : e.category;
                json += "\",\"ph\":\"X\",\"ts\":";
                json += String((unsigned long long)e.startUs);
                json += ",\"dur\":";
                json += String((unsigned long long)e.DurationUs());
                json += ",\"pid\":0,\"tid\":";
                json += String(e.lane == LANE_GPU ? 1 : 0);
                json += "}";
            }
        }

        // Export markers as instant events
        for (unsigned fi = 0; fi < frames_.Size(); ++fi)
        {
            const Frame& f = frames_[fi];
            for (unsigned mi = 0; mi < f.markers.Size(); ++mi)
            {
                const Marker& m = f.markers[mi];
                if (!first) json += ",\n";
                first = false;

                json += "{\"name\":\"";
                json += m.name;
                json += "\",\"cat\":\"marker\",\"ph\":\"i\",\"ts\":";
                json += String((unsigned long long)m.timeUs);
                json += ",\"pid\":0,\"tid\":0,\"s\":\"g\"}";
            }
        }

        json += "\n]}";
        return json;
    }

private:
    std::chrono::high_resolution_clock::time_point epoch_;
    Vector<Frame>     frames_;
    Vector<unsigned>  openEventStack_;  // indices into the current frame's events vector
    unsigned          maxFrames_{300};
    bool              currentFrameOpen_{false};
    bool              paused_{false};
};

/// RAII helper for scoped events. Use via the URHO3D_TIMELINE_EVENT macro.
class ProfilerTimelineScope
{
public:
    ProfilerTimelineScope(ProfilerTimeline* timeline, const String& name, const String& category)
        : timeline_(timeline)
    {
        if (timeline_)
            timeline_->BeginEvent(name, category);
    }

    ~ProfilerTimelineScope()
    {
        if (timeline_)
            timeline_->EndEvent();
    }

    ProfilerTimelineScope(const ProfilerTimelineScope&) = delete;
    ProfilerTimelineScope& operator=(const ProfilerTimelineScope&) = delete;

private:
    ProfilerTimeline* timeline_;
};

/// RAII helper for scoped frames. BeginFrame on construction, EndFrame on
/// destruction. Drop into the top of any per-frame update routine and the
/// frame closes automatically on every exit path (early return, exception).
class ProfilerTimelineFrameScope
{
public:
    explicit ProfilerTimelineFrameScope(ProfilerTimeline* timeline)
        : timeline_(timeline)
    {
        if (timeline_)
            timeline_->BeginFrame();
    }

    ~ProfilerTimelineFrameScope()
    {
        if (timeline_)
            timeline_->EndFrame();
    }

    ProfilerTimelineFrameScope(const ProfilerTimelineFrameScope&) = delete;
    ProfilerTimelineFrameScope& operator=(const ProfilerTimelineFrameScope&) = delete;

private:
    ProfilerTimeline* timeline_;
};

} // namespace Urho3D

/// Convenience macro: declare a scoped event for the rest of the current C++ scope.
/// Caller must supply a `ProfilerTimeline*` named `timeline`. The macro is
/// deliberately untied to a global singleton — Phase 2+ will wire timeline access
/// through the existing Graphics subsystem so engine call sites can use a shorter
/// macro form.
#define URHO3D_TIMELINE_EVENT(timeline, name) \
    Urho3D::ProfilerTimelineScope _urho3d_timeline_scope_##__LINE__((timeline), (name), Urho3D::String::EMPTY)

#define URHO3D_TIMELINE_EVENT_CAT(timeline, name, category) \
    Urho3D::ProfilerTimelineScope _urho3d_timeline_scope_##__LINE__((timeline), (name), (category))
