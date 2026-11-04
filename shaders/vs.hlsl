#include "common.hlsli"

struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float4 tangent : TANGENT; };
struct VS_OUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float4 tangent : TEXCOORD2;   // xyz = world-space tangent, w = handedness (the bitangent is rebuilt per pixel)
    float clip : SV_ClipDistance0;
};
VS_OUT main(VS_IN input) {
    VS_OUT output;
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4 pos = mul(worldPos, view);
    output.position = mul(pos, proj);
    output.normal = mul(input.normal, (float3x3)world);
    // The tangent is a DIRECTION along the surface, so it rotates and scales with the object like the normal does.
    output.tangent = float4(mul(input.tangent.xyz, (float3x3)world), input.tangent.w);
    output.texCoord = input.texCoord * materialParams.x;   // x = UV tiling (1 = texture covers the mesh once)
    output.worldPos = worldPos.xyz;
    // A clip plane: the rasterizer throws away every pixel where this is negative. It is how the
    // reflection pass hides everything below the water. With clipPlane = (0,0,0,1) this is always 1.
    output.clip = dot(worldPos, clipPlane);
    return output;
}
