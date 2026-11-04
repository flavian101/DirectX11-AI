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

    // Samplers (PS)
    constexpr unsigned int kSamplerLinear  = 0;  // trilinear + anisotropic, bound once by the renderer
    constexpr unsigned int kSamplerShadow  = 1;  // comparison sampler for the shadow map
    constexpr unsigned int kSamplerClamp   = 2;  // linear, edges CLAMP instead of wrapping (screen-space lookups)
}
