// Tests for src/HeightSample.{h,cpp} (roadmap H3): smooth sampling and area-average resampling. No Direct3D, no framework.
// Run from the repository root (the real-picture test reads assets/).
#include "HeightPicture.h"
#include "HeightRaster.h"
#include "HeightSample.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// A rough but deterministic surface: smooth hills plus pixel-scale noise (so neighbouring pixels differ a lot).
static HeightRaster RoughRaster(int w, int h, double spacing, double originX = 0.0, double originZ = 0.0)
{
    HeightRaster r;
    r.width = w; r.height = h; r.spacingX = r.spacingZ = spacing; r.originX = originX; r.originZ = originZ;
    r.metres.resize(static_cast<size_t>(w) * h);
    unsigned s = 12345u;
    for (int z = 0; z < h; ++z) for (int x = 0; x < w; ++x) {
        s = s * 1664525u + 1013904223u;
        const double noise = ((s >> 8) & 0xFFFF) / 65535.0 - 0.5;
        r.metres[static_cast<size_t>(z) * w + x] = static_cast<float>(20.0 * std::sin(x * 0.31) * std::cos(z * 0.23) + 6.0 * noise);
    }
    return r;
}

// What the OLD way did, for contrast: straight lines between the pixels.
static double Bilinear(const HeightRaster& r, double worldX, double worldZ)
{
    const double u = std::clamp((worldX - r.originX) / r.spacingX, 0.0, r.width - 1.0), v = std::clamp((worldZ - r.originZ) / r.spacingZ, 0.0, r.height - 1.0);
    const int x0 = (std::min)(static_cast<int>(u), r.width - 2), z0 = (std::min)(static_cast<int>(v), r.height - 2);
    const double tx = u - x0, tz = v - z0;
    const double top = r.At(x0, z0) + (r.At(x0 + 1, z0) - r.At(x0, z0)) * tx, bottom = r.At(x0, z0 + 1) + (r.At(x0 + 1, z0 + 1) - r.At(x0, z0 + 1)) * tx;
    return top + (bottom - top) * tz;
}

static void TestNodesRampAndEdges()
{
    const HeightRaster r = RoughRaster(12, 9, 2.5, -7.0, 3.0);
    bool exact = true;
    for (int z = 0; z < r.height; ++z) for (int x = 0; x < r.width; ++x)
        if (!Near(SampleHeightBicubic(r, r.originX + x * r.spacingX, r.originZ + z * r.spacingZ), r.At(x, z), 1e-9)) exact = false;
    CHECK(exact);   // it passes through every sample

    // A straight slope is reproduced exactly (in the interior, where all sixteen neighbours exist). Values are whole numbers: exact in float.
    HeightRaster ramp; ramp.width = 10; ramp.height = 8; ramp.spacingX = 4.0; ramp.spacingZ = 2.0; ramp.originX = 100.0; ramp.originZ = -50.0;
    for (int z = 0; z < 8; ++z) for (int x = 0; x < 10; ++x) ramp.metres.push_back(static_cast<float>(3.0 * x * 4.0 + 5.0 * z * 2.0 + 7.0));   // 3 per metre in X, 5 per metre in Z
    bool linear = true;
    for (double fx = 1.0; fx <= 8.0; fx += 0.37) for (double fz = 1.0; fz <= 6.0; fz += 0.41)
        if (!Near(SampleHeightBicubic(ramp, 100.0 + fx * 4.0, -50.0 + fz * 2.0), 3.0 * fx * 4.0 + 5.0 * fz * 2.0 + 7.0, 1e-9)) linear = false;
    CHECK(linear);

    // Outside the raster the edge repeats; an invalid raster gives 0.
    CHECK(Near(SampleHeightBicubic(r, -1e6, -1e6), r.At(0, 0), 1e-9) && Near(SampleHeightBicubic(r, 1e6, 1e6), r.At(r.width - 1, r.height - 1), 1e-9));
    CHECK(SampleHeightBicubic(HeightRaster{}, 1.0, 1.0) == 0.0);
}

static void TestSmoothness()
{
    const HeightRaster r = RoughRaster(40, 40, 1.0);
    // The slope on each side of every pixel border, one millionth of a pixel away from it. Smooth ground has the same slope on both sides.
    const double h = 1e-6;
    double worstBicubic = 0.0, worstBilinear = 0.0;
    for (int border = 2; border <= 36; ++border) {
        for (double other = 3.0; other < 35.0; other += 2.71) {
            auto jump = [&](auto sample, bool alongX) {
                const double x = border, z = other;
                const double a = alongX ? sample(x - h, z) : sample(z, x - h), b = alongX ? sample(x, z) : sample(z, x), c = alongX ? sample(x + h, z) : sample(z, x + h);
                return std::fabs((c - b) / h - (b - a) / h);
            };
            auto bicubic = [&](double x, double z) { return SampleHeightBicubic(r, x, z); };
            auto bilinear = [&](double x, double z) { return Bilinear(r, x, z); };
            worstBicubic = (std::max)({ worstBicubic, jump(bicubic, true), jump(bicubic, false) });
            worstBilinear = (std::max)({ worstBilinear, jump(bilinear, true), jump(bilinear, false) });
        }
    }
    CHECK(worstBicubic < 1e-3);    // no crease: the slope is the same on both sides
    CHECK(worstBilinear > 1.0);    // the straight-line blend of the old terrain has a crease at every border (this test can tell them apart)
}

static void TestNoClamp()
{
    // A cliff: 0 m on the left, 10 m on the right. Smooth interpolation overshoots a little beside it; the ground is not clamped to its neighbours.
    HeightRaster r; r.width = 8; r.height = 3; r.spacingX = r.spacingZ = 1.0;
    for (int z = 0; z < 3; ++z) for (int x = 0; x < 8; ++x) r.metres.push_back(x < 4 ? 0.0f : 10.0f);
    double lowest = 0.0, highest = 10.0;
    for (double x = 0.0; x <= 7.0; x += 0.01) { const double v = SampleHeightBicubic(r, x, 1.0); lowest = (std::min)(lowest, v); highest = (std::max)(highest, v); }
    CHECK(lowest < -0.1 && highest > 10.1);
}

static void TestResampleBasics()
{
    // Constant stays constant; the corners stay on the corners; the spacing is the area over a whole number of cells.
    HeightRaster flat; flat.width = 129; flat.height = 65; flat.spacingX = flat.spacingZ = 0.5; flat.originX = 10.0; flat.originZ = -4.0;
    flat.metres.assign(129u * 65u, 7.25f);
    const HeightRaster coarse = ResampleToSpacing(flat, 4.0);
    CHECK(coarse.width == 17 && coarse.height == 9 && coarse.originX == 10.0 && coarse.originZ == -4.0);
    CHECK(Near(coarse.spacingX, 4.0, 1e-12) && Near(coarse.spacingZ, 4.0, 1e-12) && Near(coarse.SizeX(), flat.SizeX(), 1e-9) && Near(coarse.SizeZ(), flat.SizeZ(), 1e-9));
    bool constant = true;
    for (float v : coarse.metres) if (v != 7.25f) constant = false;
    CHECK(constant);

    // A raster is never made finer, and a target at or below the spacing changes nothing.
    CHECK(ResampleToSpacing(flat, 0.5).width == 129 && ResampleToSpacing(flat, 0.1).width == 129 && ResampleToSpacing(flat, 0.0).width == 129);

    // One axis only: coarse along X, already fine enough along Z.
    HeightRaster wide = flat; wide.spacingZ = 8.0;
    const HeightRaster oneAxis = ResampleToSpacing(wide, 4.0);
    CHECK(oneAxis.width == 17 && oneAxis.height == 65 && oneAxis.spacingZ == 8.0);

    // A slope is kept: with a whole number of pixels per cell, every sample away from the picture's edge is exactly on the slope.
    // (At the edge the averaged stretch is cut short and lopsided, so the first and last samples are left out.)
    HeightRaster ramp; ramp.width = 65; ramp.height = 9; ramp.spacingX = ramp.spacingZ = 1.0;
    for (int z = 0; z < 9; ++z) for (int x = 0; x < 65; ++x) ramp.metres.push_back(static_cast<float>(2.0 * x + 3.0 * z));
    const HeightRaster rampCoarse = ResampleToSpacing(ramp, 4.0);   // 4 pixels per cell along X
    CHECK(rampCoarse.width == 17);
    bool slopeKept = true;
    for (int x = 1; x + 1 < rampCoarse.width; ++x)
        if (!Near(rampCoarse.At(x, 0) - rampCoarse.At(1, 0), 8.0 * (x - 1), 1e-4)) slopeKept = false;   // 2 m per pixel x 4 pixels per cell
    CHECK(slopeKept);

    HeightRaster bad; bad.width = 5;
    CHECK(!ResampleToSpacing(bad, 4.0).IsValid());   // invalid in, unchanged out
}

// The old complaint: a thin ridge is hit or missed by chance. A ridge one pixel wide, 10 m high, at 8 pixels per cell.
static void TestThinRidge()
{
    auto make = [](int ridgeColumn) {
        HeightRaster r; r.width = 129; r.height = 9; r.spacingX = r.spacingZ = 0.5;
        for (int z = 0; z < 9; ++z) for (int x = 0; x < 129; ++x) r.metres.push_back(x == ridgeColumn ? 10.0f : 0.0f);
        return r;
    };
    const HeightRaster a = make(16), b = make(17);   // the same ridge, moved by one pixel (0.5 m)
    // Point sampling at the lattice (every 8th pixel): the first ridge sits ON a lattice point and the second BETWEEN two. All or nothing.
    double pointChange = 0.0;
    for (int x = 0; x < 129; x += 8) pointChange = (std::max)(pointChange, static_cast<double>(std::fabs(a.At(x, 4) - b.At(x, 4))));
    CHECK(pointChange == 10.0);
    // Area average to the same lattice (4 m = 8 pixels): each cell sees at most an eighth of the ridge.
    const HeightRaster ca = ResampleToSpacing(a, 4.0), cb = ResampleToSpacing(b, 4.0);   // every row is the same, and Z (4 m deep) shrinks too: read row 0
    CHECK(ca.width == 17 && cb.width == 17);
    double areaChange = 0.0, areaMax = 0.0;
    for (int x = 0; x < ca.width; ++x) { areaChange = (std::max)(areaChange, static_cast<double>(std::fabs(ca.At(x, 0) - cb.At(x, 0)))); areaMax = (std::max)(areaMax, static_cast<double>(ca.At(x, 0))); }
    CHECK(areaChange <= 10.0 / 8.0 + 1e-6);
    CHECK(areaMax <= 10.0 / 8.0 + 1e-6 && areaMax > 0.5);   // the ridge is still there, spread over its cell, not lost
}

// Reference numbers computed independently with Python (numpy): the same box average, the same crops of the repository's picture.
static HeightRaster Crop(const HeightRaster& r, int x0, int z0, int n)
{
    HeightRaster c; c.width = c.height = n; c.spacingX = r.spacingX; c.spacingZ = r.spacingZ;
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) c.metres.push_back(r.At(x0 + x, z0 + z));
    return c;
}

static void TestTheRealPicture()
{
    HeightPicture picture; std::string error;
    if (!LoadHeightPicture("assets/dark alien/Height Map.png", picture, error)) { std::printf("FAIL: %s (run this test from the repository root)\n", error.c_str()); ++g_failures; return; }
    HeightImportSettings settings; settings.lowestY = -30.0f; settings.relief = 240.0f; settings.widthMetres = 1024.0;
    HeightRaster raster;
    CHECK(ImportHeightRaster(picture, settings, raster));

    struct Case { int x0, z0; double pointRms, areaRms, pointMax, areaMax; };
    const Case cases[] = { { 0, 0, 1.1030, 0.7586, 13.60, 6.75 }, { 300, 1200, 0.4256, 0.2256, 8.86, 2.34 } };
    for (const Case& c : cases) {
        // Two crops of the picture, one pixel apart: the same ground seen with the lattice shifted by 0.5 m. 513 pixels, 8 per cell.
        const HeightRaster a = Crop(raster, c.x0, c.z0, 513), b = Crop(raster, c.x0 + 1, c.z0, 513);
        const double target = 8.0 * raster.spacingX;
        const HeightRaster la = ResampleToSpacing(a, target), lb = ResampleToSpacing(b, target);
        CHECK(la.width == 65 && lb.width == 65 && la.height == 65);
        double pointSum = 0.0, areaSum = 0.0, pointMax = 0.0, areaMax = 0.0;
        for (int z = 0; z < 65; ++z) for (int x = 0; x < 65; ++x) {
            const double dp = a.At(x * 8, z * 8) - b.At(x * 8, z * 8), da = la.At(x, z) - lb.At(x, z);
            pointSum += dp * dp; areaSum += da * da; pointMax = (std::max)(pointMax, std::fabs(dp)); areaMax = (std::max)(areaMax, std::fabs(da));
        }
        const double pointRms = std::sqrt(pointSum / (65.0 * 65.0)), areaRms = std::sqrt(areaSum / (65.0 * 65.0));
        CHECK(Near(pointRms, c.pointRms, 2e-3) && Near(areaRms, c.areaRms, 2e-3));
        CHECK(Near(pointMax, c.pointMax, 0.02) && Near(areaMax, c.areaMax, 0.02));
        CHECK(areaMax < 0.6 * pointMax);   // the worst streak is at least 40% smaller (not 8x: real ground is steep, a shift is real change)
    }
}

int main()
{
    TestNodesRampAndEdges();
    TestSmoothness();
    TestNoClamp();
    TestResampleBasics();
    TestThinRidge();
    TestTheRealPicture();
    if (g_failures == 0) std::printf("All height sample tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
