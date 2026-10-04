#pragma once

#include <Urho3D/UI/UIElement.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Math/Color.h>

using namespace Urho3D;

namespace Urho3D { class UIBatch; }

/// Drop-in 2D progress graph. Feed it scalar samples; it draws an autoscaled
/// polyline inside its own UI bounds (clipped to the scissor). The two versions
/// collapse to one component with a mode flag — everything is shared except the
/// interpolation: STRAIGHT connects the raw points, CATMULL_ROM draws a smooth
/// curve that still passes through every point (interpolating, not approximating).
class ProgressGraph : public UIElement
{
    URHO3D_OBJECT(ProgressGraph, UIElement);

public:
    enum Mode { STRAIGHT = 0, CATMULL_ROM };

    explicit ProgressGraph(Context* context);
    ~ProgressGraph() override;
    static void RegisterObject(Context* context);

    void GetBatches(Vector<UIBatch>& batches, Vector<float>& vertexData, const IntRect& currentScissor) override;

    /// Push a new sample; the oldest is dropped past capacity.
    void AddValue(float v);
    void Clear();

    void SetCapacity(unsigned n);
    void SetMode(Mode m) { mode_ = m; }
    void SetLineColor(const Color& c) { lineColor_ = c; }
    /// Translucent overlay backing drawn behind the curve; alpha 0 = no fill.
    void SetBackgroundColor(const Color& c) { bgColor_ = c; }
    void SetThickness(float t) { thickness_ = Max(1.0f, t); }
    /// Fix the Y range. Default is autoscale to the data min/max.
    void SetRange(float minY, float maxY) { minY_ = minY; maxY_ = maxY; autoScale_ = false; }
    void SetAutoScale(bool e) { autoScale_ = e; }
    /// Anchor autoscale: enforce a MINIMUM Y span so a near-constant series isn't amplified to full height.
    /// When the data range is smaller than this, the band is floored to it and CENTERED on the data, so a flat
    /// line reads flat (sits mid-height) instead of exploding into noise. 0 = disabled (pure autoscale).
    void SetMinSpan(float s) { minSpan_ = Max(0.0f, s); }
    void SetSubdivisions(unsigned n) { subdiv_ = Max(1u, n); }

    Mode GetMode() const { return mode_; }
    unsigned GetCount() const { return values_.Size(); }

private:
    /// Scalar Catmull-Rom (uniform spacing), t in [0,1] between p1 and p2.
    static float CatmullRom(float p0, float p1, float p2, float p3, float t);

    Vector<float> values_;
    unsigned capacity_{256};
    Mode mode_{CATMULL_ROM};
    Color lineColor_{0.4f, 0.9f, 0.5f, 1.0f};
    Color bgColor_{0.0f, 0.0f, 0.0f, 0.0f};   ///< Overlay backing; alpha 0 = no fill.
    float thickness_{1.5f};
    bool autoScale_{true};
    float minY_{0.0f};
    float maxY_{1.0f};
    float minSpan_{0.0f};   ///< Anchored autoscale: minimum Y span (0 = disabled). See SetMinSpan.
    unsigned subdiv_{16};   ///< Catmull-Rom tessellation steps per segment.
};
