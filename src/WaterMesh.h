#pragma once
// Index lists for one water TILE (a square patch of the water grid), at several levels of detail.
// Plain C++, no Direct3D. Vertices are laid out row by row: index = z * (cells + 1) + x.
//
// THE RULE THAT PREVENTS CRACKS: whatever the level, a tile's four BORDER edges keep every vertex. Two neighbouring
// tiles at different levels therefore share exactly the same border vertices, so no gap can open between them (the
// terrain needs skirts for that; the water, which is see-through, could not hide them). Only the INTERIOR gets
// coarser: it is built from big cells of `stride` x `stride` small cells, and the big cells that touch the border
// are fanned out from their centre vertex to the border's full-resolution vertices.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace WaterMesh {

// How many tiles the water is cut into along each side. A square lake is 8 x 8. A long river is cut into a few long
// rows instead (16 x 2, say), so the tiles stay square and the grid cells stay square: no vertices wasted on
// a thin strip, none missing along its length.
struct TileGrid {
	int x = 8, z = 8;
	int shapeClass = 0;   // 0..4: how stretched the water is (ratio 1, 2, 4, 8, 16). Used for hysteresis
	bool operator==(const TileGrid& o) const { return x == o.x && z == o.z; }
	bool operator!=(const TileGrid& o) const { return !(*this == o); }
};

// How the tile count grows with the body's size (lesson 34).
//
// A tile always has the same number of cells, so on a bigger body the cells get bigger too. The shader fades out every wave
// that a cell is too coarse to show, so on a big plane only the longest waves survive and the sea goes flat. To keep the
// finest cells near `targetCell` world units, big bodies are cut into MORE tiles.
//
//  * The count is quantised (kSteps) so that dragging a size slider rebuilds the mesh a few times, not every frame.
//  * Every tile costs memory, so the total is capped by a vertex budget: finer tiles (more cells each) allow fewer of them.
constexpr int kSteps[] = { 1, 2, 3, 4, 6, 8, 12, 16, 20, 24 };
constexpr int kMaxTilesPerAxis = 24;
constexpr int kVertexBudget = 450000;   // vertices over all tiles at the finest level (about 14 MB with 32-byte vertices)

inline int MaxTiles(int cellsPerTile)
{
	const int row = cellsPerTile + 1;
	return std::clamp(kVertexBudget / (row * row), 64, 400);
}

// The smallest step that is at least `wanted` (and at least `atLeast`), never above the per-axis limit.
inline int StepAtLeast(int wanted, int atLeast)
{
	wanted = (std::max)(wanted, atLeast);
	for (int step : kSteps) if (step >= wanted) return (std::min)(step, kMaxTilesPerAxis);
	return kMaxTilesPerAxis;
}

// The step below `count` (but not below `floor`), or `count` itself if there is none.
inline int StepBelow(int count, int floor)
{
	int best = count;
	for (int step : kSteps) if (step < count && step >= floor) best = step;
	return best;
}

// `previousClass`: the class of the grid that is built now (-1 = none). The class only changes once the shape has clearly
// moved past a boundary, so dragging a size slider near a boundary does not rebuild the mesh every frame.
// `cellsPerTile` > 0 turns on the growth with size described above; 0 keeps the base grid (the shape table alone).
inline TileGrid ChooseTileGrid(float extentX, float extentZ, int previousClass = -1, int cellsPerTile = 0, float targetCell = 1.0f)
{
	static const int kLong[5]  = { 8, 12, 16, 16, 16 };
	static const int kShort[5] = { 8, 6, 4, 2, 1 };
	const float longSide = (std::max)((std::max)(extentX, extentZ), 1e-3f);
	const float shortSide = (std::max)((std::min)(extentX, extentZ), 1e-3f);
	const float ideal = std::log2(longSide / shortSide);   // 0 = square, 1 = twice as long as wide, ...
	int shapeClass = std::clamp(static_cast<int>(std::lround(ideal)), 0, 4);
	if (previousClass >= 0 && previousClass <= 4 && std::fabs(ideal - static_cast<float>(previousClass)) < 0.75f) shapeClass = previousClass;
	TileGrid grid;
	grid.shapeClass = shapeClass;
	const bool alongX = extentX >= extentZ;
	grid.x = alongX ? kLong[shapeClass] : kShort[shapeClass];
	grid.z = alongX ? kShort[shapeClass] : kLong[shapeClass];
	if (cellsPerTile <= 0) return grid;

	// Tiles needed along each side so that one cell is about `targetCell` wide.
	const float tileWorld = static_cast<float>(cellsPerTile) * (std::max)(targetCell, 0.05f);
	const int baseX = grid.x, baseZ = grid.z;
	grid.x = StepAtLeast(static_cast<int>(std::ceil(extentX / tileWorld)), baseX);
	grid.z = StepAtLeast(static_cast<int>(std::ceil(extentZ / tileWorld)), baseZ);
	// Over the budget: take sides down a step at a time, never below the base grid, and always the side whose cells are the
	// FINEST (the one that can best afford it), so a square body stays square instead of ending up 8 x 12.
	const int budget = MaxTiles(cellsPerTile);
	while (grid.x * grid.z > budget) {
		const bool canX = grid.x > baseX, canZ = grid.z > baseZ;
		if (!canX && !canZ) break;   // already the base grid
		const float cellX = extentX / static_cast<float>(grid.x), cellZ = extentZ / static_cast<float>(grid.z);
		if (canX && canZ && std::fabs(cellX - cellZ) <= 1e-3f * (std::max)(cellX, cellZ)) {   // equally fine: a square stays square
			grid.x = StepBelow(grid.x, baseX);
			grid.z = StepBelow(grid.z, baseZ);
		} else if (canX && (!canZ || cellX < cellZ)) grid.x = StepBelow(grid.x, baseX);
		else grid.z = StepBelow(grid.z, baseZ);
	}
	return grid;
}

inline std::vector<uint32_t> BuildTileIndices(int cells, int stride)
{
	std::vector<uint32_t> out;
	if (cells < 1) return out;
	if (stride < 1 || cells % stride != 0 || (stride > 1 && (stride % 2) != 0)) stride = 1;
	const int row = cells + 1;
	auto at = [row](int x, int z) { return static_cast<uint32_t>(z * row + x); };
	const int big = cells / stride;

	for (int cz = 0; cz < big; ++cz) {
		for (int cx = 0; cx < big; ++cx) {
			const int x0 = cx * stride, z0 = cz * stride, x1 = x0 + stride, z1 = z0 + stride;
			const bool left = cx == 0, right = cx == big - 1, bottom = cz == 0, top = cz == big - 1;

			if (stride == 1 || (!left && !right && !bottom && !top)) {
				// Corners: a=(x0,z0) b=(x0,z1) c=(x1,z1) d=(x1,z0). Clockwise seen from above = front face.
				const uint32_t a = at(x0, z0), b = at(x0, z1), c = at(x1, z1), d = at(x1, z0);
				out.insert(out.end(), { a, b, c, a, c, d });
				continue;
			}

			// A big cell on the tile border: walk its outline in the same order (a -> b -> c -> d), listing every
			// vertex on edges that lie on the border and only the corners elsewhere, then fan from the centre.
			std::vector<uint32_t> loop;
			const int step = 1;
			// a -> b: the x0 edge, z rising
			for (int z = z0; z < z1; z += (left ? step : stride)) loop.push_back(at(x0, z));
			// b -> c: the z1 edge, x rising
			for (int x = x0; x < x1; x += (top ? step : stride)) loop.push_back(at(x, z1));
			// c -> d: the x1 edge, z falling
			for (int z = z1; z > z0; z -= (right ? step : stride)) loop.push_back(at(x1, z));
			// d -> a: the z0 edge, x falling
			for (int x = x1; x > x0; x -= (bottom ? step : stride)) loop.push_back(at(x, z0));

			const uint32_t centre = at(x0 + stride / 2, z0 + stride / 2);
			for (size_t i = 0; i < loop.size(); ++i) {
				out.push_back(centre);
				out.push_back(loop[i]);
				out.push_back(loop[(i + 1) % loop.size()]);
			}
		}
	}
	return out;
}

}   // namespace WaterMesh
