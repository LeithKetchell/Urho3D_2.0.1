// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "DrivenKey.h"
#include "../Resource/JSONValue.h"
#include "../IO/Log.h"

#include "../DebugNew.h"

namespace Urho3D
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static InterpolationMode ParseInterp(const String& s)
{
    if (s == "smooth")     return INTERP_SMOOTH;
    if (s == "catmullrom") return INTERP_CATMULLROM;
    if (s == "step")       return INTERP_STEP;
    if (s == "constant")   return INTERP_CONSTANT;
    return INTERP_LINEAR; // default
}

static InfinityMode ParseInfinity(const String& s)
{
    if (s == "extrapolate") return INFINITY_EXTRAPOLATE;
    return INFINITY_CLAMP;
}

static const char* InterpToString(InterpolationMode m)
{
    switch (m)
    {
    case INTERP_SMOOTH:     return "smooth";
    case INTERP_CATMULLROM: return "catmullrom";
    case INTERP_STEP:       return "step";
    case INTERP_CONSTANT:   return "constant";
    default:                return "linear";
    }
}

static const char* InfinityToString(InfinityMode m)
{
    return (m == INFINITY_EXTRAPOLATE) ? "extrapolate" : "clamp";
}

// ---------------------------------------------------------------------------
// DrivenKey::Evaluate
// ---------------------------------------------------------------------------

float DrivenKey::Evaluate(float driverValue) const
{
    if (points.Empty())
        return 0.0f;
    if (points.Size() == 1)
        return points[0].out;

    // CONSTANT: ignore driver entirely
    if (mode == INTERP_CONSTANT)
        return points[0].out;

    // Pre-infinity (driver below first point)
    if (driverValue <= points[0].in)
    {
        if (preInfinity == INFINITY_EXTRAPOLATE && points.Size() >= 2)
        {
            float dx = points[1].in - points[0].in;
            if (dx > M_EPSILON)
            {
                float slope = (points[1].out - points[0].out) / dx;
                return points[0].out + slope * (driverValue - points[0].in);
            }
        }
        return points[0].out;
    }

    // Post-infinity (driver above last point)
    if (driverValue >= points.Back().in)
    {
        if (postInfinity == INFINITY_EXTRAPOLATE && points.Size() >= 2)
        {
            unsigned n = points.Size();
            float dx = points[n - 1].in - points[n - 2].in;
            if (dx > M_EPSILON)
            {
                float slope = (points[n - 1].out - points[n - 2].out) / dx;
                return points[n - 1].out + slope * (driverValue - points[n - 1].in);
            }
        }
        return points.Back().out;
    }

    // Binary search for the segment [lo, hi] containing driverValue
    unsigned lo = 0, hi = points.Size() - 1;
    while (hi - lo > 1)
    {
        unsigned mid = (lo + hi) / 2;
        if (points[mid].in <= driverValue)
            lo = mid;
        else
            hi = mid;
    }

    float dx = points[hi].in - points[lo].in;
    float t  = (dx > M_EPSILON) ? (driverValue - points[lo].in) / dx : 0.0f;

    switch (mode)
    {
    case INTERP_STEP:
        return points[lo].out;

    case INTERP_SMOOTH:
    {
        // Hermite cubic — uses stored tangents if nonzero, otherwise Catmull-Rom auto-tangents
        float p0 = points[lo].out;
        float p1 = points[hi].out;
        float m0 = points[lo].tangentOut;
        float m1 = points[hi].tangentIn;

        // Auto-tangent: finite-difference from neighbours when tangents are zero
        if (Abs(m0) < M_EPSILON && lo + 1 < points.Size())
        {
            if (lo > 0)
                m0 = (points[lo + 1].out - points[lo - 1].out) /
                     (points[lo + 1].in  - points[lo - 1].in);
            else
                m0 = (points[lo + 1].out - points[lo].out) /
                     (points[lo + 1].in  - points[lo].in);
        }
        if (Abs(m1) < M_EPSILON && hi < points.Size())
        {
            if (hi + 1 < points.Size())
                m1 = (points[hi + 1].out - points[hi - 1].out) /
                     (points[hi + 1].in  - points[hi - 1].in);
            else
                m1 = (points[hi].out - points[hi - 1].out) /
                     (points[hi].in  - points[hi - 1].in);
        }
        // Scale tangents from parameterized space to actual dx
        m0 *= dx;
        m1 *= dx;

        float t2 = t * t, t3 = t2 * t;
        return (2*t3 - 3*t2 + 1)*p0 + (t3 - 2*t2 + t)*m0
             + (-2*t3 + 3*t2)*p1 + (t3 - t2)*m1;
    }

    case INTERP_CATMULLROM:
    {
        // Catmull-Rom — tangents always derived from neighbors, guaranteed to pass through all points
        float p0 = points[lo].out;
        float p1 = points[hi].out;
        float m0, m1;

        // Tangent at lo: finite difference of surrounding points
        if (lo > 0)
            m0 = (points[lo + 1].out - points[lo - 1].out) /
                 (points[lo + 1].in  - points[lo - 1].in);
        else
            m0 = (p1 - p0) / dx;

        // Tangent at hi: finite difference of surrounding points
        if (hi + 1 < points.Size())
            m1 = (points[hi + 1].out - points[hi - 1].out) /
                 (points[hi + 1].in  - points[hi - 1].in);
        else
            m1 = (p1 - p0) / dx;

        m0 *= dx;
        m1 *= dx;

        float t2 = t * t, t3 = t2 * t;
        return (2*t3 - 3*t2 + 1)*p0 + (t3 - 2*t2 + t)*m0
             + (-2*t3 + 3*t2)*p1 + (t3 - t2)*m1;
    }

    case INTERP_LINEAR:
    default:
        return points[lo].out + t * (points[hi].out - points[lo].out);
    }
}

// ---------------------------------------------------------------------------
// DrivenKey JSON
// ---------------------------------------------------------------------------

bool DrivenKey::LoadJSON(const JSONValue& obj)
{
    name       = obj["name"].GetString();
    driverParam = StringHash(obj["driver"].GetString());
    drivenParam = StringHash(obj["driven"].GetString());
    mode       = ParseInterp(obj["mode"].GetString());
    preInfinity  = ParseInfinity(obj["preInfinity"].GetString());
    postInfinity = ParseInfinity(obj["postInfinity"].GetString());
    deadBand   = obj.Contains("deadBand") ? obj["deadBand"].GetFloat() : 0.001f;
    enabled    = obj.Contains("enabled") ? obj["enabled"].GetBool() : true;

    points.Clear();
    const JSONValue& pts = obj["points"];
    for (unsigned i = 0; i < pts.Size(); ++i)
    {
        const JSONValue& p = pts[i];
        CurvePoint cp;
        cp.in         = p["in"].GetFloat();
        cp.out        = p["out"].GetFloat();
        cp.tangentIn  = p.Contains("tangentIn")  ? p["tangentIn"].GetFloat()  : 0.0f;
        cp.tangentOut = p.Contains("tangentOut") ? p["tangentOut"].GetFloat() : 0.0f;
        points.Push(cp);
    }

    // Ensure points are sorted by in-value
    Sort(points.Begin(), points.End(),
         [](const CurvePoint& a, const CurvePoint& b) { return a.in < b.in; });

    lastOutput = 0.0f;
    return !points.Empty();
}

JSONValue DrivenKey::SaveJSON() const
{
    JSONValue obj;
    obj["name"]        = name;
    obj["driver"]      = driverParam.ToString();
    obj["driven"]      = drivenParam.ToString();
    obj["mode"]        = InterpToString(mode);
    obj["preInfinity"] = InfinityToString(preInfinity);
    obj["postInfinity"]= InfinityToString(postInfinity);
    obj["deadBand"]    = deadBand;
    obj["enabled"]     = enabled;

    JSONValue pts;
    pts.SetType(JSON_ARRAY);
    for (const CurvePoint& cp : points)
    {
        JSONValue p;
        p["in"]  = cp.in;
        p["out"] = cp.out;
        if (Abs(cp.tangentIn)  > M_EPSILON) p["tangentIn"]  = cp.tangentIn;
        if (Abs(cp.tangentOut) > M_EPSILON) p["tangentOut"] = cp.tangentOut;
        pts.Push(p);
    }
    obj["points"] = pts;
    return obj;
}

// ---------------------------------------------------------------------------
// DrivenKeySet JSON
// ---------------------------------------------------------------------------

bool DrivenKeySet::LoadJSON(const JSONValue& root)
{
    name = root["name"].GetString();
    keys.Clear();

    const JSONValue& arr = root["keys"];
    for (unsigned i = 0; i < arr.Size(); ++i)
    {
        DrivenKey key;
        if (key.LoadJSON(arr[i]))
            keys.Push(key);
        else
            URHO3D_LOGWARNINGF("DrivenKeySet '%s': key %u has no points — skipped", name.CString(), i);
    }
    return true;
}

JSONValue DrivenKeySet::SaveJSON() const
{
    JSONValue root;
    root["name"] = name;
    JSONValue arr;
    arr.SetType(JSON_ARRAY);
    for (const DrivenKey& k : keys)
        arr.Push(k.SaveJSON());
    root["keys"] = arr;
    return root;
}

} // namespace Urho3D
