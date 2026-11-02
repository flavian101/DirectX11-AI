struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };
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
    float amplitude = materialParameters.x;
    float speed = materialParameters.y;
    float wave = sin(input.position.x * 2.4f + time * speed) * amplitude;
    wave += cos(input.position.z * 1.8f + time * speed * 0.8f) * amplitude * 0.65f;
    float3 position = input.position + float3(0.0f, wave, 0.0f);
    float4 worldPos = mul(float4(position, 1.0f), world);
    output.position = mul(mul(worldPos, view), proj);
    output.normal = mul(float3(0.0f, 1.0f, 0.0f), (float3x3)world);
    output.texCoord = input.texCoord;
    output.worldPos = worldPos.xyz;
    return output;
}
