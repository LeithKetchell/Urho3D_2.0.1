// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "BuildingSystem.h"

#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/ParticleEffect.h>
#include <Urho3D/Graphics/ParticleEmitter.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Network/Protocol.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Game/GameDB.h>

BuildingSystem::BuildingSystem(Context* context)
    : Component(context)
{
}

void BuildingSystem::SetBuildMode(bool enabled, int buildingTypeId)
{
    if (enabled == buildMode_ && buildingTypeId == currentBuildTypeId_)
        return;

    buildMode_ = enabled;
    currentBuildTypeId_ = buildingTypeId;

    if (buildMode_)
        CreateGhostNode(buildingTypeId);
    else
        DestroyGhostNode();
}

const BuildingTypeInfo* BuildingSystem::FindTypeInfo(int typeId) const
{
    auto it = typeMap_.Find(typeId);
    return (it != typeMap_.End()) ? it->second_ : nullptr;
}

SnapResult BuildingSystem::FindSnapPoint(const Vector3& cursorPos, int buildingTypeId, Terrain* terrain) const
{
    SnapResult result;

    const BuildingTypeInfo* newInfo = FindTypeInfo(buildingTypeId);
    if (!newInfo || newInfo->snapType == "free" || newInfo->snapType == "interior")
        return result;

    float bestDist = SNAP_DISTANCE;

    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        const PlacedBuilding& existing = placedBuildings_[i];
        if (existing.snapType.Empty() || existing.snapType == "free" || existing.snapType == "interior")
            continue;

        // Check if there's a snap rule for this combination via GameDB
        String alignStr;
        if (gameDB_)
            alignStr = gameDB_->GetSnapAlign(newInfo->snapType, existing.snapType);
        else
        {
            // Fallback to cached rules if no DB
            for (unsigned r = 0; r < snapRules_.Size(); ++r)
            {
                if (snapRules_[r].fromType == newInfo->snapType && snapRules_[r].toType == existing.snapType)
                {
                    alignStr = snapRules_[r].align;
                    break;
                }
            }
        }
        if (alignStr.Empty())
            continue;

        // Compute the two endpoint anchors of the existing building
        Quaternion existingRot(existing.rotation, Vector3::UP);
        Vector3 dir = existingRot * Vector3::RIGHT;  // wall extends along local X
        float halfW = existing.footprintX * 0.5f;
        Vector3 anchorRight = existing.position + dir * halfW;
        Vector3 anchorLeft  = existing.position - dir * halfW;

        // Check distance to each anchor (XZ plane only for snap detection)
        float dxR = cursorPos.x_ - anchorRight.x_, dzR = cursorPos.z_ - anchorRight.z_;
        float distRight = sqrtf(dxR * dxR + dzR * dzR);
        float dxL = cursorPos.x_ - anchorLeft.x_, dzL = cursorPos.z_ - anchorLeft.z_;
        float distLeft = sqrtf(dxL * dxL + dzL * dzL);

        // Test right anchor
        if (distRight < bestDist)
        {
            float halfNew = newInfo->footprintX * 0.5f;
            Vector3 snapPos = existing.position + dir * (halfW + halfNew);
            if (terrain)
                snapPos.y_ = terrain->GetHeight(snapPos);

            float snapRot = existing.rotation;
            if (alignStr == "corner")
                snapRot += 90.0f;

            result.found = true;
            result.position = snapPos;
            result.rotation = snapRot;
            result.snappedToId = existing.placedId;
            bestDist = distRight;
        }

        // Test left anchor
        if (distLeft < bestDist)
        {
            float halfNew = newInfo->footprintX * 0.5f;
            Vector3 snapPos = existing.position - dir * (halfW + halfNew);
            if (terrain)
                snapPos.y_ = terrain->GetHeight(snapPos);

            float snapRot = existing.rotation;
            if (alignStr == "corner")
                snapRot -= 90.0f;

            result.found = true;
            result.position = snapPos;
            result.rotation = snapRot;
            result.snappedToId = existing.placedId;
            bestDist = distLeft;
        }
    }

    return result;
}

void BuildingSystem::UpdateGhostPreview(Camera* camera, Terrain* terrain, float waterLevel)
{
    if (!buildMode_ || !ghostNode_ || !camera || !terrain)
        return;

    auto* graphics = GetSubsystem<Graphics>();
    auto* input = GetSubsystem<Input>();
    IntVector2 mousePos = input->GetMousePosition();

    float mx = (float)mousePos.x_ / (float)graphics->GetWidth();
    float my = (float)mousePos.y_ / (float)graphics->GetHeight();
    Ray ray = camera->GetScreenRay(mx, my);

    // Intersect ray with terrain — coarse step then binary refinement
    // Coarse: 8-unit steps to find bracket, then 8 binary iterations to refine.
    // Worst case: ~33 terrain queries instead of ~400.
    Vector3 hitPos;
    bool hit = false;
    float tLo = 1.0f, tHi = 200.0f;
    {
        float prevT = tLo;
        for (float t = tLo; t < tHi; t += 8.0f)
        {
            Vector3 p = ray.origin_ + ray.direction_ * t;
            if (p.y_ <= terrain->GetHeight(p))
            {
                // Found bracket [prevT, t] — binary search to refine
                float lo = prevT, hi = t;
                for (int iter = 0; iter < 8; ++iter)
                {
                    float mid = (lo + hi) * 0.5f;
                    Vector3 pm = ray.origin_ + ray.direction_ * mid;
                    if (pm.y_ <= terrain->GetHeight(pm))
                        hi = mid;
                    else
                        lo = mid;
                }
                Vector3 ph = ray.origin_ + ray.direction_ * hi;
                hitPos = Vector3(ph.x_, terrain->GetHeight(ph), ph.z_);
                hit = true;
                break;
            }
            prevT = t;
        }
    }

    if (!hit)
        return;

    // Try snap detection for wall/gate/corner pieces
    SnapResult snap = FindSnapPoint(hitPos, currentBuildTypeId_, terrain);
    if (snap.found)
    {
        ghostPosition_ = snap.position;
        ghostRotation_ = snap.rotation;
        snappedToId_ = snap.snappedToId;
    }
    else
    {
        ghostPosition_ = hitPos;
        snappedToId_ = -1;
    }

    ghostNode_->SetPosition(ghostPosition_);
    ghostNode_->SetRotation(Quaternion(ghostRotation_, Vector3::UP));

    ghostValid_ = ValidatePlacement(ghostPosition_, currentBuildTypeId_, terrain, waterLevel);
    UpdateGhostColor();
}

void BuildingSystem::RotateGhost()
{
    ghostRotation_ += 45.0f;
    if (ghostRotation_ >= 360.0f)
        ghostRotation_ -= 360.0f;

    if (ghostNode_)
        ghostNode_->SetRotation(Quaternion(ghostRotation_, Vector3::UP));
}

bool BuildingSystem::ValidatePlacement(const Vector3& pos, int buildingTypeId,
                                        Terrain* terrain, float waterLevel) const
{
    const BuildingTypeInfo* info = FindTypeInfo(buildingTypeId);
    if (!info)
        return false;

    // 1. Slope check — terrain normal must be mostly upward
    Vector3 normal = terrain->GetNormal(pos);
    if (normal.y_ < 0.85f)  // ~30 degree slope max
        return false;

    // 2. Water check — can't build underwater (except fish weir)
    if (pos.y_ < waterLevel && info->name != "Fish Weir")
        return false;

    // 3. Overlap check — no existing building too close
    //    Skip the building we're snapping to (it's supposed to be adjacent)
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        if (placedBuildings_[i].placedId == snappedToId_)
            continue;
        float minDist = (info->footprintX + info->footprintZ) * 0.5f;
        float minDist2 = minDist * minDist;
        if ((placedBuildings_[i].position - pos).LengthSquared() < minDist2)
            return false;
    }

    return true;
}

void BuildingSystem::RequestBuild(Connection* serverConn)
{
    if (!buildMode_ || !ghostValid_)
        return;

    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteI32(currentBuildTypeId_);
        buf.WriteFloat(ghostPosition_.x_);
        buf.WriteFloat(ghostPosition_.y_);
        buf.WriteFloat(ghostPosition_.z_);
        buf.WriteFloat(ghostRotation_);
        buf.WriteI32(snappedToId_);  // -1 = freestanding
        serverConn->SendMessage(MSG_BUILD, true, true, buf);
    }
    else
    {
        // Offline mode — create locally with a fake ID
        static int offlineId = 10000;
        HandleBuildingSpawn(offlineId++, currentBuildTypeId_,
                           ghostPosition_, ghostRotation_, 100);
    }
}

void BuildingSystem::RequestDemolish(Connection* serverConn, int placedBuildingId)
{
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteI32(placedBuildingId);
        serverConn->SendMessage(MSG_DEMOLISH, true, true, buf);
    }
    else
    {
        HandleBuildingRemove(placedBuildingId);
    }
}

void BuildingSystem::HandleBuildingSpawn(int placedId, int typeId,
                                          const Vector3& pos, float rotation, int hp)
{
    auto* scene = GetScene();
    if (!scene)
        return;

    const BuildingTypeInfo* info = FindTypeInfo(typeId);

    // Auto-flatten terrain under building footprint (with margin)
    if (terrain_ && heightMap_ && info)
    {
        float footX = info->footprintX + 1.0f;  // 0.5m margin each side
        float footZ = info->footprintZ + 1.0f;
        float targetH = terrain_->GetHeight(pos);

        Vector3 spacing = terrain_->GetSpacing();
        IntVector2 numVerts = terrain_->GetNumVertices();
        float terrainW = (float)(numVerts.x_ - 1) * spacing.x_;
        float terrainH = (float)(numVerts.y_ - 1) * spacing.z_;
        Vector3 terrainPos = terrain_->GetNode()->GetWorldPosition();

        // Convert footprint to pixel range
        float halfFX = footX * 0.5f;
        float halfFZ = footZ * 0.5f;

        int imgW = heightMap_->GetWidth();
        int imgH = heightMap_->GetHeight();

        for (float wz = pos.z_ - halfFZ; wz <= pos.z_ + halfFZ; wz += spacing.z_)
        {
            for (float wx = pos.x_ - halfFX; wx <= pos.x_ + halfFX; wx += spacing.x_)
            {
                float normX = (wx - terrainPos.x_ + terrainW * 0.5f) / terrainW;
                float normZ = (wz - terrainPos.z_ + terrainH * 0.5f) / terrainH;
                int px = Clamp((int)(normX * imgW), 0, imgW - 1);
                int pz = Clamp((int)(normZ * imgH), 0, imgH - 1);

                // Encode height as normalized value matching terrain's height range
                float normH = targetH / (spacing.y_ * 255.0f);
                Color c = heightMap_->GetPixel(px, pz);
                c.r_ = normH;
                heightMap_->SetPixel(px, pz, c);
            }
        }
        terrain_->ApplyHeightMap();
        URHO3D_LOGINFOF("Building auto-flatten: %.1fx%.1f pad at (%.1f,%.1f,%.1f)",
            footX, footZ, pos.x_, pos.y_, pos.z_);
    }

    auto* cache = GetSubsystem<ResourceCache>();
    Node* node = scene->CreateChild(info ? info->name : "Building", LOCAL);
    node->SetPosition(pos);
    node->SetRotation(Quaternion(rotation, Vector3::UP));
    node->SetVar("PlacedBuildingId", placedId);
    node->SetVar("BuildingTypeId", typeId);

    // Try to load model — use a box placeholder if model doesn't exist.
    // Animated buildings (chests) use AnimatedModel + AnimationController.
    Model* mdl = nullptr;
    if (info && info->modelPath.Length() > 0)
        mdl = cache->GetResource<Model>(info->modelPath, false);

    if (mdl)
    {
        if (typeId == BUILDING_TYPE_CHEST)
        {
            auto* animModel = node->CreateComponent<AnimatedModel>(LOCAL);
            animModel->SetModel(mdl);
            node->CreateComponent<AnimationController>(LOCAL);
            // Start in closed idle pose
            auto* ctrl = node->GetComponent<AnimationController>();
            if (ctrl)
                ctrl->PlayExclusive("Models/Props/Chest_Wood_Chest_ArmatureChest_Closed.ani", 0, false, 0.0f);
        }
        else
        {
            auto* model = node->CreateComponent<StaticModel>(LOCAL);
            model->SetModel(mdl);
        }
    }
    else
    {
        // Placeholder box scaled to footprint
        auto* model = node->CreateComponent<StaticModel>(LOCAL);
        auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
        if (boxMdl)
        {
            model->SetModel(boxMdl);
            float sx = info ? info->footprintX : 2.0f;
            float sy = info ? info->height : 2.5f;
            float sz = info ? info->footprintZ : 2.0f;
            node->SetScale(Vector3(sx, sy, sz));
            node->SetPosition(pos + Vector3(0, sy * 0.5f, 0));
        }
    }

    // Lighting props — torches, candles, lanterns, chandeliers (decoration types
    // carrying warmth) emit point light. building_types has no light column, so the
    // per-type warmth value doubles as the light-range proxy.
    if (info && info->category == "decoration" && info->warmth > 0.0f)
    {
        Node* lightNode = node->CreateChild("PropLight", LOCAL);
        lightNode->SetPosition(Vector3(0.0f, Max(info->height * 0.75f, 0.2f), 0.0f));
        auto* propLight = lightNode->CreateComponent<Light>(LOCAL);
        propLight->SetLightType(LIGHT_POINT);
        propLight->SetColor(Color(1.0f, 0.72f, 0.38f));      // warm flame
        propLight->SetRange(Max(info->warmth, 2.0f));
        propLight->SetBrightness(0.85f);
        propLight->SetCastShadows(false);
    }

    // Chimney smoke — buildings with warmth emit a gentle smoke plume from roof
    if (info && info->warmth > 0.0f)
    {
        auto* smokeEffect = cache->GetResource<ParticleEffect>("Particle/ChimneySmoke.xml", false);
        if (smokeEffect)
        {
            Node* smokeNode = node->CreateChild("ChimneySmoke", LOCAL);
            smokeNode->SetPosition(Vector3(0.0f, info->height, 0.0f));
            auto* emitter = smokeNode->CreateComponent<ParticleEmitter>(LOCAL);
            emitter->SetEffect(smokeEffect);
        }
    }

    // Settlement banner identity — tint cloth by settlement color
    if (info && (typeId == 142 || typeId == 143))
    {
        // Derive unique color from placedId — deterministic per settlement
        unsigned h = (unsigned)placedId;
        h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
        float hue = (float)(h & 0xFF) / 255.0f;
        // HSV to RGB with high saturation and medium brightness
        float s = 0.7f, v = 0.85f;
        int hi = (int)(hue * 6.0f) % 6;
        float f = hue * 6.0f - (float)hi;
        float p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s);
        Color bannerColor;
        switch (hi)
        {
        case 0: bannerColor = Color(v, t, p); break;
        case 1: bannerColor = Color(q, v, p); break;
        case 2: bannerColor = Color(p, v, t); break;
        case 3: bannerColor = Color(p, q, v); break;
        case 4: bannerColor = Color(t, p, v); break;
        default: bannerColor = Color(v, p, q); break;
        }

        auto* sm = node->GetComponent<StaticModel>();
        if (sm)
        {
            auto* baseMat = sm->GetMaterial();
            if (baseMat)
            {
                SharedPtr<Material> tinted = baseMat->Clone();
                tinted->SetShaderParameter("MatDiffColor", bannerColor);
                sm->SetMaterial(tinted);
            }
        }
    }

    // Building aging — moss/vines appear on weathered structures (HP < 75%)
    if (info && info->category != "furniture" && info->maxHp > 0)
    {
        float hpRatio = (float)hp / (float)info->maxHp;
        if (hpRatio < 0.75f)
        {
            // More moss as building decays: 1 piece at 75%, up to 3 at 25%
            int mossCount = (hpRatio < 0.25f) ? 3 : (hpRatio < 0.5f) ? 2 : 1;
            auto* mossMdl = cache->GetResource<Model>("Models/Nature/hanging_moss.mdl", false);
            auto* mossMat = cache->GetResource<Material>("Models/Nature/Materials/leafsDark.xml", false);
            if (mossMdl)
            {
                float halfX = info->footprintX * 0.4f;
                float halfZ = info->footprintZ * 0.4f;
                for (int m = 0; m < mossCount; ++m)
                {
                    Node* mossNode = node->CreateChild("AgingMoss", LOCAL);
                    // Distribute around building walls at varying heights
                    float angle = (float)m * (360.0f / (float)mossCount) + 30.0f;
                    float rad = angle * 0.0174533f;
                    float mx = halfX * cosf(rad);
                    float mz = halfZ * sinf(rad);
                    float my = info->height * (0.4f + (float)m * 0.2f);
                    mossNode->SetPosition(Vector3(mx, my, mz));
                    mossNode->SetScale(0.3f + hpRatio * 0.1f);
                    auto* sm = mossNode->CreateComponent<StaticModel>(LOCAL);
                    sm->SetModel(mossMdl);
                    if (mossMat)
                        sm->SetMaterial(mossMat);
                    sm->SetCastShadows(false);
                }
            }
        }
    }

    PlacedBuilding pb;
    pb.placedId = placedId;
    pb.buildingTypeId = typeId;
    pb.position = pos;
    pb.rotation = rotation;
    pb.hp = hp;
    pb.maxHp = info ? info->maxHp : 100;
    pb.name = info ? info->name : "Building";
    pb.snapType = info ? info->snapType : "free";
    pb.footprintX = info ? info->footprintX : 2.0f;
    pb.gateOpen = false;
    pb.node = node;
    // Fire system Phase 2b: Woodpile reuses storageSlots field as per-wood-type capacity.
    // Other building types ignore — counters stay 0, hover hint won't fire.
    static constexpr int BUILDING_TYPE_WOODPILE = 56;  // matches buildings_seed.sql
    if (info && typeId == BUILDING_TYPE_WOODPILE)
        pb.woodCapacity = info->storageSlots;
    // Fire system Phase 2a: Stone Ring starts UNLIT (field default, explicit for clarity).
    if (typeId == BUILDING_TYPE_STONE_RING)
        pb.fireState = FIRE_UNLIT;
    placedBuildings_.Push(pb);

    URHO3D_LOGINFOF("Building spawned: %s (id=%d) at %.1f,%.1f,%.1f",
                    pb.name.CString(), placedId, pos.x_, pos.y_, pos.z_);
}

void BuildingSystem::HandleBuildingRemove(int placedId)
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        if (placedBuildings_[i].placedId == placedId)
        {
            if (placedBuildings_[i].node)
                placedBuildings_[i].node->Remove();
            placedBuildings_.Erase(i);
            URHO3D_LOGINFOF("Building removed: id=%d", placedId);
            return;
        }
    }
}

int BuildingSystem::FindNearestBuilding(const Vector3& pos, float maxDist) const
{
    int bestId = -1;
    float bestDist2 = maxDist * maxDist;
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        float dist2 = (placedBuildings_[i].position - pos).LengthSquared();
        if (dist2 < bestDist2)
        {
            bestDist2 = dist2;
            bestId = placedBuildings_[i].placedId;
        }
    }
    return bestId;
}

bool BuildingSystem::IsGate(int placedBuildingId) const
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        if (placedBuildings_[i].placedId == placedBuildingId)
            return placedBuildings_[i].snapType == "gate";
    }
    return false;
}

bool BuildingSystem::IsGateOpen(int placedBuildingId) const
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        if (placedBuildings_[i].placedId == placedBuildingId)
            return placedBuildings_[i].gateOpen;
    }
    return false;
}

void BuildingSystem::RequestGateToggle(Connection* serverConn, int placedBuildingId)
{
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteI32(placedBuildingId);
        serverConn->SendMessage(MSG_GATE_TOGGLE, true, true, buf);
    }
    else
    {
        // Offline: toggle immediately
        for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
        {
            if (placedBuildings_[i].placedId == placedBuildingId)
            {
                HandleGateState(placedBuildingId, !placedBuildings_[i].gateOpen);
                return;
            }
        }
    }
}

void BuildingSystem::HandleGateState(int placedBuildingId, bool open)
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        PlacedBuilding& pb = placedBuildings_[i];
        if (pb.placedId == placedBuildingId)
        {
            pb.gateOpen = open;
            if (pb.node)
            {
                float rot = pb.rotation + (open ? 90.0f : 0.0f);
                pb.node->SetRotation(Quaternion(rot, Vector3::UP));
            }
            URHO3D_LOGINFOF("Gate %s: id=%d", open ? "opened" : "closed", placedBuildingId);
            return;
        }
    }
}

bool BuildingSystem::IsChest(int placedBuildingId) const
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
        if (placedBuildings_[i].placedId == placedBuildingId)
            return placedBuildings_[i].buildingTypeId == BUILDING_TYPE_CHEST;
    return false;
}

void BuildingSystem::PlayChestOpen(int placedBuildingId)
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        PlacedBuilding& pb = placedBuildings_[i];
        if (pb.placedId == placedBuildingId && pb.node)
        {
            auto* ctrl = pb.node->GetComponent<AnimationController>();
            if (ctrl)
            {
                ctrl->PlayExclusive("Models/Props/Chest_Wood_Chest_ArmatureChest_Open.ani", 0, false, 0.2f);
                ctrl->SetAutoFade("Models/Props/Chest_Wood_Chest_ArmatureChest_Open.ani", 0.0f);
            }
            return;
        }
    }
}

void BuildingSystem::PlayChestClose(int placedBuildingId)
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        PlacedBuilding& pb = placedBuildings_[i];
        if (pb.placedId == placedBuildingId && pb.node)
        {
            auto* ctrl = pb.node->GetComponent<AnimationController>();
            if (ctrl)
            {
                ctrl->PlayExclusive("Models/Props/Chest_Wood_Chest_ArmatureChest_Close.ani", 0, false, 0.2f);
                ctrl->SetAutoFade("Models/Props/Chest_Wood_Chest_ArmatureChest_Close.ani", 0.0f);
            }
            return;
        }
    }
}

void BuildingSystem::CreateGhostNode(int buildingTypeId)
{
    DestroyGhostNode();

    // Reset cached ghost materials for the new ghost model
    ghostMatValid_.Reset();
    ghostMatSnapped_.Reset();
    ghostMatInvalid_.Reset();
    ghostColorState_ = -1;

    auto* scene = GetScene();
    if (!scene)
        return;

    const BuildingTypeInfo* info = FindTypeInfo(buildingTypeId);

    auto* cache = GetSubsystem<ResourceCache>();
    Node* node = scene->CreateChild("BuildGhost", LOCAL);

    auto* model = node->CreateComponent<StaticModel>(LOCAL);

    // Try ghost model, then regular model, then placeholder box
    Model* mdl = nullptr;
    if (info && info->ghostModelPath.Length() > 0)
        mdl = cache->GetResource<Model>(info->ghostModelPath, false);
    if (!mdl && info && info->modelPath.Length() > 0)
        mdl = cache->GetResource<Model>(info->modelPath, false);

    if (mdl)
    {
        model->SetModel(mdl);
    }
    else
    {
        auto* boxMdl = cache->GetResource<Model>("Models/Box.mdl");
        if (boxMdl)
        {
            model->SetModel(boxMdl);
            float sx = info ? info->footprintX : 2.0f;
            float sy = info ? info->height : 2.5f;
            float sz = info ? info->footprintZ : 2.0f;
            node->SetScale(Vector3(sx, sy, sz));
        }
    }

    ghostNode_ = node;
    ghostRotation_ = 0.0f;
}

void BuildingSystem::DestroyGhostNode()
{
    if (ghostNode_)
    {
        ghostNode_->Remove();
        ghostNode_ = nullptr;
    }
}

void BuildingSystem::HandleBuildingHpUpdate(int placedBuildingId, int newHp)
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        if (placedBuildings_[i].placedId == placedBuildingId)
        {
            placedBuildings_[i].hp = newHp;
            return;
        }
    }
}

void BuildingSystem::RequestRepair(Connection* serverConn, int placedBuildingId)
{
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteI32(placedBuildingId);
        serverConn->SendMessage(MSG_REPAIR, true, true, buf);
    }
    else
    {
        PlacedBuilding* pb = FindPlacedMutable(placedBuildingId);
        if (pb)
        {
            HandleBuildingHpUpdate(placedBuildingId, pb->maxHp);
            URHO3D_LOGINFOF("Building %d repaired (offline): hp=%d", placedBuildingId, pb->maxHp);
        }
    }
}

void BuildingSystem::RequestSleep(Connection* serverConn, int placedBuildingId)
{
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteI32(placedBuildingId);
        serverConn->SendMessage(MSG_SLEEP, true, true, buf);
    }
    else
    {
        URHO3D_LOGINFOF("Sleep at building %d (offline)", placedBuildingId);
    }
}

void BuildingSystem::RequestSetRespawn(Connection* serverConn, int placedBuildingId)
{
    if (serverConn)
    {
        VectorBuffer buf;
        buf.WriteI32(placedBuildingId);
        serverConn->SendMessage(MSG_SET_RESPAWN, true, true, buf);
    }
    else
    {
        PlacedBuilding* pb = FindPlacedMutable(placedBuildingId);
        if (pb)
            URHO3D_LOGINFOF("Respawn set at building %d (offline) pos=(%.1f,%.1f,%.1f)",
                placedBuildingId, pb->position.x_, pb->position.y_, pb->position.z_);
    }
}

float BuildingSystem::GetShelterWarmth(const Vector3& pos) const
{
    float bestWarmth = 0.0f;

    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        const PlacedBuilding& pb = placedBuildings_[i];
        // Find the type info for warmth
        const BuildingTypeInfo* info = FindTypeInfo(pb.buildingTypeId);
        if (!info || info->warmth <= 0.0f)
            continue;

        // Fire System Phase 2a: Stone Ring warmth is state-derived, not static.
        // UNLIT/COLD → 0 (cold stones don't warm you), EMBERS → 4 (low glow),
        // LIT → info->warmth (full fire, 15 per buildings_seed.sql). Non-pit
        // buildings (shelters, huts, etc.) keep using their static info->warmth.
        float effectiveWarmth = info->warmth;
        if (pb.buildingTypeId == BUILDING_TYPE_STONE_RING)
        {
            switch (pb.fireState)
            {
            case FIRE_LIT:    effectiveWarmth = info->warmth; break;
            case FIRE_EMBERS: effectiveWarmth = 4.0f; break;
            case FIRE_UNLIT:
            case FIRE_COLD:
            default:          effectiveWarmth = 0.0f; break;
            }
            if (effectiveWarmth <= 0.0f)
                continue;  // No heat — skip distance check
        }

        float dx = pos.x_ - pb.position.x_;
        float dz = pos.z_ - pb.position.z_;
        float dist2 = dx * dx + dz * dz;

        // Range: footprint for shelters, 5m for utility (fire pits)
        float range = Max(info->footprintX, info->footprintZ) * 0.75f;
        if (info->category == "utility")
            range = 5.0f;

        if (dist2 <= range * range && effectiveWarmth > bestWarmth)
            bestWarmth = effectiveWarmth;
    }

    return bestWarmth;
}

FireState BuildingSystem::GetFirePitState(int placedBuildingId) const
{
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        if (placedBuildings_[i].placedId == placedBuildingId)
            return placedBuildings_[i].fireState;
    }
    return FIRE_UNLIT;  // Unknown id — safe default
}

void BuildingSystem::SetFirePitState(int placedBuildingId, FireState state)
{
    PlacedBuilding* pb = FindPlacedMutable(placedBuildingId);
    if (!pb)
        return;
    // Guard — state field is only meaningful for Stone Rings. Silently ignore
    // attempts to set state on other building types so callers can't corrupt
    // unrelated placements.
    if (pb->buildingTypeId != BUILDING_TYPE_STONE_RING)
        return;
    pb->fireState = state;
}

bool BuildingSystem::HasNearbyWorkshop(const Vector3& pos, float range) const
{
    float range2 = range * range;
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        const PlacedBuilding& pb = placedBuildings_[i];
        float dx = pos.x_ - pb.position.x_;
        float dz = pos.z_ - pb.position.z_;
        if (dx * dx + dz * dz > range2)
            continue;  // Distance cull before type lookup
        const BuildingTypeInfo* info = FindTypeInfo(pb.buildingTypeId);
        if (info && info->category == "workshop")
            return true;
    }
    return false;
}

bool BuildingSystem::HasNearbyWatchtower(const Vector3& pos, float range) const
{
    float range2 = range * range;
    for (unsigned i = 0; i < placedBuildings_.Size(); ++i)
    {
        const PlacedBuilding& pb = placedBuildings_[i];
        float dx = pos.x_ - pb.position.x_;
        float dz = pos.z_ - pb.position.z_;
        if (dx * dx + dz * dz > range2)
            continue;  // Distance cull before type lookup
        const BuildingTypeInfo* info = FindTypeInfo(pb.buildingTypeId);
        if (info && info->name == "Watchtower")
            return true;
    }
    return false;
}

void BuildingSystem::UpdateGhostColor()
{
    if (!ghostNode_)
        return;

    auto* model = ghostNode_->GetComponent<StaticModel>();
    if (!model)
        return;

    // Determine desired state: 0=valid, 1=snapped, 2=invalid
    int desiredState = !ghostValid_ ? 2 : (snappedToId_ >= 0 ? 1 : 0);
    if (desiredState == ghostColorState_)
        return;  // No change — skip material work entirely
    ghostColorState_ = desiredState;

    Color ghostColor;
    SharedPtr<Material>* cached;
    if (desiredState == 2)
    {
        ghostColor = Color(0.8f, 0.2f, 0.2f, 0.4f);
        cached = &ghostMatInvalid_;
    }
    else if (desiredState == 1)
    {
        ghostColor = Color(0.2f, 0.6f, 1.0f, 0.5f);
        cached = &ghostMatSnapped_;
    }
    else
    {
        ghostColor = Color(0.2f, 0.8f, 0.2f, 0.4f);
        cached = &ghostMatValid_;
    }

    // Clone once per state, reuse thereafter
    if (!*cached)
    {
        auto* baseMat = model->GetMaterial(0);
        if (baseMat)
        {
            *cached = baseMat->Clone();
            (*cached)->SetShaderParameter("MatDiffColor", Variant(ghostColor));
        }
    }

    if (*cached)
    {
        for (unsigned i = 0; i < model->GetNumGeometries(); ++i)
            model->SetMaterial(i, *cached);
    }
}
