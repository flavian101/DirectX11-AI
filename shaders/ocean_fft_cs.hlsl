// The ocean's FFT pass (roadmap O2): the inverse FFT of every row (oceanFftVertical = 0) or every column (= 1) of a texture.
// CPU twin: OceanGpuFft::FftGroup / Pass (src/OceanGpuFft.h), tested against OceanFFT. Keep the three in step.
//
// One thread GROUP per line, dispatched as (n, 1, 1). The group loads its line into groupshared memory in bit-reversed order, runs log2(n) stages of
// n/2 butterflies (every stage touches each element exactly once, so the threads never clash), and writes the line back. Each element is a float4
// = two complex numbers (see ocean_evolve_cs.hlsl), so one butterfly transforms two spectra at once. The result is not scaled.
//
// The group has OCEAN_FFT_THREADS threads whatever n is, so each thread loops over several elements when the line is longer (n up to 512).
// Registers of THIS pass: b0 constants, t0 source texture, u0 target texture. (A texture is never the source and the target of one pass.)
#define OCEAN_FFT_THREADS 128
#define OCEAN_FFT_MAX_N 512

cbuffer OceanFft : register(b0) {
    uint oceanFftN;          // samples per line (a power of two)
    uint oceanFftLog2N;
    uint oceanFftVertical;   // 0 = lines are rows, 1 = lines are columns
    uint oceanFftPadding;
};

Texture2D<float4> oceanFftSource : register(t0);
RWTexture2D<float4> oceanFftTarget : register(u0);

groupshared float4 oceanFftShared[OCEAN_FFT_MAX_N];

// (c + i s) * v for both complex numbers of the float4.
float4 TimesTwiddle(float4 v, float c, float s)
{
    return float4(v.x * c - v.y * s, v.x * s + v.y * c, v.z * c - v.w * s, v.z * s + v.w * c);
}

[numthreads(OCEAN_FFT_THREADS, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
    const uint n = oceanFftN;
    const uint lineIndex = groupId.x;
    const uint tid = threadId.x;

    // Load, bit-reversed: element i of the line goes to the slot whose index is i with its bits in reverse order.
    for (uint i = tid; i < n; i += OCEAN_FFT_THREADS) {
        const uint2 coord = oceanFftVertical != 0 ? uint2(lineIndex, i) : uint2(i, lineIndex);
        oceanFftShared[reversebits(i) >> (32u - oceanFftLog2N)] = oceanFftSource.Load(int3(coord, 0));
    }
    GroupMemoryBarrierWithGroupSync();

    // log2(n) stages; stage with half-block length halfLen combines elements halfLen apart inside blocks of 2 * halfLen.
    for (uint halfLen = 1; halfLen < n; halfLen <<= 1) {
        const uint span = halfLen * 2;
        for (uint b = tid; b < n / 2; b += OCEAN_FFT_THREADS) {
            const uint block = b / halfLen;
            const uint k = b % halfLen;
            const uint i0 = block * span + k;
            const uint i1 = i0 + halfLen;
            float s, c;
            sincos(6.2831853f * (float)k / (float)span, s, c);   // + : the INVERSE transform, like OceanFFT::Fft1D(data, n, true)
            const float4 u = oceanFftShared[i0];
            const float4 v = TimesTwiddle(oceanFftShared[i1], c, s);
            oceanFftShared[i0] = u + v;
            oceanFftShared[i1] = u - v;
        }
        GroupMemoryBarrierWithGroupSync();   // the next stage reads what this one wrote
    }

    for (uint j = tid; j < n; j += OCEAN_FFT_THREADS) {
        const uint2 coord = oceanFftVertical != 0 ? uint2(lineIndex, j) : uint2(j, lineIndex);
        oceanFftTarget[coord] = oceanFftShared[j];
    }
}
