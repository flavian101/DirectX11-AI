#pragma once
#include "ThumbnailCache.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct AssetAction {
    enum class Kind { None, OpenScene, ImportModel, UseAsSkybox, AddAsSkybox };   // UseAsSkybox swaps the active sky's cubemap; AddAsSkybox makes a new sky
    Kind kind = Kind::None;
    std::string path;
};

// Drag-and-drop: every row is a drag source with this payload type. The data is the file's path as UTF-8 text, including
// the closing '\0'. Targets (the Viewport takes models, the Inspector's sky picker takes skybox folders) check what it is.
constexpr const char* kAssetPathPayload = "ASSET_PATH";

// A file browser for the project's asset folders. It only LISTS and reports what was double-clicked: opening a scene or
// importing a model is EditorLayer's job (at the start of the next frame, like every other request).
class AssetBrowserPanel {
public:
    // One place to browse, shown as a button above the list ("Project", "Engine").
    struct AssetRoot {
        std::string label;
        std::filesystem::path path;
    };
    // `roots`: the folders to browse. The panel never leaves the one that is selected. A project's assets/ folder holds
    // what ships with the game (scenes and whatever was copied in); the engine's own assets/ holds the stock textures,
    // skyboxes and models, so with a project open both are offered.
    AssetAction Draw(bool* open, const std::vector<AssetRoot>& roots);

    // The device that makes the thumbnails. Set once when the editor starts; Release before the device goes away.
    void SetDevice(ID3D11Device* device) { thumbnails_.SetDevice(device); }
    void Release() { thumbnails_.Release(); }

private:
    struct Entry {
        std::filesystem::path path;
        std::string name;
        std::string kind;         // "Folder", "Skybox" (a folder with the six faces), "Scene", "Model", "Image", "Shader", "File"
        bool isDirectory = false;
        std::uintmax_t size = 0;
        std::string previewImage;   // an image to preview: the file itself, or one face of a skybox folder. Empty = none.
        std::string previewStamp;   // size + time of that image, so a changed file gets a new thumbnail
    };
    void Refresh();
    // What every row and tile does, whatever the view: be dragged, open on a double-click, show the context menu. Call it
    // straight after the item was submitted (it works on "the last item").
    void HandleEntry(const Entry& entry, bool doubleClicked, AssetAction& action, std::filesystem::path& enter);
    void DrawTile(const Entry& entry, float tile, bool selectedLook);   // grid view: the thumbnail (or an icon) and the name
    void DrawRow(const Entry& entry);                                    // list view: an icon, the name, the type, the size
    void GoTo(const std::filesystem::path& folder);

    int rootIndex_ = 0;
    std::filesystem::path root_;
    std::filesystem::path current_;
    std::vector<Entry> entries_;
    double lastRefresh_ = -100.0;
    char filter_[64] = "";
    bool gridView_ = true;       // thumbnails (true) or a plain list
    ThumbnailCache thumbnails_;
};
