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

ScatterRules MakeRules(const ScatterSpeciesSettings& s, float modelHeight, int speciesIndex, int globalSeed)
{
    ScatterRules r;
    r.spacing = s.spacing; r.minDistance = s.minDistance; r.density = s.density;
    for (int k = 0; k < kSplatChannels; ++k) r.layerDensity[k] = s.layerDensity[k];
    r.minSlope = s.minSlope; r.maxSlope = s.maxSlope; r.minHeight = s.minHeight; r.maxHeight = s.maxHeight;
    r.clusterStrength = s.clusterStrength; r.clusterScale = s.clusterScale;
    r.scaleMin = s.scaleMin; r.scaleMax = s.scaleMax; r.alignToNormal = s.alignToNormal; r.tilt = s.tilt; r.sink = s.sink;
    r.modelHeight = modelHeight;
    // Every species gets its own random stream: the global seed, the species' own seed and its place in the list.
    r.seed = static_cast<std::uint32_t>(globalSeed) * 0x9e3779b1u ^ static_cast<std::uint32_t>(s.seed) * 0x85ebca6bu ^
             static_cast<std::uint32_t>(speciesIndex + 1) * 0xc2b2ae35u;
    ClampScatterRules(r);
    return r;
}
}  // namespace

// ---- Settings -----------------------------------------------------------------------------------------------------------

void ScatterSpeciesSettings::Clamp()
{
    ScatterRules rules = MakeRules(*this, 1.0f, 0, 0);   // the placement rules know their own ranges: borrow them
    spacing = rules.spacing; minDistance = rules.minDistance; density = rules.density;
    for (int k = 0; k < kSplatChannels; ++k) layerDensity[k] = rules.layerDensity[k];
    minSlope = rules.minSlope; maxSlope = rules.maxSlope; minHeight = rules.minHeight; maxHeight = rules.maxHeight;
    clusterStrength = rules.clusterStrength; clusterScale = rules.clusterScale;
    scaleMin = rules.scaleMin; scaleMax = rules.scaleMax; alignToNormal = rules.alignToNormal; tilt = rules.tilt; sink = rules.sink;
    lodScreen[0] = std::clamp(lodScreen[0], 0.003f, 1.0f);
    lodScreen[1] = std::clamp(lodScreen[1], 0.002f, lodScreen[0] * 0.95f);   // each level starts below the one before it
    lodScreen[2] = std::clamp(lodScreen[2], 0.001f, lodScreen[1] * 0.95f);
    cullScreen = std::clamp(cullScreen, 0.0f, lodScreen[2]);
    shadowDistance = std::clamp(shadowDistance, 0.0f, 2000.0f);
}

void ScatterSettings::Clamp()
{
    cellSize = std::clamp(cellSize, kCellMin, kCellMax);
    lodBias = std::clamp(lodBias, ModelRenderSettings::kBiasMin, ModelRenderSettings::kBiasMax);
    reflectionDetail = std::clamp(reflectionDetail, 0.1f, 1.0f);
    generationBudgetMs = std::clamp(generationBudgetMs, 0.5f, 100.0f);
    maxInstancesPerSpecies = std::clamp(maxInstancesPerSpecies, 1000, 20000000);
    for (ScatterSpeciesSettings& s : species) s.Clamp();
}

ScatterSettings ScatterSettings::Starter()
{
    ScatterSettings settings;
    ScatterSpeciesSettings pines;
    pines.name = "Pines";
    pines.model = kBuiltinPine;
    pines.spacing = 7.0f; pines.minDistance = 4.5f; pines.density = 0.9f;
    pines.layerDensity = { 1.0f, 0.0f, 0.0f, 0.0f };   // grass only: sand at the shore and rock on the cliffs stay clear
    pines.maxSlope = 32.0f;
    pines.clusterStrength = 0.6f; pines.clusterScale = 60.0f;
    pines.scaleMin = 0.8f; pines.scaleMax = 1.4f;
    pines.tilt = 2.0f;

    ScatterSpeciesSettings rocks;
    rocks.name = "Boulders";
    rocks.model = kBuiltinRock;
    rocks.spacing = 9.0f; rocks.minDistance = 4.0f; rocks.density = 0.5f;
    rocks.layerDensity = { 0.3f, 1.0f, 0.2f, 0.4f };   // mostly where the terrain is rock, a few lying in the grass
    rocks.maxSlope = 75.0f;
    rocks.scaleMin = 0.6f; rocks.scaleMax = 1.8f;
    rocks.alignToNormal = 0.7f; rocks.tilt = 0.0f; rocks.sink = 0.25f;
    rocks.shadowDistance = 60.0f;
    rocks.cullScreen = 0.006f;
    settings.species = { pines, rocks };
    settings.Clamp();
    return settings;
}

// ---- One species at run time --------------------------------------------------------------------------------------------

struct ScatterObject::Species {
    // The model: meshes (with their levels of detail) and materials.
    bool loaded = false;
    std::string loadedModel;
    std::string error;
    std::vector<std::unique_ptr<SurfaceMaterial>> materials;
    struct DrawPart {
        std::shared_ptr<Mesh> mesh;          // shared: a part with no "_LODn" in its name belongs to every authored level
        int material = 0;
        bool doubleSided = false;
    };
    std::vector<std::vector<DrawPart>> groups;   // one group per AUTHORED level of detail; a single group when there are none
    int levelCount = 1;                          // levels the species switches between
    float height = 1.0f;                         // the model's size at scale 1: height, and the radius of a sphere around it
    float radius = 1.0f;

    // The instances: one slot per grid cell, and ONE GPU buffer holding them all in cell order.
    struct Cell {
        std::vector<ScatterInstance> instances;
        std::uint32_t start = 0, count = 0;      // this cell's range in the GPU buffer
        XMFLOAT3 boundsMin = {}, boundsMax = {}; // world box of everything in the cell, including the plants' own size
        bool dirty = true;
    };
    std::vector<Cell> cells;
    ScatterRules appliedRules;                   // the rules the instances in `cells` were made with
    bool rulesValid = false;
    ID3D11Buffer* instanceBuffer = nullptr;
    UINT capacity = 0;                           // instances the buffer has room for
    std::uint32_t total = 0;
    bool uploadPending = false;
    Clock::time_point lastUpload{};
    ID3D11Buffer* materialCb = nullptr;          // this species' material block (b4)

    // Per view: each cell's level last frame (255 = hidden / none yet), and the draw ranges chosen this frame.
    mutable std::vector<unsigned char> lodState[kViewKindCount];
    mutable std::vector<DrawRun> runs[kMaxLodLevels];

    ~Species()
    {
        if (instanceBuffer) instanceBuffer->Release();
        if (materialCb) materialCb->Release();
    }
    void ClearInstances()
    {
        for (Cell& c : cells) { c.instances.clear(); c.instances.shrink_to_fit(); c.count = 0; c.start = 0; c.dirty = false; }
        total = 0;
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
    // What actually changed is worked out in Refresh: a species with a new model reloads, a species with new rules places
    // its cells again, and drawing options (levels of detail, shadows...) need nothing at all.
}

size_t ScatterObject::GetSpeciesCount() const { return species_.size(); }
bool ScatterObject::IsSpeciesReady(size_t i) const { return i < species_.size() && species_[i]->loaded; }
const std::string& ScatterObject::GetSpeciesError(size_t i) const
{
    static const std::string none;
    return i < species_.size() ? species_[i]->error : none;
}
size_t ScatterObject::GetInstanceCount(size_t i) const { return i < species_.size() ? species_[i]->total : 0; }
int ScatterObject::GetSpeciesLodLevels(size_t i) const { return i < species_.size() ? species_[i]->levelCount : 1; }

bool ScatterObject::GetBoundingSphere(XMFLOAT3& center, float& radius) const
{
    if (!hasGround_) return false;
    center = center_;
    radius = boundsRadius_;
    return true;
}

bool ScatterObject::LoadModel(Species& sp, const ScatterSpeciesSettings& settings)
{
    sp.groups.clear();
    sp.materials.clear();
    sp.error.clear();
    sp.loaded = false;
    sp.loadedModel = settings.model;
    sp.rulesValid = false;   // the model's size feeds the rules (sinking) and the cells' boxes: place everything again

    ImportedModel imported;
    if (IsBuiltinModel(settings.model)) {
        if (!MakeBuiltinModel(settings.model, imported)) { sp.error = "Unknown built-in model \"" + settings.model + "\"."; return false; }
    } else if (settings.model.empty()) {
        sp.error = "No model chosen.";
        return false;
    } else if (!ImportModel(settings.model, imported, sp.error)) {
        return false;
    }
    if (imported.parts.empty()) { sp.error = "The model has no geometry."; return false; }

    // Instances are placed by their BASE, so move every vertex so that the model's bottom centre is the origin (the same
    // pivot ModelObject uses).
    const XMFLOAT3 pivot((imported.boundsMin.x + imported.boundsMax.x) * 0.5f, imported.boundsMin.y, (imported.boundsMin.z + imported.boundsMax.z) * 0.5f);
    const XMFLOAT3 size(imported.boundsMax.x - imported.boundsMin.x, imported.boundsMax.y - imported.boundsMin.y, imported.boundsMax.z - imported.boundsMin.z);
    sp.height = (std::max)(size.y, 1e-3f);
    sp.radius = (std::max)(0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z), 1e-3f);

    for (const ImportedMaterial& source : imported.materials) sp.materials.push_back(BuildSurfaceMaterial(device_, imported, source));
    if (sp.materials.empty()) sp.materials.push_back(std::make_unique<SurfaceMaterial>(device_));

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
    sp.groups.resize(authored ? tags.size() : 1);
    sp.levelCount = 1;

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
        Species::DrawPart built;
        built.mesh = levels.empty() ? std::make_shared<Mesh>(device_, part.vertices, part.indices) : std::make_shared<Mesh>(device_, part.vertices, levels);
        built.material = (std::min)(part.material, static_cast<int>(sp.materials.size()) - 1);
        built.doubleSided = !imported.materials.empty() && part.material < static_cast<int>(imported.materials.size()) && imported.materials[part.material].doubleSided;
        if (!authored) {
            sp.levelCount = (std::max)(sp.levelCount, static_cast<int>(levels.size()));
            sp.groups[0].push_back(built);
        } else if (groupOf[i] < 0) {
            for (auto& group : sp.groups) group.push_back(built);   // untagged: part of every level
        } else {
            sp.groups[std::find(tags.begin(), tags.end(), groupOf[i]) - tags.begin()].push_back(built);
        }
    }
    if (authored) sp.levelCount = static_cast<int>(sp.groups.size());
    sp.levelCount = (std::min)(sp.levelCount, kMaxLodLevels);
    for (auto& group : sp.groups) {   // fewer material switches
        std::stable_sort(group.begin(), group.end(), [](const Species::DrawPart& a, const Species::DrawPart& b) { return a.material < b.material; });
    }

    if (!sp.materialCb) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(SurfaceMaterialCB);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &sp.materialCb))) { sp.error = "Could not create the material buffer."; return false; }
    }
    sp.loaded = true;
    return true;
}

void ScatterObject::SyncSpecies()
{
    while (species_.size() < settings_.species.size()) species_.push_back(std::make_unique<Species>());
    if (species_.size() > settings_.species.size()) species_.resize(settings_.species.size());
    for (size_t i = 0; i < species_.size(); ++i) {
        Species& sp = *species_[i];
        const ScatterSpeciesSettings& cfg = settings_.species[i];
        if (sp.loadedModel != cfg.model || (!sp.loaded && sp.error.empty())) {
            if (!LoadModel(*species_[i], cfg)) LOG_WARNING("Scatter species \"" + cfg.name + "\": " + sp.error);
        }
        if (!cfg.enabled && sp.total > 0) sp.ClearInstances();   // switched off: give the memory back
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
    std::vector<ScatterInstance> all;
    size_t wanted = 0;
    for (const Species::Cell& c : sp.cells) wanted += c.instances.size();
    const size_t cap = static_cast<size_t>(settings_.maxInstancesPerSpecies);
    all.reserve((std::min)(wanted, cap));
    for (Species::Cell& c : sp.cells) {
        c.start = static_cast<std::uint32_t>(all.size());
        const size_t room = cap > all.size() ? cap - all.size() : 0;
        const size_t n = (std::min)(c.instances.size(), room);   // past the cap the remaining cells stay empty
        all.insert(all.end(), c.instances.begin(), c.instances.begin() + static_cast<std::ptrdiff_t>(n));
        c.count = static_cast<std::uint32_t>(n);
    }
    sp.total = static_cast<std::uint32_t>(all.size());
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

    for (const Job& job : jobs) {
        Species& sp = *species_[job.species];
        const ScatterSpeciesSettings& cfg = settings_.species[job.species];
        Species::Cell& cell = sp.cells[static_cast<size_t>(job.cell)];
        const ScatterRect rect = ScatterCellRect(grid_, job.cell % grid_.cellsX, job.cell / grid_.cellsX);
        cell.instances.clear();
        GenerateScatter(ground, sp.appliedRules, rect, cell.instances);
        cell.dirty = false;
        sp.uploadPending = true;
        --pendingCells_;
        if (!cell.instances.empty()) {
            float lowest = cell.instances[0].y, highest = cell.instances[0].y;
            for (const ScatterInstance& inst : cell.instances) { lowest = (std::min)(lowest, inst.y); highest = (std::max)(highest, inst.y); }
            const float side = sp.radius * cfg.scaleMax;                       // plants are wider than their base point
            const float top = (std::max)(sp.height, sp.radius) * cfg.scaleMax; // ... and taller
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
        tallest = (std::max)(tallest, (std::max)(sp.height, sp.radius) * cfg.scaleMax);
        const ScatterRules rules = MakeRules(cfg, sp.height, static_cast<int>(i), settings_.seed);
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

        // 1. CULL and choose a LEVEL for every cell. Cells with the same level that sit side by side in the buffer
        //    merge into one range (AppendRun), so a whole stretch of forest is one draw call.
        const int cellCount = static_cast<int>(sp.cells.size());
        std::vector<unsigned char>& state = sp.lodState[kind];
        if (static_cast<int>(state.size()) != cellCount) state.assign(static_cast<size_t>(cellCount), 255);
        for (auto& runs : sp.runs) runs.clear();
        const int levels = (std::max)(1, sp.levelCount);
        const float lodRadius = sp.radius * 0.5f * (cfg.scaleMin + cfg.scaleMax);   // a typical plant
        const float thresholds[3] = { cfg.lodScreen[0], cfg.lodScreen[1], cfg.lodScreen[2] };
        for (int i = 0; i < cellCount; ++i) {
            const Species::Cell& cell = sp.cells[static_cast<size_t>(i)];
            if (cell.count == 0) continue;
            ++stats.cellsTotal;
            const float distance = DistanceToBox(view.lodOrigin, cell.boundsMin, cell.boundsMax);
            if (shadowPass && distance > cfg.shadowDistance) { state[static_cast<size_t>(i)] = 255; continue; }
            const float screen = ScreenFraction(lodRadius, distance, view.projScale) * lodScale;
            if (BelowCullSize(screen, cfg.cullScreen)) { state[static_cast<size_t>(i)] = 255; continue; }   // too small to matter
            if (settings_.frustumCulling && !view.frustum.IntersectsAABB(cell.boundsMin, cell.boundsMax)) { state[static_cast<size_t>(i)] = 255; continue; }
            if (view.clipBelow && cell.boundsMax.y < view.clipBelowY) { state[static_cast<size_t>(i)] = 255; continue; }   // wholly under the mirror's clip plane
            int lod = 0;
            if (settings_.lodEnabled && levels > 1) {
                const unsigned char previous = state[static_cast<size_t>(i)];
                lod = SelectLod(screen, thresholds, levels, previous == 255 ? -1 : previous);
                state[static_cast<size_t>(i)] = static_cast<unsigned char>(lod);
                if (shadowPass) lod = (std::min)(lod + 1, levels - 1);   // nobody studies a shadow: one level coarser
            }
            AppendRun(sp.runs[lod], cell.start, cell.count);
            ++stats.cellsDrawn;
            stats.instancesDrawn += static_cast<int>(cell.count);
            stats.instancesPerLod[lod] += static_cast<int>(cell.count);
        }
        bool any = false;
        for (int l = 0; l < levels; ++l) if (!sp.runs[l].empty()) any = true;
        if (!any) continue;

        // 2. DRAW. The instance buffer is stream 1; each run is a StartInstanceLocation + an instance count.
        const UINT stride = sizeof(ScatterInstance), offset = 0;
        ctx->IASetVertexBuffers(1, 1, &sp.instanceBuffer, &stride, &offset);
        const bool authored = sp.groups.size() > 1;
        const Shader* applied = nullptr;
        int boundMaterial = -1;
        for (size_t g = 0; g < sp.groups.size(); ++g) {
            for (const Species::DrawPart& part : sp.groups[g]) {
                const SurfaceMaterial& material = *sp.materials[static_cast<size_t>(part.material)];
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
                    const std::vector<DrawRun>& runs = sp.runs[lod];
                    if (runs.empty()) continue;
                    // Authored levels are separate meshes (one group each); generated levels are index buffers of one mesh.
                    const size_t groupForLod = authored ? (std::min)(static_cast<size_t>(lod), sp.groups.size() - 1) : 0;
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
    if (rasterSaved) {
        ctx->RSSetState(previousRaster);
        if (previousRaster) previousRaster->Release();
    }
}
