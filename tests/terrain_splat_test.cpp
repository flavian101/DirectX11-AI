// Tests for src/TerrainSplat.{h,cpp}: the layer weights baked from the heightfield. No Direct3D, no framework.
//   1. Every texel's four weights add up to exactly 255 (the shader relies on it: nothing darkens, nothing is left over).
//   2. The rules do what lesson 12 says: low flat ground is sand, mid flat ground is grass, high flat ground is snow,
//      a steep face is rock.
//   3. Baking only the block SplatCellsForSamples names gives the same bytes as baking everything, and no texel outside
//      that block ever changes: the sculpting path uploads only that block, so it must be exact.
//   4. The terrain's transform matters (heights are world heights), and the result is deterministic.
#include "TerrainSplat.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static std::uint32_t g_rng = 99;
static float Random01() { g_rng = g_rng * 1664525u + 1013904223u; return static_cast<float>(g_rng >> 8) / 16777216.0f; }
static int RandomInt(int lo, int hi) { return lo + static_cast<int>(Random01() * static_cast<float>(hi - lo + 1)) % (hi - lo + 1); }

static Heightfield MakeField(int cellsX, int cellsZ, float size, float heightScale, float (*shape)(int, int))
{
    std::vector<float> samples;
    for (int z = 0; z <= cellsZ; ++z)
        for (int x = 0; x <= cellsX; ++x) samples.push_back(shape(x, z));
    Heightfield field;
    field.Reset({ cellsX, cellsZ, size, size, heightScale }, samples);
    return field;
}

static float Hills(int x, int z) { return 0.5f + 0.35f * std::sin(x * 0.21f) * std::cos(z * 0.17f); }
static float Low(int, int) { return 0.02f; }     // world height 0.02 * 100 = 2   (below the shore at 5)
static float Mid(int, int) { return 0.15f; }     // 15: between shore (5..7.5) and snow (28)
static float High(int, int) { return 0.60f; }    // 60: above snow
static float Ramp(int x, int) { return 0.15f + 0.02f * x; }   // 2 world units of rise per cell of 1 unit: far past the slope limit

static SplatRules CleanRules()
{
    SplatRules rules;
    rules.breakupAmount = 0.0f;   // no noise: the rules alone
    return rules;
}

static const std::uint8_t* Texel(const SplatMap& map, int x, int z) { return &map.rgba[(static_cast<size_t>(z) * map.width + x) * kSplatChannels]; }

static void TestWeightsAddUp()
{
    const Heightfield field = MakeField(64, 48, 64.0f, 100.0f, Hills);
    SplatMap map;
    SplatPlacement place;
    SplatRules rules;   // WITH noise
    BakeSplatAll(field, rules, place, map);
    CHECK(map.width == 64 && map.height == 48);
    bool allExact = true;
    for (int z = 0; z < map.height; ++z)
        for (int x = 0; x < map.width; ++x) {
            const std::uint8_t* t = Texel(map, x, z);
            if (t[0] + t[1] + t[2] + t[3] != 255) allExact = false;
        }
    CHECK(allExact);
}

static void TestRules()
{
    SplatPlacement place;
    SplatMap map;

    BakeSplatAll(MakeField(32, 32, 32.0f, 100.0f, Low), CleanRules(), place, map);
    CHECK(Texel(map, 10, 10)[2] > 240);   // sand

    BakeSplatAll(MakeField(32, 32, 32.0f, 100.0f, Mid), CleanRules(), place, map);
    CHECK(Texel(map, 10, 10)[0] > 240);   // grass

    BakeSplatAll(MakeField(32, 32, 32.0f, 100.0f, High), CleanRules(), place, map);
    CHECK(Texel(map, 10, 10)[3] > 240);   // snow

    BakeSplatAll(MakeField(32, 32, 32.0f, 100.0f, Ramp), CleanRules(), place, map);
    CHECK(Texel(map, 10, 10)[1] > 200);   // rock
}

static void TestTransform()
{
    const Heightfield field = MakeField(32, 32, 32.0f, 100.0f, Mid);   // local height 15
    SplatMap map;
    SplatPlacement place;
    BakeSplatAll(field, CleanRules(), place, map);
    CHECK(Texel(map, 5, 5)[0] > 240);          // grass at 15

    place.originY = 50.0f;                      // the terrain is lifted by 50: now at 65, above the snow line
    BakeSplatAll(field, CleanRules(), place, map);
    CHECK(Texel(map, 5, 5)[3] > 240);

    place.originY = 0.0f;
    place.scaleY = 0.1f;                        // squashed to a tenth: 1.5 world units, below the shore
    BakeSplatAll(field, CleanRules(), place, map);
    CHECK(Texel(map, 5, 5)[2] > 240);
}

static void TestDeterministic()
{
    const Heightfield field = MakeField(40, 40, 40.0f, 80.0f, Hills);
    SplatMap a, b;
    SplatPlacement place;
    SplatRules rules;
    BakeSplatAll(field, rules, place, a);
    BakeSplatAll(field, rules, place, b);
    CHECK(a.rgba == b.rgba);
    rules.seed = 5;
    BakeSplatAll(field, rules, place, b);
    CHECK(a.rgba != b.rgba);   // another seed, another border pattern
}

// The sculpting path: edit samples, bake only the named block, compare with a bake of everything.
static void TestPartialBakeIsExact()
{
    const int cellsX = 90, cellsZ = 70;
    SplatPlacement place;
    place.originY = -2.0f;
    SplatRules rules;
    for (int trial = 0; trial < 60; ++trial) {
        Heightfield field = MakeField(cellsX, cellsZ, 90.0f, 60.0f, Hills);
        SplatMap before;
        BakeSplatAll(field, rules, place, before);

        // a brush-sized edit somewhere (sometimes touching an edge of the grid)
        GridRect edited;
        const int ax = RandomInt(0, cellsX), az = RandomInt(0, cellsZ);
        edited.Include(ax, az);
        edited.Include((std::min)(cellsX, ax + RandomInt(0, 15)), (std::min)(cellsZ, az + RandomInt(0, 15)));
        for (int z = edited.z0; z <= edited.z1; ++z)
            for (int x = edited.x0; x <= edited.x1; ++x) field.SetSample(field.IndexOf(x, z), Random01());

        SplatMap full;
        BakeSplatAll(field, rules, place, full);

        const GridRect block = SplatCellsForSamples(edited, cellsX, cellsZ);
        SplatMap partial = before;
        BakeSplat(field, rules, place, block, partial);
        CHECK(partial.rgba == full.rgba);   // the block is enough...

        bool outsideUntouched = true;       // ...and it is not too big: nothing outside it changed in the full bake
        for (int z = 0; z < cellsZ; ++z)
            for (int x = 0; x < cellsX; ++x) {
                const bool inside = x >= block.x0 && x <= block.x1 && z >= block.z0 && z <= block.z1;
                if (!inside) for (int c = 0; c < kSplatChannels; ++c) if (Texel(before, x, z)[c] != Texel(full, x, z)[c]) outsideUntouched = false;
            }
        CHECK(outsideUntouched);
    }
}

static void TestBlockBounds()
{
    GridRect edited;
    edited.Include(0, 0);
    GridRect block = SplatCellsForSamples(edited, 64, 64);
    CHECK(block.x0 == 0 && block.z0 == 0 && block.x1 == 1 && block.z1 == 1);
    edited = GridRect();
    edited.Include(64, 64);   // the last SAMPLE; the last cell is 63
    block = SplatCellsForSamples(edited, 64, 64);
    CHECK(block.x1 == 63 && block.z1 == 63 && block.x0 == 62);
    CHECK(SplatCellsForSamples(GridRect(), 64, 64).IsEmpty());
}

static void TimeFullBake()
{
    const int sizes[] = { 512, 1024 };
    for (int cells : sizes) {
        const Heightfield field = MakeField(cells, cells, 1000.0f, 150.0f, Hills);
        SplatMap map;
        SplatPlacement place;
        SplatRules rules;
        const auto start = std::chrono::steady_clock::now();
        BakeSplatAll(field, rules, place, map);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  full bake %d x %d: %.1f ms\n", cells, cells, ms);
    }
}

int main()
{
    TestWeightsAddUp();
    TestRules();
    TestTransform();
    TestDeterministic();
    TestBlockBounds();
    TestPartialBakeIsExact();
    TimeFullBake();
    if (g_failures == 0) std::printf("terrain_splat_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
