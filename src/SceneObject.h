#pragma once
#include "Camera.h"
#include <d3d11.h>
#include <DirectXMath.h>

class SceneObject {
public:
    virtual ~SceneObject() = default;
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
    virtual void Update(float dt) = 0;
    virtual void Render(ID3D11DeviceContext* ctx, const Camera& camera) const = 0;
};
