// Tests for the walls of a bounded world (roadmap X1): OuterEdgesOfTile (src/WorldGrid.{h,cpp}), ChunkWallEdges and WallFloorFor
// (src/TerrainSkirts.h). Pure logic, no Direct3D, no framework.
//   1. Which sides of a tile are the edge of the world; never the seams between tiles, never in an endless world.
//   2. Which chunks of a tile carry a wall, so only the outline of the world gets one.
//   3. Where the floor is.
#include "WorldGrid.h"
#include <cstdio>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static WorldBounds Bounds(int minX, int minZ, int maxX, int maxZ)
{
    WorldBounds b;
    b.bounded = true; b.minX = minX; b.minZ = minZ; b.maxX = maxX; b.maxZ = maxZ;
    return b;
}

static void TestOuterEdges()
{
    const WorldBounds endless;   // not bounded
    for (int z = -3; z <= 3; ++z) for (int x = -3; x <= 3; ++x) CHECK(OuterEdgesOfTile(endless, { x, z }) == 0);

    const WorldBounds b = Bounds(0, 0, 2, 1);   // 3 tiles across, 2 deep
    CHECK(OuterEdgesOfTile(b, { 0, 0 }) == (kEdgeNorth | kEdgeWest));
    CHECK(OuterEdgesOfTile(b, { 1, 0 }) == kEdgeNorth);
    CHECK(OuterEdgesOfTile(b, { 2, 0 }) == (kEdgeNorth | kEdgeEast));
    CHECK(OuterEdgesOfTile(b, { 0, 1 }) == (kEdgeSouth | kEdgeWest));
    CHECK(OuterEdgesOfTile(b, { 1, 1 }) == kEdgeSouth);
    CHECK(OuterEdgesOfTile(b, { 2, 1 }) == (kEdgeSouth | kEdgeEast));
    CHECK(OuterEdgesOfTile(b, { 5, 5 }) == 0);       // a tile outside the bounds is not part of the world
    CHECK(OuterEdgesOfTile(b, { -1, 0 }) == 0);

    CHECK(OuterEdgesOfTile(Bounds(0, 0, 0, 0), { 0, 0 }) == 15);          // a one-tile world is closed on all four sides
    CHECK(OuterEdgesOfTile(Bounds(-2, -1, -1, 0), { -2, -1 }) == (kEdgeNorth | kEdgeWest));   // negative tile numbers work the same way
    CHECK(OuterEdgesOfTile(Bounds(-2, -1, -1, 0), { -1, 0 }) == (kEdgeSouth | kEdgeEast));

    // The property that matters: a side is a wall EXACTLY when the tile across it is not part of the world (so the walls form one
    // closed outline, with no gap and no wall inside the world). Every rectangle of tiles up to 5 x 5, anywhere.
    int perimeterTiles = 0, interiorTiles = 0;
    bool consistent = true;
    for (int minX = -2; minX <= 1; ++minX) for (int minZ = -2; minZ <= 1; ++minZ)
        for (int w = 1; w <= 5; ++w) for (int h = 1; h <= 5; ++h) {
            const WorldBounds world = Bounds(minX, minZ, minX + w - 1, minZ + h - 1);
            for (int z = minZ; z < minZ + h; ++z) for (int x = minX; x < minX + w; ++x) {
                const unsigned char mask = OuterEdgesOfTile(world, { x, z });
                consistent = consistent && (((mask & kEdgeNorth) != 0) == !world.Contains({ x, z - 1 }));
                consistent = consistent && (((mask & kEdgeSouth) != 0) == !world.Contains({ x, z + 1 }));
                consistent = consistent && (((mask & kEdgeWest) != 0) == !world.Contains({ x - 1, z }));
                consistent = consistent && (((mask & kEdgeEast) != 0) == !world.Contains({ x + 1, z }));
                if (w == 5 && h == 4 && minX == 0 && minZ == 0) (mask ? perimeterTiles : interiorTiles)++;
            }
        }
    CHECK(consistent);
    CHECK(perimeterTiles == 14 && interiorTiles == 6);   // a 5 x 4 world: 2 x 5 + 2 x 4 - 4 tiles on the outline, 3 x 2 inside
}

static void TestChunkWalls()
{
    const int cells = 256, chunk = 32;   // the default tile: 8 x 8 chunks
    auto wallsOf = [&](unsigned char tileEdges, int cx, int cz) {
        return ChunkWallEdges(tileEdges, cx * chunk, cz * chunk, (cx + 1) * chunk, (cz + 1) * chunk, cells, cells);
    };
    CHECK(wallsOf(kEdgeNorth | kEdgeWest, 0, 0) == (kEdgeNorth | kEdgeWest));
    CHECK(wallsOf(kEdgeNorth | kEdgeWest, 1, 0) == kEdgeNorth);
    CHECK(wallsOf(kEdgeNorth | kEdgeWest, 0, 5) == kEdgeWest);
    CHECK(wallsOf(kEdgeNorth | kEdgeWest, 7, 7) == 0);      // the far corner has no wall: it is a seam with the next tile
    CHECK(wallsOf(15, 7, 7) == (kEdgeSouth | kEdgeEast));
    CHECK(wallsOf(15, 7, 0) == (kEdgeNorth | kEdgeEast));
    CHECK(wallsOf(0, 0, 0) == 0);                           // an ordinary tile: no walls at all

    int withWall = 0, bits = 0;
    for (int cz = 0; cz < 8; ++cz) for (int cx = 0; cx < 8; ++cx) {
        const unsigned char m = wallsOf(15, cx, cz);
        if (m) ++withWall;
        for (int b = 0; b < 4; ++b) if (m & (1 << b)) ++bits;
    }
    CHECK(withWall == 28);   // the outline of 8 x 8 chunks
    CHECK(bits == 32);       // 4 sides x 8 chunks: each side of the tile is covered once, corners count twice

    // A chunk at the end of a tile whose size is not a multiple of 32 (a single terrain of 42 x 39 cells): its last block is smaller.
    CHECK(ChunkWallEdges(kEdgeEast | kEdgeSouth, 32, 32, 42, 39, 42, 39) == (kEdgeEast | kEdgeSouth));
    CHECK(ChunkWallEdges(kEdgeEast | kEdgeSouth, 0, 0, 32, 32, 42, 39) == 0);
}

static void TestFloor()
{
    CHECK(WallFloorFor(240.0f) == -24.0f);
    CHECK(WallFloorFor(50.0f) == -10.0f);       // never shallower than 10 m
    CHECK(WallFloorFor(1000.0f) == -100.0f);
    for (float height : { 0.0f, 1.0f, 45.0f, 240.0f, 5000.0f }) CHECK(WallFloorFor(height) < 0.0f);   // always below the lowest ground (height 0)
}

int main()
{
    TestOuterEdges();
    TestChunkWalls();
    TestFloor();
    if (g_failures == 0) std::printf("All terrain wall tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
