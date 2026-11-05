#include "Cube.h"
#include "Logger.h"
#include "Mesh.h"
#include <algorithm>
#include <cmath>
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
    ComputeTangents(vertices, indices);   // normal mapping needs a tangent per vertex (lesson 16)
    Mesh* mesh = new Mesh(device, vertices, indices);
    return new Model({mesh});
}

Cube::Cube(ID3D11Device* device)
{
    renderable_ = std::make_unique<Renderable>(device, CreateCubeModel(device),
                                                L"shaders/vs.hlsl", L"shaders/ps.hlsl");
    surface_ = std::make_unique<SurfaceMaterial>(device);
    surface_->LoadAlbedo("assets/broken_brick_wall_diff_1k.png");
    // Optional maps (Poly Haven names). Missing files fall back to neutral defaults, so the cube still renders.
    if (surface_->LoadNormal("assets/broken_brick_wall_nor_gl_1k.png")) {
        surface_->Settings().flipGreen = true;   // "_nor_gl_" = OpenGL convention. A "_nor_dx_" map would leave this false.
    }
    surface_->LoadOrm("assets/broken_brick_wall_arm_1k.png");   // "arm" = AO / roughness / metallic packed in R / G / B
    if (!renderable_->IsValid()) LOG_ERROR("Cube renderable initialization failed");
}

void Cube::Update(float dt)
{
    if (!spinning_) return;
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
    spinning_ = false;   // whoever sets the transform by hand wants the cube to stay where they put it
    UpdateWorldTransform();
    return true;
}

bool Cube::GetOrientedBounds(XMFLOAT4X4& world, XMFLOAT3& boundsMin, XMFLOAT3& boundsMax) const
{
    world = world_;
    boundsMin = XMFLOAT3(-1.0f, -1.0f, -1.0f);   // the cube's own vertices span [-1, 1] on each axis
    boundsMax = XMFLOAT3(1.0f, 1.0f, 1.0f);
    return true;
}

std::unique_ptr<SceneObject> Cube::Clone(ID3D11Device* device) const
{
    auto copy = std::make_unique<Cube>(device);
    copy->position_ = position_;
    copy->rotation_ = rotation_;
    copy->scale_ = scale_;
    copy->spinning_ = spinning_;
    copy->surface_->Settings() = surface_->Settings();
    copy->UpdateWorldTransform();
    return copy;
}

void Cube::UpdateWorldTransform()
{
    const XMMATRIX transform = XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
        XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) *
        XMMatrixRotationZ(rotation_.z) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);
    XMStoreFloat4x4(&world_, transform);
    if (renderable_) renderable_->SetWorld(world_);
}

bool Cube::GetBoundingSphere(XMFLOAT3& center, float& radius) const
{
    center = position_;
    // The cube spans [-1,1] on each axis, so its corner is sqrt(3) away from the centre.
    radius = 1.7320508f * (std::max)({ std::fabs(scale_.x), std::fabs(scale_.y), std::fabs(scale_.z) });
    return true;
}

void Cube::RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& /*view*/) const
{
    if (renderable_ && renderable_->IsValid()) renderable_->RenderWith(ctx, shader);
}

void Cube::Render(ID3D11DeviceContext* ctx, const RenderView& /*view*/) const
{
    if (renderable_ && surface_ && surface_->IsReady() && surface_->HasAlbedo()) {
        surface_->Bind(ctx);                  // albedo, normal, ORM and emissive maps
        surface_->Upload(*renderable_);       // strength, flip, roughness scale... into the material block (b4)
        renderable_->Render(ctx);
    }
}

