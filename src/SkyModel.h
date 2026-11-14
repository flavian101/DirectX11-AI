#pragma once
// The colours of a procedural sky, from how high the sun is. No Direct3D: unit-tested.
//
// Evaluate() turns the direction of the sun (and the moon) into the handful of numbers the sky shader needs: the colour overhead,
// at the horizon and below it, the warm glow of a sunset, the sun's and moon's discs, and how many stars to show. The layout of
// Params is the layout of `cbuffer SkyParams : register(b6)` in shaders/sky_procedural.hlsli, field for field: change one, change
// both. All colours are LINEAR light (HDR: the sun's disc is far brighter than 1).
#include "LightAngles.h"
#include "TimeOfDay.h"
#include <algorithm>
#include <cmath>

namespace SkyModel {

using LightAngles::Vec3;

struct Look {   // the user's tuning; 1 = the default look
    float sunSize = 1.0f, moonSize = 1.0f, starBrightness = 1.0f, sunsetStrength = 1.0f, skyBrightness = 1.0f;
};

struct Params {
    float zenith[4];      // rgb = the sky straight overhead
    float horizon[4];     // rgb = the sky at the horizon
    float ground[4];      // rgb = what is below the horizon
    float sunDir[4];      // xyz = unit direction TOWARD the sun, w = cos(the sun's angular radius)
    float sunColor[4];    // rgb = the sun disc's radiance, w = 1 when the disc is drawn (the sun is up)
    float glow[4];        // rgb = colour of the haze around the sun, w = how strong it is
    float moonDir[4];     // xyz = unit direction toward the moon, w = cos(the moon's angular radius)
    float moonColor[4];   // rgb = the moon disc's radiance, w = 1 when the disc is drawn
    float sunset[4];      // rgb = the warm colour of a sunset horizon, w = how much of it there is (0..1)
    float night[4];       // x = star brightness (already scaled by how dark it is), y = how dark it is (0..1)
};
static_assert(sizeof(Params) == 160 && sizeof(Params) % 16 == 0, "Params is a constant buffer: a multiple of 16 bytes");

namespace detail {
struct Key { float s; float c[3]; };   // at sin(sun elevation) = s, the colour is c

template <size_t N>
inline void Sample(const Key (&keys)[N], float s, float out[3])
{
    if (s <= keys[0].s) { for (int i = 0; i < 3; ++i) out[i] = keys[0].c[i]; return; }
    for (size_t k = 1; k < N; ++k) {
        if (s <= keys[k].s) {
            const float t = (s - keys[k - 1].s) / (keys[k].s - keys[k - 1].s);
            for (int i = 0; i < 3; ++i) out[i] = keys[k - 1].c[i] + (keys[k].c[i] - keys[k - 1].c[i]) * t;
            return;
        }
    }
    for (int i = 0; i < 3; ++i) out[i] = keys[N - 1].c[i];
}
}  // namespace detail

inline Params Evaluate(const Vec3& toSun, bool moonUp, const Vec3& toMoon, const Look& look)
{
    using detail::Key;
    using TimeOfDay::SmoothStep;
    static const Key kZenith[] = {
        { -0.25f, { 0.003f, 0.005f, 0.018f } }, { -0.08f, { 0.012f, 0.020f, 0.070f } }, { 0.00f, { 0.040f, 0.070f, 0.200f } },
        {  0.08f, { 0.070f, 0.140f, 0.380f } }, {  0.30f, { 0.100f, 0.240f, 0.620f } }, { 1.00f, { 0.120f, 0.300f, 0.750f } } };
    static const Key kHorizon[] = {
        { -0.25f, { 0.008f, 0.011f, 0.025f } }, { -0.08f, { 0.050f, 0.040f, 0.090f } }, { 0.00f, { 0.550f, 0.300f, 0.220f } },
        {  0.08f, { 0.750f, 0.550f, 0.450f } }, {  0.30f, { 0.550f, 0.680f, 0.850f } }, { 1.00f, { 0.600f, 0.720f, 0.900f } } };

    const float s = std::clamp(toSun.y, -1.0f, 1.0f);
    const float brightness = look.skyBrightness;
    Params p{};
    float c[3];
    detail::Sample(kZenith, s, c);
    for (int i = 0; i < 3; ++i) p.zenith[i] = c[i] * brightness;
    detail::Sample(kHorizon, s, c);
    for (int i = 0; i < 3; ++i) { p.horizon[i] = c[i] * brightness; p.ground[i] = c[i] * brightness * 0.4f; }

    // The warm sunset glow: strongest with the sun on the horizon, gone once it is well up or far down.
    const float sunsetAmount = SmoothStep(-0.12f, 0.0f, s) * (1.0f - SmoothStep(0.05f, 0.30f, s));
    p.sunset[0] = 1.0f; p.sunset[1] = 0.38f; p.sunset[2] = 0.12f;
    p.sunset[3] = std::clamp(sunsetAmount * look.sunsetStrength, 0.0f, 1.0f);

    // The sun: a disc, brighter and whiter the higher it is, redder and dimmer near the horizon (more air in the way).
    const float sunRadiusDegrees = 1.2f * look.sunSize;
    p.sunDir[0] = toSun.x; p.sunDir[1] = toSun.y; p.sunDir[2] = toSun.z;
    p.sunDir[3] = std::cos(LightAngles::Radians(sunRadiusDegrees));
    const float up = SmoothStep(0.0f, 0.30f, s);
    const float sunPower = 30.0f * (0.14f + 0.86f * up);   // a low sun is far dimmer: its light crosses a lot of air
    p.sunColor[0] = sunPower * 1.00f;
    p.sunColor[1] = sunPower * (0.38f + 0.57f * up);
    p.sunColor[2] = sunPower * (0.14f + 0.76f * up);
    p.sunColor[3] = s > -0.03f ? 1.0f : 0.0f;
    // The haze around it.
    p.glow[0] = 1.0f; p.glow[1] = 0.62f + 0.30f * up; p.glow[2] = 0.35f + 0.45f * up;
    p.glow[3] = (0.25f + 0.5f * sunsetAmount) * TimeOfDay::SunFactor(s);

    // The moon.
    const float moonRadiusDegrees = 1.2f * look.moonSize;
    p.moonDir[0] = toMoon.x; p.moonDir[1] = toMoon.y; p.moonDir[2] = toMoon.z;
    p.moonDir[3] = std::cos(LightAngles::Radians(moonRadiusDegrees));
    p.moonColor[0] = 3.0f; p.moonColor[1] = 3.1f; p.moonColor[2] = 3.4f;
    p.moonColor[3] = (moonUp && toMoon.y > -0.03f) ? 1.0f : 0.0f;

    // Stars come out as the sky darkens.
    const float night = TimeOfDay::NightFactor(s);
    p.night[0] = look.starBrightness * night;
    p.night[1] = night;
    return p;
}

// True when the two skies differ enough that the sky's LIGHTING should be baked again. The background is drawn from the exact
// numbers every frame, so this only has to catch changes the light on the scene would show: a direction that moved by about a degree,
// or a colour that changed by about a hundredth. A drifting sun must not cost a bake every frame.
inline bool Differs(const Params& a, const Params& b)
{
    const float* fa = &a.zenith[0];
    const float* fb = &b.zenith[0];
    const size_t count = sizeof(Params) / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
        const bool direction = (i >= 12 && i < 15) || (i >= 24 && i < 27);   // sunDir.xyz, moonDir.xyz: 0.02 is about a degree
        const bool sunBrightness = (i >= 16 && i < 19);                        // the disc is ~30x a colour's range; it is not baked anyway
        const float allowance = direction ? 0.02f : sunBrightness ? 0.5f : 0.01f;
        if (std::abs(fa[i] - fb[i]) > allowance) return true;
    }
    return false;
}

}  // namespace SkyModel
