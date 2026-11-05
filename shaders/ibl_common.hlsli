// Shared by the three "bake" shaders that turn the sky into image-based lighting (lesson 19).
#ifndef IBL_COMMON_HLSLI
#define IBL_COMMON_HLSLI

static const float kPi = 3.14159265359f;

// b5: this bake draw's parameters (b0..b4 belong to the scene, so the bake never disturbs them).
cbuffer BakeParams : register(b5) {
    float4 bakeParams;   // x = which cube face is being drawn (0..5), y = roughness, z = source cube face size in texels
};

// The world direction that a pixel of a cube-map face stands for. uv runs 0..1 across the face with
// y pointing DOWN (like texture coordinates). These six formulas are exactly the inverse of how the
// GPU picks a face and texel when you sample a TextureCube with a direction.
float3 CubeFaceDirection(uint face, float2 uv)
{
    const float2 c = uv * 2.0f - 1.0f;
    float3 d;
    if (face == 0)      d = float3( 1.0f, -c.y, -c.x);   // +X
    else if (face == 1) d = float3(-1.0f, -c.y,  c.x);   // -X
    else if (face == 2) d = float3( c.x,  1.0f,  c.y);   // +Y
    else if (face == 3) d = float3( c.x, -1.0f, -c.y);   // -Y
    else if (face == 4) d = float3( c.x, -c.y,  1.0f);   // +Z
    else                d = float3(-c.x, -c.y, -1.0f);   // -Z
    return normalize(d);
}

// Van der Corput radical inverse: mirrors the bits of i around the "decimal" point, giving a number in
// [0,1) that keeps landing in the biggest gap. Pair it with i/n and you get Hammersley points: sample
// positions that cover the square evenly without the clumps of random numbers.
float RadicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;   // / 2^32
}

float2 Hammersley(uint i, uint count)
{
    return float2(float(i) / float(count), RadicalInverse(i));
}

// Picks a "half vector" H around N with the probability density D(h) * (N.h) of the GGX distribution:
// rough surfaces spread H widely, smooth ones keep it near N.
float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness)
{
    const float a = roughness * roughness;
    const float phi = 2.0f * kPi * xi.x;
    const float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    const float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);   // around +Z

    const float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(up, N));
    const float3 bitangent = cross(N, tangent);
    return normalize(tangent * h.x + bitangent * h.y + N * h.z);                    // rotate from "around +Z" to "around N"
}

float DistributionGGX(float NdotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

#endif
