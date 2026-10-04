#include "ProgressGraph.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/GraphicsAPI/GraphicsDefs.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/Math/Matrix3x4.h>
#include <Urho3D/Math/Vector2.h>
#include <Urho3D/UI/UIBatch.h>

ProgressGraph::ProgressGraph(Context* context) : UIElement(context)
{
}

ProgressGraph::~ProgressGraph() = default;

void ProgressGraph::RegisterObject(Context* context)
{
    context->RegisterFactory<ProgressGraph>();
}

void ProgressGraph::AddValue(float v)
{
    values_.Push(v);
    while (values_.Size() > capacity_)
        values_.Erase(0);
}

void ProgressGraph::Clear()
{
    values_.Clear();
}

void ProgressGraph::SetCapacity(unsigned n)
{
    capacity_ = Max(2u, n);
    while (values_.Size() > capacity_)
        values_.Erase(0);
}

float ProgressGraph::CatmullRom(float p0, float p1, float p2, float p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    // Standard Catmull-Rom basis: interpolating, so the curve passes through p1 and p2.
    return 0.5f * ((2.0f * p1)
        + (-p0 + p2) * t
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void ProgressGraph::GetBatches(Vector<UIBatch>& batches, Vector<float>& vertexData, const IntRect& currentScissor)
{
    const unsigned n = values_.Size();
    if (n < 2)
        return;

    // Y range — autoscale to the data unless a fixed range was set.
    float lo = minY_, hi = maxY_;
    if (autoScale_)
    {
        lo = M_INFINITY;
        hi = -M_INFINITY;
        for (unsigned i = 0; i < n; ++i)
        {
            lo = Min(lo, values_[i]);
            hi = Max(hi, values_[i]);
        }
    }
    float span = hi - lo;
    if (span < M_EPSILON)
        span = 1.0f;
    // Anchored autoscale: if the data barely moves, don't stretch that tiny wobble to full height. Floor the
    // span to minSpan_ and re-center on the data midpoint, so a flat series reads flat (sits mid-height).
    if (minSpan_ > 0.0f && span < minSpan_)
    {
        const float mid = (lo + hi) * 0.5f;
        lo = mid - minSpan_ * 0.5f;
        span = minSpan_;
    }

    const IntVector2 size = GetSize();
    const float w = (float)size.x_;
    const float h = (float)size.y_;
    if (w < 2.0f || h < 2.0f)
        return;

    // Map sample (index, value) -> element-local point. Larger value plots higher.
    auto toPoint = [&](float idx, float val) -> Vector2
    {
        const float x = (idx / (float)(n - 1)) * w;
        const float y = h - ((val - lo) / span) * h;
        return Vector2(Clamp(x, 0.0f, w), Clamp(y, 0.0f, h));
    };

    // Build the path: raw points (STRAIGHT) or Catmull-Rom tessellation (CATMULL_ROM).
    Vector<Vector2> pts;
    if (mode_ == STRAIGHT)
    {
        for (unsigned i = 0; i < n; ++i)
            pts.Push(toPoint((float)i, values_[i]));
    }
    else
    {
        for (unsigned i = 0; i + 1 < n; ++i)
        {
            const float v0 = values_[i == 0 ? 0 : i - 1];
            const float v1 = values_[i];
            const float v2 = values_[i + 1];
            const float v3 = values_[(i + 2 < n) ? (i + 2) : (n - 1)];
            for (unsigned s = 0; s < subdiv_; ++s)
            {
                const float t = (float)s / (float)subdiv_;
                pts.Push(toPoint((float)i + t, CatmullRom(v0, v1, v2, v3, t)));
            }
        }
        pts.Push(toPoint((float)(n - 1), values_[n - 1]));   // close on the final point
    }

    // Each segment is a thin solid quad. Null texture + SetColor => solid colour.
    // Translucent backing first, behind the curve — the overlay's alpha fill.
    if (bgColor_.a_ > 0.0f)
    {
        UIBatch bg(this, BLEND_ALPHA, currentScissor, nullptr, &vertexData);
        bg.SetColor(bgColor_);
        bg.AddQuad(0, 0, (int)w, (int)h, 0, 0);
        UIBatch::AddOrMerge(bg, batches);
    }

    // NOTE: corners are element-local; if position/winding reads wrong, that's the
    // only thing to tweak here (reversible, visible) — not a structural fault.
    UIBatch batch(this, BLEND_ALPHA, currentScissor, nullptr, &vertexData);
    batch.SetColor(lineColor_);
    // The freeform AddQuad applies only this transform — unlike the (x,y) form it does
    // NOT add the element's screen position. So feed it a translation = our screen pos,
    // turning the element-local corners below into screen-space (else it paints at the
    // window origin and spills across the whole window).
    const IntVector2 sp = GetScreenPosition();
    const Matrix3x4 toScreen(Vector3((float)sp.x_, (float)sp.y_, 0.0f), Quaternion::IDENTITY, 1.0f);
    const float half = thickness_ * 0.5f;
    for (unsigned i = 0; i + 1 < pts.Size(); ++i)
    {
        const Vector2 a = pts[i];
        const Vector2 b = pts[i + 1];
        const Vector2 dir = b - a;
        const float len = dir.Length();
        if (len < M_EPSILON)
            continue;
        const Vector2 perp(-dir.y_ / len * half, dir.x_ / len * half);
        const IntVector2 c0((int)(a.x_ + perp.x_), (int)(a.y_ + perp.y_));
        const IntVector2 c1((int)(b.x_ + perp.x_), (int)(b.y_ + perp.y_));
        const IntVector2 c2((int)(b.x_ - perp.x_), (int)(b.y_ - perp.y_));
        const IntVector2 c3((int)(a.x_ - perp.x_), (int)(a.y_ - perp.y_));
        batch.AddQuad(toScreen, c0, c1, c2, c3,
            IntVector2::ZERO, IntVector2::ZERO, IntVector2::ZERO, IntVector2::ZERO);
    }
    UIBatch::AddOrMerge(batch, batches);
}
