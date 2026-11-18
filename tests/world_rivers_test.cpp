// Tests for src/WorldRivers.{h,cpp}: rivers as part of the world's height function. No Direct3D, no framework.
//   1. It agrees with the carve a single terrain already uses (CarveRiver on a Heightfield).
//   2. A river crosses tile borders without a seam: neighbouring tiles hold the same samples (bit for bit), and so do the
//      same tile at different levels of detail.
//   3. Lighting is seamless across the river too (the apron is carved with it).
//   4. Nothing changes far from the river; with no river near, a tile is exactly the plain tile.
#include "WorldRivers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static float At(const std::vector<float>& samples, int cells, int x, int z) { return samples[static_cast<size_t>(z) * (cells + 1) + x]; }

// A river that runs across tiles (0,0), (1,0) and (1,1) of the default grid (1024 m tiles).
static WorldRiver MakeRiver()
{
    WorldRiver river;
    river.id = 7;
    const float points[][2] = { { 120, 260 }, { 500, 380 }, { 900, 330 }, { 1300, 500 }, { 1650, 900 }, { 1700, 1500 } };
    for (const auto& p : points) {
        RiverPath::Sample s;
        s.x = p[0];
        s.z = p[1];
        s.halfWidth = 12.0f;
        s.level = 0.0f;
        river.path.push_back(s);
    }
    river.carve.level = 0.36f;      // sample values: the hills sit around 0.5, so this digs a valley through them
    river.carve.depth = 0.03f;
    river.carve.bankWidth = 25.0f;  // metres
    river.carve.bankHeight = 0.04f;
    return river;
}

static const FractalWorldSettings kHills = { 7, 6, 400.0f };

static void TestAgreesWithTheSingleTerrainCarve()
{
    const WorldGridSettings grid;
    const FractalHeightSource source(kHills);
    const TileCoord tile{ 0, 0 };
    const WorldRiver river = MakeRiver();
    const std::vector<WorldRiver> rivers = { river };

    const TileHeights world = GenerateTileHeights(source, grid, tile, &rivers);

    // The same river dug into a Heightfield the way a single Terrain does it: local space, origin at the tile's centre.
    Heightfield field;
    field.Reset(TileLayout(grid, 100.0f), GenerateTileSamples(source, grid, tile));
    std::vector<RiverPath::Sample> local = river.path;
    for (RiverPath::Sample& s : local) { s.x -= 512.0f; s.z -= 512.0f; }
    CarveRiver(field, local, river.carve, nullptr);

    const int cells = grid.TileCells();
    int changed = 0, different = 0;
    float worst = 0.0f;
    const std::vector<float> plain = GenerateTileSamples(source, grid, tile);
    for (size_t i = 0; i < plain.size(); ++i) {
        if (world.samples[i] != plain[i]) ++changed;
        const float diff = std::fabs(world.samples[i] - field.Samples()[i]);
        worst = std::max(worst, diff);
        if (diff > 1e-5f) ++different;
    }
    std::printf("tile (0,0): the river changes %d of %zu samples; worst difference from CarveRiver %.2e, %d above 1e-5\n", changed, plain.size(), worst, different);
    CHECK(changed > 500);                 // it really dug
    CHECK(worst < 1e-3f);
    CHECK(different < 20);                // a few samples on the very edge of the bank may differ by float rounding
    (void)cells;
}

static void TestSeamsAndLevels()
{
    const WorldGridSettings grid;
    const FractalHeightSource source(kHills);
    const std::vector<WorldRiver> rivers = { MakeRiver() };
    const int cells = grid.TileCells();

    const TileHeights a = GenerateTileHeights(source, grid, { 0, 0 }, &rivers);
    const TileHeights east = GenerateTileHeights(source, grid, { 1, 0 }, &rivers);
    const TileHeights south = GenerateTileHeights(source, grid, { 0, 1 }, &rivers);
    const TileHeights both = GenerateTileHeights(source, grid, { 1, 1 }, &rivers);
    bool same = true;
    for (int i = 0; i <= cells; ++i) {
        same = same && At(a.samples, cells, cells, i) == At(east.samples, cells, 0, i);      // across the border at x = 1024
        same = same && At(east.samples, cells, i, cells) == At(both.samples, cells, i, 0);   // across the border at z = 1024
        same = same && a.apron.east[i] == At(east.samples, cells, 1, i);                      // the apron holds the neighbour's carved samples
        same = same && east.apron.west[i] == At(a.samples, cells, cells - 1, i);
        same = same && a.apron.south[i] == At(south.samples, cells, i, 1);
    }
    CHECK(same);

    // The same tile at a coarser level meets the fine one at the same points.
    for (int level = 1; level <= 3; ++level) {
        const WorldGridSettings lod = grid.ForLod(level);
        const TileHeights coarse = GenerateTileHeights(source, lod, { 1, 0 }, &rivers);
        const int coarseCells = lod.TileCells(), step = 1 << level;
        bool subset = true;
        for (int z = 0; z <= coarseCells; ++z)
            for (int x = 0; x <= coarseCells; ++x) subset = subset && At(coarse.samples, coarseCells, x, z) == At(east.samples, cells, x * step, z * step);
        CHECK(subset);
    }

    // Lighting across the seams: the normals of two tiles agree along their shared edge.
    auto make = [&](TileCoord tile, const TileHeights& heights) {
        Heightfield field;
        field.Reset(TileLayout(grid, 100.0f), heights.samples);
        field.SetApron(heights.apron);
        (void)tile;
        return field;
    };
    const Heightfield fa = make({ 0, 0 }, a), fe = make({ 1, 0 }, east);
    float worst = 0.0f;
    for (int i = 0; i <= cells; ++i) {
        const HfVec3 l = fa.VertexNormal(cells, i), r = fe.VertexNormal(0, i);
        worst = std::max({ worst, std::fabs(l.x - r.x), std::fabs(l.y - r.y), std::fabs(l.z - r.z) });
    }
    CHECK(worst == 0.0f);
}

static void TestFarFromTheRiver()
{
    const WorldGridSettings grid;
    const FractalHeightSource source(kHills);
    const std::vector<WorldRiver> rivers = { MakeRiver() };

    // A tile nowhere near the river is exactly the plain tile (and costs nothing extra).
    const TileHeights far = GenerateTileHeights(source, grid, { 6, -5 }, &rivers);
    CHECK(far.samples == GenerateTileSamples(source, grid, { 6, -5 }));
    const HeightfieldApron plainApron = GenerateTileApron(source, grid, { 6, -5 });
    CHECK(far.apron.west == plainApron.west && far.apron.north == plainApron.north);
    CHECK(GenerateTileHeights(source, grid, { 0, 0 }, nullptr).samples == GenerateTileSamples(source, grid, { 0, 0 }));

    // In a tile the river does cross, samples farther than its reach from the path are untouched.
    const WorldRiver& river = rivers[0];
    const TileHeights near = GenerateTileHeights(source, grid, { 0, 0 }, &rivers);
    const std::vector<float> plain = GenerateTileSamples(source, grid, { 0, 0 });
    const int cells = grid.TileCells();
    bool untouchedFar = true, allInRange = true;
    for (int z = 0; z <= cells; ++z) {
        for (int x = 0; x <= cells; ++x) {
            const double px = WorldSamplePosition(grid, 0, x), pz = WorldSamplePosition(grid, 0, z);
            double nearest = 1e30;
            for (size_t i = 0; i + 1 < river.path.size(); ++i) {
                const RiverPath::Sample& s = river.path[i];
                const RiverPath::Sample& t = river.path[i + 1];
                const double ex = t.x - s.x, ez = t.z - s.z, l2 = ex * ex + ez * ez;
                const double u = std::clamp(((px - s.x) * ex + (pz - s.z) * ez) / l2, 0.0, 1.0);
                nearest = std::min(nearest, std::hypot(px - (s.x + ex * u), pz - (s.z + ez * u)));
            }
            const float h = At(near.samples, cells, x, z);
            if (nearest > 12.0 + 25.0 + 1.0) untouchedFar = untouchedFar && h == At(plain, cells, x, z);
            allInRange = allInRange && h >= 0.0f && h <= 1.0f;
        }
    }
    CHECK(untouchedFar);
    CHECK(allInRange);

    // Bounds and which tiles a river touches.
    double minX, maxX, minZ, maxZ;
    CHECK(river.Bounds(minX, maxX, minZ, maxZ) && minX < 120.0 && maxX > 1700.0 && minZ < 260.0 && maxZ > 1500.0);
    WorldRiver empty;
    CHECK(!empty.Bounds(minX, maxX, minZ, maxZ));
    CHECK(RiverTouchesTile(grid, { 0, 0 }, river) && RiverTouchesTile(grid, { 1, 0 }, river) && RiverTouchesTile(grid, { 1, 1 }, river));
    CHECK(!RiverTouchesTile(grid, { 3, 3 }, river) && !RiverTouchesTile(grid, { -2, 0 }, river) && !RiverTouchesTile(grid, { 0, 0 }, empty));
}

int main()
{
    TestAgreesWithTheSingleTerrainCarve();
    TestSeamsAndLevels();
    TestFarFromTheRiver();
    if (g_failures == 0) std::printf("world_rivers_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
