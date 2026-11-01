#include "Cube.h"
#include "Logger.h"
#include "Mesh.h"
#include <vector>

using namespace DirectX;

static Model* CreateCubeModel(ID3D11Device* device)
{
    std::vector<Vertex> vertices =
    {
        // Front face
        Vertex({ -1.0f, -1.0f, -1.0f },{ 0.0f, 0.0f,-1.0f },{ 0.0f, 1.0f }),
        Vertex({ -1.0f,  1.0f, -1.0f },{ 0.0f, 0.0f,-1.0f },{ 0.0f, 0.0f }),
        Vertex({ 1.0f,  1.0f, -1.0f },{ 0.0f, 0.0f,-1.0f },{ 1.0f, 0.0f }),
        Vertex({ 1.0f, -1.0f, -1.0f },{ 0.0f, 0.0f,-1.0f },{ 1.0f, 1.0f }),

        // Back face
        Vertex({ 1.0f, -1.0f,  1.0f },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 1.0f }),
        Vertex({ 1.0f,  1.0f,  1.0f },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 0.0f }),
        Vertex({ -1.0f,  1.0f,  1.0f },{ 0.0f, 0.0f, 1.0f },{ 1.0f, 0.0f }),
        Vertex({ -1.0f, -1.0f,  1.0f },{ 0.0f, 0.0f, 1.0f },{ 1.0f, 1.0f }),

        // Left face
        Vertex({ -1.0f, -1.0f,  1.0f },{ -1.0f, 0.0f, 0.0f },{ 0.0f, 1.0f }),
        Vertex({ -1.0f,  1.0f,  1.0f },{ -1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f }),
        Vertex({ -1.0f,  1.0f, -1.0f },{ -1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f }),
        Vertex({ -1.0f, -1.0f, -1.0f },{ -1.0f, 0.0f, 0.0f },{ 1.0f, 1.0f }),

        // Right face
        Vertex({ 1.0f, -1.0f, -1.0f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 1.0f }),
        Vertex({ 1.0f,  1.0f, -1.0f },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f }),
        Vertex({ 1.0f,  1.0f,  1.0f },{ 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f }),
        Vertex({ 1.0f, -1.0f,  1.0f },{ 1.0f, 0.0f, 0.0f },{ 1.0f, 1.0f }),

        // Top face
        Vertex({ -1.0f,  1.0f, -1.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f }),
        Vertex({ -1.0f,  1.0f,  1.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f }),
        Vertex({ 1.0f,  1.0f,  1.0f },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 0.0f }),
        Vertex({ 1.0f,  1.0f, -1.0f },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 1.0f }),

        // Bottom face
        Vertex({ -1.0f, -1.0f,  1.0f },{ 0.0f,-1.0f, 0.0f },{ 0.0f, 1.0f }),
        Vertex({ -1.0f, -1.0f, -1.0f },{ 0.0f,-1.0f, 0.0f },{ 0.0f, 0.0f }),
        Vertex({ 1.0f, -1.0f, -1.0f },{ 0.0f,-1.0f, 0.0f },{ 1.0f, 0.0f }),
        Vertex({ 1.0f, -1.0f,  1.0f },{ 0.0f,-1.0f, 0.0f },{ 1.0f, 1.0f })
    };

    std::vector<UINT> indices =
    {
        // Front
        0,1,2,
        0,2,3,

        // Back
        4,5,6,
        4,6,7,

        // Left
        8,9,10,
        8,10,11,

        // Right
        12,13,14,
        12,14,15,

        // Top
        16,17,18,
        16,18,19,

        // Bottom
        20,21,22,
        20,22,23
    };
    Mesh* mesh = new Mesh(device, vertices, indices);
    return new Model({mesh});
}

Cube::Cube(ID3D11Device* device)
{
    renderable_ = std::make_unique<Renderable>(device, CreateCubeModel(device),
                                                L"shaders/vs.hlsl", L"shaders/ps.hlsl");
    texture_ = std::make_unique<Texture>(device, "assets/broken_brick_wall_diff_1k.png");
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
    if (renderable_ && texture_ && texture_->IsValid()) {
        renderable_->SetTexture(ctx, texture_->GetShaderResourceView());
        renderable_->Render(ctx, camera.GetViewMatrix(), camera.GetProjectionMatrix());
    }
}

