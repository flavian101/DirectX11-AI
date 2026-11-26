#pragma once
#include <string>
#include "Heightfield.h"
#include "WorldGrid.h"
#include "WorldTileHeights.h"
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
	// UNITS: one world unit is one metre (docs/ROADMAP.md). Width and height are independent and both ABSOLUTE:
	// the heightmap picture is stretched over `width` metres and its white is `heightScale` metres tall, so making the terrain
	// wider WITHOUT raising heightScale makes it flatter (slopes = height / distance). The editor can keep the proportions for you.
	float width = 240.0f;       // metres along X; depth follows the heightmap's aspect ratio
	float heightScale = 45.0f;  // metres for a 1.0 (white) heightmap pixel
	int maxCells = 512;         // mesh resolution: grid cells along X (Z follows the aspect)
	int breakupSeed = 0;        // the random seed of the layer rules' breakup (which patches of rock and sand differ)

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
	static constexpr float kTilingMin = 0.25f, kTilingMax = 64.0f;
	static constexpr float kRockTilingMin = 0.005f, kRockTilingMax = 0.5f;   // also the range of every layer's tiling
	static constexpr float kBandMin = -50.0f, kBandMax = 200.0f;     // world-Y range of the shore/snow SLIDERS of a single terrain
	static constexpr float kBandHardMin = -2000.0f, kBandHardMax = 2000.0f;   // what Clamp allows: a world's automatic bands follow its height and base (roadmap H1)
	static constexpr float kBreakupScaleMin = 0.005f, kBreakupScaleMax = 0.5f;
	static constexpr float kLodDistanceMin = 10.0f, kLodDistanceMax = 1500.0f;

	void Clamp();
	void ResetLayers();   // puts only the layer/material fields back to defaults
	bool SameShape(const TerrainSettings& other) const;  // true if only material fields differ
	bool SameSplatRules(const TerrainSettings& other) const; // true if the rules that decide where each layer goes agree (they are baked, lesson 29)
};

// The layer textures of a terrain (the two packed texture arrays and where they came from), as one value. A world keeps one so
// that tiles loaded later share the textures of the first, even after the first tile itself has been unloaded (lesson 49).
struct TerrainLayerSet {
	std::shared_ptr<TextureArray> diffHeight;
	std::shared_ptr<TextureArray> normRough;
	int fileCounts[3] = {};
	std::string mapPaths[kTerrainLayerCount][3];
	bool IsValid() const { return diffHeight && normRough; }
};

// One tile of a world (lesson 48). The heights are not a picture stretched over the terrain: they come from `source`, a
// function of the WORLD position, so neighbouring tiles agree along their shared edge (WorldGrid.h).
struct TerrainTile {
	WorldGridSettings grid;                          // tile size and resolution: the same for every tile of the world
	TileCoord coord;                                 // which tile this is
	int lod = 0;                                     // its level of detail (WorldGridSettings::ForLod): `grid` is already the grid at this level
	std::shared_ptr<const WorldHeightSource> source; // the ground, shared by every tile of the world
	bool capBottom = false;                          // every tile of a BOUNDED world also draws a bottom at the floor, so the world is a closed block (roadmap X1)
	unsigned char outerEdges = 0;                    // TerrainEdgeBit mask: the sides of this tile that are the edge of a BOUNDED world. They are closed with a wall down to a floor (roadmap X1)
	float baseHeight = 0.0f;                         // the world Y of ground height 0: the tile's position.y. Every tile of a world has the same one (roadmap H1)
};

class Terrain final : public SceneObject {
public:
	// A terrain is always one TILE of a world. `material` supplies the look (layers, rules, LOD distances); its width, resolution
	// and noise are replaced by the tile's own. `shareLayersFrom` lets tiles of one world use ONE set of layer textures instead
	// of each reading and packing twelve images (null, or not valid: this tile loads its own).
	Terrain(ID3D11Device* device, const TerrainTile& tile, const TerrainSettings& material, const TerrainLayerSet* shareLayers);
	TerrainLayerSet GetLayerSet() const;
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
	// True for a tile of a world. Its heights come from the world, so (for now) it cannot be sculpted or carved: an edit would
	// stop it matching its neighbours. Painting works.
	bool IsTile() const { return tile_.source != nullptr; }   // always true for a built terrain; false only for a tile without a ground (never made)
	TerrainWalls Walls() const;   // the walls of this tile (none unless it is on the edge of a bounded world)
	const TerrainTile& GetTile() const { return tile_; }
	// The terrain can be deleted (and the delete undone): a scene without one is how a terrain world is tried on its own. The tiles of
	// a world are not content: the world owns them.
	bool CanDelete() const override { return !IsTile(); }
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

	// --- The layer images (the heights come from the world) ---
	static std::string DefaultLayerMapPath(int layer, TerrainLayerMap map);   // assets/Terrain/<Layer>/albedo.png ...
	const std::string& GetLayerMapPath(int layer, TerrainLayerMap map) const { return layerMapPaths_[layer][static_cast<int>(map)]; }
	bool SetLayerMap(int layer, TerrainLayerMap map, const std::string& path);

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

	// Rewrites the samples of THIS TILE (level of detail 0) inside `rect` (tile-local, apron included) from the world's ground, and
	// refreshes only what they touch: the vertices of the chunks around them, the splat texels, nothing else. The result is exactly
	// the tile a rebuild would give (tests/tile_patch_test.cpp), at a fraction of the cost: no new Terrain, no new index buffers, no
	// heights for the other 99% of the tile (roadmap T2). `groundIsFinal` false while a stroke is going on: the revision the water
	// measures its shore by moves when the stroke ends (NotifyGroundChanged), not on every step. False: nothing was done.
	bool PatchTile(const TileSampleRect& rect, bool groundIsFinal);
	void NotifyGroundChanged() { NewRevision(); }

	// How long the last tile build took and where the time went, and the last patch (milliseconds, measured with a steady clock).
	// Shared by every tile; shown in the world inspector so a slow stroke can be told from a slow build.
	struct BuildTimes {
		double heightsMs = 0.0;   // the tile's heights and apron through the whole source chain (edits included)
		double meshMs = 0.0;      // the vertex and index buffers of all its chunks
		double splatMs = 0.0;     // the layer weights and their texture
		double patchMs = 0.0;     // the last PatchTile
		int builds = 0, patches = 0;
	};
	static const BuildTimes& GetBuildTimes();

	// The painted layer weights the world keeps for this tile, laid over the weights the rules bake (the world owns the paint: lesson 30). They
	// never touch the heights. False if the size does not match.
	bool ReplacePaint(std::vector<float> weights, bool painted);

	// --- For things that stand on the ground (scatter, lesson 31) ---
	// The final layer weights (baked rules + paint), one texel per grid cell: what the pixel shader reads.
	const SplatMap& GetSplatMap() const { return splat_; }
	// Where the ground changed (heights, layer weights, or the terrain itself moved / was rebuilt). Every change gets a
	// number from a counter shared by all terrains; the last few are remembered with the world XZ box they touched.
	struct SurfaceChange {
		unsigned revision = 0;
		float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;   // world X/Z box of the change
	};
	unsigned GetSurfaceRevision() const { return surfaceRevision_; }
	// Appends the changes made AFTER `sinceRevision` to `changes`. Returns false when the log no longer reaches back that
	// far (or `sinceRevision` came from another terrain): the caller must then treat the whole ground as changed.
	bool GetSurfaceChangesSince(unsigned sinceRevision, std::vector<SurfaceChange>& changes) const;

	// For tools that need to fit things to the terrain (e.g. the water plane).
	DirectX::XMFLOAT2 GetWorldExtent() const;               // x/z size in world units
	void GetHeightRange(float& minY, float& maxY) const;    // world-space Y range of the surface

	bool CastsShadow() const override { return true; }
	bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
	void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const override;

	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

private:
	bool Rebuild(const TerrainSettings& settings);
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
	std::shared_ptr<TextureArray> layerDiffHeight_;     // slices: 0 grass, 1 rock, 2 sand, 3 snow. rgb = albedo (sRGB), a = height. SHARED between the tiles of a world
	std::shared_ptr<TextureArray> layerNormRough_;      // same slices. r,g = normal XY, b = occlusion, a = roughness
	int layerFileCounts_[3] = {};                       // files found per kind: albedo, normal, ORM
	SplatMap splat_;                                    // the layer weights, one texel per grid cell
	ID3D11Texture2D* splatTexture_ = nullptr;           // the same weights on the GPU (RGBA8, no mips)
	ID3D11ShaderResourceView* splatView_ = nullptr;
	Heightfield heightfield_;  // the FINAL grid the mesh was built from (heightmap + noise, at mesh resolution)
	PaintMap paint_;           // painted layer weights, same grid as splat_ (always sized after the first Rebuild)
	bool painted_ = false;     // something has been painted (or loaded): saved next to the scene file
	std::string layerMapPaths_[kTerrainLayerCount][3];   // [layer][albedo / normal / orm]
	float depth_ = 0.0f;       // world-space size along Z of the current mesh
	unsigned revision_ = 0;    // see GetRevision()
	unsigned surfaceRevision_ = 0;                  // see GetSurfaceRevision()
	unsigned surfaceLogFloor_ = 0;                  // changes with a revision <= this are forgotten
	std::vector<SurfaceChange> surfaceLog_;         // the most recent changes, oldest first
	void LogSurfaceChange(float minX, float minZ, float maxX, float maxZ);   // world X/Z box
	void LogSurfaceChangeEverywhere();
	void NewRevision();        // gives the terrain a revision no terrain has had before
	std::vector<TerrainChunk> chunks_;                    // one per mesh in the model, same order
	mutable std::vector<unsigned char> drawList_;         // scratch: per chunk, the LOD to draw or Model::kSkipMesh
	mutable std::vector<unsigned char> chunkLod_;         // each chunk's CURRENT level (kept for hysteresis)
	mutable std::vector<unsigned char> partMasks_;        // scratch: per chunk, which skirt edges to draw (see TerrainSkirts.h)
	int chunksX_ = 0;                                     // the grid of tiles: chunksX_ x chunksZ_
	int chunksZ_ = 0;
	mutable ID3D11RasterizerState* wireframeState_ = nullptr;   // created on first use (debug view)
	mutable TerrainViewStats stats_[kViewKindCount];
	TerrainTile tile_;
	// Puts the settings the tile decides back (its width, resolution, no noise).
	void ForceTileSettings(TerrainSettings& settings) const;
	TileHeights BuildTileHeights() const;   // the tile's samples and apron from the world's ground function
	TerrainSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, -2.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
};
