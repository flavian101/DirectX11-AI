#include "ModelObject.h"
#include "Logger.h"
#include "Mesh.h"
#include "MeshLod.h"
#include "Water.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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

void FloatingSettings::Clamp()
{
    draft = std::clamp(draft, 0.0f, 0.9f);
    responsiveness = std::clamp(responsiveness, 0.3f, 20.0f);
    damping = std::clamp(damping, 0.1f, 1.5f);
    tilt = std::clamp(tilt, 0.0f, 1.0f);
    maskPart = (std::max)(maskPart, -1);
    if (maskHeight >= 0.0f) maskHeight = std::clamp(maskHeight, 0.0f, 1.5f);
    maskInset = std::clamp(maskInset, 0.0f, 0.4f);
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
        built.name = part.name;
        built.lodGroup = groupOf[index];

        // The part seen from above: its convex outline. Only the hull mask uses it (lesson 33), and only for the part the
        // user calls the hull, so it is kept small: a handful of points instead of every vertex.
        {
            std::vector<HullMask::Point2> flat;
            flat.reserve(part.vertices.size());
            for (const Vertex& v : part.vertices) flat.push_back({ v.position.x, v.position.z });
            built.footprint = HullMask::ConvexHull(std::move(flat));
            built.footprintArea = HullMask::Area(built.footprint);
        }

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
    // The hull, unless the user says otherwise: the part with the biggest outline (oars and masts have thin ones).
    for (size_t i = 0; i < parts_.size(); ++i) {
        if (parts_[i].footprint.size() >= 3 && (autoHullPart_ < 0 || parts_[i].footprintArea > parts_[autoHullPart_].footprintArea)) autoHullPart_ = static_cast<int>(i);
    }
    UpdateWorldTransform();
}

ModelObject::~ModelObject()
{
    if (cullNone_) cullNone_->Release();
    if (maskBlend_) maskBlend_->Release();
    if (maskDepth_) maskDepth_->Release();
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
    if (floating_.enabled && floatReady_) position.y = floatHeave_.value;   // the gizmo sits where the boat is, not where it was put
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
    // A floating model also leans with the water: a turn that takes "up" to the surface's normal, applied in WORLD axes
    // (after the model's own rotation, around its base), and its height comes from the spring instead of from `position`.
    XMFLOAT3 position = position_;
    XMMATRIX lean = XMMatrixIdentity();
    if (floating_.enabled && floatReady_) {
        position.y = floatHeave_.value;
        float normal[3], m[9];
        Buoyancy::Normal(floatSlopeX_.value, floatSlopeZ_.value, floating_.tilt, normal);
        Buoyancy::RotationFromUp(normal, m);
        lean = XMMATRIX(m[0], m[1], m[2], 0.0f, m[3], m[4], m[5], 0.0f, m[6], m[7], m[8], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    const XMMATRIX transform = XMMatrixTranslation(-pivot_.x, -pivot_.y, -pivot_.z) *
        XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
        XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) * XMMatrixRotationZ(rotation_.z) *
        lean *
        XMMatrixTranslation(position.x, position.y, position.z);
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
    if ((settings.frustumCulling && !view.frustum.IntersectsAABB(worldMin_, worldMax_)) ||
        (view.clipBelow && worldMax_.y < view.clipBelowY)) {   // (or wholly under the mirror's clip plane)
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
        if (view.clipBelow && part.worldMax.y < view.clipBelowY) { remembered[i] = 255; continue; }   // wholly under the mirror's clip plane

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

// ---------------------------------------------------------------------------------------------------------------------
// Floating (lesson 33)
// ---------------------------------------------------------------------------------------------------------------------

void ModelObject::SetFloatingSettings(FloatingSettings settings)
{
    settings.Clamp();
    const bool wasEnabled = floating_.enabled;
    floating_ = settings;
    if (!floating_.enabled && (wasEnabled || floatReady_)) {
        floatReady_ = false;   // back to the author's transform
        UpdateWorldTransform();
    }
}

void ModelObject::StepFloating(const Water* water, float time, float dt)
{
    if (!floating_.enabled) return;
    const float height = GetModelHeight();

    float targetHeave = position_.y;   // no water here: stay at the height the model already has
    float slopeX = 0.0f, slopeZ = 0.0f;
    if (water) {
        // Where to read the water: a 3 x 3 grid over the hull's footprint (its box, pulled in a little), turned and scaled
        // like the model's own heading. The lean must not feed back into where we look, so the lean is left out here.
        const int part = ResolveMaskPart();
        const XMFLOAT3 lo = part >= 0 ? parts_[part].boundsMin : boundsMin_;
        const XMFLOAT3 hi = part >= 0 ? parts_[part].boundsMax : boundsMax_;
        const XMMATRIX heading = XMMatrixTranslation(-pivot_.x, -pivot_.y, -pivot_.z) *
            XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
            XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) * XMMatrixRotationZ(rotation_.z) *
            XMMatrixTranslation(position_.x, position_.y, position_.z);
        const float cx = (lo.x + hi.x) * 0.5f, cz = (lo.z + hi.z) * 0.5f;
        const float hx = (hi.x - lo.x) * 0.5f * 0.7f, hz = (hi.z - lo.z) * 0.5f * 0.7f;
        std::vector<Buoyancy::Sample> samples;
        samples.reserve(9);
        for (int gx = -1; gx <= 1; ++gx) {
            for (int gz = -1; gz <= 1; ++gz) {
                XMFLOAT3 world;
                XMStoreFloat3(&world, XMVector3Transform(XMVectorSet(cx + gx * hx, boundsMin_.y, cz + gz * hz, 1.0f), heading));
                samples.push_back({ world.x, world.z, water->SampleHeight(world.x, world.z, time) });
            }
        }
        const Buoyancy::Plane plane = Buoyancy::FitPlane(samples);
        targetHeave = plane.mean - floating_.draft * height;
        slopeX = plane.slopeX;
        slopeZ = plane.slopeZ;
    }

    if (!floatReady_) {   // the first step: begin at the water, do not fall or rise to it from the author's height
        floatHeave_.Snap(targetHeave);
        floatSlopeX_.Snap(slopeX);
        floatSlopeZ_.Snap(slopeZ);
        floatReady_ = true;
    } else {
        floatHeave_.Step(targetHeave, floating_.responsiveness, floating_.damping, dt);
        floatSlopeX_.Step(slopeX, floating_.responsiveness, floating_.damping, dt);
        floatSlopeZ_.Step(slopeZ, floating_.responsiveness, floating_.damping, dt);
    }
    UpdateWorldTransform();
}

int ModelObject::ResolveMaskPart() const
{
    if (floating_.maskPart >= 0 && floating_.maskPart < static_cast<int>(parts_.size()) && parts_[floating_.maskPart].footprint.size() >= 3) return floating_.maskPart;
    return autoHullPart_;
}

void ModelObject::EnsureHullMask(int part) const
{
    const Part& hull = parts_[part];
    const float modelHeight = boundsMax_.y - boundsMin_.y;
    // Lid height: what the user set, or just under the hull part's own top (so the lid never pokes out above the rim).
    const float lidY = floating_.maskHeight >= 0.0f ? boundsMin_.y + floating_.maskHeight * modelHeight
                                                   : hull.boundsMax.y - 0.02f * modelHeight;
    const float key[3] = { static_cast<float>(part), lidY, floating_.maskInset };
    if (maskRenderable_ && std::memcmp(key, maskKey_, sizeof(key)) == 0) return;

    // The outline, pulled in by a fraction of the hull's width so the lid stays inside the walls.
    float minX = hull.footprint[0].x, maxX = minX, minZ = hull.footprint[0].z, maxZ = minZ;
    for (const HullMask::Point2& p : hull.footprint) {
        minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
        minZ = (std::min)(minZ, p.z); maxZ = (std::max)(maxZ, p.z);
    }
    const float beam = (std::min)(maxX - minX, maxZ - minZ);
    const std::vector<HullMask::Point2> outline = HullMask::InsetConvex(hull.footprint, floating_.maskInset * beam);

    // A fan around the middle: vertex 0 is the centre, then the outline, closing back to its first point.
    const HullMask::Point2 centre = HullMask::Centroid(outline);
    std::vector<Vertex> vertices(outline.size() + 1);
    vertices[0].position = XMFLOAT3(centre.x, lidY, centre.z);
    for (size_t i = 0; i < outline.size(); ++i) vertices[i + 1].position = XMFLOAT3(outline[i].x, lidY, outline[i].z);
    for (Vertex& v : vertices) v.normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
    std::vector<std::uint32_t> indices;
    for (size_t i = 0; i < outline.size(); ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<std::uint32_t>(i + 1));
        indices.push_back(static_cast<std::uint32_t>((i + 1) % outline.size() + 1));
    }

    // Same vertex shader as every model (the vertex layout is shared); NO pixel shader: only depth is written. The shader
    // comes from the shader cache, so rebuilding the lid while a slider moves costs a small buffer, not a compile.
    maskRenderable_ = std::make_unique<Renderable>(device_, new Model({ new Mesh(device_, vertices, indices) }), L"shaders/vs.hlsl", nullptr);
    std::memcpy(maskKey_, key, sizeof(key));
}

void ModelObject::RenderHullMask(ID3D11DeviceContext* ctx, const RenderView& view) const
{
    if (!floating_.hullMask || !ctx || view.kind != ViewKind::Main) return;
    const int part = ResolveMaskPart();
    if (part < 0) return;
    if (renderSettings_.frustumCulling && !view.frustum.IntersectsAABB(worldMin_, worldMax_)) return;
    EnsureHullMask(part);
    if (!maskRenderable_ || !maskRenderable_->IsValid()) return;

    if (!maskBlend_) {
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = 0;   // colour untouched
        device_->CreateBlendState(&blend, &maskBlend_);
    }
    if (!maskDepth_) {
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;   // the whole point: leave the lid in the depth buffer
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        device_->CreateDepthStencilState(&depth, &maskDepth_);
    }
    if (!maskBlend_ || !maskDepth_) return;

    // Remember the states we change and put them back: whatever draws next must find the pipeline as it left it.
    ID3D11BlendState* oldBlend = nullptr;
    FLOAT oldFactor[4] = {};
    UINT oldSampleMask = 0;
    ctx->OMGetBlendState(&oldBlend, oldFactor, &oldSampleMask);
    ID3D11DepthStencilState* oldDepth = nullptr;
    UINT oldStencilRef = 0;
    ctx->OMGetDepthStencilState(&oldDepth, &oldStencilRef);
    ID3D11RasterizerState* oldRaster = nullptr;
    ctx->RSGetState(&oldRaster);

    const FLOAT factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->OMSetBlendState(maskBlend_, factor, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(maskDepth_, 0);
    ctx->RSSetState(GetCullNone());   // the lid is flat: either side may face the camera
    maskRenderable_->SetWorld(world_);
    maskRenderable_->Render(ctx);

    ctx->OMSetBlendState(oldBlend, oldFactor, oldSampleMask);
    ctx->OMSetDepthStencilState(oldDepth, oldStencilRef);
    ctx->RSSetState(oldRaster);
    if (oldBlend) oldBlend->Release();
    if (oldDepth) oldDepth->Release();
    if (oldRaster) oldRaster->Release();
}
