// Shared declarations for every shader. Register numbers must match src/ShaderSlots.h.
// Previously each .hlsl file pasted its own copy of these cbuffers, which is how
// the vertex and pixel stages drifted out of sync.
#ifndef COMMON_HLSLI
#define COMMON_HLSLI

// b0 – once per frame (CPU: FrameConstants). Matrices are transposed on the CPU,
// so HLSL's default column-major packing gives us mul(vector, matrix) semantics.
cbuffer PerFrame : register(b0) {
    float4x4 view;
    float4x4 proj;
    float4   cameraPos;     // xyz = world-space camera position
    float4   frameParams;   // x = elapsed time in seconds
    float4x4 lightViewProj; // world -> the sun's clip space (see shadow.hlsli)
    float4   shadowParams;  // x = 1/mapSize, y = world units per texel, z = strength (0 = off), w = normal offset in texels
    float4   screenParams;  // x = 1/viewport width, y = 1/viewport height, z = near plane, w = far plane
    float4   clipPlane;     // world-space plane (a,b,c,d): pixels with dot(abc, pos) + d < 0 are discarded. (0,0,0,1) = off
    float4   reflectionParams; // x = 1 when a planar reflection was rendered this frame (the water may use t3), y = 1 when the scene copy for refraction is bound (t14)
    float4   iblParams;     // x = image-based lighting on (1/0), y = diffuse intensity, z = specular intensity, w = last prefilter mip
    float4   iblDebug;      // x = sky background: 0 sky, 1 irradiance, 2 prefiltered; y = mip to show
    float4   skyParams;     // x = cos(sky yaw), y = sin(sky yaw)
    float4   skyTint;       // rgb = sky tint (linear), w = sky intensity
    float4   fogParams;     // x = density (0 = no fog), y = start distance, z = height falloff, w = height of full density
    float4   fogColor;      // rgb = fog colour (linear), w = most opaque the fog may get
    float4   fogExtra;      // x = sun glow, y = how much the sky's horizon takes the fog colour, z = 1 when the camera is under water
};

// The sky is baked and drawn facing one way. To TURN it, every lookup into a sky texture (the sky itself, the baked
// diffuse and reflection maps, the water's mirror of the sky) rotates its direction around +Y first. Nothing is re-baked.
float3 SkyDirection(float3 d)
{
    return float3(skyParams.x * d.x + skyParams.y * d.z, d.y, -skyParams.y * d.x + skyParams.x * d.z);
}

// b1 – lights, once per frame (CPU: LightSystem). Pixel shaders only.
#define MAX_POINT_LIGHTS 8   // must equal LightSystem::kMaxPointLights (src/Light.h)
cbuffer Lights : register(b1) {
    float4 ambientColor;                          // rgb ambient term
    float4 dirDirection;                          // xyz direction light travels, w intensity
    float4 dirColor;                              // rgb directional color
    float4 pointPosRange[MAX_POINT_LIGHTS];       // xyz position, w range
    float4 pointColorIntensity[MAX_POINT_LIGHTS]; // rgb color, w intensity
    float4 cameraPosNumLights;                    // xyz camera position, w point light count
    float4 dirDirection2;                         // the second directional light (a moon beside the sun): xyz direction, w intensity (0 = none)
    float4 dirColor2;                             // rgb colour
};

// b2 – per draw call (CPU: Renderable / CBPerObject).
cbuffer PerObject : register(b2) {
    float4x4 world;
    float4   materialColor;
    float4   materialParams;  // vs.hlsl reads x = UV tiling. Anything bigger belongs in the material's OWN block (b4).
};

// b4 - the material's own block. NOT declared here: each shader that has one declares it
// (see water_material.hlsli), because its layout belongs to that material.


// ---- Fog (the air, or under water the water, between the camera and a surface) ----
// The fog is thicker low down: density(height) = density * exp(-falloff * (height - baseHeight)). The thickness the ray crosses is
// that integrated along the ray (exact for an exponential), then Beer-Lambert: the share that gets through is exp(-thickness).
// Returns how much of the surface is hidden (0..maxOpacity) and the colour that replaces it (with a glow toward the sun).
float FogAt(float3 worldPos, out float3 fog)
{
    fog = fogColor.rgb;
    [branch]
    if (fogParams.x <= 0.0f) return 0.0f;
    const float3 toPos = worldPos - cameraPos.xyz;
    const float dist = length(toPos);
    const float3 dir = toPos / max(dist, 1e-4f);
    const float marched = max(dist - fogParams.y, 0.0f);
    const float k = fogParams.z;
    const float startDensity = fogParams.x * exp(clamp(-k * (cameraPos.y - fogParams.w), -40.0f, 40.0f));   // density at the camera's height
    const float kdy = clamp(k * dir.y * marched, -40.0f, 40.0f);                                              // how much thinner (or thicker) it gets along the ray
    const float along = abs(kdy) > 1e-3f ? (1.0f - exp(-kdy)) / kdy : 1.0f;
    const float thickness = startDensity * along * marched;
    const float amount = min(1.0f - exp(-thickness), fogColor.w);
    // Fog toward the sun glows: the light scattered by the air toward your eye.
    const float towardSun = saturate(dot(dir, -dirDirection.xyz));
    fog += dirColor.rgb * (dirDirection.w * fogExtra.x * 0.25f * pow(towardSun, 8.0f));
    return amount;
}

float3 ApplyFog(float3 color, float3 worldPos)
{
    float3 fog;
    const float amount = FogAt(worldPos, fog);
    return lerp(color, fog, amount);
}

#endif
