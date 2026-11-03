#include "common.hlsli"

// Depth-only vertex shader for the shadow pass. There is NO pixel shader: the GPU just writes
// the depth it computes here. In this pass `view` and `proj` are the SUN's matrices, because
// the Scene fills the per-frame buffer from the light instead of the camera.
struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };

float4 main(VS_IN input) : SV_POSITION
{
    return mul(mul(mul(float4(input.position, 1.0f), world), view), proj);
}
