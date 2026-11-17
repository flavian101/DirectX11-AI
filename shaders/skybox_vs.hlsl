#include "common.hlsli"
struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 direction : TEXCOORD0; };
VS_OUT main(VS_IN input) {
    VS_OUT output;
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4x4 viewNoTranslation = view;
    viewNoTranslation[3] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float4 clipPosition = mul(mul(worldPos, viewNoTranslation), proj);
    // Reversed-Z: the far plane is depth 0, so the sky is pinned there with z = 0 (it used to be z = w, depth 1).
    output.position = float4(clipPosition.xy, 0.0f, clipPosition.w);
    output.direction = input.position;
    return output;
}
