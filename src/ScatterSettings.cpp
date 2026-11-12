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
    r.spacing = s.spacing; r.minDistance = s.minDistance; r.density = s.density;
    for (int k = 0; k < kSplatChannels; ++k) r.layerDensity[k] = s.layerDensity[k];
    r.minSlope = s.minSlope; r.maxSlope = s.maxSlope; r.minHeight = s.minHeight; r.maxHeight = s.maxHeight;
    r.clusterStrength = s.clusterStrength; r.clusterScale = s.clusterScale;
    r.scaleMin = s.scaleMin; r.scaleMax = s.scaleMax; r.alignToNormal = s.alignToNormal; r.tilt = s.tilt; r.sink = s.sink;
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
        out.weight = ok ? look.weight : 0.0f;
        for (int k = 0; k < kSplatChannels; ++k) out.layerBias[k] = look.layerBias[k];
        out.minSlope = look.minSlope; out.maxSlope = look.maxSlope; out.scaleMul = look.scaleMul;
        out.modelHeight = ok ? heights[static_cast<size_t>(v)] : -1.0f;
    }
    ClampScatterRules(r);
    return r;
}

// ---- Settings -----------------------------------------------------------------------------------------------------------

void ScatterVariantSettings::Clamp()
{
    weight = std::clamp(weight, 0.0f, 1000.0f);
    for (float& bias : layerBias) bias = std::clamp(bias, 0.0f, 4.0f);
    minSlope = std::clamp(minSlope, 0.0f, 90.0f);
    maxSlope = std::clamp(maxSlope, minSlope, 90.0f);
    scaleMul = std::clamp(scaleMul, 0.05f, 20.0f);
    if (materials.size() > 16) materials.resize(16);
    for (ScatterMaterialOverride& o : materials) {
        o.slot = std::clamp(o.slot, 0, 63);
        for (float& c : o.baseColor) c = std::clamp(c, 0.0f, 8.0f);
        o.roughness = o.roughness < 0.0f ? -1.0f : (std::min)(o.roughness, 2.0f);
    }
}

std::vector<ScatterVariantSettings> ScatterSpeciesSettings::EffectiveVariants() const
{
    if (!variants.empty()) return variants;
    ScatterVariantSettings only;
    only.name = name;
    only.model = model;
    return { only };
}

void ScatterSpeciesSettings::Clamp()
{
    ScatterRules rules = MakeBaseRules(*this, 0, 0);   // the placement rules know their own ranges: borrow them
    spacing = rules.spacing; minDistance = rules.minDistance; density = rules.density;
    for (int k = 0; k < kSplatChannels; ++k) layerDensity[k] = rules.layerDensity[k];
    minSlope = rules.minSlope; maxSlope = rules.maxSlope; minHeight = rules.minHeight; maxHeight = rules.maxHeight;
    clusterStrength = rules.clusterStrength; clusterScale = rules.clusterScale;
    scaleMin = rules.scaleMin; scaleMax = rules.scaleMax; alignToNormal = rules.alignToNormal; tilt = rules.tilt; sink = rules.sink;
    lodScreen[0] = std::clamp(lodScreen[0], 0.003f, 1.0f);
    lodScreen[1] = std::clamp(lodScreen[1], 0.002f, lodScreen[0] * 0.95f);   // each level starts below the one before it
    lodScreen[2] = std::clamp(lodScreen[2], 0.001f, lodScreen[1] * 0.95f);
    cullScreen = std::clamp(cullScreen, 0.0f, lodScreen[2]);
    shadowDistance = std::clamp(shadowDistance, 0.0f, 2000.0f);
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
    pines.model = kBuiltinPine;
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
    rocks.model = kBuiltinRock;
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

