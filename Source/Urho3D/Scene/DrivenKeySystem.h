// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"
#include "../Container/HashMap.h"
#include "DrivenKey.h"

namespace Urho3D
{

/// Universal parameter-to-parameter curve evaluation system.
/// Game code pushes driver values; the system evaluates curves and fires
/// E_DRIVENKEY_OUTPUT events for any changed driven parameters.
/// Register as a subsystem: context->RegisterSubsystem(new DrivenKeySystem(context));
class URHO3D_API DrivenKeySystem : public Object
{
    URHO3D_OBJECT(DrivenKeySystem, Object);

public:
    explicit DrivenKeySystem(Context* context);

    /// Add or replace a driven key set by name.
    void AddSet(const DrivenKeySet& set);
    /// Remove a set by name hash.
    void RemoveSet(StringHash name);
    /// Load a set from a JSON resource path (e.g. "DrivenKeys/weather_atmosphere.json").
    bool LoadSet(const String& resourcePath);

    /// Push a driver value. Call once per frame before Update() for each active driver.
    void SetDriver(StringHash param, float value);
    /// Read the current driver value (returns 0 if not set).
    float GetDriver(StringHash param) const;

    /// Evaluate all enabled curves and fire E_DRIVENKEY_OUTPUT for any changed outputs.
    /// Call once per frame after all drivers have been pushed.
    void Update();

    /// Return the number of loaded sets.
    unsigned GetNumSets() const { return sets_.Size(); }

private:
    HashMap<StringHash, DrivenKeySet> sets_;
    HashMap<StringHash, float>        drivers_;
};

} // namespace Urho3D
