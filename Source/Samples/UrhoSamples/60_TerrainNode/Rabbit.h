// Rabbit — small herbivore. Sits, hops, flees when startled.

#pragma once

#include "LandAnimal.h"

class Rabbit : public LandAnimal
{
    URHO3D_OBJECT(Rabbit, LandAnimal);

public:
    explicit Rabbit(Context* context) : LandAnimal(context)
    {
        vocalizationSoundPath_ = "Sounds/Animals/RabbitVocalization.ogg";
    }

    static void RegisterObject(Context* context) { context->RegisterFactory<Rabbit>(); }

protected:
    // Rabbit keeps old model — no Ult_ version exists
    String GetModelPath() const override { return "Models/Animals/Rabbit.mdl"; }

    // Legacy per-species paths (no prefix)
    String GetIdleAnim() const override
    {
        return (Random(1.0f) < 0.6f)
            ? "Models/Animals/Rabbit_ArmatureSitting.000.ani"
            : "Models/Animals/Rabbit_ArmatureBasic.ani";
    }
    String GetRunAnim() const override { return "Models/Animals/Rabbit_ArmatureRunning.ani"; }
    String GetDieAnim() const override { return "Models/Animals/Rabbit_ArmatureDying.000.ani"; }

    int GetCreatureId() const override { return 1; }
    float GetDesiredSize() const override { return 0.35f; }
    float GetWanderRadius() const override { return 12.0f; }
    float GetWanderSpeed() const override { return 1.5f; }
    float GetMinIdleDuration() const override { return 2.0f; }
    float GetMaxIdleDuration() const override { return 6.0f; }
    float GetFoodShrubWeight() const override { return 0.2f; }
};
