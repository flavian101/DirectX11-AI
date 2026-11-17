#include "TerrainWorld.h"
#include "Scene.h"
#include "Logger.h"
#include <algorithm>
#include <memory>
#include <string>

void TerrainWorldSettings::Clamp()
{
    grid.Clamp();
    fractal.octaves = std::clamp(fractal.octaves, 1, 8);
    fractal.baseWavelength = std::clamp(fractal.baseWavelength, 50.0f, 50000.0f);
    heightScale = std::clamp(heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax);
    radiusInTiles = std::clamp(radiusInTiles, 0, kRadiusMax);
}

TerrainSettings MakeWorldMaterial(const TerrainWorldSettings& world)
{
    TerrainSettings material;
    material.heightScale = world.heightScale;
    // The layer rules talk in world metres. The defaults suit a 45 m tall terrain, so they are scaled to this world's height
    // (the editor's band sliders stop at 200 m: Clamp keeps them in range).
    material.shoreHeight = world.heightScale * 0.03f;
    material.shoreWidth = world.heightScale * 0.02f;
    material.snowHeight = world.heightScale * 0.72f;
    material.snowFade = world.heightScale * 0.08f;
    // A tile is 1 km wide: the default detail distances (90 m, 220 m) would make most of it a coarse mesh.
    material.lodDistance0 = 350.0f;
    material.lodDistance1 = 900.0f;
    material.Clamp();
    return material;
}

std::vector<Terrain*> SpawnTerrainWorld(Scene& scene, ID3D11Device* device, TerrainWorldSettings settings)
{
    settings.Clamp();
    std::vector<Terrain*> tiles;
    const auto source = std::make_shared<const FractalHeightSource>(settings.fractal);
    const TerrainSettings material = MakeWorldMaterial(settings);

    // Nearest the middle first, so a failure (out of memory) leaves the middle of the world rather than one corner.
    std::vector<TileCoord> coords;
    for (int z = -settings.radiusInTiles; z <= settings.radiusInTiles; ++z)
        for (int x = -settings.radiusInTiles; x <= settings.radiusInTiles; ++x) coords.push_back({ x, z });
    std::stable_sort(coords.begin(), coords.end(), [](const TileCoord& a, const TileCoord& b) {
        return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z;
    });

    const Terrain* layersFrom = nullptr;
    for (const TileCoord& coord : coords) {
        TerrainTile tile;
        tile.grid = settings.grid;
        tile.coord = coord;
        tile.source = source;
        Terrain* terrain = scene.Add<Terrain>(device, tile, material, layersFrom);
        if (!terrain) continue;
        if (!terrain->IsValid()) {
            LOG_ERROR("Terrain world: tile (" + std::to_string(coord.x) + ", " + std::to_string(coord.z) + ") could not be built");
            scene.Remove(terrain->GetId());
            continue;
        }
        terrain->SetName("Terrain tile (" + std::to_string(coord.x) + ", " + std::to_string(coord.z) + ")");
        if (!layersFrom) layersFrom = terrain;   // the first tile's textures are shared by all the others
        tiles.push_back(terrain);
    }
    return tiles;
}
