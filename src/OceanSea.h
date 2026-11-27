#pragma once
// The CPU mirror of the GPU sea (roadmap O2, decision D7): the heights a boat reads. Pure: no Direct3D. Tests: tests/ocean_sea_test.cpp.
//
// The GPU makes the sea with four FFTs per band (height, displacement, slopes ... see OceanGpuPack.h). A boat needs only the HEIGHT, so the CPU
// does one FFT per band, and two bands of the same size share one (the packing trick of OceanGpuPack.h: the inverse FFT of A + iB is a + ib when A
// and B are both conjugate-symmetric, so one complex FFT carries two real height fields). The default bands are 128, 256 and 128 modes: two FFTs
// instead of three, and the 256 one is the only big one.
//
// It is the same arithmetic as OceanFFT::Evolve, so the heights agree with it exactly (to float rounding) and with the GPU's height channel to its
// half-float precision. Like the GPU it reads the patch periodically with a bilinear blend (OceanFFT::Surface::HeightAt).
//
// What it does NOT do: the choppy sideways push. The water on screen is the height field read at p and drawn at p + choppiness * D(p), so a boat at
// the same x, z can be off by about choppiness * D in the wave's own direction: centimetres in a breeze, up to about a metre in a storm.
#include "OceanFFT.h"
#include "SeaSpectrum.h"
#include <cmath>
#include <vector>

namespace OceanSea {

using OceanFFT::Cx;

// The height field of one or two bands at time t, each band's `height` filled and nothing else. `a` and `b` must have the same resolution; `b` may be
// null (one band, one FFT). One FFT of (value_a + i value_b): the real part is band a's height, the imaginary part band b's.
inline void EvolveHeights(const SeaSpectrum::BandField& a, const SeaSpectrum::BandField* b, double t, OceanFFT::Surface& outA, OceanFFT::Surface* outB)
{
    const int n = a.N();
    const double dk = a.band.DeltaK();
    const size_t count = static_cast<size_t>(n) * n;
    std::vector<Cx> spectrum(count);
    const Cx i1(0.0, 1.0);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const size_t index = a.Index(i, j);
            const double kx = dk * SeaSpectrum::SignedFrequency(i, n), kz = dk * SeaSpectrum::SignedFrequency(j, n);
            if (std::sqrt(kx * kx + kz * kz) < 1e-9) continue;   // the zero wave vector: no wave, and Evolve skips it too
            auto value = [&](const SeaSpectrum::BandField& f) {
                const size_t opposite = f.Opposite(i, j);
                if (f.h0[index].re == 0.0f && f.h0[index].im == 0.0f && f.h0[opposite].re == 0.0f && f.h0[opposite].im == 0.0f) return Cx(0.0, 0.0);
                const double phase = static_cast<double>(f.omega[index]) * t;
                const Cx e(std::cos(phase), std::sin(phase));
                return Cx(f.h0[index].re, f.h0[index].im) * std::conj(e) + Cx(f.h0[opposite].re, -f.h0[opposite].im) * e;   // h0(k) e^{-iwt} + conj(h0(-k)) e^{iwt}
            };
            spectrum[index] = value(a) + (b ? i1 * value(*b) : Cx(0.0, 0.0));
        }
    }
    OceanFFT::Fft2D(spectrum, n, true);
    outA.n = n; outA.patchSize = a.band.patchSize;
    outA.height.resize(count);
    if (b && outB) { outB->n = n; outB->patchSize = b->band.patchSize; outB->height.resize(count); }
    for (size_t i = 0; i < count; ++i) {
        outA.height[i] = static_cast<float>(spectrum[i].real());
        if (b && outB) outB->height[i] = static_cast<float>(spectrum[i].imag());
    }
}

// The whole sea: every band's spectrum, and the heights at the time of the last Update.
class Sea {
public:
    // Builds every band's spectrum (SeaSpectrum::DefaultBands). The heights are empty until the next Update.
    void Build(const SeaSpectrum::Settings& settings)
    {
        settings_ = settings;
        fields_.clear();
        surfaces_.clear();
        const std::vector<SeaSpectrum::Band> bands = SeaSpectrum::DefaultBands();
        for (size_t i = 0; i < bands.size(); ++i) fields_.push_back(SeaSpectrum::Make(settings, bands[i], static_cast<int>(i)));
        surfaces_.resize(fields_.size());
        valid_ = false;
    }
    bool IsBuilt() const { return !fields_.empty(); }
    int BandCount() const { return static_cast<int>(fields_.size()); }

    // The heights at time `t` (seconds). Asking again for the same time costs nothing, which is what lets every boat in a frame call this.
    void Update(double t)
    {
        if (fields_.empty() || (valid_ && t == time_)) return;
        std::vector<bool> done(fields_.size(), false);
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (done[i]) continue;
            size_t partner = fields_.size();   // the next band of the same size, if any, shares this band's FFT
            for (size_t j = i + 1; j < fields_.size(); ++j)
                if (!done[j] && fields_[j].N() == fields_[i].N()) { partner = j; break; }
            if (partner < fields_.size()) { EvolveHeights(fields_[i], &fields_[partner], t, surfaces_[i], &surfaces_[partner]); done[partner] = true; }
            else EvolveHeights(fields_[i], nullptr, t, surfaces_[i], nullptr);
            done[i] = true;
        }
        time_ = t;
        valid_ = true;
    }

    // The height of the sea at a world position at the time of the last Update: every band's patch added (what a boat reads).
    double HeightAt(double x, double z) const { return valid_ ? OceanFFT::SeaHeightAt(surfaces_, x, z) : 0.0; }
    const std::vector<OceanFFT::Surface>& Surfaces() const { return surfaces_; }

    // The standard deviation of the sea's height, from the spectrum (noise-free; phase-averaged).
    double Sigma() const
    {
        double variance = 0.0;
        for (const SeaSpectrum::BandField& f : fields_) variance += SeaSpectrum::RealizedVariance(f);
        return std::sqrt(variance);
    }
    // A height the sea (and, since |D| has the same size, the sideways push with choppiness <= 1) practically never passes: 6 sigma. The mesh
    // margins and the "is this tile under dry ground" test use it, so too small would drop water that is on screen and too large only draws more.
    double HeightBound() const { return kBoundSigmas * Sigma(); }
    static constexpr double kBoundSigmas = 6.0;

private:
    SeaSpectrum::Settings settings_;
    std::vector<SeaSpectrum::BandField> fields_;
    std::vector<OceanFFT::Surface> surfaces_;
    double time_ = 0.0;
    bool valid_ = false;
};

}   // namespace OceanSea
