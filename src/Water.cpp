#include "Water.h"
#include "Logger.h"
#include "Mesh.h"
#include <DirectXMath.h>
#include <algorithm>
#include <vector>

using namespace DirectX;

namespace {
// The vertex shader displaces vertices, so it can only make waves where vertices exist.
// The old mesh was ONE quad (4 vertices): the "waves" just wobbled its corners.
// A 128x128 grid gives ~16k vertices over the plane, plenty for the wave sizes we use.
constexpr int kGridCells = 128;

Model* CreateWaterModel(ID3D11Device* device)
{
	const int vertsPerRow = kGridCells + 1;
	std::vector<Vertex> vertices;
	vertices.reserve(static_cast<size_t>(vertsPerRow) * vertsPerRow);
	for (int z = 0; z < vertsPerRow; ++z) {
		const float v = static_cast<float>(z) / kGridCells;
		for (int x = 0; x < vertsPerRow; ++x) {
			const float u = static_cast<float>(x) / kGridCells;
			// Local space is [-1, 1]; Water::UpdateWorldTransform scales it to settings_.size.
			vertices.emplace_back(XMFLOAT3(u * 2.0f - 1.0f, 0.0f, v * 2.0f - 1.0f),
								  XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(u, v));
		}
	}

	std::vector<uint32_t> indices;
	indices.reserve(static_cast<size_t>(kGridCells) * kGridCells * 6);
	for (int z = 0; z < kGridCells; ++z) {
		for (int x = 0; x < kGridCells; ++x) {
			const uint32_t a = static_cast<uint32_t>(z * vertsPerRow + x);  // (x,   z)
			const uint32_t b = a + vertsPerRow;                              // (x,   z+1)
			const uint32_t c = b + 1;                                        // (x+1, z+1)
			const uint32_t d = a + 1;                                        // (x+1, z)
			// Same winding as the original quad: clockwise seen from above = front face.
			indices.insert(indices.end(), { a, b, c,  a, c, d });
		}
	}
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

void WaterSettings::Clamp()
{
	height = std::clamp(height, kHeightMin, kHeightMax);
	size = std::clamp(size, kSizeMin, kSizeMax);
	color.x = std::clamp(color.x, 0.0f, 1.0f);
	color.y = std::clamp(color.y, 0.0f, 1.0f);
	color.z = std::clamp(color.z, 0.0f, 1.0f);
	alpha = std::clamp(alpha, 0.05f, 1.0f);
	waveAmplitude = std::clamp(waveAmplitude, 0.0f, kAmplitudeMax);
	waveSpeed = std::clamp(waveSpeed, 0.0f, kSpeedMax);
	waveFrequency = std::clamp(waveFrequency, kFrequencyMin, kFrequencyMax);
	reflectivity = std::clamp(reflectivity, 0.0f, 1.0f);
	glintStrength = std::clamp(glintStrength, 0.0f, kGlintMax);
	glintSharpness = std::clamp(glintSharpness, kSharpnessMin, kSharpnessMax);
}

void WaterSettings::ApplyWavePreset(WavePreset preset)
{
	// Max wave slope is roughly 3.4 * amplitude * frequency, so keep that product around 1 or less.
	switch (preset) {
	case WavePreset::Calm:   waveAmplitude = 0.12f; waveSpeed = 0.6f; waveFrequency = 0.45f; break;
	case WavePreset::Choppy: waveAmplitude = 0.35f; waveSpeed = 1.0f; waveFrequency = 0.6f;  break;
	case WavePreset::Stormy: waveAmplitude = 0.6f;  waveSpeed = 1.8f; waveFrequency = 0.5f;  break;
	}
}

void Water::SetSettings(const WaterSettings& settings)
{
	settings_ = settings;
	settings_.Clamp();
	position_.y = settings_.height;
	UpdateWorldTransform();
	if (renderable_) {
		renderable_->SetMaterialColor({ settings_.color.x, settings_.color.y,
										settings_.color.z, settings_.alpha });
		renderable_->SetMaterialParameters({ settings_.waveAmplitude, settings_.waveSpeed,
											 settings_.waveFrequency, settings_.reflectivity });
		renderable_->SetMaterialExtra({ settings_.glintStrength, settings_.glintSharpness, 0.0f, 0.0f });
	}
}

void Water::Update(float /*dt*/)
{
	// Wave animation reads the shared scene clock (PerFrame.frameParams.x) on the GPU.
}

void Water::Render(ID3D11DeviceContext* ctx, const Camera& camera) const
{
	if (!IsValid() || !ctx) return;
	material_.Apply(ctx);
	renderable_->Render(ctx);
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
	settings_.height = position_.y;   // one source of truth: otherwise the next SetSettings() snaps Y back
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
