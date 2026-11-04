#include "Renderable.h"
#include "Logger.h"
#include "ShaderSlots.h"
#include <cstring>

using namespace DirectX;

Renderable::Renderable(ID3D11Device* device, Model* model,
                       const wchar_t* vsPath, const wchar_t* psPath)
    : model_(model), device_(device)
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
    if (materialCb_) materialCb_->Release();
}

void Renderable::SetMaterialBlock(const void* data, UINT byteSize)
{
    if (!data || byteSize == 0) return;
    const UINT padded = (byteSize + 15u) & ~15u;                 // constant buffers are sized in 16-byte units
    if (materialData_.size() != padded) {                          // layout changed: the GPU buffer must too
        materialData_.assign(padded, 0);
        if (materialCb_) { materialCb_->Release(); materialCb_ = nullptr; }
    }
    std::memcpy(materialData_.data(), data, byteSize);
    materialDirty_ = true;
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

    ctx->Unmap(cb_, 0);

    // The material's own block, if it has one: created on first use, re-uploaded only when changed.
    if (!materialData_.empty()) {
        if (!materialCb_) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = static_cast<UINT>(materialData_.size());
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(&desc, nullptr, &materialCb_))) {
                LOG_ERROR("Create material constant buffer failed");
            } else {
                materialDirty_ = true;
            }
        }
        if (materialCb_) {
            if (materialDirty_) {
                D3D11_MAPPED_SUBRESOURCE materialMapped{};
                if (SUCCEEDED(ctx->Map(materialCb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &materialMapped))) {
                    std::memcpy(materialMapped.pData, materialData_.data(), materialData_.size());
                    ctx->Unmap(materialCb_, 0);
                    materialDirty_ = false;
                }
            }
            ctx->VSSetConstantBuffers(Slots::kCbMaterial, 1, &materialCb_);
            ctx->PSSetConstantBuffers(Slots::kCbMaterial, 1, &materialCb_);
        }
    }
    shader.Apply(ctx);
    // Bind to BOTH stages: the water/skybox pixel shaders read materialColor from here.
    // (An unbound constant buffer reads as zeros - that was the original black-sky /
    // invisible-water bug.)
    ctx->VSSetConstantBuffers(Slots::kCbPerObject, 1, &cb_);
    ctx->PSSetConstantBuffers(Slots::kCbPerObject, 1, &cb_);
    model_->Draw(ctx);
}
