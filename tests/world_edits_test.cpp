// Tests for src/WorldEdits.{h,cpp}: sculpting and painting stored per tile, and the migration of an old single terrain. No Direct3D.
//   1. The store: points across tile borders (negative too), ownership, nothing created for nothing.
//   2. EditedHeightSource: no edits = the base's bits; tiles of every level of detail still meet after edits.
//   3. BRUSH PARITY: the world's four brushes give the old terrain's heights, and a stroke over a tile border equals the same stroke
//      on one big tile (the lattice does not care how it is cut into tiles).
//   4. Undo / redo patches.   5. Paint parity and the coarse-level average.   6. Which tiles to rebuild.
//   7. MIGRATION: a sculpted and painted old terrain becomes edits that give back its heights sample for sample; an untouched one makes none.
//   8. The file.
#include "WorldEdits.h"
#include "WorldPicture.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(double a, double b, double tolerance = 1e-5) { return std::fabs(a - b) <= tolerance; }

static std::shared_ptr<const WorldHeightSource> MakeGround(int size, double metres)
{
    auto image = std::make_shared<HeightImage>();
    image->width = image->height = size;
    image->values.resize(static_cast<size_t>(size) * size);
    for (size_t i = 0; i < image->values.size(); ++i) image->values[i] = 0.2f + 0.6f * std::fmod(static_cast<float>(i) * 0.6180339f, 1.0f);
    return std::make_shared<const PictureHeightSource>(image, 0.0, 0.0, metres, metres);
}

static void TestStore()
{
    WorldEdits edits(32);
    CHECK(edits.IsEmpty() && edits.HeightDelta(5, 5) == 0.0f && edits.HeightDelta(-100, 900) == 0.0f);
    edits.SetHeightDelta(0, 0, 0.0f);
    edits.SetHeightDelta(7, 7, 0.0f);
    CHECK(edits.IsEmpty());   // clearing what holds nothing creates nothing

    edits.SetHeightDelta(31, 0, 0.25f);    // last sample of tile (0,0)
    edits.SetHeightDelta(32, 0, -0.5f);    // first sample of tile (1,0): its own block, no duplicate of the border
    edits.SetHeightDelta(-1, -1, 0.125f);  // tile (-1,-1)
    CHECK(edits.HeightDelta(31, 0) == 0.25f && edits.HeightDelta(32, 0) == -0.5f && edits.HeightDelta(-1, -1) == 0.125f);
    CHECK(edits.BlockCount() == 3);
    CHECK(edits.TileHasHeightEdits({ 0, 0 }) && edits.TileHasHeightEdits({ 1, 0 }) && edits.TileHasHeightEdits({ -1, -1 }) && !edits.TileHasHeightEdits({ 2, 2 }));
    CHECK(edits.HasHeightEdits() && !edits.HasPaint());

    const float w[4] = { 0.0f, 0.5f, 0.0f, 0.25f };
    edits.SetPaint(-33, 40, w);
    float got[4];
    edits.GetPaint(-33, 40, got);
    CHECK(got[1] == 0.5f && got[3] == 0.25f && edits.TileHasPaint({ -2, 1 }) && edits.HasPaint());
    edits.GetPaint(0, 0, got);
    CHECK(got[0] == 0.0f && got[1] == 0.0f);

    CHECK(FloorDivide(-1, 32) == -1 && FloorDivide(31, 32) == 0 && FloorDivide(-32, 32) == -1 && FloorDivide(-33, 32) == -2);
    CHECK(CeilDivide(1, 32) == 1 && CeilDivide(-1, 32) == 0 && CeilDivide(32, 32) == 1);
}

static void TestEditedSourceAndTiles()
{
    const auto ground = MakeGround(40, 512.0);
    auto edits = std::make_shared<WorldEdits>(64);
    const EditedHeightSource none(ground, edits, 4.0);
    bool exact = true;
    for (double x = 0.0; x < 512.0; x += 4.0) for (double z = 0.0; z < 512.0; z += 12.0) if (none.Height01(x, z) != ground->Height01(x, z)) exact = false;
    CHECK(exact);   // no edits: the base's own bits

    WorldGridSettings grid;
    grid.chunksPerTile = 2; grid.metresPerCell = 4.0f;   // 64 cells, 256 m tiles; can be halved (levels of detail)
    CHECK(grid.TileCells() == 64 && grid.MaxTileLod() >= 1);
    // Edits across the border between tile (0,0) and (1,0), at x = 256 m = lattice 64.
    for (int z = 4; z < 56; ++z) for (int x = 56; x < 72; ++x) edits->SetHeightDelta(x, z, 0.1f);
    edits->SetHeightDelta(64, 10, 5.0f);   // clamps: ground + 5 is above 1
    const EditedHeightSource edited(ground, edits, 4.0);
    CHECK(Near(edited.Height01(64 * 4.0, 5 * 4.0), std::clamp(ground->Height01(256.0, 20.0) + 0.1f, 0.0f, 1.0f)));
    CHECK(edited.Height01(64 * 4.0, 10 * 4.0) == 1.0f);

    const std::vector<float> left = GenerateTileSamples(edited, grid, { 0, 0 });
    const std::vector<float> right = GenerateTileSamples(edited, grid, { 1, 0 });
    const int cells = grid.TileCells();
    bool same = true;
    for (int i = 0; i <= cells; ++i) if (left[static_cast<size_t>(i) * (cells + 1) + cells] != right[static_cast<size_t>(i) * (cells + 1)]) same = false;
    CHECK(same);   // the shared border row comes from one stored value
    // A coarser tile reads a subset of the fine lattice, so it meets the fine tile.
    const WorldGridSettings lod1 = grid.ForLod(1);
    const std::vector<float> coarse = GenerateTileSamples(edited, lod1, { 0, 0 });
    const int coarseCells = lod1.TileCells();
    bool subset = coarseCells * 2 == cells;
    for (int z = 0; subset && z <= coarseCells; ++z)
        for (int x = 0; x <= coarseCells; ++x)
            if (coarse[static_cast<size_t>(z) * (coarseCells + 1) + x] != left[static_cast<size_t>(z * 2) * (cells + 1) + x * 2]) subset = false;
    CHECK(subset);

    // Between lattice points (never asked by tiles) it blends the neighbours' deltas: the edit of 0.1 is still 0.1 in the middle of the patch.
    const float groundMid = ground->Height01(60.5 * 4.0, 30.0 * 4.0);
    CHECK(Near(edited.Height01(60.5 * 4.0, 30.0 * 4.0) - groundMid, 0.1f, 1e-5));
}

// The old terrain, for comparison: a Heightfield filled from the ground at the lattice points.
struct OldField { Heightfield field; HeightfieldLayout layout; };
static OldField MakeOld(const WorldHeightSource& ground, int cells, double mpc, double centreX, double centreZ)
{
    OldField old;
    old.layout.cellsX = old.layout.cellsZ = cells;
    old.layout.sizeX = old.layout.sizeZ = static_cast<float>(cells * mpc);
    old.layout.heightScale = 100.0f;
    std::vector<float> samples(static_cast<size_t>(cells + 1) * (cells + 1));
    const double minX = centreX - 0.5 * cells * mpc, minZ = centreZ - 0.5 * cells * mpc;
    for (int z = 0; z <= cells; ++z) for (int x = 0; x <= cells; ++x) samples[static_cast<size_t>(z) * (cells + 1) + x] = ground.Height01(minX + x * mpc, minZ + z * mpc);
    old.field.Reset(old.layout, std::move(samples));
    return old;
}

static void TestBrushParity()
{
    const double mpc = 2.0;
    const int cells = 96;                      // the old terrain: 96 cells of 2 m, its corner at the world origin, so lattice (i, j) = sample (i, j)
    const auto ground = MakeGround(50, cells * mpc);
    const double centre = 0.5 * cells * mpc;

    const BrushMode modes[] = { BrushMode::Raise, BrushMode::Lower, BrushMode::Smooth, BrushMode::Flatten };
    for (const BrushMode mode : modes) {
        OldField old = MakeOld(*ground, cells, mpc, centre, centre);
        WorldEdits edits(32);
        BrushSettings brush;
        brush.mode = mode; brush.radius = 18.0f; brush.strength = 0.7f; brush.falloff = 0.5f; brush.targetHeight = 0.6f;
        // A short stroke through the middle of the field, away from its edges (where the old brush clamps and the world has no edge).
        double worst = 0.0;
        for (int step = 0; step < 6; ++step) {
            const double wx = 80.0 + step * 6.0, wz = 90.0 + step * 3.0;
            ApplyBrush(old.field, brush, static_cast<float>(wx - centre), static_cast<float>(wz - centre), 0.05f, nullptr);
            ApplyWorldHeightBrush(edits, *ground, mpc, brush, wx, wz, 0.05f, nullptr);
        }
        for (int z = 0; z <= cells; ++z) {
            for (int x = 0; x <= cells; ++x) {
                const float world = std::clamp(ground->Height01(x * mpc, z * mpc) + edits.HeightDelta(x, z), 0.0f, 1.0f);
                worst = (std::max)(worst, static_cast<double>(std::fabs(world - old.field.Sample(x, z))));
            }
        }
        CHECK(worst < 2e-4);   // the same maths: float vs double rounding is all that differs
    }

    // A stroke across a tile border equals the same stroke on one big tile: the lattice does not care how it is cut.
    WorldEdits small(32), big(4096);
    BrushSettings brush;
    brush.mode = BrushMode::Raise; brush.radius = 30.0f; brush.strength = 0.9f;
    for (int step = 0; step < 4; ++step) {
        ApplyWorldHeightBrush(small, *ground, mpc, brush, 64.0 + step, 64.0, 0.1f, nullptr);   // x = 64 m is lattice 32: the border of tiles 0 and 1
        ApplyWorldHeightBrush(big, *ground, mpc, brush, 64.0 + step, 64.0, 0.1f, nullptr);
    }
    bool sameEverywhere = true;
    for (int z = 0; z <= 64; ++z) for (int x = 0; x <= 80; ++x) if (small.HeightDelta(x, z) != big.HeightDelta(x, z)) sameEverywhere = false;
    CHECK(sameEverywhere);
    CHECK(small.TileHasHeightEdits({ 0, 0 }) && small.TileHasHeightEdits({ 1, 0 }) && small.TileHasHeightEdits({ 0, 1 }) && small.TileHasHeightEdits({ 1, 1 }));   // it really crossed

    // A brush with no radius, no time or an unknown spot does nothing.
    WorldEdits none(32);
    brush.radius = 0.0f;
    CHECK(ApplyWorldHeightBrush(none, *ground, mpc, brush, 50.0, 50.0, 0.1f, nullptr).IsEmpty() && none.IsEmpty());
}

static void TestPatch()
{
    const double mpc = 2.0;
    const auto ground = MakeGround(50, 192.0);
    WorldEdits edits(32);
    BrushSettings brush;
    brush.radius = 20.0f; brush.strength = 0.8f;
    WorldStrokeRecorder recorder;
    // A first stroke that is already on the ground (the delta is not zero before the second one).
    ApplyWorldHeightBrush(edits, *ground, mpc, brush, 60.0, 60.0, 0.1f, nullptr);
    std::vector<float> beforeAll;
    for (int z = 0; z < 96; ++z) for (int x = 0; x < 96; ++x) beforeAll.push_back(edits.HeightDelta(x, z));
    brush.mode = BrushMode::Lower;
    for (int step = 0; step < 5; ++step) ApplyWorldHeightBrush(edits, *ground, mpc, brush, 62.0 + step, 60.0, 0.1f, &recorder);
    const WorldEditPatch patch = recorder.Finish(edits);
    CHECK(!patch.IsEmpty() && patch.heights.size() > 10 && patch.paints.empty());

    std::vector<float> afterAll;
    for (int z = 0; z < 96; ++z) for (int x = 0; x < 96; ++x) afterAll.push_back(edits.HeightDelta(x, z));
    ApplyWorldEditPatch(edits, patch, false);   // undo
    bool undone = true;
    size_t i = 0;
    for (int z = 0; z < 96; ++z) for (int x = 0; x < 96; ++x, ++i) if (edits.HeightDelta(x, z) != beforeAll[i]) undone = false;
    CHECK(undone);
    const WorldPatchExtent extent = ApplyWorldEditPatch(edits, patch, true);   // redo
    bool redone = true;
    i = 0;
    for (int z = 0; z < 96; ++z) for (int x = 0; x < 96; ++x, ++i) if (edits.HeightDelta(x, z) != afterAll[i]) redone = false;
    CHECK(redone);
    CHECK(!extent.heights.IsEmpty() && extent.paint.IsEmpty());

    // A point that went away and came back is not in the patch.
    WorldEdits other(32);
    WorldStrokeRecorder back;
    back.RememberHeight(3, 3, 0.0f);
    CHECK(back.Finish(other).IsEmpty());
}

static void TestPaint()
{
    const double mpc = 2.0;
    const int cells = 64;
    PaintMap old;
    old.Resize(cells, cells);
    HeightfieldLayout layout;
    layout.cellsX = layout.cellsZ = cells; layout.sizeX = layout.sizeZ = static_cast<float>(cells * mpc); layout.heightScale = 100.0f;
    WorldEdits edits(32);
    PaintBrush brush;
    brush.layer = 1; brush.radius = 14.0f; brush.strength = 0.8f; brush.falloff = 0.5f;
    const double centre = 0.5 * cells * mpc;
    for (int step = 0; step < 5; ++step) {
        const double wx = 50.0 + step * 4.0, wz = 60.0;
        ApplyPaintBrush(old, layout, brush, static_cast<float>(wx - centre), static_cast<float>(wz - centre), 0.1f, nullptr);
        ApplyWorldPaintBrush(edits, mpc, brush, wx, wz, 0.1f, nullptr);
    }
    brush.layer = 3; brush.erase = false;
    ApplyPaintBrush(old, layout, brush, 0.0f, 0.0f, 0.2f, nullptr);
    ApplyWorldPaintBrush(edits, mpc, brush, centre, centre, 0.2f, nullptr);
    brush.erase = true; brush.radius = 6.0f;
    ApplyPaintBrush(old, layout, brush, static_cast<float>(54.0 - centre), static_cast<float>(60.0 - centre), 0.2f, nullptr);
    ApplyWorldPaintBrush(edits, mpc, brush, 54.0, 60.0, 0.2f, nullptr);

    double worst = 0.0;
    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            float w[4];
            edits.GetPaint(x, z, w);
            for (int c = 0; c < 4; ++c) worst = (std::max)(worst, static_cast<double>(std::fabs(w[c] - old.weights[old.TexelIndex(x, z) * kSplatChannels + c])));
        }
    }
    CHECK(worst < 1e-4);   // the old paint brush's maths, on the lattice

    // The paint of a tile at full and at a coarser level: the coarse texel is the average of the cells it covers.
    const PaintMap fine = edits.PaintForTile({ 0, 0 }, 32);
    CHECK(!fine.IsEmpty() && fine.width == 32 && fine.height == 32);
    const PaintMap coarse = edits.PaintForTile({ 0, 0 }, 16);
    CHECK(coarse.width == 16);
    double avg = 0.0;
    for (int dz = 0; dz < 2; ++dz) for (int dx = 0; dx < 2; ++dx) avg += fine.weights[fine.TexelIndex(10 + dx, 12 + dz) * kSplatChannels + 1] * 0.25;
    CHECK(Near(coarse.weights[coarse.TexelIndex(5, 6) * kSplatChannels + 1], avg, 1e-6));
    CHECK(edits.PaintForTile({ 5, 5 }, 32).IsEmpty());     // no paint there
    CHECK(edits.PaintForTile({ 0, 0 }, 7).IsEmpty());      // not a divisor of the tile size
}

static void TestTilesAffected()
{
    WorldEdits edits(32);
    LatticeRect inside; inside.Include(10, 10); inside.Include(20, 20);
    CHECK(edits.TilesAffectedBy(inside, true).size() == 1);
    LatticeRect border; border.Include(30, 10); border.Include(33, 12);   // across x = 32
    CHECK(edits.TilesAffectedBy(border, true).size() == 2);
    LatticeRect farFromBorder; farFromBorder.Include(20, 10);   // 12 points from the border: out of reach of the next tile's apron and coarse levels
    CHECK(edits.TilesAffectedBy(farFromBorder, true).size() == 1);
    LatticeRect nearBorder; nearBorder.Include(30, 10);   // 2 points from it: the neighbour reads it too
    CHECK(edits.TilesAffectedBy(nearBorder, true).size() == 2 && edits.TilesAffectedBy(nearBorder, false).size() == 1);
    LatticeRect edge; edge.Include(26, 10);   // within 8 of the border: the neighbour reads it at a coarse level
    CHECK(edits.TilesAffectedBy(edge, true).size() == 2);
    CHECK(edits.TilesAffectedBy(edge, false).size() == 1);   // paint belongs to the owner alone
    LatticeRect corner; corner.Include(31, 31); corner.Include(32, 32);
    CHECK(edits.TilesAffectedBy(corner, true).size() == 4);
    CHECK(edits.TilesAffectedBy(LatticeRect{}, true).empty());
    LatticeRect negative; negative.Include(-1, -1);
    bool hasMinus = false, hasZero = false;
    for (const TileCoord& t : edits.TilesAffectedBy(negative, true)) { if (t == TileCoord{ -1, -1 }) hasMinus = true; if (t == TileCoord{ 0, 0 }) hasZero = true; }
    CHECK(hasMinus && hasZero);
}

static void TestFile()
{
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "world_edits_test.worldedits").string();
    WorldEdits edits(32);
    edits.SetHeightDelta(3, 4, 0.125f);
    edits.SetHeightDelta(-40, 70, -0.0625f);
    const float w[4] = { 0.0f, 0.5f, 0.25f, 0.0f };
    edits.SetPaint(-40, 70, w);
    edits.SetPaint(5, 5, w);
    std::string error;
    CHECK(SaveWorldEdits(path, edits, error));
    WorldEdits loaded(8);
    CHECK(LoadWorldEdits(path, loaded, error));
    CHECK(loaded.CellsPerTile() == 32 && loaded.BlockCount() == edits.BlockCount());
    CHECK(loaded.HeightDelta(3, 4) == 0.125f && loaded.HeightDelta(-40, 70) == -0.0625f && loaded.HeightDelta(0, 0) == 0.0f);
    float got[4];
    loaded.GetPaint(-40, 70, got);
    CHECK(Near(got[1], 0.5, 0.003) && Near(got[2], 0.25, 0.003));
    std::string error2;
    CHECK(SaveWorldEdits(path + "2", edits, error2));   // saving twice gives the same bytes
    {
        std::FILE* a = std::fopen(path.c_str(), "rb"); std::FILE* b = std::fopen((path + "2").c_str(), "rb");
        bool same = a && b;
        while (same) { const int ca = std::fgetc(a), cb = std::fgetc(b); if (ca != cb) same = false; if (ca == EOF) break; }
        CHECK(same);
        if (a) std::fclose(a);
        if (b) std::fclose(b);
    }
    // Damage and absence are errors, not crashes, and leave the target untouched.
    CHECK(!LoadWorldEdits((fs::temp_directory_path() / "no_such_edits_file.worldedits").string(), loaded, error) && !error.empty());
    {
        std::FILE* f = std::fopen(path.c_str(), "r+b");
        std::fputs("XXXX", f);
        std::fclose(f);
    }
    CHECK(!LoadWorldEdits(path, loaded, error));
    CHECK(loaded.HeightDelta(3, 4) == 0.125f);
    std::error_code ignore;
    fs::remove(path, ignore);
    fs::remove(path + "2", ignore);
}

int main()
{
    TestStore();
    TestEditedSourceAndTiles();
    TestBrushParity();
    TestPatch();
    TestPaint();
    TestTilesAffected();
    TestFile();
    if (g_failures == 0) std::printf("All world edits tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
