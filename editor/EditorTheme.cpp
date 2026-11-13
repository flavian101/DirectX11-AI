#include "EditorTheme.h"
#include "imgui.h"
#include <filesystem>

void ApplyEditorTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // Shape: a little rounding and more air than the default, which is cramped for a tool with this many sliders.
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.SeparatorTextBorderSize = 1.0f;       // the section titles in the Inspector: a hairline, a little more air around it
    style.SeparatorTextPadding = ImVec2(16.0f, 4.0f);
    style.WindowMenuButtonPosition = ImGuiDir_None;   // no collapse arrow in every title: docked panels do not collapse

    // Colour: cool dark greys, one accent. Surfaces step up in brightness the closer they are to your hands:
    // window < field < button < hovered.
    const ImVec4 accent(0.26f, 0.59f, 0.98f, 1.0f);
    const ImVec4 accentSoft(0.26f, 0.59f, 0.98f, 0.35f);
    const ImVec4 accentHover(0.36f, 0.66f, 1.00f, 1.0f);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.91f, 0.93f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.105f, 0.110f, 0.122f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.130f, 0.135f, 0.150f, 0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.200f, 0.210f, 0.235f, 1.00f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.160f, 0.170f, 0.195f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.200f, 0.215f, 0.250f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.235f, 0.255f, 0.300f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.085f, 0.090f, 0.100f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.120f, 0.128f, 0.145f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.085f, 0.090f, 0.100f, 0.80f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.125f, 0.130f, 0.145f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.085f, 0.090f, 0.100f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.250f, 0.265f, 0.300f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.320f, 0.340f, 0.385f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;
    c[ImGuiCol_CheckMark]            = accentHover;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentHover;
    c[ImGuiCol_Button]               = ImVec4(0.200f, 0.220f, 0.265f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.260f, 0.300f, 0.370f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.230f, 0.480f, 0.820f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.190f, 0.230f, 0.300f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.235f, 0.295f, 0.385f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.260f, 0.340f, 0.450f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.210f, 0.220f, 0.250f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = accent;
    c[ImGuiCol_SeparatorActive]      = accentHover;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.26f, 0.59f, 0.98f, 0.15f);
    c[ImGuiCol_ResizeGripHovered]    = accentSoft;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_TabHovered]           = ImVec4(0.260f, 0.400f, 0.620f, 1.00f);
    // Tabs: the selected one is the brightest, with an accent line on top; the others sit back into the title bar. Without
    // these the tabs keep the base theme's blue, which is the one colour here that is not ours. (Renamed in ImGui 1.90.9.)
#if IMGUI_VERSION_NUM >= 19090
    c[ImGuiCol_Tab]                  = ImVec4(0.100f, 0.105f, 0.120f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.160f, 0.200f, 0.270f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = ImVec4(0.090f, 0.095f, 0.108f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.130f, 0.150f, 0.190f, 1.00f);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.26f, 0.59f, 0.98f, 0.45f);
#else
    c[ImGuiCol_Tab]                  = ImVec4(0.100f, 0.105f, 0.120f, 1.00f);
    c[ImGuiCol_TabActive]            = ImVec4(0.160f, 0.200f, 0.270f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.090f, 0.095f, 0.108f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.130f, 0.150f, 0.190f, 1.00f);
#endif
    c[ImGuiCol_TextSelectedBg]       = accentSoft;
    c[ImGuiCol_DragDropTarget]       = accentHover;
    c[ImGuiCol_NavHighlight]         = accent;
    c[ImGuiCol_DockingPreview]       = ImVec4(0.26f, 0.59f, 0.98f, 0.45f);
    c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.075f, 0.080f, 0.090f, 1.00f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.135f, 0.145f, 0.165f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.200f, 0.210f, 0.235f, 1.00f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.160f, 0.170f, 0.190f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);
}

void ApplyEditorFont(float dpiScale)
{
    if (dpiScale < 1.0f) dpiScale = 1.0f;
    ImGuiIO& io = ImGui::GetIO();
    // ImGui's built-in font is a 13 px bitmap-style font: legible, but it is the thing that most makes an ImGui tool look
    // like a prototype. Segoe UI ships with every Windows. The first font added becomes the default one.
    static const char* const kCandidates[] = { "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf" };
    for (const char* path : kCandidates) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) continue;
        if (io.Fonts->AddFontFromFileTTF(path, 15.0f * dpiScale)) break;
    }
    // Spacing and radii were chosen for 100%: grow them with the text, or a 150% screen gets big letters in cramped boxes.
    if (dpiScale > 1.0f) ImGui::GetStyle().ScaleAllSizes(dpiScale);
}
