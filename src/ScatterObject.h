#pragma once
// Trees, rocks, bushes... scattered over the terrain by rules, drawn with GPU instancing (lesson 31).
//
// WHAT HAPPENS (the pipeline, in the order the data flows):
//   1. PLACE.   ScatterPlacement.h decides every position from the terrain's heights and its layer weights (the baked rules
//               plus the paint). The terrain is cut into square CELLS; each cell is generated on its own, nearest to the
//               camera first, a few milliseconds per frame, so even a huge forest never freezes the editor.
//   2. UPLOAD.  All the instances of a species go into ONE vertex buffer, 32 bytes each, grouped by VARIANT and then by
//               cell (a variant is one look of the species: its own mesh and textures). A cell's instances of one variant
//               are a range of that buffer. When you sculpt or paint, only the cells under the brush are generated again.
//   3. CULL.    Every frame, every cell is tested: too small to matter? outside the view? That is a few hundred box tests,
//               not a few hundred thousand.
//   4. DETAIL.  Each surviving cell picks a level of detail from how big its trees look on screen (LodSelect.h).
//   5. DRAW.    For each variant, neighbouring cells that picked the same level are merged into one range, and each range
//               is ONE DrawIndexedInstanced call: the GPU draws thousands of copies of the mesh, each placed by its 32 bytes.
#include "LodSelect.h"
#include "ModelObject.h"
#include "ScatterPlacement.h"
#include "SceneObject.h"
#include "Shader.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Terrain;

// The artist's replacements for ONE material of a variant's model (lesson 36). Empty file = keep what the model says.
// This is how three rocks can share one mesh and still look like three different stones.
struct ScatterMaterialOverride {
    int slot = 0;                                       // which material of the model (the editor lists them by name)
    std::string albedo, normal, orm, emissive;          // image files. ORM: R = occlusion, G = roughness, B = metallic
    std::array<float, 4> baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };   // linear tint: multiplies the model's base colour
    float roughness = -1.0f;                            // >= 0 replaces the roughness (scales an ORM map's, or is the constant); < 0 keeps it
    bool flipGreen = true;                              // only for a replaced normal map: OpenGL-style (true, glTF) or DirectX-style (false)
};

// One look of a species: its own mesh and materials, and how often and where it is chosen (lesson 36).
struct ScatterVariantSettings {
    std::string name = "Variant";
    std::string model = "builtin:rock";   // a model file or a "builtin:..." name (ProceduralModels.h)
    bool enabled = true;
    float weight = 1.0f;                  // relative chance among the variants the ground allows
    std::array<float, 4> layerBias = { 1.0f, 1.0f, 1.0f, 1.0f };   // x the weight on grass / rock / sand / snow
    float minSlope = 0.0f;                // degrees: only on ground in this band
    float maxSlope = 90.0f;
    float scaleMul = 1.0f;                // multiplies the species' scale range
    std::vector<ScatterMaterialOverride> materials;
    void Clamp();
};

// What one species (pine, birch, boulder...) needs. Plain data: the editor edits it, the scene file stores it.
// A species with no `variants` is the one-look species of lesson 31: `model` is its only variant. As soon as `variants`
// has an entry, those are the looks and `model` is ignored (the editor moves `model` into the list when you add a second).
struct ScatterSpeciesSettings {
    std::string name = "Species";
    std::string model = "builtin:pine";   // a model file, or "builtin:pine" / "builtin:rock"
    bool enabled = true;
    std::vector<ScatterVariantSettings> variants;   // up to kMaxScatterVariants

    // WHERE it grows (ScatterRules in ScatterPlacement.h)
    float spacing = 6.0f;                 // metres: at most one plant per spacing x spacing square
    float minDistance = 3.5f;             // metres: two plants are never closer than this
    float density = 1.0f;                 // 0..1: the chance on perfect ground
    std::array<float, 4> layerDensity = { 1.0f, 0.0f, 0.0f, 0.0f };   // how much grass, rock, sand and snow like it
    float minSlope = 0.0f;                // degrees
    float maxSlope = 35.0f;
    float minHeight = -1000.0f;           // world Y
    float maxHeight = 5000.0f;
    float clusterStrength = 0.0f;
    float clusterScale = 40.0f;

    // HOW each one looks
    float scaleMin = 0.8f;
    float scaleMax = 1.3f;
    float alignToNormal = 0.0f;
    float tilt = 3.0f;
    float sink = 0.0f;
    int seed = 1;

    // HOW it is drawn
    std::array<float, 3> lodScreen = { 0.20f, 0.08f, 0.03f };   // level 1, 2, 3 start below these screen fractions
    float cullScreen = 0.004f;            // below this fraction of the screen (about 4 pixels at 1080p) it is not drawn
    bool castShadows = true;
    float shadowDistance = 120.0f;        // metres from the camera; farther cells cast no shadow

    void Clamp();
    // The looks the scatter actually uses: `variants`, or a single one made from `model` when there are none.
    std::vector<ScatterVariantSettings> EffectiveVariants() const;
};

// The placement rules of one species, made from its settings (ScatterSettings.cpp: no Direct3D, tested).
// `looks` are EffectiveVariants(); `modelHeights` and `usable` say, per look, how tall its loaded mesh is and whether it can be drawn at all.
// A look that is switched off or whose model did not load gets weight 0: it is never chosen, so the others take its place.
ScatterRules MakeScatterRules(const ScatterSpeciesSettings& species, const std::vector<ScatterVariantSettings>& looks,
                              const std::vector<float>& modelHeights, const std::vector<char>& usable, int speciesIndex, int globalSeed);

struct ScatterSettings {
    bool enabled = true;
    float cellSize = 32.0f;               // metres: the unit of culling and regeneration
    int seed = 1;                         // changes every species' forest at once
    float lodBias = 1.0f;                 // above 1: stay detailed for longer
    bool frustumCulling = true;
    bool lodEnabled = true;
    bool shadows = true;                  // the master switch for scatter in the sun's pass
    float reflectionDetail = 0.5f;        // scales what the water mirror draws: 0.5 = half the distance (mirrors need less)
    float generationBudgetMs = 6.0f;      // how long each frame may spend placing cells
    int maxInstancesPerSpecies = 1000000;
    std::vector<ScatterSpeciesSettings> species;

    static constexpr float kCellMin = 8.0f, kCellMax = 128.0f;
    void Clamp();
    // A pine forest on the grass and boulders on the slopes: what "Add > Scatter" starts with.
    static ScatterSettings Starter();
};

// What one view (camera / mirror / sun) drew of the scatter last frame - shown in the editor.
struct ScatterViewStats {
    int cellsTotal = 0;
    int cellsDrawn = 0;                   // cells that survived culling (summed over species)
    int instancesTotal = 0;
    int instancesDrawn = 0;
    int drawCalls = 0;
    int trianglesDrawn = 0;
    int instancesPerLod[kMaxLodLevels] = {};
};

class ScatterObject final : public SceneObject {
public:
    explicit ScatterObject(ID3D11Device* device);
    ~ScatterObject() override;

    const char* GetEditorName() const override { return "Scatter"; }
    const char* GetTypeName() const override { return "Scatter"; }
    DirectX::XMFLOAT3 GetSortPosition() const override { return center_; }
    bool CanDelete() const override { return true; }
    bool CastsShadow() const override { return settings_.enabled && settings_.shadows && hasGround_; }
    bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
    void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;
    void Update(float) override {}
    void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

    const ScatterSettings& GetSettings() const { return settings_; }
    // Takes effect at the next Refresh: what changed is worked out then (new model? new rules? only drawing options?).
    void SetSettings(ScatterSettings settings);

    // Called by Scene::Update every frame. Reads the ground, regenerates the cells that need it (within the time budget,
    // nearest to `cameraPosition` first) and refreshes the GPU buffers.
    void Refresh(const Terrain* terrain, const DirectX::XMFLOAT3& cameraPosition);
    // Places every pending cell now, ignoring the budget (tools, tests, loading a scene for a screenshot).
    void GenerateAllNow(const Terrain* terrain);

    // For the editor.
    size_t GetSpeciesCount() const;
    bool IsSpeciesReady(size_t index) const;
    const std::string& GetSpeciesError(size_t index) const;
    size_t GetInstanceCount(size_t index) const;
    int GetSpeciesLodLevels(size_t index) const;
    // Per variant (index = its place in ScatterSpeciesSettings::EffectiveVariants()).
    size_t GetVariantCount(size_t species) const;
    bool IsVariantReady(size_t species, size_t variant) const;
    const std::string& GetVariantError(size_t species, size_t variant) const;
    size_t GetVariantInstanceCount(size_t species, size_t variant) const;
    size_t GetVariantMaterialCount(size_t species, size_t variant) const;
    std::string GetVariantMaterialName(size_t species, size_t variant, size_t slot) const;
    int GetPendingCells() const { return pendingCells_; }
    bool HasGround() const { return hasGround_; }
    const ScatterViewStats& GetViewStats(ViewKind kind) const { return stats_[static_cast<int>(kind)]; }

private:
    struct ModelAsset;   // a loaded model file: meshes and levels of detail, shared by every variant that uses the file
    struct Variant;      // one look: a ModelAsset plus its own materials
    struct Species;
    void Draw(ID3D11DeviceContext* ctx, const RenderView& view, bool shadowPass) const;
    void SyncSpecies();
    std::shared_ptr<ModelAsset> AcquireModel(const std::string& model, std::string& error);
    void LoadVariant(Variant& variant, const ScatterVariantSettings& settings);
    void ResetGrid(const ScatterRect& extent);
    void MarkDirty(const ScatterRect& rect);
    void GenerateCells(const struct ScatterGround& ground, const DirectX::XMFLOAT3& cameraPosition, double budgetMs);
    void UploadInstances(Species& species);
    ID3D11RasterizerState* GetCullNone() const;

    ID3D11Device* device_ = nullptr;
    ScatterSettings settings_;
    std::vector<std::unique_ptr<Species>> species_;
    std::shared_ptr<Shader> litShader_, shadowShader_, shadowAlphaShader_;
    mutable ID3D11RasterizerState* cullNone_ = nullptr;
    std::vector<std::vector<ScatterVariantSettings>> looks_;   // per species: EffectiveVariants(), made once in SetSettings
    std::map<std::string, std::shared_ptr<ModelAsset>> modelCache_;   // by model name; dropped when no variant uses it any more

    // The ground this was generated for.
    SceneObjectId terrainId_ = 0;
    unsigned surfaceRevision_ = 0;
    bool hasGround_ = false;
    bool needsFullRebuild_ = true;
    ScatterRect extent_;
    ScatterGrid grid_;
    int pendingCells_ = 0;
    DirectX::XMFLOAT3 center_ = {};
    float boundsRadius_ = 0.0f;
    mutable ScatterViewStats stats_[kViewKindCount];
};
