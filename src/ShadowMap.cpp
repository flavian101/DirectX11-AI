#include "ShadowMap.h"
#include "Logger.h"
#include "ShaderSlots.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

void ShadowSettings::Clamp()
{
    resolution = std::clamp(resolution, kResolutionMin, kResolutionMax);
    depthBias = std::clamp(depthBias, 0, kDepthBiasMax);
    slopeBias = std::clamp(slopeBias, 0.0f, kSlopeBiasMax);
    normalOffset = std::clamp(normalOffset, 0.0f, kNormalOffsetMax);
    strength = std::clamp(strength, 0.0f, 1.0f);
}

ShadowMap::~ShadowMap() { Release(); }

void ShadowMap::Release()
{
    if (sampler_) { sampler_->Release(); sampler_ = nullptr; }
    if (rasterState_) { rasterState_->Release(); rasterState_ = nullptr; }
    target_.Release();
    shader_.reset();
    device_ = nullptr;
}

bool ShadowMap::Initialize(ID3D11Device* device, const ShadowSettings& settings)
{
    Release();
    if (!device) return false;
    device_ = device;
    settings_ = settings;
    settings_.Clamp();

    // Vertex shader only (null pixel shader path): this pass writes depth and nothing else.
    shader_ = ShaderCache::Get(device, L"shaders/shadow_vs.hlsl", nullptr);
    if (!shader_) { LOG_ERROR("Shadow shader failed to load"); Release(); return false; }

    const UINT size = static_cast<UINT>(settings_.resolution);
    if (!target_.Create(device, size, size) || !CreateRasterizerState()) { Release(); return false; }

    // A COMPARISON sampler: instead of returning the stored depth it compares it with a value
    // we supply and returns 0 or 1 - and with a LINEAR filter it blends the results of the four
    // nearest texels, giving free soft edges. Outside the map (border) we answer 1.0 = "lit".
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDesc.BorderColor[0] = samplerDesc.BorderColor[1] = samplerDesc.BorderColor[2] = samplerDesc.BorderColor[3] = 1.0f;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    const HRESULT hr = device->CreateSamplerState(&samplerDesc, &sampler_);
    if (FAILED(hr)) { LOG_HR(hr, "Shadow sampler creation failed"); Release(); return false; }
    return true;
}

bool ShadowMap::CreateRasterizerState()
{
    if (rasterState_) { rasterState_->Release(); rasterState_ = nullptr; }

    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_BACK;        // not FRONT: the terrain is a one-sided sheet (lesson 2)
    desc.DepthBias = settings_.depthBias;
    desc.DepthBiasClamp = 0.0f;
    desc.SlopeScaledDepthBias = settings_.slopeBias;
    desc.DepthClipEnable = FALSE;           // casters in front of the light's near plane still count
    const HRESULT hr = device_->CreateRasterizerState(&desc, &rasterState_);
    if (FAILED(hr)) { LOG_HR(hr, "Shadow rasterizer state creation failed"); return false; }
    return true;
}

void ShadowMap::SetSettings(const ShadowSettings& requested)
{
    if (!device_) return;
    ShadowSettings next = requested;
    next.Clamp();
    const bool resolutionChanged = next.resolution != settings_.resolution;
    const bool biasChanged = next.depthBias != settings_.depthBias || next.slopeBias != settings_.slopeBias;
    settings_ = next;
    if (resolutionChanged) {
        const UINT size = static_cast<UINT>(settings_.resolution);
        target_.Create(device_, size, size);
    }
    // A rasterizer state is IMMUTABLE once created, so a new bias means a new state object.
    if (biasChanged) CreateRasterizerState();
}

void ShadowMap::Begin(ID3D11DeviceContext* ctx, const XMFLOAT3& lightDirection,
                      const XMFLOAT3& center, float radius)
{
    if (!ctx || !IsValid()) return;

    // 1. Let go of the texture as a SHADER INPUT. Last frame's main pass left it bound at t2;
    //    binding it as a depth target while it is still an input makes D3D null it silently.
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(Slots::kSrvShadow, 1, &none);

    // 2. The sun's camera: an orthographic box that wraps the bounding sphere.
    XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&lightDirection));
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 0.5f) dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
    const XMVECTOR target = XMLoadFloat3(&center);
    const XMVECTOR eye = target - dir * radius;                 // one radius back, towards the sun
    const XMVECTOR up = std::fabs(XMVectorGetY(dir)) > 0.99f
        ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)                   // looking straight down: Y is not a usable "up"
        : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMStoreFloat4x4(&view_, XMMatrixLookAtLH(eye, target, up));
    XMStoreFloat4x4(&proj_, XMMatrixOrthographicLH(2.0f * radius, 2.0f * radius, 0.0f, 2.0f * radius));
    XMStoreFloat3(&lightPosition_, eye);
    texelWorldSize_ = 2.0f * radius / static_cast<float>(target_.GetWidth());
    hasShadows_ = true;

    // 3. Draw into depth only. No colour target at all.
    ctx->OMSetRenderTargets(0, nullptr, target_.GetDepthView());
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(target_.GetWidth());
    viewport.Height = static_cast<float>(target_.GetHeight());
    viewport.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &viewport);
    target_.Clear(ctx);

    // Previous frame's last draw may have left the water's "no depth writes" state bound.
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(rasterState_);
}

void ShadowMap::End(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);   // un-bind the depth view so it can be read next
    ctx->RSSetState(nullptr);
}

void ShadowMap::FillFrameData(FrameData& frame) const
{
    if (!hasShadows_) {
        frame.shadowParams = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);   // strength 0: shaders skip the lookup
        return;
    }
    XMStoreFloat4x4(&frame.lightViewProj, XMLoadFloat4x4(&view_) * XMLoadFloat4x4(&proj_));
    frame.shadowParams = XMFLOAT4(1.0f / static_cast<float>(target_.GetWidth()), texelWorldSize_,
                                  settings_.strength, settings_.normalOffset);
}

void ShadowMap::Bind(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !sampler_) return;
    ID3D11ShaderResourceView* view = hasShadows_ ? target_.GetShaderView() : nullptr;
    ctx->PSSetShaderResources(Slots::kSrvShadow, 1, &view);
    ctx->PSSetSamplers(Slots::kSamplerShadow, 1, &sampler_);
}
