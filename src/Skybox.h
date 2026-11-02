#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include "Material.h"
#include <memory>

class Skybox final : public SceneObject {
public:
	explicit Skybox(ID3D11Device* device);
	~Skybox();

	const char* GetEditorName() const override { return "Skybox"; }
	RenderCategory GetRenderCategory() const override { return RenderCategory::Background; }
	DirectX::XMFLOAT3 GetSortPosition() const override { return {}; }
	bool IsValid() const;
	float GetIntensity() const { return intensity_; }
	void SetIntensity(float intensity);
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const Camera& camera) const override;

private:
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<Texture> cubemap_;
	Material material_;
	float intensity_ = 1.0f;
};
