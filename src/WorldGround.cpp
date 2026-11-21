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
                                     int maxSamplesPerSide)
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
    for (float& value : out.metres) value *= heightScale;   // 0..1 -> metres, as a tile's vertices do
    return out;
}
