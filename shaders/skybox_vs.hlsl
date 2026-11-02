struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 direction : TEXCOORD0; };
cbuffer Constants : register(b0) {
    float4x4 world;
    float4x4 view;
    float4x4 proj;
    float4 materialColor;
    float time;
    float3 materialParameters;
};
VS_OUT main(VS_IN input) {
    VS_OUT output;
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4x4 viewNoTranslation = view;
    viewNoTranslation[3] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float4 clipPosition = mul(mul(worldPos, viewNoTranslation), proj);
    output.position = clipPosition.xyww;
    output.direction = input.position;
    return output;
}
