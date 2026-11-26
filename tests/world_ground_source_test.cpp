// Unit tests for src/WorldGroundSource.h (roadmap H7): the raster and detail layers of the world's ground, in metres. No Direct3D, no
// framework: a failed CHECK prints and the program exits 1.
#include "HeightDetail.h"
#include "HeightRaster.h"
#include "WorldGroundSource.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
static const char* TerrainPicturePath() { return "assets/dark alien/Height Map.png"; }   // the world's default picture (run from the repository root)

// A 5 x 5 raster, 10 m between samples, the corner at (100, 200): metres rise by 3 per sample along x, 2 along z, from 50.
static std::shared_ptr<HeightRaster> MakeRamp()
{
    auto r = std::make_shared<HeightRaster>();
    r->width = r->height = 5;
    r->originX = 100.0; r->originZ = 200.0;
    r->spacingX = r->spacingZ = 10.0;
    r->metres.resize(25);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) r->metres[static_cast<size_t>(z) * 5 + x] = 50.0f + 3.0f * static_cast<float>(x) + 2.0f * static_cast<float>(z);
    return r;
}

// The ground value is (metres - lowestY) / relief, so world Y = lowestY + value * relief: the picture's metres come back exactly.
static void TestRasterUnits()
{
    const auto raster = MakeRamp();
    const float lowestY = 40.0f, relief = 80.0f;
    RasterHeightSource source(raster, lowestY, relief);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) {
            const double worldX = 100.0 + 10.0 * x, worldZ = 200.0 + 10.0 * z;
            const double metres = lowestY + static_cast<double>(source.Height01(worldX, worldZ)) * relief;
            CHECK(Near(metres, raster->At(x, z), 1e-3));   // through every sample
        }
    // A straight slope is exact between samples (3 m per 10 m along x).
    CHECK(Near(lowestY + source.Height01(115.0, 220.0) * relief, 50.0 + 3.0 * 1.5 + 2.0 * 2.0, 1e-3));
    // Outside the raster the edge repeats.
    CHECK(Near(source.Height01(-5000.0, 200.0), source.Height01(100.0, 200.0), 1e-6));
}

// Ground higher or lower than the range the tiles hold is held at the range's ends, never wrapped or folded.
static void TestRangeEnds()
{
    const auto raster = MakeRamp();
    RasterHeightSource lowCeiling(raster, 50.0f, 5.0f);   // the picture goes from 50 to 62, the range only to 55
    CHECK(lowCeiling.Height01(100.0, 200.0) == 0.0f);
    CHECK(lowCeiling.Height01(140.0, 240.0) == 1.0f);
    RasterHeightSource none(nullptr, 0.0f, 10.0f);
    CHECK(none.Height01(0.0, 0.0) == 0.0f);
}

// The detail layer is metres: strength 0 changes nothing, and with a strength the bump never exceeds it, whatever the relief.
static void TestDetail()
{
    const auto raster = MakeRamp();
    auto base = std::make_shared<RasterHeightSource>(raster, 0.0f, 200.0f);
    HeightDetailSettings off;   // strength 0
    DetailedHeightSource same(base, off, 200.0f);
    CHECK(same.Height01(123.0, 217.0) == base->Height01(123.0, 217.0));

    HeightDetailSettings on;
    on.strength = 4.0f; on.wavelength = 20.0f; on.octaves = 3; on.seed = 9;
    for (float relief : { 200.0f, 400.0f }) {
        auto b = std::make_shared<RasterHeightSource>(raster, 0.0f, relief);
        DetailedHeightSource detailed(b, on, relief);
        double largest = 0.0;
        for (int i = 0; i < 400; ++i) {
            const double x = 100.0 + 0.37 * i, z = 200.0 + 0.29 * i;
            const double metresDifference = (static_cast<double>(detailed.Height01(x, z)) - static_cast<double>(b->Height01(x, z))) * relief;
            largest = (std::max)(largest, std::fabs(metresDifference));
        }
        CHECK(largest <= 4.0 + 1e-3);   // never beyond the strength, in metres
        CHECK(largest > 0.5);           // and it really adds something
    }
    // Deterministic in world position.
    DetailedHeightSource a(base, on, 200.0f), b(base, on, 200.0f);
    CHECK(a.Height01(131.3, 244.7) == b.Height01(131.3, 244.7));
}

// H8: a picture is placed by its CENTRE. At (0, 0) the middle of the ground is the middle of the world, whatever the picture's size or shape, and
// moving the centre moves every point by exactly that much (the heights do not change, only where they are).
static void TestPlacedByItsCentre()
{
    const char* path = TerrainPicturePath();
    for (double width : { 1024.0, 3000.0 }) {
        HeightImportSettings import;
        import.widthMetres = width;
        std::shared_ptr<const HeightRaster> at00, moved;
        WorldPictureInfo info;
        std::string error;
        if (!ImportWorldRaster(path, import, 4.0, 0.0, 0.0, at00, info, error)) { std::printf("FAIL: %s\n", error.c_str()); ++failures; return; }
        CHECK(Near(at00->originX + 0.5 * at00->SizeX(), 0.0, 1e-6) && Near(at00->originZ + 0.5 * at00->SizeZ(), 0.0, 1e-6));
        CHECK(Near(at00->SizeX(), width, 1e-3));
        CHECK(info.valid && info.widthPixels > 1 && info.stats.highest > info.stats.lowest);
        CHECK(ImportWorldRaster(path, import, 4.0, 500.0, -250.0, moved, info, error));
        CHECK(Near(moved->originX - at00->originX, 500.0, 1e-6) && Near(moved->originZ - at00->originZ, -250.0, 1e-6));
        CHECK(moved->metres == at00->metres);   // the same heights
    }
}

int main()
{
    TestPlacedByItsCentre();
    TestRasterUnits();
    TestRangeEnds();
    TestDetail();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("world ground source tests: all passed\n");
    return 0;
}
