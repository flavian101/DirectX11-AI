// Tests for src/LodSelect.h: the per-frame culling and level-of-detail decisions. No Direct3D, no framework.
//   1. ScreenFraction shrinks with distance and grows with size; a bigger projScale (narrower lens) makes things bigger.
//   2. SelectLod picks the right level, and with a previous level it needs a margin to change (no flicker).
//   3. A sweep from near to far never goes back to a finer level; a jittering camera on a boundary never flickers.
//   4. ParseLodIndex reads "Pine_LOD2" style names and ignores look-alikes.
//   5. AppendRun merges ranges that touch (one draw call for a row of neighbouring cells).
#include "LodSelect.h"
#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static void TestScreenFraction()
{
    const float near10 = ScreenFraction(5.0f, 10.0f, 1.73f), far100 = ScreenFraction(5.0f, 100.0f, 1.73f);
    CHECK(std::fabs(near10 - 0.865f) < 1e-3f);
    CHECK(std::fabs(far100 * 10.0f - near10) < 1e-4f);                       // ten times farther: ten times smaller
    CHECK(ScreenFraction(10.0f, 50.0f, 1.73f) > ScreenFraction(1.0f, 50.0f, 1.73f));
    CHECK(ScreenFraction(5.0f, 50.0f, 3.0f) > ScreenFraction(5.0f, 50.0f, 1.0f));   // zoomed in
    CHECK(std::isfinite(ScreenFraction(5.0f, 0.0f, 1.73f)));                  // standing inside the bounding sphere
    CHECK(std::fabs(DistanceForScreenFraction(5.0f, 0.1f, 1.73f) - 86.5f) < 1e-2f);
    CHECK(DistanceForScreenFraction(5.0f, 0.0f, 1.73f) > 1e20f);
}

static void TestSelect()
{
    const float t[3] = { 0.20f, 0.08f, 0.03f };
    CHECK(SelectLod(0.50f, t, 4) == 0);
    CHECK(SelectLod(0.20f, t, 4) == 0);      // exactly on a threshold: still the finer level
    CHECK(SelectLod(0.15f, t, 4) == 1);
    CHECK(SelectLod(0.05f, t, 4) == 2);
    CHECK(SelectLod(0.01f, t, 4) == 3);
    CHECK(SelectLod(0.01f, t, 2) == 1);      // only two levels: never beyond level 1
    CHECK(SelectLod(0.01f, t, 1) == 0);
    CHECK(SelectLod(0.01f, t, 0) == 0);

    // With history: 0.19 is just under the first threshold, but level 0 stays until it is clearly past (10%).
    CHECK(SelectLod(0.19f, t, 4, 0) == 0);
    CHECK(SelectLod(0.17f, t, 4, 0) == 1);
    // and coming back needs to be clearly above it.
    CHECK(SelectLod(0.21f, t, 4, 1) == 1);
    CHECK(SelectLod(0.23f, t, 4, 1) == 0);
    // A big jump skips levels in one go, in both directions.
    CHECK(SelectLod(0.001f, t, 4, 0) == 3);
    CHECK(SelectLod(0.9f, t, 4, 3) == 0);
    // A stale level number (more levels than now) is ignored.
    CHECK(SelectLod(0.15f, t, 2, 5) == 1);
    CHECK(BelowCullSize(0.001f, 0.004f) && !BelowCullSize(0.01f, 0.004f) && !BelowCullSize(0.0f, 0.0f));
}

static void TestSweepAndJitter()
{
    const float t[3] = { 0.20f, 0.08f, 0.03f };
    int level = -1, previous = 0; bool monotonic = true; int changes = 0;
    for (float distance = 5.0f; distance < 600.0f; distance += 0.25f) {   // walking away from a 6 m object
        level = SelectLod(ScreenFraction(6.0f, distance, 1.73f), t, 4, level);
        if (level < previous) monotonic = false;
        if (level != previous) ++changes;
        previous = level;
    }
    CHECK(monotonic);
    CHECK(changes == 3);
    CHECK(level == 3);

    // Standing on the first boundary and jittering by +-4% never changes the level.
    const float boundary = 6.0f * 1.73f / 0.20f;   // the distance where the object covers exactly 0.20
    level = SelectLod(ScreenFraction(6.0f, boundary * 0.97f, 1.73f), t, 4, -1);
    const int start = level;
    int flips = 0;
    for (int frame = 0; frame < 200; ++frame) {
        const float jitter = 1.0f + 0.04f * ((frame % 2) ? 1.0f : -1.0f);
        const int next = SelectLod(ScreenFraction(6.0f, boundary * jitter, 1.73f), t, 4, level);
        if (next != level) ++flips;
        level = next;
    }
    CHECK(flips == 0 && level == start);
}

static void TestParse()
{
    CHECK(ParseLodIndex("Pine_LOD0") == 0);
    CHECK(ParseLodIndex("Pine_LOD2") == 2);
    CHECK(ParseLodIndex("pine.lod_3") == 3);
    CHECK(ParseLodIndex("LOD1_trunk") == 1);
    CHECK(ParseLodIndex("Tree LOD 2") == 2);              // one separator is allowed between the tag and the digits
    CHECK(ParseLodIndex("Tree LOD  2") == -1);            // two are not
    CHECK(ParseLodIndex("Tree LOD2") == 2);
    CHECK(ParseLodIndex("Pine-lod-1") == 1);
    CHECK(ParseLodIndex("Pine_LOD1_Trunk_LOD2") == 2);   // the last tag wins
    CHECK(ParseLodIndex("Pine_LOD12") == 12);
    CHECK(ParseLodIndex("Pine") == -1);
    CHECK(ParseLodIndex("") == -1);
    CHECK(ParseLodIndex("Cold1") == -1);                  // glued to letters: not a tag
    CHECK(ParseLodIndex("Unfolded2") == -1);
    CHECK(ParseLodIndex("Pine_LOD") == -1);               // no number
    CHECK(ParseLodIndex("lod") == -1);
    CHECK(ParseLodIndex("Mesh_7_LOD0") == 0);
}

static void TestRuns()
{
    std::vector<DrawRun> runs;
    AppendRun(runs, 0, 10);
    AppendRun(runs, 10, 5);     // touches: merged
    AppendRun(runs, 15, 0);     // empty: ignored
    AppendRun(runs, 20, 7);     // a gap: a new run
    AppendRun(runs, 27, 3);     // touches again
    CHECK(runs.size() == 2);
    CHECK(runs[0].start == 0 && runs[0].count == 15);
    CHECK(runs[1].start == 20 && runs[1].count == 10);
    std::vector<DrawRun> none;
    AppendRun(none, 5, 0);
    CHECK(none.empty());
}

int main()
{
    TestScreenFraction();
    TestSelect();
    TestSweepAndJitter();
    TestParse();
    TestRuns();
    if (g_failures == 0) std::printf("LodSelectTests: all passed\n");
    else std::printf("LodSelectTests: %d FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
