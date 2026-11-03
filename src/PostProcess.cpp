#include "PostProcess.h"
#include "Logger.h"

PostProcess::~PostProcess() { Release(); }

void PostProcess::Release()
{
    if (rasterState_) { rasterState_->Release(); rasterState_ = nullptr; }
    shader_.reset();
}

bool PostProcess::Initialize(ID3D11Device* device)
{
    Release();
    if (!device) return false;

    shader_ = ShaderCache::Get(device, L"shaders/fullscreen_vs.hlsl", L"shaders/resolve_ps.hlsl");
    if (!shader_) { LOG_ERROR("Post-process shaders failed to load"); return false; }

    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_NONE;   // a full-screen triangle should never be culled, whatever its winding
    desc.DepthClipEnable = TRUE;
    const HRESULT hr = device->CreateRasterizerState(&desc, &rasterState_);
    if (FAILED(hr)) { LOG_HR(hr, "Post-process rasterizer state failed"); Release(); return false; }
    return true;
}

void PostProcess::Resolve(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* hdrScene,
                          ID3D11RenderTargetView* target, UINT width, UINT height) const
{
    if (!ctx || !hdrScene || !target || !IsValid()) return;

    // No depth buffer for a full-screen pass (a null depth view also disables depth testing).
    ctx->OMSetRenderTargets(1, &target, nullptr);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &viewport);

    // The scene pass ended with the water's alpha blending still active. Reset everything this
    // pass depends on instead of trusting whatever the last draw left behind.
    const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(rasterState_);

    shader_->Apply(ctx);
    ctx->PSSetShaderResources(0, 1, &hdrScene);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);

    // Let go of the HDR texture as an INPUT: next frame it becomes a render target again.
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    ctx->RSSetState(nullptr);
}
