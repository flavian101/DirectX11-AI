// Tests for src/HeightDetail.{h,cpp} (roadmap H5): detail noise as an additive layer in metres. No Direct3D, no framework.
#include "HeightDetail.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static HeightDetailSettings Make(float strength, float wavelength, int octaves, bool ridged, int seed = 1)
{
    HeightDetailSettings s; s.strength = strength; s.wavelength = wavelength; s.octaves = octaves; s.ridged = ridged; s.seed = seed;
    return s;
}

static void TestIdentityAndLimits()
{
    // Strength 0 adds exactly nothing, anywhere, whatever the other numbers are (even wild ones).
    bool zero = true;
    for (double x = -5000.0; x < 5000.0; x += 731.3) for (double z = -5000.0; z < 5000.0; z += 977.1) {
        if (DetailMetres(Make(0.0f, 64.0f, 4, false), x, z) != 0.0 || DetailMetres(Make(0.0f, 1e9f, 99, true), x, z) != 0.0) zero = false;
        if (DetailMetres(Make(-3.0f, 64.0f, 4, false), x, z) != 0.0 || DetailMetres(Make(std::nanf(""), 64.0f, 4, false), x, z) != 0.0) zero = false;
    }
    CHECK(zero && Make(0.0f, 64, 4, false).IsIdentity() && !Make(0.5f, 64, 4, false).IsIdentity());

    HeightDetailSettings wild = Make(1e9f, -4.0f, 99, false);
    wild.Clamp();
    CHECK(wild.strength == HeightDetailSettings::kStrengthMax && wild.wavelength == HeightDetailSettings::kWavelengthMin && wild.octaves == HeightDetailSettings::kOctavesMax);

    // Never beyond +/- strength, in either style, at any octave count.
    for (int ridged = 0; ridged < 2; ++ridged) for (int octaves : { 1, 2, 4, 8 }) {
        double lowest = 1e9, highest = -1e9;
        for (double x = -3000.0; x < 3000.0; x += 7.9) for (double z = -3000.0; z < 3000.0; z += 8.3) {
            const double v = DetailMetres(Make(10.0f, 64.0f, octaves, ridged != 0), x, z); lowest = (std::min)(lowest, v); highest = (std::max)(highest, v);
        }
        CHECK(lowest >= -10.0 && highest <= 10.0);
        CHECK(highest - lowest > 5.0);   // and it really uses its strength: a layer that is always near 0 is no detail
    }
}

static void TestAveragesOutToZero()
{
    // Adding detail must not lift or sink the terrain. Mean over a 3 km x 3 km area (about 47 wavelengths across), as a share of the strength.
    for (int ridged = 0; ridged < 2; ++ridged) for (int octaves : { 1, 4 }) for (int seed : { 1, 2 }) {
        double sum = 0.0; long count = 0;
        for (double x = -1500.0; x < 1500.0; x += 3.1) for (double z = -1500.0; z < 1500.0; z += 3.3) { sum += DetailMetres(Make(10.0f, 64.0f, octaves, ridged != 0, seed), x, z); ++count; }
        CHECK(std::fabs(sum / count) < 0.05 * 10.0);
    }
}

static void TestSmoothAndBandLimited()
{
    // No crease or jump anywhere, including across x = 0 and z = 0 where careless rounding of negative numbers breaks (checked on a fine step).
    // Value noise through the quintic has slope below 1.875 x 2 per wavelength per octave; with halving sizes and heights the sum is bounded by
    // strength * 3.75 * octaves / (wavelength * (sum of the octave heights)) per metre. A finer octave than promised would break the bound.
    for (int octaves : { 1, 3, 4 }) {
        const double wavelength = 64.0, strength = 10.0, d = 0.01;
        double amplitudeSum = 0.0; for (int k = 0; k < octaves; ++k) amplitudeSum += std::pow(0.5, k);
        const double slopeBound = strength * 3.75 * octaves / (wavelength * amplitudeSum);
        double worst = 0.0;
        for (double x = -200.0; x < 200.0; x += 0.37) for (double z = -3.0; z < 3.0; z += 0.9)
            worst = (std::max)({ worst, std::fabs(DetailMetres(Make(10.0f, 64.0f, octaves, false), x + d, z) - DetailMetres(Make(10.0f, 64.0f, octaves, false), x, z)) / d,
                                        std::fabs(DetailMetres(Make(10.0f, 64.0f, octaves, false), z, x + d) - DetailMetres(Make(10.0f, 64.0f, octaves, false), z, x)) / d });
        CHECK(worst <= slopeBound * 1.001);
        CHECK(worst > 0.2 * slopeBound);   // and the bound is not vacuous: the detail really is that steep somewhere
    }
    // Across the origin, in tiny steps, no jump larger than the slope allows.
    const HeightDetailSettings s = Make(10.0f, 8.0f, 4, false);
    double biggestStep = 0.0;
    for (double x = -0.5; x < 0.5; x += 1e-4) biggestStep = (std::max)(biggestStep, std::fabs(DetailMetres(s, x + 1e-4, -1e-9) - DetailMetres(s, x, 1e-9)));
    CHECK(biggestStep < 1e-4 * 10.0 * 3.75 * 4 / 8.0 + 1e-9);
}

static void TestSameSpotSameValue()
{
    const HeightDetailSettings s = Make(10.0f, 64.0f, 4, true, 5);
    // A pure function of the position: asking twice, far from the origin, in any order, gives the same bits.
    bool same = true;
    for (double x = -2.0e6; x < 2.0e6; x += 7.3e5) for (double z = -2.0e6; z < 2.0e6; z += 9.1e5) if (DetailMetres(s, x, z) != DetailMetres(s, x, z)) same = false;
    CHECK(same);
    // Another seed gives other bumps; a ridged layer differs from a rounded one.
    int different = 0, differentStyle = 0;
    for (double x = 0.0; x < 500.0; x += 13.7) { if (DetailMetres(Make(10.0f, 64.0f, 4, false, 1), x, 21.0) != DetailMetres(Make(10.0f, 64.0f, 4, false, 2), x, 21.0)) ++different; if (DetailMetres(Make(10.0f, 64.0f, 4, false), x, 21.0) != DetailMetres(Make(10.0f, 64.0f, 4, true), x, 21.0)) ++differentStyle; }
    CHECK(different > 30 && differentStyle > 30);
}

int main()
{
    TestIdentityAndLimits();
    TestAveragesOutToZero();
    TestSmoothAndBandLimited();
    TestSameSpotSameValue();
    if (g_failures == 0) std::printf("All height detail tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
