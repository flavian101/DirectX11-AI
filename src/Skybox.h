#pragma once
#include "Renderable.h"
#include "SceneObject.h"
#include "Texture.h"
#include "Material.h"
#include <DirectXMath.h>
#include <array>
#include <memory>
#include <string>

class Skybox final : public SceneObject {
public:
	explicit Skybox(ID3D11Device* device, const std::string& folder = DefaultFolder());
	~Skybox();

	// A scene can hold several skies (day, dusk, night...): each has its own name, and ONE of them is the active sky, the
	// one that is drawn and that lights the scene (Scene::GetSkybox). Switching is Scene::SetActiveSkybox.
	const char* GetEditorName() const override { return GetName().empty() ? "Skybox" : GetName().c_str(); }
	const char* GetTypeName() const override { return "Skybox"; }
	// The sky that is showing cannot be deleted (switch to another first); the others can.
	bool CanDelete() const override { return !active_; }
	bool IsActive() const { return active_; }
	void SetActive(bool active) { active_ = active; }
	RenderCategory GetRenderCategory() const override { return RenderCategory::Background; }
	DirectX::XMFLOAT3 GetSortPosition() const override { return {}; }
	bool IsValid() const;
	float GetIntensity() const { return intensity_; }
	ID3D11ShaderResourceView* GetCubemapView() const { return cubemap_ ? cubemap_->GetShaderResourceView() : nullptr; }
	void SetIntensity(float intensity);

	// Turning and tinting the sky. Both are applied when the sky is LOOKED UP (see SkyDirection in common.hlsli), so neither
	// re-bakes the sky lighting. The sun does not turn with the sky: it is a light of its own (Lighting tab).
	float GetYawDegrees() const { return yawDegrees_; }
	void SetYawDegrees(float degrees);                        // wrapped into 0..360
	const DirectX::XMFLOAT3& GetTint() const { return tint_; }   // what the user picked (sRGB); white = no tint
	void SetTint(const DirectX::XMFLOAT3& tint);

	// Which cubemap is showing: a FOLDER holding the six face images, named in any of the schemes SkyFaces.h lists
	// (right/left/top/..., px/nx/..., 0..5 and so on). The default is "assets/Skybox".
	static const char* DefaultFolder() { return "assets/Skybox"; }
	// The six image files of `folder` in GPU cube-face order (+X, -X, +Y, -Y, +Z, -Z). False if it is not a skybox folder.
	static bool ResolveFaces(const std::string& folder, std::array<std::string, 6>& faces, std::string* schemeLabel = nullptr);
	// One spelling per folder ("a\\b\\..\\c/" and "a/c" are the same place), so folders can be compared as text.
	static std::string NormalizeFolder(const std::string& folder);
	// True if `folder` holds all six faces, i.e. LoadFromFolder has a chance of working. Cheap: six file-exists checks.
	static bool IsSkyboxFolder(const std::string& folder);
	const std::string& GetFolder() const { return folder_; }
	// How the faces of the loaded folder were named ("px/nx/py/ny/pz/nz"...), for the editor to show.
	const std::string& GetFaceScheme() const { return faceScheme_; }
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
	float yawDegrees_ = 0.0f;
	DirectX::XMFLOAT3 tint_ = { 1.0f, 1.0f, 1.0f };
	bool active_ = true;                 // see IsActive; every sky starts active, Scene::SetActiveSkybox picks the one that stays
	std::string faceScheme_;
	void ApplyMaterialColor();           // intensity x tint -> the colour the sky shader multiplies by
	ID3D11Device* device_ = nullptr;     // not owned; the device outlives every scene object (same as Terrain)
	std::string folder_ = DefaultFolder();
};
