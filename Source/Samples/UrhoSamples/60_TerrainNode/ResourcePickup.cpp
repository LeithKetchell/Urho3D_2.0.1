// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "ResourcePickup.h"
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Core/Context.h>

ResourcePickup::ResourcePickup(Context* context)
    : LogicComponent(context)
{
}

void ResourcePickup::RegisterObject(Context* context)
{
    context->RegisterFactory<ResourcePickup>();
}

bool ResourcePickup::IsInRange(Node* playerNode) const
{
    if (!playerNode || !GetNode())
        return false;
    float dist = (GetNode()->GetWorldPosition() - playerNode->GetWorldPosition()).Length();
    return dist <= pickupRange_;
}
