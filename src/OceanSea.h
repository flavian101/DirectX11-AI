#pragma once
// The CPU mirror of the GPU sea (roadmap O2, decision D7): the heights a boat or the camera reads. Pure: no Direct3D. Tests: tests/ocean_sea_test.cpp.
//
// It is the SAME sea the GPU draws (the same spectrum, the same time), but it answers "how high is the water at x, z?" by adding up the waves
// directly instead of making a whole height map with an FFT. Why: the scene asks that question EVERY FRAME for the camera (is it under water?),
// even with no boat in sight, and a 256 x 256 FFT per frame is about 6 ms in an optimised build and far more in a debug one (it ran at 8 FPS).
// One query, summed directly, is tens of microseconds, and a frame with no query costs nothing.
//
// How. The height at x is the real part of a sum over modes:  h(x) = Re sum_k value(k, t) e^{i k . x}  with
//     value(k, t) = h0(k) e^{-i w t} + conj(h0(-k)) e^{+i w t}      (the same as OceanFFT::Evolve and the GPU's evolve pass)
// * Most of a sea's energy sits in a small part of its modes: the 15 to 18 thousand strongest of 98 thousand carry all but 0.001% of the variance, so only
//   those are kept (the rest is below a centimetre in a storm). A mode and its partner -k are kept or dropped together, so the sum stays real.
// * Update(t) turns every kept mode to the time t (one sine and cosine each). Asking again for the same time costs nothing.
// * A query needs e^{i (kx x + kz z)} = e^{i kx x} e^{i kz z}: a mode's x part depends only on its column, its z part only on its row. So the two
//   tables (one value per column, one per row) are made once per query by repeated multiplication, and each mode then costs one complex multiply-add.
//
// It reads the true, continuous sea. The GPU reads its height map with a bilinear blend between texels, so the two agree exactly at the texels and
// differ between them only by the blend's error: nothing for waves longer than a few texels, which are the ones that carry a boat.
//
// What it does NOT do: the choppy sideways push. The water on screen is the height field read at p and drawn at p + choppiness * D(p), so a boat at
// the same x, z can be off by about choppiness * D in the wave's own direction: centimetres in a breeze, up to about a metre in a storm.
#include "OceanFFT.h"
#include "SeaSpectrum.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace OceanSea {

// The share of the sea's energy that is NOT kept. 1e-5 of the energy is 0.3% of the standard deviation of the height.
constexpr double kDroppedEnergy = 1e-5;

// One band's kept modes, as plain arrays (the loops run over them in every query), sorted by row (z) then column (x).
struct BandModes {
    int n = 0;                       // modes per side
    double dk = 0.0;                 // radians per metre between neighbouring modes
    double patchSize = 0.0;
    std::vector<int> rowStart;       // n + 1 entries: the modes of row j are [rowStart[j], rowStart[j + 1])
    std::vector<int> column;         // x index of each mode
    std::vector<double> aRe, aIm;    // h0(k)
    std::vector<double> bRe, bIm;    // conj(h0(-k))
    std::vector<double> omega;       // radians per second
    std::vector<double> vRe, vIm;    // value(k, t) at the last Update
};

// Picks the strongest modes of a band (with their partners) until all but `dropped` of its energy is kept.
inline BandModes SelectModes(const SeaSpectrum::BandField& field, double dropped = kDroppedEnergy)
{
    struct Pair { double weight; int i, j; };
    const int n = field.N();
    BandModes out;
    out.n = n;
    out.dk = field.band.DeltaK();
    out.patchSize = field.band.patchSize;

    std::vector<Pair> pairs;
    double total = 0.0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const size_t index = field.Index(i, j), opposite = field.Opposite(i, j);
            if (index > opposite) continue;   // each pair once, by its smaller index (a mode that is its own partner: once)
            const double kx = out.dk * SeaSpectrum::SignedFrequency(i, n), kz = out.dk * SeaSpectrum::SignedFrequency(j, n);
            if (std::sqrt(kx * kx + kz * kz) < 1e-9) continue;   // the zero wave vector: no wave
            const SeaSpectrum::Complex &a = field.h0[index], &b = field.h0[opposite];
            const double energy = static_cast<double>(a.re) * a.re + static_cast<double>(a.im) * a.im + static_cast<double>(b.re) * b.re + static_cast<double>(b.im) * b.im;
            if (energy <= 0.0) continue;
            const double weight = index == opposite ? energy : 2.0 * energy;   // the mode and its partner each carry `energy`
            total += weight;
            pairs.push_back({ weight, i, j });
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.weight > y.weight; });

    struct Kept { int i, j; };
    std::vector<Kept> kept;
    double accumulated = 0.0;
    for (const Pair& pair : pairs) {
        if (accumulated >= (1.0 - dropped) * total) break;
        accumulated += pair.weight;
        kept.push_back({ pair.i, pair.j });
        const int oi = (n - pair.i) % n, oj = (n - pair.j) % n;
        if (oi != pair.i || oj != pair.j) kept.push_back({ oi, oj });   // the partner -k comes with it
    }
    std::sort(kept.begin(), kept.end(), [](const Kept& x, const Kept& y) { return x.j != y.j ? x.j < y.j : x.i < y.i; });

    out.rowStart.assign(static_cast<size_t>(n) + 1, 0);
    for (const Kept& k : kept) {
        const size_t index = field.Index(k.i, k.j), opposite = field.Opposite(k.i, k.j);
        out.column.push_back(k.i);
        out.aRe.push_back(field.h0[index].re);
        out.aIm.push_back(field.h0[index].im);
        out.bRe.push_back(field.h0[opposite].re);
        out.bIm.push_back(-static_cast<double>(field.h0[opposite].im));   // conj
        out.omega.push_back(field.omega[index]);
        ++out.rowStart[static_cast<size_t>(k.j) + 1];
    }
    for (int j = 0; j < n; ++j) out.rowStart[static_cast<size_t>(j) + 1] += out.rowStart[static_cast<size_t>(j)];
    out.vRe.assign(out.column.size(), 0.0);
    out.vIm.assign(out.column.size(), 0.0);
    return out;
}

// value(k, t) of every kept mode of a band: h0 e^{-iwt} + conj(h0(-k)) e^{+iwt}.
inline void TurnTo(BandModes& band, double t)
{
    const size_t count = band.column.size();
    const double* aRe = band.aRe.data(); const double* aIm = band.aIm.data();
    const double* bRe = band.bRe.data(); const double* bIm = band.bIm.data();
    const double* omega = band.omega.data();
    double* vRe = band.vRe.data(); double* vIm = band.vIm.data();
    for (size_t m = 0; m < count; ++m) {
        const double phase = omega[m] * t;
        const double c = std::cos(phase), s = std::sin(phase);
        vRe[m] = aRe[m] * c + aIm[m] * s + bRe[m] * c - bIm[m] * s;   // a (c - i s) + b (c + i s)
        vIm[m] = aIm[m] * c - aRe[m] * s + bIm[m] * c + bRe[m] * s;
    }
}

// The height of one band at a world position, at the time of the last TurnTo.
inline double HeightOf(const BandModes& band, double x, double z)
{
    const int n = band.n;
    if (n <= 0 || band.column.empty()) return 0.0;
    // e^{i dk f x} for every column f (signed frequency): built for f = 0 .. n/2 by repeated multiplication by e^{i dk x}, and the negative
    // frequencies are their conjugates. The same for z.
    constexpr int kMaxN = 512;
    double exRe[kMaxN], exIm[kMaxN], ezRe[kMaxN], ezIm[kMaxN];
    auto table = [n](double position, double dk, double* re, double* im) {
        const double stepRe = std::cos(dk * position), stepIm = std::sin(dk * position);
        double pRe = 1.0, pIm = 0.0;
        const int half = n / 2;
        for (int f = 0; f <= half; ++f) {
            re[f] = pRe; im[f] = pIm;
            const double nRe = pRe * stepRe - pIm * stepIm, nIm = pRe * stepIm + pIm * stepRe;
            pRe = nRe; pIm = nIm;
        }
        // index i < n/2 has frequency +i; index i >= n/2 has frequency i - n (negative): the conjugate of the entry for n - i (for i = n/2
        // that is the entry just made, conjugated in place)
        for (int i = half; i < n; ++i) { re[i] = re[n - i]; im[i] = -im[n - i]; }
    };
    table(x, band.dk, exRe, exIm);
    table(z, band.dk, ezRe, ezIm);

    double total = 0.0;
    const int* column = band.column.data();
    const double* vRe = band.vRe.data(); const double* vIm = band.vIm.data();
    for (int j = 0; j < n; ++j) {
        const int begin = band.rowStart[static_cast<size_t>(j)], end = band.rowStart[static_cast<size_t>(j) + 1];
        if (begin == end) continue;
        double sRe = 0.0, sIm = 0.0;   // the row's modes times their x factors
        for (int m = begin; m < end; ++m) {
            const int c = column[m];
            sRe += vRe[m] * exRe[c] - vIm[m] * exIm[c];
            sIm += vRe[m] * exIm[c] + vIm[m] * exRe[c];
        }
        total += sRe * ezRe[j] - sIm * ezIm[j];   // real part of (row sum) * e^{i kz z}
    }
    return total;
}

// The whole sea: every band's kept modes, turned to the time of the last Update.
class Sea {
public:
    // Builds every band's spectrum (SeaSpectrum::DefaultBands) and keeps its strongest modes. Heights read 0 until the next Update.
    void Build(const SeaSpectrum::Settings& settings)
    {
        bands_.clear();
        variance_ = 0.0;
        const std::vector<SeaSpectrum::Band> bands = SeaSpectrum::DefaultBands();
        for (size_t i = 0; i < bands.size(); ++i) {
            const SeaSpectrum::BandField field = SeaSpectrum::Make(settings, bands[i], static_cast<int>(i));
            variance_ += SeaSpectrum::RealizedVariance(field);
            bands_.push_back(SelectModes(field));
        }
        valid_ = false;
    }
    bool IsBuilt() const { return !bands_.empty(); }
    int BandCount() const { return static_cast<int>(bands_.size()); }
    size_t ModeCount() const { size_t count = 0; for (const BandModes& b : bands_) count += b.column.size(); return count; }

    // Turns every kept mode to time `t` (seconds). Asking again for the same time costs nothing, which is what lets every query in a frame call this.
    void Update(double t)
    {
        if (bands_.empty() || (valid_ && t == time_)) return;
        for (BandModes& band : bands_) TurnTo(band, t);
        time_ = t;
        valid_ = true;
    }

    // The height of the sea at a world position at the time of the last Update (all bands added), and of one band alone.
    double HeightAt(double x, double z) const
    {
        if (!valid_) return 0.0;
        double sum = 0.0;
        for (const BandModes& band : bands_) sum += HeightOf(band, x, z);
        return sum;
    }
    double BandHeightAt(int band, double x, double z) const { return valid_ ? HeightOf(bands_[static_cast<size_t>(band)], x, z) : 0.0; }

    // The standard deviation of the sea's height, from the whole spectrum (noise-free; phase-averaged).
    double Sigma() const { return std::sqrt(variance_); }
    // A height the sea (and, since |D| has the same size, the sideways push with choppiness <= 1) practically never passes: 6 sigma. The mesh
    // margins and the "is this tile under dry ground" test use it, so too small would drop water that is on screen and too large only draws more.
    double HeightBound() const { return kBoundSigmas * Sigma(); }
    static constexpr double kBoundSigmas = 6.0;

private:
    std::vector<BandModes> bands_;
    double variance_ = 0.0;
    double time_ = 0.0;
    bool valid_ = false;
};

// ---- The sea at a shore (interim, until roadmap O6 gives waves a real run-up) ------------------------------------------------------------------
// A wave cannot stand taller than the water under it. The surface is limited to a fraction of the depth: the factor below multiplies the displacement
// (height and the sideways push together, so the shape is kept), is 1 where the water is deep and falls to 0 at the waterline. It is a soft limit,
// h' = h / (1 + (|h| / L)^4)^(1/4) with L = kShoreBreakRatio * depth, which stays under L however tall h is, and is within 0.01% of h while |h| < L / 8.
// shaders/ocean.hlsli ShoreLimit is the copy the vertex shader runs; Water::SampleHeight uses this one, so boats and the drawn surface agree.
constexpr double kShoreBreakRatio = 0.4;

inline double ShoreFactor(double height, double depth)
{
    const double limit = kShoreBreakRatio * depth;
    if (limit <= 1e-4) return 0.0;   // dry (or the very edge): no wave at all
    const double x = std::fabs(height) / limit, x2 = x * x;
    return 1.0 / std::sqrt(std::sqrt(1.0 + x2 * x2));
}
inline double ShoreLimited(double height, double depth) { return height * ShoreFactor(height, depth); }

}   // namespace OceanSea
