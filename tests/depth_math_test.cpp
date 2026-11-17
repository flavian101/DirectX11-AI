// Tests for src/DepthMath.h: the reversed-Z depth convention. No Direct3D, no framework.
//   1. The ends: the near plane is depth 1, the far plane is depth 0, and depth falls as things get farther.
//   2. LinearizeReversed undoes ReversedDepth (the water shader depends on it).
//   3. WHY we reversed: at 4 km a 32-bit float depth buffer resolves metres with the standard mapping and
//      sub-centimetres with the reversed one.
#include "DepthMath.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static const double kNear = 0.1, kFar = 10000.0;

static void TestEnds()
{
    CHECK(std::fabs(DepthMath::ReversedDepth(kNear, kNear, kFar) - 1.0) < 1e-12);
    CHECK(std::fabs(DepthMath::ReversedDepth(kFar, kNear, kFar)) < 1e-12);
    double previous = 2.0;
    for (double z = kNear; z <= kFar; z *= 1.5) {   // farther -> smaller depth, always
        const double d = DepthMath::ReversedDepth(z, kNear, kFar);
        CHECK(d < previous);
        previous = d;
    }
}

static void TestRoundTrip()
{
    for (double z : { 0.1, 0.5, 3.0, 40.0, 900.0, 4000.0, 9999.0 }) {
        const double back = DepthMath::LinearizeReversed(DepthMath::ReversedDepth(z, kNear, kFar), kNear, kFar);
        CHECK(std::fabs(back - z) < 1e-9 * z);
    }
    CHECK(std::fabs(DepthMath::LinearizeReversed(DepthMath::kClear, kNear, kFar) - kFar) < 1e-6);   // an untouched pixel reads as "at the far plane"
}

// How far apart (metres, along the view direction) two surfaces at distance z must be before a D32_FLOAT depth buffer can
// store different values for them: the gap between two neighbouring floats at this depth, divided by how fast depth
// changes with distance there.
template <class DepthFn>
static double ResolutionAt(DepthFn depth, double z)
{
    const float stored = static_cast<float>(depth(z));
    const double gap = std::fabs(static_cast<double>(std::nextafter(stored, 2.0f)) - static_cast<double>(stored));
    const double h = z * 1e-3;
    const double slope = std::fabs(depth(z + h) - depth(z - h)) / (2.0 * h);
    return gap / slope;
}

static void TestPrecision()
{
    auto standard = [](double z) { return DepthMath::StandardDepth(z, kNear, kFar); };
    auto reversed = [](double z) { return DepthMath::ReversedDepth(z, kNear, kFar); };
    const double standardStep = ResolutionAt(standard, 4000.0);
    const double reversedStep = ResolutionAt(reversed, 4000.0);
    std::printf("at 4000 m: standard resolution %.3f m, reversed resolution %.6f m\n", standardStep, reversedStep);
    CHECK(standardStep > 5.0);      // metres: z-fighting
    CHECK(reversedStep < 0.01);     // under a centimetre
}

int main()
{
    TestEnds();
    TestRoundTrip();
    TestPrecision();
    if (g_failures == 0) std::printf("depth_math_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
