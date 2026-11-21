#pragma once
// EDITS OF A WORLD (phases 3 and 4 of docs/ROADMAP.md): sculpting and painting stored PER TILE, and the migration of an old
// single terrain into them. Plain maths and a small file format: no Direct3D. Tests: tests/world_edits_test.cpp.
//
// THE IDEA. The ground of a world is a function of the world position (source -> shape -> EDITS -> rivers). An edit is therefore not
// "this tile's vertex buffer was changed" but a small difference laid over the function:
//   * a HEIGHT DELTA per sample of the finest grid (the visible height is clamp(ground + delta, 0, 1)), and
//   * painted LAYER WEIGHTS per cell of the finest grid (the same four numbers per texel as the old terrain's PaintMap).
// Both live on the world's fine LATTICE: sample (i, j) is at world position (i, j) x metresPerCell, for any integer i and j. A sample
// belongs to exactly ONE tile (tile = floor(i / cellsPerTile)), so the shared edge between two tiles is stored once and cannot disagree.
// A coarser tile (level of detail) reads every 2nd, 4th or 8th lattice point, so tiles of different detail still meet.
//
// Edits sit BEFORE the rivers: a river is dug through sculpted ground, whatever order the two were made in.
// Changing the grid (metres per cell, chunks per tile) invalidates the lattice, so the world clears its edits then (the editor locks those
// controls while the world is edited, like the old terrain's mesh resolution).
#include "HeightfieldEdit.h"
#include "TerrainPaint.h"
#include "WorldGrid.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

using LatticeIndex = std::int64_t;

inline LatticeIndex FloorDivide(LatticeIndex a, LatticeIndex b) { LatticeIndex q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
inline LatticeIndex CeilDivide(LatticeIndex a, LatticeIndex b) { return -FloorDivide(-a, b); }

// A block of lattice points, inclusive on both ends. Empty when it ends before it starts.
struct LatticeRect {
    LatticeIndex minX = 0, minZ = 0, maxX = -1, maxZ = -1;
    bool IsEmpty() const { return maxX < minX || maxZ < minZ; }
    void Include(LatticeIndex x, LatticeIndex z);
};

// What a stroke changed, for undo and redo: the delta (heights) or the four weights (paint) before and after.
struct HeightEdit { LatticeIndex x = 0, z = 0; float before = 0.0f, after = 0.0f; };
struct PaintEdit { LatticeIndex x = 0, z = 0; float before[4] = {}, after[4] = {}; };
struct WorldEditPatch {
    std::vector<HeightEdit> heights;
    std::vector<PaintEdit> paints;
    bool IsEmpty() const { return heights.empty() && paints.empty(); }
};

class WorldEdits {
public:
    explicit WorldEdits(int cellsPerTile = 256) { Reset(cellsPerTile); }
    void Reset(int cellsPerTile);   // forgets every edit and sets the tile size of the lattice
    int CellsPerTile() const { return cellsPerTile_; }
    bool IsEmpty() const { return blocks_.empty(); }
    bool HasHeightEdits() const;
    bool HasPaint() const;
    size_t BlockCount() const { return blocks_.size(); }
    // Bumps on every change, so a caller can tell "the edits changed" without comparing them.
    unsigned Revision() const { return revision_; }

    // --- heights ---
    float HeightDelta(LatticeIndex x, LatticeIndex z) const;
    void SetHeightDelta(LatticeIndex x, LatticeIndex z, float delta);   // |delta| below 1e-7 counts as none

    // --- paint (one texel per CELL, four weights; cell (i, j) covers lattice points i..i+1) ---
    void GetPaint(LatticeIndex x, LatticeIndex z, float out[4]) const;
    void SetPaint(LatticeIndex x, LatticeIndex z, const float in[4]);

    bool TileHasHeightEdits(TileCoord tile) const;
    bool TileHasPaint(TileCoord tile) const;
    // The paint of one tile for a grid of `cellsAtLod` cells across (a divisor of CellsPerTile, for a coarser level of detail): each
    // coarse texel is the average of the fine cells it covers. An empty map when the tile has no paint.
    PaintMap PaintForTile(TileCoord tile, int cellsAtLod) const;

    // The tiles whose ground can have changed when the lattice points in `rect` changed: for heights every tile that reads those points,
    // including the one-sample apron and the points a coarse level of detail reaches over (up to 8 points); for paint only the owner.
    std::vector<TileCoord> TilesAffectedBy(const LatticeRect& rect, bool heights) const;

    // --- saving ---
    struct Block { std::vector<float> height; std::vector<float> paint; };   // height: cells x cells; paint: cells x cells x 4 (either may be empty)
    void ForEachBlock(const std::function<void(TileCoord, const Block&)>& visit) const;
    void SetBlock(TileCoord tile, Block block);

private:
    static std::uint64_t Key(int tx, int tz) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tx)) << 32) | static_cast<std::uint32_t>(tz); }
    const Block* Find(LatticeIndex x, LatticeIndex z, int& localX, int& localZ) const;
    Block& Touch(LatticeIndex x, LatticeIndex z, int& localX, int& localZ);

    int cellsPerTile_ = 256;
    unsigned revision_ = 0;
    std::unordered_map<std::uint64_t, Block> blocks_;
};

// Wraps a ground source and lays the edits over it: clamp(base + delta, 0, 1). Evaluated at the lattice points tiles ask for; between
// them (never asked by tiles) it blends the four neighbours. `edits` is shared and live: change it, then rebuild the tiles it touched.
class EditedHeightSource final : public WorldHeightSource {
public:
    EditedHeightSource(std::shared_ptr<const WorldHeightSource> base, std::shared_ptr<const WorldEdits> edits, double metresPerCell);
    float Height01(double worldX, double worldZ) const override;
private:
    std::shared_ptr<const WorldHeightSource> base_;
    std::shared_ptr<const WorldEdits> edits_;
    double metresPerCell_;
};

// --- brushes on the world -------------------------------------------------------------------------------------------------
// The same brushes as the old terrain (same rates, same falloff, same two-pass smoothing) but on the lattice, so a stroke across a
// tile border behaves as one stroke on one big terrain. Radii are in METRES. `base` is the ground WITHOUT the edits (and without rivers).
class WorldStrokeRecorder {
public:
    void Clear() { heights_.clear(); paints_.clear(); }
    bool IsEmpty() const { return heights_.empty() && paints_.empty(); }
    void RememberHeight(LatticeIndex x, LatticeIndex z, float deltaBefore);   // only the first call per point counts
    void RememberPaint(LatticeIndex x, LatticeIndex z, const float before[4]);
    WorldEditPatch Finish(const WorldEdits& edits) const;   // points that ended where they started are left out
private:
    std::map<std::pair<LatticeIndex, LatticeIndex>, float> heights_;
    std::map<std::pair<LatticeIndex, LatticeIndex>, std::array<float, 4>> paints_;
};

LatticeRect ApplyWorldHeightBrush(WorldEdits& edits, const WorldHeightSource& base, double metresPerCell, const BrushSettings& brush,
                                  double worldX, double worldZ, float seconds, WorldStrokeRecorder* recorder);
LatticeRect ApplyWorldPaintBrush(WorldEdits& edits, double metresPerCell, const PaintBrush& brush, double worldX, double worldZ,
                                 float seconds, WorldStrokeRecorder* recorder);

struct WorldPatchExtent { LatticeRect heights, paint; };
// Writes a stroke back (undo = the "before" values, redo = "after"). Returns what it touched, to know which tiles to rebuild.
WorldPatchExtent ApplyWorldEditPatch(WorldEdits& edits, const WorldEditPatch& patch, bool useAfter);

// --- migrating an old single terrain ----------------------------------------------------------------------------------------
// Where the old terrain stood and how its grid was laid out (all sizes already multiplied by its scale).
struct OldTerrainPlacement {
    double centerX = 0.0, centerZ = 0.0;   // its position in the world: the terrain is centred on it
    double sizeX = 0.0, sizeZ = 0.0;       // metres
    int cellsX = 0, cellsZ = 0;
};
struct MigrationStats { int heightSamples = 0; int paintCells = 0; };

// Writes into `edits` the difference between the old terrain's heights (cellsX + 1 by cellsZ + 1 samples, 0..1) and what `base` gives at
// the same world positions. Where the world's lattice meets the old grid exactly (same cell size and alignment) the old heights are
// reproduced sample for sample; elsewhere they are blended bilinearly. Differences below `threshold` (a 16-bit step is 1.5e-5) are not
// stored, so an untouched terrain creates no edits at all.
MigrationStats MigrateHeightsIntoEdits(const std::vector<float>& heights, const OldTerrainPlacement& placement, const WorldHeightSource& base,
                                       double metresPerCell, WorldEdits& edits, float threshold = 1e-5f);
// Copies the old terrain's painted layer weights (one texel per cell) onto the lattice cells that lie inside it.
MigrationStats MigratePaintIntoEdits(const PaintMap& paint, const OldTerrainPlacement& placement, double metresPerCell, WorldEdits& edits);

// --- file ------------------------------------------------------------------------------------------------------------------
// One small binary file per scene: header, then per edited tile its heights (float32) and paint (one byte per weight, like the old paint PNG).
bool SaveWorldEdits(const std::string& path, const WorldEdits& edits, std::string& error);
bool LoadWorldEdits(const std::string& path, WorldEdits& edits, std::string& error);
