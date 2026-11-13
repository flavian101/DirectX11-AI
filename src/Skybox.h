#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include "Material.h"
#include <array>
#include <memory>
#include <string>

class Skybox final : public SceneObject {
public:
	explicit Skybox(ID3D11Device* device);
	~Skybox();

	const char* GetEditorName() const override { return "Skybox"; }
	RenderCategory GetRenderCategory() const override { return RenderCategory::Background; }
	DirectX::XMFLOAT3 GetSortPosition() const override { return {}; }
	bool IsValid() const;
	float GetIntensity() const { return intensity_; }
	ID3D11ShaderResourceView* GetCubemapView() const { return cubemap_ ? cubemap_->GetShaderResourceView() : nullptr; }
	void SetIntensity(float intensity);

	// Which cubemap is showing: a FOLDER holding the six face images (see FaceFileNames). The default is "assets/Skybox".
	static const char* DefaultFolder() { return "assets/Skybox"; }
	// The six file names a skybox folder must contain, in GPU cube-face order (+X, -X, +Y, -Y, +Z, -Z).
	static const std::array<const char*, 6>& FaceFileNames();
	// One spelling per folder ("a\\b\\..\\c/" and "a/c" are the same place), so folders can be compared as text.
	static std::string NormalizeFolder(const std::string& folder);
	// True if `folder` holds all six faces, i.e. LoadFromFolder has a chance of working. Cheap: six file-exists checks.
	static bool IsSkyboxFolder(const std::string& folder);
	const std::string& GetFolder() const { return folder_; }
	// Swaps the cubemap. All-or-nothing: if any face fails to load, the current sky stays and this returns false.
	// The scene's sky lighting re-bakes by itself the next frame (Scene::PrepareEnvironment compares the texture it baked).
	bool LoadFromFolder(const std::string& folder);
	void Update(float dt) override;
	void Render(ID3D11DeviceContext* ctx, const RenderView& view) const override;

private:
	std::unique_ptr<Renderable> renderable_;
	std::unique_ptr<Texture> cubemap_;
	Material material_;
	float intensity_ = 1.0f;
	ID3D11Device* device_ = nullptr;     // not owned; the device outlives every scene object (same as Terrain)
	std::string folder_ = DefaultFolder();
};
