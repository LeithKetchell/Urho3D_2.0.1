// ShibaInu — small domestic dog. Quick, alert.

#pragma once

#include "LandAnimal.h"

class ShibaInu : public LandAnimal
{
    URHO3D_OBJECT(ShibaInu, LandAnimal);

public:
    explicit ShibaInu(Context* context) : LandAnimal(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<ShibaInu>(); }

protected:
    String GetModelPath() const override { return "Models/Animals/ShibaInu.mdl"; }
    String GetAnimPrefix() const override { return "ShibaInu_AnimalArmature"; }
    String GetIdleAnim() const override
    {
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani";
        if (Random(1.0f) < 0.3f)
            return GetModelDir() + GetAnimPrefix() + "Idle_2.ani";
        return GetModelDir() + GetAnimPrefix() + "Idle.ani";
    }
    String GetLookAnim() const override { return GetModelDir() + GetAnimPrefix() + "Idle_2_HeadLow.ani"; }

    int GetCreatureId() const override { return 13; }
    float GetDesiredSize() const override { return 0.48f; }
    float GetWanderRadius() const override { return 15.0f; }
    float GetWanderSpeed() const override { return 2.5f; }
    float GetMinIdleDuration() const override { return 2.0f; }
    float GetMaxIdleDuration() const override { return 6.0f; }
};
