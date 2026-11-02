#pragma once
#include "Model.h"
#include "Shader.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <memory>

struct CBPerObject {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4 materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float time = 0.0f;
    DirectX::XMFLOAT3 materialParameters = {};
};

// Owns GPU draw state and the model it submits. Scene behavior belongs to SceneObject.
class Renderable {
public:
    Renderable(ID3D11Device* device, Model* model,
               const wchar_t* vsPath, const wchar_t* psPath);
    ~Renderable();

    bool IsValid() const { return valid_; }
    void Render(ID3D11DeviceContext* ctx, const DirectX::XMFLOAT4X4& view,
                const DirectX::XMFLOAT4X4& proj) const;
    void SetWorld(const DirectX::XMFLOAT4X4& world) { if (model_) model_->SetWorld(world); }
    void SetMaterialColor(const DirectX::XMFLOAT4& color) { materialColor_ = color; }
    void SetMaterialParameters(const DirectX::XMFLOAT3& parameters) { materialParameters_ = parameters; }
    void SetTime(float time) { time_ = time; }
    Model* GetModel() const { return model_.get(); }
    void SetTexture(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* texture)
    {
        shader_.SetTexture(ctx, texture);
    }

private:
    std::unique_ptr<Model> model_;
    Shader shader_;
    ID3D11Buffer* cb_ = nullptr;
    bool valid_ = false;
    DirectX::XMFLOAT4 materialColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float time_ = 0.0f;
    DirectX::XMFLOAT3 materialParameters_ = {};
};


