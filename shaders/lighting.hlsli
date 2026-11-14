// Shared surface lighting: physically based shading (PBR) with image-based lighting (IBL).
// Any pixel shader that wants "the scene's lighting" fills a PbrInput and calls ShadePbr, so a
// lighting change happens in ONE place. (Lesson 19. Before this it was Blinn-Phong with a flat ambient.)
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI
#include "common.hlsli"
#include "shadow.hlsli"

static const float kPi = 3.14159265359f;

// Baked from the sky by EnvironmentLighting (src/ShaderSlots.h). Always bound; zeros when nothing is baked.
TextureCube irradianceMap : register(t11);     // soft light from every direction, for diffuse
TextureCube prefilteredMap : register(t12);    // the sky blurred per roughness level (the mips), for reflections
Texture2D brdfTable : register(t13);           // x = scale, y = bias for the reflectivity, by (N.V, roughness)
SamplerState iblSampler : register(s2);        // linear, clamped

struct PbrInput {
    float3 albedo;       // linear base colour
    float metallic;      // 0 = plastic/stone/skin, 1 = bare metal
    float roughness;     // 0 = mirror, 1 = chalk
    float occlusion;     // 1 = open, 0 = fully hidden from the sky (from the occlusion map)
    float3 normal;       // world-space, normalised, normal map already applied
    float3 worldPos;
};

// --- the three parts of the Cook-Torrance specular BRDF ---
// D: what fraction of the surface's tiny facets point along the half vector (the highlight's shape).
float DistributionGGX(float NdotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}
// G: how much the facets shadow and mask each other (rougher = more).
float GeometrySchlickGGX(float NdotX, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;   // the "direct light" version of k
    return NdotX / (NdotX * (1.0f - k) + k);
}
// F: Fresnel - every surface turns into a mirror at grazing angles. F0 = reflectivity looking straight on.
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// What one light contributes. `radiance` is "the light's colour x intensity", in the engine's old
// convention: a white surface facing the light shows exactly that value.
float3 DirectLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness, float3 F0)
{
    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0f) return float3(0.0f, 0.0f, 0.0f);
    const float3 H = normalize(V + L);
    const float NdotV = max(dot(N, V), 1e-4f);
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));

    const float D = DistributionGGX(NdotH, roughness);
    const float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
    const float3 F = FresnelSchlick(VdotH, F0);

    const float3 specular = (D * G) * F / (4.0f * NdotV * NdotL + 1e-4f);
    // Light that is reflected (F) cannot also be absorbed and re-emitted as diffuse; metals have no diffuse at all.
    const float3 kD = (1.0f - F) * (1.0f - metallic);
    // The BRDF divides diffuse by pi; scaling the light by pi cancels it, which keeps the old light
    // intensities meaning what they always meant. (The specular gets the same pi, consistently.)
    return (kD * albedo / kPi + specular) * radiance * kPi * NdotL;
}

float3 ShadePbr(PbrInput s)
{
    const float3 N = s.normal;
    const float3 V = normalize(cameraPosNumLights.xyz - s.worldPos);
    const float NdotV = max(dot(N, V), 1e-4f);
    const float roughness = clamp(s.roughness, 0.045f, 1.0f);   // a perfect mirror has an infinitely small highlight: avoid dividing by ~0
    const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), s.albedo, s.metallic);   // plastic reflects ~4%; metal reflects its own colour

    float3 color = float3(0.0f, 0.0f, 0.0f);

    // The sun. Only the SUN is shadowed (the sky light and the point lights are not).
    {
        const float3 L = normalize(-dirDirection.xyz);
        const float shadow = ShadowFactor(s.worldPos, N, L);
        color += DirectLight(N, V, L, dirColor.rgb * dirDirection.w, s.albedo, s.metallic, roughness, F0) * shadow;
    }
    // A second directional light (the moon beside a sun): lights the scene, but casts no shadows.
    [branch]
    if (dirDirection2.w > 0.0f) {
        const float3 L2 = normalize(-dirDirection2.xyz);
        color += DirectLight(N, V, L2, dirColor2.rgb * dirDirection2.w, s.albedo, s.metallic, roughness, F0);
    }

    // Point lights with smooth quadratic range falloff.
    const int numPointLights = (int)cameraPosNumLights.w;
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= numPointLights) break;
        const float3 toLight = pointPosRange[i].xyz - s.worldPos;
        const float dist = length(toLight);
        const float3 L = toLight / max(dist, 1e-4f);
        float falloff = saturate(1.0f - dist / pointPosRange[i].w);
        falloff *= falloff;
        const float3 radiance = pointColorIntensity[i].rgb * pointColorIntensity[i].w * falloff;
        color += DirectLight(N, V, L, radiance, s.albedo, s.metallic, roughness, F0);
    }

    // Light from everywhere else: the sky (image-based lighting) - or, if there is none, a flat colour.
    if (iblParams.x > 0.5f) {
        const float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        const float3 kD = (1.0f - F) * (1.0f - s.metallic);

        // Diffuse: the irradiance map already holds the sky's cosine-weighted average for this normal.
        const float3 irradiance = irradianceMap.SampleLevel(iblSampler, SkyDirection(N), 0.0f).rgb * iblParams.y * skyTint.rgb;
        const float3 diffuse = irradiance * s.albedo * kD;

        // Specular: look up the sky in the mirror direction, from the mip that matches the roughness,
        // then scale by the reflectivity from the BRDF table ("split sum": two lookups instead of an integral).
        const float3 R = reflect(-V, N);
        const float3 prefiltered = prefilteredMap.SampleLevel(iblSampler, SkyDirection(R), roughness * iblParams.w).rgb * iblParams.z * skyTint.rgb;
        const float2 brdf = brdfTable.SampleLevel(iblSampler, float2(NdotV, roughness), 0.0f).rg;
        const float3 specular = prefiltered * (F * brdf.x + brdf.y);

        color += (diffuse + specular) * s.occlusion;
    } else {
        color += ambientColor.rgb * s.albedo * s.occlusion;
    }
    return color;
}
#endif
