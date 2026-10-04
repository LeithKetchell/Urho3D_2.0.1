// CaveWoman — proto-human female NPC.
// Animation getters and CaveAnims namespace live in HumanNPC.h (shared with CaveMan).

#pragma once

#include "HumanNPC.h"

class CaveWoman : public HumanNPC
{
    URHO3D_OBJECT(CaveWoman, HumanNPC);

public:
    explicit CaveWoman(Context* context) : HumanNPC(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<CaveWoman>(); }

protected:
    String GetModelPath() const override { return "Models/Characters/CavemanWoman.mdl"; }

    bool HasVitals() const override { return true; }
    int GetCreatureId() const override { return 21; }
    float GetDesiredSize() const override { return 1.65f; }
    float GetWanderRadius() const override { return 20.0f; }
    float GetWanderSpeed() const override { return 1.8f; }
    float GetFleeSpeed() const override { return 5.5f; }
    float GetFleeDistance() const override { return 25.0f; }
    float GetMinIdleDuration() const override { return 5.0f; }
    float GetMaxIdleDuration() const override { return 12.0f; }
};
