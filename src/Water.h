#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Material.h"
#include <memory>

enum class WavePreset { Calm, Choppy, Stormy };

// What the water mirrors. "Sky" is the cheap cubemap from before (reflects only the sky);
// the planar modes draw the whole scene again from below the water (reflects terrain, cube, sky).
enum class ReflectionMode : int { Sky = 0, PlanarHalf = 1, PlanarFull = 2 };

struct WaterSettings {
	// --- surface ---
	float height = 4.0f;          // world Y of the water plane (kept in sync with the editor transform)
	float size = 180.0f;          // side length of the square plane
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

	// --- lighting ---
	float reflectivity = 1.0f;    // 0 = no sky reflection, 1 = full Fresnel reflection
	ReflectionMode reflectionMode = ReflectionMode::PlanarHalf;
	float reflectionDistortion = 0.04f;   // how far the waves wobble the mirror image (fraction of the screen)
	float glintStrength = 1.0f;   // brightness of the sun highlight
	float glintSharpness = 128.0f;// higher = smaller, tighter highlight

	static constexpr float kHeightMin = -100.0f, kHeightMax = 200.0f;
	static constexpr float kSizeMin = 10.0f, kSizeMax = 2000.0f;
	static constexpr float kAmplitudeMax = 2.0f, kSpeedMax = 5.0f;
	static constexpr float kFrequencyMin = 0.05f, kFrequencyMax = 2.0f;
	static constexpr float kGlintMax = 3.0f;
	static constexpr float kSharpnessMin = 8.0f, kSharpnessMax = 512.0f;
	static constexpr float kAbsorptionMin = 0.02f, kAbsorptionMax = 2.0f;
	static constexpr float kFadeMin = 0.1f, kFadeMax = 20.0f;
	static constexpr float kFoamDistanceMax = 5.0f;
	static constexpr float kDistortionMax = 0.2f;

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
