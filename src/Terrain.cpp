#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Terrain.h"
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
constexpr int maxTerrainCells = 512;
constexpr float terrainWidth = 240.0f;

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

float FractalNoise(float x, float z, int seed)
{
	float value = 0.0f;
	float amplitude = 0.5f;
	float frequency = 1.0f;
	float amplitudeTotal = 0.0f;
	for (int octave = 0; octave < 4; ++octave) {
		value += ValueNoise(x * frequency, z * frequency, seed + octave * 31) * amplitude;
		amplitudeTotal += amplitude;
		frequency *= 2.0f;
		amplitude *= 0.5f;
	}
	return value / amplitudeTotal;
}

Model* CreateTerrainModel(ID3D11Device* device, const char* heightmapPath,
						  const TerrainSettings& settings)
{
	if (!device || !heightmapPath) return nullptr;

	int imageWidth = 0;
	int imageHeight = 0;
	int channels = 0;
	unsigned char* image = stbi_load(heightmapPath, &imageWidth, &imageHeight, &channels, STBI_rgb_alpha);
	if (!image || imageWidth < 2 || imageHeight < 2) {
		LOG_ERROR("Failed to load terrain heightmap: " + std::string(heightmapPath));
		if (image) stbi_image_free(image);
		return nullptr;
	}

	const int sampleStep = std::max(1, std::max(imageWidth, imageHeight) / maxTerrainCells);
	const int gridWidth = (imageWidth - 1) / sampleStep + 1;
	const int gridHeight = (imageHeight - 1) / sampleStep + 1;
	const float depth = terrainWidth * static_cast<float>(imageHeight) / static_cast<float>(imageWidth);
	const float cellWidth = terrainWidth / static_cast<float>(gridWidth - 1);
	const float cellDepth = depth / static_cast<float>(gridHeight - 1);

	std::vector<float> heights(static_cast<size_t>(gridWidth) * gridHeight);
	auto heightAt = [&](int x, int z) {
		x = std::clamp(x, 0, gridWidth - 1);
		z = std::clamp(z, 0, gridHeight - 1);
		return heights[static_cast<size_t>(z) * gridWidth + x];
	};

	for (int z = 0; z < gridHeight; ++z) {
		const int sourceZ = std::min(z * sampleStep, imageHeight - 1);
		for (int x = 0; x < gridWidth; ++x) {
			const int sourceX = std::min(x * sampleStep, imageWidth - 1);
			const size_t sourceIndex = (static_cast<size_t>(sourceZ) * imageWidth + sourceX) * 4;
			const float baseHeight = image[sourceIndex] / 255.0f;
			const float noise = FractalNoise(
				(static_cast<float>(x) / static_cast<float>(gridWidth - 1)) * settings.noiseScale,
				(static_cast<float>(z) / static_cast<float>(gridHeight - 1)) * settings.noiseScale,
				settings.noiseSeed);
			heights[static_cast<size_t>(z) * gridWidth + x] = std::clamp(
				baseHeight + noise * settings.noiseStrength, 0.0f, 1.0f);
		}
	}
	stbi_image_free(image);

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

Terrain::Terrain(ID3D11Device* device) : device_(device)
{
	texture_ = std::make_unique<Texture>(device, "assets/dark alien/Diffuse Map.png");
	if (!Regenerate(settings_)) LOG_ERROR("Terrain mesh initialization failed");
	UpdateWorldTransform();
	if (!IsValid()) LOG_ERROR("Terrain initialization failed");
}

bool Terrain::IsValid() const
{
	return renderable_ && renderable_->IsValid() && texture_ && texture_->IsValid();
}

bool Terrain::Regenerate(const TerrainSettings& settings)
{
	if (!device_ || !texture_ || !texture_->IsValid()) return false;

	auto replacement = std::make_unique<Renderable>(
		device_, CreateTerrainModel(device_, "assets/dark alien/Height Map.png", settings),
		L"shaders/vs.hlsl", L"shaders/ps.hlsl");
	if (!replacement->IsValid()) return false;

	renderable_ = std::move(replacement);
	settings_ = settings;
	UpdateWorldTransform();
	return true;
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
	renderable_->Render(ctx, camera.GetViewMatrix(), camera.GetProjectionMatrix());
}
