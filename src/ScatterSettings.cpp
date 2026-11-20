#include "ScatterObject.h"
#include "ProceduralModels.h"
#include <algorithm>

// Everything about scatter that needs no Direct3D: the settings' limits, the starter scatter, and the translation of a species'
// settings into the placement rules. It lives apart from ScatterObject.cpp so it can be tested (tests/scatter_settings_test.cpp).

namespace {
// Everything in the rules that belongs to the SPECIES (where it grows, how big, how it leans), not to a variant.
ScatterRules MakeBaseRules(const ScatterSpeciesSettings& s, int speciesIndex, int globalSeed)
{
    ScatterRules r;
    static_cast<ScatterSpeciesRules&>(r) = s;   // the species' numbers ARE the rules' numbers: one struct, copied whole
    // Every species gets its own random stream: the global seed, the species' own seed and its place in the list.
    r.seed = static_cast<std::uint32_t>(globalSeed) * 0x9e3779b1u ^ static_cast<std::uint32_t>(s.seed) * 0x85ebca6bu ^
             static_cast<std::uint32_t>(speciesIndex + 1) * 0xc2b2ae35u;
    ClampScatterRules(r);
    return r;
}

}  // namespace

// The species' rules plus its variants. A variant that is switched off, or whose model could not be loaded, gets weight 0:
// it is never chosen, so the others take its place instead of leaving holes. `heights` are the loaded meshes' heights.
ScatterRules MakeScatterRules(const ScatterSpeciesSettings& s, const std::vector<ScatterVariantSettings>& looks, const std::vector<float>& heights,
                       const std::vector<char>& usable, int speciesIndex, int globalSeed)
{
    ScatterRules r = MakeBaseRules(s, speciesIndex, globalSeed);
    r.variantCount = (std::min)((std::max)(static_cast<int>(looks.size()), 1), kMaxScatterVariants);
    for (int v = 0; v < r.variantCount && v < static_cast<int>(looks.size()); ++v) {
        const ScatterVariantSettings& look = looks[static_cast<size_t>(v)];
        ScatterVariantRules& out = r.variants[v];
        const bool ok = usable[static_cast<size_t>(v)] != 0;
        static_cast<ScatterVariantBase&>(out) = look;   // weight, layer bias, slope band, scale: copied whole
        if (!ok) out.weight = 0.0f;
        out.modelHeight = ok ? heights[static_cast<size_t>(v)] : 1.0f;   // a look that cannot be drawn has weight 0: its height is never used
    }
    ClampScatterRules(r);
    return r;
}

// ---- Settings -----------------------------------------------------------------------------------------------------------

void ScatterVariantSettings::Clamp()
{
    ClampScatterVariantBase(*this);   // the ranges live next to the placement: one clamp, not two copies of it
    if (materials.size() > 16) materials.resize(16);
    for (ScatterMaterialOverride& o : materials) {
        o.slot = std::clamp(o.slot, 0, 63);
        for (float& c : o.baseColor) c = std::clamp(c, 0.0f, 8.0f);
        o.roughness = o.roughness < 0.0f ? -1.0f : (std::min)(o.roughness, 2.0f);
    }
}

ScatterSpeciesSettings::ScatterSpeciesSettings()
{
    ScatterVariantSettings pine;
    pine.name = "Pine";
    pine.model = kBuiltinPine;
    variants = { pine };
}

void ScatterSpeciesSettings::Clamp()
{
    ClampScatterSpeciesRules(*this);   // the ranges live next to the placement: one clamp, not a copy of it
    lodScreen[0] = std::clamp(lodScreen[0], 0.003f, 1.0f);
    lodScreen[1] = std::clamp(lodScreen[1], 0.002f, lodScreen[0] * 0.95f);   // each level starts below the one before it
    lodScreen[2] = std::clamp(lodScreen[2], 0.001f, lodScreen[1] * 0.95f);
    cullScreen = std::clamp(cullScreen, 0.0f, lodScreen[2]);
    shadowDistance = std::clamp(shadowDistance, 0.0f, 2000.0f);
    if (variants.empty()) variants.push_back(ScatterVariantSettings{});   // a species without a look would place nothing and have nothing to show
    if (variants.size() > static_cast<size_t>(kMaxScatterVariants)) variants.resize(static_cast<size_t>(kMaxScatterVariants));
    for (ScatterVariantSettings& v : variants) v.Clamp();
}

void ScatterSettings::Clamp()
{
    cellSize = std::clamp(cellSize, kCellMin, kCellMax);
    lodBias = std::clamp(lodBias, ModelRenderSettings::kBiasMin, ModelRenderSettings::kBiasMax);
    reflectionDetail = std::clamp(reflectionDetail, 0.1f, 1.0f);
    generationBudgetMs = std::clamp(generationBudgetMs, 0.5f, 100.0f);
    maxInstancesPerSpecies = std::clamp(maxInstancesPerSpecies, 1000, 20000000);
    for (ScatterSpeciesSettings& s : species) s.Clamp();
}

ScatterSettings ScatterSettings::Starter()
{
    ScatterSettings settings;
    ScatterSpeciesSettings pines;
    pines.name = "Pines";
    pines.variants[0].name = "Pines"; pines.variants[0].model = kBuiltinPine;   // (the constructor already made one look: a pine)
    pines.spacing = 7.0f; pines.minDistance = 4.5f; pines.density = 0.9f;
    pines.layerDensity = { 1.0f, 0.0f, 0.0f, 0.0f };   // grass only: sand at the shore and rock on the cliffs stay clear
    pines.maxSlope = 32.0f;
    pines.clusterStrength = 0.6f; pines.clusterScale = 60.0f;
    pines.scaleMin = 0.8f; pines.scaleMax = 1.4f;
    pines.tilt = 2.0f;

    // Boulders in four looks (lesson 36): the plain one is common, the flat slab and the tall block less so, and the mossy
    // one prefers the grass. A cliff-side rock would be a fifth with minSlope 50; here the slopes belong to all of them.
    ScatterSpeciesSettings rocks;
    rocks.name = "Boulders";
    rocks.spacing = 9.0f; rocks.minDistance = 4.0f; rocks.density = 0.5f;
    rocks.layerDensity = { 0.3f, 1.0f, 0.2f, 0.4f };   // mostly where the terrain is rock, a few lying in the grass
    rocks.maxSlope = 75.0f;
    rocks.scaleMin = 0.6f; rocks.scaleMax = 1.8f;
    rocks.alignToNormal = 0.7f; rocks.tilt = 0.0f; rocks.sink = 0.25f;
    rocks.shadowDistance = 60.0f;
    rocks.cullScreen = 0.006f;
    ScatterVariantSettings plain, flat, tall, mossy;
    plain.name = "Boulder"; plain.model = kBuiltinRock; plain.weight = 3.0f;
    flat.name = "Slab"; flat.model = kBuiltinRockFlat; flat.weight = 1.5f;
    tall.name = "Block"; tall.model = kBuiltinRockTall; tall.weight = 1.0f; tall.minSlope = 15.0f;   // stands on slopes, not in meadows
    mossy.name = "Mossy boulder"; mossy.model = kBuiltinRockMossy; mossy.weight = 2.0f;
    mossy.layerBias = { 3.0f, 0.2f, 0.5f, 0.0f };   // moss grows on the grass side, not on bare rock or snow
    rocks.variants = { plain, flat, tall, mossy };
    settings.species = { pines, rocks };
    settings.Clamp();
    return settings;
}

