#include "Skybox.h"
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
#include "SkyFaces.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

namespace {
Model* CreateSkyboxModel(ID3D11Device* device)
{
	const std::vector<Vertex> vertices = {
		Vertex({ -1.0f, -1.0f, -1.0f }, {}, {}), Vertex({ -1.0f,  1.0f, -1.0f }, {}, {}),
		Vertex({  1.0f,  1.0f, -1.0f }, {}, {}), Vertex({  1.0f, -1.0f, -1.0f }, {}, {}),
		Vertex({ -1.0f, -1.0f,  1.0f }, {}, {}), Vertex({  1.0f, -1.0f,  1.0f }, {}, {}),
		Vertex({  1.0f,  1.0f,  1.0f }, {}, {}), Vertex({ -1.0f,  1.0f,  1.0f }, {}, {})
	};
	const std::vector<uint32_t> indices = {
		0, 1, 2, 0, 2, 3,
		4, 5, 6, 4, 6, 7,
		0, 4, 7, 0, 7, 1,
		3, 2, 6, 3, 6, 5,
		1, 7, 6, 1, 6, 2,
		0, 3, 5, 0, 5, 4
	};
	return new Model({ new Mesh(device, vertices, indices) });
}
}

Skybox::Skybox(ID3D11Device* device, const std::string& folder) : material_(device), device_(device)
{
	LoadFromFolder(folder);
	renderable_ = std::make_unique<Renderable>(
		device, CreateSkyboxModel(device), L"shaders/skybox_vs.hlsl", L"shaders/skybox_ps.hlsl");
	SetIntensity(intensity_);

	D3D11_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthEnable = TRUE;
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (device && !material_.CreateDepthStencilState(depthDesc)) {
		LOG_ERROR("Skybox depth state creation failed");
	}

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_FRONT;
	rasterDesc.DepthClipEnable = TRUE;
	if (device && !material_.CreateRasterizerState(rasterDesc)) {
		LOG_ERROR("Skybox rasterizer state creation failed");
	}
}

Skybox::~Skybox()
{
}

bool Skybox::ResolveFaces(const std::string& folder, std::array<std::string, 6>& faces, std::string* schemeLabel)
{
	return SkyFaces::Resolve(folder, faces, schemeLabel);
}

std::string Skybox::NormalizeFolder(const std::string& folder)
{
	std::string text = std::filesystem::path(folder).lexically_normal().generic_string();
	while (text.size() > 1 && text.back() == '/') text.pop_back();
	return text;
}

bool Skybox::IsSkyboxFolder(const std::string& folder)
{
	std::array<std::string, 6> faces;
	return SkyFaces::Resolve(folder, faces);
}

bool Skybox::LoadFromFolder(const std::string& requested)
{
	if (!device_) return false;
	const std::string folder = NormalizeFolder(requested);
	std::array<std::string, 6> faces;
	std::string scheme;
	if (!SkyFaces::Resolve(folder, faces, &scheme)) {
		LOG_ERROR("Skybox: \"" + folder + "\" has no complete set of six faces. " + SkyFaces::Hint());
		return false;
	}

	// Build the NEW cubemap while the old one is still alive, and only then swap. Two reasons: a bad folder must not
	// leave the scene with no sky, and the scene decides whether to re-bake sky lighting by comparing texture
	// POINTERS - if the old texture were freed first, the new one could be given the same address and the bake would
	// be skipped.
	auto candidate = std::make_unique<Texture>(device_, faces);
	if (!candidate->IsValid()) {
		LOG_ERROR("Skybox: could not load all six faces from \"" + folder + "\"");
		return false;
	}
	cubemap_ = std::move(candidate);
	folder_ = folder;
	faceScheme_ = scheme;
	return true;
}

bool Skybox::IsValid() const
{
	return renderable_ && renderable_->IsValid() && cubemap_ && cubemap_->IsValid() &&
		   material_.IsValid() && material_.HasDepthStencilState() &&
		   material_.HasRasterizerState();
}

void Skybox::SetIntensity(float intensity)
{
	intensity_ = intensity;
	ApplyMaterialColor();
}

void Skybox::SetTint(const DirectX::XMFLOAT3& tint)
{
	tint_ = { (std::min)((std::max)(tint.x, 0.0f), 1.0f), (std::min)((std::max)(tint.y, 0.0f), 1.0f), (std::min)((std::max)(tint.z, 0.0f), 1.0f) };
	ApplyMaterialColor();
}

void Skybox::SetYawDegrees(float degrees)
{
	yawDegrees_ = std::fmod(degrees, 360.0f);
	if (yawDegrees_ < 0.0f) yawDegrees_ += 360.0f;
}

void Skybox::ApplyMaterialColor()
{
	if (!renderable_) return;
	const DirectX::XMFLOAT3 linear = SrgbToLinear(tint_);   // the picker gives display numbers; the shader multiplies linear light
	renderable_->SetMaterialColor({ intensity_ * linear.x, intensity_ * linear.y, intensity_ * linear.z, 1.0f });
}

void Skybox::Update(float /*dt*/)
{
}

void Skybox::Render(ID3D11DeviceContext* ctx, const RenderView& /*view*/) const
{
	if (!IsValid() || !ctx) return;
	material_.Apply(ctx);
	renderable_->SetTexture(ctx, cubemap_->GetShaderResourceView());
	renderable_->Render(ctx);
}
