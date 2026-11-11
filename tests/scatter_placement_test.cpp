// Tests for src/ScatterPlacement.{h,cpp}: where trees and rocks stand. No Direct3D, no framework.
//   1. Determinism and NO SEAMS: the instances of a whole area are exactly the union of the instances of its cells. This is
//      what lets a sculpt stroke regenerate one cell and leave the rest alone.
//   2. Spacing: no two instances are closer than minDistance (and with minDistance = 0 they can be: the thinning is real).
//   3. Density scales the count; clustering removes some of it.
//   4. The terrain layers drive it: a grass species stays on grass, a rock species on rock (painted weights included).
//   5. Slope and height limits are obeyed, measured with the same ground query the placement uses.
//   6. Rotation: upright when asked, leaning with the ground when asked (the quaternion turns "up" into the normal).
//   7. Sinking, the stretched/moved terrain, the grid helpers, and a timing for a 1 km terrain.
#include "ScatterPlacement.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static Heightfield MakeField(int cells, float size, float heightScale, float (*shape)(float, float))
{
    std::vector<float> samples;
    for (int z = 0; z <= cells; ++z)
        for (int x = 0; x <= cells; ++x) samples.push_back(shape(static_cast<float>(x) / cells, static_cast<float>(z) / cells));
    Heightfield field;
    field.Reset({ cells, cells, size, size, heightScale }, samples);
    return field;
}
static float Flat(float, float) { return 0.1f; }
static float Hills(float u, float v) { return 0.5f + 0.2f * std::sin(u * 9.0f) * std::cos(v * 7.0f); }
// Flat for u < 0.5, then a steep ramp (heightScale 100, size 200: 0.4 per 0.5 of u = 80 m over 100 m: ~38 degrees)
static float FlatThenSteep(float u, float) { return u < 0.5f ? 0.1f : 0.1f + (u - 0.5f) * 1.6f; }

static void FillSplat(SplatMap& map, int w, int h, std::uint8_t (*weight)(int x, int z, int channel))
{
    map.Resize(w, h);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < kSplatChannels; ++c) map.rgba[(static_cast<size_t>(z) * w + x) * kSplatChannels + c] = weight(x, z, c);
}
static std::uint8_t AllGrass(int, int, int c) { return c == 0 ? 255 : 0; }
// Left half grass, right half rock.
static std::uint8_t GrassLeftRockRight(int x, int, int c) { const bool left = x < 50; return c == 0 ? (left ? 255 : 0) : (c == 1 ? (left ? 0 : 255) : 0); }

static ScatterGround MakeGround(const Heightfield& field, const SplatMap* splat, float px = 0, float py = 0, float pz = 0, float sx = 1, float sy = 1, float sz = 1)
{
    ScatterGround g;
    g.field = &field; g.splat = splat;
    g.posX = px; g.posY = py; g.posZ = pz; g.scaleX = sx; g.scaleY = sy; g.scaleZ = sz;
    return g;
}

static bool Less(const ScatterInstance& a, const ScatterInstance& b) { return a.x != b.x ? a.x < b.x : a.z < b.z; }
static bool SameInstance(const ScatterInstance& a, const ScatterInstance& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

static void Rotate(const ScatterInstance& i, const float v[3], float out[3])
{
    const float qx = i.qx, qy = i.qy, qz = i.qz, qw = i.qw;
    const float cx = qy * v[2] - qz * v[1] + qw * v[0], cy = qz * v[0] - qx * v[2] + qw * v[1], cz = qx * v[1] - qy * v[0] + qw * v[2];
    out[0] = v[0] + 2.0f * (qy * cz - qz * cy);
    out[1] = v[1] + 2.0f * (qz * cx - qx * cz);
    out[2] = v[2] + 2.0f * (qx * cy - qy * cx);
}

static void TestDeterministicAndSeamFree()
{
    const Heightfield field = MakeField(100, 200.0f, 60.0f, Hills);
    SplatMap splat; FillSplat(splat, 100, 100, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat);
    ScatterRules rules; rules.spacing = 5.0f; rules.minDistance = 3.0f; rules.density = 0.8f; rules.alignToNormal = 0.5f; rules.clusterStrength = 0.5f;
    const ScatterRect extent = ScatterGroundExtent(ground);

    std::vector<ScatterInstance> whole, again;
    GenerateScatter(ground, rules, extent, whole);
    GenerateScatter(ground, rules, extent, again);
    CHECK(whole.size() > 200);
    CHECK(whole.size() == again.size());
    bool identical = whole.size() == again.size();
    for (size_t i = 0; identical && i < whole.size(); ++i) identical = SameInstance(whole[i], again[i]);
    CHECK(identical);

    // The same area cell by cell, with a cell size that does NOT divide the spacing.
    const ScatterGrid grid = MakeScatterGrid(extent, 31.0f);
    std::vector<ScatterInstance> tiled;
    for (int cz = 0; cz < grid.cellsZ; ++cz)
        for (int cx = 0; cx < grid.cellsX; ++cx) GenerateScatter(ground, rules, ScatterCellRect(grid, cx, cz), tiled);
    CHECK(tiled.size() == whole.size());
    std::sort(whole.begin(), whole.end(), Less);
    std::sort(tiled.begin(), tiled.end(), Less);
    bool same = whole.size() == tiled.size();
    for (size_t i = 0; same && i < whole.size(); ++i) same = SameInstance(whole[i], tiled[i]);
    CHECK(same);

    // A different seed is a different forest.
    ScatterRules other = rules; other.seed = 2;
    std::vector<ScatterInstance> different;
    GenerateScatter(ground, other, extent, different);
    std::sort(different.begin(), different.end(), Less);
    CHECK(different.size() != whole.size() || !SameInstance(different[0], whole[0]));
}

static size_t CountCloserThan(const std::vector<ScatterInstance>& v, float distance)
{
    size_t close = 0;
    for (size_t a = 0; a < v.size(); ++a)
        for (size_t b = a + 1; b < v.size(); ++b) {
            const float dx = v[a].x - v[b].x, dz = v[a].z - v[b].z;
            if (dx * dx + dz * dz < distance * distance) ++close;
        }
    return close;
}

static void TestSpacing()
{
    const Heightfield field = MakeField(100, 200.0f, 60.0f, Hills);
    SplatMap splat; FillSplat(splat, 100, 100, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat);
    const ScatterRect extent = ScatterGroundExtent(ground);

    ScatterRules rules; rules.spacing = 5.0f; rules.minDistance = 4.0f;
    std::vector<ScatterInstance> spaced;
    GenerateScatter(ground, rules, extent, spaced);
    CHECK(spaced.size() > 300);
    CHECK(CountCloserThan(spaced, 4.0f - 1e-3f) == 0);

    rules.minDistance = 0.0f;   // no thinning at all
    std::vector<ScatterInstance> crowded;
    GenerateScatter(ground, rules, extent, crowded);
    CHECK(crowded.size() > spaced.size());
    CHECK(CountCloserThan(crowded, 2.0f) > 0);

    // Even at the largest allowed minDistance (= spacing) the guarantee holds across the whole terrain.
    rules.minDistance = 99.0f;   // clamped to the spacing
    std::vector<ScatterInstance> wide;
    GenerateScatter(ground, rules, extent, wide);
    CHECK(CountCloserThan(wide, 5.0f - 1e-3f) == 0);
}

static void TestDensityAndClusters()
{
    const Heightfield field = MakeField(100, 200.0f, 10.0f, Flat);
    SplatMap splat; FillSplat(splat, 100, 100, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat);
    const ScatterRect extent = ScatterGroundExtent(ground);

    ScatterRules rules; rules.spacing = 5.0f; rules.minDistance = 0.0f;   // 40 x 40 = 1600 candidates, none thinned
    auto count = [&](const ScatterRules& r) { std::vector<ScatterInstance> v; return static_cast<int>(GenerateScatter(ground, r, extent, v)); };
    rules.density = 1.0f; const int full = count(rules);
    rules.density = 0.5f; const int half = count(rules);
    rules.density = 0.25f; const int quarter = count(rules);
    rules.density = 0.0f; const int none = count(rules);
    CHECK(full >= 1550 && full <= 1600);          // a candidate can fall just off the terrain's edge, nothing else removes one
    CHECK(half > full * 0.42 && half < full * 0.58);
    CHECK(quarter > full * 0.17 && quarter < full * 0.33);
    CHECK(none == 0);

    rules.density = 1.0f; rules.clusterStrength = 1.0f; rules.clusterScale = 30.0f;
    const int clustered = count(rules);
    CHECK(clustered < full * 0.85 && clustered > full * 0.10);   // clearings, but still forest
    rules.clusterStrength = 0.0f;
    CHECK(count(rules) == full);

    // The layer weights are a vote: 50/50 grass and sand with a species that only likes grass halves the density.
    SplatMap mixed; FillSplat(mixed, 100, 100, [](int, int, int c) -> std::uint8_t { return c == 0 ? 128 : (c == 2 ? 127 : 0); });
    const ScatterGround mixedGround = MakeGround(field, &mixed);
    std::vector<ScatterInstance> v;
    GenerateScatter(mixedGround, rules, extent, v);
    CHECK(v.size() > full * 0.42 && v.size() < full * 0.58);
}

static void TestLayersDriveIt()
{
    const Heightfield field = MakeField(100, 200.0f, 10.0f, Flat);
    SplatMap splat; FillSplat(splat, 100, 100, GrassLeftRockRight);   // x < 0 grass, x > 0 rock (the splat is 2 m per texel)
    const ScatterGround ground = MakeGround(field, &splat);
    const ScatterRect extent = ScatterGroundExtent(ground);

    ScatterRules trees; trees.spacing = 4.0f; trees.minDistance = 2.0f; trees.layerDensity[0] = 1.0f; trees.layerDensity[1] = 0.0f;
    ScatterRules stones = trees; stones.layerDensity[0] = 0.0f; stones.layerDensity[1] = 1.0f;
    std::vector<ScatterInstance> a, b;
    GenerateScatter(ground, trees, extent, a);
    GenerateScatter(ground, stones, extent, b);
    CHECK(a.size() > 200 && b.size() > 200);
    bool treesOnGrass = true, stonesOnRock = true;
    for (const ScatterInstance& i : a) if (i.x > 1.0f + 1e-3f) treesOnGrass = false;     // bilinear blending spreads the border over +-1 m
    for (const ScatterInstance& i : b) if (i.x < -1.0f - 1e-3f) stonesOnRock = false;
    CHECK(treesOnGrass);
    CHECK(stonesOnRock);

    // No splat at all (a terrain that has not baked yet) counts as grass.
    const ScatterGround bare = MakeGround(field, nullptr);
    std::vector<ScatterInstance> c;
    GenerateScatter(bare, trees, extent, c);
    CHECK(c.size() > 400);
}

static void TestSlopeAndHeight()
{
    const Heightfield field = MakeField(200, 200.0f, 100.0f, FlatThenSteep);   // 1 m cells
    SplatMap splat; FillSplat(splat, 200, 200, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat);
    const ScatterRect extent = ScatterGroundExtent(ground);

    ScatterRules rules; rules.spacing = 3.0f; rules.minDistance = 1.5f; rules.maxSlope = 20.0f;
    std::vector<ScatterInstance> flatOnly;
    GenerateScatter(ground, rules, extent, flatOnly);
    CHECK(flatOnly.size() > 500);
    bool allFlat = true; float mostEast = -1000.0f;
    for (const ScatterInstance& i : flatOnly) {
        ScatterSample s; SampleScatterGround(ground, i.x, i.z, s);
        if (std::acos(s.normal[1]) * 57.29578f > 20.0f + 0.01f) allFlat = false;
        mostEast = (std::max)(mostEast, i.x);
    }
    CHECK(allFlat);
    CHECK(mostEast < 3.0f);   // the steep part starts at x = 0

    // Rocks: only where it IS steep.
    rules.minSlope = 25.0f; rules.maxSlope = 90.0f;
    std::vector<ScatterInstance> steepOnly;
    GenerateScatter(ground, rules, extent, steepOnly);
    CHECK(steepOnly.size() > 100);
    bool allSteep = true;
    for (const ScatterInstance& i : steepOnly) {
        ScatterSample s; SampleScatterGround(ground, i.x, i.z, s);
        if (std::acos(s.normal[1]) * 57.29578f < 25.0f - 0.01f) allSteep = false;
    }
    CHECK(allSteep);

    // Height limits, in world Y.
    rules.minSlope = 0.0f; rules.maxSlope = 90.0f; rules.minHeight = 30.0f; rules.maxHeight = 60.0f;
    std::vector<ScatterInstance> band;
    GenerateScatter(ground, rules, extent, band);
    CHECK(band.size() > 100);
    bool inBand = true;
    for (const ScatterInstance& i : band) if (i.y < 30.0f - 1e-3f || i.y > 60.0f + 1e-3f) inBand = false;
    CHECK(inBand);
}

static void TestRotationAndSink()
{
    const Heightfield field = MakeField(100, 200.0f, 60.0f, Hills);
    SplatMap splat; FillSplat(splat, 100, 100, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat);
    const ScatterRect extent = ScatterGroundExtent(ground);
    const float up[3] = { 0.0f, 1.0f, 0.0f };

    ScatterRules rules; rules.spacing = 6.0f; rules.minDistance = 3.0f; rules.maxSlope = 60.0f;
    rules.tilt = 0.0f; rules.alignToNormal = 0.0f;
    std::vector<ScatterInstance> upright;
    GenerateScatter(ground, rules, extent, upright);
    CHECK(upright.size() > 100);
    bool allUp = true, unit = true; float minYaw = 10.0f, maxYaw = -10.0f;
    for (const ScatterInstance& i : upright) {
        float r[3]; Rotate(i, up, r);
        if (std::fabs(r[0]) > 1e-4f || std::fabs(r[1] - 1.0f) > 1e-4f || std::fabs(r[2]) > 1e-4f) allUp = false;
        if (std::fabs(i.qx * i.qx + i.qy * i.qy + i.qz * i.qz + i.qw * i.qw - 1.0f) > 1e-4f) unit = false;
        const float side[3] = { 1.0f, 0.0f, 0.0f }; float s[3]; Rotate(i, side, s);
        const float yaw = std::atan2(s[2], s[0]); minYaw = (std::min)(minYaw, yaw); maxYaw = (std::max)(maxYaw, yaw);
    }
    CHECK(allUp);
    CHECK(unit);
    CHECK(minYaw < -2.5f && maxYaw > 2.5f);   // the yaw really is spread around the whole circle

    rules.alignToNormal = 1.0f;
    std::vector<ScatterInstance> leaning;
    GenerateScatter(ground, rules, extent, leaning);
    bool followsGround = true; float leanest = 0.0f;
    for (const ScatterInstance& i : leaning) {
        float r[3]; Rotate(i, up, r);
        ScatterSample s; SampleScatterGround(ground, i.x, i.z, s);
        if (std::fabs(r[0] - s.normal[0]) > 1e-3f || std::fabs(r[1] - s.normal[1]) > 1e-3f || std::fabs(r[2] - s.normal[2]) > 1e-3f) followsGround = false;
        leanest = (std::max)(leanest, 1.0f - r[1]);
    }
    CHECK(followsGround);
    CHECK(leanest > 0.01f);   // the hills really are sloped, so some instances lean

    // A random lean of 10 degrees: never more than ~10 degrees from upright (ground is not followed).
    rules.alignToNormal = 0.0f; rules.tilt = 10.0f;
    std::vector<ScatterInstance> swaying;
    GenerateScatter(ground, rules, extent, swaying);
    float worst = 0.0f; bool anyLean = false;
    for (const ScatterInstance& i : swaying) {
        float r[3]; Rotate(i, up, r);
        const float angle = std::acos((std::min)(1.0f, r[1])) * 57.29578f;
        worst = (std::max)(worst, angle); if (angle > 1.0f) anyLean = true;
    }
    CHECK(worst < 10.0f * 1.5f);   // the square-shaped random lean reaches up to atan(sqrt(2) * tan(10)) ~ 14 degrees
    CHECK(anyLean);

    // Sinking: y = ground - sink * modelHeight * scale.
    rules.tilt = 0.0f; rules.sink = 0.5f; rules.modelHeight = 4.0f; rules.scaleMin = 1.0f; rules.scaleMax = 2.0f;
    std::vector<ScatterInstance> buried;
    GenerateScatter(ground, rules, extent, buried);
    bool sunk = !buried.empty(), scaled = true;
    for (const ScatterInstance& i : buried) {
        ScatterSample s; SampleScatterGround(ground, i.x, i.z, s);
        if (std::fabs(i.y - (s.y - 0.5f * 4.0f * i.scale)) > 1e-3f) sunk = false;
        if (i.scale < 1.0f || i.scale > 2.0f) scaled = false;
    }
    CHECK(sunk);
    CHECK(scaled);
}

static void TestMovedAndStretchedGround()
{
    const Heightfield field = MakeField(100, 200.0f, 60.0f, Hills);
    SplatMap splat; FillSplat(splat, 100, 100, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat, 500.0f, -20.0f, -300.0f, 2.0f, 3.0f, 0.5f);   // 400 x 100 m, 3x taller
    const ScatterRect extent = ScatterGroundExtent(ground);
    CHECK(std::fabs(extent.minX - 300.0f) < 1e-3f && std::fabs(extent.maxX - 700.0f) < 1e-3f);
    CHECK(std::fabs(extent.minZ - (-350.0f)) < 1e-3f && std::fabs(extent.maxZ - (-250.0f)) < 1e-3f);

    ScatterRules rules; rules.spacing = 5.0f; rules.minDistance = 2.0f; rules.maxSlope = 90.0f;
    std::vector<ScatterInstance> v;
    GenerateScatter(ground, rules, { -1e6f, -1e6f, 1e6f, 1e6f }, v);   // a huge area: clipped to the terrain
    CHECK(v.size() > 200);
    bool inside = true, onGround = true;
    for (const ScatterInstance& i : v) {
        if (i.x < extent.minX || i.x >= extent.maxX || i.z < extent.minZ || i.z >= extent.maxZ) inside = false;
        ScatterSample s; SampleScatterGround(ground, i.x, i.z, s);
        if (std::fabs(i.y - s.y) > 1e-3f) onGround = false;
    }
    CHECK(inside);
    CHECK(onGround);

    std::vector<ScatterInstance> none;
    CHECK(GenerateScatter(ground, rules, { 0.0f, 0.0f, 100.0f, 100.0f }, none) == 0);   // nowhere near the terrain
    CHECK(none.empty());
    ScatterSample unused;
    CHECK(!SampleScatterGround(ground, 0.0f, 0.0f, unused));

    // A terrain stretched 3x taller has steeper normals: the same ground fails a slope limit it passed unstretched.
    const ScatterGround flatScale = MakeGround(field, &splat, 500.0f, -20.0f, -300.0f, 2.0f, 1.0f, 0.5f);
    ScatterRules gentle; gentle.spacing = 5.0f; gentle.minDistance = 2.0f; gentle.maxSlope = 20.0f;
    std::vector<ScatterInstance> onGentle, onTall;
    GenerateScatter(flatScale, gentle, extent, onGentle);
    GenerateScatter(ground, gentle, extent, onTall);
    CHECK(onGentle.size() > onTall.size());
}

static void TestGridHelpers()
{
    const ScatterRect extent{ -100.0f, -100.0f, 100.0f, 100.0f };
    const ScatterGrid grid = MakeScatterGrid(extent, 32.0f);
    CHECK(grid.cellsX == 7 && grid.cellsZ == 7);   // 200 / 32 = 6.25 -> 7
    CHECK(grid.CellCount() == 49 && grid.IndexOf(2, 3) == 3 * 7 + 2);
    const ScatterRect first = ScatterCellRect(grid, 0, 0), last = ScatterCellRect(grid, 6, 6);
    CHECK(first.minX == -100.0f && first.maxX == -68.0f && last.maxX >= 100.0f);

    int x0, z0, x1, z1;
    CHECK(ScatterCellsTouching(grid, { -90.0f, -90.0f, -80.0f, -80.0f }, x0, z0, x1, z1) && x0 == 0 && z0 == 0 && x1 == 0 && z1 == 0);
    CHECK(ScatterCellsTouching(grid, { -70.0f, -70.0f, -60.0f, -60.0f }, x0, z0, x1, z1) && x0 == 0 && x1 == 1 && z0 == 0 && z1 == 1);   // straddles x = -68
    CHECK(ScatterCellsTouching(grid, { -1e6f, -1e6f, 1e6f, 1e6f }, x0, z0, x1, z1) && x0 == 0 && z0 == 0 && x1 == 6 && z1 == 6);
    CHECK(!ScatterCellsTouching(grid, { 500.0f, 500.0f, 600.0f, 600.0f }, x0, z0, x1, z1));
    CHECK(!ScatterCellsTouching(grid, { 0.0f, 0.0f, 0.0f, 10.0f }, x0, z0, x1, z1));   // empty rectangle
}

static void TestClampAndTiming()
{
    ScatterRules wild;
    wild.spacing = 0.0f; wild.density = 7.0f; wild.minDistance = 99.0f; wild.maxSlope = -5.0f; wild.scaleMin = 0.0f; wild.scaleMax = -1.0f;
    ClampScatterRules(wild);
    CHECK(wild.spacing >= 0.25f && wild.density == 1.0f && wild.minDistance <= wild.spacing);
    CHECK(wild.maxSlope >= wild.minSlope && wild.scaleMin > 0.0f && wild.scaleMax >= wild.scaleMin);

    // A 1 km x 1 km terrain with 1 m cells and a forest at 4 m spacing: 62,500 candidates. It must be quick, because a
    // sculpt stroke regenerates cells on the fly.
    const Heightfield field = MakeField(1000, 1000.0f, 120.0f, Hills);
    SplatMap splat; FillSplat(splat, 1000, 1000, AllGrass);
    const ScatterGround ground = MakeGround(field, &splat);
    ScatterRules rules; rules.spacing = 4.0f; rules.minDistance = 2.5f; rules.maxSlope = 50.0f;
    std::vector<ScatterInstance> all;
    const auto t0 = std::chrono::steady_clock::now();
    GenerateScatter(ground, rules, ScatterGroundExtent(ground), all);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("1 km^2 at 4 m spacing: %zu instances in %.1f ms\n", all.size(), ms);
    CHECK(all.size() > 20000);
    CHECK(ms < 3000.0);
}

int main()
{
    TestDeterministicAndSeamFree();
    TestSpacing();
    TestDensityAndClusters();
    TestLayersDriveIt();
    TestSlopeAndHeight();
    TestRotationAndSink();
    TestMovedAndStretchedGround();
    TestGridHelpers();
    TestClampAndTiming();
    if (g_failures == 0) std::printf("ScatterPlacementTests: all passed\n");
    else std::printf("ScatterPlacementTests: %d FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
