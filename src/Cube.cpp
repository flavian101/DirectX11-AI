#include "Cube.h"
#include "Logger.h"
#include "Mesh.h"
#include <vector>

using namespace DirectX;

static Model* CreateCubeModel(ID3D11Device* device)
{
    const std::vector<Vertex> vertices = {
        {{-1,-1, 1}, {0,0,1}, {0,0}}, {{1,-1, 1}, {0,0,1}, {1,0}},
        {{1,1,1}, {0,0,1}, {1,1}}, {{-1,1,1}, {0,0,1}, {0,1}},
        {{-1,-1,-1}, {0,0,-1}, {0,0}}, {{1,-1,-1}, {0,0,-1}, {1,0}},
        {{1,1,-1}, {0,0,-1}, {1,1}}, {{-1,1,-1}, {0,0,-1}, {0,1}}
    };
    const std::vector<uint32_t> indices = {
        0,1,2, 2,3,0, 4,6,5, 6,4,7,
        0,5,1, 5,0,4, 3,2,6, 6,7,3,
        1,5,6, 6,2,1, 0,7,4, 7,0,3
    };
    Mesh* mesh = new Mesh(device, vertices, indices);
    return new Model({mesh});
}

Cube::Cube(ID3D11Device* device)
{
    renderable_ = std::make_unique<Renderable>(device, CreateCubeModel(device),
                                                L"shaders/vs.hlsl", L"shaders/ps.hlsl");
    if (!renderable_->IsValid()) LOG_ERROR("Cube renderable initialization failed");
}

void Cube::Update(float dt)
{
    angle_ += XMConvertToRadians(45.0f) * dt;
    const XMMATRIX rotation = XMMatrixRotationY(angle_) * XMMatrixRotationX(angle_ * 0.5f);
    XMFLOAT4X4 world;
    XMStoreFloat4x4(&world, rotation);
    renderable_->SetWorld(world);
}

void Cube::Render(ID3D11DeviceContext* ctx, const Camera& camera) const
{
    if (renderable_)
        renderable_->Render(ctx, camera.GetViewMatrix(), camera.GetProjectionMatrix());
}

