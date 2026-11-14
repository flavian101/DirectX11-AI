#pragma once
// How the world looks from under a water surface: the fog's colour and thickness at a given depth. No Direct3D: unit-tested.
//
// By default the look is derived from the water body itself (its two colours and its absorption), so a murky pond and a clear
// ocean already differ. Turn `custom` on to take over: a visibility distance, how deep the shallow colour lasts, how fast the
// light dies with depth. Derived() gives the custom numbers that reproduce the automatic look, so customising starts from
// what you were already seeing instead of from a blank.
#include <algorithm>
#include <array>
#include <cmath>

struct UnderwaterSettings {
    bool custom = false;                                       // false: derive everything from the water body
    // Only used when `custom` is on:
    std::array<float, 3> shallowColor = { 0.10f, 0.55f, 0.60f };   // sRGB: the water just under the surface
    std::array<float, 3> deepColor = { 0.05f, 0.25f, 0.45f };      // sRGB: the water far down
    float visibility = 14.0f;      // m: how far you can see (the fog hides 95% of what is this far away)
    float colorDepth = 19.0f;      // m: the depth over which the shallow colour turns into the deep one
    float lightDepth = 29.0f;      // m: the depth at which the light has dimmed to about a third
    // Used in both modes:
    float brightness = 1.0f;       // multiplies how much light reaches under the surface
    float nightBrightness = 0.3f;  // the light level with no sun or moon up (the least it can get)
    float sunGlow = 0.25f;         // 0..1: looking toward the sun (up at the surface) glows
    float startDistance = 0.0f;    // m: nothing nearer than this is fogged (a clear bubble around the camera)
    float surfaceMirror = 0.9f;    // how bright the surface looks from below outside the sky "window" (the mirror of the water)

    void Clamp()
    {
        for (float& c : shallowColor) c = std::clamp(c, 0.0f, 1.0f);
        for (float& c : deepColor) c = std::clamp(c, 0.0f, 1.0f);
        visibility = std::clamp(visibility, 1.0f, 500.0f);
        colorDepth = std::clamp(colorDepth, 0.5f, 500.0f);
        lightDepth = std::clamp(lightDepth, 1.0f, 1000.0f);
        brightness = std::clamp(brightness, 0.0f, 4.0f);
        nightBrightness = std::clamp(nightBrightness, 0.0f, 1.0f);
        sunGlow = std::clamp(sunGlow, 0.0f, 1.0f);
        startDistance = std::clamp(startDistance, 0.0f, 100.0f);
        surfaceMirror = std::clamp(surfaceMirror, 0.0f, 2.0f);
    }
};

// What the look is derived from: the water body's own settings (colours are sRGB, as the user picked them).
struct UnderwaterBody {
    std::array<float, 3> shallowColor = { 0.10f, 0.55f, 0.60f };
    std::array<float, 3> deepColor = { 0.05f, 0.25f, 0.45f };
    float absorption = 0.35f;
};

struct UnderwaterLook {
    float density = 0.0f;           // fog density (FogAt in common.hlsli)
    float startDistance = 0.0f;
    float color[3] = { 0, 0, 0 };   // fog colour, LINEAR, already dimmed by depth and light
    float sunGlow = 0.0f;
    float surfaceMirror = 0.9f;
};

namespace UnderwaterMath {
inline float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// The automatic numbers, as custom ones. (Visibility is 5 / absorption because density = 0.6 * absorption = 3 / visibility.)
inline UnderwaterSettings Derived(const UnderwaterBody& body, const UnderwaterSettings& keep = UnderwaterSettings())
{
    UnderwaterSettings s = keep;
    const float absorption = std::max(body.absorption, 0.02f);
    s.custom = true;
    s.shallowColor = body.shallowColor;
    s.deepColor = body.deepColor;
    s.visibility = 5.0f / absorption;
    s.colorDepth = 1.0f / (0.15f * absorption);
    s.lightDepth = 1.0f / (0.10f * absorption);
    s.Clamp();
    return s;
}

// `depth`: how far under the surface the camera is (m). `lightStrength`: how much sun and moon light there is overhead
// (0 at night; about 2.5 for a full sun): the scene works it out from its lights.
inline UnderwaterLook Compute(const UnderwaterSettings& settings, const UnderwaterBody& body, float depth, float lightStrength)
{
    depth = std::max(depth, 0.0f);
    const float absorption = std::max(body.absorption, 0.02f);

    float shallow[3], deep[3], density, blend, fade;
    if (settings.custom) {
        for (int i = 0; i < 3; ++i) { shallow[i] = settings.shallowColor[i]; deep[i] = settings.deepColor[i]; }
        density = 3.0f / std::max(settings.visibility, 1.0f);
        blend = std::min(depth / std::max(settings.colorDepth, 0.5f), 1.0f);
        fade = std::exp(-depth / std::max(settings.lightDepth, 1.0f));
    } else {
        for (int i = 0; i < 3; ++i) { shallow[i] = body.shallowColor[i]; deep[i] = body.deepColor[i]; }
        density = absorption * 0.6f;
        blend = std::min(depth * absorption * 0.15f, 1.0f);
        fade = std::exp(-depth * absorption * 0.1f);
    }
    // The light that reaches the water: a floor at night, rising with the strength of the sun (capped: past a point it is just bright).
    const float floorLevel = settings.nightBrightness;
    const float level = std::min(floorLevel + 0.5f * std::max(lightStrength, 0.0f), 1.6f) * settings.brightness;

    UnderwaterLook look;
    look.density = density;
    look.startDistance = settings.startDistance;
    for (int i = 0; i < 3; ++i) {
        const float srgb = shallow[i] + (deep[i] - shallow[i]) * blend;   // blend the colours the way the user sees them,
        look.color[i] = SrgbToLinear(srgb) * level * fade;                 // then go to linear light and dim it
    }
    look.sunGlow = settings.sunGlow;
    look.surfaceMirror = settings.surfaceMirror;
    return look;
}
}  // namespace UnderwaterMath
