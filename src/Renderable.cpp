#include "Renderable.h"
#include "Logger.h"
#include "ShaderSlots.h"

using namespace DirectX;

Renderable::Renderable(ID3D11Device* device, Model* model,
                       const wchar_t* vsPath, const wchar_t* psPath)
    : model_(model)
{
    if (!device || !model_) {
        LOG_ERROR("Renderable requires a valid device and model");
        return;
    }
    shader_ = ShaderCache::Get(device, vsPath, psPath);
    if (!shader_) {
        LOG_ERROR("Failed to load renderable shaders");
        return;
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(CBPerObject);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT hr = device->CreateBuffer(&desc, nullptr, &cb_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Create per-object constant buffer failed");
        return;
    }
    valid_ = true;
}

Renderable::~Renderable()
{
    if (cb_) cb_->Release();
}

void Renderable::Render(ID3D11DeviceContext* ctx) const
{
    if (valid_ && shader_) RenderWith(ctx, *shader_);
}

void Renderable::RenderWith(ID3D11DeviceContext* ctx, const Shader& shader) const
{
    if (!valid_ || !ctx) return;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = ctx->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        LOG_HR(hr, "Map per-object constant buffer failed");
        return;
    }
    auto* data = static_cast<CBPerObject*>(mapped.pData);

    // Transpose before upload: XMMATRIX is row-major, HLSL reads column-major.
    XMStoreFloat4x4(&data->world, XMMatrixTranspose(XMLoadFloat4x4(&model_->GetWorld())));
    data->materialColor = materialColor_;
    data->materialParams = materialParameters_;
    data->materialExtra = materialExtra_;

    ctx->Unmap(cb_, 0);
    shader.Apply(ctx);
    // Bind to BOTH stages: the water/skybox pixel shaders read materialColor from here.
    // (An unbound constant buffer reads as zeros - that was the original black-sky /
    // invisible-water bug.)
    ctx->VSSetConstantBuffers(Slots::kCbPerObject, 1, &cb_);
    ctx->PSSetConstantBuffers(Slots::kCbPerObject, 1, &cb_);
    model_->Draw(ctx);
}
