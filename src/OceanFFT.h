#pragma once
// The FFT sea in time (roadmap O2, decision D7), on the CPU. Pure maths: no Direct3D. Tests: tests/ocean_fft_test.cpp.
//
// This is both the CPU MIRROR boats read their wave height from (the same sea for everybody, to rounding) and the REFERENCE the GPU compute
// shader is checked against. It takes the amplitudes of SeaSpectrum.h and, at a time t, gives the surface over one patch:
//     h(k, t) = h0(k) e^{-i w t} + conj(h0(-k)) e^{+i w t}          (the waves of h0(k) travel toward +k)
// and then, with an inverse 2-D FFT of each of these (no scaling: the amplitudes already carry the variance),
//     height        h(x)                        = sum_k h(k) e^{i k.x}
//     displacement  D(x)  = -sum_k i (k/|k|) h(k) e^{i k.x}   (horizontal: it pushes crests together, the "choppy" waves)
//     slope         grad h(x) = sum_k i k h(k) e^{i k.x}
// The JACOBIAN of the displaced position, with `choppiness` lambda,
//     J = (1 + lambda dDx/dx)(1 + lambda dDz/dz) - lambda^2 (dDx/dz)(dDz/dx),
// is 1 on a flat sea, falls toward 0 where the surface squeezes together, and is negative where a crest folds over: that is where whitecaps form (O8).
#include "SeaSpectrum.h"
#include <cmath>
#include <complex>
#include <vector>

namespace OceanFFT {

using Cx = std::complex<double>;

// In-place radix-2 FFT of n values (n a power of two). inverse = +i in the exponent, forward = -i; neither scales.
inline void Fft1D(Cx* data, int n, bool inverse)
{
    for (int i = 1, j = 0; i < n; ++i) {   // bit reversal
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= n; length <<= 1) {
        const double angle = (inverse ? 2.0 : -2.0) * SeaState::kPi / length;
        const Cx step(std::cos(angle), std::sin(angle));
        for (int start = 0; start < n; start += length) {
            Cx w(1.0, 0.0);
            for (int k = 0; k < length / 2; ++k) {
                const Cx u = data[start + k], v = data[start + k + length / 2] * w;
                data[start + k] = u + v;
                data[start + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

// 2-D FFT of an n x n array stored row by row (index = z * n + x).
inline void Fft2D(std::vector<Cx>& data, int n, bool inverse)
{
    std::vector<Cx> column(static_cast<size_t>(n));
    for (int z = 0; z < n; ++z) Fft1D(&data[static_cast<size_t>(z) * n], n, inverse);
    for (int x = 0; x < n; ++x) {
        for (int z = 0; z < n; ++z) column[z] = data[static_cast<size_t>(z) * n + x];
        Fft1D(column.data(), n, inverse);
        for (int z = 0; z < n; ++z) data[static_cast<size_t>(z) * n + x] = column[z];
    }
}

// What a band's patch looks like at one moment: n x n samples, `patchSize / n` metres apart, repeating every `patchSize`.
struct Surface {
    int n = 0;
    double patchSize = 0.0;
    std::vector<float> height;      // metres
    std::vector<float> displaceX, displaceZ;   // metres, horizontal (to be multiplied by the choppiness)
    std::vector<float> slopeX, slopeZ;         // dh/dx, dh/dz
    std::vector<float> jacobian;    // with the choppiness it was made with

    size_t Index(int i, int j) const { return static_cast<size_t>(j) * n + i; }
    double Spacing() const { return n > 0 ? patchSize / n : 0.0; }

    // The height at any world position: the patch repeats, and the value between samples is bilinear.
    double HeightAt(double x, double z) const
    {
        if (n <= 0) return 0.0;
        const double fx = x / Spacing(), fz = z / Spacing();
        const double ix = std::floor(fx), iz = std::floor(fz);
        const double tx = fx - ix, tz = fz - iz;
        auto wrap = [this](double v) { int i = static_cast<int>(std::fmod(v, static_cast<double>(n))); return i < 0 ? i + n : i; };
        const int x0 = wrap(ix), z0 = wrap(iz), x1 = (x0 + 1) % n, z1 = (z0 + 1) % n;
        const double a = height[Index(x0, z0)], b = height[Index(x1, z0)], c = height[Index(x0, z1)], d = height[Index(x1, z1)];
        return (a * (1.0 - tx) + b * tx) * (1.0 - tz) + (c * (1.0 - tx) + d * tx) * tz;
    }
};

// The surface of one band at time `t` (seconds). `choppiness` (0..1) only changes the Jacobian here: the displacement is returned unscaled.
inline Surface Evolve(const SeaSpectrum::BandField& field, double t, double choppiness)
{
    const int n = field.N();
    const double dk = field.band.DeltaK();
    Surface surface;
    surface.n = n;
    surface.patchSize = field.band.patchSize;
    const size_t count = static_cast<size_t>(n) * n;
    std::vector<Cx> h(count), dispX(count), dispZ(count), sx(count), sz(count), dxx(count), dzz(count), dxz(count);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const size_t index = field.Index(i, j), opposite = field.Opposite(i, j);
            const double kx = dk * SeaSpectrum::SignedFrequency(i, n), kz = dk * SeaSpectrum::SignedFrequency(j, n), k = std::sqrt(kx * kx + kz * kz);
            if (k < 1e-9 || (field.h0[index].re == 0.0f && field.h0[index].im == 0.0f && field.h0[opposite].re == 0.0f && field.h0[opposite].im == 0.0f)) continue;
            const double phase = static_cast<double>(field.omega[index]) * t;
            const Cx e(std::cos(phase), std::sin(phase));
            const Cx a(field.h0[index].re, field.h0[index].im), b(field.h0[opposite].re, -field.h0[opposite].im);   // h0(k) and conj(h0(-k))
            const Cx value = a * std::conj(e) + b * e;
            const Cx i1(0.0, 1.0);
            h[index] = value;
            dispX[index] = -i1 * (kx / k) * value;
            dispZ[index] = -i1 * (kz / k) * value;
            sx[index] = i1 * kx * value;
            sz[index] = i1 * kz * value;
            // The derivatives of the displacement: i k_a (-i k_b / |k|) h = (k_a k_b / |k|) h.
            dxx[index] = (kx * kx / k) * value;
            dzz[index] = (kz * kz / k) * value;
            dxz[index] = (kx * kz / k) * value;
        }
    }
    for (std::vector<Cx>* g : { &h, &dispX, &dispZ, &sx, &sz, &dxx, &dzz, &dxz }) Fft2D(*g, n, true);
    surface.height.resize(count); surface.displaceX.resize(count); surface.displaceZ.resize(count);
    surface.slopeX.resize(count); surface.slopeZ.resize(count); surface.jacobian.resize(count);
    for (size_t i = 0; i < count; ++i) {
        surface.height[i] = static_cast<float>(h[i].real());
        surface.displaceX[i] = static_cast<float>(dispX[i].real());
        surface.displaceZ[i] = static_cast<float>(dispZ[i].real());
        surface.slopeX[i] = static_cast<float>(sx[i].real());
        surface.slopeZ[i] = static_cast<float>(sz[i].real());
        const double jxx = 1.0 + choppiness * dxx[i].real(), jzz = 1.0 + choppiness * dzz[i].real(), jxz = choppiness * dxz[i].real();
        surface.jacobian[i] = static_cast<float>(jxx * jzz - jxz * jxz);
    }
    return surface;
}

// The sea's height at a world position: every band's patch added, from the same time. This is what a boat reads.
inline double SeaHeightAt(const std::vector<Surface>& bands, double x, double z)
{
    double sum = 0.0;
    for (const Surface& band : bands) sum += band.HeightAt(x, z);
    return sum;
}

}   // namespace OceanFFT
