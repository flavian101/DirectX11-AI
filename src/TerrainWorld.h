#pragma once
#include <vector>
// A world of terrain tiles that loads and unloads itself around the camera (lessons 48 and 49).
// The maths is in WorldGrid.h (which tiles, where, what height); this is the part that talks to the scene and the GPU.
#include "SceneObject.h"
#include "Terrain.h"
#include "WorldGrid.h"
#include "WorldGroundSource.h"
#include "WorldEdits.h"
#include "WorldGround.h"
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class Scene;

// Where the ground of a world comes from (phase 1 of docs/ROADMAP.md). Both are functions of the WORLD position, so
// tiles meet without seams either way.
enum class WorldSourceKind : int {
    Fractal = 0,   // endless hills from layered noise (the default)
    Picture = 1,   // a heightmap picture placed in the world; the world is bounded to the tiles that cover it
};

struct TerrainWorldSettings {
    WorldGridSettings grid;         // tile size and metres per cell (8 chunks x 32 cells x 4 m = 1024 m tiles)
    WorldSourceKind source = WorldSourceKind::Fractal;
    FractalWorldSettings fractal;   // the hills: seed, octaves, size of the biggest features in metres (source Fractal)
    // Source Picture: an 8-bit or 16-bit greyscale image. Its CENTRE is at (pictureCentreX, pictureCentreZ) in world
    // metres (0, 0 by default: the middle of the island is the middle of the world), it is `pictureWidth` metres wide and its depth follows
    // its shape. Empty path: the default
    // heightmap. The world is bounded to the tiles covering the picture (the `bounds` below are ignored).
    std::string picturePath;
    static constexpr const char* kDefaultPicturePath = "assets/dark alien/Height Map.png";   // used when picturePath is empty
    float pictureWidth = 1024.0f;
    float pictureCentreX = 0.0f, pictureCentreZ = 0.0f;
    // The edge of the world for the Fractal source: unbounded (endless) unless bounded is set. A bounded fractal world is a
    // rectangle of tiles, which is also how a small test world is made.
    WorldBounds bounds;
    // Fine bumps added on top of the picture or the hills, in METRES (src/HeightDetail.h). A picture cannot hold detail finer than its pixels.
    HeightDetailSettings detail;
    bool deterrace = true;          // an 8-bit picture has stairs (256 levels): take them out before use (src/HeightDeterrace.h). 16-bit pictures are never touched
    float heightScale = 240.0f;     // the RELIEF: metres from the lowest ground to the highest. For a picture this is what the file's lowest and highest pixel are
                                    // fitted to (H1: no pixel is clipped); for the hills it is their height. Moving it changes every slope
    float baseHeight = 0.0f;        // the LOWEST Y: the world Y of the lowest ground. Lifts or sinks the whole world, changes no slope (H1)
    float viewDistance = 5000.0f;   // tiles closer than this to the camera are loaded (far ones at a lower level of detail)

    static constexpr float kPictureWidthMin = 40.0f, kPictureWidthMax = 20000.0f;
    static constexpr float kPictureCentreMax = 100000.0f;
    static constexpr float kViewDistanceMin = 500.0f, kViewDistanceMax = 8000.0f;
    static constexpr float kBaseHeightMin = -1000.0f, kBaseHeightMax = 1000.0f;   // metres
    static constexpr int kMaxTiles = 256;   // however far you look, at most this many tiles are alive (far tiles are cheap: 1/4, 1/16, 1/64 of the cells)
    void Clamp();
};

// The look every tile gets: the default terrain material with the layer rules (shore, snow) scaled to this world's height.
TerrainSettings MakeWorldMaterial(const TerrainWorldSettings& world);

// The world itself, as ONE object in the scene: it holds the settings (so it can be saved, edited and undone like anything
// else) and owns no geometry. Its tiles are ordinary Terrain objects (IsTile) that it adds to the scene and removes again.
// They are derived state: delete the world and the scene drops its tiles; undo the delete and it grows them back.
class TerrainWorld final : public SceneObject {
public:
    explicit TerrainWorld(ID3D11Device* device) : device_(device) {}

    const TerrainWorldSettings& GetSettings() const { return settings_; }
    void SetSettings(TerrainWorldSettings settings);   // clamps; every tile is rebuilt from the new settings
    // --- EDITS (roadmap phases 3 and 4): sculpting and painting stored per tile on the world's lattice, see src/WorldEdits.h ---
    const WorldEdits& Edits() const { return *edits_; }
    bool HasEdits() const { return !edits_->IsEmpty(); }
    // A stroke: BeginStroke, any number of SculptStep / PaintStep (world metres), then EndStroke gives the patch for ONE undo step. A step
    // marks the tiles it changed; they are rebuilt by Stream (nearest first).
    void BeginStroke();
    bool SculptStep(const BrushSettings& brush, double worldX, double worldZ, float seconds);
    bool PaintStep(const PaintBrush& brush, double worldX, double worldZ, float seconds);
    WorldEditPatch EndStroke();
    void ApplyEditPatch(const WorldEditPatch& patch, bool useAfter);   // undo (false) and redo (true)
    std::shared_ptr<WorldEdits> CopyEdits() const { return std::make_shared<WorldEdits>(*edits_); }
    void ReplaceEdits(const WorldEdits& edits);                        // undo of a clear
    void ClearEdits(bool heights, bool paint);
    // How high the ground is where a stroke begins (0..1): the level Flatten works towards.
    float GroundHeight01(double worldX, double worldZ) const;
    // The tile under a point, and the nearest ground hit along a ray across all loaded tiles.
    const Terrain* TileAt(Scene& scene, double worldX, double worldZ) const;
    bool Raycast(Scene& scene, const Ray& ray, float& distance, DirectX::XMFLOAT3* point) const;
    // The edits live in a small file beside the scene file (the settings go in the scene itself).
    bool SaveEdits(const std::string& path, std::string& error) const { return SaveWorldEdits(path, *edits_, error); }
    bool LoadEdits(const std::string& path, std::string& error);
    // --- the ground as a function, for things that must not depend on which tiles happen to be loaded (roadmap W1) ---
    // The ground over a world rectangle (metres) as a grid of heights, from the same source and edits the tiles are made
    // from: at full detail each sample is exactly a tile vertex's height. The water measures its shore from this. False when the world
    // has no ground (the picture could not be read).
    bool BuildGroundGrid(double minX, double maxX, double minZ, double maxZ, WorldGroundGrid& out) const;
    bool HasGround() const { return source_ != nullptr && !failed_; }
    // The ground at one point, from the ground function (roadmap C1): the same whether a tile is loaded there or not, and the same
    // triangles a full-detail tile is drawn from. False beyond the edge of a bounded world, or when the world has no ground.
    // The function borrows this world's source: use it before the next change to the world's settings.
    WorldGroundFunction GroundFunction() const;
    bool HeightAt(double worldX, double worldZ, float& heightMetres) const;
    bool NormalAt(double worldX, double worldZ, DirectX::XMFLOAT3& normal) const;
    // Where the ray first meets the ground within maxDistance, tiles loaded or not (unlike Raycast, which tests the loaded tiles' meshes).
    bool RaycastGround(const Ray& ray, float maxDistance, float& distance, DirectX::XMFLOAT3* point) const;
    // Moves whenever the ground function changes: the settings, a finished stroke, an undo, loaded or cleared edits. NOT on every
    // step of a stroke (the water measures when the stroke ends, like the tiles' revision, roadmap T2).
    unsigned GroundRevision() const { return groundRevision_; }

    // --- the picture, for the inspector (null for the hills) ---
    const WorldPictureInfo* Picture() const { return pictureInfo_.valid ? &pictureInfo_ : nullptr; }

    // The edge the world has right now: the settings' bounds for the fractal source, the tiles that cover the picture for the
    // picture source (known once the picture is read). `bounded` is false for an endless world.
    const WorldBounds& ActiveBounds() const { return activeBounds_; }
    // Empty when all is well; otherwise why the world has no land (for example: the picture could not be read). Shown by the inspector.
    const std::string& StatusMessage() const { return status_; }
    int LoadedTileCount() const { return static_cast<int>(tiles_.size()); }
    int LoadedTileCountAtLod(int lod) const { return lod >= 0 && lod <= WorldGridSettings::kTileLodMax ? lodCounts_[lod] : 0; }

    // Called by Scene::Update once a frame, after the objects have updated. Unloads tiles that are out of range and loads
    // the nearest missing one (one build per frame: a tile takes a few milliseconds and we do not want a hitch).
    void Stream(Scene& scene, double cameraX, double cameraZ);
    // Removes every tile from the scene (the world was deleted, or its settings changed).
    void UnloadAll(Scene& scene);

    const char* GetEditorName() const override { return "Terrain world"; }
    bool IsUserContent() const override { return true; }   // so the editor lets you delete it (and undo that)
    bool SupportsRotation() const override { return false; }
    bool SupportsScale() const override { return false; }
    void Update(float) override {}
    void Render(ID3D11DeviceContext*, const RenderView&) const override {}

private:
    static std::uint64_t Key(TileCoord tile) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.x)) << 32) | static_cast<std::uint32_t>(tile.z); }

    ID3D11Device* device_ = nullptr;   // non-owning
    TerrainWorldSettings settings_;
    std::shared_ptr<const WorldHeightSource> source_;   // what tiles read: ground_ with the edits laid over it
    std::shared_ptr<const WorldHeightSource> ground_;   // the picture or hills with detail; no edits (what the brushes work from)
    std::shared_ptr<WorldEdits> edits_ = std::make_shared<WorldEdits>();
    double editsMetresPerCell_ = 0.0;                   // the lattice the edits were made on (with edits_->CellsPerTile())
    WorldStrokeRecorder recorder_;
    WorldPictureInfo pictureInfo_;
    TerrainLayerSet layers_;                              // taken from the first tile built, lent to every later one
    std::unordered_map<std::uint64_t, SceneObjectId> tiles_;   // tile -> the scene object that shows it
    // Heights edited in loaded tiles, waiting to be patched in place by the next Stream (roadmap T2): the union of the lattice points
    // changed since then. A tile that is not level 0 (cheap, far away) or that is stale anyway is rebuilt as before.
    struct PendingEdit { LatticeRect rect; bool final = false; };
    std::unordered_map<std::uint64_t, PendingEdit> pendingEdits_;
    std::unordered_set<std::uint64_t> patchedDuringStroke_;   // tiles a stroke patched: their ground revision moves when it ends
    bool strokeEnded_ = false;
    unsigned groundRevision_ = 1;   // see GroundRevision()
    void GroundChanged() { ++groundRevision_; }
    std::unordered_set<std::uint64_t> staleTiles_;                    // loaded tiles that could not be patched in place: rebuilt first
    WorldBounds activeBounds_;   // see ActiveBounds()
    std::string status_;
    static TileCoord CoordOfKey(std::uint64_t key) { return { static_cast<int>(static_cast<std::uint32_t>(key >> 32)), static_cast<int>(static_cast<std::uint32_t>(key)) }; }
    void BuildSource();                           // the ground and the edge of the world, from the settings (see TerrainWorld.cpp)
    void MarkEditedTiles(const LatticeRect& rect, bool heights, bool final = false);   // loaded tiles whose ground an edit changed: patched in place (heights) or rebuilt (paint)
    void MarkAllTilesStale();
    int lodCounts_[WorldGridSettings::kTileLodMax + 1] = {};   // loaded tiles per level of detail (for the inspector)
    bool dirty_ = true;     // the settings changed: unload, then start again
    bool failed_ = false;   // a tile could not be built: stop trying until the settings change
};

// Scene::Update calls this when the scene has NO world: any tile left over (the world was deleted) goes too.
void RemoveOrphanTiles(Scene& scene);
