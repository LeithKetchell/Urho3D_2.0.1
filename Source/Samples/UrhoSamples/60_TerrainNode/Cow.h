// Cow — docile bovine. Slow, calm, grazes a lot.

#pragma once

#include "LandAnimal.h"

class Cow : public LandAnimal
{
    URHO3D_OBJECT(Cow, LandAnimal);

public:
    explicit Cow(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Cow>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Cow.mdl"; }
    String GetAnimPrefix() const override { return "Cow_AnimalArmature"; }

    int GetCreatureId() const override { return 7; }
    float GetFoodShrubWeight() const override { return 0.2f; }  // heavy grazer
    float GetDesiredSize() const override { return 1.6f; }
    float GetWanderRadius() const override { return 15.0f; }
    float GetWanderSpeed() const override { return 1.2f; }
    float GetMinIdleDuration() const override { return 6.0f; }
    float GetMaxIdleDuration() const override { return 15.0f; }
};
