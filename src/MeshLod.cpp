#include "MeshLod.h"
#include <meshoptimizer.h>
#include <algorithm>

namespace {
std::vector<std::uint32_t> CacheOptimized(const std::vector<std::uint32_t>& indices, std::size_t vertexCount, bool enabled)
{
    if (!enabled || indices.empty()) return indices;
    std::vector<std::uint32_t> result(indices.size());
    meshopt_optimizeVertexCache(result.data(), indices.data(), indices.size(), vertexCount);
    return result;
}
}  // namespace

std::vector<std::vector<std::uint32_t>> BuildMeshLods(const float* positions, std::size_t positionStride,
                                                      const float* attributes, std::size_t attributeStride, std::size_t attributeCount,
                                                      std::size_t vertexCount, const std::uint32_t* indices, std::size_t indexCount,
                                                      const MeshLodSettings& settings)
{
    std::vector<std::vector<std::uint32_t>> levels;
    if (!positions || !indices || vertexCount == 0 || indexCount < 3) return levels;
    indexCount -= indexCount % 3;

    levels.push_back(CacheOptimized(std::vector<std::uint32_t>(indices, indices + indexCount), vertexCount, settings.optimizeVertexCache));
    const int wanted = (std::min)((std::max)(settings.levels, 1), 4);
    if (wanted == 1 || indexCount / 3 < settings.minTriangles) return levels;

    // How strongly each attribute counts against the position error. Normals and UVs are 0..1-ish numbers while positions
    // are measured against the mesh's size, so a weight of 0.5 makes "a normal turned right around" about as costly as
    // "the surface moved by half the mesh's width". Seams and sharp edges survive; smooth curved parts still simplify well.
    float weights[16];
    const std::size_t attributeUsed = (attributes && attributeCount > 0) ? (std::min)(attributeCount, std::size_t{ 16 }) : 0;
    for (float& w : weights) w = 0.5f;

    const std::size_t floorIndices = static_cast<std::size_t>((std::max)(settings.floorTriangles, 1u)) * 3;
    for (int level = 1; level < wanted; ++level) {
        const std::vector<std::uint32_t>& previous = levels.back();
        const float ratio = settings.triangleRatio[level - 1];
        std::size_t target = static_cast<std::size_t>(static_cast<double>(indexCount) * ratio) / 3 * 3;
        target = (std::max)(target, floorIndices);
        if (target >= previous.size()) break;

        std::vector<std::uint32_t> simplified(previous.size());
        float error = 0.0f;
        std::size_t produced;
        if (attributeUsed > 0) {
            produced = meshopt_simplifyWithAttributes(simplified.data(), previous.data(), previous.size(), positions, vertexCount,
                                                      positionStride, attributes, attributeStride, weights, attributeUsed, nullptr,
                                                      target, settings.maxError, 0, &error);
        } else {
            produced = meshopt_simplify(simplified.data(), previous.data(), previous.size(), positions, vertexCount, positionStride,
                                        target, settings.maxError, 0, &error);
        }

        // The careful simplifier stops when the next collapse would break the error limit: foliage made of loose cards
        // and meshes full of UV seams often cannot get near the budget. For the far levels, where the object is a few
        // pixels wide, a rougher method that ignores attributes is better than keeping 3x the triangles we planned.
        if (level >= 2 && produced > target + target / 2) {
            std::vector<std::uint32_t> sloppy(previous.size());
            const std::size_t rough = meshopt_simplifySloppy(sloppy.data(), previous.data(), previous.size(), positions, vertexCount,
                                                             positionStride, target, settings.maxError * 2.0f, nullptr);
            if (rough >= 3 && rough < produced) { simplified.swap(sloppy); produced = rough; }
        }

        // Stop when a level barely differs from the one before it: it would cost memory and a switch for nothing.
        if (produced < 3 || produced + produced / 8 > previous.size()) break;
        simplified.resize(produced - produced % 3);
        levels.push_back(CacheOptimized(simplified, vertexCount, settings.optimizeVertexCache));
    }
    return levels;
}
