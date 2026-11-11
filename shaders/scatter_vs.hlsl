#include "common.hlsli"

// Instanced vertex shader (lesson 31): ONE draw call places thousands of copies of a mesh. Stream 0 is the mesh, as always.
// Stream 1 advances once per INSTANCE instead of once per vertex and carries that copy's place in the world:
//   INSTPOS = (x, y, z, scale)        the world position of the model's base, and a uniform size
//   INSTROT = (x, y, z, w)            a unit quaternion
// (CPU side: ScatterInstance in src/ScatterPlacement.h; the input layout is added in Shader.cpp when a shader asks for INSTPOS.)
// The output is exactly what vs.hlsl outputs, so the ordinary ps.hlsl lights it: a scattered tree and a hand-placed one match.
struct VS_IN {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangent : TANGENT;
    float4 instPosScale : INSTPOS;
    float4 instRot : INSTROT;
};
struct VS_OUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float3 worldPos : TEXCOORD1;
    float4 tangent : TEXCOORD2;
    float clip : SV_ClipDistance0;
};

// Rotates v by the unit quaternion q: v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v).
float3 RotateByQuaternion(float4 q, float3 v)
{
    return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

VS_OUT main(VS_IN input)
{
    VS_OUT output;
    const float3 world = RotateByQuaternion(input.instRot, input.position * input.instPosScale.w) + input.instPosScale.xyz;
    const float4 worldPos = float4(world, 1.0f);
    output.position = mul(mul(worldPos, view), proj);
    // The scale is uniform, so directions only need the rotation (no inverse-transpose).
    output.normal = RotateByQuaternion(input.instRot, input.normal);
    output.tangent = float4(RotateByQuaternion(input.instRot, input.tangent.xyz), input.tangent.w);
    output.texCoord = input.texCoord;
    output.worldPos = world;
    output.clip = dot(worldPos, clipPlane);   // the water reflection pass hides everything below the water with this
    return output;
}
