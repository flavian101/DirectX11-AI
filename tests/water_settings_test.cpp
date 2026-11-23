// Unit tests for src/WaterSettings.h (lessons 35, 43): the sections of the Inspector as data (reset, "modified", presets), the
// master foam control and the wave height scale. There is one kind of water, the ocean: its defaults ARE the ocean.
// No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "WaterSettings.h"
#include <cstdio>
#include <functional>
#include <utility>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

using Group = WaterSettings::Group;
static const Group kAllGroups[] = { Group::Appearance, Group::Waves, Group::Reflection, Group::Refraction, Group::Lighting,
									Group::Foam, Group::Shoreline, Group::Performance, Group::Underwater };

// The defaults are a complete ocean, they survive Clamp() unchanged (Clamp runs on every edit), and sea level is Y = 0.
static void TestDefaults()
{
	const WaterSettings d = WaterSettings::Defaults();
	WaterSettings clamped = d;
	clamped.Clamp();
	for (Group g : kAllGroups) CHECK(!clamped.GroupModified(g, d));
	CHECK(clamped.height == 0.0f && clamped.size == d.size && clamped.sizeZ == d.sizeZ);
	CHECK(d.waveCount == WaterWaves::kMaxWaves);   // an ocean uses every wave the table has
	CHECK(d.shoreWaves == 1.0f && d.swell > 0.0f);   // swells that shoal, break and wash up the beach
	CHECK(d.foamIntensity == 1.0f && d.waveScale == 1.0f);
}

// Defaults are not "modified"; changing ONE setting marks exactly its own section.
static void TestModified()
{
	const WaterSettings defaults = WaterSettings::Defaults();
	for (Group g : kAllGroups) CHECK(!defaults.GroupModified(g, defaults));
	struct Case { Group group; const char* what; std::function<void(WaterSettings&)> change; };
	const Case cases[] = {
		{ Group::Appearance, "colour", [](WaterSettings& s) { s.color.x += 0.2f; } },
		{ Group::Appearance, "alpha", [](WaterSettings& s) { s.alpha = 0.3f; } },
		{ Group::Waves, "height", [](WaterSettings& s) { s.waveScale += 0.3f; } },
		{ Group::Waves, "swell", [](WaterSettings& s) { s.swell = 0.1f; } },
		{ Group::Waves, "seed", [](WaterSettings& s) { s.waveSeed += 7; } },
		{ Group::Reflection, "mode", [](WaterSettings& s) { s.reflectionMode = ReflectionMode::Sky; } },
		{ Group::Refraction, "amount", [](WaterSettings& s) { s.refraction += 0.05f; } },
		{ Group::Lighting, "glint", [](WaterSettings& s) { s.glintStrength = 2.5f; } },
		{ Group::Foam, "master", [](WaterSettings& s) { s.foamIntensity = 0.2f; } },
		{ Group::Foam, "crest", [](WaterSettings& s) { s.crestFoam = 0.9f; } },
		{ Group::Shoreline, "swash", [](WaterSettings& s) { s.swash += 0.5f; } },
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
	noisy.waveScale += 1e-8f;
	CHECK(!noisy.GroupModified(Group::Waves, defaults));
	// Where the ocean is is not in any section: moving it marks nothing.
	WaterSettings moved = defaults;
	moved.height += 10.0f; moved.size = 500.0f; moved.sizeZ = 40.0f;
	for (Group g : kAllGroups) CHECK(!moved.GroupModified(g, defaults));
}

// Resetting a section restores that section only.
static void TestReset()
{
	const WaterSettings defaults = WaterSettings::Defaults();
	WaterSettings s = defaults;
	s.waveScale = 1.5f; s.waveSeed = 42; s.foamIntensity = 0.1f; s.crestFoam = 0.0f; s.color = { 1.0f, 0.0f, 0.0f };
	s.height = 12.0f; s.size = 300.0f;
	s.ResetGroup(Group::Foam, defaults);
	CHECK(s.foamIntensity == defaults.foamIntensity && s.crestFoam == defaults.crestFoam);   // foam is back
	CHECK(s.waveScale == 1.5f && s.waveSeed == 42);                                          // waves are not
	CHECK(s.color.x == 1.0f && s.color.y == 0.0f);                                           // nor the colour
	CHECK(s.height == 12.0f && s.size == 300.0f);                                            // nor the ocean's place
	s.ResetGroup(Group::Waves, defaults);
	s.ResetGroup(Group::Appearance, defaults);
	for (Group g : kAllGroups) CHECK(!s.GroupModified(g, defaults));
	CHECK(s.height == 12.0f && s.size == 300.0f);                                            // still where it was
	// The underwater look resets as a whole.
	WaterSettings under = defaults;
	under.underwater.custom = true; under.underwater.visibility = 3.0f;
	under.ResetGroup(Group::Underwater, defaults);
	CHECK(!under.underwater.custom && under.underwater.visibility == defaults.underwater.visibility);
}

// A preset says how water LOOKS, not where it is.
static void TestPreset()
{
	WaterSettings source = WaterSettings::Defaults();
	source.color = { 0.06f, 0.14f, 0.07f }; source.alpha = 0.92f; source.absorption = 1.2f;
	source.waveScale = 2.0f; source.foamIntensity = 1.7f; source.reflectivity = 0.4f; source.glintStrength = 2.0f;
	source.shoreWaves = 0.2f; source.underwater.custom = true; source.underwater.visibility = 4.0f;
	source.height = 99.0f; source.size = 1500.0f; source.meshDetail = 0;

	WaterSettings target = WaterSettings::Defaults();
	target.height = 3.0f; target.size = 60.0f; target.sizeZ = 400.0f; target.meshDetail = 2;
	target.ApplyPreset(source);

	// It took the look...
	CHECK(target.color.x == source.color.x && target.alpha == source.alpha && target.absorption == source.absorption);
	CHECK(target.waveScale == 2.0f && target.foamIntensity == 1.7f && target.reflectivity == 0.4f && target.glintStrength == 2.0f);
	CHECK(target.shoreWaves == 0.2f && target.underwater.custom && target.underwater.visibility == 4.0f);
	for (Group g : { Group::Appearance, Group::Waves, Group::Reflection, Group::Refraction, Group::Lighting, Group::Foam, Group::Shoreline, Group::Underwater })
		CHECK(!target.GroupModified(g, source));
	// ...and left everything that is the ocean's own.
	CHECK(target.height == 3.0f && target.size == 60.0f && target.sizeZ == 400.0f && target.meshDetail == 2);
}

// The master foam control: clamped, and 1 is the full foam.
static void TestFoamIntensity()
{
	WaterSettings s;
	CHECK(s.foamIntensity == 1.0f);
	s.foamIntensity = 5.0f; s.Clamp();
	CHECK(s.foamIntensity == WaterSettings::kFoamIntensityMax);
	s.foamIntensity = -1.0f; s.Clamp();
	CHECK(s.foamIntensity == 0.0f);
}

// The wave scale only exaggerates the HEIGHT: the wind decides the wavelengths (sea_state_test).
static void TestWaveScale()
{
	WaterSettings s;
	CHECK(s.waveScale == 1.0f);
	s.waveScale = 100.0f; s.Clamp();
	CHECK(s.waveScale == WaterSettings::kWaveScaleMax);
	s.waveScale = 0.0f; s.Clamp();
	CHECK(s.waveScale == WaterSettings::kWaveScaleMin);

	WaterSettings a, b;
	b.waveScale = 2.0f;
	CHECK(b.WaveParams().heightScale == 2.0f && a.WaveParams().heightScale == 1.0f);
	CHECK(b.WaveParams().seed == a.WaveParams().seed && b.WaveParams().count == a.WaveParams().count);   // nothing else moves

	// It belongs to the Waves section: reset, the changed-mark and presets all see it.
	const WaterSettings defaults = WaterSettings::Defaults();
	WaterSettings changed = defaults;
	changed.waveScale = 3.0f;
	for (Group g : kAllGroups) CHECK(changed.GroupModified(g, defaults) == (g == Group::Waves));
	changed.ResetGroup(Group::Waves, defaults);
	CHECK(changed.waveScale == 1.0f);
}

// Every number WaveParams hands to the wave table comes from the settings.
static void TestWaveParams()
{
	WaterSettings s;
	s.directionalSpread = 0.3f; s.choppiness = 0.2f; s.waveCount = 9; s.swell = 0.4f; s.swellAngle = -20.0f; s.swellLength = 3.0f; s.waveGroups = 0.6f; s.waveSeed = 77;
	const WaterWaves::Params p = s.WaveParams();
	CHECK(p.spread == 0.3f && p.choppiness == 0.2f && p.count == 9 && p.swell == 0.4f && p.swellAngle == -20.0f);
	CHECK(p.swellLength == 3.0f && p.groups == 0.6f && p.seed == 77u);
}

int main()
{
	TestDefaults();
	TestModified();
	TestReset();
	TestPreset();
	TestFoamIntensity();
	TestWaveScale();
	TestWaveParams();
	if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
	std::printf("water settings tests: all passed\n");
	return 0;
}
