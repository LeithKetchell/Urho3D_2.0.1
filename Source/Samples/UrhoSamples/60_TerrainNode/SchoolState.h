// Per-school shared state, computed once per frame, read by all members.

#pragma once

#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Container/HashMap.h>

using namespace Urho3D;

struct SchoolState
{
    Vector3 centroid;
    Vector3 averageHeading;
    int memberCount{0};
    int frameComputed{-1};
};

/// Global school state cache. One entry per school ID.
class SchoolStateCache
{
public:
    SchoolState& GetState(unsigned schoolID) { return states_[schoolID]; }

    void InvalidateAll(int currentFrame) { currentFrame_ = currentFrame; }

    int GetCurrentFrame() const { return currentFrame_; }

private:
    HashMap<unsigned, SchoolState> states_;
    int currentFrame_{0};
};
