// Fox — small predator. Hunts rabbits, flees from players.

#pragma once

#include "LandAnimal.h"

class Fox : public LandAnimal
{
    URHO3D_OBJECT(Fox, LandAnimal);

public:
    explicit Fox(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Fox>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Fox.mdl"; }
    String GetAnimPrefix() const override { return "Fox_AnimalArmature"; }
    String GetIdleAnim() const override
    {
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani";
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2.ani";
        return GetModelDir() + GetAnimPrefix() + "Idle.ani";
    }
    String GetLookAnim() const override { return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani"; }

    int GetCreatureId() const override { return 3; }
    float GetDesiredSize() const override { return 0.55f; }
    float GetWanderRadius() const override { return 25.0f; }
    float GetWanderSpeed() const override { return 3.0f; }
    float GetMinIdleDuration() const override { return 2.0f; }
    float GetMaxIdleDuration() const override { return 6.0f; }
    float GetFoodShrubWeight() const override { return 0.0f; }
};
