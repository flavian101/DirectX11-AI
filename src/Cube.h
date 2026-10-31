#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include <DirectXMath.h>
#include <memory>

class Cube final : public SceneObject {
public:
    explicit Cube(ID3D11Device* device);
    void Update(float dt) override;
    void Render(ID3D11DeviceContext* ctx, const Camera& camera) const override;

private:
    std::unique_ptr<Renderable> renderable_;
    float angle_ = 0.0f;
};
