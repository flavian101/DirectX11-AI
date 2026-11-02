#include "common.hlsli"

TextureCube  environmentMap : register(t1);   // the skybox cubemap, bound once per frame by Scene
SamplerState linearSampler  : register(s0);

struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

float4 main(PS_IN input) : SV_Target
{
    const float reflectivity = materialParams.w;

    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);   // surface -> eye
    float3 L = normalize(-dirDirection.xyz);                // surface -> sun

    // Schlick Fresnel: at a glancing angle water behaves like a mirror, looking straight
    // down you mostly see the water body. F0 = 0.02 is water's reflectance head-on.
    const float F0 = 0.02f;
    float fresnel = (F0 + (1.0f - F0) * pow(1.0f - saturate(dot(N, V)), 5.0f)) * reflectivity;

    float3 sky = environmentMap.Sample(linearSampler, reflect(-V, N)).rgb;

    // Water body: tinted diffuse (same lighting floor as the original shader).
    float lighting = 0.35f + saturate(dot(N, L)) * dirDirection.w * 0.65f;
    float3 body = materialColor.rgb * (ambientColor.rgb + lighting * dirColor.rgb);

    // Sun glint: a tight Blinn-Phong highlight on the wave normals.
    float3 H = normalize(L + V);
    float3 glint = dirColor.rgb * dirDirection.w * pow(saturate(dot(N, H)), 128.0f);

    float3 color = lerp(body, sky, fresnel) + glint;
    // More reflective -> more opaque, so the sky is not blended away over the terrain behind.
    float alpha = saturate(lerp(materialColor.a, 1.0f, fresnel) + dot(glint, float3(0.333f, 0.333f, 0.333f)));
    return float4(color, alpha);
}
