#include "Water.h"
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
#include "ShaderSlots.h"
#include "Terrain.h"
#include "TerrainWorld.h"
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
	XMFLOAT4 mirror;         // x = a mirror image is bound for this body this frame, yzw unused
	XMFLOAT4 shore;          // x = a depth map is bound (t15), y = shore wave strength, z = swash, w = unused
	XMFLOAT4 groupInfo;      // wave groups: strength (0 = off), k of the pattern, its speed
	XMFLOAT4 foam2;          // x = foam bubble scale, y = seconds crest foam lingers, zw unused
	XMFLOAT4 waveDirK[WaterWaves::kMaxWaves];     // xy = direction, z = k, w = amplitude
	XMFLOAT4 waveMotion[WaterWaves::kMaxWaves];   // x = omega, y = steepness, z = phase offset
};
static_assert(sizeof(WaterMaterialCB) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// The water is a grid of TILES (see WaterMesh::ChooseTileGrid).
// Each tile is its own mesh, so it can be skipped when the camera cannot see it and drawn with fewer triangles when it is
// far away (see ChooseTiles). The grid is finest right under the camera and coarse at the horizon, instead of one uniform
// grid that is too coarse up close and wasteful far away.
constexpr int kCellsPerTile[3] = { 16, 32, 64 };   // by WaterSettings::meshDetail: cells along one tile edge
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

std::unique_ptr<SceneObject> Water::Clone(ID3D11Device* device) const
{
	auto copy = std::make_unique<Water>(device);
	copy->SetSettings(settings_);
	copy->SetEditorTransform(position_, rotation_, scale_);
	return copy;
}

bool Water::GetBoundingSphere(XMFLOAT3& center, float& radius) const
{
	center = position_;
	radius = 0.5f * std::sqrt(ExtentX() * ExtentX() + ExtentZ() * ExtentZ()) + waves_.heightBound;
	return true;
}

Water::~Water()
{
	ReleaseShoreMap();
}

void Water::ReleaseShoreMap()
{
	if (depthView_) { depthView_->Release(); depthView_ = nullptr; }
	if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
	depthField_.Clear();
	tileDry_.assign(tiles_.size(), 0);   // no ground information: every tile counts as wet
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
	depthDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;   // reversed-Z (DepthMath.h)

	D3D11_RASTERIZER_DESC wireDesc{};
	wireDesc.FillMode = D3D11_FILL_WIREFRAME;
	wireDesc.CullMode = D3D11_CULL_NONE;
	wireDesc.DepthClipEnable = TRUE;

	// Drawn from BOTH sides: from under the surface the camera looks up at its underside (the "window" of the sky, see water_ps.hlsl).
	D3D11_RASTERIZER_DESC solidDesc{};
	solidDesc.FillMode = D3D11_FILL_SOLID;
	solidDesc.CullMode = D3D11_CULL_NONE;
	solidDesc.DepthClipEnable = TRUE;

	if (!device) return;
	if (!material_.CreateRasterizerState(solidDesc)) LOG_ERROR("Water rasterizer state creation failed");
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

void Water::SetSettings(const WaterSettings& settings)
{
	// Level and size decide which ground lies under which tile: until the shore map is measured again (next frame), the
	// "dry" flags describe the OLD placement, so show every tile rather than hide one that is no longer dry.
	const bool groundMoved = settings_.height != settings.height || settings_.size != settings.size || settings_.sizeZ != settings.sizeZ;
	settings_ = settings;
	settings_.Clamp();
	position_.y = settings_.height;
	EnsureMesh();
	RebuildWaves();
	if (groundMoved) tileDry_.assign(tiles_.size(), 0);
	UpdateWorldTransform();
	UploadMaterial(false);
}

// The grid depends on the body's proportions and on the mesh detail
// setting. Both change rarely: only then is the mesh rebuilt.
void Water::EnsureMesh()
{
	// The finest cell should be small enough to show waves up to about 2.5 times the base frequency (3 cells per wavelength),
	// or the shader fades them out: so a bigger body gets more tiles, not bigger cells (WaterMesh::ChooseTileGrid).
	const float targetCell = std::clamp(6.2831853f / (3.0f * 2.5f * WaterWaves::kBaseFrequency), 0.5f, 2.0f);
	const WaterMesh::TileGrid grid = WaterMesh::ChooseTileGrid(ExtentX(), ExtentZ(), gridBuilt_ ? builtGrid_.shapeClass : -1,
																			   kCellsPerTile[std::clamp(settings_.meshDetail, 0, 2)], targetCell);
	if (gridBuilt_ && grid == builtGrid_ && settings_.meshDetail == builtMeshDetail_ && renderable_) return;
	RebuildMesh(grid);
	RebuildWaves();   // the waves the mesh can show depend on its cell size (lesson 44)
}

void Water::RebuildMesh(const WaterMesh::TileGrid& grid)
{
	if (!device_) return;
	const int cells = kCellsPerTile[std::clamp(settings_.meshDetail, 0, 2)];
	const int totalX = grid.x * cells;
	const int totalZ = grid.z * cells;
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
	meshes.reserve(static_cast<size_t>(grid.x) * grid.z);
	for (int tz = 0; tz < grid.z; ++tz) {
		for (int tx = 0; tx < grid.x; ++tx) {
			std::vector<Vertex> vertices;
			vertices.reserve(static_cast<size_t>(row) * row);
			for (int z = 0; z < row; ++z) {
				const float v = static_cast<float>(tz * cells + z) / totalZ;
				for (int x = 0; x < row; ++x) {
					const float u = static_cast<float>(tx * cells + x) / totalX;
					// Local space is [-1, 1]; Water::UpdateWorldTransform scales it to settings_.size.
					vertices.emplace_back(XMFLOAT3(u * 2.0f - 1.0f, 0.0f, v * 2.0f - 1.0f),
										  XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(u, v));
				}
			}
			meshes.push_back(new Mesh(device_, vertices, levels));
			Tile tile;
			tile.localMin = { 2.0f * tx / grid.x - 1.0f, 0.0f, 2.0f * tz / grid.z - 1.0f };
			tile.localMax = { 2.0f * (tx + 1) / grid.x - 1.0f, 0.0f, 2.0f * (tz + 1) / grid.z - 1.0f };
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
	builtGrid_ = grid;
	gridBuilt_ = true;
	builtMeshDetail_ = settings_.meshDetail;
	for (int lod = 0; lod < kWaterLodCount; ++lod) trianglesPerLod_[lod] = triangles[lod];
	tileLod_.assign(tiles_.size(), Model::kSkipMesh);   // "no level chosen yet": the first frame picks without hysteresis
	tileDry_.clear();
	UpdateDryTiles();
}

float Water::GetTravelDegrees() const
{
	return sceneWind_.direction;   // the scene's wind (a body has none of its own since lesson 63)
}

WaterWaves::Params Water::CurrentWaveParams() const
{
	WaterWaves::Params p = settings_.WaveParams();
	p.windDegrees = GetTravelDegrees();
	p.shoreWaves = settings_.shoreWaves;   // waves can grow near a shore: the table's height bound must have room for that
	p.windSpeed = sceneWind_.speed;        // the wind makes the sea: how hard it blows. The open sea has no fetch limit, and a sea is not made bigger to fit a mesh.
	p.fetch = 0.0f;
	return p;
}

float Water::FinestCell() const
{
	const float cellsX = static_cast<float>((std::max)(builtGrid_.x, 1) * (std::max)(cellsPerTile_, 1));
	const float cellsZ = static_cast<float>((std::max)(builtGrid_.z, 1) * (std::max)(cellsPerTile_, 1));
	return (std::max)(ExtentX() / cellsX, ExtentZ() / cellsZ);   // (the cells are square: the tiles are)
}

void Water::RebuildWaves()
{
	waves_ = WaterWaves::Build(CurrentWaveParams());
	seaSummary_ = SeaState::Describe(sceneWind_.speed, 0.0);
	UpdateDryTiles();   // how high the surface can reach decides which tiles are safely under the ground
}

void Water::UploadMaterial(bool mirrored) const
{
	if (!renderable_) return;
	// Colours are picked as sRGB, the shader wants linear.
	const XMFLOAT3 deep = SrgbToLinear(settings_.color);
	const XMFLOAT3 shallow = SrgbToLinear(settings_.shallowColor);
	const float windRadians = XMConvertToRadians(GetTravelDegrees());
	// One grid cell of the finest level, in world units (the cells are square: the tiles are).
	const float cellSize = FinestCell();

	WaterMaterialCB block{};
	block.deepColor = { deep.x, deep.y, deep.z, settings_.alpha };
	block.shallowColor = { shallow.x, shallow.y, shallow.z, settings_.refraction };
	block.optics = { settings_.reflectivity, settings_.glintStrength, settings_.glintSharpness, settings_.reflectionDistortion };
	block.depth = { settings_.fadeDistance, settings_.foamDistance, std::min(settings_.foamStrength * settings_.foamIntensity, 1.0f), settings_.absorption };   // (foamIntensity: the master control, lesson 43)
	block.waveInfo = { static_cast<float>(waves_.count), settings_.detailNormal, settings_.detailScale, settings_.detailDistance };
	block.foamInfo = { std::min(settings_.crestFoam * settings_.foamIntensity, 1.0f), settings_.crestFoamThreshold, settings_.scatter,
					   static_cast<float>(static_cast<int>(settings_.debugView)) };
	block.meshInfo = { settings_.lodDistance, cellSize, waves_.heightBound, 0.0f };
	block.wind = { std::cos(windRadians), std::sin(windRadians), 0.0f, 0.0f };
	block.shore = { depthView_ ? 1.0f : 0.0f, settings_.shoreWaves, settings_.swash, settings_.shoreShelter };
	block.groupInfo = { waves_.groupStrength, waves_.groupK, waves_.groupSpeed, 0.0f };
	block.foam2 = { settings_.foamScale, settings_.foamTrail, 0.0f, 0.0f };
	block.mirror = { mirrored ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
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
	const bool dryKnown = tileDry_.size() == tiles_.size();
	for (size_t i = 0; i < tiles_.size(); ++i) {
		// Under dry ground: the terrain would hide every pixel of this tile. Skip it before any other work.
		if (dryKnown && tileDry_[i]) { ++stats.tilesDry; continue; }

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
	WaterViewStats& stats = stats_[static_cast<int>(view.kind)];
	if (stats.tilesDrawn == 0) return;   // nothing of this body is on screen: no state changes, no draw

	// Which mirror image does the scene's plan give THIS body? (Several bodies can share one, or have none.)
	ID3D11ShaderResourceView* mirror = nullptr;
	int slot = -1;
	if (view.waterPlan) mirror = view.waterPlan->ImageFor(this, &slot);
	stats.reflectionSlot = mirror ? slot : -1;
	UploadMaterial(mirror != nullptr);
	ctx->PSSetShaderResources(Slots::kSrvReflection, 1, &mirror);
	// The measured ground under this body (null when there is no terrain under it). The vertex shader reads it too: that
	// is where waves grow and die.
	ID3D11ShaderResourceView* depthMap = depthView_;
	ctx->VSSetShaderResources(Slots::kSrvWaterDepth, 1, &depthMap);
	ctx->PSSetShaderResources(Slots::kSrvWaterDepth, 1, &depthMap);

	if (settings_.wireframe && wireMaterial_.HasRasterizerState()) {
		wireMaterial_.Apply(ctx);
		renderable_->Render(ctx, &drawList_);
		ctx->RSSetState(nullptr);
	} else {
		material_.Apply(ctx);
		renderable_->Render(ctx, &drawList_);
		ctx->RSSetState(nullptr);   // the next object is not drawn double-sided
	}
}

// A tile is DRY when the ground under all of it (and a little around it, because the waves push the surface sideways too)
// stands higher above the water level than the surface can ever rise: tallest wave, plus the swash run-up, plus a margin.
// Then no pixel of the tile can show: the terrain is in front of all of it. Decided once per change, not once per frame.
void Water::UpdateDryTiles()
{
	tileDry_.assign(tiles_.size(), 0);
	if (depthField_.IsEmpty() || tiles_.empty()) return;
	const float firstAmplitude = waves_.count > 0 ? waves_.waves[0].amplitude : 0.0f;
	const float rise = waves_.heightBound + settings_.swash * firstAmplitude + 0.5f;
	const float padX = 2.0f * rise / (std::max)(ExtentX(), 0.01f);   // the same distance in the plane's own -1..1 space
	const float padZ = 2.0f * rise / (std::max)(ExtentZ(), 0.01f);
	for (size_t i = 0; i < tiles_.size(); ++i) {
		const Tile& tile = tiles_[i];
		const float deepest = depthField_.MaxDepthIn(tile.localMin.x - padX, tile.localMax.x + padX,
													 tile.localMin.z - padZ, tile.localMax.z + padZ);
		tileDry_[i] = deepest < -rise ? 1 : 0;   // depth = level - ground: below -rise means the ground is more than `rise` above the level
	}
}

bool Water::IsWetVisible(const Frustum& frustum) const
{
	if (!IsVisible(frustum)) return false;   // the whole body is off screen: the cheap test first
	if (tileDry_.size() != tiles_.size()) return true;   // nothing known about the ground: assume the water shows
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	const float margin = waves_.heightBound + 0.25f;
	for (size_t i = 0; i < tiles_.size(); ++i) {
		if (tileDry_[i]) continue;
		if (!settings_.frustumCulling) return true;
		XMFLOAT3 worldMin, worldMax;
		TransformBounds(world, tiles_[i].localMin, tiles_[i].localMax, worldMin, worldMax);
		worldMin = { worldMin.x - margin, worldMin.y - margin, worldMin.z - margin };
		worldMax = { worldMax.x + margin, worldMax.y + margin, worldMax.z + margin };
		if (frustum.IntersectsAABB(worldMin, worldMax)) return true;
	}
	return false;
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
	if (!renderable_ || !renderable_->GetModel()) return false;
	// Into the plane's own space (-1..1 across), where the outline is trivial: this takes rotation and scale into account.
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	XMVECTOR determinant;
	const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
	if (std::abs(XMVectorGetX(determinant)) < 1e-12f) return false;
	const XMVECTOR local = XMVector3Transform(XMVectorSet(x, position_.y, z, 1.0f), inverse);
	const float lx = XMVectorGetX(local), lz = XMVectorGetZ(local);
	return std::abs(lx) <= 1.0f && std::abs(lz) <= 1.0f;
}

float Water::DistanceTo(const XMFLOAT3& point) const
{
	if (!renderable_ || !renderable_->GetModel()) return 0.0f;
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	XMFLOAT3 worldMin, worldMax;
	TransformBounds(world, { -1.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 1.0f }, worldMin, worldMax);
	return DistanceToBox(point, worldMin, worldMax);
}

float Water::SampleHeight(float x, float z, float time) const
{
	if (!IsOverWater(x, z)) return position_.y;
	const float level = position_.y;
	if ((settings_.shoreWaves > 0.0f || settings_.shoreShelter > 0.0f) && !depthField_.IsEmpty()) {   // the same shoaled, sheltered waves the shader draws
		return level + WaterWaves::HeightAtShore(waves_, x, z, time, settings_.shoreWaves, settings_.shoreShelter,
													   [this](float restX, float restZ) { return ShoreAtWorld(restX, restZ); });
	}
	return level + WaterWaves::HeightAt(waves_, x, z, time);
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
	rotation_ = { 0.0f, rotation.y, 0.0f };   // water is level: a body can be turned about the vertical, never tilted (the depth map and IsOverWater assume it)
	scale_ = scale;
	settings_.height = position_.y;   // one source of truth: otherwise the next SetSettings() snaps Y back
	EnsureMesh();                      // scaling can change the body's proportions
	tileDry_.assign(tiles_.size(), 0);   // moving or scaling changes which ground is under each tile: show all until the map is re-measured
	UpdateWorldTransform();
	UploadMaterial(false);             // the grid cell size depends on the scale
	return true;
}

void Water::UpdateWorldTransform()
{
	const XMMATRIX transform = XMMatrixScaling(scale_.x * settings_.size * 0.5f,
											   scale_.y, scale_.z * settings_.sizeZ * 0.5f) *
		XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) *
		XMMatrixRotationZ(rotation_.z) *
		XMMatrixTranslation(position_.x, position_.y, position_.z);
	XMFLOAT4X4 world;
	XMStoreFloat4x4(&world, transform);
	if (renderable_) renderable_->SetWorld(world);
	// The CPU reads the depth map through this: a world position -> the body's own space.
	XMVECTOR determinant;
	const XMMATRIX inverse = XMMatrixInverse(&determinant, transform);
	worldToLocalValid_ = std::abs(XMVectorGetX(determinant)) > 1e-12f;
	if (worldToLocalValid_) XMStoreFloat4x4(&worldToLocal_, inverse);
}

float Water::DepthAtWorld(float x, float z) const
{
	if (depthField_.IsEmpty() || !worldToLocalValid_) return WaterShore::kDeepWater;
	const XMVECTOR local = XMVector3Transform(XMVectorSet(x, position_.y, z, 1.0f), XMLoadFloat4x4(&worldToLocal_));
	return depthField_.Sample(XMVectorGetX(local), XMVectorGetZ(local));
}

WaterWaves::ShoreSample Water::ShoreAtWorld(float x, float z) const
{
	WaterWaves::ShoreSample shore;   // very deep, no direction
	if (depthField_.IsEmpty() || !worldToLocalValid_) return shore;
	const XMVECTOR local = XMVector3Transform(XMVectorSet(x, position_.y, z, 1.0f), XMLoadFloat4x4(&worldToLocal_));
	const float lx = XMVectorGetX(local), lz = XMVectorGetZ(local);
	shore.depth = depthField_.Sample(lx, lz);
	depthField_.SampleToward(lx, lz, shore.towardX, shore.towardZ);   // already in world space
	return shore;
}

bool Water::RayHit(const XMFLOAT3& origin, const XMFLOAT3& direction, float& distance) const
{
	if (std::abs(direction.y) < 1e-6f) return false;
	const float t = (position_.y - origin.y) / direction.y;
	if (t <= 0.0f) return false;
	const float x = origin.x + direction.x * t, z = origin.z + direction.z * t;
	if (!IsOverWater(x, z) || DepthAtWorld(x, z) <= 0.0f) return false;   // over the plane, and over water, not the dry beach
	distance = t;
	return true;
}

void Water::SetSceneWind(const WindSettings& wind)
{
	if (wind.direction == sceneWind_.direction && wind.speed == sceneWind_.speed) return;
	sceneWind_ = wind;
	RebuildWaves();
}

void Water::RefreshShoreDepth(const std::vector<const TerrainWorld*>& worlds, std::uint64_t groundStamp)
{
	if (!device_ || !renderable_ || !renderable_->GetModel()) return;
	if (worlds.empty()) {
		if (depthView_ || shoreKeyValid_) { ReleaseShoreMap(); shoreKeyValid_ = false; }
		return;
	}

	// Measure again only when something that decides the numbers changed. (The ground stamp moves whenever a world's ground does.)
	const std::array<float, 12> key = { position_.x, position_.y, position_.z, rotation_.x, rotation_.y, rotation_.z,
										scale_.x, scale_.y, scale_.z, settings_.size, settings_.sizeZ, settings_.height };
	if (shoreKeyValid_ && key == shoreKey_ && groundStamp == shoreGroundStamp_) return;
	shoreKey_ = key;
	shoreGroundStamp_ = groundStamp;
	shoreKeyValid_ = true;

	int width = 0, height = 0;
	WaterShore::ChooseResolution(ExtentX(), ExtentZ(), width, height);
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());

	// The ground of every world under this body, from the world's ground function over the body's footprint (roadmap W1): no tile
	// has to be loaded, so a tile that is still streaming in is not "100 m of open ocean", and the numbers do not depend on the mesh's level of detail.
	std::vector<WorldGroundGrid> worldGrounds;
	{
		double minX = 1.0e30, maxX = -1.0e30, minZ = 1.0e30, maxZ = -1.0e30;
		for (int corner = 0; corner < 4; ++corner) {
			const XMVECTOR p = XMVector3Transform(XMVectorSet((corner & 1) ? 1.0f : -1.0f, 0.0f, (corner & 2) ? 1.0f : -1.0f, 1.0f), world);
			minX = (std::min)(minX, static_cast<double>(XMVectorGetX(p))); maxX = (std::max)(maxX, static_cast<double>(XMVectorGetX(p)));
			minZ = (std::min)(minZ, static_cast<double>(XMVectorGetZ(p))); maxZ = (std::max)(maxZ, static_cast<double>(XMVectorGetZ(p)));
		}
		for (const TerrainWorld* ground : worlds) {
			WorldGroundGrid grid;
			if (ground && ground->BuildGroundGrid(minX, maxX, minZ, maxZ, grid)) worldGrounds.push_back(std::move(grid));
		}
	}
	WaterShore::DepthField field = WaterShore::Bake(width, height, settings_.height,
		[&world](float localX, float localZ, float& worldX, float& worldZ) {
			const XMVECTOR p = XMVector3Transform(XMVectorSet(localX, 0.0f, localZ, 1.0f), world);
			worldX = XMVectorGetX(p);
			worldZ = XMVectorGetZ(p);
		},
		[&worldGrounds](float worldX, float worldZ, float& groundY) {   // the highest ground of the worlds that cover this point
			bool found = false;
			for (const WorldGroundGrid& grid : worldGrounds) {
				float height = 0.0f;
				if (!grid.Sample(worldX, worldZ, height)) continue;
				if (!found || height > groundY) groundY = height;
				found = true;
			}
			return found;
		});

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = static_cast<UINT>(width);
	desc.Height = static_cast<UINT>(height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // depth, toward-land x, toward-land z, unused
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;   // a new map is made whenever the numbers change: it is never edited in place
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA data{};
	const std::vector<float> packed = field.PackedTexels();   // must outlive CreateTexture2D
	data.pSysMem = packed.data();
	data.SysMemPitch = static_cast<UINT>(width * 4 * sizeof(float));
	ID3D11Texture2D* texture = nullptr;
	HRESULT hr = device_->CreateTexture2D(&desc, &data, &texture);
	if (FAILED(hr)) {
		LOG_HR(hr, "Water depth map creation failed: this body has no shore effects");
		ReleaseShoreMap();
		return;
	}
	ID3D11ShaderResourceView* view = nullptr;
	hr = device_->CreateShaderResourceView(texture, nullptr, &view);
	if (FAILED(hr)) {
		LOG_HR(hr, "Water depth map view creation failed: this body has no shore effects");
		texture->Release();
		ReleaseShoreMap();
		return;
	}
	ReleaseShoreMap();   // only now: if anything above failed we kept nothing half-built
	depthTexture_ = texture;
	depthView_ = view;
	depthField_ = std::move(field);
	UpdateDryTiles();
}
