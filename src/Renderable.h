#pragma once
#include "Model.h"
#include "Shader.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <memory>

// Data that changes per draw call. Must match `cbuffer PerObject : register(b2)` in
// shaders/common.hlsli. View/projection/time are NOT here any more: they are the same
// for every object, so FrameConstants uploads them once per frame instead.
struct CBPerObject {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4 materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 materialParams = {};  // meaning is defined by each shader
};
static_assert(sizeof(CBPerObject) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// Owns GPU draw state and the model it submits. Scene behavior belongs to SceneObject.
class Renderable {
public:
    Renderable(ID3D11Device* device, Model* model,
               const wchar_t* vsPath, const wchar_t* psPath);
    ~Renderable();

    bool IsValid() const { return valid_; }
    void Render(ID3D11DeviceContext* ctx) const;
    void SetWorld(const DirectX::XMFLOAT4X4& world) { if (model_) model_->SetWorld(world); }
    void SetMaterialColor(const DirectX::XMFLOAT4& color) { materialColor_ = color; }
    void SetMaterialParameters(const DirectX::XMFLOAT4& parameters) { materialParameters_ = parameters; }
    Model* GetModel() const { return model_.get(); }
    void SetTexture(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* texture)
    {
        if (shader_) shader_->SetTexture(ctx, texture);
    }

private:
    std::unique_ptr<Model> model_;
    std::shared_ptr<Shader> shader_;   // shared through ShaderCache
    ID3D11Buffer* cb_ = nullptr;
    bool valid_ = false;
    DirectX::XMFLOAT4 materialColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 materialParameters_ = {};
};


