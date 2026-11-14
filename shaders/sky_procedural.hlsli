// A procedural sky: a gradient from the horizon to overhead, a warm glow at sunrise and sunset, the sun and moon as discs, and stars.
// Everything comes from SkyParams, which the CPU fills from the time of day (src/SkyModel.h, field for field the same layout).
// Used twice: skybox_ps.hlsl draws it as the background, at full resolution, and sky_procedural_cube_ps.hlsl renders it into a
// small cube map that the sky lighting (IBL) is baked from, so the light on the scene always matches the sky you see.
#ifndef SKY_PROCEDURAL_HLSLI
#define SKY_PROCEDURAL_HLSLI

cbuffer SkyParams : register(b6) {
    float4 skyZenith;      // rgb = the sky straight overhead
    float4 skyHorizon;     // rgb = the sky at the horizon
    float4 skyGround;      // rgb = what is below the horizon
    float4 skySunDir;      // xyz = unit direction toward the sun, w = cos(the sun's angular radius)
    float4 skySunColor;    // rgb = the sun disc's radiance, w = 1 when the disc is drawn
    float4 skyGlow;        // rgb = colour of the haze around the sun, w = its strength
    float4 skyMoonDir;     // xyz = unit direction toward the moon, w = cos(its angular radius)
    float4 skyMoonColor;   // rgb = the moon disc's radiance, w = 1 when the disc is drawn
    float4 skySunset;      // rgb = the warm colour of a sunset horizon, w = how much of it there is (0..1)
    float4 skyNight;       // x = star brightness (already scaled by how dark it is), y = how dark it is (0..1)
};

float SkyHash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Stars: the sky cut into small cells, a few of which hold a star at a jittered spot with its own brightness.
float3 SkyStars(float3 dir)
{
    const float3 p = dir * 110.0f;
    const float3 cell = floor(p);
    const float3 local = frac(p) - 0.5f;
    const float present = SkyHash(cell);
    if (present < 0.965f) return 0.0f;                                                   // most cells are empty
    const float3 jitter = float3(SkyHash(cell + 7.1f), SkyHash(cell + 13.7f), SkyHash(cell + 3.3f)) - 0.5f;
    const float distanceToStar = length(local - jitter * 0.55f);
    const float brightness = 0.4f + 1.6f * SkyHash(cell + 29.0f);                          // dim and bright ones
    const float3 tint = lerp(float3(0.8f, 0.88f, 1.0f), float3(1.0f, 0.9f, 0.75f), SkyHash(cell + 41.0f));
    return tint * brightness * (1.0f - smoothstep(0.0f, 0.2f, distanceToStar));   // (smoothstep with its edges swapped is undefined in HLSL)
}

// `drawDiscs` is false when this sky is being baked into lighting: the sun is already a light of its own, and a disc thousands of
// times brighter than the sky around it would only make the bake noisy.
float3 ProceduralSky(float3 dir, bool drawDiscs)
{
    const float h = dir.y;

    // The gradient: horizon colour rising to the overhead colour, and the ground colour below the horizon.
    float3 color = lerp(skyHorizon.rgb, skyZenith.rgb, pow(saturate(h), 0.45f));
    color = lerp(color, skyGround.rgb, smoothstep(0.0f, 0.2f, -h));

    // The sunrise / sunset glow: warm light low on the horizon, strongest on the side of the sun.
    const float2 sunSide = skySunDir.xz;
    const float sunSideLength = length(sunSide);
    const float2 here = dir.xz;
    const float hereLength = length(here);
    const float facing = (sunSideLength > 1e-4f && hereLength > 1e-4f) ? saturate(dot(sunSide / sunSideLength, here / hereLength)) : 0.0f;
    color += skySunset.rgb * (skySunset.w * 1.4f) * pow(facing, 2.5f) * exp(-abs(h) * 5.0f);

    // The haze around the sun: a wide soft glow and a tight bright core.
    const float toSun = saturate(dot(dir, skySunDir.xyz));
    color += skyGlow.rgb * skyGlow.w * (pow(toSun, 8.0f) * 0.35f + pow(toSun, 160.0f) * 1.6f);

    // Stars (only in the dark, only above the horizon) and the discs of the sun and moon are single points of light: they belong
    // to the background you see, not to the sky lighting that is baked from this function (they would only make it noisy).
    [branch]
    if (drawDiscs && skyNight.x > 0.001f) color += SkyStars(dir) * skyNight.x * smoothstep(0.0f, 0.15f, h);

    [branch]
    if (drawDiscs) {
        const float sunEdge = max((1.0f - skySunDir.w) * 0.35f, 1e-6f);
        const float sunDisc = smoothstep(skySunDir.w - sunEdge, skySunDir.w + sunEdge, dot(dir, skySunDir.xyz));
        color = lerp(color, skySunColor.rgb, sunDisc * skySunColor.w * smoothstep(-0.02f, 0.0f, h));

        const float moonEdge = max((1.0f - skyMoonDir.w) * 0.25f, 1e-6f);
        const float moonDisc = smoothstep(skyMoonDir.w - moonEdge, skyMoonDir.w + moonEdge, dot(dir, skyMoonDir.xyz));
        color = lerp(color, skyMoonColor.rgb, moonDisc * skyMoonColor.w * smoothstep(-0.02f, 0.0f, h));
    }
    return color;
}

#endif
