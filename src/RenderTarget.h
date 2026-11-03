#pragma once
#include <d3d11.h>

// A colour texture you can draw INTO (render target view) and later read FROM (shader view).
// DepthTarget's colour twin. Used for the HDR scene buffer; the same class will serve
// reflection textures and any other off-screen image.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool Create(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format);
    void Release();
    void Clear(ID3D11DeviceContext* ctx, const float color[4]) const;

    bool IsValid() const { return texture_ && renderView_ && shaderView_; }
    UINT GetWidth() const { return width_; }
    UINT GetHeight() const { return height_; }
    DXGI_FORMAT GetFormat() const { return format_; }
    ID3D11RenderTargetView* GetRenderView() const { return renderView_; }
    ID3D11ShaderResourceView* GetShaderView() const { return shaderView_; }

private:
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11RenderTargetView* renderView_ = nullptr;
    ID3D11ShaderResourceView* shaderView_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
};
