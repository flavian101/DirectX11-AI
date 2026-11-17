#pragma once
// Making a world of terrain tiles in a scene (lesson 48). The maths is in WorldGrid.h; this is the part that talks to the GPU.
#include "Terrain.h"
#include "WorldGrid.h"
#include <d3d11.h>
#include <vector>

class Scene;

struct TerrainWorldSettings {
    WorldGridSettings grid;         // tile size and metres per cell (8 chunks x 32 cells x 4 m = 1024 m tiles)
    FractalWorldSettings fractal;   // the hills: seed, octaves, size of the biggest features in metres
    float heightScale = 240.0f;     // metres for a ground height of 1.0
    int radiusInTiles = 1;          // the world is (2r + 1) x (2r + 1) tiles, centred on tile (0, 0)

    static constexpr int kRadiusMax = 4;   // 9 x 9 = 81 tiles: more than the streaming of a later lesson will keep anyway
    void Clamp();
};

// The look every tile gets: the default terrain material with the layer rules (shore, snow) scaled to this world's height.
TerrainSettings MakeWorldMaterial(const TerrainWorldSettings& world);

// Adds the tiles of the world to the scene, nearest the middle first, and returns them (empty if the first one failed to build).
// Every tile shares the first tile's layer textures. Tiles that fail to build are not added.
std::vector<Terrain*> SpawnTerrainWorld(Scene& scene, ID3D11Device* device, TerrainWorldSettings settings);
