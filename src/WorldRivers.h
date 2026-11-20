#pragma once
// Rivers that are part of the WORLD, not of one tile (lesson 51). Pure maths: tests/world_rivers_test.cpp.
//
// THE PROBLEM. Tiles are made from a height function and thrown away when they stream out. A river carved into a tile's
// heights (the way a single terrain is edited) would vanish with it, and a river crossing a tile border would be cut in two.
// So a river is not an edit of heights: it is part of the FUNCTION. The ground at a point is the world's base height, dug by
// every river near it. Any tile that needs that point computes the same answer, so a river is the same whichever tile (at
// whichever level of detail) draws it, survives streaming, crosses borders without a seam, and is saved as a few numbers.
//
// WHY THIS WORKS. The carve (RiverCarve.h) is already a pure function of the distance to the river's middle line and of the
// ground there: "never higher than the old ground, and the limit depends only on the distance, never on the ground". Here it
// is applied to a block of samples at exact world positions, the same way CarveRiver applies it to a heightfield.
#include "RiverCarve.h"
#include "WorldGrid.h"
#include <cstdint>
#include <vector>

struct WorldRiver {
    int id = 0;                          // names the river for undo and for the editor; 0 = not given one yet
    std::vector<RiverPath::Sample> path; // x, z in WORLD metres; halfWidth in metres; level = the water's fall at this point, a SAMPLE value
    RiverCarveSettings carve;            // level, depth, bankHeight as SAMPLE values (metres / heightScale); bankWidth in metres

    // The box (world metres) this river can change: its path grown by half its widest width plus the bank. False if it has no path.
    bool Bounds(double& minX, double& maxX, double& minZ, double& maxZ) const;
};

// Digs the rivers into a block of samples (row by row, countX x countZ) whose sample (i, j) is at world position
// ((firstX + i) * step, (firstZ + j) * step). The rivers are applied in order; every height stays in 0..1.
// A sample's result depends only on its world position and the rivers, never on the block around it.
void CarveRiversIntoBlock(std::vector<float>& samples, int countX, int countZ, std::int64_t firstX, std::int64_t firstZ, double step,
                          const std::vector<WorldRiver>& rivers);

// Can this river change anything in (or next to) this tile? Used to rebuild only the tiles a new river touches.
bool RiverTouchesTile(const WorldGridSettings& grid, TileCoord tile, const WorldRiver& river);

// The heights of a tile and its apron (one sample ring outside it) with the rivers dug in. With no rivers near the tile this is
// exactly GenerateTileSamples and GenerateTileApron. `rivers` may be null.
struct TileHeights {
    std::vector<float> samples;
    HeightfieldApron apron;
};
TileHeights GenerateTileHeights(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile, const std::vector<WorldRiver>* rivers);

// Only a block of that tile (tile-local indices, apron included: see TileSampleRect), row by row, Width() x Height() values, with
// the rivers dug in. Each value is EXACTLY the one GenerateTileHeights gives for that sample: a sample depends only on its world
// position and the rivers, never on the block around it. This is what lets a sculpt stroke rewrite a few samples of a loaded
// tile instead of building the whole tile again (roadmap T2).
std::vector<float> GenerateTileBlock(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile, const std::vector<WorldRiver>* rivers, const TileSampleRect& rect);
