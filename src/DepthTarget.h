#pragma once
#include <d3d11.h>

// A depth buffer you can ALSO read from a shader afterwards - the building block for shadow
// maps (and later: water depth fade, planar reflection, post-processing).
//
// The same GPU texture is exposed through two views:
//   depth view  (DSV) - "draw INTO me" during a pass
//   shader view (SRV) - "sample FROM me" during a later pass
// A texture must never be bound as BOTH at the same moment; unbind one before using the other.
class DepthTarget {
public:
    DepthTarget() = default;
    ~DepthTarget();
    DepthTarget(const DepthTarget&) = delete;
    DepthTarget& operator=(const DepthTarget&) = delete;

    bool Create(ID3D11Device* device, UINT width, UINT height);
    void Release();
    void Clear(ID3D11DeviceContext* ctx, float depth = 1.0f) const;

    bool IsValid() const { return texture_ && depthView_ && shaderView_; }
    UINT GetWidth() const { return width_; }
    UINT GetHeight() const { return height_; }
    ID3D11DepthStencilView* GetDepthView() const { return depthView_; }
    ID3D11ShaderResourceView* GetShaderView() const { return shaderView_; }

private:
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11DepthStencilView* depthView_ = nullptr;
    ID3D11ShaderResourceView* shaderView_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
};
