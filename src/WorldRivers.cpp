#include "WorldRivers.h"
#include <algorithm>
#include <cmath>
#include <limits>

bool WorldRiver::Bounds(double& minX, double& maxX, double& minZ, double& maxZ) const
{
    if (path.size() < 2) return false;
    const double bank = std::max(static_cast<double>(carve.bankWidth), 1e-3);
    minX = minZ = std::numeric_limits<double>::max();
    maxX = maxZ = std::numeric_limits<double>::lowest();
    for (const RiverPath::Sample& s : path) {
        const double reach = static_cast<double>(s.halfWidth) + bank;
        minX = std::min(minX, static_cast<double>(s.x) - reach); maxX = std::max(maxX, static_cast<double>(s.x) + reach);
        minZ = std::min(minZ, static_cast<double>(s.z) - reach); maxZ = std::max(maxZ, static_cast<double>(s.z) + reach);
    }
    return true;
}

void CarveRiversIntoBlock(std::vector<float>& samples, int countX, int countZ, std::int64_t firstX, std::int64_t firstZ, double step,
                          const std::vector<WorldRiver>& rivers)
{
    if (countX <= 0 || countZ <= 0 || step <= 0.0 || samples.size() < static_cast<size_t>(countX) * static_cast<size_t>(countZ)) return;
    const double blockMinX = static_cast<double>(firstX) * step, blockMaxX = static_cast<double>(firstX + countX - 1) * step;
    const double blockMinZ = static_cast<double>(firstZ) * step, blockMaxZ = static_cast<double>(firstZ + countZ - 1) * step;

    struct Best { double d2 = std::numeric_limits<double>::max(); float halfWidth = 0.0f; float level = 0.0f; };
    std::vector<Best> best;

    for (const WorldRiver& river : rivers) {
        double minX, maxX, minZ, maxZ;
        if (!river.Bounds(minX, maxX, minZ, maxZ)) continue;
        if (maxX < blockMinX || minX > blockMaxX || maxZ < blockMinZ || minZ > blockMaxZ) continue;   // nowhere near this block
        const double bank = std::max(static_cast<double>(river.carve.bankWidth), 1e-3);
        best.assign(static_cast<size_t>(countX) * static_cast<size_t>(countZ), Best{});

        // The samples a piece of the path can touch: the box around it, in block indices, clamped to the block.
        auto range = [&](double lo, double hi, std::int64_t first, int count, int& outLo, int& outHi) {
            const double a = std::floor(lo / step) - static_cast<double>(first);
            const double b = std::ceil(hi / step) - static_cast<double>(first);
            outLo = static_cast<int>(std::clamp(a, 0.0, static_cast<double>(count - 1)));
            outHi = static_cast<int>(std::clamp(b, 0.0, static_cast<double>(count - 1)));
        };

        for (size_t i = 0; i + 1 < river.path.size(); ++i) {
            const RiverPath::Sample& a = river.path[i];
            const RiverPath::Sample& b = river.path[i + 1];
            const double reach = static_cast<double>(std::max(a.halfWidth, b.halfWidth)) + bank;
            int x0, x1, z0, z1;
            range(std::min(a.x, b.x) - reach, std::max(a.x, b.x) + reach, firstX, countX, x0, x1);
            range(std::min(a.z, b.z) - reach, std::max(a.z, b.z) + reach, firstZ, countZ, z0, z1);
            const double ex = static_cast<double>(b.x) - a.x, ez = static_cast<double>(b.z) - a.z;
            const double length2 = ex * ex + ez * ez;
            for (int gz = z0; gz <= z1; ++gz) {
                const double pz = static_cast<double>(firstZ + gz) * step;
                for (int gx = x0; gx <= x1; ++gx) {
                    const double px = static_cast<double>(firstX + gx) * step;
                    const double t = length2 > 1e-12 ? std::clamp(((px - a.x) * ex + (pz - a.z) * ez) / length2, 0.0, 1.0) : 0.0;
                    const double dx = px - (a.x + ex * t), dz = pz - (a.z + ez * t);
                    const double d2 = dx * dx + dz * dz;
                    Best& cell = best[static_cast<size_t>(gz) * countX + gx];
                    if (d2 < cell.d2) {
                        const float tf = static_cast<float>(t);
                        cell.d2 = d2;
                        cell.halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * tf;
                        cell.level = a.level + (b.level - a.level) * tf;
                    }
                }
            }
        }

        for (size_t index = 0; index < best.size(); ++index) {
            const Best& cell = best[index];
            if (cell.d2 == std::numeric_limits<double>::max()) continue;
            const double d = std::sqrt(cell.d2);
            if (d > static_cast<double>(cell.halfWidth) + bank) continue;
            const float before = samples[index];
            RiverCarveSettings here = river.carve;
            here.level += cell.level;   // the water's surface where this part of the river is (lesson 41)
            const float after = std::clamp(CarvedHeight(static_cast<float>(d), cell.halfWidth, before, here), 0.0f, 1.0f);
            if (after < before - 1e-6f) samples[index] = after;
        }
    }
}

bool RiverTouchesTile(const WorldGridSettings& grid, TileCoord tile, const WorldRiver& river)
{
    double minX, maxX, minZ, maxZ;
    if (!river.Bounds(minX, maxX, minZ, maxZ)) return false;
    // The apron reaches one sample outside the tile, and a tile at a coarse level has wide cells: allow for the widest.
    const double size = grid.TileSize(), margin = 2.0 * static_cast<double>(WorldGridSettings::kMetresPerCellMax);
    const double tileMinX = static_cast<double>(tile.x) * size - margin, tileMaxX = static_cast<double>(tile.x + 1) * size + margin;
    const double tileMinZ = static_cast<double>(tile.z) * size - margin, tileMaxZ = static_cast<double>(tile.z + 1) * size + margin;
    return !(maxX < tileMinX || minX > tileMaxX || maxZ < tileMinZ || minZ > tileMaxZ);
}

std::vector<float> GenerateTileBlock(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile, const std::vector<WorldRiver>* rivers, const TileSampleRect& rect)
{
    std::vector<float> block;
    if (rect.IsEmpty()) return block;
    const int width = rect.Width(), height = rect.Height();
    block.resize(static_cast<size_t>(width) * height);
    for (int j = 0; j < height; ++j) {
        const double worldZ = WorldSamplePosition(grid, tile.z, rect.z0 + j);
        for (int i = 0; i < width; ++i) block[static_cast<size_t>(j) * width + i] = source.Height01(WorldSamplePosition(grid, tile.x, rect.x0 + i), worldZ);
    }
    bool touched = false;
    if (rivers) {
        for (const WorldRiver& river : *rivers) touched = touched || RiverTouchesTile(grid, tile, river);
    }
    if (touched) {
        const std::int64_t cells = grid.TileCells();
        CarveRiversIntoBlock(block, width, height, static_cast<std::int64_t>(tile.x) * cells + rect.x0, static_cast<std::int64_t>(tile.z) * cells + rect.z0,
                             static_cast<double>(grid.metresPerCell), *rivers);
    }
    return block;
}

TileHeights GenerateTileHeights(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile, const std::vector<WorldRiver>* rivers)
{
    bool touched = false;
    if (rivers) {
        for (const WorldRiver& river : *rivers) touched = touched || RiverTouchesTile(grid, tile, river);
    }
    TileHeights result;
    if (!touched) {   // the common case, and exactly the plain tile
        result.samples = GenerateTileSamples(source, grid, tile);
        result.apron = GenerateTileApron(source, grid, tile);
        return result;
    }

    // One block one sample larger than the tile on every side: the tile's samples and its apron, carved together.
    const int cells = grid.TileCells(), n = cells + 3;
    std::vector<float> block(static_cast<size_t>(n) * n);
    for (int j = 0; j < n; ++j) {
        const double worldZ = WorldSamplePosition(grid, tile.z, j - 1);
        for (int i = 0; i < n; ++i) block[static_cast<size_t>(j) * n + i] = source.Height01(WorldSamplePosition(grid, tile.x, i - 1), worldZ);
    }
    CarveRiversIntoBlock(block, n, n, static_cast<std::int64_t>(tile.x) * cells - 1, static_cast<std::int64_t>(tile.z) * cells - 1,
                         static_cast<double>(grid.metresPerCell), *rivers);

    auto at = [&](int i, int j) { return block[static_cast<size_t>(j) * n + i]; };
    result.samples.resize(static_cast<size_t>(cells + 1) * (cells + 1));
    for (int z = 0; z <= cells; ++z)
        for (int x = 0; x <= cells; ++x) result.samples[static_cast<size_t>(z) * (cells + 1) + x] = at(x + 1, z + 1);
    result.apron.west.resize(static_cast<size_t>(cells) + 1);
    result.apron.east.resize(result.apron.west.size());
    result.apron.north.resize(result.apron.west.size());
    result.apron.south.resize(result.apron.west.size());
    for (int i = 0; i <= cells; ++i) {
        result.apron.west[i] = at(0, i + 1);
        result.apron.east[i] = at(cells + 2, i + 1);
        result.apron.north[i] = at(i + 1, 0);
        result.apron.south[i] = at(i + 1, cells + 2);
    }
    return result;
}
