// Unit tests for src/Heightfield.{h,cpp}. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
// The key idea: build the SAME triangles Terrain.cpp builds, then check the heightfield against them by brute force.
#include "Heightfield.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

// A tiny deterministic random generator, so a failure can be reproduced.
static float Random01(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

// A smooth hilly field with values inside 0.2 .. 0.8.
static Heightfield MakeHills(int cellsX, int cellsZ, float sizeX, float sizeZ, float heightScale)
{
    std::vector<float> samples;
    for (int z = 0; z <= cellsZ; ++z)
        for (int x = 0; x <= cellsX; ++x)
            samples.push_back(0.5f + 0.3f * std::sin(x * 0.4f) * std::cos(z * 0.3f));
    Heightfield field;
    field.Reset({ cellsX, cellsZ, sizeX, sizeZ, heightScale }, samples);
    return field;
}

struct Triangle { HfVec3 a, b, c; };

// The triangles of the mesh, built with the same index pattern as BuildTerrainModel: per cell (TL, BL, TR) and (TR, BL, BR).
static std::vector<Triangle> MeshTriangles(const Heightfield& field)
{
    const HeightfieldLayout& l = field.GetLayout();
    auto vertex = [&](int gx, int gz) {
        return HfVec3{ (static_cast<float>(gx) / l.cellsX - 0.5f) * l.sizeX,
                       field.Sample(gx, gz) * l.heightScale,
                       (static_cast<float>(gz) / l.cellsZ - 0.5f) * l.sizeZ };
    };
    std::vector<Triangle> triangles;
    for (int z = 0; z < l.cellsZ; ++z)
        for (int x = 0; x < l.cellsX; ++x) {
            const HfVec3 tl = vertex(x, z), tr = vertex(x + 1, z), bl = vertex(x, z + 1), br = vertex(x + 1, z + 1);
            triangles.push_back({ tl, bl, tr });
            triangles.push_back({ tr, bl, br });
        }
    return triangles;
}

// Moller-Trumore ray/triangle test (double sided). On a hit, t is the distance along the ray.
static bool RayTriangle(const HfVec3& o, const HfVec3& d, const Triangle& tri, float& t)
{
    const float e1[3] = { tri.b.x - tri.a.x, tri.b.y - tri.a.y, tri.b.z - tri.a.z };
    const float e2[3] = { tri.c.x - tri.a.x, tri.c.y - tri.a.y, tri.c.z - tri.a.z };
    const float p[3] = { d.y * e2[2] - d.z * e2[1], d.z * e2[0] - d.x * e2[2], d.x * e2[1] - d.y * e2[0] };
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-12f) return false;
    const float s[3] = { o.x - tri.a.x, o.y - tri.a.y, o.z - tri.a.z };
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
    if (u < -1e-6f || u > 1.0f + 1e-6f) return false;
    const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
    const float v = (d.x * q[0] + d.y * q[1] + d.z * q[2]) / det;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f) return false;
    t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
    return t > 0.0f;
}

static void TestReset()
{
    Heightfield field;
    CHECK(field.IsEmpty());
    CHECK(!field.Reset({ 4, 4, 10.0f, 10.0f, 1.0f }, std::vector<float>(24, 0.0f)));   // needs 25 samples
    CHECK(field.IsEmpty());
    CHECK(!field.Reset({ 0, 4, 10.0f, 10.0f, 1.0f }, std::vector<float>(5, 0.0f)));    // no cells
    CHECK(!field.Reset({ 4, 4, 0.0f, 10.0f, 1.0f }, std::vector<float>(25, 0.0f)));    // no size
    CHECK(field.Reset({ 4, 4, 10.0f, 10.0f, 1.0f }, std::vector<float>(25, 0.5f)));
    CHECK(!field.IsEmpty());
    float y = 0.0f;
    CHECK(field.HeightAt(0.0f, 0.0f, y));
}

static void TestFlatAndBounds()
{
    Heightfield field;
    field.Reset({ 8, 4, 16.0f, 8.0f, 10.0f }, std::vector<float>(9 * 5, 0.5f));
    float y = 0.0f;
    CHECK(field.HeightAt(0.0f, 0.0f, y) && std::fabs(y - 5.0f) < 1e-5f);
    CHECK(field.HeightAt(-8.0f, -4.0f, y) && std::fabs(y - 5.0f) < 1e-5f);   // the corners belong to the field
    CHECK(field.HeightAt(8.0f, 4.0f, y) && std::fabs(y - 5.0f) < 1e-5f);
    CHECK(!field.HeightAt(8.01f, 0.0f, y));                                  // just outside
    CHECK(!field.HeightAt(0.0f, -4.01f, y));
    CHECK(!field.HeightAt(NAN, 0.0f, y));
    HfVec3 n;
    CHECK(field.NormalAt(1.0f, 1.0f, n) && std::fabs(n.y - 1.0f) < 1e-5f && std::fabs(n.x) < 1e-5f);
}

// A plane is representable exactly by the triangles, so the heightfield must reproduce it exactly.
static void TestPlane()
{
    const int cells = 10;
    std::vector<float> samples;
    for (int z = 0; z <= cells; ++z)
        for (int x = 0; x <= cells; ++x) samples.push_back(0.1f + 0.02f * x + 0.01f * z);   // slopes in x and z
    Heightfield field;
    field.Reset({ cells, cells, 20.0f, 20.0f, 5.0f }, samples);
    std::uint32_t rng = 7;
    for (int i = 0; i < 500; ++i) {
        const float x = (Random01(rng) - 0.5f) * 20.0f, z = (Random01(rng) - 0.5f) * 20.0f;
        const float gx = (x + 10.0f) / 2.0f, gz = (z + 10.0f) / 2.0f;   // grid units: 2 world units per cell
        float y = 0.0f;
        CHECK(field.HeightAt(x, z, y));
        CHECK(std::fabs(y - (0.1f + 0.02f * gx + 0.01f * gz) * 5.0f) < 1e-3f);
    }
    HfVec3 n;
    CHECK(field.NormalAt(0.3f, -0.7f, n));
    const float slopeX = 0.02f * 5.0f / 2.0f, slopeZ = 0.01f * 5.0f / 2.0f;   // world height per world unit
    const float length = std::sqrt(slopeX * slopeX + 1.0f + slopeZ * slopeZ);
    CHECK(std::fabs(n.x - (-slopeX / length)) < 1e-3f && std::fabs(n.y - 1.0f / length) < 1e-3f && std::fabs(n.z - (-slopeZ / length)) < 1e-3f);
}

// The important one: on a bumpy field, HeightAt must equal where a vertical ray meets the REAL mesh triangles.
static void TestMatchesMesh()
{
    const Heightfield field = MakeHills(24, 16, 48.0f, 32.0f, 10.0f);
    const std::vector<Triangle> mesh = MeshTriangles(field);
    std::uint32_t rng = 99;
    float worst = 0.0f;
    for (int i = 0; i < 600; ++i) {
        const float x = (Random01(rng) - 0.5f) * 47.9f, z = (Random01(rng) - 0.5f) * 31.9f;
        float expected = -1.0f;
        for (const Triangle& tri : mesh) {
            float t = 0.0f;
            if (RayTriangle({ x, 50.0f, z }, { 0.0f, -1.0f, 0.0f }, tri, t)) expected = 50.0f - t;
        }
        float y = 0.0f;
        CHECK(field.HeightAt(x, z, y));
        CHECK(expected >= 0.0f);
        worst = (std::max)(worst, std::fabs(y - expected));
    }
    std::printf("  worst height error against the mesh: %g\n", worst);
    CHECK(worst < 1e-3f);
    HfVec3 n;
    for (int i = 0; i < 200; ++i) {
        const float x = (Random01(rng) - 0.5f) * 47.9f, z = (Random01(rng) - 0.5f) * 31.9f;
        CHECK(field.NormalAt(x, z, n));
        CHECK(std::fabs(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) - 1.0f) < 1e-4f && n.y > 0.0f);
    }
}

static void TestRaycast()
{
    const Heightfield field = MakeHills(24, 16, 48.0f, 32.0f, 10.0f);
    float t = 0.0f, ground = 0.0f;

    // straight down: lands exactly on the surface
    CHECK(field.Raycast({ 3.3f, 100.0f, -2.1f }, { 0.0f, -1.0f, 0.0f }, t));
    CHECK(field.HeightAt(3.3f, -2.1f, ground) && std::fabs((100.0f - t) - ground) < 1e-3f);

    // a direction that is NOT unit length: t simply scales with it
    float tScaled = 0.0f;
    CHECK(field.Raycast({ 3.3f, 100.0f, -2.1f }, { 0.0f, -4.0f, 0.0f }, tScaled));
    CHECK(std::fabs(tScaled * 4.0f - t) < 1e-3f);

    // misses: pointing up, parallel and above, beside the field, and starting under the ground
    CHECK(!field.Raycast({ 0.0f, 20.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, t));
    CHECK(!field.Raycast({ -100.0f, 50.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, t));
    CHECK(!field.Raycast({ 100.0f, 50.0f, 100.0f }, { 0.0f, -1.0f, 0.0f }, t));
    CHECK(!field.Raycast({ 0.0f, -5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, t));
    CHECK(!field.Raycast({ 0.0f, 50.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, t));

    // random downward rays, compared with a brute-force test against every triangle of the mesh
    const std::vector<Triangle> mesh = MeshTriangles(field);
    std::uint32_t rng = 2024;
    int hits = 0, mismatches = 0;
    for (int i = 0; i < 400; ++i) {
        const HfVec3 origin{ (Random01(rng) - 0.5f) * 90.0f, 20.0f + Random01(rng) * 30.0f, (Random01(rng) - 0.5f) * 70.0f };
        HfVec3 dir{ (Random01(rng) - 0.5f) * 2.0f, -0.3f - Random01(rng), (Random01(rng) - 0.5f) * 2.0f };
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        dir = { dir.x / len, dir.y / len, dir.z / len };

        float best = 1e30f;
        for (const Triangle& tri : mesh) {
            float hit = 0.0f;
            if (RayTriangle(origin, dir, tri, hit) && hit < best) best = hit;
        }
        float mine = 0.0f;
        const bool got = field.Raycast(origin, dir, mine);
        if (best < 1e29f) {
            ++hits;
            if (!got || std::fabs(mine - best) > 0.02f) ++mismatches;
        } else if (got) {
            ++mismatches;
        }
    }
    std::printf("  random rays: %d hit the mesh, %d disagreements\n", hits, mismatches);
    CHECK(hits > 50);
    CHECK(mismatches == 0);
}

int main()
{
    TestReset();
    TestFlatAndBounds();
    TestPlane();
    TestMatchesMesh();
    TestRaycast();
    if (g_failures == 0) std::printf("Heightfield: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
