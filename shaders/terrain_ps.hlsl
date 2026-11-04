#include "lighting.hlsli"
#include "noise.hlsli"

Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

// The terrain's own constant block (b4). CPU side: TerrainMaterialCB in Terrain.cpp.
cbuffer TerrainMaterial : register(b4) {
    float4 layerParams;   // x = rock tiling (texture repeats per world unit), y = breakup scale, z = breakup amount, w = rock darkness
    float4 heightBands;   // x = shore height (world Y), y = shore width, z = snow height (world Y), w = snow fade
    float4 slopeParams;   // x = slope start, y = slope fade, z = debug (1 = show layer weights)
    float4 shoreTint;     // rgb = sand colour (linear)
    float4 snowTint;      // rgb = snow colour (linear)
};

struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

float Luma(float3 c) { return dot(c, float3(0.2126f, 0.7152f, 0.0722f)); }

// The mesh's UVs are a flat top-down projection, so on a steep cliff the texture smears
// vertically. Triplanar mapping projects the texture along all three world axes and blends the
// three results by how much the surface faces each axis.
float3 SampleTriplanar(float3 worldPos, float3 normal, float tiling)
{
    float3 w = pow(abs(normal), 4.0f);        // sharpen: the dominant axis wins
    w /= (w.x + w.y + w.z);
    const float3 fromX = colorTexture.Sample(colorSampler, worldPos.zy * tiling).rgb;   // surfaces facing +-X
    const float3 fromY = colorTexture.Sample(colorSampler, worldPos.xz * tiling).rgb;   // facing up/down
    const float3 fromZ = colorTexture.Sample(colorSampler, worldPos.xy * tiling).rgb;   // facing +-Z
    return fromX * w.x + fromY * w.y + fromZ * w.z;
}

float4 main(PS_IN input) : SV_Target
{
    const float3 normal = normalize(input.normal);
    const float rockTiling = layerParams.x;
    const float breakupScale = layerParams.y;
    const float breakupAmount = layerParams.z;
    const float rockDarkness = layerParams.w;

    // ---- The layers: all derived from the one texture we have ----
    const float4 groundSample = colorTexture.Sample(colorSampler, input.texCoord);
    const float3 ground = groundSample.rgb;
    const float groundLuma = Luma(ground);

    // Rock: triplanar, mostly desaturated and darker, so cliffs read as bare stone.
    const float3 rockRaw = SampleTriplanar(input.worldPos, normal, rockTiling);
    const float3 rock = lerp(Luma(rockRaw).xxx, rockRaw, 0.35f) * rockDarkness;
    // Shore sand and snow keep the ground texture's light and dark detail, but take the layer's colour.
    const float3 sand = shoreTint.rgb * (0.5f + 0.9f * groundLuma);
    const float3 snow = snowTint.rgb * (0.85f + 0.3f * groundLuma);

    // ---- Layer weights ----
    // Without breakup every layer edge would be a perfectly smooth contour line. Noise jitters
    // the height and slope we test against, so edges wander like natural ones.
    const float n = ValueNoise(input.worldPos.xz * breakupScale) - 0.5f;
    const float height = input.worldPos.y + n * breakupAmount * 8.0f;                // world units
    const float slope = (1.0f - normal.y) + n * breakupAmount * 0.4f;                // 0 flat .. 1 vertical

    const float rockW = smoothstep(slopeParams.x, slopeParams.x + slopeParams.y, slope);
    const float snowW = smoothstep(heightBands.z, heightBands.z + heightBands.w, height) * (1.0f - rockW);   // snow does not stick to cliffs
    const float shoreW = (1.0f - smoothstep(heightBands.x, heightBands.x + heightBands.y, height)) * (1.0f - rockW * 0.7f);

    float3 albedo = ground;
    albedo = lerp(albedo, sand, shoreW);
    albedo = lerp(albedo, rock, rockW);
    albedo = lerp(albedo, snow, snowW);

    // Debug view: red = rock, green = snow, blue = shore sand. Great for tuning the thresholds.
    if (slopeParams.z > 0.5f) return float4(rockW, snowW, shoreW, 1.0f);

    float3 diffuse, specular;
    ComputeLighting(input.worldPos, normal, diffuse, specular);

    // Snow is glossier, rock is matte.
    const float specularAmount = lerp(lerp(0.5f, 0.1f, rockW), 0.9f, snowW);
    return float4(albedo * diffuse + specular * specularAmount, groundSample.a);
}
