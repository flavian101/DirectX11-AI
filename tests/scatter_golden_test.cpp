// Golden test for the scatter clean-up (roadmap S11) and the biome migration after it (S4). No Direct3D, no framework.
//
// The clean-up deletes and merges settings structs. It must not move one tree. This test generates the starter scatter over a
// fixed, made-up ground and hashes every instance (quantised: positions to 1 cm, scale and rotation to 0.001). The expected hashes
// below were recorded at commit 848a23f with g++ on Linux, BEFORE the first line of S11 was deleted.
//
// FIRST RUN ON YOUR MACHINE: MSVC's maths library may round the last bit differently from mine. Before you apply any S11 patch,
// run this test on the unchanged code. If it fails with different hashes, put YOUR hashes in the two constants (they are
// per-machine references for "before"), then apply S11. What the test protects is before == after on one machine. If a refactor changes them, the refactor changed behaviour: find out why before
// touching the constants.
//
// If you add a rule on purpose (new default that changes the starter forest), run once, read the printed hashes, and write the
// new ones down together with the reason in docs/ROADMAP.md.
#include "ScatterObject.h"
#include "ScatterPlacement.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

// ---- The ONLY place that turns settings into rules. S11 changes the body of this function, never the test. ----------------
static ScatterRules RulesFor(const ScatterSettings& settings, size_t speciesIndex)
{
    const ScatterSpeciesSettings& species = settings.species[speciesIndex];
    const std::vector<ScatterVariantSettings> looks = species.EffectiveVariants();
    std::vector<float> heights(looks.size());
    for (size_t v = 0; v < looks.size(); ++v) heights[v] = 2.0f + 0.5f * static_cast<float>(v);   // made-up mesh heights, fixed forever
    const std::vector<char> usable(looks.size(), 1);
    return MakeScatterRules(species, looks, heights, usable, static_cast<int>(speciesIndex), settings.seed);
}

// ---- A fixed ground: hills, a steep ridge, and layers that use all four channels -------------------------------------------
static Heightfield MakeGroundField(int cells, float size, float heightScale)
{
    std::vector<float> samples;
    for (int z = 0; z <= cells; ++z)
        for (int x = 0; x <= cells; ++x) {
            const float u = static_cast<float>(x) / cells, v = static_cast<float>(z) / cells;
            float h = 0.45f + 0.18f * std::sin(u * 9.0f) * std::cos(v * 7.0f) + 0.07f * std::sin(u * 31.0f + v * 17.0f);
            h += 0.35f * std::exp(-((u - 0.7f) * (u - 0.7f) + (v - 0.3f) * (v - 0.3f)) * 60.0f);   // a steep peak
            samples.push_back(h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h));
        }
    Heightfield field;
    field.Reset({ cells, cells, size, size, heightScale }, samples);
    return field;
}

static SplatMap MakeGroundSplat(int cells)
{
    SplatMap map;
    map.Resize(cells, cells);
    for (int z = 0; z < cells; ++z)
        for (int x = 0; x < cells; ++x) {
            const float u = static_cast<float>(x) / cells, v = static_cast<float>(z) / cells;
            float w[kSplatChannels] = { 0.55f + 0.4f * std::sin(u * 5.0f), 0.3f + 0.3f * std::cos(v * 6.0f), 0.15f * (1.0f - u), 0.1f * v };
            float total = 0.0f;
            for (float& value : w) { if (value < 0.01f) value = 0.01f; total += value; }
            for (float& value : w) value /= total;
            // Four bytes that add up to exactly 255 (the largest channel absorbs the rounding), written here so the test links nothing else.
            std::uint8_t* texel = &map.rgba[(static_cast<size_t>(z) * cells + x) * kSplatChannels];
            int sum = 0, largest = 0;
            for (int k = 0; k < kSplatChannels; ++k) {
                texel[k] = static_cast<std::uint8_t>(w[k] * 255.0f + 0.5f);
                sum += texel[k];
                if (w[k] > w[largest]) largest = k;
            }
            texel[largest] = static_cast<std::uint8_t>(texel[largest] + (255 - sum));
        }
    return map;
}

// ---- FNV-1a over the bytes: any change of any instance or variant changes the hash ----------------------------------------
static std::uint64_t Fnv(std::uint64_t h, const void* data, size_t bytes)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

struct Recorded { std::uint64_t hash = 0; size_t count = 0; };

static Recorded GenerateAndHash(const ScatterSettings& settings, size_t speciesIndex)
{
    const Heightfield field = MakeGroundField(200, 400.0f, 120.0f);
    const SplatMap splat = MakeGroundSplat(200);
    ScatterGround ground;
    ground.field = &field; ground.splat = &splat;
    ground.posX = 0.0f; ground.posY = 3.0f; ground.posZ = 0.0f;   // a moved ground: the world position must matter too
    const ScatterRect extent = ScatterGroundExtent(ground);

    std::vector<ScatterInstance> instances;
    std::vector<std::uint8_t> variants;
    GenerateScatter(ground, RulesFor(settings, speciesIndex), extent, instances, &variants);

    // Quantised, not raw bytes: sin, acos and tan may differ in the last bit between compilers, and a hash of raw floats would
    // then fail for nothing. Positions to 1 cm, scale and rotation to 0.001.
    auto q = [](float value, float perUnit) { return static_cast<std::int32_t>(std::lround(value * perUnit)); };
    Recorded r;
    r.count = instances.size();
    r.hash = 1469598103934665603ull;
    for (const ScatterInstance& i : instances) {
        const std::int32_t packed[8] = { q(i.x, 100.0f), q(i.y, 100.0f), q(i.z, 100.0f), q(i.scale, 1000.0f),
                                         q(i.qx, 1000.0f), q(i.qy, 1000.0f), q(i.qz, 1000.0f), q(i.qw, 1000.0f) };
        r.hash = Fnv(r.hash, packed, sizeof(packed));
    }
    r.hash = Fnv(r.hash, variants.data(), variants.size());
    return r;
}

// Recorded at commit 848a23f with the starter scatter (pines, boulders in four looks), before S11.
static const std::uint64_t kPinesHash = 0xbd5d82d06a4d8ef5ull;
static const std::uint64_t kBouldersHash = 0x05fe32b4e873d253ull;

static void TestStarterScatterNeverMoves()
{
    const ScatterSettings settings = ScatterSettings::Starter();
    CHECK(settings.species.size() == 2);
    const Recorded pines = GenerateAndHash(settings, 0), boulders = GenerateAndHash(settings, 1);
    std::printf("pines:    %zu instances, hash 0x%016llxull\n", pines.count, static_cast<unsigned long long>(pines.hash));
    std::printf("boulders: %zu instances, hash 0x%016llxull\n", boulders.count, static_cast<unsigned long long>(boulders.hash));
    CHECK(pines.count > 100 && boulders.count > 20);   // the ground really grows something: the hash is not the hash of nothing
    if (kPinesHash == 0 || kBouldersHash == 0) { std::printf("RECORD MODE: copy the hashes above into kPinesHash and kBouldersHash\n"); ++g_failures; return; }
    CHECK(pines.hash == kPinesHash);
    CHECK(boulders.hash == kBouldersHash);
}

int main()
{
    TestStarterScatterNeverMoves();
    if (g_failures) { std::printf("ScatterGoldenTests: %d failure(s)\n", g_failures); return 1; }
    std::printf("ScatterGoldenTests: all passed\n");
    return 0;
}
