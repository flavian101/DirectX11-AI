#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "SurfaceMaterial.h"
#include <DirectXMath.h>
#include <memory>

class Cube final : public SceneObject {
public:
    explicit Cube(ID3D11Device* device);
    const char* GetEditorName() const override { return "Cube"; }
    DirectX::XMFLOAT3 GetSortPosition() const override { return position_; }
    bool GetEditorTransform(DirectX::XMFLOAT3& position,
                            DirectX::XMFLOAT3& rotation,
                            DirectX::XMFLOAT3& scale) const override;
    bool SetEditorTransform(const DirectX::XMFLOAT3& position,
                            const DirectX::XMFLOAT3& rotation,
                            const DirectX::XMFLOAT3& scale) override;
    bool CastsShadow() const override { return true; }
    bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
    void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;
    void Update(float dt) override;
    void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

    // The cube's maps and settings (the editor's Inspector edits these).
    SurfaceMaterial& GetSurface() { return *surface_; }

private:
    void UpdateWorldTransform();

    std::unique_ptr<Renderable> renderable_;
    std::unique_ptr<SurfaceMaterial> surface_;
    DirectX::XMFLOAT3 position_ = { 0.0f, 4.0f, 0.0f };
    DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
};