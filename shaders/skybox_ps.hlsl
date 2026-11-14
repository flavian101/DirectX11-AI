#include "common.hlsli"
TextureCube skyboxTexture : register(t0);
TextureCube irradianceDebugMap : register(t11);     // baked by EnvironmentLighting
TextureCube prefilteredDebugMap : register(t12);
SamplerState skyboxSampler : register(s0);
struct PS_IN { float4 position : SV_POSITION; float3 direction : TEXCOORD0; };
float4 main(PS_IN input) : SV_Target {
    // The sky can be turned (Skybox rotation): look it up in its own space. The tint is already in materialColor.
    const float3 direction = SkyDirection(normalize(input.direction));
    // Editor aid: show a baked lighting map instead of the sky, to see what the lighting is made of.
    if (iblDebug.x > 0.5f && iblDebug.x < 1.5f) return float4(irradianceDebugMap.SampleLevel(skyboxSampler, direction, 0.0f).rgb, 1.0f) * materialColor;
    if (iblDebug.x > 1.5f) return float4(prefilteredDebugMap.SampleLevel(skyboxSampler, direction, iblDebug.y).rgb, 1.0f) * materialColor;
    // Under water there is no sky to see: what is left of the view is the water itself (its surface draws the sky seen through it).
    if (fogExtra.z > 0.5f) return float4(fogColor.rgb, 1.0f);
    float4 sky = skyboxTexture.Sample(skyboxSampler, direction) * materialColor;
    // Haze: with fog on, the sky near the horizon takes the fog's colour, so the far ground fades into it with no visible seam.
    [branch]
    if (fogParams.x > 0.0f) {
        const float haze = saturate(fogExtra.y * exp(-abs(direction.y) * 6.0f));
        sky.rgb = lerp(sky.rgb, fogColor.rgb, haze);
    }
    return sky;
}
