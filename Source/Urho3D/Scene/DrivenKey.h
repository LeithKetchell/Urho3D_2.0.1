// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../Math/StringHash.h"
#include "../Math/MathDefs.h"

namespace Urho3D
{

class JSONValue;

// ---------------------------------------------------------------------------
// Control point on a response curve
// ---------------------------------------------------------------------------

struct CurvePoint
{
    float in{0.0f};          // Driver value at this point
    float out{0.0f};         // Driven value at this point
    float tangentIn{0.0f};   // Incoming tangent (Hermite — 0 = auto)
    float tangentOut{0.0f};  // Outgoing tangent (Hermite — 0 = auto)
};

// ---------------------------------------------------------------------------
// Interpolation mode between curve points
// ---------------------------------------------------------------------------

enum InterpolationMode
{
    INTERP_LINEAR = 0,   // Straight lines between points
    INTERP_SMOOTH,       // Hermite spline (uses stored tangents when nonzero)
    INTERP_CATMULLROM,   // Catmull-Rom spline — always passes through all control points
    INTERP_STEP,         // Hold previous value until next point
    INTERP_CONSTANT      // Always returns first point's output
};

// ---------------------------------------------------------------------------
// Behaviour when driver is outside the curve's defined range
// ---------------------------------------------------------------------------

enum InfinityMode
{
    INFINITY_CLAMP = 0,   // Hold first/last point value
    INFINITY_EXTRAPOLATE  // Continue the slope beyond the endpoint
};

// ---------------------------------------------------------------------------
// A single driver → curve → driven relationship
// ---------------------------------------------------------------------------

struct URHO3D_API DrivenKey
{
    String      name;
    StringHash  driverParam;
    StringHash  drivenParam;
    InterpolationMode mode{INTERP_LINEAR};
    InfinityMode preInfinity{INFINITY_CLAMP};
    InfinityMode postInfinity{INFINITY_CLAMP};
    Vector<CurvePoint> points;    // Must be sorted ascending by CurvePoint::in
    float deadBand{0.001f};       // Event only fires when |newVal - lastVal| > deadBand
    bool  enabled{true};

    /// Evaluate the curve at the given driver value.
    float Evaluate(float driverValue) const;

    /// Load this key from a JSON object (one element from the "keys" array).
    bool LoadJSON(const JSONValue& obj);
    /// Save this key to a JSON object.
    JSONValue SaveJSON() const;

    // --- Runtime state (not serialised) ---
    float lastOutput{0.0f};
};

// ---------------------------------------------------------------------------
// A named group of driven keys — one JSON file = one set
// ---------------------------------------------------------------------------

struct URHO3D_API DrivenKeySet
{
    String          name;
    Vector<DrivenKey> keys;

    /// Load from the root JSON object of a set file.
    bool LoadJSON(const JSONValue& root);
    /// Save to a JSON root object.
    JSONValue SaveJSON() const;
};

} // namespace Urho3D
