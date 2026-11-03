#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include <memory>
#include <vector>

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

	// One source of truth for the valid ranges: Clamp() enforces them, the editor sliders use them.
	static constexpr float kWidthMin = 40.0f, kWidthMax = 1000.0f;
	static constexpr float kHeightMin = 1.0f, kHeightMax = 150.0f;
	static constexpr int kCellsMin = 32, kCellsMax = 1024;
	static constexpr float kNoiseScaleMin = 0.5f, kNoiseScaleMax = 32.0f;
	static constexpr int kOctavesMin = 1, kOctavesMax = 8;
	static constexpr float kTilingMin = 0.25f, kTilingMax = 64.0f;

	void Clamp();
	bool SameShape(const TerrainSettings& other) const;  // true if only material fields differ
};

class Terrain final : public SceneObject {
public:
	explicit Terrain(ID3D11Device* device);

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

	// For tools that need to fit things to the terrain (e.g. the water plane).
	DirectX::XMFLOAT2 GetWorldExtent() const;               // x/z size in world units
	void GetHeightRange(float& minY, float& maxY) const;    // world-space Y range of the surface

	bool CastsShadow() const override { return true; }
	bool GetBoundingSphere(DirectX::XMFLOAT3& center, float& radius) const override;
	void RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader) const override;

	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const Camera& camera) const override;

private:
	struct Heightmap {
		int width = 0;
		int height = 0;
		std::vector<float> values;  // 0..1, row-major
	};

	bool Rebuild(const TerrainSettings& settings);
	void UpdateMaterial();
	void UpdateWorldTransform();

	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<Texture> texture_;
	Heightmap heightmap_;      // decoded ONCE; every rebuild reuses it instead of re-reading the PNG
	float depth_ = 0.0f;       // world-space size along Z of the current mesh
	TerrainSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, -2.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
};
