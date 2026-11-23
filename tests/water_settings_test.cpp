// Unit tests for src/WaterSettings.h (lessons 35, 43): the body types, the sections of the Inspector as data (reset, "modified",
// presets), and the master foam control. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "WaterMesh.h"
#include "WaterSettings.h"
#include <cstdio>
#include <functional>
#include <utility>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using Group = WaterSettings::Group;
static const Group kAllGroups[] = { Group::Appearance, Group::Waves, Group::Reflection, Group::Refraction, Group::Lighting,
									Group::Foam, Group::Shoreline, Group::Current, Group::Performance, Group::Underwater };
static const WaterBodyType kAllTypes[] = { WaterBodyType::Ocean, WaterBodyType::Lake, WaterBodyType::River, WaterBodyType::Pond, WaterBodyType::Custom };

// Every body type survives Clamp(), which runs on every edit. (It once clamped the type to 0..3: a Custom body became a Pond.)
static void TestBodyTypes()
{
	for (WaterBodyType type : kAllTypes) {
		WaterSettings s = WaterSettings::Defaults(type);
		CHECK(s.bodyType == type);
		s.Clamp();
		CHECK(s.bodyType == type);
	}
	WaterSettings wild;
	wild.bodyType = static_cast<WaterBodyType>(99);
	wild.Clamp();
	CHECK(wild.bodyType == WaterBodyType::Custom);   // nonsense lands on the last real type, not outside the list
}

// Defaults are not "modified"; changing ONE setting marks exactly its own section.
static void TestModified()
{
	for (WaterBodyType type : kAllTypes) {
		const WaterSettings defaults = WaterSettings::Defaults(type);
		for (Group g : kAllGroups) CHECK(!defaults.GroupModified(g, defaults));
	}
	const WaterSettings defaults = WaterSettings::Defaults(WaterBodyType::Lake);
	struct Case { Group group; const char* what; std::function<void(WaterSettings&)> change; };
	const Case cases[] = {
		{ Group::Appearance, "colour", [](WaterSettings& s) { s.color.x += 0.2f; } },
		{ Group::Appearance, "alpha", [](WaterSettings& s) { s.alpha = 0.3f; } },
		{ Group::Waves, "amplitude", [](WaterSettings& s) { s.waveAmplitude += 0.3f; } },
		{ Group::Waves, "style", [](WaterSettings& s) { s.waveStyle = WaterWaves::Style::Classic; } },
		{ Group::Waves, "seed", [](WaterSettings& s) { s.waveSeed += 7; } },
		{ Group::Reflection, "mode", [](WaterSettings& s) { s.reflectionMode = ReflectionMode::Sky; } },
		{ Group::Refraction, "amount", [](WaterSettings& s) { s.refraction += 0.05f; } },
		{ Group::Lighting, "glint", [](WaterSettings& s) { s.glintStrength = 2.5f; } },
		{ Group::Foam, "master", [](WaterSettings& s) { s.foamIntensity = 0.2f; } },
		{ Group::Foam, "crest", [](WaterSettings& s) { s.crestFoam = 0.9f; } },
		{ Group::Shoreline, "swash", [](WaterSettings& s) { s.swash += 0.5f; } },
		{ Group::Current, "speed", [](WaterSettings& s) { s.flowSpeed += 3.0f; } },
		{ Group::Performance, "culling", [](WaterSettings& s) { s.frustumCulling = !s.frustumCulling; } },
		{ Group::Underwater, "visibility", [](WaterSettings& s) { s.underwater.visibility += 5.0f; } },
		{ Group::Underwater, "colour", [](WaterSettings& s) { s.underwater.deepColor[1] += 0.1f; } },
	};
	for (const Case& c : cases) {
		WaterSettings s = defaults;
		c.change(s);
		for (Group g : kAllGroups) CHECK(s.GroupModified(g, defaults) == (g == c.group));   // that section, and no other
	}
	// Float noise below 1e-6 is not a change; a real one is.
	WaterSettings noisy = defaults;
	noisy.waveAmplitude += 1e-8f;
	CHECK(!noisy.GroupModified(Group::Waves, defaults));
	// Where the body is and what it is called are not in any section: moving it marks nothing.
	WaterSettings moved = defaults;
	moved.height += 10.0f; moved.size = 500.0f; moved.sizeZ = 40.0f; moved.shape = WaterShape::Ellipse;
	moved.riverPath = { { 0, 0, 8 }, { 10, 0, 8 } }; moved.riverDepth += 1.0f;
	for (Group g : kAllGroups) CHECK(!moved.GroupModified(g, defaults));
}

// Resetting a section restores that section only.
static void TestReset()
{
	const WaterSettings defaults = WaterSettings::Defaults(WaterBodyType::Ocean);
	WaterSettings s = defaults;
	s.waveAmplitude = 1.5f; s.waveSeed = 42; s.foamIntensity = 0.1f; s.crestFoam = 0.0f; s.color = { 1.0f, 0.0f, 0.0f };
	s.height = 12.0f; s.size = 300.0f; s.riverPath = { { 0, 0, 8 }, { 10, 0, 8 } };
	s.ResetGroup(Group::Foam, defaults);
	CHECK(s.foamIntensity == defaults.foamIntensity && s.crestFoam == defaults.crestFoam);   // foam is back
	CHECK(s.waveAmplitude == 1.5f && s.waveSeed == 42);                                      // waves are not
	CHECK(s.color.x == 1.0f && s.color.y == 0.0f);                                           // nor the colour
	CHECK(s.height == 12.0f && s.size == 300.0f && s.riverPath.size() == 2u);                // nor the body's place or river
	s.ResetGroup(Group::Waves, defaults);
	s.ResetGroup(Group::Appearance, defaults);
	for (Group g : kAllGroups) CHECK(!s.GroupModified(g, defaults));
	CHECK(s.height == 12.0f && s.size == 300.0f && s.riverPath.size() == 2u);                // still where it was
	// A river's section does not touch the river's shape.
	WaterSettings river = WaterSettings::Defaults(WaterBodyType::River);
	river.riverPath = { { 0, 0, 8 }, { 10, 0, 8 }, { 20, 5, 8 } };
	river.flowSpeed = 9.0f;
	river.ResetGroup(Group::Current, WaterSettings::Defaults(WaterBodyType::River));
	CHECK(river.flowSpeed == WaterSettings::Defaults(WaterBodyType::River).flowSpeed && river.riverPath.size() == 3u);
	// The underwater look resets as a whole.
	WaterSettings under = defaults;
	under.underwater.custom = true; under.underwater.visibility = 3.0f;
	under.ResetGroup(Group::Underwater, defaults);
	CHECK(!under.underwater.custom && under.underwater.visibility == defaults.underwater.visibility);
}

// A preset says how water LOOKS, not where it is or what kind of current it has.
static void TestPreset()
{
	WaterSettings source = WaterSettings::Defaults(WaterBodyType::Ocean);
	source.ApplyLook(WaterLook::Swamp);
	source.waveAmplitude = 0.9f; source.foamIntensity = 1.7f; source.reflectivity = 0.4f; source.glintStrength = 2.0f;
	source.shoreWaves = 0.2f; source.underwater.custom = true; source.underwater.visibility = 4.0f;
	source.height = 99.0f; source.size = 1500.0f; source.shape = WaterShape::Ellipse; source.flowSpeed = 7.0f;
	source.meshDetail = 0; source.riverPath = { { 0, 0, 8 }, { 5, 5, 8 } };

	WaterSettings target = WaterSettings::Defaults(WaterBodyType::River);
	target.height = 3.0f; target.size = 60.0f; target.sizeZ = 400.0f; target.flowSpeed = 2.5f; target.meshDetail = 2;
	target.riverPath = { { 0, 0, 6 }, { 0, 30, 6 }, { 5, 60, 6 } };
	target.ApplyPreset(source);

	// It took the look...
	CHECK(target.color.x == source.color.x && target.alpha == source.alpha && target.absorption == source.absorption);
	CHECK(target.waveAmplitude == 0.9f && target.foamIntensity == 1.7f && target.reflectivity == 0.4f && target.glintStrength == 2.0f);
	CHECK(target.shoreWaves == 0.2f && target.underwater.custom && target.underwater.visibility == 4.0f);
	for (Group g : { Group::Appearance, Group::Waves, Group::Reflection, Group::Refraction, Group::Lighting, Group::Foam, Group::Shoreline, Group::Underwater })
		CHECK(!target.GroupModified(g, source));
	// ...and left everything that is the body's own.
	CHECK(target.height == 3.0f && target.size == 60.0f && target.sizeZ == 400.0f && target.shape == WaterShape::Rectangle);
	CHECK(target.bodyType == WaterBodyType::River && target.flowSpeed == 2.5f && target.meshDetail == 2);
	CHECK(target.riverPath.size() == 3u && target.riverPath[2].z == 60.0f);
}

// The master foam control: clamped, and 1 is the old behaviour.
static void TestFoamIntensity()
{
	WaterSettings s;
	CHECK(s.foamIntensity == 1.0f);
	s.foamIntensity = 5.0f; s.Clamp();
	CHECK(s.foamIntensity == WaterSettings::kFoamIntensityMax);
	s.foamIntensity = -1.0f; s.Clamp();
	CHECK(s.foamIntensity == 0.0f);
	for (WaterBodyType type : kAllTypes) CHECK(WaterSettings::Defaults(type).foamIntensity == 1.0f);   // no body type changes the look of existing scenes
}

// Lesson 44: the wave scale, and the reported bug: a big body's waves flatten out.
static void TestWaveScale()
{
	WaterSettings s;
	CHECK(s.waveScale == 1.0f && s.autoWaveScale);   // the defaults change nothing for a small body, and protect a big one
	s.waveScale = 100.0f; s.Clamp();
	CHECK(s.waveScale == WaterSettings::kWaveScaleMax);
	s.waveScale = 0.0f; s.Clamp();
	CHECK(s.waveScale == WaterSettings::kWaveScaleMin);

	// The author's Scale is part of the artist's (Spectrum) sea: it reaches the wave parameters (lengths and heights together).
	// (The Sea style is not scaled: its Scale only exaggerates the height, and the wind decides the lengths. See sea_state_test.)
	WaterSettings a = WaterSettings::Defaults(WaterBodyType::Ocean), b;
	a.waveStyle = WaterWaves::Style::Spectrum;
	b = a;
	b.waveScale = 2.0f;
	CHECK(std::abs(b.WaveParams().amplitude - 2.0f * a.WaveParams().amplitude) < 1e-5f);
	CHECK(std::abs(b.WaveParams().frequency * 2.0f - a.WaveParams().frequency) < 1e-5f);
	CHECK(b.WaveParams().seed == a.WaveParams().seed && b.WaveParams().count == a.WaveParams().count);   // nothing else moves

	// Both belong to the Waves section: reset, the changed-mark and presets all see them.
	const WaterSettings defaults = WaterSettings::Defaults(WaterBodyType::Lake);
	WaterSettings changed = defaults;
	changed.waveScale = 3.0f;
	for (Group g : kAllGroups) CHECK(changed.GroupModified(g, defaults) == (g == Group::Waves));
	changed = defaults;
	changed.autoWaveScale = false;
	for (Group g : kAllGroups) CHECK(changed.GroupModified(g, defaults) == (g == Group::Waves));
	changed.ResetGroup(Group::Waves, defaults);
	CHECK(changed.autoWaveScale && changed.waveScale == 1.0f);
	WaterSettings target = WaterSettings::Defaults(WaterBodyType::Ocean);
	WaterSettings source = target;
	source.waveScale = 2.5f; source.autoWaveScale = false;
	target.ApplyPreset(source);
	CHECK(target.waveScale == 2.5f && !target.autoWaveScale);

	// The report: with the Stormy preset, a water body fit to a big terrain went flat. On the real grid the engine builds, count
	// what the mesh keeps of the sea near the camera, without and with the automatic scale. (Calm, Choppy, Stormy.)
	const int cells = 32;   // the default mesh detail
	for (WavePreset preset : { WavePreset::Calm, WavePreset::Choppy, WavePreset::Stormy }) {
		WaterSettings sea = WaterSettings::Defaults(WaterBodyType::Ocean);
		sea.waveStyle = WaterWaves::Style::Spectrum;   // the artist's model: it is the one that needs a scale to fit a big mesh
		sea.waveCount = 7;                             // (what the Ocean had before the Sea style: sixteen waves reach much finer than a mesh shows)
		sea.ApplyWavePreset(preset);
		const float targetCell = std::clamp(6.2831853f / (3.0f * 2.5f * std::max(sea.waveFrequency, 0.05f)), 0.5f, 2.0f);   // as Water::EnsureMesh
		float kept[4];
		int n = 0;
		for (float size : { 100.0f, 500.0f, 1000.0f, 2000.0f }) {
			const WaterMesh::TileGrid grid = WaterMesh::ChooseTileGrid(size, size, -1, cells, targetCell);
			const float cell = size / static_cast<float>(grid.x * cells);
			const WaterWaves::Params params = sea.WaveParams();
			const float scale = WaterWaves::ResolvableScale(params, cell);
			const float without = WaterWaves::ShownShare(WaterWaves::Build(params), cell);
			const float with = WaterWaves::ShownShare(WaterWaves::Build(WaterWaves::ScaledBy(params, scale)), cell);
			CHECK(with >= WaterWaves::kShownShareWanted - 1e-3f);          // with it, the sea shows at every size
			CHECK(with >= without - 1e-4f);                                 // and it never makes anything worse
			kept[n++] = without;
		}
		CHECK(kept[0] > 0.95f);                                             // a small body was never the problem
		CHECK(kept[3] < kept[0] - 0.3f);                                    // a body of the maximum size lost a lot of its sea: the bug
	}
}

int main()
{
	TestBodyTypes();
	TestModified();
	TestReset();
	TestPreset();
	TestFoamIntensity();
	TestWaveScale();
	if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
	std::printf("water settings tests: all passed\n");
	return 0;
}
