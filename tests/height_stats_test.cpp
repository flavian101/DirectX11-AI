// Tests for src/HeightStats.{h,cpp} (roadmap H2): the true range, the share below a level, the steepness. No Direct3D, no framework.
// Run from the repository root (the real-picture test reads assets/).
#include "HeightPicture.h"
#include "HeightRaster.h"
#include "HeightStats.h"
#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// A plane that rises `rise` metres per metre along X, on a w x h grid with `spacing` metres between samples.
static HeightRaster Ramp(int w, int h, double spacing, double rise)
{
    HeightRaster r;
    r.width = w; r.height = h; r.spacingX = r.spacingZ = spacing;
    r.metres.resize(static_cast<size_t>(w) * h);
    for (int z = 0; z < h; ++z) for (int x = 0; x < w; ++x) r.metres[static_cast<size_t>(z) * w + x] = static_cast<float>(x * spacing * rise);
    return r;
}

static void TestRangeAndFractions()
{
    HeightRaster r = Ramp(11, 3, 10.0, 0.5);   // heights 0, 5, 10 .. 50 along X, the same on every row
    const HeightStats s = ComputeHeightStats(r);
    CHECK(s.lowest == 0.0f && s.highest == 50.0f);

    CHECK(FractionBelow(r, 0.0f) == 0.0);               // nothing is below the lowest point
    CHECK(Near(FractionBelow(r, 0.001f), 1.0 / 11.0, 1e-12));
    CHECK(Near(FractionBelow(r, 25.0f), 5.0 / 11.0, 1e-12));   // strictly below: 0, 5, 10, 15, 20
    CHECK(Near(FractionBelow(r, 50.0f), 10.0 / 11.0, 1e-12));  // the highest row of samples is not below its own height
    CHECK(FractionBelow(r, 51.0f) == 1.0);
    CHECK(FractionBelow(HeightRaster{}, 5.0f) == 0.0);

    // HeightAtFraction is the inverse: the level below which that share of the ground lies.
    CHECK(HeightAtFraction(r, 0.0) == 0.0f && HeightAtFraction(r, 1.0) == 50.0f);
    CHECK(HeightAtFraction(r, 0.5) == 25.0f);
    CHECK(HeightAtFraction(r, -3.0) == 0.0f && HeightAtFraction(r, 7.0) == 50.0f);   // clamped, not undefined
    CHECK(FractionBelow(r, HeightAtFraction(r, 0.5)) <= 0.5);
}

static void TestSlope()
{
    // 0.1 rise per metre: a 10% grade, 5.7106 degrees, at any spacing and any size.
    for (double spacing : { 0.5, 4.0, 10.0 }) {
        const HeightStats s = ComputeHeightStats(Ramp(9, 7, spacing, 0.1));
        CHECK(Near(s.meanGradient, 0.1, 1e-6));
        CHECK(Near(s.MeanSlopeDegrees(), 5.710593, 1e-4));
    }
    // A flat raster has no slope, and a 45 degree ramp has gradient 1.
    CHECK(ComputeHeightStats(Ramp(5, 5, 3.0, 0.0)).meanGradient == 0.0);
    CHECK(Near(ComputeHeightStats(Ramp(5, 5, 3.0, 1.0)).MeanSlopeDegrees(), 45.0, 1e-4));
    // Along Z as well: a diagonal plane rising 1 per metre in both directions has gradient sqrt(2).
    HeightRaster d = Ramp(6, 6, 2.0, 1.0);
    for (int z = 0; z < 6; ++z) for (int x = 0; x < 6; ++x) d.metres[static_cast<size_t>(z) * 6 + x] += static_cast<float>(z * 2.0);
    CHECK(Near(ComputeHeightStats(d).meanGradient, std::sqrt(2.0), 1e-6));
}

// Reference numbers for the repository's picture were computed independently with Python (PIL + numpy), using the same definitions.
static void TestTheRealPicture()
{
    HeightPicture picture; std::string error;
    if (!LoadHeightPicture("assets/dark alien/Height Map.png", picture, error)) { std::printf("FAIL: %s (run this test from the repository root)\n", error.c_str()); ++g_failures; return; }
    HeightImportSettings settings;
    settings.lowestY = -30.0f; settings.relief = 240.0f; settings.widthMetres = 1024.0;
    HeightRaster raster;
    CHECK(ImportHeightRaster(picture, settings, raster));
    const double samples = 2048.0 * 2048.0;

    const HeightStats s = ComputeHeightStats(raster);
    CHECK(s.lowest == -30.0f && s.highest == 210.0f);   // the true range, nothing thrown away

    // The flat floor: 521,605 samples (12.436%) sit at the lowest height; none is below it.
    CHECK(FractionBelow(raster, -30.0f) == 0.0);
    CHECK(Near(FractionBelow(raster, -30.0f + 1e-3f), 521605.0 / samples, 1e-12));

    // The top of the picture is all there: 21,406 samples stand above 161.5 m (more than 80% of the way up). The old auto-levels flattened them.
    CHECK(std::llround((1.0 - FractionBelow(raster, 161.5f)) * samples) == 21406);

    // A default sea that covers the lowest 13% of the ground lies where the 13th percentile is (-26.1452 m by numpy; the heights come in steps of 0.037 m).
    CHECK(Near(HeightAtFraction(raster, 0.13), -26.1452, 0.05));

    // At 0.5 m per pixel this picture is rough: the average steepness between neighbouring pixels is 54.86 degrees at 240 m of relief.
    // That roughness is far finer than a 4 m lattice can show, which is why the resampling of H3 averages it instead of point sampling it.
    CHECK(Near(s.meanGradient, 1.420797977, 1e-3));
    CHECK(Near(s.MeanSlopeDegrees(), 54.860974, 0.02));
}

int main()
{
    TestRangeAndFractions();
    TestSlope();
    TestTheRealPicture();
    if (g_failures == 0) std::printf("All height stats tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
