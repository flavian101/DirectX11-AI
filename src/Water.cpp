#include "Water.h"
#include "Logger.h"
#include "Mesh.h"
#include <DirectXMath.h>
#include <vector>

using namespace DirectX;

namespace {
Model* CreateWaterModel(ID3D11Device* device)
{
	const std::vector<Vertex> vertices = {
		Vertex({ -1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }),
		Vertex({ -1.0f, 0.0f,  1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }),
		Vertex({  1.0f, 0.0f,  1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }),
		Vertex({  1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f })
	};
	const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
	return new Model({ new Mesh(device, vertices, indices) });
}
}

Water::Water(ID3D11Device* device) : material_(device)
{
	renderable_ = std::make_unique<Renderable>(
		device, CreateWaterModel(device), L"shaders/water_vs.hlsl", L"shaders/water_ps.hlsl");

	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (device && !material_.CreateBlendState(blendDesc)) {
		LOG_ERROR("Water blend state creation failed");
	}

	D3D11_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthEnable = TRUE;
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (device && !material_.CreateDepthStencilState(depthDesc)) {
		LOG_ERROR("Water depth state creation failed");
	}

	SetSettings(settings_);
}

Water::~Water()
{
}

bool Water::IsValid() const
{
	return renderable_ && renderable_->IsValid() && material_.IsValid() &&
		material_.HasBlendState() && material_.HasDepthStencilState();
}

void Water::SetSettings(const WaterSettings& settings)
{
	settings_ = settings;
	position_.y = settings_.height;
	UpdateWorldTransform();
	if (renderable_) {
		renderable_->SetMaterialColor({ settings_.color.x, settings_.color.y,
										settings_.color.z, settings_.alpha });
		renderable_->SetMaterialParameters({ settings_.waveAmplitude, settings_.waveSpeed, 0.0f });
	}
}

void Water::Update(float dt)
{
	elapsedTime_ += dt;
	if (renderable_) renderable_->SetTime(elapsedTime_);
}

void Water::Render(ID3D11DeviceContext* ctx, const Camera& camera) const
{
	if (!IsValid() || !ctx) return;
	material_.Apply(ctx);
	renderable_->Render(ctx, camera.GetViewMatrix(), camera.GetProjectionMatrix());
}

bool Water::GetEditorTransform(XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale) const
{
	position = position_;
	rotation = rotation_;
	scale = scale_;
	return true;
}

bool Water::SetEditorTransform(const XMFLOAT3& position, const XMFLOAT3& rotation,
							   const XMFLOAT3& scale)
{
	position_ = position;
	rotation_ = rotation;
	scale_ = scale;
	UpdateWorldTransform();
	return true;
}

void Water::UpdateWorldTransform()
{
	const XMMATRIX transform = XMMatrixScaling(scale_.x * settings_.size * 0.5f,
												scale_.y, scale_.z * settings_.size * 0.5f) *
		XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) *
		XMMatrixRotationZ(rotation_.z) *
		XMMatrixTranslation(position_.x, position_.y, position_.z);
	XMFLOAT4X4 world;
	XMStoreFloat4x4(&world, transform);
	if (renderable_) renderable_->SetWorld(world);
}
