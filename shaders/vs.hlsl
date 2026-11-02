#include "common.hlsli"

struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };
VS_OUT main(VS_IN input) {
    VS_OUT output;
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4 pos = mul(worldPos, view);
    output.position = mul(pos, proj);
    output.normal = mul(input.normal, (float3x3)world);
    output.texCoord = input.texCoord * materialParams.x;   // x = UV tiling (1 = texture covers the mesh once)
    output.worldPos = worldPos.xyz;
    return output;
}
