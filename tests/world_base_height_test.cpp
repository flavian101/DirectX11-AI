// Tests for the two "heights" of a world (roadmap H1) and for migration keeping an old terrain where it stood (roadmap C3).
// src/WorldGround.{h,cpp}. No Direct3D, no framework.
//   1. `heightScale` is how TALL the ground is, `baseHeight` how HIGH it starts: a ground value g stands at baseHeight + g * heightScale,
//      so the base lifts every height by the same amount and a taller hill leaves the lowest ground where it is.
//   2. The base moves nothing but height: normals are the same, a ray lifted by the base hits at the same distance, the ground grid
//      the water bakes from is the function's heights, and a caller that never heard of the base gets the old answers.
//   3. A migrated terrain stands exactly where the old one did, at any position.y and scale.y (the default terrain stands at -2 m).
#include "WorldGround.h"
#include "Heightfield.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static const float kHeightScale = 120.0f;
static const FractalWorldSettings kHills = { 11, 6, 300.0f };

static WorldGridSettings SmallGrid()
{
    WorldGridSettings grid;
    grid.chunksPerTile = 2;       // 64 cells
    grid.metresPerCell = 4.0f;    // 256 m tiles
    return grid;
}

static unsigned g_seed = 4242u;
static double Random01() { g_seed = g_seed * 1664525u + 1013904223u; return static_cast<double>((g_seed >> 8) & 0xFFFF) / 65535.0; }

static WorldGroundFunction MakeFunction(const WorldHeightSource& source, float heightScale, float baseHeight)
{
    WorldGroundFunction f;
    f.source = &source; f.grid = SmallGrid(); f.bounds = WorldBounds{}; f.heightScale = heightScale; f.baseHeight = baseHeight;
    return f;
}

// 1. The base lifts every height by the same amount.
static void TestBaseLiftsEveryHeight()
{
    const FractalHeightSource source(kHills);
    const float bases[] = { -250.0f, -2.0f, 0.0f, 37.5f, 800.0f };
    {
        const WorldGroundFunction flat = MakeFunction(source, kHeightScale, 0.0f);
        double worst = 0.0;
        int compared = 0;
        for (float base : bases) {
            const WorldGroundFunction lifted = MakeFunction(source, kHeightScale, base);
            for (int k = 0; k < 400; ++k) {
                const double x = Random01() * 700.0 - 100.0, z = Random01() * 700.0 - 100.0;
                float a = 0.0f, b = 0.0f;
                CHECK(flat.HeightAt(x, z, a) && lifted.HeightAt(x, z, b));
                worst = (std::max)(worst, static_cast<double>(std::fabs((b - base) - a)));
                // Ground stays between where it starts and where it ends: base .. base + heightScale.
                CHECK(b >= base - 1.0e-2f && b <= base + kHeightScale + 1.0e-2f);
                ++compared;
            }
        }
        std::printf("base height (%s): worst difference from 'height + base' %.6f m over %d points\n", "ground", worst, compared);
        CHECK(worst < 2.0e-3);   // float precision at 800 m is about 6e-5 m per step
    }
}

// 1b. A taller ground leaves the lowest ground where it is: the height ABOVE the base scales, the base does not.
static void TestHillHeightScalesAboveTheBase()
{
    const FractalHeightSource source(kHills);
    const float base = 55.0f;
    const WorldGroundFunction low = MakeFunction(source, 100.0f, base);
    const WorldGroundFunction tall = MakeFunction(source, 250.0f, base);
    double worst = 0.0;
    for (int k = 0; k < 400; ++k) {
        const double x = Random01() * 700.0, z = Random01() * 700.0;
        float a = 0.0f, b = 0.0f;
        CHECK(low.HeightAt(x, z, a) && tall.HeightAt(x, z, b));
        worst = (std::max)(worst, static_cast<double>(std::fabs((b - base) - 2.5f * (a - base))));
    }
    std::printf("hill height 100 -> 250 m at base 55 m: above the base every height is 2.5 x, worst difference %.6f m\n", worst);
    CHECK(worst < 2.0e-3);
}

// 2. The base moves nothing but height.
static void TestNormalsAndRaysIgnoreTheBase()
{
    const FractalHeightSource source(kHills);
    const float base = 420.0f;
    const WorldGroundFunction flat = MakeFunction(source, kHeightScale, 0.0f);
    const WorldGroundFunction lifted = MakeFunction(source, kHeightScale, base);
    double worstNormal = 0.0, worstRay = 0.0;
    int hits = 0;
    for (int k = 0; k < 200; ++k) {
        const double x = Random01() * 600.0, z = Random01() * 600.0;
        WorldVec3 a, b;
        CHECK(flat.NormalAt(x, z, a) && lifted.NormalAt(x, z, b));
        worstNormal = (std::max)(worstNormal, static_cast<double>(std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z)));

        // The same ray, lifted by the base, meets the lifted ground at the same distance.
        const WorldVec3 origin{ static_cast<float>(Random01() * 600.0), static_cast<float>(200.0 + Random01() * 100.0), static_cast<float>(Random01() * 600.0) };
        const WorldVec3 dir{ static_cast<float>(Random01() - 0.5), static_cast<float>(-0.15 - Random01() * 0.85), static_cast<float>(Random01() - 0.5) };
        float d0 = 0.0f, d1 = 0.0f;
        const bool hit0 = flat.Raycast(origin, dir, 2000.0f, d0);
        const bool hit1 = lifted.Raycast(WorldVec3{ origin.x, origin.y + base, origin.z }, dir, 2000.0f, d1);
        CHECK(hit0 == hit1);
        if (hit0 && hit1) { ++hits; worstRay = (std::max)(worstRay, static_cast<double>(std::fabs(d0 - d1))); }
        // A ray that does NOT move with the ground is under it: the base really did lift the ground away from where it was.
        float dStay = 0.0f;
        if (hit0) CHECK(!lifted.Raycast(origin, dir, 2000.0f, dStay));
    }
    std::printf("normals: worst difference %.6f; rays: %d hits, worst distance difference %.6f m\n", worstNormal, hits, worstRay);
    CHECK(hits > 100);
    CHECK(worstNormal < 2.0e-3);
    CHECK(worstRay < 5.0e-3);
}

static void TestGroundGridFollowsTheBase()
{
    const FractalHeightSource source(kHills);
    const WorldBounds bounds;   // endless
    const float base = -37.0f;
    const WorldGroundGrid plain = BuildWorldGroundGrid(source, SmallGrid(), bounds, kHeightScale, 0.0, 400.0, 0.0, 400.0);
    const WorldGroundGrid defaulted = BuildWorldGroundGrid(source, SmallGrid(), bounds, kHeightScale, 0.0, 400.0, 0.0, 400.0, 1024, 0.0f);
    const WorldGroundGrid lifted = BuildWorldGroundGrid(source, SmallGrid(), bounds, kHeightScale, 0.0, 400.0, 0.0, 400.0, 1024, base);
    CHECK(!plain.IsEmpty() && plain.metres.size() == lifted.metres.size());
    CHECK(plain.metres == defaulted.metres);   // a base of 0 gives bit for bit the old grid
    CHECK(plain.hasGround == lifted.hasGround);
    double worst = 0.0;
    for (size_t i = 0; i < plain.metres.size(); ++i) worst = (std::max)(worst, static_cast<double>(std::fabs(lifted.metres[i] - (plain.metres[i] + base))));
    std::printf("ground grid: %d x %d samples, worst difference from 'plain + base' %.6f m\n", lifted.countX, lifted.countZ, worst);
    CHECK(worst < 1.0e-3);

    // The grid the water measures and the function the player stands on may differ a little (the grid is bilinear, the mesh is two triangles
    // per cell: near a steep slope that is metres), but the BASE must not change that: the gap is the same at a base of 0 and of -37 m.
    auto gapBetweenGridAndFunction = [&](const WorldGroundGrid& grid, float baseHeight) {
        const WorldGroundFunction ground = MakeFunction(source, kHeightScale, baseHeight);
        std::vector<float> gaps;
        unsigned seed = 99u;
        for (int k = 0; k < 300; ++k) {
            seed = seed * 1664525u + 1013904223u; const double x = 8.0 + static_cast<double>((seed >> 8) & 0xFFFF) / 65535.0 * 380.0;
            seed = seed * 1664525u + 1013904223u; const double z = 8.0 + static_cast<double>((seed >> 8) & 0xFFFF) / 65535.0 * 380.0;
            float fromGrid = 0.0f, fromFunction = 0.0f;
            if (grid.Sample(x, z, fromGrid) && ground.HeightAt(x, z, fromFunction)) gaps.push_back(fromGrid - fromFunction);
        }
        return gaps;
    };
    const std::vector<float> gapPlain = gapBetweenGridAndFunction(plain, 0.0f), gapLifted = gapBetweenGridAndFunction(lifted, base);
    CHECK(gapPlain.size() == gapLifted.size() && gapPlain.size() > 250);
    double worstChange = 0.0, worstGap = 0.0;
    for (size_t i = 0; i < gapPlain.size() && i < gapLifted.size(); ++i) {
        worstChange = (std::max)(worstChange, static_cast<double>(std::fabs(gapLifted[i] - gapPlain[i])));
        worstGap = (std::max)(worstGap, static_cast<double>(std::fabs(gapPlain[i])));
    }
    std::printf("grid against function: they differ by up to %.2f m (bilinear against triangles); a base of %.0f m changes that gap by %.6f m\n", worstGap, base, worstChange);
    CHECK(worstChange < 2.0e-3);
}

// 3. A migrated terrain stands where the old one did. The old terrain is a Heightfield at position (x, y, z) with its heights stretched by
// scale.y; the world is the function built from HeightFrameOfOldTerrain. Same triangles, same lattice, so they must agree anywhere inside.
namespace {
// The world's ground as the old terrain's own samples: a value at each lattice point of the old grid (4 m cells), nothing outside it.
class OldSamples final : public WorldHeightSource {
public:
    OldSamples(const std::vector<float>& samples, int cells, double cornerX, double cornerZ, double cell)
        : samples_(samples), cells_(cells), cornerX_(cornerX), cornerZ_(cornerZ), cell_(cell) {}
    float Height01(double worldX, double worldZ) const override
    {
        const int i = std::clamp(static_cast<int>(std::lround((worldX - cornerX_) / cell_)), 0, cells_);
        const int j = std::clamp(static_cast<int>(std::lround((worldZ - cornerZ_) / cell_)), 0, cells_);
        return samples_[static_cast<size_t>(j) * (cells_ + 1) + i];
    }
private:
    const std::vector<float>& samples_;
    int cells_;
    double cornerX_, cornerZ_, cell_;
};
}

int main()
{
    TestBaseLiftsEveryHeight();
    TestHillHeightScalesAboveTheBase();
    TestNormalsAndRaysIgnoreTheBase();
    TestGroundGridFollowsTheBase();
    if (g_failures) { std::printf("%d CHECK(s) failed.\n", g_failures); return 1; }
    std::printf("All world base height tests passed.\n");
    return 0;
}
