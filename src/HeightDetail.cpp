#include "HeightDetail.h"
#include <algorithm>
#include <cmath>

namespace {
// Average of (1 - 2|v|) for one octave of value noise: 0.2425 and 0.2422 over 16 million samples with two seeds (the test checks the result: its mean is near 0). Taking it off keeps ridged
// detail centred on zero.
constexpr double kRidgedMean = 0.2423;

std::uint64_t Mix(std::uint64_t h)
{
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

// A pseudo-random number in [-1, 1] for one lattice point of one octave.
double Lattice(std::int64_t ix, std::int64_t iz, std::uint32_t seed, std::uint32_t octave)
{
    const std::uint64_t h = Mix(static_cast<std::uint64_t>(ix) * 0x9E3779B97F4A7C15ull ^ Mix(static_cast<std::uint64_t>(iz) + 0x632BE59BD9B4E019ull) ^ ((static_cast<std::uint64_t>(seed) << 8) | octave) * 0x165667B19E3779F9ull);
    return static_cast<double>(h >> 11) / 4503599627370495.5 - 1.0;   // 53 bits -> [-1, 1]
}

double Quintic(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }   // smooth in value, slope and curvature

// Value noise, one octave: smooth between the lattice values, in [-1, 1].
double ValueNoise(double x, double z, std::uint32_t seed, std::uint32_t octave)
{
    const double fx = std::floor(x), fz = std::floor(z);
    const std::int64_t ix = static_cast<std::int64_t>(fx), iz = static_cast<std::int64_t>(fz);
    const double sx = Quintic(x - fx), sz = Quintic(z - fz);
    const double a = Lattice(ix, iz, seed, octave), b = Lattice(ix + 1, iz, seed, octave), c = Lattice(ix, iz + 1, seed, octave), d = Lattice(ix + 1, iz + 1, seed, octave);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz;
}
}   // namespace

void HeightDetailSettings::Clamp()
{
    if (!(strength >= 0.0f)) strength = 0.0f;   // also catches NaN
    strength = (std::min)(strength, kStrengthMax);
    if (!(wavelength >= kWavelengthMin)) wavelength = kWavelengthMin;
    wavelength = (std::min)(wavelength, kWavelengthMax);
    octaves = std::clamp(octaves, kOctavesMin, kOctavesMax);
}

double DetailMetres(const HeightDetailSettings& requested, double worldX, double worldZ)
{
    HeightDetailSettings s = requested;
    s.Clamp();
    if (s.IsIdentity()) return 0.0;
    double sum = 0.0, amplitudeSum = 0.0, amplitude = 1.0, wavelength = s.wavelength;
    for (int octave = 0; octave < s.octaves; ++octave) {
        double v = ValueNoise(worldX / wavelength, worldZ / wavelength, static_cast<std::uint32_t>(s.seed), static_cast<std::uint32_t>(octave));
        if (s.ridged) v = (1.0 - 2.0 * std::fabs(v) - kRidgedMean) / (1.0 + std::fabs(kRidgedMean));   // crests where the noise crosses zero; centred; still within +/- 1
        sum += v * amplitude;
        amplitudeSum += amplitude;
        amplitude *= 0.5;
        wavelength *= 0.5;
    }
    return static_cast<double>(s.strength) * sum / amplitudeSum;   // within +/- strength
}
