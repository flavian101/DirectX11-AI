#pragma once
// The weather of the whole scene (lesson 35). Wind is not a property of one water body: it moves the sea, the grass, the
// smoke and the clouds. So it lives here, once, and everything that is blown about reads it. (Fog and rain will join it.)
// Not to be confused with EnvironmentSettings, which is the sky's LIGHTING.
#include <algorithm>
#include <array>
#include <cmath>

// Where a water body gets its wind: the scene's weather, or a direction of its own. Saved by name: only ever ADD to the end.
enum class WindSource : int { Scene = 0, Custom = 1 };

struct WindSettings {
	float direction = 20.0f;   // degrees: the way the wind blows TOWARD (0 = +X, 90 = +Z), the same convention the waves use
	float strength = 1.0f;     // 0 = dead calm, 1 = the sea the water body was set up for, 2 = a much rougher one

	static constexpr float kStrengthMax = 2.0f;
	void Clamp()
	{
		direction = std::fmod(direction, 360.0f);   // an angle: wrap instead of clamping
		if (direction < 0.0f) direction += 360.0f;
		strength = std::clamp(strength, 0.0f, kStrengthMax);
	}
};

// Fog: the air between the camera and what it looks at. The farther away something is, the more it takes the fog's colour, so
// the far hills melt into the sky instead of stopping at a hard edge. (Under water the water body takes over: see
// Scene::FillEnvironmentFrameData.) Saved with the scene; a field missing from an old file keeps its default.
struct FogSettings {
	bool enabled = false;
	std::array<float, 3> color = { 0.66f, 0.74f, 0.86f };   // what the picker shows (sRGB)
	float brightness = 1.0f;       // multiplies the colour: the scene is HDR, so fog in strong sun may need to be brighter than 1
	float visibility = 400.0f;     // distance at which the fog hides 95% of what is behind it (density = 3 / visibility)
	float startDistance = 0.0f;    // nothing nearer than this is fogged
	float heightFalloff = 0.0f;    // 0 = the same thickness at every height; larger = thins out faster going up (per world unit)
	float baseHeight = 0.0f;       // the height where the fog has its full thickness
	float sunGlow = 0.4f;          // 0..1: fog toward the sun glows with the sun's colour
	float skyBlend = 0.6f;         // 0..1: how much the horizon of the sky takes the fog's colour (hides the line where the ground ends)
	float maxOpacity = 1.0f;       // fog never hides more than this much of anything

	static constexpr float kVisibilityMin = 5.0f, kVisibilityMax = 20000.0f;
	float Density() const { return 3.0f / std::clamp(visibility, kVisibilityMin, kVisibilityMax); }
	void Clamp()
	{
		for (float& channel : color) channel = std::clamp(channel, 0.0f, 1.0f);
		brightness = std::clamp(brightness, 0.0f, 10.0f);
		visibility = std::clamp(visibility, kVisibilityMin, kVisibilityMax);
		startDistance = std::clamp(startDistance, 0.0f, 10000.0f);
		heightFalloff = std::clamp(heightFalloff, 0.0f, 2.0f);
		baseHeight = std::clamp(baseHeight, -1000.0f, 1000.0f);
		sunGlow = std::clamp(sunGlow, 0.0f, 1.0f);
		skyBlend = std::clamp(skyBlend, 0.0f, 1.0f);
		maxOpacity = std::clamp(maxOpacity, 0.0f, 1.0f);
	}
};

struct WeatherSettings {
	WindSettings wind;
	FogSettings fog;
	void Clamp() { wind.Clamp(); fog.Clamp(); }
};

// How much taller than its own setting a water body's waves are under this wind. Only the height follows the wind's
// strength; wavelength, speed and the rest stay as the body's settings say.
inline float WindWaveScale(const WindSettings& wind) { return std::clamp(wind.strength, 0.0f, WindSettings::kStrengthMax); }
