#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct AssetAction {
    enum class Kind { None, OpenScene, ImportModel };
    Kind kind = Kind::None;
    std::string path;
};

// A file browser for the project's assets folder. It only LISTS and reports what was double-clicked: opening a scene or
// importing a model is EditorLayer's job (at the start of the next frame, like every other request).
class AssetBrowserPanel {
public:
    // `root`: the folder to browse (a project's assets/ folder, or "assets" when no project is open). The panel never
    // leaves it.
    AssetAction Draw(bool* open, const std::filesystem::path& root);

private:
    struct Entry {
        std::filesystem::path path;
        std::string name;
        std::string kind;         // "Folder", "Scene", "Model", "Image", "Shader", "File"
        bool isDirectory = false;
        std::uintmax_t size = 0;
    };
    void Refresh();
    void GoTo(const std::filesystem::path& folder);

    std::filesystem::path root_;
    std::filesystem::path current_;
    std::vector<Entry> entries_;
    double lastRefresh_ = -100.0;
    char filter_[64] = "";
};
