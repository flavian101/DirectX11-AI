// Unit tests for src/WaterWaves.h and src/WaterMesh.h. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
//  - tile index lists: watertight, right winding, full area, and the border keeps every vertex (so neighbouring tiles at
//    different levels of detail can never crack apart)
//  - the tile grid chosen for a body's proportions
//  - the wave table: the height-at-a-position inversion, no folding at full choppiness, and the exact normal/Jacobian the
//    pixel shader uses against a finite-difference normal of the real displaced surface
#include "WaterMesh.h"
#include "WaterWaves.h"
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

int main()
{
	TestTileIndices();
	TestTileGrid();
	TestWaves();
	if (g_failures == 0) std::printf("water tests: all passed\n");
	return g_failures == 0 ? 0 : 1;
}
