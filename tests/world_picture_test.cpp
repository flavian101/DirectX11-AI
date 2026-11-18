// Tests for src/WorldPicture.{h,cpp} and the bounded part of src/WorldGrid.{h,cpp} (phase 1 of docs/terrain-world-roadmap.md).
// No Direct3D, no framework.
//   1. Sampling a picture: corners, bilinear middle, values outside the picture repeat the edge.
//   2. A picture placed in the world: the corner, an offset, the far side, and a position 500 km from the origin.
//   3. Tiles made from a picture share their edges bit for bit, at every level of detail (the world's core promise).
//   4. WorldBounds: Contains, Covering (an exact fit does not pull in a second tile), Normalize.
//   5. A BOUNDED world streams only its own tiles; an unbounded one behaves as before.
//   6. REBUILDING THE OLD TERRAIN: one tile as wide as the old terrain gives the old terrain's heights, sample for sample.
//   7. Reading a 16-bit PNG back keeps its precision; a missing file is an error, not a crash.
#include "WorldPicture.h"
#include "Png16.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(double a, double b, double tolerance = 1e-5) { return std::fabs(a - b) <= tolerance; }

// A small test picture whose value at pixel (x, z) is easy to predict: x + 10 * z, over 100.
static std::shared_ptr<HeightImage> MakeImage(int width, int height)
{
    auto image = std::make_shared<HeightImage>();
    image->width = width;
    image->height = height;
    image->values.resize(static_cast<size_t>(width) * height);
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) image->values[static_cast<size_t>(z) * width + x] = static_cast<float>(x + 10 * z) / 100.0f;
    }
    return image;
}

static void TestSampling()
{
    const auto image = MakeImage(4, 4);
    CHECK(Near(SampleHeightImage(*image, 0.0f, 0.0f), 0.00f));
    CHECK(Near(SampleHeightImage(*image, 1.0f, 0.0f), 0.03f));
    CHECK(Near(SampleHeightImage(*image, 0.0f, 1.0f), 0.30f));
    CHECK(Near(SampleHeightImage(*image, 1.0f, 1.0f), 0.33f));
    CHECK(Near(SampleHeightImage(*image, 0.5f, 0.5f), 0.165f));   // the middle of a plane is the average of its corners
    CHECK(Near(SampleHeightImage(*image, -3.0f, -3.0f), 0.0f));   // outside: the edge pixel again
    CHECK(Near(SampleHeightImage(*image, 9.0f, 9.0f), 0.33f));
    HeightImage empty;
    CHECK(SampleHeightImage(empty, 0.5f, 0.5f) == 0.0f);          // no picture: flat, not a crash
}

static void TestPlacement()
{
    const auto image = MakeImage(4, 4);
    // 300 m wide, 300 m deep, its corner at (1000, -500).
    const PictureHeightSource source(image, 1000.0, -500.0, 300.0, 300.0);
    CHECK(Near(source.Height01(1000.0, -500.0), 0.00f));
    CHECK(Near(source.Height01(1300.0, -500.0), 0.03f));
    CHECK(Near(source.Height01(1000.0, -200.0), 0.30f));
    CHECK(Near(source.Height01(1300.0, -200.0), 0.33f));
    CHECK(Near(source.Height01(1150.0, -350.0), 0.165f));
    CHECK(Near(source.Height01(0.0, 0.0), source.Height01(1000.0, -200.0)));   // left of the picture, below its top edge: the edge pixel repeats along X (x clamps to 0)
    CHECK(Near(source.Height01(-1.0e6, -1.0e6), 0.00f));    // far outside, up-left: the corner pixel
    CHECK(Near(source.Height01(+1.0e6, +1.0e6), 0.33f));    // far outside, down-right: the opposite corner pixel
    // 500 km from the origin: the picture is placed there, and double arithmetic keeps the position exact.
    const PictureHeightSource far(image, 500000.0, 500000.0, 300.0, 300.0);
    CHECK(Near(far.Height01(500150.0, 500150.0), 0.165f));
    CHECK(Near(PictureDepth(100.0, 200, 100), 50.0));
    CHECK(Near(PictureDepth(100.0, 100, 300), 300.0));
    const PictureHeightSource none(nullptr, 0, 0, 1, 1);
    CHECK(none.Height01(0.5, 0.5) == 0.0f);
}

static void TestTilesShareEdges()
{
    const auto image = MakeImage(64, 64);
    // Fill the picture with something less regular than a plane, so a mistake cannot hide.
    for (size_t i = 0; i < image->values.size(); ++i) image->values[i] = std::fmod(static_cast<float>(i) * 0.6180339f, 1.0f);
    WorldGridSettings grid;
    grid.chunksPerTile = 2;       // 64 cells
    grid.metresPerCell = 2.0f;    // 128 m tiles
    const PictureHeightSource source(image, 0.0, 0.0, 256.0, 256.0);
    const int cells = grid.TileCells();
    const std::vector<float> left = GenerateTileSamples(source, grid, { 0, 0 });
    const std::vector<float> right = GenerateTileSamples(source, grid, { 1, 0 });
    const std::vector<float> below = GenerateTileSamples(source, grid, { 0, 1 });
    bool eastWest = true, northSouth = true;
    for (int i = 0; i <= cells; ++i) {
        if (left[static_cast<size_t>(i) * (cells + 1) + cells] != right[static_cast<size_t>(i) * (cells + 1) + 0]) eastWest = false;
        if (left[static_cast<size_t>(cells) * (cells + 1) + i] != below[static_cast<size_t>(0) * (cells + 1) + i]) northSouth = false;
    }
    CHECK(eastWest);
    CHECK(northSouth);

    // Level of detail: a coarse tile's samples are a subset of the fine tile's, so they meet along a border at the same points.
    const WorldGridSettings coarseGrid = grid.ForLod(1);
    const std::vector<float> coarse = GenerateTileSamples(source, coarseGrid, { 0, 0 });
    const int coarseCells = coarseGrid.TileCells();
    bool subset = true;
    for (int z = 0; z <= coarseCells; ++z) {
        for (int x = 0; x <= coarseCells; ++x) {
            if (coarse[static_cast<size_t>(z) * (coarseCells + 1) + x] != left[static_cast<size_t>(z * 2) * (cells + 1) + x * 2]) subset = false;
        }
    }
    CHECK(subset);
}

static void TestBounds()
{
    WorldGridSettings grid;   // 1024 m tiles
    WorldBounds none;
    CHECK(none.Contains({ 1000, -1000 }));   // not bounded: everything
    CHECK(none.TilesAcrossX() == 0);

    // 240 m picture, 240 m tile: exactly one tile, even though 240 / 240 is only "1.0" up to floating-point noise.
    WorldGridSettings small;
    small.chunksPerTile = 8;
    small.metresPerCell = 240.0f / 256.0f;
    const WorldBounds one = WorldBounds::Covering(small, 0.0, 0.0, 240.0, 240.0);
    CHECK(one.bounded && one.minX == 0 && one.maxX == 0 && one.minZ == 0 && one.maxZ == 0);

    const WorldBounds two = WorldBounds::Covering(grid, 0.0, 0.0, 1024.5, 100.0);   // half a metre over: a second tile
    CHECK(two.minX == 0 && two.maxX == 1 && two.minZ == 0 && two.maxZ == 0);
    CHECK(two.TilesAcrossX() == 2 && two.TilesAcrossZ() == 1);

    const WorldBounds negative = WorldBounds::Covering(grid, -1.0, -2048.0, 10.0, -1.0);   // starts in tile -1
    CHECK(negative.minX == -1 && negative.maxX == 0 && negative.minZ == -2 && negative.maxZ == -1);
    CHECK(negative.Contains({ -1, -2 }) && negative.Contains({ 0, -1 }));
    CHECK(!negative.Contains({ 1, -1 }) && !negative.Contains({ 0, 0 }) && !negative.Contains({ -2, -2 }));

    WorldBounds swapped;
    swapped.bounded = true; swapped.minX = 5; swapped.maxX = -2; swapped.minZ = 1; swapped.maxZ = 1;
    swapped.Normalize();
    CHECK(swapped.minX == -2 && swapped.maxX == 5 && swapped.TilesAcrossX() == 8 && swapped.TilesAcrossZ() == 1);
    WorldBounds huge;
    huge.bounded = true; huge.minX = 0; huge.maxX = 100000; huge.minZ = 0; huge.maxZ = 0;
    huge.Normalize();
    CHECK(huge.TilesAcrossX() == WorldBounds::kMaxTilesAcross);
}

static bool Contains(const std::vector<TileRequest>& list, TileCoord tile)
{
    return std::any_of(list.begin(), list.end(), [&](const TileRequest& r) { return r.coord == tile; });
}

static void TestBoundedStreaming()
{
    WorldGridSettings grid;   // 1024 m tiles
    WorldBounds bounds;
    bounds.bounded = true; bounds.minX = 0; bounds.maxX = 1; bounds.minZ = 0; bounds.maxZ = 1;   // 2 x 2 tiles

    // Standing in the middle of the 2 x 2 world, seeing 5 km: only the four tiles of the world, however far we can see.
    const StreamingPlan plan = PlanTileStreaming(grid, 1024.0, 1024.0, 5000.0, 256, {}, bounds);
    CHECK(plan.toLoad.size() == 4);
    CHECK(Contains(plan.toLoad, { 0, 0 }) && Contains(plan.toLoad, { 1, 0 }) && Contains(plan.toLoad, { 0, 1 }) && Contains(plan.toLoad, { 1, 1 }));
    CHECK(!Contains(plan.toLoad, { -1, 0 }) && !Contains(plan.toLoad, { 2, 2 }));

    // Without bounds the same view wants many more.
    const StreamingPlan open = PlanTileStreaming(grid, 1024.0, 1024.0, 5000.0, 256, {});
    CHECK(open.toLoad.size() > 4);
    // ... and a default-constructed WorldBounds is exactly "no bounds".
    const StreamingPlan same = PlanTileStreaming(grid, 1024.0, 1024.0, 5000.0, 256, {}, WorldBounds{});
    CHECK(same.toLoad.size() == open.toLoad.size());

    // A tile that is loaded but lies outside the bounds (the bounds just changed) is unloaded, even though it is in view.
    const std::vector<LoadedTile> loaded = { { { 0, 0 }, 0 }, { { 5, 5 }, 0 } };
    const StreamingPlan drop = PlanTileStreaming(grid, 1024.0, 1024.0, 8000.0, 256, loaded, bounds);
    CHECK(drop.toUnload.size() == 1 && drop.toUnload[0] == TileCoord({ 5, 5 }));
    CHECK(!Contains(drop.toLoad, { 5, 5 }));   // and the tile outside the bounds is not asked for again
}

// The old terrain's height at grid sample (x, z): the picture sampled at u = x / cells, v = z / cells (Terrain.cpp, GenerateHeights).
static void TestRebuildOldTerrain()
{
    const auto image = MakeImage(64, 64);
    for (size_t i = 0; i < image->values.size(); ++i) image->values[i] = std::fmod(static_cast<float>(i) * 0.7548777f, 1.0f);

    const float width = 240.0f;
    const int maxCells = 512;
    const WorldGridSettings grid = WorldGridForTerrain(width, maxCells);
    CHECK(grid.chunksPerTile == 16 && grid.TileCells() == 512);
    CHECK(Near(grid.TileSize(), 240.0, 1e-3));             // one tile as wide as the terrain
    CHECK(Near(grid.metresPerCell, 240.0 / 512.0, 1e-6));  // with the same cell size

    const PictureHeightSource source(image, 0.0, 0.0, width, PictureDepth(width, image->width, image->height));
    const std::vector<float> tile = GenerateTileSamples(source, grid, { 0, 0 });
    const int cells = grid.TileCells();
    double worst = 0.0;
    for (int z = 0; z <= cells; ++z) {
        for (int x = 0; x <= cells; ++x) {
            const float old = SampleHeightImage(*image, static_cast<float>(x) / cells, static_cast<float>(z) / cells);
            worst = (std::max)(worst, static_cast<double>(std::fabs(old - tile[static_cast<size_t>(z) * (cells + 1) + x])));
        }
    }
    CHECK(worst < 1e-4);   // float vs double rounding only

    const WorldBounds bounds = WorldBounds::Covering(grid, 0.0, 0.0, width, PictureDepth(width, image->width, image->height));
    CHECK(bounds.TilesAcrossX() == 1 && bounds.TilesAcrossZ() == 1);

    // A terrain wider than the finest cell allows becomes a tile that is bigger than the picture, never a failure.
    const WorldGridSettings tiny = WorldGridForTerrain(40.0f, 1024);
    CHECK(Near(tiny.metresPerCell, WorldGridSettings::kMetresPerCellMin) && Near(tiny.TileSize(), 256.0, 1e-3));
    const WorldGridSettings huge = WorldGridForTerrain(1000.0f, 32);
    CHECK(huge.chunksPerTile == 1 && Near(huge.TileSize(), 32.0 * huge.metresPerCell, 1e-6));
    // A mesh resolution that is not a whole number of chunks is rounded to the nearest one.
    CHECK(WorldGridForTerrain(240.0f, 500).chunksPerTile == 16);
}

static void TestLoadImage()
{
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "world_picture_test_height16.png").string();
    std::vector<std::uint16_t> values(4 * 3);
    for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<std::uint16_t>(i * 5000 + 7);
    std::string error;
    CHECK(WriteGrayscale16Png(path, 4, 3, values, error));
    HeightImage image;
    CHECK(LoadHeightImage(path, image, error));
    CHECK(image.width == 4 && image.height == 3 && image.values.size() == 12);
    bool exact = image.values.size() == values.size();
    for (size_t i = 0; exact && i < values.size(); ++i) exact = std::fabs(image.values[i] - values[i] / 65535.0f) < 1e-6f;
    CHECK(exact);   // 16 bits survive: not rounded to 256 steps
    std::error_code ignore;
    fs::remove(path, ignore);

    HeightImage missing;
    CHECK(!LoadHeightImage((fs::temp_directory_path() / "no_such_picture_12345.png").string(), missing, error));
    CHECK(!error.empty());
}

int main()
{
    TestSampling();
    TestPlacement();
    TestTilesShareEdges();
    TestBounds();
    TestBoundedStreaming();
    TestRebuildOldTerrain();
    TestLoadImage();
    if (g_failures == 0) std::printf("All world picture tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
