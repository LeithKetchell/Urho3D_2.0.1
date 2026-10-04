// SchoolFish — tiny fish that school together around their center of mass.
// Uses the same UrhoFish model at reduced scale with flocking behavior:
// cohesion (steer toward centroid), alignment (match school heading),
// separation (avoid crowding neighbours).

#pragma once

#include "Fish.h"

/// Fired when a fish is born via breeding. TerrainNode subscribes to track the new node.
URHO3D_EVENT(E_FISHBORN, FishBorn)
{
    URHO3D_PARAM(P_NODE, Node);       // Node* — the newly spawned fish node
    URHO3D_PARAM(P_SCHOOLID, SchoolID); // unsigned — school membership
}

class SchoolStateCache;
class FishSpatialHash;
struct FishPopulationState;

/// Per-school population tracking (owned by TerrainNode, shared with all school members).
struct FishPopulationState
{
    unsigned schoolID{0};
    int currentCount{0};       ///< Live fish in this school right now
    int populationCap{15};     ///< Maximum fish this school can hold (habitat-driven)
    int totalCaught{0};        ///< Cumulative fish removed by fishing/traps

    /// Population ratio: currentCount / populationCap
    float GetRatio() const { return populationCap > 0 ? (float)currentCount / (float)populationCap : 0.0f; }
    /// Breeding rate multiplier based on fishing pressure.
    /// Fish never go extinct — low population breeds faster to recover.
    float GetBreedingMultiplier() const
    {
        float ratio = GetRatio();
        if (ratio <= 0.3f) return 2.0f;    // Stress response — double breeding rate
        return 1.0f;                        // Normal
    }
    /// Minimum fish that must remain — can't be caught below this floor.
    static constexpr int MIN_POPULATION = 2;
};

class SchoolFish : public Fish
{
    URHO3D_OBJECT(SchoolFish, Fish);

public:
    explicit SchoolFish(Context* context);

    static void RegisterObject(Context* context);

    void Start() override;
    void Stop() override;
    void Update(float timeStep) override;

    /// Assign this fish to a school. All fish sharing the same school ID will flock together.
    void SetSchoolID(unsigned id) { schoolID_ = id; }
    unsigned GetSchoolID() const { return schoolID_; }
    void SetSchoolCache(SchoolStateCache* cache) { schoolCache_ = cache; }

    /// Population state (owned by TerrainNode, shared across school members).
    void SetPopulationState(FishPopulationState* state) { popState_ = state; }
    FishPopulationState* GetPopulationState() const { return popState_; }

    /// Call when this fish is caught by a player action (fishing rod, trap, weir).
    /// Returns false if population is at the floor (can't catch the last few).
    bool MarkCaught()
    {
        if (!popState_) return false;
        if (popState_->currentCount <= FishPopulationState::MIN_POPULATION)
            return false;  // Can't overfish to extinction
        popState_->totalCaught++;
        return true;
    }

    /// True when the fish is large enough to be worth catching.
    bool IsCatchable() const { return mature_; }

    bool IsSchoolFish() const override { return true; }

    // --- Vision: school fish are prey (DB-overridable) ---
    bool IsPredator() const override { return false; }
    float GetVisionRange() const override { return dbVisionRange_; }
    float GetVisionCosAngle() const override { return dbVisionAngle_; }

protected:
    float GetWanderSpeed() const override { return dbSwimSpeed_; }
    float GetWanderRadius() const override { return dbWanderRadius_; }

private:
    /// Compute school centroid and average heading from scene siblings.
    void ComputeSchoolState(Vector3& centroid, Vector3& avgHeading, unsigned& count);

    /// Attempt breeding — spawn a juvenile if conditions met.
    void TryBreed(float timeStep);

    /// Graduate from school fish to solitary Fish — swap component, change behavior.
    void Graduate();

    unsigned schoolID_{0};

    // Flocking weights
    float cohesionWeight_{1.5f};     // pull toward centroid
    float alignmentWeight_{1.0f};    // match school heading
    float separationWeight_{2.0f};   // push away from close neighbours
    float separationDist_{0.8f};     // min distance before separation kicks in
    float schoolTurnSpeed_{3.0f};    // faster turning for tight schooling

    // School state cache (owned by TerrainNode, not by SchoolFish)
    SchoolStateCache* schoolCache_{nullptr};

    /// Population tracking (shared across school, owned by TerrainNode).
    FishPopulationState* popState_{nullptr};

    /// Breeding timer — counts down, spawns juvenile at zero.
    float breedTimer_{0.0f};
    static constexpr float BREED_INTERVAL_BASE = 30.0f; ///< Seconds between breed attempts (normal pressure)

    /// Age at which this fish leaves the school and becomes a solitary Fish.
    float graduationAge_{90.0f};

    /// Adult scale (captured at Start, used for growth interpolation).
    float adultScale_{0.3f};

    /// Reusable query buffers — avoid per-frame heap allocations.
    mutable Vector<SchoolFish*> schoolBuf_;
    mutable Vector<Fish*> separationBuf_;

    /// Scatter state — when a predator is sighted, break cohesion temporarily.
    bool scattering_{false};
    float scatterTimer_{0.0f};
    Vector3 scatterDir_;  ///< Individual flee direction (away from predator)
    static constexpr float SCATTER_DURATION = 4.0f;   ///< How long to flee before reforming
    static constexpr float SCATTER_SPEED    = 1.6f;    ///< Flee speed (2x normal wander)
};
