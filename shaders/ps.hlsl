Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

#define MAX_POINT_LIGHTS 4

// Matches LightSystem::CBLights in Light.h; uploaded once per frame.
cbuffer Lights : register(b1) {
    float4 ambientColor;                          // rgb ambient term
    float4 dirDirection;                          // xyz direction light travels, w intensity
    float4 dirColor;                              // rgb directional color
    float4 pointPosRange[MAX_POINT_LIGHTS];       // xyz position, w range
    float4 pointColorIntensity[MAX_POINT_LIGHTS]; // rgb color, w intensity
    float4 cameraPosNumLights;                    // xyz camera position, w point light count
};

struct PS_IN { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

float4 main(PS_IN input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(cameraPosNumLights.xyz - input.worldPos);

    float3 diffuse = ambientColor.rgb;
    float3 specular = float3(0.0f, 0.0f, 0.0f);

    // Directional light (Blinn-Phong)
    {
        float3 L = normalize(-dirDirection.xyz);
        diffuse += dirColor.rgb * dirDirection.w * saturate(dot(normal, L));
        float3 h = normalize(L + viewDir);
        specular += dirColor.rgb * dirDirection.w * pow(saturate(dot(normal, h)), 32.0f);
    }

    // Point lights with smooth quadratic range falloff
    const int numPointLights = (int)cameraPosNumLights.w;
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= numPointLights) break;
        float3 toLight = pointPosRange[i].xyz - input.worldPos;
        float dist = length(toLight);
        float3 L = toLight / max(dist, 1e-4f);
        float falloff = saturate(1.0f - dist / pointPosRange[i].w);
        falloff *= falloff;
        float3 radiance = pointColorIntensity[i].rgb * pointColorIntensity[i].w * falloff;
        diffuse += radiance * saturate(dot(normal, L));
        float3 h = normalize(L + viewDir);
        specular += radiance * pow(saturate(dot(normal, h)), 32.0f);
    }

    float4 texColor = colorTexture.Sample(colorSampler, input.texCoord);
    float3 litColor = texColor.rgb * diffuse + specular * 0.5f;
    return float4(litColor, texColor.a);
}
