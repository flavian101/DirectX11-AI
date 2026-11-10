// Tests for src/TerrainChunks.h. Two questions, both answered by brute force:
//   1. Does ChunksToRefresh name EXACTLY the chunks that hold a changed vertex (no stale chunk, no wasted work)?
//   2. After a brush stroke, does refreshing only those chunks give the same vertex data as rebuilding every chunk?
#include "TerrainChunks.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static std::uint32_t g_rng = 4242;
static int RandomInt(int lo, int hi) { g_rng = g_rng * 1664525u + 1013904223u; return lo + static_cast<int>((g_rng >> 8) % static_cast<std::uint32_t>(hi - lo + 1)); }
static float Random01() { g_rng = g_rng * 1664525u + 1013904223u; return static_cast<float>(g_rng >> 8) / 16777216.0f; }

struct Dims { int cellsX, cellsZ; };

static int ChunkCount(int cells) { return (cells + kTerrainChunkCells - 1) / kTerrainChunkCells; }

// Does chunk (cx, cz) hold the vertex at grid (x, z)? Written from the definition, with no cleverness.
static bool Holds(int cx, int cz, int x, int z, const Dims& d)
{
    const int x0 = cx * kTerrainChunkCells, x1 = (std::min)(x0 + kTerrainChunkCells, d.cellsX);
    const int z0 = cz * kTerrainChunkCells, z1 = (std::min)(z0 + kTerrainChunkCells, d.cellsZ);
    return x >= x0 && x <= x1 && z >= z0 && z <= z1;
}

static void TestExactRange()
{
    const Dims sizes[] = { { 64, 64 }, { 70, 45 }, { 33, 33 }, { 32, 32 }, { 96, 32 }, { 100, 7 }, { 1024, 512 } };
    for (const Dims& d : sizes) {
        for (int trial = 0; trial < 400; ++trial) {
            GridRect edited;
            const int ax = RandomInt(0, d.cellsX), az = RandomInt(0, d.cellsZ);
            edited.Include(ax, az);
            edited.Include((std::min)(d.cellsX, ax + RandomInt(0, 40)), (std::min)(d.cellsZ, az + RandomInt(0, 40)));

            // brute force: every vertex within one sample of the edit, and every chunk that holds any of them
            const GridRect vertices = edited.Grown(1, d.cellsX, d.cellsZ);
            std::set<std::pair<int, int>> expected;
            for (int cz = 0; cz < ChunkCount(d.cellsZ); ++cz)
                for (int cx = 0; cx < ChunkCount(d.cellsX); ++cx)
                    for (int z = vertices.z0; z <= vertices.z1 && !expected.count({ cx, cz }); ++z)
                        for (int x = vertices.x0; x <= vertices.x1; ++x)
                            if (Holds(cx, cz, x, z, d)) { expected.insert({ cx, cz }); break; }

            const ChunkRange range = ChunksToRefresh(edited, d.cellsX, d.cellsZ);
            std::set<std::pair<int, int>> got;
            for (int cz = range.firstZ; cz <= range.lastZ; ++cz)
                for (int cx = range.firstX; cx <= range.lastX; ++cx) got.insert({ cx, cz });
            if (got != expected) {
                std::printf("  mismatch: grid %dx%d, edit x %d..%d z %d..%d: expected %zu chunks, got %zu\n",
                            d.cellsX, d.cellsZ, edited.x0, edited.x1, edited.z0, edited.z1, expected.size(), got.size());
                ++g_failures;
                return;
            }
        }
    }
    CHECK(ChunksToRefresh(GridRect{}, 64, 64).IsEmpty());
}

// What a chunk's vertex buffer holds, reduced to the numbers that matter: height and normal of every vertex it owns.
struct ChunkData { std::vector<float> numbers; };

static ChunkData BuildChunk(const Heightfield& field, int cx, int cz)
{
    const HeightfieldLayout& l = field.GetLayout();
    const int x0 = cx * kTerrainChunkCells, x1 = (std::min)(x0 + kTerrainChunkCells, l.cellsX);
    const int z0 = cz * kTerrainChunkCells, z1 = (std::min)(z0 + kTerrainChunkCells, l.cellsZ);
    ChunkData data;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const HfVec3 n = field.VertexNormal(x, z);
            data.numbers.insert(data.numbers.end(), { field.Sample(x, z), n.x, n.y, n.z });
        }
    return data;
}

static void TestIncrementalEqualsFull()
{
    const Dims d{ 100, 70 };                                    // not a multiple of 32: ragged edge chunks
    std::vector<float> start(static_cast<size_t>(d.cellsX + 1) * (d.cellsZ + 1));
    for (float& v : start) v = 0.3f + 0.4f * Random01();
    Heightfield field;
    field.Reset({ d.cellsX, d.cellsZ, 100.0f, 70.0f, 20.0f }, start);

    const int chunksX = ChunkCount(d.cellsX), chunksZ = ChunkCount(d.cellsZ);
    std::vector<ChunkData> mesh;                                // the "GPU buffers"
    for (int cz = 0; cz < chunksZ; ++cz) for (int cx = 0; cx < chunksX; ++cx) mesh.push_back(BuildChunk(field, cx, cz));

    int strokesOnBorders = 0;
    for (int stroke = 0; stroke < 200; ++stroke) {
        BrushSettings brush;
        brush.mode = static_cast<BrushMode>(RandomInt(0, 3));
        brush.radius = 2.0f + Random01() * 14.0f;
        brush.strength = 0.3f + Random01() * 0.7f;
        brush.falloff = Random01();
        brush.targetHeight = Random01();
        // centres everywhere, including right on chunk borders (multiples of 32 cells) and off the edges
        const float cx = (RandomInt(-5, 105) - 50.0f), cz = (RandomInt(-5, 75) - 35.0f);
        if ((RandomInt(0, 105) % 32) == 0) ++strokesOnBorders;
        const GridRect changed = ApplyBrush(field, brush, cx, cz, 0.1f, nullptr);

        const ChunkRange range = ChunksToRefresh(changed, d.cellsX, d.cellsZ);
        for (int zc = range.firstZ; zc <= range.lastZ; ++zc)    // refresh ONLY these
            for (int xc = range.firstX; xc <= range.lastX; ++xc) mesh[static_cast<size_t>(zc) * chunksX + xc] = BuildChunk(field, xc, zc);

        for (int zc = 0; zc < chunksZ; ++zc)                    // and compare with rebuilding EVERYTHING
            for (int xc = 0; xc < chunksX; ++xc)
                if (mesh[static_cast<size_t>(zc) * chunksX + xc].numbers != BuildChunk(field, xc, zc).numbers) {
                    std::printf("  stale chunk (%d,%d) after stroke %d (edit x %d..%d z %d..%d)\n", xc, zc, stroke, changed.x0, changed.x1, changed.z0, changed.z1);
                    ++g_failures;
                    return;
                }
    }
}

int main()
{
    TestExactRange();
    TestIncrementalEqualsFull();
    if (g_failures == 0) std::printf("TerrainChunks: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
