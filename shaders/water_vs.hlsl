#include "waves.hlsli"

struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

VS_OUT main(VS_IN input)
{
    VS_OUT output;

    // World space FIRST: waves are measured in world units, so resizing the plane changes how
    // much water you see, not how big the waves are. (Assumes a horizontal plane.)
    float3 worldPos = mul(float4(input.position, 1.0f), world).xyz;

    float height;
    float2 slope;   // not needed here - the pixel shader recomputes it per pixel
    EvaluateWaves(worldPos.xz, height, slope);
    worldPos.y += height;

    output.position = mul(mul(float4(worldPos, 1.0f), view), proj);
    output.texCoord = input.texCoord;
    output.worldPos = worldPos;
    return output;
}
