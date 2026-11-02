#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include <memory>

struct TerrainSettings {
	float heightScale = 45.0f;
	float noiseStrength = 0.0f;
	float noiseScale = 4.0f;
	int noiseSeed = 0;
};

class Terrain final : public SceneObject {
public:
	explicit Terrain(ID3D11Device* device);

	bool IsValid() const;
	bool Regenerate(const TerrainSettings& settings);
	const TerrainSettings& GetSettings() const { return settings_; }
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const Camera& camera) const override;

private:
	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<Texture> texture_;
	TerrainSettings settings_;
};
