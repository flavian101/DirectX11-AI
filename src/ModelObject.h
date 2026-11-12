#pragma once
#include "Buoyancy.h"
#include "HullMask.h"
#include "LodSelect.h"
#include "ModelImporter.h"
#include "Renderable.h"
#include "SceneObject.h"
#include "SurfaceMaterial.h"
#include <DirectXMath.h>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// One SurfaceMaterial built from an imported material: its textures, colour factors, roughness and metallic. Shared by
// ModelObject and ScatterObject, so a tree looks the same whether you placed it by hand or scattered a thousand of it.
class Water;

std::unique_ptr<SurfaceMaterial> BuildSurfaceMaterial(ID3D11Device* device, const ImportedModel& model, const ImportedMaterial& source);

// How a model is culled and simplified while it is drawn (lesson 31). Saved with the scene.
struct ModelRenderSettings {
    bool frustumCulling = true;   // skip parts the camera (or the sun) cannot see
    bool lodEnabled = true;       // draw simpler versions of parts that look small
    float lodBias = 1.0f;         // above 1: stay detailed for longer; below 1: simplify sooner
    std::array<float, 3> lodScreen = { 0.20f, 0.08f, 0.03f };   // a part uses level 1, 2, 3 once it covers less than this fraction of the screen
    float cullScreen = 0.0f;      // a part covering less than this fraction of the screen is not drawn at all (0 = never)

    static constexpr float kBiasMin = 0.25f, kBiasMax = 4.0f;
    void Clamp();                 // keeps the thresholds in order (biggest first) and in range
};

// A model that floats (lesson 33): it rides the water under it, and can hide the water inside its hull. Saved with the scene.
// Lengths are fractions of the model's height (or of the hull's beam), so they stay right when the model is scaled.
struct FloatingSettings {
    bool enabled = false;          // ride the water: height and lean follow the waves under the model
    float draft = 0.15f;           // how deep the keel (the model's bottom) sits below the surface, as a fraction of the model's height
    float responsiveness = 3.0f;   // how stiff the spring is that follows the water (per second): high = stiff, low = lazy
    float damping = 0.6f;          // 1 = no bounce; lower = it overshoots and rocks a little
    float tilt = 1.0f;             // 0 = always level, 1 = leans fully with the waves
    bool hullMask = false;         // hide the water inside the hull (a lid written into the depth buffer, see HullMask.h)
    int maskPart = -1;             // which part's outline is the hull: -1 = the part with the biggest outline
    float maskHeight = -1.0f;      // lid height as a fraction of the model's height; negative = just under the hull part's top
    float maskInset = 0.03f;       // how far the lid is kept in from the outline, as a fraction of the hull's width

    void Clamp();
};

// What one view (camera / mirror / sun) drew of a model last frame - shown in the editor.
struct ModelViewStats {
    int partsTotal = 0;
    int partsDrawn = 0;
    int trianglesDrawn = 0;
    int partsPerLod[kMaxLodLevels] = {};
};

// A model loaded from a file (glTF, FBX, OBJ...), shown in the scene. Takes the importer's CPU data,
// uploads each part to the GPU and draws it with the material the file asked for.
class ModelObject final : public SceneObject {
public:
    ModelObject(ID3D11Device* device, ImportedModel&& imported);
    ~ModelObject() override;

    const char* GetEditorName() const override { return displayName_.c_str(); }
    const char* GetTypeName() const override { return "Model"; }
    DirectX::XMFLOAT3 GetSortPosition() const override { return position_; }
    bool GetEditorTransform(DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& rotation, DirectX::XMFLOAT3& scale) const override;
    bool SetEditorTransform(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation,
                            const DirectX::XMFLOAT3& scale) override;
    bool CastsShadow() const override { return true; }
    bool GetOrientedBounds(DirectX::XMFLOAT4X4& world, DirectX::XMFLOAT3& boundsMin, DirectX::XMFLOAT3& boundsMax) const override;
    bool IsUserContent() const override { return true; }
    bool CanClone() const noexcept override { return !sourcePath_.empty(); }
    // Reads the file again (the CPU data was not kept) and copies this object's transform and material settings.
    std::unique_ptr<SceneObject> Clone(ID3D11Device* device) const override;
    bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
    void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;
    void Update(float) override {}
    void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

    // --- floating (lesson 33) ---
    const FloatingSettings& GetFloatingSettings() const { return floating_; }
    void SetFloatingSettings(FloatingSettings settings);
    // World position of the model's base centre: where to look for the water under it.
    DirectX::XMFLOAT3 GetFloatAnchor() const { return position_; }
    // One step of the floating spring. `water` is the body under the model (null: there is none, so it stays where it is).
    // `time` is the scene clock, so the waves read here are the waves on screen. Does nothing unless floating is enabled.
    void StepFloating(const Water* water, float time, float dt);
    // Writes the hull's lid into the DEPTH buffer (no colour), so the water drawn after it is hidden inside the hull.
    // Called by the scene after the opaque phase, before the water.
    void RenderHullMask(ID3D11DeviceContext* ctx, const RenderView& view) const;
    // For the Inspector.
    const std::string& GetPartName(size_t index) const { return parts_[index].name; }
    int GetAutoHullPart() const { return autoHullPart_; }
    float GetModelHeight() const { return (boundsMax_.y - boundsMin_.y) * std::fabs(scale_.y); }   // in world units

    bool IsValid() const { return !parts_.empty(); }
    // Scales the model uniformly so its longest side is `size` world units. (Files come in any unit:
    // a castle may be 0.01 or 500 units wide.)
    void FitToSize(float size);

    // For the Inspector.
    const std::string& GetSourcePath() const { return sourcePath_; }
    size_t GetPartCount() const { return parts_.size(); }
    size_t GetMaterialCount() const { return materials_.size(); }
    SurfaceMaterial& GetMaterial(size_t index) { return *materials_[index]; }
    const SurfaceMaterial& GetMaterial(size_t index) const { return *materials_[index]; }
    const std::string& GetMaterialName(size_t index) const { return materialNames_[index]; }
    std::uint32_t GetTriangleCount() const { return triangleCount_; }

    // Culling and level of detail (lesson 31).
    const ModelRenderSettings& GetRenderSettings() const { return renderSettings_; }
    void SetRenderSettings(ModelRenderSettings settings) { settings.Clamp(); renderSettings_ = settings; }
    // How many levels the model has: its authored ones ("Pine_LOD0", "Pine_LOD1"...) or the most any part got generated.
    int GetLodLevelCount() const { return authoredGroups_.size() > 1 ? static_cast<int>(authoredGroups_.size()) : generatedLevels_; }
    bool HasAuthoredLods() const { return authoredGroups_.size() > 1; }
    const ModelViewStats& GetViewStats(ViewKind kind) const { return stats_[static_cast<int>(kind)]; }

private:
    struct Part {
        std::string name;                     // from the file: lets the Inspector offer "which part is the hull"
        std::vector<HullMask::Point2> footprint;   // the part seen from above (x, z), its convex outline, model space
        float footprintArea = 0.0f;
        std::unique_ptr<Renderable> renderable;
        int material = 0;
        bool doubleSided = false;
        int lodGroup = -1;                    // the part's authored level ("Pine_LOD2" -> 2), or -1: not tagged
        int levels = 1;                       // generated levels of detail of its mesh (1 = none)
        std::uint32_t levelTriangles[kMaxLodLevels] = {};   // triangles of each level
        DirectX::XMFLOAT3 boundsMin = {}, boundsMax = {};    // model space
        DirectX::XMFLOAT3 worldMin = {}, worldMax = {};      // world-space box (refreshed when the transform changes)
        DirectX::XMFLOAT3 worldCenter = {};
        float worldRadius = 0.0f;
    };

    void UpdateWorldTransform();
    // The common body of Render and RenderShadow: culls, picks a level and draws. `shader` is the shadow pass's depth-only
    // shader, or null for the normal lit draw.
    void DrawParts(ID3D11DeviceContext* ctx, const Shader* shader, const RenderView& view) const;
    ID3D11RasterizerState* GetCullNone() const;
    int ResolveMaskPart() const;                       // the part whose outline is the hull, or -1
    void EnsureHullMask(int part) const;               // (re)builds the lid when its settings changed

    ID3D11Device* device_ = nullptr;
    std::string displayName_;
    std::string sourcePath_;
    std::vector<Part> parts_;                                // sorted by material: fewer texture switches
    std::vector<std::unique_ptr<SurfaceMaterial>> materials_;
    std::vector<std::string> materialNames_;
    std::uint32_t triangleCount_ = 0;
    mutable ID3D11RasterizerState* cullNone_ = nullptr;      // for double-sided materials; created on first use

    // Floating (lesson 33).
    FloatingSettings floating_;
    int autoHullPart_ = -1;                       // the part with the biggest outline seen from above
    bool floatReady_ = false;                     // false until the first step: then the springs start AT the water, not at the author's height
    Buoyancy::Spring floatHeave_;                 // world height of the keel
    Buoyancy::Spring floatSlopeX_, floatSlopeZ_;  // how the surface leans under the hull (world x and z)
    // The lid: a flat fan over the hull's outline, in model space, drawn depth-only. Rebuilt when its key changes.
    mutable std::unique_ptr<Renderable> maskRenderable_;
    mutable float maskKey_[3] = { -1.0f, -1.0f, -1.0f };   // part, lid height, inset that the current lid was built with
    mutable ID3D11BlendState* maskBlend_ = nullptr;        // writes no colour
    mutable ID3D11DepthStencilState* maskDepth_ = nullptr; // tests and WRITES depth

    // Model space: the box around everything, and the pivot (bottom centre) that scale/rotation act around.
    DirectX::XMFLOAT3 boundsMin_ = {}, boundsMax_ = {};
    DirectX::XMFLOAT3 pivot_ = {};
    DirectX::XMFLOAT3 position_ = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4X4 world_;

    ModelRenderSettings renderSettings_;
    std::vector<int> authoredGroups_;     // the distinct authored LOD numbers, ascending; fewer than 2 = the model has none
    int generatedLevels_ = 1;
    DirectX::XMFLOAT3 worldMin_ = {}, worldMax_ = {};   // the whole model's world-space box
    DirectX::XMFLOAT3 worldCenter_ = {};
    float worldRadius_ = 0.0f;
    // Per view (camera / mirror / sun): the level each part used last frame (255 = none yet), for hysteresis.
    mutable std::vector<unsigned char> partLod_[kViewKindCount];
    mutable unsigned char modelLod_[kViewKindCount] = { 255, 255, 255 };
    mutable std::vector<unsigned char> drawList_;       // scratch: one byte, the level of the part being drawn
    mutable ModelViewStats stats_[kViewKindCount];
};
