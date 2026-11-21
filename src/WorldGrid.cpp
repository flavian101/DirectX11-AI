#include "WorldGrid.h"
#include <algorithm>
#include <cmath>

void WorldGridSettings::Clamp()
{
    chunksPerTile = std::clamp(chunksPerTile, kChunksMin, kChunksMax);
    metresPerCell = std::clamp(metresPerCell, kMetresPerCellMin, kMetresPerCellMax);
}

int WorldGridSettings::MaxTileLod() const
{
    int level = 0, chunks = chunksPerTile;
    float cell = metresPerCell;
    while (level < kTileLodMax && chunks >= 2 && chunks % 2 == 0 && cell * 2.0f <= kMetresPerCellMax) {
        chunks /= 2;
        cell *= 2.0f;
        ++level;
    }
    return level;
}

WorldGridSettings WorldGridSettings::ForLod(int level) const
{
    level = std::clamp(level, 0, MaxTileLod());
    WorldGridSettings result = *this;
    result.chunksPerTile = chunksPerTile >> level;
    result.metresPerCell = metresPerCell * static_cast<float>(1 << level);
    return result;
}

int WantedTileLod(const WorldGridSettings& grid, double distance, int currentLod)
{
    const double size = grid.TileSize();
    const int maxLod = grid.MaxTileLod();
    auto start = [&](int level) { return kTileLodStartInTiles[level - 1] * size; };   // where `level` (>= 1) begins
    if (currentLod < 0) {
        int level = 0;
        while (level < maxLod && distance >= start(level + 1)) ++level;
        return level;
    }
    const double margin = 0.25 * size;
    int level = std::clamp(currentLod, 0, maxLod);
    while (level < maxLod && distance > start(level + 1) + margin) ++level;   // coarser only well past the boundary
    while (level > 0 && distance < start(level) - margin) --level;            // finer only well inside it
    return level;
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

void WorldBounds::Normalize()
{
    if (!bounded) return;
    if (minX > maxX) std::swap(minX, maxX);
    if (minZ > maxZ) std::swap(minZ, maxZ);
    maxX = (std::min)(maxX, minX + kMaxTilesAcross - 1);
    maxZ = (std::min)(maxZ, minZ + kMaxTilesAcross - 1);
}

WorldBounds WorldBounds::Covering(const WorldGridSettings& grid, double minXm, double minZm, double maxXm, double maxZm)
{
    const double size = grid.TileSize();
    // A tiny tolerance, so that a rectangle that ends exactly on a border (240 m picture, 240 m tile) is not rounded up by the
    // last bit of floating-point noise into a second, empty tile.
    const double eps = 1e-9;
    WorldBounds bounds;
    bounds.bounded = true;
    bounds.minX = static_cast<int>(std::floor(minXm / size + eps));
    bounds.minZ = static_cast<int>(std::floor(minZm / size + eps));
    bounds.maxX = (std::max)(bounds.minX, static_cast<int>(std::ceil(maxXm / size - eps)) - 1);
    bounds.maxZ = (std::max)(bounds.minZ, static_cast<int>(std::ceil(maxZm / size - eps)) - 1);
    bounds.Normalize();
    return bounds;
}

StreamingPlan PlanTileStreaming(const WorldGridSettings& grid, double cameraX, double cameraZ, double viewDistance, int maxTiles,
                                const std::vector<LoadedTile>& loaded, const WorldBounds& bounds)
{
    StreamingPlan plan;
    maxTiles = std::max(maxTiles, 0);
    const double unloadDistance = viewDistance + 0.5 * grid.TileSize();
    std::vector<LoadedTile> kept;
    for (const LoadedTile& tile : loaded) {
        if (!bounds.Contains(tile.coord) || DistanceToTile(grid, tile.coord, cameraX, cameraZ) > unloadDistance) plan.toUnload.push_back(tile.coord);
        else kept.push_back(tile);
    }
    int newTiles = 0;   // a rebuild at another level does not change how many tiles are alive
    // Nearest first, so that if the cap bites it is the far tiles that go.
    for (const TileCoord& tile : TilesWithin(grid, cameraX, cameraZ, viewDistance)) {
        if (!bounds.Contains(tile)) continue;   // the edge of a bounded world: no land beyond it
        const auto found = std::find_if(kept.begin(), kept.end(), [&](const LoadedTile& other) { return other.coord == tile; });
        const double distance = DistanceToTile(grid, tile, cameraX, cameraZ);
        if (found == kept.end()) {
            if (static_cast<int>(kept.size()) + newTiles >= maxTiles) continue;
            plan.toLoad.push_back({ tile, WantedTileLod(grid, distance, -1) });
            ++newTiles;
        } else {
            const int wanted = WantedTileLod(grid, distance, found->lod);
            if (wanted != found->lod) plan.toLoad.push_back({ tile, wanted });
        }
    }
    return plan;
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

}   // namespace

double WorldSamplePosition(const WorldGridSettings& grid, int tile, int local)
{
    const std::int64_t globalIndex = static_cast<std::int64_t>(tile) * grid.TileCells() + local;
    return static_cast<double>(globalIndex) * static_cast<double>(grid.metresPerCell);
}

unsigned char OuterEdgesOfTile(const WorldBounds& bounds, TileCoord tile)
{
    if (!bounds.bounded || !bounds.Contains(tile)) return 0;
    unsigned char mask = 0;
    if (tile.z == bounds.minZ) mask |= kEdgeNorth;
    if (tile.z == bounds.maxZ) mask |= kEdgeSouth;
    if (tile.x == bounds.minX) mask |= kEdgeWest;
    if (tile.x == bounds.maxX) mask |= kEdgeEast;
    return mask;
}

TileSampleRect TileSamplesAffectedBy(const WorldGridSettings& grid, TileCoord tile, std::int64_t minX, std::int64_t minZ, std::int64_t maxX, std::int64_t maxZ)
{
    TileSampleRect none;
    if (maxX < minX || maxZ < minZ) return none;
    const std::int64_t cells = grid.TileCells();
    const std::int64_t originX = static_cast<std::int64_t>(tile.x) * cells, originZ = static_cast<std::int64_t>(tile.z) * cells;
    const std::int64_t x0 = (std::max)(minX - 1 - originX, static_cast<std::int64_t>(-1)), x1 = (std::min)(maxX + 1 - originX, cells + 1);
    const std::int64_t z0 = (std::max)(minZ - 1 - originZ, static_cast<std::int64_t>(-1)), z1 = (std::min)(maxZ + 1 - originZ, cells + 1);
    if (x1 < x0 || z1 < z0) return none;
    TileSampleRect rect;
    rect.x0 = static_cast<int>(x0); rect.x1 = static_cast<int>(x1);
    rect.z0 = static_cast<int>(z0); rect.z1 = static_cast<int>(z1);
    return rect;
}

namespace {
double SampleWorldPosition(const WorldGridSettings& grid, int tile, int local) { return WorldSamplePosition(grid, tile, local); }
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
