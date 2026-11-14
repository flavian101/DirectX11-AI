// Renders the procedural sky into one face of a cube map, so the sky's lighting can be baked from it (see ProceduralSky.cpp).
#include "ibl_common.hlsli"
#include "sky_procedural.hlsli"

struct PS_IN { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(PS_IN input) : SV_Target
{
    const float3 dir = CubeFaceDirection((uint)bakeParams.x, input.uv);
    return float4(ProceduralSky(dir, false), 1.0f);
}
