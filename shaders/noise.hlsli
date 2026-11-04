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

#endif
