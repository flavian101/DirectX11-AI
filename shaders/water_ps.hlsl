#include "common.hlsli"
struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };
float4 main(PS_IN input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(-dirDirection.xyz);
    float lighting = 0.35f + saturate(dot(normal, lightDirection)) * dirDirection.w * 0.65f;
    float3 color = materialColor.rgb * (ambientColor.rgb + lighting * dirColor.rgb);
    return float4(saturate(color), materialColor.a);
}
