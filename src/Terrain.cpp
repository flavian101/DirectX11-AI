#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Terrain.h"
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
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
	XMFLOAT4 layers;       // rock tiling, breakup scale, breakup amount, rock brightness
	XMFLOAT4 heightBands;  // shore height, shore width, snow height, snow fade
	XMFLOAT4 slope;        // slope start, slope fade, debug flag, height blend
	XMFLOAT4 tilings;      // ground tiling, sand tiling, snow tiling, macro colour influence
	XMFLOAT4 surface;      // normal strength, flip green (1/0), AO strength, roughness scale
	XMFLOAT4 groundTint;   // linear rgb
	XMFLOAT4 rockTint;
	XMFLOAT4 shoreTint;
	XMFLOAT4 snowTint;
};
static_assert(sizeof(TerrainMaterialCB) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

float SmoothStep(float value)
{
	return value * value * (3.0f - 2.0f * value);
}

float HashValue(int x, int z, int seed)
{
	uint32_t value = static_cast<uint32_t>(x) * 374761393u;
	value += static_cast<uint32_t>(z) * 668265263u;
	value += static_cast<uint32_t>(seed) * 1442695041u;
	value = (value ^ (value >> 13u)) * 1274126177u;
	value ^= value >> 16u;
	return static_cast<float>(value & 0x00ffffffu) / 8388607.5f - 1.0f;
}

float ValueNoise(float x, float z, int seed)
{
	const int x0 = static_cast<int>(std::floor(x));
	const int z0 = static_cast<int>(std::floor(z));
	const float tx = SmoothStep(x - static_cast<float>(x0));
	const float tz = SmoothStep(z - static_cast<float>(z0));
	const float a = HashValue(x0, z0, seed);
	const float b = HashValue(x0 + 1, z0, seed);
	const float c = HashValue(x0, z0 + 1, seed);
	const float d = HashValue(x0 + 1, z0 + 1, seed);
	const float ab = a + (b - a) * tx;
	const float cd = c + (d - c) * tx;
	return ab + (cd - ab) * tz;
}

float FractalNoise(float x, float z, int seed, int octaves)
{
	float value = 0.0f;
	float amplitude = 0.5f;
	float frequency = 1.0f;
	float amplitudeTotal = 0.0f;
	for (int octave = 0; octave < octaves; ++octave) {
		value += ValueNoise(x * frequency, z * frequency, seed + octave * 31) * amplitude;
		amplitudeTotal += amplitude;
		frequency *= 2.0f;
		amplitude *= 0.5f;
	}
	return value / amplitudeTotal;
}

// Decodes the heightmap once (red channel, 0..1). A 16-bit image has 65,536 height steps instead of 256, which is the
// difference between smooth slopes and visible terraces on a gentle hill.
bool LoadHeightmapFile(const char* path, int& width, int& height, std::vector<float>& values)
{
	int channels = 0;
	if (stbi_is_16_bit(path)) {
		stbi_us* image = stbi_load_16(path, &width, &height, &channels, STBI_rgb_alpha);
		if (!image || width < 2 || height < 2) {
			LOG_ERROR("Failed to load terrain heightmap: " + std::string(path));
			if (image) stbi_image_free(image);
			return false;
		}
		values.resize(static_cast<size_t>(width) * height);
		for (size_t i = 0; i < values.size(); ++i) values[i] = image[i * 4] / 65535.0f;
		stbi_image_free(image);
		return true;
	}
	unsigned char* image = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
	if (!image || width < 2 || height < 2) {
		LOG_ERROR("Failed to load terrain heightmap: " + std::string(path));
		if (image) stbi_image_free(image);
		return false;
	}
	values.resize(static_cast<size_t>(width) * height);
	for (size_t i = 0; i < values.size(); ++i) values[i] = image[i * 4] / 255.0f;
	stbi_image_free(image);
	return true;
}

// Bilinear lookup so the mesh can have ANY resolution, not just integer divisions of the image.
float SampleHeightmap(const std::vector<float>& values, int width, int height, float u, float v)
{
	const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width - 1);
	const float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height - 1);
	const int x0 = std::min(static_cast<int>(fx), width - 1);
	const int z0 = std::min(static_cast<int>(fz), height - 1);
	const int x1 = std::min(x0 + 1, width - 1);
	const int z1 = std::min(z0 + 1, height - 1);
	const float tx = fx - static_cast<float>(x0);
	const float tz = fz - static_cast<float>(z0);
	auto at = [&](int x, int z) { return values[static_cast<size_t>(z) * width + x]; };
	const float top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
	const float bottom = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
	return top + (bottom - top) * tz;
}

static_assert(kTerrainSkipChunk == Model::kSkipMesh, "the terrain and the model must agree on the \"not drawn\" marker");

constexpr int kChunkCells = kTerrainChunkCells;   // grid cells per tile side: 32x32 quads = 2048 triangles per tile

// How many cells the grid has for a heightmap of this pixel size, and how deep the terrain is (it follows the picture's shape).
struct GridShape {
	int cellsX = 0;
	int cellsZ = 0;
	float depth = 0.0f;   // world-space size along Z
};

GridShape ComputeGridShape(const TerrainSettings& settings, int mapWidth, int mapHeight)
{
	GridShape shape;
	shape.cellsX = settings.maxCells;
	shape.cellsZ = std::max(2, static_cast<int>(std::lround(
		static_cast<float>(shape.cellsX) * static_cast<float>(mapHeight) / static_cast<float>(mapWidth))));
	shape.depth = settings.width * static_cast<float>(mapHeight) / static_cast<float>(mapWidth);
	return shape;
}

// The generated heights: the picture resampled to the grid, plus noise. 0..1, row by row.
std::vector<float> GenerateHeights(const std::vector<float>& map, int mapWidth, int mapHeight,
								   const TerrainSettings& settings, const GridShape& shape)
{
	const int gridWidth = shape.cellsX + 1;
	const int gridHeight = shape.cellsZ + 1;
	std::vector<float> heights(static_cast<size_t>(gridWidth) * gridHeight);
	for (int z = 0; z < gridHeight; ++z) {
		const float v = static_cast<float>(z) / static_cast<float>(shape.cellsZ);
		for (int x = 0; x < gridWidth; ++x) {
			const float u = static_cast<float>(x) / static_cast<float>(shape.cellsX);
			const float baseHeight = SampleHeightmap(map, mapWidth, mapHeight, u, v);
			const float noise = FractalNoise(u * settings.noiseScale, v * settings.noiseScale,
				settings.noiseSeed, settings.noiseOctaves);
			heights[static_cast<size_t>(z) * gridWidth + x] = std::clamp(
				baseHeight + noise * settings.noiseStrength, 0.0f, 1.0f);
		}
	}
	return heights;
}

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
						XMFLOAT3& boundsMin, XMFLOAT3& boundsMax)
{
	const HeightfieldLayout& layout = field.GetLayout();
	const float skirtDepth = SkirtDepthFor(layout.heightScale);
	const int cellsInX = x1 - x0;
	const int cellsInZ = z1 - z0;
	const int vertsX = cellsInX + 1;

	vertices.clear();
	vertices.reserve(static_cast<size_t>(vertsX) * (cellsInZ + 1) + 2 * static_cast<size_t>(vertsX + cellsInZ + 1));
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
	auto addSkirtVertex = [&](int x, int z) {
		Vertex copy = vertices[static_cast<size_t>(z) * vertsX + x];
		copy.position.y -= skirtDepth;
		boundsMin.y = (std::min)(boundsMin.y, copy.position.y);
		vertices.push_back(copy);
	};
	for (int x = 0; x <= cellsInX; ++x) addSkirtVertex(x, 0);
	for (int x = 0; x <= cellsInX; ++x) addSkirtVertex(x, cellsInZ);
	for (int z = 0; z <= cellsInZ; ++z) addSkirtVertex(0, z);
	for (int z = 0; z <= cellsInZ; ++z) addSkirtVertex(cellsInX, z);
}

Model* BuildTerrainModel(ID3D11Device* device, const Heightfield& field, std::vector<TerrainChunk>& outChunks,
						 int& outChunksX, int& outChunksZ)
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
			const int vertsX = x1 - x0 + 1;

			std::vector<Vertex> vertices;
			XMFLOAT3 boundsMin, boundsMax;
			BuildChunkVertices(field, x0, z0, x1, z1, vertices, boundsMin, boundsMax);

			const int cellsInX = x1 - x0;
			const int cellsInZ = z1 - z0;

			// SKIRTS. Two neighbouring tiles at different detail levels do not share edge vertices,
			// which opens thin cracks between them. We hang a vertical strip of triangles below a
			// tile edge so that whatever is behind a crack is the terrain, not the sky. These extra
			// vertices are copies of the edge vertices, moved straight down. The skirts are stored as
			// separate PARTS of the index buffer (north, south, west, east) so that a draw can include
			// only the edges that really have a neighbour at a different level (see TerrainSkirts.h).
			const int gridVertexCount = vertsX * (cellsInZ + 1);   // the surface's vertices; the skirt vertices (made by BuildChunkVertices) follow them
			const int northBase = gridVertexCount;                       // z = 0 edge, one per x
			const int southBase = northBase + (cellsInX + 1);             // z = cellsInZ edge
			const int westBase = southBase + (cellsInX + 1);              // x = 0 edge, one per z
			const int eastBase = westBase + (cellsInZ + 1);               // x = cellsInX edge
			auto grid = [&](int x, int z) { return static_cast<uint32_t>(z * vertsX + x); };

			// LEVELS OF DETAIL: the same vertices, connected more coarsely. Level L skips grid lines
			// so that only every (2^L)-th vertex is used. The last line is always kept, so the tile's
			// outline never changes.
			std::vector<MeshLevel> levels;
			for (int lod = 0; lod < kTerrainLodCount; ++lod) {
				const int step = 1 << lod;
				auto lines = [step](int cells) {
					std::vector<int> positions;
					for (int i = 0; i < cells; i += step) positions.push_back(i);
					positions.push_back(cells);
					return positions;
				};
				const std::vector<int> xs = lines(cellsInX);
				const std::vector<int> zs = lines(cellsInZ);

				std::vector<uint32_t> indices;                 // the surface (part 0)
				std::vector<uint32_t> skirtIndices[4];         // north, south, west, east (parts 1..4)
				indices.reserve((xs.size() - 1) * (zs.size() - 1) * 6);

				// The surface itself (clockwise seen from above = front face).
				for (size_t zi = 0; zi + 1 < zs.size(); ++zi) {
					for (size_t xi = 0; xi + 1 < xs.size(); ++xi) {
						const uint32_t topLeft = grid(xs[xi], zs[zi]);
						const uint32_t topRight = grid(xs[xi + 1], zs[zi]);
						const uint32_t bottomLeft = grid(xs[xi], zs[zi + 1]);
						const uint32_t bottomRight = grid(xs[xi + 1], zs[zi + 1]);
						indices.insert(indices.end(), {topLeft, bottomLeft, topRight,
													   topRight, bottomLeft, bottomRight});
					}
				}
				// The skirts, wound so they face OUTWARD from the tile (the winding for each of the four
				// edges differs because "clockwise as seen from outside" depends on which side you stand on).
				for (size_t i = 0; i + 1 < xs.size(); ++i) {
					const int a = xs[i], b = xs[i + 1];
					{   // north edge (faces -z)
						const uint32_t ea = grid(a, 0), eb = grid(b, 0), sa = northBase + a, sb = northBase + b;
						skirtIndices[0].insert(skirtIndices[0].end(), {ea, eb, sa,  eb, sb, sa});
					}
					{   // south edge (faces +z)
						const uint32_t ea = grid(a, cellsInZ), eb = grid(b, cellsInZ), sa = southBase + a, sb = southBase + b;
						skirtIndices[1].insert(skirtIndices[1].end(), {eb, ea, sb,  ea, sa, sb});
					}
				}
				for (size_t i = 0; i + 1 < zs.size(); ++i) {
					const int a = zs[i], b = zs[i + 1];
					{   // west edge (faces -x)
						const uint32_t ea = grid(0, a), eb = grid(0, b), sa = westBase + a, sb = westBase + b;
						skirtIndices[2].insert(skirtIndices[2].end(), {eb, ea, sb,  ea, sa, sb});
					}
					{   // east edge (faces +x)
						const uint32_t ea = grid(cellsInX, a), eb = grid(cellsInX, b), sa = eastBase + a, sb = eastBase + b;
						skirtIndices[3].insert(skirtIndices[3].end(), {ea, eb, sa,  eb, sb, sa});
					}
				}
				// One index buffer per level: the surface first (part 0, always drawn), then the four skirts.
				MeshLevel level;
				level.partIndexCounts.push_back(static_cast<uint32_t>(indices.size()));
				level.indices = std::move(indices);
				for (int edge = 0; edge < 4; ++edge) {
					level.partIndexCounts.push_back(static_cast<uint32_t>(skirtIndices[edge].size()));
					level.indices.insert(level.indices.end(), skirtIndices[edge].begin(), skirtIndices[edge].end());
				}
				levels.push_back(std::move(level));
			}

			meshes.push_back(new Mesh(device, vertices, levels, /*updatableVertices=*/true));   // sculpting rewrites them
			TerrainChunk chunk;
			chunk.boundsMin = boundsMin;
			chunk.boundsMax = boundsMax;
			for (int lod = 0; lod < kTerrainLodCount; ++lod) {
				chunk.triangleCount[lod] = static_cast<int>(levels[lod].partIndexCounts[0] / 3);
				for (int edge = 0; edge < 4; ++edge) chunk.skirtTriangles[lod][edge] = static_cast<int>(levels[lod].partIndexCounts[1 + edge] / 3);
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
	noiseStrength = std::clamp(noiseStrength, 0.0f, 1.0f);
	noiseScale = std::clamp(noiseScale, kNoiseScaleMin, kNoiseScaleMax);
	noiseOctaves = std::clamp(noiseOctaves, kOctavesMin, kOctavesMax);
	uvTiling = std::clamp(uvTiling, kTilingMin, kTilingMax);
	groundTiling = std::clamp(groundTiling, kRockTilingMin, kRockTilingMax);
	rockTiling = std::clamp(rockTiling, kRockTilingMin, kRockTilingMax);
	sandTiling = std::clamp(sandTiling, kRockTilingMin, kRockTilingMax);
	snowTiling = std::clamp(snowTiling, kRockTilingMin, kRockTilingMax);
	rockDarkness = std::clamp(rockDarkness, 0.1f, 1.5f);
	macroInfluence = std::clamp(macroInfluence, 0.0f, 1.0f);
	normalStrength = std::clamp(normalStrength, 0.0f, 3.0f);
	heightBlend = std::clamp(heightBlend, 0.0f, 1.0f);
	aoStrength = std::clamp(aoStrength, 0.0f, 1.0f);
	roughnessScale = std::clamp(roughnessScale, 0.0f, 2.0f);
	slopeStart = std::clamp(slopeStart, 0.0f, 1.0f);
	slopeFade = std::clamp(slopeFade, 0.01f, 1.0f);
	shoreHeight = std::clamp(shoreHeight, kBandMin, kBandMax);
	shoreWidth = std::clamp(shoreWidth, 0.1f, 30.0f);
	snowHeight = std::clamp(snowHeight, kBandMin, kBandMax);
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
	rockDarkness = defaults.rockDarkness; macroInfluence = defaults.macroInfluence;
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
	return width == o.width && heightScale == o.heightScale && maxCells == o.maxCells &&
		noiseStrength == o.noiseStrength && noiseScale == o.noiseScale &&
		noiseOctaves == o.noiseOctaves && noiseSeed == o.noiseSeed;
}

bool TerrainSettings::SameSource(const TerrainSettings& o) const
{
	return maxCells == o.maxCells && noiseStrength == o.noiseStrength && noiseScale == o.noiseScale &&
		noiseOctaves == o.noiseOctaves && noiseSeed == o.noiseSeed;
}

std::string Terrain::DefaultLayerMapPath(int layer, TerrainLayerMap map)
{
	static const char* const kFileNames[] = { "albedo.png", "normal.png", "orm.png" };
	if (layer < 0 || layer >= kTerrainLayerCount) return {};
	return std::string("assets/Terrain/") + kTerrainLayerNames[layer] + "/" + kFileNames[static_cast<int>(map)];
}

Terrain::Terrain(ID3D11Device* device) : device_(device)
{
	heightmapPath_ = DefaultHeightmapPath();
	colorMapPath_ = DefaultColorMapPath();
	texture_ = std::make_unique<Texture>(device, colorMapPath_);

	// The layer maps: assets/Terrain/<Layer>/albedo.png, normal.png (alpha = height), orm.png, unless replaced.
	// Slice order is part of the contract with terrain_ps.hlsl: 0 grass, 1 rock, 2 sand, 3 snow.
	for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
		for (int map = 0; map < 3; ++map) layerMapPaths_[layer][map] = DefaultLayerMapPath(layer, static_cast<TerrainLayerMap>(map));
	}
	RebuildLayerTexture(TerrainLayerMap::Albedo);
	RebuildLayerTexture(TerrainLayerMap::Normal);
	RebuildLayerTexture(TerrainLayerMap::Orm);
	LoadHeightmapFile(heightmapPath_.c_str(), heightmap_.width, heightmap_.height, heightmap_.values);
	settings_.Clamp();
	if (!Rebuild(settings_)) LOG_ERROR("Terrain mesh initialization failed");
	UpdateWorldTransform();
	if (!IsValid()) LOG_ERROR("Terrain initialization failed");
}

Terrain::~Terrain()
{
	if (wireframeState_) wireframeState_->Release();
}

bool Terrain::IsValid() const
{
	return renderable_ && renderable_->IsValid() && texture_ && texture_->IsValid() &&
		layerAlbedo_ && layerAlbedo_->IsValid() && layerNormal_ && layerNormal_->IsValid() &&
		layerOrm_ && layerOrm_->IsValid();
}

void Terrain::GetLayerFileCounts(int& albedo, int& normal, int& orm) const
{
	albedo = layerAlbedo_ ? layerAlbedo_->GetLoadedCount() : 0;
	normal = layerNormal_ ? layerNormal_->GetLoadedCount() : 0;
	orm = layerOrm_ ? layerOrm_->GetLoadedCount() : 0;
}

// Reads the four files of one kind of map into a fresh texture array. The fill for a missing file is the neutral
// value of that kind: white colour (the tint decides the look), a flat normal with middling height, no occlusion +
// medium roughness + not metal. The old array stays if the new one cannot be built.
bool Terrain::RebuildLayerTexture(TerrainLayerMap map)
{
	const int kind = static_cast<int>(map);
	std::vector<std::string> paths;
	for (int layer = 0; layer < kTerrainLayerCount; ++layer) paths.push_back(layerMapPaths_[layer][kind]);

	std::unique_ptr<TextureArray>& target = (map == TerrainLayerMap::Albedo) ? layerAlbedo_
										  : (map == TerrainLayerMap::Normal) ? layerNormal_ : layerOrm_;
	const ColorSpace colorSpace = (map == TerrainLayerMap::Albedo) ? ColorSpace::SRGB : ColorSpace::Linear;
	const std::array<std::uint8_t, 4> fill = (map == TerrainLayerMap::Albedo) ? std::array<std::uint8_t, 4>{ 255, 255, 255, 255 }
										   : (map == TerrainLayerMap::Normal) ? std::array<std::uint8_t, 4>{ 128, 128, 255, 128 }
																			  : std::array<std::uint8_t, 4>{ 255, 128, 0, 255 };
	auto replacement = std::make_unique<TextureArray>(device_, paths, colorSpace, fill);
	if (!replacement->IsValid()) return false;
	target = std::move(replacement);
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
	if (!RebuildLayerTexture(map)) {   // the files for the other layers could not be read together: undo
		slot = previous;
		return false;
	}
	return true;
}

bool Terrain::SetHeightmap(const std::string& path)
{
	const std::string wanted = path.empty() ? std::string(DefaultHeightmapPath()) : path;
	Heightmap loaded;
	if (!LoadHeightmapFile(wanted.c_str(), loaded.width, loaded.height, loaded.values)) return false;

	std::swap(heightmap_, loaded);   // `loaded` now holds the OLD map, in case the rebuild fails
	if (!Rebuild(settings_)) {
		std::swap(heightmap_, loaded);
		return false;
	}
	heightmapPath_ = wanted;
	return true;
}

bool Terrain::SetColorMap(const std::string& path)
{
	const std::string wanted = path.empty() ? std::string(DefaultColorMapPath()) : path;
	auto replacement = std::make_unique<Texture>(device_, wanted);
	if (!replacement->IsValid()) return false;
	texture_ = std::move(replacement);
	colorMapPath_ = wanted;
	return true;
}

bool Terrain::ApplySettings(TerrainSettings settings)
{
	settings.Clamp();
	if (renderable_ && settings.SameShape(settings_)) {   // only the tiling changed
		settings_ = settings;
		UpdateMaterial();
		return true;
	}
	// Width and height scale only STRETCH the heights, so a sculpt survives them. A different resolution or noise
	// would mean a different grid, so those throw the sculpt away (the editor locks them while sculpted).
	return Rebuild(settings, /*keepSculpt=*/sculpted_ && settings.SameSource(settings_));
}

bool Terrain::Rebuild(const TerrainSettings& settings, bool keepSculpt)
{
	if (!device_ || !texture_ || !texture_->IsValid() || heightmap_.values.empty()) return false;

	const GridShape shape = ComputeGridShape(settings, heightmap_.width, heightmap_.height);
	// A sculpt is kept only while the grid is the very same one it was made on.
	const bool keep = keepSculpt && sculpted_ && !heightfield_.IsEmpty() &&
		heightfield_.GetLayout().cellsX == shape.cellsX && heightfield_.GetLayout().cellsZ == shape.cellsZ;
	std::vector<float> samples = keep ? heightfield_.Samples()
									  : GenerateHeights(heightmap_.values, heightmap_.width, heightmap_.height, settings, shape);
	Heightfield field;
	if (!field.Reset({ shape.cellsX, shape.cellsZ, settings.width, shape.depth, settings.heightScale }, std::move(samples))) return false;

	const float depth = shape.depth;
	std::vector<TerrainChunk> chunks;
	int chunksX = 0, chunksZ = 0;
	Model* model = BuildTerrainModel(device_, field, chunks, chunksX, chunksZ);
	if (!model) return false;
	auto replacement = std::make_unique<Renderable>(device_, model, L"shaders/vs.hlsl", L"shaders/terrain_ps.hlsl");
	if (!replacement->IsValid()) return false;

	renderable_ = std::move(replacement);
	heightfield_ = std::move(field);
	sculpted_ = keep;
	stroke_.Clear();
	strokeActive_ = false;
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
		partMasks_[i] = mask;
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
	block.layers = { settings_.rockTiling, settings_.breakupScale, settings_.breakupAmount, settings_.rockDarkness };
	block.heightBands = { settings_.shoreHeight, settings_.shoreWidth, settings_.snowHeight, settings_.snowFade };
	block.slope = { settings_.slopeStart, settings_.slopeFade, settings_.showLayers ? 1.0f : 0.0f, settings_.heightBlend };
	block.tilings = { settings_.groundTiling, settings_.sandTiling, settings_.snowTiling, settings_.macroInfluence };
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

bool Terrain::SampleNormalizedHeight(float worldX, float worldZ, float& value) const
{
	if (heightfield_.IsEmpty() || std::fabs(scale_.x) < 1e-6f || std::fabs(scale_.z) < 1e-6f) return false;
	float localY = 0.0f;
	if (!heightfield_.HeightAt((worldX - position_.x) / scale_.x, (worldZ - position_.z) / scale_.z, localY)) return false;
	value = localY / heightfield_.GetLayout().heightScale;
	return true;
}

// ---- Sculpting (lesson 27) ----------------------------------------------------------------------------------------

void Terrain::BeginSculptStroke()
{
	stroke_.Clear();
	strokeActive_ = true;
}

bool Terrain::Sculpt(const BrushSettings& brush, float worldX, float worldZ, float seconds)
{
	if (heightfield_.IsEmpty() || !renderable_ || std::fabs(scale_.x) < 1e-6f || std::fabs(scale_.z) < 1e-6f) return false;
	// The brush works in the heightfield's local space, where everything is divided by the scale.
	BrushSettings local = brush;
	local.radius = brush.radius / scale_.x;
	const GridRect changed = ApplyBrush(heightfield_, local, (worldX - position_.x) / scale_.x, (worldZ - position_.z) / scale_.z,
										seconds, strokeActive_ ? &stroke_ : nullptr);
	if (changed.IsEmpty()) return false;
	sculpted_ = true;
	RefreshChunks(changed);
	// The revision moves when the stroke ENDS, not on every step: the water re-measures its shore each time it changes,
	// which is too much work to do every frame while you drag. (A step outside a stroke has no end, so it counts at once.)
	if (!strokeActive_) NewRevision();
	return true;
}

HeightfieldPatch Terrain::EndSculptStroke()
{
	HeightfieldPatch patch = stroke_.Finish(heightfield_);
	const bool changedGround = strokeActive_ && !patch.IsEmpty();
	stroke_.Clear();
	strokeActive_ = false;
	if (changedGround) NewRevision();   // the ground is final now: the water's shore map can be measured again
	return patch;
}

bool Terrain::ApplyPatch(const HeightfieldPatch& patch, bool useAfter)
{
	if (heightfield_.IsEmpty() || !renderable_) return false;
	const GridRect changed = ::ApplyPatch(heightfield_, patch, useAfter);   // the free function: same name as this member
	if (changed.IsEmpty()) return false;
	if (useAfter) sculpted_ = true;
	RefreshChunks(changed);
	NewRevision();
	return true;
}

bool Terrain::ReplaceHeights(std::vector<float> samples, bool sculpted)
{
	if (heightfield_.IsEmpty() || !renderable_ || samples.size() != heightfield_.Samples().size()) return false;
	const HeightfieldLayout layout = heightfield_.GetLayout();
	if (!heightfield_.Reset(layout, std::move(samples))) return false;
	sculpted_ = sculpted;
	stroke_.Clear();
	strokeActive_ = false;
	NewRevision();
	GridRect everything;
	everything.Include(0, 0);
	everything.Include(layout.cellsX, layout.cellsZ);
	RefreshChunks(everything);
	return true;
}

bool Terrain::GenerateOriginalHeights(std::vector<float>& samples) const
{
	if (heightmap_.values.empty()) return false;
	const GridShape shape = ComputeGridShape(settings_, heightmap_.width, heightmap_.height);
	samples = GenerateHeights(heightmap_.values, heightmap_.width, heightmap_.height, settings_, shape);
	return true;
}

bool Terrain::LoadSculptedHeights(const std::string& path, std::string& error)
{
	int width = 0, height = 0;
	std::vector<float> values;
	if (!LoadHeightmapFile(path.c_str(), width, height, values)) {
		error = "the image could not be read";
		return false;
	}
	const HeightfieldLayout& layout = heightfield_.GetLayout();
	if (heightfield_.IsEmpty() || width != layout.cellsX + 1 || height != layout.cellsZ + 1) {
		error = "the image is " + std::to_string(width) + " x " + std::to_string(height) + " but this terrain's grid is " +
				std::to_string(layout.cellsX + 1) + " x " + std::to_string(layout.cellsZ + 1);
		return false;
	}
	return ReplaceHeights(std::move(values), true);
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
			BuildChunkVertices(heightfield_, x0, z0, x1, z1, vertices, boundsMin, boundsMax);
			if (!mesh->UpdateVertices(ctx, vertices)) continue;
			chunks_[index].boundsMin = boundsMin;   // the box culling uses must follow the new shape
			chunks_[index].boundsMax = boundsMax;
		}
	}
	ctx->Release();
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
	position_ = position;
	rotation_ = { 0.0f, 0.0f, 0.0f };
	scale_ = scale;
	UpdateWorldTransform();
	return true;
}

void Terrain::NewRevision()
{
	static unsigned nextRevision = 0;   // one counter for every terrain: a revision is never reused
	revision_ = ++nextRevision;
}

void Terrain::UpdateWorldTransform()
{
	NewRevision();
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
	renderable_->SetTexture(ctx, texture_->GetShaderResourceView());   // t0: the terrain-wide colour map
	ID3D11ShaderResourceView* albedo = layerAlbedo_->GetShaderResourceView();
	ID3D11ShaderResourceView* normal = layerNormal_->GetShaderResourceView();
	ID3D11ShaderResourceView* orm = layerOrm_->GetShaderResourceView();
	ctx->PSSetShaderResources(Slots::kSrvTerrainAlbedo, 1, &albedo);
	ctx->PSSetShaderResources(Slots::kSrvTerrainNormal, 1, &normal);
	ctx->PSSetShaderResources(Slots::kSrvTerrainOrm, 1, &orm);
	DrawChunks(ctx, nullptr, view);
}
