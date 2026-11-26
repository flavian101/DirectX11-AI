#pragma once
// What the GPU's first ocean pass reads and writes (roadmap O2, the GPU half). Pure: no Direct3D. Tests: tests/ocean_gpu_pack_test.cpp.
//
// The GPU makes the sea in three steps: (1) EVOLVE every mode of the spectrum to time t (shaders/ocean_evolve_cs.hlsl), (2) inverse FFT rows then
// columns, (3) read the result. This header is step (1) twice: the data layout the shader reads (Pack), and EvolvePacked, a line-by-line copy of the
// shader in C++ that the tests compare with OceanFFT::Evolve. If the shader and EvolvePacked say the same thing, and EvolvePacked agrees with
// OceanFFT, the shader is right. Change one, change the others.
//
// THE PACKING TRICK. OceanFFT::Evolve does 8 inverse FFTs per band (height, displacement x and z, slope x and z, and three derivatives for the
// Jacobian). Each of those spectra is CONJUGATE-SYMMETRIC (f(-k) = conj(f(k))), so its inverse FFT is purely REAL. Then for two such spectra A and B
//     IFFT(A + iB) = IFFT(A) + i IFFT(B) = a + i b      (a, b real)
// so one complex FFT carries two real results: the real part is a, the imaginary part is b. 8 FFTs become 4. And a float4 holds two complex numbers,
// so the 4 become 2 textures of float4: the GPU runs two complex FFTs at once in each.
//     texture A = (P0, P1),  texture B = (P2, P3)           one complex number = (re, im)
//     P0 = height + i dispX     P1 = dispZ + i slopeX     P2 = slopeZ + i dXdx     P3 = dZdz + i dXdz
#include "OceanFFT.h"
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace OceanGpuPack {

struct Float4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

// One band's spectrum as the shader reads it. Mode (i, j) is at index j * n + i, the same order as SeaSpectrum::BandField.
struct PackedBand {
    int n = 0;
    double patchSize = 0.0;
    std::vector<Float4> modes;   // xy = h0(k), zw = conj(h0(-k)): the two numbers Evolve needs, so the shader does ONE read per mode, not two
    std::vector<float> omega;    // radians per second of that mode (depth aware, from the CPU: the shader never recomputes the dispersion relation)
    float DeltaK() const { return n > 0 && patchSize > 0.0 ? static_cast<float>(SeaState::kTwoPi / patchSize) : 0.0f; }
};

inline PackedBand Pack(const SeaSpectrum::BandField& field)
{
    PackedBand packed;
    packed.n = field.N();
    packed.patchSize = field.band.patchSize;
    const size_t count = static_cast<size_t>(packed.n) * packed.n;
    packed.modes.assign(count, Float4{});
    packed.omega.assign(count, 0.0f);
    for (int j = 0; j < packed.n; ++j) {
        for (int i = 0; i < packed.n; ++i) {
            const size_t index = field.Index(i, j), opposite = field.Opposite(i, j);
            packed.modes[index] = Float4{ field.h0[index].re, field.h0[index].im, field.h0[opposite].re, -field.h0[opposite].im };
            packed.omega[index] = field.omega[index];
        }
    }
    return packed;
}

// The constant buffer of the evolve pass (b0 in ocean_evolve_cs.hlsl). 16 bytes: a constant buffer is made of 16-byte rows.
struct EvolveConstants {
    std::uint32_t n = 0;     // modes per side
    float deltaK = 0.0f;     // radians per metre between neighbouring modes (2 pi / patch size)
    float time = 0.0f;       // seconds
    float padding = 0.0f;
};
static_assert(sizeof(EvolveConstants) == 16, "a constant buffer must be a multiple of 16 bytes");

// The two textures the evolve pass writes: .a = (P0, P1), .b = (P2, P3), index j * n + i.
struct PackedSpectra {
    int n = 0;
    std::vector<Float4> a, b;
};

// shaders/ocean_evolve_cs.hlsl, one thread per mode, in float like the GPU. Keep the two in step.
inline PackedSpectra EvolvePacked(const PackedBand& band, float time)
{
    using C = std::complex<float>;
    PackedSpectra out;
    out.n = band.n;
    const size_t count = static_cast<size_t>(band.n) * band.n;
    out.a.assign(count, Float4{});
    out.b.assign(count, Float4{});
    const float deltaK = band.DeltaK();
    const C i1(0.0f, 1.0f);
    for (int j = 0; j < band.n; ++j) {
        for (int i = 0; i < band.n; ++i) {
            const size_t index = static_cast<size_t>(j) * band.n + i;
            const Float4 m = band.modes[index];
            const float kx = deltaK * static_cast<float>(SeaSpectrum::SignedFrequency(i, band.n));
            const float kz = deltaK * static_cast<float>(SeaSpectrum::SignedFrequency(j, band.n));
            const float k = std::sqrt(kx * kx + kz * kz);
            // A mode that is the zero wave vector, or holds no energy in either direction, stays zero (and must not divide by k = 0).
            if (k < 1e-9f || (m.x == 0.0f && m.y == 0.0f && m.z == 0.0f && m.w == 0.0f)) continue;
            const float phase = band.omega[index] * time;
            const C e(std::cos(phase), std::sin(phase));
            const C value = C(m.x, m.y) * std::conj(e) + C(m.z, m.w) * e;   // h0(k) e^{-iwt} + conj(h0(-k)) e^{+iwt}
            const C height = value;
            const C dispX = -i1 * (kx / k) * value, dispZ = -i1 * (kz / k) * value;
            const C slopeX = i1 * kx * value, slopeZ = i1 * kz * value;
            const C dxx = (kx * kx / k) * value, dzz = (kz * kz / k) * value, dxz = (kx * kz / k) * value;
            const C p0 = height + i1 * dispX, p1 = dispZ + i1 * slopeX, p2 = slopeZ + i1 * dxx, p3 = dzz + i1 * dxz;
            out.a[index] = Float4{ p0.real(), p0.imag(), p1.real(), p1.imag() };
            out.b[index] = Float4{ p2.real(), p2.imag(), p3.real(), p3.imag() };
        }
    }
    return out;
}

}   // namespace OceanGpuPack
