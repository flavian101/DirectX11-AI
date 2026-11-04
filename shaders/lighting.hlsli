// Shared surface lighting (Blinn-Phong): ambient + the shadowed sun + the point lights.
// Any pixel shader that wants "the scene's lighting" includes this instead of copying it, so
// a lighting change happens in ONE place. (It used to be pasted into every lit shader.)
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI
#include "common.hlsli"
#include "shadow.hlsli"

// ambient   : the light that comes from "everywhere" (a material's occlusion map dims this part only)
// diffuse   : the DIRECT light arriving from the sun and the point lights (multiply by the surface colour)
// specular  : the highlight to ADD on top. `shininess` is the Blinn-Phong exponent: higher = tighter, glossier.
void ComputeLightingEx(float3 worldPos, float3 normal, float shininess,
                       out float3 ambient, out float3 diffuse, out float3 specular)
{
    const float3 viewDir = normalize(cameraPosNumLights.xyz - worldPos);

    ambient = ambientColor.rgb;
    diffuse = float3(0.0f, 0.0f, 0.0f);
    specular = float3(0.0f, 0.0f, 0.0f);

    // Directional light (Blinn-Phong)
    {
        float3 L = normalize(-dirDirection.xyz);
        // Only the SUN is shadowed (ambient and the point lights are not).
        float shadow = ShadowFactor(worldPos, normal, L);
        diffuse += dirColor.rgb * dirDirection.w * saturate(dot(normal, L)) * shadow;
        float3 h = normalize(L + viewDir);
        specular += dirColor.rgb * dirDirection.w * pow(saturate(dot(normal, h)), shininess) * shadow;
    }

    // Point lights with smooth quadratic range falloff
    const int numPointLights = (int)cameraPosNumLights.w;
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= numPointLights) break;
        float3 toLight = pointPosRange[i].xyz - worldPos;
        float dist = length(toLight);
        float3 L = toLight / max(dist, 1e-4f);
        float falloff = saturate(1.0f - dist / pointPosRange[i].w);
        falloff *= falloff;
        float3 radiance = pointColorIntensity[i].rgb * pointColorIntensity[i].w * falloff;
        diffuse += radiance * saturate(dot(normal, L));
        float3 h = normalize(L + viewDir);
        specular += radiance * pow(saturate(dot(normal, h)), shininess);
    }
}

// The original entry point, unchanged for every shader that already calls it (terrain, ...):
// "diffuse" is ambient + direct, and the highlight is the classic fixed 32.
void ComputeLighting(float3 worldPos, float3 normal, out float3 diffuse, out float3 specular)
{
    float3 ambient, direct;
    ComputeLightingEx(worldPos, normal, 32.0f, ambient, direct, specular);
    diffuse = ambient + direct;
}
#endif
