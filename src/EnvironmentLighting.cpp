#include "EnvironmentLighting.h"
#include "Logger.h"
#include "ShaderSlots.h"
#include <DirectXMath.h>
#include <vector>

namespace {
// Must match `cbuffer BakeParams : register(b5)` in shaders/ibl_common.hlsli.
struct CBBake {
    DirectX::XMFLOAT4 params;   // x = face, y = roughness, z = source face size
};
static_assert(sizeof(CBBake) % 16 == 0, "constant buffers must be a multiple of 16 bytes");
constexpr UINT kBakeSlot = 5;

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
}

void EnvironmentLighting::Release()
{
    SafeRelease(brdfSrv_);
    SafeRelease(brdf_);
    SafeRelease(prefilteredSrv_);
    SafeRelease(prefiltered_);
    SafeRelease(irradianceSrv_);
    SafeRelease(irradiance_);
    SafeRelease(source_);
}

void EnvironmentLighting::Bind(ID3D11DeviceContext* ctx) const
{
    if (!ctx) return;
    ID3D11ShaderResourceView* maps[3] = { irradianceSrv_, prefilteredSrv_, brdfSrv_ };
    ctx->PSSetShaderResources(Slots::kSrvIrradiance, 3, maps);   // t11, t12, t13 are consecutive
}

bool EnvironmentLighting::CreateCube(ID3D11Device* device, UINT size, UINT mips, ID3D11Texture2D*& texture,
                                     ID3D11ShaderResourceView*& srv)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size;
    desc.Height = size;
    desc.MipLevels = mips;
    desc.ArraySize = 6;   // a cube map is 6 slices, one per face
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;   // light values above 1.0 must survive
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
    if (FAILED(hr)) { LOG_HR(hr, "IBL: cube texture creation failed"); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = mips;
    hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    if (FAILED(hr)) { LOG_HR(hr, "IBL: cube view creation failed"); return false; }
    return true;
}

bool EnvironmentLighting::Bake(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* sourceCube)
{
    Release();
    if (!device || !ctx || !sourceCube) return false;

    // The full-screen triangle vertex shader is the same one the post-process uses.
    auto irradianceShader = ShaderCache::Get(device, L"shaders/fullscreen_vs.hlsl", L"shaders/ibl_irradiance_ps.hlsl");
    auto prefilterShader = ShaderCache::Get(device, L"shaders/fullscreen_vs.hlsl", L"shaders/ibl_prefilter_ps.hlsl");
    auto brdfShader = ShaderCache::Get(device, L"shaders/fullscreen_vs.hlsl", L"shaders/ibl_brdf_ps.hlsl");
    if (!irradianceShader || !prefilterShader || !brdfShader) {
        LOG_ERROR("IBL: bake shaders failed to load; lighting falls back to the flat ambient colour");
        return false;
    }

    // How big is the sky's face? The prefilter shader needs it to pick sensible mip levels.
    float sourceFaceSize = 512.0f;
    {
        ID3D11Resource* resource = nullptr;
        sourceCube->GetResource(&resource);
        ID3D11Texture2D* sourceTexture = nullptr;
        if (resource && SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&sourceTexture)))) {
            D3D11_TEXTURE2D_DESC desc{};
            sourceTexture->GetDesc(&desc);
            sourceFaceSize = static_cast<float>(desc.Width);
            sourceTexture->Release();
        }
        if (resource) resource->Release();
    }

    // ---- create the targets ----
    bool ok = CreateCube(device, kIrradianceSize, 1, irradiance_, irradianceSrv_) &&
              CreateCube(device, kPrefilterSize, kPrefilterMips, prefiltered_, prefilteredSrv_);
    if (ok) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kBrdfSize;
        desc.Height = kBrdfSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &brdf_)) &&
             SUCCEEDED(device->CreateShaderResourceView(brdf_, nullptr, &brdfSrv_));
    }

    // ---- fixed-function state for the full-screen passes ----
    ID3D11Buffer* paramsBuffer = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    if (ok) {
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = sizeof(CBBake);
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        ok = SUCCEEDED(device->CreateBuffer(&bufferDesc, nullptr, &paramsBuffer)) &&
             SUCCEEDED(device->CreateSamplerState(&samplerDesc, &sampler)) &&
             SUCCEEDED(device->CreateRasterizerState(&rasterDesc, &raster));
    }
    if (!ok) {
        LOG_ERROR("IBL: could not create the bake resources");
        SafeRelease(paramsBuffer); SafeRelease(sampler); SafeRelease(raster);
        Release();
        return false;
    }

    // Remember the one binding we are about to replace: the renderer's sampler in slot s0.
    ID3D11SamplerState* previousSampler = nullptr;
    ctx->PSGetSamplers(0, 1, &previousSampler);

    const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(raster);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetSamplers(0, 1, &sampler);
    ctx->PSSetShaderResources(0, 1, &sourceCube);
    ctx->PSSetConstantBuffers(kBakeSlot, 1, &paramsBuffer);

    // One full-screen draw into render target `target`, `size` pixels wide.
    auto draw = [&](ID3D11RenderTargetView* target, UINT size, UINT face, float roughness) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(paramsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            static_cast<CBBake*>(mapped.pData)->params = DirectX::XMFLOAT4(static_cast<float>(face), roughness, sourceFaceSize, 0.0f);
            ctx->Unmap(paramsBuffer, 0);
        }
        ctx->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(size);
        viewport.Height = static_cast<float>(size);
        viewport.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &viewport);
        ctx->Draw(3, 0);
    };
    // A render-target view onto one face (slice) at one mip level of a cube texture.
    auto faceView = [&](ID3D11Texture2D* texture, UINT mip, UINT face) -> ID3D11RenderTargetView* {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = mip;
        desc.Texture2DArray.FirstArraySlice = face;
        desc.Texture2DArray.ArraySize = 1;
        ID3D11RenderTargetView* view = nullptr;
        device->CreateRenderTargetView(texture, &desc, &view);
        return view;
    };

    // ---- 1. diffuse irradiance: 6 faces ----
    irradianceShader->Apply(ctx);
    for (UINT face = 0; face < 6; ++face) {
        if (ID3D11RenderTargetView* view = faceView(irradiance_, 0, face)) {
            draw(view, kIrradianceSize, face, 0.0f);
            view->Release();
        }
    }

    // ---- 2. specular prefilter: 6 faces x 5 mips, roughness rising from 0 (mip 0) to 1 (last mip) ----
    prefilterShader->Apply(ctx);
    for (UINT mip = 0; mip < kPrefilterMips; ++mip) {
        const UINT size = kPrefilterSize >> mip;
        const float roughness = static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1);
        for (UINT face = 0; face < 6; ++face) {
            if (ID3D11RenderTargetView* view = faceView(prefiltered_, mip, face)) {
                draw(view, size, face, roughness);
                view->Release();
            }
        }
    }

    // ---- 3. BRDF table: one draw ----
    brdfShader->Apply(ctx);
    {
        ID3D11RenderTargetView* view = nullptr;
        if (SUCCEEDED(device->CreateRenderTargetView(brdf_, nullptr, &view))) {
            draw(view, kBrdfSize, 0, 0.0f);
            view->Release();
        }
    }

    // ---- hand the pipeline back the way the renderer expects it ----
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    ID3D11Buffer* noBuffer = nullptr;
    ctx->PSSetConstantBuffers(kBakeSlot, 1, &noBuffer);
    ctx->PSSetSamplers(0, 1, &previousSampler);
    if (previousSampler) previousSampler->Release();
    ctx->RSSetState(nullptr);

    SafeRelease(paramsBuffer);
    SafeRelease(sampler);
    SafeRelease(raster);

    source_ = sourceCube;
    source_->AddRef();   // so we notice when the sky changes (a stale pointer could look "the same")
    LOG_INFO("IBL baked: irradiance 32, prefiltered 128 x 5 mips, BRDF table 256");
    return true;
}
