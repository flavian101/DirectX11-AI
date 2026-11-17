#include "WorldGrid.h"
#include <algorithm>
#include <cmath>

void WorldGridSettings::Clamp()
{
    chunksPerTile = std::clamp(chunksPerTile, kChunksMin, kChunksMax);
    metresPerCell = std::clamp(metresPerCell, kMetresPerCellMin, kMetresPerCellMax);
}

TileCoord TileContaining(const WorldGridSettings& grid, double worldX, double worldZ)
{
    const double size = grid.TileSize();
    // floor, not truncation: -0.1 m belongs to tile -1, not tile 0
    return { static_cast<int>(std::floor(worldX / size)), static_cast<int>(std::floor(worldZ / size)) };
}

void TileCenter(const WorldGridSettings& grid, TileCoord tile, double& worldX, double& worldZ)
{
    const double size = grid.TileSize();
    worldX = (static_cast<double>(tile.x) + 0.5) * size;
    worldZ = (static_cast<double>(tile.z) + 0.5) * size;
}

double DistanceToTile(const WorldGridSettings& grid, TileCoord tile, double worldX, double worldZ)
{
    const double size = grid.TileSize();
    const double minX = static_cast<double>(tile.x) * size, minZ = static_cast<double>(tile.z) * size;
    // How far outside the square on each axis (0 if inside on that axis), then the straight-line distance.
    const double dx = (std::max)({ minX - worldX, 0.0, worldX - (minX + size) });
    const double dz = (std::max)({ minZ - worldZ, 0.0, worldZ - (minZ + size) });
    return std::sqrt(dx * dx + dz * dz);
}

std::vector<TileCoord> TilesWithin(const WorldGridSettings& grid, double worldX, double worldZ, double radius)
{
    std::vector<TileCoord> tiles;
    if (!(radius >= 0.0)) return tiles;   // also rejects NaN
    const TileCoord first = TileContaining(grid, worldX - radius, worldZ - radius);
    const TileCoord last = TileContaining(grid, worldX + radius, worldZ + radius);
    for (int z = first.z; z <= last.z; ++z) {
        for (int x = first.x; x <= last.x; ++x) {
            if (DistanceToTile(grid, { x, z }, worldX, worldZ) <= radius) tiles.push_back({ x, z });
        }
    }
    std::sort(tiles.begin(), tiles.end(), [&](const TileCoord& a, const TileCoord& b) {
        const double da = DistanceToTile(grid, a, worldX, worldZ), db = DistanceToTile(grid, b, worldX, worldZ);
        if (da != db) return da < db;
        if (a.z != b.z) return a.z < b.z;
        return a.x < b.x;
    });
    return tiles;
}

namespace {

// The same hash and smooth value noise as Terrain.cpp, but taking world positions in double: the cell index is an exact
// integer however far from the origin we are, and only the small fraction inside the cell is a float.
float SmoothStep(float value) { return value * value * (3.0f - 2.0f * value); }

float HashValue(std::int64_t x, std::int64_t z, int seed)
{
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u;
    value += static_cast<std::uint32_t>(z) * 668265263u;
    value += static_cast<std::uint32_t>(seed) * 1442695041u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) / 8388607.5f - 1.0f;   // -1..1
}

float ValueNoise(double x, double z, int seed)
{
    const double fx = std::floor(x), fz = std::floor(z);
    const std::int64_t x0 = static_cast<std::int64_t>(fx), z0 = static_cast<std::int64_t>(fz);
    const float tx = SmoothStep(static_cast<float>(x - fx));
    const float tz = SmoothStep(static_cast<float>(z - fz));
    const float a = HashValue(x0, z0, seed), b = HashValue(x0 + 1, z0, seed);
    const float c = HashValue(x0, z0 + 1, seed), d = HashValue(x0 + 1, z0 + 1, seed);
    const float ab = a + (b - a) * tx;
    const float cd = c + (d - c) * tx;
    return ab + (cd - ab) * tz;
}

// World position of sample `local` of tile `tile` along one axis. The integer product is exact; the one multiplication
// by metresPerCell is done in double.
double SampleWorldPosition(const WorldGridSettings& grid, int tile, int local)
{
    const std::int64_t globalIndex = static_cast<std::int64_t>(tile) * grid.TileCells() + local;
    return static_cast<double>(globalIndex) * static_cast<double>(grid.metresPerCell);
}

}   // namespace

float FractalHeightSource::Height01(double worldX, double worldZ) const
{
    const double wavelength = (std::max)(1.0, static_cast<double>(settings_.baseWavelength));
    float value = 0.0f, amplitude = 0.5f, amplitudeTotal = 0.0f;
    double frequency = 1.0 / wavelength;
    for (int octave = 0; octave < std::clamp(settings_.octaves, 1, 12); ++octave) {
        value += ValueNoise(worldX * frequency, worldZ * frequency, settings_.seed + octave * 31) * amplitude;
        amplitudeTotal += amplitude;
        frequency *= 2.0;
        amplitude *= 0.5f;
    }
    return std::clamp(0.5f + 0.5f * (value / amplitudeTotal), 0.0f, 1.0f);
}

std::vector<float> GenerateTileSamples(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile)
{
    const int cells = grid.TileCells();
    std::vector<float> samples(static_cast<size_t>(cells + 1) * static_cast<size_t>(cells + 1));
    for (int z = 0; z <= cells; ++z) {
        const double worldZ = SampleWorldPosition(grid, tile.z, z);
        for (int x = 0; x <= cells; ++x) {
            samples[static_cast<size_t>(z) * (cells + 1) + x] = source.Height01(SampleWorldPosition(grid, tile.x, x), worldZ);
        }
    }
    return samples;
}

HeightfieldApron GenerateTileApron(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile)
{
    const int cells = grid.TileCells();
    HeightfieldApron apron;
    apron.west.resize(static_cast<size_t>(cells) + 1);
    apron.east.resize(apron.west.size());
    apron.north.resize(apron.west.size());
    apron.south.resize(apron.west.size());
    for (int i = 0; i <= cells; ++i) {
        const double alongX = SampleWorldPosition(grid, tile.x, i), alongZ = SampleWorldPosition(grid, tile.z, i);
        apron.west[i] = source.Height01(SampleWorldPosition(grid, tile.x, -1), alongZ);
        apron.east[i] = source.Height01(SampleWorldPosition(grid, tile.x, cells + 1), alongZ);
        apron.north[i] = source.Height01(alongX, SampleWorldPosition(grid, tile.z, -1));
        apron.south[i] = source.Height01(alongX, SampleWorldPosition(grid, tile.z, cells + 1));
    }
    return apron;
}

HeightfieldLayout TileLayout(const WorldGridSettings& grid, float heightScale)
{
    HeightfieldLayout layout;
    layout.cellsX = layout.cellsZ = grid.TileCells();
    layout.sizeX = layout.sizeZ = static_cast<float>(grid.TileSize());
    layout.heightScale = heightScale;
    return layout;
}
