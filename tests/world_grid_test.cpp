// Tests for src/WorldGrid.{h,cpp} and the Heightfield apron. No Direct3D, no framework.
//   1. Tile maths: which tile holds a point (negative coordinates too), where its centre is, how far a point is from it.
//   2. TilesWithin: the right tiles, nearest first, in a fixed order.
//   3. SHARED EDGES: two neighbouring tiles hold exactly the same numbers along their common edge, even 1000 km away.
//   4. The apron holds the neighbours' samples.
//   5. SEAMLESS LIGHTING: with the apron the two tiles compute the SAME normal along the seam; without it they do not.
//   6. SEAMLESS LAYERS: baking the layer weights tile by tile gives exactly what one big bake gives, when the layer-edge
//      noise is read in world space (SplatPlacement::noiseOffset); with each tile's own local noise it does not.
#include "WorldGrid.h"
#include "TerrainChunks.h"
#include "TerrainSplat.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static_assert(kWorldChunkCells == kTerrainChunkCells, "a world tile must be a whole number of terrain chunks");

static WorldGridSettings DefaultGrid()
{
    WorldGridSettings grid;   // 8 chunks x 32 cells = 256 cells of 4 m = 1024 m tiles
    return grid;
}

static void TestTileMaths()
{
    const WorldGridSettings grid = DefaultGrid();
    CHECK(grid.TileCells() == 256);
    CHECK(grid.TileSize() == 1024.0);
    CHECK((TileContaining(grid, 0.0, 0.0) == TileCoord{ 0, 0 }));
    CHECK((TileContaining(grid, 1023.99, 5.0) == TileCoord{ 0, 0 }));
    CHECK((TileContaining(grid, 1024.0, 5.0) == TileCoord{ 1, 0 }));   // a border belongs to the tile that STARTS there
    CHECK((TileContaining(grid, -0.1, -0.1) == TileCoord{ -1, -1 }));   // floor, not truncation
    CHECK((TileContaining(grid, -1024.0, -1025.0) == TileCoord{ -1, -2 }));

    double cx = 0, cz = 0;
    TileCenter(grid, { 0, 0 }, cx, cz);
    CHECK(cx == 512.0 && cz == 512.0);
    TileCenter(grid, { -1, 2 }, cx, cz);
    CHECK(cx == -512.0 && cz == 2560.0);

    CHECK(DistanceToTile(grid, { 0, 0 }, 100.0, 900.0) == 0.0);               // inside
    CHECK(std::fabs(DistanceToTile(grid, { 0, 0 }, 1034.0, 500.0) - 10.0) < 1e-9);   // 10 m east of it
    CHECK(std::fabs(DistanceToTile(grid, { 0, 0 }, 1027.0, 1028.0) - 5.0) < 1e-9);   // 3-4-5 off the corner
    CHECK(std::fabs(DistanceToTile(grid, { 1, 1 }, 1000.0, 1000.0) - std::sqrt(24.0 * 24.0 * 2.0)) < 1e-9);

    WorldGridSettings odd;
    odd.chunksPerTile = 1000;
    odd.metresPerCell = 0.0f;
    odd.Clamp();
    CHECK(odd.chunksPerTile == WorldGridSettings::kChunksMax);
    CHECK(odd.metresPerCell == WorldGridSettings::kMetresPerCellMin);
}

static void TestTilesWithin()
{
    const WorldGridSettings grid = DefaultGrid();
    auto one = TilesWithin(grid, 300.0, 300.0, 0.0);
    CHECK(one.size() == 1 && one[0] == (TileCoord{ 0, 0 }));

    auto nine = TilesWithin(grid, 512.0, 512.0, 1500.0);   // from the middle of tile (0,0): the corners are 724 m away
    CHECK(nine.size() == 9);
    CHECK((nine[0] == TileCoord{ 0, 0 }));                  // nearest first
    CHECK((nine[1] == TileCoord{ 0, -1 }) && (nine[2] == TileCoord{ -1, 0 }) && (nine[3] == TileCoord{ 1, 0 }) && (nine[4] == TileCoord{ 0, 1 }));   // ties: z, then x
    for (size_t i = 5; i < 9; ++i) CHECK(nine[i].x != 0 && nine[i].z != 0);   // the corners are last

    CHECK(TilesWithin(grid, 512.0, 512.0, 600.0).size() == 5);   // radius 600 < 724: no corners
    CHECK(TilesWithin(grid, 512.0, 512.0, -1.0).empty());
    CHECK(TilesWithin(grid, 512.0, 512.0, std::nan("")).empty());

    // Every tile in the list really is within the radius, and every tile within the radius is in the list.
    const auto list = TilesWithin(grid, -3000.0, 777.0, 2500.0);
    for (int z = -8; z <= 8; ++z)
        for (int x = -8; x <= 8; ++x) {
            const bool near = DistanceToTile(grid, { x, z }, -3000.0, 777.0) <= 2500.0;
            const bool listed = std::find(list.begin(), list.end(), TileCoord{ x, z }) != list.end();
            CHECK(near == listed);
        }
}

static const FractalWorldSettings kHills = { 7, 6, 400.0f };   // small hills, so the slopes are steep enough to test normals

static float At(const std::vector<float>& samples, int cells, int x, int z) { return samples[static_cast<size_t>(z) * (cells + 1) + x]; }

static void TestSharedEdges(TileCoord a)
{
    const WorldGridSettings grid = DefaultGrid();
    const FractalHeightSource source(kHills);
    const int cells = grid.TileCells();
    const auto tileA = GenerateTileSamples(source, grid, a);
    const auto tileEast = GenerateTileSamples(source, grid, { a.x + 1, a.z });
    const auto tileSouth = GenerateTileSamples(source, grid, { a.x, a.z + 1 });
    CHECK(tileA.size() == static_cast<size_t>(cells + 1) * (cells + 1));
    bool same = true;
    for (int i = 0; i <= cells; ++i) {
        same = same && At(tileA, cells, cells, i) == At(tileEast, cells, 0, i);    // A's east column = the east tile's west column
        same = same && At(tileA, cells, i, cells) == At(tileSouth, cells, i, 0);   // A's south row = the south tile's north row
    }
    CHECK(same);   // bit for bit, no tolerance
    const auto again = GenerateTileSamples(source, grid, a);
    CHECK(again == tileA);   // deterministic
    const float lo = *std::min_element(tileA.begin(), tileA.end()), hi = *std::max_element(tileA.begin(), tileA.end());
    CHECK(lo >= 0.0f && hi <= 1.0f && hi - lo > 0.05f);   // in range, and not flat
}

static void TestApronHoldsTheNeighbours()
{
    const WorldGridSettings grid = DefaultGrid();
    const FractalHeightSource source(kHills);
    const int cells = grid.TileCells();
    const TileCoord a{ 3, -2 };
    const auto tileA = GenerateTileSamples(source, grid, a);
    const auto east = GenerateTileSamples(source, grid, { a.x + 1, a.z });
    const auto west = GenerateTileSamples(source, grid, { a.x - 1, a.z });
    const auto south = GenerateTileSamples(source, grid, { a.x, a.z + 1 });
    const auto north = GenerateTileSamples(source, grid, { a.x, a.z - 1 });
    const HeightfieldApron apron = GenerateTileApron(source, grid, a);
    bool ok = true;
    for (int i = 0; i <= cells; ++i) {
        ok = ok && apron.east[i] == At(east, cells, 1, i);            // one step into the east tile
        ok = ok && apron.west[i] == At(west, cells, cells - 1, i);    // one step back into the west tile
        ok = ok && apron.south[i] == At(south, cells, i, 1);
        ok = ok && apron.north[i] == At(north, cells, i, cells - 1);
    }
    CHECK(ok);

    Heightfield field;
    CHECK(!field.SetApron(apron));                                        // no grid yet
    CHECK(field.Reset(TileLayout(grid, 100.0f), tileA));
    CHECK(!field.HasApron());
    HeightfieldApron wrong = apron;
    wrong.west.pop_back();
    CHECK(!field.SetApron(wrong) && !field.HasApron());                   // a wrong length changes nothing
    HeightfieldApron loud = apron;
    loud.east[3] = 7.0f;
    loud.north[4] = -2.0f;
    CHECK(field.SetApron(loud) && field.HasApron());
    CHECK(field.SampleBeyond(cells + 1, 3) == 1.0f && field.SampleBeyond(4, -1) == 0.0f);   // clamped to 0..1
    CHECK(field.SampleBeyond(cells + 5, 3) == field.Sample(cells, 3));                       // further out: the plain clamp
    CHECK(field.SampleBeyond(-1, -1) == field.Sample(0, 0));                                 // corners are not in the apron
    CHECK(field.Reset(TileLayout(grid, 100.0f), tileA) && !field.HasApron());                // a new grid drops it
}

static Heightfield MakeTile(const FractalHeightSource& source, const WorldGridSettings& grid, TileCoord tile, bool withApron)
{
    Heightfield field;
    field.Reset(TileLayout(grid, 100.0f), GenerateTileSamples(source, grid, tile));
    if (withApron) field.SetApron(GenerateTileApron(source, grid, tile));
    return field;
}

static void TestSeamlessNormals()
{
    const WorldGridSettings grid = DefaultGrid();
    const FractalHeightSource source(kHills);
    const int cells = grid.TileCells();
    const TileCoord a{ 0, 0 };

    for (bool withApron : { true, false }) {
        const Heightfield left = MakeTile(source, grid, a, withApron);
        const Heightfield right = MakeTile(source, grid, { a.x + 1, a.z }, withApron);
        const Heightfield below = MakeTile(source, grid, { a.x, a.z + 1 }, withApron);
        float worstEast = 0.0f, worstSouth = 0.0f;
        for (int i = 0; i <= cells; ++i) {
            const HfVec3 l = left.VertexNormal(cells, i), r = right.VertexNormal(0, i);
            worstEast = std::max(worstEast, std::max({ std::fabs(l.x - r.x), std::fabs(l.y - r.y), std::fabs(l.z - r.z) }));
            const HfVec3 t = left.VertexNormal(i, cells), b = below.VertexNormal(i, 0);
            worstSouth = std::max(worstSouth, std::max({ std::fabs(t.x - b.x), std::fabs(t.y - b.y), std::fabs(t.z - b.z) }));
        }
        std::printf("%s apron: worst normal difference along the seams: east %.6f, south %.6f\n", withApron ? "with" : "without", worstEast, worstSouth);
        if (withApron) CHECK(worstEast == 0.0f && worstSouth == 0.0f);        // the two tiles agree exactly
        else           CHECK(worstEast > 1e-3f && worstSouth > 1e-3f);        // the problem the apron solves is real
    }
}

static void TestTiledBakeEqualsOneBigBake()
{
    WorldGridSettings tileGrid;                   // 256 cells = 1024 m
    WorldGridSettings bigGrid;
    bigGrid.chunksPerTile = 16;                   // 512 cells = 2048 m: the same 4 m samples as 2 x 2 tiles, in one grid
    const FractalHeightSource source(kHills);
    const SplatRules rules;

    Heightfield big;
    big.Reset(TileLayout(bigGrid, 100.0f), GenerateTileSamples(source, bigGrid, { 0, 0 }));
    big.SetApron(GenerateTileApron(source, bigGrid, { 0, 0 }));
    SplatPlacement bigPlacement;
    bigPlacement.noiseOffsetX = bigPlacement.noiseOffsetZ = 1024.0f;   // the big grid's centre, in world metres
    SplatMap bigMap;
    BakeSplatAll(big, rules, bigPlacement, bigMap);

    const int cells = tileGrid.TileCells();
    for (bool worldNoise : { true, false }) {
        long long differing = 0, texels = 0;
        for (int tz = 0; tz < 2; ++tz) {
            for (int tx = 0; tx < 2; ++tx) {
                const TileCoord tile{ tx, tz };
                const Heightfield field = MakeTile(source, tileGrid, tile, true);
                SplatPlacement placement;
                if (worldNoise) {
                    double centerX = 0.0, centerZ = 0.0;
                    TileCenter(tileGrid, tile, centerX, centerZ);
                    placement.noiseOffsetX = static_cast<float>(centerX);
                    placement.noiseOffsetZ = static_cast<float>(centerZ);
                }
                SplatMap map;
                BakeSplatAll(field, rules, placement, map);
                for (int z = 0; z < cells; ++z) {
                    for (int x = 0; x < cells; ++x) {
                        for (int channel = 0; channel < kSplatChannels; ++channel) {
                            const size_t inTile = (static_cast<size_t>(z) * map.width + x) * kSplatChannels + channel;
                            const size_t inBig = (static_cast<size_t>(tz * cells + z) * bigMap.width + (tx * cells + x)) * kSplatChannels + channel;
                            ++texels;
                            if (map.rgba[inTile] != bigMap.rgba[inBig]) ++differing;
                        }
                    }
                }
            }
        }
        std::printf("%s noise: %lld of %lld layer weights differ from one big bake\n", worldNoise ? "world" : "local", differing, texels);
        if (worldNoise) CHECK(differing == 0);
        else            CHECK(differing > 0);
    }
}

int main()
{
    TestTileMaths();
    TestTilesWithin();
    TestSharedEdges({ 0, 0 });
    TestSharedEdges({ -5, 3 });
    TestSharedEdges({ 1000, -1000 });   // a million metres from the origin: still bit-identical edges
    TestApronHoldsTheNeighbours();
    TestSeamlessNormals();
    TestTiledBakeEqualsOneBigBake();
    if (g_failures == 0) std::printf("world_grid_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
