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
//
// VARIANTS (lesson 36). One species can have several looks: three pines, a dead pine, a mossy and a plain boulder. Every
// surviving candidate also rolls for WHICH look it gets: a weighted pick among the variants that the ground allows (a
// cliff fragment only on steep ground, a mossy rock mostly on grass). The roll is one more hash of the same square, so it
// is as repeatable as everything else. The GPU instance stays 32 bytes: the variant number travels in a parallel array
// that the caller uses to sort instances into one buffer range per variant.
#include "Heightfield.h"
#include "TerrainSplat.h"
#include <array>
#include <cstdint>
#include <vector>

// One placed object. 32 bytes: the instance buffer is an array of these (shaders/scatter_vs.hlsl reads INSTPOS and INSTROT).
struct ScatterInstance {
    float x = 0.0f, y = 0.0f, z = 0.0f;    // world position of the model's BASE (its pivot)
    float scale = 1.0f;                    // uniform size
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;   // rotation, a unit quaternion
};
static_assert(sizeof(ScatterInstance) == 32, "the GPU instance layout (INSTPOS + INSTROT) is exactly 32 bytes");

// The most looks one species can have. More than this is what a second species is for.
constexpr int kMaxScatterVariants = 8;

// The numbers a variant shares between the scene file / editor (ScatterVariantSettings) and the placement (ScatterVariantRules).
// Declared ONCE, here; both structs inherit it. Add a variant number here and it is in both, in the JSON, and in the clamp.
struct ScatterVariantBase {
    float weight = 1.0f;                                   // relative chance among the variants the ground allows; 0 = never
    std::array<float, kSplatChannels> layerBias = { 1.0f, 1.0f, 1.0f, 1.0f };   // multiplies the weight where the ground is grass / rock / sand / snow
    float minSlope = 0.0f;                                 // degrees: this variant only grows on ground in this slope band
    float maxSlope = 90.0f;
    float scaleMul = 1.0f;                                 // multiplies the species' scale range ("large" granite = 2)
};

// What distinguishes one variant of a species FOR THE PLACEMENT (its mesh and textures are not the placement's business).
struct ScatterVariantRules : ScatterVariantBase {
    float modelHeight = 1.0f;                              // this variant's mesh height in metres at scale 1 (only used by `sink`)
};

// The numbers of a species that say WHERE it grows and HOW each one looks. Declared ONCE, here: ScatterSpeciesSettings (the
// scene file and the editor) and ScatterRules (the placement) both inherit it, so a new rule is added in this struct and
// nowhere else in the data. (The JSON list in SceneJson.h, the clamp and operator== below, and the inspector still name it.)
struct ScatterSpeciesRules {
    // 1. the lattice
    float spacing = 6.0f;        // metres. One candidate per spacing x spacing square: at most 1 / spacing^2 objects per m^2
    float minDistance = 3.5f;    // metres. Two objects are never closer than this (clamped to spacing)
    // 2. what the ground says
    float density = 1.0f;        // 0..1: the chance a candidate on PERFECT ground is used
    std::array<float, kSplatChannels> layerDensity = { 1.0f, 0.0f, 0.0f, 0.0f };   // how much each terrain layer (grass, rock, sand, snow) likes it
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
};

// What one species asks of the ground. Plain data, no model and no paths: the editor edits it, the placement reads it.
struct ScatterRules : ScatterSpeciesRules {
    std::uint32_t seed = 1;      // a different seed is a different forest (made from the global seed, the species' seed and its place in the list)
    // 4. which look each one gets. variantCount 1 with the default variant is exactly the species of lesson 31.
    int variantCount = 1;
    ScatterVariantRules variants[kMaxScatterVariants];
};

// Are two sets of rules the same? ScatterObject asks this to decide whether a species must be placed again. Written out field by
// field (not memcmp, which also compares padding bytes and would be wrong the day a bool is added). The static_asserts are the
// guard: add a field to one of these structs and they stop the build until the comparison is updated too.
static_assert(sizeof(ScatterVariantBase) == (1 + kSplatChannels + 3) * sizeof(float), "ScatterVariantBase changed: update operator== below");
static_assert(sizeof(ScatterVariantRules) == sizeof(ScatterVariantBase) + sizeof(float), "ScatterVariantRules changed: update operator== below");
static_assert(sizeof(ScatterSpeciesRules) == (3 + kSplatChannels + 4 + 2 + 5) * sizeof(float), "ScatterSpeciesRules changed: update operator== below");
static_assert(sizeof(ScatterRules) == sizeof(ScatterSpeciesRules) + 2 * sizeof(float) + kMaxScatterVariants * sizeof(ScatterVariantRules), "ScatterRules changed: update operator== below");
inline bool operator==(const ScatterVariantBase& a, const ScatterVariantBase& b)
{
    return a.weight == b.weight && a.layerBias == b.layerBias && a.minSlope == b.minSlope && a.maxSlope == b.maxSlope && a.scaleMul == b.scaleMul;
}
inline bool operator==(const ScatterVariantRules& a, const ScatterVariantRules& b)
{
    return static_cast<const ScatterVariantBase&>(a) == static_cast<const ScatterVariantBase&>(b) && a.modelHeight == b.modelHeight;
}
inline bool operator==(const ScatterSpeciesRules& a, const ScatterSpeciesRules& b)
{
    return a.spacing == b.spacing && a.minDistance == b.minDistance && a.density == b.density && a.layerDensity == b.layerDensity &&
           a.minSlope == b.minSlope && a.maxSlope == b.maxSlope && a.minHeight == b.minHeight && a.maxHeight == b.maxHeight &&
           a.clusterStrength == b.clusterStrength && a.clusterScale == b.clusterScale && a.scaleMin == b.scaleMin &&
           a.scaleMax == b.scaleMax && a.alignToNormal == b.alignToNormal && a.tilt == b.tilt && a.sink == b.sink;
}
inline bool operator==(const ScatterRules& a, const ScatterRules& b)
{
    if (a.variantCount != b.variantCount || a.seed != b.seed) return false;
    for (int v = 0; v < a.variantCount && v < kMaxScatterVariants; ++v) if (!(a.variants[v] == b.variants[v])) return false;
    return static_cast<const ScatterSpeciesRules&>(a) == static_cast<const ScatterSpeciesRules&>(b);
}

// One clamp per struct, so the settings (the editor's numbers) and the placement (what it reads) can never disagree about a range.
void ClampScatterVariantBase(ScatterVariantBase& variant);
void ClampScatterSpeciesRules(ScatterSpeciesRules& rules);

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

// Which variant does a candidate on this ground get? `slopeDegrees` is the ground's slope there and `roll` a uniform random
// number in [0, 1). Returns an index below rules.variantCount, or -1 when no variant allows this ground (the candidate is
// then dropped: a species made only of cliff fragments grows nothing on a meadow). Exposed so tests and tools ask the
// same question the placement does.
int ChooseScatterVariant(const ScatterRules& rules, const ScatterSample& ground, float slopeDegrees, float roll);

// Makes the instances of ONE species inside ONE rectangle, appends them to `out` and returns how many were added.
// Deterministic: the same rules and the same ground always give the same instances, and the instances of a big
// rectangle are exactly the union of the instances of any tiling of it (no seams, nothing doubled).
// If `variantOut` is given, one byte per appended instance is appended to it: that instance's variant number.
int GenerateScatter(const ScatterGround& ground, const ScatterRules& rules, const ScatterRect& area, std::vector<ScatterInstance>& out,
                    std::vector<std::uint8_t>* variantOut = nullptr);

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

// --- Variants in the instance buffer ------------------------------------------------------------------------------------
// One species draws several meshes, but they all read ONE instance buffer. The buffer is laid out VARIANT-MAJOR: first all
// the instances of variant 0, cell after cell in grid order, then all those of variant 1, and so on. Neighbouring visible
// cells of one variant are then neighbours in the buffer too, so a whole stretch of forest stays one draw call per variant
// and level of detail (see AppendRun in LodSelect.h). These two functions are the index bookkeeping of that, kept free of
// Direct3D so they can be tested.

// Reorders one cell's instances so those of variant 0 come first, then variant 1, ... (stable: the order inside a variant
// is kept). `variants` has one entry per instance. On return offsets[v] .. offsets[v + 1] is variant v's slice, for
// v < variantCount; offsets[variantCount] is the instance count. Entries of `variants` that are >= variantCount count as 0.
void GroupScatterByVariant(std::vector<ScatterInstance>& instances, const std::vector<std::uint8_t>& variants, int variantCount,
                           std::uint32_t offsets[kMaxScatterVariants + 1]);

// How many instances of each variant a cell holds, and where those go in the shared buffer.
struct ScatterCellCounts { std::uint32_t count[kMaxScatterVariants] = {}; };
struct ScatterCellLayout {
    std::uint32_t start[kMaxScatterVariants] = {};   // first instance of this cell's slice of variant v, in the buffer
    std::uint32_t count[kMaxScatterVariants] = {};   // how many of its instances fit (the cap can cut the tail short)
};
// Fills `layout` (one entry per cell, same order as `cells`) and returns the number of instances the buffer holds.
// The buffer never holds more than `cap`: past it, the remaining slices are empty.
std::uint32_t PlanScatterLayout(const std::vector<ScatterCellCounts>& cells, int variantCount, std::uint32_t cap,
                                std::vector<ScatterCellLayout>& layout);
