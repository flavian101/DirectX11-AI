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

FrameData FrameData::FromCamera(const Camera& camera, float timeSeconds)
{
    FrameData data;
    data.view = camera.GetViewMatrix();
    data.proj = camera.GetProjectionMatrix();
    data.cameraPos = camera.GetPosition();
    data.time = timeSeconds;
    return data;
}

void FrameConstants::Apply(ID3D11DeviceContext* ctx, const FrameData& frame) const
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
    XMStoreFloat4x4(&data->view, XMMatrixTranspose(XMLoadFloat4x4(&frame.view)));
    XMStoreFloat4x4(&data->proj, XMMatrixTranspose(XMLoadFloat4x4(&frame.proj)));
    data->cameraPos = XMFLOAT4(frame.cameraPos.x, frame.cameraPos.y, frame.cameraPos.z, 1.0f);
    data->frameParams = XMFLOAT4(frame.time, 0.0f, 0.0f, 0.0f);
    XMStoreFloat4x4(&data->lightViewProj, XMMatrixTranspose(XMLoadFloat4x4(&frame.lightViewProj)));
    data->shadowParams = frame.shadowParams;
    data->screenParams = XMFLOAT4(1.0f / frame.viewportWidth, 1.0f / frame.viewportHeight, frame.nearPlane, frame.farPlane);
    data->clipPlane = frame.clipPlane;
    data->reflectionParams = XMFLOAT4(frame.reflectionAvailable ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);

    ctx->Unmap(cb_, 0);
    ctx->VSSetConstantBuffers(Slots::kCbPerFrame, 1, &cb_);
    ctx->PSSetConstantBuffers(Slots::kCbPerFrame, 1, &cb_);
}
