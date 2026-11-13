#include "AssetBrowserPanel.h"
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
    if (isDirectory) return Skybox::IsSkyboxFolder(path.string()) ? "Skybox" : "Folder";
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

    // Row 1: up one level, the way back to every folder above (a breadcrumb), the filter.
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
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##assetFilter", "Filter...", filter_, sizeof(filter_));
    ImGui::Separator();

    const std::string needle = Lower(filter_);
    if (ImGui::BeginTable("##assets", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        fs::path enter;   // set when a folder is double-clicked: changed AFTER the loop, never while walking entries_
        for (const Entry& entry : entries_) {
            if (!needle.empty() && Lower(entry.name).find(needle) == std::string::npos) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, KindColor(entry.kind));
            const bool clicked = ImGui::Selectable((entry.name + "###" + entry.name).c_str(), false,
                                                   ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
            ImGui::PopStyleColor();
            if (ImGui::BeginDragDropSource()) {
                const std::string dragged = entry.path.string();
                ImGui::SetDragDropPayload(kAssetPathPayload, dragged.c_str(), dragged.size() + 1);   // +1: keep the '\0'
                ImGui::TextUnformatted(entry.name.c_str());   // what follows the pointer
                ImGui::EndDragDropSource();
            }
            if (clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (entry.isDirectory) enter = entry.path;
                else if (entry.kind == "Scene") action = { AssetAction::Kind::OpenScene, entry.path.string() };
                else if (entry.kind == "Model") action = { AssetAction::Kind::ImportModel, entry.path.string() };
            }
            if (ImGui::BeginPopupContextItem()) {
                if (entry.isDirectory && ImGui::MenuItem("Open folder")) enter = entry.path;
                if (entry.kind == "Skybox" && ImGui::MenuItem("Use as skybox")) action = { AssetAction::Kind::UseAsSkybox, entry.path.string() };
                if (entry.kind == "Scene" && ImGui::MenuItem("Open scene")) action = { AssetAction::Kind::OpenScene, entry.path.string() };
                if (entry.kind == "Model" && ImGui::MenuItem("Import into scene")) action = { AssetAction::Kind::ImportModel, entry.path.string() };
                if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(entry.path);
                if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(entry.path.string().c_str());
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", entry.kind.c_str());
            ImGui::TableNextColumn();
            if (!entry.isDirectory) ImGui::TextDisabled("%s", SizeText(entry.size).c_str());
        }
        ImGui::EndTable();
        if (!enter.empty()) GoTo(enter);
    }
    ImGui::End();
    return action;
}
