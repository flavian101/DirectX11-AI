#pragma once
// The settings of a terrain's MATERIAL and layer rules. Pure: no Direct3D, so they can be tested alone.
#include <DirectXMath.h>
#include <algorithm>

struct TerrainSettings {
	// --- shape: changing any of these rebuilds the mesh ---
	// UNITS: one world unit is one metre (docs/ROADMAP.md). Width and height are independent and both ABSOLUTE:
	// the heightmap picture is stretched over `width` metres and its white is `heightScale` metres tall, so making the terrain
	// wider WITHOUT raising heightScale makes it flatter (slopes = height / distance). The editor can keep the proportions for you.
	float width = 240.0f;       // metres along X; depth follows the heightmap's aspect ratio
	float heightScale = 45.0f;  // metres for a 1.0 (white) heightmap pixel
	int maxCells = 512;         // mesh resolution: grid cells along X (Z follows the aspect)
	int breakupSeed = 0;        // the random seed of the layer rules' breakup (which patches of rock and sand differ)

	// --- material: cheap, applied without rebuilding ---
	float uvTiling = 1.0f;      // how many times the texture repeats across the terrain

	// Layers: ground (grass), rock on steep slopes, sand near the water, snow up high. Each layer has its
	// own albedo, normal and ORM map (assets/Terrain/<Layer>/), blended by the rules below (lesson 17).
	float groundTiling = 0.08f; // how many times each layer's texture repeats per world unit
	float rockTiling = 0.05f;   // (rock is triplanar, see terrain_ps.hlsl)
	float sandTiling = 0.08f;
	float snowTiling = 0.06f;
	float rockDarkness = 1.0f;  // rock brightness: 1 = as in the texture
	float normalStrength = 1.0f; // 0 = flat layers, 1 = as authored
	bool flipGreen = true;       // layer normal maps are OpenGL-style ("_nor_gl_"); untick for DirectX-style
	float heightBlend = 0.6f;    // 0 = plain linear blends, 1 = the taller layer wins (pebbles poke through sand). Per pixel, in the shader
	float aoStrength = 1.0f;
	float roughnessScale = 1.0f;
	DirectX::XMFLOAT3 groundTint = { 1.0f, 1.0f, 1.0f };      // sRGB; tints MULTIPLY the layer's texture
	DirectX::XMFLOAT3 rockTint = { 1.0f, 1.0f, 1.0f };
	// The rules below decide WHERE each layer goes. They are baked into a splat map (lesson 29), so changing one re-bakes
	// the map (a few ms) instead of costing every pixel every frame.
	float slopeStart = 0.25f;   // slope (0 flat .. 1 vertical) where rock starts to appear
	float slopeFade = 0.25f;    // how quickly it takes over
	float shoreHeight = 5.0f;   // world Y below which the ground turns to sand
	float shoreWidth = 2.5f;    // height over which sand fades into ground
	float snowHeight = 28.0f;   // world Y above which snow appears
	float snowFade = 6.0f;
	float breakupScale = 0.06f; // size of the noise that roughens layer edges (per world unit)
	float breakupAmount = 0.5f; // 0 = clean contour lines, 1 = very ragged
	// Tints MULTIPLY a layer's texture. White (the default) leaves the texture exactly as it was made.
	DirectX::XMFLOAT3 shoreColor = { 1.0f, 1.0f, 1.0f };   // sand tint: sRGB, like every colour you pick
	DirectX::XMFLOAT3 snowColor = { 1.0f, 1.0f, 1.0f };
	bool showLayers = false;    // debug: paint rock/snow/sand weights as red/green/blue
	bool frustumCulling = true; // skip chunks outside the view (turn off to compare)

	// --- level of detail ---
	bool lodEnabled = true;
	float lodDistance0 = 90.0f;   // chunks closer than this use full detail
	float lodDistance1 = 220.0f;  // closer than this: half detail; beyond: quarter detail
	bool wireframe = false;       // debug: draw the terrain as a wire mesh to SEE the detail levels

	// One source of truth for the valid ranges: Clamp() enforces them, the editor sliders use them.
	static constexpr float kWidthMin = 40.0f, kWidthMax = 1000.0f;
	static constexpr float kHeightMin = 1.0f, kHeightMax = 500.0f;
	static constexpr int kCellsMin = 32, kCellsMax = 1024;
	static constexpr float kTilingMin = 0.25f, kTilingMax = 64.0f;
	static constexpr float kRockTilingMin = 0.005f, kRockTilingMax = 0.5f;   // also the range of every layer's tiling
	static constexpr float kBandMin = -50.0f, kBandMax = 200.0f;     // world-Y range of the shore/snow SLIDERS of a single terrain
	static constexpr float kBandHardMin = -2000.0f, kBandHardMax = 2000.0f;   // what Clamp allows: a world's automatic bands follow its height and base (roadmap H1)
	static constexpr float kBreakupScaleMin = 0.005f, kBreakupScaleMax = 0.5f;
	static constexpr float kLodDistanceMin = 10.0f, kLodDistanceMax = 1500.0f;

	void Clamp();
	void ResetLayers();   // puts only the layer/material fields back to defaults
	bool SameShape(const TerrainSettings& other) const;  // true if only material fields differ
	bool SameSplatRules(const TerrainSettings& other) const; // true if the rules that decide where each layer goes agree (they are baked, lesson 29)
};
