#pragma once
// The weather of the whole scene (lesson 35). Wind is not a property of one water body: it moves the sea, the grass, the
// smoke and the clouds. So it lives here, once, and everything that is blown about reads it. (Fog and rain will join it.)
// Not to be confused with EnvironmentSettings, which is the sky's LIGHTING.
#include <algorithm>
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

struct WeatherSettings {
	WindSettings wind;
	void Clamp() { wind.Clamp(); }
};

// How much taller than its own setting a water body's waves are under this wind. Only the height follows the wind's
// strength; wavelength, speed and the rest stay as the body's settings say.
inline float WindWaveScale(const WindSettings& wind) { return std::clamp(wind.strength, 0.0f, WindSettings::kStrengthMax); }
