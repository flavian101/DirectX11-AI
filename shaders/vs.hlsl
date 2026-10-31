// CPU matrices are row-major; transform normals with the world rotation.
struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
cbuffer Constants : register(b0) {
    row_major float4x4 world;
    row_major float4x4 view;
    row_major float4x4 proj;
};
VS_OUT main(VS_IN input) {
    VS_OUT output;
    float4 pos = mul(float4(input.position, 1.0f), world);
    pos = mul(pos, view);
    output.position = mul(pos, proj);
    output.normal = mul(input.normal, (float3x3)world);
    output.texCoord = input.texCoord;
    return output;
}