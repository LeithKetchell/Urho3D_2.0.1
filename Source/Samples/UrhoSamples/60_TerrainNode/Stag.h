// Stag — large male deer. Bigger, tougher, less skittish than Deer.

#pragma once

#include "LandAnimal.h"

class Stag : public LandAnimal
{
    URHO3D_OBJECT(Stag, LandAnimal);

public:
    explicit Stag(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Stag>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Stag.mdl"; }
    String GetAnimPrefix() const override { return "Stag_AnimalArmature"; }

    int GetCreatureId() const override { return 4; }
    float GetFoodShrubWeight() const override { return 0.5f; }  // browser like deer
    float GetDesiredSize() const override { return 1.7f; }
    float GetWanderRadius() const override { return 35.0f; }
    float GetWanderSpeed() const override { return 2.0f; }
    float GetMinIdleDuration() const override { return 5.0f; }
    float GetMaxIdleDuration() const override { return 12.0f; }
};
