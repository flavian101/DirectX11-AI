// Only used for materials with an alpha cutoff (leaves, fences): without it a leaf card would cast a solid rectangle of
// shadow. Same test as ps.hlsl: albedo alpha times the base colour's alpha against the cutoff. Opaque materials skip this
// shader entirely and use the depth-only program (no pixel shader at all), which is much cheaper.
Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

// b4 - the same material block ps.hlsl reads (CPU: SurfaceMaterialCB in SurfaceMaterial.h). Only two fields matter here.
cbuffer SurfaceMaterial : register(b4) {
    float4 surfaceParams;
    float4 surfaceFlags;
    float4 surfaceBaseColor;   // linear rgba: multiplies the albedo map
    float4 surfaceExtra;       // x = alpha cutoff
};

void main(float4 position : SV_POSITION, float2 texCoord : TEXCOORD)
{
    const float alpha = colorTexture.Sample(colorSampler, texCoord).a * surfaceBaseColor.a;
    if (alpha < surfaceExtra.x) discard;
}
