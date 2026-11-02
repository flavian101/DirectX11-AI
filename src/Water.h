#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Material.h"
#include <memory>

enum class WavePreset { Calm, Choppy, Stormy };

struct WaterSettings {
	// --- surface ---
	float height = 4.0f;          // world Y of the water plane (kept in sync with the editor transform)
	float size = 180.0f;          // side length of the square plane
	DirectX::XMFLOAT3 color = { 0.05f, 0.25f, 0.45f };
	float alpha = 0.68f;

	// --- waves ---
	float waveAmplitude = 0.35f;
	float waveSpeed = 1.0f;
	float waveFrequency = 0.6f;   // radians per world unit of the base wave (0.6 = ~10 unit wavelength)

	// --- lighting ---
	float reflectivity = 1.0f;    // 0 = no sky reflection, 1 = full Fresnel reflection
	float glintStrength = 1.0f;   // brightness of the sun highlight
	float glintSharpness = 128.0f;// higher = smaller, tighter highlight

	static constexpr float kHeightMin = -100.0f, kHeightMax = 200.0f;
	static constexpr float kSizeMin = 10.0f, kSizeMax = 2000.0f;
	static constexpr float kAmplitudeMax = 2.0f, kSpeedMax = 5.0f;
	static constexpr float kFrequencyMin = 0.05f, kFrequencyMax = 2.0f;
	static constexpr float kGlintMax = 3.0f;
	static constexpr float kSharpnessMin = 8.0f, kSharpnessMax = 512.0f;

	void Clamp();
	void ApplyWavePreset(WavePreset preset);   // only touches the three wave values
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
