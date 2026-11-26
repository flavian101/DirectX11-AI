#pragma once
// The heights of one TILE of a world, made from the world's ground function (roadmap H7). Pure maths: no Direct3D.
//
// A tile is thrown away when it streams out and made again when it streams in, so it keeps nothing of its own: its samples (and the ring of
// samples just outside it, the apron, that lets neighbouring tiles agree on their normals) are asked of the ground function at exact world
// positions. Any tile that needs a point gets the same answer for it, which is why tiles meet without seams at any level of detail.
#include "WorldGrid.h"
#include <vector>

struct TileHeights {
    std::vector<float> samples;
    HeightfieldApron apron;
};

// The heights of a tile and its apron (one sample ring outside it): exactly GenerateTileSamples and GenerateTileApron.
TileHeights GenerateTileHeights(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile);

// Only a block of that tile (tile-local indices, apron included: see TileSampleRect), row by row, Width() x Height() values. Each value is
// EXACTLY the one GenerateTileHeights gives for that sample: a sample depends only on its world position, never on the block around it. This is
// what lets a sculpt stroke rewrite a few samples of a loaded tile instead of building the whole tile again (roadmap T2).
std::vector<float> GenerateTileBlock(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile, const TileSampleRect& rect);
