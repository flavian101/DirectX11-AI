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

	const char* GetEditorName() const override { return "Terrain"; }
	bool GetEditorTransform(DirectX::XMFLOAT3& position,
		DirectX::XMFLOAT3& rotation,
		DirectX::XMFLOAT3& scale) const override;
	bool SetEditorTransform(const DirectX::XMFLOAT3& position,
		const DirectX::XMFLOAT3& rotation,
		const DirectX::XMFLOAT3& scale) override;
	bool IsValid() const;
	bool Regenerate(const TerrainSettings& settings);
	const TerrainSettings& GetSettings() const { return settings_; }
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const Camera& camera) const override;

private:
	void UpdateWorldTransform();

	ID3D11Device* device_ = nullptr;
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<Texture> texture_;
	TerrainSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, -2.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
};
