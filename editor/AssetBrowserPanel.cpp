#include "AssetBrowserPanel.h"
#include "EditorIcons.h"
#include "imgui.h"
#include "Skybox.h"
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace {
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool EndsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string KindOf(const fs::path& path, bool isDirectory)
{
    if (isDirectory) return "Folder";   // Refresh() upgrades a folder with six faces to "Skybox"
    const std::string name = Lower(path.filename().string());
    const std::string extension = Lower(path.extension().string());
    if (EndsWith(name, ".scene.json")) return "Scene";
    if (extension == ".gltf" || extension == ".glb" || extension == ".fbx" || extension == ".obj") return "Model";
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp") return "Image";
    if (extension == ".hlsl" || extension == ".hlsli") return "Shader";
    return "File";
}

std::string SizeText(std::uintmax_t bytes)
{
    char text[32];
    if (bytes < 1024) std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024ull * 1024) std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

ImVec4 KindColor(const std::string& kind)
{
    if (kind == "Folder") return ImVec4(0.95f, 0.80f, 0.45f, 1.0f);
    if (kind == "Skybox") return ImVec4(0.50f, 0.85f, 0.95f, 1.0f);
    if (kind == "Scene")  return ImVec4(0.55f, 0.85f, 0.65f, 1.0f);
    if (kind == "Model")  return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
    if (kind == "Image")  return ImVec4(0.85f, 0.65f, 0.95f, 1.0f);
    return ImVec4(0.65f, 0.67f, 0.72f, 1.0f);
}

EditorIcons::Icon IconOf(const std::string& kind)
{
    if (kind == "Folder") return EditorIcons::Icon::Folder;
    if (kind == "Skybox") return EditorIcons::Icon::Sky;
    if (kind == "Scene")  return EditorIcons::Icon::Scene;
    if (kind == "Model")  return EditorIcons::Icon::Model;
    if (kind == "Image")  return EditorIcons::Icon::Image;
    return EditorIcons::Icon::File;
}

// "size-time" of a file: changes whenever the file does.
std::string StampOf(const fs::path& path)
{
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    const auto time = fs::last_write_time(path, error);
    return std::to_string(static_cast<unsigned long long>(size)) + "-" + std::to_string(static_cast<long long>(time.time_since_epoch().count()));
}

void ShowInExplorer(const fs::path& path)
{
#ifdef _WIN32
    const std::wstring argument = L"/select,\"" + path.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
#else
    (void)path;
#endif
}
}

void AssetBrowserPanel::GoTo(const fs::path& folder)
{
    current_ = folder;
    lastRefresh_ = -100.0;   // list it on the next line
}

void AssetBrowserPanel::Refresh()
{
    entries_.clear();
    std::error_code error;
    for (fs::directory_iterator it(current_, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error)) {
        const fs::path& path = it->path();
        const std::string name = path.filename().string();
        if (name.empty() || name[0] == '.' || EndsWith(name, ".tmp")) continue;   // hidden files and half-written saves
        Entry entry;
        entry.path = path;
        entry.name = name;
        std::error_code entryError;
        entry.isDirectory = it->is_directory(entryError);
        entry.kind = KindOf(path, entry.isDirectory);
        if (!entry.isDirectory) entry.size = it->file_size(entryError);
        if (entry.isDirectory) {
            // A folder with six faces (named in any of the usual ways) is a skybox, previewed by one of its faces.
            std::array<std::string, 6> faces;
            if (Skybox::ResolveFaces(path.string(), faces)) {
                entry.kind = "Skybox";
                entry.previewImage = faces[5];
                entry.previewStamp = StampOf(faces[5]);
            }
        } else if (entry.kind == "Image") {
            entry.previewImage = path.string();
            entry.previewStamp = StampOf(path);
        }
        entries_.push_back(std::move(entry));
    }
    // Folders first, then by name without regard to case.
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return Lower(a.name) < Lower(b.name);
    });
}

AssetAction AssetBrowserPanel::Draw(bool* open, const std::vector<AssetRoot>& roots)
{
    AssetAction action;
    if (!*open) return action;
    if (!ImGui::Begin("Assets", open)) { ImGui::End(); return action; }
    if (roots.empty()) {
        ImGui::TextDisabled("There are no asset folders to browse.");
        ImGui::End();
        return action;
    }

    // Which place to browse. With one root there is nothing to choose. Opening or closing a project changes the list, so
    // the index is clamped, and a different path starts again at its top.
    rootIndex_ = (std::min)(rootIndex_, static_cast<int>(roots.size()) - 1);
    if (roots.size() > 1) {
        for (int i = 0; i < static_cast<int>(roots.size()); ++i) {
            const bool active = (i == rootIndex_);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(roots[i].label.c_str())) rootIndex_ = i;
            if (active) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", roots[i].path.string().c_str());
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    const fs::path& root = roots[rootIndex_].path;

    // A different project (or none): start again at its top.
    if (root != root_) {
        root_ = root;
        GoTo(root_);
    }
    std::error_code error;
    if (!fs::is_directory(current_, error)) GoTo(root_);   // the folder was deleted or renamed meanwhile
    if (!fs::is_directory(root_, error)) {
        ImGui::TextDisabled("There is no assets folder at:");
        ImGui::TextUnformatted(root_.string().c_str());
        ImGui::End();
        return action;
    }
    // The disk can change under us (an exporter, the Explorer): look again every second and a half. Cheap for a folder.
    if (ImGui::GetTime() - lastRefresh_ > 1.5) {
        Refresh();
        lastRefresh_ = ImGui::GetTime();
    }

    // Row 1: up one level, and the way back to every folder above (a breadcrumb).
    const bool atRoot = fs::equivalent(current_, root_, error);
    ImGui::BeginDisabled(atRoot);
    if (ImGui::Button("<")) GoTo(current_.parent_path());
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Up one folder");
    ImGui::SameLine();
    {
        std::vector<fs::path> chain;   // the folders from the root down to here
        for (fs::path folder = current_;; folder = folder.parent_path()) {
            chain.push_back(folder);
            if (fs::equivalent(folder, root_, error) || folder == folder.parent_path()) break;
        }
        std::reverse(chain.begin(), chain.end());
        for (size_t i = 0; i < chain.size(); ++i) {
            if (i > 0) { ImGui::TextDisabled("/"); ImGui::SameLine(); }
            const std::string name = i == 0 ? "assets" : chain[i].filename().string();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(name.c_str())) GoTo(chain[i]);
            ImGui::PopID();
            ImGui::SameLine();
        }
    }
    ImGui::NewLine();

    // Row 2: the filter, and the two ways to look at the folder.
    const float viewButtons = (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) * 2.0f;
    ImGui::SetNextItemWidth(-viewButtons);
    ImGui::InputTextWithHint("##assetFilter", "Filter...", filter_, sizeof(filter_));
    ImGui::SameLine();
    if (EditorIcons::Button("##assetsGrid", EditorIcons::Icon::GridView, gridView_, "Thumbnails")) gridView_ = true;
    ImGui::SameLine();
    if (EditorIcons::Button("##assetsList", EditorIcons::Icon::ListView, !gridView_, "List")) gridView_ = false;
    ImGui::Separator();

    thumbnails_.BeginFrame();
    const std::string needle = Lower(filter_);
    fs::path enter;   // set when a folder is double-clicked: changed AFTER the loop, never while walking entries_
    if (gridView_) {
        if (ImGui::BeginChild("##assetGrid")) {
            const float tile = ImGui::GetFontSize() * 6.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (tile + spacing)));
            int shown = 0;
            for (const Entry& entry : entries_) {
                if (!needle.empty() && Lower(entry.name).find(needle) == std::string::npos) continue;
                if (shown % columns != 0) ImGui::SameLine();
                ++shown;
                ImGui::PushID(entry.name.c_str());
                const bool pressed = ImGui::InvisibleButton("##tile", ImVec2(tile, tile + ImGui::GetTextLineHeight() + 8.0f));
                (void)pressed;
                const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                DrawTile(entry, tile, false);
                HandleEntry(entry, doubleClicked, action, enter);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    } else if (ImGui::BeginTable("##assets", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        for (const Entry& entry : entries_) {
            if (!needle.empty() && Lower(entry.name).find(needle) == std::string::npos) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawRow(entry);
            const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            HandleEntry(entry, doubleClicked, action, enter);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", entry.kind.c_str());
            ImGui::TableNextColumn();
            if (!entry.isDirectory) ImGui::TextDisabled("%s", SizeText(entry.size).c_str());
        }
        ImGui::EndTable();
    }
    if (!enter.empty()) GoTo(enter);
    ImGui::End();
    return action;
}

void AssetBrowserPanel::HandleEntry(const Entry& entry, bool doubleClicked, AssetAction& action, fs::path& enter)
{
    if (ImGui::BeginDragDropSource()) {
        const std::string dragged = entry.path.string();
        ImGui::SetDragDropPayload(kAssetPathPayload, dragged.c_str(), dragged.size() + 1);   // +1: keep the '\0'
        ImGui::TextUnformatted(entry.name.c_str());   // what follows the pointer
        ImGui::EndDragDropSource();
    }
    if (doubleClicked) {
        if (entry.isDirectory && entry.kind != "Skybox") enter = entry.path;   // a skybox is used, not entered (its six images are not worth browsing)
        else if (entry.kind == "Skybox") action = { AssetAction::Kind::UseAsSkybox, entry.path.string() };
        else if (entry.kind == "Scene") action = { AssetAction::Kind::OpenScene, entry.path.string() };
        else if (entry.kind == "Model") action = { AssetAction::Kind::ImportModel, entry.path.string() };
    }
    if (ImGui::BeginPopupContextItem()) {
        if (entry.isDirectory && ImGui::MenuItem("Open folder")) enter = entry.path;
        if (entry.kind == "Skybox" && ImGui::MenuItem("Use as skybox")) action = { AssetAction::Kind::UseAsSkybox, entry.path.string() };
        if (entry.kind == "Skybox") ImGui::SetItemTooltip("Changes the cubemap of the active sky.");
        if (entry.kind == "Skybox" && ImGui::MenuItem("Add as new sky")) action = { AssetAction::Kind::AddAsSkybox, entry.path.string() };
        if (entry.kind == "Skybox") ImGui::SetItemTooltip("Adds another sky to the scene and switches to it. The old one stays, to switch back to.");
        if (entry.kind == "Scene" && ImGui::MenuItem("Open scene")) action = { AssetAction::Kind::OpenScene, entry.path.string() };
        if (entry.kind == "Model" && ImGui::MenuItem("Import into scene")) action = { AssetAction::Kind::ImportModel, entry.path.string() };
        if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(entry.path);
        if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(entry.path.string().c_str());
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::DrawTile(const Entry& entry, float tile, bool /*selectedLook*/)
{
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    const float rounding = ImGui::GetStyle().FrameRounding + 2.0f;
    list->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg), rounding);

    // The picture area: a square at the top of the tile, with a little margin.
    const float margin = 6.0f;
    const ImVec2 areaMin(min.x + margin, min.y + margin);
    const ImVec2 areaMax(max.x - margin, min.y + tile - margin);
    const ImVec4 kindColor = KindColor(entry.kind);

    ThumbnailCache::Thumbnail thumbnail;
    if (!entry.previewImage.empty() && ImGui::IsRectVisible(min, max)) thumbnail = thumbnails_.Get(entry.previewImage, entry.previewStamp);
    if (thumbnail.view && thumbnail.width > 0 && thumbnail.height > 0) {
        // Fit the picture inside the square without stretching it.
        const float areaW = areaMax.x - areaMin.x, areaH = areaMax.y - areaMin.y;
        const float scale = (std::min)(areaW / static_cast<float>(thumbnail.width), areaH / static_cast<float>(thumbnail.height));
        const float w = static_cast<float>(thumbnail.width) * scale, h = static_cast<float>(thumbnail.height) * scale;
        const ImVec2 imageMin(areaMin.x + (areaW - w) * 0.5f, areaMin.y + (areaH - h) * 0.5f);
        list->AddImageRounded(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(thumbnail.view)), imageMin, ImVec2(imageMin.x + w, imageMin.y + h),
                              ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, 3.0f);
    } else {
        const float iconSize = (areaMax.x - areaMin.x) * 0.5f;
        EditorIcons::Draw(list, IconOf(entry.kind), ImVec2((areaMin.x + areaMax.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f), iconSize,
                          ImGui::GetColorU32(kindColor));
    }
    // A skybox gets a small badge over its picture, so a skybox folder is not mistaken for a plain image.
    if (entry.kind == "Skybox") {
        EditorIcons::Draw(list, EditorIcons::Icon::Sky, ImVec2(areaMax.x - 8.0f, areaMin.y + 8.0f), 13.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f)));
    }

    // The name, on one line, cut off at the tile's edge.
    const ImVec2 textPos(min.x + margin, min.y + tile);
    list->PushClipRect(ImVec2(min.x + 2.0f, min.y + tile), ImVec2(max.x - 2.0f, max.y), true);
    list->AddText(textPos, ImGui::GetColorU32(kindColor), entry.name.c_str());
    list->PopClipRect();
    if (hovered) ImGui::SetTooltip("%s\n%s", entry.name.c_str(), entry.kind.c_str());
}

void AssetBrowserPanel::DrawRow(const Entry& entry)
{
    const float iconSize = ImGui::GetTextLineHeight();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    EditorIcons::Draw(ImGui::GetWindowDrawList(), IconOf(entry.kind), ImVec2(cursor.x + iconSize * 0.5f, cursor.y + iconSize * 0.5f + 1.0f),
                      iconSize * 0.9f, ImGui::GetColorU32(KindColor(entry.kind)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconSize + 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, KindColor(entry.kind));
    ImGui::Selectable((entry.name + "###" + entry.name).c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
    ImGui::PopStyleColor();
}
