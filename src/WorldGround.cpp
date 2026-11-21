#include "WorldGround.h"
#include <algorithm>
#include <cmath>

bool WorldGroundGrid::Sample(double worldX, double worldZ, float& heightMetres) const
{
    if (metres.empty() || countX < 2 || countZ < 2 || step <= 0.0) return false;
    const double fx = (worldX - originX) / step, fz = (worldZ - originZ) / step;
    if (fx < 0.0 || fz < 0.0 || fx > static_cast<double>(countX - 1) || fz > static_cast<double>(countZ - 1)) return false;
    const int x0 = (std::min)(static_cast<int>(std::floor(fx)), countX - 2), z0 = (std::min)(static_cast<int>(std::floor(fz)), countZ - 2);
    const float tx = static_cast<float>(fx - x0), tz = static_cast<float>(fz - z0);
    const size_t i00 = static_cast<size_t>(z0) * countX + x0, i10 = i00 + 1, i01 = i00 + countX, i11 = i01 + 1;
    if (!hasGround[i00] || !hasGround[i10] || !hasGround[i01] || !hasGround[i11]) return false;
    const float a = metres[i00] + (metres[i10] - metres[i00]) * tx;
    const float b = metres[i01] + (metres[i11] - metres[i01]) * tx;
    heightMetres = a + (b - a) * tz;
    return true;
}

WorldGroundGrid BuildWorldGroundGrid(const WorldHeightSource& source, const std::vector<WorldRiver>* rivers, const WorldGridSettings& grid,
                                     const WorldBounds& bounds, float heightScale, double minX, double maxX, double minZ, double maxZ,
                                     int maxSamplesPerSide, float baseHeight)
{
    WorldGroundGrid out;
    if (maxX < minX || maxZ < minZ || grid.metresPerCell <= 0.0f) return out;
    const double lattice = static_cast<double>(grid.metresPerCell);
    maxSamplesPerSide = (std::max)(maxSamplesPerSide, 8);
    const double extent = (std::max)(maxX - minX, maxZ - minZ);
    const double multiple = (std::max)(1.0, std::ceil(extent / lattice / static_cast<double>(maxSamplesPerSide - 3)));
    const double step = lattice * multiple;   // a whole multiple of the lattice: samples stay on the world's own points

    const std::int64_t firstX = static_cast<std::int64_t>(std::floor(minX / step)) - 1, firstZ = static_cast<std::int64_t>(std::floor(minZ / step)) - 1;
    const std::int64_t lastX = static_cast<std::int64_t>(std::ceil(maxX / step)) + 1, lastZ = static_cast<std::int64_t>(std::ceil(maxZ / step)) + 1;
    out.countX = static_cast<int>(lastX - firstX + 1);
    out.countZ = static_cast<int>(lastZ - firstZ + 1);
    out.originX = static_cast<double>(firstX) * step;
    out.originZ = static_cast<double>(firstZ) * step;
    out.step = step;

    const size_t count = static_cast<size_t>(out.countX) * out.countZ;
    out.metres.resize(count);
    out.hasGround.assign(count, 1);

    const double tileMetres = static_cast<double>(grid.TileCells()) * lattice;
    const double edgeMinX = bounds.minX * tileMetres, edgeMaxX = (bounds.maxX + 1) * tileMetres;   // the shared edge row belongs to the world
    const double edgeMinZ = bounds.minZ * tileMetres, edgeMaxZ = (bounds.maxZ + 1) * tileMetres;
    for (int j = 0; j < out.countZ; ++j) {
        const double z = out.originZ + j * step;
        for (int i = 0; i < out.countX; ++i) {
            const double x = out.originX + i * step;
            const size_t index = static_cast<size_t>(j) * out.countX + i;
            out.metres[index] = source.Height01(x, z);
            if (bounds.bounded && (x < edgeMinX - 1e-6 || x > edgeMaxX + 1e-6 || z < edgeMinZ - 1e-6 || z > edgeMaxZ + 1e-6)) out.hasGround[index] = 0;
        }
    }
    if (rivers && !rivers->empty()) CarveRiversIntoBlock(out.metres, out.countX, out.countZ, firstX, firstZ, step, *rivers);   // 0..1, like a tile's
    for (float& value : out.metres) value = baseHeight + value * heightScale;   // 0..1 -> world Y in metres: a tile's vertices times heightScale, on its position.y
    return out;
}

// --- the ground at one point --------------------------------------------------------------------------------------------------------

bool WorldGroundFunction::HasGroundAt(double worldX, double worldZ) const
{
    if (!bounds.bounded) return true;
    const double tileMetres = static_cast<double>(grid.TileCells()) * static_cast<double>(grid.metresPerCell);
    constexpr double eps = 1e-6;
    return worldX >= bounds.minX * tileMetres - eps && worldX <= (bounds.maxX + 1) * tileMetres + eps &&
           worldZ >= bounds.minZ * tileMetres - eps && worldZ <= (bounds.maxZ + 1) * tileMetres + eps;
}

std::vector<float> WorldGroundFunction::Lattice(std::int64_t firstX, std::int64_t firstZ, int count) const
{
    const double lattice = static_cast<double>(grid.metresPerCell);
    std::vector<float> block(static_cast<size_t>(count) * count);
    for (int j = 0; j < count; ++j)
        for (int i = 0; i < count; ++i) block[static_cast<size_t>(j) * count + i] = source->Height01((firstX + i) * lattice, (firstZ + j) * lattice);
    if (rivers && !rivers->empty()) CarveRiversIntoBlock(block, count, count, firstX, firstZ, lattice, *rivers);
    for (float& value : block) value = baseHeight + value * heightScale;   // the same rule as BuildWorldGroundGrid
    return block;
}

namespace {
// The triangle a point lies in and its weights: the same split as Heightfield::Locate. (fx, fz) are the position inside the cell, 0..1.
struct Corners { int x[3], z[3]; float w[3]; };   // offsets 0 or 1 inside the cell
Corners LocateInCell(float fx, float fz)
{
    Corners c;
    if (fx + fz <= 1.0f) {
        c.x[0] = 0; c.z[0] = 0; c.w[0] = 1.0f - fx - fz;
        c.x[1] = 0; c.z[1] = 1; c.w[1] = fz;
        c.x[2] = 1; c.z[2] = 0; c.w[2] = fx;
    } else {
        c.x[0] = 1; c.z[0] = 0; c.w[0] = 1.0f - fz;
        c.x[1] = 0; c.z[1] = 1; c.w[1] = 1.0f - fx;
        c.x[2] = 1; c.z[2] = 1; c.w[2] = fx + fz - 1.0f;
    }
    return c;
}
}

bool WorldGroundFunction::HeightAt(double worldX, double worldZ, float& heightMetres) const
{
    if (!source || !HasGroundAt(worldX, worldZ) || grid.metresPerCell <= 0.0f) return false;
    const double lattice = static_cast<double>(grid.metresPerCell);
    const double gx = worldX / lattice, gz = worldZ / lattice;
    const std::int64_t ix = static_cast<std::int64_t>(std::floor(gx)), iz = static_cast<std::int64_t>(std::floor(gz));
    const Corners c = LocateInCell(static_cast<float>(gx - static_cast<double>(ix)), static_cast<float>(gz - static_cast<double>(iz)));
    const std::vector<float> block = Lattice(ix, iz, 2);
    float height = 0.0f;
    for (int k = 0; k < 3; ++k) height += c.w[k] * block[static_cast<size_t>(c.z[k]) * 2 + c.x[k]];
    heightMetres = height;
    return true;
}

bool WorldGroundFunction::NormalAt(double worldX, double worldZ, WorldVec3& normal) const
{
    if (!source || !HasGroundAt(worldX, worldZ) || grid.metresPerCell <= 0.0f) return false;
    const double lattice = static_cast<double>(grid.metresPerCell);
    const double gx = worldX / lattice, gz = worldZ / lattice;
    const std::int64_t ix = static_cast<std::int64_t>(std::floor(gx)), iz = static_cast<std::int64_t>(std::floor(gz));
    const Corners c = LocateInCell(static_cast<float>(gx - static_cast<double>(ix)), static_cast<float>(gz - static_cast<double>(iz)));
    const std::vector<float> block = Lattice(ix - 1, iz - 1, 4);   // the cell's corners are at 1..2; their neighbours at 0..3
    const float cell = grid.metresPerCell;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const int a = 1 + c.x[k], b = 1 + c.z[k];   // this corner's place in the block
        const float slopeX = (block[static_cast<size_t>(b) * 4 + a + 1] - block[static_cast<size_t>(b) * 4 + a - 1]) / (2.0f * cell);
        const float slopeZ = (block[static_cast<size_t>(b + 1) * 4 + a] - block[static_cast<size_t>(b - 1) * 4 + a]) / (2.0f * cell);
        const float length = std::sqrt(slopeX * slopeX + 1.0f + slopeZ * slopeZ);   // the same formula as Heightfield::VertexNormal
        sx += c.w[k] * (-slopeX / length); sy += c.w[k] * (1.0f / length); sz += c.w[k] * (-slopeZ / length);
    }
    const float length = std::sqrt(sx * sx + sy * sy + sz * sz);   // blending shortens a normal
    if (length < 1e-8f) return false;
    normal = { sx / length, sy / length, sz / length };
    return true;
}

bool WorldGroundFunction::Raycast(const WorldVec3& origin, const WorldVec3& direction, float maxDistance, float& distance) const
{
    if (!source || maxDistance <= 0.0f) return false;
    const float directionLength = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (directionLength < 1e-8f) return false;
    const double dx = direction.x / directionLength, dy = direction.y / directionLength, dz = direction.z / directionLength;
    auto above = [&](double t, bool& known) {   // height of the ray over the ground at distance t; `known` false where there is no ground
        float ground = 0.0f;
        known = HeightAt(origin.x + dx * t, origin.z + dz * t, ground);
        return (origin.y + dy * t) - static_cast<double>(ground);
    };
    const double step = (std::max)(0.05, 0.5 * static_cast<double>(grid.metresPerCell));
    bool known = false;
    double previousT = 0.0, previousAbove = 0.0;
    bool havePrevious = false;
    for (double t = 0.0; t <= static_cast<double>(maxDistance) + step; t += step) {
        const double clampedT = (std::min)(t, static_cast<double>(maxDistance));
        const double h = above(clampedT, known);
        if (!known) { havePrevious = false; if (t > static_cast<double>(maxDistance)) break; continue; }
        if (!havePrevious && h < 0.0) return false;   // the ray starts under the ground, or enters the world below the surface
        if (havePrevious && previousAbove >= 0.0 && h < 0.0) {
            double lo = previousT, hi = clampedT;   // above at lo, below at hi: narrow it down
            for (int i = 0; i < 40; ++i) {
                const double mid = 0.5 * (lo + hi);
                bool midKnown = false;
                const double hm = above(mid, midKnown);
                if (midKnown && hm < 0.0) hi = mid; else lo = mid;
            }
            distance = static_cast<float>(0.5 * (lo + hi));
            return true;
        }
        previousT = clampedT; previousAbove = h; havePrevious = true;
        if (t >= static_cast<double>(maxDistance)) break;
    }
    return false;
}
