#include "Skybox.h"
#include "Logger.h"
#include "Mesh.h"
#include <array>
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

Skybox::Skybox(ID3D11Device* device) : material_(device)
{
	const std::array<std::string, 6> faces = {
		"assets/Skybox/right.png", "assets/Skybox/left.png",
		"assets/Skybox/top.png", "assets/Skybox/bottom.png",
		"assets/Skybox/front.png", "assets/Skybox/back.png"
	};
	cubemap_ = std::make_unique<Texture>(device, faces);
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

bool Skybox::IsValid() const
{
	return renderable_ && renderable_->IsValid() && cubemap_ && cubemap_->IsValid() &&
		   material_.IsValid() && material_.HasDepthStencilState() &&
		   material_.HasRasterizerState();
}

void Skybox::SetIntensity(float intensity)
{
	intensity_ = intensity;
	if (renderable_) renderable_->SetMaterialColor({ intensity_, intensity_, intensity_, 1.0f });
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
