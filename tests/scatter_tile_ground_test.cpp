// Tests for the scatter's ground over a world (roadmap S12): src/ScatterTileGround.{h,cpp} with the ScatterGroundSource interface.
// No Direct3D, no framework. The tiles are made from a world's ground function, like the ones the engine streams.
//   1. A point is answered by the tile that contains it, and the ground is continuous across tile borders.
//   2. IsReady says "not yet" exactly while a tile that touches the area is missing (and ignores tiles beyond a bounded world's edge).
//   3. DETERMINISM: the same trees whatever order the tiles arrived in, and after a tile leaves and comes back.
//   4. NO SEAM: placing a whole area gives exactly what placing it cell by cell gives, across the borders of tiles.
//   5. Why IsReady exists: a cell next to a missing tile would lose trees that appear when the tile arrives.
#include "ScatterPlacement.h"
#include "ScatterTileGround.h"
#include "TerrainSplat.h"
#include "WorldRivers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static const float kHeightScale = 90.0f;
static const FractalWorldSettings kHills = { 21, 6, 260.0f };

static WorldGridSettings SmallGrid()
{
    WorldGridSettings grid;
    grid.chunksPerTile = 2;       // 64 cells
    grid.metresPerCell = 4.0f;    // 256 m tiles
    return grid;
}

// One tile exactly as the engine holds it: heights with their apron, and the layer weights baked with the world's continuous noise.
struct TileData {
    Heightfield field;
    SplatMap splat;
    ScatterGround ground;
};

static std::unique_ptr<TileData> MakeTile(const FractalHeightSource& source, int tx, int tz)
{
    const WorldGridSettings grid = SmallGrid();
    const int cells = grid.TileCells();
    const float size = cells * grid.metresPerCell;
    auto tile = std::make_unique<TileData>();
    const TileHeights heights = GenerateTileHeights(source, grid, { tx, tz }, nullptr);
    tile->field.Reset({ cells, cells, size, size, kHeightScale }, heights.samples);
    tile->field.SetApron(heights.apron);
    tile->splat.Resize(cells, cells);
    SplatRules rules;
    rules.shoreHeight = 40.0f; rules.snowHeight = 70.0f;   // all four layers occur in this world
    SplatPlacement placement;
    placement.noiseOffsetX = (tx + 0.5f) * size; placement.noiseOffsetZ = (tz + 0.5f) * size;   // the tile's centre: every tile reads the same world noise
    BakeSplatAll(tile->field, rules, placement, tile->splat);
    tile->ground.field = &tile->field;
    tile->ground.splat = &tile->splat;
    tile->ground.posX = (tx + 0.5f) * size; tile->ground.posZ = (tz + 0.5f) * size; tile->ground.posY = 0.0f;
    return tile;
}

struct World {
    FractalHeightSource source{ kHills };
    std::vector<std::unique_ptr<TileData>> tiles;   // row by row, 3 x 3: tiles 0..2 in X and Z
    World() { for (int tz = 0; tz < 3; ++tz) for (int tx = 0; tx < 3; ++tx) tiles.push_back(MakeTile(source, tx, tz)); }
    ScatterTileGround Compose(const std::vector<int>& order, int skip = -1, bool bounded = true) const
    {
        ScatterTileGround ground(256.0f);
        ground.SetBounds(bounded, 0, 0, 2, 2);
        for (int index : order) if (index != skip) ground.AddTile(index % 3, index / 3, tiles[static_cast<size_t>(index)]->ground);
        return ground;
    }
};

static ScatterRules Rules()
{
    ScatterRules r;
    r.spacing = 8.0f; r.minDistance = 5.0f; r.density = 1.0f;
    for (float& d : r.layerDensity) d = 1.0f;
    r.maxSlope = 70.0f; r.seed = 5u;
    return r;
}

static bool Less(const ScatterInstance& a, const ScatterInstance& b) { return a.x != b.x ? a.x < b.x : a.z < b.z; }
static bool SameInstances(std::vector<ScatterInstance> a, std::vector<ScatterInstance> b)
{
    if (a.size() != b.size()) return false;
    std::sort(a.begin(), a.end(), Less); std::sort(b.begin(), b.end(), Less);
    for (size_t i = 0; i < a.size(); ++i) if (std::memcmp(&a[i], &b[i], sizeof(ScatterInstance)) != 0) return false;
    return true;
}

static std::vector<ScatterInstance> Generate(const ScatterGroundSource& ground, const ScatterRect& area)
{
    std::vector<ScatterInstance> out;
    GenerateScatter(ground, Rules(), area, out, nullptr);
    return out;
}

static void TestSampleAndContinuity(const World& world)
{
    const std::vector<int> all = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    const ScatterTileGround ground = world.Compose(all);
    CHECK(ground.IsValid() && ground.TileCount() == 9);
    const ScatterRect extent = ground.Extent();
    CHECK(extent.minX == 0.0f && extent.minZ == 0.0f && extent.maxX == 768.0f && extent.maxZ == 768.0f);

    // A point is answered by the tile that holds it.
    bool same = true;
    for (float x = 3.0f; x < 765.0f; x += 29.3f) for (float z = 5.0f; z < 765.0f; z += 31.7f) {
        ScatterSample a, b;
        const int index = static_cast<int>(std::floor(z / 256.0f)) * 3 + static_cast<int>(std::floor(x / 256.0f));
        const bool inTile = world.tiles[static_cast<size_t>(index)]->ground.Sample(x, z, b);
        same = same && ground.Sample(x, z, a) == inTile && a.y == b.y && a.weights[0] == b.weights[0] && a.normal[1] == b.normal[1];
    }
    CHECK(same);

    // Continuous across the borders between tiles: the ground does not jump where one tile ends and the next begins.
    float worstHeight = 0.0f, worstNormal = 0.0f;
    for (float border : { 256.0f, 512.0f }) for (float along = 10.0f; along < 760.0f; along += 7.3f) {
        ScatterSample west, east, north, south;
        if (ground.Sample(border - 0.01f, along, west) && ground.Sample(border + 0.01f, along, east)) {
            worstHeight = (std::max)(worstHeight, std::fabs(west.y - east.y));
            worstNormal = (std::max)(worstNormal, std::fabs(west.normal[1] - east.normal[1]));
        }
        if (ground.Sample(along, border - 0.01f, north) && ground.Sample(along, border + 0.01f, south)) {
            worstHeight = (std::max)(worstHeight, std::fabs(north.y - south.y));
            worstNormal = (std::max)(worstNormal, std::fabs(north.normal[1] - south.normal[1]));
        }
    }
    std::printf("across tile borders: ground jumps %.4f m, normal %.5f at worst\n", worstHeight, worstNormal);
    CHECK(worstHeight < 0.05f && worstNormal < 0.002f);
}

static void TestReadiness(const World& world)
{
    const std::vector<int> all = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    const ScatterTileGround full = world.Compose(all);
    CHECK(full.IsReady({ 0, 0, 768, 768 }));
    const ScatterTileGround noMiddle = world.Compose(all, 4);   // tile (1, 1) is not loaded
    CHECK(!noMiddle.IsReady({ 200, 200, 300, 300 }));            // touches it
    CHECK(noMiddle.IsReady({ 10, 10, 100, 100 }));               // does not
    CHECK(noMiddle.IsReady({ 0, 0, 256, 256 }));                 // half-open: the far edge x = 256 is not in the area
    CHECK(noMiddle.IsReady({ 0, 0, 256.5f, 256 }));              // touches tile (1, 0), which is loaded
    CHECK(!noMiddle.IsReady({ 0, 0, 256.5f, 256.5f }));          // reaches tile (1, 1)
    CHECK(noMiddle.IsReady({ 512, 0, 768, 256 }));
    // Beyond the edge of a BOUNDED world there is nothing to wait for; in an endless world a missing tile is a missing tile.
    CHECK(full.IsReady({ 700, 700, 900, 900 }));                 // tiles 3.. are outside the bounds
    const ScatterTileGround endless = world.Compose(all, -1, false);
    CHECK(!endless.IsReady({ 700, 700, 900, 900 }));
    CHECK(endless.IsReady({ 700, 700, 768, 768 }));
    CHECK(full.IsReady({ 5, 5, 5, 5 }));                         // an empty area is always ready
}

static void TestDeterminism(const World& world)
{
    const ScatterRect area{ 0.0f, 0.0f, 768.0f, 768.0f };
    const ScatterTileGround forward = world.Compose({ 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    const ScatterTileGround backward = world.Compose({ 8, 7, 6, 5, 4, 3, 2, 1, 0 });
    const ScatterTileGround shuffled = world.Compose({ 4, 0, 8, 2, 6, 1, 7, 3, 5 });
    const std::vector<ScatterInstance> a = Generate(forward, area);
    std::printf("a 768 m square of 9 tiles: %zu trees\n", a.size());
    CHECK(a.size() > 300);
    CHECK(SameInstances(a, Generate(backward, area)) && SameInstances(a, Generate(shuffled, area)));   // the order tiles arrive in does not matter

    // A tile leaves and comes back (streaming out and in): the same trees.
    ScatterTileGround leaving = world.Compose({ 0, 1, 2, 3, 4, 5, 6, 7, 8 }, 4);
    const std::vector<ScatterInstance> without = Generate(leaving, area);
    CHECK(without.size() < a.size());
    leaving.AddTile(1, 1, world.tiles[4]->ground);
    CHECK(SameInstances(a, Generate(leaving, area)));
}

static void TestNoSeamAcrossTiles(const World& world)
{
    const ScatterTileGround ground = world.Compose({ 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    const std::vector<ScatterInstance> whole = Generate(ground, { 0.0f, 0.0f, 768.0f, 768.0f });
    // The same area in cells of 32 m, which do not line up with the tiles' 256 m borders in any special way, and cells of 100 m which cross them.
    for (float cell : { 32.0f, 100.0f }) {
        std::vector<ScatterInstance> pieces;
        for (float z = 0.0f; z < 768.0f; z += cell)
            for (float x = 0.0f; x < 768.0f; x += cell) {
                const std::vector<ScatterInstance> part = Generate(ground, { x, z, (std::min)(x + cell, 768.0f), (std::min)(z + cell, 768.0f) });
                pieces.insert(pieces.end(), part.begin(), part.end());
            }
        std::printf("cells of %.0f m: %zu trees together, %zu placed at once\n", cell, pieces.size(), whole.size());
        CHECK(SameInstances(whole, pieces));   // no tree twice, none missing, none moved
    }
}

static void TestWhyReadinessMatters(const World& world)
{
    // A cell deep inside tile (0,0), far from any border, gives the same trees whether or not the neighbours are loaded...
    const ScatterTileGround onlyOne = [&] { ScatterTileGround g(256.0f); g.SetBounds(true, 0, 0, 2, 2); g.AddTile(0, 0, world.tiles[0]->ground); return g; }();
    const ScatterTileGround all = world.Compose({ 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    const ScatterRect inside{ 96.0f, 96.0f, 128.0f, 128.0f };
    CHECK(onlyOne.IsReady({ 96.0f - 16.0f, 96.0f - 16.0f, 128.0f + 16.0f, 128.0f + 16.0f }));   // the cell and a margin of two spacings
    CHECK(SameInstances(Generate(onlyOne, inside), Generate(all, inside)));
    // ...but a cell on the border loses the trees of the missing side, which appear when it arrives: that is a pop, so it must wait.
    const ScatterRect onBorder{ 240.0f, 100.0f, 272.0f, 132.0f };
    const std::vector<ScatterInstance> early = Generate(onlyOne, onBorder), late = Generate(all, onBorder);
    std::printf("a cell on a tile border: %zu trees before the neighbour arrives, %zu after\n", early.size(), late.size());
    CHECK(early.size() < late.size());
    CHECK(!onlyOne.IsReady({ 240.0f - 16.0f, 100.0f - 16.0f, 272.0f + 16.0f, 132.0f + 16.0f }));   // and IsReady says so
    CHECK(all.IsReady({ 240.0f - 16.0f, 100.0f - 16.0f, 272.0f + 16.0f, 132.0f + 16.0f }));
}

int main()
{
    const World world;
    TestSampleAndContinuity(world);
    TestReadiness(world);
    TestDeterminism(world);
    TestNoSeamAcrossTiles(world);
    TestWhyReadinessMatters(world);
    if (g_failures == 0) std::printf("All scatter tile ground tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
