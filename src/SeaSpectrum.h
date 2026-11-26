#pragma once
// The energy of the sea as a function of WAVENUMBER and DIRECTION, sampled on the grids an FFT ocean needs (roadmap O1, decision D7).
// Pure maths: no Direct3D. Tests: tests/sea_spectrum_test.cpp.
//
// SeaState (W6) knows the 1-D story: how tall and how long the waves are for a wind speed and a fetch (JONSWAP, scaled so 4 sqrt(m0) is the
// significant height). This turns it into a 2-D one: for each wave vector k of a square patch of side L, how much energy lives there,
//     S(k) = S_omega(omega) * (d omega / d k) / |k| * D(theta)      [m^2 per (rad/m)^2],
// with omega(k) the dispersion relation of the water's depth, D the directional spread SeaState already defines, and, for shallow water, the
// TMA correction (Kitaigorodskii), which takes energy out of the long waves as the seabed rises. The height field is then (Tessendorf 2001)
//     h(k, t) = h0(k) e^{i omega t} + conj(h0(-k)) e^{-i omega t},   h0(k) = (xi_r + i xi_i) sqrt(S(k) dk^2) / 2,
// xi the unit Gaussians of a seeded random number: the same seed is the same sea, whatever order the modes are made in.
//
// The sea is cut into BANDS (swell, wind sea, ripples), each its own patch size, so no single patch tiles within sight. A band keeps only the
// wavenumbers [kLow, kHigh): the bands add up to one sea without counting any wave twice.
#include "SeaState.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace SeaSpectrum {

struct Complex {
    float re = 0.0f, im = 0.0f;
};

struct Band {
    double patchSize = 250.0;   // metres: the side of the square the FFT repeats over
    int resolution = 256;       // modes along a side (a power of two)
    double kLow = 0.0;          // rad/m: the longest wave this band keeps
    double kHigh = 1.0;         // rad/m: the band stops here (the next band starts)
    double DeltaK() const { return SeaState::kTwoPi / patchSize; }
};

struct Settings {
    double windSpeed = 8.0;       // m/s, 10 m up (the scene's weather)
    double fetch = 0.0;           // metres of open water; <= 0 = unlimited
    double depth = 0.0;           // metres of water; <= 0 = deep
    double windRadians = 0.0;     // the direction the waves travel toward, in the XZ plane
    std::uint32_t seed = 1;
    double heightScale = 1.0;     // an artist's multiplier on every height
};

// The three bands. Patch sizes and edges are chosen so each band's Nyquist wavenumber (pi N / L) is above its upper edge, and the edges meet.
inline std::vector<Band> DefaultBands()
{
    return {
        { 2000.0, 128, 0.0, 0.15 },    // swell and the longest wind waves: 2 km patch, down to waves ~42 m long
        { 250.0, 256, 0.15, 1.2 },     // the wind sea: 250 m patch, down to ~5 m
        { 30.0, 128, 1.2, 8.0 },       // ripples: 30 m patch, down to ~0.8 m (anything smaller is the BRDF's roughness, not geometry)
    };
}

// Signed frequency of mode `index` in FFT order: 0, 1, ... N/2 - 1, -N/2, ... -1.
inline int SignedFrequency(int index, int n) { return index < n / 2 ? index : index - n; }

// A deterministic hash to a unit-interval double (splitmix64): independent of the order modes are made in.
inline std::uint64_t Mix(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline double Unit(std::uint64_t h) { return (static_cast<double>(h >> 11) + 0.5) / 9007199254740992.0; }   // (0, 1)

// Two independent unit Gaussians for mode `index` of band `band` (Box-Muller).
inline void GaussianPair(std::uint32_t seed, int band, std::uint64_t index, double& a, double& b)
{
    const std::uint64_t key = Mix((static_cast<std::uint64_t>(seed) << 32) ^ (static_cast<std::uint64_t>(band) << 56) ^ index);
    const double u1 = Unit(key), u2 = Unit(Mix(key ^ 0xD1B54A32D192ED03ull));
    const double r = std::sqrt(-2.0 * std::log(u1));
    a = r * std::cos(SeaState::kTwoPi * u2);
    b = r * std::sin(SeaState::kTwoPi * u2);
}

// The dispersion relation at wavenumber k and depth: omega^2 = g k tanh(k d). (Deep water when depth <= 0.)
inline double Omega(double k, double depth)
{
    const double t = depth > 0.0 ? std::tanh(k * depth) : 1.0;
    return std::sqrt(SeaState::kGravity * k * t);
}
// d omega / d k.
inline double GroupVelocityOfOmega(double k, double depth)
{
    const double omega = Omega(k, depth);
    if (omega <= 1e-9) return 0.0;
    if (depth <= 0.0) return 0.5 * SeaState::kGravity / omega;
    const double kd = k * depth, t = std::tanh(kd);
    return 0.5 * SeaState::kGravity * (t + kd * (1.0 - t * t)) / omega;
}

// The TMA shallow-water factor on a deep-water spectrum (1 in deep water, falling toward zero as the water shallows and the wave frequency is low).
inline double ShallowFactor(double omega, double depth)
{
    if (depth <= 0.0) return 1.0;
    const double wh = omega * std::sqrt(depth / SeaState::kGravity);
    if (wh >= 2.0) return 1.0;
    if (wh <= 1.0) return 0.5 * wh * wh;
    return 1.0 - 0.5 * (2.0 - wh) * (2.0 - wh);
}

// The directional spread (SeaState's cos^2s(theta/2)), normalised so that it integrates to 1 over the circle.
inline double Spread(double theta, double s)
{
    const double c = std::pow(std::max(std::cos(0.5 * theta), 0.0), 2.0 * s);
    const double norm = 2.0 * std::sqrt(SeaState::kPi) * std::exp(std::lgamma(s + 0.5) - std::lgamma(s + 1.0));   // integral of cos^2s(theta/2) over -pi..pi
    return norm > 0.0 ? c / norm : 0.0;
}

// The spectrum on the (kx, kz) plane: energy per unit area of wave-vector space, so its sum times dk^2 over a patch's modes is the variance of the heights.
struct Density {
    SeaState::Spectrum spectrum;
    double depth = 0.0, windRadians = 0.0, heightScale = 1.0;

    explicit Density(const Settings& settings)
        : spectrum(SeaState::MakeSpectrum(settings.windSpeed, settings.fetch)), depth(settings.depth), windRadians(settings.windRadians),
          heightScale(settings.heightScale)
    {
        // Calm water has no waves at all: SeaState fades the height out under its minimum wind speed.
        spectrum.scale *= SeaState::CalmFactor(settings.windSpeed) * SeaState::CalmFactor(settings.windSpeed);
    }

    double operator()(double kx, double kz) const
    {
        const double k = std::sqrt(kx * kx + kz * kz);
        if (k < 1e-9) return 0.0;
        const double omega = Omega(k, depth);
        if (omega < 1e-6) return 0.0;
        const double theta = std::atan2(kz, kx) - windRadians;
        const double wrapped = std::atan2(std::sin(theta), std::cos(theta));
        const double s = SeaState::SpreadExponent(omega, spectrum.peakOmega);
        const double density = spectrum.Density(omega) * ShallowFactor(omega, depth);
        return heightScale * heightScale * density * GroupVelocityOfOmega(k, depth) / k * Spread(wrapped, s);
    }
};

// One band's initial amplitudes h0(k) and the frequency of each mode, in FFT order: mode (i, j) is at index j * N + i, with wave vector
// (2 pi sf(i) / L, 2 pi sf(j) / L). Modes outside the band's [kLow, kHigh) and the unpaired Nyquist modes are zero.
struct BandField {
    Band band;
    std::vector<Complex> h0;
    std::vector<float> omega;
    int N() const { return band.resolution; }
    size_t Index(int i, int j) const { return static_cast<size_t>(j) * band.resolution + i; }
    // The index of the mode with the opposite wave vector, -k.
    size_t Opposite(int i, int j) const
    {
        const int n = band.resolution;
        return Index((n - i) % n, (n - j) % n);
    }
};

inline BandField Make(const Settings& settings, const Band& band, int bandIndex)
{
    BandField field;
    field.band = band;
    const int n = band.resolution;
    field.h0.assign(static_cast<size_t>(n) * n, Complex{});
    field.omega.assign(static_cast<size_t>(n) * n, 0.0f);
    const Density density(settings);
    const double dk = band.DeltaK();
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int mi = SignedFrequency(i, n), mj = SignedFrequency(j, n);
            if (mi == -n / 2 || mj == -n / 2) continue;   // the Nyquist row and column have no partner: leave them out so the field stays real
            const double kx = dk * mi, kz = dk * mj, k = std::sqrt(kx * kx + kz * kz);
            if (k < band.kLow || k >= band.kHigh || k < 1e-9) continue;
            const size_t index = field.Index(i, j);
            field.omega[index] = static_cast<float>(Omega(k, settings.depth));
            const double amplitude = std::sqrt(density(kx, kz)) * dk * 0.5;
            double a = 0.0, b = 0.0;
            GaussianPair(settings.seed, bandIndex, index, a, b);
            field.h0[index] = Complex{ static_cast<float>(a * amplitude), static_cast<float>(b * amplitude) };
        }
    }
    return field;
}

// The variance of the heights this band's spectrum holds, noise-free: the sum of S(k) dk^2 over its modes.
inline double ExpectedVariance(const Settings& settings, const Band& band)
{
    const Density density(settings);
    const int n = band.resolution;
    const double dk = band.DeltaK();
    double sum = 0.0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int mi = SignedFrequency(i, n), mj = SignedFrequency(j, n);
            if (mi == -n / 2 || mj == -n / 2) continue;
            const double kx = dk * mi, kz = dk * mj, k = std::sqrt(kx * kx + kz * kz);
            if (k < band.kLow || k >= band.kHigh || k < 1e-9) continue;
            sum += density(kx, kz) * dk * dk;
        }
    }
    return sum;
}

// The variance of this particular sea (phase-averaged): the sum over modes of |h0(k)|^2 + |h0(-k)|^2, which is 2 |h0|^2 per mode.
inline double RealizedVariance(const BandField& field)
{
    double sum = 0.0;
    for (const Complex& c : field.h0) sum += 2.0 * (static_cast<double>(c.re) * c.re + static_cast<double>(c.im) * c.im);
    return sum;
}

// The significant height of a variance: 4 sqrt(m0).
inline double SignificantHeightOf(double variance) { return 4.0 * std::sqrt(std::max(variance, 0.0)); }

}   // namespace SeaSpectrum
