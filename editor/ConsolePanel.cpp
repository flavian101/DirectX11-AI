#include "ConsolePanel.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

namespace {
constexpr size_t kMaxLines = 2000;

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

ImVec4 ColorFor(Logger::LogLevel level)
{
    switch (level) {
    case Logger::Error:   return ImVec4(1.00f, 0.45f, 0.45f, 1.0f);
    case Logger::Warning: return ImVec4(1.00f, 0.80f, 0.40f, 1.0f);
    case Logger::Debug:   return ImVec4(0.55f, 0.57f, 0.62f, 1.0f);
    default:              return ImVec4(0.88f, 0.89f, 0.91f, 1.0f);
    }
}
}

void ConsolePanel::Pull()
{
    std::vector<Logger::Entry> fresh;
    lastSeen_ = Logger::Instance().CopyNewerThan(lastSeen_, fresh);
    for (const Logger::Entry& entry : fresh) {
        Line line;
        line.level = entry.level;
        line.text = entry.time + "  " + entry.text;
        line.lowered = Lower(line.text);
        ++counts_[entry.level];
        lines_.push_back(std::move(line));
    }
    if (lines_.size() > kMaxLines) {
        for (size_t i = 0; i < lines_.size() - kMaxLines; ++i) --counts_[lines_[i].level];
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kMaxLines));
    }
}

void ConsolePanel::Draw(bool* open)
{
    if (!*open) return;
    if (!ImGui::Begin("Console", open)) { ImGui::End(); return; }
    const size_t before = lines_.size();
    Pull();

    if (ImGui::Button("Clear")) {
        lines_.clear();
        for (int& count : counts_) count = 0;
    }
    ImGui::SameLine();
    // One toggle per level, each with its count: a quick way to see that something went wrong while you were looking elsewhere.
    auto toggle = [&](const char* name, bool& flag, Logger::LogLevel level) {
        char label[48];
        std::snprintf(label, sizeof(label), "%s %d###level%d", name, counts_[level], static_cast<int>(level));
        ImGui::PushStyleColor(ImGuiCol_Text, flag ? ColorFor(level) : ImVec4(0.45f, 0.47f, 0.50f, 1.0f));
        if (ImGui::Button(label)) flag = !flag;
        ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toggle("Info", showInfo_, Logger::Info);
    toggle("Warnings", showWarnings_, Logger::Warning);
    toggle("Errors", showErrors_, Logger::Error);
    toggle("Debug", showDebug_, Logger::Debug);
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##consoleFilter", "Filter...", filter_, sizeof(filter_));
    ImGui::Separator();

    const std::string needle = Lower(filter_);
    std::vector<int> visible;
    visible.reserve(lines_.size());
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const Line& line = lines_[static_cast<size_t>(i)];
        const bool levelShown = (line.level == Logger::Info && showInfo_) || (line.level == Logger::Warning && showWarnings_) ||
                                (line.level == Logger::Error && showErrors_) || (line.level == Logger::Debug && showDebug_);
        if (levelShown && (needle.empty() || line.lowered.find(needle) != std::string::npos)) visible.push_back(i);
    }

    ImGui::BeginChild("##consoleLines", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const Line& line = lines_[static_cast<size_t>(visible[static_cast<size_t>(row)])];
            ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(line.level));
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem(("##lineMenu" + std::to_string(row)).c_str())) {
                if (ImGui::MenuItem("Copy line")) ImGui::SetClipboardText(line.text.c_str());
                ImGui::EndPopup();
            }
        }
    }
    // Follow new lines, but only while you are already at the bottom: scrolling up to read must not be undone.
    if (autoScroll_ && lines_.size() != before && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}
