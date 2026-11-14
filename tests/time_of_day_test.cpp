// Time of day: the sun's path, the light factors, the sky colours. Pure maths: no GPU.
#include "SkyModel.h"
#include "TimeOfDay.h"
#include <cstdio>
#include <cstdlib>

using namespace LightAngles;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
static float Elevation(const Vec3& v) { return Degrees(std::asin(std::clamp(v.y, -1.0f, 1.0f))); }
static float Azimuth(const Vec3& v) { float a = Degrees(std::atan2(v.z, v.x)); return a < 0 ? a + 360.0f : a; }
static bool AzNear(float a, float b) { float d = std::fmod(std::abs(a - b), 360.0f); return d < 0.05f || d > 359.95f; }

int main()
{
    TimeOfDaySettings s;
    s.noonAzimuth = 200.0f; s.maxElevation = 60.0f;

    // The sun's path: noon is the highest point, in the noon direction; sunrise and sunset are on the horizon, 90 degrees either side.
    const Vec3 noon = TimeOfDay::SunToLight(s, 12.0f);
    CHECK(Near(Elevation(noon), 60.0f, 0.01f) && AzNear(Azimuth(noon), 200.0f));
    const Vec3 rise = TimeOfDay::SunToLight(s, 6.0f), set = TimeOfDay::SunToLight(s, 18.0f);
    CHECK(Near(Elevation(rise), 0.0f, 0.01f) && Near(Elevation(set), 0.0f, 0.01f));
    CHECK(AzNear(Azimuth(rise), 110.0f) && AzNear(Azimuth(set), 290.0f));
    CHECK(Near(Elevation(TimeOfDay::SunToLight(s, 0.0f)), -60.0f, 0.01f));   // midnight: as far below as noon is above
    CHECK(Elevation(TimeOfDay::SunToLight(s, 9.0f)) > 0.0f && Elevation(TimeOfDay::SunToLight(s, 21.0f)) < 0.0f);
    // The sun climbs all morning and sinks all afternoon.
    float previous = -100.0f;
    for (float h = 6.0f; h <= 12.0f; h += 0.5f) { const float e = Elevation(TimeOfDay::SunToLight(s, h)); CHECK(e >= previous); previous = e; }
    for (float h = 12.0f; h <= 18.0f; h += 0.5f) { const float e = Elevation(TimeOfDay::SunToLight(s, h)); CHECK(e <= previous); previous = e; }
    // Always a unit vector; the moon is exactly opposite.
    for (float h = 0.0f; h < 24.0f; h += 1.7f) {
        const Vec3 a = TimeOfDay::SunToLight(s, h), m = TimeOfDay::MoonToLight(s, h);
        CHECK(Near(a.x * a.x + a.y * a.y + a.z * a.z, 1.0f) && Near(a.x, -m.x) && Near(a.y, -m.y) && Near(a.z, -m.z));
    }
    // A low maximum elevation (a winter sun) still rises and sets at the horizon.
    s.maxElevation = 25.0f;
    CHECK(Near(Elevation(TimeOfDay::SunToLight(s, 6.0f)), 0.0f, 0.01f) && Near(Elevation(TimeOfDay::SunToLight(s, 12.0f)), 25.0f, 0.01f));
    s.maxElevation = 60.0f;

    // The clock.
    CHECK(Near(TimeOfDay::WrapHour(25.5f), 1.5f) && Near(TimeOfDay::WrapHour(-1.0f), 23.0f) && Near(TimeOfDay::WrapHour(24.0f), 0.0f));
    CHECK(Near(TimeOfDay::AdvanceHour(0.0f, 150.0f, 5.0f), 12.0f));    // half of a 5-minute day is 12 hours
    CHECK(Near(TimeOfDay::AdvanceHour(23.0f, 25.0f, 5.0f), 1.0f));      // wraps past midnight: 25 s of a 300 s day = 2 hours
    CHECK(Near(TimeOfDay::AdvanceHour(10.0f, 0.0f, 5.0f), 10.0f));
    CHECK(TimeOfDay::FormatHour(14.5f) == "14:30" && TimeOfDay::FormatHour(0.0f) == "00:00" && TimeOfDay::FormatHour(23.999f) == "00:00");

    // Light factors: full sun up high, none well below the horizon; the moon only after the sun is gone.
    CHECK(Near(TimeOfDay::SunFactor(0.8f), 1.0f) && Near(TimeOfDay::SunFactor(-0.3f), 0.0f));
    CHECK(TimeOfDay::SunFactor(0.0f) > 0.2f && TimeOfDay::SunFactor(0.0f) < 0.9f);   // a soft sunrise, not a switch
    CHECK(TimeOfDay::NightFactor(0.8f) < 0.001f && TimeOfDay::NightFactor(-0.5f) > 0.999f);
    CHECK(TimeOfDay::MoonFactor(0.7f, -0.7f) > 0.99f);                  // night, moon high
    CHECK(TimeOfDay::MoonFactor(0.7f, 0.7f) < 0.001f);                  // a moon in full daylight adds nothing
    CHECK(TimeOfDay::MoonFactor(-0.5f, -0.5f) < 0.001f);                // a moon below the horizon adds nothing
    float last = 0.0f;
    for (float v = -0.3f; v <= 0.5f; v += 0.05f) { const float f = TimeOfDay::SunFactor(v); CHECK(f >= last - 1e-6f); last = f; }

    // The sun's colour: warm at the horizon, the picked colour high up.
    const TimeOfDay::Rgb white = { 1.0f, 1.0f, 1.0f };
    const TimeOfDay::Rgb low = TimeOfDay::SunColor(white, 0.0f), high = TimeOfDay::SunColor(white, 0.8f);
    CHECK(low.r > low.g && low.g > low.b);                              // red > green > blue: orange
    CHECK(Near(high.r, 1.0f) && Near(high.g, 1.0f) && Near(high.b, 1.0f));

    // The sky: a blue day, a dark night, a warm horizon at sunrise.
    SkyModel::Look look;
    const Vec3 sunNoon = { 0.0f, 1.0f, 0.0f }, moonNoon = { 0.0f, -1.0f, 0.0f };
    SkyModel::Params day = SkyModel::Evaluate(sunNoon, false, moonNoon, look);
    CHECK(day.zenith[2] > day.zenith[0] * 3.0f);                        // blue
    CHECK(day.sunColor[3] == 1.0f && day.moonColor[3] == 0.0f);
    CHECK(day.night[1] < 0.001f && day.night[0] < 0.001f);               // no stars at noon
    const Vec3 sunDown = { 0.0f, -1.0f, 0.0f }, moonUp = { 0.0f, 1.0f, 0.0f };
    SkyModel::Params night = SkyModel::Evaluate(sunDown, true, moonUp, look);
    CHECK(night.zenith[0] < 0.01f && night.zenith[1] < 0.01f && night.zenith[2] < 0.03f);   // dark
    CHECK(night.night[1] > 0.999f && night.night[0] > 0.999f);          // full stars
    CHECK(night.sunColor[3] == 0.0f && night.moonColor[3] == 1.0f);
    const Vec3 sunRise = { 1.0f, 0.0f, 0.0f };
    SkyModel::Params dawn = SkyModel::Evaluate(sunRise, true, { -1.0f, 0.0f, 0.0f }, look);
    CHECK(dawn.horizon[0] > dawn.horizon[2]);                           // warm horizon: red above blue
    CHECK(dawn.sunset[3] > 0.99f);                                      // the sunset glow is at its strongest
    CHECK(day.sunset[3] < 0.001f && night.sunset[3] < 0.001f);
    // Stronger sunset tuning, brightness tuning, star tuning.
    SkyModel::Look strong; strong.sunsetStrength = 2.0f; strong.skyBrightness = 2.0f; strong.starBrightness = 0.5f;
    SkyModel::Params dawn2 = SkyModel::Evaluate({ 0.2f, 0.0f, 0.0f }, false, { 0, -1, 0 }, strong);
    CHECK(dawn2.sunset[3] <= 1.0f);                                      // clamped
    CHECK(Near(SkyModel::Evaluate(sunNoon, false, moonNoon, strong).zenith[2], day.zenith[2] * 2.0f));
    CHECK(Near(SkyModel::Evaluate(sunDown, true, moonUp, strong).night[0], 0.5f));
    // The sky changes smoothly with the sun's height: no jumps in the horizon colour.
    float lastRed = SkyModel::Evaluate({ 0.0f, -0.4f, 1.0f }, false, { 0, 1, 0 }, look).horizon[0];
    for (float h = -0.4f; h <= 1.0f; h += 0.01f) {
        const float r = SkyModel::Evaluate({ 0.0f, h, 1.0f }, false, { 0, 1, 0 }, look).horizon[0];
        CHECK(std::abs(r - lastRed) < 0.08f);   // the steepest part (sunrise) changes ~0.06 per 0.01 of height
        lastRed = r;
    }

    // Differs: identical skies and tiny drifts do not trigger a re-bake; a real change does.
    SkyModel::Params same = SkyModel::Evaluate(sunNoon, false, moonNoon, look);
    CHECK(!SkyModel::Differs(day, same));
    SkyModel::Params nudged = SkyModel::Evaluate({ 0.001f, 1.0f, 0.0f }, false, moonNoon, look);
    CHECK(!SkyModel::Differs(day, nudged));
    SkyModel::Params later = SkyModel::Evaluate({ 0.0f, 0.6f, 0.8f }, false, moonNoon, look);
    CHECK(SkyModel::Differs(day, later));
    CHECK(SkyModel::Differs(day, night));
    // The sky lighting is only re-baked for a change of about a degree: half a degree of sun drift is free, two degrees is not.
    // (A bake is the costly part of a running day, so this tolerance is what keeps the frame rate up.)
    const float half = std::sin(Radians(0.5f)), two = std::sin(Radians(2.0f));
    SkyModel::Params base = SkyModel::Evaluate({ 0.0f, std::cos(Radians(40.0f)), std::sin(Radians(40.0f)) }, false, moonNoon, look);
    SkyModel::Params drift = base;
    drift.sunDir[0] += half;
    CHECK(!SkyModel::Differs(base, drift));
    drift = base; drift.sunDir[0] += two;
    CHECK(SkyModel::Differs(base, drift));
    // Over a whole 5-minute day at the default update rate, the number of re-bakes is small: count the hours that Differs flags if
    // we compare against the last baked sky, stepping the clock in 1/60 s frames. At most one per 0.5 s anyway, but the sky must
    // not ask for more than a few dozen over the day either.
    {
        TimeOfDaySettings clock; clock.noonAzimuth = 200.0f; clock.maxElevation = 60.0f;
        float hourNow = 0.0f; int bakes = 0;
        auto skyAt = [&](float h) { const Vec3 a = TimeOfDay::SunToLight(clock, h); return SkyModel::Evaluate(a, true, { -a.x, -a.y, -a.z }, look); };
        SkyModel::Params baked = skyAt(hourNow);
        double sinceBake = 0.0;
        for (int frame = 0; frame < 300 * 60; ++frame) {            // 5 minutes at 60 fps = one whole day
            hourNow = TimeOfDay::AdvanceHour(hourNow, 1.0f / 60.0f, 5.0f);
            sinceBake += 1.0 / 60.0;
            const SkyModel::Params now = skyAt(hourNow);
            if (sinceBake >= 0.5 && SkyModel::Differs(baked, now)) { baked = now; sinceBake = 0.0; ++bakes; }
        }
        CHECK(bakes <= 300 / 0.5 * 1 + 1);   // never more than 2 a second
        CHECK(bakes >= 20);                  // but the light does follow the sky
        std::printf("re-bakes over one 5-minute day: %d\n", bakes);
    }

    if (failures == 0) std::printf("TimeOfDay tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
