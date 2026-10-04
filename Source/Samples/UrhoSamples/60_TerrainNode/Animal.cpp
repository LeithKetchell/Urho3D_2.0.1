// Animal — base class for terrain-roaming wildlife.

#include "Animal.h"
#include "BuildingSystem.h"

#include <Urho3D/Game/GameDB.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/IO/Log.h>

unsigned Animal::nextStaggerID_ = 0;

Animal::Animal(Context* context) :
    LogicComponent(context)
{
    staggerID_ = nextStaggerID_++;
    SetUpdateEventMask(LogicComponentEvents::FixedUpdate);
}

void Animal::RegisterObject(Context* /*context*/)
{
    // Abstract base — subclasses register their own factories
}

void Animal::Start()
{
    // Cache terrain
    terrain_ = GetScene()->GetComponent<Terrain>(true);

    // Normalize model to real-world size until all models are reimported at correct scale
    auto* mdl = node_->GetComponent<AnimatedModel>(true);
    if (mdl)
    {
        BoundingBox bb = mdl->GetBoundingBox();
        Vector3 size = bb.Size();
        float longest = Max(size.x_, Max(size.y_, size.z_));
        if (longest > 0.001f)
            node_->SetScale(GetDesiredSize() / longest);
    }

    // Set home to current position
    homePos_ = node_->GetWorldPosition();

    // Start idle
    stateTimer_ = Random(GetMinIdleDuration(), GetMaxIdleDuration());
    OnStateEnter(ANIMAL_IDLE);
}

void Animal::FixedUpdate(float timeStep)
{
    static unsigned frameCounter = 0;
    ++frameCounter;

    stateTimer_ -= timeStep;

    switch (state_)
    {
    case ANIMAL_IDLE:
        if (stateTimer_ <= 0.0f)
        {
            PickWanderTarget();
            SetState(ANIMAL_WANDER);
        }
        break;

    case ANIMAL_WANDER:
    {
        MoveToward(wanderTarget_, GetWanderSpeed(), timeStep);

        // Check if reached target
        Vector3 pos = node_->GetWorldPosition();
        Vector3 diff = wanderTarget_ - pos;
        diff.y_ = 0.0f;
        if (diff.Length() < 1.0f || stateTimer_ <= 0.0f)
            SetState(ANIMAL_IDLE);
        break;
    }

    case ANIMAL_FLEE:
        if (stateTimer_ <= 0.0f)
            SetState(ANIMAL_IDLE);
        else
            MoveToward(wanderTarget_, GetFleeSpeed(), timeStep);
        break;

    case ANIMAL_DIE:
        // Death animation plays, then respawn at a safe location
        if (stateTimer_ <= 0.0f)
            Respawn();
        break;
    }

    SnapToTerrain();

    // Water avoidance: if in water and not dying, flee toward home (dry land)
    float drownTime = GetDrownTime();
    if (drownTime >= 0.0f && state_ != ANIMAL_DIE)
    {
        Vector3 pos = node_->GetWorldPosition();
        if (pos.y_ < waterLevel_)
        {
            drownTimer_ += timeStep;
            // Stagger drowning scans: only scan on frames matching our stagger slot
            // This spreads terrain queries across 3 frames instead of all-at-once
            if (state_ != ANIMAL_FLEE && terrain_ && (frameCounter % 3 == staggerID_ % 3))
            {
                // Sample 4 cardinal directions (down from 12), pick highest ground
                float bestY = -M_INFINITY;
                Vector3 bestTarget = pos;
                for (int i = 0; i < 4; ++i)
                {
                    float angle = (float)i * 90.0f;
                    float x = pos.x_ + 10.0f * Cos(angle);
                    float z = pos.z_ + 10.0f * Sin(angle);
                    float y = terrain_->GetHeight(Vector3(x, 0.0f, z));
                    if (y > bestY)
                    {
                        bestY = y;
                        bestTarget = Vector3(x, y, z);
                    }
                }
                wanderTarget_ = bestTarget;
                SetState(ANIMAL_FLEE);
            }
            if (drownTimer_ >= drownTime)
                SetState(ANIMAL_DIE);
        }
        else
        {
            drownTimer_ = 0.0f;
        }
    }
}

void Animal::SetState(AnimalState newState)
{
    if (state_ == newState)
        return;

    state_ = newState;

    switch (newState)
    {
    case ANIMAL_IDLE:
        stateTimer_ = Random(GetMinIdleDuration(), GetMaxIdleDuration());
        break;

    case ANIMAL_WANDER:
        // Timer = generous time to reach target before giving up
        stateTimer_ = 10.0f;
        break;

    case ANIMAL_FLEE:
        stateTimer_ = 3.0f;
        break;

    case ANIMAL_DIE:
        stateTimer_ = 5.0f;  // death anim + linger, then remove
        {
            using namespace AnimalDied;
            VariantMap& eventData = GetEventDataMap();
            eventData[P_CREATUREID] = GetCreatureId();
            eventData[P_POSITION]   = node_ ? node_->GetWorldPosition() : Vector3::ZERO;
            SendEvent(E_ANIMALDIED, eventData);
        }
        break;
    }

    OnStateEnter(newState);
}

void Animal::OnStateEnter(AnimalState newState)
{
    auto* animCtrl = node_->GetComponent<AnimationController>(true);
    if (!animCtrl)
        return;

    switch (newState)
    {
    case ANIMAL_IDLE:
        animCtrl->PlayExclusive(GetIdleAnim(), 0, true, 0.3f);
        break;

    case ANIMAL_WANDER:
        animCtrl->PlayExclusive(GetRunAnim(), 0, true, 0.3f);
        animCtrl->SetSpeed(GetRunAnim(), 0.5f);
        break;

    case ANIMAL_FLEE:
        animCtrl->PlayExclusive(GetRunAnim(), 0, true, 0.2f);
        animCtrl->SetSpeed(GetRunAnim(), 1.5f);
        break;

    case ANIMAL_DIE:
        animCtrl->PlayExclusive(GetDieAnim(), 0, false, 0.2f);
        break;
    }
}

void Animal::PickWanderTarget()
{
    float radius = GetWanderRadius();

    // ~25% chance: head toward the water's edge to drink
    bool seekWater = (Random(1.0f) < 0.25f);

    if (seekWater && terrain_)
    {
        // Sample radial directions from current position, find one near waterline
        Vector3 pos = node_->GetWorldPosition();
        float bestDist = M_INFINITY;
        Vector3 bestTarget = pos;
        bool found = false;

        for (int i = 0; i < 12; ++i)
        {
            float angle = Random(0.0f, 360.0f);
            float dist = Random(5.0f, radius);
            float x = pos.x_ + dist * Cos(angle);
            float z = pos.z_ + dist * Sin(angle);
            float y = terrain_->GetHeight(Vector3(x, 0.0f, z));

            // Shore zone: just above waterline (within 1 metre)
            if (y > waterLevel_ && y < waterLevel_ + 1.0f)
            {
                float d = Vector2(x - pos.x_, z - pos.z_).Length();
                if (d < bestDist)
                {
                    bestDist = d;
                    bestTarget = Vector3(x, y, z);
                    found = true;
                }
            }
        }

        if (found)
        {
            wanderTarget_ = bestTarget;
            return;
        }
        // No shore found nearby — fall through to normal wander
    }

    // Normal wander: pick dry land
    for (int tries = 0; tries < 8; ++tries)
    {
        float shrink = (tries > 3) ? 0.5f : 1.0f;
        float x = homePos_.x_ + Random(-radius * shrink, radius * shrink);
        float z = homePos_.z_ + Random(-radius * shrink, radius * shrink);
        float y = terrain_ ? terrain_->GetHeight(Vector3(x, 0.0f, z)) : 0.0f;

        if (y > waterLevel_)
        {
            wanderTarget_ = Vector3(x, y, z);
            return;
        }
    }

    // All attempts wet — stay put
    wanderTarget_ = node_->GetWorldPosition();
}

void Animal::MoveToward(const Vector3& target, float speed, float timeStep)
{
    Vector3 pos = node_->GetWorldPosition();
    Vector3 dir = target - pos;
    dir.y_ = 0.0f;
    float dist = dir.Length();

    if (dist < 0.1f)
        return;

    dir.Normalize();

    // Smooth turn: rotate toward movement direction
    Vector3 currentFwd = node_->GetWorldDirection();
    currentFwd.y_ = 0.0f;
    currentFwd.Normalize();

    float turnRate = 4.0f * timeStep;  // radians-ish per second
    Vector3 newFwd = currentFwd.Lerp(dir, Min(turnRate, 1.0f));
    newFwd.y_ = 0.0f;
    if (newFwd.LengthSquared() > 0.001f)
    {
        newFwd.Normalize();
        node_->SetWorldDirection(newFwd);
    }

    // Only move forward along the direction we're actually facing
    Vector3 newPos = pos + newFwd * speed * timeStep;

    // Wall collision check — blocked animals pick a new idle/wander
    if (IsBlockedByWall(pos, newPos))
    {
        SetState(ANIMAL_IDLE);
        return;
    }

    // Don't walk into water
    if (terrain_)
    {
        float groundY = terrain_->GetHeight(newPos);
        if (groundY < waterLevel_)
        {
            SetState(ANIMAL_IDLE);
            return;
        }
    }

    node_->SetWorldPosition(newPos);
}

void Animal::SnapToTerrain()
{
    if (!terrain_)
        return;

    Vector3 pos = node_->GetWorldPosition();

    // Only re-query terrain height if we moved more than 0.5 units horizontally
    float dx = pos.x_ - cachedHeightPos_.x_;
    float dz = pos.z_ - cachedHeightPos_.z_;
    if (dx * dx + dz * dz > 0.25f)
    {
        cachedHeight_ = terrain_->GetHeight(pos);
        cachedHeightPos_ = pos;
    }

    if (Abs(pos.y_ - cachedHeight_) > 0.01f)
    {
        pos.y_ = cachedHeight_;
        node_->SetWorldPosition(pos);
    }
}

void Animal::Respawn()
{
    if (!terrain_)
        return;

    // Find safe ground near home position
    float radius = GetWanderRadius();
    Vector3 spawnPos = homePos_;

    for (int tries = 0; tries < 20; ++tries)
    {
        float x = homePos_.x_ + Random(-radius, radius);
        float z = homePos_.z_ + Random(-radius, radius);
        float y = terrain_->GetHeight(Vector3(x, 0.0f, z));
        if (y > waterLevel_)
        {
            spawnPos = Vector3(x, y, z);
            break;
        }
    }

    node_->SetWorldPosition(spawnPos);
    drownTimer_ = 0.0f;
    state_ = ANIMAL_IDLE;  // bypass SetState guard (same-state check)
    stateTimer_ = Random(GetMinIdleDuration(), GetMaxIdleDuration());
    OnStateEnter(ANIMAL_IDLE);
}

bool Animal::IsBlockedByWall(const Vector3& from, const Vector3& to) const
{
    if (!buildingSystem_)
        return false;

    int creatureId = GetCreatureId();
    if (creatureId <= 0)
        return false;

    const auto& buildings = buildingSystem_->GetPlacedBuildings();
    const auto& types = buildingSystem_->GetBuildingTypes();

    for (unsigned i = 0; i < buildings.Size(); ++i)
    {
        const PlacedBuilding& pb = buildings[i];

        // Only check wall/gate/corner snap types
        if (pb.snapType != "wall" && pb.snapType != "gate" && pb.snapType != "corner")
            continue;

        // Open gates don't block
        if (pb.snapType == "gate" && pb.gateOpen)
            continue;

        // Simple proximity check: is the new position within the wall's footprint?
        float dx = to.x_ - pb.position.x_;
        float dz = to.z_ - pb.position.z_;
        float dist = sqrtf(dx * dx + dz * dz);

        // Use footprint as collision radius
        float radius = pb.footprintX * 0.6f;
        if (dist > radius)
            continue;

        // Was the old position also within radius? (already inside — don't trap)
        float dxFrom = from.x_ - pb.position.x_;
        float dzFrom = from.z_ - pb.position.z_;
        float distFrom = sqrtf(dxFrom * dxFrom + dzFrom * dzFrom);
        if (distFrom <= radius)
            continue;  // already inside, let them move out

        // Check wall_strength via GameDB lookup
        if (gameDB_ && gameDB_->DoesWallBlock(pb.buildingTypeId, creatureId))
            return true;
    }

    return false;
}

// ============================================================================
// Combat
// ============================================================================

void Animal::InitCombatStats(int hp, int attack, int defense, int damage, int damageVar,
                              int speed, float fleeFraction, const String& aggression)
{
    hp_          = hp;
    maxHp_       = hp;
    attack_      = attack;
    defense_     = defense;
    damage_      = damage;
    damageVar_   = damageVar;
    speed_       = speed;
    fleeFraction_ = fleeFraction;
    aggression_  = aggression;
}

bool Animal::TakeDamage(int amount)
{
    if (state_ == ANIMAL_DIE)
        return false;  // already dead

    hp_ -= amount;
    if (hp_ < 0) hp_ = 0;

    URHO3D_LOGINFOF("Animal %s took %d damage — HP %d/%d",
        GetNode() ? GetNode()->GetName().CString() : "?", amount, hp_, maxHp_);

    if (hp_ <= 0)
    {
        SetState(ANIMAL_DIE);
        return true;
    }

    // Flee or fight based on aggression and HP threshold
    float hpFrac = (float)hp_ / (float)maxHp_;
    if (hpFrac <= fleeFraction_)
    {
        if (aggression_ == "aggressive")
            SetState(ANIMAL_FIGHT);
        else
            SetState(ANIMAL_FLEE);
    }
    else if (aggression_ == "aggressive" && state_ != ANIMAL_FIGHT)
    {
        SetState(ANIMAL_FIGHT);
    }

    return false;
}
