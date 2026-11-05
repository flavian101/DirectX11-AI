#include "ModelObject.h"
#include "Logger.h"
#include "Mesh.h"
#include <algorithm>
#include <cmath>
#include <numeric>

using namespace DirectX;

namespace {
// A material with no texture still has colour. Turn a linear factor into a 1x1 sRGB texture of that colour.
std::unique_ptr<Texture> SolidFromLinear(ID3D11Device* device, const XMFLOAT3& linear)
{
    auto toByte = [](float c) {
        c = std::clamp(c, 0.0f, 1.0f);
        const float srgb = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(srgb * 255.0f + 0.5f);
    };
    return Texture::Solid(device, toByte(linear.x), toByte(linear.y), toByte(linear.z), 255, ColorSpace::SRGB);
}

std::unique_ptr<Texture> CreateMap(ID3D11Device* device, const ImportedModel& model, const ImportedTextureRef& ref,
                                   ColorSpace colorSpace)
{
    std::unique_ptr<Texture> texture;
    if (ref.embedded >= 0 && ref.embedded < static_cast<int>(model.images.size())) {
        const ImportedImage& image = model.images[ref.embedded];
        texture = Texture::FromPixels(device, image.width, image.height, image.rgba.data(), colorSpace);
    } else if (!ref.path.empty()) {
        texture = std::make_unique<Texture>(device, ref.path, colorSpace);
        if (!texture->IsValid()) texture.reset();
    }
    return texture;
}
}

ModelObject::ModelObject(ID3D11Device* device, ImportedModel&& imported)
    : device_(device), displayName_(imported.name), sourcePath_(imported.sourcePath),
      triangleCount_(imported.triangleCount), boundsMin_(imported.boundsMin), boundsMax_(imported.boundsMax)
{
    XMStoreFloat4x4(&world_, XMMatrixIdentity());
    pivot_ = XMFLOAT3((boundsMin_.x + boundsMax_.x) * 0.5f, boundsMin_.y, (boundsMin_.z + boundsMax_.z) * 0.5f);

    // One SurfaceMaterial per imported material (parts that share a material share its settings, too).
    for (const ImportedMaterial& source : imported.materials) {
        auto material = std::make_unique<SurfaceMaterial>(device);
        SurfaceSettings& settings = material->Settings();
        settings.baseColor = source.baseColor;
        settings.alphaCutoff = source.alphaCutoff;
        settings.flipGreen = true;   // glTF (and most files) use OpenGL-style normal maps; ModelImporter keeps them as they are

        material->SetAlbedo(CreateMap(device, imported, source.albedo, ColorSpace::SRGB));
        material->SetNormal(CreateMap(device, imported, source.normal, ColorSpace::Linear));
        material->SetOrm(CreateMap(device, imported, source.orm, ColorSpace::Linear));
        material->SetEmissive(CreateMap(device, imported, source.emissive, ColorSpace::SRGB));
        if (!material->HasEmissiveMap() && (source.emissiveColor.x > 0.0f || source.emissiveColor.y > 0.0f || source.emissiveColor.z > 0.0f)) {
            material->SetEmissive(SolidFromLinear(device, source.emissiveColor));   // an emissive COLOUR with no map
        }
        // glTF's rule: value = factor * texture. With an ORM map the factors scale it; without one they ARE the values.
        settings.roughnessScale = settings.fallbackRoughness = source.roughness;
        settings.metallicScale = settings.fallbackMetallic = source.metallic;
        materials_.push_back(std::move(material));
        materialNames_.push_back(source.name.empty() ? "Material" : source.name);
    }

    // One Renderable per part: its own vertex data and constant buffers, drawn with the shared surface shaders.
    std::vector<size_t> order(imported.parts.size());
    std::iota(order.begin(), order.end(), size_t{ 0 });
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return imported.parts[a].material < imported.parts[b].material; });
    for (const size_t index : order) {
        const ImportedPart& part = imported.parts[index];
        Mesh* mesh = new Mesh(device, part.vertices, part.indices);
        Part built;
        built.renderable = std::make_unique<Renderable>(device, new Model({ mesh }), L"shaders/vs.hlsl", L"shaders/ps.hlsl");
        built.material = (std::min)(part.material, static_cast<int>(materials_.size()) - 1);
        built.doubleSided = imported.materials.empty() ? false : imported.materials[part.material].doubleSided;
        if (!built.renderable->IsValid()) {
            LOG_ERROR("A model part failed to initialize: " + part.name);
            continue;
        }
        parts_.push_back(std::move(built));
    }
    UpdateWorldTransform();
}

ModelObject::~ModelObject()
{
    if (cullNone_) cullNone_->Release();
}

ID3D11RasterizerState* ModelObject::GetCullNone() const
{
    if (!cullNone_ && device_) {
        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;     // draw both faces: leaves, cloth, thin walls
        desc.DepthClipEnable = TRUE;
        device_->CreateRasterizerState(&desc, &cullNone_);
    }
    return cullNone_;
}

void ModelObject::FitToSize(float size)
{
    const float longest = (std::max)({ boundsMax_.x - boundsMin_.x, boundsMax_.y - boundsMin_.y, boundsMax_.z - boundsMin_.z });
    if (longest <= 1e-6f || size <= 0.0f) return;
    const float uniform = size / longest;
    scale_ = XMFLOAT3(uniform, uniform, uniform);
    UpdateWorldTransform();
}

bool ModelObject::GetEditorTransform(XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale) const
{
    position = position_;
    rotation = rotation_;
    scale = scale_;
    return true;
}

bool ModelObject::SetEditorTransform(const XMFLOAT3& position, const XMFLOAT3& rotation, const XMFLOAT3& scale)
{
    position_ = position;
    rotation_ = rotation;
    scale_ = scale;
    UpdateWorldTransform();
    return true;
}

void ModelObject::UpdateWorldTransform()
{
    // Move the pivot (bottom centre of the model) to the origin, THEN scale and rotate, THEN move to `position`:
    // scaling and turning happen around the model's own base, and "position" is where its base sits.
    const XMMATRIX transform = XMMatrixTranslation(-pivot_.x, -pivot_.y, -pivot_.z) *
        XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
        XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) * XMMatrixRotationZ(rotation_.z) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);
    XMStoreFloat4x4(&world_, transform);
    for (Part& part : parts_) part.renderable->SetWorld(world_);
}

bool ModelObject::GetBoundingSphere(XMFLOAT3& center, float& radius) const
{
    const XMFLOAT3 centre((boundsMin_.x + boundsMax_.x) * 0.5f, (boundsMin_.y + boundsMax_.y) * 0.5f, (boundsMin_.z + boundsMax_.z) * 0.5f);
    const XMVECTOR worldCentre = XMVector3TransformCoord(XMLoadFloat3(&centre), XMLoadFloat4x4(&world_));
    XMStoreFloat3(&center, worldCentre);
    const float halfDiagonal = 0.5f * std::sqrt((boundsMax_.x - boundsMin_.x) * (boundsMax_.x - boundsMin_.x) +
                                                (boundsMax_.y - boundsMin_.y) * (boundsMax_.y - boundsMin_.y) +
                                                (boundsMax_.z - boundsMin_.z) * (boundsMax_.z - boundsMin_.z));
    radius = halfDiagonal * (std::max)({ std::fabs(scale_.x), std::fabs(scale_.y), std::fabs(scale_.z) });
    return true;
}

void ModelObject::RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& /*view*/) const
{
    for (const Part& part : parts_) part.renderable->RenderWith(ctx, shader);
}

void ModelObject::Render(ID3D11DeviceContext* ctx, const RenderView& /*view*/) const
{
    int boundMaterial = -1;
    for (const Part& part : parts_) {
        SurfaceMaterial& material = *materials_[part.material];
        if (part.material != boundMaterial) {   // parts are sorted by material, so this changes rarely
            material.Bind(ctx);
            boundMaterial = part.material;
        }
        material.Upload(*part.renderable);

        if (part.doubleSided) {
            ID3D11RasterizerState* previous = nullptr;
            ctx->RSGetState(&previous);
            ctx->RSSetState(GetCullNone());
            part.renderable->Render(ctx);
            ctx->RSSetState(previous);          // put back whatever was set (null = the default)
            if (previous) previous->Release();  // RSGetState added a reference
        } else {
            part.renderable->Render(ctx);
        }
    }
}
