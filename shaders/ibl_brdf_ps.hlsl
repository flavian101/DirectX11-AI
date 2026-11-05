// Bake 3 of 3: the BRDF lookup table. For a given angle between the normal and the viewer (x axis)
// and a given roughness (y axis), the specular reflection splits into  F0 * scale + bias:
// "scale" multiplies the surface's base reflectivity, "bias" is the extra at grazing angles.
// It depends on nothing but those two numbers, so it is baked once and never changes.
#include "ibl_common.hlsli"

struct PS_IN { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

// Smith geometry term (self-shadowing of the microscopic bumps), with the "k" used for environment lighting.
float GeometrySchlickGGX_IBL(float NdotX, float roughness)
{
    const float k = (roughness * roughness) * 0.5f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

float4 main(PS_IN input) : SV_Target
{
    const float NdotV = max(input.uv.x, 0.001f);
    const float roughness = max(input.uv.y, 0.02f);   // uv.y = 0 is the top row: roughness grows downward

    const float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
    const float3 N = float3(0.0f, 0.0f, 1.0f);

    float scale = 0.0f;
    float bias = 0.0f;
    const uint sampleCount = 512u;
    [loop] for (uint i = 0u; i < sampleCount; ++i) {
        const float3 H = ImportanceSampleGGX(Hammersley(i, sampleCount), N, roughness);
        const float3 L = normalize(2.0f * dot(V, H) * H - V);
        const float NdotL = saturate(L.z);
        const float NdotH = saturate(H.z);
        const float VdotH = saturate(dot(V, H));
        if (NdotL > 0.0f) {
            const float G = GeometrySchlickGGX_IBL(NdotL, roughness) * GeometrySchlickGGX_IBL(NdotV, roughness);
            const float visibility = (G * VdotH) / (NdotH * NdotV);
            const float fresnel = pow(1.0f - VdotH, 5.0f);
            scale += (1.0f - fresnel) * visibility;
            bias += fresnel * visibility;
        }
    }
    return float4(scale / sampleCount, bias / sampleCount, 0.0f, 1.0f);
}
