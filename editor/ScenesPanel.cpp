#include "ScenesPanel.h"
#include "imgui.h"
#include <filesystem>

namespace {
// "assets/scenes/forest.scene.json" -> "forest"
std::string DisplayName(const std::string& relative)
{
    std::string name = std::filesystem::path(relative).filename().string();
    const std::string suffix = ".scene.json";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) name.resize(name.size() - suffix.size());
    return name;
}
}

ScenesAction ScenesPanel::Draw(bool* open, const Project& project, const ScenesState& state)
{
    ScenesAction action;
    if (!*open) return action;
    if (!ImGui::Begin("Scenes", open)) { ImGui::End(); return action; }

    if (!project.IsOpen()) {
        ImGui::TextDisabled("No project is open.");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("File > New Project... turns the scene you are editing into the first scene of a project folder. "
                               "A project keeps several scenes together, and tells the game which one to start with.");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return action;
    }

    ImGui::TextUnformatted(project.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%d %s)", static_cast<int>(project.scenes.size()), project.scenes.size() == 1 ? "scene" : "scenes");
    ImGui::Separator();

    if (project.scenes.empty()) ImGui::TextDisabled("No scenes yet. Save the open scene to add it.");

    for (const std::string& scene : project.scenes) {
        std::error_code error;
        const bool missing = !std::filesystem::exists(project.Resolve(scene), error);
        const bool isCurrent = scene == state.currentScene;
        const bool isStart = scene == project.startScene;

        std::string label = std::string(isStart ? "* " : "  ") + DisplayName(scene);
        if (isCurrent && state.dirty) label += " (unsaved)";
        if (missing) label += " (missing)";
        label += "###" + scene;

        if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
        if (ImGui::Selectable(label.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !missing && !isCurrent) {
            action = { ScenesAction::Kind::Open, scene };
        }
        if (missing) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", scene.c_str(), isStart ? "\nThis scene starts the game." : "");

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open", nullptr, false, !missing && !isCurrent)) action = { ScenesAction::Kind::Open, scene };
            if (ImGui::MenuItem("Set as start scene", nullptr, false, !isStart)) action = { ScenesAction::Kind::SetStart, scene };
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from project")) action = { ScenesAction::Kind::Remove, scene };
            ImGui::SetItemTooltip("Only takes it off the list: the file stays on disk.");
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("New scene")) action.kind = ScenesAction::Kind::NewScene;
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.canAddCurrent);
    if (ImGui::Button("Add open scene")) action.kind = ScenesAction::Kind::AddCurrent;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Scenes you save inside this project are added automatically.\nThis is for one that was saved in the folder before.");
    ImGui::TextDisabled("* = starts the game");
    ImGui::End();
    return action;
}
