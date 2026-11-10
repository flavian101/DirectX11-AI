#pragma once

// Single source of truth for GPU register slots. The HLSL side lives in
// shaders/common.hlsli – if you change a number here, change it there too.
namespace Slots {
    // Constant buffers (VS + PS unless noted)
    constexpr unsigned int kCbPerFrame  = 0;  // view, proj, camera, time   -> FrameConstants
    constexpr unsigned int kCbLights    = 1;  // ambient + dir + point lights (PS only) -> LightSystem
    constexpr unsigned int kCbPerObject = 2;  // world, material             -> Renderable
    constexpr unsigned int kCbPost      = 3;  // post-process settings (PS)  -> PostProcess
    constexpr unsigned int kCbMaterial  = 4;  // material-specific block     -> Renderable::SetMaterialBlock

    // Shader resource views (PS)
    constexpr unsigned int kSrvAlbedo      = 0;  // Texture2D / skybox TextureCube, set per draw
    constexpr unsigned int kSrvEnvironment = 1;  // TextureCube bound once per frame by Scene
    constexpr unsigned int kSrvShadow      = 2;  // the sun's depth map (Texture2D<float>)
    constexpr unsigned int kSrvReflection  = 3;  // the scene seen from below the water (planar reflection)
    constexpr unsigned int kSrvSceneDepth  = 4;  // the camera's own depth buffer, bound only for the water pass
    // Surface material maps (SurfaceMaterial::Bind). Every one always has a texture bound: a neutral
    // default when the artist supplied none, so the shader never needs an "is there a map?" branch.
    constexpr unsigned int kSrvNormal      = 5;  // tangent-space normal map (linear data, NOT sRGB)
    constexpr unsigned int kSrvOrm         = 6;  // R = ambient occlusion, G = roughness, B = metallic (linear)
    constexpr unsigned int kSrvEmissive    = 7;  // light the surface gives off by itself (sRGB colour)
    // Terrain layers (Terrain::Render): Texture2DArray, slice 0 grass / 1 rock / 2 sand / 3 snow. Two arrays, not three (lesson 29).
    constexpr unsigned int kSrvTerrainAlbedo = 8;    // rgb = albedo (sRGB colour), a = HEIGHT (used for height-based blending)
    constexpr unsigned int kSrvTerrainNormal = 9;    // r,g = tangent-space normal XY (z is rebuilt), b = occlusion, a = roughness (linear)
    //                                          10    (was the ORM array; free now)
    // Image-based lighting, baked from the sky by EnvironmentLighting and bound once per frame by Scene.
    constexpr unsigned int kSrvIrradiance  = 11;  // TextureCube: soft light from every direction (diffuse)
    constexpr unsigned int kSrvPrefiltered = 12;  // TextureCube: the sky blurred per roughness level, in its mips (reflections)
    constexpr unsigned int kSrvBrdfTable   = 13;  // Texture2D: reflectivity vs. view angle and roughness (kSrvIrradiance + 2: keep them consecutive)
    constexpr unsigned int kSrvSceneColor  = 14;  // a copy of the opaque scene, bound only for the water pass (refraction)
    constexpr unsigned int kSrvWaterDepth  = 15;  // R32_FLOAT map of the water's depth over the terrain, bound per water body (VS + PS)
    constexpr unsigned int kSrvTerrainSplat = 16; // RGBA8: the terrain's layer weights (grass, rock, sand, snow), baked from the heightfield (PS)

    // Samplers (PS)
    constexpr unsigned int kSamplerLinear  = 0;  // trilinear + anisotropic, bound once by the renderer
    constexpr unsigned int kSamplerShadow  = 1;  // comparison sampler for the shadow map
    constexpr unsigned int kSamplerClamp   = 2;  // linear, edges CLAMP instead of wrapping (screen-space lookups)
}
