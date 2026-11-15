#pragma once
// A river as a CURVE (lesson 38). Pure logic, no Direct3D: tested on its own (tests/river_test.cpp).
//
// The author places a few control points; the river passes through every one of them (a Catmull-Rom spline), is `width` wide
// there, and runs from the first point (upstream) to the last (downstream). Everything else in the river system asks the
// same question of this curve: "I am at (x, z): how far along the river am I, which side of the middle, which way does the
// water run here?" That is Closest().
#include <algorithm>
#include <cmath>
#include <vector>

// One control point. x and z are in the water body's own frame: world units from the body's centre, before the body is turned,
// so the whole river moves and turns with the body. Saved with the scene.
struct RiverPoint {
	float x = 0.0f;
	float z = 0.0f;
	float width = 8.0f;   // the water's width here (bank to bank), world units
};

namespace RiverPath {

constexpr int kMaxPoints = 64;
constexpr float kMinWidth = 1.0f, kMaxWidth = 200.0f;
constexpr int kMaxSamples = 4096;

// A point on the curve, found by walking along it.
struct Sample {
	float x = 0.0f, z = 0.0f;       // position
	float halfWidth = 4.0f;         // half of the width there
	float tx = 1.0f, tz = 0.0f;     // unit direction of the flow (downstream)
	float s = 0.0f;                 // distance travelled along the river from its start
};

// Where a spot is, measured against the curve.
struct Hit {
	bool found = false;
	float distance = 0.0f;          // to the middle line (always >= 0)
	float signedDistance = 0.0f;    // the same, + on one side of the flow and - on the other (the sign of cross(tangent, offset))
	float across = 0.0f;            // signedDistance / halfWidth: -1..1 is inside the water, beyond that is bank
	float s = 0.0f;                 // distance along the river of the nearest point
	float tx = 1.0f, tz = 0.0f;     // unit flow direction at the nearest point
	float halfWidth = 4.0f;
};

namespace detail {
inline float CatmullRom(float p0, float p1, float p2, float p3, float t)
{
	const float t2 = t * t, t3 = t2 * t;
	return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
}   // namespace detail

// The curve as a chain of samples about `step` world units apart (the spacing tightens where it has to, never exceeds the cap
// on the number of samples). Fewer than two points: empty. The first sample is the first point, the last is the last point.
inline std::vector<Sample> Build(const std::vector<RiverPoint>& points, float step)
{
	std::vector<Sample> out;
	const int n = static_cast<int>(std::min<size_t>(points.size(), kMaxPoints));
	if (n < 2) return out;
	step = std::max(step, 0.25f);
	auto point = [&](int i) -> RiverPoint {
		if (i < 0) return { 2.0f * points[0].x - points[1].x, 2.0f * points[0].z - points[1].z, points[0].width };   // mirror the end: no kink there
		if (i >= n) return { 2.0f * points[n - 1].x - points[n - 2].x, 2.0f * points[n - 1].z - points[n - 2].z, points[n - 1].width };
		return points[static_cast<size_t>(i)];
	};
	for (int i = 0; i < n - 1; ++i) {
		const RiverPoint p0 = point(i - 1), p1 = point(i), p2 = point(i + 1), p3 = point(i + 2);
		const float chord = std::hypot(p2.x - p1.x, p2.z - p1.z);
		const int pieces = std::clamp(static_cast<int>(std::ceil(chord / step)), 1, 256);
		for (int k = 0; k < pieces; ++k) {
			if (static_cast<int>(out.size()) >= kMaxSamples - 1) break;
			const float t = static_cast<float>(k) / static_cast<float>(pieces);
			Sample s;
			s.x = detail::CatmullRom(p0.x, p1.x, p2.x, p3.x, t);
			s.z = detail::CatmullRom(p0.z, p1.z, p2.z, p3.z, t);
			s.halfWidth = 0.5f * std::clamp(p1.width + (p2.width - p1.width) * t, kMinWidth, kMaxWidth);
			out.push_back(s);
		}
	}
	Sample last;
	last.x = points[static_cast<size_t>(n - 1)].x;
	last.z = points[static_cast<size_t>(n - 1)].z;
	last.halfWidth = 0.5f * std::clamp(points[static_cast<size_t>(n - 1)].width, kMinWidth, kMaxWidth);
	out.push_back(last);

	// Distance along, and the direction of travel (the average of the way in and the way out, so it turns smoothly).
	float travelled = 0.0f;
	for (size_t i = 0; i < out.size(); ++i) {
		if (i > 0) travelled += std::hypot(out[i].x - out[i - 1].x, out[i].z - out[i - 1].z);
		out[i].s = travelled;
		const Sample& a = out[i > 0 ? i - 1 : i];
		const Sample& b = out[i + 1 < out.size() ? i + 1 : i];
		float dx = b.x - a.x, dz = b.z - a.z;
		const float length = std::hypot(dx, dz);
		if (length > 1e-6f) { dx /= length; dz /= length; } else { dx = 1.0f; dz = 0.0f; }
		out[i].tx = dx;
		out[i].tz = dz;
	}
	return out;
}

inline float Length(const std::vector<Sample>& path) { return path.empty() ? 0.0f : path.back().s; }

// The sharpest bend of the curve, as (radius of the bend) / (half width of the river there). Below 1 the bend is tighter than
// the river is wide: on the inside of it, "how far across" and "how far along" fold over each other, so ripples and foam pinch
// (see the lesson). A straight river gives a huge number.
inline float TightestBendRatio(const std::vector<Sample>& path)
{
	float best = 1.0e9f;
	for (size_t i = 1; i + 1 < path.size(); ++i) {
		const Sample& a = path[i - 1];
		const Sample& b = path[i + 1];
		const float run = b.s - a.s;
		if (run < 1e-4f) continue;
		const float turn = std::atan2(std::abs(a.tx * b.tz - a.tz * b.tx), a.tx * b.tx + a.tz * b.tz);   // how far the direction turns over `run`
		if (turn < 1e-5f) continue;
		best = std::min(best, (run / turn) / std::max(path[i].halfWidth, 1e-3f));
	}
	return best;
}

// Which part of the curve is nearest to (x, z), and where the spot is relative to it.
inline Hit Closest(const std::vector<Sample>& path, float x, float z)
{
	Hit best;
	if (path.size() < 2) return best;
	float bestD2 = 3.402823466e+38f;
	size_t bestIndex = 0;
	float bestT = 0.0f;
	for (size_t i = 0; i + 1 < path.size(); ++i) {
		const Sample& a = path[i];
		const Sample& b = path[i + 1];
		const float ex = b.x - a.x, ez = b.z - a.z;
		const float length2 = ex * ex + ez * ez;
		const float t = length2 > 1e-12f ? std::clamp(((x - a.x) * ex + (z - a.z) * ez) / length2, 0.0f, 1.0f) : 0.0f;
		const float dx = x - (a.x + ex * t), dz = z - (a.z + ez * t);
		const float d2 = dx * dx + dz * dz;
		if (d2 < bestD2) { bestD2 = d2; bestIndex = i; bestT = t; }
	}
	const Sample& a = path[bestIndex];
	const Sample& b = path[bestIndex + 1];
	const float qx = a.x + (b.x - a.x) * bestT, qz = a.z + (b.z - a.z) * bestT;
	float tx = a.tx + (b.tx - a.tx) * bestT, tz = a.tz + (b.tz - a.tz) * bestT;
	const float tl = std::hypot(tx, tz);
	if (tl > 1e-6f) { tx /= tl; tz /= tl; } else { tx = a.tx; tz = a.tz; }
	best.found = true;
	best.distance = std::sqrt(bestD2);
	best.signedDistance = (tx * (z - qz) - tz * (x - qx) >= 0.0f) ? best.distance : -best.distance;
	best.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * bestT;
	best.across = best.signedDistance / std::max(best.halfWidth, 1e-3f);
	best.s = a.s + (b.s - a.s) * bestT;
	best.tx = tx;
	best.tz = tz;
	return best;
}

}   // namespace RiverPath
