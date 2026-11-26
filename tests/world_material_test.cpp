// Unit tests for src/WorldLook.h (roadmap H10): the world's layer rules follow the SEA, and only the sea. No Direct3D, no framework: a failed
// CHECK prints and the program exits 1.
#include "WorldLook.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
static bool Near(float a, float b) { return std::fabs(a - b) <= 1e-4f; }

// The sand line and the snow line are the look's heights above the sea, in world Y.
static void TestFollowsTheSea()
{
    const TerrainSettings look = DefaultWorldLook();
    for (float sea : { -150.0f, -20.0f, 0.0f, 35.0f, 400.0f }) {
        const TerrainSettings m = WorldMaterial(look, sea, 240.0f);
        CHECK(Near(m.shoreHeight, sea + look.shoreHeight));
        CHECK(Near(m.snowHeight, sea + look.snowHeight));
        CHECK(Near(m.shoreWidth, look.shoreWidth) && Near(m.snowFade, look.snowFade));   // the widths are metres, not world Y
    }
}

// The relief only sets the tile range: it never moves the sand or the snow.
static void TestReliefDoesNotMoveTheLayers()
{
    const TerrainSettings look = DefaultWorldLook();
    const TerrainSettings a = WorldMaterial(look, 12.0f, 100.0f), b = WorldMaterial(look, 12.0f, 500.0f);
    CHECK(Near(a.shoreHeight, b.shoreHeight) && Near(a.snowHeight, b.snowHeight));
    CHECK(Near(a.heightScale, 100.0f) && Near(b.heightScale, 500.0f));
    CHECK(a.SameSplatRules(b));   // the baked splat map does not change with the relief
}

// Moving the sea changes the baked rules (the splat map is baked again); the same sea does not.
static void TestRulesBakeAgainOnlyWhenTheSeaMoves()
{
    const TerrainSettings look = DefaultWorldLook();
    CHECK(WorldMaterial(look, 0.0f, 240.0f).SameSplatRules(WorldMaterial(look, 0.0f, 240.0f)));
    CHECK(!WorldMaterial(look, 0.0f, 240.0f).SameSplatRules(WorldMaterial(look, 5.0f, 240.0f)));
}

// A look the user sets (sand 3 m above the sea, snow 90 m) is respected, and Clamp keeps a world's look valid.
static void TestUserLook()
{
    TerrainSettings look = DefaultWorldLook();
    look.shoreHeight = 3.0f; look.snowHeight = 90.0f; look.rockTiling = 9999.0f;
    const TerrainSettings m = WorldMaterial(look, 10.0f, 240.0f);
    CHECK(Near(m.shoreHeight, 13.0f) && Near(m.snowHeight, 100.0f));
    CHECK(m.rockTiling <= TerrainSettings::kRockTilingMax);
}

// The defaults are a complete look for a 1 km tile: long detail distances, sand above the sea, snow far above it.
static void TestDefaults()
{
    const TerrainSettings look = DefaultWorldLook();
    CHECK(look.lodDistance0 >= 300.0f && look.lodDistance1 > look.lodDistance0);
    CHECK(look.shoreHeight > 0.0f && look.snowHeight > look.shoreHeight + look.shoreWidth);
    TerrainSettings clamped = look;
    clamped.Clamp();
    CHECK(clamped.SameSplatRules(look) && Near(clamped.lodDistance0, look.lodDistance0));   // Clamp leaves the defaults alone
}

int main()
{
    TestFollowsTheSea();
    TestReliefDoesNotMoveTheLayers();
    TestRulesBakeAgainOnlyWhenTheSeaMoves();
    TestUserLook();
    TestDefaults();
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("world material tests: all passed\n");
    return 0;
}
