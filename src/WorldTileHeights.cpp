#include "WorldTileHeights.h"

std::vector<float> GenerateTileBlock(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile, const TileSampleRect& rect)
{
    std::vector<float> block;
    if (rect.IsEmpty()) return block;
    const int width = rect.Width(), height = rect.Height();
    block.resize(static_cast<size_t>(width) * height);
    for (int j = 0; j < height; ++j) {
        const double worldZ = WorldSamplePosition(grid, tile.z, rect.z0 + j);
        for (int i = 0; i < width; ++i) block[static_cast<size_t>(j) * width + i] = source.Height01(WorldSamplePosition(grid, tile.x, rect.x0 + i), worldZ);
    }
    return block;
}

TileHeights GenerateTileHeights(const WorldHeightSource& source, const WorldGridSettings& grid, TileCoord tile)
{
    TileHeights result;
    result.samples = GenerateTileSamples(source, grid, tile);
    result.apron = GenerateTileApron(source, grid, tile);
    return result;
}
