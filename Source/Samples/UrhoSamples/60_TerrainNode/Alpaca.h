// Alpaca — small, woolly herd animal. Gentle, stays close to group.

#pragma once

#include "LandAnimal.h"

class Alpaca : public LandAnimal
{
    URHO3D_OBJECT(Alpaca, LandAnimal);

public:
    explicit Alpaca(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Alpaca>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Alpaca.mdl"; }
    String GetAnimPrefix() const override { return "Alpaca_AnimalArmature"; }

    int GetCreatureId() const override { return 11; }
    float GetFoodShrubWeight() const override { return 0.2f; }  // grazer
    float GetDesiredSize() const override { return 1.5f; }
    float GetWanderRadius() const override { return 15.0f; }
    float GetWanderSpeed() const override { return 1.5f; }
    float GetMinIdleDuration() const override { return 4.0f; }
    float GetMaxIdleDuration() const override { return 10.0f; }
};
