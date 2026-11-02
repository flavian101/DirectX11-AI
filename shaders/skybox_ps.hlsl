TextureCube skyboxTexture : register(t0);
SamplerState skyboxSampler : register(s0);
cbuffer Constants : register(b0) {
    float4x4 world;
    float4x4 view;
    float4x4 proj;
    float4 materialColor;
    float time;
    float3 materialParameters;
};
struct PS_IN { float4 position : SV_POSITION; float3 direction : TEXCOORD0; };
float4 main(PS_IN input) : SV_Target {
    return skyboxTexture.Sample(skyboxSampler, normalize(input.direction)) * materialColor;
}
