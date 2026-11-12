#pragma once
// The ground under a water body, as a small map of "how deep is the water here". Plain C++ with no Direct3D, so the SAME
// numbers can be baked from the terrain, uploaded as a texture for the shaders, and read on the CPU by gameplay code
// (Water::SampleHeight), and so a test can check them on their own.
//
//     depth = water level - ground height        (world units; <= 0 means dry land)
//
// The map lives in the body's OWN space: u = (x + 1) / 2 and v = (z + 1) / 2 for local x, z in -1..1, exactly the
// coordinates the water mesh vertices carry. Moving, rotating or scaling the body therefore never changes what a
// texel means, only the (cheap) question "which world position is this texel?".
#include <algorithm>
#include <cmath>
#include <vector>

namespace WaterShore {

// What the map says where there is no ground at all (outside the terrain): open, deep water.
constexpr float kDeepWater = 100.0f;

constexpr int kMinTexels = 4;
constexpr int kMaxTexelsPerSide = 1024;
constexpr int kMaxTexelsTotal = 131072;   // ~360 x 360 for a square body: bakes in a few milliseconds
constexpr float kMinTexelSize = 0.25f;    // world units: finer than this buys nothing (waves are longer than 1 unit)

// How many texels the map gets for a body of this size: roughly square texels, never finer than kMinTexelSize, never
// more than kMaxTexelsTotal in all. A big lake gets coarse texels (fine: shoaling is a slow, wide effect); a thin river
// gets fine ones along its length.
inline void ChooseResolution(float extentX, float extentZ, int& width, int& height)
{
	extentX = std::max(extentX, 0.01f);
	extentZ = std::max(extentZ, 0.01f);
	const float texel = std::max(kMinTexelSize, std::sqrt(extentX * extentZ / static_cast<float>(kMaxTexelsTotal)));
	width = std::clamp(static_cast<int>(std::ceil(extentX / texel)), kMinTexels, kMaxTexelsPerSide);
	height = std::clamp(static_cast<int>(std::ceil(extentZ / texel)), kMinTexels, kMaxTexelsPerSide);
}

class DepthField {
public:
	bool IsEmpty() const { return depth_.empty(); }
	int Width() const { return width_; }
	int Height() const { return height_; }
	const std::vector<float>& Data() const { return depth_; }   // row by row, z-major: index = z * Width() + x

	bool Reset(int width, int height, std::vector<float> depth)
	{
		if (width < 1 || height < 1 || depth.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) return false;
		width_ = width;
		height_ = height;
		depth_ = std::move(depth);
		return true;
	}
	void Clear() { width_ = height_ = 0; depth_.clear(); }

	// Bilinear, clamped at the edges, in LOCAL coordinates (-1..1). Texel centres sit at (i + 0.5) / width, which is also
	// what the GPU's texture sampling does, so the CPU and the shader read the same number (shaders/water_material.hlsli).
	float Sample(float localX, float localZ) const
	{
		if (depth_.empty()) return kDeepWater;
		const float px = std::clamp((localX * 0.5f + 0.5f), 0.0f, 1.0f) * static_cast<float>(width_) - 0.5f;
		const float pz = std::clamp((localZ * 0.5f + 0.5f), 0.0f, 1.0f) * static_cast<float>(height_) - 0.5f;
		const float fx0 = std::floor(px), fz0 = std::floor(pz);
		const float tx = px - fx0, tz = pz - fz0;
		const int x0 = static_cast<int>(fx0), z0 = static_cast<int>(fz0);
		auto at = [&](int x, int z) {
			x = std::clamp(x, 0, width_ - 1);
			z = std::clamp(z, 0, height_ - 1);
			return depth_[static_cast<size_t>(z) * width_ + x];
		};
		const float d00 = at(x0, z0), d10 = at(x0 + 1, z0), d01 = at(x0, z0 + 1), d11 = at(x0 + 1, z0 + 1);
		const float a = d00 + (d10 - d00) * tx;
		const float b = d01 + (d11 - d01) * tx;
		return a + (b - a) * tz;
	}

	// The DEEPEST (largest) value in a rectangle of the map, in LOCAL coordinates (-1..1). Every texel the rectangle touches
	// counts, plus one texel of border all round (a bilinear read near the edge also mixes in its neighbour), so the answer
	// is never smaller than what Sample() could return anywhere inside the rectangle. Used to ask "is any of this tile wet?".
	float MaxDepthIn(float localMinX, float localMaxX, float localMinZ, float localMaxZ) const
	{
		if (depth_.empty()) return kDeepWater;
		auto toTexel = [](float local, int count) { return std::clamp(local * 0.5f + 0.5f, 0.0f, 1.0f) * static_cast<float>(count); };
		const int x0 = std::clamp(static_cast<int>(std::floor(toTexel(localMinX, width_))) - 1, 0, width_ - 1);
		const int x1 = std::clamp(static_cast<int>(std::floor(toTexel(localMaxX, width_))) + 1, 0, width_ - 1);
		const int z0 = std::clamp(static_cast<int>(std::floor(toTexel(localMinZ, height_))) - 1, 0, height_ - 1);
		const int z1 = std::clamp(static_cast<int>(std::floor(toTexel(localMaxZ, height_))) + 1, 0, height_ - 1);
		float deepest = -1.0e30f;
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
				deepest = std::max(deepest, depth_[static_cast<size_t>(z) * width_ + x]);
		return deepest;
	}

private:
	int width_ = 0;
	int height_ = 0;
	std::vector<float> depth_;
};

// Fills a map by asking two questions of the world, for every texel:
//   toWorld(localX, localZ, worldX, worldZ)   where is this spot of the body in the world?
//   groundAt(worldX, worldZ, groundY) -> bool the height of the ground there (false = no ground: deep water)
// The two are passed in, so this code knows nothing about the terrain or the body's transform.
template <class ToWorld, class GroundAt>
inline DepthField Bake(int width, int height, float waterLevel, ToWorld&& toWorld, GroundAt&& groundAt)
{
	std::vector<float> depth(static_cast<size_t>(width) * static_cast<size_t>(height), kDeepWater);
	for (int z = 0; z < height; ++z) {
		const float localZ = (static_cast<float>(z) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
		for (int x = 0; x < width; ++x) {
			const float localX = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
			float worldX = 0.0f, worldZ = 0.0f, groundY = 0.0f;
			toWorld(localX, localZ, worldX, worldZ);
			if (groundAt(worldX, worldZ, groundY)) {
				// Clamp so one deep trench cannot dominate the bilinear blend with its neighbours.
				depth[static_cast<size_t>(z) * width + x] = std::min(waterLevel - groundY, kDeepWater);
			}
		}
	}
	DepthField field;
	field.Reset(width, height, std::move(depth));
	return field;
}

}   // namespace WaterShore
