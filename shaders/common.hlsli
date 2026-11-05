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
    float4   reflectionParams; // x = 1 when a planar reflection was rendered this frame (the water may use t3)
    float4   iblParams;     // x = image-based lighting on (1/0), y = diffuse intensity, z = specular intensity, w = last prefilter mip
    float4   iblDebug;      // x = sky background: 0 sky, 1 irradiance, 2 prefiltered; y = mip to show
};

// b1 – lights, once per frame (CPU: LightSystem). Pixel shaders only.
#define MAX_POINT_LIGHTS 4
cbuffer Lights : register(b1) {
    float4 ambientColor;                          // rgb ambient term
    float4 dirDirection;                          // xyz direction light travels, w intensity
    float4 dirColor;                              // rgb directional color
    float4 pointPosRange[MAX_POINT_LIGHTS];       // xyz position, w range
    float4 pointColorIntensity[MAX_POINT_LIGHTS]; // rgb color, w intensity
    float4 cameraPosNumLights;                    // xyz camera position, w point light count
};

// b2 – per draw call (CPU: Renderable / CBPerObject).
cbuffer PerObject : register(b2) {
    float4x4 world;
    float4   materialColor;
    float4   materialParams;  // vs.hlsl reads x = UV tiling. Anything bigger belongs in the material's OWN block (b4).
};

// b4 - the material's own block. NOT declared here: each shader that has one declares it
// (see water_material.hlsli), because its layout belongs to that material.

#endif
