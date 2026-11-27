#pragma once
#include <cstdint>
#include "Renderable.h"
#include "SceneObject.h"
#include "Material.h"
#include "OceanGpu.h"
#include "OceanSea.h"
#include "RenderView.h"
#include "UnderwaterLook.h"
#include "WaterMesh.h"
#include "WaterPlan.h"
#include "WaterSettings.h"
#include "WaterShore.h"
#include "WaterWaves.h"
#include "Weather.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

class Terrain;
class TerrainWorld;


// What one draw of the water did, per kind of view (camera, mirror, sun).
struct WaterViewStats {
	int tilesTotal = 0;
	int tilesDrawn = 0;
	int tilesDry = 0;          // tiles skipped because the ground under them stands above the water (nothing to show)
	int tilesPerLod[kWaterLodCount] = {};
	int trianglesDrawn = 0;
	int reflectionSlot = -1;   // which planar reflection image this body read (-1 = the sky cubemap)
};

class Water final : public SceneObject {
public:
	explicit Water(ID3D11Device* device);
	~Water();

	// Several water bodies can exist, so each shows its own name (Scene::Adopt gives "Water 7" and so on).
	const char* GetEditorName() const override { return GetName().empty() ? "Water" : GetName().c_str(); }
	const char* GetTypeName() const override { return "Water"; }
	bool CanClone() const noexcept override { return true; }
	std::unique_ptr<SceneObject> Clone(ID3D11Device* device) const override;
	bool CanDelete() const override { return true; }
	// Water is sized by its Width and Length (the Scale tool edits those), and stays level: it can be turned but not tilted (lesson 42).
	bool SupportsScale() const override { return false; }
	// Where a click ray meets the water's surface (the level plane), provided that spot is really water: over the plane and over wet ground.
	// `distance` = how far along the ray.
	bool RayHit(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float& distance) const;
	// A sphere around the whole body, so the editor's Frame command works. (Water casts no shadow: the sun ignores it.)
	bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
	RenderCategory GetRenderCategory() const override { return RenderCategory::Transparent; }
	DirectX::XMFLOAT3 GetSortPosition() const override { return position_; }
	bool GetEditorTransform(DirectX::XMFLOAT3& position,
							DirectX::XMFLOAT3& rotation,
							DirectX::XMFLOAT3& scale) const override;
	bool SetEditorTransform(const DirectX::XMFLOAT3& position,
							const DirectX::XMFLOAT3& rotation,
							const DirectX::XMFLOAT3& scale) override;
	bool IsValid() const;
	const WaterSettings& GetSettings() const { return settings_; }
	// The direction the surface travels toward, in WORLD space, as an angle (0 = +X, 90 = +Z): the scene's wind.
	float GetTravelDegrees() const;
	// The scene's wind, pushed in by the scene every frame. The waves are rebuilt only when it actually changed: the wind makes the sea
	// (there is no wind of a body's own: lesson 63). The ocean is the open sea, so nothing but the wind limits its waves.
	void SetSceneWind(const WindSettings& wind);
	// The scene clock, pushed in by the scene every frame. The GPU makes the sea for exactly this moment, and SampleHeight is asked for the same
	// moment by boats, so what is drawn and what floats agree.
	void SetSceneTime(float seconds) { sceneTime_ = seconds; }
	// What the wind makes of this body right now: how tall, how long and how fast its biggest waves are (the editor shows it).
	const SeaState::Summary& GetSeaSummary() const { return seaSummary_; }
	void SetSettings(const WaterSettings& settings);
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

	// --- for gameplay code: floating objects, swimming, splashes ---
	// `time` is the scene clock (Scene::GetTime()): the waves on screen are exactly the waves measured here.
	// World Y of the surface above/below (x, z). Returns the rest level when the point is outside the plane.
	float SampleHeight(float x, float z, float time) const;
	// Unit surface normal at (x, z), from the same waves.
	DirectX::XMFLOAT3 SampleNormal(float x, float z, float time) const;
	// Is (x, z) inside this body's rectangle (turned and scaled like the object)?
	bool IsOverWater(float x, float z) const;
	// Distance from a point to the nearest part of this body's box (0 inside it). Used to decide which bodies get mirrors.
	float DistanceTo(const DirectX::XMFLOAT3& point) const;

	// --- for the scene and the renderer ---
	// True if any part of the water (including its tallest waves) can be inside this frustum.
	bool IsVisible(const Frustum& frustum) const;
	// True if some tile that is not hidden under dry ground can be inside this frustum. The scene asks this before paying for
	// a mirror image: a body whose only visible tiles lie under the terrain has nothing to reflect.
	bool IsWetVisible(const Frustum& frustum) const;

	// --- the ground under the water (src/WaterShore.h) ---
	// The scene calls this every frame. It measures the terrain under this body (depth = level - ground height) into a
	// small map that the shaders and SampleHeight read, and only measures AGAIN when the terrain, the body's place or size,
	// or its level changed. No terrain (an empty list): the body then has no shore effects at all. A world is many terrains
	// (tiles): the ground at a point is the highest of those that cover it. `groundStamp` changes whenever any of them is
	// added, removed or edited (Scene::Update makes it), so a tile streaming in under the water makes it measure again.
	// `worlds` are the terrain worlds: their ground comes from the world's GROUND FUNCTION (a grid of the same heights the tiles are made
	// from), so it is the same whether a tile is loaded, still streaming in, or drawn at a coarser level of detail (roadmap W1). `terrains`
	// are the single terrains (and any tile that has no world). The ground at a point is the highest of all that cover it.
	void RefreshShoreDepth(const std::vector<const TerrainWorld*>& worlds, std::uint64_t groundStamp);
	bool HasShoreDepth() const { return depthView_ != nullptr; }
	void GetShoreMapSize(int& width, int& height) const { width = depthField_.Width(); height = depthField_.Height(); }
	const WaterViewStats& GetStats(ViewKind kind) const { return stats_[static_cast<int>(kind)]; }

	// --- for the editor's Waves and Performance panels (read-only numbers about the mesh) ---
	// One grid cell of the finest level of the mesh, in world units. The waves the mesh can show are no shorter than about six of these.
	float FinestCell() const;
private:
	void CreateRenderStates(ID3D11Device* device);
	void RebuildMesh(const WaterMesh::TileGrid& grid);
	void RebuildWaves();
	void SimulateSea(ID3D11DeviceContext* ctx) const;   // runs the compute passes for the scene clock, once per moment
	void EnsureMesh();                    // rebuilds the tiles when the mesh detail or the body's proportions need a different grid
	void UploadMaterial(bool mirrored) const;   // `mirrored`: this frame, a mirror image is bound for this body
	void UpdateWorldTransform();
	void ReleaseShoreMap();
	float DepthAtWorld(float x, float z) const;   // water depth under a world position, from the CPU copy of the map
	float ExtentX() const { return settings_.size * std::abs(scale_.x); }   // world size along X
	float ExtentZ() const { return settings_.sizeZ * std::abs(scale_.z); }
	void ChooseTiles(const RenderView& view) const;
	void UpdateDryTiles();                // flags the tiles that lie wholly under dry ground (needs the shore map and the wave table)

	struct Tile {
		DirectX::XMFLOAT3 localMin, localMax;   // the tile's box in the plane's own space ([-1, 1] across)
	};

	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	WaterSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
	Material material_;       // alpha blending, no depth writes
	Material wireMaterial_;   // the same, drawn as wires (debug)
	// The FFT sea (roadmap O2): one sea in two copies, built from the same numbers. The CPU's gives heights (SampleHeight, the margins); the GPU's makes
	// the maps the shaders draw. Rebuilt only when the wind, the seed or the height scale change (RebuildWaves).
	mutable OceanSea::Sea sea_;     // mutable: asking a height at a new time updates it, and SampleHeight is const
	mutable OceanGpu gpuSea_;       // mutable: Render is const and runs the passes
	SeaSpectrum::Settings builtSea_;
	bool seaBuilt_ = false;
	float seaBound_ = 0.5f;         // a height the sea practically never passes (6 sigma): the mesh margins and the dry-tile test use it
	std::array<float, 3> seaPatch_{ 2000.0f, 250.0f, 30.0f };   // the three cascades' patch sizes in metres (what the shaders divide a position by)
	float sceneTime_ = 0.0f;
	mutable bool gpuSimDone_ = false;
	mutable float gpuSimTime_ = 0.0f, gpuSimChoppiness_ = 0.0f;   // what the GPU's maps were last made for
	ID3D11SamplerState* oceanSampler_ = nullptr;   // WRAP, trilinear + anisotropic: the sea's patches repeat
	// The measured ground under the body. Three copies of one thing: the CPU map (for SampleHeight), the texture and its view.
	WaterShore::DepthField depthField_;
	ID3D11Texture2D* depthTexture_ = nullptr;
	ID3D11ShaderResourceView* depthView_ = nullptr;
	DirectX::XMFLOAT4X4 worldToLocal_{};
	bool worldToLocalValid_ = false;
	std::array<float, 12> shoreKey_{};     // body place, turn, scale, size and level when the map was last measured...
	std::uint64_t shoreGroundStamp_ = 0;   // ...and which terrains, in which version
	bool shoreKeyValid_ = false;
	std::vector<Tile> tiles_;
	WindSettings sceneWind_;                // the scene's wind as last pushed by the scene (SetSceneWind)
	SeaState::Summary seaSummary_;          // what that wind makes of this body (RebuildWaves)
	std::vector<unsigned char> tileDry_;   // per tile: 1 = ground stands above the highest the surface can reach, nothing to draw
	int builtMeshDetail_ = -1;
	WaterMesh::TileGrid builtGrid_;
	bool gridBuilt_ = false;
	int cellsPerTile_ = 0;
	int trianglesPerLod_[kWaterLodCount] = {};
	mutable std::vector<unsigned char> drawList_;    // per tile: level of detail, or Model::kSkipMesh
	mutable std::vector<unsigned char> tileLod_;     // the level each tile used last frame (for hysteresis)
	mutable WaterViewStats stats_[kViewKindCount];
};
