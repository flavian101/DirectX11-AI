// Unit tests for src/OceanGpuPack.h (roadmap O2, the GPU half): the data layout the evolve shader reads, and EvolvePacked, the C++ copy of
// shaders/ocean_evolve_cs.hlsl. The proof has two links: EvolvePacked + FFT gives the same sea as OceanFFT::Evolve (so the packing trick and the
// maths are right), and the shader is written line for line like EvolvePacked (checked by eye and compiled with DXC). No Direct3D, no framework:
// a failed CHECK prints and the program exits 1.
#include "OceanGpuPack.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using namespace OceanGpuPack;
using OceanFFT::Cx;

static SeaSpectrum::BandField MakeBand(int bandIndex, double wind, double depth)
{
    SeaSpectrum::Settings settings;
    settings.windSpeed = wind;
    settings.depth = depth;
    settings.windRadians = 0.7;
    settings.seed = 7;
    return SeaSpectrum::Make(settings, SeaSpectrum::DefaultBands()[static_cast<size_t>(bandIndex)], bandIndex);
}

// The layout: each mode carries h0(k) and conj(h0(-k)); the shader never has to find the opposite mode itself.
static void TestLayout()
{
    const SeaSpectrum::BandField field = MakeBand(2, 8.0, 0.0);
    const PackedBand packed = Pack(field);
    CHECK(packed.n == field.N());
    CHECK(packed.modes.size() == static_cast<size_t>(packed.n) * packed.n && packed.omega.size() == packed.modes.size());
    bool same = true;
    for (int j = 0; j < packed.n; j += 5) {
        for (int i = 0; i < packed.n; i += 3) {
            const size_t index = field.Index(i, j), opposite = field.Opposite(i, j);
            const Float4 m = packed.modes[index];
            same = same && m.x == field.h0[index].re && m.y == field.h0[index].im
                        && m.z == field.h0[opposite].re && m.w == -field.h0[opposite].im
                        && packed.omega[index] == field.omega[index];
        }
    }
    CHECK(same);
    CHECK(std::fabs(packed.DeltaK() - static_cast<float>(field.band.DeltaK())) < 1e-9f);
    CHECK(sizeof(EvolveConstants) == 16);
}

// Packing two real results into one complex FFT, then splitting them again, gives back both. The same maths as OceanFFT::Evolve, in 4 FFTs.
// Compared in metres against the double-precision reference; the float shader maths may differ from it only by rounding.
static void TestPackedMatchesReference(int bandIndex, double wind, double depth, double time, double choppiness)
{
    const SeaSpectrum::BandField field = MakeBand(bandIndex, wind, depth);
    const OceanFFT::Surface reference = OceanFFT::Evolve(field, time, choppiness);
    const PackedBand packed = Pack(field);
    const PackedSpectra spectra = EvolvePacked(packed, static_cast<float>(time));
    const int n = packed.n;
    const size_t count = static_cast<size_t>(n) * n;

    std::vector<Cx> p0(count), p1(count), p2(count), p3(count);
    for (size_t i = 0; i < count; ++i) {
        p0[i] = Cx(spectra.a[i].x, spectra.a[i].y);
        p1[i] = Cx(spectra.a[i].z, spectra.a[i].w);
        p2[i] = Cx(spectra.b[i].x, spectra.b[i].y);
        p3[i] = Cx(spectra.b[i].z, spectra.b[i].w);
    }
    for (std::vector<Cx>* g : { &p0, &p1, &p2, &p3 }) OceanFFT::Fft2D(*g, n, true);

    double scaleH = 0.0, scaleD = 0.0, scaleS = 0.0;
    double errH = 0.0, errDx = 0.0, errDz = 0.0, errSx = 0.0, errSz = 0.0, errJ = 0.0;
    for (size_t i = 0; i < count; ++i) {
        scaleH = std::max(scaleH, static_cast<double>(std::fabs(reference.height[i])));
        scaleD = std::max(scaleD, static_cast<double>(std::max(std::fabs(reference.displaceX[i]), std::fabs(reference.displaceZ[i]))));
        scaleS = std::max(scaleS, static_cast<double>(std::max(std::fabs(reference.slopeX[i]), std::fabs(reference.slopeZ[i]))));
        errH = std::max(errH, std::fabs(p0[i].real() - reference.height[i]));
        errDx = std::max(errDx, std::fabs(p0[i].imag() - reference.displaceX[i]));
        errDz = std::max(errDz, std::fabs(p1[i].real() - reference.displaceZ[i]));
        errSx = std::max(errSx, std::fabs(p1[i].imag() - reference.slopeX[i]));
        errSz = std::max(errSz, std::fabs(p2[i].real() - reference.slopeZ[i]));
        // The Jacobian is built from the three derivatives, exactly as the assemble pass will: J = (1 + l dxx)(1 + l dzz) - (l dxz)^2.
        const double jxx = 1.0 + choppiness * p2[i].imag(), jzz = 1.0 + choppiness * p3[i].real(), jxz = choppiness * p3[i].imag();
        errJ = std::max(errJ, std::fabs(jxx * jzz - jxz * jxz - reference.jacobian[i]));
    }
    // The waves are real, so there is something to compare, and the result agrees to about 1e-4 of the biggest value.
    CHECK(scaleH > 1e-3 && scaleD > 1e-3 && scaleS > 1e-4);
    CHECK(errH <= 1e-4 * scaleH);
    CHECK(errDx <= 1e-4 * scaleD && errDz <= 1e-4 * scaleD);
    CHECK(errSx <= 1e-4 * scaleS && errSz <= 1e-4 * scaleS);
    CHECK(errJ <= 1e-3);
    if (failures) std::printf("  band %d wind %.0f depth %.0f t %.1f: scale H %.4f D %.4f S %.5f, errors H %.2e D %.2e/%.2e S %.2e/%.2e J %.2e\n",
                              bandIndex, wind, depth, time, scaleH, scaleD, scaleS, errH, errDx, errDz, errSx, errSz, errJ);
}

// Nothing the shader writes is NaN or infinite, in any band, and the unused modes (the zero wave vector, anything with no energy) are exactly zero.
static void TestFiniteAndZeroModes()
{
    for (int band = 0; band < 3; ++band) {
        const PackedBand packed = Pack(MakeBand(band, 12.0, 0.0));
        const PackedSpectra spectra = EvolvePacked(packed, 123.456f);
        bool finite = true;
        for (size_t i = 0; i < spectra.a.size(); ++i) {
            for (float v : { spectra.a[i].x, spectra.a[i].y, spectra.a[i].z, spectra.a[i].w, spectra.b[i].x, spectra.b[i].y, spectra.b[i].z, spectra.b[i].w })
                finite = finite && std::isfinite(v);
        }
        CHECK(finite);
        const Float4 dc = spectra.a[0], dcB = spectra.b[0];
        CHECK(dc.x == 0.0f && dc.y == 0.0f && dc.z == 0.0f && dc.w == 0.0f && dcB.x == 0.0f && dcB.y == 0.0f && dcB.z == 0.0f && dcB.w == 0.0f);
    }
}

int main()
{
    TestLayout();
    TestPackedMatchesReference(0, 8.0, 0.0, 3.7, 0.8);     // swell, deep water
    TestPackedMatchesReference(1, 12.0, 0.0, 41.25, 1.0);  // wind sea, a strong wind, a long time in, full choppiness
    TestPackedMatchesReference(2, 5.0, 0.0, 0.0, 0.0);     // ripples at t = 0, no choppiness (Jacobian is 1 everywhere)
    TestPackedMatchesReference(1, 8.0, 25.0, 9.0, 0.6);    // shallow water: the dispersion relation changes each mode's omega
    TestFiniteAndZeroModes();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ocean_gpu_pack_test: all passed\n");
    return 0;
}
