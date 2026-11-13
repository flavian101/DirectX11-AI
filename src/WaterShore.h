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
		return Bilinear(depth_, localX, localZ);
	}

	// The direction of the nearest land, per texel, in WORLD space (lesson 35): (wx, wz) is a unit vector pointing from the
	// water TOWARD the land, multiplied by how sure we are of it (0 where the bottom is flat, 1 on a clear slope). Premultiplying
	// matters: blending two texels that point opposite ways (a headland, a narrow channel) then cancels to "not sure" instead
	// of inventing a direction. Empty until ComputeShoreDirections fills it.
	bool HasDirections() const { return !towardX_.empty(); }
	const std::vector<float>& TowardX() const { return towardX_; }
	const std::vector<float>& TowardZ() const { return towardZ_; }
	void SetDirections(std::vector<float> towardX, std::vector<float> towardZ)
	{
		const size_t n = static_cast<size_t>(width_) * static_cast<size_t>(height_);
		if (towardX.size() != n || towardZ.size() != n) return;
		towardX_ = std::move(towardX);
		towardZ_ = std::move(towardZ);
	}
	// Bilinear read of the direction channels: the same blend as Sample(), so the shader (which reads all of them in one
	// fetch) and the CPU agree.
	void SampleToward(float localX, float localZ, float& wx, float& wz) const
	{
		wx = wz = 0.0f;
		if (towardX_.empty()) return;
		wx = Bilinear(towardX_, localX, localZ);
		wz = Bilinear(towardZ_, localX, localZ);
	}
	// What the shader's texture holds: four floats per texel, row by row: depth, toward-land x, toward-land z, unused.
	std::vector<float> PackedTexels() const
	{
		std::vector<float> out(depth_.size() * 4, 0.0f);
		for (size_t i = 0; i < depth_.size(); ++i) {
			out[i * 4 + 0] = depth_[i];
			if (!towardX_.empty()) { out[i * 4 + 1] = towardX_[i]; out[i * 4 + 2] = towardZ_[i]; }
		}
		return out;
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
	float Bilinear(const std::vector<float>& values, float localX, float localZ) const
	{
		const float px = std::clamp((localX * 0.5f + 0.5f), 0.0f, 1.0f) * static_cast<float>(width_) - 0.5f;
		const float pz = std::clamp((localZ * 0.5f + 0.5f), 0.0f, 1.0f) * static_cast<float>(height_) - 0.5f;
		const float fx0 = std::floor(px), fz0 = std::floor(pz);
		const float tx = px - fx0, tz = pz - fz0;
		const int x0 = static_cast<int>(fx0), z0 = static_cast<int>(fz0);
		auto at = [&](int x, int z) {
			x = std::clamp(x, 0, width_ - 1);
			z = std::clamp(z, 0, height_ - 1);
			return values[static_cast<size_t>(z) * width_ + x];
		};
		const float d00 = at(x0, z0), d10 = at(x0 + 1, z0), d01 = at(x0, z0 + 1), d11 = at(x0 + 1, z0 + 1);
		const float a = d00 + (d10 - d00) * tx;
		const float b = d01 + (d11 - d01) * tx;
		return a + (b - a) * tz;
	}

	int width_ = 0;
	int height_ = 0;
	std::vector<float> depth_;
	std::vector<float> towardX_, towardZ_;   // premultiplied unit direction toward land (empty = not computed)
};

// How far (in depth units) the bottom is allowed to matter when finding the shore direction: deeper than this counts as
// "open water". Without it one deep trench would swamp the slope of the beach next to it.
constexpr float kShelterRange = 25.0f;

// Finds, for every texel, which way the nearest land lies, and how sure we are (lesson 35).
// (exX, exZ) = how far one texel step in local +x moves you in the WORLD (x and z), (ezX, ezZ) = the same for local +z. The
// body may be turned and scaled; this is what lets the answer come out in world space whatever its transform.
// The slope of the (clamped, smoothed) bottom points from shallow to deep, so the land lies the OTHER way. Smoothing over
// about `smoothingWorld` units keeps the answer to the broad shore, not to every bump of the terrain.
inline void ComputeShoreDirections(DepthField& field, float exX, float exZ, float ezX, float ezZ, float smoothingWorld = 12.0f)
{
	const int w = field.Width(), h = field.Height();
	if (w < 3 || h < 3) return;
	const float det = exX * ezZ - exZ * ezX;
	if (std::abs(det) < 1e-9f) return;
	const float texelWorld = std::max(0.5f * (std::hypot(exX, exZ) + std::hypot(ezX, ezZ)), 1e-4f);
	const int radius = std::clamp(static_cast<int>(std::lround(smoothingWorld / texelWorld)), 1, 12);

	// 1. Clamp the depth (so deep water is "flat"), 2. smooth it with two box blurs (close to a Gaussian).
	const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
	std::vector<float> a(count), b(count);
	for (size_t i = 0; i < count; ++i) a[i] = std::clamp(field.Data()[i], -kShelterRange, kShelterRange);
	auto blur = [&](std::vector<float>& in, std::vector<float>& out) {
		// A box blur by a running sum: each step adds the texel entering the window and drops the one leaving it, so the cost
		// per texel does not grow with the radius. Both ends clamp (the edge texel repeats). Horizontal into `out`, then vertical
		// back into `in`.
		const float inv = 1.0f / static_cast<float>(2 * radius + 1);
		for (int z = 0; z < h; ++z) {
			const float* row = &in[static_cast<size_t>(z) * w];
			float* dst = &out[static_cast<size_t>(z) * w];
			float sum = 0.0f;
			for (int k = -radius; k <= radius; ++k) sum += row[std::clamp(k, 0, w - 1)];
			for (int x = 0; x < w; ++x) {
				dst[x] = sum * inv;
				sum += row[std::clamp(x + radius + 1, 0, w - 1)] - row[std::clamp(x - radius, 0, w - 1)];
			}
		}
		for (int x = 0; x < w; ++x) {
			float sum = 0.0f;
			for (int k = -radius; k <= radius; ++k) sum += out[static_cast<size_t>(std::clamp(k, 0, h - 1)) * w + x];
			for (int z = 0; z < h; ++z) {
				in[static_cast<size_t>(z) * w + x] = sum * inv;
				sum += out[static_cast<size_t>(std::clamp(z + radius + 1, 0, h - 1)) * w + x] - out[static_cast<size_t>(std::clamp(z - radius, 0, h - 1)) * w + x];
			}
		}
	};
	blur(a, b);
	blur(a, b);

	// 3. The slope, in texel steps, then in the world: M * g = G with M's rows the two texel-step vectors.
	std::vector<float> towardX(count, 0.0f), towardZ(count, 0.0f);
	auto at = [&](int x, int z) { return a[static_cast<size_t>(std::clamp(z, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
	for (int z = 0; z < h; ++z) {
		for (int x = 0; x < w; ++x) {
			const float gx = 0.5f * (at(x + 1, z) - at(x - 1, z));
			const float gz = 0.5f * (at(x, z + 1) - at(x, z - 1));
			const float worldGx = (gx * ezZ - exZ * gz) / det;   // depth change per world unit, along world x
			const float worldGz = (exX * gz - ezX * gx) / det;
			const float slope = std::hypot(worldGx, worldGz);
			if (slope < 1e-6f) continue;
			// Sure of the direction from a 2% slope (barely there) to a 10% slope (a clear beach).
			const float t = std::clamp((slope - 0.02f) / 0.08f, 0.0f, 1.0f);
			const float confidence = t * t * (3.0f - 2.0f * t);
			const size_t i = static_cast<size_t>(z) * w + x;
			towardX[i] = -worldGx / slope * confidence;   // depth falls toward the land
			towardZ[i] = -worldGz / slope * confidence;
		}
	}
	field.SetDirections(std::move(towardX), std::move(towardZ));
}

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
	// Which way the land lies, in world space: from how far one texel step moves you in the world.
	float ox = 0.0f, oz = 0.0f, ax = 0.0f, az = 0.0f, bx = 0.0f, bz = 0.0f;
	toWorld(0.0f, 0.0f, ox, oz);
	toWorld(2.0f / static_cast<float>(width), 0.0f, ax, az);
	toWorld(0.0f, 2.0f / static_cast<float>(height), bx, bz);
	ComputeShoreDirections(field, ax - ox, az - oz, bx - ox, bz - oz);
	return field;
}

}   // namespace WaterShore
