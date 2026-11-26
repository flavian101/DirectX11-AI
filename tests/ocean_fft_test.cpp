// Unit tests for src/OceanFFT.h (roadmap O2): the FFT is an FFT, a wave moves at its dispersion speed in the right direction, the sea is real and has the
// spectrum's variance, slopes match the heights, and the Jacobian says where crests fold. No Direct3D, no framework: a failed CHECK prints and the program
// exits 1.
#include "OceanFFT.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
static bool NearRel(double a, double b, double r) { return std::fabs(a - b) <= r * std::max(std::fabs(a), std::fabs(b)); }

using namespace OceanFFT;

// The inverse of a single mode is a pure cosine; forward after inverse gives the input back (times n * n).
static void TestFft()
{
    const int n = 16;
    std::vector<Cx> data(static_cast<size_t>(n) * n);
    data[3] = Cx(1.0, 0.0);                                   // mode (3, 0)
    data[static_cast<size_t>(n - 3)] = Cx(1.0, 0.0);          // and its partner (-3, 0): a real wave
    Fft2D(data, n, true);
    double worstImag = 0.0, worstShape = 0.0;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const Cx v = data[static_cast<size_t>(z) * n + x];
            worstImag = std::max(worstImag, std::fabs(v.imag()));
            worstShape = std::max(worstShape, std::fabs(v.real() - 2.0 * std::cos(2.0 * SeaState::kPi * 3.0 * x / n)));
        }
    CHECK(worstImag < 1e-12 && worstShape < 1e-12);
    // Round trip on a random-ish array.
    std::vector<Cx> original(static_cast<size_t>(n) * n);
    for (size_t i = 0; i < original.size(); ++i) original[i] = Cx(std::sin(0.37 * i), std::cos(1.3 * i));
    std::vector<Cx> copy = original;
    Fft2D(copy, n, true);
    Fft2D(copy, n, false);
    double worst = 0.0;
    for (size_t i = 0; i < copy.size(); ++i) worst = std::max(worst, std::abs(copy[i] / static_cast<double>(n * n) - original[i]));
    CHECK(worst < 1e-12);
}

// A band with one wave in it: mode (i, 0), a known amplitude and phase.
static SeaSpectrum::BandField OneWave(int n, double patch, int mode, double depth, float re, float im)
{
    SeaSpectrum::BandField field;
    field.band.patchSize = patch;
    field.band.resolution = n;
    field.h0.assign(static_cast<size_t>(n) * n, SeaSpectrum::Complex{});
    field.omega.assign(static_cast<size_t>(n) * n, 0.0f);
    const double k = field.band.DeltaK() * mode;
    const size_t index = field.Index(mode, 0);
    field.omega[index] = static_cast<float>(SeaSpectrum::Omega(k, depth));
    field.omega[field.Opposite(mode, 0)] = field.omega[index];
    field.h0[index] = SeaSpectrum::Complex{ re, im };
    return field;
}

// A wave travels toward +k at omega / k, deep or shallow, and keeps its height.
static void TestWaveMoves()
{
    for (double depth : { 0.0, 6.0 }) {
        const int n = 64, mode = 4;
        const double patch = 100.0;
        const SeaSpectrum::BandField field = OneWave(n, patch, mode, depth, 0.5f, 0.0f);
        const double k = field.band.DeltaK() * mode, omega = field.omega[field.Index(mode, 0)], speed = omega / k;
        // The wave's phase from the samples along x (a projection on cos kx and sin kx), so the answer is not quantised to the sample spacing: h = a cos(k x - psi).
        auto measure = [&](double t, double& amplitude) {
            const Surface surface = Evolve(field, t, 0.0);
            double c = 0.0, sn = 0.0;
            for (int i = 0; i < n; ++i) {
                const double x = surface.Spacing() * i, h = surface.height[surface.Index(i, 0)];
                c += h * std::cos(k * x);
                sn += h * std::sin(k * x);
            }
            amplitude = 2.0 * std::sqrt(c * c + sn * sn) / n;
            return std::atan2(sn, c);   // psi: the crest is at x = psi / k (mod the wavelength)
        };
        double amplitudeNow = 0.0, amplitudeLater = 0.0;
        const double psi0 = measure(0.0, amplitudeNow);
        const double dt = 0.2 * 2.0 * SeaState::kPi / omega;   // a fifth of a period
        double psi1 = measure(dt, amplitudeLater);
        double moved = (psi1 - psi0) / k;
        const double wavelength = 2.0 * SeaState::kPi / k;
        while (moved < 0.0) moved += wavelength;
        std::printf("depth %.0f: speed %.3f m/s, crest moved %.3f m (expected %.3f)\n", depth, speed, moved, speed * dt);
        CHECK(Near(moved, speed * dt, 0.01));   // toward +x, at omega / k
        CHECK(Near(amplitudeNow, 1.0, 0.01) && Near(amplitudeLater, 1.0, 0.01));   // 2 |h0| = 1 m, and it does not shrink
    }
    // Shallow water is slower for the same wave number.
    const SeaState::Summary deep = SeaState::Describe(10.0, 0.0), shallow = SeaState::Describe(10.0, 0.0, 5.0);
    CHECK(shallow.peakSpeed < deep.peakSpeed);
}

// The sea is real, repeats over the patch, and has the spectrum's variance (Parseval), at every time.
static void TestSea()
{
    SeaSpectrum::Settings settings;
    settings.windSpeed = 11.0;
    const std::vector<SeaSpectrum::Band> bands = SeaSpectrum::DefaultBands();
    const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bands[1], 1);
    double mean = 0.0, meanSquare = 0.0;
    int samples = 0;
    for (double t : { 0.0, 2.3, 5.1, 9.7 }) {
        const Surface s = Evolve(field, t, 0.8);
        for (float v : s.height) { mean += v; meanSquare += static_cast<double>(v) * v; ++samples; }
    }
    mean /= samples; meanSquare /= samples;
    const double expected = SeaSpectrum::RealizedVariance(field);
    std::printf("band 1: mean %.4f, mean square %.4f, spectrum (this seed) %.4f\n", mean, meanSquare, expected);
    CHECK(std::fabs(mean) < 0.05 * std::sqrt(meanSquare));
    CHECK(NearRel(meanSquare, expected, 0.15));
    // Periodic in the patch: one patch over, the same height.
    const Surface s = Evolve(field, 1.0, 0.0);
    CHECK(Near(s.HeightAt(37.3, 12.9), s.HeightAt(37.3 + bands[1].patchSize, 12.9 - bands[1].patchSize), 1e-4));
    // The bands added: the same sea for anybody who asks the same question.
    std::vector<Surface> all;
    for (size_t b = 0; b < bands.size(); ++b) all.push_back(Evolve(SeaSpectrum::Make(settings, bands[b], static_cast<int>(b)), 3.0, 0.0));
    std::vector<Surface> again;
    for (size_t b = 0; b < bands.size(); ++b) again.push_back(Evolve(SeaSpectrum::Make(settings, bands[b], static_cast<int>(b)), 3.0, 0.0));
    CHECK(SeaHeightAt(all, 123.4, -56.7) == SeaHeightAt(again, 123.4, -56.7));
    // And the pattern does not repeat inside a patch of the biggest band: heights 1 km apart differ.
    CHECK(std::fabs(SeaHeightAt(all, 10.0, 10.0) - SeaHeightAt(all, 1010.0, 10.0)) > 1e-3);
}

// The spectral slope is the exact derivative of the height: for a single wave h = a cos(k x - psi), the slope is -a k sin(k x - psi).
static void TestSlope()
{
    const int n = 64, mode = 5;
    const SeaSpectrum::BandField field = OneWave(n, 100.0, mode, 0.0, 0.3f, -0.2f);
    const double k = field.band.DeltaK() * mode;
    const Surface s = Evolve(field, 1.234, 0.0);
    double c = 0.0, sn = 0.0;
    for (int i = 0; i < n; ++i) { const double x = s.Spacing() * i; c += s.height[s.Index(i, 0)] * std::cos(k * x); sn += s.height[s.Index(i, 0)] * std::sin(k * x); }
    const double amplitude = 2.0 * std::sqrt(c * c + sn * sn) / n, psi = std::atan2(sn, c);
    double worst = 0.0, worstZ = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = s.Spacing() * i;
        worst = std::max(worst, std::fabs(-amplitude * k * std::sin(k * x - psi) - s.slopeX[s.Index(i, 0)]));
        worstZ = std::max(worstZ, std::fabs(static_cast<double>(s.slopeZ[s.Index(i, 0)])));
    }
    std::printf("slope: worst difference %.2e against largest slope %.4f\n", worst, amplitude * k);
    CHECK(worst < 1e-6);
    CHECK(worstZ < 1e-6);   // a wave along x has no slope across it
}

// The Jacobian: 1 with no choppiness, falling as it grows, and negative (a fold) where a steep sea is pushed hard.
static void TestJacobian()
{
    SeaSpectrum::Settings settings;
    settings.windSpeed = 14.0;
    const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, SeaSpectrum::DefaultBands()[1], 1);
    double previousMin = 1e9;
    for (double lambda : { 0.0, 0.3, 0.7, 1.5 }) {
        const Surface s = Evolve(field, 2.0, lambda);
        double lowest = 1e9, highest = -1e9;
        for (float j : s.jacobian) { lowest = std::min<double>(lowest, j); highest = std::max<double>(highest, j); }
        std::printf("choppiness %.1f: jacobian %.3f .. %.3f\n", lambda, lowest, highest);
        if (lambda == 0.0) CHECK(Near(lowest, 1.0, 1e-9) && Near(highest, 1.0, 1e-9));
        CHECK(lowest <= previousMin + 1e-9);
        previousMin = lowest;
    }
    // A storm pushed hard folds somewhere; a light breeze never does.
    SeaSpectrum::Settings storm;
    storm.windSpeed = 24.0;
    const Surface stormy = Evolve(SeaSpectrum::Make(storm, SeaSpectrum::DefaultBands()[1], 1), 2.0, 5.0);
    double stormMin = 1e9;
    for (float j : stormy.jacobian) stormMin = std::min<double>(stormMin, j);
    std::printf("storm: lowest jacobian %.3f\n", stormMin);
    CHECK(stormMin < 0.0);
    SeaSpectrum::Settings breeze;
    breeze.windSpeed = 4.0;
    const Surface gentle = Evolve(SeaSpectrum::Make(breeze, SeaSpectrum::DefaultBands()[1], 1), 2.0, 0.5);
    double gentleMin = 1e9;
    for (float j : gentle.jacobian) gentleMin = std::min<double>(gentleMin, j);
    CHECK(gentleMin > 0.0);
}

int main()
{
    TestFft();
    TestWaveMoves();
    TestSea();
    TestSlope();
    TestJacobian();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("ocean fft tests: all passed\n");
    return 0;
}
