// Wolf — pack predator. Aggressive, fast, dangerous.

#pragma once

#include "LandAnimal.h"

class Wolf : public LandAnimal
{
    URHO3D_OBJECT(Wolf, LandAnimal);

public:
    explicit Wolf(Context* context) : LandAnimal(context)
    {
        vocalizationSoundPath_ = "Sounds/Animals/WolfVocalization.ogg";
    }

    static void RegisterObject(Context* context) { context->RegisterFactory<Wolf>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Wolf.mdl"; }
    String GetAnimPrefix() const override { return "Wolf_AnimalArmature"; }
    String GetIdleAnim() const override
    {
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani";
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2.ani";
        return GetModelDir() + GetAnimPrefix() + "Idle.ani";
    }
    String GetLookAnim() const override { return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani"; }

    int GetCreatureId() const override { return 5; }
    float GetDesiredSize() const override { return 0.9f; }
    float GetWanderRadius() const override { return 35.0f; }
    float GetWanderSpeed() const override { return 3.5f; }
    float GetMinIdleDuration() const override { return 2.0f; }
    float GetMaxIdleDuration() const override { return 5.0f; }
    float GetFoodShrubWeight() const override { return 0.0f; }  // predator — eats prey, not plants
};
