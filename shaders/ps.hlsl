Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
float4 main(PS_IN input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(float3(-0.5f, 0.8f, -0.6f));
    float diffuse = saturate(dot(normal, -lightDirection));
    
    // Sample texture
    float4 texColor = colorTexture.Sample(colorSampler, input.texCoord);
    
    // Apply lighting to sampled texture
    float3 baseColor = texColor.rgb;
    float3 litColor = baseColor * (0.22f + 0.78f * diffuse);
    
    return float4(litColor, texColor.a);
}