// Unit tests for src/Project.h/.cpp: the project file, the scene list, the shipped start file. No Direct3D, no framework:
// a failed CHECK prints and the program exits 1. Everything happens inside a fresh temporary folder.
#include "Project.h"
#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;
static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

static void WriteText(const fs::path& file, const std::string& text)
{
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary) << text;
}

static void TestSceneList()
{
	Project p;
	p.file = fs::absolute("somewhere/Game.d3dproj");
	CHECK(p.AddScene("assets/scenes/a.scene.json"));
	CHECK(p.startScene == "assets/scenes/a.scene.json");        // the first scene starts the game
	CHECK(p.AddScene("assets/scenes/b.scene.json"));
	CHECK(!p.AddScene("./assets/scenes/b.scene.json"));          // the same scene written another way is a duplicate
	CHECK(p.scenes.size() == 2);
	CHECK(p.SetStartScene("assets/scenes/b.scene.json"));
	CHECK(!p.SetStartScene("assets/scenes/nope.scene.json"));    // only a listed scene can start the game
	CHECK(p.startScene == "assets/scenes/b.scene.json");
	CHECK(p.RemoveScene("assets/scenes/b.scene.json"));
	CHECK(p.startScene == "assets/scenes/a.scene.json");        // the start scene moved on to the next one
	CHECK(!p.RemoveScene("assets/scenes/b.scene.json"));
	CHECK(p.RemoveScene("assets/scenes/a.scene.json") && p.startScene.empty() && p.scenes.empty());
}

static void TestCreateSaveLoad(const fs::path& base)
{
	const fs::path file = base / "Forest" / "Forest.d3dproj";
	Project created;
	std::string error;
	CHECK(CreateProject(file, created, error));
	CHECK(created.name == "Forest");
	CHECK(fs::exists(file) && fs::is_directory(created.ScenesFolder()));
	Project again;
	CHECK(!CreateProject(file, again, error) && !error.empty());   // never overwrites an existing project

	created.AddScene("assets/scenes/main.scene.json");
	created.AddScene("assets/scenes/cave.scene.json");
	created.SetStartScene("assets/scenes/cave.scene.json");
	std::vector<std::string> warnings;
	CHECK(SaveProject(created, error, &warnings) && warnings.empty());

	Project loaded;
	CHECK(LoadProject(file, loaded, error));
	CHECK(loaded.name == "Forest");
	CHECK(loaded.scenes == created.scenes);
	CHECK(loaded.startScene == "assets/scenes/cave.scene.json");
	CHECK(loaded.Root() == created.Root());

	// The shipped game only sees assets/: the start scene is named relative to it.
	CHECK(ReadStartupScene(created.AssetsRoot()) == "scenes/cave.scene.json");

	// Paths: inside the project they are relative, outside they are not.
	CHECK(loaded.RelativeToRoot(created.ScenesFolder() / "main.scene.json") == "assets/scenes/main.scene.json");
	CHECK(loaded.RelativeToRoot(base / "Elsewhere" / "x.scene.json").empty());
	CHECK(loaded.RelativeToRoot(base / "Forest").empty() || loaded.RelativeToRoot(base / "Forest") == ".");
}

static void TestStartSceneOutsideAssets(const fs::path& base)
{
	Project project;
	std::string error;
	CHECK(CreateProject(base / "Odd" / "Odd.d3dproj", project, error));
	project.AddScene("loose.scene.json");   // inside the project, but not under assets/
	std::vector<std::string> warnings;
	CHECK(SaveProject(project, error, &warnings));
	CHECK(warnings.size() == 1);            // saved, but the user is told the game could not find it
}

static void TestBadFiles(const fs::path& base)
{
	Project project;
	std::string error;
	CHECK(!LoadProject(base / "missing.d3dproj", project, error));
	WriteText(base / "junk.d3dproj", "{ not json");
	CHECK(!LoadProject(base / "junk.d3dproj", project, error));
	WriteText(base / "other.d3dproj", R"({"format":"something-else","version":1})");
	CHECK(!LoadProject(base / "other.d3dproj", project, error));
	WriteText(base / "future.d3dproj", R"({"format":"d3d11-project","version":99})");
	CHECK(!LoadProject(base / "future.d3dproj", project, error));
	// A scene that is listed but missing from disk stays listed; a start scene that is not listed falls back to the first one.
	WriteText(base / "odd.d3dproj", R"({"format":"d3d11-project","version":1,"startScene":"zzz","scenes":["a.scene.json", 5, "a.scene.json", "b.scene.json"]})");
	CHECK(LoadProject(base / "odd.d3dproj", project, error));
	CHECK(project.scenes.size() == 2 && project.startScene == "a.scene.json" && project.name == "odd");

	// The startup file is read by a shipped game: nothing in it may point outside assets/.
	const fs::path assets = base / "shipped";
	CHECK(ReadStartupScene(assets).empty());   // no file at all
	WriteText(assets / "startup.json", R"({"format":"d3d11-startup","version":1,"startScene":"../secret.scene.json"})");
	CHECK(ReadStartupScene(assets).empty());
	WriteText(assets / "startup.json", R"({"format":"d3d11-startup","version":1,"startScene":"/etc/passwd"})");
	CHECK(ReadStartupScene(assets).empty());
	WriteText(assets / "startup.json", R"({"format":"d3d11-startup","version":1,"startScene":"scenes/ok.scene.json"})");
	CHECK(ReadStartupScene(assets) == "scenes/ok.scene.json");
	WriteText(assets / "startup.json", "garbage");
	CHECK(ReadStartupScene(assets).empty());
}

int main()
{
	const fs::path base = fs::temp_directory_path() / "d3d_project_test";
	std::error_code ignored;
	fs::remove_all(base, ignored);
	fs::create_directories(base);
	TestSceneList();
	TestCreateSaveLoad(base);
	TestStartSceneOutsideAssets(base);
	TestBadFiles(base);
	fs::remove_all(base, ignored);
	if (g_failures == 0) std::printf("project tests: all passed\n");
	return g_failures == 0 ? 0 : 1;
}
