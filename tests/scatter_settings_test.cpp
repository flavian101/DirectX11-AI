// Tests for src/ScatterSettings.cpp (lesson 36): the variants of a scatter species, their limits, how a species' settings become
// placement rules, and that scene files written before variants existed still load. No Direct3D is run, no framework.
//   1. An old scene file (no "variants" key) is a one-look species made from its "model".
//   2. The starter scatter, its variants and material overrides round-trip through the scene file's JSON unchanged.
//   3. Clamp keeps weights, biases, slope bands, sizes and the override list in range, and at most kMaxScatterVariants looks.
//   4. MakeScatterRules: a variant's numbers reach the rules; a look that is off or did not load gets weight 0 and the
//      placement gives its share to the others.
#include "SceneJson.h"
#include "ScatterPlacement.h"
#include <cstdio>
#include <cmath>
using nlohmann::json;

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_failures; } } while (0)

static void TestOldSceneFilesLoad()
{
    const json old = json::parse(R"({"enabled":true,"cellSize":32,"species":[{"name":"Pines","model":"assets/pine.glb","spacing":7.0}]})");
    const ScatterSettings a = old.get<ScatterSettings>();
    CHECK(a.species.size() == 1 && a.species[0].variants.empty() && a.species[0].model == "assets/pine.glb");
    const std::vector<ScatterVariantSettings> looks = a.species[0].EffectiveVariants();
    CHECK(looks.size() == 1 && looks[0].model == "assets/pine.glb" && looks[0].name == "Pines" && looks[0].weight == 1.0f && looks[0].enabled);
    CHECK(looks[0].materials.empty() && looks[0].minSlope == 0.0f && looks[0].maxSlope == 90.0f && looks[0].scaleMul == 1.0f);
    // A species that asks for nothing new writes an empty list.
    const json plain = ScatterSpeciesSettings{};
    CHECK(plain["variants"].is_array() && plain["variants"].empty());
}

static void TestRoundTrip()
{
    const ScatterSettings starter = ScatterSettings::Starter();
    CHECK(starter.species.size() == 2 && starter.species[0].variants.empty() && starter.species[1].variants.size() == 4);
    const json written = starter;
    const ScatterSettings back = written.get<ScatterSettings>();
    CHECK(back.species[1].variants.size() == 4 && back.species[1].variants[3].name == "Mossy boulder" && back.species[1].variants[3].layerBias[0] == 3.0f);
    CHECK(json(back) == written);

    ScatterSpeciesSettings species;
    ScatterVariantSettings granite; granite.name = "Granite"; granite.model = "assets/rock.glb";
    ScatterMaterialOverride o;
    o.slot = 1; o.albedo = "assets/g_albedo.png"; o.normal = "assets/g_n.png"; o.orm = "assets/g_orm.png"; o.emissive = "assets/g_e.png";
    o.roughness = 0.4f; o.flipGreen = false; o.baseColor = { 0.5f, 0.6f, 0.7f, 1.0f };
    granite.materials.push_back(o);
    species.variants.push_back(granite);
    const ScatterSpeciesSettings loaded = json(species).get<ScatterSpeciesSettings>();
    CHECK(loaded.variants.size() == 1 && loaded.variants[0].materials.size() == 1);
    const ScatterMaterialOverride& r = loaded.variants[0].materials[0];
    CHECK(r.slot == 1 && r.albedo == o.albedo && r.normal == o.normal && r.orm == o.orm && r.emissive == o.emissive);
    CHECK(r.roughness == 0.4f && !r.flipGreen && r.baseColor[0] == 0.5f && r.baseColor[1] == 0.6f && r.baseColor[2] == 0.7f);
}

static void TestClamp()
{
    ScatterSpeciesSettings wild;
    for (int i = 0; i < 12; ++i) wild.variants.push_back(ScatterVariantSettings{});
    ScatterVariantSettings& v = wild.variants[0];
    v.weight = -3.0f; v.scaleMul = 0.0f; v.minSlope = 80.0f; v.maxSlope = 10.0f; v.layerBias[2] = 99.0f;
    v.materials.resize(40); v.materials[0].slot = -5; v.materials[0].baseColor = { -1.0f, 99.0f, 1.0f, 1.0f }; v.materials[0].roughness = -7.0f; v.materials[1].roughness = 50.0f;
    wild.Clamp();
    CHECK(wild.variants.size() == static_cast<size_t>(kMaxScatterVariants));
    CHECK(wild.variants[0].weight == 0.0f && wild.variants[0].scaleMul >= 0.05f);
    CHECK(wild.variants[0].maxSlope >= wild.variants[0].minSlope && wild.variants[0].layerBias[2] <= 4.0f);
    CHECK(wild.variants[0].materials.size() == 16 && wild.variants[0].materials[0].slot == 0);
    CHECK(wild.variants[0].materials[0].baseColor[0] == 0.0f && wild.variants[0].materials[0].baseColor[1] == 8.0f);
    CHECK(wild.variants[0].materials[0].roughness == -1.0f && wild.variants[0].materials[1].roughness == 2.0f);
}

static void TestRulesFromSettings()
{
    ScatterSpeciesSettings species;
    species.spacing = 4.0f; species.minDistance = 2.0f; species.sink = 0.5f;
    ScatterVariantSettings a, b, c;
    a.weight = 3.0f; b.weight = 1.0f; b.minSlope = 20.0f; b.scaleMul = 2.0f; b.layerBias = { 0.0f, 1.0f, 1.0f, 1.0f }; c.weight = 5.0f;
    const std::vector<ScatterVariantSettings> looks = { a, b, c };
    const std::vector<float> heights = { 1.5f, 3.0f, 9.0f };

    // All three usable: the numbers arrive as they were written.
    ScatterRules rules = MakeScatterRules(species, looks, heights, { 1, 1, 1 }, 0, 1);
    CHECK(rules.variantCount == 3);
    CHECK(rules.variants[0].weight == 3.0f && rules.variants[0].modelHeight == 1.5f);
    CHECK(rules.variants[1].weight == 1.0f && rules.variants[1].minSlope == 20.0f && rules.variants[1].scaleMul == 2.0f &&
          rules.variants[1].layerBias[0] == 0.0f && rules.variants[1].modelHeight == 3.0f);
    CHECK(rules.variants[2].weight == 5.0f && rules.variants[2].modelHeight == 9.0f);
    CHECK(rules.spacing == 4.0f && rules.sink == 0.5f);

    // Variant 2 is switched off or failed to load: weight 0 and "use the species' height", so nothing is ever placed as it.
    rules = MakeScatterRules(species, looks, heights, { 1, 1, 0 }, 0, 1);
    CHECK(rules.variants[2].weight == 0.0f && rules.variants[2].modelHeight < 0.0f);
    {
        const Heightfield field = [] { std::vector<float> samples(101 * 101, 0.1f); Heightfield f; f.Reset({ 100, 100, 200.0f, 200.0f, 10.0f }, samples); return f; }();
        SplatMap splat; splat.Resize(100, 100); for (size_t i = 0; i < splat.rgba.size(); i += 4) splat.rgba[i] = 255;   // all grass
        ScatterGround ground; ground.field = &field; ground.splat = &splat;
        std::vector<ScatterInstance> out; std::vector<std::uint8_t> which;
        GenerateScatter(ground, rules, ScatterGroundExtent(ground), out, &which);
        size_t counts[3] = {};
        for (std::uint8_t v : which) { CHECK(v < 3); if (v < 3) ++counts[v]; }
        CHECK(out.size() > 300 && counts[0] == out.size() && counts[1] == 0 && counts[2] == 0);   // b needs slope >= 20, c is gone: all a
    }

    // Nothing usable at all: nothing grows, and nothing crashes.
    rules = MakeScatterRules(species, looks, heights, { 0, 0, 0 }, 0, 1);
    CHECK(rules.variants[0].weight == 0.0f && rules.variants[1].weight == 0.0f && rules.variants[2].weight == 0.0f);

    // A species without variants is the one-look species of lesson 31: one variant, weight 1, the mesh's height.
    ScatterSpeciesSettings oldStyle; oldStyle.model = "builtin:pine";
    rules = MakeScatterRules(oldStyle, oldStyle.EffectiveVariants(), { 10.95f }, { 1 }, 0, 1);
    CHECK(rules.variantCount == 1 && rules.variants[0].weight == 1.0f && std::fabs(rules.variants[0].modelHeight - 10.95f) < 1e-4f);

    // Each species has its own random stream, and so has each global seed.
    const ScatterRules s0 = MakeScatterRules(species, looks, heights, { 1, 1, 1 }, 0, 1);
    const ScatterRules s1 = MakeScatterRules(species, looks, heights, { 1, 1, 1 }, 1, 1);
    const ScatterRules g2 = MakeScatterRules(species, looks, heights, { 1, 1, 1 }, 0, 2);
    CHECK(s0.seed != s1.seed && s0.seed != g2.seed);
}

int main()
{
    TestOldSceneFilesLoad();
    TestRoundTrip();
    TestClamp();
    TestRulesFromSettings();
    if (g_failures == 0) std::printf("ScatterSettingsTests: all passed\n");
    else std::printf("ScatterSettingsTests: %d FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
