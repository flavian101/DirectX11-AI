// Bake 2 of 3: SPECULAR prefiltered environment. A mirror shows the sky sharply; a rough surface shows
// it blurred, and the blur depends on the surface's roughness. We precompute one blurred copy of the
// sky per roughness level and store them as the MIP LEVELS of a cube map: mip 0 = mirror, last = chalk.
#include "ibl_common.hlsli"

TextureCube environmentMap : register(t0);
SamplerState environmentSampler : register(s0);

struct PS_IN { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(PS_IN input) : SV_Target
{
    const float3 N = CubeFaceDirection((uint)bakeParams.x, input.uv);
    const float roughness = bakeParams.y;
    if (roughness < 0.001f) return float4(environmentMap.SampleLevel(environmentSampler, N, 0.0f).rgb, 1.0f);   // mirror: the sky itself

    // Assume the viewer looks straight down the normal (V = N = R). That is the "split sum" shortcut:
    // it is exact there and a good approximation elsewhere (the BRDF table corrects the rest).
    const float3 V = N;
    const uint sampleCount = bakeParams.w > 0.5f ? 96u : 1024u;   // w = 1: "fast", for a sky that is baked again and again (see ibl_irradiance_ps.hlsl)
    const float sourceFaceSize = bakeParams.z;
    const float texelSolidAngle = 4.0f * kPi / (6.0f * sourceFaceSize * sourceFaceSize);

    float3 color = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    [loop] for (uint i = 0u; i < sampleCount; ++i) {
        const float2 xi = Hammersley(i, sampleCount);
        const float3 H = ImportanceSampleGGX(xi, N, roughness);
        const float3 L = normalize(2.0f * dot(V, H) * H - V);   // V mirrored about H: where the light must come from
        const float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0f) {
            // Each sample covers a patch of sky that is large where the GGX density is low. Reading a
            // SMALLER mip of the source for those samples is the same as averaging the patch, and it
            // removes the speckle that plain sampling leaves.
            const float NdotH = saturate(dot(N, H));
            const float pdf = DistributionGGX(NdotH, roughness) * NdotH / (4.0f * saturate(dot(H, V))) + 0.0001f;
            const float sampleSolidAngle = 1.0f / (sampleCount * pdf + 0.0001f);
            const float mip = max(0.5f * log2(sampleSolidAngle / texelSolidAngle) + 1.0f, 0.0f);
            color += environmentMap.SampleLevel(environmentSampler, L, mip).rgb * NdotL;
            weight += NdotL;
        }
    }
    return float4(color / max(weight, 0.0001f), 1.0f);
}
