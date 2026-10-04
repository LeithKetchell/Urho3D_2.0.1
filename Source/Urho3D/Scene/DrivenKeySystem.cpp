// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "DrivenKeySystem.h"
#include "DrivenKeyEvents.h"
#include "../IO/Log.h"
#include "../Math/MathDefs.h"
#include "../Resource/JSONFile.h"
#include "../Resource/ResourceCache.h"

#include "../DebugNew.h"

namespace Urho3D
{

DrivenKeySystem::DrivenKeySystem(Context* context) :
    Object(context)
{
}

void DrivenKeySystem::AddSet(const DrivenKeySet& set)
{
    StringHash nameHash(set.name);
    sets_[nameHash] = set;
    URHO3D_LOGINFOF("DrivenKeySystem: added set '%s' (%u keys)", set.name.CString(), set.keys.Size());
}

void DrivenKeySystem::RemoveSet(StringHash name)
{
    sets_.Erase(name);
}

bool DrivenKeySystem::LoadSet(const String& resourcePath)
{
    auto* cache = GetSubsystem<ResourceCache>();
    if (!cache)
    {
        URHO3D_LOGERROR("DrivenKeySystem: no ResourceCache");
        return false;
    }

    auto* jsonFile = cache->GetResource<JSONFile>(resourcePath);
    if (!jsonFile)
    {
        URHO3D_LOGERRORF("DrivenKeySystem: could not load '%s'", resourcePath.CString());
        return false;
    }

    DrivenKeySet set;
    if (!set.LoadJSON(jsonFile->GetRoot()))
    {
        URHO3D_LOGERRORF("DrivenKeySystem: failed to parse '%s'", resourcePath.CString());
        return false;
    }

    AddSet(set);
    return true;
}

void DrivenKeySystem::SetDriver(StringHash param, float value)
{
    drivers_[param] = value;
}

float DrivenKeySystem::GetDriver(StringHash param) const
{
    auto it = drivers_.Find(param);
    return (it != drivers_.End()) ? it->second_ : 0.0f;
}

void DrivenKeySystem::Update()
{
    for (auto& setPair : sets_)
    {
        DrivenKeySet& set = setPair.second_;
        for (DrivenKey& key : set.keys)
        {
            if (!key.enabled)
                continue;

            auto it = drivers_.Find(key.driverParam);
            if (it == drivers_.End())
                continue;

            float driverVal = it->second_;
            float newVal = key.Evaluate(driverVal);

            if (Abs(newVal - key.lastOutput) > key.deadBand)
            {
                key.lastOutput = newVal;

                using namespace DrivenKeyOutput;
                VariantMap& eventData = GetEventDataMap();
                eventData[P_PARAM]       = key.drivenParam;
                eventData[P_VALUE]       = newVal;
                eventData[P_DRIVER]      = key.driverParam;
                eventData[P_DRIVERVALUE] = driverVal;
                SendEvent(E_DRIVENKEY_OUTPUT, eventData);
            }
        }
    }
}

} // namespace Urho3D
