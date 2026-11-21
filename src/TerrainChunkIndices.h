#pragma once
// The index buffers of one terrain chunk (roadmap T3). They depend ONLY on the chunk's size in cells (cellsInX x cellsInZ), never
// on its heights or its place in the world: every chunk of 32 x 32 cells uses the very same buffers. They used to be built
// again for every chunk of every tile (64 chunks x 3 levels of detail x a surface and four skirts per tile); now one set per
// size is built once and shared.
#include "Mesh.h"
#include <vector>

// The levels of detail of a chunk of cellsInX x cellsInZ cells, as indices into the chunk's vertices: the surface's grid vertices
// first, then the four skirts (north, south, west, east), then the four corners of the bottom, exactly as Terrain's BuildChunkVertices lays them out. Level L skips grid
// lines so that only every (2^L)-th vertex is used; the last line is always kept so the outline never changes. Part 0 of a level
// is the surface, parts 1 to 4 the four skirts (wound to face outward), part 5 the bottom (two triangles facing down on four extra
// vertices after the skirts' vertices). Pure: no Direct3D object is made.
std::vector<MeshLevel> BuildChunkIndexLevels(int cellsInX, int cellsInZ, int lodCount);

// The same levels as GPU buffers, made once per (size, level count) and shared by every chunk of that size. Null if a buffer could
// not be made. The pointer stays valid until ReleaseSharedChunkIndices.
const SharedIndexLevels* GetSharedChunkIndices(ID3D11Device* device, int cellsInX, int cellsInZ, int lodCount);

// Drops the cache's own references (meshes keep theirs). Call it before the device is released; also fine to call at any time.
void ReleaseSharedChunkIndices();
