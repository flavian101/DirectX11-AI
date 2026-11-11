#pragma once
// Automatic levels of detail for a triangle mesh. Pure logic (no Direct3D, no DirectXMath): it wraps meshoptimizer
// (https://github.com/zeux/meshoptimizer) and is tested on its own: tests/mesh_lod_test.cpp.
//
// WHAT A LEVEL OF DETAIL IS HERE (lesson 31). Level 0 is the mesh as it came from the file. Every further level is the
// SAME vertices connected into fewer triangles: the simplifier repeatedly collapses the cheapest edge (the one whose
// removal changes the surface least) until the triangle budget is met. Because the vertex buffer is shared, a level is
// only a new index buffer: that is exactly what Mesh already supports (Mesh(device, vertices, lodIndices)).
//
// Each level is also re-ordered for the GPU's vertex cache (meshopt_optimizeVertexCache): the same triangles in an order
// that makes the GPU re-use transformed vertices instead of transforming them again. Free speed, even for level 0.
#include <cstddef>
#include <cstdint>
#include <vector>

struct MeshLodSettings {
    int levels = 4;                                        // how many levels to make, level 0 included (at most 6)
    float triangleRatio[3] = { 0.5f, 0.2f, 0.06f };        // the share of level 0's triangles that levels 1, 2 and 3 keep
    float maxError = 0.10f;                                // how far the surface may move, as a fraction of the mesh's size
    std::uint32_t minTriangles = 200;                      // a mesh smaller than this is left alone (one level)
    std::uint32_t floorTriangles = 8;                      // no level has fewer triangles than this
    bool optimizeVertexCache = true;
};

// `positions` points at the first vertex's x; consecutive vertices are `positionStride` bytes apart.
// `attributes` (optional, may be null) points at `attributeCount` contiguous floats per vertex (a normal and a UV, say)
// that the simplifier tries to preserve: without them, collapsing across a texture seam smears the texture.
// Returns one index list per level, finest first; fewer than settings.levels if simplification could not go further
// without exceeding maxError. Never returns an empty list for a non-empty mesh.
std::vector<std::vector<std::uint32_t>> BuildMeshLods(const float* positions, std::size_t positionStride,
                                                      const float* attributes, std::size_t attributeStride, std::size_t attributeCount,
                                                      std::size_t vertexCount, const std::uint32_t* indices, std::size_t indexCount,
                                                      const MeshLodSettings& settings);
