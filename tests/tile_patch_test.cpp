// Tests for patching a loaded tile in place (roadmap T2): src/WorldGrid.{h,cpp} TileSamplesAffectedBy and src/WorldRivers.{h,cpp}
// GenerateTileBlock. No Direct3D, no framework.
//   1. Which samples of a tile a changed lattice rectangle can reach (the block, grown by one, apron included, clipped).
//   2. A block of a tile equals the same samples of the whole tile (with and without a river dug in).
//   3. THE ACCEPTANCE: after every one of 200 random brush strokes, a tile that was only PATCHED (just the affected samples were
//      rewritten) equals a tile built from scratch, sample for sample, apron included. This is what Terrain::PatchTile relies on.
#include "WorldEdits.h"
#include "WorldRivers.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static WorldGridSettings SmallGrid()
{
    WorldGridSettings grid;
    grid.chunksPerTile = 2;       // 64 cells
    grid.metresPerCell = 4.0f;    // 256 m tiles
    return grid;
}

static void TestAffectedRect()
{
    const WorldGridSettings grid = SmallGrid();
    const int cells = grid.TileCells();
    CHECK(cells == 64);

    TileSampleRect r = TileSamplesAffectedBy(grid, { 0, 0 }, 10, 20, 12, 21);
    CHECK(r.x0 == 9 && r.x1 == 13 && r.z0 == 19 && r.z1 == 22);      // grown by one

    r = TileSamplesAffectedBy(grid, { 0, 0 }, 63, 30, 64, 31);       // reaches the last column and the apron beyond it
    CHECK(r.x0 == 62 && r.x1 == cells + 1 && r.z0 == 29 && r.z1 == 32);

    r = TileSamplesAffectedBy(grid, { 0, 0 }, 66, 30, 66, 30);       // lattice 66: only the apron (65) is reached
    CHECK(r.x0 == 65 && r.x1 == 65);
    CHECK(TileSamplesAffectedBy(grid, { 0, 0 }, 67, 30, 67, 30).IsEmpty());   // too far

    // Tile (1, 0) starts at lattice 64. Lattice 63 is its west apron (local -1); lattice 60 is out of reach.
    r = TileSamplesAffectedBy(grid, { 1, 0 }, 63, 10, 63, 10);
    CHECK(r.x0 == -1 && r.x1 == 0);
    CHECK(TileSamplesAffectedBy(grid, { 1, 0 }, 60, 10, 60, 10).IsEmpty());

    // A tile on the other side of the world, and a big rectangle covering everything: clipped to the apron.
    r = TileSamplesAffectedBy(grid, { -3, 2 }, -1000, -1000, 1000, 1000);
    CHECK(r.x0 == -1 && r.x1 == cells + 1 && r.z0 == -1 && r.z1 == cells + 1);
    CHECK(TileSamplesAffectedBy(grid, { 0, 0 }, 5, 5, 4, 4).IsEmpty());       // an empty change reaches nothing
}

// A river across tile (0, 0): digs a valley through the hills.
static WorldRiver MakeRiver()
{
    WorldRiver river;
    river.id = 3;
    const float points[][2] = { { -40, 60 }, { 90, 120 }, { 170, 110 }, { 230, 190 }, { 300, 260 } };
    for (const auto& p : points) {
        RiverPath::Sample s;
        s.x = p[0]; s.z = p[1]; s.halfWidth = 10.0f; s.level = 0.0f;
        river.path.push_back(s);
    }
    river.carve.level = 0.36f; river.carve.depth = 0.03f; river.carve.bankWidth = 20.0f; river.carve.bankHeight = 0.04f;
    return river;
}

static const FractalWorldSettings kHills = { 7, 6, 400.0f };

// The value GenerateTileHeights holds for tile-local sample (x, z), apron included. The four corners of the apron are not stored.
static bool ValueAt(const TileHeights& h, int cells, int x, int z, float& out)
{
    const bool insideX = x >= 0 && x <= cells, insideZ = z >= 0 && z <= cells;
    if (insideX && insideZ) { out = h.samples[static_cast<size_t>(z) * (cells + 1) + x]; return true; }
    if (x == -1 && insideZ) { out = h.apron.west[z]; return true; }
    if (x == cells + 1 && insideZ) { out = h.apron.east[z]; return true; }
    if (z == -1 && insideX) { out = h.apron.north[x]; return true; }
    if (z == cells + 1 && insideX) { out = h.apron.south[x]; return true; }
    return false;
}

static void StoreAt(TileHeights& h, int cells, int x, int z, float value)
{
    float ignore = 0.0f;
    if (!ValueAt(h, cells, x, z, ignore)) return;
    const bool insideX = x >= 0 && x <= cells, insideZ = z >= 0 && z <= cells;
    if (insideX && insideZ) h.samples[static_cast<size_t>(z) * (cells + 1) + x] = value;
    else if (x == -1) h.apron.west[z] = value;
    else if (x == cells + 1) h.apron.east[z] = value;
    else if (z == -1) h.apron.north[x] = value;
    else h.apron.south[x] = value;
}

static bool SameTile(const TileHeights& a, const TileHeights& b)
{
    return a.samples == b.samples && a.apron.west == b.apron.west && a.apron.east == b.apron.east && a.apron.north == b.apron.north && a.apron.south == b.apron.south;
}

static void TestBlockEqualsTileSlice()
{
    const WorldGridSettings grid = SmallGrid();
    const int cells = grid.TileCells();
    const FractalHeightSource source(kHills);
    const std::vector<WorldRiver> rivers = { MakeRiver() };
    const TileSampleRect rects[] = { { 0, 0, cells, cells }, { 10, 12, 20, 40 }, { -1, -1, cells + 1, cells + 1 }, { -1, 30, 3, 33 }, { cells - 2, -1, cells + 1, 5 } };
    for (const std::vector<WorldRiver>* withRivers : { static_cast<const std::vector<WorldRiver>*>(nullptr), &rivers }) {
        const TileHeights whole = GenerateTileHeights(source, grid, { 0, 0 }, withRivers);
        bool same = true;
        int compared = 0;
        for (const TileSampleRect& rect : rects) {
            const std::vector<float> block = GenerateTileBlock(source, grid, { 0, 0 }, withRivers, rect);
            same = same && block.size() == static_cast<size_t>(rect.Width()) * rect.Height();
            for (int z = rect.z0; z <= rect.z1; ++z) {
                for (int x = rect.x0; x <= rect.x1; ++x) {
                    float expected = 0.0f;
                    if (!ValueAt(whole, cells, x, z, expected)) continue;   // an apron corner
                    same = same && block[static_cast<size_t>(z - rect.z0) * rect.Width() + (x - rect.x0)] == expected;   // bit for bit
                    ++compared;
                }
            }
        }
        CHECK(same);
        CHECK(compared > 4000);
    }
    CHECK(GenerateTileBlock(source, grid, { 0, 0 }, nullptr, TileSampleRect{}).empty());
}

static unsigned g_seed = 12345u;
static float Random01() { g_seed = g_seed * 1664525u + 1013904223u; return static_cast<float>((g_seed >> 8) & 0xFFFF) / 65535.0f; }

static void RunStrokes(const std::vector<WorldRiver>* rivers, const char* label)
{
    const WorldGridSettings grid = SmallGrid();
    const int cells = grid.TileCells();
    const double mpc = grid.metresPerCell;
    const auto ground = std::make_shared<const FractalHeightSource>(kHills);
    auto edits = std::make_shared<WorldEdits>(cells);
    const EditedHeightSource edited(ground, edits, mpc);
    const TileCoord tile{ 0, 0 };

    TileHeights live = GenerateTileHeights(edited, grid, tile, rivers);   // the tile as loaded; from now on it is only PATCHED
    const TileHeights start = live;
    int touched = 0, identical = 0;
    const BrushMode modes[] = { BrushMode::Raise, BrushMode::Lower, BrushMode::Smooth, BrushMode::Flatten };
    for (int stroke = 0; stroke < 200; ++stroke) {
        BrushSettings brush;
        brush.mode = modes[stroke % 4];
        brush.radius = 6.0f + 40.0f * Random01();
        brush.strength = 0.3f + 0.7f * Random01();
        brush.targetHeight = Random01();
        // Positions reach well past the tile on every side: strokes next to the tile change only its apron.
        const double x = -60.0 + 380.0 * Random01(), z = -60.0 + 380.0 * Random01();
        const LatticeRect changed = ApplyWorldHeightBrush(*edits, *ground, mpc, brush, x, z, 0.1f, nullptr);
        const TileSampleRect rect = TileSamplesAffectedBy(grid, tile, changed.minX, changed.minZ, changed.maxX, changed.maxZ);
        if (!rect.IsEmpty()) {
            ++touched;
            const std::vector<float> block = GenerateTileBlock(edited, grid, tile, rivers, rect);
            for (int j = 0; j < rect.Height(); ++j)
                for (int i = 0; i < rect.Width(); ++i) StoreAt(live, cells, rect.x0 + i, rect.z0 + j, block[static_cast<size_t>(j) * rect.Width() + i]);
        }
        const TileHeights fresh = GenerateTileHeights(edited, grid, tile, rivers);
        if (SameTile(live, fresh)) ++identical;
    }
    std::printf("%s: %d of 200 strokes reached the tile; patched tile equals rebuilt tile after %d of 200\n", label, touched, identical);
    CHECK(identical == 200);
    CHECK(touched > 60);                  // the test is not vacuous
    CHECK(!SameTile(live, start));        // and the ground really changed
}

static void TestPatchEqualsRebuild()
{
    RunStrokes(nullptr, "no river");
    const std::vector<WorldRiver> rivers = { MakeRiver() };
    RunStrokes(&rivers, "with a river");
}

int main()
{
    TestAffectedRect();
    TestBlockEqualsTileSlice();
    TestPatchEqualsRebuild();
    if (g_failures == 0) std::printf("All tile patch tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
