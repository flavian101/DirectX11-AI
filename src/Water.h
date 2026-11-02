#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Material.h"
#include <memory>

struct WaterSettings {
	float height = 4.0f;
	float size = 180.0f;
	DirectX::XMFLOAT3 color = { 0.05f, 0.25f, 0.45f };
	float alpha = 0.68f;
	float waveAmplitude = 0.12f;
	float waveSpeed = 0.7f;
};

class Water final : public SceneObject {
public:
	explicit Water(ID3D11Device* device);
	~Water();

	const char* GetEditorName() const override { return "Water"; }
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
	void SetSettings(const WaterSettings& settings);
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const Camera& camera) const override;

private:
	void UpdateWorldTransform();

	std::unique_ptr<Renderable> renderable_;
	WaterSettings settings_;
	DirectX::XMFLOAT3 position_ = { 0.0f, 4.0f, 0.0f };
	DirectX::XMFLOAT3 rotation_ = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 scale_ = { 1.0f, 1.0f, 1.0f };
	Material material_;
};
