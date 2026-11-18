#pragma once
// The world as a grid of terrain tiles, as plain maths (no Direct3D, no DirectXMath): tests/world_grid_test.cpp.
// Design note: docs/terrain-world-design.md (roadmap 7b). Nothing in the engine uses this yet; the next patches build on it.
//
// THE IDEAS, in the order they depend on each other (lesson 47):
//
// 1. A TILE is the unit of data: tileCells x tileCells cells, each `metresPerCell` metres wide. Tile (x, z) covers the world
//    square [x * size, (x + 1) * size) in X and the same in Z, where size = tileCells * metresPerCell. Tile (0, 0) starts at
//    the world origin, and the numbers are integers that may be negative: the grid has no edge.
//
// 2. SHARED EDGES. A tile of N cells has N + 1 samples per side, and its last sample is the SAME POINT of the world as the
//    first sample of the next tile. We do not copy or stitch anything: both tiles ask the same question ("how high is the
//    ground at this world position?") and get the same answer. That only works if the answer depends on the WORLD
//    position and nothing else, which is why heights come from a WorldHeightSource (a function of world X and Z) and not
//    from a picture stretched over each tile.
//
// 3. NO FLOAT PRECISION LOSS. The world position of sample i of tile t is (t * cells + i) * metresPerCell. The integer part
//    is exact (a 64-bit number), and the multiplication is done in double, so the answer is exact enough at 1000 km from
//    the origin where a float would jitter by centimetres. Only a tile's own small local numbers ever reach the GPU.
//
// 4. THE APRON. Lighting needs each vertex's slope, which needs its neighbours, and on a tile's edge a neighbour is in the
//    next tile. GenerateTileApron reads those samples from the same height source, so two tiles agree on the normals
//    along their shared edge (Heightfield::SetApron).
#include "Heightfield.h"
#include <cstdint>
#include <vector>

constexpr int kWorldChunkCells = 32;   // must equal kTerrainChunkCells (TerrainChunks.h): a tile is a whole number of chunks

struct TileCoord {
    int x = 0;
    int z = 0;
    bool operator==(const TileCoord& other) const { return x == other.x && z == other.z; }
    bool operator!=(const TileCoord& other) const { return !(*this == other); }
};

struct WorldGridSettings {
    int chunksPerTile = 8;          // chunks along one side of a tile: 8 x 32 = 256 cells
    float metresPerCell = 4.0f;     // the fixed resolution of the whole world

    static constexpr int kChunksMin = 1, kChunksMax = 32;
    static constexpr float kMetresPerCellMin = 0.25f, kMetresPerCellMax = 32.0f;

    int TileCells() const { return chunksPerTile * kWorldChunkCells; }
    double TileSize() const { return static_cast<double>(TileCells()) * static_cast<double>(metresPerCell); }   // metres
    void Clamp();

    // LEVEL OF DETAIL OF A WHOLE TILE (lesson 50). Level L is the same square of the world with 2^L times fewer cells along each
    // side, each 2^L times wider: 8 chunks of 4 m, then 4 chunks of 8 m, 2 of 16 m, 1 of 32 m. All sizes are powers of two, so
    // the square is exactly the same size at every level, and a coarse tile's samples are a subset of the fine tile's: along a
    // border they meet at the same points (the test checks it bit for bit). Far tiles are a fraction of the work.
    static constexpr int kTileLodMax = 3;
    int MaxTileLod() const;                       // how many times this grid can be halved evenly (3 for the default 8 chunks of 4 m)
    WorldGridSettings ForLod(int level) const;    // the grid of a tile at that level (clamped to MaxTileLod)
};

// THE EDGE OF THE WORLD (phase 1 of docs/terrain-world-roadmap.md). A world is endless unless it is BOUNDED: a rectangle of
// tiles, given by its first and last tile (both inclusive, negative numbers allowed). No tile outside it is ever wanted, and a
// loaded one outside it is dropped. A small terrain is then a small world: the old single terrain is "one tile, heights from a
// picture" (roadmap, "Phasing out the single terrain").
struct WorldBounds {
    bool bounded = false;                       // false: the world has no edge and the numbers below are ignored
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0; // first and last tile, inclusive

    static constexpr int kMaxTilesAcross = 64;  // a bounded world is at most 64 x 64 tiles (it keeps the numbers small and the editor sane)

    bool Contains(TileCoord tile) const { return !bounded || (tile.x >= minX && tile.x <= maxX && tile.z >= minZ && tile.z <= maxZ); }
    int TilesAcrossX() const { return bounded ? maxX - minX + 1 : 0; }
    int TilesAcrossZ() const { return bounded ? maxZ - minZ + 1 : 0; }
    void Normalize();   // swaps first/last if they are the wrong way round and keeps each side within kMaxTilesAcross
    // The smallest bounded rectangle of tiles that covers the world rectangle [minXm, maxXm] x [minZm, maxZm] (metres). A
    // rectangle whose edge lies exactly on a tile border does not pull in the next tile.
    static WorldBounds Covering(const WorldGridSettings& grid, double minXm, double minZm, double maxXm, double maxZm);
};

// --- where things are -----------------------------------------------------------------------------------------------
TileCoord TileContaining(const WorldGridSettings& grid, double worldX, double worldZ);
void TileCenter(const WorldGridSettings& grid, TileCoord tile, double& worldX, double& worldZ);
// How far (metres) a point is from the tile's square; 0 when the point is inside it. This is what "distance to a tile"
// means for streaming and for choosing a level of detail: the nearest EDGE counts, not the middle.
double DistanceToTile(const WorldGridSettings& grid, TileCoord tile, double worldX, double worldZ);
// Every tile that comes within `radius` metres of the point, nearest first (ties broken by z then x, so the order is the
// same every time). The streaming list of a later patch.
std::vector<TileCoord> TilesWithin(const WorldGridSettings& grid, double worldX, double worldZ, double radius);

// --- streaming ------------------------------------------------------------------------------------------------------
// What to load and what to unload as the camera moves (lesson 49). A pure decision: the caller does the loading.
//  - A tile is WANTED when it comes within `viewDistance` of the camera.
//  - A loaded tile is only UNLOADED when it is farther than viewDistance + half a tile. The gap is the hysteresis: a camera
//    standing on the edge of the loaded area, or wobbling across it, would otherwise load and drop the same tile every frame.
//  - At most `maxTiles` stay loaded, nearest first.
//  - A tile's LEVEL OF DETAIL comes from its distance (WantedTileLod): near tiles are full, far tiles use fewer cells. Changing
//    level means building the tile again, so the level only changes when the distance is well past the boundary (hysteresis again).
struct LoadedTile {
    TileCoord coord;
    int lod = 0;
};
struct TileRequest {
    TileCoord coord;
    int lod = 0;
};
struct StreamingPlan {
    std::vector<TileRequest> toLoad;   // nearest first. A request for a tile that is already loaded at another level means "rebuild it"
    std::vector<TileCoord> toUnload;
};
//  - In a BOUNDED world (WorldBounds) a tile outside the bounds is never wanted, and a loaded one outside them is unloaded.
StreamingPlan PlanTileStreaming(const WorldGridSettings& grid, double cameraX, double cameraZ, double viewDistance, int maxTiles,
                                const std::vector<LoadedTile>& loaded, const WorldBounds& bounds = WorldBounds{});

// The level a tile should be at, given how far its nearest edge is from the camera. Level 1 starts 1.5 tiles out, level 2 at 3,
// level 3 at 6. `currentLod` is the level it is loaded at (-1 if it is not): it is kept until the distance is a quarter of a
// tile past the boundary, so a camera on a ring does not rebuild the same tile back and forth.
constexpr double kTileLodStartInTiles[WorldGridSettings::kTileLodMax] = { 1.5, 3.0, 6.0 };
int WantedTileLod(const WorldGridSettings& grid, double distance, int currentLod);

// --- what the ground is ---------------------------------------------------------------------------------------------
// The ground as a function of the world position alone. 0..1, like every height in the engine (times heightScale = metres).
class WorldHeightSource {
public:
    virtual ~WorldHeightSource() = default;
    virtual float Height01(double worldX, double worldZ) const = 0;
};

// Hills from layered value noise. `baseWavelength` is the size, in METRES, of the biggest features, so the same settings
// give the same hills whatever the tile size or the resolution is.
struct FractalWorldSettings {
    int seed = 0;
    int octaves = 6;
    float baseWavelength = 3000.0f;
};

class FractalHeightSource final : public WorldHeightSource {
public:
    explicit FractalHeightSource(const FractalWorldSettings& settings) : settings_(settings) {}
    float Height01(double worldX, double worldZ) const override;
private:
    FractalWorldSettings settings_;
};

// The world position (metres, along one axis) of sample `local` of tile `tile` (local may be -1 or cells + 1: the apron). Exact
// integer arithmetic, then one multiplication in double: every tile that shares the sample gets the same number.
double WorldSamplePosition(const WorldGridSettings& grid, int tile, int local);

// --- making a tile --------------------------------------------------------------------------------------------------
// The samples of one tile: (cells + 1) x (cells + 1), row by row (z-major, the same layout as Heightfield::Reset takes).
std::vector<float> GenerateTileSamples(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile);
// The ring just outside that tile, read from the same source (see "THE APRON" above).
HeightfieldApron GenerateTileApron(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile);
// The layout to give Heightfield::Reset for a tile of this world.
HeightfieldLayout TileLayout(const WorldGridSettings& grid, float heightScale);
