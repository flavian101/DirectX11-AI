#pragma once
// The settings of the ocean, and the ways to change them as a group (lessons 35, 43). Pure: no Direct3D, so the logic can
// be tested on its own. (It used to live in Water.h, which needs the graphics headers.)
//
// There is ONE kind of water: the ocean (roadmap M0, T2). Lakes, ponds, rivers and swamps were removed; each of them needs its own
// design and returns later on the same foundation. The defaults below ARE the ocean: there is no "type" and no preset to start from.
#include "UnderwaterLook.h"
#include "WaterWaves.h"
#include "Weather.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

// What the water mirrors. "Sky" is the cheap cubemap from before (reflects only the sky);
// the planar modes draw the whole scene again from below the water (reflects terrain, cube, sky),
// at a quarter / half / full of the screen's resolution.
enum class ReflectionMode : int { Sky = 0, PlanarHalf = 1, PlanarFull = 2, PlanarQuarter = 3 };

// Shows one ingredient of the water on its own, to tune it. Never saved with the scene.
enum class WaterDebugView : int { Off = 0, Normals = 1, Depth = 2, Foam = 3, Fresnel = 4, CrestHeight = 5, ShoreDepth = 6 };

constexpr int kWaterLodCount = 4;   // level 0 = every vertex; each next level keeps fewer (the tile interior thins out, the border never does)

struct WaterSettings {
	// --- the shore: how the water reacts to the ground under it (needs a terrain) ---
	float shoreWaves = 1.0f;      // 0 = waves identical everywhere; 1 = they grow in shallow water, then break and die at the shore
	float swash = 0.6f;           // how far the waterline (foam and the see-through edge) slides up and down the beach with each wave

	// --- surface ---
	float height = 0.0f;          // world Y of the water plane = sea level (kept in sync with the editor transform)
	float size = 2000.0f;         // width: the plane's extent along X. TEMPORARY: the camera-centred ocean (roadmap S5) removes the size
	float sizeZ = 2000.0f;        // length: the plane's extent along Z. TEMPORARY, as above
	DirectX::XMFLOAT3 color = { 0.02f, 0.12f, 0.28f };   // DEEP water colour
	float alpha = 0.80f;                                 // opacity of deep water

	// --- depth: how the water changes between shore and open sea ---
	DirectX::XMFLOAT3 shallowColor = { 0.08f, 0.40f, 0.50f };
	float absorption = 0.45f;     // how fast light is absorbed with depth (per world unit)
	UnderwaterSettings underwater;   // how the world looks from UNDER this water (fog, light): automatic from the colours above, or custom
	float fadeDistance = 2.5f;    // depth at which the water becomes fully opaque (shore softness)
	float foamDistance = 1.0f;    // width of the shoreline foam band, in depth units
	float foamStrength = 0.9f;
	float foamScale = 1.0f;       // size of the bubbles: larger = finer bubbles (lesson 37)
	float foamTrail = 1.8f;       // seconds the foam of a breaking crest lingers behind it (0 = only while it is breaking)
	float foamIntensity = 1.0f;   // MASTER foam control (lesson 43): scales the shoreline foam and the crest foam together. 0 = no foam at all

	// --- waves: what the scene's WIND makes (lesson 63). The wind speed and the fetch decide how tall and how long the waves are ---
	float waveScale = 1.0f;       // an artist's exaggeration of the wave HEIGHT (1 = what the wind makes); wavelengths and speeds do not change
	float directionalSpread = 0.7f;   // 0 = all waves run with the wind (a clean swell), 1 = waves from every which way
	float choppiness = 0.55f;     // 0 = rounded sine waves, 1 = sharp crests and wide troughs
	int waveCount = 16;           // 2..16 waves summed (WaterWaves::kMaxWaves). Fewer = cheaper and simpler
	float swell = 0.5f;           // 0 = none; 1 = a swell nearly as tall as the biggest wind wave
	float swellAngle = 35.0f;     // degrees from the wind: where the swell comes from
	float swellLength = 2.5f;     // the swell's wavelength as a multiple of the biggest wind wave's
	float waveGroups = 0.4f;      // 0 = the same waves everywhere; 1 = patches of calm and rough water that drift
	float shoreShelter = 0.8f;    // 0 = off; 1 = waves running AWAY from a nearby shore (the lee of an island) nearly vanish
	int waveSeed = 1;             // another seed = another sea with the same character
	float detailNormal = 0.5f;    // strength of the small ripples on top of the waves (0 = off, saves pixel work)
	float detailScale = 0.8f;     // ripples per world unit
	float detailDistance = 90.0f; // ripples fade out by this distance from the camera

	// --- lighting ---
	float reflectivity = 1.0f;    // 0 = no sky reflection, 1 = full Fresnel reflection
	ReflectionMode reflectionMode = ReflectionMode::PlanarHalf;
	float reflectionDistortion = 0.04f;   // how far the waves wobble the mirror image (fraction of the screen)
	float refraction = 0.035f;    // how far waves bend the view of the bottom (fraction of the screen). 0 = off, skips a screen copy
	float scatter = 0.5f;         // glow of crests lit from behind
	float glintStrength = 1.0f;   // brightness of the sun highlight
	float glintSharpness = 128.0f;// higher = smaller, tighter highlight
	float crestFoam = 0.5f;       // foam on the steepest wave crests (0 = shoreline foam only)
	float crestFoamThreshold = 0.25f;   // higher = only the sharpest crests foam

	// --- mesh and speed ---
	int meshDetail = 1;           // 0 = 128, 1 = 256, 2 = 512 grid cells across the full plane at the finest level (rebuilds the mesh)
	float lodDistance = 60.0f;    // full mesh detail until this far from the camera; coarser (4 levels) beyond
	bool frustumCulling = true;   // skip the parts of the water the camera cannot see

	// --- debugging: not saved in scene files ---
	WaterDebugView debugView = WaterDebugView::Off;
	bool wireframe = false;       // draw the mesh as wires: shows the level-of-detail tiles

	static constexpr float kFoamScaleMin = 0.3f, kFoamScaleMax = 3.0f, kFoamTrailMax = 4.0f, kFoamIntensityMax = 2.0f;
	static constexpr float kSwashMax = 1.5f;
	static constexpr float kHeightMin = -100.0f, kHeightMax = 200.0f;
	static constexpr float kSizeMin = 2.0f, kSizeMax = 2000.0f;
	static constexpr float kWaveScaleMin = 0.25f, kWaveScaleMax = 8.0f;
	static constexpr int kWaveCountMin = 2, kWaveCountMax = WaterWaves::kMaxWaves;
	static constexpr float kSwellLengthMin = 1.5f, kSwellLengthMax = 6.0f, kWaveGroupsMax = 0.9f;
	static constexpr float kDetailMax = 2.0f, kDetailScaleMin = 0.1f, kDetailScaleMax = 4.0f;
	static constexpr float kDetailDistanceMin = 10.0f, kDetailDistanceMax = 500.0f;
	static constexpr float kGlintMax = 3.0f;
	static constexpr float kSharpnessMin = 8.0f, kSharpnessMax = 512.0f;
	static constexpr float kAbsorptionMin = 0.02f, kAbsorptionMax = 2.0f;
	static constexpr float kFadeMin = 0.1f, kFadeMax = 20.0f;
	static constexpr float kFoamDistanceMax = 5.0f;
	static constexpr float kDistortionMax = 0.2f;
	static constexpr float kRefractionMax = 0.15f;
	static constexpr float kScatterMax = 2.0f;
	static constexpr int kMeshDetailMax = 2;
	static constexpr float kLodDistanceMin = 10.0f, kLodDistanceMax = 1000.0f;

	void Clamp();
	WaterWaves::Params WaveParams() const;

	// ---- The sections of the Inspector, as data (lesson 43) ----------------------------------------------------------
	// Which setting belongs to which section is written down ONCE, in VisitGroup. Resetting a section, marking the ones that
	// differ from their defaults, and applying a preset all walk that one list, so they cannot disagree about what a section is.
	enum class Group : int { Appearance, Waves, Reflection, Refraction, Lighting, Foam, Shoreline, Performance, Underwater, Count };
	// Calls f(this ocean's field, other's field) for each field of the group.
	template <class Self, class F>
	static void VisitGroup(Group group, Self& a, const WaterSettings& b, F&& f);
	// An ocean with every setting at its default.
	static WaterSettings Defaults() { return WaterSettings{}; }
	void ResetGroup(Group group, const WaterSettings& defaults);
	bool GroupModified(Group group, const WaterSettings& defaults) const;
	// Takes the LOOK of another ocean (a saved preset file): appearance, waves, reflection, refraction, lighting, foam, shoreline, underwater.
	// Not its place (level, size) or its performance choices: a preset says how water looks, not where it is.
	void ApplyPreset(const WaterSettings& preset);
};
inline void WaterSettings::Clamp()
{
	auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
	height = std::clamp(height, kHeightMin, kHeightMax);
	size = std::clamp(size, kSizeMin, kSizeMax);
	sizeZ = std::clamp(sizeZ, kSizeMin, kSizeMax);
	shoreWaves = clamp01(shoreWaves);
	underwater.Clamp();
	swash = std::clamp(swash, 0.0f, kSwashMax);
	foamScale = std::clamp(foamScale, kFoamScaleMin, kFoamScaleMax);
	foamTrail = std::clamp(foamTrail, 0.0f, kFoamTrailMax);
	foamIntensity = std::clamp(foamIntensity, 0.0f, kFoamIntensityMax);
	color = { clamp01(color.x), clamp01(color.y), clamp01(color.z) };
	alpha = std::clamp(alpha, 0.05f, 1.0f);
	shallowColor = { clamp01(shallowColor.x), clamp01(shallowColor.y), clamp01(shallowColor.z) };
	absorption = std::clamp(absorption, kAbsorptionMin, kAbsorptionMax);
	fadeDistance = std::clamp(fadeDistance, kFadeMin, kFadeMax);
	foamDistance = std::clamp(foamDistance, 0.0f, kFoamDistanceMax);
	foamStrength = clamp01(foamStrength);
	reflectionDistortion = std::clamp(reflectionDistortion, 0.0f, kDistortionMax);
	reflectionMode = static_cast<ReflectionMode>(std::clamp(static_cast<int>(reflectionMode), 0, 3));
	refraction = std::clamp(refraction, 0.0f, kRefractionMax);
	scatter = std::clamp(scatter, 0.0f, kScatterMax);
	waveScale = std::clamp(waveScale, kWaveScaleMin, kWaveScaleMax);
	directionalSpread = clamp01(directionalSpread);
	choppiness = clamp01(choppiness);
	waveCount = std::clamp(waveCount, kWaveCountMin, kWaveCountMax);
	swell = clamp01(swell);
	swellAngle = std::clamp(swellAngle, -90.0f, 90.0f);
	swellLength = std::clamp(swellLength, kSwellLengthMin, kSwellLengthMax);
	waveGroups = std::clamp(waveGroups, 0.0f, kWaveGroupsMax);
	waveSeed = std::clamp(waveSeed, 0, 9999);
	shoreShelter = clamp01(shoreShelter);
	detailNormal = std::clamp(detailNormal, 0.0f, kDetailMax);
	detailScale = std::clamp(detailScale, kDetailScaleMin, kDetailScaleMax);
	detailDistance = std::clamp(detailDistance, kDetailDistanceMin, kDetailDistanceMax);
	reflectivity = clamp01(reflectivity);
	glintStrength = std::clamp(glintStrength, 0.0f, kGlintMax);
	glintSharpness = std::clamp(glintSharpness, kSharpnessMin, kSharpnessMax);
	crestFoam = clamp01(crestFoam);
	crestFoamThreshold = clamp01(crestFoamThreshold);
	meshDetail = std::clamp(meshDetail, 0, kMeshDetailMax);
	lodDistance = std::clamp(lodDistance, kLodDistanceMin, kLodDistanceMax);
	debugView = static_cast<WaterDebugView>(std::clamp(static_cast<int>(debugView), 0, 6));
}

inline WaterWaves::Params WaterSettings::WaveParams() const
{
	WaterWaves::Params p;
	p.spread = directionalSpread;
	p.choppiness = choppiness;
	p.count = waveCount;
	p.swell = swell;
	p.swellAngle = swellAngle;
	p.swellLength = swellLength;
	p.groups = waveGroups;
	p.seed = static_cast<std::uint32_t>(waveSeed);
	p.heightScale = waveScale;   // the sea is what the wind makes: the author's scale only exaggerates the height (Water adds the wind and the fetch)
	return p;
}

// ---- Groups ---------------------------------------------------------------------------------------------------------------
namespace WaterSettingsDetail {
inline bool Same(float a, float b) { return std::fabs(a - b) <= 1e-6f; }
inline bool Same(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z); }
inline bool Same(const UnderwaterSettings& a, const UnderwaterSettings& b)
{
	return a.custom == b.custom && a.shallowColor == b.shallowColor && a.deepColor == b.deepColor && Same(a.visibility, b.visibility) &&
		   Same(a.colorDepth, b.colorDepth) && Same(a.lightDepth, b.lightDepth) && Same(a.brightness, b.brightness) &&
		   Same(a.nightBrightness, b.nightBrightness) && Same(a.sunGlow, b.sunGlow) && Same(a.startDistance, b.startDistance) &&
		   Same(a.surfaceMirror, b.surfaceMirror);
}
template <class T> inline bool Same(const T& a, const T& b) { return a == b; }   // ints, bools, enums
}

template <class Self, class F>
inline void WaterSettings::VisitGroup(Group group, Self& a, const WaterSettings& b, F&& f)
{
#define WATER_FIELD(name) f(a.name, b.name)
	switch (group) {
	case Group::Appearance:  WATER_FIELD(color); WATER_FIELD(alpha); WATER_FIELD(shallowColor); WATER_FIELD(absorption); break;
	case Group::Waves:
		WATER_FIELD(waveScale); WATER_FIELD(directionalSpread); WATER_FIELD(choppiness); WATER_FIELD(waveCount);
		WATER_FIELD(swell); WATER_FIELD(swellAngle); WATER_FIELD(swellLength); WATER_FIELD(waveGroups); WATER_FIELD(waveSeed);
		WATER_FIELD(detailNormal); WATER_FIELD(detailScale); WATER_FIELD(detailDistance);
		break;
	case Group::Reflection:  WATER_FIELD(reflectivity); WATER_FIELD(reflectionMode); WATER_FIELD(reflectionDistortion); break;
	case Group::Refraction:  WATER_FIELD(refraction); break;
	case Group::Lighting:    WATER_FIELD(scatter); WATER_FIELD(glintStrength); WATER_FIELD(glintSharpness); break;
	case Group::Foam:
		WATER_FIELD(foamIntensity); WATER_FIELD(foamDistance); WATER_FIELD(foamStrength); WATER_FIELD(foamScale); WATER_FIELD(foamTrail);
		WATER_FIELD(crestFoam); WATER_FIELD(crestFoamThreshold);
		break;
	case Group::Shoreline:   WATER_FIELD(fadeDistance); WATER_FIELD(shoreWaves); WATER_FIELD(swash); WATER_FIELD(shoreShelter); break;
	case Group::Performance: WATER_FIELD(meshDetail); WATER_FIELD(lodDistance); WATER_FIELD(frustumCulling); break;
	case Group::Underwater:  WATER_FIELD(underwater); break;
	case Group::Count:       break;
	}
#undef WATER_FIELD
}

inline void WaterSettings::ResetGroup(Group group, const WaterSettings& defaults)
{
	VisitGroup(group, *this, defaults, [](auto& mine, const auto& theirs) { mine = theirs; });
	Clamp();
}

inline bool WaterSettings::GroupModified(Group group, const WaterSettings& defaults) const
{
	bool differs = false;
	VisitGroup(group, *this, defaults, [&](const auto& mine, const auto& theirs) { if (!WaterSettingsDetail::Same(mine, theirs)) differs = true; });
	return differs;
}

inline void WaterSettings::ApplyPreset(const WaterSettings& preset)
{
	for (Group g : { Group::Appearance, Group::Waves, Group::Reflection, Group::Refraction, Group::Lighting, Group::Foam, Group::Shoreline, Group::Underwater })
		ResetGroup(g, preset);   // "reset to that ocean's values" is exactly "take its values"
}
