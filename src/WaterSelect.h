#pragma once
// Selecting a water body by clicking it, and drawing it as selected (lesson 42). Pure maths: no Direct3D, no ImGui.
//
// A water body is not a box. A lake is a flat sheet, a river is a ribbon that follows a curve across a much bigger rectangle,
// and a river that falls is a sheet at a different height at every point. So it is picked where its SURFACE is: the ray meets
// the surface, and the spot it meets must be water (inside the river's corridor, over wet ground). Clicking the empty part of
// a river's rectangle, or the dry beach of a lake, selects nothing.
#include "RiverPath.h"
#include <cmath>
#include <vector>

namespace WaterSelect {

// Where a ray (origin o, unit direction d) crosses the horizontal plane y = planeY: how far along the ray (t) and where (x, z).
// False when the ray runs parallel to the plane or the plane lies behind it.
inline bool RayMeetsHeight(const float o[3], const float d[3], float planeY, float& t, float& x, float& z)
{
	if (std::fabs(d[1]) < 1e-6f) return false;
	t = (planeY - o[1]) / d[1];
	if (t <= 0.0f) return false;
	x = o[0] + d[0] * t;
	z = o[2] + d[2] * t;
	return true;
}

// Where a ray meets a surface whose height is `baseY + levelAt(x, z)` (a river that falls is not level), provided the spot is
// `accept`ed (it is really water). Starts on the level plane and re-aims at the height the surface has where that landed; for
// a gentle fall this settles in a few steps. On a very steep fall seen at a shallow angle it may not settle: the last guess is
// used, which is off by a little, not wrong by a lot. `distance` = how far along the ray the surface is.
template <class LevelAt, class Accept>
inline bool RayMeetsSurface(const float o[3], const float d[3], float baseY, LevelAt&& levelAt, Accept&& accept, float& distance)
{
	float y = baseY, t = 0.0f, x = 0.0f, z = 0.0f;
	for (int i = 0; i < 8; ++i) {
		if (!RayMeetsHeight(o, d, y, t, x, z)) return false;
		const float next = baseY + levelAt(x, z);
		const bool settled = std::fabs(next - y) < 1e-3f;
		y = next;
		if (settled) break;
	}
	if (!RayMeetsHeight(o, d, y, t, x, z)) return false;
	if (!accept(x, z)) return false;
	distance = t;
	return true;
}

struct Spot { float x = 0.0f, z = 0.0f; };

// The edge of the plane in the body's own frame (world units from its centre, before it is turned): the four corners of a
// rectangle, or `segments` points round an ellipse.
inline std::vector<Spot> PlaneOutline(bool ellipse, float halfX, float halfZ, int segments = 48)
{
	std::vector<Spot> out;
	if (!ellipse) {
		out = { { -halfX, -halfZ }, { halfX, -halfZ }, { halfX, halfZ }, { -halfX, halfZ } };
		return out;
	}
	segments = std::max(segments, 8);
	for (int i = 0; i < segments; ++i) {
		const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
		out.push_back({ std::cos(a) * halfX, std::sin(a) * halfZ });
	}
	return out;
}

struct BankPoint { float x = 0.0f, z = 0.0f, level = 0.0f; };

// The two edges of a river's water as lines, a half width to either side of the middle line (positions as the path has them;
// `level` = the water's height relative to the body's, so the line can be drawn on a river that falls). Every `stride`th sample
// is used, always including the last, so a long river is not drawn with thousands of segments.
inline void RiverBanks(const std::vector<RiverPath::Sample>& path, int stride, std::vector<BankPoint>& left, std::vector<BankPoint>& right)
{
	left.clear();
	right.clear();
	if (path.size() < 2) return;
	stride = std::max(stride, 1);
	auto add = [&](const RiverPath::Sample& s) {
		const float nx = -s.tz, nz = s.tx;   // sideways, to the left of the flow
		left.push_back({ s.x + nx * s.halfWidth, s.z + nz * s.halfWidth, s.level });
		right.push_back({ s.x - nx * s.halfWidth, s.z - nz * s.halfWidth, s.level });
	};
	for (size_t i = 0; i < path.size(); i += static_cast<size_t>(stride)) add(path[i]);
	if ((path.size() - 1) % static_cast<size_t>(stride) != 0) add(path.back());
}

}   // namespace WaterSelect
