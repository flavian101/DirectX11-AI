// The water surface as a sum of GERSTNER waves, read from the table the CPU builds (src/WaterWaves.h, which is also
// what Water::SampleHeight uses, so gameplay code and the GPU agree on where the surface is).
//
//   rest position (x, z) -> phase = k * dot(dir, (x, z)) + omega * time + phase0
//   displaced x, z       = rest + Q * a * cos(phase) * dir          (crests pull points sideways: sharp crests)
//   height               = a * sin(phase)
//
// The vertex shader needs the displacement (to move vertices); the pixel shader needs the exact surface NORMAL and the
// "Jacobian" (how squeezed the surface is: small = about to fold over = foam). Both are evaluated at the REST position,
// which is what a Gerstner wave is parameterised by, so the lighting stays exact however far points were pulled sideways.
#ifndef WAVES_HLSLI
#define WAVES_HLSLI
#include "common.hlsli"
#include "water_material.hlsli"

static const float kTwoPi = 6.2831853f;

// Vertex stage. `maxK`: waves with a higher k (shorter wavelength) are faded out, because the grid is too coarse
// out there to show them (they would turn into noisy lumps instead of waves).
// Returns (dx, dy, dz): how far the surface point that rests at restXZ has moved.
float3 WaveDisplacement(float2 restXZ, float time, float maxK)
{
    float3 d = float3(0.0f, 0.0f, 0.0f);
    const int count = (int)waterWaveInfo.x;
    [loop]
    for (int i = 0; i < count; ++i) {
        const float4 dirK = waterWaveDirK[i];
        const float4 motion = waterWaveMotion[i];
        const float fade = saturate(2.0f - 2.0f * dirK.z / maxK);
        const float a = dirK.w * fade;
        const float phase = dirK.z * dot(dirK.xy, restXZ) + motion.x * time + motion.z;
        float s, c;
        sincos(phase, s, c);
        d.xz += dirK.xy * (motion.y * a * c);
        d.y  += a * s;
    }
    return d;
}

// Pixel stage. `pixelsPerUnit`: how many screen pixels one world unit covers at this distance. A wave shorter than
// a few pixels cannot be drawn (it shimmers), so it fades out smoothly instead, like a mip level.
// N = unit surface normal. J = Jacobian: 1 on flat water, falling toward 0 where a crest is about to fold over.
void EvaluateWaveShading(float2 restXZ, float time, float pixelsPerUnit, out float3 N, out float J)
{
    float cx = 0.0f, cz = 0.0f, sxx = 0.0f, sxz = 0.0f, szz = 0.0f;
    const int count = (int)waterWaveInfo.x;
    [loop]
    for (int i = 0; i < count; ++i) {
        const float4 dirK = waterWaveDirK[i];
        const float4 motion = waterWaveMotion[i];
        const float k = dirK.z;
        const float wavelengthPixels = (kTwoPi / k) * pixelsPerUnit;
        const float a = dirK.w * saturate((wavelengthPixels - 2.0f) * 0.25f);
        const float phase = k * dot(dirK.xy, restXZ) + motion.x * time + motion.z;
        float s, c;
        sincos(phase, s, c);
        const float ka = k * a;
        cx += ka * dirK.x * c;
        cz += ka * dirK.y * c;
        const float q = motion.y * ka * s;
        sxx += q * dirK.x * dirK.x;
        sxz += q * dirK.x * dirK.y;
        szz += q * dirK.y * dirK.y;
    }
    // Exact normal of the displaced surface: cross(dP/dz, dP/dx). Its Y component IS the Jacobian.
    const float3 n = float3(-cx * (1.0f - szz) - cz * sxz,
                            (1.0f - sxx) * (1.0f - szz) - sxz * sxz,
                            -cz * (1.0f - sxx) - cx * sxz);
    J = n.y;
    N = normalize(float3(n.x, max(n.y, 0.02f), n.z));
}
#endif
