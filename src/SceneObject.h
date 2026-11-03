#pragma once
#include "Camera.h"
#include <d3d11.h>
#include <DirectXMath.h>

class Shader;

class SceneObject {
public:
    enum class RenderCategory { Opaque, Transparent, Background };
    virtual ~SceneObject() = default;
    virtual RenderCategory GetRenderCategory() const { return RenderCategory::Opaque; }
    virtual DirectX::XMFLOAT3 GetSortPosition() const { return {}; }
    virtual const char* GetEditorName() const { return "Scene Object"; }
    virtual bool GetEditorTransform(DirectX::XMFLOAT3& position,
                                    DirectX::XMFLOAT3& rotation,
                                    DirectX::XMFLOAT3& scale) const
    {
        (void)position;
        (void)rotation;
        (void)scale;
        return false;
    }
    virtual bool SetEditorTransform(const DirectX::XMFLOAT3& position,
                                    const DirectX::XMFLOAT3& rotation,
                                    const DirectX::XMFLOAT3& scale)
    {
        (void)position;
        (void)rotation;
        (void)scale;
        return false;
    }

    // --- shadow casting (opt-in: most objects, like water and the sky, cast none) ---
    virtual bool CastsShadow() const { return false; }
    // A sphere that fully contains the object; the scene merges these to size the sun's view.
    virtual bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const
    {
        (void)center;
        (void)radius;
        return false;
    }
    // Draw geometry only, with the depth-only shader the shadow pass supplies.
    virtual void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader) const
    {
        (void)ctx;
        (void)shader;
    }

    virtual void Update(float dt) = 0;
    virtual void Render(ID3D11DeviceContext* ctx, const Camera& camera) const = 0;
};
