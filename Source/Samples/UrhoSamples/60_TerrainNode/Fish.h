// Fish — aquatic creature. Swims underwater, avoids shallows, reacts to camera.

#pragma once

#include "WaterAnimal.h"
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/StaticModel.h>

class FishSpatialHash;

class Fish : public WaterAnimal
{
    URHO3D_OBJECT(Fish, WaterAnimal);

public:
    explicit Fish(Context* context);

    static void RegisterObject(Context* context);

    void Start() override;
    void Update(float timeStep) override;

    void SetMaterials(Material* base, Material* orbit, Material* stare);
    void SetCameraNode(Node* camNode) { cameraNode_ = WeakPtr<Node>(camNode); }
    Node* GetCameraNode() const { return cameraNode_; }
    void SetSpatialHash(FishSpatialHash* hash) { spatialHash_ = hash; }

    /// Type flag for fast RTTI avoidance in spatial hash queries.
    virtual bool IsSchoolFish() const { return false; }

    // --- Age / Maturity / Lifespan (breeding system, client-local) ---
    float GetAge() const { return age_; }
    bool IsMature() const { return mature_; }
    void SetMaturityAge(float seconds) { maturityAge_ = seconds; }
    void SetMaxAge(float seconds) { maxAge_ = seconds; }

    // --- Vision: uses base class DB-driven members (set via InitFromDB → InitBehaviorStats) ---

    /// Initialize from DB creature stats. Call after CreateComponent.
    void InitFromDB(int creatureId, const String& modelPath, float speed,
                    float wanderRad, float visionRange, float visionAngle,
                    bool isPredator);

protected:
    String GetModelPath() const override { return dbModelPath_; }

    float GetWanderSpeed() const override { return dbSwimSpeed_; }
    float GetWanderRadius() const override { return dbWanderRadius_; }

protected:
    // Fish-specific DB values (vision/predator use base class db*_ members)
    String dbModelPath_{"Models/UrhoFish.mdl"};
    float dbSwimSpeed_{0.5f};
    float dbWanderRadius_{75.0f};
    float swimSpeed_{0.5f};   // overwritten by InitFromDB
    float boundary_{75.0f};

    // --- Age / Maturity / Lifespan ---
    float age_{0.0f};           ///< Seconds alive
    float maturityAge_{60.0f};  ///< Seconds to reach breeding age (DB-overridable)
    float maxAge_{300.0f};      ///< Lifespan in seconds — dies when exceeded
    bool mature_{false};        ///< Cached flag: age >= maturityAge_

private:
    void Swim(float timeStep);

    float turnSpeed_{2.0f};
    float comfortDist_{12.0f};

    // Camera interaction
    float stareDist_{3.0f};
    float orbitNear_{5.0f};
    float orbitFar_{15.0f};

    WeakPtr<Node> cameraNode_;

    // Materials for wiggle zones
    SharedPtr<Material> baseMat_;
    SharedPtr<Material> orbitMat_;
    SharedPtr<Material> stareMat_;

    /// Cached StaticModel — avoids GetComponent hash lookup every frame.
    WeakPtr<StaticModel> cachedModel_;

    /// Reusable query buffer — avoids heap allocation every frame.
    mutable Vector<Fish*> nearbyBuf_;

    /// Hunt target (big fish chasing school fish). WeakPtr auto-nulls on removal.
    WeakPtr<Node> huntTarget_;
    /// Seconds remaining of "memory" after prey leaves vision cone.
    float huntTimer_{0.0f};
    /// True while actively pursuing prey.
    bool hunting_{false};

protected:
    FishSpatialHash* spatialHash_{nullptr};
};
