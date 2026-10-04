// Bull — large, aggressive bovine. Territorial, charges when threatened.

#pragma once

#include "LandAnimal.h"

class Bull : public LandAnimal
{
    URHO3D_OBJECT(Bull, LandAnimal);

public:
    explicit Bull(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<Bull>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/Bull.mdl"; }
    String GetAnimPrefix() const override { return "Bull_AnimalArmature"; }

    int GetCreatureId() const override { return 6; }
    float GetFoodShrubWeight() const override { return 0.2f; }  // heavy grazer
    float GetDesiredSize() const override { return 1.8f; }
    float GetWanderRadius() const override { return 20.0f; }
    float GetWanderSpeed() const override { return 1.5f; }
    float GetMinIdleDuration() const override { return 5.0f; }
    float GetMaxIdleDuration() const override { return 15.0f; }
};
