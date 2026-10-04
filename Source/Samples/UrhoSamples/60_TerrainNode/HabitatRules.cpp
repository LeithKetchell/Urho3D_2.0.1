// HabitatRules — JSON loader for per-species habitat constraints.

#include "HabitatRules.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/JSONValue.h>
#include <Urho3D/Resource/ResourceCache.h>

bool HabitatRules::Load(Context* context, const String& path)
{
    auto* cache = context->GetSubsystem<ResourceCache>();
    if (!cache)
        return false;

    auto* jsonFile = cache->GetResource<JSONFile>(path);
    if (!jsonFile)
    {
        URHO3D_LOGERRORF("HabitatRules: failed to load %s", path.CString());
        return false;
    }

    const JSONValue& root = jsonFile->GetRoot();
    if (!root.IsObject())
    {
        URHO3D_LOGERROR("HabitatRules: root is not a JSON object");
        return false;
    }

    rules_.Clear();
    speciesOrder_.Clear();

    const JSONObject& obj = root.GetObject();
    for (auto it = obj.Begin(); it != obj.End(); ++it)
    {
        const String& species = it->first_;
        const JSONValue& val = it->second_;
        if (!val.IsObject())
            continue;

        HabitatRule rule;
        rule.species = species;
        rule.altitudeMin = val.Get("altitudeMin").GetFloat(-999.0f);
        rule.altitudeMax = val.Get("altitudeMax").GetFloat(999.0f);
        rule.waterDistMin = val.Get("waterDistMin").GetFloat(0.0f);
        rule.waterDistMax = val.Get("waterDistMax").GetFloat(999.0f);
        rule.minWaterDepth = val.Get("minWaterDepth").GetFloat(0.0f);
        rule.density = val.Get("density").GetI32(1);
        rule.nearBuilding = val.Get("nearBuilding").GetBool(false);

        // groupSize: [min, max]
        const JSONValue& gs = val.Get("groupSize");
        if (gs.IsArray() && gs.GetArray().Size() >= 2)
        {
            rule.groupSizeMin = gs.GetArray()[0].GetI32(1);
            rule.groupSizeMax = gs.GetArray()[1].GetI32(1);
        }

        // biomes: ["grassland", "forest", ...]
        const JSONValue& biomes = val.Get("biomes");
        if (biomes.IsArray())
        {
            for (unsigned i = 0; i < biomes.GetArray().Size(); ++i)
                rule.biomes.Push(BiomeFromString(biomes.GetArray()[i].GetString()));
        }

        rules_[species] = rule;
        speciesOrder_.Push(species);

        URHO3D_LOGINFOF("HabitatRules: loaded %s — alt [%.1f, %.1f], density %d, biomes %u",
            species.CString(), rule.altitudeMin, rule.altitudeMax, rule.density,
            rule.biomes.Size());
    }

    URHO3D_LOGINFOF("HabitatRules: %u species loaded from %s", rules_.Size(), path.CString());
    return !rules_.Empty();
}

const HabitatRule* HabitatRules::GetRule(const String& species) const
{
    auto it = rules_.Find(species);
    if (it != rules_.End())
        return &it->second_;
    return nullptr;
}

bool HabitatRules::Save(Context* context, const String& path) const
{
    JSONFile jsonFile(context);
    JSONValue& root = jsonFile.GetRoot();
    root.SetType(JSON_OBJECT);

    for (const auto& species : speciesOrder_)
    {
        const HabitatRule* rule = GetRule(species);
        if (!rule)
            continue;

        JSONValue entry;
        entry.Set("altitudeMin", JSONValue(rule->altitudeMin));
        entry.Set("altitudeMax", JSONValue(rule->altitudeMax));
        entry.Set("waterDistMin", JSONValue(rule->waterDistMin));
        entry.Set("waterDistMax", JSONValue(rule->waterDistMax));
        entry.Set("density", JSONValue(rule->density));
        entry.Set("nearBuilding", JSONValue(rule->nearBuilding));

        if (rule->minWaterDepth > 0.0f)
            entry.Set("minWaterDepth", JSONValue(rule->minWaterDepth));

        // groupSize
        JSONValue gs;
        gs.SetType(JSON_ARRAY);
        gs.Push(JSONValue(rule->groupSizeMin));
        gs.Push(JSONValue(rule->groupSizeMax));
        entry.Set("groupSize", gs);

        // biomes
        JSONValue biomes;
        biomes.SetType(JSON_ARRAY);
        for (unsigned i = 0; i < rule->biomes.Size(); ++i)
            biomes.Push(JSONValue(BiomeToString(rule->biomes[i])));
        entry.Set("biomes", biomes);

        root.Set(species, entry);
    }

    auto* cache = context->GetSubsystem<ResourceCache>();
    if (!cache)
        return false;

    // Write to the first resource directory that contains Data/
    const Vector<String>& dirs = cache->GetResourceDirs();
    for (unsigned i = 0; i < dirs.Size(); ++i)
    {
        if (dirs[i].Contains("Data"))
        {
            String fullPath = dirs[i] + path;
            Urho3D::File file(context, fullPath, FILE_WRITE);
            if (file.IsOpen())
            {
                bool ok = jsonFile.Save(file);
                if (ok)
                    URHO3D_LOGINFOF("HabitatRules: saved to %s", fullPath.CString());
                return ok;
            }
        }
    }

    URHO3D_LOGERROR("HabitatRules: could not find writable Data directory for save");
    return false;
}

bool HabitatRules::IsValidSpawnPoint(const HabitatRule& rule, HabitatBiome biome,
                                     float altitude, float waterDist)
{
    // Altitude check
    if (altitude < rule.altitudeMin || altitude > rule.altitudeMax)
        return false;

    // Biome check — empty biome list means "any biome"
    if (!rule.biomes.Empty())
    {
        bool biomeMatch = false;
        for (unsigned i = 0; i < rule.biomes.Size(); ++i)
        {
            if (rule.biomes[i] == HABITAT_ANY || rule.biomes[i] == biome)
            {
                biomeMatch = true;
                break;
            }
        }
        if (!biomeMatch)
            return false;
    }

    // Water distance check (for land animals)
    if (waterDist >= 0.0f)
    {
        if (waterDist < rule.waterDistMin || waterDist > rule.waterDistMax)
            return false;
    }

    return true;
}

HabitatBiome HabitatRules::BiomeFromString(const String& str)
{
    if (str == "water")     return HABITAT_WATER;
    if (str == "riverbank") return HABITAT_RIVERBANK;
    if (str == "grassland") return HABITAT_GRASSLAND;
    if (str == "forest")    return HABITAT_FOREST;
    if (str == "mountain")  return HABITAT_MOUNTAIN;
    if (str == "desert")    return HABITAT_DESERT;
    return HABITAT_ANY;
}

HabitatBiome HabitatRules::FromBiomeType(int biomeType)
{
    // Maps TerrainNode's BiomeType enum (same order) to HabitatBiome
    switch (biomeType)
    {
    case 0: return HABITAT_WATER;      // BIOME_WATER
    case 1: return HABITAT_RIVERBANK;  // BIOME_RIVERBANK
    case 2: return HABITAT_GRASSLAND;  // BIOME_GRASSLAND
    case 3: return HABITAT_FOREST;     // BIOME_FOREST
    case 4: return HABITAT_MOUNTAIN;   // BIOME_MOUNTAIN
    case 5: return HABITAT_DESERT;    // BIOME_DESERT
    default: return HABITAT_ANY;       // BIOME_ANY
    }
}

String HabitatRules::BiomeToString(HabitatBiome biome)
{
    switch (biome)
    {
    case HABITAT_WATER:     return "water";
    case HABITAT_RIVERBANK: return "riverbank";
    case HABITAT_GRASSLAND: return "grassland";
    case HABITAT_FOREST:    return "forest";
    case HABITAT_MOUNTAIN:  return "mountain";
    case HABITAT_DESERT:    return "desert";
    default:                return "any";
    }
}
