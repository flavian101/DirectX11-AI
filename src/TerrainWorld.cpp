#include "TerrainWorld.h"
#include "Scene.h"
#include "Logger.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

void TerrainWorldSettings::Clamp()
{
    grid.Clamp();
    fractal.octaves = std::clamp(fractal.octaves, 1, 8);
    fractal.baseWavelength = std::clamp(fractal.baseWavelength, 50.0f, 50000.0f);
    heightScale = std::clamp(heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax);
    viewDistance = std::clamp(viewDistance, kViewDistanceMin, kViewDistanceMax);
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

void TerrainWorld::SetSettings(TerrainWorldSettings settings)
{
    settings.Clamp();
    settings_ = settings;
    dirty_ = true;
    failed_ = false;
}

void TerrainWorld::UnloadAll(Scene& scene)
{
    for (const auto& entry : tiles_) scene.Remove(entry.second);
    tiles_.clear();
}

void TerrainWorld::Stream(Scene& scene, double cameraX, double cameraZ)
{
    if (!device_) return;
    if (dirty_) {
        UnloadAll(scene);
        source_ = std::make_shared<const FractalHeightSource>(settings_.fractal);
        layers_ = TerrainLayerSet{};   // the material may have changed with the settings: load the textures again
        dirty_ = false;
    }

    // Forget tiles that are gone from the scene without us (an undo, a cleared scene).
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        if (scene.Find(it->second)) ++it; else it = tiles_.erase(it);
    }
    if (failed_) return;

    std::vector<LoadedTile> loaded;
    loaded.reserve(tiles_.size());
    for (const auto& entry : tiles_) {
        if (const auto* terrain = dynamic_cast<const Terrain*>(scene.Find(entry.second))) loaded.push_back({ terrain->GetTile().coord, terrain->GetTile().lod });
    }
    const StreamingPlan plan = PlanTileStreaming(settings_.grid, cameraX, cameraZ, settings_.viewDistance, TerrainWorldSettings::kMaxTiles, loaded);

    for (const TileCoord& tile : plan.toUnload) {
        const auto it = tiles_.find(Key(tile));
        if (it == tiles_.end()) continue;
        scene.Remove(it->second);
        tiles_.erase(it);
    }

    // Build the nearest requests first, within a budget measured in FULL tiles: a tile at level L has 4^-L of the cells, so
    // several far tiles cost about as much as one near tile. At least one request is always served, so nothing starves.
    double spent = 0.0;
    int built = 0;
    for (const TileRequest& request : plan.toLoad) {
        const double cost = 1.0 / static_cast<double>(1 << (2 * request.lod));
        if (built > 0 && spent + cost > 1.0) break;
        spent += cost;
        ++built;

        TerrainTile tile;
        tile.grid = settings_.grid.ForLod(request.lod);
        tile.coord = request.coord;
        tile.lod = request.lod;
        tile.source = source_;
        Terrain* terrain = scene.Add<Terrain>(device_, tile, MakeWorldMaterial(settings_), layers_.IsValid() ? &layers_ : nullptr);
        if (!terrain) { failed_ = true; break; }
        if (!terrain->IsValid()) {
            LOG_ERROR("Terrain world: tile (" + std::to_string(request.coord.x) + ", " + std::to_string(request.coord.z) + ") could not be built; streaming stopped");
            scene.Remove(terrain->GetId());
            failed_ = true;
            break;
        }
        terrain->SetName("Terrain tile (" + std::to_string(request.coord.x) + ", " + std::to_string(request.coord.z) + ") level " + std::to_string(request.lod));
        if (!layers_.IsValid()) layers_ = terrain->GetLayerSet();   // the first tile's textures are lent to the rest
        // A rebuild at another level: the new tile is in place before the old one goes, in the same call, so no frame sees a hole
        // or two tiles on top of each other.
        const auto old = tiles_.find(Key(request.coord));
        if (old != tiles_.end()) scene.Remove(old->second);
        tiles_[Key(request.coord)] = terrain->GetId();
    }

    for (int& count : lodCounts_) count = 0;
    for (const auto& entry : tiles_) {
        if (const auto* terrain = dynamic_cast<const Terrain*>(scene.Find(entry.second))) ++lodCounts_[std::clamp(terrain->GetTile().lod, 0, WorldGridSettings::kTileLodMax)];
    }
}

void RemoveOrphanTiles(Scene& scene)
{
    std::vector<SceneObjectId> orphans;
    for (const auto& object : scene.GetObjects()) {
        const auto* terrain = dynamic_cast<const Terrain*>(object.get());
        if (terrain && terrain->IsTile()) orphans.push_back(terrain->GetId());
    }
    for (SceneObjectId id : orphans) scene.Remove(id);
}
