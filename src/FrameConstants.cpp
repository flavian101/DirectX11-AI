#include "FrameConstants.h"
#include "Logger.h"
#include "ShaderSlots.h"

using namespace DirectX;

FrameConstants::~FrameConstants() { Release(); }

void FrameConstants::Release()
{
    if (cb_) { cb_->Release(); cb_ = nullptr; }
}

bool FrameConstants::Initialize(ID3D11Device* device)
{
    Release();
    if (!device) {
        LOG_ERROR("FrameConstants requires a valid device");
        return false;
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(CBPerFrame);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT hr = device->CreateBuffer(&desc, nullptr, &cb_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Create per-frame constant buffer failed");
        return false;
    }
    return true;
}

void FrameConstants::Apply(ID3D11DeviceContext* ctx, const Camera& camera, float timeSeconds) const
{
    if (!ctx || !cb_) return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = ctx->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        LOG_HR(hr, "Map per-frame constant buffer failed");
        return;
    }
    auto* data = static_cast<CBPerFrame*>(mapped.pData);

    // XMMATRIX is row-major; HLSL reads cbuffer matrices column-major by default.
    // Transposing on the CPU makes `mul(vector, matrix)` in the shaders correct.
    const XMFLOAT4X4 view = camera.GetViewMatrix();
    const XMFLOAT4X4 proj = camera.GetProjectionMatrix();
    XMStoreFloat4x4(&data->view, XMMatrixTranspose(XMLoadFloat4x4(&view)));
    XMStoreFloat4x4(&data->proj, XMMatrixTranspose(XMLoadFloat4x4(&proj)));

    const XMFLOAT3 pos = camera.GetPosition();
    data->cameraPos = XMFLOAT4(pos.x, pos.y, pos.z, 1.0f);
    data->frameParams = XMFLOAT4(timeSeconds, 0.0f, 0.0f, 0.0f);

    ctx->Unmap(cb_, 0);
    ctx->VSSetConstantBuffers(Slots::kCbPerFrame, 1, &cb_);
    ctx->PSSetConstantBuffers(Slots::kCbPerFrame, 1, &cb_);
}
