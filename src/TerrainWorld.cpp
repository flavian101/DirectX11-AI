#include "TerrainWorld.h"
#include "Scene.h"
#include "Logger.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
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
    pictureCentreX = std::clamp(pictureCentreX, -kPictureCentreMax, kPictureCentreMax);
    pictureCentreZ = std::clamp(pictureCentreZ, -kPictureCentreMax, kPictureCentreMax);
    bounds.Normalize();
    detail.Clamp();
    look.Clamp();
    seaLevel = std::clamp(seaLevel, kSeaLevelMin, kSeaLevelMax);
    heightScale = std::clamp(heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax);
    baseHeight = std::clamp(baseHeight, kBaseHeightMin, kBaseHeightMax);
    viewDistance = std::clamp(viewDistance, kViewDistanceMin, kViewDistanceMax);
}

TerrainSettings MakeWorldMaterial(const TerrainWorldSettings& world)
{
    return WorldMaterial(world.look, world.seaLevel, world.heightScale);
}

void TerrainWorld::SetLook(Scene& scene, const TerrainSettings& look, float seaLevel)
{
    settings_.look = look;
    settings_.seaLevel = seaLevel;
    settings_.Clamp();
    dirty_ = true;
    const TerrainSettings material = MakeWorldMaterial(settings_);
    for (const auto& entry : tiles_) {
        if (auto* terrain = dynamic_cast<Terrain*>(scene.Find(entry.second))) terrain->ApplySettings(material);
    }
}

void TerrainWorld::SetSettings(TerrainWorldSettings settings)
{
    settings.Clamp();
    // The edits sit on a lattice of the grid's cells: while there are edits the grid stays as it is (the inspector says why).
    if (edits_ && !edits_->IsEmpty() && editsMetresPerCell_ > 0.0 &&
        (settings.grid.TileCells() != edits_->CellsPerTile() || static_cast<double>(settings.grid.metresPerCell) != editsMetresPerCell_)) {
        settings.grid = settings_.grid;
    }
    settings_ = settings;
    GroundChanged();
    dirty_ = true;
    failed_ = false;
}

// Makes the ground (ground_, then source_ with the edits over it) and the edge of the world (activeBounds_) from the settings. If the picture
// cannot be read the world has no land: failed_ stops the streaming until the settings change, and status_ says why.
void TerrainWorld::BuildSource()
{
    GroundChanged();   // the ground function is made again (the water measures its shore from it)
    status_.clear();
    activeBounds_ = settings_.bounds;
    pictureInfo_ = WorldPictureInfo{};
    std::shared_ptr<const WorldHeightSource> raw;
    if (settings_.source != WorldSourceKind::Picture) {
        raw = std::make_shared<const FractalHeightSource>(settings_.fractal);
    } else {
        const std::string path = settings_.picturePath.empty() ? std::string(TerrainWorldSettings::kDefaultPicturePath) : settings_.picturePath;
        HeightImportSettings import;
        import.widthMetres = settings_.pictureWidth;
        import.lowestY = settings_.baseHeight;       // the picture's lowest pixel is the lowest ground, its highest pixel is `heightScale` above it (H1)
        import.relief = settings_.heightScale;
        import.deterrace = settings_.deterrace;
        std::shared_ptr<const HeightRaster> raster;
        std::string error;
        if (!ImportWorldRaster(path, import, static_cast<double>(settings_.grid.metresPerCell), settings_.pictureCentreX, settings_.pictureCentreZ, raster, pictureInfo_, error)) {
            status_ = "Terrain world: " + error;
            LOG_ERROR(status_);
            source_ = nullptr;
            ground_ = nullptr;
            activeBounds_ = WorldBounds{};
            failed_ = true;   // no ground, no tiles; changing a setting (another picture) tries again
            return;
        }
        raw = std::make_shared<const RasterHeightSource>(raster, settings_.baseHeight, settings_.heightScale);
        activeBounds_ = WorldBounds::Covering(settings_.grid, raster->originX, raster->originZ, raster->originX + raster->SizeX(), raster->originZ + raster->SizeZ());
    }
    if (settings_.detail.IsIdentity()) ground_ = raw;
    else ground_ = std::make_shared<const DetailedHeightSource>(raw, settings_.detail, settings_.heightScale);

    // The edits' lattice is the grid's cells. SetSettings keeps the grid while there are edits, so a mismatch here means the edits were
    // loaded from a file made with another grid: they cannot be used and are dropped.
    const int cells = settings_.grid.TileCells();
    const double metresPerCell = static_cast<double>(settings_.grid.metresPerCell);
    if (edits_->CellsPerTile() != cells || (!edits_->IsEmpty() && editsMetresPerCell_ != metresPerCell && editsMetresPerCell_ > 0.0)) {
        if (!edits_->IsEmpty()) {
            status_ = "Terrain world: the saved edits were made on another grid and were dropped.";
            LOG_WARNING(status_);
        }
        edits_->Reset(cells);
    }
    editsMetresPerCell_ = metresPerCell;
    source_ = std::make_shared<const EditedHeightSource>(ground_, edits_, metresPerCell, settings_.heightScale);
}

void TerrainWorld::UnloadAll(Scene& scene)
{
    for (const auto& entry : tiles_) scene.Remove(entry.second);
    tiles_.clear();
    staleTiles_.clear();
    pendingEdits_.clear();
    patchedDuringStroke_.clear();
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

    // Heights edited in loaded tiles (a sculpt step, an undo): rewrite only the edited samples in place instead of building a whole
    // new Terrain per tile per step (roadmap T2). The patched tile equals the rebuilt one (tests/tile_patch_test.cpp).
    for (auto it = pendingEdits_.begin(); it != pendingEdits_.end(); it = pendingEdits_.erase(it)) {
        const std::uint64_t key = it->first;
        const auto tileIt = tiles_.find(key);
        if (tileIt == tiles_.end() || staleTiles_.count(key)) continue;   // not loaded, or a full rebuild is coming anyway
        Terrain* terrain = dynamic_cast<Terrain*>(scene.Find(tileIt->second));
        if (!terrain) continue;
        if (terrain->GetTile().lod != 0) { staleTiles_.insert(key); continue; }   // far tiles are cheap (4^-level of the cells): rebuilt as before
        const LatticeRect& edited = it->second.rect;
        const TileSampleRect rect = TileSamplesAffectedBy(settings_.grid, CoordOfKey(key), edited.minX, edited.minZ, edited.maxX, edited.maxZ);
        if (rect.IsEmpty()) continue;
        if (!terrain->PatchTile(rect, it->second.final)) { staleTiles_.insert(key); continue; }   // could not patch: the safe way
        if (!it->second.final) patchedDuringStroke_.insert(key);
    }
    if (strokeEnded_) {
        // The stroke is over: the ground is final, so the water may measure its shore again (it does not at every step).
        for (const std::uint64_t key : patchedDuringStroke_) {
            const auto tileIt = tiles_.find(key);
            if (tileIt == tiles_.end()) continue;
            if (Terrain* terrain = dynamic_cast<Terrain*>(scene.Find(tileIt->second))) terrain->NotifyGroundChanged();
        }
        patchedDuringStroke_.clear();
        strokeEnded_ = false;
    }

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

    // The tiles that could not be patched in place are rebuilt first (at the level they are loaded at, or the one the plan now wants).
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
        tile.outerEdges = OuterEdgesOfTile(activeBounds_, request.coord);   // the edge of a bounded world gets a wall down to a floor (X1)
        tile.capBottom = activeBounds_.bounded;                              // and every tile of it a bottom
        tile.baseHeight = settings_.baseHeight;                              // where the ground starts (H1)
        Terrain* terrain = scene.Add<Terrain>(device_, tile, MakeWorldMaterial(settings_), layers_.IsValid() ? &layers_ : nullptr);
        if (!terrain) { failed_ = true; break; }
        if (!terrain->IsValid()) {
            LOG_ERROR("Terrain world: tile (" + std::to_string(request.coord.x) + ", " + std::to_string(request.coord.z) + ") could not be built; streaming stopped");
            scene.Remove(terrain->GetId());
            failed_ = true;
            break;
        }
        terrain->SetName("Terrain tile (" + std::to_string(request.coord.x) + ", " + std::to_string(request.coord.z) + ") level " + std::to_string(request.lod));
        if (!layers_.IsValid()) {
            // The first tile's textures are lent to the rest.
            layers_ = terrain->GetLayerSet();
        }
        // Painting: the world keeps it, the tile only shows it (at this level of detail: each coarse texel averages the cells it covers).
        if (edits_->TileHasPaint(request.coord)) {
            PaintMap painted = edits_->PaintForTile(request.coord, tile.grid.TileCells());
            if (!painted.IsEmpty()) terrain->ReplacePaint(std::move(painted.weights), true);
        }
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

void TerrainWorld::MarkEditedTiles(const LatticeRect& rect, bool heights, bool final)
{
    for (const TileCoord& coord : edits_->TilesAffectedBy(rect, heights)) {
        const std::uint64_t key = Key(coord);
        if (!tiles_.count(key)) continue;
        if (!heights) { staleTiles_.insert(key); continue; }   // paint: rebuilt as before
        // Heights: remember WHICH lattice points changed; Stream patches the loaded tile (or rebuilds it if it is not at full detail).
        PendingEdit& pending = pendingEdits_[key];
        pending.rect.Include(rect.minX, rect.minZ);
        pending.rect.Include(rect.maxX, rect.maxZ);
        pending.final = pending.final || final;
    }
}

void TerrainWorld::MarkAllTilesStale()
{
    pendingEdits_.clear();   // everything is rebuilt from the current edits anyway
    for (const auto& entry : tiles_) staleTiles_.insert(entry.first);
}

void TerrainWorld::BeginStroke() { recorder_.Clear(); }

bool TerrainWorld::SculptStep(const BrushSettings& brush, double worldX, double worldZ, float seconds)
{
    if (!ground_) return false;
    const LatticeRect rect = ApplyWorldHeightBrush(*edits_, *ground_, editsMetresPerCell_, settings_.heightScale, brush, worldX, worldZ, seconds, &recorder_);
    MarkEditedTiles(rect, true);
    return !rect.IsEmpty();
}

bool TerrainWorld::PaintStep(const PaintBrush& brush, double worldX, double worldZ, float seconds)
{
    if (!ground_) return false;
    const LatticeRect rect = ApplyWorldPaintBrush(*edits_, editsMetresPerCell_, brush, worldX, worldZ, seconds, &recorder_);
    MarkEditedTiles(rect, false);
    return !rect.IsEmpty();
}

WorldEditPatch TerrainWorld::EndStroke()
{
    WorldEditPatch patch = recorder_.Finish(*edits_);
    recorder_.Clear();
    strokeEnded_ = true;   // the next Stream tells the water (and anything else watching) that the patched ground is final
    GroundChanged();
    return patch;
}

void TerrainWorld::ApplyEditPatch(const WorldEditPatch& patch, bool useAfter)
{
    const WorldPatchExtent extent = ApplyWorldEditPatch(*edits_, patch, useAfter);
    GroundChanged();
    MarkEditedTiles(extent.heights, true, true);   // an undo or redo is final at once: no stroke to wait for
    MarkEditedTiles(extent.paint, false);
}

void TerrainWorld::ReplaceEdits(const WorldEdits& edits)
{
    *edits_ = edits;
    GroundChanged();
    MarkAllTilesStale();
}

void TerrainWorld::ClearEdits(bool heights, bool paint)
{
    WorldEdits kept(edits_->CellsPerTile());
    edits_->ForEachBlock([&](TileCoord tile, const WorldEdits::Block& block) {
        WorldEdits::Block copy;
        if (!heights) copy.height = block.height;
        if (!paint) copy.paint = block.paint;
        kept.SetBlock(tile, std::move(copy));
    });
    *edits_ = std::move(kept);
    GroundChanged();
    MarkAllTilesStale();
}

WorldGroundFunction TerrainWorld::GroundFunction() const
{
    WorldGroundFunction f;
    if (!HasGround()) return f;
    f.source = source_.get();
    f.grid = settings_.grid;
    f.bounds = activeBounds_;
    f.heightScale = settings_.heightScale;
    f.baseHeight = settings_.baseHeight;
    return f;
}

bool TerrainWorld::HeightAt(double worldX, double worldZ, float& heightMetres) const
{
    const WorldGroundFunction f = GroundFunction();
    return f.IsValid() && f.HeightAt(worldX, worldZ, heightMetres);
}

bool TerrainWorld::NormalAt(double worldX, double worldZ, DirectX::XMFLOAT3& normal) const
{
    const WorldGroundFunction f = GroundFunction();
    WorldVec3 n;
    if (!f.IsValid() || !f.NormalAt(worldX, worldZ, n)) return false;
    normal = DirectX::XMFLOAT3(n.x, n.y, n.z);
    return true;
}

bool TerrainWorld::RaycastGround(const Ray& ray, float maxDistance, float& distance, DirectX::XMFLOAT3* point) const
{
    const WorldGroundFunction f = GroundFunction();
    if (!f.IsValid()) return false;
    float hit = 0.0f;
    if (!f.Raycast(WorldVec3{ ray.origin.x, ray.origin.y, ray.origin.z }, WorldVec3{ ray.direction.x, ray.direction.y, ray.direction.z }, maxDistance, hit)) return false;
    distance = hit;
    if (point) *point = DirectX::XMFLOAT3(ray.origin.x + ray.direction.x * hit, ray.origin.y + ray.direction.y * hit, ray.origin.z + ray.direction.z * hit);
    return true;
}

bool TerrainWorld::BuildGroundGrid(double minX, double maxX, double minZ, double maxZ, WorldGroundGrid& out) const
{
    out = WorldGroundGrid{};
    if (!HasGround()) return false;
    out = BuildWorldGroundGrid(*source_, settings_.grid, activeBounds_, settings_.heightScale, minX, maxX, minZ, maxZ,
                               1024, settings_.baseHeight);
    return !out.IsEmpty();
}

float TerrainWorld::GroundHeight01(double worldX, double worldZ) const
{
    return source_ ? source_->Height01(worldX, worldZ) : 0.0f;
}

const Terrain* TerrainWorld::TileAt(Scene& scene, double worldX, double worldZ) const
{
    const auto found = tiles_.find(Key(TileContaining(settings_.grid, worldX, worldZ)));
    return found == tiles_.end() ? nullptr : dynamic_cast<const Terrain*>(scene.Find(found->second));
}

bool TerrainWorld::Raycast(Scene& scene, const Ray& ray, float& distance, DirectX::XMFLOAT3* point) const
{
    bool hit = false;
    float best = FLT_MAX;
    for (const auto& entry : tiles_) {
        const auto* tile = dynamic_cast<const Terrain*>(scene.Find(entry.second));
        if (!tile) continue;
        float d = 0.0f;
        DirectX::XMFLOAT3 p;
        if (tile->Raycast(ray, d, &p) && d < best) { best = d; hit = true; if (point) *point = p; }
    }
    if (hit) distance = best;
    return hit;
}

bool TerrainWorld::LoadEdits(const std::string& path, std::string& error)
{
    WorldEdits loaded;
    if (!LoadWorldEdits(path, loaded, error)) return false;
    if (loaded.CellsPerTile() != settings_.grid.TileCells()) {
        error = "the edits were made with tiles of " + std::to_string(loaded.CellsPerTile()) + " cells but this world has " + std::to_string(settings_.grid.TileCells());
        return false;
    }
    *edits_ = std::move(loaded);
    GroundChanged();
    editsMetresPerCell_ = static_cast<double>(settings_.grid.metresPerCell);   // the settings (saved with the scene) are the lattice's other half
    dirty_ = true;   // every tile is rebuilt with the edits
    return true;
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
