// Donkey — pack animal implementation: follow owner, carry cargo.

#include "Donkey.h"

#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Skeleton.h>
#include <Urho3D/IO/Log.h>

using namespace Urho3D;

void Donkey::Start()
{
    LandAnimal::Start();

    // If we have an owner, start in follow mode
    if (!ownerNode_.Expired())
        SetState(CREATURE_FOLLOW);
}

void Donkey::FixedUpdate(float timeStep)
{
    // Dead donkeys don't follow
    if (state_ == CREATURE_DIE || state_ == CREATURE_CORPSE)
    {
        LandAnimal::FixedUpdate(timeStep);
        return;
    }

    // Server-driven donkeys defer to the server
    if (IsServerDriven())
    {
        LandAnimal::FixedUpdate(timeStep);
        return;
    }

    Node* owner = ownerNode_.Get();

    // No owner — behave like a normal grazing animal
    if (!owner)
    {
        if (state_ == CREATURE_FOLLOW)
            SetState(CREATURE_IDLE);
        LandAnimal::FixedUpdate(timeStep);
        return;
    }

    Vector3 myPos = node_->GetWorldPosition();
    Vector3 ownerPos = owner->GetWorldPosition();
    Vector3 toOwner = ownerPos - myPos;
    toOwner.y_ = 0.0f;
    float dist = toOwner.Length();

    if (state_ == CREATURE_FOLLOW)
    {
        // Close enough — stop and idle
        if (dist < FOLLOW_STOP_DISTANCE)
        {
            SetState(CREATURE_IDLE);
        }
        else
        {
            // Walk toward a point FOLLOW_DISTANCE behind the owner
            Vector3 ownerFwd = owner->GetWorldDirection();
            ownerFwd.y_ = 0.0f;
            if (ownerFwd.LengthSquared() < 0.001f)
                ownerFwd = Vector3::FORWARD;
            ownerFwd.Normalize();

            Vector3 followTarget = ownerPos - ownerFwd * FOLLOW_DISTANCE;
            if (terrain_)
                followTarget.y_ = terrain_->GetHeight(followTarget);

            MoveToward(followTarget, FOLLOW_SPEED, timeStep);
        }

        PostMovementUpdate(timeStep);
        return;
    }

    // Not currently following — check if we should start
    if (state_ == CREATURE_IDLE || state_ == CREATURE_WANDER || state_ == CREATURE_EAT)
    {
        if (dist > FOLLOW_START_DISTANCE)
        {
            SetState(CREATURE_FOLLOW);
            PostMovementUpdate(timeStep);
            return;
        }
    }

    // Otherwise run normal LandAnimal AI (idle, wander, eat near owner)
    LandAnimal::FixedUpdate(timeStep);
}

// ============================================================================
// Cargo
// ============================================================================

int Donkey::AddCargo(int resourceType, int quantity)
{
    if (resourceType <= 0 || quantity <= 0)
        return 0;

    int stored = 0;

    // First pass: stack into existing slots of the same type
    for (int i = 0; i < NUM_CARGO_SLOTS && quantity > 0; ++i)
    {
        if (cargo_[i].resourceType == resourceType && cargo_[i].quantity < MAX_STACK)
        {
            int space = MAX_STACK - cargo_[i].quantity;
            int add = Min(space, quantity);
            cargo_[i].quantity += add;
            quantity -= add;
            stored += add;
        }
    }

    // Second pass: fill empty slots
    for (int i = 0; i < NUM_CARGO_SLOTS && quantity > 0; ++i)
    {
        if (cargo_[i].resourceType == 0)
        {
            int add = Min(MAX_STACK, quantity);
            cargo_[i].resourceType = resourceType;
            cargo_[i].quantity = add;
            quantity -= add;
            stored += add;
        }
    }

    if (stored > 0)
        UpdateBackpackVisual();

    return stored;
}

int Donkey::RemoveCargo(int resourceType, int quantity)
{
    if (resourceType <= 0 || quantity <= 0)
        return 0;

    int removed = 0;

    for (int i = 0; i < NUM_CARGO_SLOTS && quantity > 0; ++i)
    {
        if (cargo_[i].resourceType == resourceType)
        {
            int take = Min(cargo_[i].quantity, quantity);
            cargo_[i].quantity -= take;
            quantity -= take;
            removed += take;

            // Clear slot if emptied
            if (cargo_[i].quantity <= 0)
            {
                cargo_[i].resourceType = 0;
                cargo_[i].quantity = 0;
            }
        }
    }

    if (removed > 0)
        UpdateBackpackVisual();

    return removed;
}

int Donkey::GetCargoCount(int resourceType) const
{
    int total = 0;
    for (int i = 0; i < NUM_CARGO_SLOTS; ++i)
    {
        if (cargo_[i].resourceType == resourceType)
            total += cargo_[i].quantity;
    }
    return total;
}

bool Donkey::IsCargoEmpty() const
{
    for (int i = 0; i < NUM_CARGO_SLOTS; ++i)
    {
        if (cargo_[i].resourceType != 0 && cargo_[i].quantity > 0)
            return false;
    }
    return true;
}

bool Donkey::IsCargoFull() const
{
    for (int i = 0; i < NUM_CARGO_SLOTS; ++i)
    {
        if (cargo_[i].resourceType == 0)
            return false;
    }
    return true;
}

// ============================================================================
// Backpack visual
// ============================================================================

void Donkey::UpdateBackpackVisual()
{
    bool hasCargo = !IsCargoEmpty();

    if (hasCargo && backpackNode_.Expired())
    {
        // Find the spine bone on the model node to attach the backpack
        auto* mdl = node_->GetComponent<AnimatedModel>(true);
        if (!mdl)
            return;

        const Skeleton& skel = mdl->GetSkeleton();
        Node* modelNode = mdl->GetNode();
        if (!modelNode)
            return;

        // Try Spine, Spine1, or fall back to the model node itself
        Node* spineNode = modelNode->GetChild("Spine1", true);
        if (!spineNode)
            spineNode = modelNode->GetChild("Spine", true);
        if (!spineNode)
            spineNode = modelNode;

        auto* cache = GetSubsystem<ResourceCache>();
        auto* backpackModel = cache->GetResource<Model>("Models/CharacterItems/Backpack.mdl");
        if (!backpackModel)
        {
            URHO3D_LOGWARNING("[Donkey] Backpack model not found");
            return;
        }

        Node* bpNode = spineNode->CreateChild("PackCargo");
        auto* sm = bpNode->CreateComponent<StaticModel>();
        sm->SetModel(backpackModel);
        sm->SetCastShadows(false);

        // Position on the donkey's back — offset up and slightly back
        bpNode->SetPosition(Vector3(0.0f, 0.4f, 0.0f));
        bpNode->SetScale(1.2f);

        backpackNode_ = bpNode;

        // Apply materials from Backpack.txt
        auto* brownMat = cache->GetResource<Material>("Materials/Brown.xml");
        if (brownMat)
            sm->SetMaterial(brownMat);
    }
    else if (!hasCargo && !backpackNode_.Expired())
    {
        // Remove backpack visual when cargo is empty
        backpackNode_->Remove();
        backpackNode_.Reset();
    }
}
