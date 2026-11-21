#pragma once
// The ground of a world as a plain grid of heights in metres, made from the world's GROUND FUNCTION (source -> shape -> edits ->
// rivers: decision 3 of docs/ROADMAP.md) and not from whichever tile meshes happen to be loaded. Pure C++, no Direct3D.
//
// Why it exists (roadmap W1): the water measures the ground under it to know where it is deep and where it is dry. It used to ask
// the loaded tile meshes, and "no tile here" meant "100 m of open ocean". A tile that had not streamed in yet, a tile being
// replaced, and a tile at a coarser level of detail all gave the water a wrong answer. The ground function has an answer for
// every point of the world, loaded or not.
#include "WorldGrid.h"
#include "WorldRivers.h"
#include <vector>

struct WorldGroundGrid {
    double originX = 0.0, originZ = 0.0;   // the world position of sample (0, 0)
    double step = 1.0;                     // metres between samples
    int countX = 0, countZ = 0;
    std::vector<float> metres;             // the ground's height in metres, row by row (index = z * countX + x)
    std::vector<unsigned char> hasGround;  // 0 = beyond the edge of a bounded world

    bool IsEmpty() const { return metres.empty(); }
    // Bilinear between the four samples around the point, like the mesh between its vertices. False when the point is outside the
    // grid, or one of the four samples lies beyond the world's edge.
    bool Sample(double worldX, double worldZ, float& heightMetres) const;
};

// The ground over the rectangle minX..maxX, minZ..maxZ (world metres; grown by one sample all round). The samples sit on the
// world's own lattice (multiples of `step`), so at full resolution each one is exactly the height a tile's vertex has. A
// rectangle that would need more than maxSamplesPerSide samples across gets a step that is a whole multiple of the lattice.
// `rivers` may be null. A point beyond the edge of a bounded world has no ground.
WorldGroundGrid BuildWorldGroundGrid(const WorldHeightSource& source, const std::vector<WorldRiver>* rivers, const WorldGridSettings& grid,
                                     const WorldBounds& bounds, float heightScale, double minX, double maxX, double minZ, double maxZ,
                                     int maxSamplesPerSide = 1024);
