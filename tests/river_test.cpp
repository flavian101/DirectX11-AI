// Unit tests for src/RiverPath.h and src/RiverCarve.cpp (lesson 38; they leave with the old terrain, roadmap T1). No Direct3D, no framework: a failed CHECK
// prints and the program exits 1.
//  - the curve passes through its control points, runs in order, and answers "where am I relative to it?" correctly
//  - the carve digs a channel that is deepest in the middle, level with the water at its edge, never raises ground, and is
//    exactly undone by its recorded patch
//  - (lesson 45) the router: a river found by walking downhill, thinned into control points, that stops at the sea, the edge or a pit
//  - the flow map: direction follows the curve, strength is 1 in the middle and falls to the bank, nothing far from the river,
//    and the fast bake agrees with the plain per-point question
#include "RiverCarve.h"
#include "RiverPath.h"
#include "WaterShore.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static float Random01(std::uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	return static_cast<float>(state >> 8) / 16777216.0f;
}

static void TestCurve()
{
	CHECK(RiverPath::Build({}, 1.0f).empty());
	CHECK(RiverPath::Build({ { 0, 0, 8 } }, 1.0f).empty());

	// A straight river: samples run in order along it, 1 unit or less apart, and know their direction.
	const std::vector<RiverPoint> straight = { { 0, -30, 8 }, { 0, 0, 8 }, { 0, 30, 8 } };
	const std::vector<RiverPath::Sample> line = RiverPath::Build(straight, 1.0f);
	CHECK(line.size() >= 60);
	CHECK(std::abs(line.front().z + 30.0f) < 1e-4f && std::abs(line.back().z - 30.0f) < 1e-4f);
	CHECK(std::abs(RiverPath::Length(line) - 60.0f) < 0.01f);
	for (size_t i = 1; i < line.size(); ++i) {
		CHECK(line[i].s > line[i - 1].s);
		CHECK(std::hypot(line[i].x - line[i - 1].x, line[i].z - line[i - 1].z) <= 1.0f + 1e-3f);
		CHECK(std::abs(line[i].x) < 1e-3f);                       // stays on its line
		CHECK(line[i].tz > 0.999f);                               // flows toward +z
		CHECK(std::abs(line[i].halfWidth - 4.0f) < 1e-3f);
	}

	// A bendy river passes THROUGH every control point (that is what makes it easy to author), and its width follows.
	const std::vector<RiverPoint> bendy = { { -40, -40, 6 }, { -10, -10, 10 }, { 15, 10, 14 }, { 20, 40, 8 }, { 50, 60, 6 } };
	const std::vector<RiverPath::Sample> curve = RiverPath::Build(bendy, 0.5f);
	for (const RiverPoint& p : bendy) {
		const RiverPath::Hit hit = RiverPath::Closest(curve, p.x, p.z);
		CHECK(hit.found && hit.distance < 0.3f);
		CHECK(std::abs(hit.halfWidth * 2.0f - p.width) < 0.3f);
	}
	for (size_t i = 0; i < curve.size(); ++i) CHECK(std::abs(std::hypot(curve[i].tx, curve[i].tz) - 1.0f) < 1e-3f);

	// Too many points are ignored past the cap instead of overflowing it.
	std::vector<RiverPoint> many;
	for (int i = 0; i < 200; ++i) many.push_back({ static_cast<float>(i) * 3.0f, std::sin(static_cast<float>(i)) * 5.0f, 6.0f });
	CHECK(static_cast<int>(RiverPath::Build(many, 0.25f).size()) <= RiverPath::kMaxSamples);
}

static void TestBends()
{
	// Straight: no bend at all.
	CHECK(RiverPath::TightestBendRatio(RiverPath::Build({ { 0, -30, 8 }, { 0, 0, 8 }, { 0, 30, 8 } }, 1.0f)) > 1000.0f);
	// Points on a circle of radius 30, the river 8 wide (half width 4). A true circle would be 30 / 4 = 7.5 half widths; a spline
	// through points 30 degrees apart is a little tighter, and tighter again in the last segment (about 4): well above 1.
	std::vector<RiverPoint> circle;
	for (int i = 0; i < 9; ++i) {
		const float a = 6.2831853f * static_cast<float>(i) / 12.0f;   // three quarters of a circle
		circle.push_back({ 30.0f * std::cos(a), 30.0f * std::sin(a), 8.0f });
	}
	const float gentle = RiverPath::TightestBendRatio(RiverPath::Build(circle, 0.5f));
	CHECK(gentle > 3.0f && gentle < 10.0f);
	// A hairpin: the river turns back on itself within a few units, much tighter than it is wide.
	const float tight = RiverPath::TightestBendRatio(RiverPath::Build({ { -20, 0, 10 }, { 0, 0, 10 }, { 3, 4, 10 }, { 0, 8, 10 }, { -20, 8, 10 } }, 0.5f));
	CHECK(tight < 1.0f);
	// Wider water makes the same curve relatively tighter.
	const float wide = RiverPath::TightestBendRatio(RiverPath::Build({ { 0, 0, 20 }, { 20, 0, 20 }, { 40, 20, 20 }, { 60, 20, 20 } }, 0.5f));
	const float narrow = RiverPath::TightestBendRatio(RiverPath::Build({ { 0, 0, 5 }, { 20, 0, 5 }, { 40, 20, 5 }, { 60, 20, 5 } }, 0.5f));
	CHECK(narrow > wide * 2.0f);
	CHECK(RiverPath::TightestBendRatio({}) > 1000.0f);
}

static void TestClosest()
{
	const std::vector<RiverPath::Sample> line = RiverPath::Build({ { 0, -30, 8 }, { 0, 30, 8 } }, 1.0f);
	// Flow runs toward +z. On this river, +x is one side and -x the other, and the sign flips between them.
	const RiverPath::Hit right = RiverPath::Closest(line, 3.0f, 10.0f);
	const RiverPath::Hit left = RiverPath::Closest(line, -3.0f, 10.0f);
	CHECK(right.found && left.found);
	CHECK(std::abs(right.distance - 3.0f) < 1e-3f && std::abs(left.distance - 3.0f) < 1e-3f);
	CHECK(right.signedDistance * left.signedDistance < 0.0f);
	CHECK(std::abs(std::abs(right.across) - 0.75f) < 1e-3f);                 // 3 of 4 half-widths
	CHECK(std::abs(std::abs(RiverPath::Closest(line, 4.0f, 0.0f).across) - 1.0f) < 1e-3f);   // the bank is exactly across = +-1
	CHECK(std::abs(right.s - 40.0f) < 0.01f);                               // 10 is 40 along a river that starts at -30
	CHECK(std::abs(RiverPath::Closest(line, 0.0f, -50.0f).s) < 1e-4f);      // beyond the start: the start
	CHECK(!RiverPath::Closest({}, 0.0f, 0.0f).found);
	// A direction at a bend is the average of in and out, and is a unit vector everywhere.
	const std::vector<RiverPath::Sample> bend = RiverPath::Build({ { 0, 0, 8 }, { 20, 0, 8 }, { 20, 20, 8 } }, 0.5f);
	std::uint32_t state = 5;
	for (int i = 0; i < 500; ++i) {
		const RiverPath::Hit h = RiverPath::Closest(bend, Random01(state) * 60.0f - 20.0f, Random01(state) * 60.0f - 20.0f);
		CHECK(std::abs(std::hypot(h.tx, h.tz) - 1.0f) < 1e-3f);
	}
}

static Heightfield FlatField(int cells, float size, float value)
{
	Heightfield field;
	HeightfieldLayout layout;
	layout.cellsX = layout.cellsZ = cells;
	layout.sizeX = layout.sizeZ = size;
	layout.heightScale = 20.0f;
	CHECK(field.Reset(layout, std::vector<float>(static_cast<size_t>(cells + 1) * static_cast<size_t>(cells + 1), value)));
	return field;
}

static void TestCarvedHeight()
{
	RiverCarveSettings s;
	s.level = 0.5f; s.depth = 0.1f; s.bankWidth = 4.0f; s.bankHeight = 0.3f;   // the bank climbs to 0.8 over 4 units
	const float hw = 4.0f, ground = 0.7f;
	CHECK(std::abs(CarvedHeight(0.0f, hw, ground, s) - 0.4f) < 1e-6f);      // the middle: level - depth
	CHECK(std::abs(CarvedHeight(hw, hw, ground, s) - 0.5f) < 1e-6f);        // the water's edge: exactly the level
	CHECK(std::abs(CarvedHeight(hw + 4.0f, hw, ground, s) - ground) < 1e-6f);   // the far side of the bank: ground below the bank's top is untouched
	CHECK(std::abs(CarvedHeight(hw + 4.0f, hw, 0.95f, s) - 0.8f) < 1e-6f);      // higher ground is cut down to the bank's top (a wall)
	CHECK(std::abs(CarvedHeight(hw + 2.0f, hw, 0.95f, s) - 0.65f) < 1e-6f);     // half way up the bank: half way up (smoothstep(0.5) = 0.5)
	float previous = CarvedHeight(0.0f, hw, ground, s);
	for (float d = 0.0f; d <= hw + 4.0f; d += 0.1f) {   // from the middle outward the ground only ever rises
		const float h = CarvedHeight(d, hw, ground, s);
		CHECK(h >= previous - 1e-6f && h <= ground + 1e-6f);
		previous = h;
	}
	// A river only digs: ground already below the bed or the level is left alone.
	CHECK(std::abs(CarvedHeight(0.0f, hw, 0.2f, s) - 0.2f) < 1e-6f);
	CHECK(std::abs(CarvedHeight(hw + 1.0f, hw, 0.45f, s) - 0.45f) < 1e-6f);
}

static void TestCarve()
{
	Heightfield field = FlatField(64, 64.0f, 0.6f);   // 1 unit per cell, ground at 0.6, the water at 0.5
	RiverCarveSettings s;
	s.level = 0.5f; s.depth = 0.08f; s.bankWidth = 4.0f; s.bankHeight = 0.3f;
	// A river along the z axis through the middle of the field, 8 wide, in local coordinates.
	const std::vector<RiverPath::Sample> path = RiverPath::Build({ { 0, -20, 8 }, { 0, 20, 8 } }, 1.0f);
	const std::vector<float> original = field.Samples();
	StrokeRecorder recorder;
	const GridRect changed = CarveRiver(field, path, s, &recorder);
	CHECK(!changed.IsEmpty());

	auto at = [&](float x, float z) { return field.Sample(static_cast<int>(std::lround(x + 32.0f)), static_cast<int>(std::lround(z + 32.0f))); };
	CHECK(std::abs(at(0.0f, 0.0f) - 0.42f) < 0.005f);         // the bed in the middle
	CHECK(std::abs(at(4.0f, 0.0f) - 0.5f) < 0.005f);          // the water's edge
	CHECK(at(2.0f, 0.0f) < at(4.0f, 0.0f) && at(6.0f, 0.0f) > at(4.0f, 0.0f));   // bed < edge < bank
	CHECK(std::abs(at(9.0f, 0.0f) - 0.6f) < 1e-5f);           // beyond bank: the old ground
	CHECK(std::abs(at(0.0f, 28.0f) - 0.6f) < 1e-5f);          // past the end of the river: the old ground
	for (size_t i = 0; i < original.size(); ++i) CHECK(field.Samples()[i] <= original[i] + 1e-6f);   // nothing was raised

	// The recorded patch is the whole story: undo puts every sample back, redo digs it again, and a second carve changes nothing.
	const HeightfieldPatch patch = recorder.Finish(field);
	CHECK(!patch.IsEmpty());
	const std::vector<float> carved = field.Samples();
	ApplyPatch(field, patch, false);
	for (size_t i = 0; i < original.size(); ++i) CHECK(std::abs(field.Samples()[i] - original[i]) < 1e-6f);
	ApplyPatch(field, patch, true);
	for (size_t i = 0; i < original.size(); ++i) CHECK(std::abs(field.Samples()[i] - carved[i]) < 1e-6f);
	CHECK(CarveRiver(field, path, s, nullptr).IsEmpty());

	// No path, no change.
	CHECK(CarveRiver(field, {}, s, nullptr).IsEmpty());
}


// Lesson 39: where a click puts a new point, and what is wrong with the curve as drawn.
static std::vector<RiverPoint> ApplyPlacement(std::vector<RiverPoint> points, const RiverPath::Placement& placement)
{
	points.insert(points.begin() + static_cast<std::ptrdiff_t>(placement.index), placement.point);
	return points;
}

static void TestPlace()
{
	// No points yet: the click is the first point, with the default width. One point: still just appended.
	RiverPath::Placement first = RiverPath::Place({}, {}, 5.0f, 6.0f, 9.0f);
	CHECK(first.kind == RiverPath::Placement::Kind::Append && first.index == 0);
	CHECK(first.point.x == 5.0f && first.point.z == 6.0f && first.point.width == 9.0f);
	const std::vector<RiverPoint> one = { { 0, 0, 12 } };
	const RiverPath::Placement second = RiverPath::Place(one, {}, 20.0f, 0.0f);
	CHECK(second.kind == RiverPath::Placement::Kind::Append && second.index == 1 && second.point.width == 12.0f);

	// A straight river from (-40, 0) to (40, 0), 8 wide, with a middle point: three control points.
	const std::vector<RiverPoint> river = { { -40, 0, 8 }, { 0, 0, 12 }, { 40, 0, 8 } };
	const std::vector<RiverPath::Sample> path = RiverPath::Build(river, 1.0f);

	// Control spans: samples before the middle point are on stretch 0, after it on stretch 1; the last sample is on the last stretch.
	CHECK(path.front().span == 0 && path.back().span == 1);
	for (size_t i = 1; i < path.size(); ++i) CHECK(path[i].span >= path[i - 1].span);

	// Beyond the downstream end: appended. Beyond the upstream end: prepended, at the front.
	const RiverPath::Placement after = RiverPath::Place(river, path, 70.0f, 5.0f);
	CHECK(after.kind == RiverPath::Placement::Kind::Append && after.index == 3);
	CHECK(after.point.x == 70.0f && after.point.z == 5.0f);
	const RiverPath::Placement before = RiverPath::Place(river, path, -70.0f, 20.0f);
	CHECK(before.kind == RiverPath::Placement::Kind::Prepend && before.index == 0);
	CHECK(before.point.width == 8.0f);
	// Just past the end but within the width of the water: still extends, never inserts.
	CHECK(RiverPath::Place(river, path, 42.0f, 1.0f).kind == RiverPath::Placement::Kind::Append);
	CHECK(RiverPath::Place(river, path, -42.0f, 1.0f).kind == RiverPath::Placement::Kind::Prepend);

	// On the water, between the first and middle points: inserted at index 1, ONTO the middle line, with the river's width there.
	const RiverPath::Placement low = RiverPath::Place(river, path, -20.0f, 2.5f);
	CHECK(low.kind == RiverPath::Placement::Kind::Insert && low.index == 1);
	CHECK(std::abs(low.point.x + 20.0f) < 0.6f && std::abs(low.point.z) < 1e-3f);
	CHECK(low.point.width > 8.0f && low.point.width < 12.0f);
	// And between the middle and last: index 2.
	const RiverPath::Placement high = RiverPath::Place(river, path, 20.0f, -2.0f);
	CHECK(high.kind == RiverPath::Placement::Kind::Insert && high.index == 2);
	// Inserting a point on the line leaves the curve where it was (to within the spline's own change).
	const std::vector<RiverPath::Sample> after2 = RiverPath::Build(ApplyPlacement(river, low), 1.0f);
	CHECK(RiverPath::Closest(after2, -20.0f, 0.0f).distance < 0.05f);
	// Beside the water, not on it: extends the nearer end instead.
	const RiverPath::Placement beside = RiverPath::Place(river, path, 30.0f, 40.0f);
	CHECK(beside.kind == RiverPath::Placement::Kind::Append);
}

static void TestAnalyse()
{
	// A straight river and a gentle curve: nothing wrong.
	CHECK(!RiverPath::Analyse(RiverPath::Build({ { -40, 0, 8 }, { 0, 0, 8 }, { 40, 0, 8 } }, 1.0f)).Any());
	CHECK(!RiverPath::Analyse(RiverPath::Build({ { -40, 0, 8 }, { 0, 5, 8 }, { 40, 30, 8 }, { 60, 80, 8 } }, 1.0f)).Any());
	// A hairpin tighter than the river is wide: reported as a tight bend, with the sample where it is.
	const std::vector<RiverPath::Sample> pin = RiverPath::Build({ { -20, 0, 10 }, { 0, 0, 10 }, { 3, 4, 10 }, { 0, 8, 10 }, { -20, 8, 10 } }, 0.5f);
	const RiverPath::Problems tight = RiverPath::Analyse(pin);
	CHECK(tight.tightIndex > 0 && tight.tightRatio < 1.0f);
	// Two parts of a river lying on top of each other: reported as an overlap, between samples that are far apart along it.
	const std::vector<RiverPath::Sample> twice = RiverPath::Build({ { 0, 0, 8 }, { 60, 0, 8 }, { 120, 0, 8 }, { 60, 2, 8 }, { 0, 3, 8 } }, 1.0f);
	const RiverPath::Problems overlap = RiverPath::Analyse(twice);
	CHECK(overlap.overlapA >= 0 && overlap.overlapB > overlap.overlapA);
	CHECK(twice[static_cast<size_t>(overlap.overlapB)].s - twice[static_cast<size_t>(overlap.overlapA)].s > 12.0f);   // more than half a bend as wide as the river
	// Two stretches running side by side with clear water between them are fine.
	CHECK(RiverPath::Analyse(RiverPath::Build({ { 0, 0, 8 }, { 60, 0, 8 }, { 90, 25, 8 }, { 60, 50, 8 }, { 0, 50, 8 } }, 1.0f)).overlapA < 0);
}

// The author's actual sequence of clicks that went wrong: along the river from the middle to the left, then back out on the
// right, then up round a hairpin. With the old rule (every click appended) the curve doubled back along itself; with Place the
// same clicks make one river.
static void TestClickSequence()
{
	const float clicks[][2] = { { 100, 0 }, { 40, 2 }, { -20, 3 }, { -80, 4 }, { 150, -5 }, { 120, -60 }, { 40, -90 }, { -10, -100 }, { -12, -96 }, { 5, -88 } };
	const int sensible = 8;   // the first eight make a river; the last two squeeze a hairpin in beside the eighth
	std::vector<RiverPoint> appended, placed;
	for (int i = 0; i < 10; ++i) {
		appended.push_back({ clicks[i][0], clicks[i][1], 8.0f });
		placed = ApplyPlacement(placed, RiverPath::Place(placed, RiverPath::Build(placed, 1.0f), clicks[i][0], clicks[i][1]));
		if (i + 1 == sensible) {
			CHECK(!RiverPath::Analyse(RiverPath::Build(placed, 1.0f)).Any());                          // one clean river
			CHECK(RiverPath::Analyse(RiverPath::Build({ appended.begin(), appended.end() }, 1.0f)).overlapA >= 0);   // the old way: it lies on itself
		}
	}
	CHECK(placed.size() == 10);
	// The hairpin is still a hairpin, and is reported as one (the points were meant to be that close).
	CHECK(RiverPath::Analyse(RiverPath::Build(placed, 1.0f)).tightIndex >= 0);
	// Everything clicked from the right-hand end went on the upstream side: the river runs from the last click to the first stretch's far end.
	CHECK(placed.front().x == 5.0f && placed.back().x == -80.0f);
}

// Lesson 41: the water's level along the river.
static void TestLevels()
{
	// Levels come along with the curve: exactly the point's own at a control point, in between straight, never overshooting.
	const std::vector<RiverPoint> falling = { { -40, 0, 8, 0.0f }, { 0, 0, 8, -4.0f }, { 40, 0, 8, -10.0f } };
	const std::vector<RiverPath::Sample> path = RiverPath::Build(falling, 1.0f);
	CHECK(path.front().level == 0.0f && path.back().level == -10.0f);
	for (size_t i = 1; i < path.size(); ++i) CHECK(path[i].level <= path[i - 1].level + 1e-6f);   // the water never runs uphill
	bool atMiddle = false;
	for (const RiverPath::Sample& s : path) if (s.span == 1 && std::abs(s.x) < 1e-3f) atMiddle = std::abs(s.level + 4.0f) < 1e-4f;
	CHECK(atMiddle);
	// A spot beside the river reads the level of the nearest point of it.
	CHECK(std::abs(RiverPath::Closest(path, -20.0f, 3.0f).level + 2.0f) < 0.1f);
	CHECK(std::abs(RiverPath::Closest(path, 20.0f, -3.0f).level + 7.0f) < 0.1f);
	// A river with no levels set is level: the default is 0 everywhere.
	for (const RiverPath::Sample& s : RiverPath::Build({ { -40, 0, 8 }, { 40, 0, 8 } }, 1.0f)) CHECK(s.level == 0.0f);

	// New points start at the level the curve has there (inserted on the water) or at their neighbour's (an end).
	CHECK(std::abs(RiverPath::Place(falling, path, -20.0f, 1.0f).point.level + 2.0f) < 0.1f);
	CHECK(RiverPath::Place(falling, path, 80.0f, 0.0f).point.level == -10.0f);   // beyond the downstream end
	CHECK(RiverPath::Place(falling, path, -80.0f, 0.0f).point.level == 0.0f);    // beyond the upstream end

	// FollowGround: the ground under each point, but never higher than the point before it.
	std::vector<RiverPoint> points = { { 0, 0, 8 }, { 10, 0, 8 }, { 20, 0, 8 }, { 30, 0, 8 }, { 40, 0, 8 } };
	RiverPath::FollowGround(points, { 10.0f, 12.0f, 8.0f, 9.0f, 5.0f }, 10.0f);
	const float expected[] = { 0.0f, 0.0f, -2.0f, -2.0f, -5.0f };
	for (size_t i = 0; i < points.size(); ++i) CHECK(std::abs(points[i].level - expected[i]) < 1e-5f);
	// The wrong number of heights changes nothing.
	RiverPath::FollowGround(points, { 1.0f }, 0.0f);
	CHECK(points[4].level == -5.0f);
}

// Letting the ground draw the river (lesson 45): a walk downhill, then smoothed and thinned into control points.
static void TestRoute()
{
	// Simplify: a straight line of many points is two; the ends always survive; the cap is always respected.
	{
		std::vector<RiverPoint> line;
		for (int i = 0; i < 100; ++i) line.push_back({ static_cast<float>(i), 0.01f * static_cast<float>(i % 3), 6.0f });
		const std::vector<RiverPoint> two = RiverPath::Simplify(line, 1.0f, 40);
		CHECK(two.size() == 2);
		CHECK(two.front().x == line.front().x && two.back().x == line.back().x);

		std::vector<RiverPoint> wave;
		for (int i = 0; i < 200; ++i) wave.push_back({ static_cast<float>(i), 10.0f * std::sin(static_cast<float>(i) * 0.2f), 6.0f });
		for (int cap : { 2, 5, 10, 40 }) {
			const std::vector<RiverPoint> few = RiverPath::Simplify(wave, 0.01f, cap);   // a tolerance far too small for the cap
			CHECK(static_cast<int>(few.size()) <= cap && few.size() >= 2);
			CHECK(few.front().x == wave.front().x && few.back().x == wave.back().x);
		}
	}

	// Smooth: the ends stay, the middle comes closer to its neighbours.
	{
		std::vector<RiverPoint> zig = { { 0, 0 }, { 4, 4 }, { 8, -4 }, { 12, 4 }, { 16, 0 } };
		RiverPath::Smooth(zig, 2);
		CHECK(zig.front().x == 0.0f && zig.front().z == 0.0f && zig.back().x == 16.0f && zig.back().z == 0.0f);
		CHECK(std::abs(zig[2].z) < 4.0f);
	}

	// A valley that falls toward +z, its floor along x = 30. A river that starts on the slope beside it must find the floor.
	const auto valley = [](float x, float z, float& y) {
		if (x < -100.0f || x > 200.0f || z < 0.0f || z > 300.0f) return false;
		y = 60.0f - 0.2f * z + 0.004f * (x - 30.0f) * (x - 30.0f);
		return true;
	};
	{
		RiverPath::RouteParams params;
		params.stopHeight = 10.0f;
		const RiverPath::Route route = RiverPath::FlowDownhill(valley, 0.0f, 10.0f, params);
		CHECK(route.end == RiverPath::RouteEnd::Sea);
		CHECK(route.points.size() >= 2 && static_cast<int>(route.points.size()) <= params.maxPoints);
		CHECK(std::abs(route.points.front().x - 0.0f) < 1e-3f && std::abs(route.points.front().z - 10.0f) < 1e-3f);   // starts at the spring
		CHECK(route.points.back().z > 220.0f);                       // ran most of the way down
		CHECK(std::abs(route.points.back().x - 30.0f) < 8.0f);       // and ended in the valley floor
		CHECK(route.fall > 40.0f);
		CHECK(route.length > 200.0f);
		for (size_t i = 0; i < route.points.size(); ++i) {           // wider downstream, within the asked range
			CHECK(route.points[i].width >= params.startWidth - 1e-3f && route.points[i].width <= params.endWidth + 1e-3f);
			if (i > 0) CHECK(route.points[i].width >= route.points[i - 1].width - 1e-3f);
		}
		// The curve through the control points runs downhill (a river must not climb), to within the ground's own bumps.
		const std::vector<RiverPath::Sample> curve = RiverPath::Build(route.points, 1.0f);
		float previous = 1.0e9f;
		for (const RiverPath::Sample& sample : curve) {
			float y = 0.0f;
			if (!valley(sample.x, sample.z, y)) continue;
			CHECK(y <= previous + 1.0f);
			previous = (std::min)(previous, y);
		}
		const RiverPath::Problems problems = RiverPath::Analyse(curve);
		CHECK(problems.overlapA < 0);                                // it does not run into itself
	}

	// The same river, the same way, every time (no randomness).
	{
		RiverPath::RouteParams params;
		params.stopHeight = 10.0f;
		const RiverPath::Route a = RiverPath::FlowDownhill(valley, 0.0f, 10.0f, params);
		const RiverPath::Route b = RiverPath::FlowDownhill(valley, 0.0f, 10.0f, params);
		CHECK(a.points.size() == b.points.size());
		for (size_t i = 0; i < a.points.size() && i < b.points.size(); ++i) CHECK(a.points[i].x == b.points[i].x && a.points[i].z == b.points[i].z);
	}

	// Without a stop height it runs to the edge of the terrain.
	{
		const RiverPath::Route route = RiverPath::FlowDownhill(valley, 30.0f, 10.0f, RiverPath::RouteParams{});
		CHECK(route.end == RiverPath::RouteEnd::EdgeOfTerrain);
		CHECK(route.points.back().z > 280.0f);
	}

	// A bowl: the river runs into the middle and stops there (a pit), and never spins forever.
	{
		const auto bowl = [](float x, float z, float& y) {
			if (std::abs(x) > 200.0f || std::abs(z) > 200.0f) return false;
			y = 0.05f * (x * x + z * z) * 0.1f;
			return true;
		};
		const RiverPath::Route route = RiverPath::FlowDownhill(bowl, 60.0f, 10.0f, RiverPath::RouteParams{});
		CHECK(route.end == RiverPath::RouteEnd::Pit);
		CHECK(std::hypot(route.points.back().x, route.points.back().z) < 12.0f);
		CHECK(static_cast<int>(route.points.size()) <= 40);
	}

	// Flat ground has no way down: it stops instead of wandering.
	{
		const auto flat = [](float x, float z, float& y) { if (std::abs(x) > 500.0f || std::abs(z) > 500.0f) return false; y = 10.0f; return true; };
		const RiverPath::Route route = RiverPath::FlowDownhill(flat, 0.0f, 0.0f, RiverPath::RouteParams{});
		CHECK(route.end == RiverPath::RouteEnd::Pit);
		CHECK(route.length < 400.0f);
	}

	// A spring that is not on the ground gives nothing; a spring already below the sea is a river of nothing.
	{
		const RiverPath::Route off = RiverPath::FlowDownhill(valley, 500.0f, 500.0f, RiverPath::RouteParams{});
		CHECK(off.end == RiverPath::RouteEnd::NoGround && off.points.empty());
		RiverPath::RouteParams params;
		params.stopHeight = 100.0f;
		const RiverPath::Route drowned = RiverPath::FlowDownhill(valley, 0.0f, 10.0f, params);
		CHECK(drowned.end == RiverPath::RouteEnd::Sea && drowned.points.size() < 2);
	}

	// A winding valley (the floor swings from side to side): the river follows the swing, and the bends it makes are not tighter
	// than it is wide.
	{
		const auto winding = [](float x, float z, float& y) {
			if (x < -150.0f || x > 150.0f || z < 0.0f || z > 400.0f) return false;
			const float floorX = 40.0f * std::sin(z * 0.025f);
			y = 80.0f - 0.2f * z + 0.006f * (x - floorX) * (x - floorX);
			return true;
		};
		RiverPath::RouteParams params;
		params.stopHeight = 5.0f;
		const RiverPath::Route route = RiverPath::FlowDownhill(winding, 0.0f, 5.0f, params);
		CHECK(route.end == RiverPath::RouteEnd::Sea || route.end == RiverPath::RouteEnd::EdgeOfTerrain);
		CHECK(route.points.back().z > 300.0f);
		float widest = 0.0f;
		float lateral = 0.0f;
		for (const RiverPoint& p : route.points) { widest = (std::max)(widest, p.width); lateral = (std::max)(lateral, std::abs(p.x)); }
		CHECK(lateral > 15.0f);                                       // it did swing with the valley
		const std::vector<RiverPath::Sample> curve = RiverPath::Build(route.points, 1.0f);
		CHECK(RiverPath::TightestBendRatio(curve) > 1.0f);
		CHECK(RiverPath::Analyse(curve).overlapA < 0);
	}
	// Rough ground (hills made of value noise), many springs: every walk ends, stays inside its limits and stays finite.
	{
		const auto hash = [](int ix, int iz) {
			std::uint32_t h = static_cast<std::uint32_t>(ix) * 374761393u + static_cast<std::uint32_t>(iz) * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
		};
		const auto hills = [&hash](float x, float z, float& y) {
			if (x < 0.0f || x > 400.0f || z < 0.0f || z > 400.0f) return false;
			float height = 0.0f, amplitude = 30.0f, scale = 1.0f / 64.0f;
			for (int octave = 0; octave < 4; ++octave) {
				const float fx = x * scale, fz = z * scale;
				const int ix = static_cast<int>(std::floor(fx)), iz = static_cast<int>(std::floor(fz));
				const float tx = fx - static_cast<float>(ix), tz = fz - static_cast<float>(iz);
				const float a = hash(ix, iz) + (hash(ix + 1, iz) - hash(ix, iz)) * tx;
				const float b = hash(ix, iz + 1) + (hash(ix + 1, iz + 1) - hash(ix, iz + 1)) * tx;
				height += (a + (b - a) * tz) * amplitude;
				amplitude *= 0.5f;
				scale *= 2.0f;
			}
			y = height;
			return true;
		};
		std::uint32_t state = 12345u;
		for (int trial = 0; trial < 60; ++trial) {
			const float sx = 20.0f + 360.0f * Random01(state), sz = 20.0f + 360.0f * Random01(state);
			RiverPath::RouteParams params;
			params.step = trial % 2 == 0 ? 4.0f : 2.5f;
			const RiverPath::Route route = RiverPath::FlowDownhill(hills, sx, sz, params);
			CHECK(static_cast<int>(route.points.size()) <= params.maxPoints);
			CHECK(std::isfinite(route.length) && std::isfinite(route.fall));
			CHECK(route.fall >= -1e-3f);   // never ends higher than it began
			CHECK(route.length <= params.maxLength * 1.2f);
			for (const RiverPoint& p : route.points) CHECK(std::isfinite(p.x) && std::isfinite(p.z) && p.width >= RiverPath::kMinWidth && p.width <= RiverPath::kMaxWidth);
		}
	}

}

int main()
{
	TestCurve();
	TestBends();
	TestClosest();
	TestCarvedHeight();
	TestCarve();
	TestPlace();
	TestAnalyse();
	TestClickSequence();
	TestLevels();
	TestRoute();
	if (g_failures == 0) std::printf("river tests: all passed\n");
	return g_failures == 0 ? 0 : 1;
}
