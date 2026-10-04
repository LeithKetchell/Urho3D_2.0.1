// Spatial hash for O(1) fish neighbor queries.
// Replaces O(N*SceneSize) GetScene()->GetChildren() iteration.

#pragma once

#include <Urho3D/Container/Vector.h>
#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Math/MathDefs.h>

using namespace Urho3D;

class Fish;
class SchoolFish;

class FishSpatialHash
{
public:
    explicit FishSpatialHash(float cellSize = 5.0f);

    /// Clear all cells. Call once at start of frame before re-inserting.
    void Clear();

    /// Insert a fish into the grid.
    void Insert(Fish* fish, const Vector3& pos);

    /// Query all fish within radius of position. Results appended to output.
    void Query(const Vector3& pos, float radius, Vector<Fish*>& results) const;

    /// Query only SchoolFish with matching schoolID within radius.
    void QuerySchool(const Vector3& pos, float radius, unsigned schoolID,
                     Vector<SchoolFish*>& results) const;

private:
    float cellSize_;
    float invCellSize_;

    long long HashKey(int cx, int cy, int cz) const;

    struct Entry
    {
        Fish* fish;
        Vector3 pos;
    };

    HashMap<long long, Vector<Entry>> cells_;
};
