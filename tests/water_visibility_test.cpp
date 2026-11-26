// Tests for the water-terrain contract (docs/ROADMAP.md section 5, item W1): src/WorldGround.{h,cpp} with src/WaterShore.h.
// No Direct3D, no framework. The water measures the ground under it from the world's GROUND FUNCTION, not from loaded tile meshes.
//   A. The depth map says what the ground says: water where the ground is below the level, none where it is above (contract 1, 2).
//   B. Inside a bounded world there is always ground, whether or not any tile is loaded; beyond the edge there is none (contract 3).
//   C. The ground grid equals the heights of a tile's vertices at full detail (contract 4).
//   D. Moving, turning or scaling a body does not move the shoreline in the world (contract 5).
//   E. Information for the open W1 suspect "a coarse mesh dips below the sea": how far a coarser tile can lie from the true ground.
#include "WaterShore.h"
#include "WorldGround.h"
#include "WorldTileHeights.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static const float kHeightScale = 100.0f;
static float g_sea = 0.0f;   // metres: the MEDIAN ground height of the test world, so about half of it is wet (chosen in main)
static const FractalWorldSettings kHills = { 7, 6, 400.0f };

static WorldGridSettings SmallGrid()
{
    WorldGridSettings grid;
    grid.chunksPerTile = 2;       // 64 cells
    grid.metresPerCell = 4.0f;    // 256 m tiles
    return grid;
}

static WorldBounds TwoByTwo()
{
    WorldBounds bounds;
    bounds.bounded = true; bounds.minX = 0; bounds.minZ = 0; bounds.maxX = 1; bounds.maxZ = 1;   // the world is [0, 512] x [0, 512] metres
    return bounds;
}

// A body of the water: a square of `half` metres either way around (cx, cz), turned by `angle` radians. Local (-1..1) -> world.
struct Body {
    double cx, cz, halfX, halfZ, angle;
    void ToWorld(float lx, float lz, float& wx, float& wz) const
    {
        const double c = std::cos(angle), s = std::sin(angle), ax = lx * halfX, az = lz * halfZ;
        wx = static_cast<float>(cx + c * ax - s * az);
        wz = static_cast<float>(cz + s * ax + c * az);
    }
    void ToLocal(double wx, double wz, float& lx, float& lz) const
    {
        const double c = std::cos(angle), s = std::sin(angle), dx = wx - cx, dz = wz - cz;
        lx = static_cast<float>((c * dx + s * dz) / halfX);
        lz = static_cast<float>((-s * dx + c * dz) / halfZ);
    }
};

static WaterShore::DepthField BakeBody(const Body& body, const WorldGroundGrid& ground, int texels)
{
    return WaterShore::Bake(texels, texels, g_sea,
        [&body](float lx, float lz, float& wx, float& wz) { body.ToWorld(lx, lz, wx, wz); },
        [&ground](float wx, float wz, float& y) { return ground.Sample(wx, wz, y); });
}

static WorldGroundGrid MakeGround(const FractalHeightSource& source, double lo, double hi)
{
    return BuildWorldGroundGrid(source, SmallGrid(), TwoByTwo(), kHeightScale, lo, hi, lo, hi);
}

static void TestDepthMapSaysWhatTheGroundSays()
{
    const FractalHeightSource source(kHills);
    const WorldGroundGrid ground = MakeGround(source, -128.0, 640.0);
    const Body body{ 256.0, 256.0, 384.0, 384.0, 0.0 };   // covers the world and a margin all round
    const int texels = 192;
    const WaterShore::DepthField field = BakeBody(body, ground, texels);

    double worstError = 0.0;
    int wet = 0, dry = 0, outside = 0, inside = 0;
    bool signsRight = true, outsideDeep = true, insideNeverDeep = true;
    for (int z = 0; z < texels; ++z) {
        for (int x = 0; x < texels; ++x) {
            const float lx = (x + 0.5f) / texels * 2.0f - 1.0f, lz = (z + 0.5f) / texels * 2.0f - 1.0f;
            float wx, wz;
            body.ToWorld(lx, lz, wx, wz);
            const float depth = field.Data()[static_cast<size_t>(z) * texels + x];
            const bool strictlyInside = wx > 8.0f && wx < 504.0f && wz > 8.0f && wz < 504.0f;
            const bool strictlyOutside = wx < -8.0f || wx > 520.0f || wz < -8.0f || wz > 520.0f;
            if (strictlyOutside) { ++outside; outsideDeep = outsideDeep && depth == WaterShore::kDeepWater; continue; }   // B: no ground beyond the edge
            if (!strictlyInside) continue;
            ++inside;
            insideNeverDeep = insideNeverDeep && depth < WaterShore::kDeepWater;                                           // B: ground everywhere inside
            const double truth = static_cast<double>(g_sea) - static_cast<double>(source.Height01(wx, wz)) * kHeightScale;   // the ground function itself
            worstError = (std::max)(worstError, std::fabs(truth - depth));
            if (truth > 2.0) { ++wet; signsRight = signsRight && depth > 0.0f; }                                           // A: below the level: water
            if (truth < -2.0) { ++dry; signsRight = signsRight && depth < 0.0f; }                                          // A: above it: none
        }
    }
    std::printf("depth map vs the ground function: worst error %.3f m over %d inside texels (%d wet, %d dry, %d outside)\n", worstError, inside, wet, dry, outside);
    CHECK(inside > 10000 && outside > 5000);
    CHECK(wet > 2000 && dry > 2000);       // the test world really has both
    CHECK(signsRight);
    CHECK(outsideDeep);
    CHECK(insideNeverDeep);
    CHECK(worstError < 1.0);
}

// The value a tile's vertex has at lattice point (x, z) of tile (tx, tz), in metres.
static float TileVertexMetres(const FractalHeightSource& source, TileCoord tile, int x, int z)
{
    const WorldGridSettings grid = SmallGrid();
    const TileHeights heights = GenerateTileHeights(source, grid, tile);
    return heights.samples[static_cast<size_t>(z) * (grid.TileCells() + 1) + x] * kHeightScale;
}

static void TestGroundEqualsTheTilesAtFullDetail()
{
    const FractalHeightSource source(kHills);
    const WorldGridSettings grid = SmallGrid();
    const int cells = grid.TileCells();
    {
        const WorldGroundGrid ground = MakeGround(source, -20.0, 540.0);
        CHECK(ground.step == static_cast<double>(grid.metresPerCell));   // full detail: one sample per lattice point
        double worst = 0.0;
        int compared = 0;
        for (int tz = 0; tz < 2; ++tz) {
            for (int tx = 0; tx < 2; ++tx) {
                for (int z = 0; z <= cells; z += 5) {
                    for (int x = 0; x <= cells; x += 5) {
                        const double wx = (static_cast<double>(tx) * cells + x) * grid.metresPerCell, wz = (static_cast<double>(tz) * cells + z) * grid.metresPerCell;
                        float metres = 0.0f;
                        CHECK(ground.Sample(wx, wz, metres));
                        worst = (std::max)(worst, static_cast<double>(std::fabs(metres - TileVertexMetres(source, { tx, tz }, x, z))));
                        ++compared;
                    }
                }
            }
        }
        std::printf("ground grid vs tile vertices (%s): worst difference %.6f m over %d vertices\n", "ground", worst, compared);
        CHECK(compared > 600);
        CHECK(worst < 1.0e-3);   // contract 4: the water uses the ground the player sees
    }
}

static void TestBodyTransformDoesNotMoveTheShore()
{
    const FractalHeightSource source(kHills);
    const WorldGroundGrid ground = MakeGround(source, -300.0, 800.0);
    const Body one{ 256.0, 256.0, 300.0, 300.0, 0.0 };
    const Body two{ 230.0, 290.0, 330.0, 260.0, 0.7 };    // moved, turned and a different size
    const WaterShore::DepthField fieldOne = BakeBody(one, ground, 256), fieldTwo = BakeBody(two, ground, 256);
    double worst = 0.0;
    int probes = 0;
    for (double wx = 120.0; wx <= 400.0; wx += 17.0) {
        for (double wz = 120.0; wz <= 400.0; wz += 19.0) {
            float l1x, l1z, l2x, l2z;
            one.ToLocal(wx, wz, l1x, l1z);
            two.ToLocal(wx, wz, l2x, l2z);
            if (std::fabs(l1x) > 0.95f || std::fabs(l1z) > 0.95f || std::fabs(l2x) > 0.95f || std::fabs(l2z) > 0.95f) continue;
            const double d1 = fieldOne.Sample(l1x, l1z), d2 = fieldTwo.Sample(l2x, l2z);
            worst = (std::max)(worst, std::fabs(d1 - d2));
            // And the sign (is this point wet?) agrees unless the ground is within the interpolation error of the level.
            if (std::fabs(d1) > 1.0) CHECK((d1 > 0.0) == (d2 > 0.0));
            ++probes;
        }
    }
    std::printf("two bodies over the same ground: worst depth difference %.3f m at %d probes\n", worst, probes);
    CHECK(probes > 100);
    CHECK(worst < 1.0);
}

static void TestHowFarACoarseTileLiesFromTheGround()
{
    // The real default tile: 8 x 8 chunks of 32 cells of 4 m = 1024 m, so levels of detail 1, 2 and 3 exist.
    // Information for W1's first suspect: the water measures the true ground, but the player sees the MESH, and a tile far away is
    // drawn at a lower level of detail (1/4, 1/16 of the cells). Where does such a mesh stray from the ground, and by how much?
    const FractalHeightSource source(kHills);
    WorldGridSettings grid;
    grid.chunksPerTile = 8;
    grid.metresPerCell = 4.0f;
    const WorldGroundGrid ground = BuildWorldGroundGrid(source, grid, WorldBounds{}, kHeightScale, -20.0, 1050.0, -20.0, 1050.0);
    for (int lod = 1; lod <= 3; ++lod) {
        const WorldGridSettings coarse = grid.ForLod(lod);
        const int cells = coarse.TileCells();
        const TileHeights heights = GenerateTileHeights(source, coarse, { 0, 0 });
        double above = 0.0, below = 0.0;   // the biggest amount the mesh stands above / below the true ground, at a vertex midway between its samples
        for (int z = 0; z < cells; ++z) {
            for (int x = 0; x < cells; ++x) {
                // the middle of the quad: the mesh there is the average of its corners (a good stand-in for its two triangles)
                const double wx = (x + 0.5) * coarse.metresPerCell, wz = (z + 0.5) * coarse.metresPerCell;
                const float mesh = 0.25f * kHeightScale * (heights.samples[static_cast<size_t>(z) * (cells + 1) + x] + heights.samples[static_cast<size_t>(z) * (cells + 1) + x + 1] +
                                                           heights.samples[static_cast<size_t>(z + 1) * (cells + 1) + x] + heights.samples[static_cast<size_t>(z + 1) * (cells + 1) + x + 1]);
                float truth = 0.0f;
                if (!ground.Sample(wx, wz, truth)) continue;
                above = (std::max)(above, static_cast<double>(mesh - truth));
                below = (std::max)(below, static_cast<double>(truth - mesh));
            }
        }
        std::printf("level of detail %d (%.0f m cells): the mesh stands up to %.2f m above and %.2f m below the true ground\n", lod, coarse.metresPerCell, above, below);
    }
}

static void ChooseSeaLevel()
{
    const FractalHeightSource source(kHills);
    std::vector<float> heights;
    for (double z = 0.0; z <= 512.0; z += 8.0) for (double x = 0.0; x <= 512.0; x += 8.0) heights.push_back(source.Height01(x, z) * kHeightScale);
    std::sort(heights.begin(), heights.end());
    g_sea = heights[heights.size() / 2];
    std::printf("test world: ground from %.1f to %.1f m, sea level %.1f m (the median)\n", heights.front(), heights.back(), g_sea);
}

int main()
{
    ChooseSeaLevel();
    TestDepthMapSaysWhatTheGroundSays();
    TestGroundEqualsTheTilesAtFullDetail();
    TestBodyTransformDoesNotMoveTheShore();
    TestHowFarACoarseTileLiesFromTheGround();
    if (g_failures == 0) std::printf("All water visibility tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
