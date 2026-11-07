// Cheap procedural noise (no texture needed): a hashed grid, smoothly interpolated.
// Used to break up straight edges: the shoreline foam and the terrain layer boundaries.
#ifndef NOISE_HLSLI
#define NOISE_HLSLI

float Hash(float2 p) { return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f); }

float ValueNoise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);   // smooth S-curve so the grid cells blend without visible seams
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), f.x),
                lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
}

// The same noise PLUS its slope, computed exactly (no extra samples): x = value, yz = d value / d p.
// The water uses the slope as a ripple normal. Quintic curve, so the slope itself is continuous (no grid lines in the lighting).
float3 ValueNoiseD(float2 p)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u  = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float2 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);
    const float a = Hash(i);
    const float b = Hash(i + float2(1, 0));
    const float c = Hash(i + float2(0, 1));
    const float d = Hash(i + float2(1, 1));
    const float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
    return float3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y,
                  du * float2(k1 + k4 * u.y, k2 + k4 * u.x));
}

#endif
