#pragma once
// Where trees, rocks, bushes... stand on the terrain. Pure logic (no Direct3D, no DirectXMath), like Heightfield.h and
// TerrainSplat.h, so it is tested on its own: tests/scatter_placement_test.cpp.
//
// THE IDEA (lesson 31). Placing things by hand does not scale past a few hundred. Instead the artist describes WHAT may
// grow WHERE ("pines like grass, not steeper than 30 degrees, not above 120 m") and the computer decides every single
// position. Three questions, in order:
//   1. WHERE could something stand?  A lattice of squares, `spacing` metres wide, laid over the world. Each square holds
//      exactly one CANDIDATE at a random place inside it. (Random, but the random numbers come from a hash of the
//      square's index, not from a running generator: the same square always gives the same candidate. That is what makes
//      the result identical whether you generate the whole terrain at once or one little cell at a time.)
//   2. DOES the ground want it?  The candidate is kept if the terrain layer under it (the baked-and-painted splat weights
//      of lessons 29 and 30) likes this species, the slope and height are in range, and a dice roll against the density
//      passes. Paint grass, and trees appear; paint rock, and they do not.
//   3. IS there room?  Two kept candidates closer than `minDistance` cannot both stay: the one with the lower random
//      priority loses (a "Matern type II" thinning). Because priorities come from the same hash, the winner is the same
//      from either side of a cell border: no seams.
// The survivors become ScatterInstances: a position, a size and a rotation, 32 bytes each, exactly what the GPU reads.
#include "Heightfield.h"
#include "TerrainSplat.h"
#include <cstdint>
#include <vector>

// One placed object. 32 bytes: the instance buffer is an array of these (shaders/scatter_vs.hlsl reads INSTPOS and INSTROT).
struct ScatterInstance {
    float x = 0.0f, y = 0.0f, z = 0.0f;    // world position of the model's BASE (its pivot)
    float scale = 1.0f;                    // uniform size
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;   // rotation, a unit quaternion
};
static_assert(sizeof(ScatterInstance) == 32, "the GPU instance layout (INSTPOS + INSTROT) is exactly 32 bytes");

// What one species asks of the ground. Plain data, no model and no paths: the editor edits it, the placement reads it.
struct ScatterRules {
    // 1. the lattice
    float spacing = 6.0f;        // metres. One candidate per spacing x spacing square: at most 1 / spacing^2 objects per m^2
    float minDistance = 3.5f;    // metres. Two objects are never closer than this (clamped to spacing)
    // 2. what the ground says
    float density = 1.0f;        // 0..1: the chance a candidate on PERFECT ground is used
    float layerDensity[kSplatChannels] = { 1.0f, 0.0f, 0.0f, 0.0f };   // how much each terrain layer (grass, rock, sand, snow) likes it
    float minSlope = 0.0f;       // degrees from flat; rocks like slopes, trees do not
    float maxSlope = 35.0f;
    float minHeight = -1000.0f;  // world Y limits
    float maxHeight = 5000.0f;
    float clusterStrength = 0.0f;   // 0 = even, 1 = clumps with clearings (a low-frequency noise multiplies the density)
    float clusterScale = 40.0f;     // metres across one clump
    // 3. the look of each instance
    float scaleMin = 0.8f;
    float scaleMax = 1.3f;
    float alignToNormal = 0.0f;  // 0 = upright, 1 = leans with the ground (rocks)
    float tilt = 3.0f;           // degrees of random lean
    float sink = 0.0f;           // fraction of the model's height buried in the ground (rocks sit IN the ground)
    float modelHeight = 1.0f;    // the model's height at scale 1, in metres (only used by `sink`)
    std::uint32_t seed = 1;      // a different seed is a different forest
};

// Puts every value into a range the placement can work with (a spacing of 0 would mean infinitely many objects).
void ClampScatterRules(ScatterRules& rules);

// The terrain as the placement sees it. All pointers are borrowed and read-only.
struct ScatterGround {
    const Heightfield* field = nullptr;
    const SplatMap* splat = nullptr;   // the FINAL weights (baked rules + paint); null or empty = all grass
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;       // the terrain's position (its local origin) in the world
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f; // and its scale. (The terrain is never rotated.)
};
bool ScatterGroundIsValid(const ScatterGround& ground);

// A rectangle on the ground plane, in world X and Z. Half-open: minX <= x < maxX.
struct ScatterRect {
    float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
    bool IsEmpty() const { return maxX <= minX || maxZ <= minZ; }
};
// Where the terrain is, in world X and Z.
ScatterRect ScatterGroundExtent(const ScatterGround& ground);

// What the placement learns about the ground at one point. Exposed so tests (and tools) can ask the same question.
struct ScatterSample {
    float y = 0.0f;                         // world height of the ground
    float normal[3] = { 0.0f, 1.0f, 0.0f };   // unit WORLD-space normal (a stretched terrain has steeper normals)
    float weights[kSplatChannels] = { 1.0f, 0.0f, 0.0f, 0.0f };   // grass, rock, sand, snow: add up to 1
};
// False if (x, z) is off the terrain.
bool SampleScatterGround(const ScatterGround& ground, float x, float z, ScatterSample& out);

// Makes the instances of ONE species inside ONE rectangle, appends them to `out` and returns how many were added.
// Deterministic: the same rules and the same ground always give the same instances, and the instances of a big
// rectangle are exactly the union of the instances of any tiling of it (no seams, nothing doubled).
int GenerateScatter(const ScatterGround& ground, const ScatterRules& rules, const ScatterRect& area, std::vector<ScatterInstance>& out);

// --- The scatter grid: the unit of CULLING, of regeneration and of drawing -------------------------------------------
// Square cells of `cellSize` metres laid over the terrain, starting at its lower corner. A cell is regenerated alone
// when the ground under it changes (a sculpt or paint stroke), and is culled and level-of-detail-ed as one block.
struct ScatterGrid {
    float originX = 0.0f, originZ = 0.0f;
    float cellSize = 32.0f;
    int cellsX = 0, cellsZ = 0;
    int CellCount() const { return cellsX * cellsZ; }
    int IndexOf(int cx, int cz) const { return cz * cellsX + cx; }
};
ScatterGrid MakeScatterGrid(const ScatterRect& extent, float cellSize);
ScatterRect ScatterCellRect(const ScatterGrid& grid, int cx, int cz);
// The block of cells (inclusive) that a world rectangle touches, clamped to the grid. False if it touches none.
bool ScatterCellsTouching(const ScatterGrid& grid, const ScatterRect& rect, int& cx0, int& cz0, int& cx1, int& cz1);
