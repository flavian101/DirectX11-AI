// Tests for the chunk index buffers (roadmap T3): src/TerrainChunkIndices.{h,cpp}. No Direct3D, no framework.
// The indices were moved out of Terrain.cpp unchanged so that every chunk of one size can share them. These tests pin down what
// they must be: how many, in range, wound the right way, and (golden hash) exactly what the old code made.
#include "TerrainChunkIndices.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

// The vertex layout BuildChunkVertices makes: the (cx+1) x (cz+1) grid row by row, then the skirts north, south, west, east, then the
// four corners of the bottom (north-west, north-east, south-west, south-east).
struct P { float x, y, z; };
static std::vector<P> MakeVertices(int cx, int cz)
{
    std::vector<P> v;
    for (int z = 0; z <= cz; ++z) for (int x = 0; x <= cx; ++x) v.push_back({ float(x), 0.0f, float(z) });
    for (int x = 0; x <= cx; ++x) v.push_back({ float(x), -1.0f, 0.0f });          // north skirt: the z = 0 edge, moved down
    for (int x = 0; x <= cx; ++x) v.push_back({ float(x), -1.0f, float(cz) });     // south
    for (int z = 0; z <= cz; ++z) v.push_back({ 0.0f, -1.0f, float(z) });          // west
    for (int z = 0; z <= cz; ++z) v.push_back({ float(cx), -1.0f, float(z) });     // east
    v.push_back({ 0.0f, -2.0f, 0.0f }); v.push_back({ float(cx), -2.0f, 0.0f });          // the bottom, at the floor
    v.push_back({ 0.0f, -2.0f, float(cz) }); v.push_back({ float(cx), -2.0f, float(cz) });
    return v;
}

static P Cross(P a, P b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static P Sub(P a, P b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static P Normal(const std::vector<P>& v, const std::vector<uint32_t>& idx, size_t first)
{
    return Cross(Sub(v[idx[first + 1]], v[idx[first]]), Sub(v[idx[first + 2]], v[idx[first]]));
}

static void TestCountsForTheFullChunk()
{
    const std::vector<MeshLevel> levels = BuildChunkIndexLevels(32, 32, 3);
    CHECK(levels.size() == 3);
    // Level 0: every quad (32 x 32 x 6 indices); each skirt 32 quads x 6. Level 1: every 2nd line (16 x 16); level 2: every 4th (8 x 8).
    const uint32_t surface[3] = { 32 * 32 * 6, 16 * 16 * 6, 8 * 8 * 6 };
    const uint32_t skirt[3] = { 32 * 6, 16 * 6, 8 * 6 };
    for (int lod = 0; lod < 3; ++lod) {
        CHECK(levels[lod].partIndexCounts.size() == 6);   // the surface, four skirts, the bottom
        CHECK(levels[lod].partIndexCounts[0] == surface[lod]);
        for (int edge = 1; edge <= 4; ++edge) CHECK(levels[lod].partIndexCounts[edge] == skirt[lod]);
        CHECK(levels[lod].partIndexCounts[5] == 6);       // the bottom: two triangles at every level
        uint32_t total = 0;
        for (uint32_t part : levels[lod].partIndexCounts) total += part;
        CHECK(total == levels[lod].indices.size());
    }
}

static void TestInRangeAndLastLineKept()
{
    // Square, wide, and the awkward sizes an edge chunk of a single terrain can have (the last line must always be kept).
    const int sizes[][2] = { { 32, 32 }, { 32, 16 }, { 10, 7 }, { 3, 3 }, { 1, 1 }, { 5, 32 } };
    for (const auto& size : sizes) {
        const int cx = size[0], cz = size[1];
        const std::vector<MeshLevel> levels = BuildChunkIndexLevels(cx, cz, 3);
        const size_t vertexCount = static_cast<size_t>(cx + 1) * (cz + 1) + 2 * (cx + 1) + 2 * (cz + 1) + 4;   // grid, skirts, the bottom's corners
        for (const MeshLevel& level : levels) {
            bool inRange = level.indices.size() % 3 == 0 && !level.indices.empty();
            for (uint32_t i : level.indices) inRange = inRange && i < vertexCount;
            CHECK(inRange);
        }
        // The tile's outline never changes: the far corner (cx, cz) is used at every level.
        const uint32_t farCorner = static_cast<uint32_t>(cz * (cx + 1) + cx);
        for (const MeshLevel& level : levels) {
            bool used = false;
            for (size_t k = 0; k < level.partIndexCounts[0]; ++k) used = used || level.indices[k] == farCorner;
            CHECK(used);
        }
    }
}

static void TestWinding()
{
    const int cx = 8, cz = 6;
    const std::vector<P> v = MakeVertices(cx, cz);
    const std::vector<MeshLevel> levels = BuildChunkIndexLevels(cx, cz, 3);
    const P outward[4] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };   // north, south, west, east skirts face away from the chunk
    for (const MeshLevel& level : levels) {
        size_t cursor = 0;
        for (int part = 0; part < 6; ++part) {
            bool consistent = true;
            for (size_t t = cursor; t < cursor + level.partIndexCounts[part]; t += 3) {
                const P n = Normal(v, level.indices, t);
                if (part == 0) consistent = consistent && n.y > 0.0f && std::fabs(n.x) < 1e-6f && std::fabs(n.z) < 1e-6f;   // the surface: every triangle the same way
                else if (part == 5) consistent = consistent && n.y < 0.0f && std::fabs(n.x) < 1e-6f && std::fabs(n.z) < 1e-6f;   // the bottom: facing down
                else consistent = consistent && n.x * outward[part - 1].x + n.z * outward[part - 1].z > 0.0f;               // a skirt: facing outward
            }
            CHECK(consistent);
            cursor += level.partIndexCounts[part];
        }
    }
}

// Hashes the surface and the four skirts only (parts 0 to 4): exactly what the code made before the bottom (part 5) was added at the end.
static uint64_t HashLevels(const std::vector<MeshLevel>& levels)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t value) { for (int b = 0; b < 4; ++b) { h ^= (value >> (8 * b)) & 0xFF; h *= 1099511628211ull; } };
    for (const MeshLevel& level : levels) {
        size_t kept = 0;
        for (int part = 0; part < 5; ++part) { mix(level.partIndexCounts[part]); kept += level.partIndexCounts[part]; }
        for (size_t i = 0; i < kept; ++i) mix(level.indices[i]);
    }
    return h;
}

// Recorded from the code AS IT WAS in Terrain.cpp's BuildTerrainModel, moved here without a change (roadmap T3). If this fails,
// the triangles of every terrain changed: find out why before touching the constants.
static const uint64_t kGolden32x32 = 0x20f979f749eb7116ull;
static const uint64_t kGolden10x7 = 0x7ae2757192f416f8ull;

static void TestTheBottom()
{
    // The bottom is the LAST part of every level, built on the four vertices after the skirts' vertices, the same at every level of detail.
    const int cx = 32, cz = 32;
    const std::vector<MeshLevel> levels = BuildChunkIndexLevels(cx, cz, 3);
    const uint32_t capBase = static_cast<uint32_t>((cx + 1) * (cz + 1) + 2 * (cx + 1) + 2 * (cz + 1));
    for (const MeshLevel& level : levels) {
        const size_t first = level.indices.size() - 6;
        const uint32_t expected[6] = { capBase, capBase + 1, capBase + 2, capBase + 1, capBase + 3, capBase + 2 };
        bool same = true;
        for (int k = 0; k < 6; ++k) same = same && level.indices[first + k] == expected[k];
        CHECK(same);
    }
}

static void TestGolden()
{
    const uint64_t a = HashLevels(BuildChunkIndexLevels(32, 32, 3)), b = HashLevels(BuildChunkIndexLevels(10, 7, 3));
    std::printf("32x32: 0x%016llxull   10x7: 0x%016llxull\n", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    if (kGolden32x32 == 0 || kGolden10x7 == 0) { std::printf("RECORD MODE\n"); ++g_failures; return; }
    CHECK(a == kGolden32x32);
    CHECK(b == kGolden10x7);
}

int main()
{
    TestCountsForTheFullChunk();
    TestInRangeAndLastLineKept();
    TestWinding();
    TestTheBottom();
    TestGolden();
    if (g_failures == 0) std::printf("All terrain chunk index tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
