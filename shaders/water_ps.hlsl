#include "waves.hlsli"
#include "noise.hlsli"

TextureCube  environmentMap : register(t1);   // the skybox cubemap, bound once per frame by Scene
Texture2D<float4> planarReflection : register(t3);   // the scene seen from below the water (when reflectionParams.x = 1)
Texture2D<float> sceneDepth : register(t4);   // the camera's depth buffer, bound for the water pass only
SamplerState linearSampler  : register(s0);
SamplerState clampSampler   : register(s2);   // edges clamp: screen-space lookups must not wrap

struct PS_IN { float4 position : SV_POSITION; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

// Hardware depth (0..1, spread non-linearly) -> distance in front of the camera, in world units.
// This is the inverse of the perspective projection's depth mapping for D3D (z in 0..1).
float LinearizeDepth(float d)
{
    const float n = screenParams.z;   // near plane
    const float f = screenParams.w;   // far plane
    return n * f / (f - d * (f - n));
}

float4 main(PS_IN input) : SV_Target
{
    const float reflectivity = waveParams.w;
    const float3 deepColor = waterDeepColor.rgb;
    const float3 shallowColor = waterShallowColor.rgb;
    const float baseAlpha = waterDeepColor.a;
    const float fadeDistance = waterDepthParams.x;
    const float foamDistance = waterDepthParams.y;
    const float foamStrength = waterDepthParams.z;
    const float absorption   = waterDepthParams.w;

    // Per-pixel normal from the analytic wave slope: a hill rising toward +x tilts the normal toward -x.
    float height;
    float2 slope;
    EvaluateWaves(input.worldPos.xz, height, slope);
    float3 N = normalize(float3(-slope.x, 1.0f, -slope.y));
    float3 V = normalize(cameraPos.xyz - input.worldPos);   // surface -> eye
    float3 L = normalize(-dirDirection.xyz);                // surface -> sun

    // ---- How much water lies behind this pixel? ----
    // Same screen pixel in the camera's depth buffer = the nearest SOLID thing along this ray
    // (ground, or the far plane where there is only sky). Subtract the water's own distance.
    const float groundZ = LinearizeDepth(sceneDepth.Load(int3(int2(input.position.xy), 0)));
    const float waterZ = LinearizeDepth(input.position.z);
    const float thickness = max(groundZ - waterZ, 0.0f);

    const float depthFade = saturate(thickness / fadeDistance);        // 0 at the shore, 1 in deep water
    const float absorb = 1.0f - exp(-thickness * absorption);           // Beer-Lambert: light fades exponentially
    const float3 baseColor = lerp(shallowColor, deepColor, absorb);     // clear and bright -> dark and saturated

    // Schlick Fresnel: at a glancing angle water behaves like a mirror, looking straight
    // down you mostly see the water body. F0 = 0.02 is water's reflectance head-on.
    const float F0 = 0.02f;
    float fresnel = (F0 + (1.0f - F0) * pow(1.0f - saturate(dot(N, V)), 5.0f)) * reflectivity;
    float3 reflected = environmentMap.Sample(linearSampler, reflect(-V, N)).rgb;   // cheap fallback: the sky only
    if (reflectionParams.x > 0.5f) {
        // Where on screen is this pixel? The reflection image was drawn with the same projection,
        // so the same screen position shows the mirror image of what lies behind the surface.
        float2 screenUV = input.position.xy * screenParams.xy;
        screenUV += N.xz * waterLighting.z;                      // wave normals wobble the mirror
        // The mirrored camera renders left-right flipped (a mirror reverses handedness), so flip u back.
        const float2 reflectionUV = float2(1.0f - screenUV.x, screenUV.y);
        reflected = planarReflection.SampleLevel(clampSampler, reflectionUV, 0).rgb;
    }

    // Water body: tinted diffuse (same lighting floor as before).
    float lighting = 0.35f + saturate(dot(N, L)) * dirDirection.w * 0.65f;
    float3 body = baseColor * (ambientColor.rgb + lighting * dirColor.rgb);

    // Sun glint: a tight Blinn-Phong highlight on the wave normals.
    float3 H = normalize(L + V);
    float3 glint = dirColor.rgb * dirDirection.w * waterLighting.x * pow(saturate(dot(N, H)), waterLighting.y);

    float3 color = lerp(body, reflected, fresnel) + glint;

    // Shallow water is more see-through; right at the shoreline it fades to nothing so there is
    // no hard line where the plane cuts through the ground.
    const float alphaBody = baseAlpha * lerp(0.35f, 1.0f, depthFade);
    const float edgeFade = saturate(thickness / (0.35f * fadeDistance));
    float alpha = saturate(lerp(alphaBody, 1.0f, fresnel) + dot(glint, float3(0.333f, 0.333f, 0.333f))) * edgeFade;

    // ---- Shoreline foam: a thin band where the water is shallow, broken up by moving noise ----
    const float foamBand = 1.0f - saturate(thickness / max(foamDistance, 0.001f));
    const float2 foamUV = input.worldPos.xz * 0.9f + float2(frameParams.x * 0.25f, -frameParams.x * 0.18f);
    const float noise = ValueNoise(foamUV) * 0.65f + ValueNoise(foamUV * 2.7f + 17.0f) * 0.35f;
    const float foam = saturate(foamBand * smoothstep(0.25f, 0.7f, noise + foamBand * 0.35f) * foamStrength);
    const float3 foamColor = float3(0.9f, 0.95f, 1.0f) * (ambientColor.rgb + lighting * dirColor.rgb);

    color = lerp(color, foamColor, foam);
    alpha = max(alpha, foam);   // foam is solid even where the water itself is almost invisible
    return float4(color, alpha);
}
