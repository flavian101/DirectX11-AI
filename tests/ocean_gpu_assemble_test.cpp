// Unit tests for src/OceanGpuAssemble.h (roadmap O2, the GPU half): the C++ copy of shaders/ocean_assemble_cs.hlsl and the half-float reader the GPU
// check uses. The chain pack -> evolve -> FFT -> assemble must give the maps the water reads: the choppy displacement, the height, the slopes and the
// Jacobian of OceanFFT::Evolve. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "OceanGpuAssemble.h"
#include "OceanGpuFft.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using namespace OceanGpuAssemble;

static double Diff(double a, double b) { return std::fabs(a - b); }

static void TestHalfToFloat()
{
    CHECK(HalfToFloat(0x0000) == 0.0f && HalfToFloat(0x3C00) == 1.0f && HalfToFloat(0xBC00) == -1.0f && HalfToFloat(0xC000) == -2.0f);
    CHECK(HalfToFloat(0x3800) == 0.5f && HalfToFloat(0x4000) == 2.0f && HalfToFloat(0x4900) == 10.0f);
    CHECK(HalfToFloat(0x7BFF) == 65504.0f);                          // the largest half
    CHECK(HalfToFloat(0x0001) == std::ldexp(1.0f, -24));             // the smallest subnormal
    CHECK(HalfToFloat(0x0400) == std::ldexp(1.0f, -14));             // the smallest normal
    CHECK(std::isinf(HalfToFloat(0x7C00)) && HalfToFloat(0x7C00) > 0 && std::isinf(HalfToFloat(0xFC00)) && HalfToFloat(0xFC00) < 0);
    CHECK(std::isnan(HalfToFloat(0x7E00)));
    CHECK(Diff(HalfToFloat(0x3555), 1.0 / 3.0) < 2e-4);              // the half nearest 1/3
    // Counting up through the bit patterns, the values only ever grow (that is what makes the format sortable), and the sign bit just negates.
    bool increasing = true, symmetric = true;
    float previous = HalfToFloat(0);
    for (std::uint32_t h = 1; h <= 0x7BFF; ++h) {
        const float v = HalfToFloat(static_cast<std::uint16_t>(h));
        increasing = increasing && v > previous;
        symmetric = symmetric && HalfToFloat(static_cast<std::uint16_t>(h | 0x8000)) == -v;
        previous = v;
    }
    CHECK(increasing && symmetric);
}

static void TestTexelLayout()
{
    const Float4 a{ 1.0f, 2.0f, 3.0f, 4.0f }, b{ 5.0f, 0.5f, -0.25f, 0.125f };
    const Texel t = AssembleTexel(a, b, 0.5f);
    CHECK(t.displacement.x == 1.0f && t.displacement.y == 1.0f && t.displacement.z == 1.5f && t.displacement.w == 0.0f);   // 0.5 * 2, height 1, 0.5 * 3
    CHECK(t.slope.x == 4.0f && t.slope.y == 5.0f && t.slope.w == 0.0f);
    // J = (1 + 0.5 * 0.5)(1 + 0.5 * -0.25) - (0.5 * 0.125)^2 = 1.25 * 0.875 - 0.00390625
    CHECK(Diff(t.slope.z, 1.25 * 0.875 - 0.00390625) < 1e-7);
    // No choppiness: nothing moves sideways and the Jacobian is exactly 1, whatever the derivatives are.
    const Texel flat = AssembleTexel(a, b, 0.0f);
    CHECK(flat.displacement.x == 0.0f && flat.displacement.z == 0.0f && flat.displacement.y == 1.0f && flat.slope.z == 1.0f);
    CHECK(sizeof(AssembleConstants) == 16);
}

// The whole chain the GPU runs, texel by texel against the double-precision reference.
static void TestChain(int bandIndex, double wind, double depth, double time, double choppiness)
{
    SeaSpectrum::Settings settings;
    settings.windSpeed = wind;
    settings.depth = depth;
    settings.windRadians = 1.1;
    settings.seed = 5;
    const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, SeaSpectrum::DefaultBands()[static_cast<size_t>(bandIndex)], bandIndex);
    const OceanFFT::Surface reference = OceanFFT::Evolve(field, time, choppiness);
    const OceanGpuPack::PackedBand packed = OceanGpuPack::Pack(field);
    const OceanGpuPack::PackedSpectra spectra = OceanGpuPack::EvolvePacked(packed, static_cast<float>(time));
    const int n = packed.n;
    const std::vector<Float4> a = OceanGpuFft::Transform2D(spectra.a, n), b = OceanGpuFft::Transform2D(spectra.b, n);

    double scaleD = 0.0, scaleH = 0.0, scaleS = 0.0, errD = 0.0, errH = 0.0, errS = 0.0, errJ = 0.0, smallestJ = 1e9;
    for (size_t i = 0; i < a.size(); ++i) {
        const Texel t = AssembleTexel(a[i], b[i], static_cast<float>(choppiness));
        scaleD = std::max(scaleD, choppiness * std::max(std::fabs(reference.displaceX[i]), std::fabs(reference.displaceZ[i])));
        scaleH = std::max(scaleH, static_cast<double>(std::fabs(reference.height[i])));
        scaleS = std::max(scaleS, static_cast<double>(std::max(std::fabs(reference.slopeX[i]), std::fabs(reference.slopeZ[i]))));
        errD = std::max({ errD, Diff(t.displacement.x, choppiness * reference.displaceX[i]), Diff(t.displacement.z, choppiness * reference.displaceZ[i]) });
        errH = std::max(errH, Diff(t.displacement.y, reference.height[i]));
        errS = std::max({ errS, Diff(t.slope.x, reference.slopeX[i]), Diff(t.slope.y, reference.slopeZ[i]) });
        errJ = std::max(errJ, Diff(t.slope.z, reference.jacobian[i]));
        smallestJ = std::min(smallestJ, static_cast<double>(reference.jacobian[i]));
    }
    CHECK(scaleH > 1e-3 && scaleS > 1e-4);
    if (choppiness > 0.0) CHECK(scaleD > 1e-3);
    CHECK(errH <= 1e-4 * scaleH && errS <= 1e-4 * scaleS && errD <= 1e-4 * std::max(scaleD, 1e-3));
    CHECK(errJ <= 1e-3);
    if (choppiness == 0.0) CHECK(smallestJ == 1.0);   // a flat map: no folding anywhere
    if (failures) std::printf("  band %d t %.1f c %.1f: errors D %.2e H %.2e S %.2e J %.2e\n", bandIndex, time, choppiness, errD, errH, errS, errJ);
}

int main()
{
    TestHalfToFloat();
    TestTexelLayout();
    TestChain(0, 8.0, 0.0, 3.7, 0.8);
    TestChain(1, 12.0, 0.0, 41.25, 1.0);
    TestChain(2, 5.0, 0.0, 0.0, 0.0);
    TestChain(1, 8.0, 25.0, 9.0, 0.6);
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ocean_gpu_assemble_test: all passed\n");
    return 0;
}
