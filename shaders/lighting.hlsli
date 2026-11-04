// Shared surface lighting (Blinn-Phong): ambient + the shadowed sun + the point lights.
// Any pixel shader that wants "the scene's lighting" includes this instead of copying it, so
// a lighting change happens in ONE place. (It used to be pasted into every lit shader.)
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI
#include "common.hlsli"
#include "shadow.hlsli"

// diffuse  : the total light arriving (multiply by the surface colour)
// specular : the highlight to ADD on top
void ComputeLighting(float3 worldPos, float3 normal, out float3 diffuse, out float3 specular)
{
    const float3 viewDir = normalize(cameraPosNumLights.xyz - worldPos);

    diffuse = ambientColor.rgb;
    specular = float3(0.0f, 0.0f, 0.0f);

    // Directional light (Blinn-Phong)
    {
        float3 L = normalize(-dirDirection.xyz);
        // Only the SUN is shadowed (ambient and the point lights are not).
        float shadow = ShadowFactor(worldPos, normal, L);
        diffuse += dirColor.rgb * dirDirection.w * saturate(dot(normal, L)) * shadow;
        float3 h = normalize(L + viewDir);
        specular += dirColor.rgb * dirDirection.w * pow(saturate(dot(normal, h)), 32.0f) * shadow;
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
        specular += radiance * pow(saturate(dot(normal, h)), 32.0f);
    }
}
#endif
