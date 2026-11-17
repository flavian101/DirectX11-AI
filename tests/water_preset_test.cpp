// Unit tests for src/WaterPreset.h (lesson 43): a water preset file holds a LOOK, round-trips exactly, refuses what is not a
// preset, and leaves the body's place and river out. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
#include "SceneJson.h"   // how a WaterSettings becomes JSON (defined once, for scene files and presets alike)
#include "WaterPreset.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

namespace fs = std::filesystem;
using Group = WaterSettings::Group;
static const Group kLook[] = { Group::Appearance, Group::Waves, Group::Reflection, Group::Refraction, Group::Lighting, Group::Foam, Group::Shoreline, Group::Underwater };

static WaterSettings Busy()
{
	WaterSettings s = WaterSettings::Defaults(WaterBodyType::Ocean);
	s.ApplyLook(WaterLook::Arctic);
	s.waveScale = 2.25f; s.autoWaveScale = false;
	s.waveAmplitude = 0.77f; s.waveSeed = 31; s.windDirection = 123.0f; s.detailNormal = 1.3f;
	s.reflectivity = 0.6f; s.reflectionMode = ReflectionMode::PlanarFull; s.refraction = 0.09f;
	s.glintStrength = 2.2f; s.glintSharpness = 300.0f; s.scatter = 1.1f;
	s.foamIntensity = 1.6f; s.crestFoam = 0.35f; s.foamTrail = 3.0f;
	s.shoreWaves = 0.25f; s.swash = 1.1f; s.fadeDistance = 6.0f;
	s.underwater.custom = true; s.underwater.visibility = 9.5f; s.underwater.deepColor = { 0.0f, 0.1f, 0.3f };
	s.height = 17.0f; s.size = 777.0f; s.sizeZ = 55.0f; s.shape = WaterShape::Ellipse;
	s.flowSpeed = 6.0f; s.meshDetail = 0; s.riverPath = { { 0, 0, 8 }, { 10, 4, 8, -2.0f } };
	return s;
}

static void TestJson()
{
	const nlohmann::json j = WaterPreset::ToJson(Busy());
	CHECK(j["kind"] == "water_preset");
	const nlohmann::json& water = j["water"];
	for (const char* key : WaterPreset::NotPartOfALook()) CHECK(!water.contains(key));   // not the place, not the river, not the current
	// ...stated here on its own, so the test does not trust the list it is testing:
	for (const char* key : { "bodyType", "height", "size", "sizeZ", "shape", "riverPath", "flowSpeed", "meshDetail" }) CHECK(!water.contains(key));
	for (const char* key : { "color", "shallowColor", "alpha", "waveAmplitude", "foamIntensity", "reflectionMode", "underwater", "swash" }) CHECK(water.contains(key));

	WaterSettings back;
	std::string error;
	CHECK(WaterPreset::FromJson(j, back, error));
	for (Group g : kLook) CHECK(!back.GroupModified(g, Busy()));   // the look came back exactly
	// Through text, as a file would carry it.
	WaterSettings viaText;
	CHECK(WaterPreset::FromJson(nlohmann::json::parse(j.dump(2)), viaText, error));
	for (Group g : kLook) CHECK(!viaText.GroupModified(g, Busy()));
	// Applying it to another body takes the look and leaves the body alone.
	WaterSettings river = WaterSettings::Defaults(WaterBodyType::River);
	river.height = 3.0f; river.riverPath = { { 0, 0, 6 }, { 0, 20, 6 } };
	river.ApplyPreset(back);
	CHECK(river.foamIntensity == 1.6f && river.waveSeed == 31 && river.height == 3.0f && river.riverPath.size() == 2u && river.bodyType == WaterBodyType::River);
}

static void TestRefusals()
{
	WaterSettings keep = WaterSettings::Defaults(WaterBodyType::Lake);
	keep.waveAmplitude = 0.123f;
	WaterSettings out = keep;
	std::string error;
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"scene","water":{}})"), out, error) && !error.empty());
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"([1,2,3])"), out, error));
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":99,"water":{}})"), out, error));
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":1})"), out, error));              // no "water"
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":1,"water":{"alpha":"x"}})"), out, error));   // wrong type
	CHECK(out.waveAmplitude == 0.123f);   // a refused preset leaves the target untouched
	// A hand-edited value out of range is clamped, not handed to the renderer.
	CHECK(WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":1,"water":{"waveAmplitude":999,"foamIntensity":-4}})"), out, error));
	CHECK(out.waveAmplitude == WaterSettings::kAmplitudeMax && out.foamIntensity == 0.0f);
}

static void TestFiles()
{
	const fs::path folder = fs::temp_directory_path() / "water_preset_test_folder";
	std::error_code ec;
	fs::remove_all(folder, ec);
	CHECK(WaterPreset::List(folder).empty());   // a folder that does not exist has no presets

	std::string error;
	CHECK(WaterPreset::Save(Busy(), folder / ("Stormy Sea" + std::string(WaterPreset::kExtension)), error));   // creates the folder
	CHECK(WaterPreset::Save(WaterSettings::Defaults(WaterBodyType::Lake), folder / ("alpine lake" + std::string(WaterPreset::kExtension)), error));
	CHECK(WaterPreset::Save(Busy(), folder / ("Stormy Sea" + std::string(WaterPreset::kExtension)), error));   // overwriting is fine
	{ std::ofstream other(folder / "notes.txt"); other << "not a preset"; }
	const std::vector<fs::path> found = WaterPreset::List(folder);
	CHECK(found.size() == 2u);                                                       // the .txt is not a preset
	CHECK(found.size() == 2u && found[0].stem() == "alpine lake" && found[1].stem() == "Stormy Sea");   // by name, ignoring case
	CHECK(!fs::exists(folder / ("Stormy Sea" + std::string(WaterPreset::kExtension) + ".tmp")));          // no leftovers

	WaterSettings loaded;
	CHECK(WaterPreset::Load(found[1], loaded, error));
	for (Group g : kLook) CHECK(!loaded.GroupModified(g, Busy()));
	CHECK(!WaterPreset::Load(folder / "missing.waterpreset", loaded, error) && !error.empty());
	{ std::ofstream bad(folder / "broken.waterpreset"); bad << "{ this is not json"; }
	CHECK(!WaterPreset::Load(folder / "broken.waterpreset", loaded, error));
	fs::remove_all(folder, ec);
}

static void TestNames()
{
	CHECK(WaterPreset::FileNameFor("My Sea") == "My Sea");
	CHECK(WaterPreset::FileNameFor("  padded  ") == "padded");
	CHECK(WaterPreset::FileNameFor("a:b*c?") == "a_b_c_");
	CHECK(WaterPreset::FileNameFor("../evil/x").find('/') == std::string::npos);   // never a path
	CHECK(WaterPreset::FileNameFor("..\\evil").find('\\') == std::string::npos);
	CHECK(WaterPreset::FileNameFor("").empty() && WaterPreset::FileNameFor("   ").empty() && WaterPreset::FileNameFor("///").empty());
	CHECK(WaterPreset::FileNameFor(std::string(200, 'x')).size() == 64u);
}

// Scene files saved before the master foam control have no "foamIntensity": they load with 1, the foam they always had.
static void TestOldScenes()
{
	nlohmann::json old = nlohmann::json(WaterSettings::Defaults(WaterBodyType::Lake));
	CHECK(old.contains("foamIntensity"));       // new scenes write it
	old.erase("foamIntensity");
	CHECK(old.get<WaterSettings>().foamIntensity == 1.0f);
	WaterSettings custom = WaterSettings::Defaults(WaterBodyType::Custom);
	CHECK(nlohmann::json(custom).get<WaterSettings>().bodyType == WaterBodyType::Custom);   // and a Custom body is still Custom after a save
	// Scenes saved before the wave scale (lesson 44) have neither key: a scale of 1, and the automatic scale ON.
	nlohmann::json older = nlohmann::json(WaterSettings::Defaults(WaterBodyType::Ocean));
	CHECK(older.contains("waveScale") && older.contains("autoWaveScale"));   // new scenes write them
	older.erase("waveScale"); older.erase("autoWaveScale");
	const WaterSettings loadedOlder = older.get<WaterSettings>();
	CHECK(loadedOlder.waveScale == 1.0f && loadedOlder.autoWaveScale);
}

int main()
{
	TestJson();
	TestRefusals();
	TestFiles();
	TestNames();
	TestOldScenes();
	if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
	std::printf("water preset tests: all passed\n");
	return 0;
}
