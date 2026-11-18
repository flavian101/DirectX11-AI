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
    if (source != WorldSourceKind::Fractal && source != WorldSourceKind::Picture) source = WorldSourceKind::Fractal;   // a scene file from a newer version
    pictureWidth = std::clamp(pictureWidth, kPictureWidthMin, kPictureWidthMax);
    pictureOffsetX = std::clamp(pictureOffsetX, -kPictureOffsetMax, kPictureOffsetMax);
    pictureOffsetZ = std::clamp(pictureOffsetZ, -kPictureOffsetMax, kPictureOffsetMax);
    bounds.Normalize();
    heightScale = std::clamp(heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax);
    viewDistance = std::clamp(viewDistance, kViewDistanceMin, kViewDistanceMax);
    if (static_cast<int>(rivers.size()) > kMaxRivers) rivers.resize(kMaxRivers);
    for (WorldRiver& river : rivers) {
        if (static_cast<int>(river.path.size()) > kMaxRiverSamples) river.path.resize(kMaxRiverSamples);
        river.carve.bankWidth = std::clamp(river.carve.bankWidth, 0.5f, 200.0f);
    }
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
    RefreshRivers();
    dirty_ = true;
    failed_ = false;
}

void TerrainWorld::RefreshRivers()
{
    for (const WorldRiver& river : settings_.rivers) nextRiverId_ = std::max(nextRiverId_, river.id + 1);
    for (WorldRiver& river : settings_.rivers) {
        if (river.id <= 0) river.id = nextRiverId_++;
    }
    riversSnapshot_ = settings_.rivers.empty() ? nullptr : std::make_shared<const std::vector<WorldRiver>>(settings_.rivers);
}

void TerrainWorld::MarkTilesTouchedBy(const WorldRiver& river)
{
    for (const auto& entry : tiles_) {
        if (RiverTouchesTile(settings_.grid, CoordOfKey(entry.first), river)) staleTiles_.insert(entry.first);
    }
}

int TerrainWorld::AddRiver(WorldRiver river)
{
    if (river.path.size() < 2 || static_cast<int>(settings_.rivers.size()) >= TerrainWorldSettings::kMaxRivers) return 0;
    if (static_cast<int>(river.path.size()) > TerrainWorldSettings::kMaxRiverSamples) river.path.resize(TerrainWorldSettings::kMaxRiverSamples);
    if (river.id > 0) nextRiverId_ = std::max(nextRiverId_, river.id + 1); else river.id = nextRiverId_++;
    settings_.rivers.push_back(river);
    RefreshRivers();
    MarkTilesTouchedBy(river);   // only the tiles it touches are rebuilt, nearest the camera first
    return river.id;
}

bool TerrainWorld::RemoveRiver(int id)
{
    const auto it = std::find_if(settings_.rivers.begin(), settings_.rivers.end(), [id](const WorldRiver& river) { return river.id == id; });
    if (it == settings_.rivers.end()) return false;
    const WorldRiver removed = *it;
    settings_.rivers.erase(it);
    RefreshRivers();
    MarkTilesTouchedBy(removed);
    return true;
}

// Makes the ground (source_) and the edge of the world (activeBounds_) from the settings. If the picture cannot be read the world
// has no land: failed_ stops the streaming until the settings change, and status_ says why.
void TerrainWorld::BuildSource()
{
    status_.clear();
    activeBounds_ = settings_.bounds;
    if (settings_.source != WorldSourceKind::Picture) {
        source_ = std::make_shared<const FractalHeightSource>(settings_.fractal);
        return;
    }
    const std::string path = settings_.picturePath.empty() ? std::string(Terrain::DefaultHeightmapPath()) : settings_.picturePath;
    auto image = std::make_shared<HeightImage>();
    std::string error;
    if (!LoadHeightImage(path, *image, error)) {
        status_ = "Terrain world: " + error;
        LOG_ERROR(status_);
        source_ = nullptr;
        activeBounds_ = WorldBounds{};
        failed_ = true;   // no ground, no tiles; changing a setting (another picture) tries again
        return;
    }
    const double width = settings_.pictureWidth;
    const double depth = PictureDepth(width, image->width, image->height);
    const double offsetX = settings_.pictureOffsetX, offsetZ = settings_.pictureOffsetZ;
    source_ = std::make_shared<const PictureHeightSource>(image, offsetX, offsetZ, width, depth);
    activeBounds_ = WorldBounds::Covering(settings_.grid, offsetX, offsetZ, offsetX + width, offsetZ + depth);
}

void TerrainWorld::UnloadAll(Scene& scene)
{
    for (const auto& entry : tiles_) scene.Remove(entry.second);
    tiles_.clear();
    staleTiles_.clear();
}

void TerrainWorld::Stream(Scene& scene, double cameraX, double cameraZ)
{
    if (!device_) return;
    if (dirty_) {
        UnloadAll(scene);
        BuildSource();
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
    const StreamingPlan plan = PlanTileStreaming(settings_.grid, cameraX, cameraZ, settings_.viewDistance, TerrainWorldSettings::kMaxTiles, loaded, activeBounds_);

    for (const TileCoord& tile : plan.toUnload) {
        const auto it = tiles_.find(Key(tile));
        if (it == tiles_.end()) continue;
        scene.Remove(it->second);
        staleTiles_.erase(it->first);
        tiles_.erase(it);
    }

    // The tiles a river change touched are rebuilt first (at the level they are loaded at, or the one the plan now wants).
    std::vector<TileRequest> requests;
    for (const std::uint64_t key : staleTiles_) {
        const TileCoord coord = CoordOfKey(key);
        const auto planned = std::find_if(plan.toLoad.begin(), plan.toLoad.end(), [&](const TileRequest& r) { return r.coord == coord; });
        int lod = 0;
        if (planned != plan.toLoad.end()) lod = planned->lod;
        else if (const auto loadedTile = std::find_if(loaded.begin(), loaded.end(), [&](const LoadedTile& t) { return t.coord == coord; }); loadedTile != loaded.end()) lod = loadedTile->lod;
        else continue;
        requests.push_back({ coord, lod });
    }
    for (const TileRequest& request : plan.toLoad) {
        if (staleTiles_.count(Key(request.coord)) == 0) requests.push_back(request);
    }

    // Build the nearest requests first, within a budget measured in FULL tiles: a tile at level L has 4^-L of the cells, so
    // several far tiles cost about as much as one near tile. At least one request is always served, so nothing starves.
    double spent = 0.0;
    int built = 0;
    for (const TileRequest& request : requests) {
        const double cost = 1.0 / static_cast<double>(1 << (2 * request.lod));
        if (built > 0 && spent + cost > 1.0) break;
        spent += cost;
        ++built;

        TerrainTile tile;
        tile.grid = settings_.grid.ForLod(request.lod);
        tile.coord = request.coord;
        tile.lod = request.lod;
        tile.source = source_;
        tile.rivers = riversSnapshot_;
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
        staleTiles_.erase(Key(request.coord));
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
