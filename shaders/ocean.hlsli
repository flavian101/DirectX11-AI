// The sea the water draws: three FFT CASCADES made by compute shaders each frame (src/OceanGpu.h; the CPU copy boats read is src/OceanSea.h).
// A cascade is a patch of sea that repeats; the three have different sizes (2000 m, 250 m, 30 m by default: swell, wind sea, ripples), so the sum
// does not repeat visibly. Two maps per cascade, both RGBA16F with a mip chain, sampled at  world position / patch size  (wrapping):
//   displacement map (t17..t19)   (choppiness * displacement x, height, choppiness * displacement z, 0)    the VERTEX shader moves a vertex by this
//   slope map        (t20..t22)   (slope x, slope z, Jacobian, 0)                                          the PIXEL shader: the normal, and the folding
// The surface is parameterised by REST position: a vertex at rest position p is drawn at  p + displacement(p),  and its normal and Jacobian are the
// maps' values at p, which stay exact however far the point was pushed sideways.
//
// Near a shore the surface is limited to a fraction of the water depth (ShoreLimit): an interim stand-in for the shore waves of roadmap O6.
#ifndef OCEAN_HLSLI
#define OCEAN_HLSLI
#include "common.hlsli"
#include "water_material.hlsli"

static const float kShoreBreakRatio = 0.4f;   // the surface may stand at most this fraction of the depth above the level: OceanSea::kShoreBreakRatio

Texture2D<float4> oceanDisplacement0 : register(t17);
Texture2D<float4> oceanDisplacement1 : register(t18);
Texture2D<float4> oceanDisplacement2 : register(t19);
Texture2D<float4> oceanSlope0 : register(t20);
Texture2D<float4> oceanSlope1 : register(t21);
Texture2D<float4> oceanSlope2 : register(t22);
SamplerState oceanSampler : register(s3);   // trilinear + anisotropic, WRAP: the patches repeat (bound by Water::Render for both stages)

// The factor that keeps a wave of this height under the depth: 1 in deep water, 0 at the waterline. OceanSea::ShoreFactor is the CPU twin.
float ShoreLimit(float height, float depth)
{
    const float limit = kShoreBreakRatio * depth;
    if (limit <= 1e-4f) return 0.0f;
    const float x = abs(height) / limit;
    const float x2 = x * x;
    return rsqrt(sqrt(1.0f + x2 * x2));   // (1 + x^4)^(-1/4)
}

// One cascade's displacement at a rest position, from the mip whose texels are about as big as the grid cells here: a coarser grid cannot hold a
// finer wave, so it reads the averaged map instead (that is what keeps the far sea from crawling). The vertex shader has no neighbours to guess a
// mip from, so it names one: SampleLevel.
float3 OceanBandDisplacement(Texture2D<float4> map, float patch, float2 restXZ, float spacing)
{
    uint width, height, levels;
    map.GetDimensions(0, width, height, levels);
    const float texel = patch / (float)width;                                      // metres per texel at mip 0
    const float lod = clamp(log2(max(spacing / texel, 1.0f)), 0.0f, (float)(levels - 1));
    return map.SampleLevel(oceanSampler, restXZ / patch, lod).xyz;
}

// The displacement of the whole sea at a rest position. `spacing` = the grid cell size here (metres), `depth` = the water depth under the point
// (1000 when the body has no depth map).
float3 OceanDisplacement(float2 restXZ, float spacing, float depth)
{
    const float3 d = OceanBandDisplacement(oceanDisplacement0, waterSeaPatch.x, restXZ, spacing)
                   + OceanBandDisplacement(oceanDisplacement1, waterSeaPatch.y, restXZ, spacing)
                   + OceanBandDisplacement(oceanDisplacement2, waterSeaPatch.z, restXZ, spacing);
    return d * ShoreLimit(d.y, depth);
}

// The shading of the sea at a rest position, from the slope maps: the surface normal and the Jacobian (1 flat, below 1 squeezed, below 0 folded).
// `shoreScale` = the vertex shader's ShoreLimit factor here, which flattens the normal in step with the flattened surface. The pixel shader's
// `Sample` picks the mip from how fast the position changes per pixel, so waves smaller than a pixel average out by themselves.
void OceanShading(float2 restXZ, float shoreScale, out float3 N, out float J)
{
    const float3 a = oceanSlope0.Sample(oceanSampler, restXZ / waterSeaPatch.x).xyz;
    const float3 b = oceanSlope1.Sample(oceanSampler, restXZ / waterSeaPatch.y).xyz;
    const float3 c = oceanSlope2.Sample(oceanSampler, restXZ / waterSeaPatch.z).xyz;
    const float2 slope = (a.xy + b.xy + c.xy) * shoreScale;
    N = normalize(float3(-slope.x, 1.0f, -slope.y));
    J = 1.0f + (a.z - 1.0f) + (b.z - 1.0f) + (c.z - 1.0f);   // the folds of small waves add to the folds of big ones
}
#endif
