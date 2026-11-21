#pragma once
// Which skirts does a terrain tile need? Pure logic, no Direct3D, so it can be tested on its own.
//
// A SKIRT is a vertical strip of triangles hung below one edge of a tile to hide the crack that opens when two
// neighbouring tiles are drawn at different levels of detail (their edge vertices no longer line up). Two tiles at
// the SAME level share exactly the same edge vertices, so there is no crack and a skirt would be wasted work.
#include <algorithm>
#include <vector>

constexpr unsigned char kTerrainSkipChunk = 255;   // a tile that is not drawn in this view (same value as Model::kSkipMesh)

// The four edges of a tile; the values are bits of the mask, in the order the mesh stores its skirt parts.
enum TerrainEdgeBit : unsigned char {
    kEdgeNorth = 1,   // the tile's z = 0 side: the neighbour is chunkZ - 1
    kEdgeSouth = 2,   // the other z side: chunkZ + 1
    kEdgeWest = 4,    // the x = 0 side: chunkX - 1
    kEdgeEast = 8,    // the other x side: chunkX + 1
};

// WALLS (roadmap X1). The outer edge of a BOUNDED world used to end in a skirt a few metres deep: from the side the world was a
// thin sheet floating in the void. A wall is the same skirt taken all the way down to a floor below the lowest ground, so the
// world reads as a block. Only the edges that are the world's edge get one (never the seams between tiles).
struct TerrainWalls {
    unsigned char edges = 0;   // TerrainEdgeBit mask of the TILE's sides that are the edge of the world
    float floorY = 0.0f;       // the y (metres, in the tile's own space) the wall goes down to
    bool cap = false;          // the tile also has a bottom at the floor, so the block is closed from below (every tile of a bounded world)
};

// The bottom of a chunk is part 5 of its mesh (after the surface and the four skirts): two triangles on four extra vertices at the floor,
// facing down. A tile of a bounded world draws it; every other tile leaves this bit of the part mask clear.
constexpr unsigned char kTerrainCapPartBit = 16;

// Where the floor is: below the lowest ground (height 0), by a tenth of the world's height, at least 10 m.
inline float WallFloorFor(float heightScale) { return -(std::max)(10.0f, heightScale * 0.1f); }

// Which edges of ONE CHUNK (the block x0..x1, z0..z1 of a tile of cellsX x cellsZ cells) are walls: the chunk must touch that side of the tile.
inline unsigned char ChunkWallEdges(unsigned char tileEdges, int x0, int z0, int x1, int z1, int cellsX, int cellsZ)
{
    unsigned char mask = 0;
    if ((tileEdges & kEdgeNorth) && z0 == 0) mask |= kEdgeNorth;
    if ((tileEdges & kEdgeSouth) && z1 == cellsZ) mask |= kEdgeSouth;
    if ((tileEdges & kEdgeWest) && x0 == 0) mask |= kEdgeWest;
    if ((tileEdges & kEdgeEast) && x1 == cellsX) mask |= kEdgeEast;
    return mask;
}

// `lods` holds one byte per tile, row by row (index = chunkZ * chunksX + chunkX): the level of detail the tile is
// drawn at in this view, or kTerrainSkipChunk if it is not drawn.
inline unsigned char ComputeSkirtMask(int chunkX, int chunkZ, int chunksX, int chunksZ, const std::vector<unsigned char>& lods)
{
    const unsigned char own = lods[static_cast<size_t>(chunkZ) * chunksX + chunkX];
    if (own == kTerrainSkipChunk) return 0;

    // For one side: is there a neighbour, and does it need us to cover a crack?
    auto edge = [&](int dx, int dz, TerrainEdgeBit bit) -> unsigned char {
        const int nx = chunkX + dx, nz = chunkZ + dz;
        if (nx < 0 || nz < 0 || nx >= chunksX || nz >= chunksZ) return bit;   // the edge of the whole terrain: seal it (visible from the side)
        const unsigned char neighbour = lods[static_cast<size_t>(nz) * chunksX + nx];
        if (neighbour == kTerrainSkipChunk) return 0;                          // not drawn in this view: nothing to crack against
        return neighbour != own ? bit : 0;                                     // different detail: edge vertices differ: cover it
    };
    return static_cast<unsigned char>(edge(0, -1, kEdgeNorth) | edge(0, 1, kEdgeSouth) | edge(-1, 0, kEdgeWest) | edge(1, 0, kEdgeEast));
}
