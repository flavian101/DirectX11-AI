#include "ocean.hlsli"

struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;   // where the surface point ended up
    float3 rest     : TEXCOORD1;   // xy = where it rests (the sea is parameterised by this), z = wave height / (half the tallest wave), -1..1
    float2 local    : TEXCOORD2;   // position on the plane itself, -1..1 in both directions (for round bodies)
    float  shore    : TEXCOORD3;   // ShoreLimit here: 1 in deep water, falling to 0 at the waterline (the pixel shader flattens the normal by it)
};

VS_OUT main(VS_IN input)
{
    VS_OUT output;

    // World space FIRST: waves are measured in world units, so resizing the plane changes how
    // much water you see, not how big the waves are. (Assumes a horizontal plane.)
    float3 restPos = mul(float4(input.position, 1.0f), world).xyz;

    // The mesh gets coarser with distance (see Water.cpp): the grid spacing grows about in proportion to distance beyond the LOD distance, up to 8x.
    // The sea is read from the mip whose texels match that spacing, so a coarse far grid is given the averaged sea it can hold, not waves it would alias.
    const float lodDistance = waterMeshInfo.x;
    const float cellSize = waterMeshInfo.y;
    const float distanceToCamera = length(restPos - cameraPos.xyz);
    const float spacing = cellSize * clamp(distanceToCamera / max(lodDistance, 0.001f), 1.0f, 8.0f);

    // How deep the water is here (from the terrain): the surface is limited to a fraction of it, so waves flatten out toward the waterline.
    const float depth = WaterShoreAt(input.position.xz).x;
    const float3 d = OceanDisplacement(restPos.xz, spacing, depth);
    float3 worldPos = restPos;
    worldPos.xz += d.xz;
    worldPos.y  += d.y;

    output.position = mul(mul(float4(worldPos, 1.0f), view), proj);
    output.worldPos = worldPos;
    output.rest = float3(restPos.xz, clamp(d.y / max(0.5f * waterMeshInfo.z, 1e-4f), -1.0f, 1.0f));   // +-1 at 3 sigma
    output.local = input.position.xz;
    output.shore = ShoreLimit(d.y, depth);
    return output;
}
