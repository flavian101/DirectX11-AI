#pragma once
// A PROJECT is a folder that holds a game: its scenes, and the assets they use.
//
//     MyGame/
//       MyGame.d3dproj          the project file (editor only): the list of scenes and which one starts the game
//       assets/                 everything that is SHIPPED with the game
//         startup.json          written by the editor: tells the shipped game which scene to start with
//         scenes/*.scene.json
//         models/ textures/ ...
//
// A scene file used to be the whole story: you opened one, saved one, and the game always started with
// assets/scenes/main.scene.json. A project adds the layer above it: several scenes that belong together, one marked as
// the start scene, and ONE assets folder that every scene measures its paths against.
//
// Plain C++ with no Direct3D and no ImGui: the editor draws the panels, this file only knows the rules.
#include <filesystem>
#include <string>
#include <vector>

constexpr int kProjectFileVersion = 1;
constexpr const char* kProjectExtension = ".d3dproj";
constexpr const char* kStartupFileName = "startup.json";   // inside the assets folder; this is what a shipped game reads

struct Project {
	std::string name;
	std::filesystem::path file;          // the .d3dproj, absolute. Empty = no project is open.
	std::vector<std::string> scenes;     // paths relative to the project folder, with '/' separators
	std::string startScene;              // one entry of `scenes`, or empty

	bool IsOpen() const { return !file.empty(); }
	std::filesystem::path Root() const { return file.parent_path(); }
	std::filesystem::path AssetsRoot() const { return Root() / "assets"; }
	std::filesystem::path ScenesFolder() const { return AssetsRoot() / "scenes"; }

	std::filesystem::path Resolve(const std::string& relative) const { return Root() / relative; }
	// The path of `absolute` relative to the project folder ('/' separators), or "" if it is not inside the project.
	std::string RelativeToRoot(const std::filesystem::path& absolute) const;

	bool HasScene(const std::string& relative) const;
	// Adds a scene to the list. The FIRST scene of a project becomes its start scene. False if it was already there.
	bool AddScene(const std::string& relative);
	// Takes a scene off the list (the file stays on disk). If it was the start scene, the first remaining one takes over.
	bool RemoveScene(const std::string& relative);
	// `relative` must already be in the list.
	bool SetStartScene(const std::string& relative);
};

// Makes a new, empty project: creates the folders and writes the project file. Refuses to overwrite an existing file.
bool CreateProject(const std::filesystem::path& file, Project& out, std::string& error);
bool LoadProject(const std::filesystem::path& file, Project& out, std::string& error);
// Writes the project file, and assets/startup.json for the shipped game. A start scene that is not under assets/ cannot be
// shipped: that is reported in `warnings` (when given), and the project file is still saved.
bool SaveProject(const Project& project, std::string& error, std::vector<std::string>* warnings = nullptr);

// What a SHIPPED game reads: the start scene named in <assetsFolder>/startup.json, relative to that folder.
// "" when there is no such file, it is unreadable, or the path would leave the folder (absolute, or containing "..").
std::string ReadStartupScene(const std::filesystem::path& assetsFolder);
