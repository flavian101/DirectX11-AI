#pragma once
// A river as a CURVE (lesson 38). Pure logic, no Direct3D: tested on its own (tests/river_test.cpp).
//
// The author places a few control points; the river passes through every one of them (a Catmull-Rom spline), is `width` wide
// there, and runs from the first point (upstream) to the last (downstream). Everything else in the river system asks the
// same question of this curve: "I am at (x, z): how far along the river am I, which side of the middle, which way does the
// water run here?" That is Closest().
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

// One control point. x and z are in the water body's own frame: world units from the body's centre, before the body is turned,
// so the whole river moves and turns with the body. Saved with the scene.
struct RiverPoint {
	float x = 0.0f;
	float z = 0.0f;
	float width = 8.0f;   // the water's width here (bank to bank), world units
	float level = 0.0f;   // the water's HEIGHT here, relative to the body's own level (world units, negative = lower). 0 everywhere = a level river (lesson 41)
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
	int span = 0;                   // which stretch of control points it lies on: between point `span` and point `span + 1`
	float level = 0.0f;             // the water's height here relative to the body's level (from RiverPoint::level)
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
	float qx = 0.0f, qz = 0.0f;     // the nearest point itself, on the middle line
	float level = 0.0f;             // the water's height at the nearest point, relative to the body's level
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
		if (i < 0) return { 2.0f * points[0].x - points[1].x, 2.0f * points[0].z - points[1].z, points[0].width, points[0].level };   // mirror the end: no kink there
		if (i >= n) return { 2.0f * points[n - 1].x - points[n - 2].x, 2.0f * points[n - 1].z - points[n - 2].z, points[n - 1].width, points[n - 1].level };
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
			s.span = i;
			s.level = p1.level + (p2.level - p1.level) * t;   // straight between the two points: it can never overshoot and run uphill
			out.push_back(s);
		}
	}
	Sample last;
	last.x = points[static_cast<size_t>(n - 1)].x;
	last.z = points[static_cast<size_t>(n - 1)].z;
	last.halfWidth = 0.5f * std::clamp(points[static_cast<size_t>(n - 1)].width, kMinWidth, kMaxWidth);
	last.span = n - 2;
	last.level = points[static_cast<size_t>(n - 1)].level;
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
inline float TightestBend(const std::vector<Sample>& path, int& index)
{
	float best = 1.0e9f;
	index = -1;
	for (size_t i = 1; i + 1 < path.size(); ++i) {
		const Sample& a = path[i - 1];
		const Sample& b = path[i + 1];
		const float run = b.s - a.s;
		if (run < 1e-4f) continue;
		const float turn = std::atan2(std::abs(a.tx * b.tz - a.tz * b.tx), a.tx * b.tx + a.tz * b.tz);   // how far the direction turns over `run`
		if (turn < 1e-5f) continue;
		const float ratio = (run / turn) / std::max(path[i].halfWidth, 1e-3f);
		if (ratio < best) { best = ratio; index = static_cast<int>(i); }
	}
	return best;
}

inline float TightestBendRatio(const std::vector<Sample>& path)
{
	int index = -1;
	return TightestBend(path, index);
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
	best.qx = qx;
	best.qz = qz;
	best.distance = std::sqrt(bestD2);
	best.signedDistance = (tx * (z - qz) - tz * (x - qx) >= 0.0f) ? best.distance : -best.distance;
	best.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * bestT;
	best.across = best.signedDistance / std::max(best.halfWidth, 1e-3f);
	best.s = a.s + (b.s - a.s) * bestT;
	best.level = a.level + (b.level - a.level) * bestT;
	best.tx = tx;
	best.tz = tz;
	return best;
}

// ---- Drawing a river (lesson 39) -------------------------------------------------------------------------------------------
//
// Where does a click at (x, z) put a new control point? Clicks used to always join the downstream end, so clicking a river
// out in the order the mouse happened to go made the curve double back on itself. Now the click decides:
//   * on the water of the river (nearer the middle line than half its width): INSERT there, onto the middle line, between the
//     two control points it lies between. The curve keeps its shape; the new point is a handle to bend it.
//   * anywhere else: EXTEND the end of the river that is nearer, whether that is the downstream end (append) or the upstream
//     end (prepend).
// A click past an end (nearest to the very first or last sample) always extends that end, even right beside it.
struct Placement {
	enum class Kind { Append, Prepend, Insert };
	Kind kind = Kind::Append;
	size_t index = 0;               // where the new point goes in the list of control points
	RiverPoint point;               // its position (on the middle line for Insert, the click otherwise) and width
};

inline Placement Place(const std::vector<RiverPoint>& points, const std::vector<Sample>& path, float x, float z, float defaultWidth = 8.0f)
{
	Placement placement;
	placement.point = { x, z, defaultWidth };
	placement.kind = Placement::Kind::Append;
	placement.index = points.size();
	if (points.empty()) return placement;
	placement.point.width = points.back().width;
	placement.point.level = points.back().level;   // a new end starts level with its neighbour; the author bends it from there
	if (points.size() < 2 || path.size() < 2) {
		return placement;   // one point: there is no "other end" yet
	}
	const Hit hit = Closest(path, x, z);
	const float length = Length(path);
	const bool pastStart = hit.s < 1e-3f;
	const bool pastEnd = hit.s > length - 1e-3f;
	if (hit.found && !pastStart && !pastEnd && hit.distance <= hit.halfWidth) {
		int span = 0;
		for (size_t i = 0; i + 1 < path.size(); ++i) {   // the stretch of control points the nearest spot lies on
			if (path[i + 1].s >= hit.s) { span = path[i].span; break; }
		}
		span = std::clamp(span, 0, static_cast<int>(points.size()) - 2);
		placement.kind = Placement::Kind::Insert;
		placement.index = static_cast<size_t>(span) + 1;
		placement.point = { hit.qx, hit.qz, 2.0f * hit.halfWidth, hit.level };
		return placement;
	}
	const RiverPoint& first = points.front();
	const RiverPoint& last = points.back();
	const float toFirst = std::hypot(x - first.x, z - first.z);
	const float toLast = std::hypot(x - last.x, z - last.z);
	if (toFirst < toLast) {
		placement.kind = Placement::Kind::Prepend;
		placement.index = 0;
		placement.point.width = first.width;
		placement.point.level = first.level;
	}
	return placement;
}

// What is wrong with the curve as drawn, for the viewport to mark while the author is still placing points.
struct Problems {
	int tightIndex = -1;            // a sample (index in the path) where the bend is tighter than the river is wide, or -1
	float tightRatio = 1.0e9f;      // that bend's radius / half width
	int overlapA = -1, overlapB = -1;   // two samples from different stretches of the river whose water lies on top of itself, or -1
	bool Any() const { return tightIndex >= 0 || overlapA >= 0; }
};

inline Problems Analyse(const std::vector<Sample>& path)
{
	Problems out;
	if (path.size() < 3) return out;
	int index = -1;
	const float ratio = TightestBend(path, index);
	if (index >= 0 && ratio < 1.0f) { out.tightIndex = index; out.tightRatio = ratio; }

	// Two parts of the river that are far apart ALONG it (more than half a turn of a bend as wide as the river) but their
	// water overlaps ACROSS: the river has doubled back or crossed itself. A tight bend is the same thing seen from one
	// side, and is reported above, not here. At most ~500 samples are compared, however long the river is.
	const size_t stride = std::max<size_t>(1, path.size() / 500);
	float worst = 0.95f;   // report only when the centre lines are closer than 95% of the two half widths together
	for (size_t i = 0; i < path.size(); i += stride) {
		for (size_t j = i + stride; j < path.size(); j += stride) {
			const Sample& a = path[i];
			const Sample& b = path[j];
			const float along = b.s - a.s;
			const float reach = a.halfWidth + b.halfWidth;
			if (along < 3.14159265f * std::max(a.halfWidth, b.halfWidth)) continue;
			const float dx = b.x - a.x, dz = b.z - a.z;
			if (dx * dx + dz * dz >= reach * reach * worst * worst) continue;
			const float r = std::sqrt(dx * dx + dz * dz) / std::max(reach, 1e-3f);
			if (r < worst) { worst = r; out.overlapA = static_cast<int>(i); out.overlapB = static_cast<int>(j); }
		}
	}
	return out;
}

// ---- A river that falls (lesson 41) ------------------------------------------------------------------------------------------
//
// Sets the water's level at every control point to the ground under it, but never higher than the point before it: a river
// runs downhill, so where the ground rises the water stays level and the carve digs the channel through. `ground[i]` is the
// ground's height (world units) at control point i; `baseLevel` is the body's own level, which the point levels are measured
// from; `sink` is how far below the ground the surface sits.
inline void FollowGround(std::vector<RiverPoint>& points, const std::vector<float>& ground, float baseLevel, float sink = 0.0f)
{
	if (ground.size() != points.size()) return;
	float level = 1.0e30f;
	for (size_t i = 0; i < points.size(); ++i) {
		level = std::min(level, ground[i] - sink);
		points[i].level = level - baseLevel;
	}
}

// ---- Letting the ground draw the river (lesson 45) ---------------------------------------------------------------------------
//
// Water runs downhill, so given a spring and the shape of the ground the whole river can be found instead of drawn: from where
// it stands, look around, step toward the steepest way down (with a little momentum so it does not zigzag), repeat until it
// reaches the sea, the edge of the terrain or a pit. Then the many small steps are smoothed and thinned into a few control
// points that the author can still grab and move. Pure logic: the ground is a function, so a test can give it any shape.

// The ground's height at (x, z); false where there is no ground (off the terrain). Any frame will do, as long as the caller
// uses the same one for the start and for the points that come back.
using GroundFn = std::function<bool(float x, float z, float& y)>;

struct RouteParams {
	float step = 4.0f;                  // distance between probes while walking downhill, world units
	float stopHeight = -1.0e30f;        // the walk ends once the ground is at or below this height (the sea)
	float maxLength = 1500.0f;          // never longer than this, world units (a water plane is at most 2000 across: WaterSettings::kSizeMax)
	float maxClimb = 1.0f;              // a bump this high is crossed (in total, over a run of steps that do not go down); more is a pit wall
	float startWidth = 4.0f;            // the river is this wide at the spring...
	float endWidth = 14.0f;             // ...and this wide at its mouth (rivers widen downstream)
	int maxPoints = 40;                 // control points returned at most (below kMaxPoints, which leaves room to add more by hand)
};

enum class RouteEnd {
	NoGround,       // the spring is not on the ground at all
	Sea,            // reached stopHeight
	EdgeOfTerrain,  // ran off the terrain
	Pit,            // nowhere lower to go (or closed in by its own water): a basin, where a lake would form
	TooLong         // hit maxLength
};

struct Route {
	std::vector<RiverPoint> points;     // control points: x, z and width set, level 0 (Follow terrain sets the fall)
	RouteEnd end = RouteEnd::NoGround;
	float length = 0.0f;                // along the control points, world units
	float fall = 0.0f;                  // ground height at the spring minus ground height at the mouth
};

// Three-point average, repeated: takes the zigzag out of a walk that moved in fixed steps. The two ends stay where they are.
inline void Smooth(std::vector<RiverPoint>& points, int passes)
{
	if (points.size() < 3) return;
	std::vector<RiverPoint> copy;
	for (int pass = 0; pass < passes; ++pass) {
		copy = points;
		for (size_t i = 1; i + 1 < points.size(); ++i) {
			points[i].x = 0.25f * copy[i - 1].x + 0.5f * copy[i].x + 0.25f * copy[i + 1].x;
			points[i].z = 0.25f * copy[i - 1].z + 0.5f * copy[i].z + 0.25f * copy[i + 1].z;
		}
	}
}

// Fewer points, same shape (Douglas-Peucker): keeps the point farthest from the straight line between two kept points, as long
// as it is farther than `tolerance`, and splits there. The two ends are always kept. If more than `maxPoints` survive, the
// tolerance grows until they fit, so the result is always usable.
inline std::vector<RiverPoint> Simplify(const std::vector<RiverPoint>& dense, float tolerance, int maxPoints)
{
	const int n = static_cast<int>(dense.size());
	maxPoints = std::max(maxPoints, 2);
	if (n <= 2) return dense;
	tolerance = std::max(tolerance, 1e-3f);
	for (int attempt = 0; attempt < 32; ++attempt) {
		std::vector<char> keep(static_cast<size_t>(n), 0);
		keep.front() = 1;
		keep.back() = 1;
		std::vector<std::pair<int, int>> work = { { 0, n - 1 } };
		while (!work.empty()) {
			const std::pair<int, int> range = work.back();
			work.pop_back();
			const int a = range.first, b = range.second;
			if (b - a < 2) continue;
			const float ax = dense[static_cast<size_t>(a)].x, az = dense[static_cast<size_t>(a)].z;
			const float ex = dense[static_cast<size_t>(b)].x - ax, ez = dense[static_cast<size_t>(b)].z - az;
			const float length2 = ex * ex + ez * ez;
			float worst = 0.0f;
			int at = -1;
			for (int i = a + 1; i < b; ++i) {
				const float px = dense[static_cast<size_t>(i)].x - ax, pz = dense[static_cast<size_t>(i)].z - az;
				const float t = length2 > 1e-12f ? std::clamp((px * ex + pz * ez) / length2, 0.0f, 1.0f) : 0.0f;
				const float d = std::hypot(px - ex * t, pz - ez * t);
				if (d > worst) { worst = d; at = i; }
			}
			if (at >= 0 && worst > tolerance) {
				keep[static_cast<size_t>(at)] = 1;
				work.push_back({ a, at });
				work.push_back({ at, b });
			}
		}
		std::vector<RiverPoint> out;
		for (int i = 0; i < n; ++i) if (keep[static_cast<size_t>(i)]) out.push_back(dense[static_cast<size_t>(i)]);
		if (static_cast<int>(out.size()) <= maxPoints) return out;
		tolerance *= 1.5f;
	}
	std::vector<RiverPoint> out;   // (cannot happen in practice) even spacing as a last resort
	for (int i = 0; i < maxPoints; ++i) out.push_back(dense[static_cast<size_t>(static_cast<long long>(i) * (n - 1) / (maxPoints - 1))]);
	return out;
}

// Walks downhill from (startX, startZ) and returns the river it found. See the comment above for the idea.
inline Route FlowDownhill(const GroundFn& ground, float startX, float startZ, const RouteParams& params)
{
	Route route;
	float groundHere = 0.0f;
	if (!ground || !ground(startX, startZ, groundHere)) return route;   // end stays NoGround
	const float groundAtStart = groundHere;
	const float step = std::max(params.step, 0.5f);
	const int maxSteps = std::clamp(static_cast<int>(params.maxLength / step), 8, 2000);
	constexpr float kTwoPi = 6.28318530718f;
	constexpr float kFlat = 0.002f;     // a drop smaller than this per unit of run is flat
	constexpr int kMaxStale = 12;       // this many steps without finding lower ground than ever before: it is a basin, not a river

	std::vector<RiverPoint> dense;
	dense.push_back({ startX, startZ });
	float x = startX, z = startZ, headX = 0.0f, headZ = 0.0f;
	bool haveHeading = false;
	int stale = 0;                       // steps since the lowest ground so far was found
	size_t lowestIndex = 0;              // where in `dense` that lowest ground is: a river that gives up ends THERE, not where it wandered to
	float lowestY = groundHere;
	float climbed = 0.0f;                // how much higher the ground got across the current run of flat or rising steps
	route.end = RouteEnd::TooLong;

	for (int index = 0; index < maxSteps; ++index) {
		if (groundHere <= params.stopHeight) { route.end = RouteEnd::Sea; break; }

		// Look all the way round, 24 directions, the ring turned so that one of them is straight ahead. (Going back onto its own
		// water is refused below, so no cone is needed: a river that overshoots the bottom of a bowl can turn back.)
		const int count = 24;
		const float baseAngle = haveHeading ? std::atan2(headZ, headX) : 0.0f;
		bool found = false, offTerrain = false;
		float bestScore = -1.0e30f, bestSlope = 0.0f, bestX = 0.0f, bestZ = 0.0f, bestDx = 0.0f, bestDz = 0.0f, bestGround = 0.0f;
		for (int k = 0; k < count; ++k) {
			const float angle = baseAngle + static_cast<float>(k) * (kTwoPi / 24.0f);
			const float dx = std::cos(angle), dz = std::sin(angle);
			const float qx = x + dx * step, qz = z + dz * step;
			float qy = 0.0f;
			if (!ground(qx, qz, qy)) { offTerrain = true; continue; }
			float farY = qy;   // a second look, twice as far, so a small bump is not mistaken for the end of the slope
			if (!ground(x + dx * 2.0f * step, z + dz * 2.0f * step, farY)) farY = qy;
			const float slope = 0.6f * (groundHere - qy) / step + 0.4f * (groundHere - farY) / (2.0f * step);
			const float score = slope + 0.03f * (dx * headX + dz * headZ);   // a little momentum (zero at the spring: no heading yet)
			if (score <= bestScore) continue;   // cannot win: skip the (slower) look at the river's own water
			bool loops = false;   // would this step land on the river's own earlier water?
			const float closest = 0.75f * step;
			for (size_t i = 0; i < dense.size() && !loops; ++i) {
				const float ox = dense[i].x - qx, oz = dense[i].z - qz;
				loops = ox * ox + oz * oz < closest * closest;
			}
			if (loops) continue;
			bestScore = score; bestSlope = slope; bestX = qx; bestZ = qz; bestDx = dx; bestDz = dz; bestGround = qy; found = true;
		}
		if (!found) { route.end = offTerrain ? RouteEnd::EdgeOfTerrain : RouteEnd::Pit; break; }   // closed in by its own water: a basin
		if (offTerrain && bestSlope < kFlat) { route.end = RouteEnd::EdgeOfTerrain; break; }   // the way down is off the map

		if (bestSlope < kFlat) {
			// Flat or uphill: a small bump is crossed, but a rise that keeps adding up is the wall of a basin.
			climbed += std::max(0.0f, bestGround - groundHere);
			if (climbed > params.maxClimb) { route.end = RouteEnd::Pit; break; }
		} else {
			climbed = 0.0f;
		}
		x = bestX;
		z = bestZ;
		groundHere = bestGround;
		dense.push_back({ x, z });
		if (groundHere < lowestY - 0.02f) { lowestY = groundHere; lowestIndex = dense.size() - 1; stale = 0; }
		else if (++stale > kMaxStale) { route.end = RouteEnd::Pit; break; }   // wandering around the bottom of a basin, or crossing flat ground
		if (!haveHeading) { headX = bestDx; headZ = bestDz; haveHeading = true; }
		else {
			headX = 0.6f * headX + 0.4f * bestDx;
			headZ = 0.6f * headZ + 0.4f * bestDz;
			const float length = std::hypot(headX, headZ);
			if (length > 1e-6f) { headX /= length; headZ /= length; }
		}
	}

	if (route.end == RouteEnd::Pit) dense.resize(lowestIndex + 1);   // the river ends at the bottom it found
	if (dense.size() < 2) { route.points.clear(); return route; }
	Smooth(dense, 3);
	route.points = Simplify(dense, 0.5f * step, std::min(params.maxPoints, kMaxPoints));

	// Wider downstream: the width grows with the distance along the river.
	float total = 0.0f;
	for (size_t i = 1; i < route.points.size(); ++i) total += std::hypot(route.points[i].x - route.points[i - 1].x, route.points[i].z - route.points[i - 1].z);
	float walked = 0.0f;
	for (size_t i = 0; i < route.points.size(); ++i) {
		if (i > 0) walked += std::hypot(route.points[i].x - route.points[i - 1].x, route.points[i].z - route.points[i - 1].z);
		const float t = total > 1e-4f ? walked / total : 0.0f;
		route.points[i].width = std::clamp(params.startWidth + (params.endWidth - params.startWidth) * t, kMinWidth, kMaxWidth);
		route.points[i].level = 0.0f;
	}
	route.length = total;
	float groundAtEnd = groundAtStart;
	if (ground(route.points.back().x, route.points.back().z, groundAtEnd)) route.fall = groundAtStart - groundAtEnd;
	return route;
}

}   // namespace RiverPath
