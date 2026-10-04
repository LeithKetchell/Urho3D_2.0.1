// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#pragma once

#include <Urho3D/Scene/Component.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Container/HashMap.h>

using namespace Urho3D;

namespace Urho3D { class GameDB; }

/// Info about a building type from the DB.
struct BuildingTypeInfo
{
    int id{};
    String name;
    String category;
    int tier{1};
    float footprintX{2.0f};
    float footprintZ{2.0f};
    float height{2.5f};
    int maxHp{100};
    float decayRate{1.0f};
    float warmth{0.0f};
    int storageSlots{0};
    int sleepCapacity{0};
    bool respawn{false};
    String snapType;
    String modelPath;
    String ghostModelPath;
    String description;
};

/// Snap rule — which building snap_types can connect.
struct SnapRule
{
    String fromType;  // snap_type of piece being placed
    String toType;    // snap_type of existing piece
    String align;     // 'end' (extend line) or 'corner' (turn 90)
};

/// Result of snap point detection.
struct SnapResult
{
    bool found{false};
    Vector3 position;
    float rotation{};
    int snappedToId{-1};  // placed_building_id we're snapping to
};

/// Fire-pit state (Fire System Phase 2a — applies only to Stone Ring placements).
/// Phase 3 will add MSG_PIT_STATE network sync; for now state is client-local.
enum FireState
{
    FIRE_UNLIT = 0,
    FIRE_LIT,
    FIRE_EMBERS,
    FIRE_COLD
};

/// Info about a placed building instance.
struct PlacedBuilding
{
    int placedId{};
    int buildingTypeId{};
    Vector3 position;
    float rotation{};
    int hp{};
    int maxHp{};
    String name;
    String snapType;
    float footprintX{2.0f};
    bool gateOpen{false};
    WeakPtr<Node> node;
    // Fire system Phase 2a: per-pit state. Only meaningful for Stone Ring
    // (typeId 50); other buildings ignore. Defaults to UNLIT; Phase 4 ignition
    // writes it and Phase 3 will wire MSG_PIT_STATE for server authority.
    FireState fireState{FIRE_UNLIT};
    // Fire system Phase 2b: per-woodpile state. Only meaningful for Woodpile
    // (typeId 56); other buildings ignore. Server-authoritative state lands
    // in Phase 3 alongside MSG_PIT_STATE / MSG_WOODPILE_STATE — until then,
    // counters live client-local.
    int softwoodBu{0};
    int hardwoodBu{0};
    int woodCapacity{0};   // copied from BuildingTypeInfo::storageSlots on spawn
    float wetness{0.0f};   // 0..1, untouched by logic until Phase 3
};

/// Client-side building placement system.
/// Handles build mode, ghost preview, validation, and server communication.
class BuildingSystem : public Component
{
    URHO3D_OBJECT(BuildingSystem, Component);

public:
    static void RegisterObject(Context* context) { context->RegisterFactory<BuildingSystem>(); }
    explicit BuildingSystem(Context* context);

    /// Enter/exit build mode.
    void SetBuildMode(bool enabled, int buildingTypeId = 0);
    bool IsBuildMode() const { return buildMode_; }

    /// Per-frame: update ghost position from mouse raycast.
    void UpdateGhostPreview(Camera* camera, Terrain* terrain, float waterLevel);

    /// Rotate ghost by 45 degrees.
    void RotateGhost();

    /// Validate placement at current ghost position.
    bool ValidatePlacement(const Vector3& pos, int buildingTypeId,
                           Terrain* terrain, float waterLevel) const;

    /// Request server to place building at ghost position.
    void RequestBuild(Connection* serverConn);

    /// Request server to demolish a building.
    void RequestDemolish(Connection* serverConn, int placedBuildingId);

    /// Handle server building spawn — creates client-side node.
    void HandleBuildingSpawn(int placedId, int typeId,
                             const Vector3& pos, float rotation, int hp);

    /// Handle server building removal.
    void HandleBuildingRemove(int placedId);

    /// Set available building types (loaded from GameDB). Rebuilds fast lookup map.
    void SetBuildingTypes(const Vector<BuildingTypeInfo>& types)
    {
        buildingTypes_ = types;
        typeMap_.Clear();
        for (unsigned i = 0; i < types.Size(); ++i)
            typeMap_[types[i].id] = &buildingTypes_[i];
    }
    const Vector<BuildingTypeInfo>& GetBuildingTypes() const { return buildingTypes_; }

    /// Set snap rules (loaded from GameDB).
    void SetSnapRules(const Vector<SnapRule>& rules) { snapRules_ = rules; }

    /// Set GameDB reference for live snap rule lookups.
    void SetGameDB(GameDB* db) { gameDB_ = db; }

    /// Set terrain reference for auto-flatten on building placement.
    void SetTerrain(Terrain* terrain, Image* heightMap) { terrain_ = terrain; heightMap_ = heightMap; }

    /// Find best snap point near cursor for the current building type.
    SnapResult FindSnapPoint(const Vector3& cursorPos, int buildingTypeId, Terrain* terrain) const;

    /// Get placed buildings for interaction queries.
    const Vector<PlacedBuilding>& GetPlacedBuildings() const { return placedBuildings_; }

    /// Mutable lookup by placed ID — returns nullptr if not found.
    /// Phase 2b: Woodpile interaction needs to update softwoodBu/hardwoodBu
    /// without going through a network round-trip (Phase 3 will replace
    /// this with proper server authority + replication).
    PlacedBuilding* FindPlacedMutable(int placedId)
    {
        for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
            if (placedBuildings_[i].placedId == placedId)
                return &placedBuildings_[i];
        return nullptr;
    }

    /// Find nearest placed building within range of a world position.
    int FindNearestBuilding(const Vector3& pos, float maxDist) const;

    /// Check if a placed building is a gate type.
    bool IsGate(int placedBuildingId) const;

    /// Get gate open state for a placed building.
    bool IsGateOpen(int placedBuildingId) const;

    /// Request server to toggle a gate open/closed.
    void RequestGateToggle(Connection* serverConn, int placedBuildingId);

    /// Handle server gate state change — rotates the gate node.
    void HandleGateState(int placedBuildingId, bool open);

    /// Handle server HP update — update stored HP.
    void HandleBuildingHpUpdate(int placedBuildingId, int newHp);

    /// Request server to repair a building.
    void RequestRepair(Connection* serverConn, int placedBuildingId);

    /// Request server to sleep at a shelter.
    void RequestSleep(Connection* serverConn, int placedBuildingId);

    /// Request server to set respawn point.
    void RequestSetRespawn(Connection* serverConn, int placedBuildingId);

    /// Get building warmth at a world position (checks nearby placed buildings).
    /// Stone Ring warmth is derived from FireState — UNLIT/COLD=0, EMBERS=4, LIT=info->warmth.
    float GetShelterWarmth(const Vector3& pos) const;

    /// Fire System Phase 2a — state accessors for Stone Ring placements.
    /// Returns FIRE_UNLIT for unknown ids or non-pit buildings (treat as "no fire").
    FireState GetFirePitState(int placedBuildingId) const;
    /// Mutates state for the given placed building. No-op if the id doesn't
    /// exist or is not a Stone Ring (guarded to keep non-pit buildings untouched).
    void SetFirePitState(int placedBuildingId, FireState state);
    /// Building-type id for Stone Ring (matches buildings_seed.sql:68).
    static constexpr int BUILDING_TYPE_STONE_RING = 50;

    /// Chest animation — play open/close on animated chest buildings.
    /// Building type ID for Chest (matches buildings_seed.sql:100).
    static constexpr int BUILDING_TYPE_CHEST = 100;
    void PlayChestOpen(int placedBuildingId);
    void PlayChestClose(int placedBuildingId);
    bool IsChest(int placedBuildingId) const;

    /// Check if player has a workshop within range of a position.
    bool HasNearbyWorkshop(const Vector3& pos, float range = 10.0f) const;

    /// Check if player has a watchtower within range of a position.
    bool HasNearbyWatchtower(const Vector3& pos, float range = 30.0f) const;

    /// Get current ghost validity.
    bool IsGhostValid() const { return ghostValid_; }

    /// Is ghost currently snapped to an existing building?
    bool IsSnapped() const { return snappedToId_ >= 0; }

    /// Get current selected building type ID.
    int GetCurrentBuildTypeId() const { return currentBuildTypeId_; }

    /// Find building type info by id. O(1) via HashMap.
    const BuildingTypeInfo* FindTypeInfo(int typeId) const;

private:
    void CreateGhostNode(int buildingTypeId);
    void DestroyGhostNode();
    void UpdateGhostColor();

    bool buildMode_{false};
    int currentBuildTypeId_{0};
    WeakPtr<Node> ghostNode_;
    Vector3 ghostPosition_;
    float ghostRotation_{0.0f};
    bool ghostValid_{false};

    Vector<BuildingTypeInfo> buildingTypes_;
    HashMap<int, const BuildingTypeInfo*> typeMap_;  ///< O(1) type lookup by id.
    Vector<PlacedBuilding> placedBuildings_;
    Vector<SnapRule> snapRules_;
    GameDB* gameDB_{nullptr};
    WeakPtr<Terrain> terrain_;
    Image* heightMap_{nullptr};
    int snappedToId_{-1};

    /// Cached ghost materials (valid/snapped/invalid) — avoid Clone() per frame.
    SharedPtr<Material> ghostMatValid_;
    SharedPtr<Material> ghostMatSnapped_;
    SharedPtr<Material> ghostMatInvalid_;
    int ghostColorState_{-1};  ///< 0=valid, 1=snapped, 2=invalid, -1=unset

    static constexpr float SNAP_DISTANCE = 1.5f;
};
