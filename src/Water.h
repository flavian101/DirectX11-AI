#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Material.h"
#include "RenderView.h"
#include "WaterMesh.h"
#include "WaterPlan.h"
#include "WaterShore.h"
#include "WaterWaves.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

class Terrain;
enum class WavePreset { Calm, Choppy, Stormy };

// A complete colour/optics look (not the waves, not the level or size). Pick one in the editor, then tweak.
enum class WaterLook { Tropical, Ocean, Lake, Swamp, Arctic };

// What KIND of water this is. It decides how the surface moves: an ocean rolls in swells from the wind, a lake shows
// short wind chop, a pond barely moves, and a RIVER is a current: everything on it is carried downstream.
// Values are saved in scene files by name: only ever ADD to the end.
enum class WaterBodyType : int { Ocean = 0, Lake = 1, River = 2, Pond = 3 };

// What the water mirrors. "Sky" is the cheap cubemap from before (reflects only the sky);
// the planar modes draw the whole scene again from below the water (reflects terrain, cube, sky),
// at a quarter / half / full of the screen's resolution. Values are saved in scene files: only ever ADD to the end.
enum class ReflectionMode : int { Sky = 0, PlanarHalf = 1, PlanarFull = 2, PlanarQuarter = 3 };

// The outline of a water body, seen from above. Values are saved in scene files: only ever ADD to the end.
// A round pond is the square plane cut to its inscribed ellipse; a river is a long thin rectangle.
enum class WaterShape : int { Rectangle = 0, Ellipse = 1 };

// Shows one ingredient of the water on its own, to tune it. Never saved with the scene.
enum class WaterDebugView : int { Off = 0, Normals = 1, Depth = 2, Foam = 3, Fresnel = 4, CrestHeight = 5, ShoreDepth = 6 };

constexpr int kWaterLodCount = 4;   // level 0 = every vertex; each next level keeps fewer (the tile interior thins out, the border never does)

struct WaterSettings {
	// --- what kind of water ---
	WaterBodyType bodyType = WaterBodyType::Ocean;
	float flowSpeed = 2.0f;       // river: how fast the surface is carried downstream, world units per second
	float flowAngle = 0.0f;       // river: degrees away from the body's own +Z axis (its length). 0 = flows toward +Z of the body

	// --- the shore: how the water reacts to the ground under it (needs a terrain) ---
	float shoreWaves = 1.0f;      // 0 = waves identical everywhere (the old look); 1 = they grow in shallow water, then break and die at the shore
	float swash = 0.6f;           // how far the waterline (foam and the see-through edge) slides up and down the beach with each wave

	// --- surface ---
	float height = 4.0f;          // world Y of the water plane (kept in sync with the editor transform)
	float size = 180.0f;          // width: the plane's extent along X
	float sizeZ = 180.0f;         // length: the plane's extent along Z (a river is a long, thin rectangle)
	WaterShape shape = WaterShape::Rectangle;
	DirectX::XMFLOAT3 color = { 0.05f, 0.25f, 0.45f };   // DEEP water colour
	float alpha = 0.68f;                                 // opacity of deep water

	// --- depth: how the water changes between shore and open sea ---
	DirectX::XMFLOAT3 shallowColor = { 0.10f, 0.55f, 0.60f };
	float absorption = 0.35f;     // how fast light is absorbed with depth (per world unit)
	float fadeDistance = 2.5f;    // depth at which the water becomes fully opaque (shore softness)
	float foamDistance = 0.9f;    // width of the shoreline foam band, in depth units
	float foamStrength = 0.9f;

	// --- waves ---
	float waveAmplitude = 0.35f;
	float waveSpeed = 1.0f;
	float waveFrequency = 0.6f;   // radians per world unit of the base wave (0.6 = ~10 unit wavelength)
	float windDirection = 20.0f;  // degrees: the direction the waves travel toward (0 = +X, 90 = +Z)
	float directionalSpread = 0.7f;   // 0 = all waves run with the wind (a clean swell), 1 = waves from every which way
	float choppiness = 0.5f;      // 0 = rounded sine waves, 1 = sharp crests and wide troughs
	int waveCount = 6;            // 2..8 waves summed. Fewer = cheaper and simpler
	float detailNormal = 0.5f;    // strength of the small ripples on top of the waves (0 = off, saves pixel work)
	float detailScale = 0.8f;     // ripples per world unit
	float detailDistance = 90.0f; // ripples fade out by this distance from the camera

	// --- lighting ---
	float reflectivity = 1.0f;    // 0 = no sky reflection, 1 = full Fresnel reflection
	ReflectionMode reflectionMode = ReflectionMode::PlanarHalf;
	float reflectionDistortion = 0.04f;   // how far the waves wobble the mirror image (fraction of the screen)
	float refraction = 0.04f;     // how far waves bend the view of the bottom (fraction of the screen). 0 = off, skips a screen copy
	float scatter = 0.5f;         // glow of crests lit from behind
	float glintStrength = 1.0f;   // brightness of the sun highlight
	float glintSharpness = 128.0f;// higher = smaller, tighter highlight
	float crestFoam = 0.5f;       // foam on the steepest wave crests (0 = shoreline foam only)
	float crestFoamThreshold = 0.25f;   // higher = only the sharpest crests foam

	// --- mesh and speed ---
	int meshDetail = 1;           // 0 = 128, 1 = 256, 2 = 512 grid cells across the full plane at the finest level (rebuilds the mesh)
	float lodDistance = 60.0f;    // full mesh detail until this far from the camera; coarser (4 levels) beyond
	bool frustumCulling = true;   // skip the parts of the water the camera cannot see

	// --- debugging: not saved in scene files ---
	WaterDebugView debugView = WaterDebugView::Off;
	bool wireframe = false;       // draw the mesh as wires: shows the level-of-detail tiles

	static constexpr float kFlowSpeedMax = 10.0f;
	static constexpr float kSwashMax = 1.5f;
	static constexpr float kHeightMin = -100.0f, kHeightMax = 200.0f;
	static constexpr float kSizeMin = 2.0f, kSizeMax = 2000.0f;
	static constexpr float kAmplitudeMax = 2.0f, kSpeedMax = 5.0f;
	static constexpr float kFrequencyMin = 0.05f, kFrequencyMax = 2.0f;
	static constexpr int kWaveCountMin = 2, kWaveCountMax = WaterWaves::kMaxWaves;
	static constexpr float kDetailMax = 2.0f, kDetailScaleMin = 0.1f, kDetailScaleMax = 4.0f;
	static constexpr float kDetailDistanceMin = 10.0f, kDetailDistanceMax = 500.0f;
	static constexpr float kGlintMax = 3.0f;
	static constexpr float kSharpnessMin = 8.0f, kSharpnessMax = 512.0f;
	static constexpr float kAbsorptionMin = 0.02f, kAbsorptionMax = 2.0f;
	static constexpr float kFadeMin = 0.1f, kFadeMax = 20.0f;
	static constexpr float kFoamDistanceMax = 5.0f;
	static constexpr float kDistortionMax = 0.2f;
	static constexpr float kRefractionMax = 0.15f;
	static constexpr float kScatterMax = 2.0f;
	static constexpr int kMeshDetailMax = 2;
	static constexpr float kLodDistanceMin = 10.0f, kLodDistanceMax = 1000.0f;

	void Clamp();
	void ApplyWavePreset(WavePreset preset);   // only touches the wave shape values (not wind direction)
	void ApplyLook(WaterLook look);            // only touches colours, depth and optics (not waves, level or size)
	// Sets the type AND the starting values that suit it (waves, ripples, foam, look). Keeps level, size, shape and debug switches.
	void ApplyBodyType(WaterBodyType type);
	WaterWaves::Params WaveParams() const;
};

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
	// The direction the surface travels toward, in WORLD space, as an angle (0 = +X, 90 = +Z). For a river it follows the
	// body's own rotation, so turning the river turns its flow; for every other type it is the wind direction.
	float GetTravelDegrees() const;
	void SetSettings(const WaterSettings& settings);
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

	// --- for gameplay code: floating objects, swimming, splashes ---
	// `time` is the scene clock (Scene::GetTime()): the waves on screen are exactly the waves measured here.
	// World Y of the surface above/below (x, z). Returns the rest level when the point is outside the plane.
	float SampleHeight(float x, float z, float time) const;
	// Unit surface normal at (x, z), from the same waves.
	DirectX::XMFLOAT3 SampleNormal(float x, float z, float time) const;
	// Is (x, z) inside this body's outline (rectangle or ellipse, turned and scaled like the object)?
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
	// or its level changed. Null = no terrain: the body then has no shore effects at all.
	void RefreshShoreDepth(const Terrain* terrain);
	bool HasShoreDepth() const { return depthView_ != nullptr; }
	void GetShoreMapSize(int& width, int& height) const { width = depthField_.Width(); height = depthField_.Height(); }
	const WaterViewStats& GetStats(ViewKind kind) const { return stats_[static_cast<int>(kind)]; }

private:
	void CreateRenderStates(ID3D11Device* device);
	void RebuildMesh(const WaterMesh::TileGrid& grid);
	void RebuildWaves();
	WaterWaves::Params CurrentWaveParams() const;   // WaterSettings::WaveParams() plus what only the body knows (its rotation)
	void EnsureMesh();                    // rebuilds the tiles when the mesh detail or the body's proportions need a different grid
	void UploadMaterial(bool mirrored) const;   // `mirrored`: this frame, a mirror image is bound for this body
	void UpdateWorldTransform();
	void ReleaseShoreMap();
	float DepthAtWorld(float x, float z) const;   // water depth under a world position, from the CPU copy of the map
	float ExtentX() const { return settings_.size * std::abs(scale_.x); }   // world size along X
	float ExtentZ() const { return settings_.sizeZ * std::abs(scale_.z); }
	void ChooseTiles(const RenderView& view) const;
	void UpdateDryTiles();                // flags the tiles that lie wholly under dry ground (needs the shore map and the wave table)
	bool TileOutsideOutline(size_t index) const;   // a corner tile of a round body: wholly outside the ellipse

	struct Tile {
		DirectX::XMFLOAT3 localMin, localMax;   // the tile's box in the plane's own space ([-1, 1] across)
	};

	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	WaterSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, 4.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
	Material material_;       // alpha blending, no depth writes
	Material wireMaterial_;   // the same, drawn as wires (debug)
	WaterWaves::Table waves_;
	// The measured ground under the body. Three copies of one thing: the CPU map (for SampleHeight), the texture and its view.
	WaterShore::DepthField depthField_;
	ID3D11Texture2D* depthTexture_ = nullptr;
	ID3D11ShaderResourceView* depthView_ = nullptr;
	DirectX::XMFLOAT4X4 worldToLocal_{};
	bool worldToLocalValid_ = false;
	std::array<float, 12> shoreKey_{};     // body place, turn, scale, size and level when the map was last measured...
	unsigned shoreTerrainRevision_ = 0;    // ...and which version of which terrain
	const Terrain* shoreTerrain_ = nullptr;
	bool shoreKeyValid_ = false;
	std::vector<Tile> tiles_;
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
