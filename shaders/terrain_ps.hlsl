#include "lighting.hlsli"
#include "noise.hlsli"

Texture2D colorTexture : register(t0);          // the terrain-wide colour map ("macro" colour), mapped once over the whole terrain
Texture2DArray layerAlbedo : register(t8);      // slice 0 grass, 1 rock, 2 sand, 3 snow (sRGB: arrives as linear light)
Texture2DArray layerNormal : register(t9);      // rgb = tangent-space normal, a = height
Texture2DArray layerOrm : register(t10);        // r = ambient occlusion, g = roughness, b = metallic (unused until IBL)
SamplerState colorSampler : register(s0);

static const int kGrass = 0;
static const int kRock = 1;
static const int kSand = 2;
static const int kSnow = 3;

// The terrain's own constant block (b4). CPU side: TerrainMaterialCB in Terrain.cpp.
cbuffer TerrainMaterial : register(b4) {
    float4 layerParams;   // x = rock tiling (texture repeats per world unit), y = breakup scale, z = breakup amount, w = rock brightness
    float4 heightBands;   // x = shore height (world Y), y = shore width, z = snow height (world Y), w = snow fade
    float4 slopeParams;   // x = slope start, y = slope fade, z = debug (1 = show layer weights), w = height blend (0..1)
    float4 tilings;       // x = ground tiling, y = sand tiling, z = snow tiling, w = macro colour influence
    float4 surfaceParams; // x = normal strength, y = flip green (1/0), z = AO strength, w = roughness scale
    float4 groundTint;    // rgb = linear; tints MULTIPLY the layer's texture
    float4 rockTint;
    float4 shoreTint;
    float4 snowTint;
};

struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

// Everything one layer has to say about one pixel.
struct LayerSample {
    float3 albedo;
    float3 tangentNormal;   // already decoded, flipped if needed, strength applied, normalised
    float height;           // 0..1, from the normal map's alpha
    float occlusion;
    float roughness;
};

LayerSample SampleLayer(int slice, float2 uv)
{
    const float3 coord = float3(uv, slice);                  // x,y = where in the image, z = which slice
    LayerSample s;
    s.albedo = layerAlbedo.Sample(colorSampler, coord).rgb;
    const float4 n = layerNormal.Sample(colorSampler, coord);
    const float3 orm = layerOrm.Sample(colorSampler, coord).rgb;

    float3 t = n.xyz * 2.0f - 1.0f;
    if (surfaceParams.y > 0.5f) t.y = -t.y;                   // OpenGL-style map: green points the other way
    t.xy *= surfaceParams.x;
    s.tangentNormal = normalize(t);
    s.height = n.a;
    s.occlusion = orm.r;
    s.roughness = orm.g;
    return s;
}

// The world-space direction along which a texture coordinate grows, flattened onto the surface
// (so it is perpendicular to the normal N). For a texture projected straight down, U grows along
// +X and V along +Z; on a slope those directions TILT with the ground, which is exactly why the
// terrain's tangent is not a constant (1,0,0). rsqrt(max(..)) keeps it finite when the axis is
// almost parallel to N (that projection then has weight ~0 anyway).
float3 Flatten(float3 axis, float3 N)
{
    const float3 v = axis - N * dot(N, axis);
    return v * rsqrt(max(dot(v, v), 1e-8f));
}

// Turns a tangent-space normal into a world-space one for a projection whose U / V run along
// the world axes axisU / axisV. No stored tangents are needed: the frame is built from the projection.
float3 ProjectedNormal(float3 tangentNormal, float3 N, float3 axisU, float3 axisV)
{
    return Flatten(axisU, N) * tangentNormal.x + Flatten(axisV, N) * tangentNormal.y + N * tangentNormal.z;
}

float4 main(PS_IN input) : SV_Target
{
    const float3 N = normalize(input.normal);
    const float3 p = input.worldPos;

    // ---- 1. How much of each layer is here? (the rules from lesson 12, unchanged) ----
    const float n = ValueNoise(p.xz * layerParams.y) - 0.5f;
    const float height = p.y + n * layerParams.z * 8.0f;
    const float slope = (1.0f - N.y) + n * layerParams.z * 0.4f;

    const float rockW = smoothstep(slopeParams.x, slopeParams.x + slopeParams.y, slope);
    const float snowW = smoothstep(heightBands.z, heightBands.z + heightBands.w, height) * (1.0f - rockW);
    const float shoreW = (1.0f - smoothstep(heightBands.x, heightBands.x + heightBands.y, height)) * (1.0f - rockW * 0.7f);

    // Debug view: red = rock, green = snow, blue = shore sand.
    if (slopeParams.z > 0.5f) return float4(rockW, snowW, shoreW, 1.0f);

    // The old code stacked lerps (ground -> sand -> rock -> snow). Expanding that stack gives one weight
    // per layer, and the four weights always add up to 1:
    float4 w = float4((1.0f - shoreW) * (1.0f - rockW) * (1.0f - snowW),   // grass
                      rockW * (1.0f - snowW),                               // rock
                      shoreW * (1.0f - rockW) * (1.0f - snowW),             // sand
                      snowW);                                               // snow

    // ---- 2. Read the layers ----
    // Grass, sand and snow lie mostly on flat ground: ONE projection from above (uv = world x,z).
    const LayerSample grass = SampleLayer(kGrass, p.xz * tilings.x);
    const LayerSample sand = SampleLayer(kSand, p.xz * tilings.y);
    const LayerSample snow = SampleLayer(kSnow, p.xz * tilings.z);

    // Rock lives on cliffs, where a top-down projection smears: use three projections and blend by
    // how much the surface faces each axis (triplanar).
    float3 triWeight = pow(abs(N), 4.0f);
    triWeight /= (triWeight.x + triWeight.y + triWeight.z);
    const LayerSample rockX = SampleLayer(kRock, p.zy * layerParams.x);    // faces +-X: U = world z, V = world y
    const LayerSample rockY = SampleLayer(kRock, p.xz * layerParams.x);    // faces up/down
    const LayerSample rockZ = SampleLayer(kRock, p.xy * layerParams.x);    // faces +-Z: U = world x, V = world y

    LayerSample rock;
    rock.albedo = rockX.albedo * triWeight.x + rockY.albedo * triWeight.y + rockZ.albedo * triWeight.z;
    rock.height = rockX.height * triWeight.x + rockY.height * triWeight.y + rockZ.height * triWeight.z;
    rock.occlusion = rockX.occlusion * triWeight.x + rockY.occlusion * triWeight.y + rockZ.occlusion * triWeight.z;
    rock.roughness = rockX.roughness * triWeight.x + rockY.roughness * triWeight.y + rockZ.roughness * triWeight.z;
    rock.tangentNormal = float3(0, 0, 1);   // unused: rock's world normal is built per projection below

    // ---- 3. Height-based blending ----
    // A plain lerp fades two textures into each other like a double exposure. Instead, let the TALLER
    // layer win: add each layer's height to its weight, find the leader, and keep only layers close
    // behind it. Pebbles then poke through sand rather than dissolving into it.
    const float4 heights = float4(grass.height, rock.height, sand.height, snow.height);
    const float4 score = w + heights * 0.5f;
    const float top = max(max(score.x, score.y), max(score.z, score.w));
    float4 sharpened = max(score - (top - 0.2f), 0.0f) * saturate(w * 10.0f);   // a layer with ~no weight must not pop in
    sharpened /= max(sharpened.x + sharpened.y + sharpened.z + sharpened.w, 1e-4f);
    w = lerp(w, sharpened, slopeParams.w);

    // ---- 4. Colour: layers x tints x the terrain-wide colour map ----
    const float3 albedoMix = grass.albedo * groundTint.rgb * w.x
                           + rock.albedo * rockTint.rgb * layerParams.w * w.y
                           + sand.albedo * shoreTint.rgb * w.z
                           + snow.albedo * snowTint.rgb * w.w;

    // The colour map keeps the big, hand-made colour variation across the whole terrain. Dividing by
    // its average makes it a gentle modulation around 1 instead of darkening everything. The 1x1
    // smallest mip level IS the average colour (SampleLevel with a huge level clamps to it).
    const float4 macroSample = colorTexture.Sample(colorSampler, input.texCoord);
    const float3 macroAverage = max(colorTexture.SampleLevel(colorSampler, float2(0.5f, 0.5f), 12.0f).rgb, 0.02f);
    const float3 macroVariation = clamp(macroSample.rgb / macroAverage, 0.25f, 4.0f);
    const float macroAmount = tilings.w * (1.0f - w.w);     // snow keeps its own colour
    const float3 albedo = albedoMix * lerp(float3(1.0f, 1.0f, 1.0f), macroVariation, macroAmount);

    // ---- 5. Normal: tilt each layer's normal into world space, then blend by the same weights ----
    const float3 axisX = float3(1, 0, 0), axisY = float3(0, 1, 0), axisZ = float3(0, 0, 1);
    const float3 nGrass = ProjectedNormal(grass.tangentNormal, N, axisX, axisZ);
    const float3 nSand = ProjectedNormal(sand.tangentNormal, N, axisX, axisZ);
    const float3 nSnow = ProjectedNormal(snow.tangentNormal, N, axisX, axisZ);
    const float3 nRock = ProjectedNormal(rockX.tangentNormal, N, axisZ, axisY) * triWeight.x
                       + ProjectedNormal(rockY.tangentNormal, N, axisX, axisZ) * triWeight.y
                       + ProjectedNormal(rockZ.tangentNormal, N, axisX, axisY) * triWeight.z;
    const float3 normal = normalize(nGrass * w.x + nRock * w.y + nSand * w.z + nSnow * w.w);

    // ---- 6. Occlusion and roughness blend like colour does ----
    const float occlusion = lerp(1.0f, grass.occlusion * w.x + rock.occlusion * w.y + sand.occlusion * w.z + snow.occlusion * w.w, surfaceParams.z);
    const float roughness = clamp((grass.roughness * w.x + rock.roughness * w.y + sand.roughness * w.z + snow.roughness * w.w) * surfaceParams.w, 0.05f, 1.0f);

    float3 ambient, direct, specular;
    ComputeLightingEx(p, normal, RoughnessToShininess(roughness), ambient, direct, specular);
    return float4(albedo * (ambient * occlusion + direct) + specular * 0.5f * (1.0f - roughness), macroSample.a);
}
