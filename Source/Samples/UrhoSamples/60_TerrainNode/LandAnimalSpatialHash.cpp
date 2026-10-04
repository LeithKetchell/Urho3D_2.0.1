// Spatial hash for O(1) land-animal neighbor queries.

#include "LandAnimalSpatialHash.h"
#include "LandAnimal.h"

#include <Urho3D/Scene/Node.h>

LandAnimalSpatialHash::LandAnimalSpatialHash(float cellSize)
    : cellSize_(cellSize), invCellSize_(1.0f / cellSize)
{
}

void LandAnimalSpatialHash::Clear()
{
    for (auto it = cells_.Begin(); it != cells_.End(); ++it)
        it->second_.Clear();
}

long long LandAnimalSpatialHash::HashKey(int cx, int cy, int cz) const
{
    return ((long long)cx * 73856093LL) ^
           ((long long)cy * 19349663LL) ^
           ((long long)cz * 83492791LL);
}

void LandAnimalSpatialHash::Insert(LandAnimal* animal, const Vector3& pos)
{
    int cx = (int)Floor(pos.x_ * invCellSize_);
    int cy = (int)Floor(pos.y_ * invCellSize_);
    int cz = (int)Floor(pos.z_ * invCellSize_);
    cells_[HashKey(cx, cy, cz)].Push({animal, pos});
}

void LandAnimalSpatialHash::Query(const Vector3& pos, float radius,
                                   Vector<LandAnimal*>& results) const
{
    float r2 = radius * radius;
    int minCX = (int)Floor((pos.x_ - radius) * invCellSize_);
    int maxCX = (int)Floor((pos.x_ + radius) * invCellSize_);
    int minCY = (int)Floor((pos.y_ - radius) * invCellSize_);
    int maxCY = (int)Floor((pos.y_ + radius) * invCellSize_);
    int minCZ = (int)Floor((pos.z_ - radius) * invCellSize_);
    int maxCZ = (int)Floor((pos.z_ + radius) * invCellSize_);

    for (int cx = minCX; cx <= maxCX; ++cx)
        for (int cy = minCY; cy <= maxCY; ++cy)
            for (int cz = minCZ; cz <= maxCZ; ++cz)
            {
                auto it = cells_.Find(HashKey(cx, cy, cz));
                if (it == cells_.End()) continue;
                for (unsigned i = 0; i < it->second_.Size(); ++i)
                {
                    const Entry& e = it->second_[i];
                    if ((e.pos - pos).LengthSquared() <= r2)
                        results.Push(e.animal);
                }
            }
}
