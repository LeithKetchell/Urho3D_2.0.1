// Husky — medium domestic dog. Loyal, energetic.

#pragma once

#include "LandAnimal.h"

class Husky : public LandAnimal
{
    URHO3D_OBJECT(Husky, LandAnimal);

public:
    explicit Husky(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Husky>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Husky.mdl"; }
    String GetAnimPrefix() const override { return "Husky_AnimalArmature"; }
    String GetIdleAnim() const override
    {
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani";
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2.ani";
        return GetModelDir() + GetAnimPrefix() + "Idle.ani";
    }
    String GetLookAnim() const override { return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani"; }

    int GetCreatureId() const override { return 12; }
    float GetDesiredSize() const override { return 0.65f; }
    float GetWanderRadius() const override { return 20.0f; }
    float GetWanderSpeed() const override { return 3.0f; }
    float GetMinIdleDuration() const override { return 2.0f; }
    float GetMaxIdleDuration() const override { return 5.0f; }
    float GetFoodShrubWeight() const override { return 0.0f; }  // predator — eats prey, not plants
};
