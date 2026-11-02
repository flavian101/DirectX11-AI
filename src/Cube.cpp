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
    rotation_.y += XMConvertToRadians(45.0f) * dt;
    rotation_.x += XMConvertToRadians(22.5f) * dt;
    UpdateWorldTransform();
}

bool Cube::GetEditorTransform(XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale) const
{
    position = position_;
    rotation = rotation_;
    scale = scale_;
    return true;
}

bool Cube::SetEditorTransform(const XMFLOAT3& position, const XMFLOAT3& rotation,
                              const XMFLOAT3& scale)
{
    position_ = position;
    rotation_ = rotation;
    scale_ = scale;
    UpdateWorldTransform();
    return true;
}

void Cube::UpdateWorldTransform()
{
    const XMMATRIX transform = XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
        XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) *
        XMMatrixRotationZ(rotation_.z) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);
    XMFLOAT4X4 world;
    XMStoreFloat4x4(&world, transform);
    if (renderable_) renderable_->SetWorld(world);
}

void Cube::Render(ID3D11DeviceContext* ctx, const Camera& camera) const
{
    if (renderable_ && texture_ && texture_->IsValid()) {
        renderable_->SetTexture(ctx, texture_->GetShaderResourceView());
        renderable_->Render(ctx);
    }
}

