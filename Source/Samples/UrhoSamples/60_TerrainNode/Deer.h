// Deer — medium herbivore. Grazes, wanders wide, flees at distance.

#pragma once

#include "LandAnimal.h"

class Deer : public LandAnimal
{
    URHO3D_OBJECT(Deer, LandAnimal);

public:
    explicit Deer(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Deer>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Deer.mdl"; }
    String GetAnimPrefix() const override { return "Deer_AnimalArmature"; }

    int GetCreatureId() const override { return 2; }
    float GetDesiredSize() const override { return 1.15f; }
    float GetWanderRadius() const override { return 30.0f; }
    float GetWanderSpeed() const override { return 2.5f; }
    float GetMinIdleDuration() const override { return 4.0f; }
    float GetMaxIdleDuration() const override { return 10.0f; }
    float GetFoodShrubWeight() const override { return 0.5f; }  // browsers, prefer shrubs
};
