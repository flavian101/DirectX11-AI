#include "DepthTarget.h"
#include "Logger.h"

DepthTarget::~DepthTarget() { Release(); }

void DepthTarget::Release()
{
    if (shaderView_) { shaderView_->Release(); shaderView_ = nullptr; }
    if (readOnlyDepthView_) { readOnlyDepthView_->Release(); readOnlyDepthView_ = nullptr; }
    if (depthView_) { depthView_->Release(); depthView_ = nullptr; }
    if (texture_) { texture_->Release(); texture_ = nullptr; }
    width_ = height_ = 0;
}

bool DepthTarget::Create(ID3D11Device* device, UINT width, UINT height)
{
    Release();
    if (!device || width == 0 || height == 0) {
        LOG_ERROR("DepthTarget::Create needs a device and a non-zero size");
        return false;
    }

    // The texture is TYPELESS: the memory is just "32 bits per pixel" and each view decides
    // how to read it. A depth view must see it as D32_FLOAT, a shader view as R32_FLOAT.
    // If we created it as D32_FLOAT directly, the shader view could not be created.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture_);
    if (FAILED(hr)) {
        LOG_HR(hr, "DepthTarget texture creation failed");
        Release();
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(texture_, &dsvDesc, &depthView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "DepthTarget depth view creation failed");
        Release();
        return false;
    }

    dsvDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;   // same memory, "look but do not touch"
    hr = device->CreateDepthStencilView(texture_, &dsvDesc, &readOnlyDepthView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "DepthTarget read-only depth view creation failed");
        Release();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(texture_, &srvDesc, &shaderView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "DepthTarget shader view creation failed");
        Release();
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

void DepthTarget::Clear(ID3D11DeviceContext* ctx, float depth) const
{
    if (ctx && depthView_) ctx->ClearDepthStencilView(depthView_, D3D11_CLEAR_DEPTH, depth, 0);
}
