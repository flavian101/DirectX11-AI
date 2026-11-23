// Unit tests for src/WaterPreset.h (lesson 43): a water preset file holds a LOOK, round-trips exactly, refuses what is not a
// preset, and leaves the ocean's place out. No Direct3D, no framework: a failed CHECK prints and the program exits 1.
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
	WaterSettings s = WaterSettings::Defaults();
	s.color = { 0.05f, 0.30f, 0.38f }; s.shallowColor = { 0.35f, 0.70f, 0.75f }; s.alpha = 0.7f; s.absorption = 0.9f;
	s.waveScale = 2.25f; s.waveSeed = 31; s.swellAngle = 41.0f; s.detailNormal = 1.3f;
	s.reflectivity = 0.6f; s.reflectionMode = ReflectionMode::PlanarFull; s.refraction = 0.09f;
	s.glintStrength = 2.2f; s.glintSharpness = 300.0f; s.scatter = 1.1f;
	s.foamIntensity = 1.6f; s.crestFoam = 0.35f; s.foamTrail = 3.0f;
	s.shoreWaves = 0.25f; s.swash = 1.1f; s.fadeDistance = 6.0f;
	s.underwater.custom = true; s.underwater.visibility = 9.5f; s.underwater.deepColor = { 0.0f, 0.1f, 0.3f };
	s.height = 17.0f; s.size = 777.0f; s.sizeZ = 55.0f; s.meshDetail = 0;
	return s;
}

static void TestJson()
{
	const nlohmann::json j = WaterPreset::ToJson(Busy());
	CHECK(j["kind"] == "water_preset");
	const nlohmann::json& water = j["water"];
	for (const char* key : WaterPreset::NotPartOfALook()) CHECK(!water.contains(key));   // not the place, not the mesh
	// ...stated here on its own, so the test does not trust the list it is testing:
	for (const char* key : { "height", "size", "sizeZ", "meshDetail", "lodDistance", "frustumCulling" }) CHECK(!water.contains(key));
	for (const char* key : { "color", "shallowColor", "alpha", "waveScale", "foamIntensity", "reflectionMode", "underwater", "swash" }) CHECK(water.contains(key));

	WaterSettings back;
	std::string error;
	CHECK(WaterPreset::FromJson(j, back, error));
	for (Group g : kLook) CHECK(!back.GroupModified(g, Busy()));   // the look came back exactly
	// Through text, as a file would carry it.
	WaterSettings viaText;
	CHECK(WaterPreset::FromJson(nlohmann::json::parse(j.dump(2)), viaText, error));
	for (Group g : kLook) CHECK(!viaText.GroupModified(g, Busy()));
	// Applying it to another ocean takes the look and leaves the ocean's place alone.
	WaterSettings other = WaterSettings::Defaults();
	other.height = 3.0f; other.size = 120.0f;
	other.ApplyPreset(back);
	CHECK(other.foamIntensity == 1.6f && other.waveSeed == 31 && other.height == 3.0f && other.size == 120.0f);
}

static void TestRefusals()
{
	WaterSettings keep = WaterSettings::Defaults();
	keep.waveScale = 1.23f;
	WaterSettings out = keep;
	std::string error;
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"scene","water":{}})"), out, error) && !error.empty());
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"([1,2,3])"), out, error));
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":99,"water":{}})"), out, error));
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":1})"), out, error));              // no "water"
	CHECK(!WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":1,"water":{"alpha":"x"}})"), out, error));   // wrong type
	CHECK(out.waveScale == 1.23f);   // a refused preset leaves the target untouched
	// A hand-edited value out of range is clamped, not handed to the renderer.
	CHECK(WaterPreset::FromJson(nlohmann::json::parse(R"({"kind":"water_preset","version":1,"water":{"waveScale":999,"foamIntensity":-4}})"), out, error));
	CHECK(out.waveScale == WaterSettings::kWaveScaleMax && out.foamIntensity == 0.0f);
}

static void TestFiles()
{
	const fs::path folder = fs::temp_directory_path() / "water_preset_test_folder";
	std::error_code ec;
	fs::remove_all(folder, ec);
	CHECK(WaterPreset::List(folder).empty());   // a folder that does not exist has no presets

	std::string error;
	CHECK(WaterPreset::Save(Busy(), folder / ("Stormy Sea" + std::string(WaterPreset::kExtension)), error));   // creates the folder
	CHECK(WaterPreset::Save(WaterSettings::Defaults(), folder / ("alpine lake" + std::string(WaterPreset::kExtension)), error));
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

int main()
{
	TestJson();
	TestRefusals();
	TestFiles();
	TestNames();
	if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
	std::printf("water preset tests: all passed\n");
	return 0;
}
