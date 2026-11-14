// LightAngles: azimuth/elevation <-> the direction a light travels <-> the Euler rotation the gizmo edits.
#include "LightAngles.h"
#include <cstdio>
#include <cstdlib>

using namespace LightAngles;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
static bool NearV(const Vec3& a, const Vec3& b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

int main()
{
    // The compass: azimuth 0 is +X, 90 is +Z, like the wind. A sun on the horizon at azimuth 0 shines toward -X.
    CHECK(NearV(ToLight(0, 0), { 1, 0, 0 }));
    CHECK(NearV(ToLight(90, 0), { 0, 0, 1 }));
    CHECK(NearV(ToLight(0, 90), { 0, 1, 0 }));
    CHECK(NearV(Travel(0, 0), { -1, 0, 0 }));
    CHECK(NearV(Travel(0, 90), { 0, -1, 0 }));   // overhead sun shines straight down

    // Round trip: angles -> travel -> angles.
    for (float az : { 0.0f, 37.0f, 90.0f, 181.0f, 359.0f }) {
        for (float el : { -30.0f, 0.0f, 10.0f, 55.0f, 89.0f }) {
            float a = -1.0f, e = -1.0f;
            FromTravel(Travel(az, el), a, e);
            CHECK(Near(a, az, 0.01f) || Near(a, az + 360.0f, 0.01f) || Near(a + 360.0f, az, 0.01f));
            CHECK(Near(e, el, 0.01f));
        }
    }
    // Straight down keeps the azimuth it had (no compass direction there).
    { float a = 123.0f, e = 0.0f; FromTravel({ 0, -1, 0 }, a, e); CHECK(Near(a, 123.0f) && Near(e, 90.0f)); }

    // The Euler convention: no rotation shines along +Z; pitching 90 degrees points straight down; yaw 90 points along +X.
    CHECK(NearV(TravelFromEuler(0, 0, 0), { 0, 0, 1 }));
    CHECK(NearV(TravelFromEuler(kPi * 0.5f, 0, 0), { 0, -1, 0 }));
    CHECK(NearV(TravelFromEuler(0, kPi * 0.5f, 0), { 1, 0, 0 }));

    // Direction -> Euler -> direction gives the same direction back.
    for (float az : { 10.0f, 120.0f, 250.0f }) {
        for (float el : { -20.0f, 5.0f, 60.0f }) {
            const Vec3 d = Travel(az, el);
            float pitch = 0.0f, yaw = 0.0f;
            EulerFromTravel(d, pitch, yaw);
            CHECK(NearV(TravelFromEuler(pitch, yaw, 0.0f), d));
        }
    }
    // Roll about the forward axis does not change where the light shines (it is applied last, but a Z turn of a vector
    // already on the Z axis is a no-op): pitch 0, yaw 0, any roll still shines along +Z.
    CHECK(NearV(TravelFromEuler(0, 0, 1.2f), { 0, 0, 1 }));

    if (failures == 0) std::printf("LightAngles tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
