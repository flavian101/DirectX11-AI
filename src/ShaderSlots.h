#pragma once

// Single source of truth for GPU register slots. The HLSL side lives in
// shaders/common.hlsli – if you change a number here, change it there too.
namespace Slots {
    // Constant buffers (VS + PS unless noted)
    constexpr unsigned int kCbPerFrame  = 0;  // view, proj, camera, time   -> FrameConstants
    constexpr unsigned int kCbLights    = 1;  // ambient + dir + point lights (PS only) -> LightSystem
    constexpr unsigned int kCbPerObject = 2;  // world, material             -> Renderable

    // Shader resource views (PS)
    constexpr unsigned int kSrvAlbedo      = 0;  // Texture2D / skybox TextureCube, set per draw
    constexpr unsigned int kSrvEnvironment = 1;  // TextureCube bound once per frame by Scene
    constexpr unsigned int kSrvShadow      = 2;  // the sun's depth map (Texture2D<float>)

    // Samplers (PS)
    constexpr unsigned int kSamplerLinear  = 0;  // trilinear + anisotropic, bound once by the renderer
    constexpr unsigned int kSamplerShadow  = 1;  // comparison sampler for the shadow map
}
