#include "Heightfield.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace {
constexpr int kMaxMarchSteps = 100000;   // a safety net: no ray may walk more steps than this
constexpr int kBisectSteps = 24;         // each step halves the bracket: 2^24 is far below any visible error
}

bool Heightfield::Reset(const HeightfieldLayout& layout, std::vector<float> samples)
{
    if (layout.cellsX < 1 || layout.cellsZ < 1 || !(layout.sizeX > 0.0f) || !(layout.sizeZ > 0.0f)) return false;
    const size_t expected = static_cast<size_t>(layout.cellsX + 1) * static_cast<size_t>(layout.cellsZ + 1);
    if (samples.size() != expected) return false;
    layout_ = layout;
    samples_ = std::move(samples);
    return true;
}

float Heightfield::Sample(int gridX, int gridZ) const
{
    if (samples_.empty()) return 0.0f;
    gridX = std::clamp(gridX, 0, layout_.cellsX);
    gridZ = std::clamp(gridZ, 0, layout_.cellsZ);
    return samples_[static_cast<size_t>(gridZ) * (layout_.cellsX + 1) + gridX];
}

// A cell is a square of four samples. The mesh cuts it into TWO triangles along the diagonal from bottom-left to
// top-right (Terrain.cpp: {topLeft, bottomLeft, topRight} and {topRight, bottomLeft, bottomRight}). A point inside the
// cell is at (fx, fz), each 0..1 across the cell. On that diagonal fx + fz == 1: below it the point is in the first
// triangle, above it in the second. The weights are the point's barycentric coordinates: how much of each corner it
// "is", which also makes them the interpolation weights the GPU uses for the same triangle.
bool Heightfield::Locate(float x, float z, Location& out) const
{
    if (samples_.empty()) return false;
    const float halfX = layout_.sizeX * 0.5f;
    const float halfZ = layout_.sizeZ * 0.5f;
    if (!(x >= -halfX && x <= halfX && z >= -halfZ && z <= halfZ)) return false;   // also rejects NaN

    const float gx = (x + halfX) / layout_.sizeX * static_cast<float>(layout_.cellsX);   // position in grid units
    const float gz = (z + halfZ) / layout_.sizeZ * static_cast<float>(layout_.cellsZ);
    const int ix = (std::min)(static_cast<int>(gx), layout_.cellsX - 1);   // gx >= 0, so truncating is flooring
    const int iz = (std::min)(static_cast<int>(gz), layout_.cellsZ - 1);   // the far edge belongs to the last cell
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);

    if (fx + fz <= 1.0f) {   // triangle (topLeft, bottomLeft, topRight)
        out.gridX[0] = ix;     out.gridZ[0] = iz;      out.weight[0] = 1.0f - fx - fz;
        out.gridX[1] = ix;     out.gridZ[1] = iz + 1;  out.weight[1] = fz;
        out.gridX[2] = ix + 1; out.gridZ[2] = iz;      out.weight[2] = fx;
    } else {                 // triangle (topRight, bottomLeft, bottomRight)
        out.gridX[0] = ix + 1; out.gridZ[0] = iz;      out.weight[0] = 1.0f - fz;
        out.gridX[1] = ix;     out.gridZ[1] = iz + 1;  out.weight[1] = 1.0f - fx;
        out.gridX[2] = ix + 1; out.gridZ[2] = iz + 1;  out.weight[2] = fx + fz - 1.0f;
    }
    return true;
}

bool Heightfield::HeightAt(float x, float z, float& y) const
{
    Location where;
    if (!Locate(x, z, where)) return false;
    float height = 0.0f;
    for (int i = 0; i < 3; ++i) height += where.weight[i] * Sample(where.gridX[i], where.gridZ[i]);
    y = height * layout_.heightScale;
    return true;
}

// The same formula as makeVertex() in Terrain.cpp: the slope at a vertex is the height difference between its two
// neighbours (a "central difference") divided by the distance between them. Neighbours past the edge are clamped.
HfVec3 Heightfield::VertexNormal(int gridX, int gridZ) const
{
    const float cellWidth = layout_.sizeX / static_cast<float>(layout_.cellsX);
    const float cellDepth = layout_.sizeZ / static_cast<float>(layout_.cellsZ);
    const float slopeX = (Sample(gridX + 1, gridZ) - Sample(gridX - 1, gridZ)) * layout_.heightScale / (2.0f * cellWidth);
    const float slopeZ = (Sample(gridX, gridZ + 1) - Sample(gridX, gridZ - 1)) * layout_.heightScale / (2.0f * cellDepth);
    const float length = std::sqrt(slopeX * slopeX + 1.0f + slopeZ * slopeZ);
    return { -slopeX / length, 1.0f / length, -slopeZ / length };
}

bool Heightfield::NormalAt(float x, float z, HfVec3& normal) const
{
    Location where;
    if (!Locate(x, z, where)) return false;
    HfVec3 sum;
    for (int i = 0; i < 3; ++i) {
        const HfVec3 corner = VertexNormal(where.gridX[i], where.gridZ[i]);
        sum.x += corner.x * where.weight[i];
        sum.y += corner.y * where.weight[i];
        sum.z += corner.z * where.weight[i];
    }
    const float length = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);   // blending shortens a normal
    if (length < 1e-8f) return false;
    normal = { sum.x / length, sum.y / length, sum.z / length };
    return true;
}

float Heightfield::HeightClamped(float x, float z) const
{
    const float halfX = layout_.sizeX * 0.5f;
    const float halfZ = layout_.sizeZ * 0.5f;
    float y = 0.0f;
    HeightAt(std::clamp(x, -halfX, halfX), std::clamp(z, -halfZ, halfZ), y);
    return y;
}

// Ray against a heightfield, in three moves:
//   1. CLIP. The whole field fits in a box (its size, from height 0 to heightScale). A ray that misses the box misses
//      the terrain; otherwise only the part of the ray inside the box matters. (The "slab" test of RayPicking.cpp.)
//   2. MARCH. Walk along that part in steps of half a cell and watch the GAP = ray height - ground height. While the
//      ray is above the ground the gap is positive. The first step where it is zero or negative has just gone through
//      the surface, so the hit is somewhere between the previous step and this one.
//   3. BISECT. Halve that bracket again and again, keeping the half where the gap changes sign.
// Limit: a spike narrower than half a cell can slip between two steps. Walking cell by cell (a "DDA" walk) with an exact
// ray-triangle test in each cell has no such gap; it is the upgrade if this ever matters.
bool Heightfield::Raycast(const HfVec3& origin, const HfVec3& direction, float& t) const
{
    if (samples_.empty()) return false;
    if (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z < 1e-20f) return false;

    const float o[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { direction.x, direction.y, direction.z };
    const float low[3] = { -layout_.sizeX * 0.5f, 0.0f, -layout_.sizeZ * 0.5f };
    const float high[3] = { layout_.sizeX * 0.5f, layout_.heightScale, layout_.sizeZ * 0.5f };

    // 1. clip to the box
    float tEnter = 0.0f;
    float tExit = FLT_MAX;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(d[axis]) < 1e-12f) {
            if (o[axis] < low[axis] || o[axis] > high[axis]) return false;   // parallel to this slab and outside it
        } else {
            float t0 = (low[axis] - o[axis]) / d[axis];
            float t1 = (high[axis] - o[axis]) / d[axis];
            if (t0 > t1) std::swap(t0, t1);
            tEnter = (std::max)(tEnter, t0);
            tExit = (std::min)(tExit, t1);
            if (tEnter > tExit) return false;
        }
    }

    auto gapAt = [&](float along) {   // ray height minus ground height at this point of the ray
        const float px = o[0] + d[0] * along;
        const float pz = o[2] + d[2] * along;
        return (o[1] + d[1] * along) - HeightClamped(px, pz);
    };

    // 2. march. The step is half a cell of HORIZONTAL travel. A straight-down ray does not move sideways, and its gap
    // only ever shrinks, so one big step is enough for it.
    const float cell = (std::min)(layout_.sizeX / static_cast<float>(layout_.cellsX),
                                  layout_.sizeZ / static_cast<float>(layout_.cellsZ));
    const float horizontal = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    const float span = tExit - tEnter;
    float step = horizontal > 1e-6f ? 0.5f * cell / horizontal : span;
    step = (std::max)(step, span / static_cast<float>(kMaxMarchSteps));

    const float startGap = gapAt(tEnter);
    if (startGap < 0.0f) return false;   // already under the ground: this is a "from above" query
    if (startGap == 0.0f) { t = tEnter; return true; }

    float previous = tEnter;
    while (previous < tExit) {
        const float next = (std::min)(previous + step, tExit);
        if (gapAt(next) <= 0.0f) {
            // 3. bisect: `above` is always still over the ground, `below` is always at or under it
            float above = previous;
            float below = next;
            for (int i = 0; i < kBisectSteps; ++i) {
                const float middle = 0.5f * (above + below);
                if (gapAt(middle) > 0.0f) above = middle; else below = middle;
            }
            t = 0.5f * (above + below);
            return true;
        }
        previous = next;
    }
    return false;
}
