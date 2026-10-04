// HabitatRules — data-driven spawning rules loaded from JSON.
// Each species has altitude, biome, water distance, and density constraints.

#pragma once

#include <Urho3D/Container/HashMap.h>
#include <Urho3D/Container/Str.h>
#include <Urho3D/Container/Vector.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Math/MathDefs.h>

using namespace Urho3D;

/// Biome types matching TerrainNode::BiomeType enum.
/// Stored as strings in JSON for readability.
enum HabitatBiome
{
    HABITAT_WATER,
    HABITAT_RIVERBANK,
    HABITAT_GRASSLAND,
    HABITAT_FOREST,
    HABITAT_MOUNTAIN,
    HABITAT_DESERT,
    HABITAT_ANY
};

/// Per-species habitat rule loaded from habitat_rules.json.
struct HabitatRule
{
    String species;
    float altitudeMin{-999.0f};
    float altitudeMax{999.0f};
    Vector<HabitatBiome> biomes;
    float waterDistMin{0.0f};
    float waterDistMax{999.0f};
    float minWaterDepth{0.0f};     ///< For aquatic species only
    int density{1};                ///< Target count per spawn pass
    int groupSizeMin{1};
    int groupSizeMax{1};
    bool nearBuilding{false};      ///< Prefer spawning near player structures
};

/// Loads and stores habitat rules from a JSON file.
class HabitatRules
{
public:
    /// Load rules from a JSON resource path (e.g. "GameDB/habitat_rules.json").
    /// Returns true if at least one rule was loaded.
    bool Load(Context* context, const String& path);

    /// Get rule for a species. Returns nullptr if not found.
    const HabitatRule* GetRule(const String& species) const;

    /// Get all loaded rules.
    const HashMap<String, HabitatRule>& GetAllRules() const { return rules_; }

    /// Get species names in load order (for UI iteration).
    const Vector<String>& GetSpeciesNames() const { return speciesOrder_; }

    /// Save current rules back to JSON. Returns true on success.
    bool Save(Context* context, const String& path) const;

    /// Check if a position satisfies a species' habitat rule.
    /// biome: the biome at this position (from ClassifyTerrain)
    /// altitude: terrain height at this position
    /// waterDist: estimated horizontal distance to nearest water edge
    static bool IsValidSpawnPoint(const HabitatRule& rule, HabitatBiome biome,
                                  float altitude, float waterDist);

    /// Convert biome string from JSON to enum.
    static HabitatBiome BiomeFromString(const String& str);
    /// Convert enum back to string for JSON serialization.
    static String BiomeToString(HabitatBiome biome);
    /// Convert from TerrainNode's BiomeType (int-compatible) to HabitatBiome.
    static HabitatBiome FromBiomeType(int biomeType);

private:
    HashMap<String, HabitatRule> rules_;
    Vector<String> speciesOrder_;
};
