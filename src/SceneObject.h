#pragma once
#include "Camera.h"
#include "RenderView.h"
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

class Shader;

using SceneObjectId = std::uint64_t;

class SceneObject {
public:
    enum class RenderCategory { Opaque, Transparent, Background };
    virtual ~SceneObject() = default;

    SceneObjectId GetId() const noexcept { return id_; }
    const std::string& GetName() const noexcept { return name_; }
    void SetName(std::string name) { name_ = std::move(name); }

    virtual RenderCategory GetRenderCategory() const { return RenderCategory::Opaque; }
    virtual DirectX::XMFLOAT3 GetSortPosition() const { return {}; }
    virtual const char* GetEditorName() const { return "Scene Object"; }
    // Type identity is separate from the user-editable instance name.
    virtual const char* GetTypeName() const { return GetEditorName(); }
    virtual bool CanClone() const noexcept { return false; }
    virtual std::unique_ptr<SceneObject> Clone(ID3D11Device* device) const
    {
        (void)device;
        return nullptr;
    }

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

    // --- editor interaction ---
    // The object's box in its OWN space, plus the matrix that places it in the world. The editor uses it to pick
    // the object by clicking and to outline it when selected. False = cannot be picked by clicking.
    virtual bool GetOrientedBounds(DirectX::XMFLOAT4X4& world, DirectX::XMFLOAT3& boundsMin,
                                   DirectX::XMFLOAT3& boundsMax) const
    {
        (void)world;
        (void)boundsMin;
        (void)boundsMax;
        return false;
    }
    // Content a user creates and manages (cubes, imported models), as opposed to the scene's fixtures (terrain,
    // water, sky), which the editor will not delete or duplicate.
    virtual bool IsUserContent() const { return false; }

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
    virtual void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const
    {
        (void)ctx;
        (void)shader;
        (void)view;
    }

    virtual void Update(float dt) = 0;
    // `view` says what this draw can see (frustum) and where LOD distances start.
    virtual void Render(ID3D11DeviceContext* ctx, const RenderView& view) const = 0;

private:
    friend class Scene;

    SceneObjectId id_ = 0;
    std::string name_;
};
