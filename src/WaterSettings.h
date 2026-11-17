#pragma once
// The settings of one water body, and the ways to change them as a group (lessons 35, 43). Pure: no Direct3D, so the logic can
// be tested on its own. (It used to live in Water.h, which needs the graphics headers.)
#include "RiverPath.h"
#include "UnderwaterLook.h"
#include "WaterBodyType.h"
#include "WaterWaves.h"
#include "Weather.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

enum class WavePreset { Calm, Choppy, Stormy };

// A complete colour/optics look (not the waves, not the level or size). Pick one in the editor, then tweak.
enum class WaterLook { Tropical, Ocean, Lake, Swamp, Arctic };

// What KIND of water this is. It decides how the surface moves: an ocean rolls in swells from the wind, a lake shows
// short wind chop, a pond barely moves, and a RIVER is a current: everything on it is carried downstream.
// Values are saved in scene files by name: only ever ADD to the end.
// (WaterBodyType itself lives in WaterBodyType.h, and WaterTypeUi.h says which settings each type shows.)

// What the water mirrors. "Sky" is the cheap cubemap from before (reflects only the sky);
// the planar modes draw the whole scene again from below the water (reflects terrain, cube, sky),
// at a quarter / half / full of the screen's resolution. Values are saved in scene files: only ever ADD to the end.
enum class ReflectionMode : int { Sky = 0, PlanarHalf = 1, PlanarFull = 2, PlanarQuarter = 3 };

// The outline of a water body, seen from above. Values are saved in scene files: only ever ADD to the end.
// A round pond is the square plane cut to its inscribed ellipse; a river is a long thin rectangle.
enum class WaterShape : int { Rectangle = 0, Ellipse = 1 };

// Shows one ingredient of the water on its own, to tune it. Never saved with the scene.
enum class WaterDebugView : int { Off = 0, Normals = 1, Depth = 2, Foam = 3, Fresnel = 4, CrestHeight = 5, ShoreDepth = 6 };

constexpr int kWaterLodCount = 4;   // level 0 = every vertex; each next level keeps fewer (the tile interior thins out, the border never does)

struct WaterSettings {
	// --- what kind of water ---
	WaterBodyType bodyType = WaterBodyType::Ocean;
	float flowSpeed = 2.0f;       // river: how fast the surface is carried downstream, world units per second
	float flowAngle = 0.0f;       // river: degrees away from the body's own +Z axis (its length). 0 = flows toward +Z of the body
	// A current is not one speed across the whole river (lesson 37): the water drags on its bed and banks.
	float bankDrag = 0.6f;        // 0 = the bed does not slow the water; 1 = at a shallow bank it crawls
	float channelProfile = 0.5f;  // 0 = one speed across the plane; 1 = the sides of the plane are much slower than the middle
	float rippleStretch = 3.0f;   // how much longer than wide the ripples are along the flow (1 = round blobs)
	float rapids = 0.5f;          // 0 = foam only at the very edge; 1 = foam spreads into fast shallow water
	// The river as a CURVE (lesson 38). Two or more points: the current follows the curve (the Flow direction is then ignored),
	// and "Carve channel" digs the terrain along it. Points are in the body's own frame, so the river turns with the body.
	std::vector<RiverPoint> riverPath;
	float riverDepth = 1.8f;       // carve: how far below the surface the bed lies in the middle (world units)
	float riverBank = 5.0f;        // carve: how wide the bank is beyond the water's edge
	float riverBankHeight = 2.5f;  // carve: how high the bank has climbed by the end of that width
	bool HasRiverPath() const { return riverPath.size() >= 2; }

	// --- the shore: how the water reacts to the ground under it (needs a terrain) ---
	float shoreWaves = 1.0f;      // 0 = waves identical everywhere (the old look); 1 = they grow in shallow water, then break and die at the shore
	float swash = 0.6f;           // how far the waterline (foam and the see-through edge) slides up and down the beach with each wave

	// --- surface ---
	float height = 4.0f;          // world Y of the water plane (kept in sync with the editor transform)
	float size = 180.0f;          // width: the plane's extent along X
	float sizeZ = 180.0f;         // length: the plane's extent along Z (a river is a long, thin rectangle)
	WaterShape shape = WaterShape::Rectangle;
	DirectX::XMFLOAT3 color = { 0.05f, 0.25f, 0.45f };   // DEEP water colour
	float alpha = 0.68f;                                 // opacity of deep water

	// --- depth: how the water changes between shore and open sea ---
	DirectX::XMFLOAT3 shallowColor = { 0.10f, 0.55f, 0.60f };
	float absorption = 0.35f;     // how fast light is absorbed with depth (per world unit)
	UnderwaterSettings underwater;   // how the world looks from UNDER this water (fog, light): automatic from the colours above, or custom
	float fadeDistance = 2.5f;    // depth at which the water becomes fully opaque (shore softness)
	float foamDistance = 0.9f;    // width of the shoreline foam band, in depth units
	float foamStrength = 0.9f;
	float foamScale = 1.0f;       // size of the bubbles: larger = finer bubbles (lesson 37)
	float foamTrail = 1.5f;       // seconds the foam of a breaking crest lingers behind it (0 = only while it is breaking)
	float foamIntensity = 1.0f;   // MASTER foam control (lesson 43): scales the shoreline foam and the crest foam together. 0 = no foam at all

	// --- waves ---
	float waveAmplitude = 0.35f;
	float waveSpeed = 1.0f;
	float waveFrequency = 0.6f;   // radians per world unit of the base wave (0.6 = ~10 unit wavelength)
	// Where the wind comes from: the scene's weather (one wind for everything: Weather.h), or a direction of this body's own.
	// Scenes saved before the scene wind existed keep the direction they had (they load as Custom).
	WindSource windSource = WindSource::Scene;
	float windDirection = 20.0f;  // degrees: the direction the waves travel toward (0 = +X, 90 = +Z). Used when windSource is Custom
	float directionalSpread = 0.7f;   // 0 = all waves run with the wind (a clean swell), 1 = waves from every which way
	float choppiness = 0.5f;      // 0 = rounded sine waves, 1 = sharp crests and wide troughs
	int waveCount = 6;            // 2..12 waves summed. Fewer = cheaper and simpler
	// The sea model (lesson 34). Classic = the original fixed waves (the five values below are ignored); Spectrum = a wind sea
	// built from a spectrum, plus a swell, plus wave groups. Scenes saved before this existed load as Spectrum with these defaults.
	WaterWaves::Style waveStyle = WaterWaves::Style::Spectrum;
	float waveRoughness = 0.5f;   // 0 = glassy (small waves die away fast), 1 = rough (many small waves)
	float swell = 0.3f;           // 0 = none; 1 = a swell nearly as tall as the biggest wind wave. A river has none
	float swellAngle = 35.0f;     // degrees from the wind: where the swell comes from
	float swellLength = 2.5f;     // the swell's wavelength as a multiple of the biggest wind wave's
	float waveGroups = 0.35f;     // 0 = the same waves everywhere; 1 = patches of calm and rough water that drift
	float shoreShelter = 0.8f;    // 0 = off; 1 = waves running AWAY from a nearby shore (the lee of an island) nearly vanish
	int waveSeed = 1;             // another seed = another sea with the same character
	float detailNormal = 0.5f;    // strength of the small ripples on top of the waves (0 = off, saves pixel work)
	float detailScale = 0.8f;     // ripples per world unit
	float detailDistance = 90.0f; // ripples fade out by this distance from the camera

	// --- lighting ---
	float reflectivity = 1.0f;    // 0 = no sky reflection, 1 = full Fresnel reflection
	ReflectionMode reflectionMode = ReflectionMode::PlanarHalf;
	float reflectionDistortion = 0.04f;   // how far the waves wobble the mirror image (fraction of the screen)
	float refraction = 0.04f;     // how far waves bend the view of the bottom (fraction of the screen). 0 = off, skips a screen copy
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

	static constexpr float kFlowSpeedMax = 10.0f;
	static constexpr float kRippleStretchMax = 8.0f;
	static constexpr float kRiverDepthMax = 30.0f, kRiverBankMax = 60.0f, kRiverBankHeightMax = 60.0f;
	static constexpr float kFoamScaleMin = 0.3f, kFoamScaleMax = 3.0f, kFoamTrailMax = 4.0f, kFoamIntensityMax = 2.0f;
	static constexpr float kSwashMax = 1.5f;
	static constexpr float kHeightMin = -100.0f, kHeightMax = 200.0f;
	static constexpr float kSizeMin = 2.0f, kSizeMax = 2000.0f;
	static constexpr float kAmplitudeMax = 2.0f, kSpeedMax = 5.0f;
	static constexpr float kFrequencyMin = 0.05f, kFrequencyMax = 2.0f;
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

	// A current carries everything downstream. Rivers always have one; a Custom body has one when its flow speed is above 0.
	bool HasCurrent() const { return bodyType == WaterBodyType::River || (bodyType == WaterBodyType::Custom && flowSpeed > 0.0f); }
	void Clamp();
	void ApplyWavePreset(WavePreset preset);   // only touches the wave shape values (not wind direction)
	void ApplyLook(WaterLook look);            // only touches colours, depth and optics (not waves, level or size)
	// Sets the type AND the starting values that suit it (waves, ripples, foam, look). Keeps level, size, shape and debug switches.
	void ApplyBodyType(WaterBodyType type);
	WaterWaves::Params WaveParams() const;

	// ---- The sections of the Inspector, as data (lesson 43) ----------------------------------------------------------
	// Which setting belongs to which section is written down ONCE, in VisitGroup. Resetting a section, marking the ones that
	// differ from their defaults, and applying a preset all walk that one list, so they cannot disagree about what a section is.
	enum class Group : int { Appearance, Waves, Reflection, Refraction, Lighting, Foam, Shoreline, Current, Performance, Underwater, Count };
	// Calls f(this body's field, other's field) for each field of the group.
	template <class Self, class F>
	static void VisitGroup(Group group, Self& a, const WaterSettings& b, F&& f);
	// A body of this kind with every setting at its default (the type's starting point; its place in the world is not part of that).
	static WaterSettings Defaults(WaterBodyType type) { WaterSettings s; s.ApplyBodyType(type); return s; }
	void ResetGroup(Group group, const WaterSettings& defaults);
	bool GroupModified(Group group, const WaterSettings& defaults) const;
	// Takes the LOOK of another body: appearance, waves, reflection, refraction, lighting, foam, shoreline, underwater. Not its
	// place (level, size, shape), its current or river, or its performance choices: a preset says how water looks, not where it is.
	void ApplyPreset(const WaterSettings& preset);
};
inline void WaterSettings::Clamp()
{
	auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
	height = std::clamp(height, kHeightMin, kHeightMax);
	size = std::clamp(size, kSizeMin, kSizeMax);
	sizeZ = std::clamp(sizeZ, kSizeMin, kSizeMax);
	shape = static_cast<WaterShape>(std::clamp(static_cast<int>(shape), 0, 1));
	bodyType = static_cast<WaterBodyType>(std::clamp(static_cast<int>(bodyType), 0, 4));   // Ocean .. Custom (this said 3: a Custom body became a Pond at its next edit)
	flowSpeed = std::clamp(flowSpeed, 0.0f, kFlowSpeedMax);
	shoreWaves = clamp01(shoreWaves);
	underwater.Clamp();
	swash = std::clamp(swash, 0.0f, kSwashMax);
	bankDrag = clamp01(bankDrag);
	channelProfile = clamp01(channelProfile);
	rippleStretch = std::clamp(rippleStretch, 1.0f, kRippleStretchMax);
	rapids = clamp01(rapids);
	riverDepth = std::clamp(riverDepth, 0.1f, kRiverDepthMax);
	riverBank = std::clamp(riverBank, 0.5f, kRiverBankMax);
	riverBankHeight = std::clamp(riverBankHeight, 0.0f, kRiverBankHeightMax);
	if (riverPath.size() > static_cast<size_t>(RiverPath::kMaxPoints)) riverPath.resize(RiverPath::kMaxPoints);
	for (RiverPoint& p : riverPath) {
		if (!std::isfinite(p.x)) p.x = 0.0f;
		if (!std::isfinite(p.z)) p.z = 0.0f;
		p.x = std::clamp(p.x, -kSizeMax, kSizeMax);
		p.z = std::clamp(p.z, -kSizeMax, kSizeMax);
		p.width = std::isfinite(p.width) ? std::clamp(p.width, RiverPath::kMinWidth, RiverPath::kMaxWidth) : 8.0f;
	}
	foamScale = std::clamp(foamScale, kFoamScaleMin, kFoamScaleMax);
	foamTrail = std::clamp(foamTrail, 0.0f, kFoamTrailMax);
	foamIntensity = std::clamp(foamIntensity, 0.0f, kFoamIntensityMax);
	flowAngle = std::fmod(flowAngle, 360.0f);   // an angle: wrap into (-180, 180] instead of clamping
	if (flowAngle > 180.0f) flowAngle -= 360.0f;
	if (flowAngle <= -180.0f) flowAngle += 360.0f;
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
	waveAmplitude = std::clamp(waveAmplitude, 0.0f, kAmplitudeMax);
	waveSpeed = std::clamp(waveSpeed, 0.0f, kSpeedMax);
	waveFrequency = std::clamp(waveFrequency, kFrequencyMin, kFrequencyMax);
	windDirection = std::fmod(windDirection, 360.0f);   // an angle: wrap instead of clamping
	if (windDirection < 0.0f) windDirection += 360.0f;
	directionalSpread = clamp01(directionalSpread);
	choppiness = clamp01(choppiness);
	waveCount = std::clamp(waveCount, kWaveCountMin, kWaveCountMax);
	waveStyle = static_cast<WaterWaves::Style>(std::clamp(static_cast<int>(waveStyle), 0, 1));
	waveRoughness = clamp01(waveRoughness);
	swell = clamp01(swell);
	swellAngle = std::clamp(swellAngle, -90.0f, 90.0f);
	swellLength = std::clamp(swellLength, kSwellLengthMin, kSwellLengthMax);
	waveGroups = std::clamp(waveGroups, 0.0f, kWaveGroupsMax);
	waveSeed = std::clamp(waveSeed, 0, 9999);
	shoreShelter = clamp01(shoreShelter);
	windSource = windSource == WindSource::Custom ? WindSource::Custom : WindSource::Scene;
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

inline void WaterSettings::ApplyWavePreset(WavePreset preset)
{
	switch (preset) {
	case WavePreset::Calm:   waveAmplitude = 0.12f; waveSpeed = 0.6f; waveFrequency = 0.45f; choppiness = 0.25f; waveRoughness = 0.3f; swell = 0.5f; waveGroups = 0.25f; break;
	case WavePreset::Choppy: waveAmplitude = 0.35f; waveSpeed = 1.0f; waveFrequency = 0.6f;  choppiness = 0.5f;  waveRoughness = 0.5f; swell = 0.3f; waveGroups = 0.35f; break;
	case WavePreset::Stormy: waveAmplitude = 0.6f;  waveSpeed = 1.8f; waveFrequency = 0.5f;  choppiness = 0.8f;  waveRoughness = 0.7f; swell = 0.6f; waveGroups = 0.5f;  break;
	}
}

inline void WaterSettings::ApplyLook(WaterLook look)
{
	switch (look) {
	case WaterLook::Tropical:
		color = { 0.02f, 0.28f, 0.45f }; shallowColor = { 0.15f, 0.75f, 0.70f }; alpha = 0.60f;
		absorption = 0.25f; fadeDistance = 3.0f; foamDistance = 0.9f; foamStrength = 0.9f;
		scatter = 0.8f; refraction = 0.05f; break;
	case WaterLook::Ocean:
		color = { 0.02f, 0.12f, 0.28f }; shallowColor = { 0.08f, 0.40f, 0.50f }; alpha = 0.80f;
		absorption = 0.45f; fadeDistance = 2.5f; foamDistance = 1.0f; foamStrength = 0.9f;
		scatter = 0.5f; refraction = 0.035f; break;
	case WaterLook::Lake:
		color = { 0.05f, 0.22f, 0.25f }; shallowColor = { 0.15f, 0.45f, 0.40f }; alpha = 0.75f;
		absorption = 0.60f; fadeDistance = 2.0f; foamDistance = 0.6f; foamStrength = 0.6f;
		scatter = 0.3f; refraction = 0.03f; break;
	case WaterLook::Swamp:
		color = { 0.06f, 0.14f, 0.07f }; shallowColor = { 0.20f, 0.30f, 0.12f }; alpha = 0.92f;
		absorption = 1.20f; fadeDistance = 1.2f; foamDistance = 0.5f; foamStrength = 0.4f;
		scatter = 0.1f; refraction = 0.01f; break;
	case WaterLook::Arctic:
		color = { 0.04f, 0.20f, 0.32f }; shallowColor = { 0.45f, 0.75f, 0.85f }; alpha = 0.70f;
		absorption = 0.30f; fadeDistance = 3.5f; foamDistance = 1.2f; foamStrength = 1.0f;
		scatter = 0.4f; refraction = 0.04f; break;
	}
}

inline void WaterSettings::ApplyBodyType(WaterBodyType type)
{
	bodyType = type;
	// Every body gets a complete, consistent starting point: look, waves, ripples and foam together. Level, size, shape and
	// the debug switches are the body's place in the world, not its kind: they stay.
	switch (type) {
	case WaterBodyType::Ocean:
		ApplyLook(WaterLook::Ocean);
		waveAmplitude = 0.45f; waveSpeed = 1.0f; waveFrequency = 0.5f; directionalSpread = 0.7f; choppiness = 0.55f; waveCount = 7;
		waveRoughness = 0.5f; swell = 0.5f; swellAngle = 35.0f; swellLength = 2.5f; waveGroups = 0.4f;
		detailNormal = 0.5f; detailScale = 0.8f; crestFoam = 0.5f; crestFoamThreshold = 0.25f;
		shoreWaves = 1.0f; swash = 0.6f;   // swells shoal, break and wash up the beach
		foamScale = 1.0f; foamTrail = 1.8f;   // big white caps that linger behind the crest
		break;
	case WaterBodyType::Lake:
		// Short wind chop that hardly ever breaks: wavelengths a third of the ocean's, a fraction of its height.
		ApplyLook(WaterLook::Lake);
		waveAmplitude = 0.12f; waveSpeed = 0.7f; waveFrequency = 0.9f; directionalSpread = 1.0f; choppiness = 0.2f; waveCount = 5;
		waveRoughness = 0.55f; swell = 0.1f; swellAngle = 25.0f; swellLength = 2.0f; waveGroups = 0.3f;
		detailNormal = 0.6f; detailScale = 1.2f; crestFoam = 0.1f; crestFoamThreshold = 0.4f;
		shoreWaves = 0.8f; swash = 0.3f;
		foamScale = 1.3f; foamTrail = 0.8f;   // finer bubbles, short-lived
		break;
	case WaterBodyType::River:
		// A current, not a swell: low waves that all travel one way, fast ripples carried downstream, no white caps.
		ApplyLook(WaterLook::Lake);
		foamDistance = 0.5f; absorption = 0.45f;
		waveAmplitude = 0.05f; waveSpeed = 1.0f; waveFrequency = 1.2f; directionalSpread = 0.1f; choppiness = 0.1f; waveCount = 4;
		waveRoughness = 0.5f; swell = 0.0f; waveGroups = 0.2f;   // a current has one direction: no cross swell
		detailNormal = 1.2f; detailScale = 1.6f; crestFoam = 0.0f; crestFoamThreshold = 0.5f;
		flowSpeed = 2.5f; flowAngle = 0.0f;
		shoreWaves = 0.5f; swash = 0.1f;   // a current laps at its banks; it does not surf
		bankDrag = 0.6f; channelProfile = 0.5f; rippleStretch = 3.0f; rapids = 0.5f;   // fastest in the deep middle, streaming along the flow
		foamScale = 1.1f; foamTrail = 0.6f;
		break;
	case WaterBodyType::Custom:
		// A blank slate that shows every setting. It starts as a lake with the full sea model: a sensible middle to bend either way.
		ApplyLook(WaterLook::Lake);
		waveAmplitude = 0.2f; waveSpeed = 0.8f; waveFrequency = 0.7f; directionalSpread = 0.8f; choppiness = 0.3f; waveCount = 6;
		waveRoughness = 0.5f; swell = 0.2f; swellAngle = 30.0f; swellLength = 2.2f; waveGroups = 0.3f;
		detailNormal = 0.6f; detailScale = 1.2f; crestFoam = 0.2f; crestFoamThreshold = 0.35f;
		shoreWaves = 0.8f; swash = 0.3f;
		flowSpeed = 0.0f;   // no current until the user asks for one
		foamScale = 1.0f; foamTrail = 1.2f;
		break;
	case WaterBodyType::Pond:
		ApplyLook(WaterLook::Lake);
		alpha = 0.85f; foamStrength = 0.3f;
		waveAmplitude = 0.03f; waveSpeed = 0.5f; waveFrequency = 1.4f; directionalSpread = 1.0f; choppiness = 0.05f; waveCount = 3;
		waveRoughness = 0.4f; swell = 0.0f; waveGroups = 0.2f;
		detailNormal = 0.7f; detailScale = 1.8f; crestFoam = 0.0f; crestFoamThreshold = 0.5f;
		shoreWaves = 0.5f; swash = 0.15f;
		foamScale = 1.4f; foamTrail = 0.5f;
		break;
	}
}

inline WaterWaves::Params WaterSettings::WaveParams() const
{
	WaterWaves::Params p;
	p.amplitude = waveAmplitude;
	p.speed = waveSpeed;
	p.frequency = waveFrequency;
	p.windDegrees = windDirection;
	p.spread = directionalSpread;
	p.choppiness = choppiness;
	p.count = waveCount;
	p.style = waveStyle;
	p.roughness = waveRoughness;
	p.swell = swell;
	p.swellAngle = swellAngle;
	p.swellLength = swellLength;
	p.groups = waveGroups;
	p.seed = static_cast<std::uint32_t>(waveSeed);
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
		WATER_FIELD(waveAmplitude); WATER_FIELD(waveSpeed); WATER_FIELD(waveFrequency); WATER_FIELD(windSource); WATER_FIELD(windDirection);
		WATER_FIELD(directionalSpread); WATER_FIELD(choppiness); WATER_FIELD(waveCount); WATER_FIELD(waveStyle); WATER_FIELD(waveRoughness);
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
	case Group::Current:
		WATER_FIELD(flowSpeed); WATER_FIELD(flowAngle); WATER_FIELD(bankDrag); WATER_FIELD(channelProfile); WATER_FIELD(rippleStretch); WATER_FIELD(rapids);
		break;   // (not the river's path or its carve: that is the river's shape, drawn by hand)
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
		ResetGroup(g, preset);   // "reset to that body's values" is exactly "take its values"
}
