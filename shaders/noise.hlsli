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

// ---- Cellular noise (lesson 37): the bubbles of foam ----
// An integer hash (PCG): unlike sin()-based hashes it stays random far from the origin, where the water's coordinates live.
uint PcgHash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Two random numbers in 0..1 for one grid cell.
float2 CellHash(int2 cell)
{
    const uint h = PcgHash(asuint(cell.x) + PcgHash(asuint(cell.y)));
    return float2(h & 0xFFFFu, h >> 16u) * (1.0f / 65535.0f);
}

// Distance to the nearest (x) and the second nearest (y) of a set of random points, one per grid cell. Round bubbles are
// where x is small; the thin walls between bubbles are where y - x is small.
float2 Voronoi(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = p - cell;
    float nearest = 8.0f, second = 8.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float2 g = float2(x, y);
            const float2 d = g + CellHash((int2)cell + int2(x, y)) - f;
            const float dist = dot(d, d);
            if (dist < nearest) { second = nearest; nearest = dist; }
            else if (dist < second) { second = dist; }
        }
    }
    return sqrt(float2(nearest, second));
}

#endif
