#include "lighting.hlsli"

Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

float4 main(PS_IN input) : SV_Target {
    float3 normal = normalize(input.normal);

    float3 diffuse, specular;
    ComputeLighting(input.worldPos, normal, diffuse, specular);

    float4 texColor = colorTexture.Sample(colorSampler, input.texCoord);
    float3 litColor = texColor.rgb * diffuse + specular * 0.5f;
    return float4(litColor, texColor.a);
}
