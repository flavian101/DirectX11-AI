#include "waves.hlsli"

struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;   // where the surface point ended up
    float3 rest     : TEXCOORD1;   // xy = where it rests (waves are parameterised by this), z = wave height / tallest wave (-1..1)
    float2 local    : TEXCOORD2;   // position on the plane itself, -1..1 in both directions (for round bodies)
};

VS_OUT main(VS_IN input)
{
    VS_OUT output;

    // World space FIRST: waves are measured in world units, so resizing the plane changes how
    // much water you see, not how big the waves are. (Assumes a horizontal plane.)
    const float3 restPos = mul(float4(input.position, 1.0f), world).xyz;

    // The mesh gets coarser with distance (see Water.cpp), so the shortest waves must fade out before the grid can
    // no longer hold them. Grid spacing grows about in proportion to distance beyond the LOD distance, up to 8x.
    const float lodDistance = waterMeshInfo.x;
    const float cellSize = waterMeshInfo.y;
    const float distanceToCamera = length(restPos - cameraPos.xyz);
    const float spacing = cellSize * clamp(distanceToCamera / max(lodDistance, 0.001f), 1.0f, 8.0f);
    const float maxK = kTwoPi / (3.0f * spacing);   // keep >= 3 grid cells per wavelength

    // How deep the water is here (from the terrain), so waves grow and break as they reach the shore.
    const float3 d = WaveDisplacement(restPos.xz, frameParams.x, maxK, WaterShoreAt(input.position.xz));
    float3 worldPos = restPos;
    worldPos.xz += d.xz;
    worldPos.y  += d.y;

    output.position = mul(mul(float4(worldPos, 1.0f), view), proj);
    output.worldPos = worldPos;
    output.rest = float3(restPos.xz, d.y / max(waterMeshInfo.z, 1e-4f));
    output.local = input.position.xz;
    return output;
}
