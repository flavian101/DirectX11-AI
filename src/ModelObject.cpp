#include "ModelObject.h"
#include "Logger.h"
#include "Mesh.h"
#include "MeshLod.h"
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

std::unique_ptr<SurfaceMaterial> BuildSurfaceMaterial(ID3D11Device* device, const ImportedModel& imported, const ImportedMaterial& source)
{
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
    return material;
}

void ModelRenderSettings::Clamp()
{
    lodBias = std::clamp(lodBias, kBiasMin, kBiasMax);
    lodScreen[0] = std::clamp(lodScreen[0], 0.003f, 1.0f);
    lodScreen[1] = std::clamp(lodScreen[1], 0.002f, lodScreen[0] * 0.95f);   // each level starts below the one before it
    lodScreen[2] = std::clamp(lodScreen[2], 0.001f, lodScreen[1] * 0.95f);
    cullScreen = std::clamp(cullScreen, 0.0f, lodScreen[2]);
}

ModelObject::ModelObject(ID3D11Device* device, ImportedModel&& imported)
    : device_(device), displayName_(imported.name), sourcePath_(imported.sourcePath),
      triangleCount_(imported.triangleCount), boundsMin_(imported.boundsMin), boundsMax_(imported.boundsMax)
{
    XMStoreFloat4x4(&world_, XMMatrixIdentity());
    pivot_ = XMFLOAT3((boundsMin_.x + boundsMax_.x) * 0.5f, boundsMin_.y, (boundsMin_.z + boundsMax_.z) * 0.5f);

    // One SurfaceMaterial per imported material (parts that share a material share its settings, too).
    for (const ImportedMaterial& source : imported.materials) {
        materials_.push_back(BuildSurfaceMaterial(device, imported, source));
        materialNames_.push_back(source.name.empty() ? "Material" : source.name);
    }

    // One Renderable per part: its own vertex data and constant buffers, drawn with the shared surface shaders.
    std::vector<size_t> order(imported.parts.size());
    std::iota(order.begin(), order.end(), size_t{ 0 });
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return imported.parts[a].material < imported.parts[b].material; });
    // Authored levels of detail: parts named "Pine_LOD0", "Pine_LOD1"... Only if at least two different numbers occur; a
    // model with a single "LOD0" mesh is just a model.
    std::vector<int> groupOf(imported.parts.size(), -1);
    for (size_t i = 0; i < imported.parts.size(); ++i) {
        const int tag = ParseLodIndex(imported.parts[i].name);
        if (tag >= 0) groupOf[i] = (std::min)(tag, 99);
    }
    for (const int group : groupOf) if (group >= 0 && std::find(authoredGroups_.begin(), authoredGroups_.end(), group) == authoredGroups_.end()) authoredGroups_.push_back(group);
    std::sort(authoredGroups_.begin(), authoredGroups_.end());
    if (authoredGroups_.size() < 2) { authoredGroups_.clear(); std::fill(groupOf.begin(), groupOf.end(), -1); }
    if (authoredGroups_.size() > static_cast<size_t>(kMaxLodLevels)) authoredGroups_.resize(kMaxLodLevels);

    for (const size_t index : order) {
        const ImportedPart& part = imported.parts[index];
        Part built;
        built.lodGroup = groupOf[index];

        // The part's own box (model space): what per-part culling and level of detail measure.
        if (!part.vertices.empty()) {
            built.boundsMin = built.boundsMax = part.vertices[0].position;
            for (const Vertex& v : part.vertices) {
                built.boundsMin = XMFLOAT3((std::min)(built.boundsMin.x, v.position.x), (std::min)(built.boundsMin.y, v.position.y), (std::min)(built.boundsMin.z, v.position.z));
                built.boundsMax = XMFLOAT3((std::max)(built.boundsMax.x, v.position.x), (std::max)(built.boundsMax.y, v.position.y), (std::max)(built.boundsMax.z, v.position.z));
            }
        }

        // Levels of detail: the same vertices, fewer triangles (MeshLod.h). Skipped for authored levels (the artist made
        // them) and for tiny meshes. Even level 0 is re-ordered for the GPU's vertex cache.
        std::vector<std::vector<std::uint32_t>> levels;
        if (!part.vertices.empty() && !part.indices.empty()) {
            MeshLodSettings lodSettings;
            if (built.lodGroup >= 0) lodSettings.levels = 1;
            levels = BuildMeshLods(&part.vertices[0].position.x, sizeof(Vertex), &part.vertices[0].normal.x, sizeof(Vertex), 5,
                                   part.vertices.size(), part.indices.data(), part.indices.size(), lodSettings);
        }
        Mesh* mesh = levels.empty() ? new Mesh(device, part.vertices, part.indices) : new Mesh(device, part.vertices, levels);
        built.levels = (std::max)(1, static_cast<int>(levels.size()));
        for (int level = 0; level < built.levels && level < kMaxLodLevels; ++level) {
            built.levelTriangles[level] = static_cast<std::uint32_t>((levels.empty() ? part.indices.size() : levels[level].size()) / 3);
        }
        generatedLevels_ = (std::max)(generatedLevels_, built.levels);

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

    // World-space boxes for culling: the centre goes through the matrix, the half-size is spread through |matrix|.
    auto boxOf = [&](const XMFLOAT3& lo, const XMFLOAT3& hi, XMFLOAT3& outMin, XMFLOAT3& outMax, XMFLOAT3& outCenter, float& outRadius) {
        const XMMATRIX m = XMLoadFloat4x4(&world_);
        const XMVECTOR vlo = XMLoadFloat3(&lo), vhi = XMLoadFloat3(&hi);
        const XMVECTOR half = (vhi - vlo) * 0.5f;
        const XMVECTOR center = XMVector3Transform((vlo + vhi) * 0.5f, m);
        const XMVECTOR extent = XMVectorScale(XMVectorAbs(m.r[0]), XMVectorGetX(half)) + XMVectorScale(XMVectorAbs(m.r[1]), XMVectorGetY(half)) +
                                XMVectorScale(XMVectorAbs(m.r[2]), XMVectorGetZ(half));
        XMStoreFloat3(&outMin, center - extent);
        XMStoreFloat3(&outMax, center + extent);
        XMStoreFloat3(&outCenter, center);
        // A sphere around the (possibly rotated) box: half its model-space diagonal, stretched by the largest scale.
        outRadius = XMVectorGetX(XMVector3Length(half)) * (std::max)({ std::fabs(scale_.x), std::fabs(scale_.y), std::fabs(scale_.z) });
    };
    for (Part& part : parts_) boxOf(part.boundsMin, part.boundsMax, part.worldMin, part.worldMax, part.worldCenter, part.worldRadius);
    boxOf(boundsMin_, boundsMax_, worldMin_, worldMax_, worldCenter_, worldRadius_);
}

bool ModelObject::GetOrientedBounds(XMFLOAT4X4& world, XMFLOAT3& boundsMin, XMFLOAT3& boundsMax) const
{
    world = world_;
    boundsMin = boundsMin_;
    boundsMax = boundsMax_;
    return true;
}

std::unique_ptr<SceneObject> ModelObject::Clone(ID3D11Device* device) const
{
    ImportedModel imported;
    std::string error;
    if (!ImportModel(sourcePath_, imported, error)) {
        LOG_ERROR("Duplicate failed, could not read the model again: " + error);
        return nullptr;
    }
    auto copy = std::make_unique<ModelObject>(device, std::move(imported));
    if (!copy->IsValid()) return nullptr;
    copy->position_ = position_;
    copy->rotation_ = rotation_;
    copy->scale_ = scale_;
    copy->renderSettings_ = renderSettings_;
    const size_t shared = (std::min)(materials_.size(), copy->materials_.size());
    for (size_t i = 0; i < shared; ++i) copy->materials_[i]->Settings() = materials_[i]->Settings();
    copy->UpdateWorldTransform();
    return copy;
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

void ModelObject::RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const
{
    DrawParts(ctx, &shader, view);
}

void ModelObject::Render(ID3D11DeviceContext* ctx, const RenderView& view) const
{
    DrawParts(ctx, nullptr, view);
}

void ModelObject::DrawParts(ID3D11DeviceContext* ctx, const Shader* shadowShader, const RenderView& view) const
{
    const int kind = static_cast<int>(view.kind);
    ModelViewStats& stats = stats_[kind];
    stats = {};
    stats.partsTotal = static_cast<int>(parts_.size());
    if (parts_.empty()) return;
    const ModelRenderSettings& settings = renderSettings_;

    // 1. The whole model first: one box test can save looking at every part (a castle off to the side).
    if (settings.frustumCulling && !view.frustum.IntersectsAABB(worldMin_, worldMax_)) {
        partLod_[kind].assign(parts_.size(), 255);
        modelLod_[kind] = 255;
        return;
    }

    auto distanceTo = [&](const XMFLOAT3& p) {
        const float dx = p.x - view.lodOrigin.x, dy = p.y - view.lodOrigin.y, dz = p.z - view.lodOrigin.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    // The sun's pass draws shadows, which nobody studies up close: one level coarser than the lit draw.
    const int shadowBias = shadowShader ? 1 : 0;

    // 2. Authored levels ("Pine_LOD0".."Pine_LOD3") are separate meshes, so the CHOICE is for the whole model.
    int authoredLevel = 0;
    if (HasAuthoredLods()) {
        const int levelCount = static_cast<int>(authoredGroups_.size());
        if (settings.lodEnabled) {
            const float screen = ScreenFraction(worldRadius_, distanceTo(worldCenter_), view.projScale) * settings.lodBias;
            if (BelowCullSize(screen, settings.cullScreen)) { modelLod_[kind] = 255; return; }
            const int level = SelectLod(screen, settings.lodScreen.data(), levelCount, modelLod_[kind] == 255 ? -1 : modelLod_[kind]);
            modelLod_[kind] = static_cast<unsigned char>(level);
            authoredLevel = (std::min)(level + shadowBias, levelCount - 1);
        }
    }
    const int authoredGroup = HasAuthoredLods() ? authoredGroups_[authoredLevel] : -1;

    std::vector<unsigned char>& remembered = partLod_[kind];
    if (remembered.size() != parts_.size()) remembered.assign(parts_.size(), 255);
    if (drawList_.size() != 1) drawList_.assign(1, 0);

    int boundMaterial = -1;
    ID3D11RasterizerState* previousRaster = nullptr;   // the state to restore if a double-sided part switched it
    bool rasterChanged = false, doubleSidedNow = false;
    for (size_t i = 0; i < parts_.size(); ++i) {
        const Part& part = parts_[i];
        // A part of another authored level is simply not part of this picture (an untagged part belongs to every level).
        if (authoredGroup >= 0 && part.lodGroup >= 0 && part.lodGroup != authoredGroup) { remembered[i] = 255; continue; }
        // 3. Can this view see the part?
        if (settings.frustumCulling && !view.frustum.IntersectsAABB(part.worldMin, part.worldMax)) { remembered[i] = 255; continue; }

        // 4. How much detail does it deserve? (Only generated levels: authored ones were chosen above.)
        int lod = 0;
        if (settings.lodEnabled && part.levels > 1 && authoredGroup < 0) {
            const float screen = ScreenFraction(part.worldRadius, distanceTo(part.worldCenter), view.projScale) * settings.lodBias;
            if (BelowCullSize(screen, settings.cullScreen)) { remembered[i] = 255; continue; }
            lod = SelectLod(screen, settings.lodScreen.data(), part.levels, remembered[i] == 255 ? -1 : remembered[i]);
            remembered[i] = static_cast<unsigned char>(lod);
            lod = (std::min)(lod + shadowBias, part.levels - 1);
        } else if (settings.lodEnabled && settings.cullScreen > 0.0f) {
            const float screen = ScreenFraction(part.worldRadius, distanceTo(part.worldCenter), view.projScale) * settings.lodBias;
            if (BelowCullSize(screen, settings.cullScreen)) continue;
        }
        drawList_[0] = static_cast<unsigned char>(lod);

        // 5. Draw. The shadow pass has no materials: just the geometry with its depth-only shader.
        if (shadowShader) {
            part.renderable->RenderWith(ctx, *shadowShader, &drawList_);
        } else {
            SurfaceMaterial& material = *materials_[part.material];
            if (part.material != boundMaterial) {   // parts are sorted by material, so this changes rarely
                material.Bind(ctx);
                boundMaterial = part.material;
            }
            material.Upload(*part.renderable);
            if (part.doubleSided != doubleSidedNow) {   // switch the rasterizer only when the kind of part changes
                if (!rasterChanged) { ctx->RSGetState(&previousRaster); rasterChanged = true; }
                ctx->RSSetState(part.doubleSided ? GetCullNone() : previousRaster);
                doubleSidedNow = part.doubleSided;
            }
            part.renderable->Render(ctx, &drawList_);
        }
        ++stats.partsDrawn;
        ++stats.partsPerLod[(std::min)(lod, kMaxLodLevels - 1)];
        stats.trianglesDrawn += static_cast<int>(part.levelTriangles[(std::min)(lod, kMaxLodLevels - 1)]);
    }
    if (rasterChanged) {
        ctx->RSSetState(previousRaster);       // put back whatever was set (null = the default)
        if (previousRaster) previousRaster->Release();   // RSGetState added a reference
    }
}
