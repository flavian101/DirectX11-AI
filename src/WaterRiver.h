#pragma once
// The flow map of a river (lesson 38): what the water shader needs to know about the curve at every texel of the body.
// Pure logic. The shader (shaders/water_ps.hlsl) reads these four numbers per texel; WaterRiver::Read is the CPU twin.
//
//   x = s   how far along the river this spot is (world units, from its start)
//   y = d   how far from the middle line, signed (world units)
//   z, w    the unit direction of the flow in the WORLD (x, z), times a strength: kStrengthFloor + (1 - kStrengthFloor) * profile
//           inside the river's corridor, 0 outside it. The strength carries two things in one number: "is there a river here"
//           (> 0) and how fast it runs across its width (profile: 1 in the middle, 0 at the bank).
//
// Ripples and foam are drawn in (d, s) coordinates, not in world x, z: on a bend that makes them follow the curve instead of
// being cut across it, and it needs no rotating frame (which would shear the pattern as the direction changes).
#include "RiverPath.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace WaterRiver {

constexpr float kStrengthFloor = 0.1f;   // even at a bank the direction is known
constexpr float kCorridorFull = 1.2f;    // out to this many half widths (measured from the middle) the river's data is used fully...
constexpr float kCorridorEnd = 1.8f;     // ...and fades to nothing by this many

inline float Smoothstep(float a, float b, float x)
{
	const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// How fast the river runs across its width: 1 in the middle, 0 at the bank. `across` is signedDistance / halfWidth.
inline float Profile(float across)
{
	const float c = std::min(std::abs(across), 1.0f);
	return 1.0f - c * c;
}

// What one texel holds, for a spot that RiverPath::Closest found.
struct Texel {
	float s = 0.0f, d = 0.0f, dirX = 0.0f, dirZ = 0.0f;
};

inline Texel MakeTexel(const RiverPath::Hit& hit)
{
	Texel t;
	if (!hit.found) return t;
	const float a = std::abs(hit.across);
	const float mask = 1.0f - Smoothstep(kCorridorFull, kCorridorEnd, a);
	const float strength = (kStrengthFloor + (1.0f - kStrengthFloor) * Profile(hit.across)) * mask;
	t.s = hit.s;
	t.d = hit.signedDistance;
	t.dirX = hit.tx * strength;
	t.dirZ = hit.tz * strength;
	return t;
}

// What the shader gets out of a texel: is there a river, which way, how fast across its width.
struct Read {
	bool inRiver = false;
	float dirX = 1.0f, dirZ = 0.0f;
	float profile = 0.0f;   // 0..1
};
inline Read Decode(const Texel& t)
{
	Read r;
	const float strength = std::hypot(t.dirX, t.dirZ);
	r.inRiver = strength > 0.05f;
	if (!r.inRiver) return r;
	r.dirX = t.dirX / strength;
	r.dirZ = t.dirZ / strength;
	r.profile = std::clamp((strength - kStrengthFloor) / (1.0f - kStrengthFloor), 0.0f, 1.0f);
	return r;
}

// The map for a body: width x height texels (the same texel layout as the depth map: texel centres at (i + 0.5) / count of the
// body's -1..1 space), four floats each: s, d, dirX, dirZ. `path` is in WORLD space.
//   toWorld(localX, localZ, worldX, worldZ)   a spot of the body to a spot of the world
//   toLocal(worldX, worldZ, localX, localZ)   and back
// Only texels near the river are asked about the curve: the rest stay empty.
template <class ToWorld, class ToLocal>
inline std::vector<float> Bake(int width, int height, const std::vector<RiverPath::Sample>& path, ToWorld&& toWorld, ToLocal&& toLocal)
{
	std::vector<float> texels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0.0f);
	if (path.size() < 2 || width < 1 || height < 1) return texels;
	struct Best { float d2 = 3.402823466e+38f; size_t segment = 0; float t = 0.0f; };
	std::vector<Best> best(static_cast<size_t>(width) * static_cast<size_t>(height));
	auto toTexel = [](float local, int count) { return (local * 0.5f + 0.5f) * static_cast<float>(count) - 0.5f; };

	for (size_t i = 0; i + 1 < path.size(); ++i) {
		const RiverPath::Sample& a = path[i];
		const RiverPath::Sample& b = path[i + 1];
		const float reach = std::max(a.halfWidth, b.halfWidth) * kCorridorEnd + 1.0f;
		// The piece's box in the world, grown by `reach`, as a box in the body's own space (the body may be turned).
		float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
		const float cx[2] = { std::min(a.x, b.x) - reach, std::max(a.x, b.x) + reach };
		const float cz[2] = { std::min(a.z, b.z) - reach, std::max(a.z, b.z) + reach };
		for (int ix = 0; ix < 2; ++ix)
			for (int iz = 0; iz < 2; ++iz) {
				float lx = 0.0f, lz = 0.0f;
				toLocal(cx[ix], cz[iz], lx, lz);
				minX = std::min(minX, lx); maxX = std::max(maxX, lx);
				minZ = std::min(minZ, lz); maxZ = std::max(maxZ, lz);
			}
		const int x0 = std::clamp(static_cast<int>(std::floor(toTexel(minX, width))) - 1, 0, width - 1);
		const int x1 = std::clamp(static_cast<int>(std::ceil(toTexel(maxX, width))) + 1, 0, width - 1);
		const int z0 = std::clamp(static_cast<int>(std::floor(toTexel(minZ, height))) - 1, 0, height - 1);
		const int z1 = std::clamp(static_cast<int>(std::ceil(toTexel(maxZ, height))) + 1, 0, height - 1);
		const float ex = b.x - a.x, ez = b.z - a.z;
		const float length2 = ex * ex + ez * ez;
		for (int z = z0; z <= z1; ++z) {
			const float localZ = (static_cast<float>(z) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
			for (int x = x0; x <= x1; ++x) {
				const float localX = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
				float wx = 0.0f, wz = 0.0f;
				toWorld(localX, localZ, wx, wz);
				const float t = length2 > 1e-12f ? std::clamp(((wx - a.x) * ex + (wz - a.z) * ez) / length2, 0.0f, 1.0f) : 0.0f;
				const float dx = wx - (a.x + ex * t), dz = wz - (a.z + ez * t);
				const float d2 = dx * dx + dz * dz;
				Best& cell = best[static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x)];
				if (d2 < cell.d2) { cell.d2 = d2; cell.segment = i; cell.t = t; }
			}
		}
	}
	for (int z = 0; z < height; ++z) {
		const float localZ = (static_cast<float>(z) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
		for (int x = 0; x < width; ++x) {
			const size_t index = static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x);
			if (best[index].d2 == 3.402823466e+38f) continue;
			const float localX = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
			float wx = 0.0f, wz = 0.0f;
			toWorld(localX, localZ, wx, wz);
			// Same answer as RiverPath::Closest, for the one piece that was nearest.
			const RiverPath::Sample& a = path[best[index].segment];
			const RiverPath::Sample& b = path[best[index].segment + 1];
			const float t = best[index].t;
			RiverPath::Hit hit;
			hit.found = true;
			const float qx = a.x + (b.x - a.x) * t, qz = a.z + (b.z - a.z) * t;
			float tx = a.tx + (b.tx - a.tx) * t, tz = a.tz + (b.tz - a.tz) * t;
			const float tl = std::hypot(tx, tz);
			if (tl > 1e-6f) { tx /= tl; tz /= tl; } else { tx = a.tx; tz = a.tz; }
			hit.distance = std::sqrt(best[index].d2);
			hit.signedDistance = (tx * (wz - qz) - tz * (wx - qx) >= 0.0f) ? hit.distance : -hit.distance;
			hit.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
			hit.across = hit.signedDistance / std::max(hit.halfWidth, 1e-3f);
			hit.s = a.s + (b.s - a.s) * t;
			hit.tx = tx;
			hit.tz = tz;
			const Texel texel = MakeTexel(hit);
			texels[index * 4 + 0] = texel.s;
			texels[index * 4 + 1] = texel.d;
			texels[index * 4 + 2] = texel.dirX;
			texels[index * 4 + 3] = texel.dirZ;
		}
	}
	return texels;
}

// ---- Where a curved river is water at all (lesson 40) ----------------------------------------------------------------------
//
// The flow map is empty beyond the river's corridor, and the pixel shader clips the water there (shaders/water_ps.hlsl). Every
// other part of the system has to agree with that: tiles that no part of the corridor reaches are not drawn, and a boat or the
// camera outside it is not on water. This is the CPU twin of that clip.
//
// The flow map's strength is (floor + (1 - floor) * profile) * mask. Out at the bank the profile is 0, so the strength is
// floor * mask, and "is there a river" (Decode: strength > 0.05) turns false where the mask falls to a half: the middle of the
// smoothstep between kCorridorFull and kCorridorEnd.
constexpr float kClipAcross = 0.5f * (kCorridorFull + kCorridorEnd);   // 1.5 half widths from the middle line

// Is this spot inside the river's corridor?
inline bool InCorridor(const std::vector<RiverPath::Sample>& path, float x, float z)
{
	const RiverPath::Hit hit = RiverPath::Closest(path, x, z);
	return hit.found && std::abs(hit.across) < kClipAcross;
}

// Can any spot of a round patch (centre, radius; world units) lie in the corridor? Conservative: false only when NONE can, so a
// tile that is skipped on this answer never hides water. With no curve there is no corridor to test, and everything counts.
inline bool CorridorReaches(const std::vector<RiverPath::Sample>& path, float cx, float cz, float radius)
{
	if (path.size() < 2) return true;
	for (size_t i = 0; i + 1 < path.size(); ++i) {
		const RiverPath::Sample& a = path[i];
		const RiverPath::Sample& b = path[i + 1];
		const float ex = b.x - a.x, ez = b.z - a.z;
		const float length2 = ex * ex + ez * ez;
		const float t = length2 > 1e-12f ? std::clamp(((cx - a.x) * ex + (cz - a.z) * ez) / length2, 0.0f, 1.0f) : 0.0f;
		const float dx = cx - (a.x + ex * t), dz = cz - (a.z + ez * t);
		// The widest the water is anywhere on this piece, so a spot whose nearest point is here cannot be missed.
		const float reach = kClipAcross * std::max(a.halfWidth, b.halfWidth) + radius;
		if (dx * dx + dz * dz <= reach * reach) return true;
	}
	return false;
}

}   // namespace WaterRiver
