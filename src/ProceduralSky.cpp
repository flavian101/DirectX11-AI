#include "ProceduralSky.h"
#include "Logger.h"
#include "Shader.h"
#include "ShaderSlots.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstring>

namespace {
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
// Must match `cbuffer BakeParams : register(b5)` in shaders/ibl_common.hlsli.
struct CBBake { DirectX::XMFLOAT4 params; };
}

void ProceduralSky::Release()
{
    SafeRelease(cubeSrv_);
    SafeRelease(cube_);
    SafeRelease(paramsBuffer_);
    SafeRelease(bakeBuffer_);
    hasDrawn_ = false;
    attempted_ = false;
}

bool ProceduralSky::Initialize(ID3D11Device* device)
{
    Release();
    if (!device) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kSize;
    desc.Height = kSize;
    desc.MipLevels = 0;   // 0 = the whole chain: the lighting bake picks a blurrier level for each roughness
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;   // light values above 1.0 must survive
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;   // both are needed to generate mips
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &cube_);
    if (FAILED(hr)) { LOG_HR(hr, "ProceduralSky: cube texture creation failed"); Release(); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = static_cast<UINT>(-1);   // every level
    hr = device->CreateShaderResourceView(cube_, &srvDesc, &cubeSrv_);
    if (FAILED(hr)) { LOG_HR(hr, "ProceduralSky: cube view creation failed"); Release(); return false; }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.ByteWidth = sizeof(SkyModel::Params);
    hr = device->CreateBuffer(&bufferDesc, nullptr, &paramsBuffer_);
    if (SUCCEEDED(hr)) {
        bufferDesc.ByteWidth = sizeof(CBBake);
        hr = device->CreateBuffer(&bufferDesc, nullptr, &bakeBuffer_);
    }
    if (FAILED(hr)) { LOG_HR(hr, "ProceduralSky: constant buffer creation failed"); Release(); return false; }
    return true;
}

void ProceduralSky::Upload(ID3D11DeviceContext* ctx, const SkyModel::Params& params)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &params, sizeof(SkyModel::Params));
        ctx->Unmap(paramsBuffer_, 0);
    }
}

bool ProceduralSky::Update(ID3D11Device* device, ID3D11DeviceContext* ctx, const SkyModel::Params& params, float minSecondsBetweenRedraws)
{
    if (!IsReady() || !ctx) return false;
    Upload(ctx, params);   // the background always has this frame's sky, whatever the cube shows

    const auto now = std::chrono::steady_clock::now();
    const bool changed = !hasDrawn_ || SkyModel::Differs(drawn_, params);
    const bool rested = !attempted_ || std::chrono::duration<double>(now - lastRedraw_).count() >= std::max(minSecondsBetweenRedraws, 0.05f);
    if (!changed || !rested) return false;

    attempted_ = true;
    lastRedraw_ = now;   // also when it fails: a broken shader must cost one try per interval, not one per frame
    drawn_ = params;     // Redraw reads the buffer we just filled
    if (!Redraw(device, ctx)) {
        if (!reportedFailure_) { LOG_ERROR("ProceduralSky: drawing the sky cube failed; the sky lighting will not follow the time of day"); reportedFailure_ = true; }
        return false;
    }
    hasDrawn_ = true;
    return true;
}

bool ProceduralSky::Redraw(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    if (!shader_) shader_ = ShaderCache::Get(device, L"shaders/fullscreen_vs.hlsl", L"shaders/sky_procedural_cube_ps.hlsl");
    if (!shader_) { LOG_ERROR("ProceduralSky: the sky cube shader failed to load"); return false; }

    ID3D11RasterizerState* raster = nullptr;
    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &raster))) return false;

    const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(raster);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetConstantBuffers(Slots::kCbBake, 1, &bakeBuffer_);
    ctx->PSSetConstantBuffers(Slots::kCbSky, 1, &paramsBuffer_);
    shader_->Apply(ctx);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(kSize);
    viewport.Height = static_cast<float>(kSize);
    viewport.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &viewport);

    bool ok = true;
    for (UINT face = 0; face < 6 && ok; ++face) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(bakeBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            static_cast<CBBake*>(mapped.pData)->params = DirectX::XMFLOAT4(static_cast<float>(face), 0.0f, static_cast<float>(kSize), 0.0f);
            ctx->Unmap(bakeBuffer_, 0);
        }
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.FirstArraySlice = face;
        rtvDesc.Texture2DArray.ArraySize = 1;
        ID3D11RenderTargetView* target = nullptr;
        if (FAILED(device->CreateRenderTargetView(cube_, &rtvDesc, &target))) { ok = false; break; }
        ctx->OMSetRenderTargets(1, &target, nullptr);
        ctx->Draw(3, 0);
        target->Release();
    }

    // Hand the pipeline back the way the renderer expects it.
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11Buffer* none = nullptr;
    ctx->PSSetConstantBuffers(Slots::kCbBake, 1, &none);
    ctx->RSSetState(nullptr);
    raster->Release();
    if (ok) ctx->GenerateMips(cubeSrv_);   // the blurrier levels the lighting bake reads
    return ok;
}
