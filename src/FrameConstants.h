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
};
static_assert(sizeof(CBPerFrame) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

class FrameConstants {
public:
    FrameConstants() = default;
    ~FrameConstants();
    FrameConstants(const FrameConstants&) = delete;
    FrameConstants& operator=(const FrameConstants&) = delete;

    bool Initialize(ID3D11Device* device);
    void Release();

    // Uploads the data and binds the buffer to VS + PS slot b0.
    void Apply(ID3D11DeviceContext* ctx, const Camera& camera, float timeSeconds) const;

private:
    ID3D11Buffer* cb_ = nullptr;
};
