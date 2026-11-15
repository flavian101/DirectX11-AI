// Unit tests for src/RiverPath.h, src/RiverCarve.cpp and src/WaterRiver.h (lesson 38). No Direct3D, no framework: a failed CHECK
// prints and the program exits 1.
//  - the curve passes through its control points, runs in order, and answers "where am I relative to it?" correctly
//  - the carve digs a channel that is deepest in the middle, level with the water at its edge, never raises ground, and is
//    exactly undone by its recorded patch
//  - the flow map: direction follows the curve, strength is 1 in the middle and falls to the bank, nothing far from the river,
//    and the fast bake agrees with the plain per-point question
#include "RiverCarve.h"
#include "RiverPath.h"
#include "WaterRiver.h"
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

// A square body 64 units wide, centred on the origin: local -1..1 is world -32..32.
static void ToWorld(float lx, float lz, float& wx, float& wz) { wx = lx * 32.0f; wz = lz * 32.0f; }
static void ToLocal(float wx, float wz, float& lx, float& lz) { lx = wx / 32.0f; lz = wz / 32.0f; }

static void TestFlowMap()
{
	const int n = 64;   // one texel = one world unit
	const std::vector<RiverPath::Sample> path = RiverPath::Build({ { 0, -30, 8 }, { 0, 30, 8 } }, 1.0f);
	const std::vector<float> map = WaterRiver::Bake(n, n, path, ToWorld, ToLocal);
	CHECK(map.size() == static_cast<size_t>(n) * n * 4);
	auto texel = [&](int x, int z) {   // texel (x, z) has its centre at world (x - 31.5, z - 31.5)
		WaterRiver::Texel t;
		const size_t i = (static_cast<size_t>(z) * n + static_cast<size_t>(x)) * 4;
		t.s = map[i]; t.d = map[i + 1]; t.dirX = map[i + 2]; t.dirZ = map[i + 3];
		return t;
	};
	// The middle of the river: flowing toward +z, at full strength.
	{
		const WaterRiver::Read r = WaterRiver::Decode(texel(32, 32));
		CHECK(r.inRiver && r.dirZ > 0.99f && std::abs(r.dirX) < 0.05f && r.profile > 0.95f);
	}
	// Toward the bank the strength falls, the direction stays, and the signed distance changes sign across the middle.
	{
		const WaterRiver::Read mid = WaterRiver::Decode(texel(32, 32)), near = WaterRiver::Decode(texel(34, 32)), bank = WaterRiver::Decode(texel(36, 32));
		CHECK(near.inRiver && bank.inRiver);
		CHECK(mid.profile > near.profile && near.profile > bank.profile);
		CHECK(bank.profile < 0.15f && bank.dirZ > 0.99f);   // at the bank the direction is still known
		CHECK(texel(34, 32).d * texel(29, 32).d < 0.0f);
	}
	// Far from the river there is no river, and the data is empty rather than wrong.
	{
		const WaterRiver::Read far = WaterRiver::Decode(texel(52, 32));
		CHECK(!far.inRiver);
	}
	// How far along: s grows by one unit per texel downstream.
	CHECK(std::abs((texel(32, 40).s - texel(32, 30).s) - 10.0f) < 0.01f);

	// A real bend: the flow direction at a texel on the middle line is the curve's own direction there.
	const std::vector<RiverPath::Sample> bend = RiverPath::Build({ { -25, -20, 8 }, { -5, -5, 8 }, { 8, 5, 8 }, { 25, 22, 8 } }, 0.5f);
	const std::vector<float> bendMap = WaterRiver::Bake(n, n, bend, ToWorld, ToLocal);
	std::uint32_t state = 21;
	int compared = 0;
	for (int i = 0; i < 3000; ++i) {
		const int x = static_cast<int>(Random01(state) * n), z = static_cast<int>(Random01(state) * n);
		const float wx = static_cast<float>(x) - 31.5f, wz = static_cast<float>(z) - 31.5f;
		const RiverPath::Hit hit = RiverPath::Closest(bend, wx, wz);
		if (std::abs(hit.across) > 1.5f) continue;   // only where the river's data is fully there
		const WaterRiver::Texel expected = WaterRiver::MakeTexel(hit);
		const size_t k = (static_cast<size_t>(z) * n + static_cast<size_t>(x)) * 4;
		++compared;
		// The fast bake (one piece of the curve at a time) agrees with the plain question (every piece).
		CHECK(std::abs(bendMap[k] - expected.s) < 0.01f);
		CHECK(std::abs(bendMap[k + 1] - expected.d) < 0.01f);
		CHECK(std::abs(bendMap[k + 2] - expected.dirX) < 1e-3f && std::abs(bendMap[k + 3] - expected.dirZ) < 1e-3f);
	}
	CHECK(compared > 200);
	// Edge cases.
	CHECK(WaterRiver::Bake(n, n, {}, ToWorld, ToLocal).size() == static_cast<size_t>(n) * n * 4);
	CHECK(!WaterRiver::Decode(WaterRiver::MakeTexel(RiverPath::Hit{})).inRiver);
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

int main()
{
	TestCurve();
	TestBends();
	TestClosest();
	TestCarvedHeight();
	TestCarve();
	TestFlowMap();
	TestPlace();
	TestAnalyse();
	TestClickSequence();
	if (g_failures == 0) std::printf("river tests: all passed\n");
	return g_failures == 0 ? 0 : 1;
}
