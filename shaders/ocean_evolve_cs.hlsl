// The ocean's first compute pass (roadmap O2): move every mode of one band's spectrum to time t and pack the results for the FFT.
// CPU twin: OceanGpuPack::EvolvePacked (src/OceanGpuPack.h). Same maths as OceanFFT::Evolve. Keep the three in step.
//
//   value(k, t)  = h0(k) e^{-i w t} + conj(h0(-k)) e^{+i w t}        (h0(k) and conj(h0(-k)) arrive already paired in oceanModes)
//   height = value        displacement = -i (k / |k|) value        slope = i k value        derivatives of the displacement: (k_a k_b / |k|) value
// Eight real results need four complex FFTs: two real results share one complex number (A + iB), because each spectrum is conjugate-symmetric.
//   P0 = height + i dispX     P1 = dispZ + i slopeX     P2 = slopeZ + i dXdx     P3 = dZdz + i dXdz
// Output: oceanSpectrumA = (P0, P1) and oceanSpectrumB = (P2, P3), one complex number per two floats, so one float4 is two complex numbers.
//
// Registers of THIS pass (the compute stage has its own set; they do not clash with the graphics slots in ShaderSlots.h):
//   b0 constants, t0 modes, t1 omega, u0 spectrum A, u1 spectrum B.
cbuffer OceanEvolve : register(b0) {
    uint  oceanN;        // modes per side (a power of two)
    float oceanDeltaK;   // radians per metre between neighbouring modes
    float oceanTime;     // seconds
    float oceanPadding;
};

StructuredBuffer<float4> oceanModes : register(t0);   // xy = h0(k), zw = conj(h0(-k))
StructuredBuffer<float>  oceanOmega : register(t1);   // radians per second
// Typed UAV STORES to R32G32B32A32_FLOAT are part of Direct3D 11. (Typed LOADS from it are not, so the FFT passes read through an SRV instead.)
RWTexture2D<float4> oceanSpectrumA : register(u0);
RWTexture2D<float4> oceanSpectrumB : register(u1);

float2 CMul(float2 a, float2 b) { return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
float2 TimesI(float2 v) { return float2(-v.y, v.x); }       // i * v
float2 TimesMinusI(float2 v) { return float2(v.y, -v.x); }  // -i * v

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= oceanN || id.y >= oceanN) return;   // the grid of groups is rounded up: extra threads do nothing

    const uint index = id.y * oceanN + id.x;
    const float4 mode = oceanModes[index];
    // Signed frequency in FFT order: 0, 1, ... N/2 - 1, -N/2, ... -1 (SeaSpectrum::SignedFrequency).
    const int fx = id.x < oceanN / 2 ? (int)id.x : (int)id.x - (int)oceanN;
    const int fz = id.y < oceanN / 2 ? (int)id.y : (int)id.y - (int)oceanN;
    const float kx = oceanDeltaK * fx;
    const float kz = oceanDeltaK * fz;
    const float k = sqrt(kx * kx + kz * kz);

    float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (k >= 1e-9f && any(mode != 0.0f)) {
        float s, c;
        sincos(oceanOmega[index] * oceanTime, s, c);
        const float2 value = CMul(mode.xy, float2(c, -s)) + CMul(mode.zw, float2(c, s));
        const float2 dispX = (kx / k) * TimesMinusI(value);
        const float2 dispZ = (kz / k) * TimesMinusI(value);
        const float2 slopeX = kx * TimesI(value);
        const float2 slopeZ = kz * TimesI(value);
        const float2 dxx = (kx * kx / k) * value;
        const float2 dzz = (kz * kz / k) * value;
        const float2 dxz = (kx * kz / k) * value;
        a = float4(value + TimesI(dispX), dispZ + TimesI(slopeX));
        b = float4(slopeZ + TimesI(dxx), dzz + TimesI(dxz));
    }
    oceanSpectrumA[id.xy] = a;
    oceanSpectrumB[id.xy] = b;
}
