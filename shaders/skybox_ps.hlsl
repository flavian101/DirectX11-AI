#include "common.hlsli"
TextureCube skyboxTexture : register(t0);
TextureCube irradianceDebugMap : register(t11);     // baked by EnvironmentLighting
TextureCube prefilteredDebugMap : register(t12);
SamplerState skyboxSampler : register(s0);
struct PS_IN { float4 position : SV_POSITION; float3 direction : TEXCOORD0; };
float4 main(PS_IN input) : SV_Target {
    const float3 direction = normalize(input.direction);
    // Editor aid: show a baked lighting map instead of the sky, to see what the lighting is made of.
    if (iblDebug.x > 0.5f && iblDebug.x < 1.5f) return float4(irradianceDebugMap.SampleLevel(skyboxSampler, direction, 0.0f).rgb, 1.0f) * materialColor;
    if (iblDebug.x > 1.5f) return float4(prefilteredDebugMap.SampleLevel(skyboxSampler, direction, iblDebug.y).rgb, 1.0f) * materialColor;
    return skyboxTexture.Sample(skyboxSampler, direction) * materialColor;
}
