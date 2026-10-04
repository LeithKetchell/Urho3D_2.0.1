// Donkey — sturdy pack animal. Follows its owner NPC, carries resources between buildings.

#pragma once

#include "LandAnimal.h"

#include <Urho3D/Scene/Node.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Model.h>

/// A single cargo slot on a pack donkey.
struct CargoSlot
{
    int resourceType{0};   ///< ResourceType enum value (0 = empty)
    int quantity{0};        ///< Stack count
};

class Donkey : public LandAnimal
{
    URHO3D_OBJECT(Donkey, LandAnimal);

public:
    explicit Donkey(Context* context) : LandAnimal(context)
    {
        vocalizationSoundPath_ = "Sounds/Animals/DonkeyVocalization.ogg";
        for (int i = 0; i < NUM_CARGO_SLOTS; ++i)
            cargo_[i] = CargoSlot{};
    }

    static void RegisterObject(Context* context) { context->RegisterFactory<Donkey>(); }

    void Start() override;
    void FixedUpdate(float timeStep) override;

    // --- Ownership ---
    void SetOwner(Node* ownerNode) { ownerNode_ = ownerNode; }
    Node* GetOwner() const { return ownerNode_.Get(); }

    // --- Cargo ---
    static constexpr int NUM_CARGO_SLOTS = 6;

    /// Add resources to cargo. Returns the quantity actually stored (may be less if full).
    int AddCargo(int resourceType, int quantity);
    /// Remove resources from cargo. Returns the quantity actually removed.
    int RemoveCargo(int resourceType, int quantity);
    /// Total quantity of a given resource type across all slots.
    int GetCargoCount(int resourceType) const;
    /// True if all cargo slots are empty.
    bool IsCargoEmpty() const;
    /// True if all cargo slots are occupied (non-zero resourceType).
    bool IsCargoFull() const;
    /// Direct slot access for UI.
    const CargoSlot& GetCargoSlot(int index) const { return cargo_[Clamp(index, 0, NUM_CARGO_SLOTS - 1)]; }

protected:
    String GetModelPath() const override { return "Models/Animals/Donkey.mdl"; }
    String GetAnimPrefix() const override { return "Donkey_AnimalArmature"; }

    int GetCreatureId() const override { return 9; }
    float GetFoodShrubWeight() const override { return 0.2f; }  // grazer, some browse
    float GetDesiredSize() const override { return 1.3f; }
    float GetWanderRadius() const override { return 20.0f; }
    float GetWanderSpeed() const override { return 1.5f; }
    float GetMinIdleDuration() const override { return 5.0f; }
    float GetMaxIdleDuration() const override { return 12.0f; }

private:
    /// Follow distance — how far behind the owner the donkey trails.
    static constexpr float FOLLOW_DISTANCE = 3.0f;
    /// If owner is farther than this, start following.
    static constexpr float FOLLOW_START_DISTANCE = 4.5f;
    /// If owner is closer than this, stop and idle.
    static constexpr float FOLLOW_STOP_DISTANCE = 2.5f;
    /// Follow walk speed — slightly faster than NPC walk to catch up.
    static constexpr float FOLLOW_SPEED = 2.2f;
    /// Max stack per cargo slot.
    static constexpr int MAX_STACK = 64;

    /// Owner NPC node. WeakPtr auto-nulls if removed.
    WeakPtr<Node> ownerNode_;

    /// Cargo inventory.
    CargoSlot cargo_[NUM_CARGO_SLOTS];

    /// Backpack visual node (child of donkey's spine bone).
    WeakPtr<Node> backpackNode_;

    /// Attach or detach the backpack visual based on cargo state.
    void UpdateBackpackVisual();
};
