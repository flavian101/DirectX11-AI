#include "Project.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
constexpr const char* kProjectFormat = "d3d11-project";
constexpr const char* kStartupFormat = "d3d11-startup";

// '/' separators, no "./" noise: the same text on every machine.
std::string Normalize(const std::string& path)
{
	std::string text = fs::path(path).lexically_normal().generic_string();
	while (text.size() > 2 && text.compare(0, 2, "./") == 0) text.erase(0, 2);
	return text;
}

// Writes `text` to `file` through a temporary file, so a crash half-way never leaves a half-written file in its place.
bool WriteAtomically(const fs::path& file, const std::string& text, std::string& error)
{
	std::error_code fsError;
	fs::create_directories(file.parent_path(), fsError);
	const fs::path temporary = file.string() + ".tmp";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out) { error = "Cannot write to " + temporary.string(); return false; }
		out << text << '\n';
		if (!out) { error = "Writing " + file.string() + " failed (disk full?)."; return false; }
	}
	fs::rename(temporary, file, fsError);
	if (fsError) {   // some platforms refuse to rename over an existing file
		fs::remove(file, fsError);
		fs::rename(temporary, file, fsError);
		if (fsError) { error = "Could not replace " + file.string() + ": " + fsError.message(); return false; }
	}
	return true;
}
}

std::string Project::RelativeToRoot(const fs::path& absolute) const
{
	if (!IsOpen()) return {};
	std::error_code error;
	const fs::path target = fs::weakly_canonical(absolute, error);
	const fs::path base = fs::weakly_canonical(Root(), error);
	const fs::path relative = target.lexically_relative(base);   // empty if there is no relative route (another drive)
	if (relative.empty() || *relative.begin() == "..") return {};
	return relative.generic_string();
}

bool Project::HasScene(const std::string& relative) const
{
	return std::find(scenes.begin(), scenes.end(), Normalize(relative)) != scenes.end();
}

bool Project::AddScene(const std::string& relative)
{
	const std::string entry = Normalize(relative);
	if (entry.empty() || HasScene(entry)) return false;
	scenes.push_back(entry);
	if (startScene.empty()) startScene = entry;
	return true;
}

bool Project::RemoveScene(const std::string& relative)
{
	const std::string entry = Normalize(relative);
	const auto found = std::find(scenes.begin(), scenes.end(), entry);
	if (found == scenes.end()) return false;
	scenes.erase(found);
	if (startScene == entry) startScene = scenes.empty() ? std::string() : scenes.front();
	return true;
}

bool Project::SetStartScene(const std::string& relative)
{
	const std::string entry = Normalize(relative);
	if (!HasScene(entry)) return false;
	startScene = entry;
	return true;
}

bool CreateProject(const fs::path& file, Project& out, std::string& error)
{
	const fs::path absolute = fs::absolute(file);
	std::error_code fsError;
	if (fs::exists(absolute, fsError)) {
		error = "A project file already exists there:\n" + absolute.string() + "\nUse File > Open Project to open it.";
		return false;
	}
	Project project;
	project.file = absolute;
	project.name = absolute.stem().string();
	// The folders every project starts with. (Empty folders are fine: they are what the Assets panel shows first.)
	for (const char* folder : { "scenes", "models", "textures" }) fs::create_directories(project.AssetsRoot() / folder, fsError);
	if (fsError) { error = "Could not create the project folders: " + fsError.message(); return false; }
	if (!SaveProject(project, error)) return false;
	out = std::move(project);
	return true;
}

bool LoadProject(const fs::path& file, Project& out, std::string& error)
{
	std::ifstream in(file, std::ios::binary);
	if (!in) { error = "Cannot open " + file.string(); return false; }
	json root;
	try {
		root = json::parse(in);
	} catch (const json::exception& e) {
		error = std::string("The file is not valid JSON: ") + e.what();
		return false;
	}
	if (!root.is_object() || root.value("format", "") != kProjectFormat) {
		error = "This is not a project file written by this editor.";
		return false;
	}
	if (root.value("version", 0) > kProjectFileVersion) {
		error = "This project was saved by a newer version of the editor.";
		return false;
	}

	Project project;
	project.file = fs::absolute(file);
	project.name = root.value("name", project.file.stem().string());
	if (root.contains("scenes") && root["scenes"].is_array()) {
		for (const json& entry : root["scenes"]) {
			if (entry.is_string()) project.AddScene(entry.get<std::string>());   // drops duplicates and normalises slashes
		}
	}
	// A scene that is listed stays listed even when its file has gone: the Scenes panel shows it as missing, so you can
	// see what happened instead of the entry silently vanishing.
	project.startScene.clear();
	const std::string start = root.value("startScene", "");
	if (!start.empty() && project.HasScene(start)) project.startScene = Normalize(start);
	else if (!project.scenes.empty()) project.startScene = project.scenes.front();
	out = std::move(project);
	return true;
}

bool SaveProject(const Project& project, std::string& error, std::vector<std::string>* warnings)
{
	if (!project.IsOpen()) { error = "There is no project to save."; return false; }
	json root;
	root["format"] = kProjectFormat;
	root["version"] = kProjectFileVersion;
	root["name"] = project.name;
	root["startScene"] = project.startScene;
	root["scenes"] = project.scenes;
	if (!WriteAtomically(project.file, root.dump(2), error)) return false;

	// The part a shipped game needs, kept inside assets/ because that is the only folder that ships.
	if (!project.startScene.empty()) {
		const fs::path scenePath = fs::weakly_canonical(project.Resolve(project.startScene));
		const fs::path inAssets = scenePath.lexically_relative(fs::weakly_canonical(project.AssetsRoot()));
		if (inAssets.empty() || *inAssets.begin() == "..") {
			if (warnings) warnings->push_back("The start scene \"" + project.startScene + "\" is not inside the project's assets folder, so a shipped game cannot find it. Keep scenes under assets/scenes.");
		} else {
			json startup;
			startup["format"] = kStartupFormat;
			startup["version"] = 1;
			startup["startScene"] = inAssets.generic_string();
			std::string startupError;
			if (!WriteAtomically(project.AssetsRoot() / kStartupFileName, startup.dump(2), startupError)) {
				if (warnings) warnings->push_back("The project was saved, but the game's start file could not be written: " + startupError);
			}
		}
	}
	return true;
}

std::string ReadStartupScene(const fs::path& assetsFolder)
{
	std::ifstream in(assetsFolder / kStartupFileName, std::ios::binary);
	if (!in) return {};
	try {
		const json root = json::parse(in);
		if (!root.is_object() || root.value("format", "") != kStartupFormat) return {};
		const std::string scene = root.value("startScene", "");
		const fs::path path(scene);
		if (scene.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return {};
		for (const fs::path& part : path) if (part == "..") return {};   // the start scene can never point outside assets/
		return path.generic_string();
	} catch (const json::exception&) {
		return {};
	}
}
