// Spatial hash for O(1) land-animal neighbor queries.
// Mirrors FishSpatialHash design — HashMap grid, configurable cell size.
// Land animals are sparser than fish, so default cell size is larger (10m).

#pragma once

#include <Urho3D/Container/Vector.h>
#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Math/Vector3.h>
#include <Urho3D/Math/MathDefs.h>

using namespace Urho3D;

class LandAnimal;

class LandAnimalSpatialHash
{
public:
    explicit LandAnimalSpatialHash(float cellSize = 10.0f);

    /// Clear all cells. Call once at start of frame before re-inserting.
    void Clear();

    /// Insert a land animal into the grid.
    void Insert(LandAnimal* animal, const Vector3& pos);

    /// Query all land animals within radius of position. Results appended to output.
    void Query(const Vector3& pos, float radius, Vector<LandAnimal*>& results) const;

private:
    float cellSize_;
    float invCellSize_;

    long long HashKey(int cx, int cy, int cz) const;

    struct Entry
    {
        LandAnimal* animal;
        Vector3 pos;
    };

    HashMap<long long, Vector<Entry>> cells_;
};
