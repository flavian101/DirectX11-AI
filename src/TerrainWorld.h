#pragma once
// A world of terrain tiles that loads and unloads itself around the camera (lessons 48 and 49).
// The maths is in WorldGrid.h (which tiles, where, what height); this is the part that talks to the scene and the GPU.
#include "SceneObject.h"
#include "Terrain.h"
#include "WorldGrid.h"
#include "WorldPicture.h"
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

class Scene;

// Where the ground of a world comes from (phase 1 of docs/terrain-world-roadmap.md). Both are functions of the WORLD position, so
// tiles meet without seams either way.
enum class WorldSourceKind : int {
    Fractal = 0,   // endless hills from layered noise (the default)
    Picture = 1,   // a heightmap picture placed in the world; the world is bounded to the tiles that cover it
};

struct TerrainWorldSettings {
    WorldGridSettings grid;         // tile size and metres per cell (8 chunks x 32 cells x 4 m = 1024 m tiles)
    WorldSourceKind source = WorldSourceKind::Fractal;
    FractalWorldSettings fractal;   // the hills: seed, octaves, size of the biggest features in metres (source Fractal)
    // Source Picture: an 8-bit or 16-bit greyscale image. Its top-left corner is at (pictureOffsetX, pictureOffsetZ) in world
    // metres, it is `pictureWidth` metres wide and its depth follows its shape, like the old terrain. Empty path: the default
    // heightmap. The world is bounded to the tiles covering the picture (the `bounds` below are ignored).
    std::string picturePath;
    float pictureWidth = 1024.0f;
    float pictureOffsetX = 0.0f, pictureOffsetZ = 0.0f;
    // The edge of the world for the Fractal source: unbounded (endless) unless bounded is set. A bounded fractal world is a
    // rectangle of tiles, which is also how a small test world is made.
    WorldBounds bounds;
    float heightScale = 240.0f;     // metres for a ground height of 1.0
    float viewDistance = 5000.0f;   // tiles closer than this to the camera are loaded (far ones at a lower level of detail)
    std::vector<WorldRiver> rivers; // the rivers dug into the ground (lesson 51): part of the world, so they survive streaming

    static constexpr float kPictureWidthMin = 40.0f, kPictureWidthMax = 20000.0f;
    static constexpr float kPictureOffsetMax = 100000.0f;
    static constexpr float kViewDistanceMin = 500.0f, kViewDistanceMax = 8000.0f;
    static constexpr int kMaxRivers = 32, kMaxRiverSamples = 4096;
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
    // Rivers (lesson 51). A river is part of the world's height function, so adding one rebuilds only the tiles it touches, and
    // it comes back whenever a tile streams in again. AddRiver returns the river's id (0 if there is no room); give the river an
    // id to put a removed one back under the same name (redo).
    int AddRiver(WorldRiver river);
    bool RemoveRiver(int id);
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
    std::shared_ptr<const WorldHeightSource> source_;
    TerrainLayerSet layers_;                              // taken from the first tile built, lent to every later one
    std::unordered_map<std::uint64_t, SceneObjectId> tiles_;   // tile -> the scene object that shows it
    std::shared_ptr<const std::vector<WorldRiver>> riversSnapshot_;   // what new tiles are built with: replaced whenever the rivers change
    std::unordered_set<std::uint64_t> staleTiles_;                    // loaded tiles that a river change touched: rebuilt first
    int nextRiverId_ = 1;
    WorldBounds activeBounds_;   // see ActiveBounds()
    std::string status_;
    static TileCoord CoordOfKey(std::uint64_t key) { return { static_cast<int>(static_cast<std::uint32_t>(key >> 32)), static_cast<int>(static_cast<std::uint32_t>(key)) }; }
    void BuildSource();                           // the ground and the edge of the world, from the settings (see TerrainWorld.cpp)
    void RefreshRivers();                         // new snapshot, and ids for rivers that have none
    void MarkTilesTouchedBy(const WorldRiver& river);
    int lodCounts_[WorldGridSettings::kTileLodMax + 1] = {};   // loaded tiles per level of detail (for the inspector)
    bool dirty_ = true;     // the settings changed: unload, then start again
    bool failed_ = false;   // a tile could not be built: stop trying until the settings change
};

// Scene::Update calls this when the scene has NO world: any tile left over (the world was deleted) goes too.
void RemoveOrphanTiles(Scene& scene);
