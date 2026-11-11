#include "common.hlsli"

// The sun's depth pass for scattered objects. Same placement as scatter_vs.hlsl, but only the position goes out (plus the
// UV, which scatter_shadow_ps.hlsl needs to cut leaves out of the shadow). In this pass `view` and `proj` are the sun's.
struct VS_IN {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float4 instPosScale : INSTPOS;
    float4 instRot : INSTROT;
};
struct VS_OUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

float3 RotateByQuaternion(float4 q, float3 v)
{
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

VS_OUT main(VS_IN input)
{
    VS_OUT output;
    const float3 world = RotateByQuaternion(input.instRot, input.position * input.instPosScale.w) + input.instPosScale.xyz;
    output.position = mul(mul(float4(world, 1.0f), view), proj);
    output.texCoord = input.texCoord;
    return output;
}
