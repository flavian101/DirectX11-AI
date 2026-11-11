// Unit tests for src/TerrainPaint.{h,cpp}: the paint brush, undo patches, the overlay on the baked splat, and saving.
#include "TerrainPaint.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static std::uint32_t g_rng = 777;
static float Random01() { g_rng = g_rng * 1664525u + 1013904223u; return static_cast<float>(g_rng >> 8) / 16777216.0f; }
static int RandomInt(int lo, int hi) { return lo + static_cast<int>(Random01() * static_cast<float>(hi - lo + 1)) % (hi - lo + 1); }

// 24 x 16 cells of 2 x 2 metres: a 48 x 32 terrain with hills, so the baked rules give a real mix of layers.
static Heightfield MakeHills()
{
    const int cellsX = 24, cellsZ = 16;
    std::vector<float> samples;
    for (int z = 0; z <= cellsZ; ++z)
        for (int x = 0; x <= cellsX; ++x) samples.push_back(0.5f + 0.35f * std::sin(x * 0.45f) * std::cos(z * 0.35f));
    Heightfield field;
    field.Reset({ cellsX, cellsZ, 48.0f, 32.0f, 40.0f }, samples);
    return field;
}

static HeightfieldLayout Layout20() { return { 20, 20, 20.0f, 20.0f, 10.0f }; }   // 1 metre per cell

static float Sum4(const PaintMap& m, int x, int z)
{
    float s = 0.0f;
    for (int c = 0; c < kSplatChannels; ++c) s += m.weights[m.TexelIndex(x, z) * kSplatChannels + c];
    return s;
}
static float At(const PaintMap& m, int x, int z, int c) { return m.weights[m.TexelIndex(x, z) * kSplatChannels + c]; }

static void TestBrush()
{
    PaintMap map; map.Resize(20, 20);
    PaintBrush brush; brush.layer = 2; brush.radius = 4.0f; brush.strength = 0.5f; brush.falloff = 0.0f;
    // local (0.5, 0.5) is the CENTRE of texel (10, 10): distance 0 -> full weight
    const GridRect changed = ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.1f, nullptr);
    const float t = 0.5f * kPaintRatePerSecond * 0.1f;                              // 0.3
    CHECK(std::fabs(At(map, 10, 10, 2) - t) < 1e-6f);
    CHECK(At(map, 10, 10, 0) == 0.0f && At(map, 10, 10, 1) == 0.0f && At(map, 10, 10, 3) == 0.0f);
    CHECK(At(map, 10 + 4, 10, 2) == 0.0f);                                          // at the radius: untouched
    CHECK(At(map, 0, 0, 2) == 0.0f);
    CHECK(!changed.IsEmpty() && changed.x0 >= 6 && changed.x1 <= 14 && changed.z0 >= 6 && changed.z1 <= 14);

    // painting the same layer again closes the gap, never overshoots; sum stays <= 1
    for (int i = 0; i < 200; ++i) ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.1f, nullptr);
    CHECK(At(map, 10, 10, 2) > 0.999f && At(map, 10, 10, 2) <= 1.0f);

    // painting ANOTHER layer takes over: the first fades, the sum never exceeds 1, nothing goes negative
    brush.layer = 1;
    for (int i = 0; i < 300; ++i) {
        ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.05f, nullptr);
        for (int z = 6; z <= 14; ++z)
            for (int x = 6; x <= 14; ++x) {
                CHECK(Sum4(map, x, z) <= 1.0f + 1e-5f);
                for (int c = 0; c < kSplatChannels; ++c) CHECK(At(map, x, z, c) >= 0.0f);
            }
    }
    CHECK(At(map, 10, 10, 1) > 0.99f && At(map, 10, 10, 2) == 0.0f);               // the old layer really went away

    // erasing takes the paint off, down to exactly zero
    brush.erase = true;
    for (int i = 0; i < 300; ++i) ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.1f, nullptr);
    CHECK(Sum4(map, 10, 10) == 0.0f);

    // nothing to erase = nothing changed; a bad layer paints nothing
    CHECK(ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.1f, nullptr).IsEmpty());
    brush.erase = false; brush.layer = 9;
    CHECK(ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.1f, nullptr).IsEmpty());
    brush.layer = 0;
    CHECK(ApplyPaintBrush(map, Layout20(), brush, 500.0f, 500.0f, 0.1f, nullptr).IsEmpty());   // far outside: no crash
    CHECK(ApplyPaintBrush(map, Layout20(), brush, 0.5f, 0.5f, 0.0f, nullptr).IsEmpty());       // no time, no paint
    CHECK(!ApplyPaintBrush(map, Layout20(), brush, -10.0f, -10.0f, 0.1f, nullptr).IsEmpty());  // on the corner: a quarter works
}

// On a stretched grid (cells 2 wide, 1 deep) the brush is still a circle in LOCAL units.
static void TestStretched()
{
    PaintMap map; map.Resize(24, 32);
    const HeightfieldLayout layout{ 24, 32, 48.0f, 32.0f, 10.0f };                   // texels 2 m wide, 1 m deep
    PaintBrush brush; brush.layer = 0; brush.radius = 8.0f; brush.strength = 1.0f; brush.falloff = 0.0f;
    ApplyPaintBrush(map, layout, brush, 1.0f, 0.5f, 0.1f, nullptr);                  // the centre of texel (12, 16)
    CHECK(At(map, 12 + 3, 16, 0) > 0.0f);                                           // 3 texels * 2 m = 6 m: inside
    CHECK(At(map, 12 + 5, 16, 0) == 0.0f);                                          // 10 m: outside
    CHECK(At(map, 12, 16 + 7, 0) > 0.0f);                                           // 7 texels * 1 m = 7 m: inside
    CHECK(At(map, 12, 16 + 9, 0) == 0.0f);                                          // 9 m: outside
}

static void TestStrokePatch()
{
    PaintMap map; map.Resize(20, 20);
    const std::vector<float> original = map.weights;
    StrokeRecorder stroke;
    PaintBrush brush; brush.layer = 3; brush.radius = 5.0f; brush.strength = 0.8f; brush.falloff = 0.5f;
    for (int step = 0; step < 12; ++step) ApplyPaintBrush(map, Layout20(), brush, -4.0f + step * 0.6f, 1.0f, 0.016f, &stroke);
    brush.layer = 1;
    for (int step = 0; step < 12; ++step) ApplyPaintBrush(map, Layout20(), brush, 4.0f - step * 0.6f, -1.0f, 0.016f, &stroke);
    const std::vector<float> edited = map.weights;
    CHECK(edited != original);

    const HeightfieldPatch patch = stroke.Finish(map.weights);
    CHECK(!patch.IsEmpty());
    for (size_t i = 0; i < patch.index.size(); ++i) {
        CHECK(patch.before[i] == original[static_cast<size_t>(patch.index[i])]);     // "before" is from before the whole stroke
        CHECK(patch.after[i] == edited[static_cast<size_t>(patch.index[i])]);
    }
    // The block that undo reports is what the terrain re-uploads to the GPU: it must be EXACTLY the bounding box of the
    // texels that changed (a smaller one leaves stale texels on screen, a bigger one wastes work).
    GridRect expected;
    for (int z = 0; z < map.height; ++z)
        for (int x = 0; x < map.width; ++x)
            for (int c = 0; c < kSplatChannels; ++c)
                if (original[map.TexelIndex(x, z) * kSplatChannels + c] != edited[map.TexelIndex(x, z) * kSplatChannels + c]) expected.Include(x, z);
    const GridRect undone = ApplyPaintPatch(map, patch, false);
    CHECK(map.weights == original && !undone.IsEmpty());
    CHECK(undone.x0 == expected.x0 && undone.x1 == expected.x1 && undone.z0 == expected.z0 && undone.z1 == expected.z1);
    const GridRect redone = ApplyPaintPatch(map, patch, true);
    CHECK(map.weights == edited);
    CHECK(redone.x0 == expected.x0 && redone.x1 == expected.x1 && redone.z0 == expected.z0 && redone.z1 == expected.z1);

    HeightfieldPatch bad; bad.index = { -3, 999999 }; bad.before = { 0.0f, 0.0f }; bad.after = { 1.0f, 1.0f };
    CHECK(ApplyPaintPatch(map, bad, true).IsEmpty());                                // bad indices are ignored, not trusted
}

static void TestOverlay()
{
    const Heightfield field = MakeHills();
    const SplatRules rules; const SplatPlacement placement;
    SplatMap baked; BakeSplatAll(field, rules, placement, baked);
    PaintMap paint; paint.Resize(baked.width, baked.height);

    // no paint: the overlay leaves every byte alone
    SplatMap untouched = baked;
    ApplyPaintAll(paint, untouched);
    CHECK(untouched.rgba == baked.rgba);

    // hand-set weights: fully painted, half painted, and untouched texels side by side
    auto set = [&](int x, int z, float a, float b, float c, float d) {
        float* w = &paint.weights[paint.TexelIndex(x, z) * kSplatChannels];
        w[0] = a; w[1] = b; w[2] = c; w[3] = d;
    };
    set(3, 3, 0, 0, 1, 0);          // fully sand
    set(5, 5, 0, 0.5f, 0, 0);       // half painted rock
    set(7, 7, 0.1f, 0.1f, 0.2f, 0.1f);
    SplatMap result = baked;
    ApplyPaintAll(paint, result);
    auto texel = [&](const SplatMap& m, int x, int z) { return &m.rgba[(static_cast<size_t>(z) * m.width + x) * kSplatChannels]; };
    CHECK(texel(result, 3, 3)[2] == 255 && texel(result, 3, 3)[0] == 0 && texel(result, 3, 3)[1] == 0 && texel(result, 3, 3)[3] == 0);
    const std::uint8_t* b = texel(baked, 5, 5);
    const std::uint8_t* r = texel(result, 5, 5);                                     // baked * 0.5 + 0.5 on the rock channel
    CHECK(std::abs(r[0] - static_cast<int>(b[0] * 0.5f + 0.5f)) <= 1 && std::abs(r[1] - static_cast<int>(b[1] * 0.5f + 127.5f)) <= 1);
    CHECK(texel(result, 0, 0)[0] == texel(baked, 0, 0)[0]);                          // far from the paint: as baked
    for (int z = 0; z < result.height; ++z)                                          // EVERY texel still adds up to exactly 255
        for (int x = 0; x < result.width; ++x) {
            const std::uint8_t* p = texel(result, x, z);
            CHECK(p[0] + p[1] + p[2] + p[3] == 255);
        }

    // wrong-size paint is ignored, not trusted
    PaintMap wrong; wrong.Resize(5, 5); wrong.weights[0] = 1.0f;
    SplatMap copy = baked;
    ApplyPaintAll(wrong, copy);
    CHECK(copy.rgba == baked.rgba);
}

// THE IMPORTANT ONE. The live splat is updated block by block (a brush step, a sculpt step, a slider). After any mix of
// those it must equal "bake everything, then lay all the paint over it" byte for byte. If a re-bake ever forgot to put
// the paint back, painted ground would silently turn back into rules.
static void TestIncrementalEqualsFull(bool skipOverlayAfterSculpt)
{
    Heightfield field = MakeHills();
    const SplatRules rules; const SplatPlacement placement;
    SplatMap live; BakeSplatAll(field, rules, placement, live);
    PaintMap paint; paint.Resize(live.width, live.height);
    const HeightfieldLayout layout = field.GetLayout();

    for (int step = 0; step < 150; ++step) {
        if (step % 3 != 2) {                                   // a paint step: overlay only the block the brush changed
            PaintBrush brush; brush.layer = RandomInt(0, 3); brush.erase = (RandomInt(0, 5) == 0);
            brush.radius = 2.0f + Random01() * 10.0f; brush.strength = 0.3f + Random01() * 0.7f; brush.falloff = Random01();
            const GridRect changed = ApplyPaintBrush(paint, layout, brush, (Random01() - 0.5f) * 56.0f, (Random01() - 0.5f) * 38.0f, 0.1f, nullptr);
            if (changed.IsEmpty()) continue;
            BakeSplat(field, rules, placement, changed, live);   // what Terrain::RefreshSplat does: bake the block...
            ApplyPaint(paint, changed, live);                    // ...then lay the paint over it
        } else {                                               // a sculpt step: heights change, the block is re-baked
            BrushSettings height; height.mode = static_cast<BrushMode>(RandomInt(0, 3)); height.radius = 3.0f + Random01() * 8.0f;
            height.strength = 0.5f; height.falloff = 0.5f; height.targetHeight = Random01();
            const GridRect samples = ApplyBrush(field, height, (Random01() - 0.5f) * 48.0f, (Random01() - 0.5f) * 32.0f, 0.2f, nullptr);
            const GridRect cells = SplatCellsForSamples(samples, layout.cellsX, layout.cellsZ);
            if (cells.IsEmpty()) continue;
            BakeSplat(field, rules, placement, cells, live);
            if (!skipOverlayAfterSculpt) ApplyPaint(paint, cells, live);
        }
    }
    SplatMap full; BakeSplatAll(field, rules, placement, full);
    ApplyPaintAll(paint, full);
    if (skipOverlayAfterSculpt) {
        CHECK(live.rgba != full.rgba);                         // the mutation: forgetting the overlay IS detected
    } else {
        CHECK(live.rgba == full.rgba);
    }
}

static void TestErasingRestoresRules()
{
    const Heightfield field = MakeHills();
    const SplatRules rules; const SplatPlacement placement;
    SplatMap baked; BakeSplatAll(field, rules, placement, baked);
    SplatMap live = baked;
    PaintMap paint; paint.Resize(baked.width, baked.height);
    PaintBrush brush; brush.layer = 2; brush.radius = 9.0f; brush.strength = 1.0f; brush.falloff = 0.3f;
    for (int i = 0; i < 40; ++i) { const GridRect c = ApplyPaintBrush(paint, field.GetLayout(), brush, 0.0f, 0.0f, 0.1f, nullptr); BakeSplat(field, rules, placement, c, live); ApplyPaint(paint, c, live); }
    CHECK(live.rgba != baked.rgba);                            // painting really changed the ground
    brush.erase = true;
    for (int i = 0; i < 400; ++i) { const GridRect c = ApplyPaintBrush(paint, field.GetLayout(), brush, 0.0f, 0.0f, 0.1f, nullptr); if (c.IsEmpty()) break; BakeSplat(field, rules, placement, c, live); ApplyPaint(paint, c, live); }
    CHECK(live.rgba == baked.rgba);                            // ...and erasing brings the rules back EXACTLY
}

static void TestSaving()
{
    PaintMap paint; paint.Resize(7, 5);
    for (size_t i = 0; i < paint.weights.size(); ++i) paint.weights[i] = (i % 4 == 1) ? Random01() * 0.25f : 0.0f;
    const std::vector<std::uint8_t> bytes = QuantisePaint(paint);
    CHECK(bytes.size() == paint.weights.size());
    PaintMap loaded;
    PaintFromBytes(bytes.data(), 7, 5, loaded);
    CHECK(loaded.width == 7 && loaded.height == 5 && loaded.weights.size() == paint.weights.size());
    for (size_t i = 0; i < paint.weights.size(); ++i) CHECK(std::fabs(loaded.weights[i] - paint.weights[i]) <= 0.5f / 255.0f + 1e-6f);

    // bytes that add up to more than 255 (rounding) are scaled back: coverage never exceeds 1
    const std::uint8_t heavy[4] = { 200, 200, 0, 0 };
    PaintFromBytes(heavy, 1, 1, loaded);
    CHECK(std::fabs(Sum4(loaded, 0, 0) - 1.0f) < 1e-5f);
    CHECK(QuantisePaint(PaintMap{}).empty());
}

int main()
{
    TestBrush();
    TestStretched();
    TestStrokePatch();
    TestOverlay();
    TestIncrementalEqualsFull(false);
    TestIncrementalEqualsFull(true);
    TestErasingRestoresRules();
    TestSaving();
    if (g_failures == 0) std::printf("TerrainPaint: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
