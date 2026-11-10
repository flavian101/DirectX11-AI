// Unit tests for src/HeightfieldEdit.{h,cpp}: brush weights, the four brushes, stroke recording and undo patches.
#include "HeightfieldEdit.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

// A square field, 20 cells of 1 unit each (so grid units == local units), every sample = `value`.
static Heightfield MakeFlat(float value, int cells = 20, float size = 20.0f, float heightScale = 10.0f)
{
    Heightfield field;
    field.Reset({ cells, cells, size, size, heightScale }, std::vector<float>(static_cast<size_t>(cells + 1) * (cells + 1), value));
    return field;
}

static void TestWeight()
{
    CHECK(BrushWeight(0.0f, 5.0f, 0.6f) == 1.0f);                       // the centre is always full strength
    CHECK(BrushWeight(5.0f, 5.0f, 0.6f) == 0.0f);                       // the edge and beyond: nothing
    CHECK(BrushWeight(9.0f, 5.0f, 0.6f) == 0.0f);
    CHECK(BrushWeight(4.99f, 5.0f, 0.0f) == 1.0f);                      // falloff 0: a hard edge
    CHECK(BrushWeight(2.0f, 5.0f, 0.0f) == 1.0f);
    float previous = 2.0f;
    for (int i = 0; i <= 100; ++i) {                                    // never rises as you move outwards
        const float w = BrushWeight(5.0f * i / 100.0f, 5.0f, 1.0f);
        CHECK(w <= previous + 1e-6f && w >= 0.0f && w <= 1.0f);
        previous = w;
    }
    CHECK(BrushWeight(2.5f, 5.0f, 1.0f) > 0.4f && BrushWeight(2.5f, 5.0f, 1.0f) < 0.6f);   // halfway: about half (smoothstep)
    CHECK(BrushWeight(1.0f, 0.0f, 0.5f) == 0.0f);                       // no radius, no brush
}

static void TestRaiseLower()
{
    Heightfield field = MakeFlat(0.5f);
    BrushSettings brush;
    brush.mode = BrushMode::Raise; brush.radius = 5.0f; brush.strength = 0.4f; brush.falloff = 0.6f;
    const GridRect changed = ApplyBrush(field, brush, 0.0f, 0.0f, 0.1f, nullptr);   // local (0,0) is grid (10,10)

    const float expected = 0.5f + 0.4f * kRaiseRatePerSecond * 0.1f;               // 0.51 at the centre
    CHECK(std::fabs(field.Sample(10, 10) - expected) < 1e-6f);
    CHECK(field.Sample(10, 10 + 5) == 0.5f);                                       // exactly at the radius: untouched
    CHECK(field.Sample(0, 0) == 0.5f);                                             // far away: untouched
    CHECK(!changed.IsEmpty() && changed.x0 >= 5 && changed.x1 <= 15 && changed.z0 >= 5 && changed.z1 <= 15);
    CHECK(changed.x0 <= 10 && changed.x1 >= 10);

    brush.mode = BrushMode::Lower;
    ApplyBrush(field, brush, 0.0f, 0.0f, 0.1f, nullptr);
    CHECK(std::fabs(field.Sample(10, 10) - 0.5f) < 1e-6f);                         // raise then lower = back where it was

    // clamping: nothing goes above 1 or below 0, and a brush that changes nothing reports an empty rect
    Heightfield top = MakeFlat(1.0f);
    brush.mode = BrushMode::Raise;
    CHECK(ApplyBrush(top, brush, 0.0f, 0.0f, 1.0f, nullptr).IsEmpty());
    Heightfield bottom = MakeFlat(0.0f);
    brush.mode = BrushMode::Lower;
    CHECK(ApplyBrush(bottom, brush, 0.0f, 0.0f, 1.0f, nullptr).IsEmpty());
    Heightfield nearTop = MakeFlat(0.999f);
    brush.mode = BrushMode::Raise;
    ApplyBrush(nearTop, brush, 0.0f, 0.0f, 5.0f, nullptr);
    CHECK(nearTop.Sample(10, 10) == 1.0f);
}

static void TestFlatten()
{
    Heightfield field = MakeFlat(0.2f);
    field.SetSample(field.IndexOf(10, 10), 0.9f);                                  // one tall spike
    BrushSettings brush;
    brush.mode = BrushMode::Flatten; brush.radius = 4.0f; brush.strength = 1.0f; brush.falloff = 0.0f; brush.targetHeight = 0.4f;
    for (int i = 0; i < 20; ++i) ApplyBrush(field, brush, 0.0f, 0.0f, 0.1f, nullptr);
    CHECK(std::fabs(field.Sample(10, 10) - 0.4f) < 1e-3f);                         // pulled to the target...
    CHECK(std::fabs(field.Sample(11, 10) - 0.4f) < 1e-3f);                         // ...together with its neighbours
    CHECK(field.Sample(16, 10) == 0.2f);                                           // outside the radius: unchanged
}

static void TestSmooth()
{
    Heightfield field = MakeFlat(0.2f);
    field.SetSample(field.IndexOf(10, 10), 0.8f);
    BrushSettings brush;
    brush.mode = BrushMode::Smooth; brush.radius = 4.0f; brush.strength = 1.0f; brush.falloff = 0.0f;
    ApplyBrush(field, brush, 0.0f, 0.0f, 0.05f, nullptr);
    CHECK(field.Sample(10, 10) < 0.8f);                                            // the spike comes down
    CHECK(field.Sample(11, 10) > 0.2f && field.Sample(10, 9) > 0.2f);              // and its neighbours come up

    // The two-pass rule: a symmetric spike must stay symmetric. If smoothing wrote as it went, the loop direction
    // would leak in and left/right (or up/down) would differ.
    for (int dz = -3; dz <= 3; ++dz)
        for (int dx = -3; dx <= 3; ++dx) {
            CHECK(std::fabs(field.Sample(10 + dx, 10 + dz) - field.Sample(10 - dx, 10 - dz)) < 1e-6f);
            CHECK(std::fabs(field.Sample(10 + dx, 10 + dz) - field.Sample(10 + dz, 10 + dx)) < 1e-6f);
        }

    // repeated smoothing keeps flattening: the largest difference between neighbours only shrinks
    auto roughness = [&] {
        float worst = 0.0f;
        for (int z = 0; z <= 20; ++z) for (int x = 0; x < 20; ++x) worst = (std::max)(worst, std::fabs(field.Sample(x, z) - field.Sample(x + 1, z)));
        return worst;
    };
    const float before = roughness();
    for (int i = 0; i < 10; ++i) ApplyBrush(field, brush, 0.0f, 0.0f, 0.05f, nullptr);
    CHECK(roughness() < before);
}

// On a stretched grid (cells 2 wide in x, 1 deep in z) the brush must still be a circle in LOCAL units.
static void TestStretchedGrid()
{
    Heightfield field;
    field.Reset({ 24, 32, 48.0f, 32.0f, 10.0f }, std::vector<float>(25 * 33, 0.5f));
    BrushSettings brush;
    brush.mode = BrushMode::Raise; brush.radius = 8.0f; brush.strength = 1.0f; brush.falloff = 0.0f;
    ApplyBrush(field, brush, 0.0f, 0.0f, 0.1f, nullptr);                           // centre = grid (12, 16)
    CHECK(field.Sample(12 + 3, 16) > 0.5f);                                        // 3 cells * 2 = 6 units in x: inside
    CHECK(field.Sample(12 + 5, 16) == 0.5f);                                       // 5 cells * 2 = 10 units: outside
    CHECK(field.Sample(12, 16 + 7) > 0.5f);                                        // 7 cells * 1 = 7 units in z: inside
    CHECK(field.Sample(12, 16 + 9) == 0.5f);                                       // 9 units: outside
}

static void TestEdges()
{
    Heightfield field = MakeFlat(0.5f);
    BrushSettings brush;
    brush.mode = BrushMode::Raise; brush.radius = 5.0f; brush.strength = 1.0f;
    CHECK(!ApplyBrush(field, brush, -10.0f, -10.0f, 0.1f, nullptr).IsEmpty());     // centred ON the corner: one quarter works
    CHECK(field.Sample(0, 0) > 0.5f);
    CHECK(ApplyBrush(field, brush, 100.0f, 100.0f, 0.1f, nullptr).IsEmpty());      // far outside: nothing, and no crash
    CHECK(ApplyBrush(field, brush, 0.0f, 0.0f, 0.0f, nullptr).IsEmpty());          // zero time: nothing
    Heightfield empty;
    CHECK(ApplyBrush(empty, brush, 0.0f, 0.0f, 0.1f, nullptr).IsEmpty());
}

static void TestStrokeAndPatch()
{
    Heightfield field = MakeFlat(0.5f);
    const std::vector<float> original = field.Samples();
    StrokeRecorder stroke;
    BrushSettings brush;
    brush.mode = BrushMode::Raise; brush.radius = 6.0f; brush.strength = 0.8f;
    GridRect everything;
    for (int step = 0; step < 15; ++step) {                                        // a dragged stroke: the brush moves
        const GridRect r = ApplyBrush(field, brush, -5.0f + step * 0.7f, 2.0f, 0.016f, &stroke);
        if (!r.IsEmpty()) { everything.Include(r.x0, r.z0); everything.Include(r.x1, r.z1); }
    }
    const std::vector<float> edited = field.Samples();
    CHECK(edited != original);

    const HeightfieldPatch patch = stroke.Finish(field);
    CHECK(!patch.IsEmpty());
    CHECK(patch.index.size() == patch.before.size() && patch.index.size() == patch.after.size());
    for (size_t i = 0; i < patch.index.size(); ++i) {
        CHECK(patch.before[i] == original[static_cast<size_t>(patch.index[i])]);   // "before" is from before the WHOLE stroke
        CHECK(patch.after[i] == edited[static_cast<size_t>(patch.index[i])]);
    }
    // only samples that changed are in the patch
    size_t changedCount = 0;
    for (size_t i = 0; i < original.size(); ++i) if (original[i] != edited[i]) ++changedCount;
    CHECK(patch.index.size() == changedCount);

    const GridRect undone = ApplyPatch(field, patch, false);                       // undo
    CHECK(field.Samples() == original);
    CHECK(!undone.IsEmpty() && undone.x0 >= everything.x0 && undone.x1 <= everything.x1);
    ApplyPatch(field, patch, true);                                                // redo
    CHECK(field.Samples() == edited);

    // a stroke that nets out to nothing leaves an empty patch
    Heightfield other = MakeFlat(0.5f);
    StrokeRecorder netZero;
    brush.mode = BrushMode::Raise; ApplyBrush(other, brush, 0.0f, 0.0f, 0.1f, &netZero);
    brush.mode = BrushMode::Lower; ApplyBrush(other, brush, 0.0f, 0.0f, 0.1f, &netZero);
    CHECK(netZero.Finish(other).IsEmpty());

    // bad indices in a patch are ignored, not trusted
    HeightfieldPatch bad;
    bad.index = { -1, 99999 }; bad.before = { 0.0f, 0.0f }; bad.after = { 1.0f, 1.0f };
    CHECK(ApplyPatch(other, bad, true).IsEmpty());
}

static void TestRect()
{
    GridRect r;
    CHECK(r.IsEmpty());
    r.Include(5, 7); r.Include(3, 9);
    CHECK(!r.IsEmpty() && r.x0 == 3 && r.x1 == 5 && r.z0 == 7 && r.z1 == 9);
    const GridRect g = r.Grown(2, 20, 8);
    CHECK(g.x0 == 1 && g.x1 == 7 && g.z0 == 5 && g.z1 == 8);                       // kept inside the grid
    CHECK(GridRect{}.Grown(3, 20, 20).IsEmpty());
}

int main()
{
    TestWeight();
    TestRaiseLower();
    TestFlatten();
    TestSmooth();
    TestStretchedGrid();
    TestEdges();
    TestStrokeAndPatch();
    TestRect();
    if (g_failures == 0) std::printf("HeightfieldEdit: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
