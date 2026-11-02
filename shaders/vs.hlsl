// Matrices are now transposed on CPU side for column-major layout
struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };
cbuffer Constants : register(b0) {
    float4x4 world;
    float4x4 view;
    float4x4 proj;
};
VS_OUT main(VS_IN input) {
    VS_OUT output;
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4 pos = mul(worldPos, view);
    output.position = mul(pos, proj);
    output.normal = mul(input.normal, (float3x3)world);
    output.texCoord = input.texCoord;
    output.worldPos = worldPos.xyz;
    return output;
}
