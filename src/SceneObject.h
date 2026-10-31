#pragma once
#include "Camera.h"
#include <d3d11.h>

class SceneObject {
public:
    virtual ~SceneObject() = default;
    virtual void Update(float dt) = 0;
    virtual void Render(ID3D11DeviceContext* ctx, const Camera& camera) const = 0;
};
