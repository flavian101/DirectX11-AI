#include "common.hlsli"
TextureCube skyboxTexture : register(t0);
SamplerState skyboxSampler : register(s0);
struct PS_IN { float4 position : SV_POSITION; float3 direction : TEXCOORD0; };
float4 main(PS_IN input) : SV_Target {
    return skyboxTexture.Sample(skyboxSampler, normalize(input.direction)) * materialColor;
}
