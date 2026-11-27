// Unit tests for src/OceanSea.h (roadmap O2): the CPU mirror a boat reads. It must give the heights of OceanFFT::Evolve (the reference the GPU is also
// checked against), cost little, and be safe to ask many times a frame. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "OceanSea.h"
#include <chrono>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static SeaSpectrum::Settings MakeSettings(double wind, double depth, unsigned seed)
{
    SeaSpectrum::Settings s;
    s.windSpeed = wind;
    s.depth = depth;
    s.windRadians = 0.6;
    s.seed = seed;
    return s;
}

// Every band's height field is the one OceanFFT::Evolve makes, including the two bands that share an FFT (bands 0 and 2 are both 128 modes).
static void TestMatchesReference(double wind, double depth, double time)
{
    OceanSea::Sea sea;
    sea.Build(MakeSettings(wind, depth, 11));
    sea.Update(time);
    CHECK(sea.BandCount() == 3 && sea.Surfaces().size() == 3);
    const std::vector<SeaSpectrum::Band> bands = SeaSpectrum::DefaultBands();
    for (int b = 0; b < sea.BandCount(); ++b) {
        const SeaSpectrum::BandField field = SeaSpectrum::Make(MakeSettings(wind, depth, 11), bands[static_cast<size_t>(b)], b);
        const OceanFFT::Surface reference = OceanFFT::Evolve(field, time, 0.7);
        const OceanFFT::Surface& mine = sea.Surfaces()[static_cast<size_t>(b)];
        CHECK(mine.n == reference.n && mine.patchSize == reference.patchSize && mine.height.size() == reference.height.size());
        double scale = 0.0, worst = 0.0;
        for (size_t i = 0; i < reference.height.size(); ++i) {
            scale = std::max(scale, static_cast<double>(std::fabs(reference.height[i])));
            worst = std::max(worst, static_cast<double>(std::fabs(mine.height[i] - reference.height[i])));
        }
        CHECK(scale > 1e-4);
        CHECK(worst <= 1e-5 * scale);
        if (worst > 1e-5 * scale) std::printf("  band %d wind %.0f depth %.0f t %.2f: worst %.3e scale %.4f\n", b, wind, depth, time, worst, scale);
    }
    // And the sum a boat reads is the sum of the reference bands at arbitrary world positions (negative ones and far ones included).
    double worstSum = 0.0, scaleSum = 0.0;
    std::vector<OceanFFT::Surface> reference;
    for (int b = 0; b < 3; ++b)
        reference.push_back(OceanFFT::Evolve(SeaSpectrum::Make(MakeSettings(wind, depth, 11), bands[static_cast<size_t>(b)], b), time, 0.7));
    for (int p = 0; p < 200; ++p) {
        const double x = -3500.0 + 37.31 * p * ((p % 3) + 1), z = 2900.0 - 41.7 * p * ((p % 2) + 1);
        const double expected = OceanFFT::SeaHeightAt(reference, x, z);
        worstSum = std::max(worstSum, std::fabs(sea.HeightAt(x, z) - expected));
        scaleSum = std::max(scaleSum, std::fabs(expected));
    }
    CHECK(scaleSum > 1e-3);
    CHECK(worstSum <= 1e-4 * scaleSum);
}

// Asking again for the same time changes nothing, and going back in time gives the same sea again (the sea is a function of t, not of history).
static void TestUpdateIsAFunctionOfTime()
{
    OceanSea::Sea sea;
    sea.Build(MakeSettings(10.0, 0.0, 3));
    CHECK(sea.HeightAt(5.0, 7.0) == 0.0);   // before the first Update: nothing, not garbage
    sea.Update(12.5);
    const double a = sea.HeightAt(123.0, -456.0);
    sea.Update(12.5);
    CHECK(sea.HeightAt(123.0, -456.0) == a);
    sea.Update(80.0);
    const double later = sea.HeightAt(123.0, -456.0);
    sea.Update(12.5);
    CHECK(sea.HeightAt(123.0, -456.0) == a);
    CHECK(later != a);   // the sea moves
}

// The bound is a real bound: over many times and the whole 256-sample grid of the biggest band, the sea never gets close to 6 sigma, and the bound is
// not absurdly loose either (the highest crest over a few hundred thousand points is about 4 to 5 sigma).
static void TestBound()
{
    for (double wind : { 5.0, 12.0, 20.0 }) {
        OceanSea::Sea sea;
        sea.Build(MakeSettings(wind, 0.0, 21));
        const double bound = sea.HeightBound(), sigma = sea.Sigma();
        CHECK(sigma > 0.0 && std::fabs(bound - 6.0 * sigma) < 1e-12);
        double highest = 0.0;
        for (int step = 0; step < 12; ++step) {
            sea.Update(7.0 * step);
            for (int j = 0; j < 400; ++j)
                for (int i = 0; i < 400; ++i) highest = std::max(highest, std::fabs(sea.HeightAt(i * 5.0, j * 5.0)));
        }
        CHECK(highest < bound);
        CHECK(highest > 1.5 * sigma);   // so the bound is not 100 sigma
        std::printf("  wind %.0f m/s: sigma %.3f m, highest seen %.3f m (%.1f sigma), bound %.3f m\n", wind, sigma, highest, highest / sigma, bound);
    }
    // No wind, no sea, no bound to speak of.
    OceanSea::Sea calm;
    calm.Build(MakeSettings(0.0, 0.0, 1));
    CHECK(calm.HeightBound() < 0.05);
}

// What it costs, because a boat's Update runs on the game thread: reported, and held to a generous limit so a slow regression shows up.
static void TestCost()
{
    OceanSea::Sea sea;
    sea.Build(MakeSettings(12.0, 0.0, 1));
    const auto start = std::chrono::steady_clock::now();
    const int updates = 20;
    for (int i = 0; i < updates; ++i) sea.Update(1.0 + i);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / updates;
    std::printf("  one Update (two FFTs: 128 x 2 bands in one, 256 in one): %.2f ms\n", ms);
    CHECK(ms < 100.0);
}

int main()
{
    TestMatchesReference(8.0, 0.0, 3.7);
    TestMatchesReference(14.0, 0.0, 61.25);
    TestMatchesReference(8.0, 25.0, 9.0);   // shallow: omega differs per mode
    TestUpdateIsAFunctionOfTime();
    TestBound();
    TestCost();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ocean_sea_test: all passed\n");
    return 0;
}
