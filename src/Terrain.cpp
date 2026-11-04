#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Terrain.h"
#include "Color.h"
#include "Logger.h"
#include "Mesh.h"
#include "libs/stb_image.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
// Must match `cbuffer TerrainMaterial : register(b4)` in shaders/terrain_ps.hlsl, field for field.
struct TerrainMaterialCB {
	XMFLOAT4 layers;       // rock tiling, breakup scale, breakup amount, rock darkness
	XMFLOAT4 heightBands;  // shore height, shore width, snow height, snow fade
	XMFLOAT4 slope;        // slope start, slope fade, debug flag, unused
	XMFLOAT4 shoreTint;    // linear rgb
	XMFLOAT4 snowTint;     // linear rgb
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

// Decodes the heightmap once (red channel, 0..1).
bool LoadHeightmapFile(const char* path, int& width, int& height, std::vector<float>& values)
{
	int channels = 0;
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

Model* BuildTerrainModel(ID3D11Device* device, const std::vector<float>& map, int mapWidth, int mapHeight,
						 const TerrainSettings& settings, float& outDepth)
{
	if (!device || map.empty()) return nullptr;

	const float terrainWidth = settings.width;
	const int cellsX = settings.maxCells;
	const int cellsZ = std::max(2, static_cast<int>(std::lround(
		static_cast<float>(cellsX) * static_cast<float>(mapHeight) / static_cast<float>(mapWidth))));
	const int gridWidth = cellsX + 1;
	const int gridHeight = cellsZ + 1;
	const float depth = terrainWidth * static_cast<float>(mapHeight) / static_cast<float>(mapWidth);
	const float cellWidth = terrainWidth / static_cast<float>(cellsX);
	const float cellDepth = depth / static_cast<float>(cellsZ);
	outDepth = depth;

	std::vector<float> heights(static_cast<size_t>(gridWidth) * gridHeight);
	auto heightAt = [&](int x, int z) {
		x = std::clamp(x, 0, gridWidth - 1);
		z = std::clamp(z, 0, gridHeight - 1);
		return heights[static_cast<size_t>(z) * gridWidth + x];
	};

	for (int z = 0; z < gridHeight; ++z) {
		const float v = static_cast<float>(z) / static_cast<float>(cellsZ);
		for (int x = 0; x < gridWidth; ++x) {
			const float u = static_cast<float>(x) / static_cast<float>(cellsX);
			const float baseHeight = SampleHeightmap(map, mapWidth, mapHeight, u, v);
			const float noise = FractalNoise(u * settings.noiseScale, v * settings.noiseScale,
				settings.noiseSeed, settings.noiseOctaves);
			heights[static_cast<size_t>(z) * gridWidth + x] = std::clamp(
				baseHeight + noise * settings.noiseStrength, 0.0f, 1.0f);
		}
	}

	std::vector<Vertex> vertices;
	vertices.reserve(static_cast<size_t>(gridWidth) * gridHeight);
	for (int z = 0; z < gridHeight; ++z) {
		const float v = static_cast<float>(z) / static_cast<float>(gridHeight - 1);
		for (int x = 0; x < gridWidth; ++x) {
			const float u = static_cast<float>(x) / static_cast<float>(gridWidth - 1);
			const float y = heightAt(x, z) * settings.heightScale;
			const float slopeX = (heightAt(x + 1, z) - heightAt(x - 1, z)) * settings.heightScale /
								 (2.0f * cellWidth);
			const float slopeZ = (heightAt(x, z + 1) - heightAt(x, z - 1)) * settings.heightScale /
								 (2.0f * cellDepth);
			XMVECTOR normal = XMVector3Normalize(XMVectorSet(-slopeX, 1.0f, -slopeZ, 0.0f));
			XMFLOAT3 normalFloat;
			XMStoreFloat3(&normalFloat, normal);

			vertices.emplace_back(
				XMFLOAT3((u - 0.5f) * terrainWidth, y, (v - 0.5f) * depth),
				normalFloat,
				XMFLOAT2(u, v));
		}
	}

	std::vector<uint32_t> indices;
	indices.reserve(static_cast<size_t>(gridWidth - 1) * (gridHeight - 1) * 6);
	for (int z = 0; z < gridHeight - 1; ++z) {
		for (int x = 0; x < gridWidth - 1; ++x) {
			const uint32_t topLeft = static_cast<uint32_t>(z * gridWidth + x);
			const uint32_t topRight = topLeft + 1;
			const uint32_t bottomLeft = static_cast<uint32_t>((z + 1) * gridWidth + x);
			const uint32_t bottomRight = bottomLeft + 1;
			indices.insert(indices.end(), {topLeft, bottomLeft, topRight,
										   topRight, bottomLeft, bottomRight});
		}
	}

	return new Model({new Mesh(device, vertices, indices)});
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
	rockTiling = std::clamp(rockTiling, kRockTilingMin, kRockTilingMax);
	rockDarkness = std::clamp(rockDarkness, 0.1f, 1.5f);
	slopeStart = std::clamp(slopeStart, 0.0f, 1.0f);
	slopeFade = std::clamp(slopeFade, 0.01f, 1.0f);
	shoreHeight = std::clamp(shoreHeight, kBandMin, kBandMax);
	shoreWidth = std::clamp(shoreWidth, 0.1f, 30.0f);
	snowHeight = std::clamp(snowHeight, kBandMin, kBandMax);
	snowFade = std::clamp(snowFade, 0.1f, 50.0f);
	breakupScale = std::clamp(breakupScale, kBreakupScaleMin, kBreakupScaleMax);
	breakupAmount = std::clamp(breakupAmount, 0.0f, 1.0f);
}

void TerrainSettings::ResetLayers()
{
	const TerrainSettings defaults;
	rockTiling = defaults.rockTiling;     rockDarkness = defaults.rockDarkness;
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

Terrain::Terrain(ID3D11Device* device) : device_(device)
{
	texture_ = std::make_unique<Texture>(device, "assets/dark alien/Diffuse Map.png");
	LoadHeightmapFile("assets/dark alien/Height Map.png", heightmap_.width, heightmap_.height, heightmap_.values);
	settings_.Clamp();
	if (!Rebuild(settings_)) LOG_ERROR("Terrain mesh initialization failed");
	UpdateWorldTransform();
	if (!IsValid()) LOG_ERROR("Terrain initialization failed");
}

bool Terrain::IsValid() const
{
	return renderable_ && renderable_->IsValid() && texture_ && texture_->IsValid();
}

bool Terrain::ApplySettings(TerrainSettings settings)
{
	settings.Clamp();
	if (renderable_ && settings.SameShape(settings_)) {   // only the tiling changed
		settings_ = settings;
		UpdateMaterial();
		return true;
	}
	return Rebuild(settings);
}

bool Terrain::Rebuild(const TerrainSettings& settings)
{
	if (!device_ || !texture_ || !texture_->IsValid() || heightmap_.values.empty()) return false;

	float depth = 0.0f;
	Model* model = BuildTerrainModel(device_, heightmap_.values, heightmap_.width, heightmap_.height,
									 settings, depth);
	if (!model) return false;
	auto replacement = std::make_unique<Renderable>(device_, model, L"shaders/vs.hlsl", L"shaders/terrain_ps.hlsl");
	if (!replacement->IsValid()) return false;

	renderable_ = std::move(replacement);
	settings_ = settings;
	depth_ = depth;
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

void Terrain::RenderShadow(ID3D11DeviceContext* ctx, const Shader& shader) const
{
	if (renderable_ && renderable_->IsValid()) renderable_->RenderWith(ctx, shader);
}

void Terrain::UpdateMaterial()
{
	if (!renderable_) return;
	renderable_->SetMaterialParameters({ settings_.uvTiling, 1.0f, 1.0f, 1.0f });   // vs.hlsl: UV tiling

	const XMFLOAT3 sand = SrgbToLinear(settings_.shoreColor);   // picked as sRGB, shader wants linear
	const XMFLOAT3 snow = SrgbToLinear(settings_.snowColor);
	TerrainMaterialCB block;
	block.layers = { settings_.rockTiling, settings_.breakupScale, settings_.breakupAmount, settings_.rockDarkness };
	block.heightBands = { settings_.shoreHeight, settings_.shoreWidth, settings_.snowHeight, settings_.snowFade };
	block.slope = { settings_.slopeStart, settings_.slopeFade, settings_.showLayers ? 1.0f : 0.0f, 0.0f };
	block.shoreTint = { sand.x, sand.y, sand.z, 1.0f };
	block.snowTint = { snow.x, snow.y, snow.z, 1.0f };
	renderable_->SetMaterialBlock(&block, sizeof(block));
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
	position_ = position;
	rotation_ = rotation;
	scale_ = scale;
	UpdateWorldTransform();
	return true;
}

void Terrain::UpdateWorldTransform()
{
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

void Terrain::Render(ID3D11DeviceContext* ctx, const Camera& camera) const
{
	if (!IsValid()) return;
	renderable_->SetTexture(ctx, texture_->GetShaderResourceView());
	renderable_->Render(ctx);
}
