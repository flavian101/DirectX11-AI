#pragma once
#include "Model.h"
#include "Shader.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <memory>
#include <vector>

// Data that changes per draw call. Must match `cbuffer PerObject : register(b2)` in
// shaders/common.hlsli. View/projection/time are NOT here any more: they are the same
// for every object, so FrameConstants uploads them once per frame instead.
struct CBPerObject {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4 materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 materialParams = {};  // vs.hlsl: x = UV tiling
};
static_assert(sizeof(CBPerObject) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// Owns GPU draw state and the model it submits. Scene behavior belongs to SceneObject.
class Renderable {
public:
    Renderable(ID3D11Device* device, Model* model,
               const wchar_t* vsPath, const wchar_t* psPath);
    ~Renderable();

    bool IsValid() const { return valid_; }
    // `drawList` (optional): per-mesh level of detail, or Model::kSkipMesh (see Model::Draw).
    // `partMasks` (optional): per-mesh extra parts to draw (see Model::Draw).
    void Render(ID3D11DeviceContext* ctx, const std::vector<unsigned char>* drawList = nullptr,
                const std::vector<unsigned char>* partMasks = nullptr) const;
    // Same mesh and world matrix, but a different shader (used by the shadow pass).
    void RenderWith(ID3D11DeviceContext* ctx, const Shader& shader,
                    const std::vector<unsigned char>* drawList = nullptr,
                    const std::vector<unsigned char>* partMasks = nullptr) const;
    void SetWorld(const DirectX::XMFLOAT4X4& world) { if (model_) model_->SetWorld(world); }
    void SetMaterialColor(const DirectX::XMFLOAT4& color) { materialColor_ = color; }
    void SetMaterialParameters(const DirectX::XMFLOAT4& parameters) { materialParameters_ = parameters; }
    // A material-specific constant block (register b4). The caller defines the struct, the
    // shader declares the matching cbuffer; Renderable just stores the bytes and uploads them
    // when they change. Size is padded up to a multiple of 16 bytes.
    void SetMaterialBlock(const void* data, UINT byteSize);
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
    DirectX::XMFLOAT4 materialParameters_ = { 1.0f, 1.0f, 1.0f, 1.0f };   // x = UV tiling for vs.hlsl
    ID3D11Device* device_ = nullptr;
    std::vector<unsigned char> materialData_;          // CPU copy of the material block
    mutable ID3D11Buffer* materialCb_ = nullptr;       // created lazily at first draw
    mutable bool materialDirty_ = false;
};


