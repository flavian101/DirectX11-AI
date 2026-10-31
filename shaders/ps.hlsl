struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
float4 main(PS_IN input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(float3(-0.5f, 0.8f, -0.6f));
    float diffuse = saturate(dot(normal, -lightDirection));
    float3 baseColor = float3(0.20f, 0.75f, 0.30f);
    return float4(baseColor * (0.22f + 0.78f * diffuse), 1.0f);
}