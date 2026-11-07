#pragma once
// Which skirts does a terrain tile need? Pure logic, no Direct3D, so it can be tested on its own.
//
// A SKIRT is a vertical strip of triangles hung below one edge of a tile to hide the crack that opens when two
// neighbouring tiles are drawn at different levels of detail (their edge vertices no longer line up). Two tiles at
// the SAME level share exactly the same edge vertices, so there is no crack and a skirt would be wasted work.
#include <vector>

constexpr unsigned char kTerrainSkipChunk = 255;   // a tile that is not drawn in this view (same value as Model::kSkipMesh)

// The four edges of a tile; the values are bits of the mask, in the order the mesh stores its skirt parts.
enum TerrainEdgeBit : unsigned char {
    kEdgeNorth = 1,   // the tile's z = 0 side: the neighbour is chunkZ - 1
    kEdgeSouth = 2,   // the other z side: chunkZ + 1
    kEdgeWest = 4,    // the x = 0 side: chunkX - 1
    kEdgeEast = 8,    // the other x side: chunkX + 1
};

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
