// Receiving side of shadow mapping: "is something nearer to the sun than this pixel?"
// The map and matrix are produced by ShadowMap / Scene::RenderShadows (see docs/lessons).
#ifndef SHADOW_HLSLI
#define SHADOW_HLSLI
#include "common.hlsli"

Texture2D<float>       shadowMap     : register(t2);
SamplerComparisonState shadowSampler : register(s1);   // compares for us: ref <= stored ? 1 : 0

// worldPos : the pixel being shaded
// normal   : its world-space unit normal
// L        : unit vector from the surface TOWARDS the sun
// returns  : 1 = fully lit, (1 - strength) = fully shadowed
float ShadowFactor(float3 worldPos, float3 normal, float3 L)
{
    const float strength = shadowParams.z;
    if (strength <= 0.0f) return 1.0f;   // shadows off, or no shadow pass this frame

    // Normal-offset bias: slide the lookup point a little way OFF the surface, along its
    // normal, before testing. Surfaces seen at a grazing angle to the sun need the most.
    const float grazing = saturate(1.0f - dot(normal, L));
    const float3 samplePos = worldPos + normal * (shadowParams.w * shadowParams.y * grazing);

    // World -> the sun's clip space -> texture coordinates. The sun is orthographic, so w = 1.
    const float4 lightClip = mul(float4(samplePos, 1.0f), lightViewProj);
    const float3 ndc = lightClip.xyz / lightClip.w;
    const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);   // NDC y is up, texture v is down

    // 3x3 percentage-closer filtering: each tap already blends 2x2 texels in hardware.
    float lit = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            lit += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) * shadowParams.x, ndc.z);
        }
    }
    lit /= 9.0f;
    return lerp(1.0f - strength, 1.0f, lit);
}
#endif
