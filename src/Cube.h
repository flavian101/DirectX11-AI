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
    bool GetOrientedBounds(DirectX::XMFLOAT4X4& world, DirectX::XMFLOAT3& boundsMin, DirectX::XMFLOAT3& boundsMax) const override;
    bool IsUserContent() const override { return true; }
    bool CanClone() const noexcept override { return true; }
    std::unique_ptr<SceneObject> Clone(ID3D11Device* device) const override;
    bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
    void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;
    void Update(float dt) override;
    void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

    // The cube's maps and settings (the editor's Inspector edits these).
    SurfaceMaterial& GetSurface() { return *surface_; }
    const SurfaceMaterial& GetSurface() const { return *surface_; }
    // The demo cube turns by itself. Moving it with the editor (gizmo or Inspector) switches that off.
    bool IsSpinning() const { return spinning_; }
    void SetSpinning(bool spinning) { spinning_ = spinning; }

private:
    void UpdateWorldTransform();

    std::unique_ptr<Renderable> renderable_;
    std::unique_ptr<SurfaceMaterial> surface_;
    DirectX::XMFLOAT3 position_ = { 0.0f, 4.0f, 0.0f };
    DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4X4 world_{};
    bool spinning_ = true;
};