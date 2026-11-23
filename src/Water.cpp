#include "Water.h"
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
#include "ShaderSlots.h"
#include "Terrain.h"
#include "TerrainWorld.h"
#include "WaterMesh.h"
#include "WaterRiver.h"
#include "WaterSelect.h"
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
	XMFLOAT4 shape;          // x = a mirror image is bound for this body this frame, y = shape (0 rect, 1 ellipse), z = ellipse edge softness
	XMFLOAT4 flow;           // x = ripple drift multiplier (1 = wind-blown), y = sideways ripple drift (1 on, 0 river), z = body type, w = flow speed
	XMFLOAT4 shore;          // x = a depth map is bound (t15), y = shore wave strength, z = swash, w = unused
	XMFLOAT4 groupInfo;      // wave groups: strength (0 = off), k of the pattern, its speed
	XMFLOAT4 river;          // x = bank drag, y = how much slower the sides of the plane are, zw = unit vector ACROSS the flow in the body's own (x, z) space
	XMFLOAT4 river2;         // x = ripple stretch along the flow, y = rapids, zw unused
	XMFLOAT4 foam2;          // x = foam bubble scale, y = seconds crest foam lingers, zw unused
	XMFLOAT4 waveDirK[WaterWaves::kMaxWaves];     // xy = direction, z = k, w = amplitude
	XMFLOAT4 waveMotion[WaterWaves::kMaxWaves];   // x = omega, y = steepness, z = phase offset
};
static_assert(sizeof(WaterMaterialCB) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// The water is a grid of TILES (8 x 8 for a square body; fewer, longer rows for a river: see WaterMesh::ChooseTileGrid).
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
	ReleaseRiverFlow();
}

void Water::ReleaseRiverFlow()
{
	if (flowView_) { flowView_->Release(); flowView_ = nullptr; }
	if (flowTexture_) { flowTexture_->Release(); flowTexture_ = nullptr; }
	riverWorld_.clear();
	riverKey_.clear();
	tileOffRiver_.clear();   // no curve: every tile counts again
	if (!riverLevels_.empty()) { riverLevels_.clear(); ++riverLevelRevision_; }   // ...and the surface is level again
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

// The grid depends on the body's proportions (a river needs long rows of tiles, a pond square ones) and on the mesh detail
// setting. Both change rarely: only then is the mesh rebuilt.
void Water::EnsureMesh()
{
	// The finest cell should be small enough to show waves up to about 2.5 times the base frequency (3 cells per wavelength),
	// or the shader fades them out: so a bigger body gets more tiles, not bigger cells (WaterMesh::ChooseTileGrid).
	const float targetCell = std::clamp(6.2831853f / (3.0f * 2.5f * (std::max)(settings_.waveFrequency, 0.05f)), 0.5f, 2.0f);
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
	tileOffRiver_.assign(tiles_.size(), 0);   // new tiles: show all until RefreshRiverFlow measures them (its key includes the grid)
}

float Water::GetTravelDegrees() const
{
	if (!settings_.HasCurrent()) return sceneWind_.direction;   // the scene's wind (a body has none of its own since lesson 63)
	// The flow is stored in the body's OWN space: flowAngle 0 = along its +Z. The world matrix turns that direction by the
	// yaw (rotation about Y), so a local (sin a, cos a) becomes the world direction (sin(a + yaw), cos(a + yaw)).
	// The editor's angle runs 0 = +X, 90 = +Z, which makes that direction 90 - (a + yaw) degrees.
	return WaterWaves::RiverTravelDegrees(settings_.flowAngle, rotation_.y);
}

WaterWaves::Params Water::CurrentWaveParams() const
{
	WaterWaves::Params p = settings_.WaveParams();
	p.windDegrees = GetTravelDegrees();
	p.shoreWaves = settings_.shoreWaves;   // waves can grow near a shore: the table's height bound must have room for that
	if (settings_.HasCurrent()) {
		p.flowSpeed = settings_.flowSpeed;
		p.spread = (std::min)(p.spread, 0.25f);   // a current has one direction: a wide spread would send waves sideways or upstream
	}
	if (WavesFollowWind()) {   // the wind makes the sea (lesson 63): how hard, and over how much water. No scale: a sea is not made bigger to fit a mesh.
		p.windSpeed = sceneWind_.speed;
		p.fetch = WindFetch();
		return p;
	}
	return WaterWaves::ScaledBy(p, autoWaveScale_);   // a big body's mesh cannot show small waves: the same sea, bigger (lesson 44)
}

float Water::WindFetch() const
{
	if (settings_.bodyType == WaterBodyType::Ocean) return 0.0f;   // the open sea: no shore upwind, so nothing limits the waves but the wind
	// The wind's direction in the body's own frame. A body turned by `yaw` about Y sends its local +X to the world direction -yaw
	// (XMMatrixRotationY), so a world direction at angle a is at angle a + yaw in the body's frame.
	const float angle = XMConvertToRadians(GetTravelDegrees()) + rotation_.y;
	const double chord = SeaState::ChordLength(ExtentX(), ExtentZ(), settings_.shape == WaterShape::Ellipse, std::cos(angle), std::sin(angle));
	return static_cast<float>((std::max)(chord, 1.0));
}

float Water::FinestCell() const
{
	const float cellsX = static_cast<float>((std::max)(builtGrid_.x, 1) * (std::max)(cellsPerTile_, 1));
	const float cellsZ = static_cast<float>((std::max)(builtGrid_.z, 1) * (std::max)(cellsPerTile_, 1));
	return (std::max)(ExtentX() / cellsX, ExtentZ() / cellsZ);   // (the cells are square: the tiles are)
}

void Water::RebuildWaves()
{
	// The automatic scale is found for the sea as the author set it (so with the scale of 1), then applied by CurrentWaveParams.
	autoWaveScale_ = 1.0f;
	if (settings_.autoWaveScale && gridBuilt_ && !settings_.HasCurrent() && !WavesFollowWind()) {   // (a river keeps its waves as set: the current carries its look, and its grid is long and thin, not coarse; the wind's sea is never made bigger to fit the mesh)
		autoWaveScale_ = WaterWaves::ResolvableScale(CurrentWaveParams(), FinestCell());
	}
	waves_ = WaterWaves::Build(CurrentWaveParams());
	seaSummary_ = SeaState::Describe(sceneWind_.speed, WindFetch());
	UpdateDryTiles();   // how high the surface can reach decides which tiles are safely under the ground
}

void Water::UploadMaterial(bool mirrored) const
{
	if (!renderable_) return;
	// Colours are picked as sRGB, the shader wants linear.
	const XMFLOAT3 deep = SrgbToLinear(settings_.color);
	const XMFLOAT3 shallow = SrgbToLinear(settings_.shallowColor);
	const bool river = settings_.HasCurrent();
	const float windRadians = XMConvertToRadians(GetTravelDegrees());
	// One grid cell of the finest level, in world units (the cells are square: the tiles are).
	const float cellSize = FinestCell();
	const float halfSmallSide = 0.5f * (std::max)((std::min)(ExtentX(), ExtentZ()), 0.01f);

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
	// The ripples drift with the wind at a fixed pace (multiplier 1: how it always looked). On a river they ride the current
	// instead: the shader drifts layer 1 by 0.35 NOISE units per second, and one world unit is `detailScale` noise units,
	// so asking for flowSpeed world units per second means a multiplier of flowSpeed * detailScale / 0.35.
	block.shore = { depthView_ ? 1.0f : 0.0f, settings_.shoreWaves, settings_.swash, settings_.shoreShelter };
	block.groupInfo = { waves_.groupStrength, waves_.groupK, waves_.groupSpeed, 0.0f };
	// The flow runs along the body's own (sin a, cos a) in (x, z); "across" is turned a quarter: (cos a, -sin a).
	const float flowRadians = XMConvertToRadians(settings_.flowAngle);
	block.river = { settings_.bankDrag, settings_.channelProfile, std::cos(flowRadians), -std::sin(flowRadians) };
	block.river2 = { settings_.rippleStretch, settings_.rapids, flowView_ ? 1.0f : 0.0f, 0.0f };   // z: the river has a curve, read its flow map
	block.foam2 = { settings_.foamScale, settings_.foamTrail, 0.0f, 0.0f };
	block.flow = { river ? settings_.flowSpeed * settings_.detailScale / 0.35f : 1.0f, river ? 0.0f : 1.0f,
				   static_cast<float>(static_cast<int>(settings_.bodyType)), river ? settings_.flowSpeed : 0.0f };
	// The rim of a round body fades over about 40 cm (as a fraction of its radius; the plane's own space is -1..1).
	block.shape = { mirrored ? 1.0f : 0.0f, settings_.shape == WaterShape::Ellipse ? 1.0f : 0.0f, 0.4f / halfSmallSide, 0.0f };
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
	const bool round = settings_.shape == WaterShape::Ellipse;

	// To CHANGE level a tile must pass the threshold by 10%, so a tile on a boundary does not flicker as the camera jitters.
	const float thresholds[kWaterLodCount - 1] = { settings_.lodDistance, settings_.lodDistance * 2.2f, settings_.lodDistance * 4.5f };
	const float hysteresis = 0.10f;

	drawList_.assign(tiles_.size(), Model::kSkipMesh);
	const bool dryKnown = tileDry_.size() == tiles_.size();
	const bool offRiverKnown = tileOffRiver_.size() == tiles_.size();
	for (size_t i = 0; i < tiles_.size(); ++i) {
		// Under dry ground: the terrain would hide every pixel of this tile. Skip it before any other work.
		if (dryKnown && tileDry_[i]) { ++stats.tilesDry; continue; }
		// A river is only where its curve is: the shader clips the rest, so do not even send the tile.
		if (offRiverKnown && tileOffRiver_[i]) { ++stats.tilesOffRiver; continue; }

		XMFLOAT3 worldMin, worldMax;
		TransformBounds(world, tiles_[i].localMin, tiles_[i].localMax, worldMin, worldMax);
		worldMin = { worldMin.x - margin, worldMin.y - margin, worldMin.z - margin };
		worldMax = { worldMax.x + margin, worldMax.y + margin, worldMax.z + margin };

		if (settings_.frustumCulling && !view.frustum.IntersectsAABB(worldMin, worldMax)) continue;

		// A round body only fills the middle of its square: tiles in the corners lie wholly outside the ellipse.
		if (round && TileOutsideOutline(i)) continue;

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
	ID3D11ShaderResourceView* flowMap = flowView_;   // null for a body with no curve (the shader is told by river2.z)
	ctx->PSSetShaderResources(Slots::kSrvWaterFlow, 1, &flowMap);

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

bool Water::TileOutsideOutline(size_t index) const
{
	const XMFLOAT3& lo = tiles_[index].localMin;
	const XMFLOAT3& hi = tiles_[index].localMax;
	const float nearestX = (lo.x <= 0.0f && hi.x >= 0.0f) ? 0.0f : (std::min)(std::abs(lo.x), std::abs(hi.x));
	const float nearestZ = (lo.z <= 0.0f && hi.z >= 0.0f) ? 0.0f : (std::min)(std::abs(lo.z), std::abs(hi.z));
	return nearestX * nearestX + nearestZ * nearestZ > 1.0f;
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

// A tile is OFF THE RIVER when no part of the river's corridor reaches it (WaterRiver::CorridorReaches; the shader clips the water
// beyond the corridor, so such a tile could not show a pixel anyway). The waves push the surface sideways, so the tile's reach
// is grown by the wave margin like the frustum test. Decided when the curve, the body or the mesh changes, not every frame.
void Water::UpdateRiverTiles()
{
	tileOffRiver_.assign(tiles_.size(), 0);
	if (!settings_.HasRiverPath() || riverWorld_.size() < 2 || tiles_.empty() || !renderable_ || !renderable_->GetModel()) return;
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	const float margin = waves_.heightBound + 0.25f;
	for (size_t i = 0; i < tiles_.size(); ++i) {
		XMFLOAT3 worldMin, worldMax;
		TransformBounds(world, tiles_[i].localMin, tiles_[i].localMax, worldMin, worldMax);   // (a box in the world, turned or not)
		const float cx = 0.5f * (worldMin.x + worldMax.x), cz = 0.5f * (worldMin.z + worldMax.z);
		const float radius = 0.5f * std::hypot(worldMax.x - worldMin.x, worldMax.z - worldMin.z) + margin;
		tileOffRiver_[i] = WaterRiver::CorridorReaches(riverWorld_, cx, cz, radius) ? 0 : 1;
	}
}

bool Water::IsWetVisible(const Frustum& frustum) const
{
	if (!IsVisible(frustum)) return false;   // the whole body is off screen: the cheap test first
	if (tileDry_.size() != tiles_.size()) return true;   // nothing known about the ground: assume the water shows
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	const float margin = waves_.heightBound + 0.25f;
	const bool round = settings_.shape == WaterShape::Ellipse;
	for (size_t i = 0; i < tiles_.size(); ++i) {
		if (tileDry_[i]) continue;
		if (tileOffRiver_.size() == tiles_.size() && tileOffRiver_[i]) continue;
		if (round && TileOutsideOutline(i)) continue;
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
	// Into the plane's own space (-1..1 across), where both outlines are trivial: this takes rotation and scale into account.
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	XMVECTOR determinant;
	const XMMATRIX inverse = XMMatrixInverse(&determinant, world);
	if (std::abs(XMVectorGetX(determinant)) < 1e-12f) return false;
	const XMVECTOR local = XMVector3Transform(XMVectorSet(x, position_.y, z, 1.0f), inverse);
	const float lx = XMVectorGetX(local), lz = XMVectorGetZ(local);
	const bool onPlane = settings_.shape == WaterShape::Ellipse ? lx * lx + lz * lz <= 1.0f : (std::abs(lx) <= 1.0f && std::abs(lz) <= 1.0f);
	if (!onPlane) return false;
	// A river with a curve is water only inside its corridor (the shader clips the rest): a boat, a wave query or the camera
	// out beside it is not on water.
	if (settings_.HasRiverPath() && riverWorld_.size() >= 2) return WaterRiver::InCorridor(riverWorld_, x, z);
	return true;
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
	const float level = position_.y + LevelAtWorld(x, z);   // a river that falls: the surface at rest is not at the body's level everywhere
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

XMFLOAT2 Water::BodyToWorld(float bodyX, float bodyZ) const
{
	// The plane's own space is -1..1; the body's frame is the same thing in world units from its centre.
	if (!renderable_ || !renderable_->GetModel()) return { position_.x + bodyX, position_.z + bodyZ };
	const float lx = bodyX / (std::max)(0.5f * ExtentX(), 1e-3f), lz = bodyZ / (std::max)(0.5f * ExtentZ(), 1e-3f);
	const XMVECTOR p = XMVector3Transform(XMVectorSet(lx, 0.0f, lz, 1.0f), XMLoadFloat4x4(&renderable_->GetModel()->GetWorld()));
	return { XMVectorGetX(p), XMVectorGetZ(p) };
}

bool Water::WorldToBody(float worldX, float worldZ, float& bodyX, float& bodyZ) const
{
	if (!worldToLocalValid_) return false;
	const XMVECTOR local = XMVector3Transform(XMVectorSet(worldX, position_.y, worldZ, 1.0f), XMLoadFloat4x4(&worldToLocal_));
	bodyX = XMVectorGetX(local) * 0.5f * ExtentX();
	bodyZ = XMVectorGetZ(local) * 0.5f * ExtentZ();
	return true;
}

void Water::RefreshRiverFlow()
{
	if (!device_ || !renderable_ || !renderable_->GetModel() || !worldToLocalValid_) return;
	if (!settings_.HasRiverPath()) {
		if (flowView_ || !riverWorld_.empty() || !riverKey_.empty()) ReleaseRiverFlow();
		return;
	}
	// Measure again only when something that decides the numbers changed: the curve, or where the body is and how big.
	std::vector<float> key = { position_.x, position_.y, position_.z, rotation_.x, rotation_.y, rotation_.z,
							   scale_.x, scale_.y, scale_.z, settings_.size, settings_.sizeZ, static_cast<float>(settings_.riverPath.size()),
							   static_cast<float>(builtGrid_.x), static_cast<float>(builtGrid_.z), static_cast<float>(settings_.meshDetail) };   // (a rebuilt mesh has new tiles to measure)
	for (const RiverPoint& p : settings_.riverPath) { key.push_back(p.x); key.push_back(p.z); key.push_back(p.width); key.push_back(p.level); }
	if (key == riverKey_) return;
	riverKey_ = std::move(key);

	// The curve in the world. A spline is the same curve whether it is built before or after a move, a turn or an even scale.
	std::vector<RiverPoint> worldPoints;
	worldPoints.reserve(settings_.riverPath.size());
	for (const RiverPoint& p : settings_.riverPath) {
		const XMFLOAT2 w = BodyToWorld(p.x, p.z);
		worldPoints.push_back({ w.x, w.y, p.width, p.level });
	}
	riverWorld_ = RiverPath::Build(worldPoints, 1.0f);
	UpdateRiverTiles();   // which tiles the corridor reaches: decided here, once per change, not once per frame

	int width = 0, height = 0;
	WaterShore::ChooseResolution(ExtentX(), ExtentZ(), width, height);   // the same texel grid as the depth map
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	const XMMATRIX toLocal = XMLoadFloat4x4(&worldToLocal_);
	const float planeY = position_.y;
	std::vector<float> levels;
	const std::vector<float> texels = WaterRiver::Bake(width, height, riverWorld_,
		[&world](float localX, float localZ, float& worldX, float& worldZ) {
			const XMVECTOR p = XMVector3Transform(XMVectorSet(localX, 0.0f, localZ, 1.0f), world);
			worldX = XMVectorGetX(p);
			worldZ = XMVectorGetZ(p);
		},
		[&toLocal, planeY](float worldX, float worldZ, float& localX, float& localZ) {
			const XMVECTOR p = XMVector3Transform(XMVectorSet(worldX, planeY, worldZ, 1.0f), toLocal);
			localX = XMVectorGetX(p);
			localZ = XMVectorGetZ(p);
		}, &levels);

	// The water's height along the river (lesson 41). A river with no fall bakes all zeros, which equals what was there before,
	// so the depth map (which includes the levels) is measured again only when they really changed.
	bool anyFall = false;
	for (float v : levels) if (v != 0.0f) { anyFall = true; break; }
	if (!anyFall) levels.clear();
	if (levels != riverLevels_) { riverLevels_ = std::move(levels); ++riverLevelRevision_; }

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = static_cast<UINT>(width);
	desc.Height = static_cast<UINT>(height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // s, d, flow direction x, z (WaterRiver.h)
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;             // a new map is made whenever the curve changes
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA data{};
	data.pSysMem = texels.data();
	data.SysMemPitch = static_cast<UINT>(width * 4 * sizeof(float));
	ID3D11Texture2D* texture = nullptr;
	HRESULT hr = device_->CreateTexture2D(&desc, &data, &texture);
	if (FAILED(hr)) {
		LOG_HR(hr, "River flow map creation failed: this river has no curve to follow");
		if (flowView_) { flowView_->Release(); flowView_ = nullptr; }
		if (flowTexture_) { flowTexture_->Release(); flowTexture_ = nullptr; }
		return;   // (the key stays: no retry every frame)
	}
	ID3D11ShaderResourceView* view = nullptr;
	hr = device_->CreateShaderResourceView(texture, nullptr, &view);
	if (FAILED(hr)) {
		LOG_HR(hr, "River flow map view creation failed: this river has no curve to follow");
		texture->Release();
		if (flowView_) { flowView_->Release(); flowView_ = nullptr; }
		if (flowTexture_) { flowTexture_->Release(); flowTexture_ = nullptr; }
		return;
	}
	if (flowView_) flowView_->Release();
	if (flowTexture_) flowTexture_->Release();
	flowTexture_ = texture;
	flowView_ = view;
}

XMFLOAT2 Water::SampleFlow(float x, float z) const
{
	if (!settings_.HasCurrent() || !IsOverWater(x, z) || !worldToLocalValid_) return { 0.0f, 0.0f };
	if (settings_.HasRiverPath() && !riverWorld_.empty()) {
		// A river with a curve: the water runs along it, fastest in the middle (the same numbers as the shader's flow map).
		const RiverPath::Hit hit = RiverPath::Closest(riverWorld_, x, z);
		if (!hit.found) return { 0.0f, 0.0f };
		const float mask = 1.0f - WaterRiver::Smoothstep(WaterRiver::kCorridorFull, WaterRiver::kCorridorEnd, std::abs(hit.across));
		const float speed = mask * settings_.flowSpeed * WaterFlow::SpeedFactor(hit.across, DepthAtWorld(x, z), settings_.bankDrag, settings_.channelProfile);
		return { hit.tx * speed, hit.tz * speed };
	}
	const XMVECTOR local = XMVector3Transform(XMVectorSet(x, position_.y, z, 1.0f), XMLoadFloat4x4(&worldToLocal_));
	const float flowRadians = XMConvertToRadians(settings_.flowAngle);
	const float across = XMVectorGetX(local) * std::cos(flowRadians) - XMVectorGetZ(local) * std::sin(flowRadians);   // the shader's dot(local, river.zw)
	const float speed = settings_.flowSpeed * WaterFlow::SpeedFactor(across, DepthAtWorld(x, z), settings_.bankDrag, settings_.channelProfile);
	const float travel = XMConvertToRadians(GetTravelDegrees());
	return { std::cos(travel) * speed, std::sin(travel) * speed };
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
	rotation_ = { 0.0f, rotation.y, 0.0f };   // water is level: a body can be turned about the vertical, never tilted (the river's frame, the depth map and IsOverWater all assume it)
	scale_ = scale;
	settings_.height = position_.y;   // one source of truth: otherwise the next SetSettings() snaps Y back
	if (settings_.HasCurrent() || WavesFollowWind()) RebuildWaves();   // a current's flow follows the way the body is turned; a wind sea's fetch follows its size and turn
	EnsureMesh();                      // scaling can change the body's proportions: a river needs a different grid than a pond
	if (settings_.autoWaveScale) RebuildWaves();   // ...and the size of its cells, so the waves it can show (lesson 44)
	tileDry_.assign(tiles_.size(), 0);   // moving or scaling changes which ground is under each tile: show all until the map is re-measured
	tileOffRiver_.assign(tiles_.size(), 0);   // ...and where the corridor lies relative to them (RefreshRiverFlow measures again: the key has the placement)
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
	const float o[3] = { origin.x, origin.y, origin.z };
	const float d[3] = { direction.x, direction.y, direction.z };
	return WaterSelect::RayMeetsSurface(
		o, d, position_.y,
		[this](float x, float z) { return LevelAtWorld(x, z); },                                        // a river that falls: the surface is not at one height
		[this](float x, float z) { return IsOverWater(x, z) && DepthAtWorld(x, z) > 0.0f; },            // water here (the corridor), and not the dry beach
		distance);
}

float Water::LevelAtWorld(float x, float z) const
{
	if (!depthField_.HasLevelOffsets() || !worldToLocalValid_) return 0.0f;
	const XMVECTOR local = XMVector3Transform(XMVectorSet(x, position_.y, z, 1.0f), XMLoadFloat4x4(&worldToLocal_));
	return depthField_.SampleLevel(XMVectorGetX(local), XMVectorGetZ(local));   // the same blend as the vertex shader's read of the map
}

void Water::SetSceneWind(const WindSettings& wind)
{
	if (wind.direction == sceneWind_.direction && wind.speed == sceneWind_.speed) return;
	sceneWind_ = wind;
	// A current carries its own waves, and the artist's styles do not listen to the wind: nothing to rebuild for them.
	if (WavesFollowWind()) RebuildWaves();
}

void Water::RefreshShoreDepth(const std::vector<const Terrain*>& terrains, const std::vector<const TerrainWorld*>& worlds, std::uint64_t groundStamp)
{
	if (!device_ || !renderable_ || !renderable_->GetModel()) return;
	if (terrains.empty() && worlds.empty()) {
		if (depthView_ || shoreKeyValid_) { ReleaseShoreMap(); shoreKeyValid_ = false; }
		return;
	}

	// Measure again only when something that decides the numbers changed. (The terrain's revision moves whenever its
	// heights or its placement do.)
	const std::array<float, 12> key = { position_.x, position_.y, position_.z, rotation_.x, rotation_.y, rotation_.z,
										scale_.x, scale_.y, scale_.z, settings_.size, settings_.sizeZ, settings_.height };
	if (shoreKeyValid_ && key == shoreKey_ && groundStamp == shoreGroundStamp_ && shoreLevelRevision_ == riverLevelRevision_) return;
	shoreKey_ = key;
	shoreLevelRevision_ = riverLevelRevision_;
	shoreGroundStamp_ = groundStamp;
	shoreKeyValid_ = true;

	int width = 0, height = 0;
	WaterShore::ChooseResolution(ExtentX(), ExtentZ(), width, height);
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());

	// The ground of every world under this body, from the world's ground function over the body's footprint (roadmap W1): no tile
	// has to be loaded, so a tile that is still streaming in is not "100 m of open ocean", and the numbers do not depend on the mesh's level of detail.
	std::vector<WorldGroundGrid> worldGrounds;
	if (!worlds.empty()) {
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
		[&terrains, &worldGrounds](float worldX, float worldZ, float& groundY) {   // the highest ground of the terrains and worlds that cover this point
			bool found = false;
			for (const WorldGroundGrid& grid : worldGrounds) {
				float height = 0.0f;
				if (!grid.Sample(worldX, worldZ, height)) continue;
				if (!found || height > groundY) groundY = height;
				found = true;
			}
			for (const Terrain* terrain : terrains) {
				float height = 0.0f;
				if (!terrain->GetHeightAt(worldX, worldZ, height)) continue;
				if (!found || height > groundY) groundY = height;
				found = true;
			}
			return found;
		},
		riverLevels_.empty() ? nullptr : &riverLevels_);   // a river that falls: depth = its level there - the ground (ignored if it is not this map's size)

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = static_cast<UINT>(width);
	desc.Height = static_cast<UINT>(height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // depth, toward-land x, toward-land z, level offset (lesson 41)
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
