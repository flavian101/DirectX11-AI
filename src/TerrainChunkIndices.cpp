#include "TerrainChunkIndices.h"
#include <utility>
#include <cstdint>

std::vector<MeshLevel> BuildChunkIndexLevels(int cellsInX, int cellsInZ, int lodCount)
{
	const int vertsX = cellsInX + 1;
	const int gridVertexCount = vertsX * (cellsInZ + 1);   // the surface's vertices; the skirt vertices (made by BuildChunkVertices) follow them
	const int northBase = gridVertexCount;                       // z = 0 edge, one per x
	const int southBase = northBase + (cellsInX + 1);             // z = cellsInZ edge
	const int westBase = southBase + (cellsInX + 1);              // x = 0 edge, one per z
	const int eastBase = westBase + (cellsInZ + 1);               // x = cellsInX edge
	const int capBase = eastBase + (cellsInZ + 1);                // the four corners of the bottom: north-west, north-east, south-west, south-east
	auto grid = [&](int x, int z) { return static_cast<uint32_t>(z * vertsX + x); };

	// LEVELS OF DETAIL: the same vertices, connected more coarsely. Level L skips grid lines
	// so that only every (2^L)-th vertex is used. The last line is always kept, so the tile's
	// outline never changes.
	std::vector<MeshLevel> levels;
	for (int lod = 0; lod < lodCount; ++lod) {
		const int step = 1 << lod;
		auto lines = [step](int cells) {
			std::vector<int> positions;
			for (int i = 0; i < cells; i += step) positions.push_back(i);
			positions.push_back(cells);
			return positions;
		};
		const std::vector<int> xs = lines(cellsInX);
		const std::vector<int> zs = lines(cellsInZ);

		std::vector<uint32_t> indices;                 // the surface (part 0)
		std::vector<uint32_t> skirtIndices[4];         // north, south, west, east (parts 1..4)
		indices.reserve((xs.size() - 1) * (zs.size() - 1) * 6);

		// The surface itself (clockwise seen from above = front face).
		for (size_t zi = 0; zi + 1 < zs.size(); ++zi) {
			for (size_t xi = 0; xi + 1 < xs.size(); ++xi) {
				const uint32_t topLeft = grid(xs[xi], zs[zi]);
				const uint32_t topRight = grid(xs[xi + 1], zs[zi]);
				const uint32_t bottomLeft = grid(xs[xi], zs[zi + 1]);
				const uint32_t bottomRight = grid(xs[xi + 1], zs[zi + 1]);
				indices.insert(indices.end(), {topLeft, bottomLeft, topRight,
											   topRight, bottomLeft, bottomRight});
			}
		}
		// The skirts, wound so they face OUTWARD from the tile (the winding for each of the four
		// edges differs because "clockwise as seen from outside" depends on which side you stand on).
		for (size_t i = 0; i + 1 < xs.size(); ++i) {
			const int a = xs[i], b = xs[i + 1];
			{   // north edge (faces -z)
				const uint32_t ea = grid(a, 0), eb = grid(b, 0), sa = northBase + a, sb = northBase + b;
				skirtIndices[0].insert(skirtIndices[0].end(), {ea, eb, sa,  eb, sb, sa});
			}
			{   // south edge (faces +z)
				const uint32_t ea = grid(a, cellsInZ), eb = grid(b, cellsInZ), sa = southBase + a, sb = southBase + b;
				skirtIndices[1].insert(skirtIndices[1].end(), {eb, ea, sb,  ea, sa, sb});
			}
		}
		for (size_t i = 0; i + 1 < zs.size(); ++i) {
			const int a = zs[i], b = zs[i + 1];
			{   // west edge (faces -x)
				const uint32_t ea = grid(0, a), eb = grid(0, b), sa = westBase + a, sb = westBase + b;
				skirtIndices[2].insert(skirtIndices[2].end(), {eb, ea, sb,  ea, sa, sb});
			}
			{   // east edge (faces +x)
				const uint32_t ea = grid(cellsInX, a), eb = grid(cellsInX, b), sa = eastBase + a, sb = eastBase + b;
				skirtIndices[3].insert(skirtIndices[3].end(), {ea, eb, sa,  eb, sb, sa});
			}
		}
		// One index buffer per level: the surface first (part 0, always drawn), then the four skirts.
		MeshLevel level;
		level.partIndexCounts.push_back(static_cast<uint32_t>(indices.size()));
		level.indices = std::move(indices);
		for (int edge = 0; edge < 4; ++edge) {
			level.partIndexCounts.push_back(static_cast<uint32_t>(skirtIndices[edge].size()));
			level.indices.insert(level.indices.end(), skirtIndices[edge].begin(), skirtIndices[edge].end());
		}
		// Part 5: the bottom (roadmap X1), two triangles facing down on the four corner vertices at the floor. The same at every level of detail.
		{
			const uint32_t nw = capBase, ne = capBase + 1, sw = capBase + 2, se = capBase + 3;
			const uint32_t cap[6] = { nw, ne, sw,  ne, se, sw };
			level.partIndexCounts.push_back(6);
			level.indices.insert(level.indices.end(), cap, cap + 6);
		}
		levels.push_back(std::move(level));
	}
	return levels;
}
