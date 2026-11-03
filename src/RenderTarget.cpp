#include "RenderTarget.h"
#include "Logger.h"

RenderTarget::~RenderTarget() { Release(); }

void RenderTarget::Release()
{
    if (shaderView_) { shaderView_->Release(); shaderView_ = nullptr; }
    if (renderView_) { renderView_->Release(); renderView_ = nullptr; }
    if (texture_) { texture_->Release(); texture_ = nullptr; }
    width_ = height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
}

bool RenderTarget::Create(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
    Release();
    if (!device || width == 0 || height == 0) {
        LOG_ERROR("RenderTarget::Create needs a device and a non-zero size");
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture_);
    if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(texture_, nullptr, &renderView_);
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(texture_, nullptr, &shaderView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "RenderTarget creation failed");
        Release();
        return false;
    }
    width_ = width;
    height_ = height;
    format_ = format;
    return true;
}

void RenderTarget::Clear(ID3D11DeviceContext* ctx, const float color[4]) const
{
    if (ctx && renderView_) ctx->ClearRenderTargetView(renderView_, color);
}
