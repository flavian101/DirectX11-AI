// UnderwaterLook: the automatic look matches what the water always did; custom mode starts from it; the numbers behave.
#include "UnderwaterLook.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(float a, float b, float eps = 1e-5f) { return std::abs(a - b) < eps; }

int main()
{
    UnderwaterBody body;   // the default ocean
    UnderwaterSettings automatic;

    // The automatic look is the formula the engine used before there was an inspector (so nothing changes until you ask):
    // density 0.6 * absorption, shallow->deep over depth*absorption*0.15, light fading as exp(-depth*absorption*0.1).
    const float depth = 4.0f, strength = 2.5f;
    const UnderwaterLook look = UnderwaterMath::Compute(automatic, body, depth, strength);
    CHECK(Near(look.density, 0.35f * 0.6f));
    const float blend = std::min(depth * 0.35f * 0.15f, 1.0f);
    const float level = std::min(0.3f + 0.5f * strength, 1.6f) * std::exp(-depth * 0.35f * 0.1f);
    for (int i = 0; i < 3; ++i) {
        const float srgb = body.shallowColor[i] + (body.deepColor[i] - body.shallowColor[i]) * blend;
        CHECK(Near(look.color[i], UnderwaterMath::SrgbToLinear(srgb) * level, 1e-5f));
    }

    // Derived(): switching to custom reproduces exactly the same look.
    const UnderwaterSettings custom = UnderwaterMath::Derived(body);
    CHECK(custom.custom);
    for (float d : { 0.0f, 1.0f, 4.0f, 12.0f, 40.0f }) {
        const UnderwaterLook a = UnderwaterMath::Compute(automatic, body, d, 2.0f);
        const UnderwaterLook b = UnderwaterMath::Compute(custom, body, d, 2.0f);
        CHECK(Near(a.density, b.density, 1e-4f));
        for (int i = 0; i < 3; ++i) CHECK(Near(a.color[i], b.color[i], 1e-4f));
    }
    // A different absorption gives a different derived visibility.
    UnderwaterBody murky = body; murky.absorption = 1.0f;
    CHECK(UnderwaterMath::Derived(murky).visibility < UnderwaterMath::Derived(body).visibility);

    // Custom controls do what they say.
    UnderwaterSettings s = custom;
    s.visibility = 10.0f;
    CHECK(Near(UnderwaterMath::Compute(s, body, 1.0f, 2.5f).density, 0.3f));       // 3 / 10
    const float clear = UnderwaterMath::Compute(s, body, 1.0f, 2.5f).density;
    s.visibility = 100.0f;
    CHECK(UnderwaterMath::Compute(s, body, 1.0f, 2.5f).density < clear);          // clearer water: thinner fog
    s = custom; s.brightness = 2.0f;
    CHECK(UnderwaterMath::Compute(s, body, 1.0f, 0.5f).color[1] > UnderwaterMath::Compute(custom, body, 1.0f, 0.5f).color[1]);
    // Night: no light at all falls back to the night floor, and a brighter floor is brighter.
    s = custom; s.nightBrightness = 0.0f;
    const UnderwaterLook dark = UnderwaterMath::Compute(s, body, 2.0f, 0.0f);
    CHECK(dark.color[0] == 0.0f && dark.color[1] == 0.0f && dark.color[2] == 0.0f);
    s.nightBrightness = 0.5f;
    CHECK(UnderwaterMath::Compute(s, body, 2.0f, 0.0f).color[1] > 0.0f);
    // Deeper is darker, and shifts from the shallow colour toward the deep one.
    const UnderwaterLook near = UnderwaterMath::Compute(custom, body, 1.0f, 2.5f), far = UnderwaterMath::Compute(custom, body, 30.0f, 2.5f);
    CHECK(far.color[1] < near.color[1]);
    // Bright sun is capped, not unbounded.
    CHECK(Near(UnderwaterMath::Compute(custom, body, 0.0f, 100.0f).color[1], UnderwaterMath::Compute(custom, body, 0.0f, 3.0f).color[1]));
    // Pass-through values and clamping.
    s = custom; s.sunGlow = 0.7f; s.startDistance = 3.0f; s.surfaceMirror = 0.5f;
    const UnderwaterLook passed = UnderwaterMath::Compute(s, body, 1.0f, 2.0f);
    CHECK(Near(passed.sunGlow, 0.7f) && Near(passed.startDistance, 3.0f) && Near(passed.surfaceMirror, 0.5f));
    s.visibility = -5.0f; s.brightness = 99.0f; s.Clamp();
    CHECK(s.visibility == 1.0f && s.brightness == 4.0f);
    // Depth below zero (the camera just at the surface) behaves as zero.
    CHECK(Near(UnderwaterMath::Compute(custom, body, -3.0f, 2.0f).color[1], UnderwaterMath::Compute(custom, body, 0.0f, 2.0f).color[1]));

    if (failures == 0) std::printf("UnderwaterLook tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
