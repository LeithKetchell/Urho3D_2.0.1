// CaveMan — proto-human male NPC.
// Animation getters and CaveAnims namespace live in HumanNPC.h (shared with CaveWoman).

#pragma once

#include "HumanNPC.h"

class CaveMan : public HumanNPC
{
    URHO3D_OBJECT(CaveMan, HumanNPC);

public:
    explicit CaveMan(Context* context) : HumanNPC(context) {}

    static void RegisterObject(Context* context) { context->RegisterFactory<CaveMan>(); }

protected:
    String GetModelPath() const override { return "Models/Characters/CavemanMan.mdl"; }

    bool HasVitals() const override { return true; }
    int GetCreatureId() const override { return 20; }
    float GetDesiredSize() const override { return 1.8f; }
    float GetWanderRadius() const override { return 20.0f; }
    float GetWanderSpeed() const override { return 1.8f; }
    float GetFleeSpeed() const override { return 6.0f; }
    float GetFleeDistance() const override { return 25.0f; }
    float GetMinIdleDuration() const override { return 5.0f; }
    float GetMaxIdleDuration() const override { return 12.0f; }
};
