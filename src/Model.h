#pragma once
#include "Mesh.h"
#include <DirectXMath.h>
#include <vector>

class Model {
public:
    explicit Model(const std::vector<Mesh*>& meshes);
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    void SetWorld(const DirectX::XMFLOAT4X4& world) { world_ = world; }
    const DirectX::XMFLOAT4X4& GetWorld() const { return world_; }
    void Draw(ID3D11DeviceContext* ctx) const;

private:
    std::vector<Mesh*> meshes_; // Model takes ownership of each mesh.
    DirectX::XMFLOAT4X4 world_;
};
