#pragma once
// Time of day: where the sun and moon are at a given hour, and how strong and what colour their light is. No Direct3D: unit-tested.
//
// The sun travels a great circle in the sky: it rises on one side of the compass, climbs to its highest point (maxElevation) in
// the direction noonAzimuth at 12:00, and sets on the opposite side at 18:00. Day and night are always 12 hours (no seasons).
// The moon is exactly opposite the sun. Everything here works in "sin of the elevation" (the height of the object's direction
// vector, -1..1): 0 is the horizon, so no angle conversions are needed to decide how much light there is.
#include "LightAngles.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

// Which time-of-day light a directional LightObject is. Saved by name (see SceneJson.h): only ever ADD to the end.
enum class LightRole : int { None = 0, Sun = 1, Moon = 2 };

struct TimeOfDaySettings {
    bool enabled = false;              // drive the lights that have a Sun or Moon role from the clock
    float hour = 10.0f;                // the saved time, 0..24: where the day starts
    bool animate = false;              // the clock runs by itself while the scene runs
    float dayLengthMinutes = 5.0f;     // real minutes for a whole 24 hours
    float noonAzimuth = 200.0f;        // compass direction of the sun at noon (the wind's convention: 0 = +X, 90 = +Z)
    float maxElevation = 60.0f;        // how high the noon sun climbs, in degrees (90 = straight overhead)
    // How the procedural sky looks (a Skybox in Procedural mode); 1 = the default look.
    float sunSize = 1.0f;
    float moonSize = 1.0f;
    float starBrightness = 1.0f;
    float sunsetStrength = 1.0f;
    float skyBrightness = 1.0f;
    // A procedural sky re-bakes the light it casts on the scene as it changes: at most once per this many seconds. Longer = cheaper.
    float skyUpdateSeconds = 0.5f;

    void Clamp()
    {
        hour = std::isfinite(hour) ? std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f) : 12.0f;
        dayLengthMinutes = std::clamp(dayLengthMinutes, 0.1f, 240.0f);
        noonAzimuth = std::fmod(std::fmod(noonAzimuth, 360.0f) + 360.0f, 360.0f);
        maxElevation = std::clamp(maxElevation, 10.0f, 90.0f);
        sunSize = std::clamp(sunSize, 0.2f, 6.0f);
        moonSize = std::clamp(moonSize, 0.2f, 6.0f);
        starBrightness = std::clamp(starBrightness, 0.0f, 4.0f);
        sunsetStrength = std::clamp(sunsetStrength, 0.0f, 3.0f);
        skyBrightness = std::clamp(skyBrightness, 0.1f, 4.0f);
        skyUpdateSeconds = std::clamp(skyUpdateSeconds, 0.1f, 5.0f);
    }
};

namespace TimeOfDay {

using LightAngles::Vec3;

inline float WrapHour(float hour)
{
    if (!std::isfinite(hour)) return 12.0f;
    hour = std::fmod(hour, 24.0f);
    return hour < 0.0f ? hour + 24.0f : hour;
}

// The clock after `seconds` of real time. A day lasts `dayLengthMinutes` real minutes.
inline float AdvanceHour(float hour, float seconds, float dayLengthMinutes)
{
    const float minutes = std::max(dayLengthMinutes, 0.1f);
    return WrapHour(hour + seconds * 24.0f / (minutes * 60.0f));
}

// The unit direction TOWARD the sun at `hour`.
inline Vec3 SunToLight(const TimeOfDaySettings& settings, float hour)
{
    const Vec3 noon = LightAngles::ToLight(settings.noonAzimuth, settings.maxElevation);
    const float eastAzimuth = LightAngles::Radians(settings.noonAzimuth + 90.0f);
    const Vec3 east = { std::cos(eastAzimuth), 0.0f, std::sin(eastAzimuth) };
    const float theta = (WrapHour(hour) - 12.0f) / 24.0f * 2.0f * LightAngles::kPi;   // 0 at noon, -pi/2 sunrise, +pi/2 sunset
    const float c = std::cos(theta), s = std::sin(theta);
    return { noon.x * c + east.x * s, noon.y * c + east.y * s, noon.z * c + east.z * s };
}

// The moon is always exactly opposite the sun.
inline Vec3 MoonToLight(const TimeOfDaySettings& settings, float hour)
{
    const Vec3 sun = SunToLight(settings, hour);
    return { -sun.x, -sun.y, -sun.z };
}

inline float SmoothStep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// How much of the sun's light there is, given sin(sun elevation): none a little below the horizon, all of it once it is up.
inline float SunFactor(float sunSin) { return SmoothStep(-0.07f, 0.10f, sunSin); }

// How dark the night is: 0 in daylight, 1 when the sun is well below the horizon.
inline float NightFactor(float sunSin) { return 1.0f - SmoothStep(-0.25f, 0.02f, sunSin); }

// The moon's light: it has to be up, and it only shines once the sun has gone (a moon in daylight adds nothing).
inline float MoonFactor(float moonSin, float sunSin)
{
    return SmoothStep(-0.05f, 0.15f, moonSin) * (1.0f - SmoothStep(-0.12f, 0.05f, sunSin));
}

struct Rgb { float r = 1.0f, g = 1.0f, b = 1.0f; };

// The sun's colour: the picked (noon) colour high up, shifting to a warm orange as it nears the horizon.
inline Rgb SunColor(const Rgb& noon, float sunSin)
{
    const Rgb warm = { 1.0f, 0.52f, 0.26f };
    const float t = SmoothStep(0.02f, 0.45f, sunSin);   // 0 on the horizon, 1 well up
    return { warm.r + (noon.r - warm.r) * t, warm.g + (noon.g - warm.g) * t, warm.b + (noon.b - warm.b) * t };
}

inline std::string FormatHour(float hour)
{
    const float wrapped = WrapHour(hour);
    int minutes = static_cast<int>(std::lround(wrapped * 60.0f)) % (24 * 60);
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

}  // namespace TimeOfDay
