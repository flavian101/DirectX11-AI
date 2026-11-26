#pragma once
// How a world's ground LOOKS (roadmap H10): which layer goes where, and the layer textures' tiling and tints. Pure: no Direct3D.
//
// A world's look is a TerrainSettings whose HEIGHT numbers are measured from SEA LEVEL, not from the lowest ground and not from Y = 0:
// the sand reaches `shoreHeight` metres above the sea, the snow starts `snowHeight` metres above it. So the beach stays at the shore when
// the relief, the lowest ground or the picture changes, and moves with the sea when the sea level does. (The first automatic look put the
// sand at a fixed fraction of the relief above the lowest ground, which is nowhere near the sea unless the picture happened to fit.)
#include "TerrainSettings.h"

constexpr float kSeaLevelMin = -2000.0f, kSeaLevelMax = 2000.0f;   // world Y of the sea

// The look a new world starts with: sand along the shore, grass, rock on steep slopes, snow high up; tiles are 1 km wide, so the detail
// distances are longer than for a small terrain.
inline TerrainSettings DefaultWorldLook()
{
    TerrainSettings look;
    look.shoreHeight = 7.0f;    // metres above sea level where the sand ends
    look.shoreWidth = 5.0f;     // over this many metres it fades into the ground
    look.snowHeight = 170.0f;   // metres above sea level where snow begins
    look.snowFade = 20.0f;
    look.lodDistance0 = 350.0f;
    look.lodDistance1 = 900.0f;
    look.Clamp();
    return look;
}

// The material every tile of the world gets: the look, with the heights turned into world Y by the sea level. `reliefMetres` is the tile
// height range the ground is held in (it does not move the layers: only the sea level does).
inline TerrainSettings WorldMaterial(const TerrainSettings& look, float seaLevel, float reliefMetres)
{
    TerrainSettings material = look;
    material.heightScale = reliefMetres;
    material.shoreHeight = seaLevel + look.shoreHeight;
    material.snowHeight = seaLevel + look.snowHeight;
    material.Clamp();
    return material;
}
