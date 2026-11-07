#pragma once
// Index lists for one water TILE (a square patch of the water grid), at several levels of detail.
// Plain C++, no Direct3D. Vertices are laid out row by row: index = z * (cells + 1) + x.
//
// THE RULE THAT PREVENTS CRACKS: whatever the level, a tile's four BORDER edges keep every vertex. Two neighbouring
// tiles at different levels therefore share exactly the same border vertices, so no gap can open between them (the
// terrain needs skirts for that; the water, which is see-through, could not hide them). Only the INTERIOR gets
// coarser: it is built from big cells of `stride` x `stride` small cells, and the big cells that touch the border
// are fanned out from their centre vertex to the border's full-resolution vertices.
#include <cstdint>
#include <vector>

namespace WaterMesh {

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
