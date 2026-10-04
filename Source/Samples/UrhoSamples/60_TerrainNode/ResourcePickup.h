// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Scene/LogicComponent.h>

using namespace Urho3D;

/// A gatherable resource node on the terrain. Click to collect.
/// The server reads ItemID and ItemQty node vars on MSG_PICKUP.
class ResourcePickup : public LogicComponent
{
    URHO3D_OBJECT(ResourcePickup, LogicComponent);

public:
    explicit ResourcePickup(Context* context);

    static void RegisterObject(Context* context);

    /// Resource display name (e.g. "Loose Stone").
    String sourceName_;
    /// Item ID matching game_rules.db items table.
    int itemId_{1};
    /// Quantity given on pickup.
    int quantity_{1};
    /// Maximum interaction distance from player (metres).
    float pickupRange_{2.5f};

    /// Returns true if playerNode is within pickup range.
    bool IsInRange(Node* playerNode) const;
};
