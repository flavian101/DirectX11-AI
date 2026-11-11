#pragma once
#include "Heightfield.h"
#include "HeightfieldEdit.h"
#include "RayPicking.h"
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include "TextureArray.h"
#include "TerrainPaint.h"
#include "TerrainSkirts.h"
#include "TerrainSplat.h"
#include <memory>
#include <vector>

// One tile of the terrain: its own small mesh plus a box that bounds it. The box is what lets us
// ask "can the camera possibly see this tile?" without looking at a single triangle.
constexpr int kTerrainLayerCount = 4;   // grass, rock, sand, snow (the slice order the shader expects)
constexpr const char* kTerrainLayerNames[kTerrainLayerCount] = { "Grass", "Rock", "Sand", "Snow" };
enum class TerrainLayerMap { Albedo = 0, Normal = 1, Orm = 2 };

constexpr int kTerrainLodCount = 3;   // level 0 = every quad, 1 = every 2nd, 2 = every 4th

struct TerrainChunk {
	DirectX::XMFLOAT3 boundsMin;   // in the terrain's local space (includes the skirt)
	DirectX::XMFLOAT3 boundsMax;
	int triangleCount[kTerrainLodCount] = {};   // SURFACE triangles at each level of detail (skirts are counted separately)
	int skirtTriangles[kTerrainLodCount][4] = {};   // per level, per edge (north, south, west, east): triangles of that skirt
};

// What one view (camera / mirror / sun) drew last frame - shown in the editor.
struct TerrainViewStats {
	int chunksTotal = 0;
	int chunksDrawn = 0;
	int trianglesDrawn = 0;
	int chunksPerLod[kTerrainLodCount] = {};   // how many drawn chunks used each level of detail
};

struct TerrainSettings {
	// --- shape: changing any of these rebuilds the mesh ---
	// UNITS: one world unit is one metre (docs/terrain-world-design.md). Width and height are independent and both ABSOLUTE:
	// the heightmap picture is stretched over `width` metres and its white is `heightScale` metres tall, so making the terrain
	// wider WITHOUT raising heightScale makes it flatter (slopes = height / distance). The editor can keep the proportions for you.
	float width = 240.0f;       // metres along X; depth follows the heightmap's aspect ratio
	float heightScale = 45.0f;  // metres for a 1.0 (white) heightmap pixel
	int maxCells = 512;         // mesh resolution: grid cells along X (Z follows the aspect)
	float noiseStrength = 0.0f;
	float noiseScale = 4.0f;
	int noiseOctaves = 4;
	int noiseSeed = 0;

	// --- material: cheap, applied without rebuilding ---
	float uvTiling = 1.0f;      // how many times the texture repeats across the terrain

	// Layers: ground (grass), rock on steep slopes, sand near the water, snow up high. Each layer has its
	// own albedo, normal and ORM map (assets/Terrain/<Layer>/), blended by the rules below (lesson 17).
	float groundTiling = 0.08f; // how many times each layer's texture repeats per world unit
	float rockTiling = 0.05f;   // (rock is triplanar, see terrain_ps.hlsl)
	float sandTiling = 0.08f;
	float snowTiling = 0.06f;
	float rockDarkness = 1.0f;  // rock brightness: 1 = as in the texture
	float normalStrength = 1.0f; // 0 = flat layers, 1 = as authored
	bool flipGreen = true;       // layer normal maps are OpenGL-style ("_nor_gl_"); untick for DirectX-style
	float heightBlend = 0.6f;    // 0 = plain linear blends, 1 = the taller layer wins (pebbles poke through sand). Per pixel, in the shader
	float aoStrength = 1.0f;
	float roughnessScale = 1.0f;
	DirectX::XMFLOAT3 groundTint = { 1.0f, 1.0f, 1.0f };      // sRGB; tints MULTIPLY the layer's texture
	DirectX::XMFLOAT3 rockTint = { 1.0f, 1.0f, 1.0f };
	// The rules below decide WHERE each layer goes. They are baked into a splat map (lesson 29), so changing one re-bakes
	// the map (a few ms) instead of costing every pixel every frame.
	float slopeStart = 0.25f;   // slope (0 flat .. 1 vertical) where rock starts to appear
	float slopeFade = 0.25f;    // how quickly it takes over
	float shoreHeight = 5.0f;   // world Y below which the ground turns to sand
	float shoreWidth = 2.5f;    // height over which sand fades into ground
	float snowHeight = 28.0f;   // world Y above which snow appears
	float snowFade = 6.0f;
	float breakupScale = 0.06f; // size of the noise that roughens layer edges (per world unit)
	float breakupAmount = 0.5f; // 0 = clean contour lines, 1 = very ragged
	// Tints MULTIPLY a layer's texture. White (the default) leaves the texture exactly as it was made.
	DirectX::XMFLOAT3 shoreColor = { 1.0f, 1.0f, 1.0f };   // sand tint: sRGB, like every colour you pick
	DirectX::XMFLOAT3 snowColor = { 1.0f, 1.0f, 1.0f };
	bool showLayers = false;    // debug: paint rock/snow/sand weights as red/green/blue
	bool frustumCulling = true; // skip chunks outside the view (turn off to compare)

	// --- level of detail ---
	bool lodEnabled = true;
	float lodDistance0 = 90.0f;   // chunks closer than this use full detail
	float lodDistance1 = 220.0f;  // closer than this: half detail; beyond: quarter detail
	bool wireframe = false;       // debug: draw the terrain as a wire mesh to SEE the detail levels

	// One source of truth for the valid ranges: Clamp() enforces them, the editor sliders use them.
	static constexpr float kWidthMin = 40.0f, kWidthMax = 1000.0f;
	static constexpr float kHeightMin = 1.0f, kHeightMax = 500.0f;
	static constexpr int kCellsMin = 32, kCellsMax = 1024;
	static constexpr float kNoiseScaleMin = 0.5f, kNoiseScaleMax = 32.0f;
	static constexpr int kOctavesMin = 1, kOctavesMax = 8;
	static constexpr float kTilingMin = 0.25f, kTilingMax = 64.0f;
	static constexpr float kRockTilingMin = 0.005f, kRockTilingMax = 0.5f;   // also the range of every layer's tiling
	static constexpr float kBandMin = -50.0f, kBandMax = 200.0f;     // world-Y limits for shore/snow heights
	static constexpr float kBreakupScaleMin = 0.005f, kBreakupScaleMax = 0.5f;
	static constexpr float kLodDistanceMin = 10.0f, kLodDistanceMax = 1500.0f;

	void Clamp();
	void ResetLayers();   // puts only the layer/material fields back to defaults
	bool SameShape(const TerrainSettings& other) const;  // true if only material fields differ
	bool SameSource(const TerrainSettings& other) const; // true if the grid and the generated heights (resolution, noise) agree
	bool SameSplatRules(const TerrainSettings& other) const; // true if the rules that decide where each layer goes agree (they are baked, lesson 29)
};

class Terrain final : public SceneObject {
public:
	explicit Terrain(ID3D11Device* device);
	~Terrain() override;

	const char* GetEditorName() const override { return "Terrain"; }
	DirectX::XMFLOAT3 GetSortPosition() const override { return position_; }
	bool GetEditorTransform(DirectX::XMFLOAT3& position,
		DirectX::XMFLOAT3& rotation,
		DirectX::XMFLOAT3& scale) const override;
	bool SetEditorTransform(const DirectX::XMFLOAT3& position,
		const DirectX::XMFLOAT3& rotation,
		const DirectX::XMFLOAT3& scale) override;
	bool IsValid() const;
	bool SupportsRotation() const override { return false; }   // moved and scaled only: see lesson 25

	// Clamps the settings, then rebuilds the mesh only if the shape changed
	// (a tiling change just updates a shader constant). Returns false if the rebuild failed;
	// the previous terrain stays on screen in that case.
	bool ApplySettings(TerrainSettings settings);
	const TerrainSettings& GetSettings() const { return settings_; }
	const TerrainViewStats& GetStats(ViewKind kind) const { return stats_[static_cast<int>(kind)]; }

	// How many of the 4 layers found their albedo / normal / ORM file (the rest use neutral defaults). (The GPU packs the three
	// kinds of file into two arrays, lesson 29, but the files, and these counts, are unchanged.)
	void GetLayerFileCounts(int& albedo, int& normal, int& orm) const;

	// --- The images the terrain is made from. All can be replaced while the program runs. ---
	// An empty path means "the default file". The Set functions return false (and change nothing) if the file is
	// missing or cannot be read.
	static std::string DefaultLayerMapPath(int layer, TerrainLayerMap map);   // assets/Terrain/<Layer>/albedo.png ...
	static const char* DefaultHeightmapPath() { return "assets/dark alien/Height Map.png"; }
	const std::string& GetLayerMapPath(int layer, TerrainLayerMap map) const { return layerMapPaths_[layer][static_cast<int>(map)]; }
	bool SetLayerMap(int layer, TerrainLayerMap map, const std::string& path);
	const std::string& GetHeightmapPath() const { return heightmapPath_; }
	bool SetHeightmap(const std::string& path);   // 8-bit or 16-bit greyscale image; 16-bit gives much smoother slopes

	// --- Asking the terrain about its own surface (lesson 25) ---
	// All in WORLD space, and all answered from the kept height grid, not from the GPU.
	// Height of the ground at (worldX, worldZ), exactly as the full-detail mesh draws it. False outside the terrain.
	bool GetHeightAt(float worldX, float worldZ, float& worldY) const;
	// Unit surface normal there. False outside the terrain.
	bool GetNormalAt(float worldX, float worldZ, DirectX::XMFLOAT3& normal) const;
	// First hit of `ray` (unit direction) with the ground, coming from above. `distance` is in world units.
	bool Raycast(const Ray& ray, float& distance, DirectX::XMFLOAT3* point = nullptr) const;
	const Heightfield& GetHeightfield() const { return heightfield_; }
	// A number that changes whenever the ground changes: its heights (a rebuild) or its place or size. Never repeats, even
	// across terrains. Things that measured the ground (the water's depth map) compare it to know when to measure again.
	unsigned GetRevision() const { return revision_; }

	// The ground height as a 0..1 sample value (what the brushes and the saved PNG use). False outside the terrain.
	bool SampleNormalizedHeight(float worldX, float worldZ, float& value) const;

	// --- Sculpting (lesson 27) ---
	// True once the heights have been shaped by hand (or loaded from a saved sculpt). From then on the generated source
	// (heightmap, noise, mesh resolution) is not used any more and the editor locks those controls; width and height
	// scale still work because the heights are kept as 0..1.
	bool IsSculpted() const { return sculpted_; }
	// A stroke is everything from pressing the mouse button to releasing it: one undo step.
	void BeginSculptStroke();
	// One step of a brush at a WORLD position; brush.radius is in world units. False if nothing changed.
	bool Sculpt(const BrushSettings& brush, float worldX, float worldZ, float seconds);
	HeightfieldPatch EndSculptStroke();   // the finished stroke (empty if it changed nothing)
	// Undo / redo of a stroke: writes the patch's "before" or "after" values and refreshes the chunks it touches.
	bool ApplyPatch(const HeightfieldPatch& patch, bool useAfter);
	// Replaces every height at once (loading a saved sculpt, discarding or restoring one). The size must match the grid.
	bool ReplaceHeights(std::vector<float> samples, bool sculpted);
	// What the heightmap and noise settings make, ignoring any sculpting: the "Discard sculpting" target.
	bool GenerateOriginalHeights(std::vector<float>& samples) const;
	// Reads a saved sculpt (16-bit PNG, one pixel per grid vertex). `error` says what was wrong if it fails.
	bool LoadSculptedHeights(const std::string& path, std::string& error);

	// --- Painting layers (lesson 30) ---
	// The artist's layer weights, laid over the weights the rules bake. They never touch the heights, so they survive
	// sculpting, noise and every rule slider; they are lost only if the GRID changes (mesh resolution, or a heightmap of a
	// different shape), which is why the editor locks those while painted.
	bool IsPainted() const { return painted_; }
	// A stroke is everything from pressing the mouse button to releasing it: one undo step.
	void BeginPaintStroke();
	// One step of a brush at a WORLD position; brush.radius is in world units. False if nothing changed.
	bool Paint(const PaintBrush& brush, float worldX, float worldZ, float seconds);
	HeightfieldPatch EndPaintStroke();   // the finished stroke (empty if it changed nothing)
	// Undo / redo of a stroke: writes the patch's "before" or "after" values and re-uploads the texels it touches.
	bool ApplyPaintPatch(const HeightfieldPatch& patch, bool useAfter);
	const PaintMap& GetPaintMap() const { return paint_; }
	// Replaces all the painted weights at once (clearing, or restoring a cleared paint). The size must match the map.
	bool ReplacePaint(std::vector<float> weights, bool painted);
	// Reads a saved paint map (RGBA8 PNG, one pixel per grid cell). `error` says what was wrong if it fails.
	bool LoadPaint(const std::string& path, std::string& error);

	// For tools that need to fit things to the terrain (e.g. the water plane).
	DirectX::XMFLOAT2 GetWorldExtent() const;               // x/z size in world units
	void GetHeightRange(float& minY, float& maxY) const;    // world-space Y range of the surface

	bool CastsShadow() const override { return true; }
	bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
	void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;

	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

private:
	struct Heightmap {
		int width = 0;
		int height = 0;
		std::vector<float> values;  // 0..1, row-major
	};

	// `keepSculpt`: carry the sculpted heights over into the rebuilt mesh (only when the grid is unchanged).
	bool Rebuild(const TerrainSettings& settings, bool keepSculpt = false);
	// Re-makes the vertices of every chunk that holds a sample in `changed` (plus one sample around it, for the normals).
	void RefreshChunks(const GridRect& changed);
	// Re-reads the twelve layer files and PACKS them into the two texture arrays the shader reads (lesson 29): albedo + height,
	// and normal XY + occlusion + roughness. The old arrays stay if the new ones cannot be built.
	bool RebuildLayerTextures();

	// The baked layer weights (lesson 29). `splat_` is the CPU copy, the texture is what the shader reads.
	SplatRules CurrentSplatRules() const;
	SplatPlacement CurrentSplatPlacement() const;   // where the terrain's transform puts the heights in the world
	// Re-bakes the weights of the cells in `cells` from the heightfield and uploads just that block. Nothing to do before
	// the first Rebuild.
	void RefreshSplat(const GridRect& cells);
	void RefreshSplatAll();
	void ReleaseSplatTexture();
	// Draws the chunks that are inside `view`. shaderOverride == nullptr -> the terrain's own shader.
	void DrawChunks(ID3D11DeviceContext* ctx, const Shader* shaderOverride, const RenderView& view) const;
	void UpdateMaterial();
	void UpdateWorldTransform();

	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<TextureArray> layerDiffHeight_;     // slices: 0 grass, 1 rock, 2 sand, 3 snow. rgb = albedo (sRGB), a = height
	std::unique_ptr<TextureArray> layerNormRough_;      // same slices. r,g = normal XY, b = occlusion, a = roughness
	int layerFileCounts_[3] = {};                       // files found per kind: albedo, normal, ORM
	SplatMap splat_;                                    // the layer weights, one texel per grid cell
	ID3D11Texture2D* splatTexture_ = nullptr;           // the same weights on the GPU (RGBA8, no mips)
	ID3D11ShaderResourceView* splatView_ = nullptr;
	Heightmap heightmap_;      // decoded ONCE; every rebuild reuses it instead of re-reading the PNG
	Heightfield heightfield_;  // the FINAL grid the mesh was built from (heightmap + noise, at mesh resolution)
	bool sculpted_ = false;    // the grid has been shaped by hand: it is the truth, not the heightmap
	StrokeRecorder stroke_;    // the "before" values of the stroke in progress
	bool strokeActive_ = false;
	PaintMap paint_;           // painted layer weights, same grid as splat_ (always sized after the first Rebuild)
	bool painted_ = false;     // something has been painted (or loaded): saved next to the scene file
	StrokeRecorder paintStroke_;
	bool paintStrokeActive_ = false;
	std::string heightmapPath_;
	std::string layerMapPaths_[kTerrainLayerCount][3];   // [layer][albedo / normal / orm]
	float depth_ = 0.0f;       // world-space size along Z of the current mesh
	unsigned revision_ = 0;    // see GetRevision()
	void NewRevision();        // gives the terrain a revision no terrain has had before
	std::vector<TerrainChunk> chunks_;                    // one per mesh in the model, same order
	mutable std::vector<unsigned char> drawList_;         // scratch: per chunk, the LOD to draw or Model::kSkipMesh
	mutable std::vector<unsigned char> chunkLod_;         // each chunk's CURRENT level (kept for hysteresis)
	mutable std::vector<unsigned char> partMasks_;        // scratch: per chunk, which skirt edges to draw (see TerrainSkirts.h)
	int chunksX_ = 0;                                     // the grid of tiles: chunksX_ x chunksZ_
	int chunksZ_ = 0;
	mutable ID3D11RasterizerState* wireframeState_ = nullptr;   // created on first use (debug view)
	mutable TerrainViewStats stats_[kViewKindCount];
	TerrainSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, -2.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
};
