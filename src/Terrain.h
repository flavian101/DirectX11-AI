#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include "TextureArray.h"
#include <memory>
#include <vector>

// One tile of the terrain: its own small mesh plus a box that bounds it. The box is what lets us
// ask "can the camera possibly see this tile?" without looking at a single triangle.
constexpr int kTerrainLodCount = 3;   // level 0 = every quad, 1 = every 2nd, 2 = every 4th

struct TerrainChunk {
	DirectX::XMFLOAT3 boundsMin;   // in the terrain's local space (includes the skirt)
	DirectX::XMFLOAT3 boundsMax;
	int triangleCount[kTerrainLodCount] = {};   // triangles at each level of detail
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
	float width = 240.0f;       // world units along X; depth follows the heightmap's aspect ratio
	float heightScale = 45.0f;  // world units for a 1.0 (white) heightmap pixel
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
	float macroInfluence = 0.5f; // how much the terrain-wide colour map ("Diffuse Map") tints the layers: 0 = not at all
	float normalStrength = 1.0f; // 0 = flat layers, 1 = as authored
	bool flipGreen = true;       // layer normal maps are OpenGL-style ("_nor_gl_"); untick for DirectX-style
	float heightBlend = 0.6f;    // 0 = plain linear blends, 1 = the taller layer wins (pebbles poke through sand)
	float aoStrength = 1.0f;
	float roughnessScale = 1.0f;
	DirectX::XMFLOAT3 groundTint = { 1.0f, 1.0f, 1.0f };      // sRGB; tints MULTIPLY the layer's texture
	DirectX::XMFLOAT3 rockTint = { 1.0f, 1.0f, 1.0f };
	float slopeStart = 0.25f;   // slope (0 flat .. 1 vertical) where rock starts to appear
	float slopeFade = 0.25f;    // how quickly it takes over
	float shoreHeight = 5.0f;   // world Y below which the ground turns to sand
	float shoreWidth = 2.5f;    // height over which sand fades into ground
	float snowHeight = 28.0f;   // world Y above which snow appears
	float snowFade = 6.0f;
	float breakupScale = 0.06f; // size of the noise that roughens layer edges (per world unit)
	float breakupAmount = 0.5f; // 0 = clean contour lines, 1 = very ragged
	DirectX::XMFLOAT3 shoreColor = { 0.76f, 0.70f, 0.50f };   // sand tint: sRGB, like every colour you pick
	DirectX::XMFLOAT3 snowColor = { 0.92f, 0.95f, 1.00f };
	bool showLayers = false;    // debug: paint rock/snow/sand weights as red/green/blue
	bool frustumCulling = true; // skip chunks outside the view (turn off to compare)

	// --- level of detail ---
	bool lodEnabled = true;
	float lodDistance0 = 90.0f;   // chunks closer than this use full detail
	float lodDistance1 = 220.0f;  // closer than this: half detail; beyond: quarter detail
	bool wireframe = false;       // debug: draw the terrain as a wire mesh to SEE the detail levels

	// One source of truth for the valid ranges: Clamp() enforces them, the editor sliders use them.
	static constexpr float kWidthMin = 40.0f, kWidthMax = 1000.0f;
	static constexpr float kHeightMin = 1.0f, kHeightMax = 150.0f;
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

	// Clamps the settings, then rebuilds the mesh only if the shape changed
	// (a tiling change just updates a shader constant). Returns false if the rebuild failed;
	// the previous terrain stays on screen in that case.
	bool ApplySettings(TerrainSettings settings);
	const TerrainSettings& GetSettings() const { return settings_; }
	const TerrainViewStats& GetStats(ViewKind kind) const { return stats_[static_cast<int>(kind)]; }

	// How many of the 4 layers found their albedo / normal / ORM file (the rest use neutral defaults).
	void GetLayerFileCounts(int& albedo, int& normal, int& orm) const;

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

	bool Rebuild(const TerrainSettings& settings);
	// Draws the chunks that are inside `view`. shaderOverride == nullptr -> the terrain's own shader.
	void DrawChunks(ID3D11DeviceContext* ctx, const Shader* shaderOverride, const RenderView& view) const;
	void UpdateMaterial();
	void UpdateWorldTransform();

	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<Texture> texture_;                  // the terrain-wide colour map ("macro" colour)
	std::unique_ptr<TextureArray> layerAlbedo_;         // slices: 0 grass, 1 rock, 2 sand, 3 snow
	std::unique_ptr<TextureArray> layerNormal_;
	std::unique_ptr<TextureArray> layerOrm_;
	Heightmap heightmap_;      // decoded ONCE; every rebuild reuses it instead of re-reading the PNG
	float depth_ = 0.0f;       // world-space size along Z of the current mesh
	std::vector<TerrainChunk> chunks_;                    // one per mesh in the model, same order
	mutable std::vector<unsigned char> drawList_;         // scratch: per chunk, the LOD to draw or Model::kSkipMesh
	mutable std::vector<unsigned char> chunkLod_;         // each chunk's CURRENT level (kept for hysteresis)
	mutable ID3D11RasterizerState* wireframeState_ = nullptr;   // created on first use (debug view)
	mutable TerrainViewStats stats_[kViewKindCount];
	TerrainSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, -2.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
};
