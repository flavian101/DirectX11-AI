// Tests for the pure maths of lesson 33: the hull mask's outline and the floating plane fit and spring.
#include "Buoyancy.h"
#include "HullMask.h"
#include "WaterWaves.h"
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAILED line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)
static bool Near(float a, float b, float tolerance = 1e-3f) { return std::abs(a - b) <= tolerance; }
static float Random01(unsigned& state) { state = state * 1664525u + 1013904223u; return static_cast<float>((state >> 8) & 0xFFFF) / 65535.0f; }

static void TestConvexHull()
{
    using namespace HullMask;
    // A square with points inside it and on its edges: the hull is the 4 corners.
    std::vector<Point2> points = { {0,0}, {4,0}, {4,4}, {0,4}, {2,2}, {1,1}, {2,0}, {4,2}, {3,3} };
    const std::vector<Point2> hull = ConvexHull(points);
    CHECK(hull.size() == 4);
    CHECK(Near(Area(hull), 16.0f));
    // Random clouds: every input point must lie inside (or on) the hull, and the hull must be counter-clockwise.
    unsigned state = 7u;
    for (int round = 0; round < 50; ++round) {
        std::vector<Point2> cloud;
        for (int i = 0; i < 200; ++i) cloud.push_back({ Random01(state) * 10.0f - 5.0f, Random01(state) * 4.0f - 2.0f });
        const std::vector<Point2> h = ConvexHull(cloud);
        CHECK(h.size() >= 3);
        CHECK(Area(h) > 0.0f);
        for (const Point2& p : cloud) CHECK(InsideConvex(h, p, 1e-3f));
    }
    CHECK(ConvexHull({ {1,1} }).size() == 1);
}

static void TestInset()
{
    using namespace HullMask;
    // A 10 x 4 rectangle moved in by 0.5 on every side: 9 x 3.
    const std::vector<Point2> rect = { {0,0}, {10,0}, {10,4}, {0,4} };
    const std::vector<Point2> inset = InsetConvex(rect, 0.5f);
    CHECK(inset.size() == 4);
    CHECK(Near(Area(inset), 9.0f * 3.0f));
    // A boat-shaped hexagon: the inset is inside the original, smaller, and still convex.
    std::vector<Point2> boat = ConvexHull({ {-5,0}, {-3,-1.5f}, {3,-1.5f}, {5,0}, {3,1.5f}, {-3,1.5f} });
    for (float d : { 0.05f, 0.2f, 0.6f }) {
        const std::vector<Point2> in = InsetConvex(boat, d);
        CHECK(in.size() == boat.size());
        CHECK(Area(in) < Area(boat));
        for (const Point2& p : in) CHECK(InsideConvex(boat, p, 1e-4f));
        CHECK(Area(ConvexHull(in)) > 0.0f && Near(Area(ConvexHull(in)), Area(in), 1e-2f));   // still convex: its own hull is itself
    }
    // Far too large: no inversion (the area stays >= 0 and the points stay inside).
    const std::vector<Point2> crushed = InsetConvex(boat, 50.0f);
    for (const Point2& p : crushed) CHECK(InsideConvex(boat, p, 1e-3f));
    CHECK(InsetConvex(boat, 0.0f).size() == boat.size());
}

static void TestPlaneFit()
{
    using namespace Buoyancy;
    // A level sea: no slope, mean is the level.
    std::vector<Sample> flat;
    for (int i = -1; i <= 1; ++i) for (int j = -1; j <= 1; ++j) flat.push_back({ i * 2.0f, j * 3.0f, 5.0f });
    Plane p = FitPlane(flat);
    CHECK(Near(p.mean, 5.0f) && Near(p.slopeX, 0.0f) && Near(p.slopeZ, 0.0f));
    // A tilted plane h = 2 + 0.3 x - 0.1 z, sampled on a grid turned 40 degrees: the fit recovers it exactly.
    std::vector<Sample> tilted;
    const float c = std::cos(0.7f), s = std::sin(0.7f);
    for (int i = -1; i <= 1; ++i) for (int j = -1; j <= 1; ++j) {
        const float lx = i * 2.0f, lz = j * 1.0f;
        const float x = 10.0f + lx * c - lz * s, z = -4.0f + lx * s + lz * c;
        tilted.push_back({ x, z, 2.0f + 0.3f * (x - 10.0f) - 0.1f * (z + 4.0f) });
    }
    p = FitPlane(tilted);
    CHECK(Near(p.mean, 2.0f) && Near(p.slopeX, 0.3f) && Near(p.slopeZ, -0.1f));
    // Degenerate: all samples at one spot, or none: level, no crash.
    p = FitPlane({ { 1, 1, 3 }, { 1, 1, 3 } });
    CHECK(Near(p.slopeX, 0.0f) && Near(p.slopeZ, 0.0f));
    CHECK(Near(FitPlane({}).mean, 0.0f));
}

static void TestSpring()
{
    using namespace Buoyancy;
    // Reaches its target, for tame and for huge frame times, and never blows up.
    for (float dt : { 0.001f, 0.016f, 0.033f, 0.1f, 5.0f }) {
        Spring spring;
        spring.Snap(0.0f);
        for (int i = 0; i < 4000; ++i) spring.Step(2.0f, 4.0f, 0.8f, dt);
        CHECK(Near(spring.value, 2.0f, 1e-2f));
        CHECK(std::abs(spring.velocity) < 1e-2f);
    }
    // Below critical damping it overshoots; at critical damping it does not.
    Spring bouncy, critical;
    float bouncyMax = 0.0f, criticalMax = 0.0f;
    for (int i = 0; i < 600; ++i) {
        bouncy.Step(1.0f, 6.0f, 0.4f, 0.01f);
        critical.Step(1.0f, 6.0f, 1.0f, 0.01f);
        bouncyMax = (std::max)(bouncyMax, bouncy.value);
        criticalMax = (std::max)(criticalMax, critical.value);
    }
    CHECK(bouncyMax > 1.05f);
    CHECK(criticalMax < 1.01f);
}

static void TestRotation()
{
    using namespace Buoyancy;
    // The rotation must turn up into the normal, keep lengths, and not twist: forward stays in the plane of the turn.
    unsigned state = 99u;
    for (int i = 0; i < 100; ++i) {
        const float slopeX = Random01(state) * 1.2f - 0.6f, slopeZ = Random01(state) * 1.2f - 0.6f;
        float n[3], m[9];
        Normal(slopeX, slopeZ, 1.0f, n);
        CHECK(Near(n[0] * n[0] + n[1] * n[1] + n[2] * n[2], 1.0f));
        RotationFromUp(n, m);
        // Row-vector convention: up * M = second row of M.
        CHECK(Near(m[3], n[0]) && Near(m[4], n[1]) && Near(m[5], n[2]));
        // Orthonormal rows.
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) {
            const float dot = m[a * 3] * m[b * 3] + m[a * 3 + 1] * m[b * 3 + 1] + m[a * 3 + 2] * m[b * 3 + 2];
            CHECK(Near(dot, a == b ? 1.0f : 0.0f));
        }
    }
    float flat[3], m[9];
    Normal(0.4f, -0.3f, 0.0f, flat);   // follow = 0: always level
    CHECK(Near(flat[1], 1.0f));
    RotationFromUp(flat, m);
    CHECK(Near(m[0], 1.0f) && Near(m[4], 1.0f) && Near(m[8], 1.0f));
}

// The whole loop of Model::StepFloating with the REAL wave maths: sample the sea at 9 points under a hull, fit a plane, let
// springs follow it. The boat must stay close to the surface, lean no more than the waves do, and never blow up.
static void TestFloatingOnRealWaves()
{
    WaterWaves::Params params;
    params.amplitude = 0.5f; params.count = 8; params.choppiness = 0.8f;
    const WaterWaves::Table table = WaterWaves::Build(params);
    const float hullHalfLength = 2.0f, hullHalfWidth = 0.7f, draft = 0.3f;
    Buoyancy::Spring heave, slopeX, slopeZ;
    float worstMiss = 0.0f, worstSlope = 0.0f;
    const float yaw = 0.9f, c = std::cos(yaw), s = std::sin(yaw);
    for (int frame = 0; frame < 60 * 40; ++frame) {
        const float time = frame / 60.0f;
        const float cx = 20.0f + 0.5f * time, cz = -7.0f;   // drifting slowly
        std::vector<Buoyancy::Sample> samples;
        for (int gx = -1; gx <= 1; ++gx) for (int gz = -1; gz <= 1; ++gz) {
            const float lx = gx * hullHalfLength * 0.7f, lz = gz * hullHalfWidth * 0.7f;
            const float x = cx + lx * c - lz * s, z = cz + lx * s + lz * c;
            samples.push_back({ x, z, WaterWaves::HeightAt(table, x, z, time) });
        }
        const Buoyancy::Plane plane = Buoyancy::FitPlane(samples);
        if (frame == 0) { heave.Snap(plane.mean - draft); slopeX.Snap(plane.slopeX); slopeZ.Snap(plane.slopeZ); }
        else {
            heave.Step(plane.mean - draft, 3.0f, 0.6f, 1.0f / 60.0f);
            slopeX.Step(plane.slopeX, 3.0f, 0.6f, 1.0f / 60.0f);
            slopeZ.Step(plane.slopeZ, 3.0f, 0.6f, 1.0f / 60.0f);
        }
        // How far is the keel from where it should be (water at the centre minus the draft)?
        const float centreWater = WaterWaves::HeightAt(table, cx, cz, time);
        worstMiss = (std::max)(worstMiss, std::abs((heave.value + draft) - centreWater));
        worstSlope = (std::max)(worstSlope, std::sqrt(slopeX.value * slopeX.value + slopeZ.value * slopeZ.value));
        CHECK(std::isfinite(heave.value) && std::isfinite(slopeX.value) && std::isfinite(slopeZ.value));
    }
    CHECK(worstMiss < 0.5f);    // never further from the water than the tallest wave (0.5)
    CHECK(worstSlope < 1.0f);   // never leans more than 45 degrees: the sea here is gentle
}

int main()
{
    TestConvexHull();
    TestInset();
    TestPlaneFit();
    TestSpring();
    TestRotation();
    TestFloatingOnRealWaves();
    if (g_failures == 0) std::printf("buoyancy tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
