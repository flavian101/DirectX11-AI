#pragma once
// A world of terrain tiles that loads and unloads itself around the camera (lessons 48 and 49).
// The maths is in WorldGrid.h (which tiles, where, what height); this is the part that talks to the scene and the GPU.
#include "SceneObject.h"
#include "Terrain.h"
#include "WorldGrid.h"
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <unordered_map>

class Scene;

struct TerrainWorldSettings {
    WorldGridSettings grid;         // tile size and metres per cell (8 chunks x 32 cells x 4 m = 1024 m tiles)
    FractalWorldSettings fractal;   // the hills: seed, octaves, size of the biggest features in metres
    float heightScale = 240.0f;     // metres for a ground height of 1.0
    float viewDistance = 2000.0f;   // tiles closer than this to the camera are loaded

    static constexpr float kViewDistanceMin = 500.0f, kViewDistanceMax = 10000.0f;
    static constexpr int kMaxTiles = 64;   // however far you look, at most this many tiles are alive (memory and build time)
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
    int LoadedTileCount() const { return static_cast<int>(tiles_.size()); }

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
    bool dirty_ = true;     // the settings changed: unload, then start again
    bool failed_ = false;   // a tile could not be built: stop trying until the settings change
};

// Scene::Update calls this when the scene has NO world: any tile left over (the world was deleted) goes too.
void RemoveOrphanTiles(Scene& scene);
