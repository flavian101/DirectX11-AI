#include "Water.h"
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
#include "WaterMesh.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX;

namespace {
// Must match `cbuffer WaterMaterial : register(b4)` in shaders/water_material.hlsli, field for field.
struct WaterMaterialCB {
	XMFLOAT4 deepColor;      // rgb = deep colour (linear), a = opacity of deep water
	XMFLOAT4 shallowColor;   // rgb = shallow colour (linear), a = refraction strength
	XMFLOAT4 optics;         // reflectivity, glint strength, glint sharpness, mirror distortion
	XMFLOAT4 depth;          // fade distance, foam distance, foam strength, absorption
	XMFLOAT4 waveInfo;       // wave count, detail strength, detail scale, detail fade distance
	XMFLOAT4 foamInfo;       // crest foam strength, crest foam threshold, scatter strength, debug view
	XMFLOAT4 meshInfo;       // LOD distance, finest grid cell size, tallest possible wave, unused
	XMFLOAT4 wind;           // xy = unit direction the waves travel toward
	XMFLOAT4 waveDirK[WaterWaves::kMaxWaves];     // xy = direction, z = k, w = amplitude
	XMFLOAT4 waveMotion[WaterWaves::kMaxWaves];   // x = omega, y = steepness, z = phase offset
};
static_assert(sizeof(WaterMaterialCB) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// The water is a grid of kTiles x kTiles TILES. Each tile is its own mesh, so it can be skipped when the camera
// cannot see it and drawn with fewer triangles when it is far away (see ChooseTiles). The grid is finest right
// under the camera and coarse at the horizon, instead of one uniform grid that is too coarse up close and wasteful far away.
constexpr int kTiles = 8;
constexpr int kCellsPerTile[3] = { 16, 32, 64 };   // by WaterSettings::meshDetail: 128, 256, 512 cells across
constexpr int kLodStride[kWaterLodCount] = { 1, 2, 4, 8 };   // level n keeps every stride-th vertex of the tile's interior

// World-space box that contains a local-space box after a transform (which may scale, rotate and move it).
void TransformBounds(const XMMATRIX& world, const XMFLOAT3& localMin, const XMFLOAT3& localMax,
					 XMFLOAT3& worldMin, XMFLOAT3& worldMax)
{
	const XMVECTOR lo = XMLoadFloat3(&localMin);
	const XMVECTOR hi = XMLoadFloat3(&localMax);
	const XMVECTOR center = XMVector3Transform((lo + hi) * 0.5f, world);
	const XMVECTOR half = (hi - lo) * 0.5f;
	const XMVECTOR extent = XMVectorScale(XMVectorAbs(world.r[0]), XMVectorGetX(half)) +
							XMVectorScale(XMVectorAbs(world.r[1]), XMVectorGetY(half)) +
							XMVectorScale(XMVectorAbs(world.r[2]), XMVectorGetZ(half));
	XMStoreFloat3(&worldMin, center - extent);
	XMStoreFloat3(&worldMax, center + extent);
}

// Distance from a point to the nearest point of a box (0 if inside).
float DistanceToBox(const XMFLOAT3& p, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax)
{
	const float dx = (std::max)((std::max)(boxMin.x - p.x, 0.0f), p.x - boxMax.x);
	const float dy = (std::max)((std::max)(boxMin.y - p.y, 0.0f), p.y - boxMax.y);
	const float dz = (std::max)((std::max)(boxMin.z - p.z, 0.0f), p.z - boxMax.z);
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}

Water::Water(ID3D11Device* device) : device_(device), material_(device), wireMaterial_(device)
{
	CreateRenderStates(device);
	SetSettings(settings_);   // builds the mesh, the wave table and the shader constants
}

Water::~Water()
{
}

void Water::CreateRenderStates(ID3D11Device* device)
{
	// PREMULTIPLIED alpha: result = src.rgb + dst * (1 - src.a). The shader outputs colour already multiplied by
	// its opacity, which lets it blend foam, refraction and the water body exactly (see water_ps.hlsl).
	D3D11_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	D3D11_DEPTH_STENCIL_DESC depthDesc{};
	depthDesc.DepthEnable = TRUE;
	depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	D3D11_RASTERIZER_DESC wireDesc{};
	wireDesc.FillMode = D3D11_FILL_WIREFRAME;
	wireDesc.CullMode = D3D11_CULL_NONE;
	wireDesc.DepthClipEnable = TRUE;

	if (!device) return;
	if (!material_.CreateBlendState(blendDesc)) LOG_ERROR("Water blend state creation failed");
	if (!material_.CreateDepthStencilState(depthDesc)) LOG_ERROR("Water depth state creation failed");
	if (!wireMaterial_.CreateBlendState(blendDesc) || !wireMaterial_.CreateDepthStencilState(depthDesc) ||
		!wireMaterial_.CreateRasterizerState(wireDesc)) {
		LOG_ERROR("Water wireframe state creation failed");
	}
}

bool Water::IsValid() const
{
	return renderable_ && renderable_->IsValid() && material_.IsValid() &&
		material_.HasBlendState() && material_.HasDepthStencilState();
}

void WaterSettings::Clamp()
{
	auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
	height = std::clamp(height, kHeightMin, kHeightMax);
	size = std::clamp(size, kSizeMin, kSizeMax);
	color = { clamp01(color.x), clamp01(color.y), clamp01(color.z) };
	alpha = std::clamp(alpha, 0.05f, 1.0f);
	shallowColor = { clamp01(shallowColor.x), clamp01(shallowColor.y), clamp01(shallowColor.z) };
	absorption = std::clamp(absorption, kAbsorptionMin, kAbsorptionMax);
	fadeDistance = std::clamp(fadeDistance, kFadeMin, kFadeMax);
	foamDistance = std::clamp(foamDistance, 0.0f, kFoamDistanceMax);
	foamStrength = clamp01(foamStrength);
	reflectionDistortion = std::clamp(reflectionDistortion, 0.0f, kDistortionMax);
	reflectionMode = static_cast<ReflectionMode>(std::clamp(static_cast<int>(reflectionMode), 0, 3));
	refraction = std::clamp(refraction, 0.0f, kRefractionMax);
	scatter = std::clamp(scatter, 0.0f, kScatterMax);
	waveAmplitude = std::clamp(waveAmplitude, 0.0f, kAmplitudeMax);
	waveSpeed = std::clamp(waveSpeed, 0.0f, kSpeedMax);
	waveFrequency = std::clamp(waveFrequency, kFrequencyMin, kFrequencyMax);
	windDirection = std::fmod(windDirection, 360.0f);   // an angle: wrap instead of clamping
	if (windDirection < 0.0f) windDirection += 360.0f;
	directionalSpread = clamp01(directionalSpread);
	choppiness = clamp01(choppiness);
	waveCount = std::clamp(waveCount, kWaveCountMin, kWaveCountMax);
	detailNormal = std::clamp(detailNormal, 0.0f, kDetailMax);
	detailScale = std::clamp(detailScale, kDetailScaleMin, kDetailScaleMax);
	detailDistance = std::clamp(detailDistance, kDetailDistanceMin, kDetailDistanceMax);
	reflectivity = clamp01(reflectivity);
	glintStrength = std::clamp(glintStrength, 0.0f, kGlintMax);
	glintSharpness = std::clamp(glintSharpness, kSharpnessMin, kSharpnessMax);
	crestFoam = clamp01(crestFoam);
	crestFoamThreshold = clamp01(crestFoamThreshold);
	meshDetail = std::clamp(meshDetail, 0, kMeshDetailMax);
	lodDistance = std::clamp(lodDistance, kLodDistanceMin, kLodDistanceMax);
	debugView = static_cast<WaterDebugView>(std::clamp(static_cast<int>(debugView), 0, 5));
}

void WaterSettings::ApplyWavePreset(WavePreset preset)
{
	switch (preset) {
	case WavePreset::Calm:   waveAmplitude = 0.12f; waveSpeed = 0.6f; waveFrequency = 0.45f; choppiness = 0.25f; break;
	case WavePreset::Choppy: waveAmplitude = 0.35f; waveSpeed = 1.0f; waveFrequency = 0.6f;  choppiness = 0.5f;  break;
	case WavePreset::Stormy: waveAmplitude = 0.6f;  waveSpeed = 1.8f; waveFrequency = 0.5f;  choppiness = 0.8f;  break;
	}
}

void WaterSettings::ApplyLook(WaterLook look)
{
	switch (look) {
	case WaterLook::Tropical:
		color = { 0.02f, 0.28f, 0.45f }; shallowColor = { 0.15f, 0.75f, 0.70f }; alpha = 0.60f;
		absorption = 0.25f; fadeDistance = 3.0f; foamDistance = 0.9f; foamStrength = 0.9f;
		scatter = 0.8f; refraction = 0.05f; break;
	case WaterLook::Ocean:
		color = { 0.02f, 0.12f, 0.28f }; shallowColor = { 0.08f, 0.40f, 0.50f }; alpha = 0.80f;
		absorption = 0.45f; fadeDistance = 2.5f; foamDistance = 1.0f; foamStrength = 0.9f;
		scatter = 0.5f; refraction = 0.035f; break;
	case WaterLook::Lake:
		color = { 0.05f, 0.22f, 0.25f }; shallowColor = { 0.15f, 0.45f, 0.40f }; alpha = 0.75f;
		absorption = 0.60f; fadeDistance = 2.0f; foamDistance = 0.6f; foamStrength = 0.6f;
		scatter = 0.3f; refraction = 0.03f; break;
	case WaterLook::Swamp:
		color = { 0.06f, 0.14f, 0.07f }; shallowColor = { 0.20f, 0.30f, 0.12f }; alpha = 0.92f;
		absorption = 1.20f; fadeDistance = 1.2f; foamDistance = 0.5f; foamStrength = 0.4f;
		scatter = 0.1f; refraction = 0.01f; break;
	case WaterLook::Arctic:
		color = { 0.04f, 0.20f, 0.32f }; shallowColor = { 0.45f, 0.75f, 0.85f }; alpha = 0.70f;
		absorption = 0.30f; fadeDistance = 3.5f; foamDistance = 1.2f; foamStrength = 1.0f;
		scatter = 0.4f; refraction = 0.04f; break;
	}
}

WaterWaves::Params WaterSettings::WaveParams() const
{
	WaterWaves::Params p;
	p.amplitude = waveAmplitude;
	p.speed = waveSpeed;
	p.frequency = waveFrequency;
	p.windDegrees = windDirection;
	p.spread = directionalSpread;
	p.choppiness = choppiness;
	p.count = waveCount;
	return p;
}

void Water::SetSettings(const WaterSettings& settings)
{
	settings_ = settings;
	settings_.Clamp();
	position_.y = settings_.height;
	if (settings_.meshDetail != builtMeshDetail_) RebuildMesh();
	RebuildWaves();
	UpdateWorldTransform();
	UploadMaterial();
}

void Water::RebuildMesh()
{
	if (!device_) return;
	const int cells = kCellsPerTile[std::clamp(settings_.meshDetail, 0, 2)];
	const int total = kTiles * cells;
	const int row = cells + 1;

	// The same triangle lists serve every tile (a tile is just a patch of the same grid), one per level of detail.
	std::vector<std::vector<uint32_t>> levels;
	int triangles[kWaterLodCount] = {};
	for (int lod = 0; lod < kWaterLodCount; ++lod) {
		levels.push_back(WaterMesh::BuildTileIndices(cells, kLodStride[lod]));
		triangles[lod] = static_cast<int>(levels.back().size() / 3);
	}

	std::vector<Mesh*> meshes;
	std::vector<Tile> tiles;
	meshes.reserve(static_cast<size_t>(kTiles) * kTiles);
	for (int tz = 0; tz < kTiles; ++tz) {
		for (int tx = 0; tx < kTiles; ++tx) {
			std::vector<Vertex> vertices;
			vertices.reserve(static_cast<size_t>(row) * row);
			for (int z = 0; z < row; ++z) {
				const float v = static_cast<float>(tz * cells + z) / total;
				for (int x = 0; x < row; ++x) {
					const float u = static_cast<float>(tx * cells + x) / total;
					// Local space is [-1, 1]; Water::UpdateWorldTransform scales it to settings_.size.
					vertices.emplace_back(XMFLOAT3(u * 2.0f - 1.0f, 0.0f, v * 2.0f - 1.0f),
										  XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(u, v));
				}
			}
			meshes.push_back(new Mesh(device_, vertices, levels));
			Tile tile;
			tile.localMin = { 2.0f * tx / kTiles - 1.0f, 0.0f, 2.0f * tz / kTiles - 1.0f };
			tile.localMax = { 2.0f * (tx + 1) / kTiles - 1.0f, 0.0f, 2.0f * (tz + 1) / kTiles - 1.0f };
			tiles.push_back(tile);
		}
	}

	auto replacement = std::make_unique<Renderable>(device_, new Model(meshes), L"shaders/water_vs.hlsl", L"shaders/water_ps.hlsl");
	if (!replacement->IsValid()) {
		LOG_ERROR("Water mesh rebuild failed");
		return;   // keep whatever we had
	}
	renderable_ = std::move(replacement);
	tiles_ = std::move(tiles);
	cellsPerTile_ = cells;
	builtMeshDetail_ = settings_.meshDetail;
	for (int lod = 0; lod < kWaterLodCount; ++lod) trianglesPerLod_[lod] = triangles[lod];
	tileLod_.assign(tiles_.size(), Model::kSkipMesh);   // "no level chosen yet": the first frame picks without hysteresis
}

void Water::RebuildWaves()
{
	waves_ = WaterWaves::Build(settings_.WaveParams());
}

void Water::UploadMaterial()
{
	if (!renderable_) return;
	// Colours are picked as sRGB, the shader wants linear.
	const XMFLOAT3 deep = SrgbToLinear(settings_.color);
	const XMFLOAT3 shallow = SrgbToLinear(settings_.shallowColor);
	const float windRadians = XMConvertToRadians(settings_.windDirection);
	const float totalCells = static_cast<float>(kTiles * (std::max)(cellsPerTile_, 1));
	const float cellSize = settings_.size * (std::max)(scale_.x, scale_.z) / totalCells;

	WaterMaterialCB block{};
	block.deepColor = { deep.x, deep.y, deep.z, settings_.alpha };
	block.shallowColor = { shallow.x, shallow.y, shallow.z, settings_.refraction };
	block.optics = { settings_.reflectivity, settings_.glintStrength, settings_.glintSharpness, settings_.reflectionDistortion };
	block.depth = { settings_.fadeDistance, settings_.foamDistance, settings_.foamStrength, settings_.absorption };
	block.waveInfo = { static_cast<float>(waves_.count), settings_.detailNormal, settings_.detailScale, settings_.detailDistance };
	block.foamInfo = { settings_.crestFoam, settings_.crestFoamThreshold, settings_.scatter,
					   static_cast<float>(static_cast<int>(settings_.debugView)) };
	block.meshInfo = { settings_.lodDistance, cellSize, waves_.heightBound, 0.0f };
	block.wind = { std::cos(windRadians), std::sin(windRadians), 0.0f, 0.0f };
	for (int i = 0; i < waves_.count; ++i) {
		const WaterWaves::Wave& w = waves_.waves[i];
		block.waveDirK[i] = { w.dirX, w.dirZ, w.k, w.amplitude };
		block.waveMotion[i] = { w.omega, w.steepness, w.phase0, 0.0f };
	}
	renderable_->SetMaterialBlock(&block, sizeof(block));
}

void Water::Update(float /*dt*/)
{
	// Wave animation reads the shared scene clock (PerFrame.frameParams.x) on the GPU.
}

// Decides, for the camera's view, which tiles to draw and at which level of detail.
void Water::ChooseTiles(const RenderView& view) const
{
	WaterViewStats& stats = stats_[static_cast<int>(view.kind)];
	stats = {};
	stats.tilesTotal = static_cast<int>(tiles_.size());

	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	// The waves lift and push the surface by up to heightBound in every direction: the boxes must include that, or
	// a tile whose wave crest is still on screen could be skipped.
	const float margin = waves_.heightBound + 0.25f;

	// To CHANGE level a tile must pass the threshold by 10%, so a tile on a boundary does not flicker as the camera jitters.
	const float thresholds[kWaterLodCount - 1] = { settings_.lodDistance, settings_.lodDistance * 2.2f, settings_.lodDistance * 4.5f };
	const float hysteresis = 0.10f;

	drawList_.assign(tiles_.size(), Model::kSkipMesh);
	for (size_t i = 0; i < tiles_.size(); ++i) {
		XMFLOAT3 worldMin, worldMax;
		TransformBounds(world, tiles_[i].localMin, tiles_[i].localMax, worldMin, worldMax);
		worldMin = { worldMin.x - margin, worldMin.y - margin, worldMin.z - margin };
		worldMax = { worldMax.x + margin, worldMax.y + margin, worldMax.z + margin };

		if (settings_.frustumCulling && !view.frustum.IntersectsAABB(worldMin, worldMax)) continue;

		// Distance is measured from the REAL camera even for the mirror view, so every view draws the same level.
		const float distance = DistanceToBox(view.lodOrigin, worldMin, worldMax);
		int lod = 0;
		if (tileLod_[i] == Model::kSkipMesh) {
			while (lod < kWaterLodCount - 1 && distance > thresholds[lod]) ++lod;
		} else {
			lod = tileLod_[i];
			while (lod > 0 && distance < thresholds[lod - 1] * (1.0f - hysteresis)) --lod;
			while (lod < kWaterLodCount - 1 && distance > thresholds[lod] * (1.0f + hysteresis)) ++lod;
		}
		tileLod_[i] = static_cast<unsigned char>(lod);

		drawList_[i] = static_cast<unsigned char>(lod);
		++stats.tilesDrawn;
		++stats.tilesPerLod[lod];
		stats.trianglesDrawn += trianglesPerLod_[lod];
	}
}

void Water::Render(ID3D11DeviceContext* ctx, const RenderView& view) const
{
	if (!IsValid() || !ctx) return;
	ChooseTiles(view);
	if (settings_.wireframe && wireMaterial_.HasRasterizerState()) {
		wireMaterial_.Apply(ctx);
		renderable_->Render(ctx, &drawList_);
		ctx->RSSetState(nullptr);
	} else {
		material_.Apply(ctx);
		renderable_->Render(ctx, &drawList_);
	}
}

bool Water::IsVisible(const Frustum& frustum) const
{
	if (!renderable_ || !renderable_->GetModel()) return false;
	if (!settings_.frustumCulling) return true;
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	XMFLOAT3 worldMin, worldMax;
	TransformBounds(world, { -1.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 1.0f }, worldMin, worldMax);
	const float margin = waves_.heightBound + 0.25f;
	worldMin = { worldMin.x - margin, worldMin.y - margin, worldMin.z - margin };
	worldMax = { worldMax.x + margin, worldMax.y + margin, worldMax.z + margin };
	return frustum.IntersectsAABB(worldMin, worldMax);
}

bool Water::IsOverWater(float x, float z) const
{
	const float halfX = std::abs(scale_.x) * settings_.size * 0.5f;
	const float halfZ = std::abs(scale_.z) * settings_.size * 0.5f;
	return std::abs(x - position_.x) <= halfX && std::abs(z - position_.z) <= halfZ;
}

float Water::SampleHeight(float x, float z, float time) const
{
	if (!IsOverWater(x, z)) return position_.y;
	return position_.y + WaterWaves::HeightAt(waves_, x, z, time);
}

XMFLOAT3 Water::SampleNormal(float x, float z, float time) const
{
	const float e = 0.1f;   // finite differences over 10 cm: far smaller than any wave, far larger than float noise
	const float hx = SampleHeight(x + e, z, time) - SampleHeight(x - e, z, time);
	const float hz = SampleHeight(x, z + e, time) - SampleHeight(x, z - e, time);
	XMFLOAT3 n;
	XMStoreFloat3(&n, XMVector3Normalize(XMVectorSet(-hx, 2.0f * e, -hz, 0.0f)));
	return n;
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
	UploadMaterial();   // the grid cell size depends on the scale
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
