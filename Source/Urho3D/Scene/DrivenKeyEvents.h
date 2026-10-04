// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include "../Core/Object.h"

namespace Urho3D
{

/// Fired once per changed driven parameter per frame by DrivenKeySystem::Update().
/// Not fired when the driven value is within dead-band of its previous value.
URHO3D_EVENT(E_DRIVENKEY_OUTPUT, DrivenKeyOutput)
{
    URHO3D_PARAM(P_PARAM,        Param);        // StringHash — which driven parameter changed
    URHO3D_PARAM(P_VALUE,        Value);        // float — new driven value
    URHO3D_PARAM(P_DRIVER,       Driver);       // StringHash — which driver caused this
    URHO3D_PARAM(P_DRIVERVALUE,  DriverValue);  // float — driver's current value
}

} // namespace Urho3D
