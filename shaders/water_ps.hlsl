#include "waves.hlsli"

TextureCube  environmentMap : register(t1);   // the skybox cubemap, bound once per frame by Scene
Texture2D<float> sceneDepth : register(t4);   // the camera's depth buffer, bound for the water pass only
SamplerState linearSampler  : register(s0);

struct PS_IN { float4 position : SV_POSITION; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

// Hardware depth (0..1, spread non-linearly) -> distance in front of the camera, in world units.
// This is the inverse of the perspective projection's depth mapping for D3D (z in 0..1).
float LinearizeDepth(float d)
{
    const float n = screenParams.z;   // near plane
    const float f = screenParams.w;   // far plane
    return n * f / (f - d * (f - n));
}

// Cheap procedural noise (no texture needed): a hashed grid, smoothly interpolated.
float Hash(float2 p) { return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f); }
float ValueNoise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), f.x),
                lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
}

float4 main(PS_IN input) : SV_Target
{
    const float reflectivity = materialParams.w;
    const float3 deepColor = materialColor.rgb;
    const float3 shallowColor = materialColor2.rgb;
    const float baseAlpha = materialColor.a;
    // materialExtra2 = (fade distance, foam distance, foam strength, absorption)
    const float fadeDistance = materialExtra2.x;
    const float foamDistance = materialExtra2.y;
    const float foamStrength = materialExtra2.z;
    const float absorption   = materialExtra2.w;

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
    float3 sky = environmentMap.Sample(linearSampler, reflect(-V, N)).rgb;

    // Water body: tinted diffuse (same lighting floor as before).
    float lighting = 0.35f + saturate(dot(N, L)) * dirDirection.w * 0.65f;
    float3 body = baseColor * (ambientColor.rgb + lighting * dirColor.rgb);

    // Sun glint: a tight Blinn-Phong highlight on the wave normals.
    // materialExtra = (glint strength, glint sharpness)
    float3 H = normalize(L + V);
    float3 glint = dirColor.rgb * dirDirection.w * materialExtra.x * pow(saturate(dot(N, H)), materialExtra.y);

    float3 color = lerp(body, sky, fresnel) + glint;

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
