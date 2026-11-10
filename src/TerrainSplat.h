#pragma once
// The terrain's layer weights as DATA: one RGBA8 texel per grid cell, one channel per layer (grass, rock, sand, snow).
// Pure logic (no Direct3D) like Heightfield.h, so it is tested on its own: tests/terrain_splat_test.cpp.
//
// WHY THIS EXISTS (lesson 29). The pixel shader used to decide "how much rock is here?" for EVERY pixel of EVERY frame:
// height, slope and noise, then smoothsteps. Shipped engines do not pay for that per pixel: they work the weights out
// once (Unreal's landscape auto-material cached into a runtime virtual texture, Frostbite's masks, an artist's painted
// control maps) and the shader just reads them. That is what this file does, from the heightfield, with the same
// rules as before (lesson 12), so the look is unchanged while the pixel shader loses the noise, the slope maths and
// the smoothsteps. It also fixes a quiet bug: the old rules read the INTERPOLATED MESH NORMAL, so the borders between
// layers moved when a chunk switched level of detail. The heightfield does not have levels of detail.
//
// It is also the foundation of painting layers (roadmap item 3): a painted layer is just a second set of weights that
// overrides the baked ones before the result is uploaded; the shader never needs to know.
#include "HeightfieldEdit.h"
#include <cstdint>
#include <vector>

constexpr int kSplatChannels = 4;   // grass, rock, sand, snow: the slice order terrain_ps.hlsl and the texture arrays use

// The rules of lesson 12, unchanged. Heights are in WORLD units, slope is 0 (flat) .. 1 (vertical).
struct SplatRules {
    float slopeStart = 0.25f;     // slope where rock starts to appear
    float slopeFade = 0.25f;      // how quickly it takes over
    float shoreHeight = 5.0f;     // world Y below which the ground turns to sand
    float shoreWidth = 2.5f;      // height over which sand fades into ground
    float snowHeight = 28.0f;     // world Y above which snow appears
    float snowFade = 6.0f;
    float breakupScale = 0.06f;   // size of the noise that roughens layer edges (per world unit)
    float breakupAmount = 0.5f;   // 0 = clean contour lines, 1 = very ragged
    int seed = 0;
};

// Where the field sits in the world. The rules talk in world height and world slope, so the splat has to know how the
// terrain's transform stretches and lifts the local heights.
struct SplatPlacement {
    float originY = 0.0f;   // world Y of local height 0
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
};

struct SplatMap {
    int width = 0, height = 0;           // one texel per grid CELL (not per sample): texel i covers cell i
    std::vector<std::uint8_t> rgba;      // row by row; r = grass, g = rock, b = sand, a = snow; always add up to 255
    bool IsEmpty() const { return rgba.empty(); }
    void Resize(int w, int h) { width = w; height = h; rgba.assign(static_cast<size_t>(w) * h * kSplatChannels, 0); }
};

// The block of texels (cells) whose weights can change when the samples in `editedSamples` change: a cell reads its four
// corner samples AND the neighbours those corners read for their normals, which is two samples further out on the low
// side and one on the high side. Clamped to the map.
GridRect SplatCellsForSamples(const GridRect& editedSamples, int cellsX, int cellsZ);

// Bakes the texels in `cells` (inclusive; clamped to the map) from the field. The map must already have the size of the
// field's grid (cellsX x cellsZ); call map.Resize first for a new grid.
void BakeSplat(const Heightfield& field, const SplatRules& rules, const SplatPlacement& placement, const GridRect& cells,
               SplatMap& map);

// The whole map.
void BakeSplatAll(const Heightfield& field, const SplatRules& rules, const SplatPlacement& placement, SplatMap& map);
