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

// --- asking the ground at ONE point (roadmap C1) -----------------------------------------------------------------------------------
// The same function, for collision, placing things on the ground, a character's feet, a line of sight: it answers for every point of
// the world, whether or not a tile is loaded there. It reads the SAME triangles a tile's mesh is drawn from at full detail (each
// cell is split along the diagonal from its south-west to its north-east corner: the triangles of Heightfield::Locate), so what it
// says is what the player sees standing on it, and it matches Terrain::GetHeightAt / GetNormalAt on a one-tile world.
struct WorldVec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct WorldGroundFunction {
    const WorldHeightSource* source = nullptr;          // shape and edits over the base ground (not owned: it must outlive this struct)
    const std::vector<WorldRiver>* rivers = nullptr;    // may be null
    WorldGridSettings grid;
    WorldBounds bounds;
    float heightScale = 1.0f;

    bool IsValid() const { return source != nullptr; }
    // Is the point inside the world? Always true in an endless world; inside the edge of a bounded one.
    bool HasGroundAt(double worldX, double worldZ) const;
    // The ground's height in metres. False beyond the edge of a bounded world.
    bool HeightAt(double worldX, double worldZ, float& heightMetres) const;
    // The unit normal, blended from the normals of the triangle's three vertices exactly as Terrain::GetNormalAt does.
    bool NormalAt(double worldX, double worldZ, WorldVec3& normal) const;
    // Where a ray first goes from above the ground to below it, within maxDistance. `direction` need not be normalised. A ray that
    // starts below the ground, or leaves the world before it meets the ground, does not hit. It walks the ray in half-cell steps and
    // then narrows the crossing down by bisection, so it finds the surface to well under a millimetre, but a ridge thinner than half
    // a cell that the ray clips between two steps is missed (a mesh has no such ridge: its features are a cell wide).
    bool Raycast(const WorldVec3& origin, const WorldVec3& direction, float maxDistance, float& distance) const;

private:
    // The heights in metres of a count x count block of lattice points whose first is (firstX, firstZ), rivers dug in.
    std::vector<float> Lattice(std::int64_t firstX, std::int64_t firstZ, int count) const;
};

// The ground over the rectangle minX..maxX, minZ..maxZ (world metres; grown by one sample all round). The samples sit on the
// world's own lattice (multiples of `step`), so at full resolution each one is exactly the height a tile's vertex has. A
// rectangle that would need more than maxSamplesPerSide samples across gets a step that is a whole multiple of the lattice.
// `rivers` may be null. A point beyond the edge of a bounded world has no ground.
WorldGroundGrid BuildWorldGroundGrid(const WorldHeightSource& source, const std::vector<WorldRiver>* rivers, const WorldGridSettings& grid,
                                     const WorldBounds& bounds, float heightScale, double minX, double maxX, double minZ, double maxZ,
                                     int maxSamplesPerSide = 1024);
