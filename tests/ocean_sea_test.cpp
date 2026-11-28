// Unit tests for src/OceanSea.h (roadmap O2): the CPU mirror the camera and the boats read. It is the same sea the GPU draws, answered by adding up the
// strongest modes directly. It must agree with the full spectrum (brute force) and with the FFT sea (OceanFFT::Evolve, the reference the GPU is also
// checked against) at the texels, be safe to ask many times a frame, and cost little. No Direct3D, no framework: a failed CHECK prints and the
// program exits 1.
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

// The height at a point from EVERY mode of the spectrum, one cosine at a time: slow (all 98 thousand modes) and obviously right.
static double BruteHeight(const SeaSpectrum::BandField& field, double t, double x, double z)
{
    const int n = field.N();
    const double dk = field.band.DeltaK();
    double sum = 0.0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const double kx = dk * SeaSpectrum::SignedFrequency(i, n), kz = dk * SeaSpectrum::SignedFrequency(j, n);
            if (std::sqrt(kx * kx + kz * kz) < 1e-9) continue;
            const size_t index = field.Index(i, j), opposite = field.Opposite(i, j);
            const double phase = static_cast<double>(field.omega[index]) * t;
            const OceanFFT::Cx value = OceanFFT::Cx(field.h0[index].re, field.h0[index].im) * OceanFFT::Cx(std::cos(phase), -std::sin(phase)) +
                                       OceanFFT::Cx(field.h0[opposite].re, -field.h0[opposite].im) * OceanFFT::Cx(std::cos(phase), std::sin(phase));
            sum += (value * OceanFFT::Cx(std::cos(kx * x + kz * z), std::sin(kx * x + kz * z))).real();
        }
    }
    return sum;
}

// The sea agrees with the sum over every mode at arbitrary world positions (negative and far ones included): the table trick, the row and column
// bookkeeping, the partners and the time turn are all in this one comparison. What it may differ by is the dropped energy: a few thousandths of sigma.
static void TestMatchesBruteForce(double wind, double depth, double time)
{
    const SeaSpectrum::Settings settings = MakeSettings(wind, depth, 11);
    OceanSea::Sea sea;
    sea.Build(settings);
    sea.Update(time);
    const std::vector<SeaSpectrum::Band> bands = SeaSpectrum::DefaultBands();
    std::vector<SeaSpectrum::BandField> fields;
    for (int b = 0; b < 3; ++b) fields.push_back(SeaSpectrum::Make(settings, bands[static_cast<size_t>(b)], b));
    double worst = 0.0, biggest = 0.0;
    for (int p = 0; p < 24; ++p) {
        const double x = -3500.0 + 311.3 * p * ((p % 3) + 1), z = 2900.0 - 173.9 * p * ((p % 2) + 1);
        double brute = 0.0;
        for (int b = 0; b < 3; ++b) brute += BruteHeight(fields[static_cast<size_t>(b)], time, x, z);
        worst = std::max(worst, std::fabs(sea.HeightAt(x, z) - brute));
        biggest = std::max(biggest, std::fabs(brute));
    }
    CHECK(sea.Sigma() > 1e-3);
    CHECK(biggest > 0.5 * sea.Sigma());            // there is a sea to compare, not 24 flat spots
    CHECK(worst <= 0.02 * sea.Sigma());
    std::printf("  wind %.0f depth %.0f t %.2f: %zu modes kept, worst difference from every mode %.4f m (sigma %.3f m)\n", wind, depth, time, sea.ModeCount(), worst, sea.Sigma());
}

// At the texels of a band the direct sum is the FFT's value (OceanFFT::Evolve, the reference): same numbers, different way of adding them up.
// A shift by whole patches changes nothing (the sea repeats).
static void TestMatchesFftAtTexels()
{
    const SeaSpectrum::Settings settings = MakeSettings(10.0, 0.0, 5);
    OceanSea::Sea sea;
    sea.Build(settings);
    const double time = 17.25;
    sea.Update(time);
    const std::vector<SeaSpectrum::Band> bands = SeaSpectrum::DefaultBands();
    for (int b = 0; b < 3; ++b) {
        const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bands[static_cast<size_t>(b)], b);
        const OceanFFT::Surface reference = OceanFFT::Evolve(field, time, 0.7);
        const int n = field.N();
        const double spacing = field.band.patchSize / n, sigma = std::sqrt(SeaSpectrum::RealizedVariance(field));
        double worst = 0.0, shifted = 0.0;
        for (int p = 0; p < 40; ++p) {
            const int i = (p * 37 + 5) % n, j = (p * 61 + 11) % n;
            const double x = i * spacing, z = j * spacing;
            worst = std::max(worst, std::fabs(sea.BandHeightAt(b, x, z) - reference.height[static_cast<size_t>(j) * n + i]));
            shifted = std::max(shifted, std::fabs(sea.BandHeightAt(b, x + 3.0 * field.band.patchSize, z - 2.0 * field.band.patchSize) - sea.BandHeightAt(b, x, z)));
        }
        CHECK(sigma > 1e-4);
        CHECK(worst <= 0.03 * sigma);      // the dropped energy: 0.3% of sigma on average, a few times that at the worst of 40 points
        CHECK(shifted <= 1e-6 * sigma + 1e-9);
        if (worst > 0.03 * sigma) std::printf("  band %d: worst %.3e, sigma %.4f\n", b, worst, sigma);
    }
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

// A sea that was never built, and one with no wind, are safe to ask.
static void TestEmptyAndCalm()
{
    OceanSea::Sea nothing;
    nothing.Update(3.0);
    CHECK(!nothing.IsBuilt() && nothing.HeightAt(1.0, 2.0) == 0.0);
    OceanSea::Sea calm;
    calm.Build(MakeSettings(0.0, 0.0, 1));
    calm.Update(3.0);
    CHECK(calm.HeightBound() < 0.05);
    CHECK(std::fabs(calm.HeightAt(10.0, 20.0)) < 0.05);
}

// The bound is a real bound: over many times and a grid of points the sea never reaches 6 sigma, and the bound is not absurdly loose either.
static void TestBound()
{
    for (double wind : { 5.0, 12.0, 20.0 }) {
        OceanSea::Sea sea;
        sea.Build(MakeSettings(wind, 0.0, 21));
        const double bound = sea.HeightBound(), sigma = sea.Sigma();
        CHECK(sigma > 0.0 && std::fabs(bound - 6.0 * sigma) < 1e-12);
        double highest = 0.0;
        for (int step = 0; step < 10; ++step) {
            sea.Update(7.0 * step);
            for (int j = 0; j < 50; ++j)
                for (int i = 0; i < 50; ++i) highest = std::max(highest, std::fabs(sea.HeightAt(i * 13.7, j * 11.3)));
        }
        CHECK(highest < bound);
        CHECK(highest > 1.5 * sigma);   // so the bound is not 100 sigma
        std::printf("  wind %.0f m/s: sigma %.3f m, highest seen %.3f m (%.1f sigma), bound %.3f m\n", wind, sigma, highest, highest / sigma, bound);
    }
}

// The shore limit: deep water is untouched, shallow water is capped well under the depth, the waterline is flat, and it never reverses a wave.
static void TestShoreLimit()
{
    CHECK(OceanSea::ShoreLimited(3.0, 200.0) > 3.0 * 0.9999 && OceanSea::ShoreLimited(3.0, 200.0) <= 3.0);
    CHECK(OceanSea::ShoreLimited(-3.0, 200.0) < -3.0 * 0.9999);   // troughs are limited the same way: it is symmetric
    CHECK(OceanSea::ShoreLimited(5.0, 0.0) == 0.0 && OceanSea::ShoreLimited(5.0, -2.0) == 0.0);   // dry land and below: no wave
    CHECK(OceanSea::ShoreLimited(0.0, 3.0) == 0.0);
    bool capped = true, monotonic = true, nearIdentity = true;
    for (double depth : { 0.2, 0.5, 1.0, 2.0, 5.0, 20.0 }) {
        const double limit = OceanSea::kShoreBreakRatio * depth;
        double previous = -1.0;
        for (double h = 0.0; h <= 30.0; h += 0.05) {
            const double limited = OceanSea::ShoreLimited(h, depth);
            capped = capped && limited <= limit + 1e-9;                 // never taller than 0.4 of the depth
            monotonic = monotonic && limited >= previous - 1e-12;       // a taller wave is never made SHORTER than a lower one
            previous = limited;
            if (h < limit / 8.0) nearIdentity = nearIdentity && std::fabs(limited - h) <= 1e-4 * h + 1e-12;
        }
    }
    CHECK(capped && monotonic && nearIdentity);
    CHECK(OceanSea::ShoreFactor(1.0, 2.0) > OceanSea::ShoreFactor(1.0, 1.0) && OceanSea::ShoreFactor(1.0, 1.0) > OceanSea::ShoreFactor(1.0, 0.5));
    CHECK(OceanSea::ShoreFactor(1.0, 0.5) > OceanSea::ShoreFactor(1.0, 0.1) && OceanSea::ShoreFactor(1.0, 0.1) > 0.0);
}

// What it costs, because the scene asks every frame: reported, and held to generous limits so a slow regression shows up. (The FFT mirror this
// replaced cost about 6 ms per frame whether or not anything was asked.)
static void TestCost()
{
    OceanSea::Sea sea;
    sea.Build(MakeSettings(12.0, 0.0, 1));
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    const int updates = 20;
    for (int i = 0; i < updates; ++i) sea.Update(1.0 + i);
    const double updateMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / updates;
    start = Clock::now();
    const int queries = 400;
    double sink = 0.0;
    for (int i = 0; i < queries; ++i) sink += sea.HeightAt(3.7 * i, -9.1 * i);
    const double queryUs = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / queries;
    std::printf("  %zu modes kept: one Update %.2f ms, one query %.1f us (sink %.1f)\n", sea.ModeCount(), updateMs, queryUs, sink);
    CHECK(sea.ModeCount() > 5000 && sea.ModeCount() < 40000);   // a few tenths of the 98 thousand
    CHECK(updateMs < 20.0 && queryUs < 2000.0);
}

int main()
{
    TestMatchesBruteForce(8.0, 0.0, 3.7);
    TestMatchesBruteForce(14.0, 0.0, 61.25);
    TestMatchesBruteForce(8.0, 25.0, 9.0);   // shallow: omega differs per mode
    TestMatchesFftAtTexels();
    TestUpdateIsAFunctionOfTime();
    TestEmptyAndCalm();
    TestBound();
    TestShoreLimit();
    TestCost();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ocean_sea_test: all passed\n");
    return 0;
}
