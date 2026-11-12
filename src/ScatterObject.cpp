#include "ScatterObject.h"
#include "Logger.h"
#include "Mesh.h"
#include "MeshLod.h"
#include "ModelImporter.h"
#include "ProceduralModels.h"
#include "ShaderSlots.h"
#include "SurfaceMaterial.h"
#include "Terrain.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace DirectX;

namespace {
using Clock = std::chrono::steady_clock;
double MillisecondsSince(const Clock::time_point& start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Distance from a point to the nearest point of a box (0 if inside).
float DistanceToBox(const XMFLOAT3& p, const XMFLOAT3& lo, const XMFLOAT3& hi)
{
    const float dx = (std::max)((std::max)(lo.x - p.x, 0.0f), p.x - hi.x);
    const float dy = (std::max)((std::max)(lo.y - p.y, 0.0f), p.y - hi.y);
    const float dz = (std::max)((std::max)(lo.z - p.z, 0.0f), p.z - hi.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// What a loaded variant was made from. If this text changes, the variant is loaded again; the weights and slope bands are
// not in it: they only change the placement.
std::string VariantSignature(const ScatterVariantSettings& v)
{
    std::string key = v.model;
    for (const ScatterMaterialOverride& o : v.materials) {
        char numbers[160];
        std::snprintf(numbers, sizeof(numbers), "|%d;%g,%g,%g,%g;%g;%d;", o.slot, o.baseColor[0], o.baseColor[1], o.baseColor[2], o.baseColor[3],
                      o.roughness, o.flipGreen ? 1 : 0);
        key += numbers;
        key += o.albedo; key += ';'; key += o.normal; key += ';'; key += o.orm; key += ';'; key += o.emissive;
    }
    return key;
}
}  // namespace

// ---- Run-time data -------------------------------------------------------------------------------------------------------

// A model file that has been loaded: its meshes with their levels of detail. Shared by every variant that uses the file, so
// three rocks that only differ by texture cost one set of vertex buffers.
struct ScatterObject::ModelAsset {
    struct DrawPart {
        std::shared_ptr<Mesh> mesh;          // shared: a part with no "_LODn" in its name belongs to every authored level
        int material = 0;
        bool doubleSided = false;
    };
    std::vector<std::vector<DrawPart>> groups;   // one group per AUTHORED level of detail; a single group when there are none
    int levelCount = 1;                          // levels the model switches between
    float height = 1.0f;                         // the model's size at scale 1: height, and the radius of a sphere around it
    float radius = 1.0f;
    ImportedModel materialSource;                // its materials and embedded images (the vertex data lives in the meshes now)
};

// One look of a species: a model plus the materials made for it (the model's own, with the artist's replacements on top).
struct ScatterObject::Variant {
    std::shared_ptr<ModelAsset> asset;
    std::vector<std::unique_ptr<SurfaceMaterial>> materials;
    std::string signature;       // what it was loaded from (VariantSignature); "-" = switched off
    std::string error;
    bool loaded = false;         // can be drawn
    float scaleMul = 1.0f;       // a copy of the setting: the bounds and the level-of-detail radius use it
};

struct ScatterObject::Species {
    std::vector<Variant> variants;               // index = the variant number the placement hands out
    bool loaded = false;                         // at least one variant can be drawn
    float reachRadius = 1.0f;                    // the biggest variant's size at scale 1 (scaleMul included): radius of a sphere around it
    float reachHeight = 1.0f;                    // ... and its height (or radius, if that is larger)

    // The instances: one slot per grid cell, and ONE GPU buffer holding them all, grouped by variant and then by cell.
    struct Cell {
        std::vector<ScatterInstance> instances;  // grouped by variant: variant v is [offset[v], offset[v + 1])
        std::uint32_t offset[kMaxScatterVariants + 1] = {};
        int groups = 0;                          // the number of variants the grouping was made for
        ScatterCellLayout layout;                // this cell's ranges in the GPU buffer, per variant
        std::uint32_t total = 0;                 // instances of this cell in the GPU buffer
        XMFLOAT3 boundsMin = {}, boundsMax = {}; // world box of everything in the cell, including the plants' own size
        bool dirty = true;
    };
    std::vector<Cell> cells;
    ScatterRules appliedRules;                   // the rules the instances in `cells` were made with
    bool rulesValid = false;
    ID3D11Buffer* instanceBuffer = nullptr;
    UINT capacity = 0;                           // instances the buffer has room for
    std::uint32_t total = 0;
    std::uint32_t variantTotal[kMaxScatterVariants] = {};
    bool uploadPending = false;
    Clock::time_point lastUpload{};
    ID3D11Buffer* materialCb = nullptr;          // this species' material block (b4)

    // Per view: each cell's level for each variant last frame (255 = hidden / none yet), index cell * variants + variant;
    // and the draw ranges chosen this frame, per variant and level.
    mutable std::vector<unsigned char> lodState[kViewKindCount];
    mutable std::vector<DrawRun> runs[kMaxScatterVariants][kMaxLodLevels];

    ~Species()
    {
        if (instanceBuffer) instanceBuffer->Release();
        if (materialCb) materialCb->Release();
    }
    void ClearInstances()
    {
        for (Cell& c : cells) {
            c.instances.clear(); c.instances.shrink_to_fit();
            for (std::uint32_t& o : c.offset) o = 0;
            c.groups = 0; c.layout = ScatterCellLayout{}; c.total = 0; c.dirty = false;
        }
        total = 0;
        for (std::uint32_t& t : variantTotal) t = 0;
        if (instanceBuffer) { instanceBuffer->Release(); instanceBuffer = nullptr; }
        capacity = 0;
        uploadPending = false;
        rulesValid = false;
    }
};

// ---- ScatterObject ------------------------------------------------------------------------------------------------------

ScatterObject::ScatterObject(ID3D11Device* device) : device_(device)
{
    litShader_ = ShaderCache::Get(device, L"shaders/scatter_vs.hlsl", L"shaders/ps.hlsl");
    shadowShader_ = ShaderCache::Get(device, L"shaders/scatter_shadow_vs.hlsl", nullptr);
    shadowAlphaShader_ = ShaderCache::Get(device, L"shaders/scatter_shadow_vs.hlsl", L"shaders/scatter_shadow_ps.hlsl");
    if (!litShader_) LOG_ERROR("Scatter: the instancing shaders could not be loaded; nothing will be drawn");
}

ScatterObject::~ScatterObject()
{
    if (cullNone_) cullNone_->Release();
}

ID3D11RasterizerState* ScatterObject::GetCullNone() const
{
    if (!cullNone_ && device_) {
        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;     // leaves and thin cards are seen from both sides
        desc.DepthClipEnable = TRUE;
        device_->CreateRasterizerState(&desc, &cullNone_);
    }
    return cullNone_;
}

void ScatterObject::SetSettings(ScatterSettings settings)
{
    settings.Clamp();
    settings_ = std::move(settings);
    looks_.clear();
    for (const ScatterSpeciesSettings& s : settings_.species) looks_.push_back(s.EffectiveVariants());
    // What actually changed is worked out in Refresh: a variant with a new model or new texture reloads, a species with new
    // rules places its cells again, and drawing options (levels of detail, shadows...) need nothing at all.
}

size_t ScatterObject::GetSpeciesCount() const { return species_.size(); }
bool ScatterObject::IsSpeciesReady(size_t i) const { return i < species_.size() && species_[i]->loaded; }
const std::string& ScatterObject::GetSpeciesError(size_t i) const
{
    static const std::string none;
    if (i >= species_.size()) return none;
    for (const Variant& v : species_[i]->variants) if (!v.error.empty()) return v.error;   // the first problem is the one to show
    return none;
}
size_t ScatterObject::GetInstanceCount(size_t i) const { return i < species_.size() ? species_[i]->total : 0; }
int ScatterObject::GetSpeciesLodLevels(size_t i) const
{
    if (i >= species_.size()) return 1;
    int levels = 1;
    for (const Variant& v : species_[i]->variants) if (v.loaded) levels = (std::max)(levels, v.asset->levelCount);
    return levels;
}
size_t ScatterObject::GetVariantCount(size_t i) const { return i < species_.size() ? species_[i]->variants.size() : 0; }
bool ScatterObject::IsVariantReady(size_t i, size_t v) const { return i < species_.size() && v < species_[i]->variants.size() && species_[i]->variants[v].loaded; }
const std::string& ScatterObject::GetVariantError(size_t i, size_t v) const
{
    static const std::string none;
    return i < species_.size() && v < species_[i]->variants.size() ? species_[i]->variants[v].error : none;
}
size_t ScatterObject::GetVariantInstanceCount(size_t i, size_t v) const
{
    return i < species_.size() && v < static_cast<size_t>(kMaxScatterVariants) ? species_[i]->variantTotal[v] : 0;
}
size_t ScatterObject::GetVariantMaterialCount(size_t i, size_t v) const
{
    return IsVariantReady(i, v) ? species_[i]->variants[v].materials.size() : 0;
}
std::string ScatterObject::GetVariantMaterialName(size_t i, size_t v, size_t slot) const
{
    if (!IsVariantReady(i, v)) return {};
    const ImportedModel& source = species_[i]->variants[v].asset->materialSource;
    if (slot < source.materials.size() && !source.materials[slot].name.empty()) return source.materials[slot].name;
    return "Material " + std::to_string(slot);
}

bool ScatterObject::GetBoundingSphere(XMFLOAT3& center, float& radius) const
{
    if (!hasGround_) return false;
    center = center_;
    radius = boundsRadius_;
    return true;
}

// Loads a model file once. The result is shared by every variant that names the same file (see modelCache_).
std::shared_ptr<ScatterObject::ModelAsset> ScatterObject::AcquireModel(const std::string& model, std::string& error)
{
    const auto cached = modelCache_.find(model);
    if (cached != modelCache_.end()) return cached->second;

    ImportedModel imported;
    if (IsBuiltinModel(model)) {
        if (!MakeBuiltinModel(model, imported)) { error = "Unknown built-in model \"" + model + "\"."; return nullptr; }
    } else if (model.empty()) {
        error = "No model chosen.";
        return nullptr;
    } else if (!ImportModel(model, imported, error)) {
        return nullptr;
    }
    if (imported.parts.empty()) { error = "The model has no geometry."; return nullptr; }

    auto asset = std::make_shared<ModelAsset>();

    // Instances are placed by their BASE, so move every vertex so that the model's bottom centre is the origin (the same
    // pivot ModelObject uses).
    const XMFLOAT3 pivot((imported.boundsMin.x + imported.boundsMax.x) * 0.5f, imported.boundsMin.y, (imported.boundsMin.z + imported.boundsMax.z) * 0.5f);
    const XMFLOAT3 size(imported.boundsMax.x - imported.boundsMin.x, imported.boundsMax.y - imported.boundsMin.y, imported.boundsMax.z - imported.boundsMin.z);
    asset->height = (std::max)(size.y, 1e-3f);
    asset->radius = (std::max)(0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z), 1e-3f);
    const int materialCount = (std::max)(1, static_cast<int>(imported.materials.size()));   // a variant adds a default material when the file has none

    // Authored levels of detail ("Pine_LOD0", "Pine_LOD1"...), exactly as ModelObject reads them.
    std::vector<int> groupOf(imported.parts.size(), -1);
    std::vector<int> tags;
    for (size_t i = 0; i < imported.parts.size(); ++i) {
        const int tag = ParseLodIndex(imported.parts[i].name);
        if (tag < 0) continue;
        groupOf[i] = (std::min)(tag, 99);
        if (std::find(tags.begin(), tags.end(), groupOf[i]) == tags.end()) tags.push_back(groupOf[i]);
    }
    std::sort(tags.begin(), tags.end());
    if (tags.size() < 2) { tags.clear(); std::fill(groupOf.begin(), groupOf.end(), -1); }
    if (tags.size() > static_cast<size_t>(kMaxLodLevels)) tags.resize(kMaxLodLevels);
    const bool authored = tags.size() > 1;
    asset->groups.resize(authored ? tags.size() : 1);
    asset->levelCount = 1;

    for (ImportedPart& part : imported.parts) {
        if (part.vertices.empty() || part.indices.empty()) continue;
        for (Vertex& v : part.vertices) { v.position.x -= pivot.x; v.position.y -= pivot.y; v.position.z -= pivot.z; }
    }
    for (size_t i = 0; i < imported.parts.size(); ++i) {
        const ImportedPart& part = imported.parts[i];
        if (part.vertices.empty() || part.indices.empty()) continue;
        MeshLodSettings lodSettings;
        if (authored) lodSettings.levels = 1;   // the artist made the levels
        const auto levels = BuildMeshLods(&part.vertices[0].position.x, sizeof(Vertex), &part.vertices[0].normal.x, sizeof(Vertex), 5,
                                          part.vertices.size(), part.indices.data(), part.indices.size(), lodSettings);
        ModelAsset::DrawPart built;
        built.mesh = levels.empty() ? std::make_shared<Mesh>(device_, part.vertices, part.indices) : std::make_shared<Mesh>(device_, part.vertices, levels);
        built.material = (std::min)(part.material, materialCount - 1);
        built.doubleSided = !imported.materials.empty() && part.material < static_cast<int>(imported.materials.size()) && imported.materials[part.material].doubleSided;
        if (!authored) {
            asset->levelCount = (std::max)(asset->levelCount, static_cast<int>(levels.size()));
            asset->groups[0].push_back(built);
        } else if (groupOf[i] < 0) {
            for (auto& group : asset->groups) group.push_back(built);   // untagged: part of every level
        } else {
            asset->groups[std::find(tags.begin(), tags.end(), groupOf[i]) - tags.begin()].push_back(built);
        }
    }
    if (authored) asset->levelCount = static_cast<int>(asset->groups.size());
    asset->levelCount = (std::min)(asset->levelCount, kMaxLodLevels);
    for (auto& group : asset->groups) {   // fewer material switches
        std::stable_sort(group.begin(), group.end(), [](const ModelAsset::DrawPart& a, const ModelAsset::DrawPart& b) { return a.material < b.material; });
    }

    imported.parts.clear();   // the meshes own the vertices now; what stays is what variants build their materials from
    imported.parts.shrink_to_fit();
    asset->materialSource = std::move(imported);
    modelCache_[model] = asset;
    return asset;
}

// Makes one variant ready to draw: its model (shared), and its OWN materials: the model's, then the artist's replacements.
void ScatterObject::LoadVariant(Variant& variant, const ScatterVariantSettings& settings)
{
    variant.materials.clear();
    variant.asset.reset();
    variant.error.clear();
    variant.loaded = false;

    variant.asset = AcquireModel(settings.model, variant.error);
    if (!variant.asset) return;

    const ImportedModel& source = variant.asset->materialSource;
    for (const ImportedMaterial& material : source.materials) variant.materials.push_back(BuildSurfaceMaterial(device_, source, material));
    if (variant.materials.empty()) variant.materials.push_back(std::make_unique<SurfaceMaterial>(device_));

    for (const ScatterMaterialOverride& o : settings.materials) {
        if (o.slot < 0 || o.slot >= static_cast<int>(variant.materials.size())) continue;   // the model has no such slot (any more)
        SurfaceMaterial& material = *variant.materials[static_cast<size_t>(o.slot)];
        SurfaceSettings& surface = material.Settings();
        const std::string who = "Scatter variant \"" + settings.name + "\": ";
        if (!o.albedo.empty() && !material.LoadAlbedo(o.albedo)) LOG_WARNING(who + "could not read the albedo map " + o.albedo);
        if (!o.normal.empty()) {
            if (material.LoadNormal(o.normal)) surface.flipGreen = o.flipGreen;   // the convention belongs to the map that was loaded
            else LOG_WARNING(who + "could not read the normal map " + o.normal);
        }
        if (!o.orm.empty() && !material.LoadOrm(o.orm)) LOG_WARNING(who + "could not read the ORM map " + o.orm);
        if (!o.emissive.empty() && !material.LoadEmissive(o.emissive)) LOG_WARNING(who + "could not read the emissive map " + o.emissive);
        surface.baseColor.x *= o.baseColor[0]; surface.baseColor.y *= o.baseColor[1];
        surface.baseColor.z *= o.baseColor[2]; surface.baseColor.w *= o.baseColor[3];
        if (o.roughness >= 0.0f) {
            if (material.HasOrmMap()) surface.roughnessScale = o.roughness;   // with a map, the number scales the map's roughness
            else surface.fallbackRoughness = o.roughness;                     // without one, it IS the roughness
        }
    }
    variant.loaded = true;
}

void ScatterObject::SyncSpecies()
{
    while (species_.size() < settings_.species.size()) species_.push_back(std::make_unique<Species>());
    if (species_.size() > settings_.species.size()) species_.resize(settings_.species.size());
    if (looks_.size() != settings_.species.size()) {   // (SetSettings keeps them in step: this only guards a missed call)
        looks_.clear();
        for (const ScatterSpeciesSettings& s : settings_.species) looks_.push_back(s.EffectiveVariants());
    }
    bool changed = false;
    for (size_t i = 0; i < species_.size(); ++i) {
        Species& sp = *species_[i];
        const ScatterSpeciesSettings& cfg = settings_.species[i];
        const std::vector<ScatterVariantSettings>& looks = looks_[i];

        if (sp.variants.size() != looks.size()) { sp.variants.resize(looks.size()); sp.rulesValid = false; changed = true; }
        for (size_t v = 0; v < looks.size(); ++v) {
            Variant& variant = sp.variants[v];
            variant.scaleMul = looks[v].scaleMul;
            const std::string signature = looks[v].enabled ? VariantSignature(looks[v]) : std::string("-");
            if (variant.signature == signature) continue;
            variant.signature = signature;   // remembered even if the load fails, so a broken file is not retried every frame
            sp.rulesValid = false;           // the model's size feeds the rules (sinking) and the cells' boxes: place everything again
            changed = true;
            if (!looks[v].enabled) {
                variant.materials.clear(); variant.asset.reset(); variant.error.clear(); variant.loaded = false;
                continue;
            }
            LoadVariant(variant, looks[v]);
            if (!variant.loaded) LOG_WARNING("Scatter species \"" + cfg.name + "\", variant \"" + looks[v].name + "\": " + variant.error);
        }

        sp.loaded = false;
        sp.reachRadius = sp.reachHeight = 0.0f;
        for (const Variant& variant : sp.variants) {
            if (!variant.loaded) continue;
            sp.loaded = true;
            sp.reachRadius = (std::max)(sp.reachRadius, variant.asset->radius * variant.scaleMul);
            sp.reachHeight = (std::max)(sp.reachHeight, (std::max)(variant.asset->height, variant.asset->radius) * variant.scaleMul);
        }

        if (!sp.materialCb) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(SurfaceMaterialCB);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(&desc, nullptr, &sp.materialCb))) {
                LOG_WARNING("Scatter species \"" + cfg.name + "\": could not create the material buffer");
                sp.loaded = false;
            }
        }
        if (!cfg.enabled && sp.total > 0) sp.ClearInstances();   // switched off: give the memory back
    }
    if (changed) {   // models that no variant uses any more give their vertex buffers back
        for (auto it = modelCache_.begin(); it != modelCache_.end();) {
            if (it->second.use_count() == 1) it = modelCache_.erase(it);
            else ++it;
        }
    }
}

void ScatterObject::ResetGrid(const ScatterRect& extent)
{
    extent_ = extent;
    grid_ = MakeScatterGrid(extent, settings_.cellSize);
    for (auto& owned : species_) {
        Species& sp = *owned;
        sp.ClearInstances();
        sp.cells.assign(static_cast<size_t>(grid_.CellCount()), Species::Cell{});   // every cell starts dirty
        for (auto& state : sp.lodState) state.clear();
    }
}

void ScatterObject::MarkDirty(const ScatterRect& rect)
{
    for (size_t i = 0; i < species_.size(); ++i) {
        Species& sp = *species_[i];
        if (!sp.rulesValid) continue;
        // A candidate looks at its neighbours (the thinning), so a cell next to the change can differ too: widen by two spacings.
        const float margin = 2.0f * sp.appliedRules.spacing + 1.0f;
        const ScatterRect wide{ rect.minX - margin, rect.minZ - margin, rect.maxX + margin, rect.maxZ + margin };
        int x0, z0, x1, z1;
        if (!ScatterCellsTouching(grid_, wide, x0, z0, x1, z1)) continue;
        for (int cz = z0; cz <= z1; ++cz)
            for (int cx = x0; cx <= x1; ++cx) sp.cells[static_cast<size_t>(grid_.IndexOf(cx, cz))].dirty = true;
    }
}

void ScatterObject::UploadInstances(Species& sp)
{
    sp.uploadPending = false;
    sp.lastUpload = Clock::now();

    // Lay the buffer out variant by variant, cell by cell (ScatterPlacement.h explains why), and copy each cell's slice in.
    const int variantCount = (std::min)((std::max)(static_cast<int>(sp.variants.size()), 1), kMaxScatterVariants);
    std::vector<ScatterCellCounts> counts(sp.cells.size());
    for (size_t c = 0; c < sp.cells.size(); ++c) {
        const Species::Cell& cell = sp.cells[c];
        // A cell generated for fewer variants than there are now (a variant was just added, regeneration still pending) has
        // nothing for the new ones yet.
        for (int v = 0; v < variantCount && v < cell.groups; ++v) counts[c].count[v] = cell.offset[v + 1] - cell.offset[v];
    }
    std::vector<ScatterCellLayout> layout;
    const std::uint32_t cap = static_cast<std::uint32_t>((std::max)(settings_.maxInstancesPerSpecies, 0));
    const std::uint32_t total = PlanScatterLayout(counts, variantCount, cap, layout);
    std::vector<ScatterInstance> all(total);
    for (std::uint32_t& t : sp.variantTotal) t = 0;
    for (size_t c = 0; c < sp.cells.size(); ++c) {
        Species::Cell& cell = sp.cells[c];
        cell.layout = layout[c];
        cell.total = 0;
        for (int v = 0; v < variantCount; ++v) {
            const std::uint32_t n = layout[c].count[v];
            if (n == 0) continue;
            std::memcpy(&all[layout[c].start[v]], &cell.instances[cell.offset[v]], static_cast<size_t>(n) * sizeof(ScatterInstance));
            cell.total += n;
            sp.variantTotal[v] += n;
        }
    }
    sp.total = total;
    if (all.empty()) {
        if (sp.instanceBuffer) { sp.instanceBuffer->Release(); sp.instanceBuffer = nullptr; }
        sp.capacity = 0;
        return;
    }
    ID3D11DeviceContext* ctx = nullptr;
    device_->GetImmediateContext(&ctx);
    if (!ctx) return;
    if (!sp.instanceBuffer || sp.capacity < all.size()) {
        // Room to grow by a quarter, so painting more forest does not create a new buffer every stroke.
        const UINT capacity = static_cast<UINT>(all.size() + all.size() / 4 + 256);
        if (sp.instanceBuffer) { sp.instanceBuffer->Release(); sp.instanceBuffer = nullptr; }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = capacity * sizeof(ScatterInstance);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        const HRESULT hr = device_->CreateBuffer(&desc, nullptr, &sp.instanceBuffer);
        if (FAILED(hr)) {
            LOG_HR(hr, "Scatter: instance buffer creation failed");
            sp.instanceBuffer = nullptr; sp.capacity = 0; sp.total = 0;
            ctx->Release();
            return;
        }
        sp.capacity = capacity;
    }
    D3D11_BOX box{};   // for a buffer only left and right matter: they are BYTE offsets
    box.left = 0;
    box.right = static_cast<UINT>(all.size() * sizeof(ScatterInstance));
    box.top = 0; box.bottom = 1; box.front = 0; box.back = 1;
    ctx->UpdateSubresource(sp.instanceBuffer, 0, &box, all.data(), 0, 0);
    ctx->Release();
}

void ScatterObject::GenerateCells(const ScatterGround& ground, const XMFLOAT3& camera, double budgetMs)
{
    const Clock::time_point start = Clock::now();

    struct Job { float distanceSquared; size_t species; int cell; };
    std::vector<Job> jobs;
    for (size_t i = 0; i < species_.size(); ++i) {
        const Species& sp = *species_[i];
        if (!sp.loaded || !sp.rulesValid || !settings_.species[i].enabled) continue;
        for (int c = 0; c < static_cast<int>(sp.cells.size()); ++c) {
            if (!sp.cells[static_cast<size_t>(c)].dirty) continue;
            const ScatterRect r = ScatterCellRect(grid_, c % grid_.cellsX, c / grid_.cellsX);
            const float dx = (std::max)((std::max)(r.minX - camera.x, 0.0f), camera.x - r.maxX);
            const float dz = (std::max)((std::max)(r.minZ - camera.z, 0.0f), camera.z - r.maxZ);
            jobs.push_back({ dx * dx + dz * dz, i, c });
        }
    }
    pendingCells_ = static_cast<int>(jobs.size());
    if (jobs.empty()) return;
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.distanceSquared < b.distanceSquared; });   // what you see first

    std::vector<std::uint8_t> variantOf;   // scratch: the variant of each instance the placement just made
    for (const Job& job : jobs) {
        Species& sp = *species_[job.species];
        const ScatterSpeciesSettings& cfg = settings_.species[job.species];
        Species::Cell& cell = sp.cells[static_cast<size_t>(job.cell)];
        const ScatterRect rect = ScatterCellRect(grid_, job.cell % grid_.cellsX, job.cell / grid_.cellsX);
        cell.instances.clear();
        variantOf.clear();
        GenerateScatter(ground, sp.appliedRules, rect, cell.instances, &variantOf);
        GroupScatterByVariant(cell.instances, variantOf, sp.appliedRules.variantCount, cell.offset);
        cell.groups = sp.appliedRules.variantCount;
        cell.dirty = false;
        sp.uploadPending = true;
        --pendingCells_;
        if (!cell.instances.empty()) {
            float lowest = cell.instances[0].y, highest = cell.instances[0].y;
            for (const ScatterInstance& inst : cell.instances) { lowest = (std::min)(lowest, inst.y); highest = (std::max)(highest, inst.y); }
            const float side = sp.reachRadius * cfg.scaleMax;   // plants are wider than their base point (the biggest variant decides)
            const float top = sp.reachHeight * cfg.scaleMax;    // ... and taller
            cell.boundsMin = XMFLOAT3(rect.minX - side, lowest - 0.1f * side, rect.minZ - side);
            cell.boundsMax = XMFLOAT3(rect.maxX + side, highest + top, rect.maxZ + side);
        }
        if (MillisecondsSince(start) >= budgetMs) break;   // the rest waits for the next frame
    }
}

void ScatterObject::Refresh(const Terrain* terrain, const XMFLOAT3& cameraPosition)
{
    if (!device_) return;
    SyncSpecies();

    if (!terrain || !terrain->IsValid()) {
        if (hasGround_) {   // the terrain was deleted: what stood on it goes with it
            for (auto& sp : species_) { sp->cells.clear(); sp->ClearInstances(); }
            hasGround_ = false;
            terrainId_ = 0;
            needsFullRebuild_ = true;
            pendingCells_ = 0;
        }
        return;
    }

    ScatterGround ground;
    ground.field = &terrain->GetHeightfield();
    ground.splat = &terrain->GetSplatMap();
    XMFLOAT3 position, rotation, scale;
    terrain->GetEditorTransform(position, rotation, scale);
    ground.posX = position.x; ground.posY = position.y; ground.posZ = position.z;
    ground.scaleX = scale.x; ground.scaleY = scale.y; ground.scaleZ = scale.z;
    if (!ScatterGroundIsValid(ground)) return;
    const ScatterRect extent = ScatterGroundExtent(ground);

    // Is this still the ground the cells were made for? A new terrain, a moved one or a new cell size starts over; otherwise
    // only the patches the terrain says changed are marked.
    auto closeTo = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    const bool sameGrid = hasGround_ && closeTo(extent.minX, extent_.minX) && closeTo(extent.maxX, extent_.maxX) && closeTo(extent.minZ, extent_.minZ) &&
                          closeTo(extent.maxZ, extent_.maxZ) && closeTo(grid_.cellSize, std::clamp(settings_.cellSize, 1.0f, 1.0e6f));
    if (needsFullRebuild_ || terrain->GetId() != terrainId_ || !sameGrid) {
        ResetGrid(extent);
        terrainId_ = terrain->GetId();
        hasGround_ = true;
        needsFullRebuild_ = false;
    } else {
        std::vector<Terrain::SurfaceChange> changes;
        if (!terrain->GetSurfaceChangesSince(surfaceRevision_, changes)) {
            for (auto& sp : species_) sp->rulesValid = false;   // the log cannot say what changed: place everything again
        } else {
            for (const Terrain::SurfaceChange& change : changes) MarkDirty({ change.minX, change.minZ, change.maxX, change.maxZ });
        }
    }
    surfaceRevision_ = terrain->GetSurfaceRevision();

    // New or changed rules: every cell of that species is placed again.
    float tallest = 0.0f;
    for (size_t i = 0; i < species_.size(); ++i) {
        Species& sp = *species_[i];
        const ScatterSpeciesSettings& cfg = settings_.species[i];
        if (!sp.loaded || !cfg.enabled) continue;
        tallest = (std::max)(tallest, sp.reachHeight * cfg.scaleMax);
        const std::vector<ScatterVariantSettings>& looks = looks_[i];
        std::vector<float> heights(looks.size(), 1.0f);
        std::vector<char> usable(looks.size(), 0);
        for (size_t v = 0; v < looks.size() && v < sp.variants.size(); ++v) {
            usable[v] = looks[v].enabled && sp.variants[v].loaded ? 1 : 0;
            if (usable[v]) heights[v] = sp.variants[v].asset->height;
        }
        const ScatterRules rules = MakeScatterRules(cfg, looks, heights, usable, static_cast<int>(i), settings_.seed);
        if (!sp.rulesValid || std::memcmp(&rules, &sp.appliedRules, sizeof(ScatterRules)) != 0) {
            sp.appliedRules = rules;
            sp.rulesValid = true;
            if (sp.cells.size() != static_cast<size_t>(grid_.CellCount())) sp.cells.assign(static_cast<size_t>(grid_.CellCount()), Species::Cell{});
            for (Species::Cell& c : sp.cells) c.dirty = true;
        }
    }

    float lowest = 0.0f, highest = 0.0f;
    terrain->GetHeightRange(lowest, highest);
    const float width = extent.maxX - extent.minX, depth = extent.maxZ - extent.minZ, height = (highest + tallest) - lowest;
    center_ = XMFLOAT3(0.5f * (extent.minX + extent.maxX), 0.5f * (lowest + highest + tallest), 0.5f * (extent.minZ + extent.maxZ));
    boundsRadius_ = 0.5f * std::sqrt(width * width + depth * depth + height * height);

    // Place what is pending, nearest first, within the time budget.
    GenerateCells(ground, cameraPosition, static_cast<double>(settings_.generationBudgetMs));

    // Upload: at once when a species is complete; while cells are still arriving, at most ten times a second.
    for (auto& owned : species_) {
        Species& sp = *owned;
        if (!sp.uploadPending) continue;
        bool pending = false;
        for (const Species::Cell& c : sp.cells) if (c.dirty) { pending = true; break; }
        if (!pending || MillisecondsSince(sp.lastUpload) > 100.0) UploadInstances(sp);
    }
}

void ScatterObject::GenerateAllNow(const Terrain* terrain)
{
    const float budget = settings_.generationBudgetMs;
    settings_.generationBudgetMs = 1.0e9f;   // (not clamped: this is only a local override)
    const XMFLOAT3 origin(0.0f, 0.0f, 0.0f);
    Refresh(terrain, origin);
    settings_.generationBudgetMs = budget;
}

void ScatterObject::Render(ID3D11DeviceContext* ctx, const RenderView& view) const { Draw(ctx, view, false); }
void ScatterObject::RenderShadow(ID3D11DeviceContext* ctx, const Shader&, const RenderView& view) const { Draw(ctx, view, true); }

void ScatterObject::Draw(ID3D11DeviceContext* ctx, const RenderView& view, bool shadowPass) const
{
    const int kind = static_cast<int>(view.kind);
    ScatterViewStats& stats = stats_[kind];
    stats = {};
    if (!ctx || !settings_.enabled || !hasGround_ || !litShader_) return;
    if (shadowPass && (!settings_.shadows || !shadowShader_)) return;

    // The water's mirror is a second picture of the whole scene: it can do with less (reflectionDetail scales the distances).
    const float detail = view.kind == ViewKind::Reflection ? settings_.reflectionDetail : 1.0f;
    const float lodScale = settings_.lodBias * detail;

    ID3D11RasterizerState* previousRaster = nullptr;
    bool rasterSaved = false, cullNoneNow = false;

    for (size_t si = 0; si < species_.size() && si < settings_.species.size(); ++si) {
        Species& sp = *species_[si];
        const ScatterSpeciesSettings& cfg = settings_.species[si];
        if (!cfg.enabled || !sp.loaded) continue;
        stats.instancesTotal += static_cast<int>(sp.total);
        if (sp.total == 0 || !sp.instanceBuffer) continue;
        if (shadowPass && !cfg.castShadows) continue;

        const int variantCount = (std::min)(static_cast<int>(sp.variants.size()), kMaxScatterVariants);
        const int cellCount = static_cast<int>(sp.cells.size());

        // What each variant needs to be culled and simplified: how many levels it has and how big a typical one is.
        struct VariantView { bool active = false; int levels = 1; float lodRadius = 1.0f; };
        VariantView views[kMaxScatterVariants];
        for (int v = 0; v < variantCount; ++v) {
            const Variant& variant = sp.variants[static_cast<size_t>(v)];
            if (!variant.loaded || sp.variantTotal[v] == 0) continue;
            views[v].active = true;
            views[v].levels = (std::max)(1, variant.asset->levelCount);
            views[v].lodRadius = variant.asset->radius * 0.5f * (cfg.scaleMin + cfg.scaleMax) * variant.scaleMul;   // a typical one
        }

        // 1. CULL and choose a LEVEL for every cell and variant. The whole cell is culled at once (a cell's box covers every
        //    variant); the level is per variant, because a boulder and a slab look different sizes from the same spot. Cells of
        //    one variant with the same level that sit side by side in the buffer merge into one range (AppendRun), so a whole
        //    stretch of forest is one draw call per variant and level.
        std::vector<unsigned char>& state = sp.lodState[kind];
        const size_t stateSize = static_cast<size_t>(cellCount) * static_cast<size_t>(variantCount);
        if (state.size() != stateSize) state.assign(stateSize, 255);
        for (auto& perVariant : sp.runs) for (auto& runs : perVariant) runs.clear();
        const float thresholds[3] = { cfg.lodScreen[0], cfg.lodScreen[1], cfg.lodScreen[2] };
        for (int i = 0; i < cellCount; ++i) {
            const Species::Cell& cell = sp.cells[static_cast<size_t>(i)];
            if (cell.total == 0) continue;
            ++stats.cellsTotal;
            const size_t base = static_cast<size_t>(i) * static_cast<size_t>(variantCount);
            const float distance = DistanceToBox(view.lodOrigin, cell.boundsMin, cell.boundsMax);
            const bool hidden = (shadowPass && distance > cfg.shadowDistance) ||
                                (settings_.frustumCulling && !view.frustum.IntersectsAABB(cell.boundsMin, cell.boundsMax)) ||
                                (view.clipBelow && cell.boundsMax.y < view.clipBelowY);   // wholly under the mirror's clip plane
            if (hidden) { for (int v = 0; v < variantCount; ++v) state[base + static_cast<size_t>(v)] = 255; continue; }
            bool drawn = false;
            for (int v = 0; v < variantCount; ++v) {
                unsigned char& levelNow = state[base + static_cast<size_t>(v)];
                const std::uint32_t count = cell.layout.count[v];
                if (!views[v].active || count == 0) { levelNow = 255; continue; }
                const float screen = ScreenFraction(views[v].lodRadius, distance, view.projScale) * lodScale;
                if (BelowCullSize(screen, cfg.cullScreen)) { levelNow = 255; continue; }   // too small to matter
                int lod = 0;
                if (settings_.lodEnabled && views[v].levels > 1) {
                    lod = SelectLod(screen, thresholds, views[v].levels, levelNow == 255 ? -1 : levelNow);
                    levelNow = static_cast<unsigned char>(lod);
                    if (shadowPass) lod = (std::min)(lod + 1, views[v].levels - 1);   // nobody studies a shadow: one level coarser
                }
                AppendRun(sp.runs[v][lod], cell.layout.start[v], count);
                stats.instancesDrawn += static_cast<int>(count);
                stats.instancesPerLod[lod] += static_cast<int>(count);
                drawn = true;
            }
            if (drawn) ++stats.cellsDrawn;
        }
        bool any = false;
        for (int v = 0; v < variantCount; ++v)
            for (int l = 0; l < kMaxLodLevels; ++l) if (!sp.runs[v][l].empty()) any = true;
        if (!any) continue;

        // 2. DRAW. The instance buffer is stream 1, the same for every variant; each run is a StartInstanceLocation + an
        //    instance count. Only the mesh and the materials change from one variant to the next.
        const UINT stride = sizeof(ScatterInstance), offset = 0;
        ctx->IASetVertexBuffers(1, 1, &sp.instanceBuffer, &stride, &offset);
        const Shader* applied = nullptr;
        for (int v = 0; v < variantCount; ++v) {
            if (!views[v].active) continue;
            bool variantAny = false;
            for (int l = 0; l < views[v].levels; ++l) if (!sp.runs[v][l].empty()) variantAny = true;
            if (!variantAny) continue;
            const Variant& variant = sp.variants[static_cast<size_t>(v)];
            const ModelAsset& asset = *variant.asset;
            const int levels = views[v].levels;
            const bool authored = asset.groups.size() > 1;
            int boundMaterial = -1;   // (each variant has its own materials: bind again)
            for (size_t g = 0; g < asset.groups.size(); ++g) {
                for (const ModelAsset::DrawPart& part : asset.groups[g]) {
                    const SurfaceMaterial& material = *variant.materials[static_cast<size_t>(part.material)];
                    const bool cutout = material.Settings().alphaCutoff > 0.0f;
                    const Shader* shader = !shadowPass ? litShader_.get() : (cutout && shadowAlphaShader_ ? shadowAlphaShader_.get() : shadowShader_.get());
                    if (shader != applied) { shader->Apply(ctx); applied = shader; }
                    if (!shadowPass || cutout) {   // lit parts need the whole material; cut-out shadows need the albedo and the cutoff
                        if (part.material != boundMaterial) {
                            material.Bind(ctx);
                            const SurfaceMaterialCB cb = material.Constants();
                            D3D11_MAPPED_SUBRESOURCE mapped{};
                            if (SUCCEEDED(ctx->Map(sp.materialCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                                std::memcpy(mapped.pData, &cb, sizeof(cb));
                                ctx->Unmap(sp.materialCb, 0);
                            }
                            ctx->PSSetConstantBuffers(Slots::kCbMaterial, 1, &sp.materialCb);
                            boundMaterial = part.material;
                        }
                    }
                    if (!shadowPass && part.doubleSided != cullNoneNow) {   // switch the rasterizer only when the kind of part changes
                        if (!rasterSaved) { ctx->RSGetState(&previousRaster); rasterSaved = true; }
                        ctx->RSSetState(part.doubleSided ? GetCullNone() : previousRaster);
                        cullNoneNow = part.doubleSided;
                    }
                    for (int lod = 0; lod < levels; ++lod) {
                        const std::vector<DrawRun>& runs = sp.runs[v][lod];
                        if (runs.empty()) continue;
                        // Authored levels are separate meshes (one group each); generated levels are index buffers of one mesh.
                        const size_t groupForLod = authored ? (std::min)(static_cast<size_t>(lod), asset.groups.size() - 1) : 0;
                        if (groupForLod != g) continue;
                        const int meshLevel = authored ? 0 : lod;
                        part.mesh->Bind(ctx, meshLevel);
                        const UINT indexCount = part.mesh->GetIndexCount(meshLevel);
                        for (const DrawRun& run : runs) {
                            ctx->DrawIndexedInstanced(indexCount, run.count, 0, 0, run.start);
                            ++stats.drawCalls;
                            stats.trianglesDrawn += static_cast<int>(indexCount / 3) * static_cast<int>(run.count);
                        }
                    }
                }
            }
        }
    }
    if (rasterSaved) {
        ctx->RSSetState(previousRaster);
        if (previousRaster) previousRaster->Release();
    }
}
