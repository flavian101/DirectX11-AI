// Tests for the world's ground at ONE point (roadmap C1): src/WorldGround.{h,cpp} WorldGroundFunction. No Direct3D, no framework.
//   1. On a one-tile world it equals Terrain's own query (Heightfield::HeightAt / NormalAt over the tile's samples and apron).
//   2. It is continuous across tile borders, whether or not any tile is loaded: there is no tile in this test.
//   3. A bounded world has ground inside its edge and none beyond it.
//   4. A ray from above finds the surface where HeightAt says it is, agrees with a slow brute-force walk, and misses what it should.
#include "WorldGround.h"
#include "WorldTileHeights.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static const float kHeightScale = 120.0f;
static const FractalWorldSettings kHills = { 11, 6, 300.0f };

static WorldGridSettings SmallGrid()
{
    WorldGridSettings grid;
    grid.chunksPerTile = 2;       // 64 cells
    grid.metresPerCell = 4.0f;    // 256 m tiles
    return grid;
}

static unsigned g_seed = 777u;
static double Random01() { g_seed = g_seed * 1664525u + 1013904223u; return static_cast<double>((g_seed >> 8) & 0xFFFF) / 65535.0; }

static WorldGroundFunction MakeFunction(const FractalHeightSource& source, const WorldBounds& bounds)
{
    WorldGroundFunction f;
    f.source = &source; f.grid = SmallGrid(); f.bounds = bounds; f.heightScale = kHeightScale;
    return f;
}

static void TestMatchesTheTilesOwnQuery()
{
    const FractalHeightSource source(kHills);
    const WorldGridSettings grid = SmallGrid();
    const int cells = grid.TileCells();
    const float size = cells * grid.metresPerCell;
    {
        // The tile exactly as Terrain holds it: its samples, and the apron that lets a border vertex's normal read the neighbour.
        const TileHeights tile = GenerateTileHeights(source, grid, { 0, 0 });
        Heightfield field;
        field.Reset({ cells, cells, size, size, kHeightScale }, tile.samples);
        field.SetApron(tile.apron);
        const WorldGroundFunction ground = MakeFunction(source, WorldBounds{});

        double worstHeight = 0.0, worstNormal = 0.0;
        int compared = 0;
        for (int k = 0; k < 4000; ++k) {
            // Anywhere in the tile, and a good share of points right at its edges (where the apron matters).
            double x = Random01() * size, z = Random01() * size;
            if (k % 4 == 1) x = (k % 8 == 1) ? 0.0 : size;
            if (k % 4 == 2) z = (k % 8 == 2) ? 0.0 : size;
            float expected = 0.0f, actual = 0.0f;
            if (!field.HeightAt(static_cast<float>(x - size * 0.5), static_cast<float>(z - size * 0.5), expected)) continue;
            CHECK(ground.HeightAt(x, z, actual));
            worstHeight = (std::max)(worstHeight, static_cast<double>(std::fabs(expected - actual)));
            HfVec3 expectedNormal;
            WorldVec3 actualNormal;
            if (field.NormalAt(static_cast<float>(x - size * 0.5), static_cast<float>(z - size * 0.5), expectedNormal)) {
                CHECK(ground.NormalAt(x, z, actualNormal));
                worstNormal = (std::max)(worstNormal, static_cast<double>(std::fabs(expectedNormal.x - actualNormal.x) + std::fabs(expectedNormal.y - actualNormal.y) + std::fabs(expectedNormal.z - actualNormal.z)));
            }
            ++compared;
        }
        std::printf("one-tile world (%s): worst height difference %.6f m, worst normal difference %.6f over %d points\n", "ground", worstHeight, worstNormal, compared);
        CHECK(compared > 3500);
        CHECK(worstHeight < 1.0e-3);
        CHECK(worstNormal < 1.0e-4);
    }
}

static void TestContinuousAcrossTiles()
{
    const FractalHeightSource source(kHills);
    const WorldGroundFunction ground = MakeFunction(source, WorldBounds{});   // an endless world: no tile is loaded anywhere
    double worstJump = 0.0;
    const double border[] = { -512.0, -256.0, 0.0, 256.0, 512.0 };
    for (double b : border) {
        for (double along = -300.0; along < 700.0; along += 3.7) {
            float west = 0.0f, east = 0.0f, north = 0.0f, south = 0.0f;
            CHECK(ground.HeightAt(b - 1e-4, along, west) && ground.HeightAt(b + 1e-4, along, east));
            CHECK(ground.HeightAt(along, b - 1e-4, north) && ground.HeightAt(along, b + 1e-4, south));
            worstJump = (std::max)(worstJump, static_cast<double>((std::max)(std::fabs(west - east), std::fabs(north - south))));
        }
    }
    std::printf("across tile borders: worst jump %.6f m\n", worstJump);
    CHECK(worstJump < 0.05);   // a slope of at most a few degrees across 0.2 mm
}

static void TestBoundedEdge()
{
    const FractalHeightSource source(kHills);
    WorldBounds bounds;
    bounds.bounded = true; bounds.minX = 0; bounds.minZ = 0; bounds.maxX = 1; bounds.maxZ = 1;   // [0, 512] x [0, 512]
    const WorldGroundFunction ground = MakeFunction(source, bounds);
    float h = 0.0f;
    WorldVec3 n;
    CHECK(ground.HeightAt(0.0, 0.0, h) && ground.HeightAt(512.0, 512.0, h) && ground.HeightAt(256.0, 256.0, h));   // the edge belongs to the world
    CHECK(ground.NormalAt(256.0, 256.0, n));
    CHECK(!ground.HeightAt(-0.01, 100.0, h) && !ground.HeightAt(512.01, 100.0, h));
    CHECK(!ground.HeightAt(100.0, -0.01, h) && !ground.HeightAt(100.0, 512.01, h));
    CHECK(!ground.NormalAt(-5.0, 100.0, n));
}

static void TestRaycast()
{
    const FractalHeightSource source(kHills);
    const WorldGroundFunction ground = MakeFunction(source, WorldBounds{});
    int hits = 0;
    double worstSurface = 0.0, worstBrute = 0.0;
    for (int k = 0; k < 300; ++k) {
        const WorldVec3 origin{ static_cast<float>(Random01() * 600.0 - 100.0), static_cast<float>(200.0 + Random01() * 100.0), static_cast<float>(Random01() * 600.0 - 100.0) };
        WorldVec3 dir{ static_cast<float>(Random01() - 0.5), static_cast<float>(-0.15 - Random01() * 0.85), static_cast<float>(Random01() - 0.5) };
        float distance = 0.0f;
        if (!ground.Raycast(origin, dir, 2000.0f, distance)) continue;
        ++hits;
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        const double px = origin.x + dir.x / len * distance, py = origin.y + dir.y / len * distance, pz = origin.z + dir.z / len * distance;
        float surface = 0.0f;
        CHECK(ground.HeightAt(px, pz, surface));
        worstSurface = (std::max)(worstSurface, std::fabs(py - surface));   // the point it reports lies ON the ground
        // A slow, fine walk finds the same first crossing.
        double brute = -1.0;
        for (double t = 0.0; t < 2000.0; t += 0.05) {
            float g = 0.0f;
            ground.HeightAt(origin.x + dir.x / len * t, origin.z + dir.z / len * t, g);
            if (origin.y + dir.y / len * t < g) { brute = t; break; }
        }
        CHECK(brute > 0.0);
        worstBrute = (std::max)(worstBrute, std::fabs(brute - distance));
    }
    std::printf("raycast: %d hits; the hit lies %.6f m off the ground at worst; %.3f m from a 5 cm brute-force walk at worst\n", hits, worstSurface, worstBrute);
    CHECK(hits > 200);
    CHECK(worstSurface < 1.0e-3);
    CHECK(worstBrute < 0.3);

    float distance = 0.0f;
    CHECK(!ground.Raycast({ 100, 250, 100 }, { 0, 1, 0 }, 2000.0f, distance));         // straight up: nothing
    CHECK(!ground.Raycast({ 100, -500, 100 }, { 1, 0, 0 }, 2000.0f, distance));        // starts under the ground
    CHECK(!ground.Raycast({ 100, 250, 100 }, { 0, 0, 0 }, 2000.0f, distance));         // no direction
    CHECK(!ground.Raycast({ 100, 250, 100 }, { 1, -0.01f, 0 }, 100.0f, distance));     // too short to reach it
}

static void TestRayLeavesABoundedWorld()
{
    const FractalHeightSource source(kHills);
    WorldBounds bounds;
    bounds.bounded = true; bounds.minX = 0; bounds.minZ = 0; bounds.maxX = 1; bounds.maxZ = 1;
    const WorldGroundFunction ground = MakeFunction(source, bounds);
    float distance = 0.0f;
    CHECK(!ground.Raycast({ 600, 300, 100 }, { 1, -0.05f, 0 }, 3000.0f, distance));    // flies away from the world: never meets it
    CHECK(ground.Raycast({ 300, 300, 100 }, { 0, -1, 0 }, 3000.0f, distance));         // straight down onto it
    float h = 0.0f;
    CHECK(ground.HeightAt(300.0, 100.0, h) && std::fabs((300.0f - distance) - h) < 1.0e-2f);
}

int main()
{
    TestMatchesTheTilesOwnQuery();
    TestContinuousAcrossTiles();
    TestBoundedEdge();
    TestRaycast();
    TestRayLeavesABoundedWorld();
    if (g_failures == 0) std::printf("All world height tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
