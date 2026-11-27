// The ocean's last compute pass (roadmap O2): regroup the FFT's output for the water's shaders and apply the choppiness.
// CPU twin: OceanGpuAssemble::AssembleTexel (src/OceanGpuAssemble.h), tested against OceanFFT. Keep the three in step.
//
//   in  A = (height, displacement x, displacement z, slope x)     B = (slope z, dXdx, dZdz, dXdz)       (ocean_evolve_cs.hlsl)
//   out displacement map (u0) = (c * displacement x, height, c * displacement z, 0)     the vertex shader
//       slope map        (u1) = (slope x, slope z, Jacobian, 0)                         the pixel shader
//   c = choppiness; Jacobian = (1 + c dXdx)(1 + c dZdz) - (c dXdz)^2
// Both outputs are RGBA16F with a mip chain: this pass writes mip 0 and the host generates the rest (GenerateMips).
//
// Registers of THIS pass: b0 constants, t0 A, t1 B, u0 displacement map, u1 slope map.
cbuffer OceanAssemble : register(b0) {
    uint  oceanAssembleN;        // texels per side
    float oceanChoppiness;
    float oceanAssemblePadding0;
    float oceanAssemblePadding1;
};

Texture2D<float4> oceanSeaA : register(t0);
Texture2D<float4> oceanSeaB : register(t1);
RWTexture2D<float4> oceanDisplacementMap : register(u0);
RWTexture2D<float4> oceanSlopeMap : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= oceanAssembleN || id.y >= oceanAssembleN) return;   // the grid of groups is rounded up: extra threads do nothing

    const float4 a = oceanSeaA.Load(int3(id.xy, 0));
    const float4 b = oceanSeaB.Load(int3(id.xy, 0));
    const float jxx = 1.0f + oceanChoppiness * b.y;
    const float jzz = 1.0f + oceanChoppiness * b.z;
    const float jxz = oceanChoppiness * b.w;
    oceanDisplacementMap[id.xy] = float4(oceanChoppiness * a.y, a.x, oceanChoppiness * a.z, 0.0f);
    oceanSlopeMap[id.xy] = float4(a.w, b.x, jxx * jzz - jxz * jxz, 0.0f);
}
