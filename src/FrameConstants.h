#pragma once
#include "Camera.h"
#include <DirectXMath.h>
#include <d3d11.h>

// Data that is identical for every draw call in a frame. Uploaded ONCE per
// frame instead of once per object. Must match `cbuffer PerFrame` in
// shaders/common.hlsli.
struct CBPerFrame {
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4   cameraPos;    // xyz = world-space camera position
    DirectX::XMFLOAT4   frameParams;  // x = elapsed time in seconds
    DirectX::XMFLOAT4X4 lightViewProj;
    DirectX::XMFLOAT4   shadowParams; // x = 1/mapSize, y = world units per texel, z = strength, w = normal offset
    DirectX::XMFLOAT4   screenParams; // x = 1/width, y = 1/height (of the viewport being drawn), z = near, w = far
    DirectX::XMFLOAT4   clipPlane;    // world-space plane; (0,0,0,1) = no clipping
    DirectX::XMFLOAT4   reflectionParams; // x = planar reflection available this frame, y = scene copy for refraction available
    DirectX::XMFLOAT4   iblParams;    // x = image-based lighting on (1/0), y = diffuse intensity, z = specular intensity, w = last prefilter mip
    DirectX::XMFLOAT4   iblDebug;     // x = sky background: 0 sky, 1 irradiance, 2 prefiltered; y = mip to show
};
static_assert(sizeof(CBPerFrame) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// Everything the per-frame buffer needs, WITHOUT assuming it comes from a Camera. The sun
// is not a Camera, but a shadow pass still wants "view + projection + eye position" -
// so the pass fills one of these by hand and the shaders cannot tell the difference.
struct FrameData {
    DirectX::XMFLOAT4X4 view{};
    DirectX::XMFLOAT4X4 proj{};
    DirectX::XMFLOAT3 cameraPos{};
    float time = 0.0f;
    float viewportWidth = 1.0f;     // size of the target being drawn, in pixels
    float viewportHeight = 1.0f;
    float nearPlane = Camera::kNearPlane;
    float farPlane = Camera::kFarPlane;
    DirectX::XMFLOAT4 clipPlane{ 0.0f, 0.0f, 0.0f, 1.0f };   // always positive: nothing is clipped
    bool reflectionAvailable = false;
    bool refractionAvailable = false;   // a copy of the opaque scene is bound to t14 (the water bends it)
    DirectX::XMFLOAT4 iblParams{};         // zeros = image-based lighting off (the sun's shadow pass uses that)
    DirectX::XMFLOAT4 iblDebug{};
    DirectX::XMFLOAT4X4 lightViewProj{};   // identity-free zero matrix is fine while strength is 0
    DirectX::XMFLOAT4 shadowParams{};      // z = 0 means "no shadows this frame"

    static FrameData FromCamera(const Camera& camera, float timeSeconds);
};

class FrameConstants {
public:
    FrameConstants() = default;
    ~FrameConstants();
    FrameConstants(const FrameConstants&) = delete;
    FrameConstants& operator=(const FrameConstants&) = delete;

    bool Initialize(ID3D11Device* device);
    void Release();

    // Uploads the data and binds the buffer to VS + PS slot b0.
    void Apply(ID3D11DeviceContext* ctx, const FrameData& frame) const;
    void Apply(ID3D11DeviceContext* ctx, const Camera& camera, float timeSeconds) const
    {
        Apply(ctx, FrameData::FromCamera(camera, timeSeconds));
    }

private:
    ID3D11Buffer* cb_ = nullptr;
};
