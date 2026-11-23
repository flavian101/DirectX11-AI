#pragma once
// Water presets (lesson 43): the LOOK of a water body as a small file, to apply to any other body.
//
// A preset says how water looks: appearance, waves, reflection, refraction, lighting, foam, shoreline and the underwater look
// (WaterSettings::ApplyPreset). It does not say where the water is, what kind of body it is, or what its river or current is:
// those keys are left out of the file, so a preset cannot move a lake or flatten a river, and the file stays small.
//
// Include this AFTER SceneJson.h: how a WaterSettings becomes JSON is defined there, once, for scene files and presets alike.
#include "WaterSettings.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace WaterPreset {

constexpr const char* kExtension = ".waterpreset";
constexpr int kVersion = 1;

// The keys of a WaterSettings that are not part of a look (see the top of this file).
inline const std::vector<const char*>& NotPartOfALook()
{
	static const std::vector<const char*> keys = {
		"height", "size", "sizeZ",
		"meshDetail", "lodDistance", "frustumCulling" };
	return keys;
}

inline nlohmann::json ToJson(const WaterSettings& settings)
{
	nlohmann::json water = settings;
	for (const char* key : NotPartOfALook()) water.erase(key);
	return nlohmann::json{ { "kind", "water_preset" }, { "version", kVersion }, { "water", water } };
}

// Reads a preset. `out` is only touched on success. Settings the file does not mention keep the defaults of a fresh body.
inline bool FromJson(const nlohmann::json& j, WaterSettings& out, std::string& error)
{
	try {
		if (!j.is_object() || j.value("kind", std::string()) != "water_preset") { error = "This is not a water preset file."; return false; }
		if (j.value("version", 0) > kVersion) { error = "This preset was made by a newer version of the editor."; return false; }
		WaterSettings loaded = j.at("water").get<WaterSettings>();
		loaded.Clamp();   // a hand-edited file must not hand the renderer a value it cannot take
		out = loaded;
		return true;
	} catch (const nlohmann::json::exception& e) {
		error = std::string("The preset could not be read: ") + e.what();
		return false;
	}
}

// A file name for a preset name: letters, digits, spaces, '-' and '_' stay; anything else (path separators, wildcards...) becomes
// '_'. Empty if nothing usable is left. No path, no extension: just the name.
inline std::string FileNameFor(const std::string& name)
{
	std::string out;
	for (unsigned char c : name) out += (std::isalnum(c) || c == ' ' || c == '-' || c == '_') ? static_cast<char>(c) : '_';
	const size_t first = out.find_first_not_of(' ');
	if (first == std::string::npos) return std::string();
	out = out.substr(first, out.find_last_not_of(' ') - first + 1);
	if (out.size() > 64) out.resize(64);
	if (out.find_first_not_of('_') == std::string::npos) return std::string();   // nothing but replaced characters
	return out;
}

// Writes the preset to `path` (the folder is created). Through a temporary file, so a failure never leaves half a preset.
inline bool Save(const WaterSettings& settings, const std::filesystem::path& path, std::string& error)
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	const std::filesystem::path temporary = path.string() + ".tmp";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out) { error = "Cannot write to " + temporary.string(); return false; }
		out << ToJson(settings).dump(2) << '\n';
		if (!out) { error = "Writing the preset failed (disk full?)."; return false; }
	}
	std::filesystem::rename(temporary, path, ec);
	if (ec) {
		std::filesystem::remove(path, ec);
		std::filesystem::rename(temporary, path, ec);
		if (ec) { error = "Could not replace " + path.string() + ": " + ec.message(); return false; }
	}
	return true;
}

inline bool Load(const std::filesystem::path& path, WaterSettings& out, std::string& error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) { error = "Cannot open " + path.string(); return false; }
	try {
		return FromJson(nlohmann::json::parse(in), out, error);
	} catch (const nlohmann::json::exception& e) {
		error = std::string("The preset could not be read: ") + e.what();
		return false;
	}
}

// The presets in a folder, by name (case-insensitive). A folder that does not exist has none.
inline std::vector<std::filesystem::path> List(const std::filesystem::path& folder)
{
	std::vector<std::filesystem::path> found;
	std::error_code ec;
	if (!std::filesystem::is_directory(folder, ec)) return found;
	for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
		if (entry.is_regular_file(ec) && entry.path().extension() == kExtension) found.push_back(entry.path());
	auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
	std::sort(found.begin(), found.end(), [&](const auto& a, const auto& b) { return lower(a.stem().string()) < lower(b.stem().string()); });
	return found;
}

}   // namespace WaterPreset
