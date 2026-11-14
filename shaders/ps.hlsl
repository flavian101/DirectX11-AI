#include "lighting.hlsli"

Texture2D colorTexture : register(t0);       // albedo (sRGB: the GPU hands us linear light)
Texture2D normalTexture : register(t5);      // tangent-space normal map (linear data)
Texture2D ormTexture : register(t6);         // R = ambient occlusion, G = roughness, B = metallic
Texture2D emissiveTexture : register(t7);    // light the surface gives off by itself
SamplerState colorSampler : register(s0);

// b4 - this material's own block (CPU: SurfaceMaterialCB in SurfaceMaterial.h)
cbuffer SurfaceMaterial : register(b4) {
    float4 surfaceParams;   // x = normal strength, y = roughness scale, z = AO strength, w = emissive intensity
    float4 surfaceFlags;    // x = flip green (1/0), y = use normal map (1/0), z = debug view (0 lit, 1 vertex normal, 2 mapped normal, 3 tangent)
    float4 surfaceBaseColor; // linear rgba: multiplies the albedo map (a surface with no map is just this colour)
    float4 surfaceExtra;     // x = alpha cutoff (0 = opaque), y = metallic scale
};

struct PS_IN {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float4 tangent : TEXCOORD2;
};

// Rotates the normal stored in the map (relative to the surface) into world space.
// N, T, B are the three axes of the surface's own little coordinate system ("tangent space").
float3 ApplyNormalMap(float3 N, float4 tangent, float2 uv)
{
    float3 t = normalTexture.Sample(colorSampler, uv).xyz * 2.0f - 1.0f;   // 0..1 colour -> -1..1 direction
    if (surfaceFlags.x > 0.5f) t.y = -t.y;                                  // OpenGL-style map: green points the other way
    t.xy *= surfaceParams.x;                                                // strength: scale the sideways tilt
    t = normalize(t);

    // The vertex shader's tangent was interpolated across the triangle, so it is no longer exactly
    // perpendicular to the (also interpolated) normal. Re-square it up (Gram-Schmidt).
    float3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
    float3 B = cross(N, T) * tangent.w;                                     // the bitangent we did not have to store
    return normalize(T * t.x + B * t.y + N * t.z);
}

float4 main(PS_IN input) : SV_Target {
    const float3 vertexNormal = normalize(input.normal);
    float3 normal = vertexNormal;
    if (surfaceFlags.y > 0.5f) normal = ApplyNormalMap(vertexNormal, input.tangent, input.texCoord);

    // Debug views show a direction as a colour: -1..1 -> 0..1. (They skip lighting and the output
    // conversion stays linear, so the colours look a little dark; the point is the pattern.)
    const int debugView = (int)(surfaceFlags.z + 0.5f);
    if (debugView == 1) return float4(vertexNormal * 0.5f + 0.5f, 1.0f);
    if (debugView == 2) return float4(normal * 0.5f + 0.5f, 1.0f);
    if (debugView == 3) return float4(normalize(input.tangent.xyz) * 0.5f + 0.5f, 1.0f);

    const float3 orm = ormTexture.Sample(colorSampler, input.texCoord).rgb;

    PbrInput surface;
    surface.roughness = orm.g * surfaceParams.y;
    surface.metallic = saturate(orm.b * surfaceExtra.y);
    surface.occlusion = lerp(1.0f, orm.r, surfaceParams.z);   // occlusion darkens only the SKY light: a crevice still catches direct sun
    surface.normal = normal;
    surface.worldPos = input.worldPos;
    const float4 texColor = colorTexture.Sample(colorSampler, input.texCoord) * surfaceBaseColor;
    // Cut-out transparency: leaves, fences, chain-link. Pixels below the cutoff are thrown away entirely,
    // so they need no sorting (unlike blended transparency).
    if (surfaceExtra.x > 0.0f && texColor.a < surfaceExtra.x) discard;
    const float3 emissive = emissiveTexture.Sample(colorSampler, input.texCoord).rgb * surfaceParams.w;
    surface.albedo = texColor.rgb;
    return float4(ApplyFog(ShadePbr(surface) + emissive, input.worldPos), texColor.a);
}
