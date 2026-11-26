// Unit tests for src/OceanGpuFft.h (roadmap O2, the GPU half): the C++ copy of shaders/ocean_fft_cs.hlsl. Its butterflies never clash, one line of it is an
// FFT (two at once, one in each half of every float4), the two passes are the 2-D FFT, and the whole chain evolve -> FFT gives OceanFFT::Evolve's sea.
// No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "OceanGpuFft.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using namespace OceanGpuFft;
using OceanFFT::Cx;
using OceanGpuPack::PackedSpectra;

// A repeatable pile of numbers that is not a pattern the FFT could hide a bug in.
static double Diff(double a, double b) { return std::fabs(a - b); }   // both sides in double, whatever the inputs were
static float Noise(size_t i, int salt) { return static_cast<float>(std::sin(0.7311 * i + 1.9 * salt) + 0.5 * std::cos(2.3 * i * salt + 0.31)); }

static void TestBitReverse()
{
    CHECK(BitReverse(0, 3) == 0 && BitReverse(1, 3) == 4 && BitReverse(3, 3) == 6 && BitReverse(6, 3) == 3 && BitReverse(7, 3) == 7);
    CHECK(BitReverse(1, 8) == 128 && BitReverse(2, 8) == 64);
    bool involution = true, permutation = true;
    for (int log2n = 1; log2n <= 9; ++log2n) {
        std::vector<int> seen(static_cast<size_t>(1) << log2n, 0);
        for (std::uint32_t i = 0; i < (1u << log2n); ++i) {
            involution = involution && BitReverse(BitReverse(i, log2n), log2n) == i;
            ++seen[BitReverse(i, log2n)];
        }
        for (int count : seen) permutation = permutation && count == 1;
    }
    CHECK(involution && permutation);   // reversing twice is the identity, and it visits every slot exactly once
    CHECK(Log2(1) == 0 && Log2(2) == 1 && Log2(128) == 7 && Log2(512) == 9);
}

// Every stage: each element is in exactly one butterfly. That is what makes it safe for the threads of a group to work at the same moment.
static void TestButterfliesNeverClash()
{
    bool disjoint = true, inRange = true;
    for (int n = 2; n <= kMaxN; n <<= 1) {
        for (int halfLen = 1; halfLen < n; halfLen <<= 1) {
            std::vector<int> touched(static_cast<size_t>(n), 0);
            for (int b = 0; b < n / 2; ++b) {
                int i0, i1, k;
                Butterfly(b, halfLen, i0, i1, k);
                inRange = inRange && i0 >= 0 && i1 < n && i1 == i0 + halfLen && k >= 0 && k < halfLen;
                if (i0 >= 0 && i0 < n) ++touched[static_cast<size_t>(i0)];
                if (i1 >= 0 && i1 < n) ++touched[static_cast<size_t>(i1)];
            }
            for (int count : touched) disjoint = disjoint && count == 1;
        }
    }
    CHECK(inRange && disjoint);
}

// One line: both complex sequences in a float4 are transformed as OceanFFT::Fft1D would, for lines shorter than, equal to and longer than the
// group (n = 64, 128, 256, 512 against 128 threads).
static void TestLine(int n)
{
    std::vector<Float4> line(static_cast<size_t>(n));
    std::vector<Cx> first(static_cast<size_t>(n)), second(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        line[static_cast<size_t>(i)] = Float4{ Noise(i, 1), Noise(i, 2), Noise(i, 3), Noise(i, 4) };
        first[static_cast<size_t>(i)] = Cx(line[static_cast<size_t>(i)].x, line[static_cast<size_t>(i)].y);
        second[static_cast<size_t>(i)] = Cx(line[static_cast<size_t>(i)].z, line[static_cast<size_t>(i)].w);
    }
    OceanFFT::Fft1D(first.data(), n, true);
    OceanFFT::Fft1D(second.data(), n, true);
    FftGroup(line, n);
    double worst = 0.0, scale = 0.0;
    for (int i = 0; i < n; ++i) {
        const Float4 v = line[static_cast<size_t>(i)];
        worst = std::max({ worst, Diff(v.x, first[static_cast<size_t>(i)].real()), Diff(v.y, first[static_cast<size_t>(i)].imag()),
                           Diff(v.z, second[static_cast<size_t>(i)].real()), Diff(v.w, second[static_cast<size_t>(i)].imag()) });
        scale = std::max(scale, std::abs(first[static_cast<size_t>(i)]));
    }
    CHECK(scale > 1.0);
    CHECK(worst <= 1e-5 * scale);
    if (worst > 1e-5 * scale) std::printf("  line n=%d: worst %.3e, scale %.3f\n", n, worst, scale);
}

// Rows then columns is the 2-D FFT.
static void TestTransform2D(int n)
{
    const size_t count = static_cast<size_t>(n) * n;
    std::vector<Float4> texture(count);
    std::vector<Cx> first(count), second(count);
    for (size_t i = 0; i < count; ++i) {
        texture[i] = Float4{ Noise(i, 5), Noise(i, 6), Noise(i, 7), Noise(i, 8) };
        first[i] = Cx(texture[i].x, texture[i].y);
        second[i] = Cx(texture[i].z, texture[i].w);
    }
    OceanFFT::Fft2D(first, n, true);
    OceanFFT::Fft2D(second, n, true);
    const std::vector<Float4> result = Transform2D(texture, n);
    double worst = 0.0, scale = 0.0;
    for (size_t i = 0; i < count; ++i) {
        worst = std::max({ worst, Diff(result[i].x, first[i].real()), Diff(result[i].y, first[i].imag()),
                           Diff(result[i].z, second[i].real()), Diff(result[i].w, second[i].imag()) });
        scale = std::max(scale, std::abs(first[i]));
    }
    CHECK(scale > 1.0);
    CHECK(worst <= 1e-5 * scale);
    // Rows and columns are different passes: a texture with one bright texel gives a plane wave only if BOTH passes ran, in the right direction.
    std::vector<Float4> impulse(count);
    impulse[static_cast<size_t>(1) * n + 2] = Float4{ 1.0f, 0.0f, 0.0f, 0.0f };   // mode (x = 2, z = 1)
    const std::vector<Float4> wave = Transform2D(impulse, n);
    double worstWave = 0.0;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const double phase = 2.0 * SeaState::kPi * (2.0 * x + 1.0 * z) / n;
            const Float4 v = wave[static_cast<size_t>(z) * n + x];
            worstWave = std::max({ worstWave, Diff(v.x, std::cos(phase)), Diff(v.y, std::sin(phase)) });
        }
    CHECK(worstWave < 1e-5);
}

// The whole chain the GPU runs: pack, evolve, FFT. Its fields are the ones OceanFFT::Evolve returns.
static void TestChain(int bandIndex, double wind, double depth, double time, double choppiness)
{
    SeaSpectrum::Settings settings;
    settings.windSpeed = wind;
    settings.depth = depth;
    settings.windRadians = 0.4;
    settings.seed = 3;
    const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, SeaSpectrum::DefaultBands()[static_cast<size_t>(bandIndex)], bandIndex);
    const OceanFFT::Surface reference = OceanFFT::Evolve(field, time, choppiness);
    const OceanGpuPack::PackedBand packed = OceanGpuPack::Pack(field);
    const PackedSpectra spectra = OceanGpuPack::EvolvePacked(packed, static_cast<float>(time));
    const int n = packed.n;
    const std::vector<Float4> a = Transform2D(spectra.a, n), b = Transform2D(spectra.b, n);

    double scaleH = 0.0, scaleD = 0.0, scaleS = 0.0, errH = 0.0, errD = 0.0, errS = 0.0, errJ = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        // a = (height, dispX, dispZ, slopeX), b = (slopeZ, dXdx, dZdz, dXdz): the real and imaginary parts of the four packed spectra.
        scaleH = std::max(scaleH, static_cast<double>(std::fabs(reference.height[i])));
        scaleD = std::max(scaleD, static_cast<double>(std::max(std::fabs(reference.displaceX[i]), std::fabs(reference.displaceZ[i]))));
        scaleS = std::max(scaleS, static_cast<double>(std::max(std::fabs(reference.slopeX[i]), std::fabs(reference.slopeZ[i]))));
        errH = std::max(errH, Diff(a[i].x, reference.height[i]));
        errD = std::max({ errD, Diff(a[i].y, reference.displaceX[i]), Diff(a[i].z, reference.displaceZ[i]) });
        errS = std::max({ errS, Diff(a[i].w, reference.slopeX[i]), Diff(b[i].x, reference.slopeZ[i]) });
        const double jxx = 1.0 + choppiness * b[i].y, jzz = 1.0 + choppiness * b[i].z, jxz = choppiness * b[i].w;
        errJ = std::max(errJ, Diff(jxx * jzz - jxz * jxz, reference.jacobian[i]));
    }
    CHECK(scaleH > 1e-3 && scaleD > 1e-3 && scaleS > 1e-4);
    CHECK(errH <= 1e-4 * scaleH && errD <= 1e-4 * scaleD && errS <= 1e-4 * scaleS);
    CHECK(errJ <= 1e-3);
    if (failures) std::printf("  band %d t %.1f: errors H %.2e D %.2e S %.2e J %.2e (scales %.3f %.3f %.4f)\n", bandIndex, time, errH, errD, errS, errJ, scaleH, scaleD, scaleS);
}

int main()
{
    TestBitReverse();
    TestButterfliesNeverClash();
    for (int n : { 2, 8, 64, 128, 256, 512 }) TestLine(n);
    TestTransform2D(32);
    TestTransform2D(128);
    TestChain(0, 8.0, 0.0, 3.7, 0.8);      // swell (128 x 128)
    TestChain(1, 12.0, 0.0, 41.25, 1.0);   // wind sea (256 x 256): lines longer than the 128 threads of a group
    TestChain(2, 5.0, 0.0, 0.0, 0.0);      // ripples at t = 0
    TestChain(1, 8.0, 25.0, 9.0, 0.6);     // shallow water
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ocean_gpu_fft_test: all passed\n");
    return 0;
}
