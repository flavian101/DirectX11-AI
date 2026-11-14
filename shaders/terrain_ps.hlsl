#include "lighting.hlsli"

// Terrain pixel shader (lesson 29). Compare with lesson 17: the LAYER WEIGHTS are no longer worked out here.
//   * "How much rock is here?" is baked once from the heightfield into `splatMap` (src/TerrainSplat.cpp): one texture read.
//   * Layers are packed two arrays: albedo + height, and normal + occlusion + roughness: two reads per layer, not three.
//   * A layer with no weight at this pixel is not read at all (a real branch, so SampleGrad with explicit derivatives).
//   * Rock is BIplanar (two projections, not three): 4 reads instead of 9. A flat meadow pixel reads only grass: 2 reads.

Texture2DArray layerDiffHeight : register(t8);  // slice 0 grass, 1 rock, 2 sand, 3 snow. rgb = albedo (sRGB: arrives as linear light), a = height
Texture2DArray layerNormRough : register(t9);   // same slices. r,g = tangent-space normal XY (z is rebuilt), b = ambient occlusion, a = roughness
Texture2D<float4> splatMap : register(t16);     // r,g,b,a = weight of grass, rock, sand, snow; baked from the heightfield; add up to 1
SamplerState colorSampler : register(s0);       // trilinear + anisotropic, wraps. (The splat map uses iblSampler: linear, clamped.)

static const int kGrass = 0;
static const int kRock = 1;
static const int kSand = 2;
static const int kSnow = 3;

// A layer lighter than one step of the 8-bit splat map is not worth reading its textures for.
static const float kMinWeight = 1.0f / 255.0f;

// The terrain's own constant block (b4). CPU side: TerrainMaterialCB in Terrain.cpp.
cbuffer TerrainMaterial : register(b4) {
    float4 layerParams;   // x = rock tiling (texture repeats per world unit), y = rock brightness
    float4 blendParams;   // x = height blend (0..1), y = height-blend transition width, z = debug (1 = show layer weights)
    float4 splatXform;    // xy = world X,Z of the terrain's low corner, zw = 1 / the terrain's world size: world XZ -> splat UV
    float4 tilings;       // x = ground tiling, y = sand tiling, z = snow tiling
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
    float height;          // 0..1
    float3 tangentNormal;  // decoded, flipped if needed, strength applied. NOT normalised (blended first, normalised once at the end)
    float occlusion;
    float roughness;
};

struct RockSample {
    LayerSample layer;     // albedo / height / occlusion / roughness already blended over the two projections
    float3 worldNormal;    // the two projections' world normals, blended (not normalised)
};

// SampleGrad with explicit derivatives: unlike Sample, it is allowed inside an `if` that different pixels of a quad
// take differently, which is what lets us skip layers.
LayerSample SampleLayer(int slice, float2 uv, float2 ddxUV, float2 ddyUV)
{
    const float3 coord = float3(uv, slice);
    const float4 a = layerDiffHeight.SampleGrad(colorSampler, coord, ddxUV, ddyUV);
    const float4 n = layerNormRough.SampleGrad(colorSampler, coord, ddxUV, ddyUV);

    LayerSample s;
    s.albedo = a.rgb;
    s.height = a.a;
    float2 xy = n.xy * 2.0f - 1.0f;
    if (surfaceParams.y > 0.5f) xy.y = -xy.y;                 // OpenGL-style map: green points the other way
    const float z = sqrt(saturate(1.0f - dot(xy, xy)));       // rebuilt from the two stored channels (a unit normal)
    xy *= surfaceParams.x;                                    // strength: tilt more or less, keep z
    s.tangentNormal = float3(xy, z);
    s.occlusion = n.z;
    s.roughness = n.w;
    return s;
}

float3 AxisVector(int i) { return float3((float)(i == 0), (float)(i == 1), (float)(i == 2)); }

// "Whiteout" reorientation of a tangent-space normal t for a texture projected ALONG world axis `a`, whose U grows along
// world axis `u` and V along `v`. No stored tangents and no per-pixel frame maths (the old code ran Flatten + rsqrt for
// every layer): the tangent-space tilt is simply added to the surface normal's components on the two axes the texture lies on.
float3 ReorientNormal(float3 t, float3 N, float3 a, float3 u, float3 v)
{
    return u * (t.x + dot(N, u)) + v * (t.y + dot(N, v)) + a * (abs(t.z) * dot(N, a));
}

// Biplanar mapping (Inigo Quilez, iquilezles.org/articles/biplanar): triplanar needs three projections, but at any point
// the third is the one facing AWAY the most and contributes the least. Pick the two that face the surface best and blend those.
// The first picks the major axis, the second the median; each axis comes with the two that follow it in cyclic order,
// which is what the texture's U and V run along.
RockSample SampleRock(float3 p, float3 N, float3 dPdx, float3 dPdy)
{
    const float3 an = abs(N);
    const int3 ma = (an.x > an.y && an.x > an.z) ? int3(0, 1, 2) : (an.y > an.z) ? int3(1, 2, 0) : int3(2, 0, 1);   // major
    const int3 mi = (an.x < an.y && an.x < an.z) ? int3(0, 1, 2) : (an.y < an.z) ? int3(1, 2, 0) : int3(2, 0, 1);   // minor
    const int3 me = int3(3, 3, 3) - mi - ma;                                                                          // median

    const float tile = layerParams.x;
    const LayerSample a = SampleLayer(kRock, float2(p[ma.y], p[ma.z]) * tile,
                                      float2(dPdx[ma.y], dPdx[ma.z]) * tile, float2(dPdy[ma.y], dPdy[ma.z]) * tile);
    const LayerSample b = SampleLayer(kRock, float2(p[me.y], p[me.z]) * tile,
                                      float2(dPdx[me.y], dPdx[me.z]) * tile, float2(dPdy[me.y], dPdy[me.z]) * tile);

    // Each projection counts only where it faces the surface well (1/sqrt(3) is the least the major axis can be).
    float2 bw = float2(an[ma.x], an[me.x]);
    bw = max(saturate((bw - 0.5773f) / (1.0f - 0.5773f)), 0.001f);
    bw /= (bw.x + bw.y);

    RockSample r;
    r.layer.albedo = a.albedo * bw.x + b.albedo * bw.y;
    r.layer.height = a.height * bw.x + b.height * bw.y;
    r.layer.tangentNormal = float3(0, 0, 1);   // unused: the world normal is built per projection below
    r.layer.occlusion = a.occlusion * bw.x + b.occlusion * bw.y;
    r.layer.roughness = a.roughness * bw.x + b.roughness * bw.y;
    r.worldNormal = ReorientNormal(a.tangentNormal, N, AxisVector(ma.x), AxisVector(ma.y), AxisVector(ma.z)) * bw.x
                  + ReorientNormal(b.tangentNormal, N, AxisVector(me.x), AxisVector(me.y), AxisVector(me.z)) * bw.y;
    return r;
}

float4 main(PS_IN input) : SV_Target
{
    const float3 N = normalize(input.normal);
    const float3 p = input.worldPos;

    // ---- 1. How much of each layer is here? ONE read of the baked weights (explicit level: no derivatives needed). ----
    float4 w = splatMap.SampleLevel(iblSampler, (p.xz - splatXform.xy) * splatXform.zw, 0);

    // Debug view: red = rock, green = snow, blue = shore sand.
    if (blendParams.z > 0.5f) return float4(w.y, w.w, w.z, 1.0f);

    w *= step(kMinWeight, w);                       // drop layers too light to matter...
    w /= max(dot(w, float4(1, 1, 1, 1)), 1e-4f);    // ...and give their share to the others (the four add up to 1 again)

    // Derivatives of the world position, ONCE and outside every branch: every layer's own derivatives are these times its tiling.
    const float3 dPdx = ddx(p);
    const float3 dPdy = ddy(p);

    // ---- 2. Read ONLY the layers that are present. Neighbouring pixels have nearly the same weights, so whole groups of pixels take the same branch. ----
    // Grass, sand and snow lie mostly on flat ground: ONE projection from above (uv = world x,z).
    LayerSample grass = (LayerSample)0;
    LayerSample sand = (LayerSample)0;
    LayerSample snow = (LayerSample)0;
    RockSample rock = (RockSample)0;
    [branch] if (w.x > 0.0f) grass = SampleLayer(kGrass, p.xz * tilings.x, dPdx.xz * tilings.x, dPdy.xz * tilings.x);
    [branch] if (w.z > 0.0f) sand = SampleLayer(kSand, p.xz * tilings.y, dPdx.xz * tilings.y, dPdy.xz * tilings.y);
    [branch] if (w.w > 0.0f) snow = SampleLayer(kSnow, p.xz * tilings.z, dPdx.xz * tilings.z, dPdy.xz * tilings.z);
    // Rock lives on cliffs, where a top-down projection smears: biplanar.
    [branch] if (w.y > 0.0f) rock = SampleRock(p, N, dPdx, dPdy);

    // ---- 3. Height-based blending ----
    // A plain blend fades two textures into each other like a double exposure. Instead let the TALLER layer win where
    // two meet (pebbles poke through sand). This is the form Unity's terrain uses: weight each layer's height by its
    // splat weight, find the highest, and keep only layers within `transition` of it. Multiplying (not adding, as the old
    // code did) means a layer with NO weight has no height, so it can never pop in: no hack needed to stop that.
    const float4 heights = float4(grass.height, rock.layer.height, sand.height, snow.height);
    const float4 weighted = heights * w;
    const float top = max(max(weighted.x, weighted.y), max(weighted.z, weighted.w));
    const float transition = max(blendParams.y, 1e-4f);
    float4 sharpened = (max(weighted + transition - top, 0.0f) + 1e-5f) * w;
    sharpened /= dot(sharpened, float4(1, 1, 1, 1));
    w = lerp(w, sharpened, blendParams.x);

    // ---- 4. Colour: layers x tints ----
    const float3 albedo = grass.albedo * groundTint.rgb * w.x
                  + rock.layer.albedo * rockTint.rgb * layerParams.y * w.y
                  + sand.albedo * shoreTint.rgb * w.z
                  + snow.albedo * snowTint.rgb * w.w;

    // ---- 5. Normal ----
    // The three planar layers share one frame (looking down), so blend their TANGENT normals first and tilt into world
    // space ONCE (top-down: texture U = world X, V = world Z). Rock already has its world normal.
    const float planarWeight = w.x + w.z + w.w;
    const float3 planarTangent = grass.tangentNormal * w.x + sand.tangentNormal * w.z + snow.tangentNormal * w.w;
    const float3 planarWorld = float3(planarTangent.x + N.x * planarWeight, abs(planarTangent.z) * N.y, planarTangent.y + N.z * planarWeight);
    const float3 normal = normalize(planarWorld + rock.worldNormal * w.y);

    // ---- 6. Occlusion and roughness blend like colour does ----
    const float occlusion = lerp(1.0f, grass.occlusion * w.x + rock.layer.occlusion * w.y + sand.occlusion * w.z + snow.occlusion * w.w, surfaceParams.z);
    const float roughness = clamp((grass.roughness * w.x + rock.layer.roughness * w.y + sand.roughness * w.z + snow.roughness * w.w) * surfaceParams.w, 0.05f, 1.0f);

    PbrInput surface;
    surface.albedo = albedo;
    surface.metallic = 0.0f;      // ground is not metal
    surface.roughness = roughness;
    surface.occlusion = occlusion;
    surface.normal = normal;
    surface.worldPos = p;
    return float4(ApplyFog(ShadePbr(surface), p), 1.0f);
}
