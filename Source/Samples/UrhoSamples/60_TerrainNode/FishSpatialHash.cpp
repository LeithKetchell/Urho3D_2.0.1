// Spatial hash for O(1) fish neighbor queries.

#include "FishSpatialHash.h"
#include "Fish.h"
#include "SchoolFish.h"

#include <Urho3D/Scene/Node.h>

FishSpatialHash::FishSpatialHash(float cellSize)
    : cellSize_(cellSize), invCellSize_(1.0f / cellSize)
{
}

void FishSpatialHash::Clear()
{
    // Don't deallocate — just clear each bucket. Reuse memory.
    for (auto it = cells_.Begin(); it != cells_.End(); ++it)
        it->second_.Clear();
}

long long FishSpatialHash::HashKey(int cx, int cy, int cz) const
{
    // Pack 3 ints into one hash. Works for worlds < 2^20 units.
    return ((long long)cx * 73856093LL) ^
           ((long long)cy * 19349663LL) ^
           ((long long)cz * 83492791LL);
}

void FishSpatialHash::Insert(Fish* fish, const Vector3& pos)
{
    int cx = (int)Floor(pos.x_ * invCellSize_);
    int cy = (int)Floor(pos.y_ * invCellSize_);
    int cz = (int)Floor(pos.z_ * invCellSize_);
    cells_[HashKey(cx, cy, cz)].Push({fish, pos});
}

void FishSpatialHash::Query(const Vector3& pos, float radius,
                             Vector<Fish*>& results) const
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
                        results.Push(e.fish);
                }
            }
}

void FishSpatialHash::QuerySchool(const Vector3& pos, float radius, unsigned schoolID,
                                   Vector<SchoolFish*>& results) const
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
                    if ((e.pos - pos).LengthSquared() > r2)
                        continue;
                    // Virtual type check — avoids RTTI overhead of dynamic_cast
                    if (!e.fish->IsSchoolFish())
                        continue;
                    SchoolFish* sf = static_cast<SchoolFish*>(e.fish);
                    if (sf->GetSchoolID() == schoolID)
                        results.Push(sf);
                }
            }
}
