// Unit tests for src/SeaSpectrum.h (roadmap O1): the 2-D sea spectrum agrees with SeaState, is real, is band-limited, repeatable, and follows the wind and the
// depth. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "SeaSpectrum.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(double a, double b, double relative) { return std::fabs(a - b) <= relative * std::max(std::fabs(a), std::fabs(b)); }

using namespace SeaSpectrum;

static double TotalVariance(const Settings& settings)
{
    double sum = 0.0;
    for (const Band& band : DefaultBands()) sum += ExpectedVariance(settings, band);
    return sum;
}

// The three bands together hold the energy SeaState says the sea has: 4 sqrt(m0) is the significant height, for several winds.
static void TestAgreesWithSeaState()
{
    for (double wind : { 5.0, 8.0, 12.0, 18.0 }) {
        Settings settings;
        settings.windSpeed = wind;
        const double expected = SeaState::Describe(wind, 0.0).significantHeight;
        const double height = SignificantHeightOf(TotalVariance(settings));
        std::printf("wind %.0f m/s: SeaState %.3f m, spectrum %.3f m\n", wind, expected, height);
        CHECK(Near(height, expected, 0.06));   // the grids stop at ripples ~0.8 m long: a few per cent of the energy is below that
    }
    // A short fetch (a bay): the same rule.
    Settings bay;
    bay.windSpeed = 12.0; bay.fetch = 4000.0;
    CHECK(Near(SignificantHeightOf(TotalVariance(bay)), SeaState::Describe(12.0, 4000.0).significantHeight, 0.08));
    // The scale is an exaggeration of the height only.
    Settings tall = bay;
    tall.heightScale = 2.0;
    CHECK(Near(SignificantHeightOf(TotalVariance(tall)), 2.0 * SignificantHeightOf(TotalVariance(bay)), 1e-6));
    // Calm air has no waves.
    Settings calm;
    calm.windSpeed = 0.0;
    CHECK(TotalVariance(calm) < 1e-9);
}

// A band keeps only its wavenumbers, and the bands meet without a gap or an overlap.
static void TestBands()
{
    const std::vector<Band> bands = DefaultBands();
    CHECK(bands.size() == 3);
    for (size_t i = 0; i + 1 < bands.size(); ++i) CHECK(bands[i].kHigh == bands[i + 1].kLow);
    for (const Band& band : bands) CHECK(SeaState::kPi * band.resolution / band.patchSize >= band.kHigh);   // the grid can hold up to its upper edge
    Settings settings;
    settings.windSpeed = 12.0;
    for (size_t b = 0; b < bands.size(); ++b) {
        const BandField field = Make(settings, bands[b], static_cast<int>(b));
        const int n = field.N();
        const double dk = bands[b].DeltaK();
        bool outside = false, inside = false;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const Complex c = field.h0[field.Index(i, j)];
                const double k = dk * std::sqrt(static_cast<double>(SignedFrequency(i, n)) * SignedFrequency(i, n) + static_cast<double>(SignedFrequency(j, n)) * SignedFrequency(j, n));
                const bool any = c.re != 0.0f || c.im != 0.0f;
                if (any && (k < bands[b].kLow || k >= bands[b].kHigh)) outside = true;
                if (any) inside = true;
            }
        CHECK(!outside);
        CHECK(inside);
    }
}

// The same seed is the same sea; another seed is another one; and the modes do not depend on the order they are made in.
static void TestRepeatable()
{
    Settings a;
    a.windSpeed = 10.0;
    const Band band = DefaultBands()[1];
    const BandField one = Make(a, band, 1), two = Make(a, band, 1);
    CHECK(one.h0.size() == two.h0.size());
    bool same = true;
    for (size_t i = 0; i < one.h0.size(); ++i) same = same && one.h0[i].re == two.h0[i].re && one.h0[i].im == two.h0[i].im;
    CHECK(same);
    Settings b = a;
    b.seed = 2;
    const BandField other = Make(b, band, 1);
    int different = 0;
    for (size_t i = 0; i < one.h0.size(); ++i) if (one.h0[i].re != other.h0[i].re) ++different;
    CHECK(different > static_cast<int>(one.h0.size()) / 10);
    // A band's seed also depends on the band: two bands do not share random numbers.
    const BandField sameBandOtherIndex = Make(a, band, 2);
    int differentBand = 0;
    for (size_t i = 0; i < one.h0.size(); ++i) if (one.h0[i].re != sameBandOtherIndex.h0[i].re) ++differentBand;
    CHECK(differentBand > static_cast<int>(one.h0.size()) / 10);
}

// Real field: the time-evolved spectrum h(k, t) = h0(k) e^{iwt} + conj(h0(-k)) e^{-iwt} satisfies h(-k) = conj(h(k)) at any time.
static void TestHermitian()
{
    Settings settings;
    settings.windSpeed = 9.0;
    const BandField field = Make(settings, DefaultBands()[1], 1);
    const int n = field.N();
    for (double t : { 0.0, 1.7, 33.3 }) {
        double worst = 0.0;
        auto h = [&](int i, int j, double& re, double& im) {
            const size_t k = field.Index(i, j), m = field.Opposite(i, j);
            const double w = field.omega[k] * t;
            const double c = std::cos(w), s = std::sin(w);
            // h0(k) e^{iw} + conj(h0(-k)) e^{-iw}
            re = field.h0[k].re * c - field.h0[k].im * s + field.h0[m].re * c - field.h0[m].im * s;
            im = field.h0[k].re * s + field.h0[k].im * c - field.h0[m].re * s - field.h0[m].im * c;
        };
        for (int j = 1; j < n / 2; ++j)
            for (int i = 1; i < n / 2; ++i) {
                double ar, ai, br, bi;
                h(i, j, ar, ai);
                h((n - i) % n, (n - j) % n, br, bi);
                worst = std::max(worst, std::fabs(ar - br) + std::fabs(ai + bi));
            }
        CHECK(worst < 1e-6);
    }
}

// The realised sea has about the variance its spectrum holds (to the noise of a random draw).
static void TestRealisedVariance()
{
    Settings settings;
    settings.windSpeed = 11.0;
    double expected = 0.0, realised = 0.0;
    const std::vector<Band> bands = DefaultBands();
    for (size_t b = 0; b < bands.size(); ++b) {
        expected += ExpectedVariance(settings, bands[b]);
        realised += RealizedVariance(Make(settings, bands[b], static_cast<int>(b)));
    }
    std::printf("variance: spectrum %.4f m^2, this seed %.4f m^2\n", expected, realised);
    CHECK(Near(realised, expected, 0.35));
    // Averaged over several seeds the noise goes away.
    double total = 0.0;
    for (std::uint32_t seed = 1; seed <= 12; ++seed) {
        Settings s = settings;
        s.seed = seed;
        for (size_t b = 0; b < bands.size(); ++b) total += RealizedVariance(Make(s, bands[b], static_cast<int>(b)));
    }
    CHECK(Near(total / 12.0, expected, 0.10));
}

// The wind sets the size, the direction and the depth the shape.
static void TestWindAndDepth()
{
    Settings weak, strong;
    weak.windSpeed = 6.0; strong.windSpeed = 16.0;
    CHECK(TotalVariance(strong) > 4.0 * TotalVariance(weak));
    // Direction: along the wind there is more energy than across it.
    Settings s;
    s.windSpeed = 12.0; s.windRadians = 0.0;
    const Density density(s);
    const double k = 0.12;
    CHECK(density(k, 0.0) > 5.0 * density(0.0, k));      // down-wind beats cross-wind
    CHECK(density(k, 0.0) > 3.0 * density(-k, 0.0));     // and up-wind: the waves travel WITH the wind
    // Turning the wind turns the sea.
    Settings turned = s;
    turned.windRadians = SeaState::kPi * 0.5;
    const Density rotated(turned);
    CHECK(Near(rotated(0.0, k), density(k, 0.0), 1e-6));
    // The spread integrates to one over the circle.
    double integral = 0.0;
    const int steps = 20000;
    for (int i = 0; i < steps; ++i) integral += Spread(-SeaState::kPi + SeaState::kTwoPi * (i + 0.5) / steps, 6.0) * SeaState::kTwoPi / steps;
    CHECK(Near(integral, 1.0, 1e-3));
    // Shallow water: less energy (the long waves feel the bottom), and shorter waves for the same frequency.
    Settings deep, shallow;
    deep.windSpeed = shallow.windSpeed = 12.0;
    shallow.depth = 4.0;
    CHECK(TotalVariance(shallow) < 0.7 * TotalVariance(deep));
    CHECK(Omega(0.05, 4.0) < Omega(0.05, 0.0));   // a long wave is slower in shallow water
    CHECK(Near(Omega(2.0, 4.0), Omega(2.0, 0.0), 1e-3));   // a short wave does not notice the bottom
    // The TMA factor goes from 0 to 1.
    CHECK(ShallowFactor(0.1, 4.0) < 0.05 && Near(ShallowFactor(5.0, 4.0), 1.0, 1e-9) && ShallowFactor(1.0, 0.0) == 1.0);
}

int main()
{
    TestAgreesWithSeaState();
    TestBands();
    TestRepeatable();
    TestHermitian();
    TestRealisedVariance();
    TestWindAndDepth();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("sea spectrum tests: all passed\n");
    return 0;
}
