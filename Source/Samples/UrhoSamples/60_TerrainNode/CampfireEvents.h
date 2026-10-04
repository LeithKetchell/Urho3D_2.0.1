#pragma once

#include <Urho3D/Core/Object.h>

// Broadcast when any campfire UI slider changes.
// All campfire instances should subscribe and apply.
URHO3D_EVENT(E_CAMPFIRE_SETTINGS_CHANGED, CampfireSettingsChanged)
{
    URHO3D_PARAM(P_SLIDERID, SliderID);  // int — which setting changed
    URHO3D_PARAM(P_VALUE, Value);        // float — new value
}
