// Unit tests for src/WaterWaves.h and src/WaterMesh.h. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
//  - tile index lists: watertight, right winding, full area, and the border keeps every vertex (so neighbouring tiles at
//    different levels of detail can never crack apart)
//  - the tile grid chosen for a body's proportions
//  - the wave table: the height-at-a-position inversion, no folding at full choppiness, and the exact normal/Jacobian the
//    pixel shader uses against a finite-difference normal of the real displaced surface
#include "WaterMesh.h"
#include "WaterShore.h"
#include "WaterWaves.h"
#include "WaterTypeUi.h"
#include "Weather.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static float Random01(std::uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	return static_cast<float>(state >> 8) / 16777216.0f;
}

static double Area2(int row, uint32_t a, uint32_t b, uint32_t c)
{
	const double ax = a % row, az = a / row, bx = b % row, bz = b / row, cx = c % row, cz = c / row;
	return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
}

static void TestTileIndices()
{
	for (int cells : { 16, 32, 64 }) {
		for (int stride : { 1, 2, 4, 8 }) {
			const std::vector<uint32_t> indices = WaterMesh::BuildTileIndices(cells, stride);
			const int row = cells + 1;
			CHECK(indices.size() % 3 == 0);
			const double referenceSign = Area2(row, 0, row, row + 1);   // the winding of the plain quad (a, b, c)
			double total = 0.0;
			int flipped = 0;
			std::map<std::pair<uint32_t, uint32_t>, int> edges;   // directed edge -> how many triangles use it
			for (size_t i = 0; i < indices.size(); i += 3) {
				const double area = Area2(row, indices[i], indices[i + 1], indices[i + 2]);
				if (area * referenceSign <= 0.0) ++flipped;
				total += std::fabs(area) * 0.5;
				for (int e = 0; e < 3; ++e) ++edges[{ indices[i + e], indices[i + (e + 1) % 3] }];
			}
			CHECK(flipped == 0);                                          // every triangle faces the right way, none degenerate
			CHECK(std::fabs(total - cells * cells) < 1e-6);               // exactly covers the tile: no gaps, no overlaps
			int unmatched = 0, duplicates = 0;
			for (const auto& [edge, count] : edges) {
				if (count > 1) ++duplicates;
				if (edges.count({ edge.second, edge.first })) continue;   // an interior edge has a partner going the other way
				const int x1 = edge.first % row, z1 = edge.first / row, x2 = edge.second % row, z2 = edge.second / row;
				const bool onBorder = (x1 == x2 && (x1 == 0 || x1 == cells)) || (z1 == z2 && (z1 == 0 || z1 == cells));
				const bool unit = std::abs(x1 - x2) + std::abs(z1 - z2) == 1;
				if (!onBorder || !unit) ++unmatched;                      // an unmatched interior edge is a T-junction = a crack
			}
			CHECK(unmatched == 0);
			CHECK(duplicates == 0);
			int borderEdges = 0;   // all 4 * cells unit edges of the border are present, at EVERY level
			for (int i = 0; i < cells; ++i) {
				auto has = [&](uint32_t a, uint32_t b) { return edges.count({ a, b }) + edges.count({ b, a }); };
				borderEdges += has(i, i + 1) + has(cells * row + i, cells * row + i + 1) + has(i * row, (i + 1) * row) + has(i * row + cells, (i + 1) * row + cells);
			}
			CHECK(borderEdges == 4 * cells);
		}
	}
}

static void TestTileGrid()
{
	auto g = WaterMesh::ChooseTileGrid(180, 180);  CHECK(g.x == 8 && g.z == 8);
	g = WaterMesh::ChooseTileGrid(400, 50);        CHECK(g.x == 16 && g.z == 2);     // a river along X
	g = WaterMesh::ChooseTileGrid(50, 400);        CHECK(g.x == 2 && g.z == 16);     // the same river along Z
	g = WaterMesh::ChooseTileGrid(1000, 10);       CHECK(g.x == 16 && g.z == 1);     // beyond the table: the most stretched grid
	g = WaterMesh::ChooseTileGrid(0.0f, 0.0f);     CHECK(g.x >= 1 && g.z >= 1);      // degenerate sizes must not crash or return nothing
	// Hysteresis: from 2:1, 2.6:1 stays put (no rebuild while dragging a slider near a boundary); 3.6:1 moves on.
	g = WaterMesh::ChooseTileGrid(260, 100, 1);    CHECK(g.shapeClass == 1);
	g = WaterMesh::ChooseTileGrid(360, 100, 1);    CHECK(g.shapeClass == 2);
	// The tiles stay about square (so the grid cells do too) for every proportion the table covers.
	for (float ratio : { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f }) {
		const WaterMesh::TileGrid q = WaterMesh::ChooseTileGrid(100.0f * ratio, 100.0f);
		float aspect = (100.0f * ratio / q.x) / (100.0f / q.z);
		if (aspect < 1.0f) aspect = 1.0f / aspect;
		CHECK(aspect < 1.45f);
	}
}

// The surface point that rests at (x, z), in double precision (finite differences need it).
struct V3 { double x, y, z; };
static V3 Surface(const WaterWaves::Table& t, double x, double z, double time)
{
	double dx = 0, dy = 0, dz = 0;
	for (int i = 0; i < t.count; ++i) {
		const auto& w = t.waves[i];
		const double phase = w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0;
		dx += w.steepness * w.amplitude * std::cos(phase) * w.dirX;
		dz += w.steepness * w.amplitude * std::cos(phase) * w.dirZ;
		dy += w.amplitude * std::sin(phase);
	}
	return { x + dx, dy, z + dz };
}

static void TestWaves()
{
	std::uint32_t seed = 12345;
	WaterWaves::Params p;
	p.choppiness = 0.9f; p.amplitude = 0.6f; p.count = 8;
	WaterWaves::Table table = WaterWaves::Build(p);

	// HeightAt: the point that ends up at (x, z) started elsewhere; the fixed-point inversion must find it (within 2 cm here).
	for (int n = 0; n < 2000; ++n) {
		const float x = Random01(seed) * 200.0f - 100.0f, z = Random01(seed) * 200.0f - 100.0f, time = Random01(seed) * 100.0f;
		const float height = WaterWaves::HeightAt(table, x, z, time);
		float rx = x, rz = z, dx, dy, dz;
		for (int it = 0; it < 200; ++it) { WaterWaves::Displacement(table, rx, rz, time, dx, dy, dz); rx = x - dx; rz = z - dz; }
		WaterWaves::Displacement(table, rx, rz, time, dx, dy, dz);
		CHECK(std::fabs(dy - height) < 0.02f);
		CHECK(std::fabs(height) <= table.heightBound + 1e-4f);
	}

	// At choppiness 1 the surface must never fold over itself: the Jacobian stays positive.
	p.choppiness = 1.0f;
	table = WaterWaves::Build(p);
	for (int n = 0; n < 20000; ++n) {
		const double x = Random01(seed) * 200.0, z = Random01(seed) * 200.0, time = Random01(seed) * 100.0;
		double sxx = 0, sxz = 0, szz = 0;
		for (int i = 0; i < table.count; ++i) {
			const auto& w = table.waves[i];
			const double s = std::sin(w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0);
			const double q = w.steepness * w.amplitude * w.k * s;
			sxx += q * w.dirX * w.dirX; sxz += q * w.dirX * w.dirZ; szz += q * w.dirZ * w.dirZ;
		}
		CHECK((1 - sxx) * (1 - szz) - sxz * sxz > 0.0);
	}

	// The pixel shader's normal and Jacobian (shaders/waves.hlsli, EvaluateWaveShading without the distance fade),
	// against the cross product of finite differences of the actual displaced surface.
	p.choppiness = 0.8f; p.windDegrees = 70.0f;
	table = WaterWaves::Build(p);
	const double e = 1e-4;
	for (int n = 0; n < 3000; ++n) {
		const double x = Random01(seed) * 200.0 - 100.0, z = Random01(seed) * 200.0 - 100.0, time = Random01(seed) * 100.0;
		double cx = 0, cz = 0, sxx = 0, sxz = 0, szz = 0;
		for (int i = 0; i < table.count; ++i) {
			const auto& w = table.waves[i];
			const double phase = w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0, s = std::sin(phase), c = std::cos(phase), ka = w.k * w.amplitude;
			cx += ka * w.dirX * c; cz += ka * w.dirZ * c;
			const double q = w.steepness * ka * s;
			sxx += q * w.dirX * w.dirX; sxz += q * w.dirX * w.dirZ; szz += q * w.dirZ * w.dirZ;
		}
		V3 shaderNormal = { -cx * (1 - szz) - cz * sxz, (1 - sxx) * (1 - szz) - sxz * sxz, -cz * (1 - sxx) - cx * sxz };
		const double jacobian = shaderNormal.y;
		const double length = std::sqrt(shaderNormal.x * shaderNormal.x + shaderNormal.y * shaderNormal.y + shaderNormal.z * shaderNormal.z);
		shaderNormal = { shaderNormal.x / length, shaderNormal.y / length, shaderNormal.z / length };

		const V3 a = Surface(table, x + e, z, time), b = Surface(table, x - e, z, time);
		const V3 c = Surface(table, x, z + e, time), d = Surface(table, x, z - e, time);
		const V3 tx = { (a.x - b.x) / (2 * e), (a.y - b.y) / (2 * e), (a.z - b.z) / (2 * e) };
		const V3 tz = { (c.x - d.x) / (2 * e), (c.y - d.y) / (2 * e), (c.z - d.z) / (2 * e) };
		V3 n3 = { tz.y * tx.z - tz.z * tx.y, tz.z * tx.x - tz.x * tx.z, tz.x * tx.y - tz.y * tx.x };   // cross(tz, tx)
		const double nl = std::sqrt(n3.x * n3.x + n3.y * n3.y + n3.z * n3.z);
		n3 = { n3.x / nl, n3.y / nl, n3.z / nl };
		CHECK(std::fabs(n3.x - shaderNormal.x) + std::fabs(n3.y - shaderNormal.y) + std::fabs(n3.z - shaderNormal.z) < 1e-4);
		CHECK(n3.y > 0.0);                                                       // the normal points up
		CHECK(std::fabs((tx.x * tz.z - tx.z * tz.x) - jacobian) < 1e-4);         // its Y IS the area scale of the surface map
	}
}

// A river is a current: every wave travels the same way, at the speed of the current, and turning the body turns the flow.
static void TestRiverFlow()
{
	const float pi = 3.14159265f;
	for (float yaw : { 0.0f, 0.5f, 1.5707963f, 2.4f, -1.1f, 3.9f }) {
		for (float flowAngle : { 0.0f, 90.0f, 180.0f, -45.0f }) {
			// What the world matrix does to the body's own direction (sin a, cos a): XMMatrixRotationY, row-vector convention.
			const float a = flowAngle * pi / 180.0f;
			const float localX = std::sin(a), localZ = std::cos(a);
			const float worldX = localX * std::cos(yaw) + localZ * std::sin(yaw);
			const float worldZ = -localX * std::sin(yaw) + localZ * std::cos(yaw);

			const float degrees = WaterWaves::RiverTravelDegrees(flowAngle, yaw);
			CHECK(degrees >= 0.0f && degrees < 360.0f);
			CHECK(std::fabs(std::cos(degrees * pi / 180.0f) - worldX) < 1e-4f);   // the angle IS that world direction
			CHECK(std::fabs(std::sin(degrees * pi / 180.0f) - worldZ) < 1e-4f);

			WaterWaves::Params p;
			p.windDegrees = degrees;
			p.spread = 0.1f;
			p.flowSpeed = 2.5f;
			p.count = 6;
			const WaterWaves::Table table = WaterWaves::Build(p);
			for (int i = 0; i < table.count; ++i) {
				const WaterWaves::Wave& w = table.waves[i];
				CHECK(std::fabs(w.omega / w.k - 2.5f) < 1e-4f);   // every wave rides the current at the same speed
				// A wave sin(k * dir . x + omega * t) moves toward -dir. That must be within the (small) spread of the flow.
				const float along = -(w.dirX * worldX + w.dirZ * worldZ);
				CHECK(along > std::cos(0.1f * 110.0f * pi / 180.0f) - 1e-3f);   // nothing runs upstream or sideways
			}
		}
	}
	// Without a flow speed the old deep-water dispersion is untouched: long waves outrun short ones.
	WaterWaves::Params open;
	const WaterWaves::Table swell = WaterWaves::Build(open);
	CHECK(swell.waves[0].omega / swell.waves[0].k > swell.waves[3].omega / swell.waves[3].k);
}

// Waves near a shore: they grow in shallow water, never exceed a fraction of the depth, and are untouched when off.
static void TestShoaling()
{
	const float k = 0.6f, a = 0.35f;
	CHECK(WaterWaves::ShoalAmplitude(a, k, 0.3f, 0.0f) == a);                       // strength 0: the open-water wave, exactly
	CHECK(std::fabs(WaterWaves::ShoalAmplitude(a, k, 60.0f, 1.0f) - a) < 0.01f * a);  // deep water: practically untouched
	CHECK(WaterWaves::ShoalAmplitude(a, k, 3.0f, 1.0f) > a * 1.1f);                  // reaching shallows: taller
	CHECK(WaterWaves::ShoalAmplitude(a, k, 0.0f, 1.0f) == 0.0f);                     // at the waterline: nothing left
	CHECK(WaterWaves::ShoalAmplitude(a, k, -2.0f, 1.0f) == 0.0f);                    // over dry land: nothing
	float previous = 0.0f;
	int rises = 0, falls = 0;
	for (float depth = 0.0f; depth <= 40.0f; depth += 0.05f) {
		const float shoaled = WaterWaves::ShoalAmplitude(a, k, depth, 1.0f);
		CHECK(shoaled >= 0.0f);
		CHECK(shoaled <= WaterWaves::kBreakRatio * depth + 1e-5f);                   // a wave is never taller than the water allows
		CHECK(std::fabs(shoaled - previous) < 0.05f);                                // no jumps: smooth in depth
		if (shoaled > previous + 1e-6f) ++rises; else if (shoaled < previous - 1e-6f) ++falls;
		previous = shoaled;
	}
	CHECK(rises > 0 && falls > 0);   // it rises coming in from deep water, peaks, then falls toward the shore
	// The strength knob is continuous: a hair above 0 is a hair away from the open-water wave, and no strength can exceed
	// the growth cap or go negative.
	for (float depth : { 0.4f, 1.0f, 3.0f, 12.0f }) {
		CHECK(std::fabs(WaterWaves::ShoalAmplitude(a, k, depth, 0.001f) - a) < 0.01f);
		for (float strength = 0.0f; strength <= 1.0f; strength += 0.1f) {
			const float value = WaterWaves::ShoalAmplitude(a, k, depth, strength);
			CHECK(value >= 0.0f && value <= a * (1.0f + WaterWaves::kShoalGrowth) + 1e-5f);
		}
	}

	// The whole surface: over a slope that rises to a beach, the displaced surface is never far below the ground.
	WaterWaves::Params p;
	p.shoreWaves = 1.0f;
	const WaterWaves::Table table = WaterWaves::Build(p);
	CHECK(table.heightBound > WaterWaves::Build(WaterWaves::Params{}).heightBound);   // room for waves that grow
	std::uint32_t state = 7u;
	for (int i = 0; i < 2000; ++i) {
		const float rx = Random01(state) * 40.0f - 5.0f;       // rest position along the slope
		const float depth = std::max(0.0f, 0.1f * (30.0f - rx));   // a beach: 3 units deep at x = 0, dry at x = 30
		float dx, dy, dz;
		WaterWaves::Displacement(table, rx, 0.0f, Random01(state) * 50.0f, dx, dy, dz, depth, 1.0f);
		CHECK(std::fabs(dy) <= table.count * WaterWaves::kBreakRatio * depth + 1e-4f);
	}
	// With depths that are deep everywhere, the shore-aware height is the open-water height to within a couple of percent.
	float worst = 0.0f;
	for (int i = 0; i < 200; ++i) {
		const float x = Random01(state) * 100.0f, z = Random01(state) * 100.0f, t = Random01(state) * 30.0f;
		const float open = WaterWaves::HeightAt(table, x, z, t);
		const float near = WaterWaves::HeightAtShore(table, x, z, t, 1.0f, [](float, float) { return 80.0f; });
		worst = std::max(worst, std::fabs(open - near));
	}
	CHECK(worst < 0.03f * table.heightBound);
}

// The depth map: bilinear reads, the bake, and how big it is for a pond and for a river.
static void TestShoreMap()
{
	// A beach along world X: the ground rises 0.1 per unit; the water is level 0; the body spans x in -50..50.
	const int width = 40, height = 8;
	const WaterShore::DepthField field = WaterShore::Bake(width, height, 0.0f,
		[](float lx, float lz, float& wx, float& wz) { wx = lx * 50.0f; wz = lz * 5.0f; },
		[](float wx, float, float& y) { y = 0.1f * wx; return true; });
	CHECK(field.Width() == width && field.Height() == height);
	for (float lx : { -0.9f, -0.4f, 0.0f, 0.3f, 0.8f }) {
		const float expected = std::min(-0.1f * (lx * 50.0f), WaterShore::kDeepWater);   // depth = level - ground
		CHECK(std::fabs(field.Sample(lx, 0.0f) - expected) < 0.1f);   // bilinear reproduces a linear slope (to within a texel)
	}
	CHECK(field.Sample(5.0f, 0.0f) == field.Sample(1.0f, 0.0f));      // outside the body: clamped to the edge, never garbage
	CHECK(field.Sample(-5.0f, -5.0f) == field.Sample(-1.0f, -1.0f));
	CHECK(WaterShore::DepthField().Sample(0.0f, 0.0f) == WaterShore::kDeepWater);   // no map: deep

	// Where there is no ground at all, the water is deep.
	const WaterShore::DepthField none = WaterShore::Bake(4, 4, 2.0f,
		[](float lx, float lz, float& wx, float& wz) { wx = lx; wz = lz; },
		[](float, float, float&) { return false; });
	CHECK(none.Sample(0.0f, 0.0f) == WaterShore::kDeepWater);

	WaterShore::DepthField bad;
	CHECK(!bad.Reset(3, 3, std::vector<float>(5, 0.0f)));   // wrong sample count: refused, nothing changes
	CHECK(bad.IsEmpty());

	// Resolution: bounded, roughly square texels, finer for a thin river than for a huge lake.
	for (auto extent : { std::make_pair(20.0f, 20.0f), std::make_pair(14.0f, 120.0f), std::make_pair(2000.0f, 2000.0f),
						 std::make_pair(2.0f, 2000.0f), std::make_pair(400.0f, 400.0f) }) {
		int w = 0, h = 0;
		WaterShore::ChooseResolution(extent.first, extent.second, w, h);
		CHECK(w >= WaterShore::kMinTexels && h >= WaterShore::kMinTexels);
		CHECK(w <= WaterShore::kMaxTexelsPerSide && h <= WaterShore::kMaxTexelsPerSide);
		CHECK(w * h <= WaterShore::kMaxTexelsTotal + 2 * WaterShore::kMaxTexelsPerSide);   // ceil() may add one row and column
	}
	int pondW = 0, pondH = 0, lakeW = 0, lakeH = 0;
	WaterShore::ChooseResolution(20.0f, 20.0f, pondW, pondH);
	WaterShore::ChooseResolution(2000.0f, 2000.0f, lakeW, lakeH);
	CHECK(20.0f / pondW < 2000.0f / lakeW);   // a pond's texels are finer than a lake's
}

// MaxDepthIn must never report less than the map holds anywhere in the rectangle (it decides which tiles are skipped).
static void TestMaxDepthIn()
{
	// A 16 x 16 map of dry land (-5) with one wet texel (+3) at texel (12, 4).
	std::vector<float> depth(16 * 16, -5.0f);
	depth[4 * 16 + 12] = 3.0f;
	WaterShore::DepthField field;
	CHECK(field.Reset(16, 16, depth));
	// The whole map sees the wet texel.
	CHECK(field.MaxDepthIn(-1.0f, 1.0f, -1.0f, 1.0f) == 3.0f);
	// The far corner (left, back) is dry.
	CHECK(field.MaxDepthIn(-1.0f, -0.6f, 0.6f, 1.0f) == -5.0f);
	// A rectangle that only touches the wet texel's neighbour still reports it (one texel of border: bilinear reads blend it in).
	CHECK(field.MaxDepthIn(0.55f, 0.70f, -0.62f, -0.55f) == 3.0f);
	// Whatever rectangle we ask about, no sampled value inside it may exceed the answer.
	std::uint32_t state = 12345u;
	for (int i = 0; i < 500; ++i) {
		const float ax = Random01(state) * 2.0f - 1.0f, bx = Random01(state) * 2.0f - 1.0f;
		const float az = Random01(state) * 2.0f - 1.0f, bz = Random01(state) * 2.0f - 1.0f;
		const float x0 = std::min(ax, bx), x1 = std::max(ax, bx), z0 = std::min(az, bz), z1 = std::max(az, bz);
		const float bound = field.MaxDepthIn(x0, x1, z0, z1);
		for (int j = 0; j < 20; ++j) {
			const float x = x0 + (x1 - x0) * Random01(state), z = z0 + (z1 - z0) * Random01(state);
			CHECK(field.Sample(x, z) <= bound + 1e-4f);
		}
	}
	CHECK(WaterShore::DepthField().MaxDepthIn(-1.0f, 1.0f, -1.0f, 1.0f) == WaterShore::kDeepWater);   // no map: deep
}

// The spectrum sea (lesson 34): whatever the settings, the table must be safe (no fold-over, bounds that hold), repeatable,
// and shaped as promised (wind waves first, swell longer, groups only turn waves down).
static void TestSpectrum()
{
	std::uint32_t seed = 777;
	for (int round = 0; round < 300; ++round) {
		WaterWaves::Params p;
		p.amplitude = 0.02f + Random01(seed) * 1.5f;
		p.frequency = 0.2f + Random01(seed) * 1.6f;
		p.speed = 0.3f + Random01(seed) * 2.0f;
		p.windDegrees = Random01(seed) * 360.0f;
		p.spread = Random01(seed);
		p.choppiness = Random01(seed);
		p.count = 2 + static_cast<int>(Random01(seed) * 10.99f);
		p.roughness = Random01(seed);
		p.swell = Random01(seed) < 0.3f ? 0.0f : Random01(seed);
		p.swellAngle = Random01(seed) * 180.0f - 90.0f;
		p.swellLength = 1.5f + Random01(seed) * 4.0f;
		p.groups = Random01(seed) < 0.3f ? 0.0f : Random01(seed) * 0.9f;
		p.seed = static_cast<std::uint32_t>(Random01(seed) * 1000.0f);
		p.shoreWaves = Random01(seed) < 0.5f ? 0.0f : Random01(seed);
		if (Random01(seed) < 0.2f) p.flowSpeed = 0.5f + Random01(seed) * 4.0f;   // a river
		const WaterWaves::Table t = WaterWaves::Build(p);

		CHECK(t.count == std::clamp(p.count, 1, WaterWaves::kMaxWaves));
		// Every wave is well formed.
		float sumQKA = 0.0f, tallest = 0.0f;
		for (int i = 0; i < t.count; ++i) {
			const auto& w = t.waves[i];
			CHECK(std::fabs(w.dirX * w.dirX + w.dirZ * w.dirZ - 1.0f) < 1e-4f);
			CHECK(w.k > 0.0f && w.amplitude > 0.0f && w.omega > 0.0f);
			CHECK(w.steepness >= 0.0f && w.steepness <= 1.0f + 1e-6f);
			if (p.flowSpeed > 0.0f) CHECK(std::fabs(w.omega / w.k - p.flowSpeed) < 1e-3f);   // a river carries every wave at the current's speed
			sumQKA += w.steepness * w.k * w.amplitude;
			tallest += w.amplitude;
		}
		// No fold-over: the sum of Q*k*a never passes the choppiness setting.
		CHECK(sumQKA <= p.choppiness + 1e-4f);
		CHECK(t.heightBound >= tallest - 1e-4f);
		// The bounds hold, wave groups included, at random places and times.
		for (int n = 0; n < 40; ++n) {
			const float x = Random01(seed) * 400.0f - 200.0f, z = Random01(seed) * 400.0f - 200.0f, time = Random01(seed) * 200.0f;
			float dx, dy, dz;
			WaterWaves::Displacement(t, x, z, time, dx, dy, dz);
			CHECK(std::fabs(dy) <= t.heightBound + 1e-4f);
			CHECK(std::hypot(dx, dz) <= t.heightBound + 1e-4f);
			const float group = WaterWaves::GroupFactor(t, x, z, time);
			CHECK(group <= 1.0f + 1e-6f && group >= 1.0f - t.groupStrength - 1e-6f);
		}
		// Repeatable: the same settings make the same sea; another seed makes another one (unless there is a single wave).
		const WaterWaves::Table again = WaterWaves::Build(p);
		CHECK(again.count == t.count && again.waves[0].k == t.waves[0].k && again.waves[t.count - 1].phase0 == t.waves[t.count - 1].phase0);
		// Shape: wind waves come first and get finer and lower; the swell (if any) comes after and is longer than the biggest wind wave.
		const bool hasSwell = p.swell > 0.0f && p.flowSpeed <= 0.0f && t.count >= 2;
		int longer = 0;
		for (int i = 0; i < t.count; ++i) if (t.waves[i].k < p.frequency * 0.9999f) ++longer;
		CHECK(hasSwell ? (longer >= 1 && longer <= 3) : longer == 0);
		CHECK(std::fabs(t.waves[0].k - p.frequency) < 1e-5f);
		const float boost = 1.0f / (1.0f - 0.5f * std::clamp(p.groups, 0.0f, 0.9f));
		CHECK(std::fabs(t.waves[0].amplitude - p.amplitude * boost) < 1e-4f * std::max(1.0f, p.amplitude));
	}
	{
		WaterWaves::Params a, b;
		b.seed = 2;
		CHECK(WaterWaves::Build(a).waves[1].phase0 != WaterWaves::Build(b).waves[1].phase0);
	}
	// Wind waves: finer and lower, one after another.
	{
		WaterWaves::Params p;
		p.count = 6;
		const WaterWaves::Table t = WaterWaves::Build(p);
		for (int i = 1; i < t.count; ++i) {
			CHECK(t.waves[i].k > t.waves[i - 1].k * 1.1f);
			CHECK(t.waves[i].amplitude < t.waves[i - 1].amplitude);
		}
	}
	// Wave groups: with the strength at 0 the factor is exactly 1; otherwise it averages about 1 - strength / 2.
	{
		WaterWaves::Params p;
		p.groups = 0.6f;
		const WaterWaves::Table t = WaterWaves::Build(p);
		double sum = 0.0;
		const int n = 20000;
		for (int i = 0; i < n; ++i) sum += WaterWaves::GroupFactor(t, Random01(seed) * 2000.0f, Random01(seed) * 2000.0f, Random01(seed) * 500.0f);
		CHECK(std::fabs(sum / n - (1.0 - 0.6 / 2.0)) < 0.03);
		p.groups = 0.0f;
		CHECK(WaterWaves::GroupFactor(WaterWaves::Build(p), 12.0f, 34.0f, 5.0f) == 1.0f);
	}
	// The classic table is exactly the original one.
	{
		WaterWaves::Params p;
		p.style = WaterWaves::Style::Classic;
		p.groups = 0.5f; p.swell = 1.0f;   // ignored by Classic
		const WaterWaves::Table t = WaterWaves::Build(p);
		CHECK(t.groupStrength == 0.0f);
		CHECK(std::fabs(t.waves[1].k - p.frequency * 1.7f) < 1e-6f);
		CHECK(std::fabs(t.waves[1].amplitude - p.amplitude * 0.55f) < 1e-6f);
		CHECK(std::fabs(t.waves[3].phase0 - 4.1f) < 1e-6f);
	}
	// The CPU height still inverts correctly with groups and a swell on, at the sharpest choppiness.
	{
		WaterWaves::Params p;
		p.choppiness = 0.9f; p.amplitude = 0.6f; p.count = 8; p.swell = 0.7f; p.groups = 0.5f;
		const WaterWaves::Table t = WaterWaves::Build(p);
		for (int n = 0; n < 1000; ++n) {
			const float x = Random01(seed) * 200.0f - 100.0f, z = Random01(seed) * 200.0f - 100.0f, time = Random01(seed) * 100.0f;
			const float height = WaterWaves::HeightAt(t, x, z, time);
			float rx = x, rz = z, dx, dy, dz;
			for (int it = 0; it < 200; ++it) { WaterWaves::Displacement(t, rx, rz, time, dx, dy, dz); rx = x - dx; rz = z - dz; }
			WaterWaves::Displacement(t, rx, rz, time, dx, dy, dz);
			CHECK(std::fabs(dy - height) < 0.02f);
		}
	}
}

// The tile count grows with the body so the finest cell stays small (lesson 34); the old behaviour stays the default.
static void TestTileGrowth()
{
	// Small bodies: unchanged.
	auto g = WaterMesh::ChooseTileGrid(180, 180, -1, 32);       CHECK(g.x == 8 && g.z == 8);
	g = WaterMesh::ChooseTileGrid(400, 50, -1, 32);             CHECK(g.x == 16 && g.z == 2);
	// Bigger: more tiles, in steps.
	g = WaterMesh::ChooseTileGrid(300, 300, -1, 32);            CHECK(g.x == 12 && g.z == 12);     // 300 / 32 = 9.4 tiles -> 12
	g = WaterMesh::ChooseTileGrid(640, 640, -1, 32);            CHECK(g.x == 20 && g.z == 20);
	// Over the per-axis cap and the vertex budget: 20 x 20 (400 tiles) is the most.
	g = WaterMesh::ChooseTileGrid(2000, 2000, -1, 32);          CHECK(g.x == 20 && g.z == 20);
	// Finer tiles (64 cells) allow fewer of them: this stays at the base grid.
	g = WaterMesh::ChooseTileGrid(1000, 1000, -1, 64);          CHECK(g.x == 8 && g.z == 8);
	// A long river grows along its length only.
	g = WaterMesh::ChooseTileGrid(2000, 50, -1, 32);            CHECK(g.x == 24 && g.z == 2);
	// Without cellsPerTile nothing changes (the old table).
	g = WaterMesh::ChooseTileGrid(2000, 2000);                  CHECK(g.x == 8 && g.z == 8);
	// Always at least the base grid, never more than the budget, steps only; and the finest cell is on target when not capped.
	std::uint32_t seed = 4242;
	for (int i = 0; i < 2000; ++i) {
		const float ex = 5.0f + Random01(seed) * 1500.0f, ez = 5.0f + Random01(seed) * 1500.0f;
		const int cells = (Random01(seed) < 0.5f) ? 32 : (Random01(seed) < 0.5f ? 16 : 64);
		const float target = 0.5f + Random01(seed) * 1.5f;
		const auto base = WaterMesh::ChooseTileGrid(ex, ez);
		const auto grown = WaterMesh::ChooseTileGrid(ex, ez, -1, cells, target);
		CHECK(grown.x >= base.x && grown.z >= base.z);
		CHECK(grown.x * grown.z <= (std::max)(WaterMesh::MaxTiles(cells), base.x * base.z));
		CHECK(grown.x <= WaterMesh::kMaxTilesPerAxis && grown.z <= WaterMesh::kMaxTilesPerAxis);
		// When the grid that the sizes call for fits the budget, that is exactly what is built, and its cells are on target
		// (the tile count is rounded UP to a step, so cells are never coarser than asked, unless the per-axis cap was hit).
		const float tileWorld = cells * target;
		const int needX = WaterMesh::StepAtLeast(static_cast<int>(std::ceil(ex / tileWorld)), base.x);
		const int needZ = WaterMesh::StepAtLeast(static_cast<int>(std::ceil(ez / tileWorld)), base.z);
		if (needX * needZ <= WaterMesh::MaxTiles(cells)) {
			CHECK(grown.x == needX && grown.z == needZ);
			if (needX < WaterMesh::kMaxTilesPerAxis) CHECK(ex / (grown.x * cells) <= target + 1e-3f);
			if (needZ < WaterMesh::kMaxTilesPerAxis) CHECK(ez / (grown.z * cells) <= target + 1e-3f);
		}
	}
}

// Which way the land lies (lesson 35): the field must point toward the land in WORLD space, whatever the body's turn and scale.
static void TestShoreDirections()
{
	const float kPi = 3.14159265f;
	// A beach that rises toward +x, on a body turned by `yaw` and stretched to `sx` x `sz` world units.
	auto bakeBeach = [](float yaw, float sx, float sz) {
		const float c = std::cos(yaw), s = std::sin(yaw);
		return WaterShore::Bake(128, 128, 0.0f,
			[=](float lx, float lz, float& wx, float& wz) { const float x = lx * sx * 0.5f, z = lz * sz * 0.5f; wx = x * c + z * s; wz = -x * s + z * c; },
			[](float wx, float, float& groundY) { groundY = 0.2f * wx; return true; });   // ground height = 20% slope toward +x
	};
	for (float yaw : { 0.0f, 0.6f, 2.0f, -1.3f }) {
		const float sx = 300.0f, sz = 120.0f;
		const WaterShore::DepthField field = bakeBeach(yaw, sx, sz);
		CHECK(field.HasDirections());
		const float c = std::cos(yaw), s = std::sin(yaw);
		// Several spots in the middle of the body: in the world, the land is always toward +x.
		for (float lx : { -0.4f, 0.0f, 0.3f }) for (float lz : { -0.3f, 0.2f }) {
			float wx, wz;
			field.SampleToward(lx, lz, wx, wz);
			CHECK(wx > 0.95f && std::fabs(wz) < 0.05f);
			(void)c; (void)s;
		}
	}
	// An island: from every side the land lies toward the middle.
	{
		const WaterShore::DepthField field = WaterShore::Bake(160, 160, 0.0f,
			[](float lx, float lz, float& wx, float& wz) { wx = lx * 100.0f; wz = lz * 100.0f; },
			[](float wx, float wz, float& groundY) { const float r = std::hypot(wx, wz); groundY = -6.0f + 0.3f * (40.0f - r); return true; });
		for (int i = 0; i < 8; ++i) {
			const float angle = i * kPi / 4.0f;
			const float lx = 0.30f * std::cos(angle), lz = 0.30f * std::sin(angle);   // 30 units from the middle: open water
			float wx, wz;
			field.SampleToward(lx, lz, wx, wz);
			CHECK(wx * -std::cos(angle) + wz * -std::sin(angle) > 0.9f);   // points back toward the island
		}
	}
	// Flat deep water: no direction to speak of.
	{
		const WaterShore::DepthField field = WaterShore::Bake(64, 64, 0.0f,
			[](float lx, float lz, float& wx, float& wz) { wx = lx * 100.0f; wz = lz * 100.0f; },
			[](float, float, float& groundY) { groundY = -50.0f; return true; });
		for (size_t i = 0; i < field.TowardX().size(); ++i) CHECK(std::hypot(field.TowardX()[i], field.TowardZ()[i]) < 0.01f);
	}
	// Never longer than 1; the packed texels hold depth first.
	{
		const WaterShore::DepthField field = bakeBeach(0.4f, 200.0f, 200.0f);
		for (size_t i = 0; i < field.TowardX().size(); ++i) CHECK(std::hypot(field.TowardX()[i], field.TowardZ()[i]) <= 1.0001f);
		const std::vector<float> packed = field.PackedTexels();
		CHECK(packed.size() == field.Data().size() * 4);
		for (size_t i = 0; i < field.Data().size(); i += 97) CHECK(packed[i * 4] == field.Data()[i] && packed[i * 4 + 1] == field.TowardX()[i]);
	}
	// No map, no directions: reads as "not sure".
	{
		WaterShore::DepthField empty;
		float wx = 1.0f, wz = 1.0f;
		empty.SampleToward(0.0f, 0.0f, wx, wz);
		CHECK(wx == 0.0f && wz == 0.0f);
	}
}

// Waves running away from a nearby shore are turned down; waves running toward it, or far from any shore, are not.
static void TestShelter()
{
	WaterWaves::Params p;
	p.windDegrees = 0.0f;   // the waves travel toward +x
	p.count = 6; p.choppiness = 0.6f; p.amplitude = 0.4f; p.spread = 0.5f;
	const WaterWaves::Table table = WaterWaves::Build(p);
	std::uint32_t seed = 31337;
	auto rms = [&](float towardX, float shelter, float depth) {
		WaterWaves::ShoreSample shore;
		shore.depth = depth; shore.towardX = towardX;
		double sum = 0.0;
		std::uint32_t local = 99;
		const int n = 3000;
		for (int i = 0; i < n; ++i) {
			const float x = Random01(local) * 300.0f, z = Random01(local) * 300.0f, time = Random01(local) * 60.0f;
			float dx, dy, dz;
			WaterWaves::Displacement(table, x, z, time, dx, dy, dz, shore, 0.0f, shelter);
			sum += static_cast<double>(dy) * dy;
		}
		(void)seed;
		return std::sqrt(sum / n);
	};
	const double open = rms(0.0f, 0.0f, 1.0f);
	// Land ahead (+x): the waves run toward it: untouched.
	CHECK(std::fabs(rms(1.0f, 0.8f, 1.0f) - open) < 0.03 * open);
	// Land behind (-x): the waves run away from it: clearly lower.
	CHECK(rms(-1.0f, 0.8f, 1.0f) < 0.75 * open);
	// Land to the side: the waves neither approach nor leave: nearly untouched (the spread sends some off-axis).
	WaterWaves::ShoreSample side;
	side.depth = 1.0f; side.towardZ = 1.0f;
	float a, b, c, d, e, f;
	WaterWaves::Displacement(table, 10.0f, 20.0f, 3.0f, a, b, c, side, 0.0f, 0.8f);
	WaterWaves::Displacement(table, 10.0f, 20.0f, 3.0f, d, e, f, side, 0.0f, 0.0f);
	CHECK(std::fabs(b - e) < 0.35f * table.heightBound);
	// Far from any shore (deep water): shelter changes nothing, even with land "behind".
	CHECK(std::fabs(rms(-1.0f, 0.8f, 1000.0f) - open) < 1e-6);
	// Off means off.
	CHECK(std::fabs(rms(-1.0f, 0.0f, 1.0f) - open) < 1e-9);
	// The factor stays in range, and the bounds still hold in the lee.
	for (int i = 0; i < table.count; ++i) {
		for (float depth : { 0.0f, 0.5f, 3.0f, 30.0f })
			for (float toward : { -1.0f, -0.4f, 0.0f, 0.7f, 1.0f }) {
				WaterWaves::ShoreSample shore; shore.depth = depth; shore.towardX = toward;
				const float f2 = WaterWaves::ShelterFactor(table.waves[i], shore, 1.0f);
				CHECK(f2 >= 0.05f - 1e-6f && f2 <= 1.0f + 1e-6f);
			}
	}
	for (int n = 0; n < 500; ++n) {
		WaterWaves::ShoreSample shore; shore.depth = 0.5f; shore.towardX = -1.0f;
		float dx, dy, dz;
		WaterWaves::Displacement(table, Random01(seed) * 200.0f, Random01(seed) * 200.0f, Random01(seed) * 50.0f, dx, dy, dz, shore, 1.0f, 1.0f);
		CHECK(std::fabs(dy) <= table.heightBound + 1e-4f);
	}
	// HeightAtShore with a shore direction still inverts correctly.
	for (int n = 0; n < 300; ++n) {
		const float x = Random01(seed) * 100.0f, z = Random01(seed) * 100.0f, time = Random01(seed) * 40.0f;
		WaterWaves::ShoreSample shore; shore.depth = 2.0f; shore.towardX = -0.8f; shore.towardZ = 0.3f;
		const float height = WaterWaves::HeightAtShore(table, x, z, time, 1.0f, 0.7f, [&](float, float) { return shore; });
		float rx = x, rz = z, dx, dy, dz;
		for (int it = 0; it < 200; ++it) { WaterWaves::Displacement(table, rx, rz, time, dx, dy, dz, shore, 1.0f, 0.7f); rx = x - dx; rz = z - dz; }
		WaterWaves::Displacement(table, rx, rz, time, dx, dy, dz, shore, 1.0f, 0.7f);
		CHECK(std::fabs(dy - height) < 0.02f);
	}
}

// Which settings each kind of water shows (lesson 35), and the scene wind's limits.
static void TestTypeUiAndWeather()
{
	using WaterTypeUi::Control;
	using WaterTypeUi::IsShown;
	const WaterBodyType types[] = { WaterBodyType::Ocean, WaterBodyType::Lake, WaterBodyType::River, WaterBodyType::Pond, WaterBodyType::Custom };
	// Custom shows everything; so does "show all" for any type.
	for (int c = 0; c < static_cast<int>(Control::Count); ++c) {
		CHECK(IsShown(WaterBodyType::Custom, static_cast<Control>(c)));
		for (WaterBodyType type : types) CHECK(IsShown(type, static_cast<Control>(c), true));
		// Every control is shown by at least one real type (a control nobody sees would be dead code).
		bool anyone = false;
		for (WaterBodyType type : types) anyone = anyone || IsShown(type, static_cast<Control>(c));
		CHECK(anyone);
	}
	// A river has a current, not a swell or a wind direction of its own; an ocean has both.
	CHECK(!IsShown(WaterBodyType::River, Control::Swell));
	CHECK(!IsShown(WaterBodyType::River, Control::WindSource));
	CHECK(!IsShown(WaterBodyType::River, Control::ShoreSection));
	CHECK(IsShown(WaterBodyType::River, Control::Current) && !IsShown(WaterBodyType::Ocean, Control::Current) && !IsShown(WaterBodyType::Lake, Control::Current));
	CHECK(IsShown(WaterBodyType::Ocean, Control::Swell));
	CHECK(IsShown(WaterBodyType::Ocean, Control::WindSource));
	CHECK(IsShown(WaterBodyType::Ocean, Control::WaveModel));
	// A lake is chop: wind and roughness, no swell, no wave model switch. A pond shows almost nothing.
	CHECK(IsShown(WaterBodyType::Lake, Control::WindSource) && !IsShown(WaterBodyType::Lake, Control::Swell) && !IsShown(WaterBodyType::Lake, Control::WaveModel));
	CHECK(!IsShown(WaterBodyType::Pond, Control::WindSource) && !IsShown(WaterBodyType::Pond, Control::Swell) && !IsShown(WaterBodyType::Pond, Control::WavePresets));
	// Each ordinary type shows strictly fewer settings than Custom.
	for (WaterBodyType type : { WaterBodyType::Ocean, WaterBodyType::Lake, WaterBodyType::River, WaterBodyType::Pond }) {
		int shown = 0;
		for (int c = 0; c < static_cast<int>(Control::Count); ++c) if (IsShown(type, static_cast<Control>(c))) ++shown;
		CHECK(shown < static_cast<int>(Control::Count));
	}
	// The saved value of the first four types never moves (scene files); Custom is new, at the end.
	CHECK(static_cast<int>(WaterBodyType::Ocean) == 0 && static_cast<int>(WaterBodyType::Pond) == 3 && static_cast<int>(WaterBodyType::Custom) == 4);

	// Wind: the direction wraps, the strength clamps, the wave scale follows the strength.
	WindSettings wind;
	wind.direction = -90.0f; wind.strength = 7.0f;
	wind.Clamp();
	CHECK(std::fabs(wind.direction - 270.0f) < 1e-4f && wind.strength == WindSettings::kStrengthMax);
	wind.direction = 725.0f; wind.strength = -1.0f;
	wind.Clamp();
	CHECK(std::fabs(wind.direction - 5.0f) < 1e-3f && wind.strength == 0.0f);
	WindSettings calm; calm.strength = 0.0f;
	CHECK(WindWaveScale(calm) == 0.0f);
	WindSettings normal;
	CHECK(WindWaveScale(normal) == 1.0f);
}

int main()
{
	TestTileIndices();
	TestTileGrid();
	TestWaves();
	TestRiverFlow();
	TestShoaling();
	TestShoreMap();
	TestSpectrum();
	TestTileGrowth();
	TestShoreDirections();
	TestShelter();
	TestTypeUiAndWeather();
	TestMaxDepthIn();
	if (g_failures == 0) std::printf("water tests: all passed\n");
	return g_failures == 0 ? 0 : 1;
}
