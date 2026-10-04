// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "MetalDeposits.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>

MetalDeposits::MetalDeposits(Context* context) :
    LogicComponent(context)
{
}

void MetalDeposits::RegisterObject(Context* context)
{
    context->RegisterFactory<MetalDeposits>();
}

bool MetalDeposits::LoadMap(const String& path, int expectedSize)
{
    auto* cache = GetSubsystem<ResourceCache>();
    depositMap_ = cache->GetResource<Image>(path);
    if (!depositMap_)
    {
        URHO3D_LOGWARNINGF("MetalDeposits: could not load '%s'", path.CString());
        return false;
    }

    mapSize_ = depositMap_->GetWidth();
    if (mapSize_ != expectedSize)
    {
        URHO3D_LOGWARNINGF("MetalDeposits: map size %d != expected %d", mapSize_, expectedSize);
        depositMap_.Reset();
        mapSize_ = 0;
        return false;
    }

    URHO3D_LOGINFOF("MetalDeposits: loaded %dx%d deposit map", mapSize_, mapSize_);
    return true;
}

bool MetalDeposits::Sample(int x, int z, unsigned char& outType, unsigned char& outPurity,
                           unsigned char& outQuantity, unsigned char& outDepth) const
{
    if (!depositMap_ || x < 0 || z < 0 || x >= mapSize_ || z >= mapSize_)
        return false;

    Color c = depositMap_->GetPixel(x, z);
    outQuantity = (unsigned char)(c.r_ * 255.0f);
    outType     = (unsigned char)(c.g_ * 255.0f);
    outPurity   = (unsigned char)(c.b_ * 255.0f);
    outDepth    = (unsigned char)(c.a_ * 255.0f);
    return outType > 0 && outQuantity > 0;
}

int MetalDeposits::Mine(int x, int z, int amount)
{
    if (!depositMap_ || x < 0 || z < 0 || x >= mapSize_ || z >= mapSize_)
        return 0;

    Color c = depositMap_->GetPixel(x, z);
    int qty = (int)(c.r_ * 255.0f);
    if (qty <= 0)
        return 0;

    int removed = Min(qty, amount);
    c.r_ = (float)(qty - removed) / 255.0f;
    depositMap_->SetPixel(x, z, c);

    if (qty - removed <= 0)
        RemoveOutcrop(x, z);

    return removed;
}

bool MetalDeposits::SaveMap(const String& path) const
{
    if (!depositMap_)
        return false;
    return depositMap_->SavePNG(path);
}

void MetalDeposits::SpawnOutcrops(Scene* scene, Terrain* terrain, float waterLevel, int maxDepth)
{
    if (!depositMap_ || !scene || !terrain)
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    // Metal type -> tint color for outcrop model
    HashMap<int, Color> tints;
    tints[METAL_COPPER]   = Color(0.2f, 0.7f, 0.3f);   // green
    tints[METAL_TIN]      = Color(0.6f, 0.6f, 0.65f);   // grey
    tints[METAL_IRON]     = Color(0.7f, 0.3f, 0.2f);    // rust
    tints[METAL_GOLD]     = Color(0.9f, 0.8f, 0.2f);    // yellow
    tints[METAL_SILVER]   = Color(0.8f, 0.8f, 0.85f);   // silver
    tints[METAL_COAL]     = Color(0.15f, 0.15f, 0.15f);  // black
    tints[METAL_OBSIDIAN] = Color(0.1f, 0.1f, 0.12f);   // dark glassy
    tints[METAL_FLINT]    = Color(0.5f, 0.4f, 0.3f);    // brown

    int count = 0;
    for (int z = 0; z < mapSize_; ++z)
    {
        for (int x = 0; x < mapSize_; ++x)
        {
            unsigned char type, purity, qty, depth;
            if (!Sample(x, z, type, purity, qty, depth))
                continue;
            if (depth > maxDepth)
                continue;

            Vector3 world = terrain->HeightMapToWorld(IntVector2(x, z));
            if (world.y_ < waterLevel + 0.2f)
                continue;

            Node* node = scene->CreateChild("Outcrop");
            node->SetPosition(world);
            node->SetScale(0.4f + (purity / 255.0f) * 0.3f);

            auto* model = node->CreateComponent<StaticModel>();
            Model* outcropMdl = cache->GetResource<Model>("Models/Pickups/Rock_Medium_3.mdl");
            if (!outcropMdl) outcropMdl = cache->GetResource<Model>("Models/Box.mdl");
            model->SetModel(outcropMdl);

            // Create tinted material clone
            auto* baseMat = cache->GetResource<Material>("Materials/Stone.xml");
            if (baseMat)
            {
                SharedPtr<Material> mat = baseMat->Clone();
                Color tint = tints.Contains((int)type) ? tints[(int)type] : Color::GRAY;
                mat->SetShaderParameter("MatDiffColor", tint);
                model->SetMaterial(mat);
            }

            outcropNodes_[IntVector2(x, z)] = node->GetID();
            ++count;
        }
    }

    URHO3D_LOGINFOF("MetalDeposits: spawned %d surface outcrops (depth <= %d)", count, maxDepth);
}

void MetalDeposits::SpawnSingleOutcrop(int x, int z, int metalType, Terrain* terrain)
{
    if (!terrain)
        return;

    Scene* scene = GetScene();
    if (!scene)
        return;

    // Already have an outcrop here
    if (outcropNodes_.Contains(IntVector2(x, z)))
        return;

    auto* cache = GetSubsystem<ResourceCache>();

    HashMap<int, Color> tints;
    tints[METAL_COPPER]   = Color(0.2f, 0.7f, 0.3f);
    tints[METAL_TIN]      = Color(0.6f, 0.6f, 0.65f);
    tints[METAL_IRON]     = Color(0.7f, 0.3f, 0.2f);
    tints[METAL_GOLD]     = Color(0.9f, 0.8f, 0.2f);
    tints[METAL_SILVER]   = Color(0.8f, 0.8f, 0.85f);
    tints[METAL_COAL]     = Color(0.15f, 0.15f, 0.15f);
    tints[METAL_OBSIDIAN] = Color(0.1f, 0.1f, 0.12f);
    tints[METAL_FLINT]    = Color(0.5f, 0.4f, 0.3f);

    Vector3 world = terrain->HeightMapToWorld(IntVector2(x, z));

    Node* node = scene->CreateChild("Outcrop");
    node->SetPosition(world);
    node->SetScale(0.5f);

    auto* model = node->CreateComponent<StaticModel>();
    Model* outcropMdl = cache->GetResource<Model>("Models/Pickups/Rock_Medium_3.mdl");
    if (!outcropMdl) outcropMdl = cache->GetResource<Model>("Models/Box.mdl");
    model->SetModel(outcropMdl);

    auto* baseMat = cache->GetResource<Material>("Materials/Stone.xml");
    if (baseMat)
    {
        SharedPtr<Material> mat = baseMat->Clone();
        Color tint = tints.Contains(metalType) ? tints[metalType] : Color::GRAY;
        mat->SetShaderParameter("MatDiffColor", tint);
        model->SetMaterial(mat);
    }

    outcropNodes_[IntVector2(x, z)] = node->GetID();
    URHO3D_LOGINFOF("MetalDeposits: spawned outcrop at (%d, %d) type=%d", x, z, metalType);
}

void MetalDeposits::RemoveOutcrop(int x, int z)
{
    auto it = outcropNodes_.Find(IntVector2(x, z));
    if (it == outcropNodes_.End())
        return;

    Scene* scene = GetScene();
    if (scene)
    {
        Node* node = scene->GetNode(it->second_);
        if (node)
            node->Remove();
    }
    outcropNodes_.Erase(it);
}
