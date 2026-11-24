#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Terrain.h"
#include "TerrainChunkIndices.h"
#include <chrono>
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
#include "RiverCarve.h"
#include "ShaderSlots.h"
#include "TerrainChunks.h"
#include "libs/stb_image.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
// Must match `cbuffer TerrainMaterial : register(b4)` in shaders/terrain_ps.hlsl, field for field.
struct TerrainMaterialCB {
	XMFLOAT4 layers;       // rock tiling, rock brightness, -, -
	XMFLOAT4 blend;        // height blend, height-blend transition width, debug flag, colour-map influence
	XMFLOAT4 splat;        // world X, world Z of the terrain's low corner, 1 / world size X, 1 / world size Z
	XMFLOAT4 tilings;      // ground tiling, sand tiling, snow tiling, -
	XMFLOAT4 surface;      // normal strength, flip green (1/0), AO strength, roughness scale
	XMFLOAT4 groundTint;   // linear rgb
	XMFLOAT4 rockTint;
	XMFLOAT4 shoreTint;
	XMFLOAT4 snowTint;
};
static_assert(sizeof(TerrainMaterialCB) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// How close behind the tallest layer another layer may be and still show through (height-based blending, terrain_ps.hlsl).
constexpr float kHeightTransition = 0.2f;

SplatRules MakeSplatRules(const TerrainSettings& s)
{
	SplatRules rules;
	rules.slopeStart = s.slopeStart;     rules.slopeFade = s.slopeFade;
	rules.shoreHeight = s.shoreHeight;   rules.shoreWidth = s.shoreWidth;
	rules.snowHeight = s.snowHeight;     rules.snowFade = s.snowFade;
	rules.breakupScale = s.breakupScale; rules.breakupAmount = s.breakupAmount;
	rules.seed = s.breakupSeed;
	return rules;
}

// The baked layer weights as a GPU texture (RGBA8, no mips: they are smooth, and the shader reads level 0). They are DATA,
// so the format is not sRGB. The caller owns the two objects it gets back.
bool CreateSplatTexture(ID3D11Device* device, const SplatMap& map, ID3D11Texture2D*& texture, ID3D11ShaderResourceView*& view)
{
	texture = nullptr;
	view = nullptr;
	if (!device || map.IsEmpty()) return false;
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = static_cast<UINT>(map.width);
	desc.Height = static_cast<UINT>(map.height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = map.rgba.data();
	initial.SysMemPitch = static_cast<UINT>(map.width * kSplatChannels);
	HRESULT hr = device->CreateTexture2D(&desc, &initial, &texture);
	if (FAILED(hr)) {
		LOG_HR(hr, "Failed to create the terrain splat map");
		texture = nullptr;
		return false;
	}
	hr = device->CreateShaderResourceView(texture, nullptr, &view);
	if (FAILED(hr)) {
		LOG_HR(hr, "Failed to create the terrain splat map view");
		texture->Release();
		texture = nullptr;
		view = nullptr;
		return false;
	}
	return true;
}

static_assert(kTerrainSkipChunk == Model::kSkipMesh, "the terrain and the model must agree on the \"not drawn\" marker");

constexpr int kChunkCells = kTerrainChunkCells;   // grid cells per tile side: 32x32 quads = 2048 triangles per tile

// How many cells the grid has for a heightmap of this pixel size, and how deep the terrain is (it follows the picture's shape).
struct GridShape {
	int cellsX = 0;
	int cellsZ = 0;
	float depth = 0.0f;   // world-space size along Z
};

// How far the skirts hang down: enough to cover the height gap a coarser neighbour can leave.
float SkirtDepthFor(float heightScale)
{
	return (std::max)(1.5f, heightScale * 0.06f);
}

// ONE chunk's vertices: the block x0..x1, z0..z1 of the height grid, then the four skirts (north, south, west, east),
// in exactly the order the chunk's index buffers expect. The same function builds a chunk the first time and rebuilds
// it after a sculpt stroke, so the two can never disagree. Normals come from the WHOLE grid, so lighting is seamless
// across chunk borders. `boundsMin/Max` come out including the skirt.
void BuildChunkVertices(const Heightfield& field, int x0, int z0, int x1, int z1, std::vector<Vertex>& vertices,
						XMFLOAT3& boundsMin, XMFLOAT3& boundsMax, const TerrainWalls& walls = TerrainWalls{})
{
	const HeightfieldLayout& layout = field.GetLayout();
	const float skirtDepth = SkirtDepthFor(layout.heightScale);
	const int cellsInX = x1 - x0;
	const int cellsInZ = z1 - z0;
	const int vertsX = cellsInX + 1;

	vertices.clear();
	vertices.reserve(static_cast<size_t>(vertsX) * (cellsInZ + 1) + 2 * static_cast<size_t>(vertsX + cellsInZ + 1) + 4);
	boundsMin = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
	boundsMax = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (int z = z0; z <= z1; ++z) {
		for (int x = x0; x <= x1; ++x) {
			const float u = static_cast<float>(x) / static_cast<float>(layout.cellsX);
			const float v = static_cast<float>(z) / static_cast<float>(layout.cellsZ);
			const HfVec3 n = field.VertexNormal(x, z);
			vertices.push_back(Vertex(XMFLOAT3((u - 0.5f) * layout.sizeX, field.Sample(x, z) * layout.heightScale, (v - 0.5f) * layout.sizeZ),
									  XMFLOAT3(n.x, n.y, n.z), XMFLOAT2(u, v)));
			const XMFLOAT3& p = vertices.back().position;
			boundsMin = { (std::min)(boundsMin.x, p.x), (std::min)(boundsMin.y, p.y), (std::min)(boundsMin.z, p.z) };
			boundsMax = { (std::max)(boundsMax.x, p.x), (std::max)(boundsMax.y, p.y), (std::max)(boundsMax.z, p.z) };
		}
	}

	// SKIRTS: copies of the edge vertices moved straight down (see the long comment in BuildTerrainModel).
	// A WALL (roadmap X1) is a skirt on the edge of a bounded world: it goes all the way down to a floor instead of a few metres, and
	// faces outward (the normal and the tangent follow, so it is lit as a wall and not as a strip of ground).
	const unsigned char wallMask = ChunkWallEdges(walls.edges, x0, z0, x1, z1, layout.cellsX, layout.cellsZ);
	auto addSkirtVertex = [&](int x, int z, unsigned char edgeBit, float outX, float outZ) {
		Vertex copy = vertices[static_cast<size_t>(z) * vertsX + x];
		if (wallMask & edgeBit) {
			copy.position.y = walls.floorY;
			copy.normal = XMFLOAT3(outX, 0.0f, outZ);
			copy.tangent = (outX != 0.0f) ? XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) : XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);   // along the wall
		} else {
			copy.position.y -= skirtDepth;
		}
		boundsMin.y = (std::min)(boundsMin.y, copy.position.y);
		vertices.push_back(copy);
	};
	for (int x = 0; x <= cellsInX; ++x) addSkirtVertex(x, 0, kEdgeNorth, 0.0f, -1.0f);
	for (int x = 0; x <= cellsInX; ++x) addSkirtVertex(x, cellsInZ, kEdgeSouth, 0.0f, 1.0f);
	for (int z = 0; z <= cellsInZ; ++z) addSkirtVertex(0, z, kEdgeWest, -1.0f, 0.0f);
	for (int z = 0; z <= cellsInZ; ++z) addSkirtVertex(cellsInX, z, kEdgeEast, 1.0f, 0.0f);

	// THE BOTTOM (roadmap X1): the four corners of the chunk at the floor, facing down. Always four vertices (the index buffers are shared
	// by every chunk of this size); a tile that is not part of a bounded world never draws them, so they stay where the surface is.
	auto addCapVertex = [&](int x, int z) {
		Vertex corner = vertices[static_cast<size_t>(z) * vertsX + x];
		if (walls.cap) {
			corner.position.y = walls.floorY;
			corner.normal = XMFLOAT3(0.0f, -1.0f, 0.0f);
			corner.tangent = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
			boundsMin.y = (std::min)(boundsMin.y, corner.position.y);
		}
		vertices.push_back(corner);
	};
	addCapVertex(0, 0);
	addCapVertex(cellsInX, 0);
	addCapVertex(0, cellsInZ);
	addCapVertex(cellsInX, cellsInZ);
}

Model* BuildTerrainModel(ID3D11Device* device, const Heightfield& field, std::vector<TerrainChunk>& outChunks,
						 int& outChunksX, int& outChunksZ, const TerrainWalls& walls = TerrainWalls{})
{
	if (!device || field.IsEmpty()) return nullptr;
	const HeightfieldLayout& layout = field.GetLayout();
	const int cellsX = layout.cellsX;
	const int cellsZ = layout.cellsZ;

	// Cut the grid into tiles of kChunkCells x kChunkCells quads. Each tile becomes its own mesh
	// (with its own bounding box), so a view can draw only the tiles it can see. Tiles on the
	// right/bottom edge are smaller when the grid does not divide evenly.
	const int chunksX = (cellsX + kChunkCells - 1) / kChunkCells;
	const int chunksZ = (cellsZ + kChunkCells - 1) / kChunkCells;
	outChunksX = chunksX;
	outChunksZ = chunksZ;
	std::vector<Mesh*> meshes;
	meshes.reserve(static_cast<size_t>(chunksX) * chunksZ);
	outChunks.clear();
	outChunks.reserve(meshes.capacity());

	for (int chunkZ = 0; chunkZ < chunksZ; ++chunkZ) {
		for (int chunkX = 0; chunkX < chunksX; ++chunkX) {
			const int x0 = chunkX * kChunkCells, x1 = (std::min)(x0 + kChunkCells, cellsX);
			const int z0 = chunkZ * kChunkCells, z1 = (std::min)(z0 + kChunkCells, cellsZ);

			std::vector<Vertex> vertices;
			XMFLOAT3 boundsMin, boundsMax;
			BuildChunkVertices(field, x0, z0, x1, z1, vertices, boundsMin, boundsMax, walls);

			const int cellsInX = x1 - x0;
			const int cellsInZ = z1 - z0;

			// SKIRTS. Two neighbouring tiles at different detail levels do not share edge vertices,
			// which opens thin cracks between them. We hang a vertical strip of triangles below a
			// tile edge so that whatever is behind a crack is the terrain, not the sky. These extra
			// vertices are copies of the edge vertices, moved straight down. The skirts are stored as
			// separate PARTS of the index buffer (north, south, west, east) so that a draw can include
			// only the edges that really have a neighbour at a different level (see TerrainSkirts.h).
			// The indices (the surface and the four skirts, three levels of detail) depend only on the chunk's size in cells: one shared
			// set per size, built once (TerrainChunkIndices.cpp), so a new tile creates vertex buffers only (roadmap T3).
			const SharedIndexLevels* shared = GetSharedChunkIndices(device, cellsInX, cellsInZ, kTerrainLodCount);
			if (!shared) {
				for (Mesh* made : meshes) delete made;
				outChunks.clear();
				return nullptr;
			}
			meshes.push_back(new Mesh(device, vertices, *shared, /*updatableVertices=*/true));   // sculpting rewrites them
			TerrainChunk chunk;
			chunk.boundsMin = boundsMin;
			chunk.boundsMax = boundsMax;
			for (int lod = 0; lod < kTerrainLodCount; ++lod) {
				chunk.triangleCount[lod] = static_cast<int>(shared->parts[lod][0] / 3);
				for (int edge = 0; edge < 4; ++edge) chunk.skirtTriangles[lod][edge] = static_cast<int>(shared->parts[lod][1 + edge] / 3);
			}
			outChunks.push_back(chunk);
		}
	}

	return new Model(meshes);
}
}

void TerrainSettings::Clamp()
{
	width = std::clamp(width, kWidthMin, kWidthMax);
	heightScale = std::clamp(heightScale, kHeightMin, kHeightMax);
	maxCells = std::clamp(maxCells, kCellsMin, kCellsMax);
	uvTiling = std::clamp(uvTiling, kTilingMin, kTilingMax);
	groundTiling = std::clamp(groundTiling, kRockTilingMin, kRockTilingMax);
	rockTiling = std::clamp(rockTiling, kRockTilingMin, kRockTilingMax);
	sandTiling = std::clamp(sandTiling, kRockTilingMin, kRockTilingMax);
	snowTiling = std::clamp(snowTiling, kRockTilingMin, kRockTilingMax);
	rockDarkness = std::clamp(rockDarkness, 0.1f, 1.5f);
	normalStrength = std::clamp(normalStrength, 0.0f, 3.0f);
	heightBlend = std::clamp(heightBlend, 0.0f, 1.0f);
	aoStrength = std::clamp(aoStrength, 0.0f, 1.0f);
	roughnessScale = std::clamp(roughnessScale, 0.0f, 2.0f);
	slopeStart = std::clamp(slopeStart, 0.0f, 1.0f);
	slopeFade = std::clamp(slopeFade, 0.01f, 1.0f);
	shoreHeight = std::clamp(shoreHeight, kBandHardMin, kBandHardMax);
	shoreWidth = std::clamp(shoreWidth, 0.1f, 30.0f);
	snowHeight = std::clamp(snowHeight, kBandHardMin, kBandHardMax);
	snowFade = std::clamp(snowFade, 0.1f, 50.0f);
	breakupScale = std::clamp(breakupScale, kBreakupScaleMin, kBreakupScaleMax);
	breakupAmount = std::clamp(breakupAmount, 0.0f, 1.0f);
	lodDistance0 = std::clamp(lodDistance0, kLodDistanceMin, kLodDistanceMax);
	lodDistance1 = std::clamp(lodDistance1, lodDistance0 * 1.2f, kLodDistanceMax * 2.0f);   // level 2 must start beyond level 1
}

void TerrainSettings::ResetLayers()
{
	const TerrainSettings defaults;
	groundTiling = defaults.groundTiling; rockTiling = defaults.rockTiling;
	sandTiling = defaults.sandTiling;     snowTiling = defaults.snowTiling;
	rockDarkness = defaults.rockDarkness;
	normalStrength = defaults.normalStrength; flipGreen = defaults.flipGreen;
	heightBlend = defaults.heightBlend;   aoStrength = defaults.aoStrength;
	roughnessScale = defaults.roughnessScale;
	groundTint = defaults.groundTint;     rockTint = defaults.rockTint;
	slopeStart = defaults.slopeStart;     slopeFade = defaults.slopeFade;
	shoreHeight = defaults.shoreHeight;   shoreWidth = defaults.shoreWidth;
	snowHeight = defaults.snowHeight;     snowFade = defaults.snowFade;
	breakupScale = defaults.breakupScale; breakupAmount = defaults.breakupAmount;
	shoreColor = defaults.shoreColor;     snowColor = defaults.snowColor;
}

bool TerrainSettings::SameShape(const TerrainSettings& o) const
{
	return width == o.width && heightScale == o.heightScale && maxCells == o.maxCells;
}

bool TerrainSettings::SameSplatRules(const TerrainSettings& o) const
{
	return slopeStart == o.slopeStart && slopeFade == o.slopeFade && shoreHeight == o.shoreHeight && shoreWidth == o.shoreWidth &&
		snowHeight == o.snowHeight && snowFade == o.snowFade && breakupScale == o.breakupScale && breakupAmount == o.breakupAmount;
}

std::string Terrain::DefaultLayerMapPath(int layer, TerrainLayerMap map)
{
	static const char* const kFileNames[] = { "albedo.png", "normal.png", "orm.png" };
	if (layer < 0 || layer >= kTerrainLayerCount) return {};
	return std::string("assets/Terrain/") + kTerrainLayerNames[layer] + "/" + kFileNames[static_cast<int>(map)];
}

TerrainLayerSet Terrain::GetLayerSet() const
{
	TerrainLayerSet set;
	set.diffHeight = layerDiffHeight_;
	set.normRough = layerNormRough_;
	for (int kind = 0; kind < 3; ++kind) set.fileCounts[kind] = layerFileCounts_[kind];
	for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
		for (int map = 0; map < 3; ++map) set.mapPaths[layer][map] = layerMapPaths_[layer][map];
	}
	return set;
}

Terrain::Terrain(ID3D11Device* device, const TerrainTile& tile, const TerrainSettings& material, const TerrainLayerSet* shareLayers)
	: device_(device), tile_(tile)
{
	for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
		for (int map = 0; map < 3; ++map) layerMapPaths_[layer][map] = DefaultLayerMapPath(layer, static_cast<TerrainLayerMap>(map));
	}
	if (shareLayers && shareLayers->IsValid()) {
		// One set of layer textures for the whole world: the same GPU arrays, not a copy.
		layerDiffHeight_ = shareLayers->diffHeight;
		layerNormRough_ = shareLayers->normRough;
		for (int kind = 0; kind < 3; ++kind) layerFileCounts_[kind] = shareLayers->fileCounts[kind];
		for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
			for (int map = 0; map < 3; ++map) layerMapPaths_[layer][map] = shareLayers->mapPaths[layer][map];
		}
	} else {
		RebuildLayerTextures();
	}
	// The tile sits where the world puts it: its centre at the tile's centre, its ground starting at the world's base height (default 0).
	// Everything a tile does with world Y (bounds, the layer rules, picking, scatter) already goes through position_.y, so it follows.
	double centerX = 0.0, centerZ = 0.0;
	TileCenter(tile_.grid, tile_.coord, centerX, centerZ);
	position_ = { static_cast<float>(centerX), tile_.baseHeight, static_cast<float>(centerZ) };
	settings_ = material;
	settings_.Clamp();
	ForceTileSettings(settings_);
	if (!Rebuild(settings_)) LOG_ERROR("Terrain tile mesh initialization failed");
	UpdateWorldTransform();
	if (!IsValid()) LOG_ERROR("Terrain tile initialization failed");
}

namespace {
Terrain::BuildTimes g_buildTimes;
using Clock = std::chrono::steady_clock;
double MillisecondsSince(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
}

const Terrain::BuildTimes& Terrain::GetBuildTimes() { return g_buildTimes; }

TileHeights Terrain::BuildTileHeights() const
{
	return GenerateTileHeights(*tile_.source, tile_.grid, tile_.coord, tile_.rivers.get());
}

void Terrain::ForceTileSettings(TerrainSettings& settings) const
{
	if (!IsTile()) return;
	settings.width = static_cast<float>(tile_.grid.TileSize());   // the tile decides its own size and resolution ...
	settings.maxCells = tile_.grid.TileCells();
}

Terrain::~Terrain()
{
	if (wireframeState_) wireframeState_->Release();
	ReleaseSplatTexture();
}

void Terrain::ReleaseSplatTexture()
{
	if (splatView_) { splatView_->Release(); splatView_ = nullptr; }
	if (splatTexture_) { splatTexture_->Release(); splatTexture_ = nullptr; }
}

bool Terrain::IsValid() const
{
	return renderable_ && renderable_->IsValid() &&
		layerDiffHeight_ && layerDiffHeight_->IsValid() && layerNormRough_ && layerNormRough_->IsValid() && splatView_;
}

void Terrain::GetLayerFileCounts(int& albedo, int& normal, int& orm) const
{
	albedo = layerFileCounts_[0];
	normal = layerFileCounts_[1];
	orm = layerFileCounts_[2];
}

// Reads the twelve files (4 layers x albedo / normal / ORM) and PACKS them into the channels of two texture arrays:
//     array 1 (sRGB):   r g b = albedo,                 a = height   (the normal map's alpha)
//     array 2 (linear): r g   = normal XY (z is rebuilt in the shader), b = ambient occlusion, a = roughness
// Shipped engines pack like this: a layer then costs TWO texture reads instead of three (and the unused metallic channel
// is dropped), and the height that blending needs rides along with the colour. The files themselves are unchanged.
// The fill for a missing file is the neutral value of that channel: white colour, middling height, a flat normal,
// no occlusion, medium roughness. The old arrays stay if the new ones cannot be built.
bool Terrain::RebuildLayerTextures()
{
	struct Image { std::vector<std::uint8_t> pixels; int width = 0, height = 0; };
	Image images[kTerrainLayerCount][3];
	int counts[3] = {};
	int width = 0, height = 0;   // the first file that loads decides the size of everything
	for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
		for (int kind = 0; kind < 3; ++kind) {
			Image& image = images[layer][kind];
			if (!TextureArray::LoadImageRgba8(layerMapPaths_[layer][kind], image.width, image.height, image.pixels)) {
				LOG_INFO("Layer map not found, using the neutral default: " + layerMapPaths_[layer][kind]);
				continue;
			}
			++counts[kind];
			if (width == 0) { width = image.width; height = image.height; }
		}
	}
	if (width == 0) { width = 4; height = 4; }   // nothing loaded: a tiny all-default array

	const size_t texels = static_cast<size_t>(width) * height;
	std::vector<std::vector<std::uint8_t>> diffHeight(kTerrainLayerCount), normRough(kTerrainLayerCount);
	for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
		for (int kind = 0; kind < 3; ++kind) {
			Image& image = images[layer][kind];
			if (image.pixels.empty() || (image.width == width && image.height == height)) continue;
			image.pixels = TextureArray::ResizeRgba8(image.pixels, image.width, image.height, width, height);   // odd-sized file: scale it
			image.width = width;
			image.height = height;
		}
		const std::vector<std::uint8_t>& albedo = images[layer][static_cast<int>(TerrainLayerMap::Albedo)].pixels;
		const std::vector<std::uint8_t>& normal = images[layer][static_cast<int>(TerrainLayerMap::Normal)].pixels;
		const std::vector<std::uint8_t>& orm = images[layer][static_cast<int>(TerrainLayerMap::Orm)].pixels;
		diffHeight[layer].resize(texels * 4);
		normRough[layer].resize(texels * 4);
		for (size_t i = 0; i < texels; ++i) {
			std::uint8_t* d = &diffHeight[layer][i * 4];
			std::uint8_t* n = &normRough[layer][i * 4];
			d[0] = albedo.empty() ? 255 : albedo[i * 4 + 0];
			d[1] = albedo.empty() ? 255 : albedo[i * 4 + 1];
			d[2] = albedo.empty() ? 255 : albedo[i * 4 + 2];
			d[3] = normal.empty() ? 128 : normal[i * 4 + 3];     // height lives in the normal map's alpha
			n[0] = normal.empty() ? 128 : normal[i * 4 + 0];
			n[1] = normal.empty() ? 128 : normal[i * 4 + 1];
			n[2] = orm.empty() ? 255 : orm[i * 4 + 0];           // ambient occlusion
			n[3] = orm.empty() ? 128 : orm[i * 4 + 1];           // roughness (the ORM's blue, metallic, is not used for ground)
		}
	}

	auto first = std::make_unique<TextureArray>(device_, width, height, ColorSpace::SRGB, diffHeight, counts[0]);
	auto second = std::make_unique<TextureArray>(device_, width, height, ColorSpace::Linear, normRough, counts[1]);
	if (!first->IsValid() || !second->IsValid()) return false;
	layerDiffHeight_ = std::move(first);
	layerNormRough_ = std::move(second);
	for (int kind = 0; kind < 3; ++kind) layerFileCounts_[kind] = counts[kind];
	return true;
}

bool Terrain::SetLayerMap(int layer, TerrainLayerMap map, const std::string& path)
{
	if (layer < 0 || layer >= kTerrainLayerCount) return false;
	const std::string wanted = path.empty() ? DefaultLayerMapPath(layer, map) : path;
	std::error_code error;
	if (!path.empty() && !std::filesystem::exists(wanted, error)) return false;

	std::string& slot = layerMapPaths_[layer][static_cast<int>(map)];
	const std::string previous = slot;
	slot = wanted;
	if (!RebuildLayerTextures()) {   // the files could not be packed together: undo
		slot = previous;
		return false;
	}
	return true;
}

bool Terrain::ApplySettings(TerrainSettings settings)
{
	settings.Clamp();
	ForceTileSettings(settings);   // a tile keeps the size and resolution the world gave it
	if (renderable_ && settings.SameShape(settings_)) {   // only the material changed
		const bool rulesChanged = !settings.SameSplatRules(settings_);
		settings_ = settings;
		UpdateMaterial();
		if (rulesChanged) RefreshSplatAll();   // the rules are baked into the splat map: a few ms, instead of per pixel every frame
		return true;
	}
	return Rebuild(settings);
}

bool Terrain::Rebuild(const TerrainSettings& settings)
{
	if (!device_ || !IsTile()) return false;

	GridShape shape;   // the tile decides its own grid
	shape.cellsX = shape.cellsZ = tile_.grid.TileCells();
	shape.depth = static_cast<float>(tile_.grid.TileSize());
	const auto heightsStart = Clock::now();
	TileHeights tileHeights = BuildTileHeights();   // a tile's samples and apron are made together (the rivers are dug into both)
	const double heightsMs = MillisecondsSince(heightsStart);
	std::vector<float> samples = std::move(tileHeights.samples);
	Heightfield field;
	if (!field.Reset({ shape.cellsX, shape.cellsZ, settings.width, shape.depth, settings.heightScale }, std::move(samples))) return false;
	field.SetApron(std::move(tileHeights.apron));   // normals that match the neighbours' (lesson 47)

	const float depth = shape.depth;
	std::vector<TerrainChunk> chunks;
	int chunksX = 0, chunksZ = 0;
	const auto meshStart = Clock::now();
	Model* model = BuildTerrainModel(device_, field, chunks, chunksX, chunksZ, Walls());
	const double meshMs = MillisecondsSince(meshStart);
	if (!model) return false;
	auto replacement = std::make_unique<Renderable>(device_, model, L"shaders/vs.hlsl", L"shaders/terrain_ps.hlsl");
	if (!replacement->IsValid()) return false;

	// The layer weights for the new grid (lesson 29), baked now so the very first frame has them. Made before anything is
	// committed, like the mesh, so a failure leaves the old terrain on screen.
	const auto splatStart = Clock::now();
	SplatMap splat;
	splat.Resize(shape.cellsX, shape.cellsZ);
	BakeSplatAll(field, MakeSplatRules(settings), CurrentSplatPlacement(), splat);
	// The painted layers (lesson 30) go over the baked ones. They are kept while the grid is the very same one they were
	// painted on (a noise or rule change keeps it); a different grid has different texels, so the paint is dropped.
	const bool keepPaint = !paint_.IsEmpty() && paint_.width == shape.cellsX && paint_.height == shape.cellsZ;
	if (keepPaint) ApplyPaintAll(paint_, splat);
	ID3D11Texture2D* splatTexture = nullptr;
	ID3D11ShaderResourceView* splatView = nullptr;
	if (!CreateSplatTexture(device_, splat, splatTexture, splatView)) return false;
	const double splatMs = MillisecondsSince(splatStart);
	{
		g_buildTimes.heightsMs = heightsMs; g_buildTimes.meshMs = meshMs; g_buildTimes.splatMs = splatMs;
		++g_buildTimes.builds;
	}

	ReleaseSplatTexture();
	splat_ = std::move(splat);
	splatTexture_ = splatTexture;
	splatView_ = splatView;
	renderable_ = std::move(replacement);
	heightfield_ = std::move(field);
	if (!keepPaint) {
		paint_.Resize(shape.cellsX, shape.cellsZ);
		painted_ = false;
	}
	settings_ = settings;
	depth_ = depth;
	chunks_ = std::move(chunks);
	chunksX_ = chunksX;
	chunksZ_ = chunksZ;
	chunkLod_.assign(chunks_.size(), Model::kSkipMesh);   // "no level chosen yet": the first frame picks without hysteresis
	UpdateMaterial();
	UpdateWorldTransform();
	return true;
}

bool Terrain::GetBoundingSphere(XMFLOAT3& center, float& radius) const
{
	const XMFLOAT2 extent = GetWorldExtent();
	const float height = settings_.heightScale * scale_.y;
	center = { position_.x, position_.y + height * 0.5f, position_.z };
	radius = 0.5f * std::sqrt(extent.x * extent.x + extent.y * extent.y + height * height);
	return true;
}

void Terrain::RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader, const RenderView& view) const
{
	if (renderable_ && renderable_->IsValid()) DrawChunks(ctx, &shader, view);
}

// World-space box that contains a local-space box after the terrain's transform (which may
// scale, rotate and move it). Transforms the centre, and spreads the half-size through |matrix|.
static void TransformBounds(const XMMATRIX& world, const XMFLOAT3& localMin, const XMFLOAT3& localMax,
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
static float DistanceToBox(const XMFLOAT3& p, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax)
{
	const float dx = (std::max)((std::max)(boxMin.x - p.x, 0.0f), p.x - boxMax.x);
	const float dy = (std::max)((std::max)(boxMin.y - p.y, 0.0f), p.y - boxMax.y);
	const float dz = (std::max)((std::max)(boxMin.z - p.z, 0.0f), p.z - boxMax.z);
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void Terrain::DrawChunks(ID3D11DeviceContext* ctx, const Shader* shaderOverride, const RenderView& view) const
{
	const XMMATRIX world = XMLoadFloat4x4(&renderable_->GetModel()->GetWorld());
	TerrainViewStats& stats = stats_[static_cast<int>(view.kind)];
	stats = {};
	stats.chunksTotal = static_cast<int>(chunks_.size());

	// Hysteresis: a chunk that sits exactly on a LOD boundary would flip back and forth every time
	// the camera jitters. So to CHANGE level it must pass the threshold by a margin (10%).
	const float thresholds[kTerrainLodCount - 1] = { settings_.lodDistance0, settings_.lodDistance1 };
	const float margin = 0.10f;

	drawList_.assign(chunks_.size(), Model::kSkipMesh);
	for (size_t i = 0; i < chunks_.size(); ++i) {
		XMFLOAT3 worldMin, worldMax;
		TransformBounds(world, chunks_[i].boundsMin, chunks_[i].boundsMax, worldMin, worldMax);

		// 1. Can this view see the tile at all?
		if (settings_.frustumCulling && !view.frustum.IntersectsAABB(worldMin, worldMax)) continue;
		// 1b. The mirror view clips everything below the water: a chunk wholly under it would be drawn, then discarded pixel by pixel.
		if (view.clipBelow && worldMax.y < view.clipBelowY) continue;

		// 2. How much detail does it deserve? Distance is measured from the REAL camera even for the
		//    mirror and sun views, so all three draw the same level and the shapes agree.
		int lod = 0;
		if (settings_.lodEnabled) {
			const float distance = DistanceToBox(view.lodOrigin, worldMin, worldMax);
			if (chunkLod_[i] == Model::kSkipMesh) {
				while (lod < kTerrainLodCount - 1 && distance > thresholds[lod]) ++lod;           // first time: plain thresholds
			} else {
				lod = chunkLod_[i];
				while (lod > 0 && distance < thresholds[lod - 1] * (1.0f - margin)) --lod;       // closer: finer, once clearly past
				while (lod < kTerrainLodCount - 1 && distance > thresholds[lod] * (1.0f + margin)) ++lod;   // farther: coarser
			}
			chunkLod_[i] = static_cast<unsigned char>(lod);
		}

		drawList_[i] = static_cast<unsigned char>(lod);
		++stats.chunksDrawn;
		++stats.chunksPerLod[lod];
		stats.trianglesDrawn += chunks_[i].triangleCount[lod];
	}

	// 3. Skirts. Now that EVERY tile's level is known, decide per tile which of its four edges needs a skirt:
	//    only an edge facing a neighbour at a different level (a crack could open there) or the edge of the
	//    whole terrain. Interior edges between equal tiles get none, which is most of them.
	partMasks_.assign(chunks_.size(), 0);
	const bool gridMatches = static_cast<size_t>(chunksX_) * chunksZ_ == chunks_.size();
	for (size_t i = 0; i < chunks_.size(); ++i) {
		const unsigned char lod = drawList_[i];
		if (lod == Model::kSkipMesh) continue;
		const unsigned char mask = gridMatches
			? ComputeSkirtMask(static_cast<int>(i % chunksX_), static_cast<int>(i / chunksX_), chunksX_, chunksZ_, drawList_)
			: static_cast<unsigned char>(15);   // (cannot happen) fall back to every skirt rather than risk a crack
		partMasks_[i] = static_cast<unsigned char>(mask | (tile_.capBottom ? kTerrainCapPartBit : 0));   // the bottom of a bounded world (X1)
		for (int edge = 0; edge < 4; ++edge) {
			if (mask & (1 << edge)) stats.trianglesDrawn += chunks_[i].skirtTriangles[lod][edge];
		}
	}

	// 4. Draw. In wireframe mode the main and mirror views show the mesh as wires; the sun view
	//    keeps the shadow pass's own rasterizer state.
	const bool wire = settings_.wireframe && view.kind != ViewKind::Shadow;
	if (wire) {
		if (!wireframeState_) {
			D3D11_RASTERIZER_DESC desc{};
			desc.FillMode = D3D11_FILL_WIREFRAME;
			desc.CullMode = D3D11_CULL_NONE;
			desc.DepthClipEnable = TRUE;
			device_->CreateRasterizerState(&desc, &wireframeState_);
		}
		ctx->RSSetState(wireframeState_);
	}
	if (shaderOverride) renderable_->RenderWith(ctx, *shaderOverride, &drawList_, &partMasks_);
	else renderable_->Render(ctx, &drawList_, &partMasks_);
	if (wire) ctx->RSSetState(nullptr);
}

void Terrain::UpdateMaterial()
{
	if (!renderable_) return;
	renderable_->SetMaterialParameters({ settings_.uvTiling, 1.0f, 1.0f, 1.0f });   // vs.hlsl: UV tiling

	const XMFLOAT3 ground = SrgbToLinear(settings_.groundTint);   // picked as sRGB, shader wants linear
	const XMFLOAT3 rock = SrgbToLinear(settings_.rockTint);
	const XMFLOAT3 sand = SrgbToLinear(settings_.shoreColor);
	const XMFLOAT3 snow = SrgbToLinear(settings_.snowColor);
	TerrainMaterialCB block;
	block.layers = { settings_.rockTiling, settings_.rockDarkness, 0.0f, 0.0f };
	block.blend = { settings_.heightBlend, kHeightTransition, settings_.showLayers ? 1.0f : 0.0f, 0.0f };
	// World XZ -> splat UV: the mesh spans `extent` world units centred on the terrain's position (it is never rotated).
	const XMFLOAT2 extent = GetWorldExtent();
	const float invX = std::fabs(extent.x) > 1e-6f ? 1.0f / extent.x : 0.0f;
	const float invZ = std::fabs(extent.y) > 1e-6f ? 1.0f / extent.y : 0.0f;
	block.splat = { position_.x - 0.5f * extent.x, position_.z - 0.5f * extent.y, invX, invZ };
	block.tilings = { settings_.groundTiling, settings_.sandTiling, settings_.snowTiling, 0.0f };
	block.surface = { settings_.normalStrength, settings_.flipGreen ? 1.0f : 0.0f, settings_.aoStrength, settings_.roughnessScale };
	block.groundTint = { ground.x, ground.y, ground.z, 1.0f };
	block.rockTint = { rock.x, rock.y, rock.z, 1.0f };
	block.shoreTint = { sand.x, sand.y, sand.z, 1.0f };
	block.snowTint = { snow.x, snow.y, snow.z, 1.0f };
	renderable_->SetMaterialBlock(&block, sizeof(block));
}

// The terrain is only ever moved and scaled (SetEditorTransform drops any rotation), so going from world space to the
// heightfield's local space is "subtract the position, divide by the scale" and back is the reverse. Rotation would
// turn a straight-down query into a slanted line through the field, which is why it is not allowed.
bool Terrain::GetHeightAt(float worldX, float worldZ, float& worldY) const
{
	if (heightfield_.IsEmpty() || std::fabs(scale_.x) < 1e-6f || std::fabs(scale_.z) < 1e-6f) return false;
	float localY = 0.0f;
	if (!heightfield_.HeightAt((worldX - position_.x) / scale_.x, (worldZ - position_.z) / scale_.z, localY)) return false;
	worldY = position_.y + localY * scale_.y;
	return true;
}

bool Terrain::GetNormalAt(float worldX, float worldZ, XMFLOAT3& normal) const
{
	if (heightfield_.IsEmpty() || std::fabs(scale_.x) < 1e-6f || std::fabs(scale_.y) < 1e-6f || std::fabs(scale_.z) < 1e-6f) return false;
	HfVec3 local;
	if (!heightfield_.NormalAt((worldX - position_.x) / scale_.x, (worldZ - position_.z) / scale_.z, local)) return false;
	// A normal is not a direction along the surface, so a stretched terrain divides it by the scale (not multiplies).
	const XMVECTOR scaled = XMVectorSet(local.x / scale_.x, local.y / scale_.y, local.z / scale_.z, 0.0f);
	XMStoreFloat3(&normal, XMVector3Normalize(scaled));
	return true;
}

bool Terrain::Raycast(const Ray& ray, float& distance, XMFLOAT3* point) const
{
	if (heightfield_.IsEmpty() || std::fabs(scale_.x) < 1e-6f || std::fabs(scale_.y) < 1e-6f || std::fabs(scale_.z) < 1e-6f) return false;
	// Carry the ray into the heightfield's space. The direction is NOT renormalised: the point at distance t of the
	// local ray is the local version of the point at distance t of the world ray, so t needs no conversion.
	const HfVec3 origin{ (ray.origin.x - position_.x) / scale_.x, (ray.origin.y - position_.y) / scale_.y, (ray.origin.z - position_.z) / scale_.z };
	const HfVec3 direction{ ray.direction.x / scale_.x, ray.direction.y / scale_.y, ray.direction.z / scale_.z };
	float t = 0.0f;
	if (!heightfield_.Raycast(origin, direction, t)) return false;
	distance = t;
	if (point) *point = { ray.origin.x + ray.direction.x * t, ray.origin.y + ray.direction.y * t, ray.origin.z + ray.direction.z * t };
	return true;
}

// ---- Sculpting (lesson 27) ----------------------------------------------------------------------------------------

bool Terrain::PatchTile(const TileSampleRect& rect, bool groundIsFinal)
{
	if (!IsTile() || tile_.lod != 0 || rect.IsEmpty() || heightfield_.IsEmpty() || !renderable_) return false;
	const int cells = heightfield_.GetLayout().cellsX;
	if (heightfield_.GetLayout().cellsZ != cells || cells != tile_.grid.TileCells()) return false;
	const auto start = Clock::now();

	// The ground the world gives at exactly these samples (edits and rivers included): the same numbers a rebuild would read.
	const std::vector<float> block = GenerateTileBlock(*tile_.source, tile_.grid, tile_.coord, tile_.rivers.get(), rect);
	if (block.size() != static_cast<size_t>(rect.Width()) * rect.Height()) return false;

	HeightfieldApron apron = heightfield_.Apron();
	const bool hasApron = heightfield_.HasApron();
	GridRect changed;   // the samples of the tile itself whose vertices must be rebuilt
	for (int z = rect.z0; z <= rect.z1; ++z) {
		for (int x = rect.x0; x <= rect.x1; ++x) {
			const float value = block[static_cast<size_t>(z - rect.z0) * rect.Width() + (x - rect.x0)];
			const bool insideX = x >= 0 && x <= cells, insideZ = z >= 0 && z <= cells;
			if (insideX && insideZ) {
				heightfield_.SetSample(heightfield_.IndexOf(x, z), value);
				changed.Include(x, z);
			} else if (!hasApron) {
				continue;
			} else if (x == -1 && insideZ) {         // a neighbour's change: only this edge vertex's normal moves
				apron.west[static_cast<size_t>(z)] = value;
				changed.Include(0, z);
			} else if (x == cells + 1 && insideZ) {
				apron.east[static_cast<size_t>(z)] = value;
				changed.Include(cells, z);
			} else if (z == -1 && insideX) {
				apron.north[static_cast<size_t>(x)] = value;
				changed.Include(x, 0);
			} else if (z == cells + 1 && insideX) {
				apron.south[static_cast<size_t>(x)] = value;
				changed.Include(x, cells);
			}   // the four corners of the apron are not stored: no normal reads them
		}
	}
	if (hasApron) heightfield_.SetApron(std::move(apron));
	if (changed.IsEmpty()) return true;

	RefreshChunks(changed);
	RefreshSplat(SplatCellsForSamples(changed, cells, cells));   // new slopes: new layer weights (painted layers go over them again)
	if (groundIsFinal) NewRevision();
	g_buildTimes.patchMs = MillisecondsSince(start);
	++g_buildTimes.patches;
	return true;
}

TerrainWalls Terrain::Walls() const
{
	TerrainWalls walls;
	if (IsTile() && (tile_.outerEdges != 0 || tile_.capBottom)) {
		walls.edges = tile_.outerEdges;
		walls.cap = tile_.capBottom;
		walls.floorY = WallFloorFor(heightfield_.GetLayout().heightScale);
	}
	return walls;
}

void Terrain::RefreshChunks(const GridRect& changed)
{
	if (changed.IsEmpty() || !renderable_ || heightfield_.IsEmpty() || !device_) return;
	Model* model = renderable_->GetModel();
	if (!model) return;
	const HeightfieldLayout& layout = heightfield_.GetLayout();

	// Which chunks hold the edited samples, plus one sample around them for the normals (see TerrainChunks.h).
	const ChunkRange range = ChunksToRefresh(changed, layout.cellsX, layout.cellsZ);
	if (range.IsEmpty()) return;
	const int firstX = range.firstX, lastX = (std::min)(range.lastX, chunksX_ - 1);
	const int firstZ = range.firstZ, lastZ = (std::min)(range.lastZ, chunksZ_ - 1);

	ID3D11DeviceContext* ctx = nullptr;
	device_->GetImmediateContext(&ctx);
	if (!ctx) return;
	std::vector<Vertex> vertices;   // one scratch buffer for every chunk
	for (int chunkZ = firstZ; chunkZ <= lastZ; ++chunkZ) {
		for (int chunkX = firstX; chunkX <= lastX; ++chunkX) {
			const size_t index = static_cast<size_t>(chunkZ) * chunksX_ + chunkX;
			Mesh* mesh = model->GetMesh(index);
			if (!mesh || index >= chunks_.size()) continue;
			const int x0 = chunkX * kChunkCells, x1 = (std::min)(x0 + kChunkCells, layout.cellsX);
			const int z0 = chunkZ * kChunkCells, z1 = (std::min)(z0 + kChunkCells, layout.cellsZ);
			XMFLOAT3 boundsMin, boundsMax;
			BuildChunkVertices(heightfield_, x0, z0, x1, z1, vertices, boundsMin, boundsMax, Walls());
			if (!mesh->UpdateVertices(ctx, vertices)) continue;
			chunks_[index].boundsMin = boundsMin;   // the box culling uses must follow the new shape
			chunks_[index].boundsMax = boundsMax;
		}
	}
	ctx->Release();
}

SplatRules Terrain::CurrentSplatRules() const
{
	return MakeSplatRules(settings_);
}

SplatPlacement Terrain::CurrentSplatPlacement() const
{
	SplatPlacement placement;
	placement.originY = position_.y;
	placement.scaleX = scale_.x;
	placement.scaleY = scale_.y;
	placement.scaleZ = scale_.z;
	if (IsTile()) { placement.noiseOffsetX = position_.x; placement.noiseOffsetZ = position_.z; }   // world noise, the same in every tile
	return placement;
}

// ---- Painting (lesson 30) ----------------------------------------------------------------------------------------------

bool Terrain::ReplacePaint(std::vector<float> weights, bool painted)
{
	if (paint_.IsEmpty() || weights.size() != paint_.weights.size()) return false;
	paint_.weights = std::move(weights);
	painted_ = painted;
	RefreshSplatAll();
	return true;
}

void Terrain::RefreshSplat(const GridRect& cells)
{
	if (cells.IsEmpty() || splat_.IsEmpty() || !splatTexture_ || heightfield_.IsEmpty() || !device_) return;
	if (std::fabs(scale_.x) < 1e-6f || std::fabs(scale_.y) < 1e-6f || std::fabs(scale_.z) < 1e-6f) return;   // a collapsed terrain has no slope to measure
	BakeSplat(heightfield_, CurrentSplatRules(), CurrentSplatPlacement(), cells, splat_);
	ApplyPaint(paint_, cells, splat_);   // the baking just overwrote those texels with the rules: lay the paint back over them (lesson 30)
	{
		// Scatter reads these weights (and the heights that produced them): tell it which patch of ground changed.
		const HeightfieldLayout& layout = heightfield_.GetLayout();
		const float cellX = layout.sizeX / static_cast<float>(layout.cellsX), cellZ = layout.sizeZ / static_cast<float>(layout.cellsZ);
		const float lx0 = -0.5f * layout.sizeX + static_cast<float>(cells.x0) * cellX, lx1 = -0.5f * layout.sizeX + static_cast<float>(cells.x1 + 1) * cellX;
		const float lz0 = -0.5f * layout.sizeZ + static_cast<float>(cells.z0) * cellZ, lz1 = -0.5f * layout.sizeZ + static_cast<float>(cells.z1 + 1) * cellZ;
		const float wx0 = position_.x + lx0 * scale_.x, wx1 = position_.x + lx1 * scale_.x;
		const float wz0 = position_.z + lz0 * scale_.z, wz1 = position_.z + lz1 * scale_.z;
		LogSurfaceChange((std::min)(wx0, wx1), (std::min)(wz0, wz1), (std::max)(wx0, wx1), (std::max)(wz0, wz1));
	}

	// Upload only the block that was re-baked (the same clamping BakeSplat did).
	const int x0 = (std::max)(cells.x0, 0), z0 = (std::max)(cells.z0, 0);
	const int x1 = (std::min)(cells.x1, splat_.width - 1), z1 = (std::min)(cells.z1, splat_.height - 1);
	if (x1 < x0 || z1 < z0) return;
	ID3D11DeviceContext* ctx = nullptr;
	device_->GetImmediateContext(&ctx);
	if (!ctx) return;
	D3D11_BOX box{};
	box.left = static_cast<UINT>(x0);
	box.top = static_cast<UINT>(z0);
	box.front = 0;
	box.right = static_cast<UINT>(x1 + 1);
	box.bottom = static_cast<UINT>(z1 + 1);
	box.back = 1;
	// The source pointer is the first texel OF THE BLOCK; the pitch is the whole map's row length.
	ctx->UpdateSubresource(splatTexture_, 0, &box, &splat_.rgba[(static_cast<size_t>(z0) * splat_.width + x0) * kSplatChannels],
						   static_cast<UINT>(splat_.width * kSplatChannels), 0);
	ctx->Release();
}

void Terrain::RefreshSplatAll()
{
	if (splat_.IsEmpty()) return;
	GridRect everything;
	everything.Include(0, 0);
	everything.Include(splat_.width - 1, splat_.height - 1);
	RefreshSplat(everything);
}

XMFLOAT2 Terrain::GetWorldExtent() const
{
	return { settings_.width * scale_.x, depth_ * scale_.z };
}

void Terrain::GetHeightRange(float& minY, float& maxY) const
{
	minY = position_.y;
	maxY = position_.y + settings_.heightScale * scale_.y;   // ignores rotation
}

bool Terrain::GetEditorTransform(XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale) const
{
	position = position_;
	rotation = rotation_;
	scale = scale_;
	return true;
}

bool Terrain::SetEditorTransform(const XMFLOAT3& position, const XMFLOAT3& rotation,
	const XMFLOAT3& scale)
{
	(void)rotation;   // the terrain is never tilted (SupportsRotation() is false): its height queries rely on it
	if (IsTile()) return false;   // the world decides where a tile is
	// The layer rules talk in WORLD height and slope, so lifting or stretching the terrain changes the weights. Moving it
	// sideways does not (the noise is read in the terrain's own space).
	const bool weightsMoved = position.y != position_.y || scale.x != scale_.x || scale.y != scale_.y || scale.z != scale_.z;
	position_ = position;
	rotation_ = { 0.0f, 0.0f, 0.0f };
	scale_ = scale;
	UpdateWorldTransform();
	UpdateMaterial();   // the splat map's place in the world moved
	if (weightsMoved) RefreshSplatAll();
	return true;
}

void Terrain::LogSurfaceChange(float minX, float minZ, float maxX, float maxZ)
{
	static unsigned nextSurfaceRevision = 0;   // one counter for every terrain, like NewRevision
	SurfaceChange change;
	change.revision = ++nextSurfaceRevision;
	change.minX = minX; change.minZ = minZ; change.maxX = maxX; change.maxZ = maxZ;
	surfaceRevision_ = change.revision;
	surfaceLog_.push_back(change);
	constexpr size_t kLogLimit = 128;
	if (surfaceLog_.size() > kLogLimit) {
		surfaceLogFloor_ = surfaceLog_[surfaceLog_.size() - kLogLimit - 1].revision;
		surfaceLog_.erase(surfaceLog_.begin(), surfaceLog_.end() - kLogLimit);
	}
}

void Terrain::LogSurfaceChangeEverywhere()
{
	constexpr float kHuge = 1.0e30f;
	LogSurfaceChange(-kHuge, -kHuge, kHuge, kHuge);
}

bool Terrain::GetSurfaceChangesSince(unsigned sinceRevision, std::vector<SurfaceChange>& changes) const
{
	if (sinceRevision == surfaceRevision_) return true;   // nothing new
	// `sinceRevision` must be a revision this terrain really issued and still remembers: either one of the logged changes,
	// or the last one that was forgotten (the log then starts right after it). Anything else (0, an older one, one from
	// another terrain) cannot be continued from.
	bool anchored = sinceRevision == surfaceLogFloor_;
	const size_t first = changes.size();
	for (const SurfaceChange& change : surfaceLog_) {
		if (change.revision == sinceRevision) anchored = true;
		else if (change.revision > sinceRevision) changes.push_back(change);
	}
	if (!anchored) changes.resize(first);
	return anchored;
}

void Terrain::NewRevision()
{
	static unsigned nextRevision = 0;   // one counter for every terrain: a revision is never reused
	revision_ = ++nextRevision;
}

void Terrain::UpdateWorldTransform()
{
	NewRevision();
	LogSurfaceChangeEverywhere();   // moved or stretched: everything standing on it must be placed again
	const XMMATRIX transform = XMMatrixScaling(scale_.x, scale_.y, scale_.z) *
		XMMatrixRotationX(rotation_.x) * XMMatrixRotationY(rotation_.y) *
		XMMatrixRotationZ(rotation_.z) *
		XMMatrixTranslation(position_.x, position_.y, position_.z);
	XMFLOAT4X4 world;
	XMStoreFloat4x4(&world, transform);
	if (renderable_) renderable_->SetWorld(world);
}

void Terrain::Update(float /*dt*/)
{
}

void Terrain::Render(ID3D11DeviceContext* ctx, const RenderView& view) const
{
	if (!IsValid()) return;
	ID3D11ShaderResourceView* diffHeight = layerDiffHeight_->GetShaderResourceView();
	ID3D11ShaderResourceView* normRough = layerNormRough_->GetShaderResourceView();
	ctx->PSSetShaderResources(Slots::kSrvTerrainAlbedo, 1, &diffHeight);
	ctx->PSSetShaderResources(Slots::kSrvTerrainNormal, 1, &normRough);
	ctx->PSSetShaderResources(Slots::kSrvTerrainSplat, 1, &splatView_);
	DrawChunks(ctx, nullptr, view);
}
